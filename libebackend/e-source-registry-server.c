/*
 * e-source-registry-server.c
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 * SECTION: e-source-registry-server
 * @include: libebackend/libebackend.h
 * @short_description: Server-side repository for data sources
 *
 * The #ESourceRegistryServer is the heart of the registry D-Bus service.
 * Acting as a global singleton store for all #EServerSideSource instances,
 * its responsibilities include loading data source content from key files,
 * exporting data sources to clients over D-Bus, handling authentication
 * and content change requests from clients, and saving content changes
 * back to key files.
 *
 * It also hosts any number of built-in or 3rd party data source collection
 * backends, which coordinate with #ESourceRegistryServer to automatically
 * advertise available data sources on a remote server.
 **/

#include "e-source-registry-server.h"

#include <config.h>
#include <string.h>
#include <glib/gi18n-lib.h>

/* Private D-Bus classes. */
#include <e-dbus-source.h>
#include <e-dbus-source-manager.h>

#include <libebackend/e-authentication-mediator.h>
#include <libebackend/e-authentication-session.h>
#include <libebackend/e-server-side-source.h>

#define E_SOURCE_REGISTRY_SERVER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_REGISTRY_SERVER, ESourceRegistryServerPrivate))

/* Collection backends get tacked on to
 * sources with a [Collection] extension. */
#define BACKEND_DATA_KEY "__e_collection_backend__"

typedef struct _AuthRequest AuthRequest;

struct _ESourceRegistryServerPrivate {
	GMainContext *main_context;

	GDBusObjectManagerServer *object_manager;
	EDBusSourceManager *source_manager;

	GHashTable *sources;  /* sources added to hierarchy */
	GHashTable *orphans;  /* sources waiting for parent */
	GHashTable *monitors;

	GMutex sources_lock;
	GMutex orphans_lock;

	/* In pseudo-Python notation:
	 *
	 * running_auths = { UID : AuthRequest }
	 * waiting_auths = { UID : [ AuthRequest, ... ] }
	 *
	 * We process all authenticators for a given source UID at once.
	 * The thought being after the first authenticator for a given UID
	 * completes (the first being most likely to trigger a user prompt),
	 * then any other authenticators for that same UID should complete
	 * quickly, hopefully without having to reprompt.  That is unless
	 * the user decides not to cache the secret at all, in which case
	 * he gets what he asked for: lots of annoying prompts.
	 */
	GMutex auth_lock;
	GHashTable *running_auths;
	GHashTable *waiting_auths;

	guint authentication_count;
};

struct _AuthRequest {
	volatile gint ref_count;
	EAuthenticationSession *session;
	GSimpleAsyncResult *simple;
	ESource *source;  /* may be NULL */
	GCancellable *cancellable;
};

enum {
	LOAD_ERROR,
	FILES_LOADED,
	SOURCE_ADDED,
	SOURCE_REMOVED,
	TWEAK_KEY_FILE,
	LAST_SIGNAL
};

/* Forward Declarations */
static void	source_registry_server_maybe_start_auth_session
						(ESourceRegistryServer *server,
						 const gchar *uid);

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (
	ESourceRegistryServer,
	e_source_registry_server,
	E_TYPE_DATA_FACTORY)

static AuthRequest *
auth_request_new (EAuthenticationSession *session,
                  GSimpleAsyncResult *simple,
                  GCancellable *cancellable)
{
	ESourceRegistryServer *server;
	AuthRequest *request;
	const gchar *uid;

	server = e_authentication_session_get_server (session);
	uid = e_authentication_session_get_source_uid (session);

	request = g_slice_new0 (AuthRequest);
	request->ref_count = 1;
	request->session = g_object_ref (session);
	request->simple = g_object_ref (simple);

	/* This will return NULL if the authenticating data source
	 * has not yet been submitted to the D-Bus registry server. */
	request->source = e_source_registry_server_ref_source (server, uid);

	if (G_IS_CANCELLABLE (cancellable))
		request->cancellable = g_object_ref (cancellable);

	return request;
}

static AuthRequest *
auth_request_ref (AuthRequest *request)
{
	g_return_val_if_fail (request != NULL, NULL);
	g_return_val_if_fail (request->ref_count > 0, NULL);

	g_atomic_int_inc (&request->ref_count);

	return request;
}

static void
auth_request_unref (AuthRequest *request)
{
	g_return_if_fail (request != NULL);
	g_return_if_fail (request->ref_count > 0);

	if (g_atomic_int_dec_and_test (&request->ref_count)) {

		g_object_unref (request->session);
		g_object_unref (request->simple);

		if (request->source != NULL)
			g_object_unref (request->source);

		if (request->cancellable != NULL)
			g_object_unref (request->cancellable);

		g_slice_free (AuthRequest, request);
	}
}

/* GDestroyNotify callback for 'sources' values */
static void
unref_data_source (ESource *source)
{
	/* The breaks the reference cycle with ECollectionBackend. */
	g_object_set_data (G_OBJECT (source), BACKEND_DATA_KEY, NULL);
	g_object_unref (source);
}

/* GDestroyNotify callback for 'waiting_auths' values */
static void
free_auth_queue (GQueue *queue)
{
	/* XXX g_queue_clear_full() would be nice here. */
	while (!g_queue_is_empty (queue))
		g_object_unref (g_queue_pop_head (queue));

	g_queue_free (queue);
}

static void
source_registry_server_sources_insert (ESourceRegistryServer *server,
                                       ESource *source)
{
	const gchar *uid;

	uid = e_source_get_uid (source);
	g_return_if_fail (uid != NULL);

	g_mutex_lock (&server->priv->sources_lock);

	g_hash_table_insert (
		server->priv->sources,
		g_strdup (uid), g_object_ref (source));

	g_mutex_unlock (&server->priv->sources_lock);
}

static gboolean
source_registry_server_sources_remove (ESourceRegistryServer *server,
                                       ESource *source)
{
	const gchar *uid;
	gboolean removed;

	uid = e_source_get_uid (source);
	g_return_val_if_fail (uid != NULL, FALSE);

	g_mutex_lock (&server->priv->sources_lock);

	removed = g_hash_table_remove (server->priv->sources, uid);

	g_mutex_unlock (&server->priv->sources_lock);

	return removed;
}

static ESource *
source_registry_server_sources_lookup (ESourceRegistryServer *server,
                                       const gchar *uid)
{
	ESource *source;

	g_return_val_if_fail (uid != NULL, NULL);

	g_mutex_lock (&server->priv->sources_lock);

	source = g_hash_table_lookup (server->priv->sources, uid);

	if (source != NULL)
		g_object_ref (source);

	g_mutex_unlock (&server->priv->sources_lock);

	return source;
}

static GList *
source_registry_server_sources_get_values (ESourceRegistryServer *server)
{
	GList *values;

	g_mutex_lock (&server->priv->sources_lock);

	values = g_hash_table_get_values (server->priv->sources);

	g_list_foreach (values, (GFunc) g_object_ref, NULL);

	g_mutex_unlock (&server->priv->sources_lock);

	return values;
}

static void
source_registry_server_orphans_insert (ESourceRegistryServer *server,
                                       ESource *orphan_source)
{
	GHashTable *orphans;
	GPtrArray *array;
	gchar *parent_uid;

	g_mutex_lock (&server->priv->orphans_lock);

	orphans = server->priv->orphans;

	parent_uid = e_source_dup_parent (orphan_source);

	/* A top-level object has no parent UID, so we
	 * use a special "empty" key in the hash table. */
	if (parent_uid == NULL)
		parent_uid = g_strdup ("");

	array = g_hash_table_lookup (orphans, parent_uid);

	if (array == NULL) {
		array = g_ptr_array_new_with_free_func (g_object_unref);

		/* Takes ownership of the 'parent_uid' string. */
		g_hash_table_insert (orphans, parent_uid, array);
		parent_uid = NULL;
	}

	g_ptr_array_add (array, g_object_ref (orphan_source));

	g_free (parent_uid);

	g_mutex_unlock (&server->priv->orphans_lock);
}

static gboolean
source_registry_server_orphans_remove (ESourceRegistryServer *server,
                                       ESource *orphan_source)
{
	GHashTable *orphans;
	GPtrArray *array;
	gchar *parent_uid;
	gboolean removed = FALSE;

	g_mutex_lock (&server->priv->orphans_lock);

	orphans = server->priv->orphans;

	parent_uid = e_source_dup_parent (orphan_source);

	/* A top-level object has no parent UID, so we
	 * use a special "empty" key in the hash table. */
	if (parent_uid == NULL)
		parent_uid = g_strdup ("");

	array = g_hash_table_lookup (orphans, parent_uid);

	if (array != NULL) {
		/* Array is not ordered, so use "remove_fast". */
		removed = g_ptr_array_remove_fast (array, orphan_source);
	}

	g_free (parent_uid);

	g_mutex_unlock (&server->priv->orphans_lock);

	return removed;
}

static GPtrArray *
source_registry_server_orphans_steal (ESourceRegistryServer *server,
                                      ESource *parent_source)
{
	GHashTable *orphans;
	GPtrArray *array;
	const gchar *parent_uid;

	parent_uid = e_source_get_uid (parent_source);
	g_return_val_if_fail (parent_uid != NULL, NULL);

	g_mutex_lock (&server->priv->orphans_lock);

	orphans = server->priv->orphans;

	array = g_hash_table_lookup (orphans, parent_uid);

	/* g_hash_table_remove() will unreference the array,
	 * so we need to reference it first to keep it alive. */
	if (array != NULL) {
		g_ptr_array_ref (array);
		g_hash_table_remove (orphans, parent_uid);
	}

	g_mutex_unlock (&server->priv->orphans_lock);

	return array;
}

static void
source_request_server_auth_request_cancel_all (ESourceRegistryServer *server)
{
	GHashTableIter iter;
	gpointer value;

	g_mutex_lock (&server->priv->auth_lock);

	g_hash_table_iter_init (&iter, server->priv->waiting_auths);

	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		GQueue *queue = value;
		GList *list, *link;

		list = g_queue_peek_head_link (queue);

		for (link = list; link != NULL; link = g_list_next (link)) {
			AuthRequest *request = link->data;
			g_cancellable_cancel (request->cancellable);
		}
	}

	g_hash_table_iter_init (&iter, server->priv->running_auths);

	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		AuthRequest *request = value;
		g_cancellable_cancel (request->cancellable);
	}

	g_mutex_unlock (&server->priv->auth_lock);
}

static void
source_registry_server_auth_request_push (ESourceRegistryServer *server,
                                          const gchar *uid,
                                          AuthRequest *request)
{
	GQueue *queue;

	g_return_if_fail (uid != NULL);

	g_mutex_lock (&server->priv->auth_lock);

	queue = g_hash_table_lookup (server->priv->waiting_auths, uid);

	if (queue == NULL) {
		queue = g_queue_new ();
		g_hash_table_insert (
			server->priv->waiting_auths,
			g_strdup (uid), queue);
	}

	g_queue_push_tail (queue, auth_request_ref (request));

	g_mutex_unlock (&server->priv->auth_lock);
}

static AuthRequest *
source_registry_server_auth_request_next (ESourceRegistryServer *server,
                                          const gchar *uid)
{
	AuthRequest *request = NULL;

	g_return_val_if_fail (uid != NULL, NULL);

	g_mutex_lock (&server->priv->auth_lock);

	/* If we're already busy processing an authentication request
	 * for this UID, the next request will have to wait in line. */
	if (!g_hash_table_contains (server->priv->running_auths, uid)) {
		GQueue *queue;

		queue = g_hash_table_lookup (
			server->priv->waiting_auths, uid);

		if (queue != NULL)
			request = g_queue_pop_head (queue);

		if (request != NULL)
			g_hash_table_insert (
				server->priv->running_auths,
				g_strdup (uid),
				auth_request_ref (request));
	}

	g_mutex_unlock (&server->priv->auth_lock);

	return request;
}

static void
source_registry_server_auth_request_done (ESourceRegistryServer *server,
                                          const gchar *uid)
{
	g_return_if_fail (uid != NULL);

	g_mutex_lock (&server->priv->auth_lock);

	g_hash_table_remove (server->priv->running_auths, uid);

	g_mutex_unlock (&server->priv->auth_lock);
}

static void
source_registry_server_auth_session_cb (GObject *source_object,
                                        GAsyncResult *result,
                                        gpointer user_data)
{
	ESourceRegistryServer *server;
	EAuthenticationSession *session;
	EAuthenticationSessionResult auth_result;
	AuthRequest *request;
	const gchar *uid;
	GError *error = NULL;

	session = E_AUTHENTICATION_SESSION (source_object);
	request = (AuthRequest *) user_data;

	auth_result = e_authentication_session_execute_finish (
		session, result, &error);

	if (error != NULL) {
		ESourceAuthenticator *authenticator;

		g_warn_if_fail (auth_result == E_AUTHENTICATION_SESSION_ERROR);
		g_simple_async_result_take_error (request->simple, error);

		/* If the authenticator is an EAuthenticationMediator,
		 * have it emit a "server-error" signal to the client. */
		authenticator =
			e_authentication_session_get_authenticator (session);
		if (E_IS_AUTHENTICATION_MEDIATOR (authenticator))
			e_authentication_mediator_server_error (
				E_AUTHENTICATION_MEDIATOR (authenticator),
				error);
	}

	/* Authentication dismissals require additional handling. */
	if (auth_result == E_AUTHENTICATION_SESSION_DISMISSED) {
		ESourceAuthenticator *authenticator;

		/* If the authenticator is an EAuthenticationMediator,
		 * have it emit a "dismissed" signal to the client. */
		authenticator =
			e_authentication_session_get_authenticator (session);
		if (E_IS_AUTHENTICATION_MEDIATOR (authenticator))
			e_authentication_mediator_dismiss (
				E_AUTHENTICATION_MEDIATOR (authenticator));

		/* Prevent further user interruptions. */
		if (request->source != NULL)
			e_server_side_source_set_allow_auth_prompt (
				E_SERVER_SIDE_SOURCE (request->source), FALSE);

		/* e_source_registry_server_authenticate_finish() should
		 * return an error since authentication did not complete. */
		g_simple_async_result_set_error (
			request->simple,
			G_IO_ERROR, G_IO_ERROR_CANCELLED,
			_("The user declined to authenticate"));
	}

	g_simple_async_result_complete_in_idle (request->simple);

	server = e_authentication_session_get_server (session);
	uid = e_authentication_session_get_source_uid (session);

	source_registry_server_auth_request_done (server, uid);
	source_registry_server_maybe_start_auth_session (server, uid);

	auth_request_unref (request);
}

static gboolean
source_registry_server_start_auth_session_idle_cb (gpointer user_data)
{
	AuthRequest *request = user_data;

	/* Execute the new active auth session.  This signals it to
	 * respond with a cached secret in the keyring if it can, or
	 * else show an authentication prompt and wait for input. */
	e_authentication_session_execute (
		request->session,
		G_PRIORITY_DEFAULT,
		request->cancellable,
		source_registry_server_auth_session_cb,
		auth_request_ref (request));

	return FALSE;
}

static void
source_registry_server_maybe_start_auth_session (ESourceRegistryServer *server,
                                                 const gchar *uid)
{
	AuthRequest *request;

	/* We own the returned reference, unless we get NULL. */
	request = source_registry_server_auth_request_next (server, uid);

	if (request != NULL) {
		GSource *idle_source;

		/* Process the next AuthRequest from an idle callback
		 * on our own GMainContext, since the current thread-
		 * default GMainContext may only be temporary. */
		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			source_registry_server_start_auth_session_idle_cb,
			auth_request_ref (request),
			(GDestroyNotify) auth_request_unref);
		g_source_attach (idle_source, server->priv->main_context);
		g_source_unref (idle_source);

		auth_request_unref (request);
	}
}

static void
source_registry_server_authenticate_done_cb (GObject *source_object,
                                             GAsyncResult *result,
                                             gpointer user_data)
{
	GError *error = NULL;

	e_source_registry_server_authenticate_finish (
		E_SOURCE_REGISTRY_SERVER (source_object), result, &error);

	/* Ignore cancellations. */
	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_error_free (error);

	} else if (error != NULL) {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_error_free (error);
	}
}

static void
source_registry_server_wait_for_client_cb (GObject *source_object,
                                           GAsyncResult *result,
                                           gpointer user_data)
{
	EAuthenticationMediator *mediator;
	EAuthenticationSession *session;
	GError *error = NULL;

	mediator = E_AUTHENTICATION_MEDIATOR (source_object);
	session = E_AUTHENTICATION_SESSION (user_data);

	e_authentication_mediator_wait_for_client_finish (
		mediator, result, &error);

	if (error == NULL) {
		ESourceRegistryServer *server;

		server = e_authentication_session_get_server (session);

		/* Client is ready and waiting to test passwords, so
		 * execute the authentication session as soon as all
		 * other authentication sessions for this same data
		 * source are finished.
		 *
		 * XXX Note this asynchronous operation is not cancellable
		 *     but it does time out on its own after a few minutes. */
		e_source_registry_server_authenticate (
			server, session, NULL,
			source_registry_server_authenticate_done_cb, NULL);

	} else {
		/* Most likely the client went dark and the operation
		 * timed out.  Emit a dismissed signal anyway just in
		 * case the client is still alive and listening. */
		e_authentication_mediator_dismiss (mediator);
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_error_free (error);
	}

	g_object_unref (session);
}

static gboolean
source_registry_server_allow_auth_prompt_all_cb (EDBusSourceManager *dbus_interface,
                                                 GDBusMethodInvocation *invocation,
                                                 ESourceRegistryServer *server)
{
	GList *list, *link;

	list = e_source_registry_server_list_sources (server, NULL);

	for (link = list; link != NULL; link = g_list_next (link))
		e_server_side_source_set_allow_auth_prompt (
			E_SERVER_SIDE_SOURCE (link->data), TRUE);

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	e_dbus_source_manager_complete_allow_auth_prompt_all (
		dbus_interface, invocation);

	return TRUE;
}

static gboolean
source_registry_server_authenticate_cb (EDBusSourceManager *dbus_interface,
                                        GDBusMethodInvocation *invocation,
                                        const gchar *source_uid,
                                        const gchar *prompt_title,
                                        const gchar *prompt_message,
                                        const gchar *prompt_description,
                                        ESourceRegistryServer *server)
{
	GDBusConnection *connection;
	EAuthenticationSession *session;
	ESourceAuthenticator *authenticator;
	const gchar *base_object_path;
	const gchar *sender;
	gchar *auth_object_path;
	GError *error = NULL;

	/* Export the D-Bus interface to a unique object path.  This
	 * effectively starts a new authentication session with the
	 * method caller. */

	base_object_path = E_SOURCE_REGISTRY_SERVER_OBJECT_PATH;
	connection = g_dbus_method_invocation_get_connection (invocation);
	sender = g_dbus_method_invocation_get_sender (invocation);

	auth_object_path = g_strdup_printf (
		"%s/auth_%u", base_object_path,
		server->priv->authentication_count++);

	authenticator = e_authentication_mediator_new (
		connection, auth_object_path, sender, &error);

	if (error != NULL) {
		g_warn_if_fail (authenticator == NULL);
		g_dbus_method_invocation_take_error (invocation, error);
		g_free (auth_object_path);
		return TRUE;
	}

	g_return_val_if_fail (
		E_IS_SOURCE_AUTHENTICATOR (authenticator), FALSE);

	/* Create the authentication session. */
	session = e_source_registry_server_new_auth_session (
		server, authenticator, source_uid);

	/* Configure the authentication session. */
	g_object_set (
		session,
		"prompt-title", prompt_title,
		"prompt-message", prompt_message,
		"prompt-description", prompt_description,
		NULL);

	/* Before adding the authentication session to the server we
	 * must handshake with the client requesting authentication.
	 * We do this by returning the object path of the exported
	 * Authenticator interface and then waiting for the client to
	 * acknowledge by calling the Ready() method on the interface.
	 * This indicates the client is ready to receive signals.
	 *
	 * XXX Note this asynchronous operation is not cancellable
	 *     but it does time out on its own after a few minutes. */
	e_authentication_mediator_wait_for_client (
		E_AUTHENTICATION_MEDIATOR (authenticator),
		NULL, source_registry_server_wait_for_client_cb,
		g_object_ref (session));

	e_dbus_source_manager_complete_authenticate (
		dbus_interface, invocation, auth_object_path);

	g_object_unref (authenticator);
	g_object_unref (session);
	g_free (auth_object_path);

	return TRUE;
}

static gboolean
source_registry_server_create_source (ESourceRegistryServer *server,
                                      const gchar *uid,
                                      const gchar *data,
                                      GError **error)
{
	ESource *source = NULL;
	GFile *file;
	GFile *parent;
	GKeyFile *key_file;
	gboolean success;
	gsize length;
	GError *local_error = NULL;

	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	length = strlen (data);

	/* Make sure the data is syntactically valid. */
	key_file = g_key_file_new ();
	success = g_key_file_load_from_data (
		key_file, data, length, G_KEY_FILE_NONE, error);
	g_key_file_free (key_file);

	if (!success)
		return FALSE;

	/* Check that the given unique identifier really is unique.
	 *
	 * XXX There's a valid case to be made that the server should be
	 *     assigning unique identifiers to new sources to avoid this
	 *     error.  That's fine for standalone sources but makes life
	 *     more difficult for clients creating a set or hierarchy of
	 *     sources that cross reference one another, such for a mail
	 *     account.  Having CLIENTS generate new UIDs means they can
	 *     prepare any cross references in advance, then submit each
	 *     source as is without having to make further modifications
	 *     as would be necessary if using server-assigned UIDs.
	 *
	 *     Anyway, if used properly the odds of a UID collision here
	 *     are slim enough that I think it's a reasonable trade-off.
	 */
	source = e_source_registry_server_ref_source (server, uid);
	if (source != NULL) {
		g_set_error (
			error, G_IO_ERROR, G_IO_ERROR_EXISTS,
			_("UID '%s' is already in use"), uid);
		g_object_unref (source);
		return FALSE;
	}

	file = e_server_side_source_new_user_file (uid);

	/* Create the directory where we'll be writing. */

	parent = g_file_get_parent (file);
	g_file_make_directory_with_parents (parent, NULL, &local_error);
	g_object_unref (parent);

	if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
		g_clear_error (&local_error);

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		success = FALSE;
	}

	/* Write the data to disk.  The file monitor should eventually
	 * notice the new file and call e_source_registry_server_load_file()
	 * per design, but we're going to beat it to the punch since we
	 * need to return the new D-Bus object path back to the caller.
	 * By the time the file monitor gets around to loading the file,
	 * it will simply get back the EDBusSourceObject we've already
	 * created and exported. */

	if (success)
		success = g_file_replace_contents (
			file, data, length, NULL, FALSE,
			G_FILE_CREATE_PRIVATE, NULL, NULL, error);

	if (success) {
		ESourcePermissionFlags flags;

		/* New sources are always writable + removable. */
		flags = E_SOURCE_PERMISSION_WRITABLE |
			E_SOURCE_PERMISSION_REMOVABLE;

		source = e_source_registry_server_load_file (
			server, file, flags, error);

		/* We don't need the returned reference. */
		if (source != NULL)
			g_object_unref (source);
		else
			success = FALSE;
	}

	g_object_unref (file);

	return success;
}

static gboolean
source_registry_server_create_sources_cb (EDBusSourceManager *dbus_interface,
                                          GDBusMethodInvocation *invocation,
                                          GVariant *array,
                                          ESourceRegistryServer *server)
{
	GVariantIter iter;
	gchar *uid, *data;
	GError *error = NULL;

	g_variant_iter_init (&iter, array);

	while (g_variant_iter_next (&iter, "{ss}", &uid, &data)) {
		source_registry_server_create_source (
			server, uid, data, &error);

		g_free (uid);
		g_free (data);

		if (error != NULL)
			break;
	}

	if (error != NULL)
		g_dbus_method_invocation_take_error (invocation, error);
	else
		e_dbus_source_manager_complete_create_sources (
			dbus_interface, invocation);

	return TRUE;
}

static gboolean
source_registry_server_reload_cb (EDBusSourceManager *dbus_interface,
                                  GDBusMethodInvocation *invocation,
                                  ESourceRegistryServer *server)
{
	e_dbus_server_quit (
		E_DBUS_SERVER (server),
		E_DBUS_SERVER_EXIT_RELOAD);

	e_dbus_source_manager_complete_reload (dbus_interface, invocation);

	return TRUE;
}

static void
source_registry_server_monitor_changed_cb (GFileMonitor *monitor,
                                           GFile *file,
                                           GFile *other_file,
                                           GFileMonitorEvent event_type,
                                           ESourceRegistryServer *server)
{
	if (event_type == G_FILE_MONITOR_EVENT_CREATED) {
		ESource *source;
		GError *error = NULL;

		/* it can return NULL source for hidden files */
		source = e_server_side_source_new (server, file, &error);

		if (!error && source) {
			/* File monitors are only placed on directories
			 * where data sources are writable and removable,
			 * so it should be safe to assume these flags. */
			e_server_side_source_set_writable (
				E_SERVER_SIDE_SOURCE (source), TRUE);
			e_server_side_source_set_removable (
				E_SERVER_SIDE_SOURCE (source), TRUE);

			e_source_registry_server_add_source (server, source);
			g_object_unref (source);
		} else if (error) {
			e_source_registry_server_load_error (
				server, file, error);
			g_error_free (error);
		}
	}

	if (event_type == G_FILE_MONITOR_EVENT_DELETED) {
		ESource *source;
		gchar *uid;

		uid = e_server_side_source_uid_from_file (file, NULL);

		if (uid == NULL)
			return;

		source = e_source_registry_server_ref_source (server, uid);

		g_free (uid);

		if (source == NULL)
			return;

		/* If the key file for a non-removable source was
		 * somehow deleted, disregard the event and leave
		 * the source object in memory. */
		if (e_source_get_removable (source))
			e_source_registry_server_remove_source (server, source);

		g_object_unref (source);
	}
}

static gboolean
source_registry_server_traverse_cb (GNode *node,
                                    GQueue *queue)
{
	g_queue_push_tail (queue, g_object_ref (node->data));

	return FALSE;
}

static void
source_registry_server_queue_subtree (ESource *source,
                                      GQueue *queue)
{
	GNode *node;

	node = e_server_side_source_get_node (E_SERVER_SIDE_SOURCE (source));

	g_node_traverse (
		node, G_POST_ORDER, G_TRAVERSE_ALL, -1,
		(GNodeTraverseFunc) source_registry_server_traverse_cb, queue);
}

static gboolean
source_registry_server_find_parent (ESourceRegistryServer *server,
                                    ESource *source)
{
	ESource *parent;
	const gchar *parent_uid;

	/* If the given source references a parent source and the
	 * parent source is not present in the hierarchy, the given
	 * source is added to an orphan table until the referenced
	 * parent is added to the hierarchy. */

	parent_uid = e_source_get_parent (source);

	if (parent_uid == NULL || *parent_uid == '\0')
		return TRUE;

	parent = g_hash_table_lookup (server->priv->sources, parent_uid);

	if (parent != NULL) {
		GNode *parent_node;
		GNode *object_node;

		parent_node = e_server_side_source_get_node (
			E_SERVER_SIDE_SOURCE (parent));
		object_node = e_server_side_source_get_node (
			E_SERVER_SIDE_SOURCE (source));
		g_node_append (parent_node, object_node);

		return TRUE;
	}

	source_registry_server_orphans_insert (server, source);

	return FALSE;
}

static void
source_registry_server_adopt_orphans (ESourceRegistryServer *server,
                                      ESource *source)
{
	GPtrArray *array;

	/* Check if a newly-added source has any orphan sources
	 * that are waiting for it.  The orphans can now be added
	 * to the hierarchy as children of the newly-added source. */

	array = source_registry_server_orphans_steal (server, source);

	if (array != NULL) {
		guint ii;

		for (ii = 0; ii < array->len; ii++) {
			ESource *orphan = array->pdata[ii];
			e_source_registry_server_add_source (server, orphan);
		}

		g_ptr_array_unref (array);
	}
}

static void
source_registry_server_dispose (GObject *object)
{
	ESourceRegistryServerPrivate *priv;

	priv = E_SOURCE_REGISTRY_SERVER_GET_PRIVATE (object);

	if (priv->main_context != NULL) {
		g_main_context_unref (priv->main_context);
		priv->main_context = NULL;
	}

	if (priv->object_manager != NULL) {
		g_object_unref (priv->object_manager);
		priv->object_manager = NULL;
	}

	if (priv->source_manager != NULL) {
		g_object_unref (priv->source_manager);
		priv->source_manager = NULL;
	}

	g_hash_table_remove_all (priv->sources);
	g_hash_table_remove_all (priv->orphans);
	g_hash_table_remove_all (priv->monitors);

	g_hash_table_remove_all (priv->running_auths);
	g_hash_table_remove_all (priv->waiting_auths);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_source_registry_server_parent_class)->
		dispose (object);
}

static void
source_registry_server_finalize (GObject *object)
{
	ESourceRegistryServerPrivate *priv;

	priv = E_SOURCE_REGISTRY_SERVER_GET_PRIVATE (object);

	g_hash_table_destroy (priv->sources);
	g_hash_table_destroy (priv->orphans);
	g_hash_table_destroy (priv->monitors);

	g_mutex_clear (&priv->sources_lock);
	g_mutex_clear (&priv->orphans_lock);

	g_mutex_clear (&priv->auth_lock);
	g_hash_table_destroy (priv->running_auths);
	g_hash_table_destroy (priv->waiting_auths);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_registry_server_parent_class)->
		finalize (object);
}

static void
source_registry_server_bus_acquired (EDBusServer *server,
                                     GDBusConnection *connection)
{
	ESourceRegistryServerPrivate *priv;
	GError *error = NULL;

	priv = E_SOURCE_REGISTRY_SERVER_GET_PRIVATE (server);

	g_dbus_object_manager_server_set_connection (
		priv->object_manager, connection);

	g_dbus_interface_skeleton_export (
		G_DBUS_INTERFACE_SKELETON (priv->source_manager),
		connection, E_SOURCE_REGISTRY_SERVER_OBJECT_PATH, &error);

	/* Terminate the server if we can't export the interface. */
	if (error != NULL) {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		e_dbus_server_quit (server, E_DBUS_SERVER_EXIT_NORMAL);
		g_error_free (error);
	}

	/* Chain up to parent's bus_acquired() method. */
	E_DBUS_SERVER_CLASS (e_source_registry_server_parent_class)->
		bus_acquired (server, connection);
}

static void
source_registry_server_quit_server (EDBusServer *server,
                                    EDBusServerExitCode code)
{
	ESourceRegistryServerPrivate *priv;

	priv = E_SOURCE_REGISTRY_SERVER_GET_PRIVATE (server);

	source_request_server_auth_request_cancel_all (
		E_SOURCE_REGISTRY_SERVER (server));

	/* This makes the object manager unexport all objects. */
	g_dbus_object_manager_server_set_connection (
		priv->object_manager, NULL);

	g_dbus_interface_skeleton_unexport (
		G_DBUS_INTERFACE_SKELETON (priv->source_manager));

	/* Chain up to parent's quit_server() method. */
	E_DBUS_SERVER_CLASS (e_source_registry_server_parent_class)->
		quit_server (server, code);
}

static void
source_registry_server_source_added (ESourceRegistryServer *server,
                                     ESource *source)
{
	GDBusObject *dbus_object;
	GDBusObject *g_dbus_object;
	const gchar *uid;
	const gchar *object_name;
	const gchar *object_path;
	const gchar *extension_name;

	/* Instantiate an ECollectionBackend if appropriate.
	 *
	 * Do this BEFORE exporting so backends have a chance
	 * to make any last-minute tweaks to the data source. */

	extension_name = E_SOURCE_EXTENSION_COLLECTION;
	if (e_source_has_extension (source, extension_name)) {
		EBackend *backend;
		ESourceBackend *extension;
		const gchar *backend_name;

		extension = e_source_get_extension (source, extension_name);
		backend_name = e_source_backend_get_backend_name (extension);

		/* For convenience, we attach the EBackend to the ESource
		 * itself, which creates a reference cycle.  The cycle is
		 * explicitly broken when the ESource is removed from the
		 * 'sources' hash table (see unref_data_source() above). */
		backend = e_data_factory_ref_backend (
			E_DATA_FACTORY (server), backend_name, source);
		if (backend != NULL) {
			g_object_set_data_full (
				G_OBJECT (source),
				BACKEND_DATA_KEY, backend,
				(GDestroyNotify) g_object_unref);
		} else {
			g_warning (
				"No collection backend '%s' for %s",
				backend_name, e_source_get_uid (source));
		}
	}

	/* Export the data source to clients over D-Bus. */

	dbus_object = e_source_ref_dbus_object (source);

	g_dbus_object_manager_server_export_uniquely (
		server->priv->object_manager,
		G_DBUS_OBJECT_SKELETON (dbus_object));

	g_object_notify (G_OBJECT (source), "exported");

	uid = e_source_get_uid (source);

	g_dbus_object = G_DBUS_OBJECT (dbus_object);
	object_path = g_dbus_object_get_object_path (g_dbus_object);
	object_name = strrchr (object_path, '/') + 1;

	g_debug ("Adding %s ('%s')\n", uid, object_name);

	g_object_unref (dbus_object);
}

static void
source_registry_server_source_removed (ESourceRegistryServer *server,
                                       ESource *source)
{
	GDBusObject *dbus_object;
	const gchar *uid;
	const gchar *object_name;
	const gchar *object_path;

	uid = e_source_get_uid (source);

	dbus_object = e_source_ref_dbus_object (source);

	object_path = g_dbus_object_get_object_path (dbus_object);
	object_name = strrchr (object_path, '/') + 1;

	e_source_registry_debug_print ("Removing %s ('%s')\n", uid, object_name);

	g_dbus_object_manager_server_unexport (
		server->priv->object_manager, object_path);

	g_object_notify (G_OBJECT (source), "exported");

	g_object_unref (dbus_object);
}

static gboolean
source_registry_server_any_true (GSignalInvocationHint *ihint,
                                 GValue *return_accu,
                                 const GValue *handler_return,
                                 gpointer unused)
{
	if (g_value_get_boolean (handler_return))
		g_value_set_boolean (return_accu, TRUE);

	return TRUE;
}

static void
e_source_registry_server_class_init (ESourceRegistryServerClass *class)
{
	GObjectClass *object_class;
	EDBusServerClass *dbus_server_class;
	EDataFactoryClass *data_factory_class;
	GType backend_factory_type;
	const gchar *modules_directory = MODULE_DIRECTORY;
	const gchar *modules_directory_env;

	modules_directory_env = g_getenv (EDS_REGISTRY_MODULES);
	if (modules_directory_env &&
	    g_file_test (modules_directory_env, G_FILE_TEST_IS_DIR))
		modules_directory = g_strdup (modules_directory_env);

	g_type_class_add_private (class, sizeof (ESourceRegistryServerPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = source_registry_server_dispose;
	object_class->finalize = source_registry_server_finalize;

	dbus_server_class = E_DBUS_SERVER_CLASS (class);
	dbus_server_class->bus_name = SOURCES_DBUS_SERVICE_NAME;
	dbus_server_class->module_directory = modules_directory;
	dbus_server_class->bus_acquired = source_registry_server_bus_acquired;
	dbus_server_class->quit_server = source_registry_server_quit_server;

	data_factory_class = E_DATA_FACTORY_CLASS (class);
	backend_factory_type = E_TYPE_COLLECTION_BACKEND_FACTORY;
	data_factory_class->backend_factory_type = backend_factory_type;

	class->source_added = source_registry_server_source_added;
	class->source_removed = source_registry_server_source_removed;

	/**
	 * ESourceRegistryServer::load-error:
	 * @server: the #ESourceRegistryServer which emitted the signal
	 * @file: the #GFile being loaded
	 * @error: a #GError describing the error
	 *
	 * Emitted when an error occurs while loading or parsing a
	 * data source key file.
	 **/
	signals[LOAD_ERROR] = g_signal_new (
		"load-error",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceRegistryServerClass, load_error),
		NULL, NULL, NULL,
		G_TYPE_NONE, 2,
		G_TYPE_FILE,
		G_TYPE_ERROR | G_SIGNAL_TYPE_STATIC_SCOPE);

	/**
	 * ESourceRegistryServer::files-loaded:
	 * @server: the #ESourceRegistryServer which emitted the signal
	 *
	 * Emitted after all data source key files are loaded on startup.
	 * Extensions can connect to this signal to perform any additional
	 * work prior to running the main loop.
	 **/
	signals[FILES_LOADED] = g_signal_new (
		"files-loaded",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceRegistryServerClass, files_loaded),
		NULL, NULL, NULL,
		G_TYPE_NONE, 0);

	/**
	 * ESourceRegistryServer::source-added:
	 * @server: the #ESourceRegistryServer which emitted the signal
	 * @source: the newly-added #EServerSideSource
	 *
	 * Emitted when an #EServerSideSource is added to @server.
	 **/
	signals[SOURCE_ADDED] = g_signal_new (
		"source-added",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceRegistryServerClass, source_added),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		E_TYPE_SERVER_SIDE_SOURCE);

	/**
	 * ESourceRegistryServer::source-removed:
	 * @server: the #ESourceRegistryServer which emitted the signal
	 * @source: the #EServerSideSource that got removed
	 *
	 * Emitted when an #EServerSideSource is removed from @server.
	 **/
	signals[SOURCE_REMOVED] = g_signal_new (
		"source-removed",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceRegistryServerClass, source_removed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		E_TYPE_SERVER_SIDE_SOURCE);

	/**
	 * ESourceRegistryServer::tweak-key-file:
	 * @server: the #ESourceRegistryServer which emitted the signal
	 * @key_file: a #GKeyFile
	 * @uid: a unique identifier string for @key_file
	 *
	 * Emitted from e_source_registry_server_load_file() just prior
	 * to instantiating an #EServerSideSource.  Signal handlers can
	 * tweak the @key_file content as necessary and return %TRUE to
	 * write the modified content back to disk.
	 *
	 * For the purposes of tweaking, it's easier to deal with a plain
	 * #GKeyFile than an #ESource instance.  An #ESource, for example,
	 * does not allow key file groups to be removed.
	 *
	 * The return value is cumulative.  If any signal handler returns
	 * %TRUE, the @key_file content is written back to disk.
	 *
	 * Returns: %TRUE if @key_file was modified, %FALSE otherwise
	 *
	 * Since: 3.8
	 **/
	signals[TWEAK_KEY_FILE] = g_signal_new (
		"tweak-key-file",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceRegistryServerClass, tweak_key_file),
		source_registry_server_any_true, NULL,
		NULL,
		G_TYPE_BOOLEAN, 2,
		G_TYPE_KEY_FILE,
		G_TYPE_STRING);
}

static void
e_source_registry_server_init (ESourceRegistryServer *server)
{
	GDBusObjectManagerServer *object_manager;
	EDBusSourceManager *source_manager;
	GHashTable *sources;
	GHashTable *orphans;
	GHashTable *monitors;
	GHashTable *running_auths;
	GHashTable *waiting_auths;
	const gchar *object_path;

	object_path = E_SOURCE_REGISTRY_SERVER_OBJECT_PATH;
	object_manager = g_dbus_object_manager_server_new (object_path);
	source_manager = e_dbus_source_manager_skeleton_new ();

	/* UID string -> ESource */
	sources = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) unref_data_source);

	/* Parent UID string -> GPtrArray of ESources */
	orphans = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_ptr_array_unref);

	/* GFile -> GFileMonitor */
	monitors = g_hash_table_new_full (
		(GHashFunc) g_file_hash,
		(GEqualFunc) g_file_equal,
		(GDestroyNotify) g_object_unref,
		(GDestroyNotify) g_object_unref);

	running_auths = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) auth_request_unref);

	waiting_auths = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) free_auth_queue);

	server->priv = E_SOURCE_REGISTRY_SERVER_GET_PRIVATE (server);
	server->priv->main_context = g_main_context_ref_thread_default ();
	server->priv->object_manager = object_manager;
	server->priv->source_manager = source_manager;
	server->priv->sources = sources;
	server->priv->orphans = orphans;
	server->priv->monitors = monitors;
	g_mutex_init (&server->priv->sources_lock);
	g_mutex_init (&server->priv->orphans_lock);
	g_mutex_init (&server->priv->auth_lock);
	server->priv->waiting_auths = waiting_auths;
	server->priv->running_auths = running_auths;

	g_signal_connect (
		source_manager, "handle-allow-auth-prompt-all",
		G_CALLBACK (source_registry_server_allow_auth_prompt_all_cb),
		server);

	g_signal_connect (
		source_manager, "handle-authenticate",
		G_CALLBACK (source_registry_server_authenticate_cb),
		server);

	g_signal_connect (
		source_manager, "handle-create-sources",
		G_CALLBACK (source_registry_server_create_sources_cb),
		server);

	g_signal_connect (
		source_manager, "handle-reload",
		G_CALLBACK (source_registry_server_reload_cb),
		server);
}

/**
 * e_source_registry_server_new:
 *
 * Creates a new instance of #ESourceRegistryServer.
 *
 * Returns: a new instance of #ESourceRegistryServer
 *
 * Since: 3.6
 **/
EDBusServer *
e_source_registry_server_new (void)
{
	return g_object_new (E_TYPE_SOURCE_REGISTRY_SERVER, NULL);
}

/**
 * e_source_registry_server_add_source:
 * @server: an #ESourceRegistryServer
 * @source: an #ESource
 *
 * Adds @source to @server.
 *
 * Since: 3.6
 **/
void
e_source_registry_server_add_source (ESourceRegistryServer *server,
                                     ESource *source)
{
	GDBusObject *dbus_object;
	EDBusSource *dbus_source;
	const gchar *extension_name;
	const gchar *uid;
	gchar *data;

	g_return_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server));
	g_return_if_fail (E_IS_SERVER_SIDE_SOURCE (source));

	uid = e_source_get_uid (source);
	g_return_if_fail (uid != NULL);

	g_mutex_lock (&server->priv->sources_lock);

	/* Check if we already have this object in the hierarchy. */
	if (g_hash_table_lookup (server->priv->sources, uid) != NULL) {
		g_mutex_unlock (&server->priv->sources_lock);
		return;
	}

	/* Make sure the parent object (if any) is in the hierarchy. */
	if (!source_registry_server_find_parent (server, source)) {
		g_mutex_unlock (&server->priv->sources_lock);
		return;
	}

	g_mutex_unlock (&server->priv->sources_lock);

	/* Before we emit, make sure the EDBusSource's "data" property
	 * is up-to-date.  ESource changes get propagated to the "data"
	 * property from an idle callback, which may still be pending. */

	dbus_object = e_source_ref_dbus_object (source);
	dbus_source = e_dbus_object_get_source (E_DBUS_OBJECT (dbus_object));

	data = e_source_to_string (source, NULL);
	e_dbus_source_set_data (dbus_source, data);
	g_free (data);

	g_object_unref (dbus_source);
	g_object_unref (dbus_object);

	/* If the added source has a [Collection] extension but the
	 * corresponding ECollectionBackendFactory is not available,
	 * the source gets permanently inserted in the orphans table
	 * to prevent it from being exported to client applications. */

	extension_name = E_SOURCE_EXTENSION_COLLECTION;
	if (e_source_has_extension (source, extension_name)) {
		ECollectionBackendFactory *backend_factory;

		backend_factory =
			e_source_registry_server_ref_backend_factory (
				server, source);
		if (backend_factory == NULL) {
			source_registry_server_orphans_insert (server, source);
			return;
		}
		g_object_unref (backend_factory);
	}

	source_registry_server_sources_insert (server, source);

	g_signal_emit (server, signals[SOURCE_ADDED], 0, source);

	/* This is to ensure the source data gets written to disk, since
	 * the ESource is exported now.  Could be racy otherwise if this
	 * function is called from a worker thread. */
	e_source_changed (source);

	/* Adopt any orphans that have been waiting for this object. */
	source_registry_server_adopt_orphans (server, source);
}

/* Helper for e_source_registry_server_remove_object() */
static void
source_registry_server_remove_object (ESourceRegistryServer *server,
                                      ESource *source)
{
	g_object_ref (source);

	if (source_registry_server_sources_remove (server, source)) {
		EServerSideSource *ss_source;

		ss_source = E_SERVER_SIDE_SOURCE (source);
		source_registry_server_orphans_insert (server, source);
		g_node_unlink (e_server_side_source_get_node (ss_source));
		g_signal_emit (server, signals[SOURCE_REMOVED], 0, source);
	}

	g_object_unref (source);
}

/**
 * e_source_registry_server_remove_source:
 * @server: an #ESourceRegistryServer
 * @source: an #ESource
 *
 * Removes @source and all of its descendants from @server.
 *
 * Since: 3.6
 **/
void
e_source_registry_server_remove_source (ESourceRegistryServer *server,
                                        ESource *source)
{
	ESource *child;
	ESource *exported;
	GQueue queue = G_QUEUE_INIT;
	const gchar *uid;

	g_return_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server));
	g_return_if_fail (E_IS_SERVER_SIDE_SOURCE (source));

	uid = e_source_get_uid (source);

	/* If the removed source is in the server hierarchy, gather
	 * it and all of its descendants into a queue in "post-order"
	 * so we're always processing leaf nodes as we pop sources off
	 * the head of the queue. */
	exported = e_source_registry_server_ref_source (server, uid);
	if (exported != NULL) {
		source_registry_server_queue_subtree (source, &queue);
		g_object_unref (exported);
	}

	/* Move the queued descendants to the orphan table, and emit a
	 * "source-removed" signal for each source.  This will include
	 * the removed source unless the source was already an orphan,
	 * in which case the queue will be empty. */
	while ((child = g_queue_pop_head (&queue)) != NULL) {
		source_registry_server_remove_object (server, child);
		g_object_unref (child);
	}

	/* The removed source should be in the orphan table now. */
	source_registry_server_orphans_remove (server, source);
}

/**
 * e_source_registry_server_load_all:
 * @server: an #ESourceRegistryServer
 * @error: return location for a #GError, or %NULL
 *
 * Loads data source key files from standard system-wide and user-specific
 * locations.  Because multiple errors can occur when loading multiple files,
 * @error is only set if a directory can not be opened.  If a data source key
 * file fails to load, the error is broadcast through the
 * #ESourceRegistryServer::load-error signal.
 *
 * Returns: %TRUE if the standard directories were successfully opened,
 *          but this does not imply the key files were successfully loaded
 *
 * Since: 3.6
 *
 * Deprecated: 3.8: Instead, implement an equivalent function yourself.
 *                  It was a mistake to encode this much file location
 *                  policy directly into the library API.
 **/
gboolean
e_source_registry_server_load_all (ESourceRegistryServer *server,
                                   GError **error)
{
	ESourcePermissionFlags flags;
	const gchar *directory;
	gboolean success;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), FALSE);

	/* Load the user's sources directory first so that user-specific
	 * data sources overshadow predefined data sources with identical
	 * UIDs.  The 'local' data source is one such example. */

	directory = e_server_side_source_get_user_dir ();
	flags = E_SOURCE_PERMISSION_REMOVABLE |
		E_SOURCE_PERMISSION_WRITABLE;
	success = e_source_registry_server_load_directory (
		server, directory, flags, error);
	g_prefix_error (error, "%s: ", directory);

	if (!success)
		return FALSE;

	directory = SYSTEM_WIDE_RO_SOURCES_DIRECTORY;
	flags = E_SOURCE_PERMISSION_NONE;
	success = e_source_registry_server_load_directory (
		server, directory, flags, error);
	g_prefix_error (error, "%s: ", directory);

	if (!success)
		return FALSE;

	directory = SYSTEM_WIDE_RW_SOURCES_DIRECTORY;
	flags = E_SOURCE_PERMISSION_WRITABLE;
	success = e_source_registry_server_load_directory (
		server, directory, flags, error);
	g_prefix_error (error, "%s: ", directory);

	if (!success)
		return FALSE;

	/* Signal that all files are now loaded. */
	g_signal_emit (server, signals[FILES_LOADED], 0);

	return TRUE;
}

/**
 * e_source_registry_server_load_directory:
 * @server: an #ESourceRegistryServer
 * @path: the path to the directory to load
 * @flags: permission flags for files loaded from @path
 * @error: return location for a #GError, or %NULL
 *
 * Loads data source key files in @path.  Because multiple errors can
 * occur when loading multiple files, @error is only set if @path can
 * not be opened.  If a key file fails to load, the error is broadcast
 * through the #ESourceRegistryServer::load-error signal.
 *
 * If the #E_DBUS_LOAD_DIRECTORY_REMOVABLE flag is given, then the @server
 * will emit signals on the D-Bus interface when key files are created or
 * deleted in @path.
 *
 * Returns: %TRUE if @path was successfully opened, but this
 *          does not imply the key files were successfully loaded
 *
 * Since: 3.6
 **/
gboolean
e_source_registry_server_load_directory (ESourceRegistryServer *server,
                                         const gchar *path,
                                         ESourcePermissionFlags flags,
                                         GError **error)
{
	GDir *dir;
	GFile *file;
	const gchar *name;
	gboolean removable;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	removable = ((flags & E_SOURCE_PERMISSION_REMOVABLE) != 0);

	/* If the directory doesn't exist then there's nothing to load.
	 * Note we do not use G_FILE_TEST_DIR here.  If the given path
	 * exists but is not a directory then we let g_dir_open() fail. */
	if (!g_file_test (path, G_FILE_TEST_EXISTS))
		return TRUE;

	dir = g_dir_open (path, 0, error);
	if (dir == NULL)
		return FALSE;

	file = g_file_new_for_path (path);

	while ((name = g_dir_read_name (dir)) != NULL) {
		ESource *source;
		GFile *child;
		GError *local_error = NULL;

		/* Ignore files with no ".source" suffix. */
		if (!g_str_has_suffix (name, ".source"))
			continue;

		child = g_file_get_child (file, name);

		source = e_source_registry_server_load_file (
			server, child, flags, &local_error);

		/* We don't need the returned reference. */
		if (source != NULL)
			g_object_unref (source);

		if (local_error != NULL) {
			e_source_registry_server_load_error (
				server, child, local_error);
			g_error_free (local_error);
		}

		g_object_unref (child);
	}

	g_dir_close (dir);

	/* Only data source files in the user's
	 * sources directory should be removable. */
	if (removable) {
		GFileMonitor *monitor;
		GError *local_error = NULL;

		/* Directory monitoring is a nice-to-have feature.
		 * If this fails, leave a breadcrumb on the console
		 * to indicate something went wrong, but don't return
		 * an error status. */
		monitor = g_file_monitor_directory (
			file, G_FILE_MONITOR_NONE, NULL, &local_error);

		/* Sanity check. */
		g_warn_if_fail (
			((monitor != NULL) && (local_error == NULL)) ||
			((monitor == NULL) && (local_error != NULL)));

		if (monitor != NULL) {
			g_signal_connect (
				monitor, "changed", G_CALLBACK (
				source_registry_server_monitor_changed_cb),
				server);

			g_hash_table_insert (
				server->priv->monitors,
				g_object_ref (file),
				g_object_ref (monitor));

			g_object_unref (monitor);
		}

		if (local_error != NULL) {
			g_warning ("%s: %s", G_STRFUNC, local_error->message);
			g_error_free (local_error);
		}
	}

	g_object_unref (file);

	return TRUE;
}

/**
 * e_source_registry_server_load_resource:
 * @server: an #ESourceRegistryServer
 * @resource: a #GResource containing data source key files
 * @path: the path to the data source key files inside @resource
 * @flags: permission flags for files loaded from @path
 * @error: return location for a #GError, or %NULL
 *
 * Loads data source key files from @resource by enumerating the children
 * at @path and calling e_source_registry_server_load_file() on each child.
 * Because multiple errors can occur when loading multiple files, @error is
 * only set if @path is invalid.  If a key file fails to load, the error is
 * broadcast through the #ESourceRegistryServer::load-error signal.
 *
 * Returns: %TRUE if @path was successfully located, but this does not
 *          imply the key files were successfully loaded
 *
 * Since: 3.8
 **/
gboolean
e_source_registry_server_load_resource (ESourceRegistryServer *server,
                                        GResource *resource,
                                        const gchar *path,
                                        ESourcePermissionFlags flags,
                                        GError **error)
{
	gchar **children;
	gint ii;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), FALSE);
	g_return_val_if_fail (resource != NULL, FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	children = g_resource_enumerate_children (
		resource, path, G_RESOURCE_LOOKUP_FLAGS_NONE, error);

	if (children == NULL)
		return FALSE;

	for (ii = 0; children[ii] != NULL; ii++) {
		ESource *source;
		GFile *file;
		gchar *child_path;
		gchar *resource_uri;
		GError *local_error = NULL;

		child_path = g_build_path ("/", path, children[ii], NULL);
		resource_uri = g_strconcat ("resource://", child_path, NULL);
		file = g_file_new_for_uri (resource_uri);
		g_free (resource_uri);
		g_free (child_path);

		source = e_source_registry_server_load_file (
			server, file, flags, &local_error);

		/* We don't need the returned reference. */
		if (source != NULL)
			g_object_unref (source);

		if (local_error != NULL) {
			e_source_registry_server_load_error (
				server, file, local_error);
			g_error_free (local_error);
		}

		g_object_unref (file);
	}

	g_strfreev (children);

	return TRUE;
}

/* Helper for e_source_registry_server_load_file() */
static gboolean
source_registry_server_tweak_key_file (ESourceRegistryServer *server,
                                       GFile *file,
                                       const gchar *uid,
                                       GError **error)
{
	GKeyFile *key_file;
	gchar *contents = NULL;
	gsize length;
	gboolean handler_pending;
	gboolean success = FALSE;
	gboolean tweaked = FALSE;

	/* Skip this if no one's listening. */
	handler_pending = g_signal_has_handler_pending (
		server, signals[TWEAK_KEY_FILE], 0, FALSE);
	if (!handler_pending)
		return TRUE;

	key_file = g_key_file_new ();

	g_file_load_contents (file, NULL, &contents, &length, NULL, error);

	if (contents != NULL) {
		success = g_key_file_load_from_data (
			key_file, contents, length,
			G_KEY_FILE_KEEP_COMMENTS |
			G_KEY_FILE_KEEP_TRANSLATIONS,
			error);
		g_free (contents);
	}

	if (success)
		g_signal_emit (
			server, signals[TWEAK_KEY_FILE], 0,
			key_file, uid, &tweaked);

	if (tweaked) {
		contents = g_key_file_to_data (key_file, &length, NULL);
		success = g_file_replace_contents (
			file, contents, length, NULL, FALSE,
			G_FILE_CREATE_NONE, NULL, NULL, error);
		g_free (contents);
	}

	g_key_file_free (key_file);

	return success;
}

/**
 * e_source_registry_server_load_file:
 * @server: an #ESourceRegistryServer
 * @file: the data source key file to load
 * @flags: initial permission flags for the data source
 * @error: return location for a #GError, or %NULL
 *
 * Creates an #ESource for a native key file and adds it to @server.
 * If an error occurs, the function returns %NULL and sets @error.
 *
 * The returned #ESource is referenced for thread-safety.  Unreference
 * the #ESource with g_object_unref() when finished with it.
 *
 * Returns: the newly-added #ESource, or %NULL on error
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_server_load_file (ESourceRegistryServer *server,
                                    GFile *file,
                                    ESourcePermissionFlags flags,
                                    GError **error)
{
	ESource *source;
	gboolean writable;
	gboolean removable;
	gboolean success = TRUE;
	gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), NULL);
	g_return_val_if_fail (G_IS_FILE (file), NULL);

	writable = ((flags & E_SOURCE_PERMISSION_WRITABLE) != 0);
	removable = ((flags & E_SOURCE_PERMISSION_REMOVABLE) != 0);

	uid = e_server_side_source_uid_from_file (file, error);

	if (uid == NULL)
		return NULL;

	/* Check if we already have this file loaded. */
	source = e_source_registry_server_ref_source (server, uid);

	/* If the source is to be removable then the key file can
	 * be written back to disk, and can therefore be tweaked. */
	if (source == NULL && removable)
		success = source_registry_server_tweak_key_file (
			server, file, uid, error);

	if (source == NULL && success)
		source = e_server_side_source_new (server, file, error);

	g_free (uid);

	if (source == NULL)
		return NULL;

	/* Set the data source's initial permissions, which
	 * determines which D-Bus methods it exports: write()
	 * if writable, remove() if removable.  We apply these
	 * before adding the source to the server because some
	 * "source-added" signal handlers may wish to override
	 * the initial permissions.
	 *
	 * Note that we apply the initial permission flags even
	 * if the data source has already been loaded.  That is
	 * intentional.  That is why the load_all() function loads
	 * the user directory before loading system-wide directories.
	 * If there's a UID collision between a data source in the
	 * user's directory and a data source in a system-wide
	 * directory, the permission flags for the system-wide
	 * directory should win.
	 *
	 * Consider an example:
	 *
	 * The built-in 'local' data source should always be
	 * writable but not removable.
	 *
	 * Suppose the user temporarily disables the 'local'
	 * data source.  The altered 'local' data source file
	 * (with Enabled=false) is saved in the user's sources
	 * directory.
	 *
	 * On the next startup, the altered 'local' file is
	 * first loaded from the user's source directory and
	 * given removable + writable permissions.
	 *
	 * We then load data sources from the 'rw-sources'
	 * system directory containing the unaltered 'local'
	 * file (with Enabled=true), which is not removable.
	 *
	 * We keep the contents of the altered 'local' file
	 * (Enabled=false), but override its permissions to
	 * just be writable, not removable.
	 */
	e_server_side_source_set_writable (
		E_SERVER_SIDE_SOURCE (source), writable);
	e_server_side_source_set_removable (
		E_SERVER_SIDE_SOURCE (source), removable);

	/* This does nothing if the source is already added. */
	e_source_registry_server_add_source (server, source);

	return source;
}

/**
 * e_source_registry_server_load_error:
 * @server: an #EBusSourceServer
 * @file: the #GFile that failed to load
 * @error: a #GError describing the load error
 *
 * Emits the #ESourceRegistryServer::load-error signal.
 *
 * Since: 3.6
 **/
void
e_source_registry_server_load_error (ESourceRegistryServer *server,
                                     GFile *file,
                                     const GError *error)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server));
	g_return_if_fail (G_IS_FILE (file));
	g_return_if_fail (error != NULL);

	g_signal_emit (server, signals[LOAD_ERROR], 0, file, error);
}

/**
 * e_source_registry_server_ref_source:
 * @server: an #ESourceRegistryServer
 * @uid: a unique identifier string
 *
 * Looks up an #ESource in @server by its unique identifier string.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: an #ESource, or %NULL if no match was found
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_server_ref_source (ESourceRegistryServer *server,
                                     const gchar *uid)
{
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	return source_registry_server_sources_lookup (server, uid);
}

/**
 * e_source_registry_server_list_sources:
 * @server: an #ESourceRegistryServer
 * @extension_name: an extension name, or %NULL
 *
 * Returns a list of registered sources, sorted by display name.  If
 * @extension_name is given, restrict the list to sources having that
 * extension name.
 *
 * The sources returned in the list are referenced for thread-safety.
 * They must each be unreferenced with g_object_unref() when finished
 * with them.  Free the returned #GList itself with g_list_free().
 *
 * An easy way to free the list properly in one step is as follows:
 *
 * |[
 *   g_list_free_full (list, g_object_unref);
 * ]|
 *
 * Returns: a sorted list of sources
 *
 * Since: 3.6
 **/
GList *
e_source_registry_server_list_sources (ESourceRegistryServer *server,
                                       const gchar *extension_name)
{
	GList *list, *link;
	GQueue trash = G_QUEUE_INIT;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), NULL);

	list = g_list_sort (
		source_registry_server_sources_get_values (server),
		(GCompareFunc) e_source_compare_by_display_name);

	if (extension_name == NULL)
		return list;

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESource *source = E_SOURCE (link->data);

		if (!e_source_has_extension (source, extension_name)) {
			g_queue_push_tail (&trash, link);
			g_object_unref (source);
		}
	}

	/* We do want pop_head() here, not pop_head_link(). */
	while ((link = g_queue_pop_head (&trash)) != NULL)
		list = g_list_delete_link (list, link);

	return list;
}

/**
 * e_source_registry_server_find_extension:
 * @server: an #ESourceRegistryServer
 * @source: an #ESource
 * @extension_name: the extension name to find
 *
 * Examines @source and its ancestors and returns the "deepest" #ESource
 * having an #ESourceExtension with the given @extension_name.  If neither
 * @source nor any of its ancestors have such an extension, the function
 * returns %NULL.
 *
 * This function is useful in cases when an #ESourceExtension is meant to
 * apply to both the #ESource it belongs to and the #ESource's descendants.
 *
 * A common example is the #ESourceCollection extension, where descendants
 * of an #ESource having an #ESourceCollection extension are implied to be
 * members of that collection.  In that example, this function can be used
 * to test whether @source is a member of a collection.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Note the function returns the #ESource containing the #ESourceExtension
 * instead of the #ESourceExtension itself because extension instances are
 * not to be referenced directly (see e_source_get_extension()).
 *
 * Returns: an #ESource, or %NULL if no match was found
 *
 * Since: 3.8
 **/
ESource *
e_source_registry_server_find_extension (ESourceRegistryServer *server,
                                         ESource *source,
                                         const gchar *extension_name)
{
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (extension_name != NULL, NULL);

	g_object_ref (source);

	while (!e_source_has_extension (source, extension_name)) {
		gchar *uid;

		uid = e_source_dup_parent (source);

		g_object_unref (source);
		source = NULL;

		if (uid != NULL) {
			source = e_source_registry_server_ref_source (
				server, uid);
			g_free (uid);
		}

		if (source == NULL)
			break;
	}

	return source;
}

/**
 * e_source_registry_server_ref_backend:
 * @server: an #ESourceRegistryServer
 * @source: an #ESource
 *
 * Returns the #ECollectionBackend associated with @source, or %NULL if
 * there is no #ECollectionBackend associated with @source.
 *
 * An #ESource is associated with an #ECollectionBackend if the #ESource has
 * an #ESourceCollection extension, or if it is a hierarchical descendant of
 * another #ESource which has an #ESourceCollection extension.
 *
 * The returned #ECollectionBackend is referenced for thread-safety.
 * Unreference the #ECollectionBackend with g_object_unref() when finished
 * with it.
 *
 * Returns: the #ECollectionBackend for @source, or %NULL
 *
 * Since: 3.6
 **/
ECollectionBackend *
e_source_registry_server_ref_backend (ESourceRegistryServer *server,
                                      ESource *source)
{
	ECollectionBackend *backend = NULL;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	source = e_source_registry_server_find_extension (
		server, source, E_SOURCE_EXTENSION_COLLECTION);

	if (source != NULL) {
		backend = g_object_get_data (
			G_OBJECT (source), BACKEND_DATA_KEY);
		if (backend != NULL)
			g_object_ref (backend);
		g_object_unref (source);
	}

	return backend;
}

/**
 * e_source_registry_server_ref_backend_factory:
 * @server: an #ESourceRegistryServer
 * @source: an #ESource
 *
 * Returns the #ECollectionBackendFactory for @source, if available.
 * If @source does not have an #ESourceCollection extension, or if the
 * #ESourceCollection extension names a #ESourceBackend:backend-name for
 * which there is no corresponding #ECollectionBackendFactory, the function
 * returns %NULL.
 *
 * The returned #ECollectionBackendFactory is referenced for thread-safety.
 * Unreference the #ECollectionBackendFactory with g_object_unref() when
 * finished with it.
 *
 * Returns: the #ECollectionBackendFactory for @source, or %NULL
 *
 * Since: 3.6
 **/
ECollectionBackendFactory *
e_source_registry_server_ref_backend_factory (ESourceRegistryServer *server,
                                              ESource *source)
{
	EBackendFactory *factory;
	ESourceBackend *extension;
	const gchar *backend_name;
	const gchar *extension_name;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	/* XXX Should we also check ancestor sources for a collection
	 *     extension so this function works for ANY source in the
	 *     collection?  Gonna refrain til a real use case emerges
	 *     but it's something to keep in mind. */

	extension_name = E_SOURCE_EXTENSION_COLLECTION;
	if (!e_source_has_extension (source, extension_name))
		return NULL;

	extension = e_source_get_extension (source, extension_name);
	backend_name = e_source_backend_get_backend_name (extension);

	factory = e_data_factory_ref_backend_factory (
		E_DATA_FACTORY (server), backend_name);

	if (factory == NULL)
		return NULL;

	/* The factory *should* be an ECollectionBackendFactory.
	 * We specify this in source_registry_server_class_init(). */
	return E_COLLECTION_BACKEND_FACTORY (factory);
}

/**
 * e_source_registry_server_new_auth_session:
 * @server: an #ESourceRegistryServer
 * @authenticator: an #ESourceAuthenticator
 * @source_uid: a data source identifier
 *
 * Convenience function instantiates an appropriate authentication
 * session type for @source_uid.
 *
 * If @server has an #EServerSideSource instance for @source_uid, then
 * its #EServerSideSource:auth-session-type is used to instantiate a new
 * authentication session.  Otherwise a plain #EAuthenticationSession is
 * instantiated.
 *
 * Unreference the returned #EAuthenticationSession with g_object_unref()
 * when finished with it.
 *
 * Returns: a new #EAuthenticationSession for @source_uid
 *
 * Since: 3.8
 **/
EAuthenticationSession *
e_source_registry_server_new_auth_session (ESourceRegistryServer *server,
                                           ESourceAuthenticator *authenticator,
                                           const gchar *source_uid)
{
	GType auth_session_type = E_TYPE_AUTHENTICATION_SESSION;
	ESource *source;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), NULL);
	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATOR (authenticator), NULL);
	g_return_val_if_fail (source_uid != NULL, NULL);

	source = e_source_registry_server_ref_source (server, source_uid);
	if (source != NULL) {
		auth_session_type =
			e_server_side_source_get_auth_session_type (
			E_SERVER_SIDE_SOURCE (source));
		g_object_unref (source);
	}

	g_return_val_if_fail (
		g_type_is_a (
			auth_session_type,
			E_TYPE_AUTHENTICATION_SESSION), NULL);

	return g_object_new (
		auth_session_type,
		"server", server,
		"authenticator", authenticator,
		"source-uid", source_uid, NULL);
}

/**
 * e_source_registry_server_authenticate_sync:
 * @server: an #ESourceRegistryServer
 * @session: an #EAuthenticationSession
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Queues the @session behind any ongoing or pending authentication
 * sessions for the same data source, and eventually executes @session
 * (see e_authentication_session_execute_sync() for more details).
 *
 * This function blocks until @session is finished executing.  For a
 * non-blocking variation see e_source_registry_server_authenticate().
 *
 * If an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.6
 **/
gboolean
e_source_registry_server_authenticate_sync (ESourceRegistryServer *server,
                                            EAuthenticationSession *session,
                                            GCancellable *cancellable,
                                            GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), FALSE);
	g_return_val_if_fail (E_IS_AUTHENTICATION_SESSION (session), FALSE);

	closure = e_async_closure_new ();

	e_source_registry_server_authenticate (
		server, session, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_source_registry_server_authenticate_finish (
		server, result, error);

	e_async_closure_free (closure);

	return success;
}

/**
 * e_source_registry_server_authenticate:
 * @server: an #ESourceRegistryServer
 * @session: an #EAuthenticationSession
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Queues the @session behind any ongoing or pending authentication
 * sessions for the same data source, and eventually executes @session
 * (see e_authentication_session_execute_sync() for more details).
 *
 * This function returns immediately after enqueuing @session.  When
 * @session is finished executing, @callback will be called.  You can
 * then call e_source_registry_server_authenticate_finish() to get the
 * result of the operation.
 *
 * Since: 3.6
 **/
void
e_source_registry_server_authenticate (ESourceRegistryServer *server,
                                       EAuthenticationSession *session,
                                       GCancellable *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AuthRequest *request;
	const gchar *uid;

	g_return_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server));
	g_return_if_fail (E_IS_AUTHENTICATION_SESSION (session));

	uid = e_authentication_session_get_source_uid (session);
	g_return_if_fail (uid != NULL);

	simple = g_simple_async_result_new (
		G_OBJECT (server), callback, user_data,
		e_source_registry_server_authenticate);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	request = auth_request_new (session, simple, cancellable);
	source_registry_server_auth_request_push (server, uid, request);
	auth_request_unref (request);

	source_registry_server_maybe_start_auth_session (server, uid);

	g_object_unref (simple);
}

/**
 * e_source_registry_server_authenticate_finish:
 * @server: an #ESourceRegistryServer
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_registry_server_authenticate().
 * If an error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.6
 **/
gboolean
e_source_registry_server_authenticate_finish (ESourceRegistryServer *server,
                                              GAsyncResult *result,
                                              GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (server),
		e_source_registry_server_authenticate), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

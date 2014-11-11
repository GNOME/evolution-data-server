/*
 * e-authentication-session.c
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
 * SECTION: e-authentication-session
 * @include: libebackend/libebackend.h
 * @short_description: Centralized authentication management
 *
 * #EAuthenticationSession provides centralized password management and
 * password prompting for all clients of the registry D-Bus service.
 *
 * An #EAuthenticationSession is created within the registry D-Bus service
 * when the service receives a request to authenticate some data source.
 * Clients can issue requests by calling e_source_registry_authenticate().
 * Requests can also come from any #ECollectionBackend running within the
 * service itself.
 *
 * An #EAuthenticationSession requires some object implementing the
 * #ESourceAuthenticator interface to verify stored or user-provided
 * passwords.  #EAuthenticationMediator is used for client-issued
 * authentication requests.  Custom collection backends derived from
 * #ECollectionBackend usually implement the #ESourceAuthenticator
 * interface themselves.
 *
 * The #EAuthenticationSession is then handed to #ESourceRegistryServer
 * through e_source_registry_server_authenticate() where it waits in line
 * behind other previously added authentication sessions.  When its turn
 * comes, the server calls e_authentication_session_execute() to begin
 * the interactive authentication session.
 **/

#include "e-authentication-session.h"

/* XXX Yeah, yeah... */
#define GCR_API_SUBJECT_TO_CHANGE

#include <config.h>
#include <string.h>
#include <glib/gi18n-lib.h>
#include <gcr/gcr-base.h>

/* Private D-Bus classes. */
#include <e-dbus-authenticator.h>

#include <libebackend/e-authentication-mediator.h>
#include <libebackend/e-backend-enumtypes.h>
#include <libebackend/e-server-side-source.h>
#include <libebackend/e-source-registry-server.h>

#define E_AUTHENTICATION_SESSION_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_AUTHENTICATION_SESSION, EAuthenticationSessionPrivate))

/* Wait forever for a system prompt. */
#define SYSTEM_PROMPT_TIMEOUT (-1)

typedef struct _AsyncContext AsyncContext;

struct _EAuthenticationSessionPrivate {
	ESourceRegistryServer *server;
	ESourceAuthenticator *authenticator;
	gchar *source_uid;

	/* These are for configuring system prompts. */
	GMutex property_lock;
	gchar *prompt_title;
	gchar *prompt_message;
	gchar *prompt_description;
};

struct _AsyncContext {
	EAuthenticationSessionResult auth_result;
	gchar *password;
	gboolean permanently;
};

enum {
	PROP_0,
	PROP_AUTHENTICATOR,
	PROP_PROMPT_DESCRIPTION,
	PROP_PROMPT_MESSAGE,
	PROP_PROMPT_TITLE,
	PROP_SERVER,
	PROP_SOURCE_UID
};

/* Forward Declarations */
static void	authentication_session_msg
					(EAuthenticationSession *session,
					 const gchar *format,
					 ...) G_GNUC_PRINTF (2, 3);

G_DEFINE_TYPE (
	EAuthenticationSession,
	e_authentication_session,
	G_TYPE_OBJECT)

static void
async_context_free (AsyncContext *async_context)
{
	g_free (async_context->password);
	g_slice_free (AsyncContext, async_context);
}

static void
authentication_session_msg (EAuthenticationSession *session,
                            const gchar *format,
                            ...)
{
	GString *buffer;
	va_list args;

	buffer = g_string_sized_new (256);

	g_string_append_printf (
		buffer, "AUTH (%s): ",
		session->priv->source_uid);

	va_start (args, format);
	g_string_append_vprintf (buffer, format, args);
	va_end (args);

	e_source_registry_debug_print ("%s\n", buffer->str);

	g_string_free (buffer, TRUE);
}

static void
authentication_session_set_authenticator (EAuthenticationSession *session,
                                          ESourceAuthenticator *authenticator)
{
	g_return_if_fail (E_IS_SOURCE_AUTHENTICATOR (authenticator));
	g_return_if_fail (session->priv->authenticator == NULL);

	session->priv->authenticator = g_object_ref (authenticator);
}

static void
authentication_session_set_server (EAuthenticationSession *session,
                                   ESourceRegistryServer *server)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server));
	g_return_if_fail (session->priv->server == NULL);

	session->priv->server = g_object_ref (server);
}

static void
authentication_session_set_source_uid (EAuthenticationSession *session,
                                       const gchar *source_uid)
{
	g_return_if_fail (source_uid != NULL);
	g_return_if_fail (session->priv->source_uid == NULL);

	session->priv->source_uid = g_strdup (source_uid);
}

static void
authentication_session_set_property (GObject *object,
                                     guint property_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_AUTHENTICATOR:
			authentication_session_set_authenticator (
				E_AUTHENTICATION_SESSION (object),
				g_value_get_object (value));
			return;

		case PROP_PROMPT_DESCRIPTION:
			e_authentication_session_set_prompt_description (
				E_AUTHENTICATION_SESSION (object),
				g_value_get_string (value));
			return;

		case PROP_PROMPT_MESSAGE:
			e_authentication_session_set_prompt_message (
				E_AUTHENTICATION_SESSION (object),
				g_value_get_string (value));
			return;

		case PROP_PROMPT_TITLE:
			e_authentication_session_set_prompt_title (
				E_AUTHENTICATION_SESSION (object),
				g_value_get_string (value));
			return;

		case PROP_SERVER:
			authentication_session_set_server (
				E_AUTHENTICATION_SESSION (object),
				g_value_get_object (value));
			return;

		case PROP_SOURCE_UID:
			authentication_session_set_source_uid (
				E_AUTHENTICATION_SESSION (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
authentication_session_get_property (GObject *object,
                                     guint property_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_AUTHENTICATOR:
			g_value_set_object (
				value,
				e_authentication_session_get_authenticator (
				E_AUTHENTICATION_SESSION (object)));
			return;

		case PROP_PROMPT_DESCRIPTION:
			g_value_take_string (
				value,
				e_authentication_session_dup_prompt_description (
				E_AUTHENTICATION_SESSION (object)));
			return;

		case PROP_PROMPT_MESSAGE:
			g_value_take_string (
				value,
				e_authentication_session_dup_prompt_message (
				E_AUTHENTICATION_SESSION (object)));
			return;

		case PROP_PROMPT_TITLE:
			g_value_take_string (
				value,
				e_authentication_session_dup_prompt_title (
				E_AUTHENTICATION_SESSION (object)));
			return;

		case PROP_SERVER:
			g_value_set_object (
				value,
				e_authentication_session_get_server (
				E_AUTHENTICATION_SESSION (object)));
			return;

		case PROP_SOURCE_UID:
			g_value_set_string (
				value,
				e_authentication_session_get_source_uid (
				E_AUTHENTICATION_SESSION (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
authentication_session_dispose (GObject *object)
{
	EAuthenticationSessionPrivate *priv;

	priv = E_AUTHENTICATION_SESSION_GET_PRIVATE (object);

	if (priv->server != NULL) {
		g_object_unref (priv->server);
		priv->server = NULL;
	}

	if (priv->authenticator != NULL) {
		g_object_unref (priv->authenticator);
		priv->authenticator = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_authentication_session_parent_class)->
		dispose (object);
}

static void
authentication_session_finalize (GObject *object)
{
	EAuthenticationSessionPrivate *priv;

	priv = E_AUTHENTICATION_SESSION_GET_PRIVATE (object);

	g_mutex_clear (&priv->property_lock);

	g_free (priv->source_uid);
	g_free (priv->prompt_title);
	g_free (priv->prompt_message);
	g_free (priv->prompt_description);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_authentication_session_parent_class)->
		finalize (object);
}

static void
authentication_session_constructed (GObject *object)
{
	EAuthenticationSession *session;
	ESourceAuthenticator *authenticator;
	ESourceRegistryServer *server;
	ESource *source;
	const gchar *source_uid;

	session = E_AUTHENTICATION_SESSION (object);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_authentication_session_parent_class)->constructed (object);

	/* If the server knows about the data source UID we've been
	 * given, then we can auto-configure our own prompt strings. */

	server = e_authentication_session_get_server (session);
	source_uid = e_authentication_session_get_source_uid (session);
	authenticator = e_authentication_session_get_authenticator (session);

	source = e_source_registry_server_ref_source (server, source_uid);
	if (source != NULL) {
		gchar *prompt_title = NULL;
		gchar *prompt_message = NULL;
		gchar *prompt_description = NULL;

		e_source_authenticator_get_prompt_strings (
			authenticator, source,
			&prompt_title,
			&prompt_message,
			&prompt_description);

		g_object_set (
			session,
			"prompt-title", prompt_title,
			"prompt-message", prompt_message,
			"prompt-description", prompt_description,
			NULL);

		g_free (prompt_title);
		g_free (prompt_message);
		g_free (prompt_description);

		g_object_unref (source);
	}
}

/* Helper for authentication_session_execute() */
static void
authentication_session_execute_thread (GSimpleAsyncResult *simple,
                                       GObject *object,
                                       GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->auth_result =
		e_authentication_session_execute_sync (
			E_AUTHENTICATION_SESSION (object),
			cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static EAuthenticationSessionResult
authentication_session_execute_sync (EAuthenticationSession *session,
                                     GCancellable *cancellable,
                                     GError **error)
{
	ESourceAuthenticator *authenticator;
	EAuthenticationSessionResult session_result;
	ESourceAuthenticationResult auth_result;
	ESourceRegistryServer *server;
	ESource *source = NULL;
	GcrPrompt *prompt;
	GString *password_string = NULL;
	const gchar *label;
	const gchar *source_uid;
	const gchar *prompt_password;
	gchar *stored_password = NULL;
	gboolean success;
	gboolean allow_auth_prompt = TRUE;
	gboolean remember_password = TRUE;
	gboolean first_prompt = TRUE;
	GError *local_error = NULL;

	/* XXX I moved the execute() operation into a class method thinking
	 *     we might someday want to subclass EAuthenticationSession and
	 *     override this method to make it behave differently.
	 *
	 *     It would be a little premature to do this now, but we might
	 *     also want to use the basic algorithm here as a template and
	 *     turn the password lookup/delete/store operations into class
	 *     methods that could also be overridden.  I reserved adequate
	 *     space in the class struct for this should the need arise.
	 *
	 *     For now though we'll keep it simple.  I don't want to over-
	 *     engineer this too much in trying to make it future-proof.
	 */

	authentication_session_msg (session, "Initiated");

	server = e_authentication_session_get_server (session);
	source_uid = e_authentication_session_get_source_uid (session);
	authenticator = e_authentication_session_get_authenticator (session);

	/* This will return NULL if the authenticating data source
	 * has not yet been submitted to the D-Bus registry service. */
	source = e_source_registry_server_ref_source (server, source_uid);

	if (e_source_authenticator_get_without_password (authenticator)) {
		password_string = g_string_new ("");

		auth_result = e_source_authenticator_try_password_sync (
			authenticator, password_string, cancellable, &local_error);

		g_string_free (password_string, TRUE);
		password_string = NULL;

		if (auth_result == E_SOURCE_AUTHENTICATION_ERROR &&
		    g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_propagate_error (error, local_error);
			session_result = E_AUTHENTICATION_SESSION_ERROR;
			goto exit;
		}

		g_clear_error (&local_error);

		if (auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
			session_result = E_AUTHENTICATION_SESSION_SUCCESS;
			goto exit;
		}

		/* if an empty password fails, then ask a user for it */
	}

	if (source != NULL) {
		ESourceExtension *extension;
		const gchar *extension_name;

		success = e_source_lookup_password_sync (
			source, cancellable, &stored_password, error);

		if (!success) {
			session_result = E_AUTHENTICATION_SESSION_ERROR;
			goto exit;
		}

		extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
		extension = e_source_get_extension (source, extension_name);

		allow_auth_prompt =
			e_server_side_source_get_allow_auth_prompt (
			E_SERVER_SIDE_SOURCE (source));

		remember_password =
			e_source_authentication_get_remember_password (
			E_SOURCE_AUTHENTICATION (extension));
	}

	auth_result = E_SOURCE_AUTHENTICATION_REJECTED;

	/* If we found a stored password, signal the client without
	 * interrupting the user.  Note, if the client responds with
	 * REJECTED, we'll have to interrupt the user after all. */
	if (stored_password != NULL) {
		password_string = g_string_new (stored_password);

		auth_result = e_source_authenticator_try_password_sync (
			authenticator, password_string, cancellable, error);

		g_string_free (password_string, TRUE);
		password_string = NULL;

		g_free (stored_password);
		stored_password = NULL;
	}

	if (auth_result == E_SOURCE_AUTHENTICATION_ERROR) {
		session_result = E_AUTHENTICATION_SESSION_ERROR;
		goto exit;
	}

	if (auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
		session_result = E_AUTHENTICATION_SESSION_SUCCESS;
		goto exit;
	}

	g_warn_if_fail (auth_result == E_SOURCE_AUTHENTICATION_REJECTED);

	/* Check if we're allowed to interrupt the user for a password.
	 * If not, we have no choice but to dismiss the authentication
	 * request. */
	if (!allow_auth_prompt) {
		session_result = E_AUTHENTICATION_SESSION_DISMISSED;
		goto exit;
	}

	/* Configure a system prompt. */

 try_again:

	prompt = gcr_system_prompt_open (
		SYSTEM_PROMPT_TIMEOUT, cancellable, error);

	if (prompt == NULL) {
		session_result = E_AUTHENTICATION_SESSION_ERROR;
		goto exit;
	}

	g_object_bind_property (
		session, "prompt-title",
		prompt, "title",
		G_BINDING_SYNC_CREATE);

	g_object_bind_property (
		session, "prompt-message",
		prompt, "message",
		G_BINDING_SYNC_CREATE);

	g_object_bind_property (
		session, "prompt-description",
		prompt, "description",
		G_BINDING_SYNC_CREATE);

	label = _("Add this password to your keyring");
	gcr_prompt_set_choice_label (prompt, label);
	gcr_prompt_set_choice_chosen (prompt, remember_password);

	if (!first_prompt)
		gcr_prompt_set_warning (prompt, _("Password was incorrect"));
	else
		first_prompt = FALSE;

	/* Prompt the user for a password. */

	prompt_password = gcr_prompt_password (
		prompt, cancellable, &local_error);

	if (local_error != NULL) {
		session_result = E_AUTHENTICATION_SESSION_ERROR;
		g_propagate_error (error, local_error);
		local_error = NULL;
		goto close_prompt;
	}

	/* No password and no error indicates a dismissal. */
	if (prompt_password == NULL) {
		session_result = E_AUTHENTICATION_SESSION_DISMISSED;
		goto close_prompt;
	}

	if (password_string)
		g_string_free (password_string, TRUE);
	password_string = g_string_new (prompt_password);
	prompt_password = NULL;

	remember_password = gcr_prompt_get_choice_chosen (prompt);

	/* Failure here does not affect the outcome of this
	 * operation, but leave a breadcrumb as evidence that
	 * something went wrong. */

	gcr_system_prompt_close (
		GCR_SYSTEM_PROMPT (prompt),
		cancellable, &local_error);

	if (local_error != NULL) {
		g_warning ("%s: %s", G_STRFUNC, local_error->message);
		g_clear_error (&local_error);
	}

	g_object_unref (prompt);
	prompt = NULL;

	if (source != NULL) {
		ESourceExtension *extension;
		const gchar *extension_name;

		extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
		extension = e_source_get_extension (source, extension_name);

		e_source_authentication_set_remember_password (
			E_SOURCE_AUTHENTICATION (extension),
			remember_password);
	}

	/* Attempt authentication with the provided password. */

	auth_result = e_source_authenticator_try_password_sync (
		authenticator, password_string, cancellable, error);

	if (auth_result == E_SOURCE_AUTHENTICATION_ERROR) {
		session_result = E_AUTHENTICATION_SESSION_ERROR;
		goto exit;
	}

	if (auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
		gchar *password_copy;

		session_result = E_AUTHENTICATION_SESSION_SUCCESS;

		/* XXX Not sure if it's safe to use the prompt's
		 *     password string after closing the prompt,
		 *     so make a copy here just to be safe. */
		password_copy = gcr_secure_memory_strdup (password_string->str);

		/* Failure here does not affect the outcome of this
		 * operation, but leave a breadcrumb as evidence that
		 * something went wrong. */

		/* Create a phony "scratch" source if necessary. */
		if (source == NULL) {
			source = e_source_new_with_uid (
				source_uid, NULL, &local_error);
		}

		if (source != NULL) {
			e_source_store_password_sync (
				source, password_copy, remember_password,
				cancellable, &local_error);
		}

		if (local_error != NULL) {
			g_warning ("%s: %s", G_STRFUNC, local_error->message);
			g_clear_error (&local_error);
		}

		gcr_secure_memory_strfree (password_copy);

		goto exit;
	}

	g_warn_if_fail (auth_result == E_SOURCE_AUTHENTICATION_REJECTED);

	goto try_again;

close_prompt:

	/* Failure here does not affect the outcome of this operation,
	 * but leave a breadcrumb as evidence that something went wrong. */

	gcr_system_prompt_close (
		GCR_SYSTEM_PROMPT (prompt),
		cancellable, &local_error);

	if (local_error != NULL) {
		g_warning ("%s: %s", G_STRFUNC, local_error->message);
		g_clear_error (&local_error);
	}

	g_object_unref (prompt);

exit:

	switch (session_result) {
		case E_AUTHENTICATION_SESSION_ERROR:
			if (error != NULL && *error != NULL)
				authentication_session_msg (
					session, "Complete (ERROR - %s)",
					(*error)->message);
			else
				authentication_session_msg (
					session, "Complete (ERROR)");
			break;
		case E_AUTHENTICATION_SESSION_SUCCESS:
			authentication_session_msg (
				session, "Complete (SUCCESS)");
			break;
		case E_AUTHENTICATION_SESSION_DISMISSED:
			authentication_session_msg (
				session, "Complete (DISMISSED)");
			break;
		/* coverity[dead_error_begin] */
		default:
			g_warn_if_reached ();
	}

	g_clear_object (&source);

	if (password_string) {
		g_string_free (password_string, TRUE);
		password_string = NULL;
	}

	return session_result;
}

static void
authentication_session_execute (EAuthenticationSession *session,
                                gint io_priority,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_AUTHENTICATION_SESSION (session));

	async_context = g_slice_new0 (AsyncContext);

	simple = g_simple_async_result_new (
		G_OBJECT (session), callback, user_data,
		e_authentication_session_execute);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, authentication_session_execute_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static EAuthenticationSessionResult
authentication_session_execute_finish (EAuthenticationSession *session,
                                       GAsyncResult *result,
                                       GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (session),
		e_authentication_session_execute),
		E_AUTHENTICATION_SESSION_DISMISSED);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return E_AUTHENTICATION_SESSION_ERROR;

	return async_context->auth_result;
}

static void
e_authentication_session_class_init (EAuthenticationSessionClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (
		class, sizeof (EAuthenticationSessionPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = authentication_session_set_property;
	object_class->get_property = authentication_session_get_property;
	object_class->dispose = authentication_session_dispose;
	object_class->finalize = authentication_session_finalize;
	object_class->constructed = authentication_session_constructed;

	class->execute_sync = authentication_session_execute_sync;
	class->execute = authentication_session_execute;
	class->execute_finish = authentication_session_execute_finish;

	g_object_class_install_property (
		object_class,
		PROP_AUTHENTICATOR,
		g_param_spec_object (
			"authenticator",
			"Authenticator",
			"Handles authentication attempts",
			E_TYPE_SOURCE_AUTHENTICATOR,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_PROMPT_DESCRIPTION,
		g_param_spec_string (
			"prompt-description",
			"Prompt Description",
			"The detailed description of the prompt",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_PROMPT_MESSAGE,
		g_param_spec_string (
			"prompt-message",
			"Prompt Message",
			"The prompt message for the user",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_PROMPT_TITLE,
		g_param_spec_string (
			"prompt-title",
			"Prompt Title",
			"The title of the prompt",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SERVER,
		g_param_spec_object (
			"server",
			"Server",
			"The server to which the session belongs",
			E_TYPE_SOURCE_REGISTRY_SERVER,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SOURCE_UID,
		g_param_spec_string (
			"source-uid",
			"Source UID",
			"Unique ID of the data source being authenticated",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_authentication_session_init (EAuthenticationSession *session)
{
	session->priv = E_AUTHENTICATION_SESSION_GET_PRIVATE (session);
	g_mutex_init (&session->priv->property_lock);
}

G_DEFINE_QUARK (
	e - authentication - session - error - quark,
	e_authentication_session_error);

/**
 * e_authentication_session_new:
 * @server: an #ESourceRegistryServer
 * @authenticator: an #ESourceAuthenticator
 * @source_uid: a data source identifier
 *
 * Creates a new #EAuthenticationSession instance for @server using
 * @authenticator to handle authentication attempts.
 *
 * Note that @source_uid does not necessarily have to be known to the
 * @server, as in the case when configuring a new data source, but it
 * still has to be unique.
 *
 * Returns: a newly-created #EAuthenticationSession
 *
 * Since: 3.6
 *
 * Deprecated: 3.8: Use e_source_registry_server_new_auth_session() instead.
 **/
EAuthenticationSession *
e_authentication_session_new (ESourceRegistryServer *server,
                              ESourceAuthenticator *authenticator,
                              const gchar *source_uid)
{
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), NULL);
	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATOR (authenticator), NULL);
	g_return_val_if_fail (source_uid != NULL, NULL);

	return g_object_new (
		E_TYPE_AUTHENTICATION_SESSION,
		"server", server,
		"authenticator", authenticator,
		"source-uid", source_uid, NULL);
}

/**
 * e_authentication_session_get_server:
 * @session: an #EAuthenticationSession
 *
 * Returns the #ESourceRegistryServer to which @session belongs.
 *
 * Returns: the #ESourceRegistryServer for @session
 *
 * Since: 3.6
 **/
ESourceRegistryServer *
e_authentication_session_get_server (EAuthenticationSession *session)
{
	g_return_val_if_fail (E_IS_AUTHENTICATION_SESSION (session), NULL);

	return session->priv->server;
}

/**
 * e_authentication_session_get_authenticator:
 * @session: an #EAuthenticationSession
 *
 * Returns the #ESourceAuthenticator handling authentication attempts for
 * @session.  This is usually an #EAuthenticationMediator but can also be
 * a custom collection backend derived from #ECollectionBackend.
 *
 * Returns: the #ESourceAuthenticator for @session
 *
 * Since: 3.6
 **/
ESourceAuthenticator *
e_authentication_session_get_authenticator (EAuthenticationSession *session)
{
	g_return_val_if_fail (E_IS_AUTHENTICATION_SESSION (session), NULL);

	return session->priv->authenticator;
}

/**
 * e_authentication_session_get_source_uid:
 * @session: an #EAuthenticationSession
 *
 * Returns the #ESource:uid of the authenticating data source.  The data
 * source may or may not be known to the #EAuthenticationSession:server.
 *
 * Returns: the UID of the authenticating data source
 *
 * Since: 3.6
 **/
const gchar *
e_authentication_session_get_source_uid (EAuthenticationSession *session)
{
	g_return_val_if_fail (E_IS_AUTHENTICATION_SESSION (session), NULL);

	return session->priv->source_uid;
}

/**
 * e_authentication_session_get_prompt_title:
 * @session: an #EAuthenticationSession
 *
 * Returns the text used for the password prompt title should prompting
 * be necessary.  See #GcrPrompt for more details about password prompts.
 *
 * Returns: the password prompt title
 *
 * Since: 3.6
 **/
const gchar *
e_authentication_session_get_prompt_title (EAuthenticationSession *session)
{
	g_return_val_if_fail (E_IS_AUTHENTICATION_SESSION (session), NULL);

	return session->priv->prompt_title;
}

/**
 * e_authentication_session_dup_prompt_title:
 * @session: an #EAuthenticationSession
 *
 * Thread-safe variation of e_authentication_session_get_prompt_title().
 * Use this function when accessing @session from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #EAuthenticationSession:prompt-title
 *
 * Since: 3.6
 **/
gchar *
e_authentication_session_dup_prompt_title (EAuthenticationSession *session)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_AUTHENTICATION_SESSION (session), NULL);

	g_mutex_lock (&session->priv->property_lock);

	protected = e_authentication_session_get_prompt_title (session);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&session->priv->property_lock);

	return duplicate;
}

/**
 * e_authentication_session_set_prompt_title:
 * @session: an #EAuthenticationSession
 * @prompt_title: the password prompt title, or %NULL
 *
 * Sets the text used for the password prompt title should prompting be
 * necessary.  See #GcrPrompt for more details about password prompts.
 *
 * Since: 3.6
 **/
void
e_authentication_session_set_prompt_title (EAuthenticationSession *session,
                                           const gchar *prompt_title)
{
	g_return_if_fail (E_IS_AUTHENTICATION_SESSION (session));

	g_mutex_lock (&session->priv->property_lock);

	if (g_strcmp0 (session->priv->prompt_title, prompt_title) == 0) {
		g_mutex_unlock (&session->priv->property_lock);
		return;
	}

	g_free (session->priv->prompt_title);
	session->priv->prompt_title = g_strdup (prompt_title);

	g_mutex_unlock (&session->priv->property_lock);

	g_object_notify (G_OBJECT (session), "prompt-title");
}

/**
 * e_authentication_session_get_prompt_message:
 * @session: an #EAuthenticationSession
 *
 * Returns the text used for the password prompt message should prompting
 * be necessary.  See #GcrPrompt for more details about password prompts.
 *
 * Returns: the password prompt message
 *
 * Since: 3.6
 **/
const gchar *
e_authentication_session_get_prompt_message (EAuthenticationSession *session)
{
	g_return_val_if_fail (E_IS_AUTHENTICATION_SESSION (session), NULL);

	return session->priv->prompt_message;
}

/**
 * e_authentication_session_dup_prompt_message:
 * @session: an #EAuthenticationSession
 *
 * Thread-safe variation of e_authentication_session_get_prompt_message().
 * Use this function when accessing @session from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #EAuthenticationSession:prompt-message
 *
 * Since: 3.6
 **/
gchar *
e_authentication_session_dup_prompt_message (EAuthenticationSession *session)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_AUTHENTICATION_SESSION (session), NULL);

	g_mutex_lock (&session->priv->property_lock);

	protected = e_authentication_session_get_prompt_message (session);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&session->priv->property_lock);

	return duplicate;
}

/**
 * e_authentication_session_set_prompt_message:
 * @session: an #EAuthenticationSession
 * @prompt_message: the password prompt message, or %NULL
 *
 * Sets the text used for the password prompt message should prompting be
 * necessary.  See #GcrPrompt for more details about password prompts.
 *
 * Since: 3.6
 **/
void
e_authentication_session_set_prompt_message (EAuthenticationSession *session,
                                             const gchar *prompt_message)
{
	g_return_if_fail (E_IS_AUTHENTICATION_SESSION (session));

	g_mutex_lock (&session->priv->property_lock);

	if (g_strcmp0 (session->priv->prompt_message, prompt_message) == 0) {
		g_mutex_unlock (&session->priv->property_lock);
		return;
	}

	g_free (session->priv->prompt_message);
	session->priv->prompt_message = g_strdup (prompt_message);

	g_mutex_unlock (&session->priv->property_lock);

	g_object_notify (G_OBJECT (session), "prompt-message");
}

/**
 * e_authentication_session_get_prompt_description:
 * @session: an #EAuthenticationSession
 *
 * Returns the text used for the password prompt description should prompting
 * be necessary.  See #GcrPrompt for more details about password prompts.
 *
 * Returns: the password prompt description
 *
 * Since: 3.6
 **/
const gchar *
e_authentication_session_get_prompt_description (EAuthenticationSession *session)
{
	g_return_val_if_fail (E_IS_AUTHENTICATION_SESSION (session), NULL);

	return session->priv->prompt_description;
}

/**
 * e_authentication_session_dup_prompt_description:
 * @session: an #EAuthenticationSession
 *
 * Thread-safe variation of e_authentication_session_get_prompt_description().
 * Use this function when accessing @session from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of
 *          #EAuthenticationSession:prompt-description
 *
 * Since: 3.6
 **/
gchar *
e_authentication_session_dup_prompt_description (EAuthenticationSession *session)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_AUTHENTICATION_SESSION (session), NULL);

	g_mutex_lock (&session->priv->property_lock);

	protected = e_authentication_session_get_prompt_description (session);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&session->priv->property_lock);

	return duplicate;
}

/**
 * e_authentication_session_set_prompt_description:
 * @session: an #EAuthenticationSession
 * @prompt_description: the password prompt description
 *
 * Sets the text used for the password prompt description should prompting
 * be necessary.  See #GcrPrompt for more details about password prompts.
 *
 * Since: 3.6
 **/
void
e_authentication_session_set_prompt_description (EAuthenticationSession *session,
                                                 const gchar *prompt_description)
{
	g_return_if_fail (E_IS_AUTHENTICATION_SESSION (session));

	g_mutex_lock (&session->priv->property_lock);

	if (g_strcmp0 (session->priv->prompt_description, prompt_description) == 0) {
		g_mutex_unlock (&session->priv->property_lock);
		return;
	}

	g_free (session->priv->prompt_description);
	session->priv->prompt_description = g_strdup (prompt_description);

	g_mutex_unlock (&session->priv->property_lock);

	g_object_notify (G_OBJECT (session), "prompt-description");
}

/**
 * e_authentication_session_execute_sync:
 * @session: an #EAuthenticationSession
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Executes an authentication session.
 *
 * First the secret service is queried for a stored password.  If found,
 * an authentication attempt is made without disturbing the user.  If no
 * stored password is found, or if the stored password is rejected, the
 * user is shown a system-modal dialog requesting a password.  Further
 * authentication attempts are repeated with user-provided passwords
 * until authentication is verified or the user dismisses the prompt.
 * The returned #EAuthenticationSessionResult indicates the outcome.
 *
 * If an error occurs while interacting with the secret service, or while
 * prompting the user for a password, or while attempting authentication,
 * the function sets @error and returns #E_AUTHENTICATION_SESSION_ERROR.
 *
 * Returns: the result of the authentication session
 *
 * Since: 3.6
 **/
EAuthenticationSessionResult
e_authentication_session_execute_sync (EAuthenticationSession *session,
                                       GCancellable *cancellable,
                                       GError **error)
{
	EAuthenticationSessionClass *class;

	g_return_val_if_fail (
		E_IS_AUTHENTICATION_SESSION (session),
		E_AUTHENTICATION_SESSION_DISMISSED);

	class = E_AUTHENTICATION_SESSION_GET_CLASS (session);
	g_return_val_if_fail (
		class->execute_sync != NULL,
		E_AUTHENTICATION_SESSION_DISMISSED);

	return class->execute_sync (session, cancellable, error);
}

/**
 * e_authentication_session_execute:
 * @session: an #EAuthenticationSession
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * See e_authentication_session_execute_sync() for details.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_authentication_session_execute_finish() to get the result of the
 * operation.
 *
 * Since: 3.6
 **/
void
e_authentication_session_execute (EAuthenticationSession *session,
                                  gint io_priority,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	EAuthenticationSessionClass *class;

	g_return_if_fail (E_IS_AUTHENTICATION_SESSION (session));

	class = E_AUTHENTICATION_SESSION_GET_CLASS (session);
	g_return_if_fail (class->execute != NULL);

	return class->execute (
		session, io_priority, cancellable, callback, user_data);
}

/**
 * e_authentication_session_execute_finish:
 * @session: an #EAuthenticationSession
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_authentication_session_execute().
 *
 * If an error occurs while interacting with the secret service, or while
 * prompting the user for a password, or while attempting authentication,
 * the function sets @error and returns #E_AUTHENTICATION_SESSION_ERROR.
 *
 * Returns: the result of the authentication session
 *
 * Since: 3.6
 **/
EAuthenticationSessionResult
e_authentication_session_execute_finish (EAuthenticationSession *session,
                                         GAsyncResult *result,
                                         GError **error)
{
	EAuthenticationSessionClass *class;

	g_return_val_if_fail (
		E_IS_AUTHENTICATION_SESSION (session),
		E_AUTHENTICATION_SESSION_DISMISSED);
	g_return_val_if_fail (
		G_IS_ASYNC_RESULT (result),
		E_AUTHENTICATION_SESSION_DISMISSED);

	class = E_AUTHENTICATION_SESSION_GET_CLASS (session);
	g_return_val_if_fail (
		class->execute_finish != NULL,
		E_AUTHENTICATION_SESSION_DISMISSED);

	return class->execute_finish (session, result, error);
}

/* Helper for e_authentication_session_store_password() */
static void
authentication_session_store_password_thread (GSimpleAsyncResult *simple,
                                              GObject *object,
                                              GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_authentication_session_store_password_sync (
		E_AUTHENTICATION_SESSION (object),
		async_context->password,
		async_context->permanently,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_authentication_session_store_password_sync:
 * @session: an #EAuthenticationSession
 * @password: the password to store
 * @permanently: store permanently or just for the session
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Store a password for the data source that @session is representing.
 * If @permanently is %TRUE, the password is stored in the default keyring.
 * Otherwise the password is stored in the memory-only session keyring.  If
 * an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.6
 *
 * Deprecated: 3.12: Use e_source_store_password_sync() instead.
 **/
gboolean
e_authentication_session_store_password_sync (EAuthenticationSession *session,
                                              const gchar *password,
                                              gboolean permanently,
                                              GCancellable *cancellable,
                                              GError **error)
{
	ESourceRegistryServer *server;
	ESource *source;
	const gchar *uid;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_AUTHENTICATION_SESSION (session), FALSE);

	server = e_authentication_session_get_server (session);
	uid = e_authentication_session_get_source_uid (session);

	/* Try to use the registered ESource instance,
	 * otherwise create a phony "scratch" source. */

	source = e_source_registry_server_ref_source (server, uid);

	if (source == NULL)
		source = e_source_new_with_uid (uid, NULL, error);

	if (source != NULL) {
		success = e_source_store_password_sync (
			source, password, permanently, cancellable, error);
		g_object_unref (source);
	}

	return success;
}

/**
 * e_authentication_session_store_password:
 * @session: an #EAuthenticationSession
 * @password: the password to store
 * @permanently: store permanently or just for the session
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously stores a password for the data source that @session
 * is representing.  If @permanently is %TRUE, the password is stored in the
 * default keyring.  Otherwise the password is stored in the memory-only
 * session keyring.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_authentication_session_store_password_finish() to get the result
 * of the operation.
 *
 * Since: 3.6
 *
 * Deprecated: 3.12: Use e_source_store_password() instead.
 **/
void
e_authentication_session_store_password (EAuthenticationSession *session,
                                         const gchar *password,
                                         gboolean permanently,
                                         gint io_priority,
                                         GCancellable *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_AUTHENTICATION_SESSION (session));
	g_return_if_fail (password != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->password = g_strdup (password);
	async_context->permanently = permanently;

	simple = g_simple_async_result_new (
		G_OBJECT (session), callback, user_data,
		e_authentication_session_store_password);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, authentication_session_store_password_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

/**
 * e_authentication_session_store_password_finish:
 * @session: an #EAuthenticationSession
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finished the operation started with
 * e_authentication_session_store_password().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.6
 *
 * Deprecated: 3.12: Use e_source_store_password_finish() instead.
 **/
gboolean
e_authentication_session_store_password_finish (EAuthenticationSession *session,
                                                GAsyncResult *result,
                                                GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (session),
		e_authentication_session_store_password), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/* Helper for e_authentication_session_store_password() */
static void
authentication_session_lookup_password_thread (GSimpleAsyncResult *simple,
                                               GObject *object,
                                               GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_authentication_session_lookup_password_sync (
		E_AUTHENTICATION_SESSION (object), cancellable,
		&async_context->password, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_authentication_session_lookup_password_sync:
 * @session: an #EAuthenticationSession
 * @cancellable: optional #GCancellable object, or %NULL
 * @password: return location for the password, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Looks up a password for the data source that @session is
 * representing.  Both the default and session keyrings are queried.
 *
 * Note the boolean return value indicates whether the lookup operation
 * itself completed successfully, not whether a password was found.  If
 * no password was found, the function will set @password to %NULL but
 * still return %TRUE.  If an error occurs, the function sets @error
 * and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.6
 *
 * Deprecated: 3.12: Use e_source_lookup_password_sync() instead.
 **/
gboolean
e_authentication_session_lookup_password_sync (EAuthenticationSession *session,
                                               GCancellable *cancellable,
                                               gchar **password,
                                               GError **error)
{
	ESourceRegistryServer *server;
	ESource *source;
	const gchar *uid;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_AUTHENTICATION_SESSION (session), FALSE);

	server = e_authentication_session_get_server (session);
	uid = e_authentication_session_get_source_uid (session);

	/* Try to use the registered ESource instance,
	 * otherwise create a phony "scratch" source. */

	source = e_source_registry_server_ref_source (server, uid);

	if (source == NULL)
		source = e_source_new_with_uid (uid, NULL, error);

	if (source != NULL) {
		success = e_source_lookup_password_sync (
			source, cancellable, password, error);
		g_object_unref (source);
	}

	return success;
}

/**
 * e_authentication_session_lookup_password:
 * @session: an #EAuthenticationSession
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously looks up a password for the data source that @session
 * is representing.  Both the default and session keyrings are queried.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_authentication_session_lookup_password_finish() to get the
 * result of the operation.
 *
 * Since: 3.6
 *
 * Deprecated: 3.12: Use e_source_lookup_password() instead.
 **/
void
e_authentication_session_lookup_password (EAuthenticationSession *session,
                                          gint io_priority,
                                          GCancellable *cancellable,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_AUTHENTICATION_SESSION (session));

	async_context = g_slice_new0 (AsyncContext);

	simple = g_simple_async_result_new (
		G_OBJECT (session), callback, user_data,
		e_authentication_session_lookup_password);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, authentication_session_lookup_password_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

/**
 * e_authentication_session_lookup_password_finish:
 * @session: an #EAuthenticationSession
 * @result: a #GAsyncResult
 * @password: return location for the password, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with
 * e_authentication_session_lookup_password().
 *
 * Note the boolean return value indicates whether the lookup operation
 * itself completed successfully, not whether a password was found.  If
 * no password was found, the function will set @password to %NULL but
 * still return %TRUE.  If an error occurs, the function sets @error
 * and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.6
 *
 * Deprecated: 3.12: Use e_source_lookup_password_finish() instead.
 **/
gboolean
e_authentication_session_lookup_password_finish (EAuthenticationSession *session,
                                                 GAsyncResult *result,
                                                 gchar **password,
                                                 GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (session),
		e_authentication_session_lookup_password), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	if (password != NULL) {
		*password = async_context->password;
		async_context->password = NULL;
	}

	return TRUE;
}

/* Helper for e_authentication_session_delete_password() */
static void
authentication_session_delete_password_thread (GSimpleAsyncResult *simple,
                                               GObject *object,
                                               GCancellable *cancellable)
{
	GError *error = NULL;

	e_authentication_session_delete_password_sync (
		E_AUTHENTICATION_SESSION (object), cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_authentication_session_delete_password_sync:
 * @session: an #EAuthenticationSession
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Deletes the password for the data source that @session is
 * representing from either the default keyring or session keyring.
 *
 * Note the boolean return value indicates whether the delete operation
 * itself completed successfully, not whether a password was found and
 * deleted.  If no password was found, the function will still return
 * %TRUE.  If an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.6
 *
 * Deprecated: 3.12: Use e_source_delete_password_sync() instead.
 **/
gboolean
e_authentication_session_delete_password_sync (EAuthenticationSession *session,
                                               GCancellable *cancellable,
                                               GError **error)
{
	ESourceRegistryServer *server;
	ESource *source;
	const gchar *uid;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_AUTHENTICATION_SESSION (session), FALSE);

	server = e_authentication_session_get_server (session);
	uid = e_authentication_session_get_source_uid (session);

	/* Try to use the registered ESource instance,
	 * otherwise create a phony "scratch" source. */

	source = e_source_registry_server_ref_source (server, uid);

	if (source == NULL)
		source = e_source_new_with_uid (uid, NULL, error);

	if (source != NULL) {
		success = e_source_delete_password_sync (
			source, cancellable, error);
		g_object_unref (source);
	}

	return success;
}

/**
 * e_authentication_session_delete_password:
 * @session: an #EAuthenticationSession
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asyncronously deletes the password for the data source that @session
 * is representing from either the default keyring or session keyring.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_authentication_session_delete_password_finish() to get the result
 * of the operation.
 *
 * Since: 3.6
 *
 * Deprecated: 3.12: Use e_source_delete_password() instead.
 **/
void
e_authentication_session_delete_password (EAuthenticationSession *session,
                                          gint io_priority,
                                          GCancellable *cancellable,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data)
{
	GSimpleAsyncResult *simple;

	g_return_if_fail (E_IS_AUTHENTICATION_SESSION (session));

	simple = g_simple_async_result_new (
		G_OBJECT (session), callback, user_data,
		e_authentication_session_delete_password);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_run_in_thread (
		simple, authentication_session_delete_password_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

/**
 * e_authentication_session_delete_password_finish:
 * @session: an #EAuthenticationSession
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with
 * e_authentication_session_delete_password().
 *
 * Note the boolean return value indicates whether the delete operation
 * itself completed successfully, not whether a password was found and
 * deleted.  If no password was found, the function will still return
 * %TRUE.  If an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.6
 *
 * Deprecated: 3.12: Use e_source_delete_password_finish() instead.
 **/
gboolean
e_authentication_session_delete_password_finish (EAuthenticationSession *session,
                                                 GAsyncResult *result,
                                                 GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (session),
		e_authentication_session_delete_password), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}


/*
 * e-book-client.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Copyright (C) 2011 Red Hat, Inc. (www.redhat.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <gio/gio.h>

/* Private D-Bus classes. */
#include <e-dbus-address-book.h>
#include <e-dbus-address-book-factory.h>

#include <libedataserver/libedataserver.h>
#include <libedataserver/e-client-private.h>

#include "e-book-client.h"
#include "e-contact.h"
#include "e-name-western.h"

#define E_BOOK_CLIENT_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BOOK_CLIENT, EBookClientPrivate))

/* Set this to a sufficiently large value
 * to cover most long-running operations. */
#define DBUS_PROXY_TIMEOUT_MS (3 * 60 * 1000)  /* 3 minutes */

typedef struct _AsyncContext AsyncContext;
typedef struct _SignalClosure SignalClosure;
typedef struct _RunInThreadClosure RunInThreadClosure;

struct _EBookClientPrivate {
	EDBusAddressBook *dbus_proxy;
	GMainContext *main_context;
	guint gone_signal_id;

	gulong dbus_proxy_error_handler_id;
	gulong dbus_proxy_notify_handler_id;
};

struct _AsyncContext {
	EContact *contact;
	EBookClientView *client_view;
	GSList *object_list;
	GSList *string_list;
	gchar *sexp;
	gchar *uid;
};

struct _SignalClosure {
	EClient *client;
	gchar *property_name;
	gchar *error_message;
};

struct _RunInThreadClosure {
	GSimpleAsyncThreadFunc func;
	GSimpleAsyncResult *simple;
	GCancellable *cancellable;
};

/* Forward Declarations */
static void	e_book_client_initable_init
					(GInitableIface *interface);
static void	e_book_client_async_initable_init
					(GAsyncInitableIface *interface);

G_DEFINE_TYPE_WITH_CODE (
	EBookClient,
	e_book_client,
	E_TYPE_CLIENT,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_book_client_initable_init)
	G_IMPLEMENT_INTERFACE (
		G_TYPE_ASYNC_INITABLE,
		e_book_client_async_initable_init))

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->contact != NULL)
		g_object_unref (async_context->contact);

	if (async_context->client_view != NULL)
		g_object_unref (async_context->client_view);

	g_slist_free_full (
		async_context->object_list,
		(GDestroyNotify) g_object_unref);

	g_slist_free_full (
		async_context->string_list,
		(GDestroyNotify) g_free);

	g_free (async_context->sexp);
	g_free (async_context->uid);

	g_slice_free (AsyncContext, async_context);
}

static void
signal_closure_free (SignalClosure *signal_closure)
{
	g_object_unref (signal_closure->client);

	g_free (signal_closure->property_name);
	g_free (signal_closure->error_message);

	g_slice_free (SignalClosure, signal_closure);
}

static void
run_in_thread_closure_free (RunInThreadClosure *run_in_thread_closure)
{
	if (run_in_thread_closure->simple != NULL)
		g_object_unref (run_in_thread_closure->simple);

	if (run_in_thread_closure->cancellable != NULL)
		g_object_unref (run_in_thread_closure->cancellable);

	g_slice_free (RunInThreadClosure, run_in_thread_closure);
}

/*
 * Well-known book backend properties:
 * @BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS: Retrieves comma-separated list
 *   of required fields by the backend. Use e_client_util_parse_comma_strings()
 *   to parse returned string value into a #GSList. These fields are required
 *   to be filled in for all contacts.
 * @BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS: Retrieves comma-separated list
 *   of supported fields by the backend. Use e_client_util_parse_comma_strings()
 *   to parse returned string value into a #GSList. These fields can be
 *   stored for contacts.
 *
 * See also: @CLIENT_BACKEND_PROPERTY_OPENED, @CLIENT_BACKEND_PROPERTY_OPENING,
 *   @CLIENT_BACKEND_PROPERTY_ONLINE, @CLIENT_BACKEND_PROPERTY_READONLY
 *   @CLIENT_BACKEND_PROPERTY_CACHE_DIR, @CLIENT_BACKEND_PROPERTY_CAPABILITIES
 */

G_DEFINE_QUARK (e-book-client-error-quark, e_book_client_error)

/**
 * e_book_client_error_to_string:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
const gchar *
e_book_client_error_to_string (EBookClientError code)
{
	switch (code) {
	case E_BOOK_CLIENT_ERROR_NO_SUCH_BOOK:
		return _("No such book");
	case E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND:
		return _("Contact not found");
	case E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS:
		return _("Contact ID already exists");
	case E_BOOK_CLIENT_ERROR_NO_SUCH_SOURCE:
		return _("No such source");
	case E_BOOK_CLIENT_ERROR_NO_SPACE:
		return _("No space");
	}

	return _("Unknown error");
}

/**
 * e_book_client_error_create:
 * @code: an #EBookClientError code to create
 * @custom_msg: custom message to use for the error; can be %NULL
 *
 * Returns: a new #GError containing an E_BOOK_CLIENT_ERROR of the given
 * @code. If the @custom_msg is NULL, then the error message is
 * the one returned from e_book_client_error_to_string() for the @code,
 * otherwise the given message is used.
 *
 * Returned pointer should be freed with g_error_free().
 *
 * Since: 3.2
 **/
GError *
e_book_client_error_create (EBookClientError code,
                            const gchar *custom_msg)
{
	return g_error_new_literal (E_BOOK_CLIENT_ERROR, code, custom_msg ? custom_msg : e_book_client_error_to_string (code));
}

/*
 * If the specified GError is a remote error, then create a new error
 * representing the remote error.  If the error is anything else, then
 * leave it alone.
 */
static gboolean
unwrap_dbus_error (GError *error,
                   GError **client_error)
{
	#define err(a,b) "org.gnome.evolution.dataserver.AddressBook." a, b
	static EClientErrorsList book_errors[] = {
		{ err ("Success",				-1) },
		{ err ("ContactNotFound",			E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND) },
		{ err ("ContactIDAlreadyExists",		E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS) },
		{ err ("NoSuchBook",				E_BOOK_CLIENT_ERROR_NO_SUCH_BOOK) },
		{ err ("BookRemoved",				E_BOOK_CLIENT_ERROR_NO_SUCH_SOURCE) },
		{ err ("NoSpace",				E_BOOK_CLIENT_ERROR_NO_SPACE) }
	}, cl_errors[] = {
		{ err ("Busy",					E_CLIENT_ERROR_BUSY) },
		{ err ("RepositoryOffline",			E_CLIENT_ERROR_REPOSITORY_OFFLINE) },
		{ err ("OfflineUnavailable",			E_CLIENT_ERROR_OFFLINE_UNAVAILABLE) },
		{ err ("PermissionDenied",			E_CLIENT_ERROR_PERMISSION_DENIED) },
		{ err ("AuthenticationFailed",			E_CLIENT_ERROR_AUTHENTICATION_FAILED) },
		{ err ("AuthenticationRequired",		E_CLIENT_ERROR_AUTHENTICATION_REQUIRED) },
		{ err ("CouldNotCancel",			E_CLIENT_ERROR_COULD_NOT_CANCEL) },
		{ err ("InvalidArg",				E_CLIENT_ERROR_INVALID_ARG) },
		{ err ("NotSupported",				E_CLIENT_ERROR_NOT_SUPPORTED) },
		{ err ("UnsupportedAuthenticationMethod",	E_CLIENT_ERROR_UNSUPPORTED_AUTHENTICATION_METHOD) },
		{ err ("TLSNotAvailable",			E_CLIENT_ERROR_TLS_NOT_AVAILABLE) },
		{ err ("SearchSizeLimitExceeded",		E_CLIENT_ERROR_SEARCH_SIZE_LIMIT_EXCEEDED) },
		{ err ("SearchTimeLimitExceeded",		E_CLIENT_ERROR_SEARCH_TIME_LIMIT_EXCEEDED) },
		{ err ("InvalidQuery",				E_CLIENT_ERROR_INVALID_QUERY) },
		{ err ("QueryRefused",				E_CLIENT_ERROR_QUERY_REFUSED) },
		{ err ("NotOpened",				E_CLIENT_ERROR_NOT_OPENED) },
		{ err ("UnsupportedField",			E_CLIENT_ERROR_OTHER_ERROR) },
		{ err ("InvalidServerVersion",			E_CLIENT_ERROR_OTHER_ERROR) },
		{ err ("OutOfSync",			        E_CLIENT_ERROR_OUT_OF_SYNC) },
		{ err ("OtherError",				E_CLIENT_ERROR_OTHER_ERROR) }
	};
	#undef err

	if (error == NULL)
		return TRUE;

	if (!e_client_util_unwrap_dbus_error (error, client_error, book_errors, G_N_ELEMENTS (book_errors), E_BOOK_CLIENT_ERROR, TRUE))
		e_client_util_unwrap_dbus_error (error, client_error, cl_errors, G_N_ELEMENTS (cl_errors), E_CLIENT_ERROR, FALSE);

	return FALSE;
}

static void
set_proxy_gone_error (GError **error)
{
	/* do not translate this string, it should ideally never happen */
	g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_DBUS_ERROR, "D-Bus book proxy gone");
}

static guint active_book_clients = 0, book_connection_closed_id = 0;
static EDBusAddressBookFactory *book_factory = NULL;
static GRecMutex book_factory_lock;
#define LOCK_FACTORY()   g_rec_mutex_lock (&book_factory_lock)
#define UNLOCK_FACTORY() g_rec_mutex_unlock (&book_factory_lock)

static void gdbus_book_factory_closed_cb (GDBusConnection *connection, gboolean remote_peer_vanished, GError *error, gpointer user_data);

static void
gdbus_book_factory_disconnect (GDBusConnection *connection)
{
	LOCK_FACTORY ();

	if (!connection && book_factory)
		connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (book_factory));

	if (connection && book_connection_closed_id) {
		g_dbus_connection_signal_unsubscribe (connection, book_connection_closed_id);
		g_signal_handlers_disconnect_by_func (connection, gdbus_book_factory_closed_cb, NULL);
	}

	if (book_factory != NULL)
		g_object_unref (book_factory);

	book_connection_closed_id = 0;
	book_factory = NULL;

	UNLOCK_FACTORY ();
}

static void
gdbus_book_factory_closed_cb (GDBusConnection *connection,
                              gboolean remote_peer_vanished,
                              GError *error,
                              gpointer user_data)
{
	GError *err = NULL;

	LOCK_FACTORY ();

	gdbus_book_factory_disconnect (connection);

	if (error)
		unwrap_dbus_error (g_error_copy (error), &err);

	if (err) {
		g_debug ("GDBus connection is closed%s: %s", remote_peer_vanished ? ", remote peer vanished" : "", err->message);
		g_error_free (err);
	} else if (active_book_clients) {
		g_debug ("GDBus connection is closed%s", remote_peer_vanished ? ", remote peer vanished" : "");
	}

	UNLOCK_FACTORY ();
}

static void
gdbus_book_factory_connection_gone_cb (GDBusConnection *connection,
                                       const gchar *sender_name,
                                       const gchar *object_path,
                                       const gchar *interface_name,
                                       const gchar *signal_name,
                                       GVariant *parameters,
                                       gpointer user_data)
{
	/* signal subscription takes care of correct parameters,
	 * thus just do what is to be done here */
	gdbus_book_factory_closed_cb (connection, TRUE, NULL, user_data);
}

static gboolean
gdbus_book_factory_activate (GCancellable *cancellable,
                             GError **error)
{
	GDBusConnection *connection;

	LOCK_FACTORY ();

	if (G_LIKELY (book_factory != NULL)) {
		UNLOCK_FACTORY ();
		return TRUE;
	}

	book_factory = e_dbus_address_book_factory_proxy_new_for_bus_sync (
		G_BUS_TYPE_SESSION,
		G_DBUS_PROXY_FLAGS_NONE,
		ADDRESS_BOOK_DBUS_SERVICE_NAME,
		"/org/gnome/evolution/dataserver/AddressBookFactory",
		cancellable, error);

	if (book_factory == NULL) {
		UNLOCK_FACTORY ();
		return FALSE;
	}

	connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (book_factory));
	book_connection_closed_id = g_dbus_connection_signal_subscribe (
		connection,
		NULL,						/* sender */
		"org.freedesktop.DBus",				/* interface */
		"NameOwnerChanged",				/* member */
		"/org/freedesktop/DBus",			/* object_path */
		"org.gnome.evolution.dataserver.AddressBook",	/* arg0 */
		G_DBUS_SIGNAL_FLAGS_NONE,
		gdbus_book_factory_connection_gone_cb, NULL, NULL);

	g_signal_connect (
		connection, "closed",
		G_CALLBACK (gdbus_book_factory_closed_cb), NULL);

	UNLOCK_FACTORY ();

	return TRUE;
}

static gpointer
book_client_dbus_thread (gpointer user_data)
{
	GMainContext *main_context = user_data;
	GMainLoop *main_loop;

	g_main_context_push_thread_default (main_context);

	main_loop = g_main_loop_new (main_context, FALSE);
	g_main_loop_run (main_loop);
	g_main_loop_unref (main_loop);

	g_main_context_pop_thread_default (main_context);

	g_main_context_unref (main_context);

	return NULL;
}

static gpointer
book_client_dbus_thread_init (gpointer unused)
{
	GMainContext *main_context;

	main_context = g_main_context_new ();

	/* This thread terminates when the process itself terminates, so
	 * no need to worry about unreferencing the returned GThread. */
	g_thread_new (
		"book-client-dbus-thread",
		book_client_dbus_thread,
		g_main_context_ref (main_context));

	return main_context;
}

static GMainContext *
book_client_ref_dbus_main_context (void)
{
	static GOnce book_client_dbus_thread_once = G_ONCE_INIT;

	g_once (
		&book_client_dbus_thread_once,
		book_client_dbus_thread_init, NULL);

	return g_main_context_ref (book_client_dbus_thread_once.retval);
}

static gboolean
book_client_run_in_dbus_thread_idle_cb (gpointer user_data)
{
	RunInThreadClosure *closure = user_data;
	GObject *source_object;
	GAsyncResult *result;

	result = G_ASYNC_RESULT (closure->simple);
	source_object = g_async_result_get_source_object (result);

	closure->func (
		closure->simple,
		source_object,
		closure->cancellable);

	if (source_object != NULL)
		g_object_unref (source_object);

	g_simple_async_result_complete_in_idle (closure->simple);

	return FALSE;
}

static void
book_client_run_in_dbus_thread (GSimpleAsyncResult *simple,
                                GSimpleAsyncThreadFunc func,
                                gint io_priority,
                                GCancellable *cancellable)
{
	RunInThreadClosure *closure;
	GMainContext *main_context;
	GSource *idle_source;

	main_context = book_client_ref_dbus_main_context ();

	closure = g_slice_new0 (RunInThreadClosure);
	closure->func = func;
	closure->simple = g_object_ref (simple);

	if (G_IS_CANCELLABLE (cancellable))
		closure->cancellable = g_object_ref (cancellable);

	idle_source = g_idle_source_new ();
	g_source_set_priority (idle_source, io_priority);
	g_source_set_callback (
		idle_source, book_client_run_in_dbus_thread_idle_cb,
		closure, (GDestroyNotify) run_in_thread_closure_free);
	g_source_attach (idle_source, main_context);
	g_source_unref (idle_source);

	g_main_context_unref (main_context);
}

static void gdbus_book_client_disconnect (EBookClient *client);

/*
 * Called when the addressbook server dies.
 */
static void
gdbus_book_client_closed_cb (GDBusConnection *connection,
                             gboolean remote_peer_vanished,
                             GError *error,
                             EBookClient *client)
{
	GError *err = NULL;

	g_assert (E_IS_BOOK_CLIENT (client));

	if (error)
		unwrap_dbus_error (g_error_copy (error), &err);

	if (err) {
		g_debug (G_STRLOC ": EBookClient GDBus connection is closed%s: %s", remote_peer_vanished ? ", remote peer vanished" : "", err->message);
		g_error_free (err);
	} else {
		g_debug (G_STRLOC ": EBookClient GDBus connection is closed%s", remote_peer_vanished ? ", remote peer vanished" : "");
	}

	gdbus_book_client_disconnect (client);

	e_client_emit_backend_died (E_CLIENT (client));
}

static void
gdbus_book_client_connection_gone_cb (GDBusConnection *connection,
                                      const gchar *sender_name,
                                      const gchar *object_path,
                                      const gchar *interface_name,
                                      const gchar *signal_name,
                                      GVariant *parameters,
                                      gpointer user_data)
{
	/* signal subscription takes care of correct parameters,
	 * thus just do what is to be done here */
	gdbus_book_client_closed_cb (connection, TRUE, NULL, user_data);
}

static void
gdbus_book_client_disconnect (EBookClient *client)
{
	g_return_if_fail (E_IS_BOOK_CLIENT (client));

	/* Ensure that everything relevant is NULL */
	LOCK_FACTORY ();

	if (client->priv->dbus_proxy != NULL) {
		GDBusConnection *connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (client->priv->dbus_proxy));

		g_signal_handlers_disconnect_by_func (connection, gdbus_book_client_closed_cb, client);
		g_dbus_connection_signal_unsubscribe (connection, client->priv->gone_signal_id);
		client->priv->gone_signal_id = 0;

		e_dbus_address_book_call_close_sync (
			client->priv->dbus_proxy, NULL, NULL);
		g_object_unref (client->priv->dbus_proxy);
		client->priv->dbus_proxy = NULL;
	}

	UNLOCK_FACTORY ();
}

static gboolean
book_client_emit_backend_error_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;

	g_signal_emit_by_name (
		signal_closure->client,
		"backend-error",
		signal_closure->error_message);

	return FALSE;
}

static gboolean
book_client_emit_backend_property_changed_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	gchar *prop_value = NULL;

	/* XXX Despite appearances, this function does not block. */
	e_client_get_backend_property_sync (
		signal_closure->client,
		signal_closure->property_name,
		&prop_value, NULL, NULL);

	if (prop_value != NULL) {
		g_signal_emit_by_name (
			signal_closure->client,
			"backend-property-changed",
			signal_closure->property_name,
			prop_value);
		g_free (prop_value);
	}

	return FALSE;
}

static void
book_client_dbus_proxy_error_cb (EDBusAddressBook *dbus_proxy,
                                 const gchar *error_message,
                                 EBookClient *book_client)
{
	GSource *idle_source;
	SignalClosure *signal_closure;

	signal_closure = g_slice_new0 (SignalClosure);
	signal_closure->client = g_object_ref (book_client);
	signal_closure->error_message = g_strdup (error_message);

	idle_source = g_idle_source_new ();
	g_source_set_callback (
		idle_source,
		book_client_emit_backend_error_idle_cb,
		signal_closure,
		(GDestroyNotify) signal_closure_free);
	g_source_attach (idle_source, book_client->priv->main_context);
	g_source_unref (idle_source);
}

static void
book_client_dbus_proxy_notify_cb (EDBusAddressBook *dbus_proxy,
                                  GParamSpec *pspec,
                                  EBookClient *book_client)
{
	const gchar *backend_prop_name = NULL;

	if (g_str_equal (pspec->name, "cache-dir")) {
		backend_prop_name = CLIENT_BACKEND_PROPERTY_CACHE_DIR;
	}

	if (g_str_equal (pspec->name, "capabilities")) {
		gchar **strv;
		gchar *csv;

		backend_prop_name = CLIENT_BACKEND_PROPERTY_CAPABILITIES;

		strv = e_dbus_address_book_dup_capabilities (dbus_proxy);
		csv = g_strjoinv (",", strv);
		e_client_set_capabilities (E_CLIENT (book_client), csv);
		g_free (csv);
		g_free (strv);
	}

	if (g_str_equal (pspec->name, "online")) {
		gboolean online;

		backend_prop_name = CLIENT_BACKEND_PROPERTY_ONLINE;

		online = e_dbus_address_book_get_online (dbus_proxy);
		e_client_set_online (E_CLIENT (book_client), online);
	}

	if (g_str_equal (pspec->name, "required-fields")) {
		backend_prop_name = BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS;
	}

	if (g_str_equal (pspec->name, "revision")) {
		backend_prop_name = CLIENT_BACKEND_PROPERTY_REVISION;
	}

	if (g_str_equal (pspec->name, "supported-fields")) {
		backend_prop_name = BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS;
	}

	if (g_str_equal (pspec->name, "writable")) {
		gboolean writable;

		backend_prop_name = CLIENT_BACKEND_PROPERTY_READONLY;

		writable = e_dbus_address_book_get_writable (dbus_proxy);
		e_client_set_readonly (E_CLIENT (book_client), !writable);
	}

	if (backend_prop_name != NULL) {
		GSource *idle_source;
		SignalClosure *signal_closure;

		signal_closure = g_slice_new0 (SignalClosure);
		signal_closure->client = g_object_ref (book_client);
		signal_closure->property_name = g_strdup (backend_prop_name);

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			book_client_emit_backend_property_changed_idle_cb,
			signal_closure,
			(GDestroyNotify) signal_closure_free);
		g_source_attach (idle_source, book_client->priv->main_context);
		g_source_unref (idle_source);
	}
}

static void
book_client_dispose (GObject *object)
{
	EBookClientPrivate *priv;

	priv = E_BOOK_CLIENT_GET_PRIVATE (object);

	e_client_cancel_all (E_CLIENT (object));

	if (priv->dbus_proxy_error_handler_id > 0) {
		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->dbus_proxy_error_handler_id);
		priv->dbus_proxy_error_handler_id = 0;
	}

	if (priv->dbus_proxy_notify_handler_id > 0) {
		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->dbus_proxy_notify_handler_id);
		priv->dbus_proxy_notify_handler_id = 0;
	}

	gdbus_book_client_disconnect (E_BOOK_CLIENT (object));

	if (priv->main_context != NULL) {
		g_main_context_unref (priv->main_context);
		priv->main_context = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_book_client_parent_class)->dispose (object);
}

static void
book_client_finalize (GObject *object)
{
	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_client_parent_class)->finalize (object);

	LOCK_FACTORY ();
	active_book_clients--;
	if (!active_book_clients)
		gdbus_book_factory_disconnect (NULL);
	UNLOCK_FACTORY ();
}

static GDBusProxy *
book_client_get_dbus_proxy (EClient *client)
{
	EBookClientPrivate *priv;

	priv = E_BOOK_CLIENT_GET_PRIVATE (client);

	return G_DBUS_PROXY (priv->dbus_proxy);
}

static void
book_client_unwrap_dbus_error (EClient *client,
                               GError *dbus_error,
                               GError **out_error)
{
	unwrap_dbus_error (dbus_error, out_error);
}

static gboolean
book_client_retrieve_capabilities_sync (EClient *client,
                                        gchar **capabilities,
                                        GCancellable *cancellable,
                                        GError **error)
{
	return e_client_get_backend_property_sync (
		client, CLIENT_BACKEND_PROPERTY_CAPABILITIES,
		capabilities, cancellable, error);
}

static gboolean
book_client_get_backend_property_sync (EClient *client,
                                       const gchar *prop_name,
                                       gchar **prop_value,
                                       GCancellable *cancellable,
                                       GError **error)
{
	EBookClient *book_client;
	EDBusAddressBook *dbus_proxy;
	gchar **strv;

	book_client = E_BOOK_CLIENT (client);
	dbus_proxy = book_client->priv->dbus_proxy;

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_OPENED)) {
		*prop_value = g_strdup ("TRUE");
		return TRUE;
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_OPENING)) {
		*prop_value = g_strdup ("FALSE");
		return TRUE;
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_ONLINE)) {
		if (e_dbus_address_book_get_online (dbus_proxy))
			*prop_value = g_strdup ("TRUE");
		else
			*prop_value = g_strdup ("FALSE");
		return TRUE;
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_READONLY)) {
		if (e_dbus_address_book_get_writable (dbus_proxy))
			*prop_value = g_strdup ("FALSE");
		else
			*prop_value = g_strdup ("TRUE");
		return TRUE;
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CACHE_DIR)) {
		*prop_value = e_dbus_address_book_dup_cache_dir (dbus_proxy);
		return TRUE;
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_REVISION)) {
		*prop_value = e_dbus_address_book_dup_revision (dbus_proxy);
		return TRUE;
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		strv = e_dbus_address_book_dup_capabilities (dbus_proxy);
		*prop_value = g_strjoinv (",", strv);
		g_strfreev (strv);
		return TRUE;
	}

	if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		strv = e_dbus_address_book_dup_required_fields (dbus_proxy);
		*prop_value = g_strjoinv (",", strv);
		g_strfreev (strv);
		return TRUE;
	}

	if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		strv = e_dbus_address_book_dup_supported_fields (dbus_proxy);
		*prop_value = g_strjoinv (",", strv);
		g_strfreev (strv);
		return TRUE;
	}

	g_set_error (
		error, E_CLIENT_ERROR, E_CLIENT_ERROR_NOT_SUPPORTED,
		_("Unknown book property '%s'"), prop_name);

	return TRUE;
}

static gboolean
book_client_set_backend_property_sync (EClient *client,
                                       const gchar *prop_name,
                                       const gchar *prop_value,
                                       GCancellable *cancellable,
                                       GError **error)
{
	g_set_error (
		error, E_CLIENT_ERROR,
		E_CLIENT_ERROR_NOT_SUPPORTED,
		_("Cannot change value of book property '%s'"),
		prop_name);

	return FALSE;
}

static gboolean
book_client_open_sync (EClient *client,
                       gboolean only_if_exists,
                       GCancellable *cancellable,
                       GError **error)
{
	EBookClient *book_client;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	book_client = E_BOOK_CLIENT (client);

	if (book_client->priv->dbus_proxy == NULL) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	return e_dbus_address_book_call_open_sync (
		book_client->priv->dbus_proxy, cancellable, error);
}

static gboolean
book_client_refresh_sync (EClient *client,
                          GCancellable *cancellable,
                          GError **error)
{
	EBookClient *book_client;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	book_client = E_BOOK_CLIENT (client);

	if (book_client->priv->dbus_proxy == NULL) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	return e_dbus_address_book_call_refresh_sync (
		book_client->priv->dbus_proxy, cancellable, error);
}

static void
book_client_init_in_dbus_thread (GSimpleAsyncResult *simple,
                                 GObject *source_object,
                                 GCancellable *cancellable)
{
	EBookClientPrivate *priv;
	EClient *client;
	ESource *source;
	GDBusConnection *connection;
	const gchar *uid;
	gchar *object_path = NULL;
	gulong handler_id;
	GError *error = NULL;

	priv = E_BOOK_CLIENT_GET_PRIVATE (source_object);

	client = E_CLIENT (source_object);
	source = e_client_get_source (client);
	uid = e_source_get_uid (source);

	LOCK_FACTORY ();
	gdbus_book_factory_activate (cancellable, &error);
	UNLOCK_FACTORY ();

	if (error != NULL) {
		unwrap_dbus_error (error, &error);
		g_simple_async_result_take_error (simple, error);
		return;
	}

	e_dbus_address_book_factory_call_open_address_book_sync (
		book_factory, uid, &object_path, cancellable, &error);

	/* Sanity check. */
	g_return_if_fail (
		((object_path != NULL) && (error == NULL)) ||
		((object_path == NULL) && (error != NULL)));

	if (object_path == NULL) {
		unwrap_dbus_error (error, &error);
		g_simple_async_result_take_error (simple, error);
		return;
	}

	connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (book_factory));

	priv->dbus_proxy = e_dbus_address_book_proxy_new_sync (
		connection,
		G_DBUS_PROXY_FLAGS_NONE,
		ADDRESS_BOOK_DBUS_SERVICE_NAME,
		object_path,
		cancellable, &error);

	g_free (object_path);

	/* Sanity check. */
	g_return_if_fail (
		((priv->dbus_proxy != NULL) && (error == NULL)) ||
		((priv->dbus_proxy == NULL) && (error != NULL)));

	if (error != NULL) {
		unwrap_dbus_error (error, &error);
		g_simple_async_result_take_error (simple, error);
		return;
	}

	g_dbus_proxy_set_default_timeout (
		G_DBUS_PROXY (priv->dbus_proxy), DBUS_PROXY_TIMEOUT_MS);

	priv->gone_signal_id = g_dbus_connection_signal_subscribe (
		connection,
		"org.freedesktop.DBus",				/* sender */
		"org.freedesktop.DBus",				/* interface */
		"NameOwnerChanged",				/* member */
		"/org/freedesktop/DBus",			/* object_path */
		"org.gnome.evolution.dataserver.AddressBook",	/* arg0 */
		G_DBUS_SIGNAL_FLAGS_NONE,
		gdbus_book_client_connection_gone_cb, client, NULL);

	g_signal_connect (
		connection, "closed",
		G_CALLBACK (gdbus_book_client_closed_cb), client);

	handler_id = g_signal_connect (
		priv->dbus_proxy, "error",
		G_CALLBACK (book_client_dbus_proxy_error_cb), client);
	priv->dbus_proxy_error_handler_id = handler_id;

	handler_id = g_signal_connect (
		priv->dbus_proxy, "notify",
		G_CALLBACK (book_client_dbus_proxy_notify_cb), client);
	priv->dbus_proxy_notify_handler_id = handler_id;
}

static gboolean
book_client_initable_init (GInitable *initable,
                           GCancellable *cancellable,
                           GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	closure = e_async_closure_new ();

	g_async_initable_init_async (
		G_ASYNC_INITABLE (initable),
		G_PRIORITY_DEFAULT, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = g_async_initable_init_finish (
		G_ASYNC_INITABLE (initable), result, error);

	e_async_closure_free (closure);

	return success;
}

static void
book_client_initable_init_async (GAsyncInitable *initable,
                                 gint io_priority,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (
		G_OBJECT (initable), callback, user_data,
		book_client_initable_init_async);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	book_client_run_in_dbus_thread (
		simple, book_client_init_in_dbus_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
book_client_initable_init_finish (GAsyncInitable *initable,
                                  GAsyncResult *result,
                                  GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (initable),
		book_client_initable_init_async), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
e_book_client_class_init (EBookClientClass *class)
{
	GObjectClass *object_class;
	EClientClass *client_class;

	g_type_class_add_private (class, sizeof (EBookClientPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = book_client_dispose;
	object_class->finalize = book_client_finalize;

	client_class = E_CLIENT_CLASS (class);
	client_class->get_dbus_proxy = book_client_get_dbus_proxy;
	client_class->unwrap_dbus_error = book_client_unwrap_dbus_error;
	client_class->retrieve_capabilities_sync = book_client_retrieve_capabilities_sync;
	client_class->get_backend_property_sync = book_client_get_backend_property_sync;
	client_class->set_backend_property_sync = book_client_set_backend_property_sync;
	client_class->open_sync = book_client_open_sync;
	client_class->refresh_sync = book_client_refresh_sync;
}

static void
e_book_client_initable_init (GInitableIface *interface)
{
	interface->init = book_client_initable_init;
}

static void
e_book_client_async_initable_init (GAsyncInitableIface *interface)
{
	interface->init_async = book_client_initable_init_async;
	interface->init_finish = book_client_initable_init_finish;
}

static void
e_book_client_init (EBookClient *client)
{
	LOCK_FACTORY ();
	active_book_clients++;
	UNLOCK_FACTORY ();

	client->priv = E_BOOK_CLIENT_GET_PRIVATE (client);

	/* This is so the D-Bus thread can schedule signal emissions
	 * on the thread-default context for this thread. */
	client->priv->main_context = g_main_context_ref_thread_default ();
}

/**
 * e_book_client_new:
 * @source: An #ESource pointer
 * @error: A #GError pointer
 *
 * Creates a new #EBookClient corresponding to the given source.  There are
 * only two operations that are valid on this book at this point:
 * e_client_open(), and e_client_remove().
 *
 * Returns: a new but unopened #EBookClient.
 *
 * Since: 3.2
 **/
EBookClient *
e_book_client_new (ESource *source,
                   GError **error)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return g_initable_new (
		E_TYPE_BOOK_CLIENT, NULL, error,
		"source", source, NULL);
}

#define SELF_UID_PATH_ID "org.gnome.evolution-data-server.addressbook"
#define SELF_UID_KEY "self-contact-uid"

static EContact *
make_me_card (void)
{
	GString *vcard;
	const gchar *s;
	EContact *contact;

	vcard = g_string_new ("BEGIN:VCARD\nVERSION:3.0\n");

	s = g_get_user_name ();
	if (s)
		g_string_append_printf (vcard, "NICKNAME:%s\n", s);

	s = g_get_real_name ();
	if (s && strcmp (s, "Unknown") != 0) {
		ENameWestern *western;

		g_string_append_printf (vcard, "FN:%s\n", s);

		western = e_name_western_parse (s);
		g_string_append_printf (
			vcard, "N:%s;%s;%s;%s;%s\n",
			western->last ? western->last : "",
			western->first ? western->first : "",
			western->middle ? western->middle : "",
			western->prefix ? western->prefix : "",
			western->suffix ? western->suffix : "");
		e_name_western_free (western);
	}
	g_string_append (vcard, "END:VCARD");

	contact = e_contact_new_from_vcard (vcard->str);

	g_string_free (vcard, TRUE);

	return contact;
}

/**
 * e_book_client_get_self:
 * @registry: an #ESourceRegistry
 * @out_contact: (out): an #EContact pointer to set
 * @out_client: (out): an #EBookClient pointer to set
 * @error: a #GError to set on failure
 *
 * Get the #EContact referring to the user of the address book
 * and set it in @out_contact and @out_client.
 *
 * Returns: %TRUE if successful, otherwise %FALSE.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_self (ESourceRegistry *registry,
                        EContact **out_contact,
                        EBookClient **out_client,
                        GError **error)
{
	EBookClient *book_client;
	ESource *source;
	EContact *contact = NULL;
	GSettings *settings;
	gchar *uid;
	gboolean success;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), FALSE);
	g_return_val_if_fail (out_contact != NULL, FALSE);
	g_return_val_if_fail (out_client != NULL, FALSE);

	source = e_source_registry_ref_builtin_address_book (registry);
	book_client = e_book_client_new (source, error);
	g_object_unref (source);

	if (book_client == NULL)
		return FALSE;

	success = e_client_open_sync (
		E_CLIENT (book_client), FALSE, NULL, error);
	if (!success) {
		g_object_unref (book_client);
		return FALSE;
	}

	*out_client = book_client;

	settings = g_settings_new (SELF_UID_PATH_ID);
	uid = g_settings_get_string (settings, SELF_UID_KEY);
	g_object_unref (settings);

	if (uid) {
		/* Don't care about errors because
		 * we'll create a new card on failure. */
		e_book_client_get_contact_sync (
			book_client, uid, &contact, NULL, NULL);
		g_free (uid);

		if (contact != NULL) {
			*out_client = book_client;
			*out_contact = contact;
			return TRUE;
		}
	}

	uid = NULL;
	contact = make_me_card ();
	success = e_book_client_add_contact_sync (
		book_client, contact, &uid, NULL, error);
	if (!success) {
		g_object_unref (book_client);
		g_object_unref (contact);
		return FALSE;
	}

	if (uid != NULL) {
		e_contact_set (contact, E_CONTACT_UID, uid);
		g_free (uid);
	}

	e_book_client_set_self (book_client, contact, NULL);

	*out_client = book_client;
	*out_contact = contact;

	return TRUE;
}

/**
 * e_book_client_set_self:
 * @client: an #EBookClient
 * @contact: an #EContact
 * @error: a #GError to set on failure
 *
 * Specify that @contact residing in @client is the #EContact that
 * refers to the user of the address book.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_set_self (EBookClient *client,
                        EContact *contact,
                        GError **error)
{
	GSettings *settings;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (contact != NULL, FALSE);
	g_return_val_if_fail (e_contact_get_const (contact, E_CONTACT_UID) != NULL, FALSE);

	settings = g_settings_new (SELF_UID_PATH_ID);
	g_settings_set_string (settings, SELF_UID_KEY, e_contact_get_const (contact, E_CONTACT_UID));
	g_object_unref (settings);

	return TRUE;
}

/**
 * e_book_client_is_self:
 * @contact: an #EContact
 *
 * Check if @contact is the user of the address book.
 *
 * Returns: %TRUE if @contact is the user, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_is_self (EContact *contact)
{
	GSettings *settings;
	gchar *uid;
	gboolean is_self;

	g_return_val_if_fail (contact && E_IS_CONTACT (contact), FALSE);

	settings = g_settings_new (SELF_UID_PATH_ID);
	uid = g_settings_get_string (settings, SELF_UID_KEY);
	g_object_unref (settings);

	is_self = uid && !g_strcmp0 (uid, e_contact_get_const (contact, E_CONTACT_UID));

	g_free (uid);

	return is_self;
}

/* Helper for e_book_client_add_contact() */
static void
book_client_add_contact_thread (GSimpleAsyncResult *simple,
                                GObject *source_object,
                                GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_book_client_add_contact_sync (
		E_BOOK_CLIENT (source_object),
		async_context->contact,
		&async_context->uid,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_book_client_add_contact:
 * @client: an #EBookClient
 * @contact: an #EContact
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Adds @contact to @client.
 * The call is finished by e_book_client_add_contact_finish()
 * from the @callback.
 *
 * Since: 3.2
 **/
void
e_book_client_add_contact (EBookClient *client,
                           EContact *contact,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (E_IS_CONTACT (contact));

	async_context = g_slice_new0 (AsyncContext);
	async_context->contact = g_object_ref (contact);

	simple = g_simple_async_result_new (
		G_OBJECT (client), callback, user_data,
		e_book_client_add_contact);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, book_client_add_contact_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_book_client_add_contact_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @out_added_uid: (out): UID of a newly added contact; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_add_contact() and
 * sets @out_added_uid to a UID of a newly added contact.
 * This string should be freed with g_free().
 *
 * Note: This is not modifying original #EContact.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_add_contact_finish (EBookClient *client,
                                  GAsyncResult *result,
                                  gchar **out_added_uid,
                                  GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (client),
		e_book_client_add_contact), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	g_return_val_if_fail (async_context->uid != NULL, FALSE);

	if (out_added_uid != NULL) {
		*out_added_uid = async_context->uid;
		async_context->uid = NULL;
	}

	return TRUE;
}

/**
 * e_book_client_add_contact_sync:
 * @client: an #EBookClient
 * @contact: an #EContact
 * @out_added_uid: (out): UID of a newly added contact; can be %NULL
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Adds @contact to @client and
 * sets @out_added_uid to a UID of a newly added contact.
 * This string should be freed with g_free().
 *
 * Note: This is not modifying original @contact, thus if it's needed,
 * then use e_contact_set (contact, E_CONTACT_UID, new_uid).
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_add_contact_sync (EBookClient *client,
                                EContact *contact,
                                gchar **out_added_uid,
                                GCancellable *cancellable,
                                GError **error)
{
	GSList link = { contact, NULL };
	GSList *uids = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	if (client->priv->dbus_proxy == NULL) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	success = e_book_client_add_contacts_sync (
		client, &link, &uids, cancellable, error);

	/* Sanity check. */
	g_return_val_if_fail (
		(success && (uids != NULL)) ||
		(!success && (uids == NULL)), FALSE);

	if (uids != NULL) {
		if (out_added_uid != NULL)
			*out_added_uid = g_strdup (uids->data);

		g_slist_free_full (uids, (GDestroyNotify) g_free);
	}

	return success;
}

/* Helper for e_book_client_add_contacts() */
static void
book_client_add_contacts_thread (GSimpleAsyncResult *simple,
                                 GObject *source_object,
                                 GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_book_client_add_contacts_sync (
		E_BOOK_CLIENT (source_object),
		async_context->object_list,
		&async_context->string_list,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_book_client_add_contacts:
 * @client: an #EBookClient
 * @contacts: (element-type EContact): a #GSList of #EContact objects to add
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Adds @contacts to @client.
 * The call is finished by e_book_client_add_contacts_finish()
 * from the @callback.
 *
 * Since: 3.4
 **/
void
e_book_client_add_contacts (EBookClient *client,
                            GSList *contacts,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (contacts != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->object_list = g_slist_copy_deep (
		contacts, (GCopyFunc) g_object_ref, NULL);

	simple = g_simple_async_result_new (
		G_OBJECT (client), callback, user_data,
		e_book_client_add_contacts);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, book_client_add_contacts_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_book_client_add_contacts_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @out_added_uids: (out) (element-type utf8) (allow-none): UIDs of
 *                  newly added contacts; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_add_contacts() and
 * sets @out_added_uids to the UIDs of newly added contacts if successful.
 * This #GSList should be freed with e_client_util_free_string_slist().
 *
 * If any of the contacts cannot be inserted, all of the insertions will be
 * reverted and this method will return %FALSE.
 *
 * Note: This is not modifying original #EContact objects.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.4
 **/
gboolean
e_book_client_add_contacts_finish (EBookClient *client,
                                   GAsyncResult *result,
                                   GSList **out_added_uids,
                                   GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (client),
		e_book_client_add_contacts), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	if (out_added_uids != NULL) {
		*out_added_uids = async_context->string_list;
		async_context->string_list = NULL;
	}

	return TRUE;
}

/**
 * e_book_client_add_contacts_sync:
 * @client: an #EBookClient
 * @contacts: (element-type EContact): a #GSList of #EContact objects to add
 * @out_added_uids: (out) (element-type utf8) (allow-none): UIDs of newly
 *                  added contacts; can be %NULL
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Adds @contacts to @client and
 * sets @out_added_uids to the UIDs of newly added contacts if successful.
 * This #GSList should be freed with e_client_util_free_string_slist().
 *
 * If any of the contacts cannot be inserted, all of the insertions will be
 * reverted and this method will return %FALSE.
 *
 * Note: This is not modifying original @contacts, thus if it's needed,
 * then use e_contact_set (contact, E_CONTACT_UID, new_uid).
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.4
 **/
gboolean
e_book_client_add_contacts_sync (EBookClient *client,
                                 GSList *contacts,
                                 GSList **out_added_uids,
                                 GCancellable *cancellable,
                                 GError **error)
{
	GSList *link;
	gchar **strv;
	gchar **uids = NULL;
	gboolean success;
	gint ii = 0;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (contacts != NULL, FALSE);

	if (client->priv->dbus_proxy == NULL) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	/* Build a string array, ensuring each element is valid UTF-8. */
	strv = g_new0 (gchar *, g_slist_length (contacts) + 1);
	for (link = contacts; link != NULL; link = g_slist_next (link)) {
		EVCard *vcard;
		gchar *string;

		vcard = E_VCARD (link->data);
		string = e_vcard_to_string (vcard, EVC_FORMAT_VCARD_30);
		strv[ii++] = e_util_utf8_make_valid (string);
		g_free (string);
	}

	success = e_dbus_address_book_call_create_contacts_sync (
		client->priv->dbus_proxy,
		(const gchar * const *) strv,
		&uids, cancellable, error);

	g_strfreev (strv);

	/* Sanity check. */
	g_return_val_if_fail (
		(success && (uids != NULL)) ||
		(!success && (uids == NULL)), FALSE);

	if (!success)
		return FALSE;

	/* XXX We should have passed the string array directly
	 *     back to the caller instead of building a linked
	 *     list.  This is unnecessary work. */
	if (out_added_uids != NULL) {
		GSList *tmp = NULL;
		gint ii;

		/* Take ownership of the string array elements. */
		for (ii = 0; uids[ii] != NULL; ii++) {
			tmp = g_slist_prepend (tmp, uids[ii]);
			uids[ii] = NULL;
		}

		*out_added_uids = g_slist_reverse (tmp);
	}

	g_strfreev (uids);

	return TRUE;
}

/* Helper for e_book_client_modify_contact() */
static void
book_client_modify_contact_thread (GSimpleAsyncResult *simple,
                                   GObject *source_object,
                                   GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_book_client_modify_contact_sync (
		E_BOOK_CLIENT (source_object),
		async_context->contact,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_book_client_modify_contact:
 * @client: an #EBookClient
 * @contact: an #EContact
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Applies the changes made to @contact to the stored version in @client.
 * The call is finished by e_book_client_modify_contact_finish()
 * from the @callback.
 *
 * Since: 3.2
 **/
void
e_book_client_modify_contact (EBookClient *client,
                              EContact *contact,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (E_IS_CONTACT (contact));

	async_context = g_slice_new0 (AsyncContext);
	async_context->contact = g_object_ref (contact);

	simple = g_simple_async_result_new (
		G_OBJECT (client), callback, user_data,
		e_book_client_modify_contact);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, book_client_modify_contact_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_book_client_modify_contact_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_modify_contact().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_modify_contact_finish (EBookClient *client,
                                     GAsyncResult *result,
                                     GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (client),
		e_book_client_modify_contact), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/**
 * e_book_client_modify_contact_sync:
 * @client: an #EBookClient
 * @contact: an #EContact
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Applies the changes made to @contact to the stored version in @client.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_modify_contact_sync (EBookClient *client,
                                   EContact *contact,
                                   GCancellable *cancellable,
                                   GError **error)
{
	GSList link = { contact, NULL };

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	return e_book_client_modify_contacts_sync (
		client, &link, cancellable, error);
}

/* Helper for e_book_client_modify_contacts() */
static void
book_client_modify_contacts_thread (GSimpleAsyncResult *simple,
                                    GObject *source_object,
                                    GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_book_client_modify_contacts_sync (
		E_BOOK_CLIENT (source_object),
		async_context->object_list,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_book_client_modify_contacts:
 * @client: an #EBookClient
 * @contacts: (element-type EContact): a #GSList of #EContact objects
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Applies the changes made to @contacts to the stored versions in @client.
 * The call is finished by e_book_client_modify_contacts_finish()
 * from the @callback.
 *
 * Since: 3.4
 **/
void
e_book_client_modify_contacts (EBookClient *client,
                               GSList *contacts,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (contacts != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->object_list = g_slist_copy_deep (
		contacts, (GCopyFunc) g_object_ref, NULL);

	simple = g_simple_async_result_new (
		G_OBJECT (client), callback, user_data,
		e_book_client_modify_contacts);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, book_client_modify_contacts_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_book_client_modify_contacts_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_modify_contacts().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.4
 **/
gboolean
e_book_client_modify_contacts_finish (EBookClient *client,
                                      GAsyncResult *result,
                                      GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (client),
		e_book_client_modify_contacts), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/**
 * e_book_client_modify_contacts_sync:
 * @client: an #EBookClient
 * @contacts: (element-type EContact): a #GSList of #EContact objects
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Applies the changes made to @contacts to the stored versions in @client.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.4
 **/
gboolean
e_book_client_modify_contacts_sync (EBookClient *client,
                                    GSList *contacts,
                                    GCancellable *cancellable,
                                    GError **error)
{
	GSList *link;
	gchar **strv;
	gboolean success;
	gint ii = 0;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (contacts != NULL, FALSE);

	if (client->priv->dbus_proxy == NULL) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	/* Build a string array, ensuring each element is valid UTF-8. */
	strv = g_new0 (gchar *, g_slist_length (contacts) + 1);
	for (link = contacts; link != NULL; link = g_slist_next (link)) {
		EVCard *vcard;
		gchar *string;

		vcard = E_VCARD (link->data);
		string = e_vcard_to_string (vcard, EVC_FORMAT_VCARD_30);
		strv[ii++] = e_util_utf8_make_valid (string);
		g_free (string);
	}

	success = e_dbus_address_book_call_modify_contacts_sync (
		client->priv->dbus_proxy,
		(const gchar * const *) strv,
		cancellable, error);

	g_strfreev (strv);

	return success;
}

/* Helper for e_book_client_remove_contact() */
static void
book_client_remove_contact_thread (GSimpleAsyncResult *simple,
                                   GObject *source_object,
                                   GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_book_client_remove_contact_sync (
		E_BOOK_CLIENT (source_object),
		async_context->contact,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_book_client_remove_contact:
 * @client: an #EBookClient
 * @contact: an #EContact
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Removes @contact from the @client.
 * The call is finished by e_book_client_remove_contact_finish()
 * from the @callback.
 *
 * Since: 3.2
 **/
void
e_book_client_remove_contact (EBookClient *client,
                              /* const */ EContact *contact,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (E_IS_CONTACT (contact));

	async_context = g_slice_new0 (AsyncContext);
	async_context->contact = g_object_ref (contact);

	simple = g_simple_async_result_new (
		G_OBJECT (client), callback, user_data,
		e_book_client_remove_contact);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, book_client_remove_contact_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_book_client_remove_contact_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_remove_contact().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_remove_contact_finish (EBookClient *client,
                                     GAsyncResult *result,
                                     GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (client),
		e_book_client_remove_contact), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/**
 * e_book_client_remove_contact_sync:
 * @client: an #EBookClient
 * @contact: an #EContact
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Removes @contact from the @client.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_remove_contact_sync (EBookClient *client,
                                   EContact *contact,
                                   GCancellable *cancellable,
                                   GError **error)
{
	const gchar *uid;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	g_return_val_if_fail (uid != NULL, FALSE);

	return e_book_client_remove_contact_by_uid_sync (
		client, uid, cancellable, error);
}

/* Helper for e_book_client_remove_contact_by_uid() */
static void
book_client_remove_contact_by_uid_thread (GSimpleAsyncResult *simple,
                                          GObject *source_object,
                                          GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_book_client_remove_contact_by_uid_sync (
		E_BOOK_CLIENT (source_object),
		async_context->uid,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_book_client_remove_contact_by_uid:
 * @client: an #EBookClient
 * @uid: a UID of a contact to remove
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Removes contact with @uid from the @client.
 * The call is finished by e_book_client_remove_contact_by_uid_finish()
 * from the @callback.
 *
 * Since: 3.2
 **/
void
e_book_client_remove_contact_by_uid (EBookClient *client,
                                     const gchar *uid,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (uid != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->uid = g_strdup (uid);

	simple = g_simple_async_result_new (
		G_OBJECT (client), callback, user_data,
		e_book_client_remove_contact_by_uid);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, book_client_remove_contact_by_uid_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_book_client_remove_contact_by_uid_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_remove_contact_by_uid().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_remove_contact_by_uid_finish (EBookClient *client,
                                            GAsyncResult *result,
                                            GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (client),
		e_book_client_remove_contact_by_uid), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/**
 * e_book_client_remove_contact_by_uid_sync:
 * @client: an #EBookClient
 * @uid: a UID of a contact to remove
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Removes contact with @uid from the @client.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_remove_contact_by_uid_sync (EBookClient *client,
                                          const gchar *uid,
                                          GCancellable *cancellable,
                                          GError **error)
{
	GSList link = { (gpointer) uid, NULL };

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	return e_book_client_remove_contacts_sync (
		client, &link, cancellable, error);
}

/* Helper for e_book_client_remove_contacts() */
static void
book_client_remove_contacts_thread (GSimpleAsyncResult *simple,
                                    GObject *source_object,
                                    GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_book_client_remove_contacts_sync (
		E_BOOK_CLIENT (source_object),
		async_context->string_list,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_book_client_remove_contacts:
 * @client: an #EBookClient
 * @uids: (element-type utf8): a #GSList of UIDs to remove
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Removes the contacts with uids from the list @uids from @client.  This is
 * always more efficient than calling e_book_client_remove_contact() if you
 * have more than one uid to remove, as some backends can implement it
 * as a batch request.
 * The call is finished by e_book_client_remove_contacts_finish()
 * from the @callback.
 *
 * Since: 3.2
 **/
void
e_book_client_remove_contacts (EBookClient *client,
                               const GSList *uids,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (uids != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->string_list = g_slist_copy_deep (
		(GSList *) uids, (GCopyFunc) g_strdup, NULL);

	simple = g_simple_async_result_new (
		G_OBJECT (client), callback, user_data,
		e_book_client_remove_contacts);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, book_client_remove_contacts_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_book_client_remove_contacts_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_remove_contacts().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_remove_contacts_finish (EBookClient *client,
                                      GAsyncResult *result,
                                      GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (client),
		e_book_client_remove_contacts), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/**
 * e_book_client_remove_contacts_sync:
 * @client: an #EBookClient
 * @uids: (element-type utf8): a #GSList of UIDs to remove
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Removes the contacts with uids from the list @uids from @client.  This is
 * always more efficient than calling e_book_client_remove_contact() if you
 * have more than one uid to remove, as some backends can implement it
 * as a batch request.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_remove_contacts_sync (EBookClient *client,
                                    const GSList *uids,
                                    GCancellable *cancellable,
                                    GError **error)
{
	gchar **strv;
	gboolean success;
	gint ii = 0;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	if (client->priv->dbus_proxy == NULL) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	strv = g_new0 (gchar *, g_slist_length ((GSList *) uids) + 1);
	while (uids != NULL) {
		strv[ii++] = e_util_utf8_make_valid (uids->data);
		uids = g_slist_next (uids);
	}

	success = e_dbus_address_book_call_remove_contacts_sync (
		client->priv->dbus_proxy,
		(const gchar * const *) strv,
		cancellable, error);

	g_strfreev (strv);

	return success;
}

/* Helper for e_book_client_get_contact() */
static void
book_client_get_contact_thread (GSimpleAsyncResult *simple,
                                GObject *source_object,
                                GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_book_client_get_contact_sync (
		E_BOOK_CLIENT (source_object),
		async_context->uid,
		&async_context->contact,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_book_client_get_contact:
 * @client: an #EBookClient
 * @uid: a unique string ID specifying the contact
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Receive #EContact from the @client for the gived @uid.
 * The call is finished by e_book_client_get_contact_finish()
 * from the @callback.
 *
 * Since: 3.2
 **/
void
e_book_client_get_contact (EBookClient *client,
                           const gchar *uid,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (uid != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->uid  = g_strdup (uid);

	simple = g_simple_async_result_new (
		G_OBJECT (client), callback, user_data,
		e_book_client_get_contact);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, book_client_get_contact_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_book_client_get_contact_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @out_contact: (out): an #EContact for previously given uid
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_get_contact().
 * If successful, then the @out_contact is set to newly allocated
 * #EContact, which should be freed with g_object_unref().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_contact_finish (EBookClient *client,
                                  GAsyncResult *result,
                                  EContact **out_contact,
                                  GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (client),
		e_book_client_get_contact), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	g_return_val_if_fail (async_context->contact != NULL, FALSE);

	if (out_contact != NULL)
		*out_contact = g_object_ref (async_context->contact);

	return TRUE;
}

/**
 * e_book_client_get_contact_sync:
 * @client: an #EBookClient
 * @uid: a unique string ID specifying the contact
 * @out_contact: (out): an #EContact for given @uid
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Receive #EContact from the @client for the gived @uid.
 * If successful, then the @out_contact is set to newly allocated
 * #EContact, which should be freed with g_object_unref().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_contact_sync (EBookClient *client,
                                const gchar *uid,
                                EContact **out_contact,
                                GCancellable *cancellable,
                                GError **error)
{
	gchar *utf8_uid;
	gchar *vcard = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_contact != NULL, FALSE);

	if (client->priv->dbus_proxy == NULL) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	utf8_uid = e_util_utf8_make_valid (uid);

	success = e_dbus_address_book_call_get_contact_sync (
		client->priv->dbus_proxy,
		utf8_uid, &vcard, cancellable, error);

	/* Sanity check. */
	g_return_val_if_fail (
		(success && (vcard != NULL)) ||
		(!success && (vcard == NULL)), FALSE);

	if (vcard != NULL) {
		*out_contact =
			e_contact_new_from_vcard_with_uid (vcard, utf8_uid);
		g_free (vcard);
	}

	g_free (utf8_uid);

	return success;
}

/* Helper for e_book_client_get_contacts() */
static void
book_client_get_contacts_thread (GSimpleAsyncResult *simple,
                                 GObject *source_object,
                                 GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_book_client_get_contacts_sync (
		E_BOOK_CLIENT (source_object),
		async_context->sexp,
		&async_context->object_list,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_book_client_get_contacts:
 * @client: an #EBookClient
 * @sexp: an S-expression representing the query
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Query @client with @sexp, receiving a list of contacts which
 * matched. The call is finished by e_book_client_get_contacts_finish()
 * from the @callback.
 *
 * Note: @sexp can be obtained through #EBookQuery, by converting it
 * to a string with e_book_query_to_string().
 *
 * Since: 3.2
 **/
void
e_book_client_get_contacts (EBookClient *client,
                            const gchar *sexp,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (sexp != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->sexp = g_strdup (sexp);

	simple = g_simple_async_result_new (
		G_OBJECT (client), callback, user_data,
		e_book_client_get_contacts);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, book_client_get_contacts_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_book_client_get_contacts_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @out_contacts: (element-type EContact) (out): a #GSList of matched
 *                #EContact-s
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_get_contacts().
 * If successful, then the @out_contacts is set to newly allocated list of
 * #EContact-s, which should be freed with e_client_util_free_object_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_contacts_finish (EBookClient *client,
                                   GAsyncResult *result,
                                   GSList **out_contacts,
                                   GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (client),
		e_book_client_get_contacts), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	if (out_contacts != NULL) {
		*out_contacts = async_context->object_list;
		async_context->object_list = NULL;
	}

	return TRUE;
}

/**
 * e_book_client_get_contacts_sync:
 * @client: an #EBookClient
 * @sexp: an S-expression representing the query
 * @out_contacts: (element-type EContact) (out): a #GSList of matched
 *                #EContact-s
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Query @client with @sexp, receiving a list of contacts which matched.
 * If successful, then the @out_contacts is set to newly allocated #GSList of
 * #EContact-s, which should be freed with e_client_util_free_object_slist().
 *
 * Note: @sexp can be obtained through #EBookQuery, by converting it
 * to a string with e_book_query_to_string().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_contacts_sync (EBookClient *client,
                                 const gchar *sexp,
                                 GSList **out_contacts,
                                 GCancellable *cancellable,
                                 GError **error)
{
	gchar *utf8_sexp;
	gchar **vcards = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (sexp != NULL, FALSE);
	g_return_val_if_fail (out_contacts != NULL, FALSE);

	if (client->priv->dbus_proxy == NULL) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	utf8_sexp = e_util_utf8_make_valid (sexp);

	success = e_dbus_address_book_call_get_contact_list_sync (
		client->priv->dbus_proxy,
		utf8_sexp, &vcards, cancellable, error);

	g_free (utf8_sexp);

	/* Sanity check. */
	g_return_val_if_fail (
		(success && (vcards != NULL)) ||
		(!success && (vcards == NULL)), FALSE);

	if (vcards != NULL) {
		EContact *contact;
		GSList *tmp = NULL;
		gint ii;

		for (ii = 0; vcards[ii] != NULL; ii++) {
			contact = e_contact_new_from_vcard (vcards[ii]);
			tmp = g_slist_prepend (tmp, contact);
		}

		*out_contacts = g_slist_reverse (tmp);

		g_strfreev (vcards);
	}

	return success;
}

/* Helper for e_book_client_get_contacts_uids() */
static void
book_client_get_contacts_uids_thread (GSimpleAsyncResult *simple,
                                      GObject *source_object,
                                      GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_book_client_get_contacts_uids_sync (
		E_BOOK_CLIENT (source_object),
		async_context->sexp,
		&async_context->string_list,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_book_client_get_contacts_uids:
 * @client: an #EBookClient
 * @sexp: an S-expression representing the query
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Query @client with @sexp, receiving a list of contacts UIDs which
 * matched. The call is finished by e_book_client_get_contacts_uids_finish()
 * from the @callback.
 *
 * Note: @sexp can be obtained through #EBookQuery, by converting it
 * to a string with e_book_query_to_string().
 *
 * Since: 3.2
 **/
void
e_book_client_get_contacts_uids (EBookClient *client,
                                 const gchar *sexp,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (sexp != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->sexp = g_strdup (sexp);

	simple = g_simple_async_result_new (
		G_OBJECT (client), callback, user_data,
		e_book_client_get_contacts_uids);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, book_client_get_contacts_uids_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_book_client_get_contacts_uids_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @out_contact_uids: (element-type utf8) (out): a #GSList of matched
 *                    contact UIDs stored as strings
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_get_contacts_uids().
 * If successful, then the @out_contact_uids is set to newly allocated list
 * of UID strings, which should be freed with e_client_util_free_string_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_contacts_uids_finish (EBookClient *client,
                                        GAsyncResult *result,
                                        GSList **out_contact_uids,
                                        GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (client),
		e_book_client_get_contacts_uids), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	if (out_contact_uids != NULL) {
		*out_contact_uids = async_context->string_list;
		async_context->string_list = NULL;
	}

	return TRUE;
}

/**
 * e_book_client_get_contacts_uids_sync:
 * @client: an #EBookClient
 * @sexp: an S-expression representing the query
 * @out_contact_uids: (element-type utf8) (out): a #GSList of matched
 *                    contacts UIDs stored as strings
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Query @client with @sexp, receiving a list of contacts UIDs which matched.
 * If successful, then the @out_contact_uids is set to newly allocated list
 * of UID strings, which should be freed with e_client_util_free_string_slist().
 *
 * Note: @sexp can be obtained through #EBookQuery, by converting it
 * to a string with e_book_query_to_string().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_contacts_uids_sync (EBookClient *client,
                                      const gchar *sexp,
                                      GSList **out_contact_uids,
                                      GCancellable *cancellable,
                                      GError **error)
{
	gchar *utf8_sexp;
	gchar **uids = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (sexp != NULL, FALSE);
	g_return_val_if_fail (out_contact_uids != NULL, FALSE);

	if (client->priv->dbus_proxy == NULL) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	utf8_sexp = e_util_utf8_make_valid (sexp);

	success = e_dbus_address_book_call_get_contact_list_uids_sync (
		client->priv->dbus_proxy,
		utf8_sexp, &uids, cancellable, error);

	g_free (utf8_sexp);

	/* Sanity check. */
	g_return_val_if_fail (
		(success && (uids != NULL)) ||
		(!success && (uids == NULL)), FALSE);

	/* XXX We should have passed the string array directly
	 *     back to the caller instead of building a linked
	 *     list.  This is unnecessary work. */
	if (uids != NULL) {
		GSList *tmp = NULL;
		gint ii;

		/* Take ownership of the string array elements. */
		for (ii = 0; uids[ii] != NULL; ii++) {
			tmp = g_slist_prepend (tmp, uids[ii]);
			uids[ii] = NULL;
		}

		*out_contact_uids = g_slist_reverse (tmp);

		g_free (uids);
	}

	return success;
}

/* Helper for e_book_client_get_view() */
static void
book_client_get_view_thread (GSimpleAsyncResult *simple,
                             GObject *source_object,
                             GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_book_client_get_view_sync (
		E_BOOK_CLIENT (source_object),
		async_context->sexp,
		&async_context->client_view,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_book_client_get_view:
 * @client: an #EBookClient
 * @sexp: an S-expression representing the query
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Query @client with @sexp, creating an #EBookClientView.
 * The call is finished by e_book_client_get_view_finish()
 * from the @callback.
 *
 * Note: @sexp can be obtained through #EBookQuery, by converting it
 * to a string with e_book_query_to_string().
 *
 * Since: 3.2
 **/
void
e_book_client_get_view (EBookClient *client,
                        const gchar *sexp,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (sexp != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->sexp = g_strdup (sexp);

	simple = g_simple_async_result_new (
		G_OBJECT (client), callback, user_data,
		e_book_client_get_view);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, book_client_get_view_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_book_client_get_view_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @out_view: (out): an #EBookClientView
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_get_view().
 * If successful, then the @out_view is set to newly allocated
 * #EBookClientView, which should be freed with g_object_unref().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_view_finish (EBookClient *client,
                               GAsyncResult *result,
                               EBookClientView **out_view,
                               GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (client),
		e_book_client_get_view), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	g_return_val_if_fail (async_context->client_view != NULL, FALSE);

	if (out_view != NULL)
		*out_view = g_object_ref (async_context->client_view);

	return TRUE;
}

/**
 * e_book_client_get_view_sync:
 * @client: an #EBookClient
 * @sexp: an S-expression representing the query
 * @out_view: (out) an #EBookClientView
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Query @client with @sexp, creating an #EBookClientView.
 * If successful, then the @out_view is set to newly allocated
 * #EBookClientView, which should be freed with g_object_unref().
 *
 * Note: @sexp can be obtained through #EBookQuery, by converting it
 * to a string with e_book_query_to_string().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_view_sync (EBookClient *client,
                             const gchar *sexp,
                             EBookClientView **out_view,
                             GCancellable *cancellable,
                             GError **error)
{
	gchar *utf8_sexp;
	gchar *object_path = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (sexp != NULL, FALSE);
	g_return_val_if_fail (out_view != NULL, FALSE);

	if (client->priv->dbus_proxy == NULL) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	utf8_sexp = e_util_utf8_make_valid (sexp);

	success = e_dbus_address_book_call_get_view_sync (
		client->priv->dbus_proxy, utf8_sexp,
		&object_path, cancellable, error);

	g_free (utf8_sexp);

	/* Sanity check. */
	g_return_val_if_fail (
		(success && (object_path != NULL)) ||
		(!success && (object_path == NULL)), FALSE);

	if (object_path != NULL) {
		GDBusConnection *connection;
		EBookClientView *client_view;

		connection = g_dbus_proxy_get_connection (
			G_DBUS_PROXY (client->priv->dbus_proxy));

		client_view = g_initable_new (
			E_TYPE_BOOK_CLIENT_VIEW,
			cancellable, error,
			"client", client,
			"connection", connection,
			"object-path", object_path,
			NULL);

		/* XXX Would have been easier to return the
		 *     EBookClientView directly rather than
		 *     through an "out" parameter. */
		if (client_view != NULL)
			*out_view = client_view;
		else
			success = FALSE;

		g_free (object_path);
	}

	return success;
}


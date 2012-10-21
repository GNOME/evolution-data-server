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

#include <libedataserver/libedataserver.h>
#include <libedataserver/e-client-private.h>

#include "e-book-client.h"
#include "e-contact.h"
#include "e-name-western.h"
#include "e-book-client-view-private.h"

#include "e-gdbus-book.h"
#include "e-gdbus-book-factory.h"
#include "e-gdbus-book-view.h"

#define E_BOOK_CLIENT_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BOOK_CLIENT, EBookClientPrivate))

struct _EBookClientPrivate {
	/* GDBus data */
	GDBusProxy *gdbus_book;
	guint gone_signal_id;
};

G_DEFINE_TYPE (EBookClient, e_book_client, E_TYPE_CLIENT)

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
 * @BOOK_BACKEND_PROPERTY_SUPPORTED_AUTH_METHODS: Retrieves comma-separated list
 *   of supported authentication methods by the backend.
 *   Use e_client_util_parse_comma_strings() to parse returned string value
 *   into a #GSList.
 *
 * See also: @CLIENT_BACKEND_PROPERTY_OPENED, @CLIENT_BACKEND_PROPERTY_OPENING,
 *   @CLIENT_BACKEND_PROPERTY_ONLINE, @CLIENT_BACKEND_PROPERTY_READONLY
 *   @CLIENT_BACKEND_PROPERTY_CACHE_DIR, @CLIENT_BACKEND_PROPERTY_CAPABILITIES
 */

GQuark
e_book_client_error_quark (void)
{
	static GQuark q = 0;
	if (q == 0)
		q = g_quark_from_static_string ("e-book-client-error-quark");

	return q;
}

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
static EGdbusBookFactory *book_factory_proxy = NULL;
static GStaticRecMutex book_factory_proxy_lock = G_STATIC_REC_MUTEX_INIT;
#define LOCK_FACTORY()   g_static_rec_mutex_lock (&book_factory_proxy_lock)
#define UNLOCK_FACTORY() g_static_rec_mutex_unlock (&book_factory_proxy_lock)

static void gdbus_book_factory_proxy_closed_cb (GDBusConnection *connection, gboolean remote_peer_vanished, GError *error, gpointer user_data);

static void
gdbus_book_factory_proxy_disconnect (GDBusConnection *connection)
{
	LOCK_FACTORY ();

	if (!connection && book_factory_proxy)
		connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (book_factory_proxy));

	if (connection && book_connection_closed_id) {
		g_dbus_connection_signal_unsubscribe (connection, book_connection_closed_id);
		g_signal_handlers_disconnect_by_func (connection, gdbus_book_factory_proxy_closed_cb, NULL);
	}

	if (book_factory_proxy)
		g_object_unref (book_factory_proxy);

	book_connection_closed_id = 0;
	book_factory_proxy = NULL;

	UNLOCK_FACTORY ();
}

static void
gdbus_book_factory_proxy_closed_cb (GDBusConnection *connection,
                                    gboolean remote_peer_vanished,
                                    GError *error,
                                    gpointer user_data)
{
	GError *err = NULL;

	LOCK_FACTORY ();

	gdbus_book_factory_proxy_disconnect (connection);

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
	gdbus_book_factory_proxy_closed_cb (connection, TRUE, NULL, user_data);
}

static gboolean
gdbus_book_factory_activate (GError **error)
{
	GDBusConnection *connection;

	LOCK_FACTORY ();

	if (G_LIKELY (book_factory_proxy)) {
		UNLOCK_FACTORY ();
		return TRUE;
	}

	book_factory_proxy = e_gdbus_book_factory_proxy_new_for_bus_sync (
		G_BUS_TYPE_SESSION,
		G_DBUS_PROXY_FLAGS_NONE,
		ADDRESS_BOOK_DBUS_SERVICE_NAME,
		"/org/gnome/evolution/dataserver/AddressBookFactory",
		NULL,
		error);

	if (!book_factory_proxy) {
		UNLOCK_FACTORY ();
		return FALSE;
	}

	connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (book_factory_proxy));
	book_connection_closed_id = g_dbus_connection_signal_subscribe (
		connection,
		NULL,						/* sender */
		"org.freedesktop.DBus",				/* interface */
		"NameOwnerChanged",				/* member */
		"/org/freedesktop/DBus",			/* object_path */
		"org.gnome.evolution.dataserver.AddressBook",	/* arg0 */
		G_DBUS_SIGNAL_FLAGS_NONE,
		gdbus_book_factory_connection_gone_cb, NULL, NULL);

	g_signal_connect (connection, "closed", G_CALLBACK (gdbus_book_factory_proxy_closed_cb), NULL);

	UNLOCK_FACTORY ();

	return TRUE;
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

	if (client->priv->gdbus_book) {
		GDBusConnection *connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (client->priv->gdbus_book));

		g_signal_handlers_disconnect_by_func (connection, gdbus_book_client_closed_cb, client);
		g_dbus_connection_signal_unsubscribe (connection, client->priv->gone_signal_id);
		client->priv->gone_signal_id = 0;

		e_gdbus_book_call_close_sync (client->priv->gdbus_book, NULL, NULL);
		g_object_unref (client->priv->gdbus_book);
		client->priv->gdbus_book = NULL;
	}

	UNLOCK_FACTORY ();
}

static void
backend_error_cb (EGdbusBook *object,
                  const gchar *message,
                  EBookClient *client)
{
	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (message != NULL);

	e_client_emit_backend_error (E_CLIENT (client), message);
}

static void
readonly_cb (EGdbusBook *object,
             gboolean readonly,
             EBookClient *client)
{
	g_return_if_fail (E_IS_BOOK_CLIENT (client));

	e_client_set_readonly (E_CLIENT (client), readonly);
}

static void
online_cb (EGdbusBook *object,
           gboolean is_online,
           EBookClient *client)
{
	g_return_if_fail (E_IS_BOOK_CLIENT (client));

	e_client_set_online (E_CLIENT (client), is_online);
}

static void
opened_cb (EGdbusBook *object,
           const gchar * const *error_strv,
           EBookClient *client)
{
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (error_strv != NULL);
	g_return_if_fail (e_gdbus_templates_decode_error (error_strv, &error));

	e_client_emit_opened (E_CLIENT (client), error);

	if (error)
		g_error_free (error);
}

static void
backend_property_changed_cb (EGdbusBook *object,
                             const gchar * const *name_value_strv,
                             EBookClient *client)
{
	gchar *prop_name = NULL, *prop_value = NULL;

	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (name_value_strv != NULL);
	g_return_if_fail (e_gdbus_templates_decode_two_strings (name_value_strv, &prop_name, &prop_value));
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (*prop_name);
	g_return_if_fail (prop_value != NULL);

	e_client_emit_backend_property_changed (E_CLIENT (client), prop_name, prop_value);

	g_free (prop_name);
	g_free (prop_value);
}

/*
 * Converts a GSList of EContact objects into a NULL-terminated array of
 * valid UTF-8 vcard strings, suitable for sending over DBus.
 */
static gchar **
contact_slist_to_utf8_vcard_array (GSList *contacts)
{
	gchar **array;
	const GSList *l;
	gint i = 0;

	array = g_new0 (gchar *, g_slist_length (contacts) + 1);
	for (l = contacts; l != NULL; l = l->next) {
		gchar *vcard = e_vcard_to_string (E_VCARD (l->data), EVC_FORMAT_VCARD_30);
		array[i++] = e_util_utf8_make_valid (vcard);
		g_free (vcard);
	}

	return array;
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
	EBookClient *client;
	GError *err = NULL;
	GDBusConnection *connection;
	const gchar *uid;
	gchar *path = NULL;

	g_return_val_if_fail (source != NULL, NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	LOCK_FACTORY ();
	if (!gdbus_book_factory_activate (&err)) {
		UNLOCK_FACTORY ();
		if (err) {
			unwrap_dbus_error (err, &err);
			g_warning ("%s: Failed to run book factory: %s", G_STRFUNC, err->message);
			g_propagate_error (error, err);
		} else {
			g_warning ("%s: Failed to run book factory: Unknown error", G_STRFUNC);
			g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_DBUS_ERROR, _("Failed to run book factory"));
		}

		return NULL;
	}

	uid = e_source_get_uid (source);

	client = g_object_new (E_TYPE_BOOK_CLIENT, "source", source, NULL);
	UNLOCK_FACTORY ();

	if (!e_gdbus_book_factory_call_get_book_sync (G_DBUS_PROXY (book_factory_proxy), uid, &path, NULL, &err)) {
		unwrap_dbus_error (err, &err);
		g_warning ("%s: Cannot get book from factory: %s", G_STRFUNC, err ? err->message : "[no error]");
		if (err)
			g_propagate_error (error, err);
		g_object_unref (client);

		return NULL;
	}

	client->priv->gdbus_book = G_DBUS_PROXY (
		e_gdbus_book_proxy_new_sync (g_dbus_proxy_get_connection (G_DBUS_PROXY (book_factory_proxy)),
		G_DBUS_PROXY_FLAGS_NONE,
		ADDRESS_BOOK_DBUS_SERVICE_NAME,
		path,
		NULL,
		&err));

	if (!client->priv->gdbus_book) {
		g_free (path);
		unwrap_dbus_error (err, &err);
		g_warning ("%s: Cannot create cal proxy: %s", G_STRFUNC, err ? err->message : "Unknown error");
		if (err)
			g_propagate_error (error, err);

		g_object_unref (client);

		return NULL;
	}

	g_free (path);

	connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (client->priv->gdbus_book));
	client->priv->gone_signal_id = g_dbus_connection_signal_subscribe (
		connection,
		"org.freedesktop.DBus",				/* sender */
		"org.freedesktop.DBus",				/* interface */
		"NameOwnerChanged",				/* member */
		"/org/freedesktop/DBus",			/* object_path */
		"org.gnome.evolution.dataserver.AddressBook",	/* arg0 */
		G_DBUS_SIGNAL_FLAGS_NONE,
		gdbus_book_client_connection_gone_cb, client, NULL);

	g_signal_connect (connection, "closed", G_CALLBACK (gdbus_book_client_closed_cb), client);

	g_signal_connect (client->priv->gdbus_book, "backend_error", G_CALLBACK (backend_error_cb), client);
	g_signal_connect (client->priv->gdbus_book, "readonly", G_CALLBACK (readonly_cb), client);
	g_signal_connect (client->priv->gdbus_book, "online", G_CALLBACK (online_cb), client);
	g_signal_connect (client->priv->gdbus_book, "opened", G_CALLBACK (opened_cb), client);
	g_signal_connect (client->priv->gdbus_book, "backend-property-changed", G_CALLBACK (backend_property_changed_cb), client);

	return client;
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
 * @contact: (out): an #EContact pointer to set
 * @client: (out): an #EBookClient pointer to set
 * @error: a #GError to set on failure
 *
 * Get the #EContact referring to the user of the address book
 * and set it in @contact and @client.
 *
 * Returns: %TRUE if successful, otherwise %FALSE.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_self (ESourceRegistry *registry,
                        EContact **contact,
                        EBookClient **client,
                        GError **error)
{
	ESource *source;
	GError *local_error = NULL;
	GSettings *settings;
	gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), FALSE);
	g_return_val_if_fail (contact != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	source = e_source_registry_ref_builtin_address_book (registry);
	*client = e_book_client_new (source, &local_error);
	g_object_unref (source);

	if (!*client) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	if (!e_client_open_sync (E_CLIENT (*client), FALSE, NULL, &local_error)) {
		g_object_unref (*client);
		*client = NULL;
		g_propagate_error (error, local_error);

		return FALSE;
	}

	settings = g_settings_new (SELF_UID_PATH_ID);
	uid = g_settings_get_string (settings, SELF_UID_KEY);
	g_object_unref (settings);

	if (uid) {
		gboolean got;

		/* Don't care about errors because we'll create a new card on failure */
		got = e_book_client_get_contact_sync (*client, uid, contact, NULL, NULL);
		g_free (uid);
		if (got)
			return TRUE;
	}

	uid = NULL;
	*contact = make_me_card ();
	if (!e_book_client_add_contact_sync (*client, *contact, &uid, NULL, &local_error)) {
		g_object_unref (*client);
		*client = NULL;
		g_object_unref (*contact);
		*contact = NULL;
		g_propagate_error (error, local_error);
		return FALSE;
	}

	if (uid) {
		e_contact_set (*contact, E_CONTACT_UID, uid);
		g_free (uid);
	}

	e_book_client_set_self (*client, *contact, NULL);

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

static gboolean
book_client_get_backend_property_from_cache_finish (EClient *client,
                                                    GAsyncResult *result,
                                                    gchar **prop_value,
                                                    GError **error)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (prop_value != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (client), book_client_get_backend_property_from_cache_finish), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, &local_error)) {
		e_client_unwrap_dbus_error (client, local_error, error);
		return FALSE;
	}

	*prop_value = g_strdup (g_simple_async_result_get_op_res_gpointer (simple));

	return *prop_value != NULL;
}

static void
book_client_get_backend_property (EClient *client,
                                  const gchar *prop_name,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	gchar *prop_value;

	prop_value = e_client_get_backend_property_from_cache (client, prop_name);
	if (prop_value) {
		e_client_finish_async_without_dbus (client, cancellable, callback, user_data, book_client_get_backend_property_from_cache_finish, prop_value, g_free);
	} else {
		e_client_proxy_call_string_with_res_op_data (
			client, prop_name, cancellable, callback, user_data, book_client_get_backend_property, prop_name,
			e_gdbus_book_call_get_backend_property,
			NULL, NULL, e_gdbus_book_call_get_backend_property_finish, NULL, NULL);
	}
}

static gboolean
book_client_get_backend_property_finish (EClient *client,
                                         GAsyncResult *result,
                                         gchar **prop_value,
                                         GError **error)
{
	gchar *str = NULL;
	gboolean res;

	g_return_val_if_fail (prop_value != NULL, FALSE);

	if (g_simple_async_result_get_source_tag (G_SIMPLE_ASYNC_RESULT (result)) == book_client_get_backend_property_from_cache_finish) {
		res = book_client_get_backend_property_from_cache_finish (client, result, &str, error);
	} else {
		res = e_client_proxy_call_finish_string (client, result, &str, error, book_client_get_backend_property);
		if (res && str) {
			const gchar *prop_name = g_object_get_data (G_OBJECT (result), "res-op-data");

			if (prop_name && *prop_name)
				e_client_update_backend_property_cache (client, prop_name, str);
		}
	}

	*prop_value = str;

	return res;
}

static gboolean
book_client_get_backend_property_sync (EClient *client,
                                       const gchar *prop_name,
                                       gchar **prop_value,
                                       GCancellable *cancellable,
                                       GError **error)
{
	EBookClient *book_client;
	gchar *prop_val;
	gboolean res;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	book_client = E_BOOK_CLIENT (client);

	if (!book_client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	prop_val = e_client_get_backend_property_from_cache (client, prop_name);
	if (prop_val) {
		g_return_val_if_fail (prop_value != NULL, FALSE);

		*prop_value = prop_val;

		return TRUE;
	}

	res = e_client_proxy_call_sync_string__string (client, prop_name, prop_value, cancellable, error, e_gdbus_book_call_get_backend_property_sync);

	if (res && prop_value)
		e_client_update_backend_property_cache (client, prop_name, *prop_value);

	return res;
}

static void
book_client_set_backend_property (EClient *client,
                                  const gchar *prop_name,
                                  const gchar *prop_value,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	gchar **prop_name_value;

	prop_name_value = e_gdbus_book_encode_set_backend_property (prop_name, prop_value);

	e_client_proxy_call_strv (
		client, (const gchar * const *) prop_name_value, cancellable, callback, user_data, book_client_set_backend_property,
		e_gdbus_book_call_set_backend_property,
		e_gdbus_book_call_set_backend_property_finish, NULL, NULL, NULL, NULL);

	g_strfreev (prop_name_value);
}

static gboolean
book_client_set_backend_property_finish (EClient *client,
                                         GAsyncResult *result,
                                         GError **error)
{
	return e_client_proxy_call_finish_void (client, result, error, book_client_set_backend_property);
}

static gboolean
book_client_set_backend_property_sync (EClient *client,
                                       const gchar *prop_name,
                                       const gchar *prop_value,
                                       GCancellable *cancellable,
                                       GError **error)
{
	EBookClient *book_client;
	gboolean res;
	gchar **prop_name_value;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	book_client = E_BOOK_CLIENT (client);

	if (!book_client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	prop_name_value = e_gdbus_book_encode_set_backend_property (prop_name, prop_value);
	res = e_client_proxy_call_sync_strv__void (client, (const gchar * const *) prop_name_value, cancellable, error, e_gdbus_book_call_set_backend_property_sync);
	g_strfreev (prop_name_value);

	return res;
}

static void
book_client_open (EClient *client,
                  gboolean only_if_exists,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
	e_client_proxy_call_boolean (
		client, only_if_exists, cancellable, callback, user_data, book_client_open,
		e_gdbus_book_call_open,
		e_gdbus_book_call_open_finish, NULL, NULL, NULL, NULL);
}

static gboolean
book_client_open_finish (EClient *client,
                         GAsyncResult *result,
                         GError **error)
{
	return e_client_proxy_call_finish_void (client, result, error, book_client_open);
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

	if (!book_client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	return e_client_proxy_call_sync_boolean__void (client, only_if_exists, cancellable, error, e_gdbus_book_call_open_sync);
}

static void
book_client_refresh (EClient *client,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
	e_client_proxy_call_void (
		client, cancellable, callback, user_data, book_client_refresh,
		e_gdbus_book_call_refresh,
		e_gdbus_book_call_refresh_finish, NULL, NULL, NULL, NULL);
}

static gboolean
book_client_refresh_finish (EClient *client,
                            GAsyncResult *result,
                            GError **error)
{
	return e_client_proxy_call_finish_void (client, result, error, book_client_refresh);
}

static gboolean
book_client_refresh_sync (EClient *client,
                          GCancellable *cancellable,
                          GError **error)
{
	EBookClient *book_client;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	book_client = E_BOOK_CLIENT (client);

	if (!book_client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	return e_client_proxy_call_sync_void__void (client, cancellable, error, e_gdbus_book_call_refresh_sync);
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
                           /* const */ EContact *contact,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	gchar *vcard, *gdbus_vcard = NULL;
	const gchar *strv[2];

	g_return_if_fail (contact != NULL);
	g_return_if_fail (E_IS_CONTACT (contact));

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	strv[0] = e_util_ensure_gdbus_string (vcard, &gdbus_vcard);
	strv[1] = NULL;

	g_return_if_fail (strv[0] != NULL);

	e_client_proxy_call_strv (
		E_CLIENT (client), strv, cancellable, callback, user_data, e_book_client_add_contact,
		e_gdbus_book_call_add_contacts,
		NULL, NULL, NULL, e_gdbus_book_call_add_contacts_finish, NULL);

	g_free (vcard);
	g_free (gdbus_vcard);
}

/**
 * e_book_client_add_contact_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @added_uid: (out): UID of a newly added contact; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_add_contact() and
 * sets @added_uid to a UID of a newly added contact.
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
                                  gchar **added_uid,
                                  GError **error)
{
	gboolean res;
	gchar **out_uids = NULL;

	res = e_client_proxy_call_finish_strv (E_CLIENT (client), result, &out_uids, error, e_book_client_add_contact);

	if (res && out_uids && added_uid) {
		*added_uid = g_strdup (out_uids[0]);
	} else {
		if (added_uid)
			*added_uid = NULL;
	}
	g_strfreev (out_uids);

	return res;
}

/**
 * e_book_client_add_contact_sync:
 * @client: an #EBookClient
 * @contact: an #EContact
 * @added_uid: (out): UID of a newly added contact; can be %NULL
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Adds @contact to @client and
 * sets @added_uid to a UID of a newly added contact.
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
                                /* const */ EContact *contact,
                                gchar **added_uid,
                                GCancellable *cancellable,
                                GError **error)
{
	gboolean res;
	gchar *vcard, *gdbus_vcard = NULL, **out_uids = NULL;
	const gchar *strv[2];

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	strv[0] = e_util_ensure_gdbus_string (vcard, &gdbus_vcard);
	strv[1] = NULL;

	g_return_val_if_fail (strv[0] != NULL, FALSE);

	res = e_client_proxy_call_sync_strv__strv (E_CLIENT (client), strv, &out_uids, cancellable, error, e_gdbus_book_call_add_contacts_sync);

	if (res && out_uids && added_uid) {
		*added_uid = g_strdup (out_uids[0]);
	} else {
		if (added_uid)
			*added_uid = NULL;
	}

	g_strfreev (out_uids);
	g_free (vcard);
	g_free (gdbus_vcard);

	return res;
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
                            /* const */ GSList *contacts,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	gchar **array;

	g_return_if_fail (contacts != NULL);

	array = contact_slist_to_utf8_vcard_array (contacts);

	e_client_proxy_call_strv (
		E_CLIENT (client), (const gchar * const *) array, cancellable, callback, user_data, e_book_client_add_contacts,
		e_gdbus_book_call_add_contacts,
		NULL, NULL, NULL, e_gdbus_book_call_add_contacts_finish, NULL);

	g_strfreev (array);
}

/**
 * e_book_client_add_contacts_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @added_uids: (out) (element-type utf8) (allow-none): UIDs of newly added
 * contacts; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_add_contacts() and
 * sets @added_uids to the UIDs of newly added contacts if successful.
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
                                   GSList **added_uids,
                                   GError **error)
{
	gboolean res;
	gchar **out_uids = NULL;

	res = e_client_proxy_call_finish_strv (E_CLIENT (client), result, &out_uids, error, e_book_client_add_contacts);

	if (res && out_uids && added_uids) {
		*added_uids = e_client_util_strv_to_slist ((const gchar * const*) out_uids);
	} else {
		if (added_uids)
			*added_uids = NULL;
	}

	g_strfreev (out_uids);

	return res;
}

/**
 * e_book_client_add_contacts_sync:
 * @client: an #EBookClient
 * @contacts: (element-type EContact): a #GSList of #EContact objects to add
 * @added_uids: (out) (element-type utf8) (allow-none): UIDs of newly added
 * contacts; can be %NULL
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Adds @contacts to @client and
 * sets @added_uids to the UIDs of newly added contacts if successful.
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
                                 /* const */ GSList *contacts,
                                 GSList **added_uids,
                                 GCancellable *cancellable,
                                 GError **error)
{
	gboolean res;
	gchar **array, **out_uids = NULL;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	array = contact_slist_to_utf8_vcard_array (contacts);

	res = e_client_proxy_call_sync_strv__strv (E_CLIENT (client), (const gchar * const *) array, &out_uids, cancellable, error, e_gdbus_book_call_add_contacts_sync);

	if (res && out_uids && added_uids) {
		*added_uids = e_client_util_strv_to_slist ((const gchar * const*) out_uids);
	} else {
		if (added_uids)
			*added_uids = NULL;
	}

	g_strfreev (out_uids);
	g_strfreev (array);

	return res;
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
                              /* const */ EContact *contact,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	gchar *vcard, *gdbus_vcard = NULL;
	const gchar *strv[2];

	g_return_if_fail (contact != NULL);
	g_return_if_fail (E_IS_CONTACT (contact));

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	strv[0] = e_util_ensure_gdbus_string (vcard, &gdbus_vcard);
	strv[1] = NULL;

	g_return_if_fail (strv[0] != NULL);

	e_client_proxy_call_strv (
		E_CLIENT (client), strv, cancellable, callback, user_data, e_book_client_modify_contact,
		e_gdbus_book_call_modify_contacts,
		e_gdbus_book_call_modify_contacts_finish, NULL, NULL, NULL, NULL);

	g_free (vcard);
	g_free (gdbus_vcard);
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
	return e_client_proxy_call_finish_void (E_CLIENT (client), result, error, e_book_client_modify_contact);
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
                                   /* const */ EContact *contact,
                                   GCancellable *cancellable,
                                   GError **error)
{
	gboolean res;
	gchar *vcard, *gdbus_vcard = NULL;
	const gchar *strv[2];

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	strv[0] = e_util_ensure_gdbus_string (vcard, &gdbus_vcard);
	strv[1] = NULL;

	g_return_val_if_fail (strv[0] != NULL, FALSE);

	res = e_client_proxy_call_sync_strv__void (E_CLIENT (client), strv, cancellable, error, e_gdbus_book_call_modify_contacts_sync);

	g_free (vcard);
	g_free (gdbus_vcard);

	return res;
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
                               /* const */ GSList *contacts,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	gchar **array;

	g_return_if_fail (contacts != NULL);

	array = contact_slist_to_utf8_vcard_array (contacts);

	e_client_proxy_call_strv (
		E_CLIENT (client), (const gchar * const *) array, cancellable, callback, user_data, e_book_client_modify_contacts,
		e_gdbus_book_call_modify_contacts,
		e_gdbus_book_call_modify_contacts_finish, NULL, NULL, NULL, NULL);

	g_strfreev (array);
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
	return e_client_proxy_call_finish_void (E_CLIENT (client), result, error, e_book_client_modify_contacts);
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
                                    /* const */ GSList *contacts,
                                    GCancellable *cancellable,
                                    GError **error)
{
	gboolean res;
	gchar **array;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (contacts != NULL, FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	array = contact_slist_to_utf8_vcard_array (contacts);

	res = e_client_proxy_call_sync_strv__void (E_CLIENT (client), (const gchar * const *) array, cancellable, error, e_gdbus_book_call_modify_contacts_sync);

	g_strfreev (array);

	return res;
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
	const gchar *uid, *safe_uid;
	const gchar *strv[2];
	gchar *gdbus_uid = NULL;

	g_return_if_fail (contact != NULL);
	g_return_if_fail (E_IS_CONTACT (contact));

	uid = e_contact_get_const ( E_CONTACT (contact), E_CONTACT_UID);
	g_return_if_fail (uid != NULL);

	safe_uid = e_util_ensure_gdbus_string (uid, &gdbus_uid);
	g_return_if_fail (safe_uid != NULL);

	strv[0] = safe_uid;
	strv[1] = NULL;

	e_client_proxy_call_strv (
		E_CLIENT (client), strv, cancellable, callback, user_data, e_book_client_remove_contact,
		e_gdbus_book_call_remove_contacts,
		e_gdbus_book_call_remove_contacts_finish, NULL, NULL, NULL, NULL);

	g_free (gdbus_uid);
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
	return e_client_proxy_call_finish_void (E_CLIENT (client), result, error, e_book_client_remove_contact);
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
                                   /* const */ EContact *contact,
                                   GCancellable *cancellable,
                                   GError **error)
{
	gboolean res;
	const gchar *strv[2];
	const gchar *uid, *safe_uid;
	gchar *gdbus_uid = NULL;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	uid = e_contact_get_const (E_CONTACT (contact), E_CONTACT_UID);
	g_return_val_if_fail (uid != NULL, FALSE);

	safe_uid = e_util_ensure_gdbus_string (uid, &gdbus_uid);
	g_return_val_if_fail (safe_uid != NULL, FALSE);

	strv[0] = safe_uid;
	strv[1] = NULL;

	res = e_client_proxy_call_sync_strv__void (E_CLIENT (client), strv, cancellable, error, e_gdbus_book_call_remove_contacts_sync);

	g_free (gdbus_uid);

	return res;
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
	const gchar *safe_uid;
	gchar *gdbus_uid = NULL;
	const gchar *strv[2];

	g_return_if_fail (uid != NULL);

	safe_uid = e_util_ensure_gdbus_string (uid, &gdbus_uid);
	g_return_if_fail (safe_uid != NULL);

	strv[0] = safe_uid;
	strv[1] = NULL;

	e_client_proxy_call_strv (
		E_CLIENT (client), strv, cancellable, callback, user_data, e_book_client_remove_contact_by_uid,
		e_gdbus_book_call_remove_contacts,
		e_gdbus_book_call_remove_contacts_finish, NULL, NULL, NULL, NULL);

	g_free (gdbus_uid);
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
	return e_client_proxy_call_finish_void (E_CLIENT (client), result, error, e_book_client_remove_contact_by_uid);
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
	gboolean res;
	const gchar *safe_uid;
	gchar *gdbus_uid = NULL;
	const gchar *strv[2];

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	safe_uid = e_util_ensure_gdbus_string (uid, &gdbus_uid);
	g_return_val_if_fail (safe_uid != NULL, FALSE);

	strv[0] = safe_uid;
	strv[1] = NULL;

	res = e_client_proxy_call_sync_strv__void (E_CLIENT (client), strv, cancellable, error, e_gdbus_book_call_remove_contacts_sync);

	g_free (gdbus_uid);

	return res;
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
	gchar **strv;

	g_return_if_fail (uids != NULL);

	strv = e_client_util_slist_to_strv (uids);
	g_return_if_fail (strv != NULL);

	e_client_proxy_call_strv (
		E_CLIENT (client), (const gchar * const *) strv, cancellable, callback, user_data, e_book_client_remove_contacts,
		e_gdbus_book_call_remove_contacts,
		e_gdbus_book_call_remove_contacts_finish, NULL, NULL, NULL, NULL);

	g_strfreev (strv);
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
	return e_client_proxy_call_finish_void (E_CLIENT (client), result, error, e_book_client_remove_contacts);
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
	gboolean res;
	gchar **strv;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	strv = e_client_util_slist_to_strv (uids);
	g_return_val_if_fail (strv != NULL, FALSE);

	res = e_client_proxy_call_sync_strv__void (E_CLIENT (client), (const gchar * const *) strv, cancellable, error, e_gdbus_book_call_remove_contacts_sync);

	g_strfreev (strv);

	return res;
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
	const gchar *safe_uid;
	gchar *gdbus_uid = NULL;

	g_return_if_fail (uid != NULL);

	safe_uid = e_util_ensure_gdbus_string (uid, &gdbus_uid);
	g_return_if_fail (safe_uid != NULL);

	e_client_proxy_call_string (
		E_CLIENT (client), safe_uid, cancellable, callback, user_data, e_book_client_get_contact,
		e_gdbus_book_call_get_contact,
		NULL, NULL, e_gdbus_book_call_get_contact_finish, NULL, NULL);

	g_free (gdbus_uid);
}

/**
 * e_book_client_get_contact_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @contact: (out): an #EContact for previously given uid
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_get_contact().
 * If successful, then the @contact is set to newly allocated
 * #EContact, which should be freed with g_object_unref().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_contact_finish (EBookClient *client,
                                  GAsyncResult *result,
                                  EContact **contact,
                                  GError **error)
{
	gboolean res;
	gchar *vcard = NULL;

	g_return_val_if_fail (contact != NULL, FALSE);

	res = e_client_proxy_call_finish_string (E_CLIENT (client), result, &vcard, error, e_book_client_get_contact);

	if (vcard && res)
		*contact = e_contact_new_from_vcard (vcard);
	else
		*contact = NULL;

	g_free (vcard);

	return res;
}

/**
 * e_book_client_get_contact_sync:
 * @client: an #EBookClient
 * @uid: a unique string ID specifying the contact
 * @contact: (out): an #EContact for given @uid
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Receive #EContact from the @client for the gived @uid.
 * If successful, then the @contact is set to newly allocated
 * #EContact, which should be freed with g_object_unref().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_contact_sync (EBookClient *client,
                                const gchar *uid,
                                EContact **contact,
                                GCancellable *cancellable,
                                GError **error)
{
	gboolean res;
	const gchar *safe_uid;
	gchar *vcard = NULL, *gdbus_uid = NULL;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (contact != NULL, FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	safe_uid = e_util_ensure_gdbus_string (uid, &gdbus_uid);
	g_return_val_if_fail (safe_uid != NULL, FALSE);

	res = e_client_proxy_call_sync_string__string (E_CLIENT (client), safe_uid, &vcard, cancellable, error, e_gdbus_book_call_get_contact_sync);

	if (vcard && res)
		*contact = e_contact_new_from_vcard_with_uid (vcard, safe_uid);
	else
		*contact = NULL;

	g_free (gdbus_uid);
	g_free (vcard);

	return res;
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
	gchar *gdbus_sexp = NULL;

	g_return_if_fail (sexp != NULL);

	e_client_proxy_call_string (
		E_CLIENT (client), e_util_ensure_gdbus_string (sexp, &gdbus_sexp), cancellable, callback, user_data, e_book_client_get_contacts,
		e_gdbus_book_call_get_contact_list,
		NULL, NULL, NULL, e_gdbus_book_call_get_contact_list_finish, NULL);

	g_free (gdbus_sexp);
}

/**
 * e_book_client_get_contacts_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @contacts: (element-type EContact) (out): a #GSList of matched #EContact-s
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_get_contacts().
 * If successful, then the @contacts is set to newly allocated list of #EContact-s,
 * which should be freed with e_client_util_free_object_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_contacts_finish (EBookClient *client,
                                   GAsyncResult *result,
                                   GSList **contacts,
                                   GError **error)
{
	gboolean res;
	gchar **vcards = NULL;

	g_return_val_if_fail (contacts != NULL, FALSE);

	res = e_client_proxy_call_finish_strv (E_CLIENT (client), result, &vcards, error, e_book_client_get_contacts);

	if (vcards && res) {
		gint ii;
		GSList *slist = NULL;

		for (ii = 0; vcards[ii]; ii++) {
			slist = g_slist_prepend (slist, e_contact_new_from_vcard (vcards[ii]));
		}

		*contacts = g_slist_reverse (slist);
	} else {
		*contacts = NULL;
	}

	g_strfreev (vcards);

	return res;
}

/**
 * e_book_client_get_contacts_sync:
 * @client: an #EBookClient
 * @sexp: an S-expression representing the query
 * @contacts: (element-type EContact) (out): a #GSList of matched #EContact-s
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Query @client with @sexp, receiving a list of contacts which matched.
 * If successful, then the @contacts is set to newly allocated #GSList of
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
                                 GSList **contacts,
                                 GCancellable *cancellable,
                                 GError **error)
{
	gboolean res;
	gchar *gdbus_sexp = NULL;
	gchar **vcards = NULL;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (sexp != NULL, FALSE);
	g_return_val_if_fail (contacts != NULL, FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	res = e_client_proxy_call_sync_string__strv (E_CLIENT (client), e_util_ensure_gdbus_string (sexp, &gdbus_sexp), &vcards, cancellable, error, e_gdbus_book_call_get_contact_list_sync);

	if (vcards && res) {
		gint ii;
		GSList *slist = NULL;

		for (ii = 0; vcards[ii]; ii++) {
			slist = g_slist_prepend (slist, e_contact_new_from_vcard (vcards[ii]));
		}

		*contacts = g_slist_reverse (slist);
	} else {
		*contacts = NULL;
	}

	g_free (gdbus_sexp);
	g_strfreev (vcards);

	return res;
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
	gchar *gdbus_sexp = NULL;

	g_return_if_fail (sexp != NULL);

	e_client_proxy_call_string (
		E_CLIENT (client), e_util_ensure_gdbus_string (sexp, &gdbus_sexp), cancellable, callback, user_data, e_book_client_get_contacts_uids,
		e_gdbus_book_call_get_contact_list_uids,
		NULL, NULL, NULL, e_gdbus_book_call_get_contact_list_uids_finish, NULL);

	g_free (gdbus_sexp);
}

/**
 * e_book_client_get_contacts_uids_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @contacts_uids: (element-type utf8) (out): a #GSList of matched contacts UIDs stored as strings
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_get_contacts_uids().
 * If successful, then the @contacts_uids is set to newly allocated list
 * of UID strings, which should be freed with e_client_util_free_string_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_contacts_uids_finish (EBookClient *client,
                                        GAsyncResult *result,
                                        GSList **contacts_uids,
                                        GError **error)
{
	gboolean res;
	gchar **uids = NULL;

	g_return_val_if_fail (contacts_uids != NULL, FALSE);

	res = e_client_proxy_call_finish_strv (E_CLIENT (client), result, &uids, error, e_book_client_get_contacts_uids);

	if (uids && res) {
		gint ii;
		GSList *slist = NULL;

		for (ii = 0; uids[ii]; ii++) {
			slist = g_slist_prepend (slist, g_strdup (uids[ii]));
		}

		*contacts_uids = g_slist_reverse (slist);
	} else {
		*contacts_uids = NULL;
	}

	g_strfreev (uids);

	return res;
}

/**
 * e_book_client_get_contacts_uids_sync:
 * @client: an #EBookClient
 * @sexp: an S-expression representing the query
 * @contacts_uids: (element-type utf8) (out): a #GSList of matched contacts UIDs stored as strings
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Query @client with @sexp, receiving a list of contacts UIDs which matched.
 * If successful, then the @contacts_uids is set to newly allocated list
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
                                      GSList **contacts_uids,
                                      GCancellable *cancellable,
                                      GError **error)
{
	gboolean res;
	gchar *gdbus_sexp = NULL;
	gchar **uids = NULL;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (sexp != NULL, FALSE);
	g_return_val_if_fail (contacts_uids != NULL, FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	res = e_client_proxy_call_sync_string__strv (E_CLIENT (client), e_util_ensure_gdbus_string (sexp, &gdbus_sexp), &uids, cancellable, error, e_gdbus_book_call_get_contact_list_uids_sync);

	if (uids && res) {
		gint ii;
		GSList *slist = NULL;

		for (ii = 0; uids[ii]; ii++) {
			slist = g_slist_prepend (slist, g_strdup (uids[ii]));
		}

		*contacts_uids = g_slist_reverse (slist);
	} else {
		*contacts_uids = NULL;
	}

	g_free (gdbus_sexp);
	g_strfreev (uids);

	return res;
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
	gchar *gdbus_sexp = NULL;

	g_return_if_fail (sexp != NULL);

	e_client_proxy_call_string (
		E_CLIENT (client), e_util_ensure_gdbus_string (sexp, &gdbus_sexp), cancellable, callback, user_data, e_book_client_get_view,
		e_gdbus_book_call_get_view,
		NULL, NULL, e_gdbus_book_call_get_view_finish, NULL, NULL);

	g_free (gdbus_sexp);
}

static gboolean
complete_get_view (EBookClient *client,
                   gboolean res,
                   gchar *view_path,
                   EBookClientView **view,
                   GError **error)
{
	g_return_val_if_fail (view != NULL, FALSE);

	if (view_path && res && book_factory_proxy) {
		GError *local_error = NULL;
		EGdbusBookView *gdbus_bookview;

		gdbus_bookview = e_gdbus_book_view_proxy_new_sync (
			g_dbus_proxy_get_connection (G_DBUS_PROXY (book_factory_proxy)),
			G_DBUS_PROXY_FLAGS_NONE,
			ADDRESS_BOOK_DBUS_SERVICE_NAME,
			view_path,
			NULL,
			&local_error);

		if (gdbus_bookview) {
			*view = _e_book_client_view_new (client, gdbus_bookview);
			g_object_unref (gdbus_bookview);
		} else {
			*view = NULL;
			res = FALSE;
		}

		if (local_error)
			unwrap_dbus_error (local_error, error);
	} else {
		*view = NULL;
		res = FALSE;
	}

	if (!*view && error && !*error)
		g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_DBUS_ERROR, _("Cannot get connection to view"));

	g_free (view_path);

	return res;
}

/**
 * e_book_client_get_view_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @view: (out): an #EBookClientView
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_book_client_get_view().
 * If successful, then the @view is set to newly allocated #EBookClientView,
 * which should be freed with g_object_unref().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_view_finish (EBookClient *client,
                               GAsyncResult *result,
                               EBookClientView **view,
                               GError **error)
{
	gboolean res;
	gchar *view_path = NULL;

	g_return_val_if_fail (view != NULL, FALSE);

	res = e_client_proxy_call_finish_string (E_CLIENT (client), result, &view_path, error, e_book_client_get_view);

	return complete_get_view (client, res, view_path, view, error);
}

/**
 * e_book_client_get_view_sync:
 * @client: an #EBookClient
 * @sexp: an S-expression representing the query
 * @view: (out) an #EBookClientView
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Query @client with @sexp, creating an #EBookClientView.
 * If successful, then the @view is set to newly allocated #EBookClientView,
 * which should be freed with g_object_unref().
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
                             EBookClientView **view,
                             GCancellable *cancellable,
                             GError **error)
{
	gboolean res;
	gchar *gdbus_sexp = NULL;
	gchar *view_path = NULL;

	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (sexp != NULL, FALSE);
	g_return_val_if_fail (view != NULL, FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	res = e_client_proxy_call_sync_string__string (E_CLIENT (client), e_util_ensure_gdbus_string (sexp, &gdbus_sexp), &view_path, cancellable, error, e_gdbus_book_call_get_view_sync);

	g_free (gdbus_sexp);

	return complete_get_view (client, res, view_path, view, error);
}

static GDBusProxy *
book_client_get_dbus_proxy (EClient *client)
{
	EBookClient *book_client;

	g_return_val_if_fail (E_IS_CLIENT (client), NULL);

	book_client = E_BOOK_CLIENT (client);

	return book_client->priv->gdbus_book;
}

static void
book_client_unwrap_dbus_error (EClient *client,
                               GError *dbus_error,
                               GError **out_error)
{
	unwrap_dbus_error (dbus_error, out_error);
}

static void
book_client_retrieve_capabilities (EClient *client,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	g_return_if_fail (E_IS_BOOK_CLIENT (client));

	book_client_get_backend_property (client, CLIENT_BACKEND_PROPERTY_CAPABILITIES, cancellable, callback, user_data);
}

static gboolean
book_client_retrieve_capabilities_finish (EClient *client,
                                          GAsyncResult *result,
                                          gchar **capabilities,
                                          GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	return book_client_get_backend_property_finish (client, result, capabilities, error);
}

static gboolean
book_client_retrieve_capabilities_sync (EClient *client,
                                        gchar **capabilities,
                                        GCancellable *cancellable,
                                        GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	return book_client_get_backend_property_sync (client, CLIENT_BACKEND_PROPERTY_CAPABILITIES, capabilities, cancellable, error);
}

static void
e_book_client_init (EBookClient *client)
{
	LOCK_FACTORY ();
	active_book_clients++;
	UNLOCK_FACTORY ();

	client->priv = E_BOOK_CLIENT_GET_PRIVATE (client);
}

static void
book_client_dispose (GObject *object)
{
	EClient *client;

	client = E_CLIENT (object);

	e_client_cancel_all (client);

	gdbus_book_client_disconnect (E_BOOK_CLIENT (client));

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
		gdbus_book_factory_proxy_disconnect (NULL);
	UNLOCK_FACTORY ();
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
	client_class->get_dbus_proxy			= book_client_get_dbus_proxy;
	client_class->unwrap_dbus_error			= book_client_unwrap_dbus_error;
	client_class->retrieve_capabilities		= book_client_retrieve_capabilities;
	client_class->retrieve_capabilities_finish	= book_client_retrieve_capabilities_finish;
	client_class->retrieve_capabilities_sync	= book_client_retrieve_capabilities_sync;
	client_class->get_backend_property		= book_client_get_backend_property;
	client_class->get_backend_property_finish	= book_client_get_backend_property_finish;
	client_class->get_backend_property_sync		= book_client_get_backend_property_sync;
	client_class->set_backend_property		= book_client_set_backend_property;
	client_class->set_backend_property_finish	= book_client_set_backend_property_finish;
	client_class->set_backend_property_sync		= book_client_set_backend_property_sync;
	client_class->open				= book_client_open;
	client_class->open_finish			= book_client_open_finish;
	client_class->open_sync				= book_client_open_sync;
	client_class->refresh				= book_client_refresh;
	client_class->refresh_finish			= book_client_refresh_finish;
	client_class->refresh_sync			= book_client_refresh_sync;
}

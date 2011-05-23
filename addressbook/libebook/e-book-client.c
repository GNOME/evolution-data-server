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

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>

#include "libedataserver/e-data-server-util.h"
#include "libedataserver/e-client-private.h"

#include "e-book-client.h"
#include "e-contact.h"
#include "e-name-western.h"
#include "e-book-client-view-private.h"

#include "e-gdbus-book.h"
#include "e-gdbus-book-factory.h"
#include "e-gdbus-book-view.h"

struct _EBookClientPrivate
{
	/* GDBus data */
	GDBusProxy *gdbus_book;
	guint gone_signal_id;
};

G_DEFINE_TYPE (EBookClient, e_book_client, E_TYPE_CLIENT)

/**
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
 **/

GQuark
e_book_client_error_quark (void)
{
	static GQuark q = 0;
	if (q == 0)
		q = g_quark_from_static_string ("e-book-client-error-quark");

	return q;
}

const gchar *
e_book_client_error_to_string (EBookClientError code)
{
	switch (code) {
	case E_BOOK_CLIENT_ERROR_NO_SUCH_BOOK:
		return C_("BookClientError", "No such book");
	case E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND:
		return C_("BookClientError", "Contact not found");
	case E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS:
		return C_("BookClientError", "Contact ID already exists");
	case E_BOOK_CLIENT_ERROR_TLS_NOT_AVAILABLE:
		return C_("BookClientError", "TLS not available");
	case E_BOOK_CLIENT_ERROR_NO_SUCH_SOURCE:
		return C_("BookClientError", "No such source");
	case E_BOOK_CLIENT_ERROR_OFFLINE_UNAVAILABLE:
		return C_("BookClientError", "Offline unavailable");
	case E_BOOK_CLIENT_ERROR_UNSUPPORTED_AUTHENTICATION_METHOD:
		return C_("BookClientError", "Unsupported authentication method");
	case E_BOOK_CLIENT_ERROR_NO_SPACE:
		return C_("BookClientError", "No space");
	}

	return C_("BookClientError", "Unknown error");
}

/**
 * If the specified GError is a remote error, then create a new error
 * representing the remote error.  If the error is anything else, then
 * leave it alone.
 */
static gboolean
unwrap_dbus_error (GError *error, GError **client_error)
{
	#define err(a,b) "org.gnome.evolution.dataserver.AddressBook." a, b
	static struct EClientErrorsList
	book_errors[] = {
		{ err ("Success",				-1) },
		{ err ("ContactNotFound",			E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND) },
		{ err ("ContactIDAlreadyExists",		E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS) },
		{ err ("UnsupportedAuthenticationMethod",	E_BOOK_CLIENT_ERROR_UNSUPPORTED_AUTHENTICATION_METHOD) },
		{ err ("TLSNotAvailable",			E_BOOK_CLIENT_ERROR_TLS_NOT_AVAILABLE) },
		{ err ("NoSuchBook",				E_BOOK_CLIENT_ERROR_NO_SUCH_BOOK) },
		{ err ("BookRemoved",				E_BOOK_CLIENT_ERROR_NO_SUCH_SOURCE) },
		{ err ("OfflineUnavailable",			E_BOOK_CLIENT_ERROR_OFFLINE_UNAVAILABLE) },
		{ err ("NoSpace",				E_BOOK_CLIENT_ERROR_NO_SPACE) }
	}, cl_errors[] = {
		{ err ("Busy",					E_CLIENT_ERROR_BUSY) },
		{ err ("RepositoryOffline",			E_CLIENT_ERROR_REPOSITORY_OFFLINE) },
		{ err ("PermissionDenied",			E_CLIENT_ERROR_PERMISSION_DENIED) },
		{ err ("AuthenticationFailed",			E_CLIENT_ERROR_AUTHENTICATION_FAILED) },
		{ err ("AuthenticationRequired",		E_CLIENT_ERROR_AUTHENTICATION_REQUIRED) },
		{ err ("CouldNotCancel",			E_CLIENT_ERROR_COULD_NOT_CANCEL) },
		{ err ("InvalidArg",				E_CLIENT_ERROR_INVALID_ARG) },
		{ err ("NotSupported",				E_CLIENT_ERROR_NOT_SUPPORTED) },
		{ err ("UnsupportedField",			E_CLIENT_ERROR_OTHER_ERROR) },
		{ err ("SearchSizeLimitExceeded",		E_CLIENT_ERROR_OTHER_ERROR) },
		{ err ("SearchTimeLimitExceeded",		E_CLIENT_ERROR_OTHER_ERROR) },
		{ err ("InvalidQuery",				E_CLIENT_ERROR_OTHER_ERROR) },
		{ err ("QueryRefused",				E_CLIENT_ERROR_OTHER_ERROR) },
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
	g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_DBUS_ERROR, _("D-Bus book proxy gone"));
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
gdbus_book_factory_proxy_closed_cb (GDBusConnection *connection, gboolean remote_peer_vanished, GError *error, gpointer user_data)
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
gdbus_book_factory_connection_gone_cb (GDBusConnection *connection, const gchar *sender_name, const gchar *object_path, const gchar *interface_name, const gchar *signal_name, GVariant *parameters, gpointer user_data)
{
	/* signal subscription takes care of correct parameters,
	   thus just do what is to be done here */
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
	book_connection_closed_id = g_dbus_connection_signal_subscribe (connection,
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
gdbus_book_client_closed_cb (GDBusConnection *connection, gboolean remote_peer_vanished, GError *error, EBookClient *client)
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
gdbus_book_client_connection_gone_cb (GDBusConnection *connection, const gchar *sender_name, const gchar *object_path, const gchar *interface_name, const gchar *signal_name, GVariant *parameters, gpointer user_data)
{
	/* signal subscription takes care of correct parameters,
	   thus just do what is to be done here */
	gdbus_book_client_closed_cb (connection, TRUE, NULL, user_data);
}

static void
gdbus_book_client_disconnect (EBookClient *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (client->priv != NULL);

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
backend_error_cb (EGdbusBook *object, const gchar *message, EBookClient *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (message != NULL);

	e_client_emit_backend_error (E_CLIENT (client), message);
}

static void
readonly_cb (EGdbusBook *object, gboolean readonly, EBookClient *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_BOOK_CLIENT (client));

	e_client_set_readonly (E_CLIENT (client), readonly);
}

static void
online_cb (EGdbusBook *object, gboolean is_online, EBookClient *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_BOOK_CLIENT (client));

	e_client_set_online (E_CLIENT (client), is_online);
}

static void
auth_required_cb (EGdbusBook *object, const gchar * const *credentials_strv, EBookClient *client)
{
	ECredentials *credentials;

	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_BOOK_CLIENT (client));

	if (credentials_strv)
		credentials = e_credentials_new_strv (credentials_strv);
	else
		credentials = e_credentials_new ();

	e_client_process_authentication (E_CLIENT (client), credentials);

	e_credentials_free (credentials);
}

static void
opened_cb (EGdbusBook *object, const gchar * const *error_strv, EBookClient *client)
{
	GError *error = NULL;

	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (error_strv != NULL);
	g_return_if_fail (e_gdbus_templates_decode_error (error_strv, &error));

	e_client_emit_opened (E_CLIENT (client), error);

	if (error)
		g_error_free (error);
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
e_book_client_new (ESource *source, GError **error)
{
	EBookClient *client;
	GError *err = NULL;
	GDBusConnection *connection;
	gchar *xml, *gdbus_xml = NULL;
	gchar *path = NULL;

	g_return_val_if_fail (source != NULL, NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	if (!gdbus_book_factory_activate (&err)) {
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

	xml = e_source_to_standalone_xml (source);
	if (!xml || !*xml) {
		g_free (xml);
		g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_INVALID_ARG, _("Invalid source"));
		return NULL;
	}

	client = g_object_new (E_TYPE_BOOK_CLIENT, "source", source, NULL);

	if (!e_gdbus_book_factory_call_get_book_sync (G_DBUS_PROXY (book_factory_proxy), e_util_ensure_gdbus_string (xml, &gdbus_xml), &path, NULL, &err)) {
		unwrap_dbus_error (err, &err);
		g_free (xml);
		g_free (gdbus_xml);
		g_warning ("%s: Cannot get book from factory: %s", G_STRFUNC, err ? err->message : "[no error]");
		if (err)
			g_propagate_error (error, err);
		g_object_unref (client);

		return NULL;
	}

	g_free (xml);
	g_free (gdbus_xml);

	client->priv->gdbus_book = G_DBUS_PROXY (e_gdbus_book_proxy_new_sync (g_dbus_proxy_get_connection (G_DBUS_PROXY (book_factory_proxy)),
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
	client->priv->gone_signal_id = g_dbus_connection_signal_subscribe (connection,
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
	g_signal_connect (client->priv->gdbus_book, "auth-required", G_CALLBACK (auth_required_cb), client);
	g_signal_connect (client->priv->gdbus_book, "opened", G_CALLBACK (opened_cb), client);

	return client;
}

/**
 * e_book_client_new_from_uri:
 * @uri: the URI to load
 * @error: A #GError pointer
 *
 * Creates a new #EBookClient corresponding to the given uri.  See the
 * documentation for e_book_client_new() for further information.
 *
 * Returns: a new but unopened #EBookClient.
 *
 * Since: 3.2
 **/
EBookClient *
e_book_client_new_from_uri (const gchar *uri, GError **error)
{
	ESourceList *source_list = NULL;
	ESource *source;
	EBookClient *client;

	g_return_val_if_fail (uri != NULL, NULL);

	if (!e_book_client_get_sources (&source_list, error))
		return NULL;

	source = e_client_util_get_source_for_uri (source_list, uri);
	if (!source && g_str_has_prefix (uri, "file://")) {
		gchar *local_uri;

		local_uri = g_strconcat ("local://", uri + 7, NULL);
		source = e_client_util_get_source_for_uri (source_list, uri);

		g_free (local_uri);
	}

	if (!source) {
		g_object_unref (source_list);
		g_set_error (error, E_CLIENT_ERROR, E_CLIENT_ERROR_INVALID_ARG, _("Incorrect uri '%s'"), uri);

		return NULL;
	}

	client = e_book_client_new (source, error);

	g_object_unref (source);
	g_object_unref (source_list);

	return client;
}

/**
 * e_book_client_new_system:
 * @error: A #GError pointer
 *
 * Creates a new #EBookClient corresponding to the user's system
 * addressbook.  See the documentation for e_book_client_new() for further
 * information.
 *
 * Returns: a new but unopened #EBookClient.
 *
 * Since: 3.2
 **/
EBookClient *
e_book_client_new_system (GError **error)
{
	ESourceList *source_list = NULL;
	ESource *source;
	EBookClient *client;

	if (!e_book_client_get_sources (&source_list, error))
		return NULL;

	source = e_client_util_get_system_source (source_list);
	if (!source) {
		g_object_unref (source_list);
		g_set_error_literal (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_NO_SUCH_SOURCE, _("Failed to find system book"));

		return NULL;
	}

	client = e_book_client_new (source, error);

	g_object_unref (source);
	g_object_unref (source_list);

	return client;
}

/**
 * e_book_client_new_default:
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #EBookClient corresponding to the user's default
 * address book.  See the documentation for e_book_client_new() for
 * further information.
 *
 * Returns: a new but unopened #EBookClient
 *
 * Since: 3.2
 **/
EBookClient *
e_book_client_new_default (GError **error)
{
	ESourceList *source_list = NULL;
	ESource *source;
	EBookClient *client;

	if (!e_book_client_get_sources (&source_list, error))
		return NULL;

	source = e_source_list_peek_default_source (source_list);
	if (!source) {
		g_set_error_literal (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_NO_SUCH_BOOK, _("Address book does not exist"));
		g_object_unref (source_list);

		return NULL;
	}

	client = e_book_client_new (source, error);

	g_object_unref (source_list);

	return client;
}

/**
 * e_book_client_set_default:
 * @client: An #EBookClient pointer
 * @error: A #GError pointer
 *
 * Sets the #ESource of the #EBookClient as the "default" addressbook.  This is the source
 * that will be loaded in the e_book_client_get_default_addressbook() call.
 *
 * Returns: %TRUE if the setting was stored in libebook's ESourceList, otherwise %FALSE.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_set_default (EBookClient *client, GError **error)
{
	ESource *source;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv != NULL, FALSE);

	source = e_client_get_source (E_CLIENT (client));
	g_return_val_if_fail (source != NULL, FALSE);

	return e_book_client_set_default_source (source, error);
}

/**
 * e_book_client_set_default_source:
 * @source: An #ESource pointer
 * @error: A #GError pointer
 *
 * Sets @source as the "default" addressbook.  This is the source that
 * will be loaded in the e_book_client_get_default_addressbook() call.
 *
 * Returns: %TRUE if the setting was stored in libebook's ESourceList, otherwise %FALSE.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_set_default_source (ESource *source, GError **error)
{
	ESourceList *source_list = NULL;
	gboolean res = FALSE;

	g_return_val_if_fail (source != NULL, FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (!e_book_client_get_sources (&source_list, error))
		return FALSE;

	res = e_client_util_set_default (source_list, source);

	if (res)
		res = e_source_list_sync (source_list, error);
	else
		g_set_error (error, E_CLIENT_ERROR, E_CLIENT_ERROR_INVALID_ARG,
			_("There was no source for UID '%s' stored in a source list."), e_source_peek_uid (source));

	g_object_unref (source_list);

	return res;
}

/**
 * e_book_client_get_sources:
 * @sources: (out): A pointer to an #ESourceList to set
 * @error: A pointer to a GError to set on error
 *
 * Populate @*sources with the list of all sources which have been
 * added to Evolution.
 *
 * Returns: %TRUE if @sources was set, otherwise %FALSE.
 *
 * Since: 3.2
 **/
gboolean
e_book_client_get_sources (ESourceList **sources, GError **error)
{
	GConfClient *gconf;

	g_return_val_if_fail (sources != NULL, FALSE);

	gconf = gconf_client_get_default ();
	*sources = e_source_list_new_for_gconf (gconf, "/apps/evolution/addressbook/sources");
	g_object_unref (gconf);

	return TRUE;
}

#define SELF_UID_KEY "/apps/evolution/addressbook/self/self_uid"

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
		g_string_append_printf (vcard, "N:%s;%s;%s;%s;%s\n",
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
 * @contact: an #EContact pointer to set
 * @client: an #EBookClient pointer to set
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
e_book_client_get_self (EContact **contact, EBookClient **client, GError **error)
{
	GError *local_error = NULL;
	GConfClient *gconf;
	gchar *uid;

	g_return_val_if_fail (contact != NULL, FALSE);
	g_return_val_if_fail (client != NULL, FALSE);

	*client = e_book_client_new_system (&local_error);
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

	gconf = gconf_client_get_default ();
	uid = gconf_client_get_string (gconf, SELF_UID_KEY, NULL);
	g_object_unref (gconf);

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
e_book_client_set_self (EBookClient *client, EContact *contact, GError **error)
{
	GConfClient *gconf;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (contact != NULL, FALSE);
	g_return_val_if_fail (e_contact_get_const (contact, E_CONTACT_UID) != NULL, FALSE);

	gconf = gconf_client_get_default ();
	gconf_client_set_string (gconf, SELF_UID_KEY, e_contact_get_const (contact, E_CONTACT_UID), NULL);
	g_object_unref (gconf);

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
	GConfClient *gconf;
	gchar *uid;
	gboolean is_self;

	g_return_val_if_fail (contact && E_IS_CONTACT (contact), FALSE);

	gconf = gconf_client_get_default ();
	uid = gconf_client_get_string (gconf, SELF_UID_KEY, NULL);
	g_object_unref (gconf);

	is_self = uid && !g_strcmp0 (uid, e_contact_get_const (contact, E_CONTACT_UID));

	g_free (uid);

	return is_self;
}

static void
book_client_get_backend_property (EClient *client, const gchar *prop_name, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_client_proxy_call_string (client, prop_name, cancellable, callback, user_data, book_client_get_backend_property,
			e_gdbus_book_call_get_backend_property,
			NULL, NULL, e_gdbus_book_call_get_backend_property_finish, NULL, NULL);
}

static gboolean
book_client_get_backend_property_finish (EClient *client, GAsyncResult *result, gchar **prop_value, GError **error)
{
	return e_client_proxy_call_finish_string (client, result, prop_value, error, book_client_get_backend_property);
}

static gboolean
book_client_get_backend_property_sync (EClient *client, const gchar *prop_name, gchar **prop_value, GCancellable *cancellable, GError **error)
{
	EBookClient *book_client;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	book_client = E_BOOK_CLIENT (client);
	g_return_val_if_fail (book_client != NULL, FALSE);
	g_return_val_if_fail (book_client->priv != NULL, FALSE);

	if (!book_client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	return e_client_proxy_call_sync_string__string (client, prop_name, prop_value, cancellable, error, e_gdbus_book_call_get_backend_property_sync);
}

static void
book_client_set_backend_property (EClient *client, const gchar *prop_name, const gchar *prop_value, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	gchar **prop_name_value;

	prop_name_value = e_gdbus_book_encode_set_backend_property (prop_name, prop_value);

	e_client_proxy_call_strv (client, (const gchar * const *) prop_name_value, cancellable, callback, user_data, book_client_set_backend_property,
			e_gdbus_book_call_set_backend_property,
			e_gdbus_book_call_set_backend_property_finish, NULL, NULL, NULL, NULL);

	g_strfreev (prop_name_value);
}

static gboolean
book_client_set_backend_property_finish (EClient *client, GAsyncResult *result, GError **error)
{
	return e_client_proxy_call_finish_void (client, result, error, book_client_set_backend_property);
}

static gboolean
book_client_set_backend_property_sync (EClient *client, const gchar *prop_name, const gchar *prop_value, GCancellable *cancellable, GError **error)
{
	EBookClient *book_client;
	gboolean res;
	gchar **prop_name_value;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	book_client = E_BOOK_CLIENT (client);
	g_return_val_if_fail (book_client != NULL, FALSE);
	g_return_val_if_fail (book_client->priv != NULL, FALSE);

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
book_client_open (EClient *client, gboolean only_if_exists, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_client_proxy_call_boolean (client, only_if_exists, cancellable, callback, user_data, book_client_open,
			e_gdbus_book_call_open,
			e_gdbus_book_call_open_finish, NULL, NULL, NULL, NULL);
}

static gboolean
book_client_open_finish (EClient *client, GAsyncResult *result, GError **error)
{
	return e_client_proxy_call_finish_void (client, result, error, book_client_open);
}

static gboolean
book_client_open_sync (EClient *client, gboolean only_if_exists, GCancellable *cancellable, GError **error)
{
	EBookClient *book_client;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	book_client = E_BOOK_CLIENT (client);
	g_return_val_if_fail (book_client != NULL, FALSE);
	g_return_val_if_fail (book_client->priv != NULL, FALSE);

	if (!book_client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	return e_client_proxy_call_sync_boolean__void (client, only_if_exists, cancellable, error, e_gdbus_book_call_open_sync);
}

static void
book_client_remove (EClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_client_proxy_call_void (client, cancellable, callback, user_data, book_client_remove,
			e_gdbus_book_call_remove,
			e_gdbus_book_call_remove_finish, NULL, NULL, NULL, NULL);
}

static gboolean
book_client_remove_finish (EClient *client, GAsyncResult *result, GError **error)
{
	return e_client_proxy_call_finish_void (client, result, error, book_client_remove);
}

static gboolean
book_client_remove_sync (EClient *client, GCancellable *cancellable, GError **error)
{
	EBookClient *book_client;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	book_client = E_BOOK_CLIENT (client);
	g_return_val_if_fail (book_client != NULL, FALSE);
	g_return_val_if_fail (book_client->priv != NULL, FALSE);

	if (!book_client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	return e_client_proxy_call_sync_void__void (client, cancellable, error, e_gdbus_book_call_remove_sync);
}

static void
book_client_refresh (EClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_client_proxy_call_void (client, cancellable, callback, user_data, book_client_refresh,
			e_gdbus_book_call_refresh,
			e_gdbus_book_call_refresh_finish, NULL, NULL, NULL, NULL);
}

static gboolean
book_client_refresh_finish (EClient *client, GAsyncResult *result, GError **error)
{
	return e_client_proxy_call_finish_void (client, result, error, book_client_refresh);
}

static gboolean
book_client_refresh_sync (EClient *client, GCancellable *cancellable, GError **error)
{
	EBookClient *book_client;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);

	book_client = E_BOOK_CLIENT (client);
	g_return_val_if_fail (book_client != NULL, FALSE);
	g_return_val_if_fail (book_client->priv != NULL, FALSE);

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
e_book_client_add_contact (EBookClient *client, /* const */ EContact *contact, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	gchar *vcard, *gdbus_vcard = NULL;

	g_return_if_fail (contact != NULL);
	g_return_if_fail (E_IS_CONTACT (contact));

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	e_client_proxy_call_string (E_CLIENT (client), e_util_ensure_gdbus_string (vcard, &gdbus_vcard), cancellable, callback, user_data, e_book_client_add_contact,
			e_gdbus_book_call_add_contact,
			NULL, NULL, e_gdbus_book_call_add_contact_finish, NULL, NULL);

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
e_book_client_add_contact_finish (EBookClient *client, GAsyncResult *result, gchar **added_uid, GError **error)
{
	gboolean res;
	gchar *out_uid = NULL;

	res = e_client_proxy_call_finish_string (E_CLIENT (client), result, &out_uid, error, e_book_client_add_contact);

	if (res && out_uid && added_uid) {
		*added_uid = out_uid;
	} else {
		g_free (out_uid);
		if (added_uid)
			*added_uid = NULL;
	}

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
e_book_client_add_contact_sync (EBookClient *client, /* const */ EContact *contact, gchar **added_uid, GCancellable *cancellable, GError **error)
{
	gboolean res;
	gchar *vcard, *gdbus_vcard = NULL, *out_uid = NULL;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv != NULL, FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	res = e_client_proxy_call_sync_string__string (E_CLIENT (client), e_util_ensure_gdbus_string (vcard, &gdbus_vcard), &out_uid, cancellable, error, e_gdbus_book_call_add_contact_sync);

	if (res && out_uid && added_uid) {
		*added_uid = out_uid;
	} else {
		g_free (out_uid);
		if (added_uid)
			*added_uid = NULL;
	}

	g_free (vcard);
	g_free (gdbus_vcard);

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
e_book_client_modify_contact (EBookClient *client, /* const */ EContact *contact, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	gchar *vcard, *gdbus_vcard = NULL;

	g_return_if_fail (contact != NULL);
	g_return_if_fail (E_IS_CONTACT (contact));

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	e_client_proxy_call_string (E_CLIENT (client), e_util_ensure_gdbus_string (vcard, &gdbus_vcard), cancellable, callback, user_data, e_book_client_modify_contact,
			e_gdbus_book_call_modify_contact,
			e_gdbus_book_call_modify_contact_finish, NULL, NULL, NULL, NULL);

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
e_book_client_modify_contact_finish (EBookClient *client, GAsyncResult *result, GError **error)
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
e_book_client_modify_contact_sync (EBookClient *client, /* const */ EContact *contact, GCancellable *cancellable, GError **error)
{
	gboolean res;
	gchar *vcard, *gdbus_vcard = NULL;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv != NULL, FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	res = e_client_proxy_call_sync_string__void (E_CLIENT (client), e_util_ensure_gdbus_string (vcard, &gdbus_vcard), cancellable, error, e_gdbus_book_call_modify_contact_sync);

	g_free (vcard);
	g_free (gdbus_vcard);

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
e_book_client_remove_contact (EBookClient *client, /* const */ EContact *contact, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	gchar *uid;
	const gchar *lst[2];

	g_return_if_fail (contact != NULL);
	g_return_if_fail (E_IS_CONTACT (contact));

	uid = e_util_utf8_make_valid (e_contact_get_const ((EContact *) contact, E_CONTACT_UID));
	g_return_if_fail (uid != NULL);

	lst[0] = uid;
	lst[1] = NULL;

	e_client_proxy_call_strv (E_CLIENT (client), lst, cancellable, callback, user_data, e_book_client_remove_contact,
			e_gdbus_book_call_remove_contacts,
			e_gdbus_book_call_remove_contacts_finish, NULL, NULL, NULL, NULL);

	g_free (uid);
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
e_book_client_remove_contact_finish (EBookClient *client, GAsyncResult *result, GError **error)
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
e_book_client_remove_contact_sync (EBookClient *client, /* const */ EContact *contact, GCancellable *cancellable, GError **error)
{
	gboolean res;
	gchar *uid;
	const gchar *lst[2];

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv != NULL, FALSE);
	g_return_val_if_fail (contact != NULL, FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	uid = e_util_utf8_make_valid (e_contact_get_const ((EContact *) contact, E_CONTACT_UID));
	g_return_val_if_fail (uid != NULL, 0);

	lst[0] = uid;
	lst[1] = NULL;

	res = e_client_proxy_call_sync_strv__void (E_CLIENT (client), lst, cancellable, error, e_gdbus_book_call_remove_contacts_sync);

	g_free (uid);

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
e_book_client_remove_contact_by_uid (EBookClient *client, const gchar *uid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	gchar *safe_uid;
	const gchar *lst[2];

	g_return_if_fail (uid != NULL);

	safe_uid = e_util_utf8_make_valid (uid);
	g_return_if_fail (safe_uid != NULL);

	lst[0] = safe_uid;
	lst[1] = NULL;

	e_client_proxy_call_strv (E_CLIENT (client), lst, cancellable, callback, user_data, e_book_client_remove_contact_by_uid,
			e_gdbus_book_call_remove_contacts,
			e_gdbus_book_call_remove_contacts_finish, NULL, NULL, NULL, NULL);

	g_free (safe_uid);
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
e_book_client_remove_contact_by_uid_finish (EBookClient *client, GAsyncResult *result, GError **error)
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
e_book_client_remove_contact_by_uid_sync (EBookClient *client, const gchar *uid, GCancellable *cancellable, GError **error)
{
	gboolean res;
	gchar *safe_uid;
	const gchar *lst[2];

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv != NULL, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	safe_uid = e_util_utf8_make_valid (uid);
	g_return_val_if_fail (safe_uid != NULL, FALSE);

	lst[0] = safe_uid;
	lst[1] = NULL;

	res = e_client_proxy_call_sync_strv__void (E_CLIENT (client), lst, cancellable, error, e_gdbus_book_call_remove_contacts_sync);

	g_free (safe_uid);

	return res;
}

/**
 * e_book_client_remove_contacts:
 * @client: an #EBookClient
 * @uids: a #GSList of UIDs to remove
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
e_book_client_remove_contacts (EBookClient *client, const GSList *uids, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	gchar **lst;

	g_return_if_fail (uids != NULL);

	lst = e_client_util_slist_to_strv (uids);
	g_return_if_fail (lst != NULL);

	e_client_proxy_call_strv (E_CLIENT (client), (const gchar * const *) lst, cancellable, callback, user_data, e_book_client_remove_contacts,
			e_gdbus_book_call_remove_contacts,
			e_gdbus_book_call_remove_contacts_finish, NULL, NULL, NULL, NULL);

	g_strfreev (lst);
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
e_book_client_remove_contacts_finish (EBookClient *client, GAsyncResult *result, GError **error)
{
	return e_client_proxy_call_finish_void (E_CLIENT (client), result, error, e_book_client_remove_contacts);
}

/**
 * e_book_client_remove_contacts_sync:
 * @client: an #EBookClient
 * @uids: a #GSList of UIDs to remove
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
e_book_client_remove_contacts_sync (EBookClient *client, const GSList *uids, GCancellable *cancellable, GError **error)
{
	gboolean res;
	gchar **lst;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv != NULL, FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	lst = e_client_util_slist_to_strv (uids);
	g_return_val_if_fail (lst != NULL, FALSE);

	res = e_client_proxy_call_sync_strv__void (E_CLIENT (client), (const gchar * const *) lst, cancellable, error, e_gdbus_book_call_remove_contacts_sync);

	g_strfreev (lst);

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
e_book_client_get_contact (EBookClient *client, const gchar *uid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	gchar *safe_uid;

	g_return_if_fail (uid != NULL);

	safe_uid = e_util_utf8_make_valid (uid);
	g_return_if_fail (safe_uid != NULL);
	
	e_client_proxy_call_string (E_CLIENT (client), safe_uid, cancellable, callback, user_data, e_book_client_get_contact,
			e_gdbus_book_call_get_contact,
			NULL, NULL, e_gdbus_book_call_get_contact_finish, NULL, NULL);

	g_free (safe_uid);
}

/**
 * e_book_client_get_contact_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @contact: (out) an #EContact for previously given uid
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
e_book_client_get_contact_finish (EBookClient *client, GAsyncResult *result, EContact **contact, GError **error)
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
 * @contact: (out) an #EContact for given @uid
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
e_book_client_get_contact_sync (EBookClient *client, const gchar *uid, EContact **contact, GCancellable *cancellable, GError **error)
{
	gboolean res;
	gchar *vcard = NULL, *safe_uid;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv != NULL, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (contact != NULL, FALSE);

	if (!client->priv->gdbus_book) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	safe_uid = e_util_utf8_make_valid (uid);
	g_return_val_if_fail (safe_uid != NULL, FALSE);

	res = e_client_proxy_call_sync_string__string (E_CLIENT (client), safe_uid, &vcard, cancellable, error, e_gdbus_book_call_get_contact_sync);

	if (vcard && res)
		*contact = e_contact_new_from_vcard (vcard);
	else
		*contact = NULL;

	g_free (safe_uid);
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
e_book_client_get_contacts (EBookClient *client, const gchar *sexp, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	gchar *gdbus_sexp = NULL;

	g_return_if_fail (sexp != NULL);

	e_client_proxy_call_string (E_CLIENT (client), e_util_ensure_gdbus_string (sexp, &gdbus_sexp), cancellable, callback, user_data, e_book_client_get_contacts,
			e_gdbus_book_call_get_contact_list,
			NULL, NULL, NULL, e_gdbus_book_call_get_contact_list_finish, NULL);

	g_free (gdbus_sexp);
}

/**
 * e_book_client_get_contacts_finish:
 * @client: an #EBookClient
 * @result: a #GAsyncResult
 * @contacts: (out) a #GSList of matched #EContact-s
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
e_book_client_get_contacts_finish (EBookClient *client, GAsyncResult *result, GSList **contacts, GError **error)
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
 * @contacts: (out) a #GSList of matched #EContact-s
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
e_book_client_get_contacts_sync (EBookClient *client, const gchar *sexp, GSList **contacts, GCancellable *cancellable, GError **error)
{
	gboolean res;
	gchar *gdbus_sexp = NULL;
	gchar **vcards = NULL;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv != NULL, FALSE);
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
e_book_client_get_view (EBookClient *client, const gchar *sexp, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	gchar *gdbus_sexp = NULL;

	g_return_if_fail (sexp != NULL);

	e_client_proxy_call_string (E_CLIENT (client), e_util_ensure_gdbus_string (sexp, &gdbus_sexp), cancellable, callback, user_data, e_book_client_get_view,
			e_gdbus_book_call_get_view,
			NULL, NULL, e_gdbus_book_call_get_view_finish, NULL, NULL);

	g_free (gdbus_sexp);
}

static gboolean
complete_get_view (EBookClient *client, gboolean res, gchar *view_path, EBookClientView **view, GError **error)
{
	g_return_val_if_fail (view != NULL, FALSE);

	if (view_path && res && book_factory_proxy) {
		GError *local_error = NULL;
		EGdbusBookView *gdbus_bookview;

		gdbus_bookview = e_gdbus_book_view_proxy_new_sync (g_dbus_proxy_get_connection (G_DBUS_PROXY (book_factory_proxy)),
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
 * @view: (out) an #EBookClientView
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
e_book_client_get_view_finish (EBookClient *client, GAsyncResult *result, EBookClientView **view, GError **error)
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
e_book_client_get_view_sync (EBookClient *client, const gchar *sexp, EBookClientView **view, GCancellable *cancellable, GError **error)
{
	gboolean res;
	gchar *gdbus_sexp = NULL;
	gchar *view_path = NULL;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv != NULL, FALSE);
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

	g_return_val_if_fail (client != NULL, NULL);

	book_client = E_BOOK_CLIENT (client);
	g_return_val_if_fail (book_client != NULL, NULL);
	g_return_val_if_fail (book_client->priv != NULL, NULL);

	return book_client->priv->gdbus_book;
}

static void
book_client_unwrap_dbus_error (EClient *client, GError *dbus_error, GError **out_error)
{
	unwrap_dbus_error (dbus_error, out_error);
}

static void
book_client_handle_authentication (EClient *client, const ECredentials *credentials)
{
	EBookClient *book_client;
	GError *error = NULL;
	gchar **strv;

	g_return_if_fail (client != NULL);
	g_return_if_fail (credentials != NULL);

	book_client = E_BOOK_CLIENT (client);
	g_return_if_fail (book_client != NULL);
	g_return_if_fail (book_client->priv != NULL);

	if (!book_client->priv->gdbus_book)
		return;

	strv = e_credentials_to_strv (credentials);
	g_return_if_fail (strv != NULL);

	e_gdbus_book_call_authenticate_user_sync (book_client->priv->gdbus_book, (const gchar * const *) strv, NULL, &error);

	g_strfreev (strv);

	if (error) {
		g_debug ("%s: Failed to authenticate user: %s", G_STRFUNC, error->message);
		g_error_free (error);
	}
}

static gchar *
book_client_retrieve_capabilities (EClient *client)
{
	EBookClient *book_client;
	GError *error = NULL;
	gchar *capabilities = NULL;

	g_return_val_if_fail (client != NULL, NULL);

	book_client = E_BOOK_CLIENT (client);
	g_return_val_if_fail (book_client != NULL, NULL);
	g_return_val_if_fail (book_client->priv != NULL, NULL);

	if (!book_client->priv->gdbus_book)
		return NULL;

	e_gdbus_book_call_get_backend_property_sync (book_client->priv->gdbus_book, CLIENT_BACKEND_PROPERTY_CAPABILITIES, &capabilities, NULL, &error);

	if (error) {
		g_debug ("%s: Failed to retrieve capabilitites: %s", G_STRFUNC, error->message);
		g_error_free (error);
	}

	return capabilities;
}

static void
e_book_client_init (EBookClient *client)
{
	LOCK_FACTORY ();
	active_book_clients++;
	UNLOCK_FACTORY ();

	client->priv = G_TYPE_INSTANCE_GET_PRIVATE (client, E_TYPE_BOOK_CLIENT, EBookClientPrivate);
}

static void
book_client_dispose (GObject *object)
{
	EClient *client;

	client = E_CLIENT (object);
	g_return_if_fail (client != NULL);
	g_return_if_fail (client->priv != NULL);

	e_client_cancel_all (client);

	gdbus_book_client_disconnect (E_BOOK_CLIENT (client));

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_book_client_parent_class)->dispose (object);
}

static void
book_client_finalize (GObject *object)
{
	EBookClient *client;

	client = E_BOOK_CLIENT (object);
	g_return_if_fail (client != NULL);
	g_return_if_fail (client->priv != NULL);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_client_parent_class)->finalize (object);

	LOCK_FACTORY ();
	active_book_clients--;
	if (!active_book_clients)
		gdbus_book_factory_proxy_disconnect (NULL);
	UNLOCK_FACTORY ();
}

static void
e_book_client_class_init (EBookClientClass *klass)
{
	GObjectClass *object_class;
	EClientClass *client_class;

	g_type_class_add_private (klass, sizeof (EBookClientPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = book_client_dispose;
	object_class->finalize = book_client_finalize;

	client_class = E_CLIENT_CLASS (klass);
	client_class->get_dbus_proxy			= book_client_get_dbus_proxy;
	client_class->unwrap_dbus_error			= book_client_unwrap_dbus_error;
	client_class->handle_authentication		= book_client_handle_authentication;
	client_class->retrieve_capabilities		= book_client_retrieve_capabilities;
	client_class->get_backend_property		= book_client_get_backend_property;
	client_class->get_backend_property_finish	= book_client_get_backend_property_finish;
	client_class->get_backend_property_sync		= book_client_get_backend_property_sync;
	client_class->set_backend_property		= book_client_set_backend_property;
	client_class->set_backend_property_finish	= book_client_set_backend_property_finish;
	client_class->set_backend_property_sync		= book_client_set_backend_property_sync;
	client_class->open				= book_client_open;
	client_class->open_finish			= book_client_open_finish;
	client_class->open_sync				= book_client_open_sync;
	client_class->remove				= book_client_remove;
	client_class->remove_finish			= book_client_remove_finish;
	client_class->remove_sync			= book_client_remove_sync;
	client_class->refresh				= book_client_refresh;
	client_class->refresh_finish			= book_client_refresh_finish;
	client_class->refresh_sync			= book_client_refresh_sync;
}

/*
 * e-cal-client.c
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

#include <libedataserver/e-client-private.h>

#include "e-cal-client.h"
#include "e-cal-client-view-private.h"
#include "e-cal-component.h"
#include "e-cal-check-timezones.h"
#include "e-cal-time-util.h"
#include "e-cal-types.h"

#include "e-gdbus-cal.h"
#include "e-gdbus-cal-factory.h"
#include "e-gdbus-cal-view.h"

#define E_CAL_CLIENT_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL_CLIENT, ECalClientPrivate))

struct _ECalClientPrivate {
	/* GDBus data */
	GDBusProxy *gdbus_cal;
	guint gone_signal_id;

	ECalClientSourceType source_type;
	icaltimezone *default_zone;
	gchar *cache_dir;

	GMutex *zone_cache_lock;
	GHashTable *zone_cache;
};

enum {
	FREE_BUSY_DATA,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (ECalClient, e_cal_client, E_TYPE_CLIENT)

/*
 * Well-known calendar backend properties:
 * @CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS: Contains default calendar's email
 *   address suggested by the backend.
 * @CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS: Contains default alarm email
 *   address suggested by the backend.
 * @CAL_BACKEND_PROPERTY_DEFAULT_OBJECT: Contains iCal component string
 *   of an #icalcomponent with the default values for properties needed.
 *   Preferred way of retrieving this property is by
 *   calling e_cal_client_get_default_object().
 *
 * See also: @CLIENT_BACKEND_PROPERTY_OPENED, @CLIENT_BACKEND_PROPERTY_OPENING,
 *   @CLIENT_BACKEND_PROPERTY_ONLINE, @CLIENT_BACKEND_PROPERTY_READONLY
 *   @CLIENT_BACKEND_PROPERTY_CACHE_DIR, @CLIENT_BACKEND_PROPERTY_CAPABILITIES
 */

/**
 * e_cal_client_source_type_enum_get_type:
 *
 * Registers the #ECalClientSourceTypeEnum type with glib.
 *
 * Returns: the ID of the #ECalClientSourceTypeEnum type.
 */
GType
e_cal_client_source_type_enum_get_type (void)
{
	static volatile gsize enum_type__volatile = 0;

	if (g_once_init_enter (&enum_type__volatile)) {
		GType enum_type;
		static GEnumValue values[] = {
			{ E_CAL_CLIENT_SOURCE_TYPE_EVENTS, "Events",  "Events"  },
			{ E_CAL_CLIENT_SOURCE_TYPE_TASKS,  "Tasks",   "Tasks"   },
			{ E_CAL_CLIENT_SOURCE_TYPE_MEMOS,  "Memos",   "Memos"   },
			{ E_CAL_CLIENT_SOURCE_TYPE_LAST,   "Invalid", "Invalid" },
			{ -1, NULL, NULL}
		};

		enum_type = g_enum_register_static ("ECalClientSourceTypeEnum", values);
		g_once_init_leave (&enum_type__volatile, enum_type);
	}

	return enum_type__volatile;
}

GQuark
e_cal_client_error_quark (void)
{
	static GQuark q = 0;
	if (q == 0)
		q = g_quark_from_static_string ("e-cal-client-error-quark");

	return q;
}

/**
 * e_cal_client_error_to_string:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
const gchar *
e_cal_client_error_to_string (ECalClientError code)
{
	switch (code) {
	case E_CAL_CLIENT_ERROR_NO_SUCH_CALENDAR:
		return _("No such calendar");
	case E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND:
		return _("Object not found");
	case E_CAL_CLIENT_ERROR_INVALID_OBJECT:
		return _("Invalid object");
	case E_CAL_CLIENT_ERROR_UNKNOWN_USER:
		return _("Unknown user");
	case E_CAL_CLIENT_ERROR_OBJECT_ID_ALREADY_EXISTS:
		return _("Object ID already exists");
	case E_CAL_CLIENT_ERROR_INVALID_RANGE:
		return _("Invalid range");
	}

	return _("Unknown error");
}

/**
 * e_cal_client_error_create:
 * @code: an #ECalClientError code to create
 * @custom_msg: custom message to use for the error; can be %NULL
 *
 * Returns: a new #GError containing an E_CAL_CLIENT_ERROR of the given
 * @code. If the @custom_msg is NULL, then the error message is
 * the one returned from e_cal_client_error_to_string() for the @code,
 * otherwise the given message is used.
 *
 * Returned pointer should be freed with g_error_free().
 *
 * Since: 3.2
 **/
GError *
e_cal_client_error_create (ECalClientError code,
                           const gchar *custom_msg)
{
	return g_error_new_literal (E_CAL_CLIENT_ERROR, code, custom_msg ? custom_msg : e_cal_client_error_to_string (code));
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
	#define err(a,b) "org.gnome.evolution.dataserver.Calendar." a, b
	static EClientErrorsList cal_errors[] = {
		{ err ("Success",				-1) },
		{ err ("ObjectNotFound",			E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND) },
		{ err ("InvalidObject",				E_CAL_CLIENT_ERROR_INVALID_OBJECT) },
		{ err ("ObjectIdAlreadyExists",			E_CAL_CLIENT_ERROR_OBJECT_ID_ALREADY_EXISTS) },
		{ err ("NoSuchCal",				E_CAL_CLIENT_ERROR_NO_SUCH_CALENDAR) },
		{ err ("UnknownUser",				E_CAL_CLIENT_ERROR_UNKNOWN_USER) },
		{ err ("InvalidRange",				E_CAL_CLIENT_ERROR_INVALID_RANGE) },
	}, cl_errors[] = {
		{ err ("Busy",					E_CLIENT_ERROR_BUSY) },
		{ err ("InvalidArg",				E_CLIENT_ERROR_INVALID_ARG) },
		{ err ("RepositoryOffline",			E_CLIENT_ERROR_REPOSITORY_OFFLINE) },
		{ err ("OfflineUnavailable",			E_CLIENT_ERROR_OFFLINE_UNAVAILABLE) },
		{ err ("PermissionDenied",			E_CLIENT_ERROR_PERMISSION_DENIED) },
		{ err ("AuthenticationFailed",			E_CLIENT_ERROR_AUTHENTICATION_FAILED) },
		{ err ("AuthenticationRequired",		E_CLIENT_ERROR_AUTHENTICATION_REQUIRED) },
		{ err ("CouldNotCancel",			E_CLIENT_ERROR_COULD_NOT_CANCEL) },
		{ err ("NotSupported",				E_CLIENT_ERROR_NOT_SUPPORTED) },
		{ err ("UnsupportedAuthenticationMethod",	E_CLIENT_ERROR_UNSUPPORTED_AUTHENTICATION_METHOD) },
		{ err ("TLSNotAvailable",			E_CLIENT_ERROR_TLS_NOT_AVAILABLE) },
		{ err ("SearchSizeLimitExceeded",		E_CLIENT_ERROR_SEARCH_SIZE_LIMIT_EXCEEDED) },
		{ err ("SearchTimeLimitExceeded",		E_CLIENT_ERROR_SEARCH_TIME_LIMIT_EXCEEDED) },
		{ err ("InvalidQuery",				E_CLIENT_ERROR_INVALID_QUERY) },
		{ err ("QueryRefused",				E_CLIENT_ERROR_QUERY_REFUSED) },
		{ err ("NotOpened",				E_CLIENT_ERROR_NOT_OPENED) },
		{ err ("UnsupportedField",			E_CLIENT_ERROR_OTHER_ERROR) },
		{ err ("UnsupportedMethod",			E_CLIENT_ERROR_OTHER_ERROR) },
		{ err ("InvalidServerVersion",			E_CLIENT_ERROR_OTHER_ERROR) },
		{ err ("OtherError",				E_CLIENT_ERROR_OTHER_ERROR) }
	};
	#undef err

	if (error == NULL)
		return TRUE;

	if (!e_client_util_unwrap_dbus_error (error, client_error, cal_errors, G_N_ELEMENTS (cal_errors), E_CAL_CLIENT_ERROR, TRUE))
		e_client_util_unwrap_dbus_error (error, client_error, cl_errors, G_N_ELEMENTS (cl_errors), E_CLIENT_ERROR, FALSE);

	return FALSE;
}

static void
set_proxy_gone_error (GError **error)
{
	/* do not translate this string, it should ideally never happen */
	g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_DBUS_ERROR, "D-Bus calendar proxy gone");
}

static guint active_cal_clients = 0, cal_connection_closed_id = 0;
static EGdbusCalFactory *cal_factory_proxy = NULL;
static GStaticRecMutex cal_factory_proxy_lock = G_STATIC_REC_MUTEX_INIT;
#define LOCK_FACTORY()   g_static_rec_mutex_lock (&cal_factory_proxy_lock)
#define UNLOCK_FACTORY() g_static_rec_mutex_unlock (&cal_factory_proxy_lock)

static void gdbus_cal_factory_proxy_closed_cb (GDBusConnection *connection, gboolean remote_peer_vanished, GError *error, gpointer user_data);

static void
gdbus_cal_factory_proxy_disconnect (GDBusConnection *connection)
{
	LOCK_FACTORY ();

	if (!connection && cal_factory_proxy)
		connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (cal_factory_proxy));

	if (connection && cal_connection_closed_id) {
		g_dbus_connection_signal_unsubscribe (connection, cal_connection_closed_id);
		g_signal_handlers_disconnect_by_func (connection, gdbus_cal_factory_proxy_closed_cb, NULL);
	}

	if (cal_factory_proxy)
		g_object_unref (cal_factory_proxy);

	cal_connection_closed_id = 0;
	cal_factory_proxy = NULL;

	UNLOCK_FACTORY ();
}

static void
gdbus_cal_factory_proxy_closed_cb (GDBusConnection *connection,
                                   gboolean remote_peer_vanished,
                                   GError *error,
                                   gpointer user_data)
{
	GError *err = NULL;

	LOCK_FACTORY ();

	gdbus_cal_factory_proxy_disconnect (connection);

	if (error)
		unwrap_dbus_error (g_error_copy (error), &err);

	if (err) {
		g_debug ("GDBus connection is closed%s: %s", remote_peer_vanished ? ", remote peer vanished" : "", err->message);
		g_error_free (err);
	} else if (active_cal_clients) {
		g_debug ("GDBus connection is closed%s", remote_peer_vanished ? ", remote peer vanished" : "");
	}

	UNLOCK_FACTORY ();
}

static void
gdbus_cal_factory_connection_gone_cb (GDBusConnection *connection,
                                      const gchar *sender_name,
                                      const gchar *object_path,
                                      const gchar *interface_name,
                                      const gchar *signal_name,
                                      GVariant *parameters,
                                      gpointer user_data)
{
	/* signal subscription takes care of correct parameters,
	 * thus just do what is to be done here */
	gdbus_cal_factory_proxy_closed_cb (connection, TRUE, NULL, user_data);
}

static gboolean
gdbus_cal_factory_activate (GError **error)
{
	GDBusConnection *connection;

	LOCK_FACTORY ();

	if (G_LIKELY (cal_factory_proxy)) {
		UNLOCK_FACTORY ();
		return TRUE;
	}

	cal_factory_proxy = e_gdbus_cal_factory_proxy_new_for_bus_sync (
		G_BUS_TYPE_SESSION,
		G_DBUS_PROXY_FLAGS_NONE,
		CALENDAR_DBUS_SERVICE_NAME,
		"/org/gnome/evolution/dataserver/CalendarFactory",
		NULL,
		error);

	if (!cal_factory_proxy) {
		UNLOCK_FACTORY ();
		return FALSE;
	}

	connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (cal_factory_proxy));
	cal_connection_closed_id = g_dbus_connection_signal_subscribe (
		connection,
		NULL,						/* sender */
		"org.freedesktop.DBus",				/* interface */
		"NameOwnerChanged",				/* member */
		"/org/freedesktop/DBus",			/* object_path */
		"org.gnome.evolution.dataserver.Calendar",	/* arg0 */
		G_DBUS_SIGNAL_FLAGS_NONE,
		gdbus_cal_factory_connection_gone_cb, NULL, NULL);

	g_signal_connect (connection, "closed", G_CALLBACK (gdbus_cal_factory_proxy_closed_cb), NULL);

	UNLOCK_FACTORY ();

	return TRUE;
}

static void gdbus_cal_client_disconnect (ECalClient *client);

/*
 * Called when the calendar server dies.
 */
static void
gdbus_cal_client_closed_cb (GDBusConnection *connection,
                            gboolean remote_peer_vanished,
                            GError *error,
                            ECalClient *client)
{
	GError *err = NULL;

	g_assert (E_IS_CAL_CLIENT (client));

	if (error)
		unwrap_dbus_error (g_error_copy (error), &err);

	if (err) {
		g_debug (G_STRLOC ": ECalClient GDBus connection is closed%s: %s", remote_peer_vanished ? ", remote peer vanished" : "", err->message);
		g_error_free (err);
	} else {
		g_debug (G_STRLOC ": ECalClient GDBus connection is closed%s", remote_peer_vanished ? ", remote peer vanished" : "");
	}

	gdbus_cal_client_disconnect (client);

	e_client_emit_backend_died (E_CLIENT (client));
}

static void
gdbus_cal_client_connection_gone_cb (GDBusConnection *connection,
                                     const gchar *sender_name,
                                     const gchar *object_path,
                                     const gchar *interface_name,
                                     const gchar *signal_name,
                                     GVariant *parameters,
                                     gpointer user_data)
{
	/* signal subscription takes care of correct parameters,
	 * thus just do what is to be done here */
	gdbus_cal_client_closed_cb (connection, TRUE, NULL, user_data);
}

static void
gdbus_cal_client_disconnect (ECalClient *client)
{
	g_return_if_fail (E_IS_CAL_CLIENT (client));

	/* Ensure that everything relevant is NULL */
	LOCK_FACTORY ();

	if (client->priv->gdbus_cal) {
		GDBusConnection *connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (client->priv->gdbus_cal));

		g_signal_handlers_disconnect_by_func (connection, gdbus_cal_client_closed_cb, client);
		g_dbus_connection_signal_unsubscribe (connection, client->priv->gone_signal_id);
		client->priv->gone_signal_id = 0;

		e_gdbus_cal_call_close_sync (client->priv->gdbus_cal, NULL, NULL);
		g_object_unref (client->priv->gdbus_cal);
		client->priv->gdbus_cal = NULL;
	}

	UNLOCK_FACTORY ();
}

static void
backend_error_cb (EGdbusCal *object,
                  const gchar *message,
                  ECalClient *client)
{
	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (message != NULL);

	e_client_emit_backend_error (E_CLIENT (client), message);
}

static void
readonly_cb (EGdbusCal *object,
             gboolean readonly,
             ECalClient *client)
{
	g_return_if_fail (E_IS_CAL_CLIENT (client));

	e_client_set_readonly (E_CLIENT (client), readonly);
}

static void
online_cb (EGdbusCal *object,
           gboolean is_online,
           ECalClient *client)
{
	g_return_if_fail (E_IS_CAL_CLIENT (client));

	e_client_set_online (E_CLIENT (client), is_online);
}

static void
opened_cb (EGdbusCal *object,
           const gchar * const *error_strv,
           ECalClient *client)
{
	GError *error = NULL;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (error_strv != NULL);
	g_return_if_fail (e_gdbus_templates_decode_error (error_strv, &error));

	e_client_emit_opened (E_CLIENT (client), error);

	if (error)
		g_error_free (error);
}

static void
free_busy_data_cb (EGdbusCal *object,
                   const gchar * const *free_busy_strv,
                   ECalClient *client)
{
	GSList *ecalcomps = NULL;
	gint ii;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (free_busy_strv != NULL);

	for (ii = 0; free_busy_strv[ii]; ii++) {
		ECalComponent *comp;
		icalcomponent *icalcomp;
		icalcomponent_kind kind;

		icalcomp = icalcomponent_new_from_string (free_busy_strv[ii]);
		if (!icalcomp)
			continue;

		kind = icalcomponent_isa (icalcomp);
		if (kind == ICAL_VFREEBUSY_COMPONENT) {
			comp = e_cal_component_new ();
			if (!e_cal_component_set_icalcomponent (comp, icalcomp)) {
				icalcomponent_free (icalcomp);
				g_object_unref (G_OBJECT (comp));
				continue;
			}

			ecalcomps = g_slist_prepend (ecalcomps, comp);
		} else {
			icalcomponent_free (icalcomp);
		}
	}

	ecalcomps = g_slist_reverse (ecalcomps);

	g_signal_emit (client, signals[FREE_BUSY_DATA], 0, ecalcomps);

	e_client_util_free_object_slist (ecalcomps);
}

static void
backend_property_changed_cb (EGdbusCal *object,
                             const gchar * const *name_value_strv,
                             ECalClient *client)
{
	gchar *prop_name = NULL, *prop_value = NULL;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (name_value_strv != NULL);
	g_return_if_fail (e_gdbus_templates_decode_two_strings (name_value_strv, &prop_name, &prop_value));
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (*prop_name);
	g_return_if_fail (prop_value != NULL);

	e_client_emit_backend_property_changed (E_CLIENT (client), prop_name, prop_value);

	g_free (prop_name);
	g_free (prop_value);
}

static EDataCalObjType
convert_type (ECalClientSourceType type)
{
	switch (type) {
	case E_CAL_CLIENT_SOURCE_TYPE_EVENTS:
		return Event;
	case E_CAL_CLIENT_SOURCE_TYPE_TASKS:
		return Todo;
	case E_CAL_CLIENT_SOURCE_TYPE_MEMOS:
		return Journal;
	default:
		return AnyType;
	}

	return AnyType;
}

/*
 * Converts a GSList of icalcomponents into a NULL-terminated array of
 * valid UTF-8 strings, suitable for sending over DBus.
 */
static gchar **
icalcomponent_slist_to_utf8_icomp_array (GSList *icalcomponents)
{
	gchar **array;
	const GSList *l;
	gint i = 0;

	array = g_new0 (gchar *, g_slist_length (icalcomponents) + 1);
	for (l = icalcomponents; l != NULL; l = l->next) {
		gchar *comp_str = icalcomponent_as_ical_string_r ((icalcomponent *) l->data);
		array[i++] = e_util_utf8_make_valid (comp_str);
		g_free (comp_str);
	}

	return array;
}

/*
 * Converts a GSList of icalcomponents into a GSList of strings.
 */
static GSList *
icalcomponent_slist_to_string_slist (GSList *icalcomponents)
{
	GSList *strings = NULL;
	const GSList *l;

	for (l = icalcomponents; l != NULL; l = l->next) {
		strings = g_slist_prepend (strings, icalcomponent_as_ical_string_r ((icalcomponent *) l->data));
	}

	return g_slist_reverse (strings);
}

/**
 * e_cal_client_new:
 * @source: An #ESource pointer
 * @source_type: source type of the calendar
 * @error: A #GError pointer
 *
 * Creates a new #ECalClient corresponding to the given source.  There are
 * only two operations that are valid on this calendar at this point:
 * e_client_open(), and e_client_remove().
 *
 * Returns: a new but unopened #ECalClient.
 *
 * Since: 3.2
 **/
ECalClient *
e_cal_client_new (ESource *source,
                  ECalClientSourceType source_type,
                  GError **error)
{
	ECalClient *client;
	GError *err = NULL;
	GDBusConnection *connection;
	const gchar *uid;
	gchar **strv;
	gchar *path = NULL;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (source_type == E_CAL_CLIENT_SOURCE_TYPE_EVENTS || source_type == E_CAL_CLIENT_SOURCE_TYPE_TASKS || source_type == E_CAL_CLIENT_SOURCE_TYPE_MEMOS, NULL);

	LOCK_FACTORY ();
	if (!gdbus_cal_factory_activate (&err)) {
		UNLOCK_FACTORY ();
		if (err) {
			unwrap_dbus_error (err, &err);
			g_warning ("%s: Failed to run calendar factory: %s", G_STRFUNC, err->message);
			g_propagate_error (error, err);
		} else {
			g_warning ("%s: Failed to run calendar factory: Unknown error", G_STRFUNC);
			g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_DBUS_ERROR, _("Failed to run calendar factory"));
		}

		return NULL;
	}

	uid = e_source_get_uid (source);
	strv = e_gdbus_cal_factory_encode_get_cal (uid, convert_type (source_type));
	if (!strv) {
		g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_OTHER_ERROR, _("Other error"));
		return NULL;
	}

	client = g_object_new (E_TYPE_CAL_CLIENT, "source", source, NULL);
	client->priv->source_type = source_type;

	UNLOCK_FACTORY ();

	if (!e_gdbus_cal_factory_call_get_cal_sync (G_DBUS_PROXY (cal_factory_proxy), (const gchar * const *) strv, &path, NULL, &err)) {
		unwrap_dbus_error (err, &err);
		g_strfreev (strv);
		g_warning ("%s: Cannot get calendar from factory: %s", G_STRFUNC, err ? err->message : "[no error]");
		if (err)
			g_propagate_error (error, err);
		g_object_unref (client);

		return NULL;
	}

	g_strfreev (strv);

	client->priv->gdbus_cal = G_DBUS_PROXY (
		e_gdbus_cal_proxy_new_sync (g_dbus_proxy_get_connection (G_DBUS_PROXY (cal_factory_proxy)),
		G_DBUS_PROXY_FLAGS_NONE,
		CALENDAR_DBUS_SERVICE_NAME,
		path,
		NULL,
		&err));

	if (!client->priv->gdbus_cal) {
		g_free (path);
		unwrap_dbus_error (err, &err);
		g_warning ("%s: Cannot create calendar proxy: %s", G_STRFUNC, err ? err->message : "Unknown error");
		if (err)
			g_propagate_error (error, err);

		g_object_unref (client);

		return NULL;
	}

	g_free (path);

	connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (client->priv->gdbus_cal));
	client->priv->gone_signal_id = g_dbus_connection_signal_subscribe (
		connection,
		"org.freedesktop.DBus",				/* sender */
		"org.freedesktop.DBus",				/* interface */
		"NameOwnerChanged",				/* member */
		"/org/freedesktop/DBus",			/* object_path */
		"org.gnome.evolution.dataserver.Calendar",	/* arg0 */
		G_DBUS_SIGNAL_FLAGS_NONE,
		gdbus_cal_client_connection_gone_cb, client, NULL);

	g_signal_connect (connection, "closed", G_CALLBACK (gdbus_cal_client_closed_cb), client);

	g_signal_connect (client->priv->gdbus_cal, "backend_error", G_CALLBACK (backend_error_cb), client);
	g_signal_connect (client->priv->gdbus_cal, "readonly", G_CALLBACK (readonly_cb), client);
	g_signal_connect (client->priv->gdbus_cal, "online", G_CALLBACK (online_cb), client);
	g_signal_connect (client->priv->gdbus_cal, "opened", G_CALLBACK (opened_cb), client);
	g_signal_connect (client->priv->gdbus_cal, "free-busy-data", G_CALLBACK (free_busy_data_cb), client);
	g_signal_connect (client->priv->gdbus_cal, "backend-property-changed", G_CALLBACK (backend_property_changed_cb), client);

	return client;
}

/**
 * e_cal_client_get_source_type:
 * @client: A calendar client.
 *
 * Gets the source type of the calendar client.
 *
 * Returns: an #ECalClientSourceType value corresponding
 * to the source type of the calendar client.
 *
 * Since: 3.2
 **/
ECalClientSourceType
e_cal_client_get_source_type (ECalClient *client)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), E_CAL_CLIENT_SOURCE_TYPE_LAST);

	return client->priv->source_type;
}

/**
 * e_cal_client_get_local_attachment_store:
 * @client: A calendar client.
 *
 * Queries the URL where the calendar attachments are
 * serialized in the local filesystem. This enable clients
 * to operate with the reference to attachments rather than the data itself
 * unless it specifically uses the attachments for open/sending
 * operations.
 *
 * Returns: The URL where the attachments are serialized in the
 * local filesystem.
 *
 * Since: 3.2
 **/
const gchar *
e_cal_client_get_local_attachment_store (ECalClient *client)
{
	gchar *cache_dir = NULL;
	GError *error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), NULL);

	if (client->priv->cache_dir || !client->priv->gdbus_cal)
		return client->priv->cache_dir;

	cache_dir = e_client_get_backend_property_from_cache (E_CLIENT (client), CLIENT_BACKEND_PROPERTY_CACHE_DIR);
	if (!cache_dir)
		e_gdbus_cal_call_get_backend_property_sync (client->priv->gdbus_cal, CLIENT_BACKEND_PROPERTY_CACHE_DIR, &cache_dir, NULL, &error);

	if (error == NULL) {
		client->priv->cache_dir = cache_dir;
	} else {
		unwrap_dbus_error (error, &error);
		g_warning ("%s", error->message);
		g_error_free (error);
	}

	return client->priv->cache_dir;
}

/* icaltimezone_copy does a shallow copy while icaltimezone_free tries to free the entire 
 * the contents inside the structure with libical 0.43. Use this, till eds allows older libical.
*/
static icaltimezone *
copy_timezone (icaltimezone *ozone)
{
	icaltimezone *zone = NULL;
	const gchar *tzid;

	tzid = icaltimezone_get_tzid (ozone);

	if (g_strcmp0 (tzid, "UTC") != 0) {
		icalcomponent *comp;

		comp = icaltimezone_get_component (ozone);
		if (comp) {
			zone = icaltimezone_new ();
			icaltimezone_set_component (zone, icalcomponent_new_clone (comp));
		}
	}

	if (!zone)
		zone = icaltimezone_get_utc_timezone ();

	return zone;
}

/**
 * e_cal_client_set_default_timezone:
 * @client: A calendar client.
 * @zone: A timezone object.
 *
 * Sets the default timezone to use to resolve DATE and floating DATE-TIME
 * values. This will typically be from the user's timezone setting. Call this
 * before using any other object fetching functions.
 *
 * Since: 3.2
 **/
void
e_cal_client_set_default_timezone (ECalClient *client, /* const */ icaltimezone *zone)
{
	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (zone != NULL);

	if (client->priv->default_zone != icaltimezone_get_utc_timezone ())
		icaltimezone_free (client->priv->default_zone, 1);

	if (zone == icaltimezone_get_utc_timezone ())
		client->priv->default_zone = zone;
	else
		client->priv->default_zone = copy_timezone (zone);
}

/**
 * e_cal_client_get_default_timezone:
 * @client: A calendar client.
 *
 * Returns: Default timezone previously set with e_cal_client_set_default_timezone().
 * Returned pointer is owned by the @client and should not be freed.
 *
 * Since: 3.2
 **/
/* const */ icaltimezone *
e_cal_client_get_default_timezone (ECalClient *client)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), NULL);

	return client->priv->default_zone;
}

/**
 * e_cal_client_check_one_alarm_only:
 * @client: A calendar client.
 *
 * Checks if a calendar supports only one alarm per component.
 *
 * Returns: TRUE if the calendar allows only one alarm, FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_check_one_alarm_only (ECalClient *client)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	return e_client_check_capability (E_CLIENT (client), CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY);
}

/**
 * e_cal_client_check_save_schedules:
 * @client: A calendar client.
 *
 * Checks whether the calendar saves schedules.
 *
 * Returns: TRUE if it saves schedules, FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_check_save_schedules (ECalClient *client)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	return e_client_check_capability (E_CLIENT (client), CAL_STATIC_CAPABILITY_SAVE_SCHEDULES);
}

/**
 * e_cal_client_check_organizer_must_attend:
 * @client: A calendar client.
 *
 * Checks if a calendar forces organizers of meetings to be also attendees.
 *
 * Returns: TRUE if the calendar forces organizers to attend meetings,
 * FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_check_organizer_must_attend (ECalClient *client)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	return e_client_check_capability (E_CLIENT (client), CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ATTEND);
}

/**
 * e_cal_client_check_organizer_must_accept:
 * @client: A calendar client.
 *
 * Checks whether a calendar requires organizer to accept their attendance to
 * meetings.
 *
 * Returns: TRUE if the calendar requires organizers to accept, FALSE
 * otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_check_organizer_must_accept (ECalClient *client)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	return e_client_check_capability (E_CLIENT (client), CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ACCEPT);
}

/**
 * e_cal_client_check_recurrences_no_master:
 * @client: A calendar client.
 *
 * Checks if the calendar has a master object for recurrences.
 *
 * Returns: TRUE if the calendar has a master object for recurrences,
 * FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_check_recurrences_no_master (ECalClient *client)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	return e_client_check_capability (E_CLIENT (client), CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER);
}

/**
 * e_cal_client_free_icalcomp_slist:
 * @icalcomps: (element-type icalcomponent): list of icalcomponent objects
 *
 * Frees each element of the @icalcomps list and the list itself.
 * Each element is an object of type #icalcomponent.
 *
 * Since: 3.2
 **/
void
e_cal_client_free_icalcomp_slist (GSList *icalcomps)
{
	g_slist_foreach (icalcomps, (GFunc) icalcomponent_free, NULL);
	g_slist_free (icalcomps);
}

/**
 * e_cal_client_free_ecalcomp_slist:
 * @ecalcomps: (element-type ECalComponent): list of #ECalComponent objects
 *
 * Frees each element of the @ecalcomps list and the list itself.
 * Each element is an object of type #ECalComponent.
 *
 * Since: 3.2
 **/
void
e_cal_client_free_ecalcomp_slist (GSList *ecalcomps)
{
	g_slist_foreach (ecalcomps, (GFunc) g_object_unref, NULL);
	g_slist_free (ecalcomps);
}

/**
 * e_cal_client_resolve_tzid_cb:
 * @tzid: ID of the timezone to resolve.
 * @data: Closure data for the callback, in this case #ECalClient.
 *
 * Resolves TZIDs for the recurrence generator.
 *
 * Returns: The timezone identified by the @tzid argument, or %NULL if
 * it could not be found.
 *
 * Since: 3.2
 */
icaltimezone *
e_cal_client_resolve_tzid_cb (const gchar *tzid,
                              gpointer data)
{
	ECalClient *client = data;
	icaltimezone *zone = NULL;
	GError *error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), NULL);

	e_cal_client_get_timezone_sync (client, tzid, &zone, NULL, &error);

	if (error) {
		g_debug ("%s: Failed to find '%s' timezone: %s", G_STRFUNC, tzid, error->message);
		g_error_free (error);
	}

	return zone;
}

struct comp_instance {
	ECalComponent *comp;
	time_t start;
	time_t end;
};

struct instances_info {
	GSList **instances;
	icaltimezone *start_zone;
	icaltimezone *end_zone;
};

/* Called from cal_recur_generate_instances(); adds an instance to the list */
static gboolean
add_instance (ECalComponent *comp,
              time_t start,
              time_t end,
              gpointer data)
{
	GSList **list;
	struct comp_instance *ci;
	icalcomponent *icalcomp;
	struct instances_info *instances_hold;

	instances_hold = data;
	list = instances_hold->instances;

	ci = g_new (struct comp_instance, 1);

	icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp));

	/* add the instance to the list */
	ci->comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (ci->comp, icalcomp);

	/* make sure we return an instance */
	if (e_cal_util_component_has_recurrences (icalcomp) &&
	    !(icalcomponent_get_first_property (icalcomp, ICAL_RECURRENCEID_PROPERTY))) {
		ECalComponentRange *range;
		struct icaltimetype itt;
		ECalComponentDateTime dtstart, dtend;

		/* update DTSTART */
		dtstart.value = NULL;
		dtstart.tzid = NULL;

		e_cal_component_get_dtstart (comp, &dtstart);

		if (instances_hold->start_zone) {
			itt = icaltime_from_timet_with_zone (start, dtstart.value && dtstart.value->is_date, instances_hold->start_zone);
			g_free ((gchar *) dtstart.tzid);
			dtstart.tzid = g_strdup (icaltimezone_get_tzid (instances_hold->start_zone));
		} else {
			itt = icaltime_from_timet (start, dtstart.value && dtstart.value->is_date);
			if (dtstart.tzid) {
				g_free ((gchar *) dtstart.tzid);
				dtstart.tzid = NULL;
			}
		}

		g_free (dtstart.value);
		dtstart.value = &itt;
		e_cal_component_set_dtstart (ci->comp, &dtstart);

		/* set the RECUR-ID for the instance */
		range = g_new0 (ECalComponentRange, 1);
		range->type = E_CAL_COMPONENT_RANGE_SINGLE;
		range->datetime = dtstart;

		e_cal_component_set_recurid (ci->comp, range);

		g_free (range);
		g_free ((gchar *) dtstart.tzid);

		/* Update DTEND */
		dtend.value = NULL;
		dtend.tzid = NULL;

		e_cal_component_get_dtend (comp, &dtend);

		if (instances_hold->end_zone) {
			itt = icaltime_from_timet_with_zone (end, dtend.value && dtend.value->is_date, instances_hold->end_zone);
			g_free ((gchar *) dtend.tzid);
			dtend.tzid = g_strdup (icaltimezone_get_tzid (instances_hold->end_zone));
		} else {
			itt = icaltime_from_timet (end, dtend.value && dtend.value->is_date);
			if (dtend.tzid) {
				g_free ((gchar *) dtend.tzid);
				dtend.tzid = NULL;
			}
		}

		g_free (dtend.value);
		dtend.value = &itt;
		e_cal_component_set_dtend (ci->comp, &dtend);

		g_free ((gchar *) dtend.tzid);
	}

	ci->start = start;
	ci->end = end;

	*list = g_slist_prepend (*list, ci);

	return TRUE;
}

/* Used from g_slist_sort(); compares two struct comp_instance structures */
static gint
compare_comp_instance (gconstpointer a,
                       gconstpointer b)
{
	const struct comp_instance *cia, *cib;
	time_t diff;

	cia = a;
	cib = b;

	diff = cia->start - cib->start;
	return (diff < 0) ? -1 : (diff > 0) ? 1 : 0;
}

static GSList *
process_detached_instances (GSList *instances,
                            GSList *detached_instances)
{
	struct comp_instance *ci, *cid;
	GSList *dl, *unprocessed_instances = NULL;

	for (dl = detached_instances; dl != NULL; dl = dl->next) {
		GSList *il;
		const gchar *uid;
		gboolean processed;
		ECalComponentRange recur_id, instance_recur_id;

		processed = FALSE;
		recur_id.type = E_CAL_COMPONENT_RANGE_SINGLE;
		instance_recur_id.type = E_CAL_COMPONENT_RANGE_SINGLE;

		cid = dl->data;
		e_cal_component_get_uid (cid->comp, &uid);
		e_cal_component_get_recurid (cid->comp, &recur_id);

		/* search for coincident instances already expanded */
		for (il = instances; il != NULL; il = il->next) {
			const gchar *instance_uid;
			gint cmp;

			ci = il->data;
			e_cal_component_get_uid (ci->comp, &instance_uid);
			e_cal_component_get_recurid (ci->comp, &instance_recur_id);
			if (strcmp (uid, instance_uid) == 0) {
				gchar *i_rid = NULL, *d_rid = NULL;

				i_rid = e_cal_component_get_recurid_as_string (ci->comp);
				d_rid = e_cal_component_get_recurid_as_string (cid->comp);

				if (i_rid && d_rid && strcmp (i_rid, d_rid) == 0) {
					g_object_unref (ci->comp);
					ci->comp = g_object_ref (cid->comp);
					ci->start = cid->start;
					ci->end = cid->end;

					processed = TRUE;
				} else {
					if (!instance_recur_id.datetime.value ||
					    !recur_id.datetime.value) {
						/*
						 * Prevent obvious segfault by ignoring missing
						 * recurrency ids. Real problem might be elsewhere,
						 * but anything is better than crashing...
						 */
						g_log (
							G_LOG_DOMAIN,
							G_LOG_LEVEL_CRITICAL,
							"UID %s: instance RECURRENCE-ID %s + detached instance RECURRENCE-ID %s: cannot compare",
							uid,
							i_rid,
							d_rid);

						e_cal_component_free_datetime (&instance_recur_id.datetime);
						g_free (i_rid);
						g_free (d_rid);
						continue;
					}
					cmp = icaltime_compare (
						*instance_recur_id.datetime.value,
								*recur_id.datetime.value);
					if ((recur_id.type == E_CAL_COMPONENT_RANGE_THISPRIOR && cmp <= 0) ||
						(recur_id.type == E_CAL_COMPONENT_RANGE_THISFUTURE && cmp >= 0)) {
						ECalComponent *comp;

						comp = e_cal_component_new ();
						e_cal_component_set_icalcomponent (
							comp,
							icalcomponent_new_clone (e_cal_component_get_icalcomponent (cid->comp)));
						e_cal_component_set_recurid (comp, &instance_recur_id);

						/* replace the generated instances */
						g_object_unref (ci->comp);
						ci->comp = comp;
					}
				}
				g_free (i_rid);
				g_free (d_rid);
			}
			e_cal_component_free_datetime (&instance_recur_id.datetime);
		}

		e_cal_component_free_datetime (&recur_id.datetime);

		if (!processed)
			unprocessed_instances = g_slist_prepend (unprocessed_instances, cid);
	}

	/* add the unprocessed instances (ie, detached instances with no master object */
	while (unprocessed_instances != NULL) {
		cid = unprocessed_instances->data;
		ci = g_new0 (struct comp_instance, 1);
		ci->comp = g_object_ref (cid->comp);
		ci->start = cid->start;
		ci->end = cid->end;
		instances = g_slist_append (instances, ci);

		unprocessed_instances = g_slist_remove (unprocessed_instances, cid);
	}

	return instances;
}

static void
generate_instances (ECalClient *client,
                    time_t start,
                    time_t end,
                    GSList *objects,
                    GCancellable *cancellable,
                    ECalRecurInstanceFn cb,
                    gpointer cb_data)
{
	GSList *instances, *detached_instances = NULL;
	GSList *l;
	ECalClientPrivate *priv;

	priv = client->priv;

	instances = NULL;

	for (l = objects; l && !g_cancellable_is_cancelled (cancellable); l = l->next) {
		ECalComponent *comp;
		icaltimezone *default_zone;

		if (priv->default_zone)
			default_zone = priv->default_zone;
		else
			default_zone = icaltimezone_get_utc_timezone ();

		comp = l->data;
		if (e_cal_component_is_instance (comp)) {
			struct comp_instance *ci;
			ECalComponentDateTime dtstart, dtend;
			icaltimezone *start_zone = NULL, *end_zone = NULL;

			/* keep the detached instances apart */
			ci = g_new0 (struct comp_instance, 1);
			ci->comp = g_object_ref (comp);

			e_cal_component_get_dtstart (comp, &dtstart);
			e_cal_component_get_dtend (comp, &dtend);

			/* For DATE-TIME values with a TZID, we use
			 * e_cal_resolve_tzid_cb to resolve the TZID.
			 * For DATE values and DATE-TIME values without a
			 * TZID (i.e. floating times) we use the default
			 * timezone. */
			if (dtstart.tzid && dtstart.value && !dtstart.value->is_date) {
				start_zone = e_cal_client_resolve_tzid_cb (dtstart.tzid, client);
				if (!start_zone)
					start_zone = default_zone;
			} else {
				start_zone = default_zone;
			}

			if (dtend.tzid && dtend.value && !dtend.value->is_date) {
				end_zone = e_cal_client_resolve_tzid_cb (dtend.tzid, client);
				if (!end_zone)
					end_zone = default_zone;
			} else {
				end_zone = default_zone;
			}

			ci->start = icaltime_as_timet_with_zone (*dtstart.value, start_zone);

			if (dtend.value)
				ci->end = icaltime_as_timet_with_zone (*dtend.value, end_zone);
			else if (icaltime_is_date (*dtstart.value))
				ci->end = time_day_end (ci->start);
			else
				ci->end = ci->start;

			e_cal_component_free_datetime (&dtstart);
			e_cal_component_free_datetime (&dtend);

			if (ci->start <= end && ci->end >= start) {
				detached_instances = g_slist_prepend (detached_instances, ci);
			} else {
				/* it doesn't fit to our time range, thus skip it */
				g_object_unref (G_OBJECT (ci->comp));
				g_free (ci);
			}
		} else {
			ECalComponentDateTime datetime;
			icaltimezone *start_zone = NULL, *end_zone = NULL;
			struct instances_info *instances_hold;

			/* Get the start timezone */
			e_cal_component_get_dtstart (comp, &datetime);
			if (datetime.tzid)
				e_cal_client_get_timezone_sync (client, datetime.tzid, &start_zone, cancellable, NULL);
			else
				start_zone = NULL;
			e_cal_component_free_datetime (&datetime);

			/* Get the end timezone */
			e_cal_component_get_dtend (comp, &datetime);
			if (datetime.tzid)
				e_cal_client_get_timezone_sync (client, datetime.tzid, &end_zone, cancellable, NULL);
			else
				end_zone = NULL;
			e_cal_component_free_datetime (&datetime);

			instances_hold = g_new0 (struct instances_info, 1);
			instances_hold->instances = &instances;
			instances_hold->start_zone = start_zone;
			instances_hold->end_zone = end_zone;

			e_cal_recur_generate_instances (
				comp, start, end, add_instance, instances_hold,
				e_cal_client_resolve_tzid_cb, client,
				default_zone);

			g_free (instances_hold);
		}
	}

	g_slist_foreach (objects, (GFunc) g_object_unref, NULL);
	g_slist_free (objects);

	/* Generate instances and spew them out */

	if (!g_cancellable_is_cancelled (cancellable)) {
		instances = g_slist_sort (instances, compare_comp_instance);
		instances = process_detached_instances (instances, detached_instances);
	}

	for (l = instances; l && !g_cancellable_is_cancelled (cancellable); l = l->next) {
		struct comp_instance *ci;
		gboolean result;

		ci = l->data;

		result = (* cb) (ci->comp, ci->start, ci->end, cb_data);

		if (!result)
			break;
	}

	/* Clean up */

	for (l = instances; l; l = l->next) {
		struct comp_instance *ci;

		ci = l->data;
		g_object_unref (G_OBJECT (ci->comp));
		g_free (ci);
	}

	g_slist_free (instances);

	for (l = detached_instances; l; l = l->next) {
		struct comp_instance *ci;

		ci = l->data;
		g_object_unref (G_OBJECT (ci->comp));
		g_free (ci);
	}

	g_slist_free (detached_instances);
}

static GSList *
get_objects_sync (ECalClient *client,
                  time_t start,
                  time_t end,
                  const gchar *uid)
{
	GSList *objects = NULL;

	/* Generate objects */
	if (uid && *uid) {
		GError *error = NULL;
		gint tries = 0;

 try_again:
		if (!e_cal_client_get_objects_for_uid_sync (client, uid, &objects, NULL, &error)) {
			if (g_error_matches (error, E_CLIENT_ERROR, E_CLIENT_ERROR_BUSY) && tries <= 10) {
				tries++;
				g_usleep (500);
				g_clear_error (&error);

				goto try_again;
			}

			unwrap_dbus_error (error, &error);
			g_message ("Failed to get recurrence objects for uid %s \n", error ? error->message : "Unknown error");
			g_clear_error (&error);
			return NULL;
		}
	} else {
		gchar *iso_start, *iso_end;
		gchar *query;

		iso_start = isodate_from_time_t (start);
		if (!iso_start)
			return NULL;

		iso_end = isodate_from_time_t (end);
		if (!iso_end) {
			g_free (iso_start);
			return NULL;
		}

		query = g_strdup_printf (
			"(occur-in-time-range? (make-time \"%s\") (make-time \"%s\"))",
			iso_start, iso_end);
		g_free (iso_start);
		g_free (iso_end);
		if (!e_cal_client_get_object_list_as_comps_sync (client, query, &objects, NULL, NULL)) {
			g_free (query);
			return NULL;
		}
		g_free (query);
	}

	return objects;
}

struct get_objects_async_data
{
	GCancellable *cancellable;
	ECalClient *client;
	time_t start;
	time_t end;
	ECalRecurInstanceFn cb;
	gpointer cb_data;
	GDestroyNotify destroy_cb_data;
	gchar *uid;
	gchar *query;
	guint tries;
	void (* ready_cb) (struct get_objects_async_data *goad, GSList *objects);
	icaltimezone *start_zone;
	icaltimezone *end_zone;
	ECalComponent *comp;
};

static void
free_get_objects_async_data (struct get_objects_async_data *goad)
{
	if (!goad)
		return;

	if (goad->cancellable)
		g_object_unref (goad->cancellable);
	if (goad->destroy_cb_data)
		goad->destroy_cb_data (goad->cb_data);
	if (goad->client)
		g_object_unref (goad->client);
	if (goad->comp)
		g_object_unref (goad->comp);
	g_free (goad->query);
	g_free (goad->uid);
	g_free (goad);
}

static gboolean repeat_get_objects_for_uid_timeout_cb (gpointer user_data);

static void
got_objects_for_uid_cb (GObject *source_object,
                        GAsyncResult *result,
                        gpointer user_data)
{
	struct get_objects_async_data *goad = user_data;
	GSList *objects = NULL;
	GError *error = NULL;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (result != NULL);
	g_return_if_fail (goad != NULL);
	g_return_if_fail (goad->client == E_CAL_CLIENT (source_object));

	if (!e_cal_client_get_objects_for_uid_finish (goad->client, result, &objects, &error)) {
		if (g_error_matches (error, E_CLIENT_ERROR, E_CLIENT_ERROR_CANCELLED) ||
		    g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			free_get_objects_async_data (goad);
			g_clear_error (&error);
			return;
		}

		if (g_error_matches (error, E_CLIENT_ERROR, E_CLIENT_ERROR_BUSY) && goad->tries < 10) {
			goad->tries++;
			g_timeout_add (250, repeat_get_objects_for_uid_timeout_cb, goad);
			g_clear_error (&error);
			return;
		}

		g_clear_error (&error);
		objects = NULL;
	}

	g_return_if_fail (goad->ready_cb != NULL);

	/* takes care of the objects and goad */
	goad->ready_cb (goad, objects);
}

static gboolean
repeat_get_objects_for_uid_timeout_cb (gpointer user_data)
{
	struct get_objects_async_data *goad = user_data;

	g_return_val_if_fail (goad != NULL, FALSE);

	e_cal_client_get_objects_for_uid (goad->client, goad->uid, goad->cancellable, got_objects_for_uid_cb, goad);

	return FALSE;
}

static gboolean repeat_get_object_list_as_comps_timeout_cb (gpointer user_data);

static void
got_object_list_as_comps_cb (GObject *source_object,
                             GAsyncResult *result,
                             gpointer user_data)
{
	struct get_objects_async_data *goad = user_data;
	GSList *objects = NULL;
	GError *error = NULL;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (result != NULL);
	g_return_if_fail (goad != NULL);
	g_return_if_fail (goad->client == E_CAL_CLIENT (source_object));

	if (!e_cal_client_get_object_list_as_comps_finish (goad->client, result, &objects, &error)) {
		if (g_error_matches (error, E_CLIENT_ERROR, E_CLIENT_ERROR_CANCELLED) ||
		    g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			free_get_objects_async_data (goad);
			g_clear_error (&error);
			return;
		}

		if (g_error_matches (error, E_CLIENT_ERROR, E_CLIENT_ERROR_BUSY) && goad->tries < 10) {
			goad->tries++;
			g_timeout_add (250, repeat_get_object_list_as_comps_timeout_cb, goad);
			g_clear_error (&error);
			return;
		}

		g_clear_error (&error);
		objects = NULL;
	}

	g_return_if_fail (goad->ready_cb != NULL);

	/* takes care of the objects and goad */
	goad->ready_cb (goad, objects);
}

static gboolean
repeat_get_object_list_as_comps_timeout_cb (gpointer user_data)
{
	struct get_objects_async_data *goad = user_data;

	g_return_val_if_fail (goad != NULL, FALSE);

	e_cal_client_get_object_list_as_comps (goad->client, goad->query, goad->cancellable, got_object_list_as_comps_cb, goad);

	return FALSE;
}

/* ready_cb may take care of both arguments, goad and objects; objects can be also NULL */
static void
get_objects_async (void (*ready_cb) (struct get_objects_async_data *goad,
                                     GSList *objects),
                   struct get_objects_async_data *goad)
{
	g_return_if_fail (ready_cb != NULL);
	g_return_if_fail (goad != NULL);

	goad->ready_cb = ready_cb;

	if (goad->uid && *goad->uid) {
		e_cal_client_get_objects_for_uid (goad->client, goad->uid, goad->cancellable, got_objects_for_uid_cb, goad);
	} else {
		gchar *iso_start, *iso_end;

		iso_start = isodate_from_time_t (goad->start);
		if (!iso_start) {
			free_get_objects_async_data (goad);
			return;
		}

		iso_end = isodate_from_time_t (goad->end);
		if (!iso_end) {
			g_free (iso_start);
			free_get_objects_async_data (goad);
			return;
		}

		goad->query = g_strdup_printf ("(occur-in-time-range? (make-time \"%s\") (make-time \"%s\"))", iso_start, iso_end);

		g_free (iso_start);
		g_free (iso_end);

		e_cal_client_get_object_list_as_comps (goad->client, goad->query, goad->cancellable, got_object_list_as_comps_cb, goad);
	}
}

static void
generate_instances_got_objects_cb (struct get_objects_async_data *goad,
                                   GSList *objects)
{
	g_return_if_fail (goad != NULL);

	/* generate_instaces () frees 'objects' slist */
	if (objects)
		generate_instances (goad->client, goad->start, goad->end, objects, goad->cancellable, goad->cb, goad->cb_data);

	free_get_objects_async_data (goad);
}

/**
 * e_cal_client_generate_instances:
 * @client: A calendar client.
 * @start: Start time for query.
 * @end: End time for query.
 * @cancellable: a #GCancellable; can be %NULL
 * @cb: Callback for each generated instance.
 * @cb_data: Closure data for the callback.
 * @destroy_cb_data: Function to call when the processing is done, to free @cb_data; can be %NULL.
 *
 * Does a combination of e_cal_client_get_object_list () and
 * e_cal_client_recur_generate_instances(). Unlike e_cal_client_generate_instances_sync (),
 * this returns immediately and the @cb callback is called asynchronously.
 *
 * The callback function should do a g_object_ref() of the calendar component
 * it gets passed if it intends to keep it around, since it will be unref'ed
 * as soon as the callback returns.
 *
 * Since: 3.2
 **/
void
e_cal_client_generate_instances (ECalClient *client,
                                 time_t start,
                                 time_t end,
                                 GCancellable *cancellable,
                                 ECalRecurInstanceFn cb,
                                 gpointer cb_data,
                                 GDestroyNotify destroy_cb_data)
{
	struct get_objects_async_data *goad;
	GCancellable *use_cancellable;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (e_client_is_opened (E_CLIENT (client)));

	g_return_if_fail (start >= 0);
	g_return_if_fail (end >= 0);
	g_return_if_fail (cb != NULL);

	use_cancellable = cancellable;
	if (!use_cancellable)
		use_cancellable = g_cancellable_new ();

	goad = g_new0 (struct get_objects_async_data, 1);
	goad->cancellable = g_object_ref (use_cancellable);
	goad->client = g_object_ref (client);
	goad->start = start;
	goad->end = end;
	goad->cb = cb;
	goad->cb_data = cb_data;
	goad->destroy_cb_data = destroy_cb_data;

	get_objects_async (generate_instances_got_objects_cb, goad);

	if (use_cancellable != cancellable)
		g_object_unref (use_cancellable);
}

/**
 * e_cal_client_generate_instances_sync:
 * @client: A calendar client
 * @start: Start time for query
 * @end: End time for query
 * @cb: (closure cb_data) (scope call): Callback for each generated instance
 * @cb_data: (closure): Closure data for the callback
 *
 * Does a combination of e_cal_client_get_object_list() and
 * e_cal_client_recur_generate_instances().
 *
 * The callback function should do a g_object_ref() of the calendar component
 * it gets passed if it intends to keep it around, since it will be unreffed
 * as soon as the callback returns.
 *
 * Since: 3.2
 **/
void
e_cal_client_generate_instances_sync (ECalClient *client,
                                      time_t start,
                                      time_t end,
                                      ECalRecurInstanceFn cb,
                                      gpointer cb_data)
{
	GSList *objects = NULL;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (e_client_is_opened (E_CLIENT (client)));

	g_return_if_fail (start >= 0);
	g_return_if_fail (end >= 0);
	g_return_if_fail (cb != NULL);

	objects = get_objects_sync (client, start, end, NULL);
	if (!objects)
		return;

	/* generate_instaces frees 'objects' slist */
	generate_instances (client, start, end, objects, NULL, cb, cb_data);
}

/* also frees 'instances' GSList */
static void
process_instances (ECalComponent *comp,
                   GSList *instances,
                   ECalRecurInstanceFn cb,
                   gpointer cb_data)
{
	gchar *rid;
	gboolean result;

	g_return_if_fail (comp != NULL);
	g_return_if_fail (cb != NULL);

	rid = e_cal_component_get_recurid_as_string (comp);

	/* Reverse the instances list because the add_instance() function is prepending */
	instances = g_slist_reverse (instances);

	/* now only return back the instances for the given object */
	result = TRUE;
	while (instances != NULL) {
		struct comp_instance *ci;
		gchar *instance_rid = NULL;

		ci = instances->data;

		if (result) {
			instance_rid = e_cal_component_get_recurid_as_string (ci->comp);

			if (rid && *rid) {
				if (instance_rid && *instance_rid && strcmp (rid, instance_rid) == 0)
					result = (* cb) (ci->comp, ci->start, ci->end, cb_data);
			} else
				result = (* cb)  (ci->comp, ci->start, ci->end, cb_data);
		}

		/* remove instance from list */
		instances = g_slist_remove (instances, ci);
		g_object_unref (ci->comp);
		g_free (ci);
		g_free (instance_rid);
	}

	/* clean up */
	g_free (rid);
}

static void
generate_instances_for_object_got_objects_cb (struct get_objects_async_data *goad,
                                              GSList *objects)
{
	struct instances_info *instances_hold;
	GSList *instances = NULL;

	g_return_if_fail (goad != NULL);

	instances_hold = g_new0 (struct instances_info, 1);
	instances_hold->instances = &instances;
	instances_hold->start_zone = goad->start_zone;
	instances_hold->end_zone = goad->end_zone;

	/* generate all instances in the given time range */
	generate_instances (goad->client, goad->start, goad->end, objects, goad->cancellable, add_instance, instances_hold);

	/* it also frees 'instances' GSList */
	process_instances (goad->comp, *(instances_hold->instances), goad->cb, goad->cb_data);

	/* clean up */
	free_get_objects_async_data (goad);
	g_free (instances_hold);
}

/**
 * e_cal_client_generate_instances_for_object:
 * @client: A calendar client.
 * @icalcomp: Object to generate instances from.
 * @start: Start time for query.
 * @end: End time for query.
 * @cancellable: a #GCancellable; can be %NULL
 * @cb: Callback for each generated instance.
 * @cb_data: Closure data for the callback.
 * @destroy_cb_data: Function to call when the processing is done, to free @cb_data; can be %NULL.
 *
 * Does a combination of e_cal_client_get_object_list() and
 * e_cal_client_recur_generate_instances(), like e_cal_client_generate_instances(), but
 * for a single object. Unlike e_cal_client_generate_instances_for_object_sync (),
 * this returns immediately and the @cb callback is called asynchronously.
 *
 * The callback function should do a g_object_ref() of the calendar component
 * it gets passed if it intends to keep it around, since it will be unref'ed
 * as soon as the callback returns.
 *
 * Since: 3.2
 **/
void
e_cal_client_generate_instances_for_object (ECalClient *client,
                                            icalcomponent *icalcomp,
                                            time_t start,
                                            time_t end,
                                            GCancellable *cancellable,
                                            ECalRecurInstanceFn cb,
                                            gpointer cb_data,
                                            GDestroyNotify destroy_cb_data)
{
	ECalComponent *comp;
	const gchar *uid;
	ECalComponentDateTime datetime;
	icaltimezone *start_zone = NULL, *end_zone = NULL;
	gboolean is_single_instance = FALSE;
	struct get_objects_async_data *goad;
	GCancellable *use_cancellable;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (e_client_is_opened (E_CLIENT (client)));

	g_return_if_fail (start >= 0);
	g_return_if_fail (end >= 0);
	g_return_if_fail (cb != NULL);

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp));

	if (!e_cal_component_has_recurrences (comp))
		is_single_instance = TRUE;

	/* If the backend stores it as individual instances and does not
	 * have a master object - do not expand */
	if (is_single_instance || e_client_check_capability (E_CLIENT (client), CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER)) {
		/* return the same instance */
		(* cb)  (comp, icaltime_as_timet_with_zone (icalcomponent_get_dtstart (icalcomp), client->priv->default_zone),
				icaltime_as_timet_with_zone (icalcomponent_get_dtend (icalcomp), client->priv->default_zone), cb_data);
		g_object_unref (comp);

		if (destroy_cb_data)
			destroy_cb_data (cb_data);
		return;
	}

	e_cal_component_get_uid (comp, &uid);

	/* Get the start timezone */
	e_cal_component_get_dtstart (comp, &datetime);
	if (datetime.tzid)
		e_cal_client_get_timezone_sync (client, datetime.tzid, &start_zone, NULL, NULL);
	else
		start_zone = NULL;
	e_cal_component_free_datetime (&datetime);

	/* Get the end timezone */
	e_cal_component_get_dtend (comp, &datetime);
	if (datetime.tzid)
		e_cal_client_get_timezone_sync (client, datetime.tzid, &end_zone, NULL, NULL);
	else
		end_zone = NULL;
	e_cal_component_free_datetime (&datetime);

	use_cancellable = cancellable;
	if (!use_cancellable)
		use_cancellable = g_cancellable_new ();

	goad = g_new0 (struct get_objects_async_data, 1);
	goad->cancellable = g_object_ref (use_cancellable);
	goad->client = g_object_ref (client);
	goad->start = start;
	goad->end = end;
	goad->cb = cb;
	goad->cb_data = cb_data;
	goad->destroy_cb_data = destroy_cb_data;
	goad->start_zone = start_zone;
	goad->end_zone = end_zone;
	goad->comp = comp;
	goad->uid = g_strdup (uid);

	get_objects_async (generate_instances_for_object_got_objects_cb, goad);

	if (use_cancellable != cancellable)
		g_object_unref (use_cancellable);
}

/**
 * e_cal_client_generate_instances_for_object_sync:
 * @client: A calendar client
 * @icalcomp: Object to generate instances from
 * @start: Start time for query
 * @end: End time for query
 * @cb: (closure cb_data) (scope call): Callback for each generated instance
 * @cb_data: (closure): Closure data for the callback
 *
 * Does a combination of e_cal_client_get_object_list() and
 * e_cal_client_recur_generate_instances(), like e_cal_client_generate_instances_sync(), but
 * for a single object.
 *
 * The callback function should do a g_object_ref() of the calendar component
 * it gets passed if it intends to keep it around, since it will be unref'ed
 * as soon as the callback returns.
 *
 * Since: 3.2
 **/
void
e_cal_client_generate_instances_for_object_sync (ECalClient *client,
                                                 icalcomponent *icalcomp,
                                                 time_t start,
                                                 time_t end,
                                                 ECalRecurInstanceFn cb,
                                                 gpointer cb_data)
{
	ECalComponent *comp;
	const gchar *uid;
	GSList *instances = NULL;
	ECalComponentDateTime datetime;
	icaltimezone *start_zone = NULL, *end_zone = NULL;
	struct instances_info *instances_hold;
	gboolean is_single_instance = FALSE;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (e_client_is_opened (E_CLIENT (client)));

	g_return_if_fail (start >= 0);
	g_return_if_fail (end >= 0);
	g_return_if_fail (cb != NULL);

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp));

	if (!e_cal_component_has_recurrences (comp))
		is_single_instance = TRUE;

	/* If the backend stores it as individual instances and does not
	 * have a master object - do not expand */
	if (is_single_instance || e_client_check_capability (E_CLIENT (client), CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER)) {
		/* return the same instance */
		(* cb)  (comp, icaltime_as_timet_with_zone (icalcomponent_get_dtstart (icalcomp), client->priv->default_zone),
				icaltime_as_timet_with_zone (icalcomponent_get_dtend (icalcomp), client->priv->default_zone), cb_data);
		g_object_unref (comp);
		return;
	}

	e_cal_component_get_uid (comp, &uid);

	/* Get the start timezone */
	e_cal_component_get_dtstart (comp, &datetime);
	if (datetime.tzid)
		e_cal_client_get_timezone_sync (client, datetime.tzid, &start_zone, NULL, NULL);
	else
		start_zone = NULL;
	e_cal_component_free_datetime (&datetime);

	/* Get the end timezone */
	e_cal_component_get_dtend (comp, &datetime);
	if (datetime.tzid)
		e_cal_client_get_timezone_sync (client, datetime.tzid, &end_zone, NULL, NULL);
	else
		end_zone = NULL;
	e_cal_component_free_datetime (&datetime);

	instances_hold = g_new0 (struct instances_info, 1);
	instances_hold->instances = &instances;
	instances_hold->start_zone = start_zone;
	instances_hold->end_zone = end_zone;

	/* generate all instances in the given time range */
	generate_instances (client, start, end, get_objects_sync (client, start, end, uid), NULL, add_instance, instances_hold);

	/* it also frees 'instances' GSList */
	process_instances (comp, *(instances_hold->instances), cb, cb_data);

	/* clean up */
	g_object_unref (comp);
	g_free (instances_hold);
}

typedef struct _ForeachTZIDCallbackData ForeachTZIDCallbackData;
struct _ForeachTZIDCallbackData {
	ECalClient *client;
	GHashTable *timezone_hash;
	gboolean success;
};

/* This adds the VTIMEZONE given by the TZID parameter to the GHashTable in
 * data. */
static void
foreach_tzid_callback (icalparameter *param,
                       gpointer cbdata)
{
	ForeachTZIDCallbackData *data = cbdata;
	const gchar *tzid;
	icaltimezone *zone = NULL;
	icalcomponent *vtimezone_comp;
	gchar *vtimezone_as_string;

	/* Get the TZID string from the parameter. */
	tzid = icalparameter_get_tzid (param);
	if (!tzid)
		return;

	/* Check if we've already added it to the GHashTable. */
	if (g_hash_table_lookup (data->timezone_hash, tzid))
		return;

	if (!e_cal_client_get_timezone_sync (data->client, tzid, &zone, NULL, NULL) || !zone) {
		data->success = FALSE;
		return;
	}

	/* Convert it to a string and add it to the hash. */
	vtimezone_comp = icaltimezone_get_component (zone);
	if (!vtimezone_comp)
		return;

	vtimezone_as_string = icalcomponent_as_ical_string_r (vtimezone_comp);

	g_hash_table_insert (data->timezone_hash, (gchar *) tzid, vtimezone_as_string);
}

/* This appends the value string to the GString given in data. */
static void
append_timezone_string (gpointer key,
                        gpointer value,
                        gpointer data)
{
	GString *vcal_string = data;

	g_string_append (vcal_string, value);
	g_free (value);
}

/* This simply frees the hash values. */
static void
free_timezone_string (gpointer key,
                      gpointer value,
                      gpointer data)
{
	g_free (value);
}

/**
 * e_cal_client_get_component_as_string:
 * @client: A calendar client.
 * @icalcomp: A calendar component object.
 *
 * Gets a calendar component as an iCalendar string, with a toplevel
 * VCALENDAR component and all VTIMEZONEs needed for the component.
 *
 * Returns: the component as a complete iCalendar string, or NULL on
 * failure. The string should be freed with g_free().
 *
 * Since: 3.2
 **/
gchar *
e_cal_client_get_component_as_string (ECalClient *client,
                                      icalcomponent *icalcomp)
{
	GHashTable *timezone_hash;
	GString *vcal_string;
	ForeachTZIDCallbackData cbdata;
	gchar *obj_string;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), NULL);
	g_return_val_if_fail (icalcomp != NULL, NULL);

	timezone_hash = g_hash_table_new (g_str_hash, g_str_equal);

	/* Add any timezones needed to the hash. We use a hash since we only
	 * want to add each timezone once at most. */
	cbdata.client = client;
	cbdata.timezone_hash = timezone_hash;
	cbdata.success = TRUE;
	icalcomponent_foreach_tzid (icalcomp, foreach_tzid_callback, &cbdata);
	if (!cbdata.success) {
		g_hash_table_foreach (timezone_hash, free_timezone_string, NULL);
		return NULL;
	}

	/* Create the start of a VCALENDAR, to add the VTIMEZONES to,
	 * and remember its length so we know if any VTIMEZONEs get added. */
	vcal_string = g_string_new (NULL);
	g_string_append (
		vcal_string,
		"BEGIN:VCALENDAR\n"
		"PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
		"VERSION:2.0\n"
		"METHOD:PUBLISH\n");

	/* Now concatenate all the timezone strings. This also frees the
	 * timezone strings as it goes. */
	g_hash_table_foreach (timezone_hash, append_timezone_string, vcal_string);

	/* Get the string for the VEVENT/VTODO. */
	obj_string = icalcomponent_as_ical_string_r (icalcomp);

	/* If there were any timezones to send, create a complete VCALENDAR,
	 * else just send the VEVENT/VTODO string. */
	g_string_append (vcal_string, obj_string);
	g_string_append (vcal_string, "END:VCALENDAR\n");
	g_free (obj_string);

	obj_string = g_string_free (vcal_string, FALSE);

	g_hash_table_destroy (timezone_hash);

	return obj_string;
}

static gboolean
cal_client_get_backend_property_from_cache_finish (EClient *client,
                                                   GAsyncResult *result,
                                                   gchar **prop_value,
                                                   GError **error)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (prop_value != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (client), cal_client_get_backend_property_from_cache_finish), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, &local_error)) {
		e_client_unwrap_dbus_error (client, local_error, error);
		return FALSE;
	}

	*prop_value = g_strdup (g_simple_async_result_get_op_res_gpointer (simple));

	return *prop_value != NULL;
}

static void
cal_client_get_backend_property (EClient *client,
                                 const gchar *prop_name,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	gchar *prop_value;

	prop_value = e_client_get_backend_property_from_cache (client, prop_name);
	if (prop_value) {
		e_client_finish_async_without_dbus (client, cancellable, callback, user_data, cal_client_get_backend_property_from_cache_finish, prop_value, g_free);
	} else {
		e_client_proxy_call_string_with_res_op_data (
			client, prop_name, cancellable, callback, user_data, cal_client_get_backend_property, prop_name,
			e_gdbus_cal_call_get_backend_property,
			NULL, NULL, e_gdbus_cal_call_get_backend_property_finish, NULL, NULL);
	}
}

static gboolean
cal_client_get_backend_property_finish (EClient *client,
                                        GAsyncResult *result,
                                        gchar **prop_value,
                                        GError **error)
{
	gchar *str = NULL;
	gboolean res;

	g_return_val_if_fail (prop_value != NULL, FALSE);

	if (g_simple_async_result_get_source_tag (G_SIMPLE_ASYNC_RESULT (result)) == cal_client_get_backend_property_from_cache_finish) {
		res = cal_client_get_backend_property_from_cache_finish (client, result, &str, error);
	} else {
		res = e_client_proxy_call_finish_string (client, result, &str, error, cal_client_get_backend_property);
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
cal_client_get_backend_property_sync (EClient *client,
                                      const gchar *prop_name,
                                      gchar **prop_value,
                                      GCancellable *cancellable,
                                      GError **error)
{
	ECalClient *cal_client;
	gchar *prop_val;
	gboolean res;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	cal_client = E_CAL_CLIENT (client);

	if (!cal_client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	prop_val = e_client_get_backend_property_from_cache (client, prop_name);
	if (prop_val) {
		g_return_val_if_fail (prop_value != NULL, FALSE);

		*prop_value = prop_val;

		return TRUE;
	}

	res = e_client_proxy_call_sync_string__string (client, prop_name, prop_value, cancellable, error, e_gdbus_cal_call_get_backend_property_sync);

	if (res && prop_value)
		e_client_update_backend_property_cache (client, prop_name, *prop_value);

	return res;
}

static void
cal_client_set_backend_property (EClient *client,
                                 const gchar *prop_name,
                                 const gchar *prop_value,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	gchar **prop_name_value;

	prop_name_value = e_gdbus_cal_encode_set_backend_property (prop_name, prop_value);

	e_client_proxy_call_strv (
		client, (const gchar * const *) prop_name_value, cancellable, callback, user_data, cal_client_set_backend_property,
		e_gdbus_cal_call_set_backend_property,
		e_gdbus_cal_call_set_backend_property_finish, NULL, NULL, NULL, NULL);

	g_strfreev (prop_name_value);
}

static gboolean
cal_client_set_backend_property_finish (EClient *client,
                                        GAsyncResult *result,
                                        GError **error)
{
	return e_client_proxy_call_finish_void (client, result, error, cal_client_set_backend_property);
}

static gboolean
cal_client_set_backend_property_sync (EClient *client,
                                      const gchar *prop_name,
                                      const gchar *prop_value,
                                      GCancellable *cancellable,
                                      GError **error)
{
	ECalClient *cal_client;
	gboolean res;
	gchar **prop_name_value;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	cal_client = E_CAL_CLIENT (client);

	if (!cal_client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	prop_name_value = e_gdbus_cal_encode_set_backend_property (prop_name, prop_value);
	res = e_client_proxy_call_sync_strv__void (client, (const gchar * const *) prop_name_value, cancellable, error, e_gdbus_cal_call_set_backend_property_sync);
	g_strfreev (prop_name_value);

	return res;
}

static void
cal_client_open (EClient *client,
                 gboolean only_if_exists,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
	e_client_proxy_call_boolean (
		client, only_if_exists, cancellable, callback, user_data, cal_client_open,
		e_gdbus_cal_call_open,
		e_gdbus_cal_call_open_finish, NULL, NULL, NULL, NULL);
}

static gboolean
cal_client_open_finish (EClient *client,
                        GAsyncResult *result,
                        GError **error)
{
	return e_client_proxy_call_finish_void (client, result, error, cal_client_open);
}

static gboolean
cal_client_open_sync (EClient *client,
                      gboolean only_if_exists,
                      GCancellable *cancellable,
                      GError **error)
{
	ECalClient *cal_client;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	cal_client = E_CAL_CLIENT (client);

	if (!cal_client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	return e_client_proxy_call_sync_boolean__void (client, only_if_exists, cancellable, error, e_gdbus_cal_call_open_sync);
}

static void
cal_client_refresh (EClient *client,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
	e_client_proxy_call_void (
		client, cancellable, callback, user_data, cal_client_refresh,
		e_gdbus_cal_call_refresh,
		e_gdbus_cal_call_refresh_finish, NULL, NULL, NULL, NULL);
}

static gboolean
cal_client_refresh_finish (EClient *client,
                           GAsyncResult *result,
                           GError **error)
{
	return e_client_proxy_call_finish_void (client, result, error, cal_client_refresh);
}

static gboolean
cal_client_refresh_sync (EClient *client,
                         GCancellable *cancellable,
                         GError **error)
{
	ECalClient *cal_client;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	cal_client = E_CAL_CLIENT (client);

	if (!cal_client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	return e_client_proxy_call_sync_void__void (client, cancellable, error, e_gdbus_cal_call_refresh_sync);
}

static gboolean
complete_string_exchange (gboolean res,
                          gchar *out_string,
                          gchar **result,
                          GError **error)
{
	g_return_val_if_fail (result != NULL, FALSE);

	if (res && out_string) {
		if (*out_string) {
			*result = out_string;
		} else {
			/* empty string is returned as NULL */
			*result = NULL;
			g_free (out_string);
		}
	} else {
		*result = NULL;
		g_free (out_string);
		res = FALSE;

		if (error && !*error)
			g_propagate_error (error, e_client_error_create (E_CLIENT_ERROR_INVALID_ARG, NULL));
	}

	return res;
}

static gboolean
complete_strv_exchange (gboolean res,
                        gchar **out_strings,
                        GSList **result,
                        GError **error)
{
	g_return_val_if_fail (result != NULL, FALSE);

	if (res && out_strings) {
		*result = e_client_util_strv_to_slist ((const gchar * const*) out_strings);
	} else {
		*result = NULL;
		res = FALSE;

		if (error && !*error)
			g_propagate_error (error, e_client_error_create (E_CLIENT_ERROR_INVALID_ARG, NULL));
	}

	g_strfreev (out_strings);

	return res;
}

static gboolean
cal_client_get_default_object_from_cache_finish (EClient *client,
                                                 GAsyncResult *result,
                                                 gchar **prop_value,
                                                 GError **error)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (prop_value != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (client), cal_client_get_default_object_from_cache_finish), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, &local_error)) {
		e_client_unwrap_dbus_error (client, local_error, error);
		return FALSE;
	}

	*prop_value = g_strdup (g_simple_async_result_get_op_res_gpointer (simple));

	return *prop_value != NULL;
}

/**
 * e_cal_client_get_default_object:
 * @client: an #ECalClient
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Retrives an #icalcomponent from the backend that contains the default
 * values for properties needed. The call is finished
 * by e_cal_client_get_default_object_finish() from the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_default_object (ECalClient *client,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	gchar *prop_value;
	EClient *base_client = E_CLIENT (client);

	prop_value = e_client_get_backend_property_from_cache (base_client, CAL_BACKEND_PROPERTY_DEFAULT_OBJECT);
	if (prop_value) {
		e_client_finish_async_without_dbus (base_client, cancellable, callback, user_data, cal_client_get_default_object_from_cache_finish, prop_value, g_free);
	} else {
		e_client_proxy_call_string (
			base_client, CAL_BACKEND_PROPERTY_DEFAULT_OBJECT, cancellable, callback, user_data, e_cal_client_get_default_object,
			e_gdbus_cal_call_get_backend_property,
			NULL, NULL, e_gdbus_cal_call_get_backend_property_finish, NULL, NULL);
	}
}

static gboolean
complete_get_object (gboolean res,
                     gchar *out_string,
                     icalcomponent **icalcomp,
                     gboolean ensure_unique_uid,
                     GError **error)
{
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	if (res && out_string) {
		*icalcomp = icalparser_parse_string (out_string);
		if (!*icalcomp) {
			g_propagate_error (error, e_cal_client_error_create (E_CAL_CLIENT_ERROR_INVALID_OBJECT, NULL));
			res = FALSE;
		} else if (ensure_unique_uid && icalcomponent_get_uid (*icalcomp)) {
			/* make sure the UID is always unique */
			gchar *new_uid = e_cal_component_gen_uid ();

			icalcomponent_set_uid (*icalcomp, new_uid);
			g_free (new_uid);
		}
	} else {
		*icalcomp = NULL;
		res = FALSE;
	}

	g_free (out_string);

	return res;
}

/**
 * e_cal_client_get_default_object_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @icalcomp: (out): Return value for the default calendar object.
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_default_object() and
 * sets @icalcomp to an #icalcomponent from the backend that contains
 * the default values for properties needed. This @icalcomp should be
 * freed with icalcomponent_free().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_default_object_finish (ECalClient *client,
                                        GAsyncResult *result,
                                        icalcomponent **icalcomp,
                                        GError **error)
{
	gboolean res;
	gchar *out_string = NULL;

	g_return_val_if_fail (icalcomp != NULL, FALSE);

	if (g_simple_async_result_get_source_tag (G_SIMPLE_ASYNC_RESULT (result)) == cal_client_get_default_object_from_cache_finish) {
		res = cal_client_get_default_object_from_cache_finish (E_CLIENT (client), result, &out_string, error);
	} else {
		res = e_client_proxy_call_finish_string (E_CLIENT (client), result, &out_string, error, e_cal_client_get_default_object);
	}

	return complete_get_object (res, out_string, icalcomp, TRUE, error);
}

/**
 * e_cal_client_get_default_object_sync:
 * @client: an #ECalClient
 * @icalcomp: (out): Return value for the default calendar object.
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Retrives an #icalcomponent from the backend that contains the default
 * values for properties needed. This @icalcomp should be freed with
 * icalcomponent_free().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_default_object_sync (ECalClient *client,
                                      icalcomponent **icalcomp,
                                      GCancellable *cancellable,
                                      GError **error)
{
	gboolean res;
	gchar *out_string = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	out_string = e_client_get_backend_property_from_cache (E_CLIENT (client), CAL_BACKEND_PROPERTY_DEFAULT_OBJECT);
	if (out_string)
		res = TRUE;
	else
		res = e_client_proxy_call_sync_string__string (E_CLIENT (client), CAL_BACKEND_PROPERTY_DEFAULT_OBJECT, &out_string, cancellable, error, e_gdbus_cal_call_get_backend_property_sync);

	return complete_get_object (res, out_string, icalcomp, TRUE, error);
}

static gboolean
complete_get_object_master (ECalClientSourceType source_type,
                            gboolean res,
                            gchar *out_string,
                            icalcomponent **icalcomp,
                            GError **error)
{
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	if (res && out_string) {
		icalcomponent *tmp_comp = icalparser_parse_string (out_string);
		if (!tmp_comp) {
			*icalcomp = NULL;
			g_propagate_error (error, e_cal_client_error_create (E_CAL_CLIENT_ERROR_INVALID_OBJECT, NULL));
			res = FALSE;
		} else {
			icalcomponent_kind kind;
			icalcomponent *master_comp = NULL;

			switch (source_type) {
			case E_CAL_CLIENT_SOURCE_TYPE_EVENTS:
				kind = ICAL_VEVENT_COMPONENT;
				break;
			case E_CAL_CLIENT_SOURCE_TYPE_TASKS:
				kind = ICAL_VTODO_COMPONENT;
				break;
			case E_CAL_CLIENT_SOURCE_TYPE_MEMOS:
				kind = ICAL_VJOURNAL_COMPONENT;
				break;
			default:
				icalcomponent_free (tmp_comp);
				*icalcomp = NULL;
				res = FALSE;

				g_warn_if_reached ();
			}

			if (res && icalcomponent_isa (tmp_comp) == kind) {
				*icalcomp = tmp_comp;
				tmp_comp = NULL;
			} else if (res && icalcomponent_isa (tmp_comp) == ICAL_VCALENDAR_COMPONENT) {
				for (master_comp = icalcomponent_get_first_component (tmp_comp, kind);
				     master_comp;
				     master_comp = icalcomponent_get_next_component (tmp_comp, kind)) {
					if (!icalcomponent_get_uid (master_comp))
						continue;

					if (icaltime_is_null_time (icalcomponent_get_recurrenceid (master_comp)) ||
					    !icaltime_is_valid_time (icalcomponent_get_recurrenceid (master_comp)))
						break;
				}

				if (!master_comp)
					master_comp = icalcomponent_get_first_component (tmp_comp, kind);

				*icalcomp = master_comp ? icalcomponent_new_clone (master_comp) : NULL;
			}

			if (tmp_comp)
				icalcomponent_free (tmp_comp);
		}
	} else {
		*icalcomp = NULL;
		res = FALSE;
	}

	g_free (out_string);

	return res && *icalcomp;
}

/**
 * e_cal_client_get_object:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component.
 * @rid: Recurrence identifier.
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Queries a calendar for a calendar component object based on its unique
 * identifier. The call is finished by e_cal_client_get_object_finish()
 * from the @callback.
 *
 * Use e_cal_client_get_objects_for_uid() to get list of all
 * objects for the given uid, which includes master object and
 * all detached instances.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_object (ECalClient *client,
                         const gchar *uid,
                         const gchar *rid,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	gchar **strv;

	g_return_if_fail (uid != NULL);

	strv = e_gdbus_cal_encode_get_object (uid, rid);

	e_client_proxy_call_strv (
		E_CLIENT (client), (const gchar * const *) strv, cancellable, callback, user_data, e_cal_client_get_object,
		e_gdbus_cal_call_get_object,
		NULL, NULL, e_gdbus_cal_call_get_object_finish, NULL, NULL);

	g_strfreev (strv);
}

/**
 * e_cal_client_get_object_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @icalcomp: (out): Return value for the calendar component object.
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_object() and
 * sets @icalcomp to queried component. This function always returns
 * master object for a case of @rid being NULL or an empty string.
 * This component should be freed with icalcomponent_free().
 *
 * Use e_cal_client_get_objects_for_uid() to get list of all
 * objects for the given uid, which includes master object and
 * all detached instances.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_object_finish (ECalClient *client,
                                GAsyncResult *result,
                                icalcomponent **icalcomp,
                                GError **error)
{
	gboolean res;
	gchar *out_string = NULL;

	g_return_val_if_fail (icalcomp != NULL, FALSE);

	res = e_client_proxy_call_finish_string (E_CLIENT (client), result, &out_string, error, e_cal_client_get_object);

	return complete_get_object_master (e_cal_client_get_source_type (client), res, out_string, icalcomp, error);
}

/**
 * e_cal_client_get_object_sync:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component.
 * @rid: Recurrence identifier.
 * @icalcomp: (out): Return value for the calendar component object.
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Queries a calendar for a calendar component object based
 * on its unique identifier. This function always returns
 * master object for a case of @rid being NULL or an empty string.
 * This component should be freed with icalcomponent_free().
 *
 * Use e_cal_client_get_objects_for_uid_sync() to get list of all
 * objects for the given uid, which includes master object and
 * all detached instances.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_object_sync (ECalClient *client,
                              const gchar *uid,
                              const gchar *rid,
                              icalcomponent **icalcomp,
                              GCancellable *cancellable,
                              GError **error)
{
	gboolean res;
	gchar *out_string = NULL, **strv;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	strv = e_gdbus_cal_encode_get_object (uid, rid);
	res = e_client_proxy_call_sync_strv__string (E_CLIENT (client), (const gchar * const *) strv, &out_string, cancellable, error, e_gdbus_cal_call_get_object_sync);
	g_strfreev (strv);

	return complete_get_object_master (e_cal_client_get_source_type (client), res, out_string, icalcomp, error);
}

/**
 * e_cal_client_get_objects_for_uid:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Queries a calendar for all calendar components with the given unique
 * ID. This will return any recurring event and all its detached recurrences.
 * For non-recurring events, it will just return the object with that ID.
 * The call is finished by e_cal_client_get_objects_for_uid_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_objects_for_uid (ECalClient *client,
                                  const gchar *uid,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	gchar **strv;

	g_return_if_fail (uid != NULL);

	strv = e_gdbus_cal_encode_get_object (uid, "");

	e_client_proxy_call_strv (
		E_CLIENT (client), (const gchar * const *) strv, cancellable, callback, user_data, e_cal_client_get_objects_for_uid,
		e_gdbus_cal_call_get_object,
		NULL, NULL, e_gdbus_cal_call_get_object_finish, NULL, NULL);

	g_strfreev (strv);
}

static gboolean
complete_get_objects_for_uid (ECalClientSourceType source_type,
                              gboolean res,
                              gchar *out_string,
                              GSList **ecalcomps,
                              GError **error)
{
	icalcomponent *icalcomp = NULL;
	icalcomponent_kind kind;
	ECalComponent *comp;

	res = complete_get_object (res, out_string, &icalcomp, FALSE, error);
	if (!res || !icalcomp)
		return FALSE;

	kind = icalcomponent_isa (icalcomp);
	if ((kind == ICAL_VEVENT_COMPONENT && source_type == E_CAL_CLIENT_SOURCE_TYPE_EVENTS) ||
	    (kind == ICAL_VTODO_COMPONENT && source_type == E_CAL_CLIENT_SOURCE_TYPE_TASKS) ||
	    (kind == ICAL_VJOURNAL_COMPONENT && source_type == E_CAL_CLIENT_SOURCE_TYPE_MEMOS)) {
		comp = e_cal_component_new ();
		e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp));
		*ecalcomps = g_slist_append (NULL, comp);
	} else if (kind == ICAL_VCALENDAR_COMPONENT) {
		icalcomponent *subcomp;
		icalcomponent_kind kind_to_find;

		switch (source_type) {
		case E_CAL_CLIENT_SOURCE_TYPE_TASKS:
			kind_to_find = ICAL_VTODO_COMPONENT;
			break;
		case E_CAL_CLIENT_SOURCE_TYPE_MEMOS:
			kind_to_find = ICAL_VJOURNAL_COMPONENT;
			break;
		case E_CAL_CLIENT_SOURCE_TYPE_EVENTS:
		default:
			kind_to_find = ICAL_VEVENT_COMPONENT;
			break;
		}

		*ecalcomps = NULL;
		subcomp = icalcomponent_get_first_component (icalcomp, kind_to_find);
		while (subcomp) {
			comp = e_cal_component_new ();
			e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (subcomp));
			*ecalcomps = g_slist_prepend (*ecalcomps, comp);
			subcomp = icalcomponent_get_next_component (icalcomp, kind_to_find);
		}

		*ecalcomps = g_slist_reverse (*ecalcomps);
	}

	icalcomponent_free (icalcomp);

	return TRUE;
}

/**
 * e_cal_client_get_objects_for_uid_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @ecalcomps: (out) (transfer full) (element-type ECalComponent): Return value
 * for the list of objects obtained from the backend
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_objects_for_uid() and
 * sets @ecalcomps to a list of #ECalComponent<!-- -->s corresponding to
 * found components for a given uid of the same type as this client.
 * This list should be freed with e_cal_client_free_ecalcomp_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_objects_for_uid_finish (ECalClient *client,
                                         GAsyncResult *result,
                                         GSList **ecalcomps,
                                         GError **error)
{
	gboolean res;
	gchar *out_string = NULL;

	g_return_val_if_fail (ecalcomps != NULL, FALSE);

	res = e_client_proxy_call_finish_string (E_CLIENT (client), result, &out_string, error, e_cal_client_get_objects_for_uid);

	return complete_get_objects_for_uid (e_cal_client_get_source_type (client), res, out_string, ecalcomps, error);
}

/**
 * e_cal_client_get_objects_for_uid_sync:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component
 * @ecalcomps: (out) (transfer full) (element-type ECalComponent): Return value
 * for the list of objects obtained from the backend
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Queries a calendar for all calendar components with the given unique
 * ID. This will return any recurring event and all its detached recurrences.
 * For non-recurring events, it will just return the object with that ID.
 * This list should be freed with e_cal_client_free_ecalcomp_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_objects_for_uid_sync (ECalClient *client,
                                       const gchar *uid,
                                       GSList **ecalcomps,
                                       GCancellable *cancellable,
                                       GError **error)
{
	gboolean res;
	gchar *out_string = NULL, **strv = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (ecalcomps != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	strv = e_gdbus_cal_encode_get_object (uid, "");
	res = e_client_proxy_call_sync_strv__string (E_CLIENT (client), (const gchar * const *) strv, &out_string, cancellable, error, e_gdbus_cal_call_get_object_sync);
	g_strfreev (strv);

	return complete_get_objects_for_uid (e_cal_client_get_source_type (client), res, out_string, ecalcomps, error);
}

/**
 * e_cal_client_get_object_list:
 * @client: an #ECalClient
 * @sexp: an S-expression representing the query
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Gets a list of objects from the calendar that match the query specified
 * by the @sexp argument, returning matching objects as a list of #icalcomponent-s.
 * The call is finished by e_cal_client_get_object_list_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_object_list (ECalClient *client,
                              const gchar *sexp,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	gchar *gdbus_sexp = NULL;

	g_return_if_fail (sexp != NULL);

	e_client_proxy_call_string (
		E_CLIENT (client), e_util_ensure_gdbus_string (sexp, &gdbus_sexp), cancellable, callback, user_data, e_cal_client_get_object_list,
		e_gdbus_cal_call_get_object_list,
		NULL, NULL, NULL, e_gdbus_cal_call_get_object_list_finish, NULL);

	g_free (gdbus_sexp);
}

static gboolean
complete_get_object_list (gboolean res,
                          gchar **out_strv,
                          GSList **icalcomps,
                          GError **error)
{
	g_return_val_if_fail (icalcomps != NULL, FALSE);

	*icalcomps = NULL;

	if (res && out_strv) {
		gint ii;
		icalcomponent *icalcomp;

		for (ii = 0; out_strv[ii]; ii++) {
			icalcomp = icalcomponent_new_from_string (out_strv[ii]);

			if (!icalcomp)
				continue;

			*icalcomps = g_slist_prepend (*icalcomps, icalcomp);
		}

		*icalcomps = g_slist_reverse (*icalcomps);
	} else {
		res = FALSE;
	}

	g_strfreev (out_strv);

	return res;
}

/**
 * e_cal_client_get_object_list_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @icalcomps: (out) (element-type icalcomponent): list of matching
 * #icalcomponent<!-- -->s
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_object_list() and
 * sets @icalcomps to a matching list of #icalcomponent-s.
 * This list should be freed with e_cal_client_free_icalcomp_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_object_list_finish (ECalClient *client,
                                     GAsyncResult *result,
                                     GSList **icalcomps,
                                     GError **error)
{
	gboolean res;
	gchar **out_strv = NULL;

	g_return_val_if_fail (icalcomps != NULL, FALSE);

	res = e_client_proxy_call_finish_strv (E_CLIENT (client), result, &out_strv, error, e_cal_client_get_object_list);

	return complete_get_object_list (res, out_strv, icalcomps, error);
}

/**
 * e_cal_client_get_object_list_sync:
 * @client: an #ECalClient
 * @sexp: an S-expression representing the query
 * @icalcomps: (out) (element-type icalcomponent): list of matching
 * #icalcomponent<!-- -->s
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Gets a list of objects from the calendar that match the query specified
 * by the @sexp argument. The objects will be returned in the @icalcomps
 * argument, which is a list of #icalcomponent.
 * This list should be freed with e_cal_client_free_icalcomp_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_object_list_sync (ECalClient *client,
                                   const gchar *sexp,
                                   GSList **icalcomps,
                                   GCancellable *cancellable,
                                   GError **error)
{
	gboolean res;
	gchar **out_strv = NULL, *gdbus_sexp = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (sexp != NULL, FALSE);
	g_return_val_if_fail (icalcomps != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	res = e_client_proxy_call_sync_string__strv (E_CLIENT (client), e_util_ensure_gdbus_string (sexp, &gdbus_sexp), &out_strv, cancellable, error, e_gdbus_cal_call_get_object_list_sync);
	g_free (gdbus_sexp);

	return complete_get_object_list (res, out_strv, icalcomps, error);
}

/**
 * e_cal_client_get_object_list_as_comps:
 * @client: an #ECalClient
 * @sexp: an S-expression representing the query
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Gets a list of objects from the calendar that match the query specified
 * by the @sexp argument, returning matching objects as a list of #ECalComponent-s.
 * The call is finished by e_cal_client_get_object_list_as_comps_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_object_list_as_comps (ECalClient *client,
                                       const gchar *sexp,
                                       GCancellable *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
	gchar *gdbus_sexp = NULL;

	g_return_if_fail (sexp != NULL);

	e_client_proxy_call_string (
		E_CLIENT (client), e_util_ensure_gdbus_string (sexp, &gdbus_sexp), cancellable, callback, user_data, e_cal_client_get_object_list_as_comps,
		e_gdbus_cal_call_get_object_list,
		NULL, NULL, NULL, e_gdbus_cal_call_get_object_list_finish, NULL);

	g_free (gdbus_sexp);
}

static gboolean
complete_get_object_list_as_comps (gboolean res,
                                   gchar **out_strv,
                                   GSList **ecalcomps,
                                   GError **error)
{
	GSList *icalcomps = NULL;

	g_return_val_if_fail (ecalcomps != NULL, FALSE);

	*ecalcomps = NULL;

	res = complete_get_object_list (res, out_strv, &icalcomps, error);

	if (res) {
		GSList *iter;

		for (iter = icalcomps; iter; iter = iter->next) {
			ECalComponent *comp;

			comp = e_cal_component_new ();
			/* takes ownership of the icalcomp, thus free only the list at the end */
			if (e_cal_component_set_icalcomponent (comp, iter->data))
				*ecalcomps = g_slist_prepend (*ecalcomps, comp);
			else
				icalcomponent_free (iter->data);
		}

		g_slist_free (icalcomps);

		*ecalcomps = g_slist_reverse (*ecalcomps);
	} else {
		e_cal_client_free_icalcomp_slist (icalcomps);
	}

	return res;
}

/**
 * e_cal_client_get_object_list_as_comps_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @ecalcomps: (out) (element-type ECalComponent): list of matching
 * #ECalComponent<!-- -->s
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_object_list_as_comps() and
 * sets @ecalcomps to a matching list of #ECalComponent-s.
 * This list should be freed with e_cal_client_free_ecalcomp_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_object_list_as_comps_finish (ECalClient *client,
                                              GAsyncResult *result,
                                              GSList **ecalcomps,
                                              GError **error)
{
	gboolean res;
	gchar **out_strv = NULL;

	g_return_val_if_fail (ecalcomps != NULL, FALSE);

	res = e_client_proxy_call_finish_strv (E_CLIENT (client), result, &out_strv, error, e_cal_client_get_object_list_as_comps);

	return complete_get_object_list_as_comps (res, out_strv, ecalcomps, error);
}

/**
 * e_cal_client_get_object_list_as_comps_sync:
 * @client: an #ECalClient
 * @sexp: an S-expression representing the query
 * @ecalcomps: (out) (element-type ECalComponent): list of matching
 * #ECalComponent<!-- -->s
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Gets a list of objects from the calendar that match the query specified
 * by the @sexp argument. The objects will be returned in the @ecalcomps
 * argument, which is a list of #ECalComponent.
 * This list should be freed with e_cal_client_free_ecalcomp_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_object_list_as_comps_sync (ECalClient *client,
                                            const gchar *sexp,
                                            GSList **ecalcomps,
                                            GCancellable *cancellable,
                                            GError **error)
{
	gboolean res;
	gchar **out_strv = NULL, *gdbus_sexp = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (sexp != NULL, FALSE);
	g_return_val_if_fail (ecalcomps != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	res = e_client_proxy_call_sync_string__strv (E_CLIENT (client), e_util_ensure_gdbus_string (sexp, &gdbus_sexp), &out_strv, cancellable, error, e_gdbus_cal_call_get_object_list_sync);
	g_free (gdbus_sexp);

	return complete_get_object_list_as_comps (res, out_strv, ecalcomps, error);
}

/**
 * e_cal_client_get_free_busy:
 * @client: an #ECalClient
 * @start: Start time for query
 * @end: End time for query
 * @users: (element-type utf8): List of users to retrieve free/busy information for
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Begins retrieval of free/busy information from the calendar server
 * as a list of #ECalComponent-s. Connect to "free-busy-data" signal
 * to receive chunks of free/busy components.
 * The call is finished by e_cal_client_get_free_busy_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_free_busy (ECalClient *client,
                            time_t start,
                            time_t end,
                            const GSList *users,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	gchar **strv;

	g_return_if_fail (start > 0);
	g_return_if_fail (end > 0);

	strv = e_gdbus_cal_encode_get_free_busy (start, end, users);

	e_client_proxy_call_strv (
		E_CLIENT (client), (const gchar * const *) strv, cancellable, callback, user_data, e_cal_client_get_free_busy,
		e_gdbus_cal_call_get_free_busy,
		e_gdbus_cal_call_get_free_busy_finish, NULL, NULL, NULL, NULL);

	g_strfreev (strv);
}

/**
 * e_cal_client_get_free_busy_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_free_busy().
 * All VFREEBUSY #ECalComponent-s were received by "free-busy-data" signal.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_free_busy_finish (ECalClient *client,
                                   GAsyncResult *result,
                                   GError **error)
{
	return e_client_proxy_call_finish_void (E_CLIENT (client), result, error, e_cal_client_get_free_busy);
}

/**
 * e_cal_client_get_free_busy_sync:
 * @client: an #ECalClient
 * @start: Start time for query
 * @end: End time for query
 * @users: (element-type utf8): List of users to retrieve free/busy information for
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Gets free/busy information from the calendar server.
 * All VFREEBUSY #ECalComponent-s were received by "free-busy-data" signal.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_free_busy_sync (ECalClient *client,
                                 time_t start,
                                 time_t end,
                                 const GSList *users,
                                 GCancellable *cancellable,
                                 GError **error)
{
	gboolean res;
	gchar **strv;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	strv = e_gdbus_cal_encode_get_free_busy (start, end, users);
	res = e_client_proxy_call_sync_strv__void (E_CLIENT (client), (const gchar * const *) strv, cancellable, error, e_gdbus_cal_call_get_free_busy_sync);
	g_strfreev (strv);

	return res;
}

/**
 * e_cal_client_create_object:
 * @client: an #ECalClient
 * @icalcomp: The component to create
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Requests the calendar backend to create the object specified by the @icalcomp
 * argument. Some backends would assign a specific UID to the newly created object,
 * but this function does not modify the original @icalcomp if its UID changes.
 * The call is finished by e_cal_client_create_object_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_create_object (ECalClient *client,
                            /* const */ icalcomponent *icalcomp,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	gchar *comp_str, *gdbus_comp = NULL;
	const gchar *strv[2];

	g_return_if_fail (icalcomp != NULL);

	comp_str = icalcomponent_as_ical_string_r (icalcomp);
	strv[0] = e_util_ensure_gdbus_string (comp_str, &gdbus_comp);
	strv[1] = NULL;

	g_return_if_fail (strv[0] != NULL);

	e_client_proxy_call_strv (
		E_CLIENT (client), strv, cancellable, callback, user_data, e_cal_client_create_object,
		e_gdbus_cal_call_create_objects,
		NULL, NULL, NULL, e_gdbus_cal_call_create_objects_finish, NULL);

	g_free (comp_str);
	g_free (gdbus_comp);
}

/**
 * e_cal_client_create_object_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @uid: (out): Return value for the UID assigned to the new component by the calendar backend
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_create_object() and
 * sets @uid to newly assigned UID for the created object.
 * This @uid should be freed with g_free().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_create_object_finish (ECalClient *client,
                                   GAsyncResult *result,
                                   gchar **uid,
                                   GError **error)
{
	gboolean res;
	gchar **out_strings = NULL;
	gchar *out_string = NULL;

	g_return_val_if_fail (uid != NULL, FALSE);

	res = e_client_proxy_call_finish_strv (E_CLIENT (client), result, &out_strings, error, e_cal_client_create_object);

	if (res && out_strings) {
		out_string = g_strdup (out_strings[0]);
		g_strfreev (out_strings);
	}

	return complete_string_exchange (res, out_string, uid, error);
}

/**
 * e_cal_client_create_object_sync:
 * @client: an #ECalClient
 * @icalcomp: The component to create
 * @uid: (out): Return value for the UID assigned to the new component by the calendar backend
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Requests the calendar backend to create the object specified by the @icalcomp
 * argument. Some backends would assign a specific UID to the newly created object,
 * in those cases that UID would be returned in the @uid argument. This function
 * does not modify the original @icalcomp if its UID changes.
 * Returned @uid should be freed with g_free().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_create_object_sync (ECalClient *client,
                                 /* const */ icalcomponent *icalcomp,
                                 gchar **uid,
                                 GCancellable *cancellable,
                                 GError **error)
{
	gboolean res;
	gchar *comp_str, *gdbus_comp = NULL;
	const gchar *strv[2];
	gchar **out_strings = NULL;
	gchar *out_string = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (icalcomp != NULL, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	comp_str = icalcomponent_as_ical_string_r (icalcomp);
	strv[0] = e_util_ensure_gdbus_string (comp_str, &gdbus_comp);
	strv[1] = NULL;

	g_return_val_if_fail (strv[0] != NULL, FALSE);

	res = e_client_proxy_call_sync_strv__strv (E_CLIENT (client), strv, &out_strings, cancellable, error, e_gdbus_cal_call_create_objects_sync);

	g_free (comp_str);
	g_free (gdbus_comp);

	if (res && out_strings) {
		out_string = g_strdup (out_strings[0]);
		g_strfreev (out_strings);
	}

	return complete_string_exchange (res, out_string, uid, error);
}

/**
 * e_cal_client_create_objects:
 * @client: an #ECalClient
 * @icalcomps: (element-type icalcomponent): The components to create
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Requests the calendar backend to create the objects specified by the @icalcomps
 * argument. Some backends would assign a specific UID to the newly created object,
 * but this function does not modify the original @icalcomps if their UID changes.
 * The call is finished by e_cal_client_create_objects_finish() from
 * the @callback.
 *
 * Since: 3.6
 **/
void
e_cal_client_create_objects (ECalClient *client,
                             GSList *icalcomps,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	gchar **array;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (icalcomps != NULL);

	array = icalcomponent_slist_to_utf8_icomp_array (icalcomps);

	e_client_proxy_call_strv (
		E_CLIENT (client), (const gchar * const *) array, cancellable, callback, user_data, e_cal_client_create_objects,
		e_gdbus_cal_call_create_objects,
		NULL, NULL, NULL, e_gdbus_cal_call_create_objects_finish, NULL);

	g_strfreev (array);
}

/**
 * e_cal_client_create_objects_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @uids: (out) (element-type utf8): Return value for the UIDs assigned to the
 * new components by the calendar backend
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_create_objects() and
 * sets @uids to newly assigned UIDs for the created objects.
 * This @uids should be freed with e_client_util_free_string_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.6
 **/
gboolean
e_cal_client_create_objects_finish (ECalClient *client,
                                    GAsyncResult *result,
                                    GSList **uids,
                                    GError **error)
{
	gboolean res;
	gchar **out_strings = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	res = e_client_proxy_call_finish_strv (E_CLIENT (client), result, &out_strings, error, e_cal_client_create_objects);

	return complete_strv_exchange (res, out_strings, uids, error);
}

/**
 * e_cal_client_create_objects_sync:
 * @client: an #ECalClient
 * @icalcomps: (element-type icalcomponent): The components to create
 * @uids: (out) (element-type utf8): Return value for the UIDs assigned to the
 * new components by the calendar backend
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Requests the calendar backend to create the objects specified by the @icalcomps
 * argument. Some backends would assign a specific UID to the newly created objects,
 * in those cases these UIDs would be returned in the @uids argument. This function
 * does not modify the original @icalcomps if their UID changes.
 * Returned @uid should be freed with e_client_util_free_string_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.6
 **/
gboolean
e_cal_client_create_objects_sync (ECalClient *client,
                                  GSList *icalcomps,
                                  GSList **uids,
                                  GCancellable *cancellable,
                                  GError **error)
{
	gboolean res;
	gchar **array, **out_strings = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (icalcomps != NULL, FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	array = icalcomponent_slist_to_utf8_icomp_array (icalcomps);

	res = e_client_proxy_call_sync_strv__strv (E_CLIENT (client), (const gchar * const *) array, &out_strings, cancellable, error, e_gdbus_cal_call_create_objects_sync);

	return complete_strv_exchange (res, out_strings, uids, error);
}

/**
 * e_cal_client_modify_object:
 * @client: an #ECalClient
 * @icalcomp: Component to modify
 * @mod: Type of modification
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Requests the calendar backend to modify an existing object. If the object
 * does not exist on the calendar, an error will be returned.
 *
 * For recurrent appointments, the @mod argument specifies what to modify,
 * if all instances (CALOBJ_MOD_ALL), a single instance (CALOBJ_MOD_THIS),
 * or a specific set of instances (CALOBJ_MOD_THISNADPRIOR and
 * CALOBJ_MOD_THISANDFUTURE).
 *
 * The call is finished by e_cal_client_modify_object_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_modify_object (ECalClient *client,
                            /* const */ icalcomponent *icalcomp,
                            CalObjModType mod,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	gchar *comp_str, **strv;
	GSList comp_strings = {0,};

	g_return_if_fail (icalcomp != NULL);

	comp_str = icalcomponent_as_ical_string_r (icalcomp);
	comp_strings.data = comp_str;
	strv = e_gdbus_cal_encode_modify_objects (&comp_strings, mod);

	e_client_proxy_call_strv (
		E_CLIENT (client), (const gchar * const *) strv, cancellable, callback, user_data, e_cal_client_modify_object,
		e_gdbus_cal_call_modify_objects,
		e_gdbus_cal_call_modify_objects_finish, NULL, NULL, NULL, NULL);

	g_strfreev (strv);
	g_free (comp_str);
}

/**
 * e_cal_client_modify_object_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_modify_object().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_modify_object_finish (ECalClient *client,
                                   GAsyncResult *result,
                                   GError **error)
{
	return e_client_proxy_call_finish_void (E_CLIENT (client), result, error, e_cal_client_modify_object);
}

/**
 * e_cal_client_modify_object_sync:
 * @client: an #ECalClient
 * @icalcomp: Component to modify
 * @mod: Type of modification
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Requests the calendar backend to modify an existing object. If the object
 * does not exist on the calendar, an error will be returned.
 *
 * For recurrent appointments, the @mod argument specifies what to modify,
 * if all instances (CALOBJ_MOD_ALL), a single instance (CALOBJ_MOD_THIS),
 * or a specific set of instances (CALOBJ_MOD_THISNADPRIOR and
 * CALOBJ_MOD_THISANDFUTURE).
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_modify_object_sync (ECalClient *client,
                                 /* const */ icalcomponent *icalcomp,
                                 CalObjModType mod,
                                 GCancellable *cancellable,
                                 GError **error)
{
	gboolean res;
	gchar *comp_str, **strv;
	GSList comp_strings = {0,};

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	comp_str = icalcomponent_as_ical_string_r (icalcomp);
	comp_strings.data = comp_str;
	strv = e_gdbus_cal_encode_modify_objects (&comp_strings, mod);

	res = e_client_proxy_call_sync_strv__void (E_CLIENT (client), (const gchar * const *) strv, cancellable, error, e_gdbus_cal_call_modify_objects_sync);

	g_strfreev (strv);
	g_free (comp_str);

	return res;
}

/**
 * e_cal_client_modify_objects:
 * @client: an #ECalClient
 * @comps: (element-type icalcomponent): Components to modify
 * @mod: Type of modification
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Requests the calendar backend to modify existing objects. If an object
 * does not exist on the calendar, an error will be returned.
 *
 * For recurrent appointments, the @mod argument specifies what to modify,
 * if all instances (CALOBJ_MOD_ALL), a single instance (CALOBJ_MOD_THIS),
 * or a specific set of instances (CALOBJ_MOD_THISNADPRIOR and
 * CALOBJ_MOD_THISANDFUTURE).
 *
 * The call is finished by e_cal_client_modify_objects_finish() from
 * the @callback.
 *
 * Since: 3.6
 **/
void
e_cal_client_modify_objects (ECalClient *client,
                             /* const */ GSList *comps,
                             CalObjModType mod,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	GSList *comp_strings;
	gchar **strv;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (comps != NULL);

	comp_strings = icalcomponent_slist_to_string_slist (comps);
	strv = e_gdbus_cal_encode_modify_objects (comp_strings, mod);

	e_client_proxy_call_strv (
		E_CLIENT (client), (const gchar * const *) strv, cancellable, callback, user_data, e_cal_client_modify_objects,
		e_gdbus_cal_call_modify_objects,
		e_gdbus_cal_call_modify_objects_finish, NULL, NULL, NULL, NULL);

	g_strfreev (strv);
	e_client_util_free_string_slist (comp_strings);
}

/**
 * e_cal_client_modify_objects_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_modify_objects().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.6
 **/
gboolean
e_cal_client_modify_objects_finish (ECalClient *client,
                                    GAsyncResult *result,
                                    GError **error)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	return e_client_proxy_call_finish_void (E_CLIENT (client), result, error, e_cal_client_modify_objects);
}

/**
 * e_cal_client_modify_objects_sync:
 * @client: an #ECalClient
 * @comps: (element-type icalcomponent): Components to modify
 * @mod: Type of modification
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Requests the calendar backend to modify existing objects. If an object
 * does not exist on the calendar, an error will be returned.
 *
 * For recurrent appointments, the @mod argument specifies what to modify,
 * if all instances (CALOBJ_MOD_ALL), a single instance (CALOBJ_MOD_THIS),
 * or a specific set of instances (CALOBJ_MOD_THISNADPRIOR and
 * CALOBJ_MOD_THISANDFUTURE).
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.6
 **/
gboolean
e_cal_client_modify_objects_sync (ECalClient *client,
                                  /* const */ GSList *comps,
                                  CalObjModType mod,
                                  GCancellable *cancellable,
                                  GError **error)
{
	gboolean res;
	gchar **strv;
	GSList *comp_strings;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (comps != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	comp_strings = icalcomponent_slist_to_string_slist (comps);
	strv = e_gdbus_cal_encode_modify_objects (comp_strings, mod);

	res = e_client_proxy_call_sync_strv__void (E_CLIENT (client), (const gchar * const *) strv, cancellable, error, e_gdbus_cal_call_modify_objects_sync);

	g_strfreev (strv);
	e_client_util_free_string_slist (comp_strings);

	return res;
}

/**
 * e_cal_client_remove_object:
 * @client: an #ECalClient
 * @uid: UID of the object to remove
 * @rid: Recurrence ID of the specific recurrence to remove
 * @mod: Type of the removal
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * This function allows the removal of instances of a recurrent
 * appointment. By using a combination of the @uid, @rid and @mod
 * arguments, you can remove specific instances. If what you want
 * is to remove all instances, use #NULL @rid and CALOBJ_MOD_ALL
 * for the @mod.
 *
 * The call is finished by e_cal_client_remove_object_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_remove_object (ECalClient *client,
                            const gchar *uid,
                            const gchar *rid,
                            CalObjModType mod,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	gchar **strv;
	GSList ids = {0,};
	ECalComponentId id;

	g_return_if_fail (uid != NULL);

	id.uid = (gchar *) uid;
	id.rid = (gchar *) rid;
	ids.data = &id;
	strv = e_gdbus_cal_encode_remove_objects (&ids, mod);

	e_client_proxy_call_strv (
		E_CLIENT (client), (const gchar * const *) strv, cancellable, callback, user_data, e_cal_client_remove_object,
		e_gdbus_cal_call_remove_objects,
		e_gdbus_cal_call_remove_objects_finish, NULL, NULL, NULL, NULL);

	g_strfreev (strv);
}

/**
 * e_cal_client_remove_object_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_remove_object().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_remove_object_finish (ECalClient *client,
                                   GAsyncResult *result,
                                   GError **error)
{
	return e_client_proxy_call_finish_void (E_CLIENT (client), result, error, e_cal_client_remove_object);
}

/**
 * e_cal_client_remove_object_sync:
 * @client: an #ECalClient
 * @uid: UID of the object to remove
 * @rid: Recurrence ID of the specific recurrence to remove
 * @mod: Type of the removal
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * This function allows the removal of instances of a recurrent
 * appointment. By using a combination of the @uid, @rid and @mod
 * arguments, you can remove specific instances. If what you want
 * is to remove all instances, use #NULL @rid and CALOBJ_MODE_THIS
 * for the @mod.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_remove_object_sync (ECalClient *client,
                                 const gchar *uid,
                                 const gchar *rid,
                                 CalObjModType mod,
                                 GCancellable *cancellable,
                                 GError **error)
{
	gboolean res;
	gchar **strv;
	GSList ids = {0,};
	ECalComponentId id;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	id.uid = (gchar *) uid;
	id.rid = (gchar *) rid;
	ids.data = &id;
	strv = e_gdbus_cal_encode_remove_objects (&ids, mod);

	res = e_client_proxy_call_sync_strv__void (E_CLIENT (client), (const gchar * const *) strv, cancellable, error, e_gdbus_cal_call_remove_objects_sync);

	g_strfreev (strv);

	return res;
}

/**
 * e_cal_client_remove_objects:
 * @client: an #ECalClient
 * @ids: (element-type ECalComponentId): A list of #ECalComponentId objects
 * identifying the objects to remove
 * @mod: Type of the removal
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * This function allows the removal of instances of recurrent
 * appointments. #ECalComponentId objects can identify specific instances (if rid is not NULL).
 * If what you want is to remove all instances, use a #NULL rid in the #ECalComponentId and CALOBJ_MOD_ALL
 * for the @mod.
 *
 * The call is finished by e_cal_client_remove_objects_finish() from
 * the @callback.
 *
 * Since: 3.6
 **/
void
e_cal_client_remove_objects (ECalClient *client,
                             const GSList *ids,
                             CalObjModType mod,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	gchar **strv;

	g_return_if_fail (ids != NULL);

	strv = e_gdbus_cal_encode_remove_objects (ids, mod);

	e_client_proxy_call_strv (
		E_CLIENT (client), (const gchar * const *) strv, cancellable, callback, user_data, e_cal_client_remove_objects,
		e_gdbus_cal_call_remove_objects,
		e_gdbus_cal_call_remove_objects_finish, NULL, NULL, NULL, NULL);

	g_strfreev (strv);
}

/**
 * e_cal_client_remove_objects_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_remove_objects().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.6
 **/
gboolean
e_cal_client_remove_objects_finish (ECalClient *client,
                                    GAsyncResult *result,
                                    GError **error)
{
	return e_client_proxy_call_finish_void (E_CLIENT (client), result, error, e_cal_client_remove_objects);
}

/**
 * e_cal_client_remove_objects_sync:
 * @client: an #ECalClient
 * @ids: (element-type ECalComponentId): A list of #ECalComponentId objects
 * identifying the objects to remove
 * @mod: Type of the removal
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * This function allows the removal of instances of recurrent
 * appointments. #ECalComponentId objects can identify specific instances (if rid is not NULL).
 * If what you want is to remove all instances, use a #NULL rid in the #ECalComponentId and CALOBJ_MOD_ALL
 * for the @mod.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.6
 **/
gboolean
e_cal_client_remove_objects_sync (ECalClient *client,
                                  const GSList *ids,
                                  CalObjModType mod,
                                  GCancellable *cancellable,
                                  GError **error)
{
	gboolean res;
	gchar **strv;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (ids != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	strv = e_gdbus_cal_encode_remove_objects (ids, mod);

	res = e_client_proxy_call_sync_strv__void (E_CLIENT (client), (const gchar * const *) strv, cancellable, error, e_gdbus_cal_call_remove_objects_sync);

	g_strfreev (strv);

	return res;
}

/**
 * e_cal_client_receive_objects:
 * @client: an #ECalClient
 * @icalcomp: An #icalcomponent
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Makes the backend receive the set of iCalendar objects specified in the
 * @icalcomp argument. This is used for iTIP confirmation/cancellation
 * messages for scheduled meetings.
 *
 * The call is finished by e_cal_client_receive_objects_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_receive_objects (ECalClient *client,
                              /* const */ icalcomponent *icalcomp,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	gchar *comp_str, *gdbus_comp = NULL;

	g_return_if_fail (icalcomp != NULL);

	comp_str = icalcomponent_as_ical_string_r (icalcomp);

	e_client_proxy_call_string (
		E_CLIENT (client), e_util_ensure_gdbus_string (comp_str, &gdbus_comp), cancellable, callback, user_data, e_cal_client_receive_objects,
		e_gdbus_cal_call_receive_objects,
		e_gdbus_cal_call_receive_objects_finish, NULL, NULL, NULL, NULL);

	g_free (comp_str);
	g_free (gdbus_comp);
}

/**
 * e_cal_client_receive_objects_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_receive_objects().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_receive_objects_finish (ECalClient *client,
                                     GAsyncResult *result,
                                     GError **error)
{
	return e_client_proxy_call_finish_void (E_CLIENT (client), result, error, e_cal_client_receive_objects);
}

/**
 * e_cal_client_receive_objects_sync:
 * @client: an #ECalClient
 * @icalcomp: An #icalcomponent
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Makes the backend receive the set of iCalendar objects specified in the
 * @icalcomp argument. This is used for iTIP confirmation/cancellation
 * messages for scheduled meetings.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_receive_objects_sync (ECalClient *client,
                                   /* const */ icalcomponent *icalcomp,
                                   GCancellable *cancellable,
                                   GError **error)
{
	gboolean res;
	gchar *comp_str, *gdbus_comp = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	comp_str = icalcomponent_as_ical_string_r (icalcomp);

	res = e_client_proxy_call_sync_string__void (E_CLIENT (client), e_util_ensure_gdbus_string (comp_str, &gdbus_comp), cancellable, error, e_gdbus_cal_call_receive_objects_sync);

	g_free (comp_str);
	g_free (gdbus_comp);

	return res;
}

/**
 * e_cal_client_send_objects:
 * @client: an #ECalClient
 * @icalcomp: An icalcomponent to be sent
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Requests a calendar backend to send meeting information stored in @icalcomp.
 * The backend can modify this component and request a send to particular users.
 * The call is finished by e_cal_client_send_objects_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_send_objects (ECalClient *client,
                           /* const */ icalcomponent *icalcomp,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	gchar *comp_str, *gdbus_comp = NULL;

	g_return_if_fail (icalcomp != NULL);

	comp_str = icalcomponent_as_ical_string_r (icalcomp);

	e_client_proxy_call_string (
		E_CLIENT (client), e_util_ensure_gdbus_string (comp_str, &gdbus_comp), cancellable, callback, user_data, e_cal_client_send_objects,
		e_gdbus_cal_call_send_objects,
		NULL, NULL, NULL, e_gdbus_cal_call_send_objects_finish, NULL);

	g_free (comp_str);
	g_free (gdbus_comp);
}

static gboolean
complete_send_objects (gboolean res,
                       gchar **out_strv,
                       GSList **users,
                       icalcomponent **modified_icalcomp,
                       GError **error)
{
	g_return_val_if_fail (users != NULL, FALSE);
	g_return_val_if_fail (modified_icalcomp != NULL, FALSE);

	*users = NULL;
	*modified_icalcomp = NULL;

	if (res && out_strv) {
		gchar *calobj = NULL;

		if (e_gdbus_cal_decode_send_objects ((const gchar * const *) out_strv, &calobj, users)) {
			*modified_icalcomp = icalparser_parse_string (calobj);
			if (!*modified_icalcomp) {
				g_propagate_error (error, e_cal_client_error_create (E_CAL_CLIENT_ERROR_INVALID_OBJECT, NULL));
				e_client_util_free_string_slist (*users);
				*users = NULL;
				res = FALSE;
			}
		} else {
			g_propagate_error (error, e_client_error_create (E_CLIENT_ERROR_INVALID_ARG, NULL));
			e_client_util_free_string_slist (*users);
			*users = NULL;
			res = FALSE;
		}

		g_free (calobj);
	} else {
		res = FALSE;
	}

	g_strfreev (out_strv);

	return res;
}

/**
 * e_cal_client_send_objects_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @users: (out) (element-type utf8): List of users to send
 * the @modified_icalcomp to
 * @modified_icalcomp: (out): Return value for the icalcomponent to be sent
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_send_objects() and
 * populates @users with a list of users to send @modified_icalcomp to.
 * The @users list should be freed with e_client_util_free_string_slist() and
 * the @modified_icalcomp should be freed with icalcomponent_free().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_send_objects_finish (ECalClient *client,
                                  GAsyncResult *result,
                                  GSList **users,
                                  icalcomponent **modified_icalcomp,
                                  GError **error)
{
	gboolean res;
	gchar **out_strv = NULL;

	g_return_val_if_fail (users != NULL, FALSE);
	g_return_val_if_fail (modified_icalcomp != NULL, FALSE);

	res = e_client_proxy_call_finish_strv (E_CLIENT (client), result, &out_strv, error, e_cal_client_send_objects);

	return complete_send_objects (res, out_strv, users, modified_icalcomp, error);
}

/**
 * e_cal_client_send_objects_sync:
 * @client: an #ECalClient
 * @icalcomp: An icalcomponent to be sent
 * @users: (out) (element-type utf8): List of users to send
 * the @modified_icalcomp to
 * @modified_icalcomp: (out): Return value for the icalcomponent to be sent
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Requests a calendar backend to send meeting information stored in @icalcomp.
 * The backend can modify this component and request a send to users in the @users list.
 * The @users list should be freed with e_client_util_free_string_slist() and
 * the @modified_icalcomp should be freed with icalcomponent_free().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_send_objects_sync (ECalClient *client,
                                /* const */ icalcomponent *icalcomp,
                                GSList **users,
                                icalcomponent **modified_icalcomp,
                                GCancellable *cancellable,
                                GError **error)
{
	gboolean res;
	gchar **out_strv = NULL, *comp_str, *gdbus_comp = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (icalcomp != NULL, FALSE);
	g_return_val_if_fail (users != NULL, FALSE);
	g_return_val_if_fail (modified_icalcomp != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	comp_str = icalcomponent_as_ical_string_r (icalcomp);

	res = e_client_proxy_call_sync_string__strv (E_CLIENT (client), e_util_ensure_gdbus_string (comp_str, &gdbus_comp), &out_strv, cancellable, error, e_gdbus_cal_call_send_objects_sync);

	g_free (comp_str);
	g_free (gdbus_comp);

	return complete_send_objects (res, out_strv, users, modified_icalcomp, error);
}

/**
 * e_cal_client_get_attachment_uris:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component
 * @rid: Recurrence identifier
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Queries a calendar for a specified component's object attachment uris.
 * The call is finished by e_cal_client_get_attachment_uris_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_attachment_uris (ECalClient *client,
                                  const gchar *uid,
                                  const gchar *rid,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	gchar **strv;

	g_return_if_fail (uid != NULL);

	strv = e_gdbus_cal_encode_get_attachment_uris (uid, rid);

	e_client_proxy_call_strv (
		E_CLIENT (client), (const gchar * const *) strv, cancellable, callback, user_data, e_cal_client_get_attachment_uris,
		e_gdbus_cal_call_get_attachment_uris,
		NULL, NULL, NULL, e_gdbus_cal_call_get_attachment_uris_finish, NULL);

	g_strfreev (strv);
}

/**
 * e_cal_client_get_attachment_uris_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @attachment_uris: (out) (element-type utf8): Return the list of attachment
 * URIs
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_attachment_uris() and
 * sets @attachment_uris to uris for component's attachments.
 * The list should be freed with e_client_util_free_string_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_attachment_uris_finish (ECalClient *client,
                                         GAsyncResult *result,
                                         GSList **attachment_uris,
                                         GError **error)
{
	gboolean res;
	gchar **out_strv = NULL;

	g_return_val_if_fail (attachment_uris != NULL, FALSE);

	res = e_client_proxy_call_finish_strv (E_CLIENT (client), result, &out_strv, error, e_cal_client_get_attachment_uris);

	if (res && out_strv) {
		*attachment_uris = e_client_util_strv_to_slist ((const gchar * const *) out_strv);
	} else {
		*attachment_uris = NULL;
	}

	g_strfreev (out_strv);

	return res;
}

/**
 * e_cal_client_get_attachment_uris_sync:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component
 * @rid: Recurrence identifier
 * @attachment_uris: (out) (element-type utf8): Return the list of attachment
 * URIs
 * @cancellable: (allow-none): a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Queries a calendar for a specified component's object attachment URIs.
 * The list should be freed with e_client_util_free_string_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_attachment_uris_sync (ECalClient *client,
                                       const gchar *uid,
                                       const gchar *rid,
                                       GSList **attachment_uris,
                                       GCancellable *cancellable,
                                       GError **error)
{
	gboolean res;
	gchar **strv, **out_strv = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (attachment_uris != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	strv = e_gdbus_cal_encode_get_attachment_uris (uid, rid);

	res = e_client_proxy_call_sync_strv__strv (E_CLIENT (client), (const gchar * const *) strv, &out_strv, cancellable, error, e_gdbus_cal_call_get_attachment_uris_sync);

	g_strfreev (strv);

	if (res && out_strv) {
		*attachment_uris = e_client_util_strv_to_slist ((const gchar * const *) out_strv);
	} else {
		*attachment_uris = NULL;
	}

	g_strfreev (out_strv);

	return res;
}

/**
 * e_cal_client_discard_alarm:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component
 * @rid: Recurrence identifier
 * @auid: Alarm identifier to remove
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Removes alarm @auid from a given component identified by @uid and @rid.
 * The call is finished by e_cal_client_discard_alarm_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_discard_alarm (ECalClient *client,
                            const gchar *uid,
                            const gchar *rid,
                            const gchar *auid,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	gchar **strv;

	g_return_if_fail (uid != NULL);
	g_return_if_fail (auid != NULL);

	strv = e_gdbus_cal_encode_discard_alarm (uid, rid, auid);

	e_client_proxy_call_strv (
		E_CLIENT (client), (const gchar * const *) strv, cancellable, callback, user_data, e_cal_client_discard_alarm,
		e_gdbus_cal_call_discard_alarm,
		e_gdbus_cal_call_discard_alarm_finish, NULL, NULL, NULL, NULL);

	g_strfreev (strv);
}

/**
 * e_cal_client_discard_alarm_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_discard_alarm().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_discard_alarm_finish (ECalClient *client,
                                   GAsyncResult *result,
                                   GError **error)
{
	return e_client_proxy_call_finish_void (E_CLIENT (client), result, error, e_cal_client_discard_alarm);
}

/**
 * e_cal_client_discard_alarm_sync:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component
 * @rid: Recurrence identifier
 * @auid: Alarm identifier to remove
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Removes alarm @auid from a given component identified by @uid and @rid.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_discard_alarm_sync (ECalClient *client,
                                 const gchar *uid,
                                 const gchar *rid,
                                 const gchar *auid,
                                 GCancellable *cancellable,
                                 GError **error)
{
	gboolean res;
	gchar **strv;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (auid != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	strv = e_gdbus_cal_encode_discard_alarm (uid, rid, auid);

	res = e_client_proxy_call_sync_strv__void (E_CLIENT (client), (const gchar * const *) strv, cancellable, error, e_gdbus_cal_call_discard_alarm_sync);

	g_strfreev (strv);

	return res;
}

/**
 * e_cal_client_get_view:
 * @client: an #ECalClient
 * @sexp: an S-expression representing the query.
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Query @client with @sexp, creating an #ECalClientView.
 * The call is finished by e_cal_client_get_view_finish()
 * from the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_view (ECalClient *client,
                       const gchar *sexp,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	gchar *gdbus_sexp = NULL;

	g_return_if_fail (sexp != NULL);

	e_client_proxy_call_string (
		E_CLIENT (client), e_util_ensure_gdbus_string (sexp, &gdbus_sexp), cancellable, callback, user_data, e_cal_client_get_view,
		e_gdbus_cal_call_get_view,
		NULL, NULL, e_gdbus_cal_call_get_view_finish, NULL, NULL);

	g_free (gdbus_sexp);
}

static gboolean
complete_get_view (ECalClient *client,
                   gboolean res,
                   gchar *view_path,
                   ECalClientView **view,
                   GError **error)
{
	g_return_val_if_fail (view != NULL, FALSE);

	if (view_path && res && cal_factory_proxy) {
		EGdbusCalView *gdbus_calview;
		GError *local_error = NULL;

		gdbus_calview = e_gdbus_cal_view_proxy_new_sync (
			g_dbus_proxy_get_connection (G_DBUS_PROXY (cal_factory_proxy)),
			G_DBUS_PROXY_FLAGS_NONE,
			CALENDAR_DBUS_SERVICE_NAME,
			view_path,
			NULL,
			&local_error);

		if (gdbus_calview) {
			*view = _e_cal_client_view_new (client, gdbus_calview);
			g_object_unref (gdbus_calview);
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
 * e_cal_client_get_view_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @view: (out) an #ECalClientView
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_view().
 * If successful, then the @view is set to newly allocated #ECalClientView,
 * which should be freed with g_object_unref().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_view_finish (ECalClient *client,
                              GAsyncResult *result,
                              ECalClientView **view,
                              GError **error)
{
	gboolean res;
	gchar *view_path = NULL;

	g_return_val_if_fail (view != NULL, FALSE);

	res = e_client_proxy_call_finish_string (E_CLIENT (client), result, &view_path, error, e_cal_client_get_view);

	return complete_get_view (client, res, view_path, view, error);
}

/**
 * e_cal_client_get_view_sync:
 * @client: an #ECalClient
 * @sexp: an S-expression representing the query.
 * @view: (out) an #ECalClientView
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Query @client with @sexp, creating an #ECalClientView.
 * If successful, then the @view is set to newly allocated #ECalClientView,
 * which should be freed with g_object_unref().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_view_sync (ECalClient *client,
                            const gchar *sexp,
                            ECalClientView **view,
                            GCancellable *cancellable,
                            GError **error)
{
	gboolean res;
	gchar *gdbus_sexp = NULL;
	gchar *view_path = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (sexp != NULL, FALSE);
	g_return_val_if_fail (view != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	res = e_client_proxy_call_sync_string__string (E_CLIENT (client), e_util_ensure_gdbus_string (sexp, &gdbus_sexp), &view_path, cancellable, error, e_gdbus_cal_call_get_view_sync);

	g_free (gdbus_sexp);

	return complete_get_view (client, res, view_path, view, error);
}

static icaltimezone *
cal_client_get_timezone_from_cache (ECalClient *client,
                                    const gchar *tzid)
{
	icaltimezone *zone = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), NULL);
	g_return_val_if_fail (tzid != NULL, NULL);
	g_return_val_if_fail (client->priv->zone_cache != NULL, NULL);
	g_return_val_if_fail (client->priv->zone_cache_lock != NULL, NULL);

	if (!*tzid)
		return NULL;

	g_mutex_lock (client->priv->zone_cache_lock);
	if (g_str_equal (tzid, "UTC")) {
		zone = icaltimezone_get_utc_timezone ();
	} else {
		/* See if we already have it in the cache. */
		zone = g_hash_table_lookup (client->priv->zone_cache, tzid);
	}

	if (!zone) {
		/*
		 * Try to replace the original time zone with a more complete
		 * and/or potentially updated system time zone. Note that this
		 * also applies to TZIDs which match system time zones exactly:
		 * they are extracted via icaltimezone_get_builtin_timezone_from_tzid()
		 * below without a roundtrip to the backend.
		 */
		const gchar *systzid = e_cal_match_tzid (tzid);
		if (systzid) {
			/*
			 * Use built-in time zone *and* rename it:
			 * if the caller is asking for a TZID=FOO,
			 * then likely because it has an event with
			 * such a TZID. Returning a different TZID
			 * would lead to broken VCALENDARs in the
			 * caller.
			 */
			icaltimezone *syszone = icaltimezone_get_builtin_timezone_from_tzid (systzid);
			if (syszone) {
				gboolean found = FALSE;
				icalcomponent *icalcomp = NULL;
				icalproperty *prop;

				icalcomp = icalcomponent_new_clone (icaltimezone_get_component (syszone));
				prop = icalcomponent_get_first_property (icalcomp, ICAL_ANY_PROPERTY);
				while (!found && prop) {
					if (icalproperty_isa (prop) == ICAL_TZID_PROPERTY) {
						icalproperty_set_value_from_string (prop, tzid, "NO");
						found = TRUE;
					}

					prop = icalcomponent_get_next_property (icalcomp, ICAL_ANY_PROPERTY);
				}

				if (icalcomp) {
					zone = icaltimezone_new ();
					if (!icaltimezone_set_component (zone, icalcomp)) {
						icalcomponent_free (icalcomp);
						icaltimezone_free (zone, 1);
						zone = NULL;
					} else {
						g_hash_table_insert (client->priv->zone_cache, g_strdup (icaltimezone_get_tzid (zone)), zone);
					}
				}
			}
		}
	}

	g_mutex_unlock (client->priv->zone_cache_lock);

	return zone;
}

static gboolean
cal_client_get_timezone_from_cache_finish (ECalClient *client,
                                           GAsyncResult *result,
                                           icaltimezone **zone,
                                           GError **error)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (zone != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (client), cal_client_get_timezone_from_cache), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, &local_error)) {
		e_client_unwrap_dbus_error (E_CLIENT (client), local_error, error);
		return FALSE;
	}

	*zone = g_simple_async_result_get_op_res_gpointer (simple);

	return *zone != NULL;
}

/**
 * e_cal_client_get_timezone:
 * @client: an #ECalClient
 * @tzid: ID of the timezone to retrieve
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Retrieves a timezone object from the calendar backend.
 * The call is finished by e_cal_client_get_timezone_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_timezone (ECalClient *client,
                           const gchar *tzid,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	gchar *gdbus_tzid = NULL;
	icaltimezone *zone;

	g_return_if_fail (tzid != NULL);

	zone = cal_client_get_timezone_from_cache (client, tzid);
	if (zone) {
		e_client_finish_async_without_dbus (E_CLIENT (client), cancellable, callback, user_data, cal_client_get_timezone_from_cache, zone, NULL);
	} else {
		e_client_proxy_call_string (
			E_CLIENT (client), e_util_ensure_gdbus_string (tzid, &gdbus_tzid), cancellable, callback, user_data, e_cal_client_get_timezone,
			e_gdbus_cal_call_get_timezone,
			NULL, NULL, e_gdbus_cal_call_get_timezone_finish, NULL, NULL);

		g_free (gdbus_tzid);
	}
}

static gboolean
complete_get_timezone (ECalClient *client,
                       gboolean res,
                       gchar *out_string,
                       icaltimezone **zone,
                       GError **error)
{
	g_return_val_if_fail (zone != NULL, FALSE);

	*zone = NULL;

	if (res && out_string) {
		icalcomponent *icalcomp;

		icalcomp = icalparser_parse_string (out_string);
		if (icalcomp) {
			*zone = icaltimezone_new ();
			if (!icaltimezone_set_component (*zone, icalcomp)) {
				icaltimezone_free (*zone, 1);
				icalcomponent_free (icalcomp);
				res = FALSE;
				g_propagate_error (error, e_cal_client_error_create (E_CAL_CLIENT_ERROR_INVALID_OBJECT, NULL));
			} else {
				g_mutex_lock (client->priv->zone_cache_lock);
				g_hash_table_insert (client->priv->zone_cache, g_strdup (icaltimezone_get_tzid (*zone)), *zone);
				g_mutex_unlock (client->priv->zone_cache_lock);
			}
		} else {
			res = FALSE;
			g_propagate_error (error, e_cal_client_error_create (E_CAL_CLIENT_ERROR_INVALID_OBJECT, NULL));
		}
	} else {
		res = FALSE;
	}

	g_free (out_string);

	return res;
}

/**
 * e_cal_client_get_timezone_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @zone: (out): Return value for the timezone
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_timezone() and
 * sets @zone to a retrieved timezone object from the calendar backend.
 * This object is owned by the @client, thus do not free it.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_timezone_finish (ECalClient *client,
                                  GAsyncResult *result,
                                  icaltimezone **zone,
                                  GError **error)
{
	gboolean res;
	gchar *out_string = NULL;

	g_return_val_if_fail (zone != NULL, FALSE);

	if (g_simple_async_result_get_source_tag (G_SIMPLE_ASYNC_RESULT (result)) == cal_client_get_timezone_from_cache) {
		res = cal_client_get_timezone_from_cache_finish (client, result, zone, error);
	} else {
		res = e_client_proxy_call_finish_string (E_CLIENT (client), result, &out_string, error, e_cal_client_get_timezone);
		res = complete_get_timezone (client, res, out_string, zone, error);
	}

	return res;
}

/**
 * e_cal_client_get_timezone_sync:
 * @client: an #ECalClient
 * @tzid: ID of the timezone to retrieve
 * @zone: (out): Return value for the timezone
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Retrieves a timezone object from the calendar backend.
 * This object is owned by the @client, thus do not free it.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_timezone_sync (ECalClient *client,
                                const gchar *tzid,
                                icaltimezone **zone,
                                GCancellable *cancellable,
                                GError **error)
{
	gboolean res;
	gchar *gdbus_tzid = NULL, *out_string = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (tzid != NULL, FALSE);
	g_return_val_if_fail (zone != NULL, FALSE);

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	*zone = cal_client_get_timezone_from_cache (client, tzid);
	if (*zone)
		return TRUE;

	res = e_client_proxy_call_sync_string__string (E_CLIENT (client), e_util_ensure_gdbus_string (tzid, &gdbus_tzid), &out_string, cancellable, error, e_gdbus_cal_call_get_timezone_sync);

	g_free (gdbus_tzid);

	return complete_get_timezone (client, res, out_string, zone, error);
}

/**
 * e_cal_client_add_timezone:
 * @client: an #ECalClient
 * @zone: The timezone to add
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Add a VTIMEZONE object to the given calendar client.
 * The call is finished by e_cal_client_add_timezone_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_add_timezone (ECalClient *client,
                           /* const */ icaltimezone *zone,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	icalcomponent *icalcomp;
	gchar *zone_str, *gdbus_zone = NULL;

	g_return_if_fail (zone != NULL);

	if (zone == icaltimezone_get_utc_timezone ())
		return;

	icalcomp = icaltimezone_get_component (zone);
	g_return_if_fail (icalcomp != NULL);

	zone_str = icalcomponent_as_ical_string_r (icalcomp);

	e_client_proxy_call_string (
		E_CLIENT (client), e_util_ensure_gdbus_string (zone_str, &gdbus_zone), cancellable, callback, user_data, e_cal_client_add_timezone,
		e_gdbus_cal_call_add_timezone,
		e_gdbus_cal_call_add_timezone_finish, NULL, NULL, NULL, NULL);

	g_free (zone_str);
	g_free (gdbus_zone);
}

/**
 * e_cal_client_add_timezone_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_add_timezone().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_add_timezone_finish (ECalClient *client,
                                  GAsyncResult *result,
                                  GError **error)
{
	return e_client_proxy_call_finish_void (E_CLIENT (client), result, error, e_cal_client_add_timezone);
}

/**
 * e_cal_client_add_timezone_sync:
 * @client: an #ECalClient
 * @zone: The timezone to add
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Add a VTIMEZONE object to the given calendar client.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_add_timezone_sync (ECalClient *client,
                                /* const */ icaltimezone *zone,
                                GCancellable *cancellable,
                                GError **error)
{
	gboolean res;
	icalcomponent *icalcomp;
	gchar *zone_str, *gdbus_zone = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (zone != NULL, FALSE);

	if (zone == icaltimezone_get_utc_timezone ())
		return TRUE;

	icalcomp = icaltimezone_get_component (zone);
	if (!icalcomp) {
		g_propagate_error (error, e_client_error_create (E_CLIENT_ERROR_INVALID_ARG, NULL));
		return FALSE;
	}

	if (!client->priv->gdbus_cal) {
		set_proxy_gone_error (error);
		return FALSE;
	}

	zone_str = icalcomponent_as_ical_string_r (icalcomp);

	res = e_client_proxy_call_sync_string__void (E_CLIENT (client), e_util_ensure_gdbus_string (zone_str, &gdbus_zone), cancellable, error, e_gdbus_cal_call_add_timezone_sync);

	g_free (zone_str);
	g_free (gdbus_zone);

	return res;
}

static GDBusProxy *
cal_client_get_dbus_proxy (EClient *client)
{
	ECalClient *cal_client;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), NULL);

	cal_client = E_CAL_CLIENT (client);

	return cal_client->priv->gdbus_cal;
}

static void
cal_client_unwrap_dbus_error (EClient *client,
                              GError *dbus_error,
                              GError **out_error)
{
	unwrap_dbus_error (dbus_error, out_error);
}

static void
cal_client_retrieve_capabilities (EClient *client,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	g_return_if_fail (E_IS_CAL_CLIENT (client));

	cal_client_get_backend_property (client, CLIENT_BACKEND_PROPERTY_CAPABILITIES, cancellable, callback, user_data);
}

static gboolean
cal_client_retrieve_capabilities_finish (EClient *client,
                                         GAsyncResult *result,
                                         gchar **capabilities,
                                         GError **error)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	return cal_client_get_backend_property_finish (client, result, capabilities, error);
}

static gboolean
cal_client_retrieve_capabilities_sync (EClient *client,
                                       gchar **capabilities,
                                       GCancellable *cancellable,
                                       GError **error)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	return cal_client_get_backend_property_sync (client, CLIENT_BACKEND_PROPERTY_CAPABILITIES, capabilities, cancellable, error);
}

static void
free_zone_cb (gpointer zone)
{
	icaltimezone_free (zone, 1);
}

static void
e_cal_client_init (ECalClient *client)
{
	LOCK_FACTORY ();
	active_cal_clients++;
	UNLOCK_FACTORY ();

	client->priv = E_CAL_CLIENT_GET_PRIVATE (client);
	client->priv->source_type = E_CAL_CLIENT_SOURCE_TYPE_LAST;
	client->priv->default_zone = icaltimezone_get_utc_timezone ();
	client->priv->cache_dir = NULL;
	client->priv->zone_cache_lock = g_mutex_new ();
	client->priv->zone_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, free_zone_cb);
}

static void
cal_client_dispose (GObject *object)
{
	EClient *client;

	client = E_CLIENT (object);

	e_client_cancel_all (client);

	gdbus_cal_client_disconnect (E_CAL_CLIENT (client));

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_cal_client_parent_class)->dispose (object);
}

static void
cal_client_finalize (GObject *object)
{
	ECalClient *client;
	ECalClientPrivate *priv;

	client = E_CAL_CLIENT (object);

	priv = client->priv;

	g_free (priv->cache_dir);
	priv->cache_dir = NULL;

	if (priv->default_zone && priv->default_zone != icaltimezone_get_utc_timezone ())
		icaltimezone_free (priv->default_zone, 1);
	priv->default_zone = NULL;

	g_mutex_lock (priv->zone_cache_lock);
	g_hash_table_destroy (priv->zone_cache);
	priv->zone_cache = NULL;
	g_mutex_unlock (priv->zone_cache_lock);
	g_mutex_free (priv->zone_cache_lock);
	priv->zone_cache_lock = NULL;

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_client_parent_class)->finalize (object);

	LOCK_FACTORY ();
	active_cal_clients--;
	if (!active_cal_clients)
		gdbus_cal_factory_proxy_disconnect (NULL);
	UNLOCK_FACTORY ();
}

static void
e_cal_client_class_init (ECalClientClass *class)
{
	GObjectClass *object_class;
	EClientClass *client_class;

	g_type_class_add_private (class, sizeof (ECalClientPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = cal_client_dispose;
	object_class->finalize = cal_client_finalize;

	client_class = E_CLIENT_CLASS (class);
	client_class->get_dbus_proxy			= cal_client_get_dbus_proxy;
	client_class->unwrap_dbus_error			= cal_client_unwrap_dbus_error;
	client_class->retrieve_capabilities		= cal_client_retrieve_capabilities;
	client_class->retrieve_capabilities_finish	= cal_client_retrieve_capabilities_finish;
	client_class->retrieve_capabilities_sync	= cal_client_retrieve_capabilities_sync;
	client_class->get_backend_property		= cal_client_get_backend_property;
	client_class->get_backend_property_finish	= cal_client_get_backend_property_finish;
	client_class->get_backend_property_sync		= cal_client_get_backend_property_sync;
	client_class->set_backend_property		= cal_client_set_backend_property;
	client_class->set_backend_property_finish	= cal_client_set_backend_property_finish;
	client_class->set_backend_property_sync		= cal_client_set_backend_property_sync;
	client_class->open				= cal_client_open;
	client_class->open_finish			= cal_client_open_finish;
	client_class->open_sync				= cal_client_open_sync;
	client_class->refresh				= cal_client_refresh;
	client_class->refresh_finish			= cal_client_refresh_finish;
	client_class->refresh_sync			= cal_client_refresh_sync;

	signals[FREE_BUSY_DATA] = g_signal_new (
		"free-busy-data",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ECalClientClass, free_busy_data),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);
}

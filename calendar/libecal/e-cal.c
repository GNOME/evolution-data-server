/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar ecal
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2009 Intel Corporation
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Rodrigo Moya <rodrigo@novell.com>
 *          Ross Burton <ross@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:e-cal
 *
 * The old signal "cal-opened" is deprecated since 3.0 and is replaced with
 * its equivalent "cal_opened_ex", which has a detailed #GError structure
 * as a parameter, instead of a status code only.
 *
 * Deprecated: 3.2: Use #ECalClient instead.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <string.h>
#include <glib/gi18n-lib.h>

#include <libical/ical.h>

#include "e-cal-check-timezones.h"
#include "e-cal-marshal.h"
#include "e-cal-time-util.h"
#include "e-cal-view-private.h"
#include "e-cal.h"

#include "e-gdbus-cal.h"
#include "e-gdbus-cal-view.h"
#include "e-gdbus-cal-factory.h"

#define E_CAL_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL, ECalPrivate))

#define CLIENT_BACKEND_PROPERTY_CACHE_DIR		"cache-dir"
#define CLIENT_BACKEND_PROPERTY_CAPABILITIES		"capabilities"
#define CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS		"cal-email-address"
#define CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS	"alarm-email-address"
#define CAL_BACKEND_PROPERTY_DEFAULT_OBJECT		"default-object"

static guint active_cals = 0, cal_connection_closed_id = 0;
static EGdbusCalFactory *cal_factory_proxy = NULL;
static GStaticRecMutex cal_factory_proxy_lock = G_STATIC_REC_MUTEX_INIT;
#define LOCK_FACTORY()   g_static_rec_mutex_lock   (&cal_factory_proxy_lock)
#define UNLOCK_FACTORY() g_static_rec_mutex_unlock (&cal_factory_proxy_lock)

#define LOCK_CACHE()   g_static_rec_mutex_lock   (&priv->cache_lock)
#define UNLOCK_CACHE() g_static_rec_mutex_unlock (&priv->cache_lock)

G_DEFINE_TYPE (ECal, e_cal, G_TYPE_OBJECT)

static gboolean open_calendar (ECal *ecal, gboolean only_if_exists, GError **error,
	ECalendarStatus *status,
	gboolean async);
static void e_cal_dispose (GObject *object);
static void e_cal_finalize (GObject *object);

/* Private part of the ECal structure */
struct _ECalPrivate {
	GDBusProxy *gdbus_cal;
	guint gone_signal_id;

	/* Load state to avoid multiple loads */
	ECalLoadState load_state;

	ESource *source;
	ECalSourceType type;

	/* Email address associated with this calendar, or NULL */
	gchar *cal_address;
	gchar *alarm_email_address;
	gchar *ldap_attribute;

	/* Scheduling info */
	gchar *capabilities;

	gint mode;

	gboolean read_only;

	/* A cache of timezones retrieved from the server, to avoid getting
	 * them repeatedly for each get_object () call. */
	GHashTable *timezones;

	/* The default timezone to use to resolve DATE and floating DATE-TIME
	 * values. */
	icaltimezone *default_zone;

	gchar *local_attachment_store;

	/* For locking the operation while localling cache values like 
	 * static capabilities, cal address etc. */
	GStaticRecMutex cache_lock;

	GList **free_busy_data;
	GMutex *free_busy_data_lock;
};



/* Signal IDs */
enum {
	CAL_OPENED,
	CAL_OPENED_EX,
	CAL_SET_MODE,
	BACKEND_ERROR,
	BACKEND_DIED,
	LAST_SIGNAL
};

static guint e_cal_signals[LAST_SIGNAL];

#ifdef __PRETTY_FUNCTION__
#define e_return_error_if_fail(expr,error_code)	G_STMT_START{		\
     if G_LIKELY (expr) { } else						\
       {								\
	 g_log (G_LOG_DOMAIN,						\
		G_LOG_LEVEL_CRITICAL,					\
		"file %s: line %d (%s): assertion `%s' failed",		\
		__FILE__,						\
		__LINE__,						\
		__PRETTY_FUNCTION__,					\
		#expr);							\
	 g_set_error (error, E_CALENDAR_ERROR, (error_code),                \
		"file %s: line %d (%s): assertion `%s' failed",		\
		__FILE__,						\
		__LINE__,						\
		__PRETTY_FUNCTION__,					\
		#expr);							\
	 return FALSE;							\
       };				}G_STMT_END
#else
#define e_return_error_if_fail(expr,error_code)	G_STMT_START{		\
     if G_LIKELY (expr) { } else						\
       {								\
	 g_log (G_LOG_DOMAIN,						\
		G_LOG_LEVEL_CRITICAL,					\
		"file %s: line %d: assertion `%s' failed",		\
		__FILE__,						\
		__LINE__,						\
		#expr);							\
	 g_set_error (error, E_CALENDAR_ERROR, (error_code),                \
		"file %s: line %d: assertion `%s' failed",		\
		__FILE__,						\
		__LINE__,						\
		#expr);							\
	 return FALSE;							\
       };				}G_STMT_END
#endif

#define E_CALENDAR_CHECK_STATUS(status,error)				\
G_STMT_START{								\
	if ((status) == E_CALENDAR_STATUS_OK)				\
		return TRUE;						\
	else {								\
		const gchar *msg;					\
		if (error && *error)					\
			return unwrap_gerror (error);			\
		msg = e_cal_get_error_message ((status));		\
		g_set_error ((error), E_CALENDAR_ERROR, (status), "%s", msg);	\
		return FALSE;						\
	}								\
} G_STMT_END



/* Error quark */
GQuark
e_calendar_error_quark (void)
{
	static GQuark q = 0;
	if (q == 0)
		q = g_quark_from_static_string ("e-calendar-error-quark");

	return q;
}

/*
 * If the GError is a remote error, extract the EBookStatus embedded inside.
 * Otherwise return CORBA_EXCEPTION (I know this is DBus...).
 */
static ECalendarStatus
get_status_from_error (const GError *error)
{
	#define err(a,b) "org.gnome.evolution.dataserver.Calendar." a, b
	static struct {
		const gchar *name;
		ECalendarStatus err_code;
	} errors[] = {
		{ err ("Success",				E_CALENDAR_STATUS_OK) },
		{ err ("Busy",					E_CALENDAR_STATUS_BUSY) },
		{ err ("RepositoryOffline",			E_CALENDAR_STATUS_REPOSITORY_OFFLINE) },
		{ err ("PermissionDenied",			E_CALENDAR_STATUS_PERMISSION_DENIED) },
		{ err ("InvalidRange",				E_CALENDAR_STATUS_OTHER_ERROR) },
		{ err ("ObjectNotFound",			E_CALENDAR_STATUS_OBJECT_NOT_FOUND) },
		{ err ("InvalidObject",				E_CALENDAR_STATUS_INVALID_OBJECT) },
		{ err ("ObjectIdAlreadyExists",			E_CALENDAR_STATUS_OBJECT_ID_ALREADY_EXISTS) },
		{ err ("AuthenticationFailed",			E_CALENDAR_STATUS_AUTHENTICATION_FAILED) },
		{ err ("AuthenticationRequired",		E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED) },
		{ err ("UnsupportedField",			E_CALENDAR_STATUS_OTHER_ERROR) },
		{ err ("UnsupportedMethod",			E_CALENDAR_STATUS_OTHER_ERROR) },
		{ err ("UnsupportedAuthenticationMethod",	E_CALENDAR_STATUS_OTHER_ERROR) },
		{ err ("TLSNotAvailable",			E_CALENDAR_STATUS_OTHER_ERROR) },
		{ err ("NoSuchCal",				E_CALENDAR_STATUS_NO_SUCH_CALENDAR) },
		{ err ("UnknownUser",				E_CALENDAR_STATUS_UNKNOWN_USER) },
		{ err ("OfflineUnavailable",			E_CALENDAR_STATUS_OTHER_ERROR) },
		{ err ("SearchSizeLimitExceeded",		E_CALENDAR_STATUS_OTHER_ERROR) },
		{ err ("SearchTimeLimitExceeded",		E_CALENDAR_STATUS_OTHER_ERROR) },
		{ err ("InvalidQuery",				E_CALENDAR_STATUS_OTHER_ERROR) },
		{ err ("QueryRefused",				E_CALENDAR_STATUS_OTHER_ERROR) },
		{ err ("CouldNotCancel",			E_CALENDAR_STATUS_COULD_NOT_CANCEL) },
		{ err ("OtherError",				E_CALENDAR_STATUS_OTHER_ERROR) },
		{ err ("InvalidServerVersion",			E_CALENDAR_STATUS_INVALID_SERVER_VERSION) },
		{ err ("InvalidArg",				E_CALENDAR_STATUS_INVALID_ARG) },
		{ err ("NotSupported",				E_CALENDAR_STATUS_NOT_SUPPORTED) }
	};
	#undef err

	if G_LIKELY (error == NULL)
		return Success;

	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR)) {
		gchar *name;
		gint i;

		name = g_dbus_error_get_remote_error (error);

		for (i = 0; i < G_N_ELEMENTS (errors); i++) {
			if (g_ascii_strcasecmp (errors[i].name, name) == 0) {
				g_free (name);
				return errors[i].err_code;
			}
		}

		g_warning ("Unmatched error name %s", name);
		g_free (name);

		return E_CALENDAR_STATUS_OTHER_ERROR;
	} else if (error->domain == E_CALENDAR_ERROR) {
		return error->code;
	} else {
		/* In this case the error was caused by DBus */
		return E_CALENDAR_STATUS_DBUS_EXCEPTION;
	}
}

/*
 * If the specified GError is a remote error, then create a new error
 * representing the remote error.  If the error is anything else, then leave it
 * alone.
 */
static gboolean
unwrap_gerror (GError **error)
{
	if (*error == NULL)
		return TRUE;

	if (g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR)) {
		GError *new_error = NULL;
		gint code;

		code = get_status_from_error (*error);
		g_dbus_error_strip_remote_error (*error);

		new_error = g_error_new_literal (E_CALENDAR_ERROR, code, (*error)->message);

		g_error_free (*error);
		*error = new_error;
	}

	return FALSE;
}

/**
 * e_cal_source_type_enum_get_type:
 *
 * Registers the #ECalSourceTypeEnum type with glib.
 *
 * Returns: the ID of the #ECalSourceTypeEnum type.
 *
 * Deprecated: 3.2: Use e_cal_client_source_type_enum_get_type() instead.
 */
GType
e_cal_source_type_enum_get_type (void)
{
	static volatile gsize enum_type__volatile = 0;

	if (g_once_init_enter (&enum_type__volatile)) {
		GType enum_type;
		static GEnumValue values[] = {
			{ E_CAL_SOURCE_TYPE_EVENT, "Event", "Event"},
			{ E_CAL_SOURCE_TYPE_TODO, "ToDo", "ToDo"},
			{ E_CAL_SOURCE_TYPE_JOURNAL, "Journal", "Journal"},
			{ E_CAL_SOURCE_TYPE_LAST, "Invalid", "Invalid"},
			{ -1, NULL, NULL}
		};

		enum_type = g_enum_register_static ("ECalSourceTypeEnum", values);
		g_once_init_leave (&enum_type__volatile, enum_type);
	}

	return enum_type__volatile;
}

/**
 * e_cal_set_mode_status_enum_get_type:
 *
 * Registers the #ECalSetModeStatusEnum type with glib.
 *
 * Returns: the ID of the #ECalSetModeStatusEnum type.
 *
 * Deprecated: 3.2: This type has been dropped completely.
 */
GType
e_cal_set_mode_status_enum_get_type (void)
{
	static volatile gsize enum_type__volatile = 0;

	if (g_once_init_enter (&enum_type__volatile)) {
		GType enum_type;
		static GEnumValue values[] = {
			{ E_CAL_SET_MODE_SUCCESS,          "ECalSetModeSuccess",         "success"     },
			{ E_CAL_SET_MODE_ERROR,            "ECalSetModeError",           "error"       },
			{ E_CAL_SET_MODE_NOT_SUPPORTED,    "ECalSetModeNotSupported",    "unsupported" },
			{ -1,                                   NULL,                              NULL}
		};

		enum_type = g_enum_register_static ("ECalSetModeStatusEnum", values);
		g_once_init_leave (&enum_type__volatile, enum_type);
	}

	return enum_type__volatile;
}

/**
 * cal_mode_enum_get_type:
 *
 * Registers the #CalModeEnum type with glib.
 *
 * Returns: the ID of the #CalModeEnum type.
 *
 * Deprecated: 3.2: This type has been dropped completely.
 */
GType
cal_mode_enum_get_type (void)
{
	static volatile gsize enum_type__volatile = 0;

	if (g_once_init_enter (&enum_type__volatile)) {
		GType enum_type;
		static GEnumValue values[] = {
			{ CAL_MODE_INVALID,                     "CalModeInvalid",                  "invalid" },
			{ CAL_MODE_LOCAL,                       "CalModeLocal",                    "local"   },
			{ CAL_MODE_REMOTE,                      "CalModeRemote",                   "remote"  },
			{ CAL_MODE_ANY,                         "CalModeAny",                      "any"     },
			{ -1,                                   NULL,                              NULL      }
		};

		enum_type = g_enum_register_static ("CalModeEnum", values);
		g_once_init_leave (&enum_type__volatile, enum_type);
	}

	return enum_type__volatile;
}

static EDataCalObjType
convert_type (ECalSourceType type)
{
	switch (type) {
	case E_CAL_SOURCE_TYPE_EVENT:
		return Event;
	case E_CAL_SOURCE_TYPE_TODO:
		return Todo;
	case E_CAL_SOURCE_TYPE_JOURNAL:
		return Journal;
	default:
		return AnyType;
	}

	return AnyType;
}

static void
e_cal_init (ECal *ecal)
{
	LOCK_FACTORY ();
	active_cals++;
	UNLOCK_FACTORY ();

	ecal->priv = E_CAL_GET_PRIVATE (ecal);

	ecal->priv->load_state = E_CAL_LOAD_NOT_LOADED;
	ecal->priv->local_attachment_store = NULL;

	ecal->priv->cal_address = NULL;
	ecal->priv->alarm_email_address = NULL;
	ecal->priv->ldap_attribute = NULL;
	ecal->priv->capabilities = NULL;
	ecal->priv->gdbus_cal = NULL;
	ecal->priv->timezones = g_hash_table_new (g_str_hash, g_str_equal);
	ecal->priv->default_zone = icaltimezone_get_utc_timezone ();
	ecal->priv->free_busy_data_lock = g_mutex_new ();
	g_static_rec_mutex_init (&ecal->priv->cache_lock);
}

static void
gdbus_cal_disconnect (ECal *ecal);

/*
 * Called when the calendar server dies.
 */
static void
gdbus_cal_closed_cb (GDBusConnection *connection,
                     gboolean remote_peer_vanished,
                     GError *error,
                     ECal *ecal)
{
	GError *err = NULL;

	g_return_if_fail (E_IS_CAL (ecal));

	if (error) {
		err = g_error_copy (error);
		unwrap_gerror (&err);
	}

	if (err) {
		g_debug (G_STRLOC ": ECal GDBus connection is closed%s: %s", remote_peer_vanished ? ", remote peer vanished" : "", err->message);
		g_error_free (err);
	} else {
		g_debug (G_STRLOC ": ECal GDBus connection is closed%s", remote_peer_vanished ? ", remote peer vanished" : "");
	}

	gdbus_cal_disconnect (ecal);

	g_signal_emit (G_OBJECT (ecal), e_cal_signals[BACKEND_DIED], 0);
}

static void
gdbus_cal_connection_gone_cb (GDBusConnection *connection,
                              const gchar *sender_name,
                              const gchar *object_path,
                              const gchar *interface_name,
                              const gchar *signal_name,
                              GVariant *parameters,
                              gpointer user_data)
{
	/* signal subscription takes care of correct parameters,
	 * thus just do what is to be done here */
	gdbus_cal_closed_cb (connection, TRUE, NULL, user_data);
}

static void
gdbus_cal_disconnect (ECal *ecal)
{
	/* Ensure that everything relevant is NULL */
	LOCK_FACTORY ();

	if (ecal->priv->gdbus_cal) {
		GError *error = NULL;
		GDBusConnection *connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (ecal->priv->gdbus_cal));

		g_signal_handlers_disconnect_by_func (connection, gdbus_cal_closed_cb, ecal);
		g_dbus_connection_signal_unsubscribe (connection, ecal->priv->gone_signal_id);
		ecal->priv->gone_signal_id = 0;

		e_gdbus_cal_call_close_sync (ecal->priv->gdbus_cal, NULL, &error);
		g_object_unref (ecal->priv->gdbus_cal);
		ecal->priv->gdbus_cal = NULL;

		if (error) {
			unwrap_gerror (&error);

			g_warning ("%s: Failed to close calendar, %s\n", G_STRFUNC, error->message);
			g_error_free (error);
		}
	}
	UNLOCK_FACTORY ();
}

/* Dispose handler for the calendar ecal */
static void
e_cal_dispose (GObject *object)
{
	ECal *ecal = E_CAL (object);

	gdbus_cal_disconnect (ecal);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_cal_parent_class)->dispose (object);
}

static void
free_timezone (gpointer key,
               gpointer value,
               gpointer data)
{
	/* Note that the key comes from within the icaltimezone value, so we
	 * don't free that. */
	icaltimezone_free (value, TRUE);
}

/* Finalize handler for the calendar ecal */
static void
e_cal_finalize (GObject *object)
{
	ECal *ecal;
	ECalPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL (object));

	ecal = E_CAL (object);
	priv = ecal->priv;

	priv->load_state = E_CAL_LOAD_NOT_LOADED;

	if (priv->source) {
		g_object_unref (priv->source);
		priv->source = NULL;
	}

	if (priv->local_attachment_store) {
		g_free (priv->local_attachment_store);
		priv->local_attachment_store = NULL;
	}

	if (priv->cal_address) {
		g_free (priv->cal_address);
		priv->cal_address = NULL;
	}
	if (priv->alarm_email_address) {
		g_free (priv->alarm_email_address);
		priv->alarm_email_address = NULL;
	}
	if (priv->ldap_attribute) {
		g_free (priv->ldap_attribute);
		priv->ldap_attribute = NULL;
	}
	if (priv->capabilities) {
		g_free (priv->capabilities);
		priv->capabilities = NULL;
	}

	if (priv->free_busy_data) {
		g_mutex_lock (priv->free_busy_data_lock);
		g_list_foreach (*priv->free_busy_data, (GFunc) g_object_unref, NULL);
		g_list_free (*priv->free_busy_data);
		*priv->free_busy_data = NULL;
		priv->free_busy_data = NULL;
		g_mutex_unlock (priv->free_busy_data_lock);
	}

	g_hash_table_foreach (priv->timezones, free_timezone, NULL);
	g_hash_table_destroy (priv->timezones);
	priv->timezones = NULL;
	g_static_rec_mutex_free (&priv->cache_lock);
	g_mutex_free (priv->free_busy_data_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_parent_class)->finalize (object);

	LOCK_FACTORY ();
	active_cals--;
	UNLOCK_FACTORY ();
}

/* Class initialization function for the calendar ecal */
static void
e_cal_class_init (ECalClass *class)
{
	GObjectClass *object_class;

	object_class = (GObjectClass *) class;

	/* XXX The "cal-opened" signal is deprecated. */
	e_cal_signals[CAL_OPENED] = g_signal_new (
		"cal_opened",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ECalClass, cal_opened),
		NULL, NULL,
		g_cclosure_marshal_VOID__INT,
		G_TYPE_NONE, 1, G_TYPE_INT);

	/**
	 * ECal::cal-opened-ex:
	 * @ecal:: self
	 * @error: (type glong):
	 */
	e_cal_signals[CAL_OPENED_EX] = g_signal_new (
		"cal_opened_ex",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ECalClass, cal_opened_ex),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);

	e_cal_signals[CAL_SET_MODE] = g_signal_new (
		"cal_set_mode",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ECalClass, cal_set_mode),
		NULL, NULL,
		e_cal_marshal_VOID__ENUM_ENUM,
		G_TYPE_NONE, 2,
		E_CAL_SET_MODE_STATUS_ENUM_TYPE,
		CAL_MODE_ENUM_TYPE);

	e_cal_signals[BACKEND_ERROR] = g_signal_new (
		"backend_error",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ECalClass, backend_error),
		NULL, NULL,
		g_cclosure_marshal_VOID__STRING,
		G_TYPE_NONE, 1,
		G_TYPE_STRING);

	e_cal_signals[BACKEND_DIED] = g_signal_new (
		"backend_died",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ECalClass, backend_died),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);

	class->cal_opened = NULL;
	class->cal_opened_ex = NULL;
	class->backend_died = NULL;

	object_class->dispose = e_cal_dispose;
	object_class->finalize = e_cal_finalize;

	g_type_class_add_private (class, sizeof (ECalPrivate));
}

static void
cal_factory_proxy_closed_cb (GDBusConnection *connection,
                             gboolean remote_peer_vanished,
                             GError *error,
                             gpointer user_data)
{
	GError *err = NULL;

	LOCK_FACTORY ();

	if (cal_connection_closed_id) {
		g_dbus_connection_signal_unsubscribe (connection, cal_connection_closed_id);
		cal_connection_closed_id = 0;
		g_signal_handlers_disconnect_by_func (connection, cal_factory_proxy_closed_cb, NULL);
	}

	if (cal_factory_proxy) {
		g_object_unref (cal_factory_proxy);
		cal_factory_proxy = NULL;
	}

	if (error) {
		err = g_error_copy (error);
		unwrap_gerror (&err);
	}

	if (err) {
		g_debug ("GDBus connection is closed%s: %s", remote_peer_vanished ? ", remote peer vanished" : "", err->message);
		g_error_free (err);
	} else if (active_cals) {
		g_debug ("GDBus connection is closed%s", remote_peer_vanished ? ", remote peer vanished" : "");
	}

	UNLOCK_FACTORY ();
}

static void
cal_factory_connection_gone_cb (GDBusConnection *connection,
                                const gchar *sender_name,
                                const gchar *object_path,
                                const gchar *interface_name,
                                const gchar *signal_name,
                                GVariant *parameters,
                                gpointer user_data)
{
	/* signal subscription takes care of correct parameters,
	 * thus just do what is to be done here */
	cal_factory_proxy_closed_cb (connection, TRUE, NULL, user_data);
}

/* one-time start up for libecal */
static gboolean
e_cal_activate (GError **error)
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
		NULL,					/* sender */
		"org.freedesktop.DBus",			/* interface */
		"NameOwnerChanged",			/* member */
		"/org/freedesktop/DBus",		/* object_path */
		CALENDAR_DBUS_SERVICE_NAME,		/* arg0 */
		G_DBUS_SIGNAL_FLAGS_NONE,
		cal_factory_connection_gone_cb, NULL, NULL);

	g_signal_connect (connection, "closed", G_CALLBACK (cal_factory_proxy_closed_cb), NULL);

	UNLOCK_FACTORY ();

	return TRUE;
}

static void async_open_report_result (ECal *ecal, const GError *error);

static void
free_busy_data_cb (EGdbusCal *gdbus_cal,
                   const gchar * const *free_busy_strv,
                   ECal *cal)
{
	ECalPrivate *priv;

	g_return_if_fail (E_IS_CAL (cal));

	priv = cal->priv;

	g_mutex_lock (priv->free_busy_data_lock);

	if (priv->free_busy_data) {
		gint ii;
		GList *list = *priv->free_busy_data;

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

				list = g_list_append (list, comp);
			} else {
				icalcomponent_free (icalcomp);
			}
		}

		*priv->free_busy_data = list;
	}

	g_mutex_unlock (priv->free_busy_data_lock);
}

typedef struct
{
	ECal *ecal;
	gchar *message;
}  ECalErrorData;

static gboolean
backend_error_idle_cb (gpointer data)
{
	ECalErrorData *error_data = data;

	g_signal_emit (G_OBJECT (error_data->ecal), e_cal_signals[BACKEND_ERROR], 0, error_data->message);

	g_object_unref (error_data->ecal);
	g_free (error_data->message);
	g_free (error_data);

	return FALSE;
}

/* Handle the error_occurred signal from the listener */
static void
backend_error_cb (EGdbusCal *gdbus_cal,
                  const gchar *message,
                  ECal *ecal)
{
	ECalErrorData *error_data;

	g_return_if_fail (E_IS_CAL (ecal));

	error_data = g_new0 (ECalErrorData, 1);

	error_data->ecal = g_object_ref (ecal);
	error_data->message = g_strdup (message);

	g_idle_add (backend_error_idle_cb, error_data);
}

static void
readonly_cb (EGdbusCal *gdbus_cal,
             gboolean read_only,
             ECal *cal)
{
	ECalPrivate *priv;

	g_return_if_fail (cal && E_IS_CAL (cal));

	priv = cal->priv;
	priv->read_only = read_only;
}

static void
online_cb (EGdbusCal *gdbus_cal,
           gboolean is_online,
           ECal *cal)
{
	g_return_if_fail (E_IS_CAL (cal));

	g_signal_emit (
		G_OBJECT (cal), e_cal_signals[CAL_SET_MODE],
		0, E_CALENDAR_STATUS_OK, is_online ? Remote : Local);
}

/*
static void
backend_died_cb (EComponentListener *cl,
 *               gpointer user_data)
{
	ECalPrivate *priv;
	ECal *ecal = (ECal *) user_data;
 *
	priv = ecal->priv;
	priv->load_state = E_CAL_LOAD_NOT_LOADED;
	g_signal_emit (G_OBJECT (ecal), e_cal_signals[BACKEND_DIED], 0);
}*/

static void
set_local_attachment_store (ECal *ecal)
{
	gchar *cache_dir = NULL;
	GError *error = NULL;

	e_gdbus_cal_call_get_backend_property_sync (
		ecal->priv->gdbus_cal, CLIENT_BACKEND_PROPERTY_CACHE_DIR, &cache_dir, NULL, &error);

	if (error == NULL)
		ecal->priv->local_attachment_store = cache_dir;
	else {
		unwrap_gerror (&error);
		g_warning ("%s", error->message);
		g_error_free (error);
	}
}

/**
 * e_cal_new:
 * @source: An #ESource to be used for the client.
 * @type: Type of the client.
 *
 * Creates a new calendar client. This does not open the calendar itself,
 * for that, e_cal_open() or e_cal_open_async() needs to be called.
 *
 * Returns: A newly-created calendar client, or NULL if the client could
 * not be constructed because it could not contact the calendar server.
 *
 * Deprecated: 3.2: Use e_cal_client_new() instead.
 **/
ECal *
e_cal_new (ESource *source,
           ECalSourceType type)
{
	ECal *ecal;
	ECalPrivate *priv;
	gchar *path;
	gchar **strv;
	const gchar *uid;
	GError *error = NULL;
	GDBusConnection *connection;

	g_return_val_if_fail (source && E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (type < E_CAL_SOURCE_TYPE_LAST, NULL);

	if (!e_cal_activate (&error)) {
		unwrap_gerror (&error);
		g_warning ("Cannot activate ECal: %s\n", error ? error->message : "Unknown error");
		if (error)
			g_error_free (error);
		return NULL;
	}

	ecal = g_object_new (E_TYPE_CAL, NULL);
	priv = ecal->priv;

	priv->source = g_object_ref (source);
	priv->type = type;

	uid = e_source_get_uid (source);
	strv = e_gdbus_cal_factory_encode_get_cal (uid, convert_type (priv->type));

	if (!e_gdbus_cal_factory_call_get_cal_sync (G_DBUS_PROXY (cal_factory_proxy), (const gchar * const *) strv, &path, NULL, &error)) {
		g_strfreev (strv);
		unwrap_gerror (&error);
		g_warning ("Cannot get cal from factory: %s", error ? error->message : "Unknown error");
		if (error)
			g_error_free (error);
		g_object_unref (ecal);
		return NULL;
	}
	g_strfreev (strv);

	priv->gdbus_cal = G_DBUS_PROXY (
		e_gdbus_cal_proxy_new_sync (g_dbus_proxy_get_connection (G_DBUS_PROXY (cal_factory_proxy)),
		G_DBUS_PROXY_FLAGS_NONE,
		CALENDAR_DBUS_SERVICE_NAME,
		path,
		NULL,
		&error));

	if (!priv->gdbus_cal) {
		g_free (path);
		unwrap_gerror (&error);
		g_warning ("Cannot create cal proxy: %s", error ? error->message : "Unknown error");
		if (error)
			g_error_free (error);
		g_object_unref (ecal);
		return NULL;
	}

	connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (priv->gdbus_cal));
	priv->gone_signal_id = g_dbus_connection_signal_subscribe (
		connection,
		"org.freedesktop.DBus",			/* sender */
		"org.freedesktop.DBus",			/* interface */
		"NameOwnerChanged",			/* member */
		"/org/freedesktop/DBus",		/* object_path */
		CALENDAR_DBUS_SERVICE_NAME,		/* arg0 */
		G_DBUS_SIGNAL_FLAGS_NONE,
		gdbus_cal_connection_gone_cb, ecal, NULL);
	g_signal_connect (connection, "closed", G_CALLBACK (gdbus_cal_closed_cb), ecal);

	g_signal_connect (priv->gdbus_cal, "backend-error", G_CALLBACK (backend_error_cb), ecal);
	g_signal_connect (priv->gdbus_cal, "readonly", G_CALLBACK (readonly_cb), ecal);
	g_signal_connect (priv->gdbus_cal, "online", G_CALLBACK (online_cb), ecal);
	g_signal_connect (priv->gdbus_cal, "free-busy-data", G_CALLBACK (free_busy_data_cb), ecal);

	/* Set the local attachment store path for the calendar */
	set_local_attachment_store (ecal);

	g_free (path);

	return ecal;
}

static void
async_open_report_result (ECal *ecal,
                          const GError *error)
{
	ECalendarStatus status;

	g_return_if_fail (ecal && E_IS_CAL (ecal));

	if (!error)
		ecal->priv->load_state = E_CAL_LOAD_LOADED;

	if (error) {
		status = get_status_from_error (error);
	} else {
		status = E_CALENDAR_STATUS_OK;
	}

	g_signal_emit (G_OBJECT (ecal), e_cal_signals[CAL_OPENED], 0, status);
	g_signal_emit (G_OBJECT (ecal), e_cal_signals[CAL_OPENED_EX], 0, error);
}

static void
async_open_ready_cb (GDBusProxy *gdbus_cal,
                     GAsyncResult *res,
                     ECal *ecal)
{
	GError *error = NULL;

	g_return_if_fail (ecal && E_IS_CAL (ecal));

	e_gdbus_cal_call_open_finish (gdbus_cal, res, &error);

	unwrap_gerror (&error);

	async_open_report_result (ecal, error);

	if (error)
		g_error_free (error);
}

static gboolean
open_calendar (ECal *ecal,
               gboolean only_if_exists,
               GError **error,
               ECalendarStatus *status,
               gboolean async)
{
	ECalPrivate *priv;

	g_return_val_if_fail (error != NULL, FALSE);

	e_return_error_if_fail (ecal != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);

	if (priv->load_state == E_CAL_LOAD_LOADED)
		return TRUE;

	priv->load_state = E_CAL_LOAD_LOADING;

	*status = E_CALENDAR_STATUS_OK;
	if (!async) {
		if (!e_gdbus_cal_call_open_sync (priv->gdbus_cal, only_if_exists, NULL, error)) {
			*status = E_CALENDAR_STATUS_DBUS_EXCEPTION;
		}
		if (!*error)
			priv->load_state = E_CAL_LOAD_LOADED;
	} else {
		e_gdbus_cal_call_open (priv->gdbus_cal, only_if_exists, NULL, (GAsyncReadyCallback) async_open_ready_cb, ecal);
	}

	if (*error) {
		unwrap_gerror (error);
		priv->load_state = E_CAL_LOAD_NOT_LOADED;
	}

	return *error == NULL;
}

/**
 * e_cal_open:
 * @ecal: A calendar client.
 * @only_if_exists: FALSE if the calendar should be opened even if there
 * was no storage for it, i.e. to create a new calendar or load an existing
 * one if it already exists.  TRUE if it should only try to load calendars
 * that already exist.
 * @error: Placeholder for error information.
 *
 * Makes a calendar client initiate a request to open a calendar.  The calendar
 * client will emit the "cal_opened" signal when the response from the server is
 * received. Since 3.0 is emitted also "cal_opened_ex" signal, which contains
 * a GError pointer from the open operation (NULL when no error occurred).
 * New signal deprecates the old "cal_opened" signal.
 *
 * Returns: TRUE on success, FALSE on failure to issue the open request.
 *
 * Deprecated: 3.2: Use e_client_open_sync() on an #ECalClient object instead.
 **/
gboolean
e_cal_open (ECal *ecal,
            gboolean only_if_exists,
            GError **error)
{
	ECalendarStatus status;
	GError *err = NULL;
	gboolean result;

	result = open_calendar (ecal, only_if_exists, &err, &status, FALSE);
	g_signal_emit (G_OBJECT (ecal), e_cal_signals[CAL_OPENED], 0, status);
	g_signal_emit (G_OBJECT (ecal), e_cal_signals[CAL_OPENED_EX], 0, err);

	if (err)
		g_propagate_error (error, err);

	return result;
}

struct idle_async_error_reply_data
{
	ECal *ecal; /* ref-ed */
	GError *error; /* can be NULL */
};

static gboolean
idle_async_error_reply_cb (gpointer user_data)
{
	struct idle_async_error_reply_data *data = user_data;

	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (data->ecal != NULL, FALSE);

	async_open_report_result (data->ecal, data->error);

	g_object_unref (data->ecal);
	if (data->error)
		g_error_free (data->error);
	g_free (data);

	return FALSE;
}

/* takes ownership of error */
static void
async_report_idle (ECal *ecal,
                   GError *error)
{
	struct idle_async_error_reply_data *data;

	g_return_if_fail (ecal != NULL);

	data = g_new0 (struct idle_async_error_reply_data, 1);
	data->ecal = g_object_ref (ecal);
	data->error = error;

	g_idle_add (idle_async_error_reply_cb, data);
}

/**
 * e_cal_open_async:
 * @ecal: A calendar client.
 * @only_if_exists: If TRUE, then only open the calendar if it already
 * exists.  If FALSE, then create a new calendar if it doesn't already
 * exist.
 *
 * Open the calendar asynchronously.  The calendar will emit the
 * "cal_opened" signal when the operation has completed.
 * Since 3.0 is emitted also "cal_opened_ex" signal, which contains
 * a GError pointer from the open operation (NULL when no error occurred).
 * New signal deprecates the old "cal_opened" signal.
 *
 * Deprecated: 3.2: Use e_client_open()/e_client_open_finish()
 * on an #ECalClient object instead.
 **/
void
e_cal_open_async (ECal *ecal,
                  gboolean only_if_exists)
{
	ECalPrivate *priv;
	GError *error = NULL;
	ECalendarStatus status;

	g_return_if_fail (ecal != NULL);
	g_return_if_fail (E_IS_CAL (ecal));

	priv = ecal->priv;

	switch (priv->load_state) {
	case E_CAL_LOAD_LOADING :
		async_report_idle (ecal, g_error_new_literal (E_CALENDAR_ERROR, E_CALENDAR_STATUS_BUSY, e_cal_get_error_message (E_CALENDAR_STATUS_BUSY)));
		return;
	case E_CAL_LOAD_LOADED :
		async_report_idle (ecal, NULL /* success */);
		return;
	default:
		/* ignore everything else */
		break;
	}

	open_calendar (ecal, only_if_exists, &error, &status, TRUE);

	if (error)
		async_report_idle (ecal, error);
}

/**
 * e_cal_refresh:
 * @ecal: A calendar client.
 * @error: Placeholder for error information.
 *
 * Invokes refresh on a calendar. See @e_cal_get_refresh_supported.
 *
 * Returns: TRUE if calendar supports refresh and it was invoked, FALSE otherwise.
 *
 * Since: 2.30
 *
 * Deprecated: 3.2: Use e_cal_client_refresh_sync() instead.
 **/
gboolean
e_cal_refresh (ECal *ecal,
               GError **error)
{
	ECalPrivate *priv;

	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);

	if (!e_gdbus_cal_call_refresh_sync (priv->gdbus_cal, NULL, error)) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	return TRUE;
}

/**
 * e_cal_remove:
 * @ecal: A calendar client.
 * @error: Placeholder for error information.
 *
 * Removes a calendar.
 *
 * Returns: TRUE if the calendar was removed, FALSE if there was an error.
 *
 * Deprecated: 3.2: Use e_client_remove_sync() on an #ECalClient object instead.
 */
gboolean
e_cal_remove (ECal *ecal,
              GError **error)
{
	ECalPrivate *priv;

	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);

	if (!e_gdbus_cal_call_remove_sync (priv->gdbus_cal, NULL, error)) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	return TRUE;
}

#if 0
/* Builds an URI list out of a CORBA string sequence */
static GList *
build_uri_list (GNOME_Evolution_Calendar_StringSeq *seq)
{
	GList *uris = NULL;
	gint i;

	for (i = 0; i < seq->_length; i++)
		uris = g_list_prepend (uris, g_strdup (seq->_buffer[i]));

	return uris;
}
#endif

/**
 * e_cal_uri_list: (skip)
 * @ecal: A calendar client.
 * @mode: Mode of the URIs to get.
 *
 * Retrieves a list of all calendar clients for the given mode.
 *
 * Returns: list of uris.
 *
 * Deprecated: 3.2: This function has been dropped completely.
 */
GList *
e_cal_uri_list (ECal *ecal,
                CalMode mode)
{
#if 0
	ECalPrivate *priv;
	GNOME_Evolution_Calendar_StringSeq *uri_seq;
	GList *uris = NULL;
	CORBA_Environment ev;
	GList *f;

	g_return_val_if_fail (ecal != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL (ecal), NULL);

	priv = ecal->priv;

	for (f = priv->factories; f; f = f->next) {
		CORBA_exception_init (&ev);
		uri_seq = GNOME_Evolution_Calendar_CalFactory_uriList (f->data, mode, &ev);

		if (BONOBO_EX (&ev)) {
			g_message ("e_cal_uri_list(): request failed");

			/* free memory and return */
			g_list_foreach (uris, (GFunc) g_free, NULL);
			g_list_free (uris);
			uris = NULL;
			break;
		}
		else {
			uris = g_list_concat (uris, build_uri_list (uri_seq));
			CORBA_free (uri_seq);
		}

		CORBA_exception_free (&ev);
	}

	return uris;
#endif

	return NULL;
}

/**
 * e_cal_get_source_type:
 * @ecal: A calendar client.
 *
 * Gets the type of the calendar client.
 *
 * Returns: an #ECalSourceType value corresponding to the type
 * of the calendar client.
 *
 * Deprecated: 3.2: Use e_cal_client_get_source_type() instead.
 */
ECalSourceType
e_cal_get_source_type (ECal *ecal)
{
	ECalPrivate *priv;

	g_return_val_if_fail (ecal != NULL, E_CAL_SOURCE_TYPE_LAST);
	g_return_val_if_fail (E_IS_CAL (ecal), E_CAL_SOURCE_TYPE_LAST);

	priv = ecal->priv;

	return priv->type;
}

/**
 * e_cal_get_load_state:
 * @ecal: A calendar client.
 *
 * Queries the state of loading of a calendar client.
 *
 * Returns: A #ECalLoadState value indicating whether the client has
 * not been loaded with e_cal_open() yet, whether it is being
 * loaded, or whether it is already loaded.
 *
 * Deprecated: 3.2: Use e_client_is_opened() on an #ECalClient instead.
 **/
ECalLoadState
e_cal_get_load_state (ECal *ecal)
{
	ECalPrivate *priv;

	g_return_val_if_fail (ecal != NULL, E_CAL_LOAD_NOT_LOADED);
	g_return_val_if_fail (E_IS_CAL (ecal), E_CAL_LOAD_NOT_LOADED);

	priv = ecal->priv;
	return priv->load_state;
}

/**
 * e_cal_get_source: (skip)
 * @ecal: A calendar client.
 *
 * Queries the source that is open in a calendar client.
 *
 * Returns: The source of the calendar that is already loaded or is being
 * loaded, or NULL if the ecal has not started a load request yet.
 *
 * Deprecated: 3.2: Use e_client_get_source() on an #ECalClient object instead.
 **/
ESource *
e_cal_get_source (ECal *ecal)
{
	ECalPrivate *priv;

	g_return_val_if_fail (ecal != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL (ecal), NULL);

	priv = ecal->priv;
	return priv->source;
}

/**
 * e_cal_get_local_attachment_store:
 * @ecal: A calendar client.
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
 * Deprecated: 3.2: Use e_cal_client_get_local_attachment_store() instead.
 **/
const gchar *
e_cal_get_local_attachment_store (ECal *ecal)
{
	ECalPrivate *priv;

	g_return_val_if_fail (ecal != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL (ecal), NULL);

	priv = ecal->priv;
	return (const gchar *) priv->local_attachment_store;
}

/**
 * e_cal_is_read_only:
 * @ecal: A calendar client.
 * @read_only: Return value for read only status.
 * @error: Placeholder for error information.
 *
 * Queries whether the calendar client can perform modifications
 * on the calendar or not. Whether the backend is read only or not
 * is specified, on exit, in the @read_only argument.
 *
 * Returns: TRUE if the call was successful, FALSE if there was an error.
 *
 * Deprecated: 3.2: Use e_cal_client_is_readonly() on an #ECalClient object instead.
 */
gboolean
e_cal_is_read_only (ECal *ecal,
                    gboolean *read_only,
                    GError **error)
{
	ECalPrivate *priv;

	if (!(ecal && E_IS_CAL (ecal)))
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_INVALID_ARG, error);

	priv = ecal->priv;
	*read_only = priv->read_only;

	return TRUE;
}

/**
 * e_cal_get_cal_address:
 * @ecal: A calendar client.
 * @cal_address: Return value for address information.
 * @error: Placeholder for error information.
 *
 * Queries the calendar address associated with a calendar client.
 *
 * Returns: TRUE if the operation was successful, FALSE if there
 * was an error.
 *
 * Deprecated: 3.2: Use e_client_get_backend_property_sync()
 * with #CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS instead.
 **/
gboolean
e_cal_get_cal_address (ECal *ecal,
                       gchar **cal_address,
                       GError **error)
{
	ECalPrivate *priv;

	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (cal_address != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);
	*cal_address = NULL;

	LOCK_CACHE ();
	if (priv->cal_address == NULL) {
		e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);
		if (priv->load_state != E_CAL_LOAD_LOADED) {
			UNLOCK_CACHE ();
			E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
		}

		if (!e_gdbus_cal_call_get_backend_property_sync (priv->gdbus_cal, CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS, &priv->cal_address, NULL, error)) {
			UNLOCK_CACHE ();
			E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
		}
	}

	*cal_address = g_strdup (priv->cal_address);
	UNLOCK_CACHE ();

	return TRUE;
}

/**
 * e_cal_get_alarm_email_address:
 * @ecal: A calendar client.
 * @alarm_address: Return value for alarm address.
 * @error: Placeholder for error information.
 *
 * Queries the address to be used for alarms in a calendar client.
 *
 * Returns: TRUE if the operation was successful, FALSE if there was
 * an error while contacting the backend.
 *
 * Deprecated: 3.2: Use e_client_get_backend_property_sync()
 * with #CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS instead.
 */
gboolean
e_cal_get_alarm_email_address (ECal *ecal,
                               gchar **alarm_address,
                               GError **error)
{
	ECalPrivate *priv;

	e_return_error_if_fail (alarm_address != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);
	*alarm_address = NULL;

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (!e_gdbus_cal_call_get_backend_property_sync (priv->gdbus_cal, CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS, alarm_address, NULL, error)) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	return TRUE;
}

/**
 * e_cal_get_ldap_attribute:
 * @ecal: A calendar client.
 * @ldap_attribute: Return value for the LDAP attribute.
 * @error: Placeholder for error information.
 *
 * Queries the LDAP attribute for a calendar client.
 *
 * Returns: TRUE if the call was successful, FALSE if there was an
 * error contacting the backend.
 *
 * Deprecated: 3.2: This function has been dropped completely.
 */
gboolean
e_cal_get_ldap_attribute (ECal *ecal,
                          gchar **ldap_attribute,
                          GError **error)
{
	ECalPrivate *priv;

	e_return_error_if_fail (ldap_attribute != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);
	*ldap_attribute = NULL;

	E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_NOT_SUPPORTED, error);
}

static gboolean
load_capabilities (ECal *ecal,
                   GError **error)
{
	ECalPrivate *priv;

	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	LOCK_CACHE ();

	if (priv->capabilities) {
		UNLOCK_CACHE ();
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);
	}

	if (!e_gdbus_cal_call_get_backend_property_sync (priv->gdbus_cal, CLIENT_BACKEND_PROPERTY_CAPABILITIES, &priv->capabilities, NULL, error)) {
		UNLOCK_CACHE ();
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	UNLOCK_CACHE ();

	return TRUE;
}

static gboolean
check_capability (ECal *ecal,
                  const gchar *cap)
{
	ECalPrivate *priv;

	priv = ecal->priv;

	/* FIXME Check result */
	load_capabilities (ecal, NULL);
	if (priv->capabilities && strstr (priv->capabilities, cap))
		return TRUE;

	return FALSE;
}

/**
 * e_cal_get_one_alarm_only:
 * @ecal: A calendar client.
 *
 * Checks if a calendar supports only one alarm per component.
 *
 * Returns: TRUE if the calendar allows only one alarm, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_check_one_alarm_only() instead.
 */
gboolean
e_cal_get_one_alarm_only (ECal *ecal)
{
	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (ecal && E_IS_CAL (ecal), FALSE);

	return check_capability (ecal, CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY);
}

/**
 * e_cal_get_organizer_must_attend:
 * @ecal: A calendar client.
 *
 * Checks if a calendar forces organizers of meetings to be also attendees.
 *
 * Returns: TRUE if the calendar forces organizers to attend meetings,
 * FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_check_organizer_must_attend() instead.
 */
gboolean
e_cal_get_organizer_must_attend (ECal *ecal)
{
	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	return check_capability (ecal, CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ATTEND);
}

/**
 * e_cal_get_recurrences_no_master:
 * @ecal: A calendar client.
 *
 * Checks if the calendar has a master object for recurrences.
 *
 * Returns: TRUE if the calendar has a master object for recurrences,
 * FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_check_recurrences_no_master() instead.
 */
gboolean
e_cal_get_recurrences_no_master (ECal *ecal)
{
	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	return check_capability (ecal, CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER);
}

/**
 * e_cal_get_static_capability:
 * @ecal: A calendar client.
 * @cap: Name of the static capability to check.
 *
 * Queries the calendar for static capabilities.
 *
 * Returns: TRUE if the capability is supported, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_client_check_capability() on an #ECalClient object instead.
 */
gboolean
e_cal_get_static_capability (ECal *ecal,
                             const gchar *cap)
{
	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	return check_capability (ecal, cap);
}

/**
 * e_cal_get_save_schedules:
 * @ecal: A calendar client.
 *
 * Checks whether the calendar saves schedules.
 *
 * Returns: TRUE if it saves schedules, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_check_save_schedules() instead.
 */
gboolean
e_cal_get_save_schedules (ECal *ecal)
{
	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	return check_capability (ecal, CAL_STATIC_CAPABILITY_SAVE_SCHEDULES);
}

/**
 * e_cal_get_organizer_must_accept:
 * @ecal: A calendar client.
 *
 * Checks whether a calendar requires organizer to accept their attendance to
 * meetings.
 *
 * Returns: TRUE if the calendar requires organizers to accept, FALSE
 * otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_check_organizer_must_accept() instead.
 */
gboolean
e_cal_get_organizer_must_accept (ECal *ecal)
{
	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	return check_capability (ecal, CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ACCEPT);
}

/**
 * e_cal_get_refresh_supported:
 * @ecal: A calendar client.
 *
 * Checks whether a calendar supports explicit refreshing (see @e_cal_refresh).
 *
 * Returns: TRUE if the calendar supports refreshing, FALSE otherwise.
 *
 * Since: 2.30
 *
 * Deprecated: 3.2: Use e_client_check_refresh_supported() instead.
 */
gboolean
e_cal_get_refresh_supported (ECal *ecal)
{
	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	return check_capability (ecal, CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED);
}

/**
 * e_cal_set_mode: (skip)
 * @ecal: A calendar client.
 * @mode: Mode to switch to.
 *
 * Switches online/offline mode on the calendar.
 *
 * Returns: TRUE if the switch was successful, FALSE if there was an error.
 *
 * Deprecated: 3.2: This function has been dropped completely.
 */
gboolean
e_cal_set_mode (ECal *ecal,
                CalMode mode)
{
	ECalPrivate *priv;

	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);
	g_return_val_if_fail (mode & CAL_MODE_ANY, FALSE);

	priv = ecal->priv;
	g_return_val_if_fail (priv->gdbus_cal, FALSE);
	g_return_val_if_fail (priv->load_state == E_CAL_LOAD_LOADED, FALSE);

	g_debug ("%s: This function is not supported since 3.2", G_STRFUNC);

	return FALSE;
}

/* This is used in the callback which fetches all the timezones needed for an
 * object. */
typedef struct _ECalGetTimezonesData ECalGetTimezonesData;
struct _ECalGetTimezonesData {
	ECal *ecal;

	/* This starts out at E_CALENDAR_STATUS_OK. If an error occurs this
	 * contains the last error. */
	ECalendarStatus status;
};

/**
 * e_cal_get_default_object: (skip)
 * @ecal: A calendar client.
 * @icalcomp: Return value for the default object.
 * @error: Placeholder for error information.
 *
 * Retrives an #icalcomponent from the backend that contains the default
 * values for properties needed.
 *
 * Returns: TRUE if the call was successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_get_default_object_sync() instead.
 */
gboolean
e_cal_get_default_object (ECal *ecal,
                          icalcomponent **icalcomp,
                          GError **error)
{
	ECalPrivate *priv;
	ECalendarStatus status;
	gchar *object = NULL;

	e_return_error_if_fail (icalcomp != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);
	*icalcomp = NULL;

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (!e_gdbus_cal_call_get_backend_property_sync (priv->gdbus_cal, CAL_BACKEND_PROPERTY_DEFAULT_OBJECT, &object, NULL, error)) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	if (object) {
		*icalcomp = icalparser_parse_string (object);
		g_free (object);

		if (!(*icalcomp))
			status = E_CALENDAR_STATUS_INVALID_OBJECT;
		else
			status = E_CALENDAR_STATUS_OK;

		E_CALENDAR_CHECK_STATUS (status, error);
	} else
		status = E_CALENDAR_STATUS_OTHER_ERROR;

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_attachments_for_comp: (skip)
 * @ecal: A calendar client.
 * @uid: Unique identifier for a calendar component.
 * @rid: Recurrence identifier.
 * @list: Return the list of attachment uris.
 * @error: Placeholder for error information.
 *
 * Queries a calendar for a calendar component object based on its unique
 * identifier and gets the attachments for the component.
 *
 * Returns: TRUE if the call was successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_get_attachment_uris_sync() instead.
 **/
gboolean
e_cal_get_attachments_for_comp (ECal *ecal,
                                const gchar *uid,
                                const gchar *rid,
                                GSList **list,
                                GError **error)
{
	ECalPrivate *priv;
	ECalendarStatus status;
	gchar **list_array;
	gchar **strv;

	e_return_error_if_fail (uid != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (list != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);
	*list = NULL;

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	strv = e_gdbus_cal_encode_get_attachment_uris (uid, rid);
	if (!e_gdbus_cal_call_get_attachment_uris_sync (priv->gdbus_cal, (const gchar * const *) strv, &list_array, NULL, error)) {
		g_strfreev (strv);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	g_strfreev (strv);

	if (list_array) {
		gchar **string;
		for (string = list_array; *string; string++) {
			*list = g_slist_append (*list, g_strdup (*string));
		}
		g_strfreev (list_array);
		status = E_CALENDAR_STATUS_OK;
	} else
		status = E_CALENDAR_STATUS_OTHER_ERROR;

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_object: (skip)
 * @ecal: A calendar client.
 * @uid: Unique identifier for a calendar component.
 * @rid: Recurrence identifier.
 * @icalcomp: Return value for the calendar component object.
 * @error: Placeholder for error information.
 *
 * Queries a calendar for a calendar component object based on its unique
 * identifier.
 *
 * Returns: TRUE if the call was successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_get_object_sync() instead.
 **/
gboolean
e_cal_get_object (ECal *ecal,
                  const gchar *uid,
                  const gchar *rid,
                  icalcomponent **icalcomp,
                  GError **error)
{
	ECalPrivate *priv;
	ECalendarStatus status;
	gchar *object = NULL, **strv;
	icalcomponent *tmp_icalcomp;
	icalcomponent_kind kind;

	e_return_error_if_fail (uid != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (icalcomp != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);
	*icalcomp = NULL;

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	strv = e_gdbus_cal_encode_get_object (uid, rid);
	if (!e_gdbus_cal_call_get_object_sync (priv->gdbus_cal, (const gchar * const *) strv, &object, NULL, error)) {
		g_strfreev (strv);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	g_strfreev (strv);

	status = E_CALENDAR_STATUS_OK;
	tmp_icalcomp = icalparser_parse_string (object);
	if (!tmp_icalcomp) {
		status = E_CALENDAR_STATUS_INVALID_OBJECT;
		*icalcomp = NULL;
	} else {
		kind = icalcomponent_isa (tmp_icalcomp);
		if ((kind == ICAL_VEVENT_COMPONENT && priv->type == E_CAL_SOURCE_TYPE_EVENT) ||
		    (kind == ICAL_VTODO_COMPONENT && priv->type == E_CAL_SOURCE_TYPE_TODO) ||
		    (kind == ICAL_VJOURNAL_COMPONENT && priv->type == E_CAL_SOURCE_TYPE_JOURNAL)) {
			*icalcomp = icalcomponent_new_clone (tmp_icalcomp);
		} else if (kind == ICAL_VCALENDAR_COMPONENT) {
			icalcomponent *subcomp = NULL;

			switch (priv->type) {
			case E_CAL_SOURCE_TYPE_EVENT :
				subcomp = icalcomponent_get_first_component (tmp_icalcomp, ICAL_VEVENT_COMPONENT);
				break;
			case E_CAL_SOURCE_TYPE_TODO :
				subcomp = icalcomponent_get_first_component (tmp_icalcomp, ICAL_VTODO_COMPONENT);
				break;
			case E_CAL_SOURCE_TYPE_JOURNAL :
				subcomp = icalcomponent_get_first_component (tmp_icalcomp, ICAL_VJOURNAL_COMPONENT);
				break;
			default:
				/* ignore everything else */
				break;
			}

			/* we are only interested in the first component */
			if (subcomp)
				*icalcomp = icalcomponent_new_clone (subcomp);
		}

		icalcomponent_free (tmp_icalcomp);
	}

	g_free (object);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_objects_for_uid: (skip)
 * @ecal: A calendar client.
 * @uid: Unique identifier for a calendar component.
 * @objects: Return value for the list of objects obtained from the backend.
 * @error: Placeholder for error information.
 *
 * Queries a calendar for all calendar components with the given unique
 * ID. This will return any recurring event and all its detached recurrences.
 * For non-recurring events, it will just return the object with that ID.
 *
 * Returns: TRUE if the call was successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_get_objects_for_uid_sync() instead.
 **/
gboolean
e_cal_get_objects_for_uid (ECal *ecal,
                           const gchar *uid,
                           GList **objects,
                           GError **error)
{
	ECalPrivate *priv;
	ECalendarStatus status;
	gchar *object = NULL, **strv;

	e_return_error_if_fail (uid != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (objects != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);
	*objects = NULL;

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	strv = e_gdbus_cal_encode_get_object (uid, "");
	if (!e_gdbus_cal_call_get_object_sync (priv->gdbus_cal, (const gchar * const *) strv, &object, NULL, error)) {
		g_strfreev (strv);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	g_strfreev (strv);

	status = E_CALENDAR_STATUS_OK;
	{
		icalcomponent *icalcomp;
		icalcomponent_kind kind;

		icalcomp = icalparser_parse_string (object);
		if (!icalcomp) {
			status = E_CALENDAR_STATUS_INVALID_OBJECT;
			*objects = NULL;
		} else {
			ECalComponent *comp;

			kind = icalcomponent_isa (icalcomp);
			if ((kind == ICAL_VEVENT_COMPONENT && priv->type == E_CAL_SOURCE_TYPE_EVENT) ||
			    (kind == ICAL_VTODO_COMPONENT && priv->type == E_CAL_SOURCE_TYPE_TODO) ||
			    (kind == ICAL_VJOURNAL_COMPONENT && priv->type == E_CAL_SOURCE_TYPE_JOURNAL)) {
				comp = e_cal_component_new ();
				e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp));
				*objects = g_list_append (NULL, comp);
			} else if (kind == ICAL_VCALENDAR_COMPONENT) {
				icalcomponent *subcomp;
				icalcomponent_kind kind_to_find;

				switch (priv->type) {
				case E_CAL_SOURCE_TYPE_TODO :
					kind_to_find = ICAL_VTODO_COMPONENT;
					break;
				case E_CAL_SOURCE_TYPE_JOURNAL :
					kind_to_find = ICAL_VJOURNAL_COMPONENT;
					break;
				case E_CAL_SOURCE_TYPE_EVENT :
				default:
					kind_to_find = ICAL_VEVENT_COMPONENT;
					break;
				}

				*objects = NULL;
				subcomp = icalcomponent_get_first_component (icalcomp, kind_to_find);
				while (subcomp) {
					comp = e_cal_component_new ();
					e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (subcomp));
					*objects = g_list_append (*objects, comp);
					subcomp = icalcomponent_get_next_component (icalcomp, kind_to_find);
				}
			}

			icalcomponent_free (icalcomp);
		}
	}
	g_free (object);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_resolve_tzid_cb: (skip)
 * @tzid: ID of the timezone to resolve.
 * @data: Closure data for the callback.
 *
 * Resolves TZIDs for the recurrence generator.
 *
 * Returns: The timezone identified by the @tzid argument, or %NULL if
 * it could not be found.
 *
 * Deprecated: 3.2: Use e_cal_client_resolve_tzid_cb() instead.
 */
icaltimezone *
e_cal_resolve_tzid_cb (const gchar *tzid,
                       gpointer data)
{
	ECal *ecal;
	icaltimezone *zone = NULL;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL (data), NULL);

	ecal = E_CAL (data);

	/* FIXME: Handle errors. */
	e_cal_get_timezone (ecal, tzid, &zone, NULL);

	return zone;
}

/**
 * e_cal_get_changes: (skip)
 * @ecal: A calendar client.
 * @change_id: ID to use for comparing changes.
 * @changes: Return value for the list of changes.
 * @error: Placeholder for error information.
 *
 * Returns a list of changes made to the calendar since a specific time. That time
 * is identified by the @change_id argument, which is used by the backend to
 * compute the changes done.
 *
 * Returns: %TRUE if the call was successful, %FALSE otherwise.
 *
 * Deprecated: 3.2: This function has been dropped completely.
 */
gboolean
e_cal_get_changes (ECal *ecal,
                   const gchar *change_id,
                   GList **changes,
                   GError **error)
{
	ECalPrivate *priv;

	e_return_error_if_fail (changes != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (change_id != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);
	*changes = NULL;

	E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_NOT_SUPPORTED, error);
}

/**
 * e_cal_free_change_list: (skip)
 * @list: List of changes to be freed.
 *
 * Free a list of changes as returned by e_cal_get_changes().
 *
 * Deprecated: 3.2: Use () instead.
 */
void
e_cal_free_change_list (GList *list)
{
	ECalChange *c;
	GList *l;

	for (l = list; l; l = l->next) {
		c = l->data;

		if (c != NULL && c->comp != NULL) {
			g_object_unref (G_OBJECT (c->comp));
			g_free (c);
		} else
			g_warn_if_reached ();
	}

	g_list_free (list);
}

/**
 * e_cal_get_object_list:
 * @ecal: A calendar client.
 * @query: Query string.
 * @objects: (out) (element-type long): Return value for list of objects.
 * @error: Placeholder for error information.
 *
 * Gets a list of objects from the calendar that match the query specified
 * by the @query argument. The objects will be returned in the @objects
 * argument, which is a list of #icalcomponent. When done, this list
 * should be freed by using the e_cal_free_object_list() function.
 *
 * Returns: TRUE if the operation was successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_get_object_list_sync() instead.
 **/
gboolean
e_cal_get_object_list (ECal *ecal,
                       const gchar *query,
                       GList **objects,
                       GError **error)
{
	ECalPrivate *priv;
	gchar **object_array = NULL, *gdbus_query = NULL;

	e_return_error_if_fail (objects != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (query, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);
	*objects = NULL;

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (!e_gdbus_cal_call_get_object_list_sync (priv->gdbus_cal, e_util_ensure_gdbus_string (query, &gdbus_query), &object_array, NULL, error)) {
		g_free (gdbus_query);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	g_free (gdbus_query);

	if (object_array) {
		icalcomponent *comp;
		gchar **object;
		for (object = object_array; *object; object++) {
			comp = icalcomponent_new_from_string (*object);
			if (!comp) continue;
			*objects = g_list_prepend (*objects, comp);
		}

		g_strfreev (object_array);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);
	}
	else
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OTHER_ERROR, error);
}

/**
 * e_cal_get_object_list_as_comp: (skip)
 * @ecal: A calendar client.
 * @query: Query string.
 * @objects: Return value for list of objects.
 * @error: Placeholder for error information.
 *
 * Gets a list of objects from the calendar that match the query specified
 * by the @query argument. The objects will be returned in the @objects
 * argument, which is a list of #ECalComponent.
 *
 * Returns: TRUE if the operation was successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_get_object_list_as_comps_sync() instead.
 */
gboolean
e_cal_get_object_list_as_comp (ECal *ecal,
                               const gchar *query,
                               GList **objects,
                               GError **error)
{
	GList *ical_objects = NULL;
	GList *l;

	e_return_error_if_fail (objects != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	*objects = NULL;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (query, E_CALENDAR_STATUS_INVALID_ARG);

	if (!e_cal_get_object_list (ecal, query, &ical_objects, error))
		return FALSE;

	for (l = ical_objects; l; l = l->next) {
		ECalComponent *comp;

		comp = e_cal_component_new ();
		e_cal_component_set_icalcomponent (comp, l->data);
		*objects = g_list_prepend (*objects, comp);
	}

	g_list_free (ical_objects);

	return TRUE;
}

/**
 * e_cal_free_object_list: (skip)
 * @objects: List of objects to be freed.
 *
 * Frees a list of objects as returned by e_cal_get_object_list().
 *
 * Deprecated: 3.2: Use e_cal_client_free_icalcomp_slist() instead.
 */
void
e_cal_free_object_list (GList *objects)
{
	GList *l;

	for (l = objects; l; l = l->next)
		icalcomponent_free (l->data);

	g_list_free (objects);
}

/**
 * e_cal_get_free_busy: (skip)
 * @ecal: A calendar client.
 * @users: List of users to retrieve free/busy information for.
 * @start: Start time for query.
 * @end: End time for query.
 * @freebusy: Return value for VFREEBUSY objects.
 * @error: Placeholder for error information.
 *
 * Gets free/busy information from the calendar server.
 *
 * Returns: TRUE if the operation was successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_get_free_busy_sync() instead.
 */
gboolean
e_cal_get_free_busy (ECal *ecal,
                     GList *users,
                     time_t start,
                     time_t end,
                     GList **freebusy,
                     GError **error)
{
	ECalPrivate *priv;
	gchar **strv;
	GSList *susers;
	GList *l;

	e_return_error_if_fail (users != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (freebusy != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);
	*freebusy = NULL;

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	susers = NULL;
	for (l = users; l; l = l->next) {
		susers = g_slist_prepend (susers, l->data);
	}
	susers = g_slist_reverse (susers);
	strv = e_gdbus_cal_encode_get_free_busy (start, end, susers);
	g_slist_free (susers);

	g_mutex_lock (priv->free_busy_data_lock);
	priv->free_busy_data = freebusy;
	g_mutex_unlock (priv->free_busy_data_lock);

	if (!e_gdbus_cal_call_get_free_busy_sync (priv->gdbus_cal, (const gchar * const *) strv, NULL, error)) {
		g_strfreev (strv);
		g_mutex_lock (priv->free_busy_data_lock);
		priv->free_busy_data = NULL;
		g_mutex_unlock (priv->free_busy_data_lock);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}
	g_strfreev (strv);

	g_mutex_lock (priv->free_busy_data_lock);
	priv->free_busy_data = NULL;
	g_mutex_unlock (priv->free_busy_data_lock);

	E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);
}

struct comp_instance {
	ECalComponent *comp;
	time_t start;
	time_t end;
};

struct instances_info {
	GList **instances;
	icaltimezone *start_zone;
};

/* Called from cal_recur_generate_instances(); adds an instance to the list */
static gboolean
add_instance (ECalComponent *comp,
              time_t start,
              time_t end,
              gpointer data)
{
	GList **list;
	struct comp_instance *ci;
	struct icaltimetype itt;
	icalcomponent *icalcomp;
	struct instances_info *instances_hold;

	instances_hold = data;
	list = instances_hold->instances;

	ci = g_new (struct comp_instance, 1);

	icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp));

	/* add the instance to the list */
	ci->comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (ci->comp, icalcomp);

	/* set the RECUR-ID for the instance */
	if (e_cal_util_component_has_recurrences (icalcomp)) {
		if (!(icalcomponent_get_first_property (icalcomp, ICAL_RECURRENCEID_PROPERTY))) {
			ECalComponentRange *range;
			ECalComponentDateTime datetime;

			datetime.value = NULL;
			datetime.tzid = NULL;

			e_cal_component_get_dtstart (comp, &datetime);

			if (instances_hold->start_zone)
				itt = icaltime_from_timet_with_zone (start, datetime.value && datetime.value->is_date, instances_hold->start_zone);
			else {
				itt = icaltime_from_timet (start, datetime.value && datetime.value->is_date);

				if (datetime.tzid) {
					g_free ((gchar *) datetime.tzid);
					datetime.tzid = NULL;
				}
			}

			g_free (datetime.value);
			datetime.value = &itt;

			range = g_new0 (ECalComponentRange, 1);
			range->type = E_CAL_COMPONENT_RANGE_SINGLE;
			range->datetime = datetime;

			e_cal_component_set_recurid (ci->comp, range);

			if (datetime.tzid)
				g_free ((gchar *) datetime.tzid);
			g_free (range);
		}
	}

	ci->start = start;
	ci->end = end;

	*list = g_list_prepend (*list, ci);

	return TRUE;
}

/* Used from g_list_sort(); compares two struct comp_instance structures */
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

static GList *
process_detached_instances (GList *instances,
                            GList *detached_instances)
{
	struct comp_instance *ci, *cid;
	GList *dl, *unprocessed_instances = NULL;

	for (dl = detached_instances; dl != NULL; dl = dl->next) {
		GList *il;
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
			unprocessed_instances = g_list_prepend (unprocessed_instances, cid);
	}

	/* add the unprocessed instances (ie, detached instances with no master object */
	while (unprocessed_instances != NULL) {
		cid = unprocessed_instances->data;
		ci = g_new0 (struct comp_instance, 1);
		ci->comp = g_object_ref (cid->comp);
		ci->start = cid->start;
		ci->end = cid->end;
		instances = g_list_append (instances, ci);

		unprocessed_instances = g_list_remove (unprocessed_instances, cid);
	}

	return instances;
}

static void
generate_instances (ECal *ecal,
                    time_t start,
                    time_t end,
                    const gchar *uid,
                    ECalRecurInstanceFn cb,
                    gpointer cb_data)
{
	GList *objects = NULL;
	GList *instances, *detached_instances = NULL;
	GList *l;
	gchar *query;
	gchar *iso_start, *iso_end;
	ECalPrivate *priv;

	priv = ecal->priv;

	/* Generate objects */
	if (uid && *uid) {
		GError *error = NULL;
		gint tries = 0;

try_again:
		if (!e_cal_get_objects_for_uid (ecal, uid, &objects, &error)) {
			if (error->code == E_CALENDAR_STATUS_BUSY && tries >= 10) {
				tries++;
				g_usleep (500);
				g_clear_error (&error);

				goto try_again;
			}

			unwrap_gerror (&error);
			g_message ("Failed to get recurrence objects for uid %s \n", error ? error->message : "Unknown error");
			g_clear_error (&error);
			return;
		}
	}
	else {
		iso_start = isodate_from_time_t (start);
		if (!iso_start)
			return;

		iso_end = isodate_from_time_t (end);
		if (!iso_end) {
			g_free (iso_start);
			return;
		}

		query = g_strdup_printf (
			"(occur-in-time-range? (make-time \"%s\") (make-time \"%s\"))",
			iso_start, iso_end);
		g_free (iso_start);
		g_free (iso_end);
		if (!e_cal_get_object_list_as_comp (ecal, query, &objects, NULL)) {
			g_free (query);
			return;
		}
		g_free (query);
	}

	instances = NULL;

	for (l = objects; l; l = l->next) {
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
			ci->comp = comp;

			e_cal_component_get_dtstart (comp, &dtstart);
			e_cal_component_get_dtend (comp, &dtend);

			/* For DATE-TIME values with a TZID, we use
			 * e_cal_resolve_tzid_cb to resolve the TZID.
			 * For DATE values and DATE-TIME values without a
			 * TZID (i.e. floating times) we use the default
			 * timezone. */
			if (dtstart.tzid && dtstart.value && !dtstart.value->is_date) {
				start_zone = e_cal_resolve_tzid_cb (dtstart.tzid, ecal);
				if (!start_zone)
					start_zone = default_zone;
			} else {
				start_zone = default_zone;
			}

			if (dtend.tzid && dtend.value && !dtend.value->is_date) {
				end_zone = e_cal_resolve_tzid_cb (dtend.tzid, ecal);
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
				detached_instances = g_list_prepend (detached_instances, ci);
			} else {
				/* it doesn't fit to our time range, thus skip it */
				g_object_unref (G_OBJECT (ci->comp));
				g_free (ci);
			}
		} else {
			ECalComponentDateTime datetime;
			icaltimezone *start_zone = NULL;
			struct instances_info *instances_hold;

			/* Get the start timezone */
			e_cal_component_get_dtstart (comp, &datetime);
			if (datetime.tzid)
				e_cal_get_timezone (ecal, datetime.tzid, &start_zone, NULL);
			else
				start_zone = NULL;
			e_cal_component_free_datetime (&datetime);

			instances_hold = g_new0 (struct instances_info, 1);
			instances_hold->instances = &instances;
			instances_hold->start_zone = start_zone;

			e_cal_recur_generate_instances (
				comp, start, end, add_instance, instances_hold,
				e_cal_resolve_tzid_cb, ecal,
				default_zone);

			g_free (instances_hold);
			g_object_unref (comp);
		}
	}

	g_list_free (objects);

	/* Generate instances and spew them out */

	instances = g_list_sort (instances, compare_comp_instance);
	instances = process_detached_instances (instances, detached_instances);

	for (l = instances; l; l = l->next) {
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

	g_list_free (instances);

	for (l = detached_instances; l; l = l->next) {
		struct comp_instance *ci;

		ci = l->data;
		g_object_unref (G_OBJECT (ci->comp));
		g_free (ci);
	}

	g_list_free (detached_instances);

}

/**
 * e_cal_generate_instances: (skip)
 * @ecal: A calendar client.
 * @start: Start time for query.
 * @end: End time for query.
 * @cb: Callback for each generated instance.
 * @cb_data: Closure data for the callback.
 *
 * Does a combination of e_cal_get_object_list() and
 * e_cal_recur_generate_instances().
 *
 * The callback function should do a g_object_ref() of the calendar component
 * it gets passed if it intends to keep it around, since it will be unref'ed
 * as soon as the callback returns.
 *
 * Deprecated: 3.2: Use e_cal_client_generate_instances() instead.
 **/
void
e_cal_generate_instances (ECal *ecal,
                          time_t start,
                          time_t end,
                          ECalRecurInstanceFn cb,
                          gpointer cb_data)
{
	ECalPrivate *priv;

	g_return_if_fail (ecal != NULL);
	g_return_if_fail (E_IS_CAL (ecal));

	priv = ecal->priv;
	g_return_if_fail (priv->load_state == E_CAL_LOAD_LOADED);

	g_return_if_fail (start >= 0);
	g_return_if_fail (end >= 0);
	g_return_if_fail (cb != NULL);

	generate_instances (ecal, start, end, NULL, cb, cb_data);
}

/**
 * e_cal_generate_instances_for_object: (skip)
 * @ecal: A calendar client.
 * @icalcomp: Object to generate instances from.
 * @start: Start time for query.
 * @end: End time for query.
 * @cb: Callback for each generated instance.
 * @cb_data: Closure data for the callback.
 *
 * Does a combination of e_cal_get_object_list() and
 * e_cal_recur_generate_instances(), like e_cal_generate_instances(), but
 * for a single object.
 *
 * The callback function should do a g_object_ref() of the calendar component
 * it gets passed if it intends to keep it around, since it will be unref'ed
 * as soon as the callback returns.
 *
 * Deprecated: 3.2: Use e_cal_client_generate_instances_for_object() instead.
 **/
void
e_cal_generate_instances_for_object (ECal *ecal,
                                     icalcomponent *icalcomp,
                                     time_t start,
                                     time_t end,
                                     ECalRecurInstanceFn cb,
                                     gpointer cb_data)
{
	ECalComponent *comp;
	const gchar *uid;
	gchar *rid;
	gboolean result;
	GList *instances = NULL;
	ECalComponentDateTime datetime;
	icaltimezone *start_zone = NULL;
	struct instances_info *instances_hold;
	gboolean is_single_instance = FALSE;

	g_return_if_fail (E_IS_CAL (ecal));
	g_return_if_fail (start >= 0);
	g_return_if_fail (end >= 0);
	g_return_if_fail (cb != NULL);

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp));

	if (!e_cal_component_has_recurrences (comp))
		is_single_instance = TRUE;

	/*If the backend stores it as individual instances and does not
	 * have a master object - do not expand*/
	if (is_single_instance || e_cal_get_static_capability (ecal, CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER)) {

		/*return the same instance */
		result = (* cb)  (comp, icaltime_as_timet_with_zone (icalcomponent_get_dtstart (icalcomp), ecal->priv->default_zone),
				icaltime_as_timet_with_zone (icalcomponent_get_dtend (icalcomp), ecal->priv->default_zone), cb_data);
		g_object_unref (comp);
		return;
	}

	e_cal_component_get_uid (comp, &uid);
	rid = e_cal_component_get_recurid_as_string (comp);

	/* Get the start timezone */
	e_cal_component_get_dtstart (comp, &datetime);
	if (datetime.tzid)
		e_cal_get_timezone (ecal, datetime.tzid, &start_zone, NULL);
	else
		start_zone = NULL;
	e_cal_component_free_datetime (&datetime);

	instances_hold = g_new0 (struct instances_info, 1);
	instances_hold->instances = &instances;
	instances_hold->start_zone = start_zone;

	/* generate all instances in the given time range */
	generate_instances (ecal, start, end, uid, add_instance, instances_hold);

	instances = *(instances_hold->instances);
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
		instances = g_list_remove (instances, ci);
		g_object_unref (ci->comp);
		g_free (ci);
		g_free (instance_rid);
	}

	/* clean up */
	g_object_unref (comp);
	g_free (instances_hold);
	g_free (rid);
}

/* Builds a list of ECalComponentAlarms structures */
static GSList *
build_component_alarms_list (ECal *ecal,
                             GList *object_list,
                             time_t start,
                             time_t end)
{
	GSList *comp_alarms;
	GList *l;

	comp_alarms = NULL;

	for (l = object_list; l != NULL; l = l->next) {
		ECalComponent *comp;
		ECalComponentAlarms *alarms;
		ECalComponentAlarmAction omit[] = {-1};

		comp = e_cal_component_new ();
		if (!e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (l->data))) {
			g_object_unref (G_OBJECT (comp));
			continue;
		}

		alarms = e_cal_util_generate_alarms_for_comp (
			comp, start, end, omit, e_cal_resolve_tzid_cb,
			ecal, ecal->priv->default_zone);
		if (alarms)
			comp_alarms = g_slist_prepend (comp_alarms, alarms);
	}

	return comp_alarms;
}

/**
 * e_cal_get_alarms_in_range: (skip)
 * @ecal: A calendar client.
 * @start: Start time for query.
 * @end: End time for query.
 *
 * Queries a calendar for the alarms that trigger in the specified range of
 * time.
 *
 * Returns: A list of #ECalComponentAlarms structures.  This should be freed
 * using the e_cal_free_alarms() function, or by freeing each element
 * separately with e_cal_component_alarms_free() and then freeing the list with
 * g_slist_free().
 *
 * Deprecated: 3.2: This function has been dropped completely.
 **/
GSList *
e_cal_get_alarms_in_range (ECal *ecal,
                           time_t start,
                           time_t end)
{
	ECalPrivate *priv;
	GSList *alarms;
	gchar *sexp, *iso_start, *iso_end;
	GList *object_list = NULL;

	g_return_val_if_fail (ecal != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL (ecal), NULL);

	priv = ecal->priv;
	g_return_val_if_fail (priv->load_state == E_CAL_LOAD_LOADED, NULL);

	g_return_val_if_fail (start >= 0 && end >= 0, NULL);
	g_return_val_if_fail (start <= end, NULL);

	iso_start = isodate_from_time_t (start);
	if (!iso_start)
		return NULL;

	iso_end = isodate_from_time_t (end);
	if (!iso_end) {
		g_free (iso_start);
		return NULL;
	}

	/* build the query string */
	sexp = g_strdup_printf (
		"(has-alarms-in-range? (make-time \"%s\") (make-time \"%s\"))",
		iso_start, iso_end);
	g_free (iso_start);
	g_free (iso_end);

	/* execute the query on the server */
	if (!e_cal_get_object_list (ecal, sexp, &object_list, NULL)) {
		g_free (sexp);
		return NULL;
	}

	alarms = build_component_alarms_list (ecal, object_list, start, end);

	g_list_foreach (object_list, (GFunc) icalcomponent_free, NULL);
	g_list_free (object_list);
	g_free (sexp);

	return alarms;
}

/**
 * e_cal_free_alarms: (skip)
 * @comp_alarms: A list of #ECalComponentAlarms structures.
 *
 * Frees a list of #ECalComponentAlarms structures as returned by
 * e_cal_get_alarms_in_range().
 *
 * Deprecated: 3.2: This function has been dropped completely.
 **/
void
e_cal_free_alarms (GSList *comp_alarms)
{
	GSList *l;

	for (l = comp_alarms; l; l = l->next) {
		ECalComponentAlarms *alarms;

		alarms = l->data;
		if (alarms != NULL)
			e_cal_component_alarms_free (alarms);
		else
			g_warn_if_reached ();
	}

	g_slist_free (comp_alarms);
}

/**
 * e_cal_get_alarms_for_object:
 * @ecal: A calendar client.
 * @id: Unique identifier for a calendar component.
 * @start: Start time for query.
 * @end: End time for query.
 * @alarms: Return value for the component's alarm instances.  Will return NULL
 * if no instances occur within the specified time range.  This should be freed
 * using the e_cal_component_alarms_free() function.
 *
 * Queries a calendar for the alarms of a particular object that trigger in the
 * specified range of time.
 *
 * Returns: TRUE on success, FALSE if the object was not found.
 *
 * Deprecated: 3.2: This function has been dropped completely.
 **/
gboolean
e_cal_get_alarms_for_object (ECal *ecal,
                             const ECalComponentId *id,
                             time_t start,
                             time_t end,
                             ECalComponentAlarms **alarms)
{
	ECalPrivate *priv;
	icalcomponent *icalcomp;
	ECalComponent *comp;
	ECalComponentAlarmAction omit[] = {-1};

	g_return_val_if_fail (alarms != NULL, FALSE);
	*alarms = NULL;

	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	priv = ecal->priv;
	g_return_val_if_fail (priv->load_state == E_CAL_LOAD_LOADED, FALSE);

	g_return_val_if_fail (id != NULL, FALSE);
	g_return_val_if_fail (start >= 0 && end >= 0, FALSE);
	g_return_val_if_fail (start <= end, FALSE);

	if (!e_cal_get_object (ecal, id->uid, id->rid, &icalcomp, NULL))
		return FALSE;
	if (!icalcomp)
		return FALSE;

	comp = e_cal_component_new ();
	if (!e_cal_component_set_icalcomponent (comp, icalcomp)) {
		icalcomponent_free (icalcomp);
		g_object_unref (G_OBJECT (comp));
		return FALSE;
	}

	*alarms = e_cal_util_generate_alarms_for_comp (comp, start, end, omit, e_cal_resolve_tzid_cb,
						       ecal, priv->default_zone);

	return TRUE;
}

/**
 * e_cal_discard_alarm:
 * @ecal: A calendar ecal.
 * @comp: The component to discard the alarm from.
 * @auid: Unique identifier of the alarm to be discarded.
 * @error: Placeholder for error information.
 *
 * Tells the calendar backend to get rid of the alarm identified by the
 * @auid argument in @comp. Some backends might remove the alarm or
 * update internal information about the alarm be discarded, or, like
 * the file backend does, ignore the operation.
 *
 * CALOBJ_MOD_ONLY_THIS is not supported in this call.
 *
 * Returns: TRUE if the operation was successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_discard_alarm_sync() instead.
 */
gboolean
e_cal_discard_alarm (ECal *ecal,
                     ECalComponent *comp,
                     const gchar *auid,
                     GError **error)
{
	ECalPrivate *priv;

	e_return_error_if_fail (ecal != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (comp != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL_COMPONENT (comp), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (auid != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_NOT_SUPPORTED, error);
}

typedef struct _ForeachTZIDCallbackData ForeachTZIDCallbackData;
struct _ForeachTZIDCallbackData {
	ECal *ecal;
	GHashTable *timezone_hash;
	gboolean include_all_timezones;
	gboolean success;
};

/* This adds the VTIMEZONE given by the TZID parameter to the GHashTable in
 * data. */
static void
foreach_tzid_callback (icalparameter *param,
                       gpointer cbdata)
{
	ForeachTZIDCallbackData *data = cbdata;
	ECalPrivate *priv;
	const gchar *tzid;
	icaltimezone *zone = NULL;
	icalcomponent *vtimezone_comp;
	gchar *vtimezone_as_string;

	priv = data->ecal->priv;

	/* Get the TZID string from the parameter. */
	tzid = icalparameter_get_tzid (param);
	if (!tzid)
		return;

	/* Check if we've already added it to the GHashTable. */
	if (g_hash_table_lookup (data->timezone_hash, tzid))
		return;

	if (data->include_all_timezones) {
		if (!e_cal_get_timezone (data->ecal, tzid, &zone, NULL)) {
			data->success = FALSE;
			return;
		}
	} else {
		/* Check if it is in our cache. If it is, it must already be
		 * on the server so return. */
		if (g_hash_table_lookup (priv->timezones, tzid))
			return;

		/* Check if it is a builtin timezone. If it isn't, return. */
		zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
		if (!zone)
			return;
	}

	/* Convert it to a string and add it to the hash. */
	vtimezone_comp = icaltimezone_get_component (zone);
	if (!vtimezone_comp)
		return;

	vtimezone_as_string = icalcomponent_as_ical_string_r (vtimezone_comp);

	g_hash_table_insert (
		data->timezone_hash, (gchar *) tzid,
		vtimezone_as_string);
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

/* This converts the VEVENT/VTODO to a string. If include_all_timezones is
 * TRUE, it includes all the VTIMEZONE components needed for the VEVENT/VTODO.
 * If not, it only includes builtin timezones that may not be on the server.
 *
 * To do that we check every TZID in the component to see if it is a builtin
 * timezone. If it is, we see if it it in our cache. If it is in our cache,
 * then we know the server already has it and we don't need to send it.
 * If it isn't in our cache, then we need to send it to the server.
 * If we need to send any timezones to the server, then we have to create a
 * complete VCALENDAR object, otherwise we can just send a single VEVENT/VTODO
 * as before. */
static gchar *
e_cal_get_component_as_string_internal (ECal *ecal,
                                        icalcomponent *icalcomp,
                                        gboolean include_all_timezones)
{
	GHashTable *timezone_hash;
	GString *vcal_string;
	gint initial_vcal_string_len;
	ForeachTZIDCallbackData cbdata;
	gchar *obj_string;

	timezone_hash = g_hash_table_new (g_str_hash, g_str_equal);

	/* Add any timezones needed to the hash. We use a hash since we only
	 * want to add each timezone once at most. */
	cbdata.ecal = ecal;
	cbdata.timezone_hash = timezone_hash;
	cbdata.include_all_timezones = include_all_timezones;
	cbdata.success = TRUE;
	icalcomponent_foreach_tzid (icalcomp, foreach_tzid_callback, &cbdata);
	if (!cbdata.success) {
		g_hash_table_foreach (timezone_hash, free_timezone_string,
				      NULL);
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
	initial_vcal_string_len = vcal_string->len;

	/* Now concatenate all the timezone strings. This also frees the
	 * timezone strings as it goes. */
	g_hash_table_foreach (timezone_hash, append_timezone_string,
			      vcal_string);

	/* Get the string for the VEVENT/VTODO. */
	obj_string = icalcomponent_as_ical_string_r (icalcomp);

	/* If there were any timezones to send, create a complete VCALENDAR,
	 * else just send the VEVENT / VTODO string. */
	if (!include_all_timezones
	    && vcal_string->len == initial_vcal_string_len) {
		g_string_free (vcal_string, TRUE);
	} else {
		g_string_append (vcal_string, obj_string);
		g_string_append (vcal_string, "END:VCALENDAR\n");
		g_free (obj_string);
		obj_string = vcal_string->str;
		g_string_free (vcal_string, FALSE);
	}

	g_hash_table_destroy (timezone_hash);

	return obj_string;
}

/**
 * e_cal_get_component_as_string: (skip)
 * @ecal: A calendar client.
 * @icalcomp: A calendar component object.
 *
 * Gets a calendar component as an iCalendar string, with a toplevel
 * VCALENDAR component and all VTIMEZONEs needed for the component.
 *
 * Returns: the component as a complete iCalendar string, or NULL on
 * failure. The string should be freed after use.
 *
 * Deprecated: 3.2: Use e_cal_client_get_component_as_string() instead.
 **/
gchar *
e_cal_get_component_as_string (ECal *ecal,
                               icalcomponent *icalcomp)
{
	return e_cal_get_component_as_string_internal (ecal, icalcomp, TRUE);
}

/**
 * e_cal_create_object: (skip)
 * @ecal: A calendar client.
 * @icalcomp: The component to create.
 * @uid: Return value for the UID assigned to the new component by the calendar backend.
 * @error: Placeholder for error information.
 *
 * Requests the calendar backend to create the object specified by the @icalcomp
 * argument. Some backends would assign a specific UID to the newly created object,
 * in those cases that UID would be returned in the @uid argument.
 *
 * Returns: TRUE if the operation was successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_create_object_sync() instead.
 */
gboolean
e_cal_create_object (ECal *ecal,
                     icalcomponent *icalcomp,
                     gchar **uid,
                     GError **error)
{
	ECalPrivate *priv;
	gchar *obj, *gdbus_obj = NULL;
	const gchar *strv[2];
	gchar **muids = NULL;

	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (icalcomp != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (icalcomponent_is_valid (icalcomp), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	obj = icalcomponent_as_ical_string_r (icalcomp);
	strv[0] = e_util_ensure_gdbus_string (obj, &gdbus_obj);
	strv[1] = NULL;

	if (!e_gdbus_cal_call_create_objects_sync (priv->gdbus_cal, strv, &muids, NULL, error)) {
		g_free (obj);
		g_free (gdbus_obj);
		g_strfreev (muids);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	g_free (obj);
	g_free (gdbus_obj);

	if (!muids) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OTHER_ERROR, error);
	} else {
		icalcomponent_set_uid (icalcomp, muids[0]);

		if (uid)
			*uid = g_strdup (muids[0]);

		g_strfreev (muids);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);
	}
}

/**
 * e_cal_modify_object: (skip)
 * @ecal: A calendar client.
 * @icalcomp: Component to modify.
 * @mod: Type of modification.
 * @error: Placeholder for error information.
 *
 * Requests the calendar backend to modify an existing object. If the object
 * does not exist on the calendar, an error will be returned.
 *
 * For recurrent appointments, the @mod argument specifies what to modify,
 * if all instances (CALOBJ_MOD_ALL), a single instance (CALOBJ_MOD_THIS),
 * or a specific set of instances (CALOBJ_MOD_THISNADPRIOR and
 * CALOBJ_MOD_THISANDFUTURE).
 *
 * Returns: TRUE if the operation was successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_modify_object_sync() instead.
 */
gboolean
e_cal_modify_object (ECal *ecal,
                     icalcomponent *icalcomp,
                     CalObjModType mod,
                     GError **error)
{
	ECalPrivate *priv;
	gchar *obj, **strv;
	GSList objs = {0,};

	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (icalcomp, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (icalcomponent_is_valid (icalcomp), E_CALENDAR_STATUS_INVALID_ARG);
	switch (mod) {
	case CALOBJ_MOD_THIS:
	case CALOBJ_MOD_THISANDPRIOR:
	case CALOBJ_MOD_THISANDFUTURE:
	case CALOBJ_MOD_ALL:
		break;
	default:
		e_return_error_if_fail ("valid CalObjModType" && FALSE, E_CALENDAR_STATUS_INVALID_ARG);
	}
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	obj = icalcomponent_as_ical_string_r (icalcomp);
	objs.data = obj;
	strv = e_gdbus_cal_encode_modify_objects (&objs, mod);
	if (!e_gdbus_cal_call_modify_objects_sync (priv->gdbus_cal, (const gchar * const *) strv, NULL, error)) {
		g_free (obj);
		g_strfreev (strv);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	g_free (obj);
	g_strfreev (strv);

	E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);
}

/**
 * e_cal_remove_object_with_mod: (skip)
 * @ecal: A calendar client.
 * @uid: UID of the object to remove.
 * @rid: Recurrence ID of the specific recurrence to remove.
 * @mod: Type of removal.
 * @error: Placeholder for error information.
 *
 * This function allows the removal of instances of a recurrent
 * appointment. If what you want is to remove all instances, use
 * e_cal_remove_object instead.
 *
 * By using a combination of the @uid, @rid and @mod arguments, you
 * can remove specific instances. @uid is mandatory.  Empty or NULL
 * @rid selects the parent appointment (the one with the recurrence
 * rule). A non-empty @rid selects the recurrence at the time specified
 * in @rid, using the same time zone as the parent appointment's start
 * time.
 *
 * The exact semantic then depends on @mod. CALOBJ_MOD_THIS,
 * CALOBJ_MOD_THISANDPRIOR, CALOBJ_MOD_THISANDFUTURE and
 * CALOBJ_MOD_ALL ensure that the event does not recur at the selected
 * instance(s). This is done by removing any detached recurrence
 * matching the selection criteria and modifying the parent
 * appointment (adding EXDATE, adjusting recurrence rules, etc.).  It
 * is not an error if @uid+@rid do not match an existing instance.
 *
 * If not all instances are removed, the client will get a
 * "obj_modified" signal for the parent appointment, while it will get
 * an "obj_removed" signal when all instances are removed.
 *
 * CALOBJ_MOD_ONLY_THIS changes the semantic of CALOBJ_MOD_THIS: @uid
 * and @rid must select an existing instance. That instance is
 * removed without modifying the parent appointment. In other words,
 * e_cal_remove_object_with_mod(CALOBJ_MOD_ONLY_THIS) is the inverse
 * operation for adding a detached recurrence. The client is
 * always sent an "obj_removed" signal.
 *
 * Note that not all backends support CALOBJ_MOD_ONLY_THIS. Check for
 * the CAL_STATIC_CAPABILITY_REMOVE_ONLY_THIS capability before using
 * it. Previous releases did not check consistently for unknown
 * @mod values, using it with them may have had unexpected results.
 *
 * Returns: TRUE if the operation was successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_remove_object_sync() instead.
 */
gboolean
e_cal_remove_object_with_mod (ECal *ecal,
                              const gchar *uid,
                              const gchar *rid,
                              CalObjModType mod,
                              GError **error)
{
	ECalPrivate *priv;
	gchar **strv;
	GSList ids = {0,};
	ECalComponentId id;

	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (uid, E_CALENDAR_STATUS_INVALID_ARG);
	switch (mod) {
	case CALOBJ_MOD_THIS:
	case CALOBJ_MOD_THISANDPRIOR:
	case CALOBJ_MOD_THISANDFUTURE:
	case CALOBJ_MOD_ONLY_THIS:
	case CALOBJ_MOD_ALL:
		break;
	default:
		e_return_error_if_fail ("valid CalObjModType" && FALSE, E_CALENDAR_STATUS_INVALID_ARG);
	}
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	id.uid = (gchar *) uid;
	id.rid = (gchar *) rid;
	ids.data = &id;
	strv = e_gdbus_cal_encode_remove_objects (&ids, mod);
	if (!e_gdbus_cal_call_remove_objects_sync (priv->gdbus_cal, (const gchar * const *) strv, NULL, error)) {
		g_strfreev (strv);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	g_strfreev (strv);

	E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);
}

/**
 * e_cal_remove_object:
 * @ecal:  A calendar client.
 * @uid: Unique identifier of the calendar component to remove.
 * @error: Placeholder for error information.
 *
 * Asks a calendar to remove all components with the given UID.
 * If more control of the removal is desired, then use e_cal_remove_object_with_mod().
 * If the server is able to remove the component(s), all clients will
 * be notified and they will emit the "obj_removed" signal.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_remove_object_sync() instead, with rid set to NULL and mod set to CALOBJ_MOD_ALL.
 **/
gboolean
e_cal_remove_object (ECal *ecal,
                     const gchar *uid,
                     GError **error)
{
	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (uid, E_CALENDAR_STATUS_INVALID_ARG);

	return e_cal_remove_object_with_mod (ecal, uid, NULL, CALOBJ_MOD_ALL, error);
}

/**
 * e_cal_receive_objects: (skip)
 * @ecal:  A calendar client.
 * @icalcomp: An icalcomponent.
 * @error: Placeholder for error information.
 *
 * Makes the backend receive the set of iCalendar objects specified in the
 * @icalcomp argument. This is used for iTIP confirmation/cancellation
 * messages for scheduled meetings.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_receive_objects_sync() instead.
 */
gboolean
e_cal_receive_objects (ECal *ecal,
                       icalcomponent *icalcomp,
                       GError **error)
{
	ECalPrivate *priv;
	gchar *obj, *gdbus_obj = NULL;

	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (icalcomp, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (icalcomponent_is_valid (icalcomp), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	obj = icalcomponent_as_ical_string_r (icalcomp);
	if (!e_gdbus_cal_call_receive_objects_sync (priv->gdbus_cal, e_util_ensure_gdbus_string (obj, &gdbus_obj), NULL, error)) {
		g_free (obj);
		g_free (gdbus_obj);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	g_free (obj);
	g_free (gdbus_obj);

	E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);
}

/**
 * e_cal_send_objects: (skip)
 * @ecal: A calendar client.
 * @icalcomp: An icalcomponent.
 * @users: List of users to send the objects to.
 * @modified_icalcomp: Return value for the icalcomponent after all the operations
 * performed.
 * @error: Placeholder for error information.
 *
 * Requests a calendar backend to send meeting information to the specified list
 * of users.
 *
 * Returns: TRUE if the operation was successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_send_objects_sync() instead.
 */
gboolean
e_cal_send_objects (ECal *ecal,
                    icalcomponent *icalcomp,
                    GList **users,
                    icalcomponent **modified_icalcomp,
                    GError **error)
{
	ECalPrivate *priv;
	ECalendarStatus status;
	gchar **out_array = NULL;
	gchar *obj, *gdbus_obj = NULL;

	e_return_error_if_fail (users != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (modified_icalcomp != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (icalcomp != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (icalcomponent_is_valid (icalcomp), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);
	*users = NULL;
	*modified_icalcomp = NULL;

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	obj = icalcomponent_as_ical_string_r (icalcomp);
	if (!e_gdbus_cal_call_send_objects_sync (priv->gdbus_cal, e_util_ensure_gdbus_string (obj, &gdbus_obj), &out_array, NULL, error)) {
		g_free (obj);
		g_free (gdbus_obj);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	g_free (obj);
	g_free (gdbus_obj);

	status = E_CALENDAR_STATUS_OK;
	if (out_array) {
		GSList *susers = NULL, *iter;
		gchar *object = NULL;

		e_return_error_if_fail (e_gdbus_cal_decode_send_objects ((const gchar * const *) out_array, &object, &susers), E_CALENDAR_STATUS_OTHER_ERROR);

		*modified_icalcomp = icalparser_parse_string (object);
		if (!(*modified_icalcomp))
			status = E_CALENDAR_STATUS_INVALID_OBJECT;

		*users = NULL;
		for (iter = susers; iter; iter = iter->next) {
			*users = g_list_append (*users, iter->data);
		}
		/* do not call g_free() on item's data of susers, it's moved to *users */
		g_slist_free (susers);
		g_strfreev (out_array);
		g_free (object);
	} else
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OTHER_ERROR, error);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_timezone: (skip)
 * @ecal: A calendar client.
 * @tzid: ID of the timezone to retrieve.
 * @zone: Return value for the timezone.
 * @error: Placeholder for error information.
 *
 * Retrieves a timezone object from the calendar backend.
 *
 * Returns: TRUE if the operation was successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_get_timezone_sync() instead.
 */
gboolean
e_cal_get_timezone (ECal *ecal,
                    const gchar *tzid,
                    icaltimezone **zone,
                    GError **error)
{
	ECalPrivate *priv;
	ECalendarStatus status = E_CALENDAR_STATUS_OK;
	icalcomponent *icalcomp = NULL;
	gchar *object = NULL, *gdbus_tzid = NULL;
	const gchar *systzid = NULL;

	e_return_error_if_fail (zone, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);
	*zone = NULL;

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	/* Check for well known zones and in the cache */
	/* If tzid is NULL or "" we return NULL, since it is a 'local time'. */
	if (!tzid || !tzid[0]) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);
	}

	LOCK_CACHE ();
	/* If it is UTC, we return the special UTC timezone. */
	if (!strcmp (tzid, "UTC")) {
		*zone = icaltimezone_get_utc_timezone ();
	} else {
		/* See if we already have it in the cache. */
		*zone = g_hash_table_lookup (priv->timezones, tzid);
	}

	if (*zone) {
		UNLOCK_CACHE ();
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);
	}

	/*
	 * Try to replace the original time zone with a more complete
	 * and/or potentially updated system time zone. Note that this
	 * also applies to TZIDs which match system time zones exactly:
	 * they are extracted via icaltimezone_get_builtin_timezone_from_tzid()
	 * below without a roundtrip to the backend.
	 */
	systzid = e_cal_match_tzid (tzid);
	if (!systzid) {
		/* call the backend */
		if (!e_gdbus_cal_call_get_timezone_sync (priv->gdbus_cal, e_util_ensure_gdbus_string (tzid, &gdbus_tzid), &object, NULL, error)) {
			g_free (gdbus_tzid);

			UNLOCK_CACHE ();
			E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
		}

		g_free (gdbus_tzid);

		icalcomp = icalparser_parse_string (object);
		if (!icalcomp)
			status = E_CALENDAR_STATUS_INVALID_OBJECT;
		g_free (object);
	} else {
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
			icalproperty *prop;

			icalcomp = icalcomponent_new_clone (icaltimezone_get_component (syszone));
			prop = icalcomponent_get_first_property (
				icalcomp, ICAL_ANY_PROPERTY);
			while (!found && prop) {
				if (icalproperty_isa (prop) == ICAL_TZID_PROPERTY) {
					icalproperty_set_value_from_string (prop, tzid, "NO");
					found = TRUE;
				}
				prop = icalcomponent_get_next_property (
					icalcomp, ICAL_ANY_PROPERTY);
			}
		} else {
			status = E_CALENDAR_STATUS_INVALID_OBJECT;
		}
	}

	if (!icalcomp) {
		UNLOCK_CACHE ();
		E_CALENDAR_CHECK_STATUS (status, error);
	}

	*zone = icaltimezone_new ();
	if (!icaltimezone_set_component (*zone, icalcomp)) {
		icaltimezone_free (*zone, 1);
		UNLOCK_CACHE ();
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OBJECT_NOT_FOUND, error);
	}

	/* Now add it to the cache, to avoid the server call in future. */
	g_hash_table_insert (priv->timezones, (gpointer) icaltimezone_get_tzid (*zone), *zone);

	UNLOCK_CACHE ();
	return TRUE;
}

/**
 * e_cal_add_timezone: (skip)
 * @ecal: A calendar client.
 * @izone: The timezone to add.
 * @error: Placeholder for error information.
 *
 * Add a VTIMEZONE object to the given calendar.
 *
 * Returns: TRUE if successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_add_timezone_sync() instead.
 */
gboolean
e_cal_add_timezone (ECal *ecal,
                    icaltimezone *izone,
                    GError **error)
{
	ECalPrivate *priv;
	gchar *tzobj, *gdbus_tzobj = NULL;
	icalcomponent *icalcomp;

	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (izone, E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	/* Make sure we have a valid component - UTC doesn't, nor do
	 * we really have to add it */
	if (izone == icaltimezone_get_utc_timezone ()) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);
	}

	icalcomp = icaltimezone_get_component (izone);
	if (!icalcomp) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_INVALID_ARG, error);
	}

	/* convert icaltimezone into a string */
	tzobj = icalcomponent_as_ical_string_r (icalcomp);

	/* call the backend */
	if (!e_gdbus_cal_call_add_timezone_sync (priv->gdbus_cal, e_util_ensure_gdbus_string (tzobj, &gdbus_tzobj), NULL, error)) {
		g_free (tzobj);
		g_free (gdbus_tzobj);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	g_free (tzobj);
	g_free (gdbus_tzobj);

	E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);
}

/**
 * e_cal_get_query:
 * @ecal: A calendar client.
 * @sexp: S-expression representing the query.
 * @query: (out): Return value for the new query.
 * @error: Placeholder for error information.
 *
 * Creates a live query object from a loaded calendar.
 *
 * Returns: A query object that will emit notification signals as calendar
 * components are added and removed from the query in the server.
 *
 * Deprecated: 3.2: Use e_cal_client_get_view_sync() instead.
 **/
gboolean
e_cal_get_query (ECal *ecal,
                 const gchar *sexp,
                 ECalView **query,
                 GError **error)
{
	ECalPrivate *priv;
	ECalendarStatus status;
	gchar *query_path = NULL, *gdbus_sexp = NULL;
	EGdbusCalView *gdbus_calview;

	e_return_error_if_fail (sexp, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (query, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);
	*query = NULL;

	if (priv->load_state != E_CAL_LOAD_LOADED) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (!e_gdbus_cal_call_get_view_sync (priv->gdbus_cal, e_util_ensure_gdbus_string (sexp, &gdbus_sexp), &query_path, NULL, error)) {
		g_free (gdbus_sexp);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_DBUS_EXCEPTION, error);
	}

	g_free (gdbus_sexp);

	status = E_CALENDAR_STATUS_OK;

	gdbus_calview = e_gdbus_cal_view_proxy_new_sync (
		g_dbus_proxy_get_connection (G_DBUS_PROXY (cal_factory_proxy)),
		G_DBUS_PROXY_FLAGS_NONE,
		CALENDAR_DBUS_SERVICE_NAME,
		query_path,
		NULL,
		error);

	g_free (query_path);

	if (!gdbus_calview) {
		*query = NULL;
		status = E_CALENDAR_STATUS_OTHER_ERROR;
	} else {
		*query = _e_cal_view_new (ecal, gdbus_calview);
		g_object_unref (gdbus_calview);
	}

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_set_default_timezone: (skip)
 * @ecal: A calendar client.
 * @zone: A timezone object.
 * @error: Placeholder for error information.
 *
 * Sets the default timezone on the calendar. This should be called before opening
 * the calendar.
 *
 * Returns: TRUE if the operation was successful, FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_cal_client_set_default_timezone() instead.
 */
gboolean
e_cal_set_default_timezone (ECal *ecal,
                            icaltimezone *zone,
                            GError **error)
{
	ECalPrivate *priv;

	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (zone, E_CALENDAR_STATUS_INVALID_ARG);
	priv = ecal->priv;
	e_return_error_if_fail (priv->gdbus_cal, E_CALENDAR_STATUS_REPOSITORY_OFFLINE);

	/* If the same timezone is already set, we don't have to do anything. */
	if (priv->default_zone == zone)
		return TRUE;

	/* FIXME Adding it to the server to change the tzid */
	if (!icaltimezone_get_component (zone)) {
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_INVALID_ARG, error);
	}

	priv->default_zone = zone;

	E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);
}

/**
 * e_cal_get_error_message:
 * @status: A status code.
 *
 * Gets an error message for the given status code.
 *
 * Returns: the error message.
 *
 * Deprecated: 3.2: Use e_cal_client_error_to_string() instead.
 */
const gchar *
e_cal_get_error_message (ECalendarStatus status)
{
	switch (status) {
	case E_CALENDAR_STATUS_INVALID_ARG :
		return _("Invalid argument");
	case E_CALENDAR_STATUS_BUSY :
		return _("Backend is busy");
	case E_CALENDAR_STATUS_REPOSITORY_OFFLINE :
		return _("Repository is offline");
	case E_CALENDAR_STATUS_NO_SUCH_CALENDAR :
		return _("No such calendar");
	case E_CALENDAR_STATUS_OBJECT_NOT_FOUND :
		return _("Object not found");
	case E_CALENDAR_STATUS_INVALID_OBJECT :
		return _("Invalid object");
	case E_CALENDAR_STATUS_URI_NOT_LOADED :
		return _("URI not loaded");
	case E_CALENDAR_STATUS_URI_ALREADY_LOADED :
		return _("URI already loaded");
	case E_CALENDAR_STATUS_PERMISSION_DENIED :
		return _("Permission denied");
	case E_CALENDAR_STATUS_UNKNOWN_USER :
		return _("Unknown User");
	case E_CALENDAR_STATUS_OBJECT_ID_ALREADY_EXISTS :
		return _("Object ID already exists");
	case E_CALENDAR_STATUS_PROTOCOL_NOT_SUPPORTED :
		return _("Protocol not supported");
	case E_CALENDAR_STATUS_CANCELLED :
		return _("Operation has been canceled");
	case E_CALENDAR_STATUS_COULD_NOT_CANCEL :
		return _("Could not cancel operation");
	case E_CALENDAR_STATUS_AUTHENTICATION_FAILED :
		return _("Authentication failed");
	case E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED :
		return _("Authentication required");
	case E_CALENDAR_STATUS_DBUS_EXCEPTION :
		return _("A D-Bus exception has occurred");
	case E_CALENDAR_STATUS_OTHER_ERROR :
		return _("Unknown error");
	case E_CALENDAR_STATUS_OK :
		return _("No error");
	case E_CALENDAR_STATUS_NOT_SUPPORTED :
		/* Translators: The string for NOT_SUPPORTED error */
		return _("Not supported");
	default:
		/* ignore everything else */
		break;
	}

	return NULL;
}

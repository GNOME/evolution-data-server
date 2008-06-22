/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar ecal
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Rodrigo Moya <rodrigo@novell.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pthread.h>
#include <string.h>
#include <glib/gi18n-lib.h>
#include <bonobo-activation/bonobo-activation.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-main.h>

#include "libecal/e-cal-check-timezones.h"
#include "libedataserver/e-component-listener.h"
#include "libedataserver/e-flag.h"
#include "libedataserver/e-url.h"
#include "e-cal-marshal.h"
#include "e-cal-time-util.h"
#include "e-cal-listener.h"
#include "e-cal-view-listener.h"
#include "e-cal-view-private.h"
#include "e-cal.h"


static gboolean
open_calendar (ECal *ecal, gboolean only_if_exists, GError **error, ECalendarStatus *status, gboolean needs_auth);

static gboolean
get_read_only (ECal *ecal, gboolean *read_only, GError **error);


typedef struct {
	EFlag *done;
	ECalendarStatus status;

	char *uid;
	GList *list;
	GSList *slist;
	gboolean bool;
	char *string;

	ECalView *query;
	ECalViewListener *listener;
} ECalendarOp;

/* Private part of the ECal structure */
struct _ECalPrivate {
	/* Load state to avoid multiple loads */
	ECalLoadState load_state;

	/* URI of the calendar that is being loaded or is already loaded, or
	 * NULL if we are not loaded.
	 */
	ESource *source;
	char *uri;
	ECalSourceType type;

	ECalendarOp *current_op;

	GMutex *mutex;

	/* Email address associated with this calendar, or NULL */
	char *cal_address;
	char *alarm_email_address;
	char *ldap_attribute;

	/* Scheduling info */
	char *capabilities;

	int mode;

	gboolean read_only;

	/* The calendar factories we are contacting */
	GList *factories;

	/* Our calendar listener implementation */
	ECalListener *listener;

	/* The calendar ecal interface object we are contacting */
	GNOME_Evolution_Calendar_Cal cal;

	/* The authentication function */
	ECalAuthFunc auth_func;
	gpointer auth_user_data;

	/* A cache of timezones retrieved from the server, to avoid getting
	   them repeatedly for each get_object() call. */
	GHashTable *timezones;

	/* The default timezone to use to resolve DATE and floating DATE-TIME
	   values. */
	icaltimezone *default_zone;

	/* The component listener to keep track of the lifetime of backends */
	EComponentListener *comp_listener;

	char *local_attachment_store;
};



/* Signal IDs */
enum {
	CAL_OPENED,
	CAL_SET_MODE,
	BACKEND_ERROR,
	BACKEND_DIED,
	LAST_SIGNAL
};

static guint e_cal_signals[LAST_SIGNAL];

static GObjectClass *parent_class;

#ifdef __PRETTY_FUNCTION__
#define e_return_error_if_fail(expr,error_code)	G_STMT_START{		\
     if G_LIKELY(expr) { } else						\
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
     if G_LIKELY(expr) { } else						\
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

#define E_CALENDAR_CHECK_STATUS(status,error) G_STMT_START{		\
	if ((status) == E_CALENDAR_STATUS_OK) {				\
		return TRUE;						\
	}								\
	else {                                                          \
                const char *msg;                                        \
                msg = e_cal_get_error_message ((status));          \
		g_set_error ((error), E_CALENDAR_ERROR, (status), msg, (status));	\
		return FALSE;						\
	}				}G_STMT_END



/* Error quark */
GQuark
e_calendar_error_quark (void)
{
	static GQuark q = 0;
	if (q == 0)
		q = g_quark_from_static_string ("e-calendar-error-quark");

	return q;
}

/**
 * e_cal_source_type_enum_get_type:
 *
 * Registers the #ECalSourceTypeEnum type with glib.
 *
 * Return value: the ID of the #ECalSourceTypeEnum type.
 */
GType
e_cal_source_type_enum_get_type (void)
{
	static GType e_cal_source_type_enum_type = 0;

	if (!e_cal_source_type_enum_type) {
		static GEnumValue values [] = {
			{ E_CAL_SOURCE_TYPE_EVENT, "Event", NULL},
			{ E_CAL_SOURCE_TYPE_TODO, "ToDo", NULL},
			{ E_CAL_SOURCE_TYPE_JOURNAL, "Journal", NULL},
			{ E_CAL_SOURCE_TYPE_LAST, "Invalid", NULL},
			{ -1, NULL, NULL}
		};

		e_cal_source_type_enum_type =
			g_enum_register_static ("ECalSourceTypeEnum", values);
	}

	return e_cal_source_type_enum_type;
}

/**
 * e_cal_set_mode_status_enum_get_type:
 *
 * Registers the #ECalSetModeStatusEnum type with glib.
 *
 * Return value: the ID of the #ECalSetModeStatusEnum type.
 */
GType
e_cal_set_mode_status_enum_get_type (void)
{
	static GType e_cal_set_mode_status_enum_type = 0;

	if (!e_cal_set_mode_status_enum_type) {
		static GEnumValue values [] = {
			{ E_CAL_SET_MODE_SUCCESS,          "ECalSetModeSuccess",         "success"     },
			{ E_CAL_SET_MODE_ERROR,            "ECalSetModeError",           "error"       },
			{ E_CAL_SET_MODE_NOT_SUPPORTED,    "ECalSetModeNotSupported",    "unsupported" },
			{ -1,                                   NULL,                              NULL          }
		};

		e_cal_set_mode_status_enum_type =
			g_enum_register_static ("ECalSetModeStatusEnum", values);
	}

	return e_cal_set_mode_status_enum_type;
}

/**
 * cal_mode_enum_get_type:
 *
 * Registers the #CalModeEnum type with glib.
 *
 * Return value: the ID of the #CalModeEnum type.
 */
GType
cal_mode_enum_get_type (void)
{
	static GType cal_mode_enum_type = 0;

	if (!cal_mode_enum_type) {
		static GEnumValue values [] = {
			{ CAL_MODE_INVALID,                     "CalModeInvalid",                  "invalid" },
			{ CAL_MODE_LOCAL,                       "CalModeLocal",                    "local"   },
			{ CAL_MODE_REMOTE,                      "CalModeRemote",                   "remote"  },
			{ CAL_MODE_ANY,                         "CalModeAny",                      "any"     },
			{ -1,                                   NULL,                              NULL      }
		};

		cal_mode_enum_type = g_enum_register_static ("CalModeEnum", values);
	}

	return cal_mode_enum_type;
}



static GNOME_Evolution_Calendar_CalObjType
convert_type (ECalSourceType type)
{
	switch (type){
	case E_CAL_SOURCE_TYPE_EVENT:
		return GNOME_Evolution_Calendar_TYPE_EVENT;
	case E_CAL_SOURCE_TYPE_TODO:
		return GNOME_Evolution_Calendar_TYPE_TODO;
	case E_CAL_SOURCE_TYPE_JOURNAL:
		return GNOME_Evolution_Calendar_TYPE_JOURNAL;
	default:
		return GNOME_Evolution_Calendar_TYPE_ANY;
	}

	return GNOME_Evolution_Calendar_TYPE_ANY;
}

/* EBookOp calls */

static ECalendarOp*
e_calendar_new_op (ECal *ecal)
{
	ECalendarOp *op = g_new0 (ECalendarOp, 1);

	op->done = e_flag_new ();

	ecal->priv->current_op = op;

	return op;
}

static ECalendarOp*
e_calendar_get_op (ECal *ecal)
{
	if (!ecal->priv->current_op) {
		g_warning (G_STRLOC ": Unexpected response");
		return NULL;
	}

	return ecal->priv->current_op;
}

static void
e_calendar_free_op (ECalendarOp *op)
{
	/* XXX more stuff here */
	e_flag_free (op->done);
	g_free (op);
}

static void
e_calendar_remove_op (ECal *ecal, ECalendarOp *op)
{
	if (ecal->priv->current_op != op)
		g_warning (G_STRLOC ": Cannot remove op, it's not current");

	ecal->priv->current_op = NULL;
}

/* Gets rid of the factories that a ecal knows about */
static void
destroy_factories (ECal *ecal)
{
	ECalPrivate *priv;
	CORBA_Object factory;
	CORBA_Environment ev;
	int result;
	GList *f;

	priv = ecal->priv;

	CORBA_exception_init (&ev);

	for (f = priv->factories; f; f = f->next) {
		factory = f->data;

		result = CORBA_Object_is_nil (factory, &ev);
		if (BONOBO_EX (&ev)) {
			g_message (G_STRLOC ": could not see if a factory was nil");
			CORBA_exception_free (&ev);

			continue;
		}

		if (result)
			continue;

		CORBA_Object_release (factory, &ev);
		if (BONOBO_EX (&ev)) {
			g_message (G_STRLOC ": could not release a factory");
			CORBA_exception_free (&ev);
		}
	}

	g_list_free (priv->factories);
	priv->factories = NULL;
}

/* Gets rid of the calendar ecal interface object that a ecal knows about */
static void
destroy_cal (ECal *ecal)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	int result;

	priv = ecal->priv;

	CORBA_exception_init (&ev);
	result = CORBA_Object_is_nil (priv->cal, &ev);
	if (BONOBO_EX (&ev)) {
		g_message (G_STRLOC ": could not see if the "
			   "calendar ecal interface object was nil");
		priv->cal = CORBA_OBJECT_NIL;
		CORBA_exception_free (&ev);
		return;
	}
	CORBA_exception_free (&ev);

	if (result)
		return;

	bonobo_object_release_unref (priv->cal, NULL);
	priv->cal = CORBA_OBJECT_NIL;

}

static void
free_timezone (gpointer key, gpointer value, gpointer data)
{
	/* Note that the key comes from within the icaltimezone value, so we
	   don't free that. */
	icaltimezone_free (value, TRUE);
}



static void
backend_died_cb (EComponentListener *cl, gpointer user_data)
{
	ECalPrivate *priv;
	ECal *ecal = (ECal *) user_data;

	priv = ecal->priv;
	priv->load_state = E_CAL_LOAD_NOT_LOADED;
	g_signal_emit (G_OBJECT (ecal), e_cal_signals[BACKEND_DIED], 0);
}

/* Signal handlers for the listener's signals */
/* Handle the cal_opened notification from the listener */

static void
cal_read_only_cb (ECalListener *listener, ECalendarStatus status, gboolean read_only, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL || !op->bool) {
		ecal->priv->read_only = read_only;
		return;
	}

	op->status = status;
	op->bool = read_only;

	e_flag_set (op->done);
}

static void
cal_cal_address_cb (ECalListener *listener, ECalendarStatus status, const char *address, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (ecal->priv->cal_address) {
		g_free (ecal->priv->cal_address);
		ecal->priv->cal_address = NULL;
	}

	ecal->priv->cal_address = g_strdup (address);

	if (op == NULL) {
		return;
	}

	op->status = status;
	op->string = g_strdup (address);

	e_flag_set (op->done);
}

static void
cal_alarm_address_cb (ECalListener *listener, ECalendarStatus status, const char *address, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;
	op->string = g_strdup (address);

	e_flag_set (op->done);
}

static void
cal_ldap_attribute_cb (ECalListener *listener, ECalendarStatus status, const char *attribute, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;
	op->string = g_strdup (attribute);

	e_flag_set (op->done);
}

static void
cal_static_capabilities_cb (ECalListener *listener, ECalendarStatus status, const char *capabilities, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;
	op->string = g_strdup (capabilities);

	e_flag_set (op->done);
}

static void
cal_opened_cb (ECalListener *listener, ECalendarStatus status, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;

	e_flag_set (op->done);
}

static void
cal_removed_cb (ECalListener *listener, ECalendarStatus status, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;

	e_flag_set (op->done);
}

static void
cal_object_created_cb (ECalListener *listener, ECalendarStatus status, const char *uid, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;
	op->uid = g_strdup (uid);

	e_flag_set (op->done);
}

static void
cal_object_modified_cb (ECalListener *listener, ECalendarStatus status, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;

	e_flag_set (op->done);
}

static void
cal_object_removed_cb (ECalListener *listener, ECalendarStatus status, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;

	e_flag_set (op->done);
}

static void
cal_alarm_discarded_cb (ECalListener *listener, ECalendarStatus status, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;

	e_flag_set (op->done);
}

static void
cal_objects_received_cb (ECalListener *listener, ECalendarStatus status, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;

	e_flag_set (op->done);
}

static void
cal_objects_sent_cb (ECalListener *listener, ECalendarStatus status, GList *users, const char *object, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;
	GList *l;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;
	op->list = g_list_copy (users);
	op->string = g_strdup (object);

	for (l = op->list; l; l = l->next)
		l->data = g_strdup (l->data);

	e_flag_set (op->done);
}

static void
cal_default_object_requested_cb (ECalListener *listener, ECalendarStatus status, const char *object, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;
	op->string = g_strdup (object);

	e_flag_set (op->done);
}

static void
cal_object_requested_cb (ECalListener *listener, ECalendarStatus status, const char *object, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;
	op->string = g_strdup (object);

	e_flag_set (op->done);
}

static void
cal_object_list_cb (ECalListener *listener, ECalendarStatus status, GList *objects, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;
	GList *l;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;
	op->list = g_list_copy (objects);

	for (l = op->list; l; l = l->next)
		l->data = icalcomponent_new_clone (l->data);

	e_flag_set (op->done);
}

static void
cal_attachment_list_cb (ECalListener *listener, ECalendarStatus status, GSList *attachments, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;
	op->slist = g_slist_copy (attachments);

	e_flag_set (op->done);
}

static void
cal_get_timezone_cb (ECalListener *listener, ECalendarStatus status, const char *object, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;
	op->string = g_strdup (object);

	e_flag_set (op->done);
}

static void
cal_add_timezone_cb (ECalListener *listener, ECalendarStatus status, const char *tzid, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;
	op->uid = g_strdup (tzid);

	e_flag_set (op->done);
}

static void
cal_set_default_timezone_cb (ECalListener *listener, ECalendarStatus status, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;

	e_flag_set (op->done);
}

static void
cal_get_changes_cb (ECalListener *listener, ECalendarStatus status, GList *changes, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;
	GList *l;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;
	op->list = g_list_copy (changes);

	for (l = op->list; l; l = l->next) {
		ECalChange *ccc = l->data, *new_ccc;

		new_ccc = g_new (ECalChange, 1);
		new_ccc->comp = e_cal_component_clone (ccc->comp);
		new_ccc->type = ccc->type;

		l->data = new_ccc;
	}

	e_flag_set (op->done);
}

static void
cal_get_free_busy_cb (ECalListener *listener, ECalendarStatus status, GList *freebusy, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;
	GList *l;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;
	op->list = g_list_copy (freebusy);

	for (l = op->list; l; l = l->next)
		l->data = e_cal_component_clone (l->data);

	e_flag_set (op->done);
}

static void
cal_query_cb (ECalListener *listener, ECalendarStatus status, GNOME_Evolution_Calendar_CalView query, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	op->status = status;
	op->query = e_cal_view_new (query, op->listener, ecal);

	e_flag_set (op->done);
}

static gboolean
reopen_with_auth (gpointer data)
{
	ECalendarStatus status;

	open_calendar (E_CAL (data), TRUE, NULL, &status, TRUE);
	return FALSE;
}

static void
auth_required_cb (ECalListener *listener, gpointer data)
{
	g_idle_add (reopen_with_auth, data);

}

/* Handle the cal_set_mode notification from the listener */
static void
cal_set_mode_cb (ECalListener *listener,
		 GNOME_Evolution_Calendar_CalListener_SetModeStatus status,
		 GNOME_Evolution_Calendar_CalMode mode,
		 gpointer data)
{
	ECal *ecal;
	ECalPrivate *priv;
	ECalSetModeStatus ecal_status;

	ecal = E_CAL (data);
	priv = ecal->priv;
	priv->mode = mode;
	ecal_status = E_CAL_SET_MODE_ERROR;

	switch (status) {
	case GNOME_Evolution_Calendar_CalListener_MODE_SET:
		ecal_status = E_CAL_SET_MODE_SUCCESS;
		break;
	case GNOME_Evolution_Calendar_CalListener_MODE_NOT_SET:
		ecal_status = E_CAL_SET_MODE_ERROR;
		break;
	case GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED:
		ecal_status = E_CAL_SET_MODE_NOT_SUPPORTED;
		break;
	default:
		g_assert_not_reached ();
	}

	/* We are *not* inside a signal handler (this is just a simple callback
	 * called from the listener), so there is not a temporary reference to
	 * the ecal object.  We ref() so that we can safely emit our own
	 * signal and clean up.
	 */

	g_object_ref (G_OBJECT (ecal));

	g_signal_emit (G_OBJECT (ecal), e_cal_signals[CAL_SET_MODE],
		       0, ecal_status, mode);

	g_object_unref (G_OBJECT (ecal));
}

typedef struct
{
	ECal *ecal;
	char *message;
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
backend_error_cb (ECalListener *listener, const char *message, gpointer data)
{
	ECalErrorData *error_data;

	error_data = g_new0 (ECalErrorData, 1);

	error_data->ecal = g_object_ref (data);
	error_data->message = g_strdup (message);

	g_idle_add (backend_error_idle_cb, error_data);
}



static gboolean
get_factories (const char *str_uri, GList **factories)
{
	GNOME_Evolution_Calendar_CalFactory factory;
	Bonobo_ServerInfoList *servers;
	EUri *uri;
	char *query;
	int i;


	/* Determine the protocol and query for factory supporting that */
	uri = e_uri_new (str_uri);
	if (!uri) {
		g_warning (G_STRLOC ": Invalid uri string");

		return FALSE;
	}

	query = "repo_ids.has ('IDL:GNOME/Evolution/DataServer/CalFactory:" API_VERSION "')";


	servers = bonobo_activation_query (query, NULL, NULL);

	e_uri_free (uri);

	if (!servers) {
		g_warning (G_STRLOC ": Unable to query for calendar factories");

		return FALSE;
	}

	/* Try to activate the servers for the protocol */
	for (i = 0; i < servers->_length; i++) {
		const Bonobo_ServerInfo *info;

		info = servers->_buffer + i;

#if 0
		g_message (G_STRLOC ": Activating calendar factory (%s)", info->iid);
#endif
		factory = bonobo_activation_activate_from_id (info->iid, 0, NULL, NULL);

		if (factory == CORBA_OBJECT_NIL)
			g_warning (G_STRLOC ": Could not activate calendar factory (%s)", info->iid);
		else
			*factories = g_list_append (*factories, factory);
	}

	CORBA_free (servers);

	return TRUE;
}

/* Object initialization function for the calendar ecal */
static void
e_cal_init (ECal *ecal, ECalClass *klass)
{
	ECalPrivate *priv;

	priv = g_new0 (ECalPrivate, 1);
	ecal->priv = priv;

	priv->load_state = E_CAL_LOAD_NOT_LOADED;
	priv->uri = NULL;
	priv->local_attachment_store = NULL;
	priv->mutex = g_mutex_new ();
	priv->listener = e_cal_listener_new (cal_set_mode_cb, ecal);

	priv->cal_address = NULL;
	priv->alarm_email_address = NULL;
	priv->ldap_attribute = NULL;
	priv->capabilities = FALSE;
	priv->factories = NULL;
	priv->timezones = g_hash_table_new (g_str_hash, g_str_equal);
	priv->default_zone = icaltimezone_get_utc_timezone ();
	priv->comp_listener = NULL;

	g_signal_connect (G_OBJECT (priv->listener), "read_only", G_CALLBACK (cal_read_only_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "cal_address", G_CALLBACK (cal_cal_address_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "alarm_address", G_CALLBACK (cal_alarm_address_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "ldap_attribute", G_CALLBACK (cal_ldap_attribute_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "static_capabilities", G_CALLBACK (cal_static_capabilities_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "open", G_CALLBACK (cal_opened_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "remove", G_CALLBACK (cal_removed_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "create_object", G_CALLBACK (cal_object_created_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "modify_object", G_CALLBACK (cal_object_modified_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "remove_object", G_CALLBACK (cal_object_removed_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "discard_alarm", G_CALLBACK (cal_alarm_discarded_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "receive_objects", G_CALLBACK (cal_objects_received_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "send_objects", G_CALLBACK (cal_objects_sent_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "default_object", G_CALLBACK (cal_default_object_requested_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "object", G_CALLBACK (cal_object_requested_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "object_list", G_CALLBACK (cal_object_list_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "attachment_list", G_CALLBACK (cal_attachment_list_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "get_timezone", G_CALLBACK (cal_get_timezone_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "add_timezone", G_CALLBACK (cal_add_timezone_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "set_default_timezone", G_CALLBACK (cal_set_default_timezone_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "get_changes", G_CALLBACK (cal_get_changes_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "get_free_busy", G_CALLBACK (cal_get_free_busy_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "query", G_CALLBACK (cal_query_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "backend_error", G_CALLBACK (backend_error_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "auth_required", G_CALLBACK (auth_required_cb), ecal);
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

	if (priv->listener) {
		e_cal_listener_stop_notification (priv->listener);
		bonobo_object_unref (priv->listener);
		priv->listener = NULL;
	}

	if (priv->comp_listener) {
		g_signal_handlers_disconnect_matched (G_OBJECT (priv->comp_listener),
						      G_SIGNAL_MATCH_DATA,
						      0, 0, NULL, NULL,
						      ecal);
		g_object_unref (G_OBJECT (priv->comp_listener));
		priv->comp_listener = NULL;
	}

	destroy_factories (ecal);
	destroy_cal (ecal);

	priv->load_state = E_CAL_LOAD_NOT_LOADED;

	if (priv->source) {
		g_object_unref (priv->source);
		priv->source = NULL;
	}

	if (priv->uri) {
		g_free (priv->uri);
		priv->uri = NULL;
	}

	if (priv->local_attachment_store) {
		g_free (priv->local_attachment_store);
		priv->local_attachment_store = NULL;
	}

	if (priv->mutex) {
		g_mutex_free (priv->mutex);
		priv->mutex = NULL;
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

	g_hash_table_foreach (priv->timezones, free_timezone, NULL);
	g_hash_table_destroy (priv->timezones);
	priv->timezones = NULL;

	g_free (priv);
	ecal->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Class initialization function for the calendar ecal */
static void
e_cal_class_init (ECalClass *klass)
{
	GObjectClass *object_class;

	object_class = (GObjectClass *) klass;

	parent_class = g_type_class_peek_parent (klass);

	e_cal_signals[CAL_OPENED] =
		g_signal_new ("cal_opened",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalClass, cal_opened),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
	e_cal_signals[CAL_SET_MODE] =
		g_signal_new ("cal_set_mode",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalClass, cal_set_mode),
			      NULL, NULL,
			      e_cal_marshal_VOID__ENUM_ENUM,
			      G_TYPE_NONE, 2,
			      E_CAL_SET_MODE_STATUS_ENUM_TYPE,
			      CAL_MODE_ENUM_TYPE);
	e_cal_signals[BACKEND_ERROR] =
		g_signal_new ("backend_error",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalClass, backend_error),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);
	e_cal_signals[BACKEND_DIED] =
		g_signal_new ("backend_died",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalClass, backend_died),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	klass->cal_opened = NULL;
	klass->backend_died = NULL;

	object_class->finalize = e_cal_finalize;
}

/**
 * e_cal_get_type:
 *
 * Registers the #ECal class if necessary, and returns the type ID assigned
 * to it.
 *
 * Return value: The type ID of the #ECal class.
 **/
GType
e_cal_get_type (void)
{
	static GType e_cal_type = 0;

	if (!e_cal_type) {
		static GTypeInfo info = {
                        sizeof (ECalClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_cal_class_init,
                        NULL, NULL,
                        sizeof (ECal),
                        0,
                        (GInstanceInitFunc) e_cal_init
                };
		e_cal_type = g_type_register_static (G_TYPE_OBJECT, "ECal", &info, 0);
	}

	return e_cal_type;
}


static gboolean
fetch_corba_cal (ECal *ecal, ESource *source, ECalSourceType type)
{
	ECalPrivate *priv;
	GList *f;
	CORBA_Environment ev;
	gchar *source_xml;
	gchar *str_uri;
	gboolean result = FALSE;

	priv = ecal->priv;
	g_return_val_if_fail (priv->load_state == E_CAL_LOAD_NOT_LOADED, FALSE);
	g_assert (priv->uri == NULL);
	g_return_val_if_fail (source != NULL, FALSE);

	str_uri = e_source_get_uri (source);
	if (!str_uri)
		return FALSE;

	if (!get_factories (str_uri, &priv->factories)) {
		g_free (str_uri);
		return FALSE;
	}

	g_object_ref (source);
	priv->source = source;

	priv->uri = g_strdup (str_uri);
	priv->type = type;

	source_xml = e_source_to_standalone_xml (source);

	for (f = priv->factories; f; f = f->next) {
		GNOME_Evolution_Calendar_Cal cal;

		CORBA_exception_init (&ev);

		cal = GNOME_Evolution_Calendar_CalFactory_getCal (f->data, source_xml, convert_type (priv->type),
								  BONOBO_OBJREF (priv->listener), &ev);
		if (BONOBO_EX (&ev))
			continue;

		priv->cal = cal;

		result = TRUE;
		break;
	}

	g_free (str_uri);
	g_free (source_xml);
	return result;
}

/* one-time start up for libecal */
static void
e_cal_activate ()
{
	static GStaticMutex e_cal_lock = G_STATIC_MUTEX_INIT;
	static gboolean activated = FALSE;

	g_static_mutex_lock (&e_cal_lock);
	if (!activated) {
		activated = TRUE;

		if (!bonobo_is_initialized ())
			bonobo_init (NULL, NULL);
	}
	g_static_mutex_unlock (&e_cal_lock);
}


/* TODO - For now, the policy of where each backend serializes its
 * attachment data is hardcoded below. Should this end up as a
 * gconf key set during the account creation  and fetched
 * from eds???
 */
static void
set_local_attachment_store (ECal *ecal)
{
	ECalPrivate *priv;
	char *mangled_uri;
	int i;

	priv = ecal->priv;
	mangled_uri = g_strdup (priv->uri);
	/* mangle the URI to not contain invalid characters */
	for (i = 0; i < strlen (mangled_uri); i++) {
		switch (mangled_uri[i]) {
		case ':' :
		case '/' :
			mangled_uri[i] = '_';
		}
	}

	/* the file backend uses its uri as the attachment store*/
	if (g_str_has_prefix (priv->uri, "file://")) {
		priv->local_attachment_store = g_strdup (priv->uri);
	} else if (g_str_has_prefix (priv->uri, "groupwise://")) {
		/* points to the location of the cache*/
		gchar *filename = g_build_filename (g_get_home_dir (),
						    ".evolution/cache/calendar",
						    mangled_uri,
						    NULL);
		priv->local_attachment_store =
			g_filename_to_uri (filename, NULL, NULL);
		g_free (filename);
	} else if (g_str_has_prefix (priv->uri, "exchange://")) {
		gchar *filename = g_build_filename (g_get_home_dir (),
						    ".evolution/exchange",
						    mangled_uri,
						    NULL);
		priv->local_attachment_store =
			g_filename_to_uri (filename, NULL, NULL);
		g_free (filename);
	} else if (g_str_has_prefix (priv->uri, "scalix://")) {
		gchar *filename = g_build_filename (g_get_home_dir (),
						    ".evolution/cache/scalix",
						    mangled_uri,
						    "attach",
						    NULL);
                priv->local_attachment_store =
			g_filename_to_uri (filename, NULL, NULL);
		g_free (filename);
        } else if (g_str_has_prefix (priv->uri, "google://")) {
		gchar *filename = g_build_filename (g_get_home_dir (),
						    ".evolution/cache/calendar",
						    mangled_uri,
						    NULL);
		priv->local_attachment_store =
			g_filename_to_uri (filename, NULL, NULL);
		g_free (filename);
	}
	g_free (mangled_uri);
}

/**
 * e_cal_new:
 * @source: An #ESource to be used for the client.
 * @type: Type of the client.
 *
 * Creates a new calendar client. This does not open the calendar itself,
 * for that, #e_cal_open or #e_cal_open_async needs to be called.
 *
 * Return value: A newly-created calendar client, or NULL if the client could
 * not be constructed because it could not contact the calendar server.
 **/
ECal *
e_cal_new (ESource *source, ECalSourceType type)
{
	ECal *ecal;

	e_cal_activate ();

	ecal = g_object_new (E_TYPE_CAL, NULL);

	if (!fetch_corba_cal (ecal, source, type)) {
		g_object_unref (ecal);

		return NULL;
	}

	/* Set the local attachment store path for the calendar */
	set_local_attachment_store (ecal);

	/* initialize component listener */
	ecal->priv->comp_listener = e_component_listener_new ((Bonobo_Unknown) ecal->priv->cal);
	g_signal_connect (G_OBJECT (ecal->priv->comp_listener), "component_died",
			  G_CALLBACK (backend_died_cb), ecal);

	return ecal;
}

/**
 * e_cal_new_from_uri:
 * @uri: The URI pointing to the calendar to open.
 * @type: Type of the client.
 *
 * Creates a new calendar client. This does not open the calendar itself,
 * for that, #e_cal_open or #e_cal_open_async needs to be called.
 *
 * Return value: A newly-created calendar client, or NULL if the client could
 * not be constructed because it could not contact the calendar server.
 **/
ECal *
e_cal_new_from_uri (const gchar *uri, ECalSourceType type)
{
	ESource *source;
	ECal *cal;

	source = e_source_new_with_absolute_uri ("", uri);
	cal = e_cal_new (source, type);

	g_object_unref (source);

	return cal;
}

/**
 * e_cal_new_system_calendar:
 *
 * Create a calendar client for the system calendar, which should always be present in
 * all Evolution installations. This does not open the calendar itself,
 * for that, #e_cal_open or #e_cal_open_async needs to be called.
 *
 * Return value: A newly-created calendar client, or NULL if the client could
 * not be constructed.
 */
ECal *
e_cal_new_system_calendar (void)
{
	ECal *ecal;
	char *filename;
	char *uri;

	filename = g_build_filename (g_get_home_dir (),
				     ".evolution/calendar/local/system",
				     NULL);
	uri = g_filename_to_uri (filename, NULL, NULL);
	g_free (filename);
	ecal = e_cal_new_from_uri (uri, E_CAL_SOURCE_TYPE_EVENT);
	g_free (uri);

	return ecal;
}

/**
 * e_cal_new_system_tasks:
 *
 * Create a calendar client for the system task list, which should always be present in
 * all Evolution installations. This does not open the tasks list itself,
 * for that, #e_cal_open or #e_cal_open_async needs to be called.
 *
 * Return value: A newly-created calendar client, or NULL if the client could
 * not be constructed.
 */
ECal *
e_cal_new_system_tasks (void)
{
	ECal *ecal;
	char *filename;
	char *uri;

	filename = g_build_filename (g_get_home_dir (),
				     ".evolution/tasks/local/system",
				     NULL);
	uri = g_filename_to_uri (filename, NULL, NULL);
	g_free (filename);
	ecal = e_cal_new_from_uri (uri, E_CAL_SOURCE_TYPE_TODO);
	g_free (uri);

	return ecal;
}

/**
 * e_cal_new_system_memos:
 *
 * Create a calendar client for the system memos, which should always be present
 * in all Evolution installations. This does not open the memos itself, for
 * that, #e_cal_open or #e_cal_open_async needs to be called.
 *
 * Return value: A newly-created calendar client, or NULL if the client could
 * not be constructed.
 */
ECal *
e_cal_new_system_memos (void)
{
	ECal *ecal;
	char *uri;
	char *filename;

	filename = g_build_filename (g_get_home_dir (),
				     ".evolution/memos/local/system",
				     NULL);
	uri = g_filename_to_uri (filename, NULL, NULL);
	g_free (filename);
	ecal = e_cal_new_from_uri (uri, E_CAL_SOURCE_TYPE_JOURNAL);
	g_free (uri);

	return ecal;
}

/**
 * e_cal_set_auth_func
 * @ecal: A calendar client.
 * @func: The authentication function
 * @data: User data to be used when calling the authentication function
 *
 * Sets the given authentication function on the calendar client. This
 * function will be called any time the calendar server needs a
 * password for an operation associated with the calendar and should
 * be supplied before any calendar is opened.
 *
 * When a calendar is opened asynchronously, the open function is
 * processed in a concurrent thread.  This means that the
 * authentication function will also be called from this thread.  As
 * such, the authentication callback cannot directly call any
 * functions that must be called from the main thread.  For example
 * any Gtk+ related functions, which must be proxied synchronously to
 * the main thread by the callback.
 *
 * The authentication function has the following signature
 * (ECalAuthFunc):
 *	char * auth_func (ECal *ecal,
 *			  const gchar *prompt,
 *			  const gchar *key,
 *			  gpointer user_data)
 */
void
e_cal_set_auth_func (ECal *ecal, ECalAuthFunc func, gpointer data)
{
	g_return_if_fail (ecal != NULL);
	g_return_if_fail (E_IS_CAL (ecal));

	ecal->priv->auth_func = func;
	ecal->priv->auth_user_data = data;
}

static char *
build_proxy_pass_key (ECal *ecal, const char* parent_user)
{
	char *euri_str;
	const char *uri;
	EUri *euri;

	uri = e_cal_get_uri (ecal);

	euri = e_uri_new (uri);
	g_free (euri->user);
	euri->user = g_strdup(parent_user);

	euri_str = e_uri_to_string (euri, FALSE);

	e_uri_free (euri);
	return euri_str;
}

static char *
build_pass_key (ECal *ecal)
{
	char *euri_str;
	const char *uri;
	EUri *euri;

	uri = e_cal_get_uri (ecal);

	euri = e_uri_new (uri);
	euri_str = e_uri_to_string (euri, FALSE);

	e_uri_free (euri);
	return euri_str;
}

static gboolean
open_calendar (ECal *ecal, gboolean only_if_exists, GError **error, ECalendarStatus *status, gboolean needs_auth)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarOp *our_op;
	char *username = NULL, *auth_type = NULL, *password = NULL;
	gboolean read_only = FALSE;

	e_return_error_if_fail (ecal != NULL, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;

	g_mutex_lock (ecal->priv->mutex);

	if (!needs_auth && priv->load_state == E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		return TRUE;
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		*status = E_CALENDAR_STATUS_BUSY;
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}
	/* start the open operation */
	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (priv->mutex);

	/* see if the backend needs authentication */
	if ( (priv->mode !=  CAL_MODE_LOCAL) && e_source_get_property (priv->source, "auth")) {
		char *prompt, *key;
		char *parent_user;

		priv->load_state = E_CAL_LOAD_AUTHENTICATING;

		if (priv->auth_func == NULL) {
			e_calendar_remove_op (ecal, our_op);
			e_calendar_free_op (our_op);
			priv->load_state = E_CAL_LOAD_NOT_LOADED;
			*status = E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED;
			E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED, error);
		}

		username = e_source_get_duped_property (priv->source, "username");
		if (!username) {
			e_calendar_remove_op (ecal, our_op);
			e_calendar_free_op (our_op);
			priv->load_state = E_CAL_LOAD_NOT_LOADED;
			*status = E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED;
			E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED, error);
		}

		prompt = g_strdup_printf (_("Enter password for %s (user %s)"),
				e_source_peek_name (priv->source), username);

		auth_type = e_source_get_duped_property (priv->source, "auth-type");
		if (auth_type)
			key = build_pass_key (ecal);
		else {
			parent_user = e_source_get_duped_property (priv->source, "parent_id_name");
			if (parent_user) {
				key = build_proxy_pass_key (ecal, parent_user);
				/*
				   This password prompt will be prompted rarely. Since the key that is passed to
				   the auth_func corresponds to the parent user.
				 */
				prompt = g_strdup_printf (_("Enter password for %s to enable proxy for user %s"), e_source_peek_name (priv->source), parent_user);
				g_free (parent_user);
			} else
				key = g_strdup (e_cal_get_uri (ecal));
		}
		g_free (auth_type);

		if (!key) {
			e_calendar_remove_op (ecal, our_op);
			e_calendar_free_op (our_op);
			priv->load_state = E_CAL_LOAD_NOT_LOADED;
			*status = E_CALENDAR_STATUS_URI_NOT_LOADED;
			E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED, error);
		}

		password = priv->auth_func (ecal, prompt, key, priv->auth_user_data);

		if (!password) {
			e_calendar_remove_op (ecal, our_op);
			e_calendar_free_op (our_op);
			priv->load_state = E_CAL_LOAD_NOT_LOADED;
			*status = E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED;
			E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED, error);
		}

		g_free (prompt);
		g_free (key);
	}


	CORBA_exception_init (&ev);

	priv->load_state = E_CAL_LOAD_LOADING;

	GNOME_Evolution_Calendar_Cal_open (priv->cal, only_if_exists,
					   username ? username : "",
					   password ? password : "",
					   &ev);
	g_free (password);
	g_free (username);

	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		*status = E_CALENDAR_STATUS_CORBA_EXCEPTION;
		priv->load_state = E_CAL_LOAD_NOT_LOADED;
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	*status = our_op->status;

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);
	if (*status == E_CALENDAR_STATUS_OK) {
		priv->load_state = E_CAL_LOAD_LOADED;
		if (get_read_only (ecal, &read_only, NULL))
			priv->read_only = read_only;
	} else
		priv->load_state = E_CAL_LOAD_NOT_LOADED;

	E_CALENDAR_CHECK_STATUS (*status, error);
}

/**
 * e_cal_open
 * @ecal: A calendar client.
 * @only_if_exists: FALSE if the calendar should be opened even if there
 * was no storage for it, i.e. to create a new calendar or load an existing
 * one if it already exists.  TRUE if it should only try to load calendars
 * that already exist.
 * @error: Placeholder for error information.
 *
 * Makes a calendar client initiate a request to open a calendar.  The calendar
 * client will emit the "cal_opened" signal when the response from the server is
 * received.
 *
 * Return value: TRUE on success, FALSE on failure to issue the open request.
 **/
gboolean
e_cal_open (ECal *ecal, gboolean only_if_exists, GError **error)
{
	ECalendarStatus status;
	gboolean result;

	result = open_calendar (ecal, only_if_exists, error, &status, FALSE);
	g_signal_emit (G_OBJECT (ecal), e_cal_signals[CAL_OPENED], 0, status);

	return result;
}

typedef struct {
	ECal *ecal;
	gboolean exists;
	ECalendarStatus status;
	const char *auth_prompt;
	const char *auth_key;
	char *password;
} ECalAsyncData;

static gboolean
async_signal_idle_cb (gpointer data)
{
	ECalAsyncData *ccad = data;

	g_signal_emit (G_OBJECT (ccad->ecal), e_cal_signals[CAL_OPENED], 0, ccad->status);

	/* free memory */
	g_object_unref (ccad->ecal);
	g_free (ccad);

	return FALSE;
}

static gpointer
open_async (gpointer data)
{
	ECalAsyncData *ccad = data;

	open_calendar (ccad->ecal, ccad->exists, NULL, &ccad->status, FALSE);
	g_idle_add ((GSourceFunc) async_signal_idle_cb, ccad);

	return NULL;
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
 *
 * Because this operation runs in another thread, any authentication
 * callback set on the calendar will be called from this other thread.
 * See #e_cal_set_auth_func() for details.
 **/
void
e_cal_open_async (ECal *ecal, gboolean only_if_exists)
{
	ECalAsyncData *ccad;
	GThread *thread;
	GError *error = NULL;
	g_return_if_fail (ecal != NULL);
	g_return_if_fail (E_IS_CAL (ecal));

	switch (ecal->priv->load_state) {
	case E_CAL_LOAD_AUTHENTICATING :
	case E_CAL_LOAD_LOADING :
		g_signal_emit (G_OBJECT (ecal), e_cal_signals[CAL_OPENED], 0, E_CALENDAR_STATUS_BUSY);
		return;
	case E_CAL_LOAD_LOADED :
		g_signal_emit (G_OBJECT (ecal), e_cal_signals[CAL_OPENED], 0, E_CALENDAR_STATUS_OK);
		return;
	default:
		/* ignore everything else */
		break;
	}

	ccad = g_new0 (ECalAsyncData, 1);
	ccad->ecal = g_object_ref (ecal);
	ccad->exists = only_if_exists;

	/* spawn a new thread for opening the calendar */
	thread = g_thread_create ((GThreadFunc) open_async, ccad, FALSE, &error);
	if (!thread) {
		g_warning (G_STRLOC ": %s", error->message);
		g_error_free (error);

		/* notify listeners of the error */
		g_signal_emit (G_OBJECT (ecal), e_cal_signals[CAL_OPENED], 0, E_CALENDAR_STATUS_OTHER_ERROR);
	}
}

/**
 * e_cal_remove:
 * @ecal: A calendar client.
 * @error: Placeholder for error information.
 *
 * Removes a calendar.
 *
 * Return value: TRUE if the calendar was removed, FALSE if there was an error.
 */
gboolean
e_cal_remove (ECal *ecal, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;

	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	priv = ecal->priv;

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);


	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_remove (priv->cal, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

#if 0
/* Builds an URI list out of a CORBA string sequence */
static GList *
build_uri_list (GNOME_Evolution_Calendar_StringSeq *seq)
{
	GList *uris = NULL;
	int i;

	for (i = 0; i < seq->_length; i++)
		uris = g_list_prepend (uris, g_strdup (seq->_buffer[i]));

	return uris;
}
#endif

/**
 * e_cal_uri_list:
 * @ecal: A calendar client.
 * @mode: Mode of the URIs to get.
 *
 * Retrieves a list of all calendar clients for the given mode.
 *
 * Return value: list of uris.
 */
GList *
e_cal_uri_list (ECal *ecal, CalMode mode)
{
#if 0
	ECalPrivate *priv;
	GNOME_Evolution_Calendar_StringSeq *uri_seq;
	GList *uris = NULL;
	CORBA_Environment ev;
	GList *f;

	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

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
 * Return value: an #ECalSourceType value corresponding to the type
 * of the calendar client.
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
 * Return value: A #ECalLoadState value indicating whether the client has
 * not been loaded with #e_cal_open yet, whether it is being
 * loaded, or whether it is already loaded.
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
 * e_cal_get_source:
 * @ecal: A calendar client.
 *
 * Queries the source that is open in a calendar client.
 *
 * Return value: The source of the calendar that is already loaded or is being
 * loaded, or NULL if the ecal has not started a load request yet.
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
 * e_cal_get_uri:
 * @ecal: A calendar client.
 *
 * Queries the URI that is open in a calendar client.
 *
 * Return value: The URI of the calendar that is already loaded or is being
 * loaded, or NULL if the client has not started a load request yet.
 **/
const char *
e_cal_get_uri (ECal *ecal)
{
	ECalPrivate *priv;

	g_return_val_if_fail (ecal != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL (ecal), NULL);

	priv = ecal->priv;
	return priv->uri;
}

/**
 * e_cal_get_local_attachment_store
 * @ecal: A calendar client.
 *
 * Queries the URL where the calendar attachments are
 * serialized in the local filesystem. This enable clients
 * to operate with the reference to attachments rather than the data itself
 * unless it specifically uses the attachments for open/sending
 * operations.
 *
 * Return value: The URL where the attachments are serialized in the
 * local filesystem.
 **/
const char *
e_cal_get_local_attachment_store (ECal *ecal)
{
	ECalPrivate *priv;

	g_return_val_if_fail (ecal != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL (ecal), NULL);

	priv = ecal->priv;
	return (const char *)priv->local_attachment_store;
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
 * Return value: TRUE if the call was successful, FALSE if there was an error.
 */
gboolean
e_cal_is_read_only (ECal *ecal, gboolean *read_only, GError **error)
{
	ECalPrivate *priv;

	if (!(ecal && E_IS_CAL (ecal)))
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_INVALID_ARG, error);

	priv = ecal->priv;
	*read_only = priv->read_only;

	return TRUE;
}

static gboolean
get_read_only (ECal *ecal, gboolean *read_only, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;

	priv = ecal->priv;
	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	/* set it to true so that op does not emit cond signals for all notifications
	   from the backend */
	our_op->bool = TRUE;

	g_mutex_unlock (ecal->priv->mutex);


	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_isReadOnly (priv->cal, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;

	if (status == E_CALENDAR_STATUS_OK)
		*read_only = our_op->bool;

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);
	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_cal_address:
 * @ecal: A calendar client.
 * @cal_address: Return value for address information.
 * @error: Placeholder for error information.
 *
 * Queries the calendar address associated with a calendar client.
 *
 * Return value: TRUE if the operation was successful, FALSE if there
 * was an error.
 **/
gboolean
e_cal_get_cal_address (ECal *ecal, char **cal_address, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;


	if (!(ecal && E_IS_CAL (ecal)))
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_INVALID_ARG, error);
	priv = ecal->priv;
	if (priv->cal_address == NULL) {
		g_mutex_lock (ecal->priv->mutex);

		if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
			g_mutex_unlock (ecal->priv->mutex);
			E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
		}

		if (ecal->priv->current_op != NULL) {
			g_mutex_unlock (ecal->priv->mutex);
			E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
		}

		our_op = e_calendar_new_op (ecal);

		g_mutex_unlock (ecal->priv->mutex);


		CORBA_exception_init (&ev);

		GNOME_Evolution_Calendar_Cal_getCalAddress (priv->cal, &ev);
		if (BONOBO_EX (&ev)) {
			e_calendar_remove_op (ecal, our_op);
			e_calendar_free_op (our_op);

			CORBA_exception_free (&ev);

			g_warning (G_STRLOC ": Unable to contact backend");

			E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
		}

		CORBA_exception_free (&ev);

		e_flag_wait (our_op->done);

		status = our_op->status;
		*cal_address = our_op->string;
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		E_CALENDAR_CHECK_STATUS (status, error);
	} else {
		*cal_address = g_strdup (priv->cal_address);

		return TRUE;
	}
}

/**
 * e_cal_get_alarm_email_address:
 * @ecal: A calendar client.
 * @alarm_address: Return value for alarm address.
 * @error: Placeholder for error information.
 *
 * Queries the address to be used for alarms in a calendar client.
 *
 * Return value: TRUE if the operation was successful, FALSE if there was
 * an error while contacting the backend.
 */
gboolean
e_cal_get_alarm_email_address (ECal *ecal, char **alarm_address, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);


	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getAlarmEmailAddress (priv->cal, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;
	*alarm_address = our_op->string;

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_ldap_attribute:
 * @ecal: A calendar client.
 * @ldap_attribute: Return value for the LDAP attribute.
 * @error: Placeholder for error information.
 *
 * Queries the LDAP attribute for a calendar client.
 *
 * Return value: TRUE if the call was successful, FALSE if there was an
 * error contacting the backend.
 */
gboolean
e_cal_get_ldap_attribute (ECal *ecal, char **ldap_attribute, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);


	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getLdapAttribute (priv->cal, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;
	*ldap_attribute = our_op->string;

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

static gboolean
load_static_capabilities (ECal *ecal, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;

	priv = ecal->priv;

	if (priv->capabilities)
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);


	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getStaticCapabilities (priv->cal, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;
	priv->capabilities = our_op->string;

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

static gboolean
check_capability (ECal *ecal, const char *cap)
{
	ECalPrivate *priv;

	priv = ecal->priv;

	/* FIXME Check result */
	load_static_capabilities (ecal, NULL);
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
 * Return value: TRUE if the calendar allows only one alarm, FALSE otherwise.
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
 * Return value: TRUE if the calendar forces organizers to attend meetings,
 * FALSE otherwise.
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
 * Return value: TRUE if the calendar has a master object for recurrences,
 * FALSE otherwise.
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
 * Return value: TRUE if the capability is supported, FALSE otherwise.
 */
gboolean
e_cal_get_static_capability (ECal *ecal, const char *cap)
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
 * Return value: TRUE if it saves schedules, FALSE otherwise.
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
 * Return value: TRUE if the calendar requires organizers to accept, FALSE
 * otherwise.
 */
gboolean
e_cal_get_organizer_must_accept (ECal *ecal)
{
	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	return check_capability (ecal, CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ACCEPT);
}

/**
 * e_cal_set_mode:
 * @ecal: A calendar client.
 * @mode: Mode to switch to.
 *
 * Switches online/offline mode on the calendar.
 *
 * Return value: TRUE if the switch was successful, FALSE if there was an error.
 */
gboolean
e_cal_set_mode (ECal *ecal, CalMode mode)
{
	ECalPrivate *priv;
	gboolean retval = TRUE;
	CORBA_Environment ev;

	g_return_val_if_fail (ecal != NULL, -1);
	g_return_val_if_fail (E_IS_CAL (ecal), -1);

	priv = ecal->priv;
	g_return_val_if_fail (priv->load_state == E_CAL_LOAD_LOADED, -1);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_Cal_setMode (priv->cal, mode, &ev);

	if (BONOBO_EX (&ev))
		retval = FALSE;

	CORBA_exception_free (&ev);

	return retval;
}

/* This is used in the callback which fetches all the timezones needed for an
   object. */
typedef struct _ECalGetTimezonesData ECalGetTimezonesData;
struct _ECalGetTimezonesData {
	ECal *ecal;

	/* This starts out at E_CALENDAR_STATUS_OK. If an error occurs this
	   contains the last error. */
	ECalendarStatus status;
};

/**
 * e_cal_get_default_object:
 * @ecal: A calendar client.
 * @icalcomp: Return value for the default object.
 * @error: Placeholder for error information.
 *
 * Retrives an #icalcomponent from the backend that contains the default
 * values for properties needed.
 *
 * Return value: TRUE if the call was successful, FALSE otherwise.
 */
gboolean
e_cal_get_default_object (ECal *ecal, icalcomponent **icalcomp, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getDefaultObject (priv->cal, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;
        if (status != E_CALENDAR_STATUS_OK) {
                *icalcomp = NULL;
        } else {
                *icalcomp = icalparser_parse_string (our_op->string);
		if (!(*icalcomp))
			status = E_CALENDAR_STATUS_INVALID_OBJECT;
	}
	g_free (our_op->string);

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_attachments_for_comp:
 * @ecal: A calendar client.
 * @uid: Unique identifier for a calendar component.
 * @rid: Recurrence identifier.
 * @list: Return the list of attachment uris.
 * @error: Placeholder for error information.
 *
 * Queries a calendar for a calendar component object based on its unique
 * identifier and gets the attachments for the component.
 *
 * Return value: TRUE if the call was successful, FALSE otherwise.
 **/
gboolean
e_cal_get_attachments_for_comp (ECal *ecal, const char *uid, const char *rid, GSList **list, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_Cal_getAttachmentList (priv->cal, uid, rid ? rid : "", &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;
        if (status != E_CALENDAR_STATUS_OK){
                *list = NULL;
        } else {
		*list = our_op->slist;
	}

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_object:
 * @ecal: A calendar client.
 * @uid: Unique identifier for a calendar component.
 * @rid: Recurrence identifier.
 * @icalcomp: Return value for the calendar component object.
 * @error: Placeholder for error information.
 *
 * Queries a calendar for a calendar component object based on its unique
 * identifier.
 *
 * Return value: TRUE if the call was successful, FALSE otherwise.
 **/
gboolean
e_cal_get_object (ECal *ecal, const char *uid, const char *rid, icalcomponent **icalcomp, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getObject (priv->cal, uid, rid ? rid : "", &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;
        if (status != E_CALENDAR_STATUS_OK){
                *icalcomp = NULL;
        } else {
		icalcomponent *tmp_icalcomp;
		icalcomponent_kind kind;

		tmp_icalcomp = icalparser_parse_string (our_op->string);
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
	}
	g_free (our_op->string);

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_objects_for_uid:
 * @ecal: A calendar client.
 * @uid: Unique identifier for a calendar component.
 * @objects: Return value for the list of objects obtained from the backend.
 * @error: Placeholder for error information.
 *
 * Queries a calendar for all calendar components with the given unique
 * ID. This will return any recurring event and all its detached recurrences.
 * For non-recurring events, it will just return the object with that ID.
 *
 * Return value: TRUE if the call was successful, FALSE otherwise.
 **/
gboolean
e_cal_get_objects_for_uid (ECal *ecal, const char *uid, GList **objects, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;
	*objects = NULL;

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getObject (priv->cal, uid, "", &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;
        if (status != E_CALENDAR_STATUS_OK){
                *objects = NULL;
        } else {
		icalcomponent *icalcomp;
		icalcomponent_kind kind, kind_to_find;

		switch (priv->type) {
		case E_CAL_SOURCE_TYPE_EVENT :
			kind_to_find = ICAL_VEVENT_COMPONENT;
			break;
		case E_CAL_SOURCE_TYPE_TODO :
			kind_to_find = ICAL_VTODO_COMPONENT;
			break;
		case E_CAL_SOURCE_TYPE_JOURNAL :
			kind_to_find = ICAL_VJOURNAL_COMPONENT;
			break;
		default:
			/* ignore everything else */
			kind_to_find = ICAL_NO_COMPONENT;
			break;
		}

				icalcomp = icalparser_parse_string (our_op->string);
		if (!icalcomp) {
			status = E_CALENDAR_STATUS_INVALID_OBJECT;
			*objects = NULL;
		} else {
			ECalComponent *comp;

			kind = icalcomponent_isa (icalcomp);
			if (kind == kind_to_find) {
				comp = e_cal_component_new ();
				e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp));
				*objects = g_list_append (NULL, comp);
			} else if (kind == ICAL_VCALENDAR_COMPONENT) {
				icalcomponent *subcomp;

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
	g_free (our_op->string);

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_resolve_tzid_cb:
 * @tzid: ID of the timezone to resolve.
 * @data: Closure data for the callback.
 *
 * Resolves TZIDs for the recurrence generator.
 *
 * Return value: The timezone identified by the @tzid argument, or %NULL if
 * it could not be found.
 */
icaltimezone*
e_cal_resolve_tzid_cb (const char *tzid, gpointer data)
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
 * e_cal_get_changes:
 * @ecal: A calendar client.
 * @change_id: ID to use for comparing changes.
 * @changes: Return value for the list of changes.
 * @error: Placeholder for error information.
 *
 * Returns a list of changes made to the calendar since a specific time. That time
 * is identified by the @change_id argument, which is used by the backend to
 * compute the changes done.
 *
 * Return value: TRUE if the call was successful, FALSE otherwise.
 */
gboolean
e_cal_get_changes (ECal *ecal, const char *change_id, GList **changes, GError **error)
{
	CORBA_Environment ev;
	ECalendarOp *our_op;
	ECalendarStatus status;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (change_id, E_CALENDAR_STATUS_INVALID_ARG);

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getChanges (ecal->priv->cal, change_id, &ev);

	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;
	*changes = our_op->list;

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_free_change_list:
 * @list: List of changes to be freed.
 *
 * Free a list of changes as returned by #e_cal_get_changes.
 */
void
e_cal_free_change_list (GList *list)
{
	ECalChange *c;
	GList *l;

	for (l = list; l; l = l->next) {
		c = l->data;

		g_assert (c != NULL);
		g_assert (c->comp != NULL);

		g_object_unref (G_OBJECT (c->comp));
		g_free (c);
	}

	g_list_free (list);
}

/**
 * e_cal_get_object_list:
 * @ecal: A calendar client.
 * @query: Query string.
 * @objects: Return value for list of objects.
 * @error: Placeholder for error information.
 *
 * Gets a list of objects from the calendar that match the query specified
 * by the @query argument. The objects will be returned in the @objects
 * argument, which is a list of #icalcomponent. When done, this list
 * should be freed by using the #e_cal_free_object_list function.
 *
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 **/
gboolean
e_cal_get_object_list (ECal *ecal, const char *query, GList **objects, GError **error)
{
	CORBA_Environment ev;
	ECalendarOp *our_op;
	ECalendarStatus status;


	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (query, E_CALENDAR_STATUS_INVALID_ARG);

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getObjectList (ecal->priv->cal, query, &ev);

	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;
	*objects = our_op->list;

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_object_list_as_comp:
 * @ecal: A calendar client.
 * @query: Query string.
 * @objects: Return value for list of objects.
 * @error: Placeholder for error information.
 *
 * Gets a list of objects from the calendar that match the query specified
 * by the @query argument. The objects will be returned in the @objects
 * argument, which is a list of #ECalComponent.
 *
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_get_object_list_as_comp (ECal *ecal, const char *query, GList **objects, GError **error)
{
	GList *ical_objects = NULL;
	GList *l;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (query, E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (objects, E_CALENDAR_STATUS_INVALID_ARG);

	if (!e_cal_get_object_list (ecal, query, &ical_objects, error))
		return FALSE;

	*objects = NULL;
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
 * e_cal_free_object_list:
 * @objects: List of objects to be freed.
 *
 * Frees a list of objects as returned by #e_cal_get_object_list.
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
 * e_cal_get_free_busy
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
 */
gboolean
e_cal_get_free_busy (ECal *ecal, GList *users, time_t start, time_t end,
		     GList **freebusy, GError **error)
{
	CORBA_Environment ev;
	ECalendarOp *our_op;
	ECalendarStatus status;
	GNOME_Evolution_Calendar_UserList corba_users;
	GList *l;
	int i, len;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);

	/* create the CORBA user list to be passed to the backend */
	len = g_list_length (users);

	corba_users._length = len;
	corba_users._buffer = CORBA_sequence_GNOME_Evolution_Calendar_User_allocbuf (len);

	for (l = users, i = 0; l; l = l->next, i++)
		corba_users._buffer[i] = CORBA_string_dup (l->data);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getFreeBusy (ecal->priv->cal, &corba_users, start, end, &ev);

	CORBA_free (corba_users._buffer);

	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;
	*freebusy = our_op->list;

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
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
add_instance (ECalComponent *comp, time_t start, time_t end, gpointer data)
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

			e_cal_component_get_dtstart (comp, &datetime);

			if (instances_hold->start_zone)
				itt = icaltime_from_timet_with_zone (start, datetime.value->is_date, instances_hold->start_zone);
			else {
				itt = icaltime_from_timet (start, datetime.value->is_date);

				if (datetime.tzid) {
					g_free ((char *) datetime.tzid);
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
				g_free ((char *) datetime.tzid);
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
compare_comp_instance (gconstpointer a, gconstpointer b)
{
	const struct comp_instance *cia, *cib;
	time_t diff;

	cia = a;
	cib = b;

	diff = cia->start - cib->start;
	return (diff < 0) ? -1 : (diff > 0) ? 1 : 0;
}

static GList *
process_detached_instances (GList *instances, GList *detached_instances)
{
	struct comp_instance *ci, *cid;
	GList *dl, *unprocessed_instances = NULL;

	for (dl = detached_instances; dl != NULL; dl = dl->next) {
		GList *il;
		const char *uid;
		gboolean processed;
		ECalComponentRange recur_id, instance_recur_id;

		processed = FALSE;

		cid = dl->data;
		e_cal_component_get_uid (cid->comp, &uid);
		e_cal_component_get_recurid (cid->comp, &recur_id);

		/* search for coincident instances already expanded */
		for (il = instances; il != NULL; il = il->next) {
			const char *instance_uid;
			int cmp;

			ci = il->data;
			e_cal_component_get_uid (ci->comp, &instance_uid);
			e_cal_component_get_recurid (ci->comp, &instance_recur_id);
			if (strcmp (uid, instance_uid) == 0) {
				char *i_rid = NULL, *d_rid = NULL;

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
						g_log (G_LOG_DOMAIN,
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
					cmp = icaltime_compare (*instance_recur_id.datetime.value,
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
generate_instances (ECal *ecal, time_t start, time_t end, const char *uid,
		    ECalRecurInstanceFn cb, gpointer cb_data)
{
	GList *objects = NULL;
	GList *instances, *detached_instances = NULL;
	GList *l;
	char *query;
	char *iso_start, *iso_end;
	ECalPrivate *priv;

	priv = ecal->priv;

	/* Generate objects */
	if (uid && *uid) {
		if (!e_cal_get_objects_for_uid (ecal, uid, &objects, NULL))
			return;
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

		query = g_strdup_printf ("(occur-in-time-range? (make-time \"%s\") (make-time \"%s\"))",
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
			e_cal_resolve_tzid_cb to resolve the TZID.
			For DATE values and DATE-TIME values without a
			TZID (i.e. floating times) we use the default
			timezone. */
			if (dtstart.tzid && !dtstart.value->is_date) {
				start_zone = e_cal_resolve_tzid_cb (dtstart.tzid, ecal);
				if (!start_zone)
					start_zone = default_zone;
			} else {
				start_zone = default_zone;
			}

			if (dtend.tzid && !dtend.value->is_date) {
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

			detached_instances = g_list_prepend (detached_instances, ci);
		} else {
			ECalComponentDateTime datetime;
			icaltimezone *start_zone;
			struct instances_info *instances_hold;

			/* Get the start timezone */
			e_cal_component_get_dtstart (comp, &datetime);
			e_cal_get_timezone (ecal, datetime.tzid, &start_zone, NULL);
			e_cal_component_free_datetime (&datetime);

			instances_hold = g_new0 (struct instances_info, 1);
			instances_hold->instances = &instances;
			instances_hold->start_zone = start_zone;

			e_cal_recur_generate_instances (comp, start, end, add_instance, instances_hold,
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
 * e_cal_generate_instances:
 * @ecal: A calendar client.
 * @start: Start time for query.
 * @end: End time for query.
 * @cb: Callback for each generated instance.
 * @cb_data: Closure data for the callback.
 *
 * Does a combination of #e_cal_get_object_list () and
 * #e_cal_recur_generate_instances().
 *
 * The callback function should do a g_object_ref() of the calendar component
 * it gets passed if it intends to keep it around, since it will be unref'ed
 * as soon as the callback returns.
 **/
void
e_cal_generate_instances (ECal *ecal, time_t start, time_t end,
			  ECalRecurInstanceFn cb, gpointer cb_data)
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
 * e_cal_generate_instances_for_object:
 * @ecal: A calendar client.
 * @icalcomp: Object to generate instances from.
 * @start: Start time for query.
 * @end: End time for query.
 * @cb: Callback for each generated instance.
 * @cb_data: Closure data for the callback.
 *
 * Does a combination of #e_cal_get_object_list () and
 * #e_cal_recur_generate_instances(), like #e_cal_generate_instances(), but
 * for a single object.
 *
 * The callback function should do a g_object_ref() of the calendar component
 * it gets passed if it intends to keep it around, since it will be unref'ed
 * as soon as the callback returns.
 **/
void
e_cal_generate_instances_for_object (ECal *ecal, icalcomponent *icalcomp,
				     time_t start, time_t end,
				     ECalRecurInstanceFn cb, gpointer cb_data)
{
	ECalPrivate *priv;
	ECalComponent *comp;
	const char *uid;
	char *rid;
	gboolean result;
	GList *instances = NULL;
	ECalComponentDateTime datetime;
	icaltimezone *start_zone;
	struct instances_info *instances_hold;

	g_return_if_fail (E_IS_CAL (ecal));
	g_return_if_fail (start >= 0);
	g_return_if_fail (end >= 0);
	g_return_if_fail (cb != NULL);

	priv = ecal->priv;

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp));

	/*If the backend stores it as individual instances and does not
	 * have a master object - do not expand*/
	if (e_cal_get_static_capability (ecal, CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER)) {

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
	e_cal_get_timezone (ecal, datetime.tzid, &start_zone, NULL);
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
		char *instance_rid = NULL;

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
build_component_alarms_list (ECal *ecal, GList *object_list, time_t start, time_t end)
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

		alarms = e_cal_util_generate_alarms_for_comp (comp, start, end, omit, e_cal_resolve_tzid_cb,
							      ecal, ecal->priv->default_zone);
		if (alarms)
			comp_alarms = g_slist_prepend (comp_alarms, alarms);
	}

	return comp_alarms;
}

/**
 * e_cal_get_alarms_in_range:
 * @ecal: A calendar client.
 * @start: Start time for query.
 * @end: End time for query.
 *
 * Queries a calendar for the alarms that trigger in the specified range of
 * time.
 *
 * Return value: A list of #ECalComponentAlarms structures.  This should be freed
 * using the #e_cal_free_alarms() function, or by freeing each element
 * separately with #e_cal_component_alarms_free() and then freeing the list with
 * #g_slist_free().
 **/
GSList *
e_cal_get_alarms_in_range (ECal *ecal, time_t start, time_t end)
{
	ECalPrivate *priv;
	GSList *alarms;
	char *sexp, *iso_start, *iso_end;
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
	sexp = g_strdup_printf ("(has-alarms-in-range? (make-time \"%s\") (make-time \"%s\"))",
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
 * e_cal_free_alarms:
 * @comp_alarms: A list of #ECalComponentAlarms structures.
 *
 * Frees a list of #ECalComponentAlarms structures as returned by
 * e_cal_get_alarms_in_range().
 **/
void
e_cal_free_alarms (GSList *comp_alarms)
{
	GSList *l;

	for (l = comp_alarms; l; l = l->next) {
		ECalComponentAlarms *alarms;

		alarms = l->data;
		g_assert (alarms != NULL);

		e_cal_component_alarms_free (alarms);
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
 * Return value: TRUE on success, FALSE if the object was not found.
 **/
gboolean
e_cal_get_alarms_for_object (ECal *ecal, const ECalComponentId *id,
			     time_t start, time_t end,
			     ECalComponentAlarms **alarms)
{
	ECalPrivate *priv;
	icalcomponent *icalcomp;
	ECalComponent *comp;
	ECalComponentAlarmAction omit[] = {-1};

	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	priv = ecal->priv;
	g_return_val_if_fail (priv->load_state == E_CAL_LOAD_LOADED, FALSE);

	g_return_val_if_fail (id != NULL, FALSE);
	g_return_val_if_fail (start >= 0 && end >= 0, FALSE);
	g_return_val_if_fail (start <= end, FALSE);
	g_return_val_if_fail (alarms != NULL, FALSE);

	*alarms = NULL;

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
 * e_cal_discard_alarm
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
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_discard_alarm (ECal *ecal, ECalComponent *comp, const char *auid, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;
	const char *uid;

	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	priv = ecal->priv;

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);

	e_cal_component_get_uid (comp, &uid);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_discardAlarm (priv->cal, uid, auid, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

typedef struct _ForeachTZIDCallbackData ForeachTZIDCallbackData;
struct _ForeachTZIDCallbackData {
	ECal *ecal;
	GHashTable *timezone_hash;
	gboolean include_all_timezones;
	gboolean success;
};

/* This adds the VTIMEZONE given by the TZID parameter to the GHashTable in
   data. */
static void
foreach_tzid_callback (icalparameter *param, void *cbdata)
{
	ForeachTZIDCallbackData *data = cbdata;
	ECalPrivate *priv;
	const char *tzid;
	icaltimezone *zone;
	icalcomponent *vtimezone_comp;
	char *vtimezone_as_string;

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
		   on the server so return. */
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

	vtimezone_as_string = icalcomponent_as_ical_string (vtimezone_comp);

	g_hash_table_insert (data->timezone_hash, (char*) tzid,
			     vtimezone_as_string);
}

/* This appends the value string to the GString given in data. */
static void
append_timezone_string (gpointer key, gpointer value, gpointer data)
{
	GString *vcal_string = data;

	g_string_append (vcal_string, value);
	g_free (value);
}


/* This simply frees the hash values. */
static void
free_timezone_string (gpointer key, gpointer value, gpointer data)
{
	g_free (value);
}


/* This converts the VEVENT/VTODO to a string. If include_all_timezones is
   TRUE, it includes all the VTIMEZONE components needed for the VEVENT/VTODO.
   If not, it only includes builtin timezones that may not be on the server.

   To do that we check every TZID in the component to see if it is a builtin
   timezone. If it is, we see if it it in our cache. If it is in our cache,
   then we know the server already has it and we don't need to send it.
   If it isn't in our cache, then we need to send it to the server.
   If we need to send any timezones to the server, then we have to create a
   complete VCALENDAR object, otherwise we can just send a single VEVENT/VTODO
   as before. */
static char*
e_cal_get_component_as_string_internal (ECal *ecal,
					icalcomponent *icalcomp,
					gboolean include_all_timezones)
{
	GHashTable *timezone_hash;
	GString *vcal_string;
	int initial_vcal_string_len;
	ForeachTZIDCallbackData cbdata;
	char *obj_string;
	ECalPrivate *priv;

	priv = ecal->priv;

	timezone_hash = g_hash_table_new (g_str_hash, g_str_equal);

	/* Add any timezones needed to the hash. We use a hash since we only
	   want to add each timezone once at most. */
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
	   and remember its length so we know if any VTIMEZONEs get added. */
	vcal_string = g_string_new (NULL);
	g_string_append (vcal_string,
			 "BEGIN:VCALENDAR\n"
			 "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
			 "VERSION:2.0\n"
			 "METHOD:PUBLISH\n");
	initial_vcal_string_len = vcal_string->len;

	/* Now concatenate all the timezone strings. This also frees the
	   timezone strings as it goes. */
	g_hash_table_foreach (timezone_hash, append_timezone_string,
			      vcal_string);

	/* Get the string for the VEVENT/VTODO. */
	obj_string = icalcomponent_as_ical_string (icalcomp);

	/* If there were any timezones to send, create a complete VCALENDAR,
	   else just send the VEVENT/VTODO string. */
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
 * e_cal_get_component_as_string:
 * @ecal: A calendar client.
 * @icalcomp: A calendar component object.
 *
 * Gets a calendar component as an iCalendar string, with a toplevel
 * VCALENDAR component and all VTIMEZONEs needed for the component.
 *
 * Return value: the component as a complete iCalendar string, or NULL on
 * failure. The string should be freed after use.
 **/
char*
e_cal_get_component_as_string (ECal *ecal, icalcomponent *icalcomp)
{
	return e_cal_get_component_as_string_internal (ecal, icalcomp, TRUE);
}

/**
 * e_cal_create_object:
 * @ecal: A calendar client.
 * @icalcomp: The component to create.
 * @uid: Return value for the UID assigned to the new component by the calendar backend.
 * @error: Placeholder for error information.
 *
 * Requests the calendar backend to create the object specified by the @icalcomp
 * argument. Some backends would assign a specific UID to the newly created object,
 * in those cases that UID would be returned in the @uid argument.
 *
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_create_object (ECal *ecal, icalcomponent *icalcomp, char **uid, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;
	char *obj;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	obj = icalcomponent_as_ical_string (icalcomp);
	GNOME_Evolution_Calendar_Cal_createObject (priv->cal, obj, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);
		g_free (obj);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	g_free (obj);
	CORBA_exception_free (&ev);

        e_flag_wait (our_op->done);

	status = our_op->status;
	if (uid)
	{
		*uid = our_op->uid;
		icalcomponent_set_uid (icalcomp, *uid);
	}

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_modify_object:
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
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_modify_object (ECal *ecal, icalcomponent *icalcomp, CalObjModType mod, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;
	char *obj;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (icalcomp, E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	obj = icalcomponent_as_ical_string (icalcomp);
	GNOME_Evolution_Calendar_Cal_modifyObject (priv->cal, obj, mod, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);
		g_free (obj);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	g_free (obj);
	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_remove_object_with_mod:
 * @ecal: A calendar client.
 * @uid: UID og the object to remove.
 * @rid: Recurrence ID of the specific recurrence to remove.
 * @mod: Type of removal.
 * @error: Placeholder for error information.
 *
 * This function allows the removal of instances of a recurrent
 * appointment. By using a combination of the @uid, @rid and @mod
 * arguments, you can remove specific instances. If what you want
 * is to remove all instances, use e_cal_remove_object instead.
 *
 * If not all instances are removed, the client will get a "obj_modified"
 * signal, while it will get a "obj_removed" signal when all instances
 * are removed.
 *
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_remove_object_with_mod (ECal *ecal, const char *uid,
			      const char *rid, CalObjModType mod, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (uid, E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);


	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_removeObject (priv->cal, uid, rid ? rid : "", mod, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_remove_object:
 * @ecal:  A calendar client.
 * @uid: Unique identifier of the calendar component to remove.
 * @error: Placeholder for error information.
 *
 * Asks a calendar to remove a component.  If the server is able to remove the
 * component, all clients will be notified and they will emit the "obj_removed"
 * signal.
 *
 * Return value: %TRUE if successful, %FALSE otherwise.
 **/
gboolean
e_cal_remove_object (ECal *ecal, const char *uid, GError **error)
{
	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (uid, E_CALENDAR_STATUS_INVALID_ARG);

	return e_cal_remove_object_with_mod (ecal, uid, NULL, CALOBJ_MOD_THIS, error);
}

/**
 * e_cal_receive_objects:
 * @ecal:  A calendar client.
 * @icalcomp: An icalcomponent.
 * @error: Placeholder for error information.
 *
 * Makes the backend receive the set of iCalendar objects specified in the
 * @icalcomp argument. This is used for iTIP confirmation/cancellation
 * messages for scheduled meetings.
 *
 * Return value: %TRUE if successful, %FALSE otherwise.
 */
gboolean
e_cal_receive_objects (ECal *ecal, icalcomponent *icalcomp, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;
	char *obj;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	obj = icalcomponent_as_ical_string (icalcomp);
	GNOME_Evolution_Calendar_Cal_receiveObjects (priv->cal, obj, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);
		g_free (obj);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	g_free (obj);
	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_send_objects:
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
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_send_objects (ECal *ecal, icalcomponent *icalcomp, GList **users, icalcomponent **modified_icalcomp, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;
	char *obj;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	obj = icalcomponent_as_ical_string (icalcomp);
	GNOME_Evolution_Calendar_Cal_sendObjects (priv->cal, obj, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);
		g_free (obj);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	g_free (obj);
	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;
	*users = our_op->list;
	if (status != E_CALENDAR_STATUS_OK) {
		*modified_icalcomp = NULL;
		g_list_foreach (*users, (GFunc) g_free, NULL);
		g_list_free (*users);
		*users = NULL;
	} else {
		*modified_icalcomp = icalparser_parse_string (our_op->string);
		if (!(*modified_icalcomp)) {
			status = E_CALENDAR_STATUS_INVALID_OBJECT;
			g_list_foreach (*users, (GFunc) g_free, NULL);
			g_list_free (*users);
			*users = NULL;
		}
	}
	g_free (our_op->string);

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_timezone:
 * @ecal: A calendar client.
 * @tzid: ID of the timezone to retrieve.
 * @zone: Return value for the timezone.
 * @error: Placeholder for error information.
 *
 * Retrieves a timezone object from the calendar backend.
 *
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_get_timezone (ECal *ecal, const char *tzid, icaltimezone **zone, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status = E_CALENDAR_STATUS_OK;
	ECalendarOp *our_op;
	icalcomponent *icalcomp;
	const char *systzid;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (zone, E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;

	g_mutex_lock (priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (priv->mutex);

	/* Check for well known zones and in the cache */
	*zone = NULL;

	/* If tzid is NULL or "" we return NULL, since it is a 'local time'. */
	if (!tzid || !tzid[0]) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		*zone = NULL;

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);
	}

	/* If it is UTC, we return the special UTC timezone. */
	if (!strcmp (tzid, "UTC")) {
		*zone = icaltimezone_get_utc_timezone ();
	} else {
		/* See if we already have it in the cache. */
		*zone = g_hash_table_lookup (priv->timezones, tzid);
	}

	if (*zone) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

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
		CORBA_exception_init (&ev);

		GNOME_Evolution_Calendar_Cal_getTimezone (priv->cal, tzid, &ev);
		if (BONOBO_EX (&ev)) {
			e_calendar_remove_op (ecal, our_op);
			e_calendar_free_op (our_op);

			CORBA_exception_free (&ev);

			g_warning (G_STRLOC ": Unable to contact backend");

			E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
		}

		CORBA_exception_free (&ev);

		e_flag_wait (our_op->done);

		status = our_op->status;
		if (status != E_CALENDAR_STATUS_OK){
			icalcomp = NULL;
		} else {
			icalcomp = icalparser_parse_string (our_op->string);
			if (!icalcomp)
				status = E_CALENDAR_STATUS_INVALID_OBJECT;
		}
		g_free (our_op->string);
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
		g_assert (syszone);
		if (syszone) {
			gboolean found = FALSE;
			icalproperty *prop;

			icalcomp = icalcomponent_new_clone (icaltimezone_get_component (syszone));
			prop = icalcomponent_get_first_property(icalcomp,
								ICAL_ANY_PROPERTY);
			while (!found && prop) {
				if (icalproperty_isa(prop) == ICAL_TZID_PROPERTY) {
					icalproperty_set_value_from_string(prop, tzid, "NO");
					found = TRUE;
				}
				prop = icalcomponent_get_next_property(icalcomp,
								       ICAL_ANY_PROPERTY);
			}
			g_assert (found);
		} else {
			status = E_CALENDAR_STATUS_INVALID_OBJECT;
		}
	}

	if (!icalcomp) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		E_CALENDAR_CHECK_STATUS (status, error);
	}

	*zone = icaltimezone_new ();
	if (!icaltimezone_set_component (*zone, icalcomp)) {
		icaltimezone_free (*zone, 1);

		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OBJECT_NOT_FOUND, error);
	}

	/* Now add it to the cache, to avoid the server call in future. */
	g_hash_table_insert (priv->timezones, icaltimezone_get_tzid (*zone), *zone);

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_add_timezone
 * @ecal: A calendar client.
 * @izone: The timezone to add.
 * @error: Placeholder for error information.
 *
 * Add a VTIMEZONE object to the given calendar.
 *
 * Returns: TRUE if successful, FALSE otherwise.
 */
gboolean
e_cal_add_timezone (ECal *ecal, icaltimezone *izone, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;
	char *tzobj;
	icalcomponent *icalcomp;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (izone, E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;

	g_mutex_lock (priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (priv->mutex);

	/* Make sure we have a valid component - UTC doesn't, nor do
	 * we really have to add it */
	if (izone == icaltimezone_get_utc_timezone ()) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);
	}

	icalcomp = icaltimezone_get_component (izone);
	if (!icalcomp) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_INVALID_ARG, error);
	}

	/* convert icaltimezone into a string */
	tzobj = icalcomponent_as_ical_string (icalcomp);

	/* call the backend */
	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_addTimezone (priv->cal, tzobj, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);
		g_free (tzobj);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	g_free (tzobj);
	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_query:
 * @ecal: A calendar client.
 * @sexp: S-expression representing the query.
 * @query: Return value for the new query.
 * @error: Placeholder for error information.
 *
 * Creates a live query object from a loaded calendar.
 *
 * Return value: A query object that will emit notification signals as calendar
 * components are added and removed from the query in the server.
 **/
gboolean
e_cal_get_query (ECal *ecal, const char *sexp, ECalView **query, GError **error)
{
	CORBA_Environment ev;
	ECalendarOp *our_op;
	ECalendarStatus status;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (query, E_CALENDAR_STATUS_INVALID_ARG);

	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->load_state != E_CAL_LOAD_LOADED) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_URI_NOT_LOADED, error);
	}

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	our_op->listener = e_cal_view_listener_new ();
	GNOME_Evolution_Calendar_Cal_getQuery (ecal->priv->cal, sexp, BONOBO_OBJREF (our_op->listener), &ev);

	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;
	*query = our_op->query;

	bonobo_object_unref (BONOBO_OBJECT (our_op->listener));

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_set_default_timezone:
 * @ecal: A calendar client.
 * @zone: A timezone object.
 * @error: Placeholder for error information.
 *
 * Sets the default timezone on the calendar. This should be called before opening
 * the calendar.
 *
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_set_default_timezone (ECal *ecal, icaltimezone *zone, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;
	icalcomponent *icalcomp = NULL;
	char *tzobj;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (zone, E_CALENDAR_STATUS_INVALID_ARG);

	priv = ecal->priv;

	/* Don't set the same timezone multiple times */
	if (priv->default_zone == zone)
		return FALSE;

	g_mutex_lock (priv->mutex);

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	our_op = e_calendar_new_op (ecal);

	g_mutex_unlock (priv->mutex);

	/* FIXME Adding it to the server to change the tzid */
	icalcomp = icaltimezone_get_component (zone);
	if (!icalcomp) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_INVALID_ARG, error);
	}

	/* convert icaltimezone into a string */
	tzobj = icalcomponent_as_ical_string (icalcomp);

	/* call the backend */
	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_setDefaultTimezone (priv->cal, tzobj, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		e_calendar_free_op (our_op);
		g_free (tzobj);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	g_free (tzobj);
	CORBA_exception_free (&ev);

	e_flag_wait (our_op->done);

	status = our_op->status;

	/* set the default timezone internally if successful */
	if (our_op->status == E_CALENDAR_STATUS_OK) {
		g_mutex_lock (priv->mutex);
		priv->default_zone = zone;
		g_mutex_unlock (priv->mutex);
	}

	e_calendar_remove_op (ecal, our_op);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_error_message
 * @status: A status code.
 *
 * Gets an error message for the given status code.
 *
 * Returns: the error message.
 */
const char *
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
	case E_CALENDAR_STATUS_CORBA_EXCEPTION :
		return _("A CORBA exception has occurred");
	case E_CALENDAR_STATUS_OTHER_ERROR :
		return _("Unknown error");
	case E_CALENDAR_STATUS_OK :
		return _("No error");
	default:
		/* ignore everything else */
		break;
	}

	return NULL;
}

static gboolean
get_default (ECal **ecal, ESourceList *sources, ECalSourceType type, ECalAuthFunc func, gpointer data, GError **error)
{
	GSList *g;
	GError *err = NULL;
	ESource *default_source = NULL;
	gboolean rv = TRUE;

	for (g = e_source_list_peek_groups (sources); g; g = g->next) {
		ESourceGroup *group = E_SOURCE_GROUP (g->data);
		GSList *s;
		for (s = e_source_group_peek_sources (group); s; s = s->next) {
			ESource *source = E_SOURCE (s->data);

			if (e_source_get_property (source, "default")) {
				default_source = source;
				break;
			}
		}

		if (default_source)
			break;
	}

	if (default_source) {
		*ecal = e_cal_new (default_source, type);
		if (!*ecal) {
			g_propagate_error (error, err);
			rv = FALSE;
			goto done;
		}

		e_cal_set_auth_func (*ecal, func, data);
		if (!e_cal_open (*ecal, TRUE, &err)) {
			g_propagate_error (error, err);
			rv = FALSE;
			goto done;
		}
	} else {
		switch (type) {
		case E_CAL_SOURCE_TYPE_EVENT:
			*ecal = e_cal_new_system_calendar ();
			break;
		case E_CAL_SOURCE_TYPE_TODO:
			*ecal = e_cal_new_system_tasks ();
			break;
		case E_CAL_SOURCE_TYPE_JOURNAL:
			*ecal = e_cal_new_system_memos ();
			break;
		default:
			break;
		}

		if (!*ecal) {
			g_propagate_error (error, err);
			rv = FALSE;
			goto done;
		}

		e_cal_set_auth_func (*ecal, func, data);
		if (!e_cal_open (*ecal, TRUE, &err)) {
			g_propagate_error (error, err);
			rv = FALSE;
			goto done;
		}
	}

 done:
	if (!rv && *ecal) {
		g_object_unref (*ecal);
		*ecal = NULL;
	}
	g_object_unref (sources);

	return rv;
}

/**
 * e_cal_open_default:
 * @ecal: A calendar client.
 * @type: Type of the calendar.
 * @func: Authentication function.
 * @data: Closure data for the authentication function.
 * @error: Placeholder for error information.
 *
 * Opens the default calendar.
 *
 * Return value: TRUE if it opened correctly, FALSE otherwise.
 */
gboolean
e_cal_open_default (ECal **ecal, ECalSourceType type, ECalAuthFunc func, gpointer data, GError **error)
{
	ESourceList *sources;
	GError *err = NULL;

	if (!e_cal_get_sources (&sources, type, &err)) {
		g_propagate_error (error, err);
		return FALSE;
	}

	return get_default (ecal, sources, type, func, data, error);
}

/**
 * e_cal_set_default:
 * @ecal: A calendar client.
 * @error: Placeholder for error information.
 *
 * Sets a calendar as the default one.
 *
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_set_default (ECal *ecal, GError **error)
{
	ESource *source;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);

	source = e_cal_get_source (ecal);
	if (!source) {
		/* XXX gerror */
		return FALSE;
	}

	return e_cal_set_default_source (source, ecal->priv->type, error);
}

static gboolean
set_default_source (ESourceList *sources, ESource *source, GError **error)
{
	const char *uid;
	GError *err = NULL;
	GSList *g;

	uid = e_source_peek_uid (source);

	/* make sure the source is actually in the ESourceList.  if
	   it's not we don't bother adding it, just return an error */
	source = e_source_list_peek_source_by_uid (sources, uid);
	if (!source) {
		/* XXX gerror */
		g_object_unref (sources);
		return FALSE;
	}

	/* loop over all the sources clearing out any "default"
	   properties we find */
	for (g = e_source_list_peek_groups (sources); g; g = g->next) {
		GSList *s;
		for (s = e_source_group_peek_sources (E_SOURCE_GROUP (g->data));
		     s; s = s->next) {
			e_source_set_property (E_SOURCE (s->data), "default", NULL);
		}
	}

	/* set the "default" property on the source */
	e_source_set_property (source, "default", "true");

	if (!e_source_list_sync (sources, &err)) {
		g_propagate_error (error, err);
		return FALSE;
	}

	return TRUE;
}

/**
 * e_cal_set_default_source:
 * @source: An #ESource.
 * @type: Type of the source.
 * @error: Placeholder for error information.
 *
 * Sets the default source for the specified @type.
 *
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_set_default_source (ESource *source, ECalSourceType type, GError **error)
{
	ESourceList *sources;
	GError *err = NULL;

	if (!e_cal_get_sources (&sources, type, &err)) {
		g_propagate_error (error, err);
		return FALSE;
	}

	return set_default_source (sources, source, error);
}

static gboolean
get_sources (ESourceList **sources, const char *key, GError **error)
{
	GConfClient *gconf = gconf_client_get_default();

	*sources = e_source_list_new_for_gconf (gconf, key);
	g_object_unref (gconf);

	return TRUE;
}

/**
 * e_cal_get_sources:
 * @sources: Return value for list of sources.
 * @type: Type of the sources to get.
 * @error: Placeholder for error information.
 *
 * Gets the list of sources defined in the configuration for the given @type.
 *
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_get_sources (ESourceList **sources, ECalSourceType type, GError **error)
{
	switch (type) {
	case E_CAL_SOURCE_TYPE_EVENT:
		return get_sources (sources, "/apps/evolution/calendar/sources", error);
		break;
	case E_CAL_SOURCE_TYPE_TODO:
		return get_sources (sources, "/apps/evolution/tasks/sources", error);
		break;
	case E_CAL_SOURCE_TYPE_JOURNAL:
		return get_sources (sources, "/apps/evolution/memos/sources", error);
		break;
	default:
		/* FIXME Fill in error */
		return FALSE;
	}

	/* FIXME Fill in error */
	return FALSE;
}

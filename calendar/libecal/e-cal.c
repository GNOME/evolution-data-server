/* Evolution calendar ecal
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pthread.h>
#include <string.h>
#include <bonobo-activation/bonobo-activation.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-i18n.h>
#include <libgnome/gnome-util.h>

#include <libedataserver/e-component-listener.h>
#include <libedataserver/e-url.h>
#include "e-cal-marshal.h"
#include "e-cal-time-util.h"
#include "e-cal-listener.h"
#include "e-cal-view-listener.h"
#include "e-cal.h"



typedef struct {
	GMutex *mutex;
	GCond *cond;
	ECalendarStatus status;

	char *uid;
	GList *list;
	gboolean bool;
	char *string;
	icalcomponent *icalcomp;

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
};



/* Signal IDs */
enum {
	CAL_OPENED,
	CAL_SET_MODE,
	BACKEND_ERROR,
	CATEGORIES_CHANGED,
	BACKEND_DIED,
	LAST_SIGNAL
};

static guint e_cal_signals[LAST_SIGNAL];

static GObjectClass *parent_class;

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

	op->mutex = g_mutex_new ();
	op->cond = g_cond_new ();

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
	g_cond_free (op->cond);
	g_mutex_free (op->mutex);
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

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	g_mutex_lock (op->mutex);

	op->status = status;
	op->bool = read_only;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
}

static void
cal_cal_address_cb (ECalListener *listener, ECalendarStatus status, const char *address, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	g_mutex_lock (op->mutex);

	op->status = status;
	op->string = g_strdup (address);

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;
	op->string = g_strdup (address);

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;
	op->string = g_strdup (attribute);

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;
	op->string = g_strdup (capabilities);

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;
	op->uid = g_strdup (uid);
	
	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
}

static void
cal_objects_sent_cb (ECalListener *listener, ECalendarStatus status, GList *users, icalcomponent *modified_icalcomp, gpointer data)
{
	ECal *ecal = data;
	ECalendarOp *op;

	op = e_calendar_get_op (ecal);

	if (op == NULL) {
		g_warning (G_STRLOC ": Cannot find operation ");
		return;
	}

	g_mutex_lock (op->mutex);

	op->status = status;
	op->list = g_list_copy (users);
	if (modified_icalcomp)
		op->icalcomp = icalcomponent_new_clone (modified_icalcomp);
	else
		op->icalcomp = NULL;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;
	op->string = g_strdup (object);
	
	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;
	op->string = g_strdup (object);
	
	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;
	op->list = g_list_copy (objects);
	
	for (l = op->list; l; l = l->next)
		l->data = icalcomponent_new_clone (l->data);
	
	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;
	op->string = g_strdup (object);

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);

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

	g_mutex_lock (op->mutex);

	op->status = status;
	op->uid = g_strdup (tzid);

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);

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

	g_mutex_lock (op->mutex);

	op->status = status;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;
	op->list = g_list_copy (changes);

	for (l = op->list; l; l = l->next) {
		ECalChange *ccc = l->data, *new_ccc;

		new_ccc = g_new (ECalChange, 1);
		new_ccc->comp = e_cal_component_clone (ccc->comp);
		new_ccc->type = ccc->type;
		
		l->data = new_ccc;
	}
	
	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;
	op->list = g_list_copy (freebusy);

	for (l = op->list; l; l = l->next)
		l->data = e_cal_component_clone (l->data);

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
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

	g_mutex_lock (op->mutex);

	op->status = status;
	op->query = e_cal_view_new (query, op->listener, ecal);
	
	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);	
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

typedef struct
{
	ECal *ecal;
	GPtrArray *categories;
}  ECalCategoryData;

static gboolean
categories_changed_idle_cb (gpointer data)
{
	ECalCategoryData *cat_data = data;
	int i;
	
	g_signal_emit (G_OBJECT (cat_data->ecal), e_cal_signals[CATEGORIES_CHANGED], 0, cat_data->categories);

	g_object_unref (cat_data->ecal);
	for (i = 0; i < cat_data->categories->len; i++)
		g_free (cat_data->categories->pdata[i]);
	g_ptr_array_free (cat_data->categories, TRUE);
	g_free (cat_data);
	
	return FALSE;
}

/* Handle the categories_changed signal from the listener */
static void
categories_changed_cb (ECalListener *listener, const GNOME_Evolution_Calendar_StringSeq *categories,
		       gpointer data)
{
	ECalCategoryData *cat_data;
	int i;

	cat_data = g_new0 (ECalCategoryData, 1);

	cat_data->ecal = g_object_ref (data);
	cat_data->categories = g_ptr_array_new ();
	g_ptr_array_set_size (cat_data->categories, categories->_length);

	for (i = 0; i < categories->_length; i++)
		cat_data->categories->pdata[i] = g_strdup (categories->_buffer[i]);
	
	g_idle_add (categories_changed_idle_cb, cat_data);
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

	query = g_strdup_printf ("repo_ids.has ('IDL:GNOME/Evolution/DataServer/CalFactory:" BASE_VERSION "')"
				 " AND calendar:supported_protocols.has ('%s')", uri->protocol);

	
	servers = bonobo_activation_query (query, NULL, NULL);

	g_free (query);
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
	g_signal_connect (G_OBJECT (priv->listener), "get_timezone", G_CALLBACK (cal_get_timezone_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "add_timezone", G_CALLBACK (cal_add_timezone_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "set_default_timezone", G_CALLBACK (cal_set_default_timezone_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "get_changes", G_CALLBACK (cal_get_changes_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "get_free_busy", G_CALLBACK (cal_get_free_busy_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "query", G_CALLBACK (cal_query_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "categories_changed", G_CALLBACK (categories_changed_cb), ecal);
	g_signal_connect (G_OBJECT (priv->listener), "backend_error", G_CALLBACK (backend_error_cb), ecal);
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
	e_cal_signals[CATEGORIES_CHANGED] =
		g_signal_new ("categories_changed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalClass, categories_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
	e_cal_signals[BACKEND_DIED] =
		g_signal_new ("backend_died",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalClass, backend_died),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	klass->cal_opened = NULL;
	klass->categories_changed = NULL;
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

/**
 * e_cal_new:
 * @source: 
 * @type: 
 *
 * Creates a new calendar ecal.  It should be initialized by calling
 * e_cal_open().
 *
 * Return value: A newly-created calendar ecal, or NULL if the ecal could
 * not be constructed because it could not contact the calendar server.
 **/
ECal *
e_cal_new (ESource *source, ECalSourceType type)
{
	ECal *ecal;

	ecal = g_object_new (E_TYPE_CAL, NULL);

	if (!fetch_corba_cal (ecal, source, type)) {
		g_object_unref (ecal);

		return NULL;
	}

	/* initialize component listener */
	ecal->priv->comp_listener = e_component_listener_new ((Bonobo_Unknown) ecal->priv->cal);
	g_signal_connect (G_OBJECT (ecal->priv->comp_listener), "component_died",
			  G_CALLBACK (backend_died_cb), ecal);

	return ecal;
}

/**
 * e_cal_new_from_uri:
 * @uri: 
 * @type: 
 *
 * Creates a new calendar ecal.  It should be initialized by calling
 * e_cal_open().
 *
 * Return value: A newly-created calendar ecal, or NULL if the ecal could
 * not be constructed because it could not contact the calendar server.
 **/
ECal *
e_cal_new_from_uri (const gchar *uri, ECalSourceType type)
{
	ESourceGroup *group;
	ESource *source;
	ECal *cal;

	group = e_source_group_new ("", uri);
	source = e_source_new ("", "");
	e_source_set_group (source, group);

	cal = e_cal_new (source, type);

	g_object_unref (source);
	g_object_unref (group);

	return cal;
}

ECal *
e_cal_new_system_calendar (void)
{
	ECal *ecal;
	char *uri;

	uri = g_build_filename ("file://", g_get_home_dir (), ".evolution", "calendar", "local", "system", NULL);
	ecal = e_cal_new_from_uri (uri, E_CAL_SOURCE_TYPE_EVENT);
	g_free (uri);
	
	return ecal;
}

ECal *
e_cal_new_system_tasks (void)
{
	ECal *ecal;
	char *uri;

	uri = g_build_filename ("file://", g_get_home_dir (), ".evolution", "tasks", "local", "system", NULL);
	ecal = e_cal_new_from_uri (uri, E_CAL_SOURCE_TYPE_EVENT);
	g_free (uri);
	
	return ecal;
}

/**
 * e_cal_set_auth_func
 * @ecal: A calendar ecal.
 * @func: The authentication function
 * @data: User data to be used when calling the authentication function
 *
 * Associates the given authentication function with a calendar ecal. This
 * function will be called any time the calendar server needs a password
 * from the ecal. So, calendar ecals should provide such authentication
 * function, which, when called, should act accordingly (by showing a dialog
 * box, for example, to ask the user for the password).
 *
 * The authentication function must have the following form:
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

static gboolean
open_calendar (ECal *ecal, gboolean only_if_exists, GError **error, ECalendarStatus *status)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarOp *our_op;
	const char *username = NULL;
	char *password = NULL;
	
	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	priv = ecal->priv;
	
	g_mutex_lock (ecal->priv->mutex);

	if (ecal->priv->current_op != NULL) {
		g_mutex_unlock (ecal->priv->mutex);
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_BUSY, error);
	}

	/* see if the backend needs authentication */
	if (e_source_get_property (priv->source, "auth")) {
		char *prompt, *key;

		priv->load_state = E_CAL_LOAD_AUTHENTICATING;

		if (priv->auth_func == NULL) {
			g_mutex_unlock (priv->mutex);
			E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED, error);
		}

		username = e_source_get_property (priv->source, "username");
		if (!username) {
			g_mutex_unlock (priv->mutex);
			E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED, error);
		}

		/* actually ask the client for authentication */
		prompt = g_strdup_printf (_("Enter password for %s (user %s)"),
					  e_source_peek_name (priv->source), username);
		key = e_source_get_uri (priv->source);

		password = priv->auth_func (ecal, prompt, key, priv->auth_user_data);
		if (!password) {
			g_mutex_unlock (priv->mutex);
			E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED, error);
		}

		g_free (prompt);
		g_free (key);
	}

	/* start the open operation */
	our_op = e_calendar_new_op (ecal);

	g_mutex_lock (our_op->mutex);

	CORBA_exception_init (&ev);

	priv->load_state = E_CAL_LOAD_LOADING;

	GNOME_Evolution_Calendar_Cal_open (priv->cal, only_if_exists,
					   username ? username : "",
					   password ? password : "",
					   &ev);
	if (password)
		g_free (password);

	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);
		g_mutex_unlock (ecal->priv->mutex);
		
		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		*status = E_CALENDAR_STATUS_CORBA_EXCEPTION;
		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	*status = our_op->status;
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	if (*status == E_CALENDAR_STATUS_OK)
		priv->load_state = E_CAL_LOAD_LOADED;
	else
		priv->load_state = E_CAL_LOAD_NOT_LOADED;

	g_mutex_unlock (ecal->priv->mutex);

	E_CALENDAR_CHECK_STATUS (*status, error);
}

/**
 * e_cal_open
 * @ecal: A calendar ecal.
 * @only_if_exists: FALSE if the calendar should be opened even if there
 * was no storage for it, i.e. to create a new calendar or load an existing
 * one if it already exists.  TRUE if it should only try to load calendars
 * that already exist.
 * @error: 
 *
 * Makes a calendar ecal initiate a request to open a calendar.  The calendar
 * ecal will emit the "cal_opened" signal when the response from the server is
 * received.
 *
 * Return value: TRUE on success, FALSE on failure to issue the open request.
 **/
gboolean
e_cal_open (ECal *ecal, gboolean only_if_exists, GError **error)
{
	ECalendarStatus status;
	gboolean result;

	result = open_calendar (ecal, only_if_exists, error, &status);
	g_signal_emit (G_OBJECT (ecal), e_cal_signals[CAL_OPENED], 0, status);

	return result;
}

typedef struct {
	ECal *ecal;
	gboolean exists;
	gboolean result;
	ECalendarStatus status;
} ECalAsyncData;

static gboolean
async_idle_cb (gpointer data)
{
	ECalAsyncData *ccad = data;

	ccad->result = open_calendar (ccad->ecal, ccad->exists, NULL, &ccad->status);
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

	g_idle_add ((GSourceFunc) async_idle_cb, ccad);

	return GINT_TO_POINTER (ccad->result);
}

void
e_cal_open_async (ECal *ecal, gboolean only_if_exists)
{
	ECalAsyncData *ccad;
	GThread *thread;
	GError *error = NULL;
	
	g_return_if_fail (ecal != NULL);
	g_return_if_fail (E_IS_CAL (ecal));

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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);


	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_remove (priv->cal, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
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
 * e_cal_get_load_state:
 * @ecal: A calendar ecal.
 * 
 * Queries the state of loading of a calendar ecal.
 * 
 * Return value: A #ECalLoadState value indicating whether the ecal has
 * not been loaded with e_cal_open_calendar() yet, whether it is being
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
 * @ecal: A calendar ecal.
 * 
 * Queries the source that is open in a calendar ecal.
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
 * @ecal: A calendar ecal.
 * 
 * Queries the URI that is open in a calendar ecal.
 * 
 * Return value: The URI of the calendar that is already loaded or is being
 * loaded, or NULL if the ecal has not started a load request yet.
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
 * e_cal_is_read_only:
 * @ecal: A calendar ecal.
 * @read_only: 
 * @error: 
 *
 * Queries whether the calendar ecal can perform modifications
 * on the calendar or not. Whether the backend is read only or not
 * is specified, on exit, in the @read_only argument.
 *
 * Return value: TRUE if the call was successful, FALSE if there was an error.
 */
gboolean
e_cal_is_read_only (ECal *ecal, gboolean *read_only, GError **error)
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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);


	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_isReadOnly (priv->cal, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*read_only = our_op->bool;
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_cal_address:
 * @ecal: A calendar ecal.
  * @cal_address: 
 * @error: 
 *
 * Queries the calendar address associated with a calendar ecal.
 * 
 * Return value: The calendar address associated with the calendar that
 * is loaded or being loaded, or %NULL if the ecal has not started a
 * load request yet or the calendar has no associated email address.
 **/
gboolean
e_cal_get_cal_address (ECal *ecal, char **cal_address, GError **error)
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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);


	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getCalAddress (priv->cal, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*cal_address = our_op->string;
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);


	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getAlarmEmailAddress (priv->cal, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*alarm_address = our_op->string;
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);


	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getLdapAttribute (priv->cal, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*ldap_attribute = our_op->string;
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
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
	char *cap;
	
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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);


	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getStaticCapabilities (priv->cal, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	cap = our_op->string;
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
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

gboolean
e_cal_get_one_alarm_only (ECal *ecal)
{
	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (ecal && E_IS_CAL (ecal), FALSE);

	return check_capability (ecal, CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY);
}

gboolean 
e_cal_get_organizer_must_attend (ECal *ecal)
{
	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	return check_capability (ecal, CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ATTEND);
}

gboolean
e_cal_get_static_capability (ECal *ecal, const char *cap)
{
	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	return check_capability (ecal, cap);
}

gboolean 
e_cal_get_save_schedules (ECal *ecal)
{
	g_return_val_if_fail (ecal != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL (ecal), FALSE);

	return check_capability (ecal, CAL_STATIC_CAPABILITY_SAVE_SCHEDULES);
}

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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getDefaultObject (priv->cal, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
        if (status)
                *icalcomp = NULL;
        else
                *icalcomp = icalparser_parse_string (our_op->string);
	g_free (our_op->string);

	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_object:
 * @ecal: A calendar ecal.
 * @uid: Unique identifier for a calendar component.
 * @rid: 
 * @icalcomp: Return value for the calendar component object.
 * @error: 
 *
 * Queries a calendar for a calendar component object based on its unique
 * identifier.
 *
 * Return value: Result code based on the status of the operation.
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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getObject (priv->cal, uid, rid ? rid : "", &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*icalcomp = icalparser_parse_string (our_op->string);
	/* FIXME if the parse fails its an error */
	g_free (our_op->string);

	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/* Resolves TZIDs for the recurrence generator. */
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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getChanges (ecal->priv->cal, change_id, &ev);

	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}
	
	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*changes = our_op->list;

	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

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
 * @ecal: 
 * @query: 
 * @objects: 
 * @error: 
 * 
 * 
 * 
 * Return value: 
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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getObjectList (ecal->priv->cal, query, &ev);

	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}
	
	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*objects = our_op->list;

	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

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
 * @ecal: A calendar ecal.
 * @users: List of users to retrieve free/busy information for.
 * @start: Start time for query.
 * @end: End time for query.
 * @freebusy: 
 * @error: 
 *
 * Gets free/busy information from the calendar server.
 *
 * Returns: a GList of VFREEBUSY ECalComponents
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

	g_mutex_lock (our_op->mutex);

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
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}
	
	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*freebusy = our_op->list;

	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

struct comp_instance {
	ECalComponent *comp;
	time_t start;
	time_t end;
};

/* Called from cal_recur_generate_instances(); adds an instance to the list */
static gboolean
add_instance (ECalComponent *comp, time_t start, time_t end, gpointer data)
{
	GList **list;
	struct comp_instance *ci;

	list = data;

	ci = g_new (struct comp_instance, 1);

	ci->comp = comp;
	g_object_ref (G_OBJECT (ci->comp));
	
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

/**
 * e_cal_generate_instances:
 * @ecal: A calendar ecal.
 * @type: Bitmask with types of objects to return.
 * @start: Start time for query.
 * @end: End time for query.
 * @cb: Callback for each generated instance.
 * @cb_data: Closure data for the callback.
 * 
 * Does a combination of e_cal_get_object_list () and
 * cal_recur_generate_instances().  
 *
 * The callback function should do a g_object_ref() of the calendar component
 * it gets passed if it intends to keep it around.
 **/
void
e_cal_generate_instances (ECal *ecal, time_t start, time_t end,
			  ECalRecurInstanceFn cb, gpointer cb_data)
{
	ECalPrivate *priv;
	GList *objects;
	GList *instances;
	GList *l;
	char *query;
	char *iso_start, *iso_end;
	
	g_return_if_fail (ecal != NULL);
	g_return_if_fail (E_IS_CAL (ecal));

	priv = ecal->priv;
	g_return_if_fail (priv->load_state == E_CAL_LOAD_LOADED);

	g_return_if_fail (start != -1 && end != -1);
	g_return_if_fail (start <= end);
	g_return_if_fail (cb != NULL);

	iso_start = isodate_from_time_t (start);
	iso_end   = isodate_from_time_t (end);
	/* Generate objects */
	query = g_strdup_printf ("(occur-in-time-range? (make-time \"%s\") (make-time \"%s\"))",
				 iso_start, iso_end);
	g_free (iso_start);
	g_free (iso_end);
	if (!e_cal_get_object_list_as_comp (ecal, query, &objects, NULL)) {
		g_free (query);
		return;
	}	
	g_free (query);

	instances = NULL;

	for (l = objects; l; l = l->next) {
		ECalComponent *comp;

		comp = l->data;
		e_cal_recur_generate_instances (comp, start, end, add_instance, &instances,
					      e_cal_resolve_tzid_cb, ecal,
					      priv->default_zone);
		g_object_unref (comp);
	}

	g_list_free (objects);

	/* Generate instances and spew them out */

	instances = g_list_sort (instances, compare_comp_instance);

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
 * @ecal: A calendar ecal.
 * @start: Start time for query.
 * @end: End time for query.
 *
 * Queries a calendar for the alarms that trigger in the specified range of
 * time.
 *
 * Return value: A list of #ECalComponentAlarms structures.  This should be freed
 * using the e_cal_free_alarms() function, or by freeing each element
 * separately with e_cal_component_alarms_free() and then freeing the list with
 * g_slist_free().
 **/
GSList *
e_cal_get_alarms_in_range (ECal *ecal, time_t start, time_t end)
{
	ECalPrivate *priv;
	GSList *alarms;
	char *sexp;
	GList *object_list = NULL;

	g_return_val_if_fail (ecal != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL (ecal), NULL);

	priv = ecal->priv;
	g_return_val_if_fail (priv->load_state == E_CAL_LOAD_LOADED, NULL);

	g_return_val_if_fail (start != -1 && end != -1, NULL);
	g_return_val_if_fail (start <= end, NULL);

	/* build the query string */
	sexp = g_strdup ("(has-alarms?)");

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
 * @ecal: A calendar ecal.
 * @uid: Unique identifier for a calendar component.
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
e_cal_get_alarms_for_object (ECal *ecal, const char *uid,
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

	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (start != -1 && end != -1, FALSE);
	g_return_val_if_fail (start <= end, FALSE);
	g_return_val_if_fail (alarms != NULL, FALSE);

	*alarms = NULL;

	if (!e_cal_get_object (ecal, uid, NULL, &icalcomp, NULL))
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
 * @error: 
 *
 * Tells the calendar backend to get rid of the alarm identified by the
 * @auid argument in @comp. Some backends might remove the alarm or
 * update internal information about the alarm be discarded, or, like
 * the file backend does, ignore the operation.
 *
 * Return value: a #ECalResult value indicating the result of the
 * operation.
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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);

	e_cal_component_get_uid (comp, &uid);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_discardAlarm (priv->cal, uid, auid, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
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
			     g_strdup (vtimezone_as_string));
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
	obj_string = g_strdup (icalcomponent_as_ical_string (icalcomp));

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
 * @ecal: A calendar ecal.
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

gboolean
e_cal_create_object (ECal *ecal, icalcomponent *icalcomp, char **uid, GError **error)
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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_createObject (priv->cal, icalcomponent_as_ical_string (icalcomp), &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	if (uid)
		*uid = our_op->uid;
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

gboolean
e_cal_modify_object (ECal *ecal, icalcomponent *icalcomp, CalObjModType mod, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;

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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_modifyObject (priv->cal, icalcomponent_as_ical_string (icalcomp), mod, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);


	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_removeObject (priv->cal, uid, rid ? rid : "", mod, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_remove_object:
 * @ecal:  A calendar ecal.
 * @uid: Unique identifier of the calendar component to remove.
 * @error: 
 * 
 * 
 * Asks a calendar to remove a component.  If the server is able to remove the
 * component, all ecals will be notified and they will emit the "obj_removed"
 * signal.
 * 
 * Return value: an #ECalResult value indicating the result of the
 * operation.
 **/
gboolean
e_cal_remove_object (ECal *ecal, const char *uid, GError **error)
{
	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);
	e_return_error_if_fail (uid, E_CALENDAR_STATUS_INVALID_ARG);

	return e_cal_remove_object_with_mod (ecal, uid, NULL, CALOBJ_MOD_ALL, error);
}

gboolean
e_cal_receive_objects (ECal *ecal, icalcomponent *icalcomp, GError **error)
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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_receiveObjects (priv->cal, icalcomponent_as_ical_string (icalcomp), &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

gboolean
e_cal_send_objects (ECal *ecal, icalcomponent *icalcomp, GList **users, icalcomponent **modified_icalcomp, GError **error)
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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_sendObjects (priv->cal, icalcomponent_as_ical_string (icalcomp), &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*users = our_op->list;
	*modified_icalcomp = our_op->icalcomp;
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

gboolean
e_cal_get_timezone (ECal *ecal, const char *tzid, icaltimezone **zone, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;
	icalcomponent *icalcomp;

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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (priv->mutex);

	/* Check for well known zones and in the cache */
	*zone = NULL;
	
	/* If tzid is NULL or "" we return NULL, since it is a 'local time'. */
	if (!tzid || !tzid[0]) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
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
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OK, error);	
	}
	
	/* call the backend */
	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_getTimezone (priv->cal, tzid, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	
	icalcomp = icalparser_parse_string (our_op->string);
	g_free (our_op->string);
	
	/* FIXME Invalid object status? */
	if (!icalcomp) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OBJECT_NOT_FOUND, error);
	}
	
	*zone = icaltimezone_new ();	
	if (!icaltimezone_set_component (*zone, icalcomp)) {
		icaltimezone_free (*zone, 1);

		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_OBJECT_NOT_FOUND, error);
	}

	/* Now add it to the cache, to avoid the server call in future. */
	g_hash_table_insert (priv->timezones, icaltimezone_get_tzid (*zone), *zone);

	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_add_timezone
 * @ecal: A calendar ecal.
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
	const char *tzobj;

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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (priv->mutex);

	/* convert icaltimezone into a string */
	tzobj = icalcomponent_as_ical_string (icaltimezone_get_component (izone));

	/* call the backend */
	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_addTimezone (priv->cal, tzobj, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_query:
 * @ecal: A calendar ecal.
 * @sexp: S-expression representing the query.
 * @query: 
 * @error: 
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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (ecal->priv->mutex);

	CORBA_exception_init (&ev);

	our_op->listener = e_cal_view_listener_new ();
	GNOME_Evolution_Calendar_Cal_getQuery (ecal->priv->cal, sexp, BONOBO_OBJREF (our_op->listener), &ev);

	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}
	
	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*query = our_op->query;

	bonobo_object_unref (BONOBO_OBJECT (our_op->listener));
	
	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}


/* This ensures that the given timezone is on the server. We use this to pass
   the default timezone to the server, so it can resolve DATE and floating
   DATE-TIME values into specific times. (Most of our IDL interface uses
   time_t values to pass specific times from the server to the ecal.) */
static gboolean
e_cal_ensure_timezone_on_server (ECal *ecal, icaltimezone *zone, GError **error)
{
	ECalPrivate *priv;
	char *tzid;
	icaltimezone *tmp_zone;

	priv = ecal->priv;

	/* FIXME This is highly broken since there is no locking */

	/* If the zone is NULL or UTC we don't need to do anything. */
	if (!zone)
		return TRUE;
	
	tzid = icaltimezone_get_tzid (zone);

	if (!strcmp (tzid, "UTC"))
		return TRUE;

	/* See if we already have it in the cache. If we do, it must be on
	   the server already. */
	tmp_zone = g_hash_table_lookup (priv->timezones, tzid);
	if (tmp_zone)
		return TRUE;

	/* Now we have to send it to the server, in case it doesn't already
	   have it. */
	return e_cal_add_timezone (ecal, zone, error);
}

gboolean
e_cal_set_default_timezone (ECal *ecal, icaltimezone *zone, GError **error)
{
	ECalPrivate *priv;
	CORBA_Environment ev;
	ECalendarStatus status;
	ECalendarOp *our_op;
	const char *tzid;

	e_return_error_if_fail (ecal && E_IS_CAL (ecal), E_CALENDAR_STATUS_INVALID_ARG);	
	e_return_error_if_fail (zone, E_CALENDAR_STATUS_INVALID_ARG);	

	priv = ecal->priv;

	/* Make sure the server has the VTIMEZONE data. */
	if (!e_cal_ensure_timezone_on_server (ecal, zone, error))
		return FALSE;

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

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (priv->mutex);

	/* FIXME Adding it to the server to change the tzid */
	tzid = icaltimezone_get_tzid (zone);

	/* call the backend */
	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_Cal_setDefaultTimezone (priv->cal, tzid, &ev);
	if (BONOBO_EX (&ev)) {
		e_calendar_remove_op (ecal, our_op);
		g_mutex_unlock (our_op->mutex);
		e_calendar_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning (G_STRLOC ": Unable to contact backend");

		E_CALENDAR_CHECK_STATUS (E_CALENDAR_STATUS_CORBA_EXCEPTION, error);
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;

	e_calendar_remove_op (ecal, our_op);
	g_mutex_unlock (our_op->mutex);
	e_calendar_free_op (our_op);

	E_CALENDAR_CHECK_STATUS (status, error);
}

/**
 * e_cal_get_error_message
 * @status: A status code.
 *
 * Get an error message for the given status code.
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
	case E_CALENDAR_STATUS_OBJECT_ID_ALREADY_EXISTS :
		return _("Object ID already exists");
	case E_CALENDAR_STATUS_PROTOCOL_NOT_SUPPORTED :
		return _("Protocol not supported");
	case E_CALENDAR_STATUS_CANCELLED :
		return _("Operation has been cancelled");
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
	default:
		/* FIXME Fill in error */
		return FALSE;
	}
	
	/* FIXME Fill in error */
	return FALSE;	
}

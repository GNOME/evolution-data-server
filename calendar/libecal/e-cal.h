/* Evolution calendar ecal
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
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

#ifndef E_CAL_H
#define E_CAL_H

#include <glib-object.h>
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-util.h>
#include <libecal/e-cal-view.h>
#include <libecal/e-cal-types.h>

G_BEGIN_DECLS



#define E_TYPE_CAL            (e_cal_get_type ())
#define E_CAL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL, ECal))
#define E_CAL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL, ECalClass))
#define E_IS_CAL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL))
#define E_IS_CAL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL))

#define E_CAL_OPEN_STATUS_ENUM_TYPE     (e_cal_open_status_enum_get_type ())
#define E_CAL_REMOVE_STATUS_ENUM_TYPE   (e_cal_remove_status_enum_get_type ())
#define E_CAL_SET_MODE_STATUS_ENUM_TYPE (e_cal_set_mode_status_enum_get_type ())
#define CAL_MODE_ENUM_TYPE                   (cal_mode_enum_get_type ())

typedef struct _ECalClass ECalClass;

typedef struct _ECalPrivate ECalPrivate;

/* Open status for the cal_opened signal */
typedef enum {
	E_CAL_OPEN_SUCCESS,
	E_CAL_OPEN_ERROR,
	E_CAL_OPEN_NOT_FOUND,
	E_CAL_OPEN_PERMISSION_DENIED,
	E_CAL_OPEN_METHOD_NOT_SUPPORTED
} ECalOpenStatus;

/* Set mode status for the e_cal_set_mode function */
typedef enum {
	E_CAL_SET_MODE_SUCCESS,
	E_CAL_SET_MODE_ERROR,
	E_CAL_SET_MODE_NOT_SUPPORTED
} ECalSetModeStatus;

/* Get status for the e_cal_get_object() function */
typedef enum {
	E_CAL_GET_SUCCESS,
	E_CAL_GET_NOT_FOUND,
	E_CAL_GET_SYNTAX_ERROR
} ECalGetStatus;

/* Status for update_object(s) and remove_object */
typedef enum {
	E_CAL_RESULT_SUCCESS,
	E_CAL_RESULT_CORBA_ERROR,
	E_CAL_RESULT_INVALID_OBJECT,
	E_CAL_RESULT_NOT_FOUND,
	E_CAL_RESULT_PERMISSION_DENIED
} ECalResult;

/* Whether the ecal is not loaded, is being loaded, or is already loaded */
typedef enum {
	E_CAL_LOAD_NOT_LOADED,
	E_CAL_LOAD_LOADING,
	E_CAL_LOAD_LOADED
} ECalLoadState;

struct _ECal {
	GObject object;

	/* Private data */
	ECalPrivate *priv;
};

struct _ECalClass {
	GObjectClass parent_class;

	/* Notification signals */

	void (* cal_opened) (ECal *ecal, ECalOpenStatus status);
	void (* cal_set_mode) (ECal *ecal, ECalSetModeStatus status, CalMode mode);	

	void (* backend_error) (ECal *ecal, const char *message);

	void (* categories_changed) (ECal *ecal, GPtrArray *categories);

	void (* forget_password) (ECal *ecal, const char *key);

	void (* backend_died) (ECal *ecal);
};

typedef gchar * (* ECalAuthFunc) (ECal *ecal,
                                      const gchar *prompt,
                                      const gchar *key,
                                      gpointer user_data);

GType e_cal_get_type (void);

GType e_cal_open_status_enum_get_type (void);
GType e_cal_set_mode_status_enum_get_type (void);
GType cal_mode_enum_get_type (void);

ECal *e_cal_new (const char *uri, CalObjType type);

void e_cal_set_auth_func (ECal *ecal, ECalAuthFunc func, gpointer data);

gboolean e_cal_open (ECal *ecal, gboolean only_if_exists, GError **error);
void e_cal_open_async (ECal *ecal, gboolean only_if_exists);
gboolean e_cal_remove_calendar (ECal *ecal, GError **error);

GList *e_cal_uri_list (ECal *ecal, CalMode mode);

ECalLoadState e_cal_get_load_state (ECal *ecal);

const char *e_cal_get_uri (ECal *ecal);

gboolean e_cal_is_read_only (ECal *ecal, gboolean *read_only, GError **error);
gboolean e_cal_get_cal_address (ECal *ecal, char **cal_address, GError **error);
gboolean e_cal_get_alarm_email_address (ECal *ecal, char **alarm_address, GError **error);
gboolean e_cal_get_ldap_attribute (ECal *ecal, char **ldap_attribute, GError **error);

gboolean e_cal_get_one_alarm_only (ECal *ecal);
gboolean e_cal_get_organizer_must_attend (ECal *ecal);
gboolean e_cal_get_save_schedules (ECal *ecal);
gboolean e_cal_get_static_capability (ECal *ecal, const char *cap);

gboolean e_cal_set_mode (ECal *ecal, CalMode mode);

gboolean e_cal_get_default_object (ECal *ecal,
					icalcomponent **icalcomp, GError **error);

gboolean e_cal_get_object (ECal *ecal,
				const char *uid,
				const char *rid,
				icalcomponent **icalcomp,
				GError **error);

gboolean e_cal_get_changes (ECal *ecal, const char *change_id, GList **changes, GError **error);

gboolean e_cal_get_object_list (ECal *ecal, const char *query, GList **objects, GError **error);
gboolean e_cal_get_object_list_as_comp (ECal *ecal, const char *query, GList **objects, GError **error);
void e_cal_free_object_list (GList *objects);

gboolean e_cal_get_free_busy (ECal *ecal, GList *users, time_t start, time_t end, 
				   GList **freebusy, GError **error);

void e_cal_generate_instances (ECal *ecal, CalObjType type,
				    time_t start, time_t end,
				    ECalRecurInstanceFn cb, gpointer cb_data);

GSList *e_cal_get_alarms_in_range (ECal *ecal, time_t start, time_t end);

void e_cal_free_alarms (GSList *comp_alarms);

gboolean e_cal_get_alarms_for_object (ECal *ecal, const char *uid,
					   time_t start, time_t end,
					   ECalComponentAlarms **alarms);

gboolean e_cal_create_object (ECal *ecal, icalcomponent *icalcomp, char **uid, GError **error);
gboolean e_cal_modify_object (ECal *ecal, icalcomponent *icalcomp, CalObjModType mod, GError **error);
gboolean e_cal_remove_object (ECal *ecal, const char *uid, GError **error);
gboolean e_cal_remove_object_with_mod (ECal *ecal, const char *uid, const char *rid, CalObjModType mod, GError **error);

gboolean e_cal_discard_alarm (ECal *ecal, ECalComponent *comp, const char *auid, GError **error);

gboolean e_cal_receive_objects (ECal *ecal, icalcomponent *icalcomp, GError **error);
gboolean e_cal_send_objects (ECal *ecal, icalcomponent *icalcomp, GError **error);

gboolean e_cal_get_timezone (ECal *ecal, const char *tzid, icaltimezone **zone, GError **error);
gboolean e_cal_add_timezone (ECal *ecal, icaltimezone *izone, GError **error);
/* Sets the default timezone to use to resolve DATE and floating DATE-TIME
   values. This will typically be from the user's timezone setting. Call this
   before using any other functions. It will pass the default timezone on to
   the server. Returns TRUE on success. */
gboolean e_cal_set_default_timezone (ECal *ecal, icaltimezone *zone, GError **error);

gboolean e_cal_get_query (ECal *ecal, const char *sexp, ECalView **query, GError **error);

/* Resolves TZIDs for the recurrence generator. */
icaltimezone *e_cal_resolve_tzid_cb (const char *tzid, gpointer data);

/* Returns a complete VCALENDAR for a VEVENT/VTODO including all VTIMEZONEs
   used by the component. It also includes a 'METHOD:PUBLISH' property. */
char* e_cal_get_component_as_string (ECal *ecal, icalcomponent *icalcomp);

const char * e_cal_get_error_message (ECalendarStatus status);

G_END_DECLS

#endif

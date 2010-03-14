/* Evolution calendar utilities and types
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
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

#ifndef E_CAL_UTIL_H
#define E_CAL_UTIL_H

#include <libical/ical.h>
#include <time.h>
#include <glib.h>
#include <libecal/e-cal-component.h>
#include <libecal/e-cal-recur.h>

G_BEGIN_DECLS



/* Instance of a calendar object.  This can be an actual occurrence, a
 * recurrence, or an alarm trigger of a `real' calendar object.
 */
typedef struct {
	gchar *uid;			/* UID of the object */
	time_t start;			/* Start time of instance */
	time_t end;			/* End time of instance */
} CalObjInstance;

void cal_obj_instance_list_free (GList *list);

/* Used for modifying objects */
typedef enum {
	CALOBJ_MOD_THIS          = 1 << 0,
	CALOBJ_MOD_THISANDPRIOR  = 1 << 1,
	CALOBJ_MOD_THISANDFUTURE = 1 << 2,
	CALOBJ_MOD_ALL           = 0x07
} CalObjModType;

/* Used for mode stuff */
typedef enum {
	CAL_MODE_INVALID = -1,
	CAL_MODE_LOCAL   = 1 << 0,
	CAL_MODE_REMOTE  = 1 << 1,
	CAL_MODE_ANY     = 0x07
} CalMode;

#define cal_mode_to_corba(mode) \
	(mode == CAL_MODE_LOCAL   ? GNOME_Evolution_Calendar_MODE_LOCAL  : \
	 mode == CAL_MODE_REMOTE  ? GNOME_Evolution_Calendar_MODE_REMOTE : \
	 GNOME_Evolution_Calendar_MODE_ANY)

void cal_obj_uid_list_free (GList *list);

icalcomponent *e_cal_util_new_top_level (void);
icalcomponent *e_cal_util_new_component (icalcomponent_kind kind);

icalcomponent *e_cal_util_parse_ics_string (const gchar *string);
icalcomponent *e_cal_util_parse_ics_file (const gchar *filename);

ECalComponentAlarms *e_cal_util_generate_alarms_for_comp (ECalComponent *comp,
						       time_t start,
						       time_t end,
						       ECalComponentAlarmAction *omit,
						       ECalRecurResolveTimezoneFn resolve_tzid,
						       gpointer user_data,
						       icaltimezone *default_timezone);
gint e_cal_util_generate_alarms_for_list (GList *comps,
				       time_t start,
				       time_t end,
				       ECalComponentAlarmAction *omit,
				       GSList **comp_alarms,
				       ECalRecurResolveTimezoneFn resolve_tzid,
				       gpointer user_data,
				       icaltimezone *default_timezone);

const gchar *e_cal_util_priority_to_string (gint priority);
gint e_cal_util_priority_from_string (const gchar *string);

void e_cal_util_add_timezones_from_component (icalcomponent *vcal_comp,
					    icalcomponent *icalcomp);

gboolean e_cal_util_component_is_instance (icalcomponent *icalcomp);
gboolean e_cal_util_component_has_alarms (icalcomponent *icalcomp);
gboolean e_cal_util_component_has_organizer (icalcomponent *icalcomp);
gboolean e_cal_util_component_has_recurrences (icalcomponent *icalcomp);
gboolean e_cal_util_component_has_rdates (icalcomponent *icalcomp);
gboolean e_cal_util_component_has_rrules (icalcomponent *icalcomp);
gboolean e_cal_util_component_has_attendee (icalcomponent *icalcomp);
gboolean e_cal_util_event_dates_match (icalcomponent *icalcomp1, icalcomponent *icalcomp2);
/* The static capabilities to be supported by backends */
#define CAL_STATIC_CAPABILITY_NO_ALARM_REPEAT             "no-alarm-repeat"
#define CAL_STATIC_CAPABILITY_NO_AUDIO_ALARMS             "no-audio-alarms"
#define CAL_STATIC_CAPABILITY_NO_DISPLAY_ALARMS           "no-display-alarms"
#define CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS             "no-email-alarms"
#define CAL_STATIC_CAPABILITY_NO_PROCEDURE_ALARMS         "no-procedure-alarms"
#define CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT          "no-task-assignment"
#define CAL_STATIC_CAPABILITY_NO_THISANDFUTURE            "no-thisandfuture"
#define CAL_STATIC_CAPABILITY_NO_THISANDPRIOR             "no-thisandprior"
#define CAL_STATIC_CAPABILITY_NO_TRANSPARENCY             "no-transparency"
#define CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY              "one-alarm-only"
#define CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ATTEND       "organizer-must-attend"
#define CAL_STATIC_CAPABILITY_ORGANIZER_NOT_EMAIL_ADDRESS "organizer-not-email-address"
#define CAL_STATIC_CAPABILITY_REMOVE_ALARMS               "remove-alarms"

/**
 * CAL_STATIC_CAPABILITY_CREATE_MESSAGES:
 *
 * Since: 2.26
 **/
#define CAL_STATIC_CAPABILITY_CREATE_MESSAGES             "create-messages"

#define CAL_STATIC_CAPABILITY_SAVE_SCHEDULES              "save-schedules"
#define CAL_STATIC_CAPABILITY_NO_CONV_TO_ASSIGN_TASK	  "no-conv-to-assign-task"
#define CAL_STATIC_CAPABILITY_NO_CONV_TO_RECUR		  "no-conv-to-recur"
#define CAL_STATIC_CAPABILITY_NO_GEN_OPTIONS		  "no-general-options"
#define CAL_STATIC_CAPABILITY_REQ_SEND_OPTIONS		  "require-send-options"
#define CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER       "recurrences-no-master-object"
#define CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ACCEPT      "organizer-must-accept"
#define CAL_STATIC_CAPABILITY_DELEGATE_SUPPORTED	 "delegate-support"
#define CAL_STATIC_CAPABILITY_NO_ORGANIZER		 "no-organizer"
#define CAL_STATIC_CAPABILITY_DELEGATE_TO_MANY		 "delegate-to-many"
#define CAL_STATIC_CAPABILITY_HAS_UNACCEPTED_MEETING     "has-unaccepted-meeting"

/**
 * CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED:
 *
 * Since: 2.30
 **/
#define CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED		"refresh-supported"

/* Recurrent events. Management for instances */
icalcomponent *e_cal_util_construct_instance (icalcomponent *icalcomp,
					    struct icaltimetype rid);
void           e_cal_util_remove_instances (icalcomponent *icalcomp,
					  struct icaltimetype rid,
					  CalObjModType mod);

gchar *e_cal_util_get_system_timezone_location (void);
icaltimezone *e_cal_util_get_system_timezone (void);

G_END_DECLS

#endif

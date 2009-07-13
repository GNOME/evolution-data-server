/* Evolution calendar - iCalendar component object
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

#ifndef E_CAL_COMPONENT_H
#define E_CAL_COMPONENT_H

#include <time.h>
#include <glib-object.h>
#include <libical/ical.h>

G_BEGIN_DECLS



#define E_TYPE_CAL_COMPONENT            (e_cal_component_get_type ())
#define E_CAL_COMPONENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_COMPONENT, ECalComponent))
#define E_CAL_COMPONENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_COMPONENT,	\
				       ECalComponentClass))
#define E_IS_CAL_COMPONENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_COMPONENT))
#define E_IS_CAL_COMPONENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_COMPONENT))

typedef struct {
	gchar *uid;
	gchar *rid;
} ECalComponentId;

/* Types of calendar components to be stored by a ECalComponent, as per RFC 2445.
 * We don't put the alarm component type here since we store alarms as separate
 * structures inside the other "real" components.
 */
typedef enum {
	E_CAL_COMPONENT_NO_TYPE,
	E_CAL_COMPONENT_EVENT,
	E_CAL_COMPONENT_TODO,
	E_CAL_COMPONENT_JOURNAL,
	E_CAL_COMPONENT_FREEBUSY,
	E_CAL_COMPONENT_TIMEZONE
} ECalComponentVType;

/* Field identifiers for a calendar component; these are used by the data model
 * for ETable.
 *
 * NOTE: These are also used in the ETable specification, and the column
 *       numbers are saved in the user settings file. So don't reorder them!
 */
typedef enum {
	E_CAL_COMPONENT_FIELD_CATEGORIES,		/* concatenation of the categories list */
	E_CAL_COMPONENT_FIELD_CLASSIFICATION,
	E_CAL_COMPONENT_FIELD_COMPLETED,
	E_CAL_COMPONENT_FIELD_DTEND,
	E_CAL_COMPONENT_FIELD_DTSTART,
	E_CAL_COMPONENT_FIELD_DUE,
	E_CAL_COMPONENT_FIELD_GEO,
	E_CAL_COMPONENT_FIELD_PERCENT,
	E_CAL_COMPONENT_FIELD_PRIORITY,
	E_CAL_COMPONENT_FIELD_SUMMARY,
	E_CAL_COMPONENT_FIELD_TRANSPARENCY,
	E_CAL_COMPONENT_FIELD_URL,
	E_CAL_COMPONENT_FIELD_HAS_ALARMS,		/* not a real field */
	E_CAL_COMPONENT_FIELD_ICON,		/* not a real field */
	E_CAL_COMPONENT_FIELD_COMPLETE,		/* not a real field */
	E_CAL_COMPONENT_FIELD_RECURRING,		/* not a real field */
	E_CAL_COMPONENT_FIELD_OVERDUE,		/* not a real field */
	E_CAL_COMPONENT_FIELD_COLOR,		/* not a real field */
	E_CAL_COMPONENT_FIELD_STATUS,
	E_CAL_COMPONENT_FIELD_COMPONENT,		/* not a real field */
	E_CAL_COMPONENT_FIELD_LOCATION,
	E_CAL_COMPONENT_FIELD_NUM_FIELDS
} ECalComponentField;

/* Structures and enumerations to return properties and their parameters */

/* CLASSIFICATION property */
typedef enum {
	E_CAL_COMPONENT_CLASS_NONE,
	E_CAL_COMPONENT_CLASS_PUBLIC,
	E_CAL_COMPONENT_CLASS_PRIVATE,
	E_CAL_COMPONENT_CLASS_CONFIDENTIAL,
	E_CAL_COMPONENT_CLASS_UNKNOWN
} ECalComponentClassification;

/* Properties that have time and timezone information */
typedef struct {
	/* Actual date/time value */
	struct icaltimetype *value;

	/* Timezone ID */
	const gchar *tzid;
} ECalComponentDateTime;

/* Way in which a period of time is specified */
typedef enum {
	E_CAL_COMPONENT_PERIOD_DATETIME,
	E_CAL_COMPONENT_PERIOD_DURATION
} ECalComponentPeriodType;

/* Period of time, can have explicit start/end times or start/duration instead */
typedef struct {
	ECalComponentPeriodType type;

	struct icaltimetype start;

	union {
		struct icaltimetype end;
		struct icaldurationtype duration;
	} u;
} ECalComponentPeriod;

/* The type of range */
typedef enum {
	E_CAL_COMPONENT_RANGE_SINGLE,
	E_CAL_COMPONENT_RANGE_THISPRIOR,
	E_CAL_COMPONENT_RANGE_THISFUTURE
} ECalComponentRangeType;

typedef struct {
	ECalComponentRangeType type;

	ECalComponentDateTime datetime;
} ECalComponentRange;

/* Text properties */
typedef struct {
	/* Description string */
	const gchar *value;

	/* Alternate representation URI */
	const gchar *altrep;
} ECalComponentText;

/* Time transparency */
typedef enum {
	E_CAL_COMPONENT_TRANSP_NONE,
	E_CAL_COMPONENT_TRANSP_TRANSPARENT,
	E_CAL_COMPONENT_TRANSP_OPAQUE,
	E_CAL_COMPONENT_TRANSP_UNKNOWN
} ECalComponentTransparency;

/* Organizer & Attendee */
typedef struct {
	const gchar *value;

	const gchar *member;
	icalparameter_cutype cutype;
	icalparameter_role role;
	icalparameter_partstat status;
	gboolean rsvp;

	const gchar *delto;
	const gchar *delfrom;
	const gchar *sentby;
	const gchar *cn;
	const gchar *language;
} ECalComponentAttendee;

typedef struct {
	const gchar *value;
	const gchar *sentby;
	const gchar *cn;
	const gchar *language;
} ECalComponentOrganizer;

/* Main calendar component object */

typedef struct _ECalComponent ECalComponent;
typedef struct _ECalComponentClass ECalComponentClass;

typedef struct _ECalComponentPrivate ECalComponentPrivate;

struct _ECalComponent {
	GObject object;

	/*< private >*/
	ECalComponentPrivate *priv;
};

struct _ECalComponentClass {
	GObjectClass parent_class;
};

/* Calendar component */

GType e_cal_component_get_type (void);

gchar *e_cal_component_gen_uid (void);

ECalComponent *e_cal_component_new (void);

ECalComponent *e_cal_component_new_from_string (const gchar *calobj);

ECalComponent *e_cal_component_clone (ECalComponent *comp);

void e_cal_component_set_new_vtype (ECalComponent *comp, ECalComponentVType type);

gboolean e_cal_component_set_icalcomponent (ECalComponent *comp, icalcomponent *icalcomp);
icalcomponent *e_cal_component_get_icalcomponent (ECalComponent *comp);
void e_cal_component_rescan (ECalComponent *comp);
void e_cal_component_strip_errors (ECalComponent *comp);

ECalComponentVType e_cal_component_get_vtype (ECalComponent *comp);

gchar *e_cal_component_get_as_string (ECalComponent *comp);

void e_cal_component_commit_sequence (ECalComponent *comp);
void e_cal_component_abort_sequence (ECalComponent *comp);

void e_cal_component_get_uid (ECalComponent *comp, const gchar **uid);
void e_cal_component_set_uid (ECalComponent *comp, const gchar *uid);

ECalComponentId *e_cal_component_get_id (ECalComponent *comp);
void e_cal_component_free_id (ECalComponentId *id);

void e_cal_component_get_categories (ECalComponent *comp, const gchar **categories);
void e_cal_component_set_categories (ECalComponent *comp, const gchar *categories);
void e_cal_component_get_categories_list (ECalComponent *comp, GSList **categ_list);
void e_cal_component_set_categories_list (ECalComponent *comp, GSList *categ_list);

void e_cal_component_get_classification (ECalComponent *comp, ECalComponentClassification *classif);
void e_cal_component_set_classification (ECalComponent *comp, ECalComponentClassification classif);

void e_cal_component_get_comment_list (ECalComponent *comp, GSList **text_list);
void e_cal_component_set_comment_list (ECalComponent *comp, GSList *text_list);

void e_cal_component_get_completed (ECalComponent *comp, struct icaltimetype **t);
void e_cal_component_set_completed (ECalComponent *comp, struct icaltimetype *t);

void e_cal_component_get_contact_list (ECalComponent *comp, GSList **text_list);
void e_cal_component_set_contact_list (ECalComponent *comp, GSList *text_list);

void e_cal_component_get_created (ECalComponent *comp, struct icaltimetype **t);
void e_cal_component_set_created (ECalComponent *comp, struct icaltimetype *t);

void e_cal_component_get_description_list (ECalComponent *comp, GSList **text_list);
void e_cal_component_set_description_list (ECalComponent *comp, GSList *text_list);

void e_cal_component_get_dtend (ECalComponent *comp, ECalComponentDateTime *dt);
void e_cal_component_set_dtend (ECalComponent *comp, ECalComponentDateTime *dt);

void e_cal_component_get_dtstamp (ECalComponent *comp, struct icaltimetype *t);
void e_cal_component_set_dtstamp (ECalComponent *comp, struct icaltimetype *t);

void e_cal_component_get_dtstart (ECalComponent *comp, ECalComponentDateTime *dt);
void e_cal_component_set_dtstart (ECalComponent *comp, ECalComponentDateTime *dt);

void e_cal_component_get_due (ECalComponent *comp, ECalComponentDateTime *dt);
void e_cal_component_set_due (ECalComponent *comp, ECalComponentDateTime *dt);

void e_cal_component_get_exdate_list (ECalComponent *comp, GSList **exdate_list);
void e_cal_component_set_exdate_list (ECalComponent *comp, GSList *exdate_list);
gboolean e_cal_component_has_exdates (ECalComponent *comp);

void e_cal_component_get_exrule_list (ECalComponent *comp, GSList **recur_list);
void e_cal_component_get_exrule_property_list (ECalComponent *comp, GSList **recur_list);
void e_cal_component_set_exrule_list (ECalComponent *comp, GSList *recur_list);
gboolean e_cal_component_has_exrules (ECalComponent *comp);

gboolean e_cal_component_has_exceptions (ECalComponent *comp);

void e_cal_component_get_geo (ECalComponent *comp, struct icalgeotype **geo);
void e_cal_component_set_geo (ECalComponent *comp, struct icalgeotype *geo);

void e_cal_component_get_last_modified (ECalComponent *comp, struct icaltimetype **t);
void e_cal_component_set_last_modified (ECalComponent *comp, struct icaltimetype *t);

void e_cal_component_get_organizer (ECalComponent *comp, ECalComponentOrganizer *organizer);
void e_cal_component_set_organizer (ECalComponent *comp, ECalComponentOrganizer *organizer);
gboolean e_cal_component_has_organizer (ECalComponent *comp);

gint  e_cal_component_get_percent_as_int (ECalComponent *comp);
void e_cal_component_set_percent_as_int (ECalComponent *comp, gint percent);

void e_cal_component_get_percent (ECalComponent *comp, gint **percent);
void e_cal_component_set_percent (ECalComponent *comp, gint *percent);

void e_cal_component_get_priority (ECalComponent *comp, gint **priority);
void e_cal_component_set_priority (ECalComponent *comp, gint *priority);

void e_cal_component_get_recurid (ECalComponent *comp, ECalComponentRange *recur_id);
gchar *e_cal_component_get_recurid_as_string (ECalComponent *comp);
void e_cal_component_set_recurid (ECalComponent *comp, ECalComponentRange *recur_id);

void e_cal_component_get_rdate_list (ECalComponent *comp, GSList **period_list);
void e_cal_component_set_rdate_list (ECalComponent *comp, GSList *period_list);
gboolean e_cal_component_has_rdates (ECalComponent *comp);

void e_cal_component_get_rrule_list (ECalComponent *comp, GSList **recur_list);
void e_cal_component_get_rrule_property_list (ECalComponent *comp, GSList **recur_list);
void e_cal_component_set_rrule_list (ECalComponent *comp, GSList *recur_list);
gboolean e_cal_component_has_rrules (ECalComponent *comp);

gboolean e_cal_component_has_recurrences (ECalComponent *comp);
gboolean e_cal_component_has_simple_recurrence (ECalComponent *comp);
gboolean e_cal_component_is_instance (ECalComponent *comp);

void e_cal_component_get_sequence (ECalComponent *comp, gint **sequence);
void e_cal_component_set_sequence (ECalComponent *comp, gint *sequence);

void e_cal_component_get_status (ECalComponent *comp, icalproperty_status *status);
void e_cal_component_set_status (ECalComponent *comp, icalproperty_status status);

void e_cal_component_get_summary (ECalComponent *comp, ECalComponentText *summary);
void e_cal_component_set_summary (ECalComponent *comp, ECalComponentText *summary);

void e_cal_component_get_transparency (ECalComponent *comp, ECalComponentTransparency *transp);
void e_cal_component_set_transparency (ECalComponent *comp, ECalComponentTransparency transp);

void e_cal_component_get_url (ECalComponent *comp, const gchar **url);
void e_cal_component_set_url (ECalComponent *comp, const gchar *url);

void e_cal_component_get_attendee_list (ECalComponent *comp, GSList **attendee_list);
void e_cal_component_set_attendee_list (ECalComponent *comp, GSList *attendee_list);
gboolean e_cal_component_has_attendees (ECalComponent *comp);

void e_cal_component_get_location (ECalComponent *comp, const gchar **location);
void e_cal_component_set_location (ECalComponent *comp, const gchar *location);

/* Attachment handling */
void e_cal_component_get_attachment_list (ECalComponent *comp, GSList **attachment_list);
void e_cal_component_set_attachment_list (ECalComponent *comp, GSList *attachment_list);
gboolean e_cal_component_has_attachments (ECalComponent *comp);
gint e_cal_component_get_num_attachments (ECalComponent *comp);

gboolean e_cal_component_event_dates_match (ECalComponent *comp1, ECalComponent *comp2);

/* Functions to free returned values */

void e_cal_component_free_categories_list (GSList *categ_list);
void e_cal_component_free_datetime (ECalComponentDateTime *dt);
void e_cal_component_free_range (ECalComponentRange *range);
void e_cal_component_free_exdate_list (GSList *exdate_list);
void e_cal_component_free_geo (struct icalgeotype *geo);
void e_cal_component_free_icaltimetype (struct icaltimetype *t);
void e_cal_component_free_percent (gint *percent);
void e_cal_component_free_priority (gint *priority);
void e_cal_component_free_period_list (GSList *period_list);
void e_cal_component_free_recur_list (GSList *recur_list);
void e_cal_component_free_sequence (gint *sequence);
void e_cal_component_free_text_list (GSList *text_list);
void e_cal_component_free_attendee_list (GSList *attendee_list);

/* Alarms */

/* Opaque structure used to represent alarm subcomponents */
typedef struct _ECalComponentAlarm ECalComponentAlarm;

/* An alarm occurrence, i.e. a trigger instance */
typedef struct {
	/* UID of the alarm that triggered */
	gchar *auid;

	/* Trigger time, i.e. "5 minutes before the appointment" */
	time_t trigger;

	/* Actual event occurrence to which this trigger corresponds */
	time_t occur_start;
	time_t occur_end;
} ECalComponentAlarmInstance;

/* Alarm trigger instances for a particular component */
typedef struct {
	/* The actual component */
	ECalComponent *comp;

	/* List of ECalComponentAlarmInstance structures */
	GSList *alarms;
} ECalComponentAlarms;

/* Alarm types */
typedef enum {
	E_CAL_COMPONENT_ALARM_NONE,
	E_CAL_COMPONENT_ALARM_AUDIO,
	E_CAL_COMPONENT_ALARM_DISPLAY,
	E_CAL_COMPONENT_ALARM_EMAIL,
	E_CAL_COMPONENT_ALARM_PROCEDURE,
	E_CAL_COMPONENT_ALARM_UNKNOWN
} ECalComponentAlarmAction;

/* Whether a trigger is relative to the start or end of an event occurrence, or
 * whether it is specified to occur at an absolute time.
 */
typedef enum {
	E_CAL_COMPONENT_ALARM_TRIGGER_NONE,
	E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START,
	E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END,
	E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE
} ECalComponentAlarmTriggerType;

typedef struct {
	ECalComponentAlarmTriggerType type;

	union {
		struct icaldurationtype rel_duration;
		struct icaltimetype abs_time;
	} u;
} ECalComponentAlarmTrigger;

typedef struct {
	/* Number of extra repetitions, zero for none */
	gint repetitions;

	/* Interval between repetitions */
	struct icaldurationtype duration;
} ECalComponentAlarmRepeat;

gboolean e_cal_component_has_alarms (ECalComponent *comp);
void e_cal_component_add_alarm (ECalComponent *comp, ECalComponentAlarm *alarm);
void e_cal_component_remove_alarm (ECalComponent *comp, const gchar *auid);
void e_cal_component_remove_all_alarms (ECalComponent *comp);

GList *e_cal_component_get_alarm_uids (ECalComponent *comp);
ECalComponentAlarm *e_cal_component_get_alarm (ECalComponent *comp, const gchar *auid);

void e_cal_component_alarms_free (ECalComponentAlarms *alarms);

/* ECalComponentAlarms */
ECalComponentAlarm *e_cal_component_alarm_new (void);
ECalComponentAlarm *e_cal_component_alarm_clone (ECalComponentAlarm *alarm);
void e_cal_component_alarm_free (ECalComponentAlarm *alarm);

const gchar *e_cal_component_alarm_get_uid (ECalComponentAlarm *alarm);

void e_cal_component_alarm_get_action (ECalComponentAlarm *alarm, ECalComponentAlarmAction *action);
void e_cal_component_alarm_set_action (ECalComponentAlarm *alarm, ECalComponentAlarmAction action);

void e_cal_component_alarm_get_attach (ECalComponentAlarm *alarm, icalattach **attach);
void e_cal_component_alarm_set_attach (ECalComponentAlarm *alarm, icalattach *attach);

void e_cal_component_alarm_get_description (ECalComponentAlarm *alarm, ECalComponentText *description);
void e_cal_component_alarm_set_description (ECalComponentAlarm *alarm, ECalComponentText *description);

void e_cal_component_alarm_get_repeat (ECalComponentAlarm *alarm, ECalComponentAlarmRepeat *repeat);
void e_cal_component_alarm_set_repeat (ECalComponentAlarm *alarm, ECalComponentAlarmRepeat repeat);

void e_cal_component_alarm_get_trigger (ECalComponentAlarm *alarm, ECalComponentAlarmTrigger *trigger);
void e_cal_component_alarm_set_trigger (ECalComponentAlarm *alarm, ECalComponentAlarmTrigger trigger);

void e_cal_component_alarm_get_attendee_list (ECalComponentAlarm *alarm, GSList **attendee_list);
void e_cal_component_alarm_set_attendee_list (ECalComponentAlarm *alarm, GSList *attendee_list);
gboolean e_cal_component_alarm_has_attendees (ECalComponentAlarm *alarm);

icalcomponent *e_cal_component_alarm_get_icalcomponent (ECalComponentAlarm *alarm);



G_END_DECLS

#endif

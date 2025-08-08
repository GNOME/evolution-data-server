/* Evolution calendar utilities and types
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 */

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_UTIL_H
#define E_CAL_UTIL_H

#include <libical-glib/libical-glib.h>
#include <time.h>
#include <libedataserver/libedataserver.h>
#include <libecal/e-cal-component.h>
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-enums.h>

G_BEGIN_DECLS

/* The static capabilities to be supported by backends */
#define E_CAL_STATIC_CAPABILITY_NO_ALARM_REPEAT		"no-alarm-repeat"
#define E_CAL_STATIC_CAPABILITY_NO_AUDIO_ALARMS		"no-audio-alarms"
#define E_CAL_STATIC_CAPABILITY_NO_DISPLAY_ALARMS	"no-display-alarms"
#define E_CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS		"no-email-alarms"
#define E_CAL_STATIC_CAPABILITY_NO_PROCEDURE_ALARMS	"no-procedure-alarms"
#define E_CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT	"no-task-assignment"
#define E_CAL_STATIC_CAPABILITY_NO_THISANDFUTURE	"no-thisandfuture"
#define E_CAL_STATIC_CAPABILITY_NO_THISANDPRIOR		"no-thisandprior"
#define E_CAL_STATIC_CAPABILITY_NO_TRANSPARENCY		"no-transparency"

/**
 * E_CAL_STATIC_CAPABILITY_MEMO_START_DATE:
 *
 * Flag indicating that the backend does not support memo's start date
 *
 * Since: 3.12
 */
#define E_CAL_STATIC_CAPABILITY_NO_MEMO_START_DATE	"no-memo-start-date"

/**
 * E_CAL_STATIC_CAPABILITY_ALARM_DESCRIPTION:
 *
 * Flag indicating that the backend supports alarm description
 *
 * Since: 3.8
 */
#define E_CAL_STATIC_CAPABILITY_ALARM_DESCRIPTION	"alarm-description"

/**
 * E_CAL_STATIC_CAPABILITY_NO_ALARM_AFTER_START:
 *
 * Flag indicating that the backend does not support alarm after start the event
 *
 * Since: 3.8
 */
#define E_CAL_STATIC_CAPABILITY_NO_ALARM_AFTER_START	"no-alarm-after-start"

/**
 * E_CAL_STATIC_CAPABILITY_BULK_ADDS:
 *
 * Flag indicating that the backend supports bulk additions.
 *
 * Since: 3.6
 */
#define E_CAL_STATIC_CAPABILITY_BULK_ADDS		"bulk-adds"

/**
 * E_CAL_STATIC_CAPABILITY_BULK_MODIFIES:
 *
 * Flag indicating that the backend supports bulk modifications.
 *
 * Since: 3.6
 */
#define E_CAL_STATIC_CAPABILITY_BULK_MODIFIES		"bulk-modifies"

/**
 * E_CAL_STATIC_CAPABILITY_BULK_REMOVES:
 *
 * Flag indicating that the backend supports bulk removals.
 *
 * Since: 3.6
 */
#define E_CAL_STATIC_CAPABILITY_BULK_REMOVES		"bulk-removes"

/**
 * E_CAL_STATIC_CAPABILITY_REMOVE_ONLY_THIS:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
#define E_CAL_STATIC_CAPABILITY_REMOVE_ONLY_THIS	"remove-only-this"

#define E_CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY		"one-alarm-only"
#define E_CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ATTEND	"organizer-must-attend"
#define E_CAL_STATIC_CAPABILITY_ORGANIZER_NOT_EMAIL_ADDRESS	"organizer-not-email-address"
#define E_CAL_STATIC_CAPABILITY_REMOVE_ALARMS		"remove-alarms"

/**
 * E_CAL_STATIC_CAPABILITY_CREATE_MESSAGES:
 *
 * Since: 2.26
 **/
#define E_CAL_STATIC_CAPABILITY_CREATE_MESSAGES		"create-messages"

#define E_CAL_STATIC_CAPABILITY_SAVE_SCHEDULES		"save-schedules"
#define E_CAL_STATIC_CAPABILITY_NO_CONV_TO_ASSIGN_TASK	"no-conv-to-assign-task"
#define E_CAL_STATIC_CAPABILITY_NO_CONV_TO_RECUR	"no-conv-to-recur"
#define E_CAL_STATIC_CAPABILITY_NO_GEN_OPTIONS		"no-general-options"
#define E_CAL_STATIC_CAPABILITY_REQ_SEND_OPTIONS	"require-send-options"
#define E_CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER	"recurrences-no-master-object"
#define E_CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ACCEPT	"organizer-must-accept"
#define E_CAL_STATIC_CAPABILITY_DELEGATE_SUPPORTED	"delegate-support"
#define E_CAL_STATIC_CAPABILITY_NO_ORGANIZER		"no-organizer"
#define E_CAL_STATIC_CAPABILITY_DELEGATE_TO_MANY	"delegate-to-many"
#define E_CAL_STATIC_CAPABILITY_HAS_UNACCEPTED_MEETING	"has-unaccepted-meeting"

/**
 * E_CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED:
 *
 * Since: 2.30
 **/
#define E_CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED	"refresh-supported"

/**
 * E_CAL_STATIC_CAPABILITY_ALL_DAY_EVENT_AS_TIME:
 *
 * Let the client know that it should store All Day event times as time
 * with a time zone, rather than as a date.
 *
 * Since: 3.18
 **/
#define E_CAL_STATIC_CAPABILITY_ALL_DAY_EVENT_AS_TIME	"all-day-event-as-time"

/**
 * E_CAL_STATIC_CAPABILITY_TASK_DATE_ONLY:
 *
 * Let the client know that the Task Start date, Due date and Completed date
 * can be entered only as dates. When the capability is not set, then these
 * can be date and time.
 *
 * Since: 3.24
 **/
#define E_CAL_STATIC_CAPABILITY_TASK_DATE_ONLY		"task-date-only"

/**
 * E_CAL_STATIC_CAPABILITY_TASK_CAN_RECUR:
 *
 * When the capability is set, the client can store and provide recurring
 * tasks, otherwise it cannot.
 *
 * Since: 3.30
 **/
#define E_CAL_STATIC_CAPABILITY_TASK_CAN_RECUR		"task-can-recur"

/**
 * E_CAL_STATIC_CAPABILITY_TASK_NO_ALARM:
 *
 * When the capability is set, the client cannot store reminders
 * on tasks, otherwise it can.
 *
 * Since: 3.30
 **/
#define E_CAL_STATIC_CAPABILITY_TASK_NO_ALARM		"task-no-alarm"

/**
 * E_CAL_STATIC_CAPABILITY_COMPONENT_COLOR:
 *
 * When the capability is set, the client supports storing color
 * for individual components.
 *
 * Since: 3.30
 **/
#define E_CAL_STATIC_CAPABILITY_COMPONENT_COLOR		"component-color"

/**
 * E_CAL_STATIC_CAPABILITY_TASK_HANDLE_RECUR:
 *
 * When the capability is set, the backend handles task recurrence
 * completion on its own. This does not imply E_CAL_STATIC_CAPABILITY_TASK_CAN_RECUR.
 *
 * Since: 3.34
 **/
#define E_CAL_STATIC_CAPABILITY_TASK_HANDLE_RECUR	"task-handle-recur"

/**
 * E_CAL_STATIC_CAPABILITY_SIMPLE_MEMO:
 *
 * When the capability is set, the backend handles only simple memos,
 * which means it stores only memo description. The summary can be changed
 * by the backend, if needed.
 *
 * Since: 3.38
 **/
#define E_CAL_STATIC_CAPABILITY_SIMPLE_MEMO		"simple-memo"

/**
 * E_CAL_STATIC_CAPABILITY_SIMPLE_MEMO_WITH_SUMMARY:
 *
 * Similar to the %E_CAL_STATIC_CAPABILITY_SIMPLE_MEMO, except the backend stores
 * the description and the summary separately.
 *
 * Since: 3.42
 **/

#define E_CAL_STATIC_CAPABILITY_SIMPLE_MEMO_WITH_SUMMARY "simple-memo-with-summary"

/**
 * E_CAL_STATIC_CAPABILITY_TASK_ESTIMATED_DURATION:
 *
 * Set, when the backend supports ESTIMATED-DURATION property for tasks.
 *
 * Since: 3.44
 **/

#define E_CAL_STATIC_CAPABILITY_TASK_ESTIMATED_DURATION "task-estimated-duration"

/**
 * E_CAL_STATIC_CAPABILITY_ITIP_SUPPRESS_ON_REMOVE_SUPPORTED:
 *
 * Set, when the backend supports %E_CAL_STATIC_CAPABILITY_SAVE_SCHEDULES and
 * it can suppress iTip message on component removal. The capability should
 * be ignored when the %E_CAL_STATIC_CAPABILITY_SAVE_SCHEDULES is not present.
 *
 * The backend checks %E_CAL_OPERATION_FLAG_DISABLE_ITIP_MESSAGE flag when these
 * capabilities are present and sends or does not send iTip message accordingly.
 *
 * Since: 3.50
 **/
#define E_CAL_STATIC_CAPABILITY_ITIP_SUPPRESS_ON_REMOVE_SUPPORTED "itip-suppress-on-remove-supported"

/**
 * E_CAL_STATIC_CAPABILITY_RETRACT_SUPPORTED:
 *
 * Set, when the backend supports retract. That's a way to ask for a meeting
 * deletion with a comment, which is stored in a component as the COMMENT property.
 *
 * Since: 3.50
 **/
#define E_CAL_STATIC_CAPABILITY_RETRACT_SUPPORTED "retract-supported"

/**
 * E_CAL_STATIC_CAPABILITY_USER_IS_ORGANIZER_ONLY:
 *
 * Set, when the backend can save meetings only if the organizer is
 * the calendar user.
 *
 * Since: 3.56
 **/
#define E_CAL_STATIC_CAPABILITY_USER_IS_ORGANIZER_ONLY "user-is-organizer-only"

struct _ECalClient;

ICalComponent *	e_cal_util_new_top_level	(void);
ICalComponent *	e_cal_util_new_component	(ICalComponentKind kind);
ICalTimezone *	e_cal_util_copy_timezone	(const ICalTimezone *zone);

ICalComponent *	e_cal_util_parse_ics_string	(const gchar *string);
ICalComponent *	e_cal_util_parse_ics_file	(const gchar *filename);

gboolean	e_cal_util_has_alarms_in_range	(ECalComponent *comp,
						 time_t start,
						 time_t end,
						 ECalComponentAlarmAction *omit,
						 ECalRecurResolveTimezoneCb resolve_tzid,
						 gpointer user_data,
						 ICalTimezone *default_timezone);
ECalComponentAlarms *
		e_cal_util_generate_alarms_for_comp
						(ECalComponent *comp,
						 time_t start,
						 time_t end,
						 ECalComponentAlarmAction *omit,
						 ECalRecurResolveTimezoneCb resolve_tzid,
						 gpointer user_data,
						 ICalTimezone *default_timezone);
gint		e_cal_util_generate_alarms_for_list
						(GList *comps, /* ECalComponent * */
						 time_t start,
						 time_t end,
						 ECalComponentAlarmAction *omit,
						 GSList **comp_alarms,
						 ECalRecurResolveTimezoneCb resolve_tzid,
						 gpointer user_data,
						 ICalTimezone *default_timezone);
ECalComponentAlarms *
		e_cal_util_generate_alarms_for_uid_sync
						(struct _ECalClient *client,
						 const gchar *uid,
						 time_t start,
						 time_t end,
						 ECalComponentAlarmAction *omit,
						 ECalRecurResolveTimezoneCb resolve_tzid,
						 gpointer user_data,
						 ICalTimezone *default_timezone,
						 gint def_reminder_before_start_seconds,
						 GCancellable *cancellable,
						 GError **error);
const gchar *	e_cal_util_priority_to_string	(gint priority);
gint		e_cal_util_priority_from_string	(const gchar *string);

gchar *		e_cal_util_seconds_to_string	(gint64 seconds);

void		e_cal_util_add_timezones_from_component
						(ICalComponent *vcal_comp,
						 ICalComponent *icalcomp);

gboolean	e_cal_util_property_has_parameter
						(ICalProperty *prop,
						 ICalParameterKind param_kind);
gboolean	e_cal_util_component_has_property
						(ICalComponent *icalcomp,
						 ICalPropertyKind prop_kind);
gboolean	e_cal_util_component_is_instance
						(ICalComponent *icalcomp);
gboolean	e_cal_util_component_has_alarms	(ICalComponent *icalcomp);
gboolean	e_cal_util_component_has_organizer
						(ICalComponent *icalcomp);
gboolean	e_cal_util_component_has_recurrences
						(ICalComponent *icalcomp);
gboolean	e_cal_util_component_has_rdates	(ICalComponent *icalcomp);
gboolean	e_cal_util_component_has_rrules	(ICalComponent *icalcomp);
gboolean	e_cal_util_component_has_attendee
						(ICalComponent *icalcomp);
gchar *		e_cal_util_component_get_recurid_as_string
						(ICalComponent *icalcomp);
ICalComponent *	e_cal_util_construct_instance	(ICalComponent *icalcomp,
						 const ICalTime *rid);
/* #ifndef EDS_DISABLE_DEPRECATED */ /* Fully deprecate it for 3.40 */
void		e_cal_util_remove_instances	(ICalComponent *icalcomp,
						 const ICalTime *rid,
						 ECalObjModType mod);
ICalComponent *	e_cal_util_split_at_instance	(ICalComponent *icalcomp,
						 const ICalTime *rid,
						 const ICalTime *master_dtstart);
/* #endif / * EDS_DISABLE_DEPRECATED */
void		e_cal_util_normalize_rrule_until_value
						(ICalComponent *icalcomp,
						 ICalTime *ttuntil,
						 ECalRecurResolveTimezoneCb tz_cb,
						 gpointer tz_cb_data);
void		e_cal_util_remove_instances_ex	(ICalComponent *icalcomp,
						 const ICalTime *rid,
						 ECalObjModType mod,
						 ECalRecurResolveTimezoneCb tz_cb,
						 gpointer tz_cb_data);
ICalComponent *	e_cal_util_split_at_instance_ex	(ICalComponent *icalcomp,
						 const ICalTime *rid,
						 const ICalTime *master_dtstart,
						 ECalRecurResolveTimezoneCb tz_cb,
						 gpointer tz_cb_data);
gboolean	e_cal_util_is_first_instance	(ECalComponent *comp,
						 const ICalTime *rid,
						 ECalRecurResolveTimezoneCb tz_cb,
						 gpointer tz_cb_data);
gboolean	e_cal_util_check_may_remove_all	(ECalComponent *comp,
						 GSList *detached_instances, /* ECalComponent * */
						 const ICalTime *rid,
						 ECalObjModType mod,
						 ECalRecurResolveTimezoneCb tz_cb,
						 gpointer tz_cb_data);

gchar *		e_cal_util_get_system_timezone_location (void);
ICalTimezone *	e_cal_util_get_system_timezone (void);
void		e_cal_util_get_component_occur_times
						(ECalComponent *comp,
						 time_t *out_start,
						 time_t *out_end,
						 ECalRecurResolveTimezoneCb tz_cb,
						 gpointer tz_cb_data,
						 const ICalTimezone *default_timezone,
						 ICalComponentKind kind);

gboolean	e_cal_util_component_has_x_property
						(ICalComponent *icalcomp,
						 const gchar *x_name);
ICalProperty *	e_cal_util_component_find_x_property
						(ICalComponent *icalcomp,
						 const gchar *x_name);
gchar *		e_cal_util_component_dup_x_property
						(ICalComponent *icalcomp,
						 const gchar *x_name);
void		e_cal_util_component_set_x_property
						(ICalComponent *icalcomp,
						 const gchar *x_name,
						 const gchar *value);
gboolean	e_cal_util_component_remove_x_property
						(ICalComponent *icalcomp,
						 const gchar *x_name);
guint		e_cal_util_component_remove_property_by_kind
						(ICalComponent *icalcomp,
						 ICalPropertyKind kind,
						 gboolean all);

gboolean	e_cal_util_init_recur_task_sync	(ICalComponent *vtodo,
						 struct _ECalClient *cal_client,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cal_util_mark_task_complete_sync
						(ICalComponent *vtodo,
						 time_t completed_time,
						 struct _ECalClient *cal_client,
						 GCancellable *cancellable,
						 GError **error);
EConflictResolution
		e_cal_util_operation_flags_to_conflict_resolution
						(guint32 flags); /* bit-or of ECalOperationFlags */
guint32		e_cal_util_conflict_resolution_to_operation_flags /* bit-or of ECalOperationFlags */
						(EConflictResolution conflict_resolution);
gboolean	e_cal_util_inline_local_attachments_sync
						(ICalComponent *component,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cal_util_set_alarm_acknowledged
						(ECalComponent *component,
						 const gchar *auid,
						 gint64 when); /* as time_t in UTC */
void		e_cal_util_clamp_vtimezone	(ICalComponent *vtimezone,
						 const ICalTime *from,
						 const ICalTime *to);
void		e_cal_util_clamp_vtimezone_by_component
						(ICalComponent *vtimezone,
						 ICalComponent *component);
/**
 * ECalUtilFilterPropertyFunc:
 * @prop: an #ICalProperty
 * @user_data: user data for the function
 *
 * Function used to determine whether the given property @prop
 * should be considered.
 *
 * Returns: %TRUE, when the @prop should be considered, %FALSE to skip it
 *
 * Since: 3.52
 **/
typedef gboolean (* ECalUtilFilterPropertyFunc)	(ICalProperty *prop,
						 gpointer user_data);
ICalProperty *	e_cal_util_component_find_property_for_locale_filtered
						(ICalComponent *icalcomp,
						 ICalPropertyKind prop_kind,
						 const gchar *locale,
						 ECalUtilFilterPropertyFunc func,
						 gpointer user_data);
ICalProperty *	e_cal_util_component_find_property_for_locale
						(ICalComponent *icalcomp,
						 ICalPropertyKind prop_kind,
						 const gchar *locale);
/**
 * ECalUtilForeachCategoryFunc:
 * @comp: an #ICalComponent
 * @inout_category: (inout): the category name
 * @user_data: user data for the callback
 *
 * Function called for each non-empty category from e_cal_util_foreach_category().
 * The @func can assume owenrship of the @inout_category content, in which case
 * it should also set its content to %NULL, to avoid free of it. The string itself,
 * if taken, should be freed with g_free(), when no longer needed.
 *
 * Returns: %TRUE to continue, %FALSE to stop
 *
 * Since: 3.48
 **/
typedef gboolean (* ECalUtilForeachCategoryFunc)(ICalComponent *comp,
						 gchar **inout_category,
						 gpointer user_data);

void		e_cal_util_foreach_category	(ICalComponent *comp,
						 ECalUtilForeachCategoryFunc func,
						 gpointer user_data);
void		e_cal_util_diff_categories	(ICalComponent *old_comp,
						 ICalComponent *new_comp,
						 GHashTable **out_added, /* const gchar *category ~> 1 */
						 GHashTable **out_removed); /* const gchar *category ~> 1 */

const gchar *	e_cal_util_strip_mailto		(const gchar *address);
gboolean	e_cal_util_email_addresses_equal(const gchar *email1,
						 const gchar *email2);
gboolean	e_cal_util_get_default_name_and_address
						(ESourceRegistry *registry,
						 gchar **out_name,
						 gchar **out_address);
const gchar *	e_cal_util_get_organizer_email	(const ECalComponentOrganizer *organizer);
const gchar *	e_cal_util_get_attendee_email	(const ECalComponentAttendee *attendee);
const gchar *	e_cal_util_get_property_email	(ICalProperty *prop);


G_END_DECLS

#endif /* E_CAL_UTIL_H */

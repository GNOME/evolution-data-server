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

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include "libedataserver/e-data-server-util.h"
#include "e-cal-util.h"
#include "e-cal-system-timezone.h"



/**
 * cal_obj_instance_list_free:
 * @list: List of #CalObjInstance structures.
 *
 * Frees a list of #CalObjInstance structures.
 **/
void
cal_obj_instance_list_free (GList *list)
{
	CalObjInstance *i;
	GList *l;

	for (l = list; l; l = l->next) {
		i = l->data;

		g_assert (i != NULL);
		g_assert (i->uid != NULL);

		g_free (i->uid);
		g_free (i);
	}

	g_list_free (list);
}

/**
 * cal_obj_uid_list_free:
 * @list: List of strings with unique identifiers.
 *
 * Frees a list of unique identifiers for calendar objects.
 **/
void
cal_obj_uid_list_free (GList *list)
{
	GList *l;

	for (l = list; l; l = l->next) {
		gchar *uid;

		uid = l->data;

		g_assert (uid != NULL);
		g_free (uid);
	}

	g_list_free (list);
}

/**
 * e_cal_util_new_top_level:
 *
 * Creates a new VCALENDAR component.
 *
 * Returns: the newly created top level component.
 */
icalcomponent *
e_cal_util_new_top_level (void)
{
	icalcomponent *icalcomp;
	icalproperty *prop;

	icalcomp = icalcomponent_new (ICAL_VCALENDAR_COMPONENT);

	/* RFC 2445, section 4.7.1 */
	prop = icalproperty_new_calscale ("GREGORIAN");
	icalcomponent_add_property (icalcomp, prop);

       /* RFC 2445, section 4.7.3 */
	prop = icalproperty_new_prodid ("-//Ximian//NONSGML Evolution Calendar//EN");
	icalcomponent_add_property (icalcomp, prop);

	/* RFC 2445, section 4.7.4.  This is the iCalendar spec version, *NOT*
	 * the product version!  Do not change this!
	 */
	prop = icalproperty_new_version ("2.0");
	icalcomponent_add_property (icalcomp, prop);

	return icalcomp;
}

/**
 * e_cal_util_new_component:
 * @kind: Kind of the component to create.
 *
 * Creates a new #icalcomponent of the specified kind.
 *
 * Returns: the newly created component.
 */
icalcomponent *
e_cal_util_new_component (icalcomponent_kind kind)
{
	icalcomponent *comp;
	struct icaltimetype dtstamp;
	gchar *uid;

	comp = icalcomponent_new (kind);
	uid = e_cal_component_gen_uid ();
	icalcomponent_set_uid (comp, uid);
	g_free (uid);
	dtstamp = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	icalcomponent_set_dtstamp (comp, dtstamp);

	return comp;
}

static gchar *
read_line (const gchar *string)
{
	gchar *line;
	GString *line_str = NULL;

	for (; *string; string++) {
		if (!line_str)
			line_str = g_string_new ("");

		line_str = g_string_append_c (line_str, *string);
		if (*string == '\n')
			break;
	}

	line = line_str->str;
	g_string_free (line_str, FALSE);

	return line;
}

/**
 * e_cal_util_parse_ics_string:
 * @string: iCalendar string to be parsed.
 *
 * Parses an iCalendar string and returns a new #icalcomponent representing
 * that string. Note that this function deals with multiple VCALENDAR's in the
 * string, something that Mozilla used to do and which libical does not
 * support.
 *
 * Returns: a newly created #icalcomponent or NULL if the string isn't a
 * valid iCalendar string.
 */
icalcomponent *
e_cal_util_parse_ics_string (const gchar *string)
{
	gchar *s;
	icalcomponent *icalcomp = NULL;

	g_return_val_if_fail (string != NULL, NULL);

	/* Split string into separated VCALENDAR's, if more than one */
	if ((s = g_strstr_len (string, strlen (string), "BEGIN:VCALENDAR"))) {
		GString *comp_str = NULL;

		while (*s) {
			gchar *line = read_line (s);
			if (line) {
				if (!comp_str)
					comp_str = g_string_new (line);
				else
					comp_str = g_string_append (comp_str, line);

				if (!strncmp (line, "END:VCALENDAR", 13)) {
					icalcomponent *tmp;

					tmp = icalparser_parse_string (comp_str->str);
					if (tmp && icalcomponent_isa (tmp) == ICAL_VCALENDAR_COMPONENT) {
						if (icalcomp)
							icalcomponent_merge_component (icalcomp, tmp);
						else
							icalcomp = tmp;
					} else
						g_warning ("Could not merge the components, the component is either invalid or not a toplevel component \n");

					g_string_free (comp_str, TRUE);
					comp_str = NULL;
				}

				s += strlen (line);

				g_free (line);
			}
		}
	} else
		icalcomp = icalparser_parse_string (string);

	return icalcomp;
}

static gchar *
get_line_fn (gchar *buf, gsize size, gpointer file)
{
	return fgets (buf, size, file);
}

/**
 * e_cal_util_parse_ics_file:
 * @filename: Name of the file to be parsed.
 *
 * Parses the given file, and, if it contains a valid iCalendar object,
 * parse it and return a new #icalcomponent.
 *
 * Returns: a newly created #icalcomponent or NULL if the file doesn't
 * contain a valid iCalendar object.
 */
icalcomponent *
e_cal_util_parse_ics_file (const gchar *filename)
{
	icalparser *parser;
	icalcomponent *icalcomp;
	FILE *file;

	file = g_fopen (filename, "rb");
	if (!file)
		return NULL;

	parser = icalparser_new ();
	icalparser_set_gen_data (parser, file);

	icalcomp = icalparser_parse (parser, get_line_fn);
	icalparser_free (parser);
	fclose (file);

	return icalcomp;
}

/* Computes the range of time in which recurrences should be generated for a
 * component in order to compute alarm trigger times.
 */
static void
compute_alarm_range (ECalComponent *comp, GList *alarm_uids, time_t start, time_t end,
		     time_t *alarm_start, time_t *alarm_end)
{
	GList *l;
	time_t repeat_time;

	*alarm_start = start;
	*alarm_end = end;

	repeat_time = 0;

	for (l = alarm_uids; l; l = l->next) {
		const gchar *auid;
		ECalComponentAlarm *alarm;
		ECalComponentAlarmTrigger trigger;
		struct icaldurationtype *dur;
		time_t dur_time;
		ECalComponentAlarmRepeat repeat;

		auid = l->data;
		alarm = e_cal_component_get_alarm (comp, auid);
		g_assert (alarm != NULL);

		e_cal_component_alarm_get_trigger (alarm, &trigger);
		e_cal_component_alarm_get_repeat (alarm, &repeat);
		e_cal_component_alarm_free (alarm);

		switch (trigger.type) {
		case E_CAL_COMPONENT_ALARM_TRIGGER_NONE:
		case E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE:
			break;

		case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START:
		case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END:
			dur = &trigger.u.rel_duration;
			dur_time = icaldurationtype_as_int (*dur);

			if (repeat.repetitions != 0) {
				gint rdur;

				rdur = repeat.repetitions * icaldurationtype_as_int (repeat.duration);
				repeat_time = MAX (repeat_time, rdur);
			}

			if (dur->is_neg)
				/* If the duration is negative then dur_time
				 * will be negative as well; that is why we
				 * subtract to expand the range.
				 */
				*alarm_end = MAX (*alarm_end, end - dur_time);
			else
				*alarm_start = MIN (*alarm_start, start - dur_time);

			break;

		default:
			g_assert_not_reached ();
		}
	}

	*alarm_start -= repeat_time;

	g_assert (*alarm_start <= *alarm_end);
}

/* Closure data to generate alarm occurrences */
struct alarm_occurrence_data {
	/* These are the info we have */
	GList *alarm_uids;
	time_t start;
	time_t end;
	ECalComponentAlarmAction *omit;

	/* This is what we compute */
	GSList *triggers;
	gint n_triggers;
};

static void
add_trigger (struct alarm_occurrence_data *aod, const gchar *auid, time_t trigger,
	     time_t occur_start, time_t occur_end)
{
	ECalComponentAlarmInstance *instance;

	instance = g_new (ECalComponentAlarmInstance, 1);
	instance->auid = g_strdup (auid);
	instance->trigger = trigger;
	instance->occur_start = occur_start;
	instance->occur_end = occur_end;

	aod->triggers = g_slist_prepend (aod->triggers, instance);
	aod->n_triggers++;
}

/* Callback used from cal_recur_generate_instances(); generates triggers for all
 * of a component's RELATIVE alarms.
 */
static gboolean
add_alarm_occurrences_cb (ECalComponent *comp, time_t start, time_t end, gpointer data)
{
	struct alarm_occurrence_data *aod;
	GList *l;

	aod = data;

	for (l = aod->alarm_uids; l; l = l->next) {
		const gchar *auid;
		ECalComponentAlarm *alarm;
		ECalComponentAlarmAction action;
		ECalComponentAlarmTrigger trigger;
		ECalComponentAlarmRepeat repeat;
		struct icaldurationtype *dur;
		time_t dur_time;
		time_t occur_time, trigger_time;
		gint i;

		auid = l->data;
		alarm = e_cal_component_get_alarm (comp, auid);
		g_assert (alarm != NULL);

		e_cal_component_alarm_get_action (alarm, &action);
		e_cal_component_alarm_get_trigger (alarm, &trigger);
		e_cal_component_alarm_get_repeat (alarm, &repeat);
		e_cal_component_alarm_free (alarm);

		for (i = 0; aod->omit[i] != -1; i++) {
			if (aod->omit[i] == action)
				break;
		}
		if (aod->omit[i] != -1)
			continue;

		if (trigger.type != E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START
		    && trigger.type != E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END)
			continue;

		dur = &trigger.u.rel_duration;
		dur_time = icaldurationtype_as_int (*dur);

		if (trigger.type == E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START)
			occur_time = start;
		else
			occur_time = end;

		/* If dur->is_neg is true then dur_time will already be
		 * negative.  So we do not need to test for dur->is_neg here; we
		 * can simply add the dur_time value to the occur_time and get
		 * the correct result.
		 */

		trigger_time = occur_time + dur_time;

		/* Add repeating alarms */

		if (repeat.repetitions != 0) {
			gint i;
			time_t repeat_time;

			repeat_time = icaldurationtype_as_int (repeat.duration);

			for (i = 0; i < repeat.repetitions; i++) {
				time_t t;

				t = trigger_time + (i + 1) * repeat_time;

				if (t >= aod->start && t < aod->end)
					add_trigger (aod, auid, t, start, end);
			}
		}

		/* Add the trigger itself */

		if (trigger_time >= aod->start && trigger_time < aod->end)
			add_trigger (aod, auid, trigger_time, start, end);
	}

	return TRUE;
}

/* Generates the absolute triggers for a component */
static void
generate_absolute_triggers (ECalComponent *comp, struct alarm_occurrence_data *aod,
			    ECalRecurResolveTimezoneFn resolve_tzid,
			    gpointer user_data,
			    icaltimezone *default_timezone)
{
	GList *l;
	ECalComponentDateTime dt_start, dt_end;

	e_cal_component_get_dtstart (comp, &dt_start);
	e_cal_component_get_dtend (comp, &dt_end);

	for (l = aod->alarm_uids; l; l = l->next) {
		const gchar *auid;
		ECalComponentAlarm *alarm;
		ECalComponentAlarmAction action;
		ECalComponentAlarmRepeat repeat;
		ECalComponentAlarmTrigger trigger;
		time_t abs_time;
		time_t occur_start, occur_end;
		icaltimezone *zone;
		gint i;

		auid = l->data;
		alarm = e_cal_component_get_alarm (comp, auid);
		g_assert (alarm != NULL);

		e_cal_component_alarm_get_action (alarm, &action);
		e_cal_component_alarm_get_trigger (alarm, &trigger);
		e_cal_component_alarm_get_repeat (alarm, &repeat);
		e_cal_component_alarm_free (alarm);

		for (i = 0; aod->omit[i] != -1; i++) {
			if (aod->omit[i] == action)
				break;
		}
		if (aod->omit[i] != -1)
			continue;

		if (trigger.type != E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE)
			continue;

		/* Absolute triggers are always in UTC; see RFC 2445 section 4.8.6.3 */
		zone = icaltimezone_get_utc_timezone ();

		abs_time = icaltime_as_timet_with_zone (trigger.u.abs_time, zone);

		/* No particular occurrence, so just use the times from the component */

		if (dt_start.value) {
			if (dt_start.tzid && !dt_start.value->is_date)
				zone = (* resolve_tzid) (dt_start.tzid, user_data);
			else
				zone = default_timezone;

			occur_start = icaltime_as_timet_with_zone (*dt_start.value, zone);
		} else
			occur_start = -1;

		if (dt_end.value) {
			if (dt_end.tzid && !dt_end.value->is_date)
				zone = (* resolve_tzid) (dt_end.tzid, user_data);
			else
				zone = default_timezone;

			occur_end = icaltime_as_timet_with_zone (*dt_end.value, zone);
		} else
			occur_end = -1;

		/* Add repeating alarms */

		if (repeat.repetitions != 0) {
			gint i;
			time_t repeat_time;

			repeat_time = icaldurationtype_as_int (repeat.duration);

			for (i = 0; i < repeat.repetitions; i++) {
				time_t t;

				t = abs_time + (i + 1) * repeat_time;

				if (t >= aod->start && t < aod->end)
					add_trigger (aod, auid, t, occur_start, occur_end);
			}
		}

		/* Add the trigger itself */

		if (abs_time >= aod->start && abs_time < aod->end)
			add_trigger (aod, auid, abs_time, occur_start, occur_end);
	}

	e_cal_component_free_datetime (&dt_start);
	e_cal_component_free_datetime (&dt_end);
}

/* Compares two alarm instances; called from g_slist_sort() */
static gint
compare_alarm_instance (gconstpointer a, gconstpointer b)
{
	const ECalComponentAlarmInstance *aia, *aib;

	aia = a;
	aib = b;

	if (aia->trigger < aib->trigger)
		return -1;
	else if (aia->trigger > aib->trigger)
		return 1;
	else
		return 0;
}

/**
 * e_cal_util_generate_alarms_for_comp
 * @comp: The #ECalComponent to generate alarms from.
 * @start: Start time.
 * @end: End time.
 * @omit: Alarm types to omit
 * @resolve_tzid: Callback for resolving timezones
 * @user_data: Data to be passed to the resolve_tzid callback
 * @default_timezone: The timezone used to resolve DATE and floating DATE-TIME
 * values.
 *
 * Generates alarm instances for a calendar component.  Returns the instances
 * structure, or NULL if no alarm instances occurred in the specified time
 * range.
 *
 * Returns: a list of all the alarms found for the given component on
 * the given time tange. The list of alarms should be freed by using the
 * #e_cal_component_free_alarm_list function.
 */
ECalComponentAlarms *
e_cal_util_generate_alarms_for_comp (ECalComponent *comp,
				     time_t start,
				     time_t end,
				     ECalComponentAlarmAction *omit,
				     ECalRecurResolveTimezoneFn resolve_tzid,
				     gpointer user_data,
				     icaltimezone *default_timezone)
{
	GList *alarm_uids;
	time_t alarm_start, alarm_end;
	struct alarm_occurrence_data aod;
	ECalComponentAlarms *alarms;

	if (!e_cal_component_has_alarms (comp))
		return NULL;

	alarm_uids = e_cal_component_get_alarm_uids (comp);
	compute_alarm_range (comp, alarm_uids, start, end, &alarm_start, &alarm_end);

	aod.alarm_uids = alarm_uids;
	aod.start = start;
	aod.end = end;
	aod.omit = omit;
	aod.triggers = NULL;
	aod.n_triggers = 0;

	e_cal_recur_generate_instances (comp, alarm_start, alarm_end,
					add_alarm_occurrences_cb, &aod,
					resolve_tzid, user_data,
					default_timezone);

	/* We add the ABSOLUTE triggers separately */
	generate_absolute_triggers (comp, &aod, resolve_tzid, user_data, default_timezone);

	cal_obj_uid_list_free (alarm_uids);

	if (aod.n_triggers == 0)
		return NULL;

	/* Create the component alarm instances structure */

	alarms = g_new (ECalComponentAlarms, 1);
	alarms->comp = comp;
	g_object_ref (G_OBJECT (alarms->comp));
	alarms->alarms = g_slist_sort (aod.triggers, compare_alarm_instance);

	return alarms;
}

/**
 * e_cal_util_generate_alarms_for_list
 * @comps: List of #ECalComponent's.
 * @start: Start time.
 * @end: End time.
 * @omit: Alarm types to omit
 * @comp_alarms: List to be returned
 * @resolve_tzid: Callback for resolving timezones
 * @user_data: Data to be passed to the resolve_tzid callback
 * @default_timezone: The timezone used to resolve DATE and floating DATE-TIME
 * values.
 *
 * Iterates through all the components in the @comps list and generates alarm
 * instances for them; putting them in the @comp_alarms list.
 *
 * Returns: the number of elements it added to the list.
 */
gint
e_cal_util_generate_alarms_for_list (GList *comps,
				     time_t start,
				     time_t end,
				     ECalComponentAlarmAction *omit,
				     GSList **comp_alarms,
				     ECalRecurResolveTimezoneFn resolve_tzid,
				     gpointer user_data,
				     icaltimezone *default_timezone)
{
	GList *l;
	gint n;

	n = 0;

	for (l = comps; l; l = l->next) {
		ECalComponent *comp;
		ECalComponentAlarms *alarms;

		comp = E_CAL_COMPONENT (l->data);
		alarms = e_cal_util_generate_alarms_for_comp (comp, start, end, omit, resolve_tzid, user_data, default_timezone);

		if (alarms) {
			*comp_alarms = g_slist_prepend (*comp_alarms, alarms);
			n++;
		}
	}

	return n;
}

/**
 * e_cal_util_priority_to_string:
 * @priority: Priority value.
 *
 * Converts an iCalendar PRIORITY value to a translated string. Any unknown
 * priority value (i.e. not 0-9) will be returned as "" (undefined).
 *
 * Returns: a string representing the PRIORITY value. This value is a
 * constant, so it should never be freed.
 */
const gchar *
e_cal_util_priority_to_string (gint priority)
{
	const gchar *retval;

	if (priority <= 0)
		retval = "";
	else if (priority <= 4)
		retval = _("High");
	else if (priority == 5)
		retval = _("Normal");
	else if (priority <= 9)
		retval = _("Low");
	else
		retval = "";

	return retval;
}

/**
 * e_cal_util_priority_from_string:
 * @string: A string representing the PRIORITY value.
 *
 * Converts a translated priority string to an iCalendar priority value.
 *
 * Returns: the priority (0-9) or -1 if the priority string is not valid.
*/
gint
e_cal_util_priority_from_string (const gchar *string)
{
	gint priority;

	/* An empty string is the same as 'None'. */
	if (!string || !string[0] || !e_util_utf8_strcasecmp (string, _("Undefined")))
		priority = 0;
	else if (!e_util_utf8_strcasecmp (string, _("High")))
		priority = 3;
	else if (!e_util_utf8_strcasecmp (string, _("Normal")))
		priority = 5;
	else if (!e_util_utf8_strcasecmp (string, _("Low")))
		priority = 7;
	else
		priority = -1;

	return priority;
}

/* callback for icalcomponent_foreach_tzid */
typedef struct {
	icalcomponent *vcal_comp;
	icalcomponent *icalcomp;
} ForeachTzidData;

static void
add_timezone_cb (icalparameter *param, gpointer data)
{
	icaltimezone *tz;
	const gchar *tzid;
	icalcomponent *vtz_comp;
	ForeachTzidData *f_data = (ForeachTzidData *) data;

	tzid = icalparameter_get_tzid (param);
	if (!tzid)
		return;

	tz = icalcomponent_get_timezone (f_data->vcal_comp, tzid);
	if (tz)
		return;

	tz = icalcomponent_get_timezone (f_data->icalcomp, tzid);
	if (!tz) {
		tz = icaltimezone_get_builtin_timezone_from_tzid (tzid);
		if (!tz)
			return;
	}

	vtz_comp = icaltimezone_get_component (tz);
	if (!vtz_comp)
		return;

	icalcomponent_add_component (f_data->vcal_comp,
				     icalcomponent_new_clone (vtz_comp));
}

/**
 * e_cal_util_add_timezones_from_component:
 * @vcal_comp: A VCALENDAR component.
 * @icalcomp: An iCalendar component, of any type.
 *
 * Adds VTIMEZONE components to a VCALENDAR for all tzid's
 * in the given @icalcomp.
 */
void
e_cal_util_add_timezones_from_component (icalcomponent *vcal_comp,
					 icalcomponent *icalcomp)
{
	ForeachTzidData f_data;

	g_return_if_fail (vcal_comp != NULL);
	g_return_if_fail (icalcomp != NULL);;

	f_data.vcal_comp = vcal_comp;
	f_data.icalcomp = icalcomp;
	icalcomponent_foreach_tzid (icalcomp, add_timezone_cb, &f_data);
}

/**
 * e_cal_util_component_is_instance:
 * @icalcomp: An #icalcomponent.
 *
 * Checks whether an #icalcomponent is an instance of a recurring appointment or not.
 *
 * Returns: TRUE if it is an instance, FALSE if not.
 */
gboolean
e_cal_util_component_is_instance (icalcomponent *icalcomp)
{
	icalproperty *prop;

	g_return_val_if_fail (icalcomp != NULL, FALSE);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_RECURRENCEID_PROPERTY);
	return prop ? TRUE : FALSE;
}

/**
 * e_cal_util_component_has_alarms:
 * @icalcomp: An #icalcomponent.
 *
 * Checks whether an #icalcomponent has any alarm.
 *
 * Returns: TRUE if it has alarms, FALSE otherwise.
 */
gboolean
e_cal_util_component_has_alarms (icalcomponent *icalcomp)
{
	icalcomponent *alarm;

	g_return_val_if_fail (icalcomp != NULL, FALSE);

	alarm = icalcomponent_get_first_component (icalcomp, ICAL_VALARM_COMPONENT);
	return alarm ? TRUE : FALSE;
}

/**
 * e_cal_util_component_has_organizer:
 * @icalcomp: An #icalcomponent.
 *
 * Checks whether an #icalcomponent has an organizer or not.
 *
 * Returns: TRUE if there is an organizer, FALSE if not.
 */
gboolean
e_cal_util_component_has_organizer (icalcomponent *icalcomp)
{
	icalproperty *prop;

	g_return_val_if_fail (icalcomp != NULL, FALSE);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_ORGANIZER_PROPERTY);
	return prop ? TRUE : FALSE;
}

/**
 * e_cal_util_component_has_attendee:
 * @icalcomp: An #icalcomponent.
 *
 * Checks if an #icalcomponent has any attendees.
 *
 * Returns: TRUE if there are attendees, FALSE if not.
 */
gboolean
e_cal_util_component_has_attendee (icalcomponent *icalcomp)
{
	icalproperty *prop;

	g_return_val_if_fail (icalcomp != NULL, FALSE);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY);

	return prop ? TRUE : FALSE;
}

/**
 * e_cal_util_component_has_recurrences:
 * @icalcomp: An #icalcomponent.
 *
 * Checks if an #icalcomponent has recurrence dates or rules.
 *
 * Returns: TRUE if there are recurrence dates/rules, FALSE if not.
 */
gboolean
e_cal_util_component_has_recurrences (icalcomponent *icalcomp)
{
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	return e_cal_util_component_has_rdates (icalcomp) || e_cal_util_component_has_rrules (icalcomp);
}

/**
 * e_cal_util_component_has_rdates:
 * @icalcomp: An #icalcomponent.
 *
 * Checks if an #icalcomponent has recurrence dates.
 *
 * Returns: TRUE if there are recurrence dates, FALSE if not.
 */
gboolean
e_cal_util_component_has_rdates (icalcomponent *icalcomp)
{
	icalproperty *prop;

	g_return_val_if_fail (icalcomp != NULL, FALSE);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_RDATE_PROPERTY);
	return prop ? TRUE : FALSE;
}

/**
 * e_cal_util_component_has_rrules:
 * @icalcomp: An #icalcomponent.
 *
 * Checks if an #icalcomponent has recurrence rules.
 *
 * Returns: TRUE if there are recurrence rules, FALSE if not.
 */
gboolean
e_cal_util_component_has_rrules (icalcomponent *icalcomp)
{
	icalproperty *prop;

	g_return_val_if_fail (icalcomp != NULL, FALSE);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY);
	return prop ? TRUE : FALSE;
}

/**
 * e_cal_util_event_dates_match:
 * @icalcomp1: An #icalcomponent.
 * @icalcomp2: An #icalcomponent.
 *
 * Compare the dates of two #icalcomponent's to check if they match.
 *
 * Returns: TRUE if the dates of both components match, FALSE otherwise.
 */
gboolean
e_cal_util_event_dates_match (icalcomponent *icalcomp1, icalcomponent *icalcomp2)
{
	struct icaltimetype c1_dtstart, c1_dtend, c2_dtstart, c2_dtend;

	g_return_val_if_fail (icalcomp1 != NULL, FALSE);
	g_return_val_if_fail (icalcomp2 != NULL, FALSE);

	c1_dtstart = icalcomponent_get_dtstart (icalcomp1);
	c1_dtend = icalcomponent_get_dtend (icalcomp1);
	c2_dtstart = icalcomponent_get_dtstart (icalcomp2);
	c2_dtend = icalcomponent_get_dtend (icalcomp2);

	/* if either value is NULL, they must both be NULL to match */
	if (icaltime_is_valid_time (c1_dtstart) || icaltime_is_valid_time (c2_dtstart)) {
		if (!(icaltime_is_valid_time (c1_dtstart) && icaltime_is_valid_time (c2_dtstart)))
			return FALSE;
	} else {
		if (icaltime_compare (c1_dtstart, c2_dtstart))
			return FALSE;
	}

	if (icaltime_is_valid_time (c1_dtend) || icaltime_is_valid_time (c2_dtend)) {
		if (!(icaltime_is_valid_time (c1_dtend) && icaltime_is_valid_time (c2_dtend)))
			return FALSE;
	} else {
		if (icaltime_compare (c1_dtend, c2_dtend))
			return FALSE;
	}

	/* now match the timezones */
	if (!(!c1_dtstart.zone && !c2_dtstart.zone) ||
	    (c1_dtstart.zone && c2_dtstart.zone &&
	     !strcmp (icaltimezone_get_tzid ((icaltimezone *) c1_dtstart.zone),
		      icaltimezone_get_tzid ((icaltimezone *) c2_dtstart.zone))))
		return FALSE;

	if (!(!c1_dtend.zone && !c2_dtend.zone) ||
	    (c1_dtend.zone && c2_dtend.zone &&
	     !strcmp (icaltimezone_get_tzid ((icaltimezone *) c1_dtend.zone),
		      icaltimezone_get_tzid ((icaltimezone *) c2_dtend.zone))))
		return FALSE;

	return TRUE;
}

/* Individual instances management */

struct instance_data {
	time_t start;
	gboolean found;
};

static void
check_instance (icalcomponent *comp, struct icaltime_span *span, gpointer data)
{
	struct instance_data *instance = data;

	if (span->start == instance->start)
		instance->found = TRUE;
}

/**
 * e_cal_util_construct_instance:
 * @icalcomp: A recurring #icalcomponent
 * @rid: The RECURRENCE-ID to construct a component for
 *
 * This checks that @rid indicates a valid recurrence of @icalcomp, and
 * if so, generates a copy of @comp containing a RECURRENCE-ID of @rid.
 *
 * Returns: the instance, or %NULL.
 **/
icalcomponent *
e_cal_util_construct_instance (icalcomponent *icalcomp,
			       struct icaltimetype rid)
{
	struct instance_data instance;
	struct icaltimetype start, end;

	g_return_val_if_fail (icalcomp != NULL, NULL);

	/* Make sure this is really recurring */
	if (!icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY) &&
	    !icalcomponent_get_first_property (icalcomp, ICAL_RDATE_PROPERTY))
		return NULL;

	/* Make sure the specified instance really exists */
	start = icaltime_convert_to_zone (rid, icaltimezone_get_utc_timezone ());
	end = start;
	icaltime_adjust (&end, 0, 0, 0, 1);

	instance.start = icaltime_as_timet (start);
	instance.found = FALSE;
	icalcomponent_foreach_recurrence (icalcomp, start, end,
					  check_instance, &instance);
	if (!instance.found)
		return NULL;

	/* Make the instance */
	icalcomp = icalcomponent_new_clone (icalcomp);
	icalcomponent_set_recurrenceid (icalcomp, rid);

	return icalcomp;
}

static inline gboolean
time_matches_rid (struct icaltimetype itt, struct icaltimetype rid, CalObjModType mod)
{
	gint compare;

	compare = icaltime_compare (itt, rid);
	if (compare == 0)
		return TRUE;
	else if (compare < 0 && (mod & CALOBJ_MOD_THISANDPRIOR))
		return TRUE;
	else if (compare > 0 && (mod & CALOBJ_MOD_THISANDFUTURE))
		return TRUE;

	return FALSE;
}

/**
 * e_cal_util_remove_instances:
 * @icalcomp: A (recurring) #icalcomponent
 * @rid: The base RECURRENCE-ID to remove
 * @mod: How to interpret @rid
 *
 * Removes one or more instances from @comp according to @rid and @mod.
 *
 * FIXME: should probably have a return value indicating whether or not
 * @icalcomp still has any instances
 **/
void
e_cal_util_remove_instances (icalcomponent *icalcomp,
			     struct icaltimetype rid,
			     CalObjModType mod)
{
	icalproperty *prop;
	struct icaltimetype itt, recur;
	struct icalrecurrencetype rule;
	icalrecur_iterator *iter;

	g_return_if_fail (icalcomp != NULL);
	g_return_if_fail (mod != CALOBJ_MOD_ALL);

	/* First remove RDATEs and EXDATEs in the indicated range. */
	for (prop = icalcomponent_get_first_property (icalcomp, ICAL_RDATE_PROPERTY);
	     prop;
	     prop = icalcomponent_get_next_property (icalcomp, ICAL_RDATE_PROPERTY)) {
		struct icaldatetimeperiodtype period;

		period = icalproperty_get_rdate (prop);
		if (time_matches_rid (period.time, rid, mod))
			icalcomponent_remove_property (icalcomp, prop);
	}
	for (prop = icalcomponent_get_first_property (icalcomp, ICAL_EXDATE_PROPERTY);
	     prop;
	     prop = icalcomponent_get_next_property (icalcomp, ICAL_EXDATE_PROPERTY)) {
		itt = icalproperty_get_exdate (prop);
		if (time_matches_rid (itt, rid, mod))
			icalcomponent_remove_property (icalcomp, prop);
	}

	/* If we're only removing one instance, just add an EXDATE. */
	if (mod == CALOBJ_MOD_THIS) {
		prop = icalproperty_new_exdate (rid);
		icalcomponent_add_property (icalcomp, prop);
		return;
	}

	/* Otherwise, iterate through RRULEs */
	/* FIXME: this may generate duplicate EXRULEs */
	for (prop = icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY);
	     prop;
	     prop = icalcomponent_get_next_property (icalcomp, ICAL_RRULE_PROPERTY)) {
		rule = icalproperty_get_rrule (prop);

		iter = icalrecur_iterator_new (rule, rid);
		recur = icalrecur_iterator_next (iter);

		if (mod & CALOBJ_MOD_THISANDFUTURE) {
			/* If there is a recurrence on or after rid,
			 * use the UNTIL parameter to truncate the rule
			 * at rid.
			 */
			if (!icaltime_is_null_time (recur)) {
				rule.count = 0;
				rule.until = rid;
				icaltime_adjust (&rule.until, 0, 0, 0, -1);
				icalproperty_set_rrule (prop, rule);
			}
		} else {
			/* (If recur == rid, skip to the next occurrence) */
			if (icaltime_compare (recur, rid) == 0)
				recur = icalrecur_iterator_next (iter);

			/* If there is a recurrence after rid, add
			 * an EXRULE to block instances up to rid.
			 * Otherwise, just remove the RRULE.
			 */
			if (!icaltime_is_null_time (recur)) {
				rule.count = 0;
				/* iCalendar says we should just use rid
				 * here, but Outlook/Exchange handle
				 * UNTIL incorrectly.
				 */
				rule.until = icaltime_add (rid, icalcomponent_get_duration (icalcomp));
				prop = icalproperty_new_exrule (rule);
				icalcomponent_add_property (icalcomp, prop);
			} else
				icalcomponent_remove_property (icalcomp, prop);
		}

		icalrecur_iterator_free (iter);
	}
}

/**
 * e_cal_util_get_system_timezone_location:
 *
 * Returns system timezone location string, NULL on an error.
 * Returned pointer should be freed with g_free().
 *
 * Since: 2.28
 **/
gchar *
e_cal_util_get_system_timezone_location (void)
{
	return e_cal_system_timezone_get_location ();
}

/**
 * e_cal_util_get_system_timezone:
 *
 * Returns icaltimezone object of the system timezone. NULL on an error.
 * Returned pointer is part of the built-in timezones, thus do not free it.
 *
 * Since: 2.28
 **/
icaltimezone *
e_cal_util_get_system_timezone (void)
{
	gchar *location;
	icaltimezone *zone;

	location = e_cal_system_timezone_get_location ();
	g_return_val_if_fail (location != NULL, NULL);

	zone = icaltimezone_get_builtin_timezone (location);

	g_free (location);

	return zone;
}

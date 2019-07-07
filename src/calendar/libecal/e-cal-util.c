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

#include "evolution-data-server-config.h"

#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include <libedataserver/libedataserver.h>

#include "e-cal-check-timezones.h"
#include "e-cal-client.h"
#include "e-cal-system-timezone.h"
#include "e-cal-recur.h"

#include "e-cal-util.h"

#define _TIME_MIN	((time_t) 0)		/* Min valid time_t	*/
#define _TIME_MAX	((time_t) INT_MAX)

/**
 * e_cal_util_new_top_level:
 *
 * Creates a new VCALENDAR component. Free it with g_object_unref(),
 * when no longer needed.
 *
 * Returns: (transfer full): the newly created top level component.
 */
ICalComponent *
e_cal_util_new_top_level (void)
{
	ICalComponent *icalcomp;
	ICalProperty *prop;

	icalcomp = i_cal_component_new (I_CAL_VCALENDAR_COMPONENT);

	/* RFC 2445, section 4.7.1 */
	prop = i_cal_property_new_calscale ("GREGORIAN");
	i_cal_component_take_property (icalcomp, prop);

       /* RFC 2445, section 4.7.3 */
	prop = i_cal_property_new_prodid ("-//Ximian//NONSGML Evolution Calendar//EN");
	i_cal_component_take_property (icalcomp, prop);

	/* RFC 2445, section 4.7.4.  This is the iCalendar spec version, *NOT*
	 * the product version!  Do not change this!
	 */
	prop = i_cal_property_new_version ("2.0");
	i_cal_component_take_property (icalcomp, prop);

	return icalcomp;
}

/**
 * e_cal_util_new_component:
 * @kind: Kind of the component to create, as #ICalComponentKind.
 *
 * Creates a new #ICalComponent of the specified kind. Free it
 * with g_object_unref(), when no longer needed.
 *
 * Returns: (transfer full): the newly created component.
 */
ICalComponent *
e_cal_util_new_component (ICalComponentKind kind)
{
	ICalComponent *icalcomp;
	ICalTime *dtstamp;
	gchar *uid;

	icalcomp = i_cal_component_new (kind);
	uid = e_util_generate_uid ();
	i_cal_component_set_uid (icalcomp, uid);
	g_free (uid);
	dtstamp = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());
	i_cal_component_set_dtstamp (icalcomp, dtstamp);
	g_object_unref (dtstamp);

	return icalcomp;
}

/**
 * e_cal_util_copy_timezone:
 * @zone: an ICalTimezone
 *
 * Copies the @zone together with its inner component and
 * returns it as a new #ICalTimezone object. Free it with
 * g_object_unref(), when no longer needed.
 *
 * Returns: (transfer full): a copy of the @zone
 *
 * Since: 3.34
 **/
ICalTimezone *
e_cal_util_copy_timezone (const ICalTimezone *zone)
{
	ICalComponent *comp;
	ICalTimezone *zone_copy;

	g_return_val_if_fail (zone != NULL, NULL);

	zone_copy = i_cal_timezone_copy (zone);
	if (!zone_copy)
		return NULL;

	/* If the original component is one of the built-in, then libcal
	   loads it during the i_cal_timezone_get_component() call and
	   assigns a component to it. */
	comp = i_cal_timezone_get_component (zone_copy);
	if (comp) {
		g_object_unref (comp);
		return zone_copy;
	}

	comp = i_cal_timezone_get_component (zone);
	if (comp) {
		ICalComponent *comp_copy;

		comp_copy = i_cal_component_clone (comp);
		if (!i_cal_timezone_set_component (zone_copy, comp_copy))
			g_clear_object (&zone_copy);
		g_object_unref (comp_copy);
		g_object_unref (comp);
	}

	return zone_copy;
}

static gchar *
read_line (const gchar *string)
{
	GString *line_str = NULL;

	for (; *string; string++) {
		if (!line_str)
			line_str = g_string_new ("");

		g_string_append_c (line_str, *string);
		if (*string == '\n')
			break;
	}

	return g_string_free (line_str, FALSE);
}

/**
 * e_cal_util_parse_ics_string:
 * @string: iCalendar string to be parsed.
 *
 * Parses an iCalendar string and returns a new #ICalComponent representing
 * that string. Note that this function deals with multiple VCALENDAR's in the
 * string, something that Mozilla used to do and which libical does not
 * support.
 *
 * Free the returned non-NULL component with g_object_unref(), when no longer needed.
 *
 * Returns: (transfer full) (nullable): a newly created #ICalComponent, or %NULL,
 *    if the string isn't a valid iCalendar string.
 */
ICalComponent *
e_cal_util_parse_ics_string (const gchar *string)
{
	GString *comp_str = NULL;
	gchar *s;
	ICalComponent *icalcomp = NULL;

	g_return_val_if_fail (string != NULL, NULL);

	/* Split string into separated VCALENDAR's, if more than one */
	s = g_strstr_len (string, strlen (string), "BEGIN:VCALENDAR");

	if (s == NULL)
		return i_cal_parser_parse_string (string);

	while (*s != '\0') {
		gchar *line = read_line (s);

		if (!comp_str)
			comp_str = g_string_new (line);
		else
			g_string_append (comp_str, line);

		if (strncmp (line, "END:VCALENDAR", 13) == 0) {
			ICalComponent *tmp;

			tmp = i_cal_parser_parse_string (comp_str->str);
			if (tmp && i_cal_component_isa (tmp) == I_CAL_VCALENDAR_COMPONENT) {
				if (icalcomp) {
					i_cal_component_merge_component (icalcomp, tmp);
					g_object_unref (tmp);
				} else
					icalcomp = tmp;
			} else {
				g_warning (
					"Could not merge the components, "
					"the component is either invalid "
					"or not a toplevel component \n");
			}

			g_string_free (comp_str, TRUE);
			comp_str = NULL;
		}

		s += strlen (line);

		g_free (line);
	}

	return icalcomp;
}

struct ics_file {
	FILE *file;
	gboolean bof;
};

static gchar *
get_line_fn (gchar *buf,
	     gsize size,
	     gpointer user_data)
{
	struct ics_file *fl = user_data;

	/* Skip the UTF-8 marker at the beginning of the file */
	if (fl->bof) {
		gchar *orig_buf = buf;
		gchar tmp[4];

		fl->bof = FALSE;

		if (fread (tmp, sizeof (gchar), 3, fl->file) != 3 || feof (fl->file))
			return NULL;

		if (((guchar) tmp[0]) != 0xEF ||
		    ((guchar) tmp[1]) != 0xBB ||
		    ((guchar) tmp[2]) != 0xBF) {
			if (size <= 3)
				return NULL;

			buf[0] = tmp[0];
			buf[1] = tmp[1];
			buf[2] = tmp[2];
			buf += 3;
			size -= 3;
		}

		if (!fgets (buf, size, fl->file))
			return NULL;

		return orig_buf;
	}

	return fgets (buf, size, fl->file);
}

/**
 * e_cal_util_parse_ics_file:
 * @filename: Name of the file to be parsed.
 *
 * Parses the given file, and, if it contains a valid iCalendar object,
 * parse it and return a new #ICalComponent.
 *
 * Free the returned non-NULL component with g_object_unref(), when no longer needed.
 *
 * Returns: (transfer full) (nullable): a newly created #ICalComponent, or %NULL,
 *    if the file doesn't contain a valid iCalendar object.
 */
ICalComponent *
e_cal_util_parse_ics_file (const gchar *filename)
{
	ICalParser *parser;
	ICalComponent *icalcomp;
	struct ics_file fl;

	fl.file = g_fopen (filename, "rb");
	if (!fl.file)
		return NULL;

	fl.bof = TRUE;

	parser = i_cal_parser_new ();

	icalcomp = i_cal_parser_parse (parser, get_line_fn, &fl);
	g_object_unref (parser);
	fclose (fl.file);

	return icalcomp;
}

/* Computes the range of time in which recurrences should be generated for a
 * component in order to compute alarm trigger times.
 */
static void
compute_alarm_range (ECalComponent *comp,
                     GSList *alarm_uids,
                     time_t start,
                     time_t end,
                     time_t *alarm_start,
                     time_t *alarm_end)
{
	GSList *link;
	time_t repeat_time;

	*alarm_start = start;
	*alarm_end = end;

	repeat_time = 0;

	for (link = alarm_uids; link; link = g_slist_next (link)) {
		const gchar *auid;
		ECalComponentAlarm *alarm;
		ECalComponentAlarmTrigger *trigger;
		ICalDuration *dur;
		time_t dur_time;
		ECalComponentAlarmRepeat *repeat;

		auid = link->data;
		alarm = e_cal_component_get_alarm (comp, auid);
		g_return_if_fail (alarm != NULL);

		trigger = e_cal_component_alarm_get_trigger (alarm);
		repeat = e_cal_component_alarm_get_repeat (alarm);

		if (!trigger) {
			e_cal_component_alarm_free (alarm);
			continue;
		}

		switch (e_cal_component_alarm_trigger_get_kind (trigger)) {
		case E_CAL_COMPONENT_ALARM_TRIGGER_NONE:
		case E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE:
			break;

		case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START:
		case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END:
			dur = e_cal_component_alarm_trigger_get_duration (trigger);
			dur_time = i_cal_duration_as_int (dur);

			if (repeat && e_cal_component_alarm_repeat_get_repetitions (repeat) != 0) {
				gint rdur;

				rdur = e_cal_component_alarm_repeat_get_repetitions (repeat) *
					e_cal_component_alarm_repeat_get_interval_seconds (repeat);
				repeat_time = MAX (repeat_time, rdur);
			}

			if (i_cal_duration_is_neg (dur))
				/* If the duration is negative then dur_time
				 * will be negative as well; that is why we
				 * subtract to expand the range.
				 */
				*alarm_end = MAX (*alarm_end, end - dur_time);
			else
				*alarm_start = MIN (*alarm_start, start - dur_time);

			break;
		}

		e_cal_component_alarm_free (alarm);
	}

	*alarm_start -= repeat_time;
	g_warn_if_fail (*alarm_start <= *alarm_end);
}

/* Closure data to generate alarm occurrences */
struct alarm_occurrence_data {
	ECalComponent *comp;

	/* These are the info we have */
	GSList *alarm_uids; /* gchar * */
	time_t start;
	time_t end;
	ECalComponentAlarmAction *omit;

	/* This is what we compute */
	GSList *triggers; /* ECalComponentAlarmInstance * */
	gint n_triggers;
};

static void
add_trigger (struct alarm_occurrence_data *aod,
             const gchar *auid,
             time_t instance_time,
             time_t occur_start,
             time_t occur_end)
{
	ECalComponentAlarmInstance *instance;

	instance = e_cal_component_alarm_instance_new (auid, instance_time, occur_start, occur_end);

	aod->triggers = g_slist_prepend (aod->triggers, instance);
	aod->n_triggers++;
}

/* Callback used from cal_recur_generate_instances(); generates triggers for all
 * of a component's RELATIVE alarms.
 */
static gboolean
add_alarm_occurrences_cb (ICalComponent *icalcomp,
			  ICalTime *instance_start,
			  ICalTime *instance_end,
			  gpointer user_data,
			  GCancellable *cancellable,
			  GError **error)
{
	struct alarm_occurrence_data *aod;
	time_t start, end;
	GSList *link;

	aod = user_data;
	start = i_cal_time_as_timet_with_zone (instance_start, i_cal_time_get_timezone (instance_start));
	end = i_cal_time_as_timet_with_zone (instance_end, i_cal_time_get_timezone (instance_end));

	for (link = aod->alarm_uids; link; link = g_slist_next (link)) {
		const gchar *auid;
		ECalComponentAlarm *alarm;
		ECalComponentAlarmAction action;
		ECalComponentAlarmTrigger *trigger;
		ECalComponentAlarmRepeat *repeat;
		ICalDuration *dur;
		time_t dur_time;
		time_t occur_time, trigger_time;
		gint i;

		auid = link->data;
		alarm = e_cal_component_get_alarm (aod->comp, auid);
		g_return_val_if_fail (alarm != NULL, FALSE);

		action = e_cal_component_alarm_get_action (alarm);
		trigger = e_cal_component_alarm_get_trigger (alarm);
		repeat = e_cal_component_alarm_get_repeat (alarm);

		if (!trigger) {
			e_cal_component_alarm_free (alarm);
			continue;
		}

		for (i = 0; aod->omit[i] != -1; i++) {
			if (aod->omit[i] == action)
				break;
		}

		if (aod->omit[i] != -1) {
			e_cal_component_alarm_free (alarm);
			continue;
		}

		if (e_cal_component_alarm_trigger_get_kind (trigger) != E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START &&
		    e_cal_component_alarm_trigger_get_kind (trigger) != E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END) {
			e_cal_component_alarm_free (alarm);
			continue;
		}

		dur = e_cal_component_alarm_trigger_get_duration (trigger);
		dur_time = i_cal_duration_as_int (dur);

		if (e_cal_component_alarm_trigger_get_kind (trigger) == E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START)
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

		if (repeat && e_cal_component_alarm_repeat_get_repetitions (repeat) != 0) {
			gint ii, repetitions;
			time_t repeat_time;

			repeat_time = e_cal_component_alarm_repeat_get_interval_seconds (repeat);
			repetitions = e_cal_component_alarm_repeat_get_repetitions (repeat);

			for (ii = 0; ii < repetitions; ii++) {
				time_t t;

				t = trigger_time + (ii + 1) * repeat_time;

				if (t >= aod->start && t < aod->end)
					add_trigger (aod, auid, t, start, end);
			}
		}

		/* Add the trigger itself */

		if (trigger_time >= aod->start && trigger_time < aod->end)
			add_trigger (aod, auid, trigger_time, start, end);

		e_cal_component_alarm_free (alarm);
	}

	return TRUE;
}

/* Generates the absolute triggers for a component */
static void
generate_absolute_triggers (ECalComponent *comp,
                            struct alarm_occurrence_data *aod,
                            ECalRecurResolveTimezoneCb resolve_tzid,
                            gpointer user_data,
                            ICalTimezone *default_timezone)
{
	GSList *link;
	ECalComponentDateTime *dtstart, *dtend;
	time_t occur_start, occur_end;

	dtstart = e_cal_component_get_dtstart (comp);
	dtend = e_cal_component_get_dtend (comp);

	/* No particular occurrence, so just use the times from the
	 * component */

	if (dtstart && e_cal_component_datetime_get_value (dtstart)) {
		ICalTimezone *zone;
		const gchar *tzid = e_cal_component_datetime_get_tzid (dtstart);

		if (tzid && !i_cal_time_is_date (e_cal_component_datetime_get_value (dtstart)))
			zone = (* resolve_tzid) (tzid, user_data, NULL, NULL);
		else
			zone = default_timezone;

		occur_start = i_cal_time_as_timet_with_zone (e_cal_component_datetime_get_value (dtstart), zone);
	} else
		occur_start = -1;

	if (dtend && e_cal_component_datetime_get_value (dtend)) {
		ICalTimezone *zone;
		const gchar *tzid = e_cal_component_datetime_get_tzid (dtend);

		if (tzid && !i_cal_time_is_date (e_cal_component_datetime_get_value (dtend)))
			zone = (* resolve_tzid) (tzid, user_data, NULL, NULL);
		else
			zone = default_timezone;

		occur_end = i_cal_time_as_timet_with_zone (e_cal_component_datetime_get_value (dtend), zone);
	} else
		occur_end = -1;

	for (link = aod->alarm_uids; link; link = g_slist_next (link)) {
		const gchar *auid;
		ECalComponentAlarm *alarm;
		ECalComponentAlarmAction action;
		ECalComponentAlarmRepeat *repeat;
		ECalComponentAlarmTrigger *trigger;
		time_t abs_time;
		ICalTimezone *zone;
		gint i;

		auid = link->data;
		alarm = e_cal_component_get_alarm (comp, auid);
		g_return_if_fail (alarm != NULL);

		action = e_cal_component_alarm_get_action (alarm);
		trigger = e_cal_component_alarm_get_trigger (alarm);
		repeat = e_cal_component_alarm_get_repeat (alarm);

		for (i = 0; aod->omit[i] != -1; i++) {
			if (aod->omit[i] == action)
				break;
		}

		if (aod->omit[i] != -1) {
			e_cal_component_alarm_free (alarm);
			continue;
		}

		if (e_cal_component_alarm_trigger_get_kind (trigger) != E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE) {
			e_cal_component_alarm_free (alarm);
			continue;
		}

		/* Absolute triggers are always in UTC;
		 * see RFC 2445 section 4.8.6.3 */
		zone = i_cal_timezone_get_utc_timezone ();

		abs_time = i_cal_time_as_timet_with_zone (e_cal_component_alarm_trigger_get_absolute_time (trigger), zone);

		/* Add repeating alarms */

		if (repeat && e_cal_component_alarm_repeat_get_repetitions (repeat) > 0) {
			gint ii, repetitions;
			time_t repeat_time;

			repeat_time = e_cal_component_alarm_repeat_get_interval_seconds (repeat);
			repetitions = e_cal_component_alarm_repeat_get_repetitions (repeat);

			for (ii = 0; ii < repetitions; ii++) {
				time_t tt;

				tt = abs_time + (ii + 1) * repeat_time;

				if (tt >= aod->start && tt < aod->end)
					add_trigger (aod, auid, tt, occur_start, occur_end);
			}
		}

		/* Add the trigger itself */

		if (abs_time >= aod->start && abs_time < aod->end)
			add_trigger (aod, auid, abs_time, occur_start, occur_end);

		e_cal_component_alarm_free (alarm);
	}

	e_cal_component_datetime_free (dtstart);
	e_cal_component_datetime_free (dtend);
}

/* Compares two alarm instances; called from g_slist_sort() */
static gint
compare_alarm_instance (gconstpointer a,
                        gconstpointer b)
{
	const ECalComponentAlarmInstance *aia, *aib;
	time_t atime, btime;

	aia = a;
	aib = b;

	atime = e_cal_component_alarm_instance_get_time (aia);
	btime = e_cal_component_alarm_instance_get_time (aib);

	if (atime < btime)
		return -1;
	else if (atime > btime)
		return 1;
	else
		return 0;
}

/**
 * e_cal_util_generate_alarms_for_comp:
 * @comp: The #ECalComponent to generate alarms from
 * @start: Start time
 * @end: End time
 * @omit: Alarm types to omit
 * @resolve_tzid: (closure user_data) (scope call): Callback for resolving
 * timezones
 * @user_data: (closure): Data to be passed to the resolve_tzid callback
 * @default_timezone: The timezone used to resolve DATE and floating DATE-TIME
 * values.
 *
 * Generates alarm instances for a calendar component. Returns the instances
 * structure, or %NULL if no alarm instances occurred in the specified time
 * range. Free the returned structure with e_cal_component_alarms_free(),
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): a list of all the alarms found
 *    for the given component in the given time range.
 */
ECalComponentAlarms *
e_cal_util_generate_alarms_for_comp (ECalComponent *comp,
                                     time_t start,
                                     time_t end,
                                     ECalComponentAlarmAction *omit,
                                     ECalRecurResolveTimezoneCb resolve_tzid,
                                     gpointer user_data,
                                     ICalTimezone *default_timezone)
{
	GSList *alarm_uids;
	time_t alarm_start, alarm_end;
	struct alarm_occurrence_data aod;
	ICalTime *alarm_start_tt, *alarm_end_tt;
	ECalComponentAlarms *alarms;

	if (!e_cal_component_has_alarms (comp))
		return NULL;

	alarm_uids = e_cal_component_get_alarm_uids (comp);
	compute_alarm_range (comp, alarm_uids, start, end, &alarm_start, &alarm_end);

	aod.comp = comp;
	aod.alarm_uids = alarm_uids;
	aod.start = start;
	aod.end = end;
	aod.omit = omit;
	aod.triggers = NULL;
	aod.n_triggers = 0;

	alarm_start_tt = i_cal_time_new_from_timet_with_zone (alarm_start, FALSE, i_cal_timezone_get_utc_timezone ());
	alarm_end_tt = i_cal_time_new_from_timet_with_zone (alarm_end, FALSE, i_cal_timezone_get_utc_timezone ());

	e_cal_recur_generate_instances_sync (e_cal_component_get_icalcomponent (comp),
		alarm_start_tt, alarm_end_tt,
		add_alarm_occurrences_cb, &aod,
		resolve_tzid, user_data,
		default_timezone, NULL, NULL);

	g_clear_object (&alarm_start_tt);
	g_clear_object (&alarm_end_tt);

	/* We add the ABSOLUTE triggers separately */
	generate_absolute_triggers (comp, &aod, resolve_tzid, user_data, default_timezone);

	g_slist_free_full (alarm_uids, g_free);

	if (aod.n_triggers == 0)
		return NULL;

	/* Create the component alarm instances structure */

	alarms = e_cal_component_alarms_new (comp);
	e_cal_component_alarms_take_instances (alarms, g_slist_sort (aod.triggers, compare_alarm_instance));

	return alarms;
}

/**
 * e_cal_util_generate_alarms_for_list:
 * @comps: (element-type ECalComponent): List of #ECalComponent<!-- -->s
 * @start: Start time
 * @end: End time
 * @omit: Alarm types to omit
 * @comp_alarms: (out) (transfer full) (element-type ECalComponentAlarms): List
 * to be returned
 * @resolve_tzid: (closure user_data) (scope call): Callback for resolving
 * timezones
 * @user_data: (closure): Data to be passed to the resolve_tzid callback
 * @default_timezone: The timezone used to resolve DATE and floating DATE-TIME
 * values.
 *
 * Iterates through all the components in the @comps list and generates alarm
 * instances for them; putting them in the @comp_alarms list. Free the @comp_alarms
 * with g_slist_free_full (comp_alarms, e_cal_component_alarms_free);, when
 * no longer neeed.
 *
 * Returns: the number of elements it added to the list
 */
gint
e_cal_util_generate_alarms_for_list (GList *comps,
                                     time_t start,
                                     time_t end,
                                     ECalComponentAlarmAction *omit,
                                     GSList **comp_alarms,
                                     ECalRecurResolveTimezoneCb resolve_tzid,
                                     gpointer user_data,
                                     ICalTimezone *default_timezone)
{
	GList *l;
	gint n;

	n = 0;

	for (l = comps; l; l = l->next) {
		ECalComponent *comp;
		ECalComponentAlarms *alarms;

		comp = E_CAL_COMPONENT (l->data);
		alarms = e_cal_util_generate_alarms_for_comp (
			comp, start, end, omit, resolve_tzid,
			user_data, default_timezone);

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
		retval = C_("Priority", "High");
	else if (priority == 5)
		retval = C_("Priority", "Normal");
	else if (priority <= 9)
		retval = C_("Priority", "Low");
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
	if (!string || !string[0] || !e_util_utf8_strcasecmp (string, C_("Priority", "Undefined")))
		priority = 0;
	else if (!e_util_utf8_strcasecmp (string, C_("Priority", "High")))
		priority = 3;
	else if (!e_util_utf8_strcasecmp (string, C_("Priority", "Normal")))
		priority = 5;
	else if (!e_util_utf8_strcasecmp (string, C_("Priority", "Low")))
		priority = 7;
	else
		priority = -1;

	return priority;
}

/**
 * e_cal_util_seconds_to_string:
 * @seconds: actual time, in seconds
 *
 * Converts time, in seconds, into a string representation readable by humans
 * and localized into the current locale. This can be used to convert event
 * duration to string or similar use cases.
 *
 * Free the returned string with g_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated string with localized description
 *    of the given time in seconds.
 *
 * Since: 3.30
 **/
gchar *
e_cal_util_seconds_to_string (gint64 seconds)
{
	gchar *times[6], *text;
	gint ii;

	ii = 0;
	if (seconds >= 7 * 24 * 3600) {
		gint weeks;

		weeks = seconds / (7 * 24 * 3600);
		seconds %= (7 * 24 * 3600);

		times[ii++] = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d week", "%d weeks", weeks), weeks);
	}

	if (seconds >= 24 * 3600) {
		gint days;

		days = seconds / (24 * 3600);
		seconds %= (24 * 3600);

		times[ii++] = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d day", "%d days", days), days);
	}

	if (seconds >= 3600) {
		gint hours;

		hours = seconds / 3600;
		seconds %= 3600;

		times[ii++] = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d hour", "%d hours", hours), hours);
	}

	if (seconds >= 60) {
		gint minutes;

		minutes = seconds / 60;
		seconds %= 60;

		times[ii++] = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d minute", "%d minutes", minutes), minutes);
	}

	if (seconds != 0) {
		/* Translators: here, "second" is the time division (like "minute"), not the ordinal number (like "third") */
		times[ii++] = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d second", "%d seconds", seconds), (gint) seconds);
	}

	times[ii] = NULL;
	text = g_strjoinv (" ", times);
	while (ii > 0) {
		g_free (times[--ii]);
	}

	return text;
}

/* callback for icalcomponent_foreach_tzid */
typedef struct {
	ICalComponent *vcal_comp;
	ICalComponent *icalcomp;
} ForeachTzidData;

static void
add_timezone_cb (ICalParameter *param,
                 gpointer user_data)
{
	ICalTimezone *tz;
	const gchar *tzid;
	ICalComponent *vtz_comp;
	ForeachTzidData *f_data = user_data;

	tzid = i_cal_parameter_get_tzid (param);
	if (!tzid)
		return;

	tz = i_cal_component_get_timezone (f_data->vcal_comp, tzid);
	if (tz) {
		g_object_unref (tz);
		return;
	}

	tz = i_cal_component_get_timezone (f_data->icalcomp, tzid);
	if (!tz) {
		tz = i_cal_timezone_get_builtin_timezone_from_tzid (tzid);
		if (!tz)
			return;

		g_object_ref (tz);
	}

	vtz_comp = i_cal_timezone_get_component (tz);
	if (vtz_comp) {
		i_cal_component_take_component (f_data->vcal_comp, i_cal_component_clone (vtz_comp));
		g_object_unref (vtz_comp);
	}

	g_object_unref (tz);
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
e_cal_util_add_timezones_from_component (ICalComponent *vcal_comp,
					 ICalComponent *icalcomp)
{
	ForeachTzidData f_data;

	g_return_if_fail (vcal_comp != NULL);
	g_return_if_fail (icalcomp != NULL);

	f_data.vcal_comp = vcal_comp;
	f_data.icalcomp = icalcomp;
	i_cal_component_foreach_tzid (icalcomp, add_timezone_cb, &f_data);
}

/**
 * e_cal_util_property_has_parameter:
 * @prop: an #ICalProperty
 * @param_kind: a parameter kind to look for, as an %ICalParameterKind
 *
 * Returns, whether the @prop has a parameter of @param_kind.
 *
 * Returns: whether the @prop has a parameter of @prop_kind
 *
 * Since: 3.34
 **/
gboolean
e_cal_util_property_has_parameter (ICalProperty *prop,
				   ICalParameterKind param_kind)
{
	ICalParameter *param;

	g_return_val_if_fail (I_CAL_IS_PROPERTY (prop), FALSE);

	param = i_cal_property_get_first_parameter (prop, param_kind);

	if (!param)
		return FALSE;

	g_object_unref (param);

	return TRUE;
}

/**
 * e_cal_util_component_has_property:
 * @icalcomp: an #ICalComponent
 * @prop_kind: a property kind to look for, as an %ICalPropertyKind
 *
 * Returns, whether the @icalcomp has a property of @prop_kind. To check
 * for a specific X property use e_cal_util_component_has_x_property().
 *
 * Returns: whether the @icalcomp has a property of @prop_kind
 *
 * Since: 3.34
 **/
gboolean
e_cal_util_component_has_property (ICalComponent *icalcomp,
				   ICalPropertyKind prop_kind)
{
	ICalProperty *prop;

	g_return_val_if_fail (I_CAL_IS_COMPONENT (icalcomp), FALSE);

	prop = i_cal_component_get_first_property (icalcomp, prop_kind);

	if (!prop)
		return FALSE;

	g_object_unref (prop);

	return TRUE;
}

/**
 * e_cal_util_component_is_instance:
 * @icalcomp: An #ICalComponent.
 *
 * Checks whether an #ICalComponent is an instance of a recurring appointment.
 *
 * Returns: TRUE if it is an instance, FALSE if not.
 */
gboolean
e_cal_util_component_is_instance (ICalComponent *icalcomp)
{
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	return e_cal_util_component_has_property (icalcomp, I_CAL_RECURRENCEID_PROPERTY);
}

/**
 * e_cal_util_component_has_alarms:
 * @icalcomp: An #ICalComponent.
 *
 * Checks whether an #ICalComponent has any alarm.
 *
 * Returns: TRUE if it has alarms, FALSE otherwise.
 */
gboolean
e_cal_util_component_has_alarms (ICalComponent *icalcomp)
{
	ICalComponent *alarm;

	g_return_val_if_fail (icalcomp != NULL, FALSE);

	alarm = i_cal_component_get_first_component (icalcomp, I_CAL_VALARM_COMPONENT);

	if (!alarm)
		return FALSE;

	g_object_unref (alarm);

	return TRUE;
}

/**
 * e_cal_util_component_has_organizer:
 * @icalcomp: An #ICalComponent.
 *
 * Checks whether an #ICalComponent has an organizer.
 *
 * Returns: TRUE if there is an organizer, FALSE if not.
 */
gboolean
e_cal_util_component_has_organizer (ICalComponent *icalcomp)
{
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	return e_cal_util_component_has_property (icalcomp, I_CAL_ORGANIZER_PROPERTY);
}

/**
 * e_cal_util_component_has_attendee:
 * @icalcomp: An #ICalComponent.
 *
 * Checks if an #ICalComponent has any attendees.
 *
 * Returns: TRUE if there are attendees, FALSE if not.
 */
gboolean
e_cal_util_component_has_attendee (ICalComponent *icalcomp)
{
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	return e_cal_util_component_has_property (icalcomp, I_CAL_ATTENDEE_PROPERTY);
}

/**
 * e_cal_util_component_get_recurid_as_string:
 * @icalcomp: an #ICalComponent
 *
 * Returns: (transfer full) (nullable): a RECURRENCEID property as string,
 *    or %NULL, when the @icalcomp is not an instance. Free the returned
 *    string with g_free(), when no longer needed.
 *
 * Since: 3.34
 **/
gchar *
e_cal_util_component_get_recurid_as_string (ICalComponent *icalcomp)
{
	ICalProperty *prop;
	ICalTime *recurid;
	gchar *rid;

	g_return_val_if_fail (icalcomp != NULL, NULL);

	prop = i_cal_component_get_first_property (icalcomp, I_CAL_RECURRENCEID_PROPERTY);
	if (!prop)
		return NULL;

	recurid = i_cal_property_get_recurrenceid (prop);
	if (!recurid ||
	    !i_cal_time_is_valid_time (recurid) ||
	    i_cal_time_is_null_time (recurid)) {
		rid = g_strdup ("0");
	} else {
		rid = i_cal_time_as_ical_string (recurid);
	}

	g_clear_object (&recurid);
	g_object_unref (prop);

	return rid;
}

/**
 * e_cal_util_component_has_recurrences:
 * @icalcomp: An #ICalComponent.
 *
 * Checks if an #ICalComponent has recurrence dates or rules.
 *
 * Returns: TRUE if there are recurrence dates/rules, FALSE if not.
 */
gboolean
e_cal_util_component_has_recurrences (ICalComponent *icalcomp)
{
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	return e_cal_util_component_has_rdates (icalcomp) ||
		e_cal_util_component_has_rrules (icalcomp);
}

/**
 * e_cal_util_component_has_rdates:
 * @icalcomp: An #ICalComponent.
 *
 * Checks if an #ICalComponent has recurrence dates.
 *
 * Returns: TRUE if there are recurrence dates, FALSE if not.
 */
gboolean
e_cal_util_component_has_rdates (ICalComponent *icalcomp)
{
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	return e_cal_util_component_has_property (icalcomp, I_CAL_RDATE_PROPERTY);
}

/**
 * e_cal_util_component_has_rrules:
 * @icalcomp: An #ICalComponent.
 *
 * Checks if an #ICalComponent has recurrence rules.
 *
 * Returns: TRUE if there are recurrence rules, FALSE if not.
 */
gboolean
e_cal_util_component_has_rrules (ICalComponent *icalcomp)
{
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	return e_cal_util_component_has_property (icalcomp, I_CAL_RRULE_PROPERTY);
}

/* Individual instances management */

struct instance_data {
	time_t start;
	gboolean found;
};

static void
check_instance (ICalComponent *comp,
		ICalTimeSpan *span,
		gpointer user_data)
{
	struct instance_data *instance = user_data;

	if (i_cal_time_span_get_start (span) == instance->start)
		instance->found = TRUE;
}

/**
 * e_cal_util_construct_instance:
 * @icalcomp: A recurring #ICalComponent
 * @rid: The RECURRENCE-ID to construct a component for
 *
 * This checks that @rid indicates a valid recurrence of @icalcomp, and
 * if so, generates a copy of @icalcomp containing a RECURRENCE-ID of @rid.
 *
 * Free the returned non-NULL component with g_object_unref(), when
 * no longer needed.
 *
 * Returns: (transfer full) (nullable): the instance as a new #ICalComponent, or %NULL.
 **/
ICalComponent *
e_cal_util_construct_instance (ICalComponent *icalcomp,
			       const ICalTime *rid)
{
	struct instance_data instance;
	ICalTime *start, *end;

	g_return_val_if_fail (icalcomp != NULL, NULL);

	/* Make sure this is really recurring */
	if (!e_cal_util_component_has_recurrences (icalcomp))
		return NULL;

	/* Make sure the specified instance really exists */
	start = i_cal_time_convert_to_zone ((ICalTime *) rid, i_cal_timezone_get_utc_timezone ());
	end = i_cal_time_clone (start);
	i_cal_time_adjust (end, 0, 0, 0, 1);

	instance.start = i_cal_time_as_timet (start);
	instance.found = FALSE;
	i_cal_component_foreach_recurrence (icalcomp, start, end, check_instance, &instance);

	g_object_unref (start);
	g_object_unref (end);

	if (!instance.found)
		return NULL;

	/* Make the instance */
	icalcomp = i_cal_component_clone (icalcomp);
	i_cal_component_set_recurrenceid (icalcomp, (ICalTime *) rid);

	return icalcomp;
}

static inline gboolean
time_matches_rid (const ICalTime *itt,
                  const ICalTime *rid,
                  ECalObjModType mod)
{
	gint compare;

	compare = i_cal_time_compare ((ICalTime *) itt, (ICalTime *) rid);
	if (compare == 0)
		return TRUE;
	else if (compare < 0 && (mod & E_CAL_OBJ_MOD_THIS_AND_PRIOR))
		return TRUE;
	else if (compare > 0 && (mod & E_CAL_OBJ_MOD_THIS_AND_FUTURE))
		return TRUE;

	return FALSE;
}

static void
e_cal_util_remove_instances_ex (ICalComponent *icalcomp,
				const ICalTime *rid,
				ECalObjModType mod,
				gboolean keep_rid,
				gboolean can_add_exrule)
{
	ICalProperty *prop;
	ICalTime *itt, *recur;
	ICalRecurrence *rule;
	ICalRecurIterator *iter;
	GSList *remove_props = NULL, *rrules = NULL, *link;

	g_return_if_fail (icalcomp != NULL);
	g_return_if_fail (mod != E_CAL_OBJ_MOD_ALL);

	/* First remove RDATEs and EXDATEs in the indicated range. */
	for (prop = i_cal_component_get_first_property (icalcomp, I_CAL_RDATE_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icalcomp, I_CAL_RDATE_PROPERTY)) {
		ICalDatetimeperiod *period;
		ICalTime *period_time;

		period = i_cal_property_get_rdate (prop);
		if (!period)
			continue;

		period_time = i_cal_datetimeperiod_get_time (period);

		if (time_matches_rid (period_time, rid, mod) && (!keep_rid ||
		    i_cal_time_compare (period_time, (ICalTime *) rid) != 0))
			remove_props = g_slist_prepend (remove_props, g_object_ref (prop));

		g_clear_object (&period_time);
		g_object_unref (period);
	}

	for (prop = i_cal_component_get_first_property (icalcomp, I_CAL_EXDATE_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icalcomp, I_CAL_EXDATE_PROPERTY)) {
		itt = i_cal_property_get_exdate (prop);
		if (!itt)
			continue;

		if (time_matches_rid (itt, rid, mod) && (!keep_rid || i_cal_time_compare (itt, (ICalTime *) rid) != 0))
			remove_props = g_slist_prepend (remove_props, g_object_ref (prop));

		g_object_unref (itt);
	}

	for (link = remove_props; link; link = g_slist_next (link)) {
		prop = link->data;

		i_cal_component_remove_property (icalcomp, prop);
	}

	g_slist_free_full (remove_props, g_object_unref);
	remove_props = NULL;

	/* If we're only removing one instance, just add an EXDATE. */
	if (mod == E_CAL_OBJ_MOD_THIS) {
		prop = i_cal_property_new_exdate ((ICalTime *) rid);
		i_cal_component_take_property (icalcomp, prop);
		return;
	}

	/* Otherwise, iterate through RRULEs */
	/* FIXME: this may generate duplicate EXRULEs */
	for (prop = i_cal_component_get_first_property (icalcomp, I_CAL_RRULE_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icalcomp, I_CAL_RRULE_PROPERTY)) {
		rrules = g_slist_prepend (rrules, g_object_ref (prop));
	}

	for (link = rrules; link; link = g_slist_next (link)) {
		prop = link->data;

		rule = i_cal_property_get_rrule (prop);
		if (!rule)
			continue;

		iter = i_cal_recur_iterator_new (rule, (ICalTime *) rid);
		recur = i_cal_recur_iterator_next (iter);

		if (!recur) {
			g_object_unref (rule);
			g_object_unref (iter);
			continue;
		}

		if (mod & E_CAL_OBJ_MOD_THIS_AND_FUTURE) {
			/* Truncate the rule at rid. */
			if (!i_cal_time_is_null_time (recur)) {
				gint rule_count = i_cal_recurrence_get_count (rule);

				/* Use count if it was used */
				if (rule_count > 0) {
					gint occurrences_count = 0;
					ICalRecurIterator *count_iter;
					ICalTime *count_recur, *dtstart;

					dtstart = i_cal_component_get_dtstart (icalcomp);
					count_iter = i_cal_recur_iterator_new (rule, dtstart);
					while (count_recur = i_cal_recur_iterator_next (count_iter),
					       count_recur && !i_cal_time_is_null_time (count_recur) && occurrences_count < rule_count) {
						if (i_cal_time_compare (count_recur, (ICalTime *) rid) >= 0)
							break;

						occurrences_count++;
						g_object_unref (count_recur);
					}

					if (keep_rid && count_recur && i_cal_time_compare (count_recur, (ICalTime *) rid) == 0)
						occurrences_count++;

					/* The caller should make sure that the remove will keep at least one instance */
					g_warn_if_fail (occurrences_count > 0);

					i_cal_recurrence_set_count (rule, occurrences_count);

					g_clear_object (&count_recur);
					g_clear_object (&count_iter);
					g_clear_object (&dtstart);
				} else {
					ICalTime *ttuntil;

					if (keep_rid && i_cal_time_compare (recur, (ICalTime *) rid) == 0) {
						ICalDuration *dur;

						dur = i_cal_component_get_duration (icalcomp);
						ttuntil = i_cal_time_add ((ICalTime *) rid, dur);
						g_clear_object (&dur);
					} else {
						ttuntil = i_cal_time_clone (rid);
					}
					i_cal_time_adjust (ttuntil, 0, 0, 0, -1);
					i_cal_recurrence_set_until (rule, ttuntil);
					g_object_unref (ttuntil);
				}

				i_cal_property_set_rrule (prop, rule);
				i_cal_property_remove_parameter_by_name (prop, E_CAL_EVOLUTION_ENDDATE_PARAMETER);
			}
		} else {
			/* (If recur == rid, skip to the next occurrence) */
			if (!keep_rid && i_cal_time_compare (recur, (ICalTime *) rid) == 0) {
				g_object_unref (recur);
				recur = i_cal_recur_iterator_next (iter);
			}

			/* If there is a recurrence after rid, add
			 * an EXRULE to block instances up to rid.
			 * Otherwise, just remove the RRULE.
			 */
			if (!i_cal_time_is_null_time (recur)) {
				if (can_add_exrule) {
					ICalTime *ttuntil;
					ICalDuration *dur = i_cal_component_get_duration (icalcomp);

					i_cal_recurrence_set_count (rule, 0);

					/* iCalendar says we should just use rid
					 * here, but Outlook/Exchange handle
					 * UNTIL incorrectly.
					 */
					if (keep_rid && i_cal_time_compare (recur, (ICalTime *) rid) == 0) {
						i_cal_duration_set_is_neg (dur, !i_cal_duration_is_neg (dur));
						ttuntil = i_cal_time_add ((ICalTime *) rid, dur);
					} else {
						ttuntil = i_cal_time_add ((ICalTime *) rid, dur);
					}

					i_cal_recurrence_set_until (rule, ttuntil);

					g_clear_object (&ttuntil);
					g_clear_object (&dur);

					prop = i_cal_property_new_exrule (rule);
					i_cal_component_take_property (icalcomp, prop);
				}
			} else {
				remove_props = g_slist_prepend (remove_props, g_object_ref (prop));
			}
		}

		g_object_unref (recur);
		g_object_unref (rule);
		g_object_unref (iter);
	}

	for (link = remove_props; link; link = g_slist_next (link)) {
		prop = link->data;

		i_cal_component_remove_property (icalcomp, prop);
	}

	g_slist_free_full (remove_props, g_object_unref);
	g_slist_free_full (rrules, g_object_unref);
}

/**
 * e_cal_util_remove_instances:
 * @icalcomp: A (recurring) #ICalComponent
 * @rid: The base RECURRENCE-ID to remove
 * @mod: How to interpret @rid
 *
 * Removes one or more instances from @icalcomp according to @rid and @mod.
 **/
void
e_cal_util_remove_instances (ICalComponent *icalcomp,
                             const ICalTime *rid,
                             ECalObjModType mod)
{
	g_return_if_fail (icalcomp != NULL);
	g_return_if_fail (rid != NULL);
	g_return_if_fail (mod != E_CAL_OBJ_MOD_ALL);

	e_cal_util_remove_instances_ex (icalcomp, rid, mod, FALSE, TRUE);
}

/**
 * e_cal_util_split_at_instance:
 * @icalcomp: A (recurring) #ICalComponent
 * @rid: The base RECURRENCE-ID to remove
 * @master_dtstart: (nullable): The DTSTART of the master object
 *
 * Splits a recurring @icalcomp into two at time @rid. The returned #ICalComponent
 * is modified @icalcomp which contains recurrences beginning at @rid, inclusive.
 * The instance identified by @rid should exist. The @master_dtstart can be
 * a null time, then it is read from the @icalcomp.
 *
 * Use e_cal_util_remove_instances() with E_CAL_OBJ_MOD_THIS_AND_FUTURE mode
 * on the @icalcomp to remove the overlapping interval from it, if needed.
 *
 * Free the returned non-NULL component with g_object_unref(), when
 * done with it.
 *
 * Returns: (transfer full) (nullable): the split @icalcom, or %NULL.
 *
 * Since: 3.16
 **/
ICalComponent *
e_cal_util_split_at_instance (ICalComponent *icalcomp,
			      const ICalTime *rid,
			      const ICalTime *master_dtstart)
{
	ICalProperty *prop;
	struct instance_data instance;
	ICalTime *start, *end, *dtstart = NULL;
	ICalDuration *duration;
	GSList *remove_props = NULL, *link;

	g_return_val_if_fail (icalcomp != NULL, NULL);
	g_return_val_if_fail (rid != NULL, NULL);
	g_return_val_if_fail (!i_cal_time_is_null_time ((ICalTime *) rid), NULL);

	/* Make sure this is really recurring */
	if (!e_cal_util_component_has_recurrences (icalcomp))
		return NULL;

	/* Make sure the specified instance really exists */
	start = i_cal_time_convert_to_zone ((ICalTime *) rid, i_cal_timezone_get_utc_timezone ());
	end = i_cal_time_clone (start);
	i_cal_time_adjust (end, 0, 0, 0, 1);

	instance.start = i_cal_time_as_timet (start);
	instance.found = FALSE;
	i_cal_component_foreach_recurrence (icalcomp, start, end, check_instance, &instance);
	g_clear_object (&start);
	g_clear_object (&end);

	/* Make the copy */
	icalcomp = i_cal_component_clone (icalcomp);

	e_cal_util_remove_instances_ex (icalcomp, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR, TRUE, FALSE);

	start = i_cal_time_clone ((ICalTime *) rid);

	if (!master_dtstart || i_cal_time_is_null_time ((ICalTime *) master_dtstart)) {
		dtstart = i_cal_component_get_dtstart (icalcomp);
		master_dtstart = dtstart;
	}

	duration = i_cal_component_get_duration (icalcomp);

	/* Expect that DTSTART and DTEND are already set when the instance could not be found */
	if (instance.found) {
		ICalTime *dtend;

		dtend = i_cal_component_get_dtend (icalcomp);

		i_cal_component_set_dtstart (icalcomp, start);

		/* Update either DURATION or DTEND */
		if (i_cal_time_is_null_time (dtend)) {
			i_cal_component_set_duration (icalcomp, duration);
		} else {
			end = i_cal_time_clone (start);
			if (i_cal_duration_is_neg (duration))
				i_cal_time_adjust (end, -i_cal_duration_get_days (duration)
							- 7 * i_cal_duration_get_weeks (duration),
							-i_cal_duration_get_hours (duration),
							-i_cal_duration_get_minutes (duration),
							-i_cal_duration_get_seconds (duration));
			else
				i_cal_time_adjust (end, i_cal_duration_get_days (duration)
							+ 7 * i_cal_duration_get_weeks (duration),
							i_cal_duration_get_hours (duration),
							i_cal_duration_get_minutes (duration),
							i_cal_duration_get_seconds (duration));

			i_cal_component_set_dtend (icalcomp, end);
		}

		g_clear_object (&dtend);
	}

	g_clear_object (&start);
	g_clear_object (&end);
	g_clear_object (&duration);

	/* any RRULE with 'count' should be shortened */
	for (prop = i_cal_component_get_first_property (icalcomp, I_CAL_RRULE_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icalcomp, I_CAL_RRULE_PROPERTY)) {
		ICalTime *recur;
		ICalRecurrence *rule;
		gint rule_count;

		rule = i_cal_property_get_rrule (prop);
		if (!rule)
			continue;

		rule_count = i_cal_recurrence_get_count (rule);

		if (rule_count != 0) {
			gint occurrences_count = 0;
			ICalRecurIterator *iter;

			iter = i_cal_recur_iterator_new (rule, (ICalTime *) master_dtstart);
			while (recur = i_cal_recur_iterator_next (iter),
			       recur && !i_cal_time_is_null_time (recur) && occurrences_count < rule_count) {
				if (i_cal_time_compare (recur, (ICalTime *) rid) >= 0)
					break;

				occurrences_count++;
				g_object_unref (recur);
			}

			if (!recur || i_cal_time_is_null_time (recur)) {
				remove_props = g_slist_prepend (remove_props, g_object_ref (prop));
			} else {
				i_cal_recurrence_set_count (rule, rule_count - occurrences_count);
				i_cal_property_set_rrule (prop, rule);
				i_cal_property_remove_parameter_by_name (prop, E_CAL_EVOLUTION_ENDDATE_PARAMETER);
			}

			g_clear_object (&iter);
			g_clear_object (&recur);
		}

		g_object_unref (rule);
	}

	for (link = remove_props; link; link = g_slist_next (link)) {
		prop = link->data;

		i_cal_component_remove_property (icalcomp, prop);
	}

	g_slist_free_full (remove_props, g_object_unref);
	g_clear_object (&dtstart);

	return icalcomp;
}

typedef struct {
	const ICalTime *rid;
	gboolean matches;
} CheckFirstInstanceData;

static gboolean
check_first_instance_cb (ICalComponent *icalcomp,
			 ICalTime *instance_start,
			 ICalTime *instance_end,
			 gpointer user_data,
			 GCancellable *cancellable,
			 GError **error)
{
	CheckFirstInstanceData *ifs = user_data;
	ICalProperty *prop;
	ICalTime *rid;

	g_return_val_if_fail (ifs != NULL, FALSE);

	prop = i_cal_component_get_first_property (icalcomp, I_CAL_RECURRENCEID_PROPERTY);
	if (prop) {
		rid = i_cal_property_get_recurrenceid (prop);
		g_object_unref (prop);
	} else {
		ICalTime *dtstart;
		ICalTimezone *zone;

		dtstart = i_cal_component_get_dtstart (icalcomp);
		zone = i_cal_time_get_timezone (dtstart);

		rid = i_cal_time_new_from_timet_with_zone (i_cal_time_as_timet (instance_start), i_cal_time_is_date (dtstart), zone);

		g_clear_object (&dtstart);
	}

	ifs->matches = i_cal_time_compare ((ICalTime *) ifs->rid, rid) == 0;

	g_clear_object (&rid);

	return FALSE;
}

/**
 * e_cal_util_is_first_instance:
 * @comp: an #ECalComponent instance
 * @rid: a recurrence ID
 * @tz_cb: (closure tz_cb_data) (scope call): The #ECalRecurResolveTimezoneCb to call
 * @tz_cb_data: (closure): User data to be passed to the @tz_cb callback
 *
 * Returns whether the given @rid is the first instance of
 * the recurrence defined in the @comp.
 *
 * Return: Whether the @rid identifies the first instance of @comp.
 *
 * Since: 3.16
 **/
gboolean
e_cal_util_is_first_instance (ECalComponent *comp,
			      const ICalTime *rid,
			      ECalRecurResolveTimezoneCb tz_cb,
			      gpointer tz_cb_data)
{
	CheckFirstInstanceData ifs;
	ICalComponent *icalcomp;
	ICalTime *start, *end;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);
	g_return_val_if_fail (rid && !i_cal_time_is_null_time ((ICalTime *) rid), FALSE);

	ifs.rid = rid;
	ifs.matches = FALSE;

	icalcomp = e_cal_component_get_icalcomponent (comp);

	start = i_cal_component_get_dtstart (icalcomp);
	i_cal_time_adjust (start, -1, 0, 0, 0);

	end = i_cal_component_get_dtend (icalcomp);
	i_cal_time_adjust (end, +1, 0, 0, 0);

	e_cal_recur_generate_instances_sync (e_cal_component_get_icalcomponent (comp),
		start, end,
		check_first_instance_cb, &ifs,
		tz_cb, tz_cb_data, i_cal_timezone_get_utc_timezone (),
		NULL, NULL);

	g_clear_object (&start);
	g_clear_object (&end);

	return ifs.matches;
}

/**
 * e_cal_util_get_system_timezone_location:
 *
 * Fetches system timezone localtion string.
 *
 * Returns: (transfer full): system timezone location string, %NULL on an error.
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
 * Fetches system timezone ICalTimezone object.
 *
 * The returned pointer is part of the built-in timezones and should not be freed.
 *
 * Returns: (transfer none) (nullable): The ICalTimezone object of the system timezone, or %NULL on an error.
 *
 * Since: 2.28
 **/
ICalTimezone *
e_cal_util_get_system_timezone (void)
{
	gchar *location;
	ICalTimezone *zone;

	location = e_cal_system_timezone_get_location ();

	/* Can be NULL when failed to detect system time zone */
	if (!location)
		return NULL;

	zone = i_cal_timezone_get_builtin_timezone (location);

	g_free (location);

	return zone;
}

static time_t
componenttime_to_utc_timet (const ECalComponentDateTime *dt_time,
                            ECalRecurResolveTimezoneCb tz_cb,
                            gpointer tz_cb_data,
                            const ICalTimezone *default_zone)
{
	ICalTime *value = NULL;
	time_t timet = -1;

	if (dt_time)
		value = e_cal_component_datetime_get_value (dt_time);

	if (value) {
		ICalTimezone *zone = NULL;
		const gchar *tzid = e_cal_component_datetime_get_tzid (dt_time);

		if (tzid)
			zone = tz_cb (tzid, tz_cb_data, NULL, NULL);

		timet = i_cal_time_as_timet_with_zone (value, zone ? zone : (ICalTimezone *) default_zone);
	}

	return timet;
}

/**
 * e_cal_util_get_component_occur_times:
 * @comp: an #ECalComponent
 * @out_start: (out): Location to store the start time
 * @out_end: (out): Location to store the end time
 * @tz_cb: (closure tz_cb_data) (scope call): The #ECalRecurResolveTimezoneCb to call
 * @tz_cb_data: (closure): User data to be passed to the @tz_cb callback
 * @default_timezone: The default timezone
 * @kind: the type of component, indicated with an #ICalComponentKind
 *
 * Find out when the component starts and stops, being careful about
 * recurrences.
 *
 * Since: 2.32
 **/
void
e_cal_util_get_component_occur_times (ECalComponent *comp,
                                      time_t *out_start,
                                      time_t *out_end,
                                      ECalRecurResolveTimezoneCb tz_cb,
                                      gpointer tz_cb_data,
                                      const ICalTimezone *default_timezone,
                                      ICalComponentKind kind)
{
	ICalTimezone *utc_zone;
	ECalComponentDateTime *dtstart, *dtend;
	time_t duration;

	g_return_if_fail (comp != NULL);
	g_return_if_fail (out_start != NULL);
	g_return_if_fail (out_end != NULL);

	utc_zone = i_cal_timezone_get_utc_timezone ();
	e_cal_recur_ensure_end_dates (comp, FALSE, tz_cb, tz_cb_data, NULL, NULL);

	/* Get dtstart of the component and convert it to UTC */
	dtstart = e_cal_component_get_dtstart (comp);

	if ((*out_start = componenttime_to_utc_timet (dtstart, tz_cb, tz_cb_data, default_timezone)) == -1)
		*out_start = _TIME_MIN;

	e_cal_component_datetime_free (dtstart);

	dtend = e_cal_component_get_dtend (comp);
	duration = componenttime_to_utc_timet (dtend, tz_cb, tz_cb_data, default_timezone);
	if (duration <= 0 || *out_start == _TIME_MIN || *out_start > duration)
		duration = 0;
	else
		duration = duration - *out_start;
	e_cal_component_datetime_free (dtend);

	/* find out end date of component */
	*out_end = _TIME_MAX;

	if (kind == I_CAL_VTODO_COMPONENT) {
		/* max from COMPLETED and DUE properties */
		ICalTime *tt;
		time_t completed_time = -1, due_time = -1, max_time;
		ECalComponentDateTime *dtdue;

		tt = e_cal_component_get_completed (comp);
		if (tt) {
			/* COMPLETED must be in UTC. */
			completed_time = i_cal_time_as_timet_with_zone (tt, utc_zone);
			g_object_unref (tt);
		}

		dtdue = e_cal_component_get_due (comp);
		if (dtdue)
			due_time = componenttime_to_utc_timet (dtdue, tz_cb, tz_cb_data, default_timezone);

		e_cal_component_datetime_free (dtdue);

		max_time = MAX (completed_time, due_time);

		if (max_time != -1)
			*out_end = max_time;

	} else {
		/* ALARMS, EVENTS: DTEND and reccurences */

		time_t may_end = _TIME_MIN;

		if (e_cal_component_has_recurrences (comp)) {
			GSList *rrules = NULL;
			GSList *exrules = NULL;
			GSList *rdates = NULL;
			GSList *elem;

			/* Do the RRULEs, EXRULEs and RDATEs*/
			rrules = e_cal_component_get_rrule_properties (comp);
			exrules = e_cal_component_get_exrule_properties (comp);
			rdates = e_cal_component_get_rdates (comp);

			for (elem = rrules; elem; elem = g_slist_next (elem)) {
				ICalProperty *prop = elem->data;
				ICalRecurrence *ir;
				time_t rule_end;

				ir = i_cal_property_get_rrule (prop);
				rule_end = e_cal_recur_obtain_enddate (ir, prop, utc_zone, TRUE);

				if (rule_end == -1) /* repeats forever */
					may_end = _TIME_MAX;
				else if (rule_end + duration > may_end) /* new maximum */
					may_end = rule_end + duration;

				g_clear_object (&ir);
			}

			/* Do the EXRULEs. */
			for (elem = exrules; elem; elem = g_slist_next (elem)) {
				ICalProperty *prop = elem->data;
				ICalRecurrence *ir;
				time_t rule_end;

				ir = i_cal_property_get_exrule (prop);

				rule_end = e_cal_recur_obtain_enddate (ir, prop, utc_zone, TRUE);

				if (rule_end == -1) /* repeats forever */
					may_end = _TIME_MAX;
				else if (rule_end + duration > may_end)
					may_end = rule_end + duration;

				g_clear_object (&ir);
			}

			/* Do the RDATEs */
			for (elem = rdates; elem; elem = g_slist_next (elem)) {
				const ECalComponentPeriod *period = elem->data;
				time_t rdate_end = _TIME_MAX;

				/* FIXME: We currently assume RDATEs are in the same timezone
				 * as DTSTART. We should get the RDATE timezone and convert
				 * to the DTSTART timezone first. */

				if (e_cal_component_period_get_kind (period) != E_CAL_COMPONENT_PERIOD_DATETIME) {
					ICalTime *tt;
					tt = i_cal_time_add (e_cal_component_period_get_start (period), e_cal_component_period_get_duration (period));
					rdate_end = i_cal_time_as_timet (tt);
					g_object_unref (tt);
				} else if (e_cal_component_period_get_end (period)) {
					rdate_end = i_cal_time_as_timet (e_cal_component_period_get_end (period));
				} else {
					rdate_end = (time_t) -1;
				}

				if (rdate_end == -1) /* repeats forever */
					may_end = _TIME_MAX;
				else if (rdate_end > may_end)
					may_end = rdate_end;
			}

			g_slist_free_full (rrules, g_object_unref);
			g_slist_free_full (exrules, g_object_unref);
			g_slist_free_full (rdates, e_cal_component_period_free);
		} else if (*out_start != _TIME_MIN) {
			may_end = *out_start;
		}

		/* Get dtend of the component and convert it to UTC */
		dtend = e_cal_component_get_dtend (comp);

		if (dtend) {
			time_t dtend_time;

			dtend_time = componenttime_to_utc_timet (dtend, tz_cb, tz_cb_data, default_timezone);

			if (dtend_time == -1 || (dtend_time > may_end))
				may_end = dtend_time;
		} else {
			may_end = _TIME_MAX;
		}

		e_cal_component_datetime_free (dtend);

		*out_end = may_end == _TIME_MIN ? _TIME_MAX : may_end;
	}
}

/**
 * e_cal_util_component_has_x_property:
 * @icalcomp: an #ICalComponent
 * @x_name: name of the X property
 *
 * Returns, whether the @icalcomp contains X property named @x_name. To check
 * for standard property use e_cal_util_component_has_property().
 *
 * Returns: whether the @icalcomp contains X property named @x_name
 *
 * Since: 3.34
 **/
gboolean
e_cal_util_component_has_x_property (ICalComponent *icalcomp,
				     const gchar *x_name)
{
	ICalProperty *prop;

	g_return_val_if_fail (I_CAL_IS_COMPONENT (icalcomp), FALSE);
	g_return_val_if_fail (x_name != NULL, FALSE);

	prop = e_cal_util_component_find_x_property (icalcomp, x_name);

	if (!prop)
		return FALSE;

	g_object_unref (prop);

	return TRUE;
}


/**
 * e_cal_util_component_find_x_property:
 * @icalcomp: an #ICalComponent
 * @x_name: name of the X property
 *
 * Searches for an X property named @x_name within X properties
 * of @icalcomp and returns it. Free the non-NULL object
 * with g_object_unref(), when no longer needed.
 *
 * Returns: (transfer full) (nullable): the first X ICalProperty named
 *    @x_name, or %NULL, when none found.
 *
 * Since: 3.34
 **/
ICalProperty *
e_cal_util_component_find_x_property (ICalComponent *icalcomp,
				      const gchar *x_name)
{
	ICalProperty *prop;

	g_return_val_if_fail (icalcomp != NULL, NULL);
	g_return_val_if_fail (x_name != NULL, NULL);

	for (prop = i_cal_component_get_first_property (icalcomp, I_CAL_X_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icalcomp, I_CAL_X_PROPERTY)) {
		const gchar *prop_name = i_cal_property_get_x_name (prop);

		if (g_strcmp0 (prop_name, x_name) == 0)
			break;
	}

	return prop;
}

/**
 * e_cal_util_component_dup_x_property:
 * @icalcomp: an #ICalComponent
 * @x_name: name of the X property
 *
 * Searches for an X property named @x_name within X properties
 * of @icalcomp and returns its value as a newly allocated string.
 * Free it with g_free(), when no longer needed.
 *
 * Returns: (nullable) (transfer full): Newly allocated value of the first @x_name
 *    X property in @icalcomp, or %NULL, if not found.
 *
 * Since: 3.34
 **/
gchar *
e_cal_util_component_dup_x_property (ICalComponent *icalcomp,
				     const gchar *x_name)
{
	ICalProperty *prop;
	gchar *x_value;

	g_return_val_if_fail (icalcomp != NULL, NULL);
	g_return_val_if_fail (x_name != NULL, NULL);

	prop = e_cal_util_component_find_x_property (icalcomp, x_name);

	if (!prop)
		return NULL;

	x_value = i_cal_property_get_value_as_string (prop);

	g_object_unref (prop);

	return x_value;
}

/**
 * e_cal_util_component_set_x_property:
 * @icalcomp: an #ICalComponent
 * @x_name: name of the X property
 * @value: (nullable): a value to set, or %NULL
 *
 * Sets a value of the first X property named @x_name in @icalcomp,
 * if any such already exists, or adds a new property with this name
 * and value. As a special case, if @value is %NULL, then removes
 * the first X property named @x_name from @icalcomp instead.
 *
 * Since: 3.34
 **/
void
e_cal_util_component_set_x_property (ICalComponent *icalcomp,
				     const gchar *x_name,
				     const gchar *value)
{
	ICalProperty *prop;

	g_return_if_fail (icalcomp != NULL);
	g_return_if_fail (x_name != NULL);

	if (!value) {
		e_cal_util_component_remove_x_property (icalcomp, x_name);
		return;
	}

	prop = e_cal_util_component_find_x_property (icalcomp, x_name);
	if (prop) {
		i_cal_property_set_value_from_string (prop, value, "NO");
		g_object_unref (prop);
	} else {
		prop = i_cal_property_new_x (value);
		i_cal_property_set_x_name (prop, x_name);
		i_cal_component_take_property (icalcomp, prop);
	}
}

/**
 * e_cal_util_component_remove_x_property:
 * @icalcomp: an #ICalComponent
 * @x_name: name of the X property
 *
 * Removes the first X property named @x_name in @icalcomp.
 *
 * Returns: %TRUE, when any such had been found and removed, %FALSE otherwise.
 *
 * Since: 3.34
 **/
gboolean
e_cal_util_component_remove_x_property (ICalComponent *icalcomp,
					const gchar *x_name)
{
	ICalProperty *prop;

	g_return_val_if_fail (icalcomp != NULL, FALSE);
	g_return_val_if_fail (x_name != NULL, FALSE);

	prop = e_cal_util_component_find_x_property (icalcomp, x_name);
	if (!prop)
		return FALSE;

	i_cal_component_remove_property (icalcomp, prop);
	g_object_unref (prop);

	return TRUE;
}

/**
 * e_cal_util_component_remove_property_by_kind:
 * @icalcomp: an #ICalComponent
 * @kind: the kind of the property to remove
 * @all: %TRUE to remove all, or %FALSE to remove only the first property of the @kind
 *
 * Removes all or only the first property of kind @kind in @icalcomp.
 *
 * Returns: How many properties had been removed.
 *
 * Since: 3.30
 **/
guint
e_cal_util_component_remove_property_by_kind (ICalComponent *icalcomp,
					      ICalPropertyKind kind,
					      gboolean all)
{
	ICalProperty *prop;
	guint count = 0;

	g_return_val_if_fail (icalcomp != NULL, 0);

	while (prop = i_cal_component_get_first_property (icalcomp, kind), prop) {
		i_cal_component_remove_property (icalcomp, prop);
		g_object_unref (prop);

		count++;

		if (!all)
			break;
	}

	return count;
}

typedef struct _NextOccurrenceData {
	ICalTime *interval_start;
	ICalTime *next;
	gboolean found_next;
	gboolean any_hit;
} NextOccurrenceData;

static gboolean
ecu_find_next_occurrence_cb (ICalComponent *comp,
			     ICalTime *instance_start,
			     ICalTime *instance_end,
			     gpointer user_data,
			     GCancellable *cancellable,
			     GError **error)
{
	NextOccurrenceData *nod = user_data;

	g_return_val_if_fail (nod != NULL, FALSE);

	nod->any_hit = TRUE;

	if (i_cal_time_compare (nod->interval_start, instance_start) < 0) {
		g_clear_object (&nod->next);
		nod->next = g_object_ref (instance_start);
		nod->found_next = TRUE;
		return FALSE;
	}

	return TRUE;
}

/* the returned FALSE means failure in timezone resolution, not in @out_time */
static gboolean
e_cal_util_find_next_occurrence (ICalComponent *vtodo,
				 ICalTime *for_time,
				 ICalTime **out_time, /* set to NULL on failure */
				 ECalClient *cal_client,
				 GCancellable *cancellable,
				 GError **error)
{
	NextOccurrenceData nod;
	ICalTime *interval_start, *interval_end = NULL, *orig_dtstart, *orig_due;
	gint advance_days = 8;
	ICalProperty *prop;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (vtodo != NULL, FALSE);
	g_return_val_if_fail (out_time != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_CLIENT (cal_client), FALSE);

	orig_dtstart = i_cal_component_get_dtstart (vtodo);
	orig_due = i_cal_component_get_due (vtodo);

	e_cal_util_component_remove_property_by_kind (vtodo, I_CAL_DUE_PROPERTY, TRUE);

	if (for_time && !i_cal_time_is_null_time (for_time) && i_cal_time_is_valid_time (for_time)) {
		i_cal_component_set_dtstart (vtodo, for_time);
	}

	interval_start = i_cal_component_get_dtstart (vtodo);
	if (!interval_start || i_cal_time_is_null_time (interval_start) || !i_cal_time_is_valid_time (interval_start)) {
		g_clear_object (&interval_start);
		interval_start = i_cal_time_new_current_with_zone (e_cal_client_get_default_timezone (cal_client));
	}

	prop = i_cal_component_get_first_property (vtodo, I_CAL_RRULE_PROPERTY);
	if (prop) {
		ICalRecurrence *rrule;

		rrule = i_cal_property_get_rrule (prop);

		if (rrule) {
			if (i_cal_recurrence_get_freq (rrule) == I_CAL_WEEKLY_RECURRENCE && i_cal_recurrence_get_interval (rrule) > 1)
				advance_days = (i_cal_recurrence_get_interval (rrule) * 7) + 1;
			else if (i_cal_recurrence_get_freq (rrule) == I_CAL_MONTHLY_RECURRENCE)
				advance_days = (i_cal_recurrence_get_interval (rrule) >= 1 ? i_cal_recurrence_get_interval (rrule) * 31 : 31) + 1;
			else if (i_cal_recurrence_get_freq (rrule) == I_CAL_YEARLY_RECURRENCE)
				advance_days = (i_cal_recurrence_get_interval (rrule) >= 1 ? i_cal_recurrence_get_interval (rrule) * 365 : 365) + 2;
		}

		g_clear_object (&rrule);
		g_clear_object (&prop);
	}

	nod.next = NULL;

	do {
		interval_end = i_cal_time_clone (interval_start);
		i_cal_time_adjust (interval_end, advance_days, 0, 0, 0);

		g_clear_object (&(nod.next));

		nod.interval_start = interval_start;
		nod.next = i_cal_time_new_null_time ();
		nod.found_next = FALSE;
		nod.any_hit = FALSE;

		success = e_cal_recur_generate_instances_sync (vtodo, interval_start, interval_end,
			ecu_find_next_occurrence_cb, &nod,
			e_cal_client_tzlookup_cb, cal_client,
			e_cal_client_get_default_timezone (cal_client),
			cancellable, &local_error) || nod.found_next;

		g_object_unref (interval_start);
		interval_start = interval_end;
		interval_end = NULL;
		i_cal_time_adjust (interval_start, -1, 0, 0, 0);

	} while (!local_error && !g_cancellable_is_cancelled (cancellable) && !nod.found_next && nod.any_hit);

	if (success)
		*out_time = (nod.next && !i_cal_time_is_null_time (nod.next)) ? g_object_ref (nod.next) : NULL;

	if (local_error)
		g_propagate_error (error, local_error);

	if (for_time && !i_cal_time_is_null_time (for_time) && i_cal_time_is_valid_time (for_time)) {
		if (!orig_dtstart || i_cal_time_is_null_time (orig_dtstart) || !i_cal_time_is_valid_time (orig_dtstart))
			e_cal_util_component_remove_property_by_kind (vtodo, I_CAL_DTSTART_PROPERTY, FALSE);
		else
			i_cal_component_set_dtstart (vtodo, orig_dtstart);
	}

	if (!orig_due || i_cal_time_is_null_time (orig_due) || !i_cal_time_is_valid_time (orig_due))
		e_cal_util_component_remove_property_by_kind (vtodo, I_CAL_DUE_PROPERTY, FALSE);
	else
		i_cal_component_set_due (vtodo, orig_due);

	g_clear_object (&interval_start);
	g_clear_object (&interval_end);
	g_clear_object (&orig_dtstart);
	g_clear_object (&orig_due);
	g_clear_object (&(nod.next));

	return success;
}

/**
 * e_cal_util_init_recur_task_sync:
 * @vtodo: a VTODO component
 * @cal_client: an #ECalClient to which the @vtodo belongs
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Initializes properties of a recurring @vtodo, like normalizing
 * the Due date and eventually the Start date. The function does
 * nothing when the @vtodo is not recurring.
 *
 * The function doesn't change LAST-MODIFIED neither the SEQUENCE
 * property, it's up to the caller to do it.
 *
 * Note the @cal_client, @cancellable and @error is used only
 * for timezone resolution. The function doesn't store the @vtodo
 * to the @cal_client, it only updates the @vtodo component.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.30
 **/
gboolean
e_cal_util_init_recur_task_sync (ICalComponent *vtodo,
				 ECalClient *cal_client,
				 GCancellable *cancellable,
				 GError **error)
{
	ICalTime *dtstart, *due;
	gboolean success = TRUE;

	g_return_val_if_fail (vtodo != NULL, FALSE);
	g_return_val_if_fail (i_cal_component_isa (vtodo) == I_CAL_VTODO_COMPONENT, FALSE);
	g_return_val_if_fail (E_IS_CAL_CLIENT (cal_client), FALSE);

	if (!e_cal_util_component_has_recurrences (vtodo))
		return TRUE;

	/* DTSTART is required for recurring components */
	dtstart = i_cal_component_get_dtstart (vtodo);
	if (!dtstart || i_cal_time_is_null_time (dtstart) || !i_cal_time_is_valid_time (dtstart)) {
		g_clear_object (&dtstart);

		dtstart = i_cal_time_new_current_with_zone (e_cal_client_get_default_timezone (cal_client));
		i_cal_component_set_dtstart (vtodo, dtstart);
	}

	due = i_cal_component_get_due (vtodo);
	if (!due || i_cal_time_is_null_time (due) || !i_cal_time_is_valid_time (due) ||
	    i_cal_time_compare (dtstart, due) < 0) {
		g_clear_object (&due);

		success = e_cal_util_find_next_occurrence (vtodo, NULL, &due, cal_client, cancellable, error);

		if (due && !i_cal_time_is_null_time (due) && i_cal_time_is_valid_time (due))
			i_cal_component_set_due (vtodo, due);
	}

	g_clear_object (&dtstart);
	g_clear_object (&due);

	return success;
}

/**
 * e_cal_util_mark_task_complete_sync:
 * @vtodo: a VTODO component
 * @completed_time: completed time to set, or (time_t) -1 to use current time
 * @cal_client: an #ECalClient to which the @vtodo belongs
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Marks the @vtodo as complete with eventual update of other
 * properties. This is useful also for recurring tasks, for which
 * it moves the @vtodo into the next occurrence according to
 * the recurrence rule.
 *
 * When the @vtodo is marked as completed, then the existing COMPLETED
 * date-time is preserved if exists, otherwise it's set either to @completed_time,
 * or to the current time, when the @completed_time is (time_t) -1.
 *
 * The function doesn't change LAST-MODIFIED neither the SEQUENCE
 * property, it's up to the caller to do it.
 *
 * Note the @cal_client, @cancellable and @error is used only
 * for timezone resolution. The function doesn't store the @vtodo
 * to the @cal_client, it only updates the @vtodo component.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.30
 **/
gboolean
e_cal_util_mark_task_complete_sync (ICalComponent *vtodo,
				    time_t completed_time,
				    ECalClient *cal_client,
				    GCancellable *cancellable,
				    GError **error)
{
	ICalProperty *prop;

	g_return_val_if_fail (vtodo != NULL, FALSE);
	g_return_val_if_fail (i_cal_component_isa (vtodo) == I_CAL_VTODO_COMPONENT, FALSE);
	g_return_val_if_fail (E_IS_CAL_CLIENT (cal_client), FALSE);

	if (e_cal_util_component_has_recurrences (vtodo) &&
	    !e_client_check_capability (E_CLIENT (cal_client), E_CAL_STATIC_CAPABILITY_TASK_HANDLE_RECUR)) {
		gboolean is_last = FALSE, change_count = FALSE;
		ICalTime *new_dtstart = NULL, *new_due = NULL;

		for (prop = i_cal_component_get_first_property (vtodo, I_CAL_RRULE_PROPERTY);
		     prop && !is_last;
		     g_object_unref (prop), prop = i_cal_component_get_next_property (vtodo, I_CAL_RRULE_PROPERTY)) {
			ICalRecurrence *rrule;

			rrule = i_cal_property_get_rrule (prop);

			if (rrule && i_cal_recurrence_get_interval (rrule) > 0) {
				gint count = i_cal_recurrence_get_count (rrule);

				if (count > 0) {
					is_last = count == 1;
					change_count = TRUE;
				}
			}

			g_clear_object (&rrule);
		}

		g_clear_object (&prop);

		if (!is_last) {
			if (!e_cal_util_find_next_occurrence (vtodo, NULL, &new_dtstart, cal_client, cancellable, error)) {
				g_clear_object (&new_dtstart);
				return FALSE;
			}

			if (new_dtstart && !i_cal_time_is_null_time (new_dtstart) && i_cal_time_is_valid_time (new_dtstart)) {
				ICalTime *old_due;

				old_due = i_cal_component_get_due (vtodo);

				/* When the previous DUE is before new DTSTART, then move relatively also the DUE
				   date, to keep the difference... */
				if (old_due && !i_cal_time_is_null_time (old_due) && i_cal_time_is_valid_time (old_due) &&
				    i_cal_time_compare (old_due, new_dtstart) < 0) {
					if (!e_cal_util_find_next_occurrence (vtodo, old_due, &new_due, cal_client, cancellable, error)) {
						g_clear_object (&new_dtstart);
						g_clear_object (&new_due);
						g_clear_object (&old_due);
						return FALSE;
					}
				}

				/* ...  otherwise set the new DUE as the next-next-DTSTART ... */
				if (!new_due || i_cal_time_is_null_time (new_due) || !i_cal_time_is_valid_time (new_due)) {
					g_clear_object (&new_due);

					if (!e_cal_util_find_next_occurrence (vtodo, new_dtstart, &new_due, cal_client, cancellable, error)) {
						g_clear_object (&new_dtstart);
						g_clear_object (&new_due);
						g_clear_object (&old_due);
						return FALSE;
					}
				}

				/* ... eventually fallback to the new DTSTART for the new DUE */
				if (!new_due || i_cal_time_is_null_time (new_due) || !i_cal_time_is_valid_time (new_due)) {
					g_clear_object (&new_due);
					new_due = i_cal_time_clone (new_dtstart);
				}

				g_clear_object (&old_due);
			}
		}

		if (!is_last && new_dtstart && new_due &&
		    !i_cal_time_is_null_time (new_dtstart) && i_cal_time_is_valid_time (new_dtstart) &&
		    !i_cal_time_is_null_time (new_due) && i_cal_time_is_valid_time (new_due)) {
			/* Move to the next occurrence */
			if (change_count) {
				for (prop = i_cal_component_get_first_property (vtodo, I_CAL_RRULE_PROPERTY);
				     prop;
				     g_object_unref (prop), prop = i_cal_component_get_next_property (vtodo, I_CAL_RRULE_PROPERTY)) {
					ICalRecurrence *rrule;

					rrule = i_cal_property_get_rrule (prop);

					if (rrule && i_cal_recurrence_get_interval (rrule) > 0) {
						gint count = i_cal_recurrence_get_count (rrule);

						if (count > 0) {
							i_cal_recurrence_set_count (rrule, count - 1);
							i_cal_property_set_rrule (prop, rrule);
						}
					}

					g_clear_object (&rrule);
				}
			}

			i_cal_component_set_dtstart (vtodo, new_dtstart);
			i_cal_component_set_due (vtodo, new_due);

			e_cal_util_component_remove_property_by_kind (vtodo, I_CAL_COMPLETED_PROPERTY, TRUE);

			prop = i_cal_component_get_first_property (vtodo, I_CAL_PERCENTCOMPLETE_PROPERTY);
			if (prop) {
				i_cal_property_set_percentcomplete (prop, 0);
				g_object_unref (prop);
			}

			prop = i_cal_component_get_first_property (vtodo, I_CAL_STATUS_PROPERTY);
			if (prop) {
				i_cal_property_set_status (prop, I_CAL_STATUS_NEEDSACTION);
				g_object_unref (prop);
			}

			g_clear_object (&new_dtstart);
			g_clear_object (&new_due);

			return TRUE;
		}
	}

	prop = i_cal_component_get_first_property (vtodo, I_CAL_COMPLETED_PROPERTY);
	if (prop) {
		g_object_unref (prop);
	} else {
		ICalTime *tt;

		tt = completed_time != (time_t) -1 ?
			i_cal_time_new_from_timet_with_zone (completed_time, FALSE, i_cal_timezone_get_utc_timezone ()) :
			i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());
		prop = i_cal_property_new_completed (tt);
		i_cal_component_take_property (vtodo, prop);
		g_object_unref (tt);
	}

	prop = i_cal_component_get_first_property (vtodo, I_CAL_PERCENTCOMPLETE_PROPERTY);
	if (prop) {
		i_cal_property_set_percentcomplete (prop, 100);
		g_object_unref (prop);
	} else {
		prop = i_cal_property_new_percentcomplete (100);
		i_cal_component_take_property (vtodo, prop);
	}

	prop = i_cal_component_get_first_property (vtodo, I_CAL_STATUS_PROPERTY);
	if (prop) {
		i_cal_property_set_status (prop, I_CAL_STATUS_COMPLETED);
		g_object_unref (prop);
	} else {
		prop = i_cal_property_new_status (I_CAL_STATUS_COMPLETED);
		i_cal_component_take_property (vtodo, prop);
	}

	return TRUE;
}

/**
 * e_cal_util_operation_flags_to_conflict_resolution:
 * @flags: bit-or of #ECalOperationFlags
 *
 * Decodes the #EConflictResolution from the bit-or of #ECalOperationFlags.
 *
 * Returns: an #EConflictResolution as stored in the @flags
 *
 * Since: 3.34
 **/
EConflictResolution
e_cal_util_operation_flags_to_conflict_resolution (guint32 flags)
{
	if ((flags & E_CAL_OPERATION_FLAG_CONFLICT_FAIL) != 0)
		return E_CONFLICT_RESOLUTION_FAIL;
	else if ((flags & E_CAL_OPERATION_FLAG_CONFLICT_USE_NEWER) != 0)
		return E_CONFLICT_RESOLUTION_USE_NEWER;
	else if ((flags & E_CAL_OPERATION_FLAG_CONFLICT_KEEP_SERVER) != 0)
		return E_CONFLICT_RESOLUTION_KEEP_SERVER;
	else if ((flags & E_CAL_OPERATION_FLAG_CONFLICT_WRITE_COPY) != 0)
		return E_CONFLICT_RESOLUTION_WRITE_COPY;

	/* E_CAL_OPERATION_FLAG_CONFLICT_KEEP_LOCAL is the default */
	return E_CONFLICT_RESOLUTION_KEEP_LOCAL;
}

/**
 * e_cal_util_conflict_resolution_to_operation_flags:
 * @conflict_resolution: an #EConflictResolution
 *
 * Encodes the #EConflictResolution into the bit-or of #ECalOperationFlags.
 * The returned value can be bit-or-ed with other #ECalOperationFlags values.
 *
 * Returns: a bit-or of #ECalOperationFlags, corresponding to the @conflict_resolution
 *
 * Since: 3.34
 **/
guint32
e_cal_util_conflict_resolution_to_operation_flags (EConflictResolution conflict_resolution)
{
	switch (conflict_resolution) {
	case E_CONFLICT_RESOLUTION_FAIL:
		return E_CAL_OPERATION_FLAG_CONFLICT_FAIL;
	case E_CONFLICT_RESOLUTION_USE_NEWER:
		return E_CAL_OPERATION_FLAG_CONFLICT_USE_NEWER;
	case E_CONFLICT_RESOLUTION_KEEP_SERVER:
		return E_CAL_OPERATION_FLAG_CONFLICT_KEEP_SERVER;
	case E_CONFLICT_RESOLUTION_KEEP_LOCAL:
		return E_CAL_OPERATION_FLAG_CONFLICT_KEEP_LOCAL;
	case E_CONFLICT_RESOLUTION_WRITE_COPY:
		return E_CAL_OPERATION_FLAG_CONFLICT_WRITE_COPY;
	}

	return E_CAL_OPERATION_FLAG_CONFLICT_KEEP_LOCAL;
}

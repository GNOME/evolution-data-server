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

	if (comp_str)
		g_string_free (comp_str, TRUE);

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

	if (*alarm_start < 0)
		*alarm_start = 0;
	if (*alarm_end < 0)
		*alarm_end = 0;

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
	gint def_reminder_before_start_seconds;
	gboolean only_check;
	gboolean any_exists;

	/* This is what we compute */
	GSList *triggers; /* ECalComponentAlarmInstance * */
};

static void
add_trigger (struct alarm_occurrence_data *aod,
	     ECalComponent *instance_comp,
             const gchar *auid,
	     const gchar *rid,
             time_t instance_time,
             time_t occur_start,
             time_t occur_end)
{
	ECalComponentAlarmInstance *instance;

	aod->any_exists = TRUE;

	if (aod->only_check)
		return;

	instance = e_cal_component_alarm_instance_new (auid, instance_time, occur_start, occur_end);

	if (rid && *rid)
		e_cal_component_alarm_instance_set_rid (instance, rid);

	e_cal_component_alarm_instance_set_component (instance, instance_comp);

	aod->triggers = g_slist_prepend (aod->triggers, instance);
}

static void
e_cal_util_add_alarm_before_start (ECalComponent *comp,
				   gint before_start_seconds)
{
	ECalComponentAlarm *alarm;
	ECalComponentAlarmTrigger *trigger;
	ECalComponentText *summary;
	ICalDuration *duration;
	GSList *alarms, *link;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (before_start_seconds >= 0);

	e_cal_component_remove_alarm (comp, "x-evolution-default-alarm");

	alarms = e_cal_component_get_all_alarms	(comp);
	for (link = alarms; link; link = g_slist_next (link)) {
		alarm = link->data;

		if (e_cal_component_alarm_get_action (alarm) != E_CAL_COMPONENT_ALARM_DISPLAY)
			continue;

		trigger = e_cal_component_alarm_get_trigger (alarm);
		if (!trigger ||
		    e_cal_component_alarm_trigger_get_kind (trigger) != E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START)
			continue;

		duration = e_cal_component_alarm_trigger_get_duration (trigger);
		if (!duration || !i_cal_duration_is_neg (duration))
			continue;

		if (i_cal_duration_as_int (duration) == (-1) * before_start_seconds)
			break;
	}

	g_slist_free_full (alarms, e_cal_component_alarm_free);

	/* Found existing alarm at the same time, skip this one */
	if (link != NULL)
		return;

	alarm = e_cal_component_alarm_new ();
	e_cal_component_alarm_set_uid (alarm, "x-evolution-default-alarm");
	summary = e_cal_component_get_summary (comp);
	e_cal_component_alarm_take_description (alarm, summary);
	e_cal_component_alarm_set_action (alarm, E_CAL_COMPONENT_ALARM_DISPLAY);

	duration = i_cal_duration_new_from_int (before_start_seconds);
	i_cal_duration_set_is_neg (duration, TRUE);

	trigger = e_cal_component_alarm_trigger_new_relative (E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START, duration);

	g_object_unref (duration);

	e_cal_component_alarm_take_trigger (alarm, trigger);
	e_cal_component_add_alarm (comp, alarm);
	e_cal_component_alarm_free (alarm);
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
	struct alarm_occurrence_data *aod = user_data;
	ECalComponent *comp;
	time_t start, end;
	GSList *link;
	gchar *rid;

	if (aod->comp) {
		comp = g_object_ref (aod->comp);
	} else {
		comp = e_cal_component_new_from_icalcomponent (i_cal_component_clone (icalcomp));
		if (aod->def_reminder_before_start_seconds >= 0 && comp)
			e_cal_util_add_alarm_before_start (comp, aod->def_reminder_before_start_seconds);
	}

	g_return_val_if_fail (comp != NULL, FALSE);

	start = i_cal_time_as_timet_with_zone (instance_start, i_cal_time_get_timezone (instance_start));
	end = i_cal_time_as_timet_with_zone (instance_end, i_cal_time_get_timezone (instance_end));
	rid = e_cal_util_component_get_recurid_as_string (icalcomp);

	if (!rid || !*rid) {
		g_clear_pointer (&rid, g_free);
		rid = i_cal_time_as_ical_string (instance_start);
	}

	for (link = aod->alarm_uids; link && (!aod->only_check || !aod->any_exists); link = g_slist_next (link)) {
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
		alarm = e_cal_component_get_alarm (comp, auid);
		if (!alarm)
			continue;

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
					add_trigger (aod, comp, auid, rid, t, start, end);
			}
		}

		/* Add the trigger itself */

		if (trigger_time >= aod->start && trigger_time < aod->end)
			add_trigger (aod, comp, auid, rid, trigger_time, start, end);

		e_cal_component_alarm_free (alarm);
	}

	g_clear_object (&comp);
	g_free (rid);

	return !aod->only_check || !aod->any_exists;
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
	gchar *rid;

	dtstart = e_cal_component_get_dtstart (comp);
	dtend = e_cal_component_get_dtend (comp);
	rid = e_cal_component_get_recurid_as_string (comp);

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
	} else {
		e_cal_component_datetime_free (dtend);
		dtend = e_cal_component_get_due (comp);

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
	}

	for (link = aod->alarm_uids; link && (!aod->only_check || !aod->any_exists); link = g_slist_next (link)) {
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
		if (!alarm)
			continue;

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
					add_trigger (aod, comp, auid, rid, tt, occur_start, occur_end);
			}
		}

		/* Add the trigger itself */

		if (abs_time >= aod->start && abs_time < aod->end)
			add_trigger (aod, comp, auid, rid, abs_time, occur_start, occur_end);

		e_cal_component_alarm_free (alarm);
	}

	e_cal_component_datetime_free (dtstart);
	e_cal_component_datetime_free (dtend);
	g_free (rid);
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

static void
ecu_generate_alarms_for_comp (ECalComponent *comp,
			      time_t start,
			      time_t end,
			      ECalComponentAlarmAction *omit,
			      ECalRecurResolveTimezoneCb resolve_tzid,
			      gpointer user_data,
			      ICalTimezone *default_timezone,
			      gboolean *out_any_exists,
			      ECalComponentAlarms **out_alarms)
{
	GSList *alarm_uids;
	time_t alarm_start, alarm_end;
	struct alarm_occurrence_data aod;
	ICalTime *alarm_start_tt, *alarm_end_tt;

	if (out_any_exists)
		*out_any_exists = FALSE;

	if (out_alarms)
		*out_alarms = NULL;

	if (!e_cal_component_has_alarms (comp))
		return;

	alarm_uids = e_cal_component_get_alarm_uids (comp);
	compute_alarm_range (comp, alarm_uids, start, end, &alarm_start, &alarm_end);

	aod.comp = comp;
	aod.alarm_uids = alarm_uids;
	aod.start = start;
	aod.end = end;
	aod.omit = omit;
	aod.only_check = !out_alarms;
	aod.any_exists = FALSE;
	aod.triggers = NULL;

	alarm_start_tt = i_cal_time_new_from_timet_with_zone (alarm_start, FALSE, i_cal_timezone_get_utc_timezone ());
	alarm_end_tt = i_cal_time_new_from_timet_with_zone (alarm_end, FALSE, i_cal_timezone_get_utc_timezone ());

	e_cal_recur_generate_instances_sync (e_cal_component_get_icalcomponent (comp),
		alarm_start_tt, alarm_end_tt,
		add_alarm_occurrences_cb, &aod,
		resolve_tzid, user_data,
		default_timezone, NULL, NULL);

	g_clear_object (&alarm_start_tt);
	g_clear_object (&alarm_end_tt);

	if (!aod.only_check || !aod.any_exists) {
		/* We add the ABSOLUTE triggers separately */
		generate_absolute_triggers (comp, &aod, resolve_tzid, user_data, default_timezone);
	}

	g_slist_free_full (alarm_uids, g_free);

	if (out_any_exists)
		*out_any_exists = aod.any_exists;

	if (out_alarms && aod.triggers) {
		*out_alarms = e_cal_component_alarms_new (comp);
		e_cal_component_alarms_take_instances (*out_alarms, g_slist_sort (aod.triggers, compare_alarm_instance));
	} else {
		g_warn_if_fail (aod.triggers == NULL);
	}
}

/**
 * e_cal_util_generate_alarms_for_comp:
 * @comp: The #ECalComponent to generate alarms from
 * @start: Start time
 * @end: End time
 * @omit: Alarm types to omit
 * @resolve_tzid: (closure user_data) (scope call): Callback for resolving
 * timezones
 * @user_data: Data to be passed to the resolve_tzid callback
 * @default_timezone: The timezone used to resolve DATE and floating DATE-TIME
 * values.
 *
 * Generates alarm instances for a calendar component. Returns the instances
 * structure, or %NULL if no alarm instances occurred in the specified time
 * range. Free the returned structure with e_cal_component_alarms_free(),
 * when no longer needed.
 *
 * See e_cal_util_generate_alarms_for_uid_sync()
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
	ECalComponentAlarms *alarms = NULL;

	ecu_generate_alarms_for_comp (comp, start, end, omit, resolve_tzid, user_data, default_timezone, NULL, &alarms);

	return alarms;
}

/**
 * e_cal_util_has_alarms_in_range:
 * @comp: an #ECalComponent to check alarms for
 * @start: start time
 * @end: end time
 * @omit: alarm types to omit
 * @resolve_tzid: (closure user_data) (scope call): Callback for resolving timezones
 * @user_data: Data to be passed to the resolve_tzid callback
 * @default_timezone: The timezone used to resolve DATE and floating DATE-TIME values.
 *
 * Checks whether the @comp has any alarms in the given time interval.
 *
 * Returns: %TRUE, when the #comp has any alarms in the given time interval
 *
 * Since: 3.48
 **/
gboolean
e_cal_util_has_alarms_in_range (ECalComponent *comp,
				time_t start,
				time_t end,
				ECalComponentAlarmAction *omit,
				ECalRecurResolveTimezoneCb resolve_tzid,
				gpointer user_data,
				ICalTimezone *default_timezone)
{
	gboolean any_exists = FALSE;

	ecu_generate_alarms_for_comp (comp, start, end, omit, resolve_tzid, user_data, default_timezone, &any_exists, NULL);

	return any_exists;
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
 * @user_data: Data to be passed to the resolve_tzid callback
 * @default_timezone: The timezone used to resolve DATE and floating DATE-TIME
 * values.
 *
 * Iterates through all the components in the @comps list and generates alarm
 * instances for them; putting them in the @comp_alarms list. Free the @comp_alarms
 * with g_slist_free_full (comp_alarms, e_cal_component_alarms_free);, when
 * no longer neeed.
 *
 * See e_cal_util_generate_alarms_for_uid_sync()
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
 * e_cal_util_generate_alarms_for_uid_sync:
 * @client: an #ECalClient
 * @uid: a component UID to generate alarms for
 * @start: start time
 * @end: end time
 * @omit: alarm types to omit
 * @resolve_tzid: (closure user_data) (scope call): Callback for resolving timezones
 * @user_data: Data to be passed to the resolve_tzid callback
 * @default_timezone: The timezone used to resolve DATE and floating DATE-TIME values
 * @def_reminder_before_start_seconds: add default reminder before start in seconds, when not negative value
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Generates alarm instances for a calendar component with UID @uid,
 * which is stored within the @client. In contrast to e_cal_util_generate_alarms_for_comp(),
 * this function handles detached instances of recurring events properly.
 *
 * The @def_reminder_before_start_seconds, if not negative, causes addition of an alarm,
 * which will trigger a "display" alarm these seconds before start of the event.
 *
 * Returns the instances structure, or %NULL if no alarm instances occurred in the specified
 * time range. Free the returned structure with e_cal_component_alarms_free(),
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): a list of all the alarms found
 *    for the given component in the given time range.
 *
 * Since: 3.48
 **/
ECalComponentAlarms *
e_cal_util_generate_alarms_for_uid_sync (ECalClient *client,
					 const gchar *uid,
					 time_t start,
					 time_t end,
					 ECalComponentAlarmAction *omit,
					 ECalRecurResolveTimezoneCb resolve_tzid,
					 gpointer user_data,
					 ICalTimezone *default_timezone,
					 gint def_reminder_before_start_seconds,
					 GCancellable *cancellable,
					 GError **error)
{
	GHashTable *alarm_uids_hash;
	GSList *alarm_uids = NULL;
	GSList *objects = NULL, *link;
	time_t alarm_start = start, alarm_end = end;
	struct alarm_occurrence_data aod;
	ECalComponentAlarms *alarms;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	if (!e_cal_client_get_objects_for_uid_sync (client, uid, &objects, cancellable, error))
		return NULL;

	alarm_uids_hash = g_hash_table_new (g_str_hash, g_str_equal);

	for (link = objects; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;
		GSList *auids;

		if (def_reminder_before_start_seconds >= 0)
			e_cal_util_add_alarm_before_start (comp, def_reminder_before_start_seconds);

		auids = e_cal_component_get_alarm_uids (comp);

		if (auids) {
			GSList *alink;

			compute_alarm_range (comp, auids, alarm_start, alarm_end, &alarm_start, &alarm_end);

			for (alink = auids; alink; alink = g_slist_next (alink)) {
				const gchar *auid = alink->data;
				if (auid && !g_hash_table_contains (alarm_uids_hash, auid)) {
					alarm_uids = g_slist_prepend (alarm_uids, (gpointer) auid);
					g_hash_table_add (alarm_uids_hash, (gpointer) auid);
					alink->data = NULL;
				}
			}

			g_slist_free_full (auids, g_free);
		}
	}

	g_clear_pointer (&alarm_uids_hash, g_hash_table_destroy);

	aod.comp = NULL;
	aod.alarm_uids = alarm_uids;
	aod.start = start;
	aod.end = end;
	aod.omit = omit;
	aod.def_reminder_before_start_seconds = def_reminder_before_start_seconds;
	aod.only_check = FALSE;
	aod.any_exists = FALSE;
	aod.triggers = NULL;

	e_cal_client_generate_instances_for_uid_sync (client, uid, alarm_start, alarm_end,
		cancellable, add_alarm_occurrences_cb, &aod);

	for (link = objects; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;

		/* We add the ABSOLUTE triggers separately */
		generate_absolute_triggers (comp, &aod, resolve_tzid, user_data, default_timezone);
	}

	g_slist_free_full (objects, g_object_unref);
	g_slist_free_full (alarm_uids, g_free);

	if (!aod.triggers)
		return NULL;

	/* Create the component alarm instances structure */

	alarms = e_cal_component_alarms_new (NULL);
	e_cal_component_alarms_take_instances (alarms, g_slist_sort (aod.triggers, compare_alarm_instance));

	return alarms;
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

/**
 * e_cal_util_normalize_rrule_until_value:
 * @icalcomp: An #ICalComponent
 * @ttuntil: An UNTIL value to validate
 * @tz_cb: (closure tz_cb_data) (scope call): The #ECalRecurResolveTimezoneCb to call
 * @tz_cb_data: User data to be passed to the @tz_cb callback
 *
 * Makes sure the @ttuntil value matches the value type with
 * the DTSTART value, as required by RFC 5545 section 3.3.10.
 * Uses @tz_cb with @tz_cb_data to resolve time zones when needed.
 *
 * Since: 3.38
 **/
void
e_cal_util_normalize_rrule_until_value (ICalComponent *icalcomp,
					ICalTime *ttuntil,
					ECalRecurResolveTimezoneCb tz_cb,
					gpointer tz_cb_data)
{
	ICalProperty *prop;

	g_return_if_fail (I_CAL_IS_COMPONENT (icalcomp));
	g_return_if_fail (I_CAL_IS_TIME (ttuntil));

	prop = i_cal_component_get_first_property (icalcomp, I_CAL_DTSTART_PROPERTY);

	if (prop) {
		ICalTime *dtstart;

		dtstart = i_cal_component_get_dtstart (icalcomp);

		if (dtstart) {
			if (i_cal_time_is_date (dtstart)) {
				i_cal_time_set_time (ttuntil, 0, 0, 0);
				i_cal_time_set_is_date (ttuntil, TRUE);
			} else {
				if (i_cal_time_is_date (ttuntil)) {
					gint hour = 0, minute = 0, second = 0;

					i_cal_time_set_is_date (ttuntil, FALSE);

					i_cal_time_get_time (dtstart, &hour, &minute, &second);
					i_cal_time_set_time (ttuntil, hour, minute, second);
				}

				if (!i_cal_time_is_utc (dtstart)) {
					ICalParameter *param;

					param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);

					if (param) {
						const gchar *tzid;

						tzid = i_cal_parameter_get_tzid (param);

						if (tzid && *tzid && g_ascii_strcasecmp (tzid, "UTC") != 0) {
							ICalTimezone *tz;

							tz = i_cal_time_get_timezone (dtstart);

							if (!tz && tz_cb)
								tz = tz_cb (tzid, tz_cb_data, NULL, NULL);

							if (tz) {
								if (!i_cal_time_get_timezone (ttuntil))
									i_cal_time_set_timezone (ttuntil, tz);
								i_cal_time_convert_to_zone_inplace (ttuntil, i_cal_timezone_get_utc_timezone ());
							}
						}

						g_object_unref (param);
					}
				}
			}

			g_object_unref (dtstart);
		}

		g_object_unref (prop);
	}
}

static void
e_cal_util_remove_instances_impl (ICalComponent *icalcomp,
				  const ICalTime *rid,
				  ECalObjModType mod,
				  gboolean keep_rid,
				  gboolean can_add_exrule,
				  ECalRecurResolveTimezoneCb tz_cb,
				  gpointer tz_cb_data)
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
	if (mod == E_CAL_OBJ_MOD_THIS || mod == E_CAL_OBJ_MOD_ONLY_THIS) {
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
					gboolean is_date;

					if (keep_rid && i_cal_time_compare (recur, (ICalTime *) rid) == 0) {
						ICalDuration *dur;

						dur = i_cal_component_get_duration (icalcomp);
						ttuntil = i_cal_time_add ((ICalTime *) rid, dur);
						g_clear_object (&dur);
					} else {
						ttuntil = i_cal_time_clone (rid);
					}

					e_cal_util_normalize_rrule_until_value (icalcomp, ttuntil, tz_cb, tz_cb_data);

					is_date = i_cal_time_is_date (ttuntil);

					i_cal_time_adjust (ttuntil, is_date ? -1 : 0, 0, 0, is_date ? 0 : -1);

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

					e_cal_util_normalize_rrule_until_value (icalcomp, ttuntil, tz_cb, tz_cb_data);
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
 *
 * Deprecated: 3.38: Use e_cal_util_remove_instances_ex() instead, with provided
 *    timezone resolve function.
 **/
void
e_cal_util_remove_instances (ICalComponent *icalcomp,
                             const ICalTime *rid,
                             ECalObjModType mod)
{
	g_return_if_fail (icalcomp != NULL);
	g_return_if_fail (rid != NULL);
	g_return_if_fail (mod != E_CAL_OBJ_MOD_ALL);

	e_cal_util_remove_instances_ex (icalcomp, rid, mod, NULL, NULL);
}

/**
 * e_cal_util_remove_instances_ex:
 * @icalcomp: A (recurring) #ICalComponent
 * @rid: The base RECURRENCE-ID to remove
 * @mod: How to interpret @rid
 * @tz_cb: (closure tz_cb_data) (scope call): The #ECalRecurResolveTimezoneCb to call
 * @tz_cb_data: User data to be passed to the @tz_cb callback
 *
 * Removes one or more instances from @icalcomp according to @rid and @mod.
 * Uses @tz_cb with @tz_cb_data to resolve time zones when needed.
 *
 * Since: 3.38
 **/
void
e_cal_util_remove_instances_ex (ICalComponent *icalcomp,
				const ICalTime *rid,
				ECalObjModType mod,
				ECalRecurResolveTimezoneCb tz_cb,
				gpointer tz_cb_data)
{
	g_return_if_fail (icalcomp != NULL);
	g_return_if_fail (rid != NULL);
	g_return_if_fail (mod != E_CAL_OBJ_MOD_ALL);

	e_cal_util_remove_instances_impl (icalcomp, rid, mod, FALSE, TRUE, tz_cb, tz_cb_data);
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
 * Use e_cal_util_remove_instances_ex() with E_CAL_OBJ_MOD_THIS_AND_FUTURE mode
 * on the @icalcomp to remove the overlapping interval from it, if needed.
 *
 * Free the returned non-NULL component with g_object_unref(), when
 * done with it.
 *
 * Returns: (transfer full) (nullable): the split @icalcomp, or %NULL.
 *
 * Since: 3.16
 *
 * Deprecated: 3.38: Use e_cal_util_split_at_instance_ex() instead, with provided
 *    timezone resolve function.
 **/
ICalComponent *
e_cal_util_split_at_instance (ICalComponent *icalcomp,
			      const ICalTime *rid,
			      const ICalTime *master_dtstart)
{
	return e_cal_util_split_at_instance_ex (icalcomp, rid, master_dtstart, NULL, NULL);
}

/**
 * e_cal_util_split_at_instance_ex:
 * @icalcomp: A (recurring) #ICalComponent
 * @rid: The base RECURRENCE-ID to remove
 * @master_dtstart: (nullable): The DTSTART of the master object
 * @tz_cb: (closure tz_cb_data) (scope call): The #ECalRecurResolveTimezoneCb to call
 * @tz_cb_data: User data to be passed to the @tz_cb callback
 *
 * Splits a recurring @icalcomp into two at time @rid. The returned #ICalComponent
 * is modified @icalcomp which contains recurrences beginning at @rid, inclusive.
 * The instance identified by @rid should exist. The @master_dtstart can be
 * a null time, then it is read from the @icalcomp.
 *
 * Uses @tz_cb with @tz_cb_data to resolve time zones when needed.
 *
 * Use e_cal_util_remove_instances_ex() with E_CAL_OBJ_MOD_THIS_AND_FUTURE mode
 * on the @icalcomp to remove the overlapping interval from it, if needed.
 *
 * Free the returned non-NULL component with g_object_unref(), when
 * done with it.
 *
 * Returns: (transfer full) (nullable): the split @icalcomp, or %NULL.
 *
 * Since: 3.38
 **/
ICalComponent *
e_cal_util_split_at_instance_ex (ICalComponent *icalcomp,
				 const ICalTime *rid,
				 const ICalTime *master_dtstart,
				 ECalRecurResolveTimezoneCb tz_cb,
				 gpointer tz_cb_data)
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

	e_cal_util_remove_instances_impl (icalcomp, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR, TRUE, FALSE, tz_cb, tz_cb_data);

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
	gboolean found_any;
	gint cmp_res;
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

	ifs->found_any = TRUE;
	ifs->cmp_res = i_cal_time_compare ((ICalTime *) ifs->rid, instance_start);
	ifs->matches = ifs->cmp_res == 0;
	if (ifs->matches)
		return FALSE;

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

	ifs->cmp_res = i_cal_time_compare ((ICalTime *) ifs->rid, rid);
	ifs->matches = ifs->cmp_res == 0;

	g_clear_object (&rid);

	return FALSE;
}

static ICalTime *
e_cal_util_dup_rid_from_slist (GSList *detached_instances, /* ECalComponent * */
			       gboolean first_by_time, /* FALSE to get last by time */
			       ECalRecurResolveTimezoneCb tz_cb,
			       gpointer tz_cb_data)
{
	GSList *link;
	ICalTime *adept_rid = NULL;

	for (link = detached_instances; link; link = g_slist_next (link)) {
		ECalComponent *ecomp = link->data;
		ICalComponent *icomp = e_cal_component_get_icalcomponent (ecomp);
		ICalProperty *prop;

		prop = i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY);
		if (prop) {
			ICalTime *rid;

			rid = i_cal_property_get_recurrenceid (prop);

			if (rid && !i_cal_time_get_timezone (rid)) {
				ICalParameter *param;

				param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);

				if (param) {
					const gchar *tzid;

					tzid = i_cal_parameter_get_tzid (param);

					if (tzid && *tzid) {
						ICalTimezone *tz = NULL;

						if (g_ascii_strcasecmp (tzid, "UTC") == 0)
							tz = i_cal_timezone_get_utc_timezone ();
						else if (tz_cb)
							tz = tz_cb (tzid, tz_cb_data, NULL, NULL);

						if (tz)
							i_cal_time_set_timezone (rid, tz);
					}

					g_object_unref (param);
				}
			}

			if (rid) {
				if (!adept_rid) {
					adept_rid = rid;
				} else {
					gint cmp_res = i_cal_time_compare (adept_rid, rid);

					if (!first_by_time)
						cmp_res *= -1;

					if (cmp_res > 0) {
						g_clear_object (&adept_rid);
						adept_rid = rid;
					} else {
						g_clear_object (&rid);
					}
				}
			}

			g_object_unref (prop);
		}
	}

	return adept_rid;
}

static gboolean
e_cal_util_is_first_instance_impl (ECalComponent *ecomp,
				   GSList *detached_instances, /* ECalComponent * */
				   const ICalTime *rid,
				   ECalRecurResolveTimezoneCb tz_cb,
				   gpointer tz_cb_data)
{
	CheckFirstInstanceData ifs;
	ICalComponent *icalcomp;
	ICalTime *start, *end;

	ifs.rid = rid;
	ifs.matches = FALSE;
	ifs.found_any = FALSE;
	ifs.cmp_res = 0;

	icalcomp = e_cal_component_get_icalcomponent (ecomp);
	start = i_cal_component_get_dtstart (icalcomp);
	i_cal_time_adjust (start, -1, 0, 0, 0);

	end = i_cal_component_get_dtend (icalcomp);
	/* in case some early instances were detached or removed */
	i_cal_time_adjust (end, +100 * 366, 0, 0, 0);

	e_cal_recur_generate_instances_sync (icalcomp,
		start, end,
		check_first_instance_cb, &ifs,
		tz_cb, tz_cb_data, i_cal_timezone_get_utc_timezone (),
		NULL, NULL);

	g_clear_object (&start);
	g_clear_object (&end);

	/* when the rid did not match the first found instance, and it was before it,
	   then the first instance can be detached, then check the detached instances */
	if (!ifs.matches && ifs.cmp_res < 0) {
		ICalTime *instance_rid;

		instance_rid = e_cal_util_dup_rid_from_slist (detached_instances, TRUE, tz_cb, tz_cb_data);
		if (instance_rid) {
			ifs.matches = i_cal_time_compare ((ICalTime *) rid, instance_rid) == 0;

			g_clear_object (&instance_rid);
		}
	} else if (ifs.matches) {
		ICalTime *instance_rid;

		/* maybe any detached instance is before the found instance */
		instance_rid = e_cal_util_dup_rid_from_slist (detached_instances, TRUE, tz_cb, tz_cb_data);
		if (instance_rid) {
			ifs.matches = i_cal_time_compare ((ICalTime *) rid, instance_rid) < 0;

			g_clear_object (&instance_rid);
		}
	}

	return ifs.matches;
}

/**
 * e_cal_util_is_first_instance:
 * @comp: an #ECalComponent instance
 * @rid: a recurrence ID
 * @tz_cb: (closure tz_cb_data) (scope call): The #ECalRecurResolveTimezoneCb to call
 * @tz_cb_data: User data to be passed to the @tz_cb callback
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
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);
	g_return_val_if_fail (rid && !i_cal_time_is_null_time ((ICalTime *) rid), FALSE);

	return e_cal_util_is_first_instance_impl (comp, NULL, rid, tz_cb, tz_cb_data);
}

typedef struct {
	const ICalTime *rid;
	gboolean found;
	gboolean has_more;
} CheckLastInstanceData;

static gboolean
check_last_instance_cb (ICalComponent *icalcomp,
			 ICalTime *instance_start,
			 ICalTime *instance_end,
			 gpointer user_data,
			 GCancellable *cancellable,
			 GError **error)
{
	CheckLastInstanceData *cli = user_data;
	ICalProperty *prop;
	ICalTime *rid;
	gint cmp;

	g_return_val_if_fail (cli != NULL, FALSE);

	if (cli->found) {
		cli->has_more = TRUE;
		return FALSE;
	}

	cli->found = i_cal_time_compare ((ICalTime *) cli->rid, instance_start) == 0;
	if (cli->found)
		return TRUE;

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

	cmp = i_cal_time_compare ((ICalTime *) cli->rid, rid);
	/* it's past the needed instance, stop now */
	if (cmp < 0) {
		cli->has_more = TRUE;
		g_clear_object (&rid);
		return FALSE;
	}

	cli->found = cmp == 0;

	g_clear_object (&rid);

	return TRUE;
}

static gboolean
e_cal_util_is_last_instance_ical (ECalComponent *comp,
				  GSList *detached_instances, /* ECalComponent * */
				  const ICalTime *rid,
				  ECalRecurResolveTimezoneCb tz_cb,
				  gpointer tz_cb_data)
{
	CheckLastInstanceData cli;
	ICalComponent *icalcomp;
	ICalProperty *prop;
	ICalTime *start, *end;
	gboolean is_forever = FALSE;

	icalcomp = e_cal_component_get_icalcomponent (comp);

	for (prop = i_cal_component_get_first_property (icalcomp, I_CAL_RRULE_PROPERTY);
	     prop && !is_forever;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icalcomp, I_CAL_RRULE_PROPERTY)) {
		ICalRecurrence *rrule;

		rrule = i_cal_property_get_rrule (prop);
		if (rrule && !i_cal_recurrence_get_count (rrule)) {
			ICalTime *until;

			until = i_cal_recurrence_get_until (rrule);

			is_forever = !(until && !i_cal_time_is_null_time (until) && i_cal_time_is_valid_time (until));

			g_clear_object (&until);
		}

		g_clear_object (&rrule);
	}

	g_clear_object (&prop);

	if (is_forever)
		return FALSE;

	cli.rid = rid;
	cli.found = FALSE;
	cli.has_more = FALSE;

	start = i_cal_time_clone (rid);
	i_cal_time_adjust (start, -1, 0, 0, 0);

	end = i_cal_time_clone (rid);
	/* hard to guess, but suppose the next instance will be in less than 100 years;
	   the recurrence expansion ends as soon as the first following instance is encountered */
	i_cal_time_adjust (end, 100 * 366, 0, 0, 0);

	e_cal_recur_generate_instances_sync (icalcomp,
		start, end,
		check_last_instance_cb, &cli,
		tz_cb, tz_cb_data, i_cal_timezone_get_utc_timezone (),
		NULL, NULL);

	g_clear_object (&start);
	g_clear_object (&end);

	if (!cli.found && !cli.has_more) {
		ICalTime *instance_rid;

		instance_rid = e_cal_util_dup_rid_from_slist (detached_instances, FALSE, tz_cb, tz_cb_data);
		if (instance_rid) {
			cli.found = i_cal_time_compare ((ICalTime *) rid, instance_rid) == 0;

			g_clear_object (&instance_rid);
		}
	} else if (cli.found && !cli.has_more) {
		ICalTime *instance_rid;

		instance_rid = e_cal_util_dup_rid_from_slist (detached_instances, FALSE, tz_cb, tz_cb_data);
		if (instance_rid) {
			cli.has_more = i_cal_time_compare ((ICalTime *) rid, instance_rid) < 0;

			g_clear_object (&instance_rid);
		}
	}

	return cli.found && !cli.has_more;
}

/**
 * e_cal_util_check_may_remove_all:
 * @comp: an #ECalComponent
 * @detached_instances: (element-type ECalComponent) (nullable): a #GSList of detached instances as #ECalComponent, or %NULL
 * @rid: (nullable): recurrence ID to remove, or %NULL
 * @mod: an #ECalObjModType removal mode
 * @tz_cb: (closure tz_cb_data) (scope call): The #ECalRecurResolveTimezoneCb to call
 * @tz_cb_data: User data to be passed to the @tz_cb callback
 *
 * Checks whether removing the instance with recurrence ID @rid from
 * the series defined in the @icalcomp using the @mod mode is equal
 * to remove all events.
 *
 * Returns: %TRUE, when the whole series should be removed
 *
 * Since: 3.58
 **/
gboolean
e_cal_util_check_may_remove_all (ECalComponent *comp,
				 GSList *detached_instances,
				 const ICalTime *rid,
				 ECalObjModType mod,
				 ECalRecurResolveTimezoneCb tz_cb,
				 gpointer tz_cb_data)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);

	if (mod == E_CAL_OBJ_MOD_ALL)
		return TRUE;

	if (!rid)
		return FALSE;

	if (mod == E_CAL_OBJ_MOD_THIS_AND_FUTURE &&
	    e_cal_util_is_first_instance_impl (comp, detached_instances, rid, tz_cb, tz_cb_data))
		return TRUE;

	if (mod == E_CAL_OBJ_MOD_THIS_AND_PRIOR &&
	    e_cal_util_is_last_instance_ical (comp, detached_instances, rid, tz_cb, tz_cb_data))
		return TRUE;

	return FALSE;
}

/**
 * e_cal_util_get_system_timezone_location:
 *
 * Fetches system timezone localtion string.
 *
 * Returns: (transfer full) (nullable): system timezone location string, %NULL
 * on an error.
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
 * @tz_cb_data: User data to be passed to the @tz_cb callback
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
		/* ALARMS, EVENTS: DTEND and recurrences */

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
 * @cal_client: (type ECalClient): an #ECalClient to which the @vtodo belongs
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
 * @cal_client: (type ECalClient): an #ECalClient to which the @vtodo belongs
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
				ICalTime *old_dtstart, *old_due;

				old_dtstart = i_cal_component_get_dtstart (vtodo);
				old_due = i_cal_component_get_due (vtodo);

				/* Move relatively also the DUE date, to keep the difference... */
				if (old_due && !i_cal_time_is_null_time (old_due) && i_cal_time_is_valid_time (old_due)) {
					if (old_dtstart && !i_cal_time_is_null_time (old_dtstart) && i_cal_time_is_valid_time (old_dtstart)) {
						gint64 diff;

						diff = i_cal_time_as_timet (old_due) - i_cal_time_as_timet (old_dtstart);
						new_due = i_cal_time_clone (new_dtstart);
						i_cal_time_adjust (new_due, diff / (24 * 60 * 60), (diff / (60 * 60)) % 24,
							(diff / 60) % 60, diff % 60);
					} else if (!e_cal_util_find_next_occurrence (vtodo, old_due, &new_due, cal_client, cancellable, error)) {
						g_clear_object (&new_dtstart);
						g_clear_object (&new_due);
						g_clear_object (&old_dtstart);
						g_clear_object (&old_due);
						return FALSE;
					}
				}

				g_clear_object (&old_dtstart);

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

static void
ecu_remove_all_but_filename_parameter (ICalProperty *prop)
{
	ICalParameter *param;

	g_return_if_fail (prop != NULL);

	while (param = i_cal_property_get_first_parameter (prop, I_CAL_ANY_PARAMETER), param) {
		if (i_cal_parameter_isa (param) == I_CAL_FILENAME_PARAMETER) {
			g_object_unref (param);
			param = i_cal_property_get_next_parameter (prop, I_CAL_ANY_PARAMETER);
			if (!param)
				break;
		}

		i_cal_property_remove_parameter_by_ref (prop, param);
		g_object_unref (param);
	}
}

/**
 * e_cal_util_inline_local_attachments_sync:
 * @component: an #ICalComponent to work with
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Changes all URL attachments which point to a local file in @component
 * to inline attachments, aka adds the file content into the @component.
 * It also populates FILENAME parameter on the attachment.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.40
 **/
gboolean
e_cal_util_inline_local_attachments_sync (ICalComponent *component,
					  GCancellable *cancellable,
					  GError **error)
{
	ICalProperty *prop;
	const gchar *uid;
	gboolean success = TRUE;

	g_return_val_if_fail (component != NULL, FALSE);

	uid = i_cal_component_get_uid (component);

	for (prop = i_cal_component_get_first_property (component, I_CAL_ATTACH_PROPERTY);
	     prop && success;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (component, I_CAL_ATTACH_PROPERTY)) {
		ICalAttach *attach;

		attach = i_cal_property_get_attach (prop);
		if (attach && i_cal_attach_get_is_url (attach)) {
			const gchar *url_data;
			gchar *url = NULL;

			url_data = i_cal_attach_get_url (attach);
			url = url_data ? i_cal_value_decode_ical_string (url_data) : NULL;

			if (url && g_str_has_prefix (url, "file://")) {
				GFile *file;
				gchar *basename;
				gchar *content;
				gsize len;

				file = g_file_new_for_uri (url);
				basename = g_file_get_basename (file);
				if (g_file_load_contents (file, cancellable, &content, &len, NULL, error)) {
					ICalAttach *new_attach;
					ICalParameter *param;
					gchar *base64;

					base64 = g_base64_encode ((const guchar *) content, len);
					new_attach = i_cal_attach_new_from_data (base64, (GFunc) g_free, NULL);
					g_free (content);

					ecu_remove_all_but_filename_parameter (prop);

					i_cal_property_set_attach (prop, new_attach);
					g_object_unref (new_attach);

					param = i_cal_parameter_new_value (I_CAL_VALUE_BINARY);
					i_cal_property_take_parameter (prop, param);

					param = i_cal_parameter_new_encoding (I_CAL_ENCODING_BASE64);
					i_cal_property_take_parameter (prop, param);

					/* Preserve existing FILENAME parameter */
					if (!e_cal_util_property_has_parameter (prop, I_CAL_FILENAME_PARAMETER)) {
						const gchar *use_filename = basename;

						/* generated filename by Evolution */
						if (uid && g_str_has_prefix (use_filename, uid) &&
						    use_filename[strlen (uid)] == '-') {
							use_filename += strlen (uid) + 1;
						}

						param = i_cal_parameter_new_filename (use_filename);
						i_cal_property_take_parameter (prop, param);
					}
				} else {
					success = FALSE;
				}

				g_object_unref (file);
				g_free (basename);
			}

			g_free (url);
		}

		g_clear_object (&attach);
	}

	g_clear_object (&prop);

	return success;
}

/**
 * e_cal_util_set_alarm_acknowledged:
 * @component: an #ECalComponent
 * @auid: an alarm UID to modify
 * @when: a time, in UTC, when to set the acknowledged property, or 0 for the current time
 *
 * Sets the ACKNOWLEDGED property on the @component's alarm with UID @auid
 * to the time @when (in UTC), or to the current time, when the @when is 0.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.40
 **/
gboolean
e_cal_util_set_alarm_acknowledged (ECalComponent *component,
				   const gchar *auid,
				   gint64 when)
{
	ECalComponentAlarm *alarm, *copy;
	ICalTime *tt;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (component), FALSE);
	g_return_val_if_fail (auid != NULL, FALSE);

	alarm = e_cal_component_get_alarm (component, auid);

	if (!alarm)
		return FALSE;

	if (when)
		tt = i_cal_time_new_from_timet_with_zone (when, 0, i_cal_timezone_get_utc_timezone ());
	else
		tt = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());

	copy = e_cal_component_alarm_copy (alarm);

	e_cal_component_alarm_take_acknowledged (copy, tt);
	e_cal_component_remove_alarm (component, auid);
	e_cal_component_add_alarm (component, copy);

	e_cal_component_alarm_free (copy);
	e_cal_component_alarm_free (alarm);

	return TRUE;
}

static void
e_cal_util_clamp_vtimezone_subcomps (ICalComponent *vtimezone,
				     ICalComponentKind kind,
				     const ICalTime *from,
				     const ICalTime *to)
{
	ICalComponent *subcomp;
	ICalComponent *nearest_from_comp = NULL, *nearest_to_comp = NULL;
	ICalTime *nearest_from_time = NULL, *nearest_to_time = NULL;
	GSList *remove = NULL, *link;

	for (subcomp = i_cal_component_get_first_component (vtimezone, kind);
	     subcomp;
	     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (vtimezone, kind)) {
		ICalTime *dtstart;

		dtstart = i_cal_component_get_dtstart (subcomp);
		if (dtstart && !i_cal_time_is_null_time (dtstart) && i_cal_time_is_valid_time (dtstart)) {
			gint cmp;

			cmp = i_cal_time_compare (dtstart, from);
			if (cmp < 0) {
				if (nearest_from_time) {
					if (i_cal_time_compare (dtstart, nearest_from_time) > 0) {
						g_clear_object (&nearest_from_time);
						nearest_from_time = g_object_ref (dtstart);
						remove = g_slist_prepend (remove, nearest_from_comp);
						nearest_from_comp = g_object_ref (subcomp);
					} else {
						remove = g_slist_prepend (remove, g_object_ref (subcomp));
					}
				} else {
					nearest_from_time = g_object_ref (dtstart);
					nearest_from_comp = g_object_ref (subcomp);
				}
			} else if (cmp > 0 && to) {
				cmp = i_cal_time_compare (to, dtstart);
				if (cmp < 0)
					remove = g_slist_prepend (remove, g_object_ref (subcomp));
			}
		}

		g_clear_object (&dtstart);
	}

	g_clear_object (&nearest_from_comp);
	g_clear_object (&nearest_from_time);
	g_clear_object (&nearest_to_comp);
	g_clear_object (&nearest_to_time);

	for (link = remove; link; link = g_slist_next (link)) {
		subcomp = link->data;

		i_cal_component_remove_component (vtimezone, subcomp);
	}

	g_slist_free_full (remove, g_object_unref);
}

/**
 * e_cal_util_clamp_vtimezone:
 * @vtimezone: (inout): a VTIMEZONE component to modify
 * @from: an #ICalTime for the minimum time
 * @to: (nullable): until which time to clamp, or %NULL for infinity
 *
 * Modifies the @vtimezone to include only subcomponents influencing
 * the passed-in time interval between @from and @to.
 *
 * Since: 3.40
 **/
void
e_cal_util_clamp_vtimezone (ICalComponent *vtimezone,
			    const ICalTime *from,
			    const ICalTime *to)
{
	g_return_if_fail (I_CAL_IS_COMPONENT (vtimezone));
	g_return_if_fail (i_cal_component_isa (vtimezone) == I_CAL_VTIMEZONE_COMPONENT);
	g_return_if_fail (I_CAL_IS_TIME ((ICalTime *) from));
	if (to) {
		g_return_if_fail (I_CAL_IS_TIME ((ICalTime *) to));

		if (i_cal_time_is_null_time (to) || !i_cal_time_is_valid_time (to))
			to = NULL;
	}

	if (i_cal_time_is_null_time (from) || !i_cal_time_is_valid_time (from))
		return;

	e_cal_util_clamp_vtimezone_subcomps (vtimezone, I_CAL_XSTANDARD_COMPONENT, from, to);
	e_cal_util_clamp_vtimezone_subcomps (vtimezone, I_CAL_XDAYLIGHT_COMPONENT, from, to);
}

/**
 * e_cal_util_clamp_vtimezone_by_component:
 * @vtimezone: (inout): a VTIMEZONE component to modify
 * @component: an #ICalComponent to read the times from
 *
 * Similar to e_cal_util_clamp_vtimezone(), only reads the clamp
 * times from the @component.
 *
 * Since: 3.40
 **/
void
e_cal_util_clamp_vtimezone_by_component (ICalComponent *vtimezone,
					 ICalComponent *component)
{
	ICalProperty *prop;
	ICalTime *dtstart, *dtend = NULL;

	g_return_if_fail (I_CAL_IS_COMPONENT (vtimezone));
	g_return_if_fail (i_cal_component_isa (vtimezone) == I_CAL_VTIMEZONE_COMPONENT);
	g_return_if_fail (I_CAL_IS_COMPONENT (component));

	dtstart = i_cal_component_get_dtstart (component);
	if (!dtstart)
		return;

	prop = i_cal_component_get_first_property (component, I_CAL_RECURRENCEID_PROPERTY);
	if (prop) {
		ICalTime *recurid;

		recurid = i_cal_property_get_recurrenceid (prop);

		dtend = i_cal_component_isa (component) == I_CAL_VEVENT_COMPONENT ? i_cal_component_get_dtend (component) : NULL;

		if (dtend && (i_cal_time_is_null_time (dtend) || !i_cal_time_is_valid_time (dtend)))
			g_clear_object (&dtend);

		if (!dtend)
			dtend = i_cal_component_get_due (component);

		if (dtend && i_cal_time_compare (recurid, dtend) >= 0) {
			g_clear_object (&dtend);
			dtend = recurid;
			recurid = NULL;
		}

		g_clear_object (&recurid);
		g_object_unref (prop);
	} else if (!e_cal_util_component_has_rrules (component)) {
		dtend = i_cal_component_isa (component) == I_CAL_VEVENT_COMPONENT ? i_cal_component_get_dtend (component) : NULL;

		if (dtend && (i_cal_time_is_null_time (dtend) || !i_cal_time_is_valid_time (dtend)))
			g_clear_object (&dtend);

		if (!dtend)
			dtend = i_cal_component_get_due (component);

		if (dtend && (i_cal_time_is_null_time (dtend) || !i_cal_time_is_valid_time (dtend)))
			g_clear_object (&dtend);

		if (!dtend)
			dtend = g_object_ref (dtstart);
	}

	if (i_cal_time_is_null_time (dtstart) || !i_cal_time_is_valid_time (dtstart)) {
		g_clear_object (&dtstart);

		if (dtend && !i_cal_time_is_null_time (dtend) && i_cal_time_is_valid_time (dtend))
			dtstart = g_object_ref (dtend);
	}

	e_cal_util_clamp_vtimezone (vtimezone, dtstart, dtend);

	g_clear_object (&dtstart);
	g_clear_object (&dtend);
}

static gboolean
locale_equals_language (const gchar *locale,
			const gchar *language)
{
	guint ii;

	for (ii = 0; locale[ii] && language[ii]; ii++) {
		if ((locale[ii] == '-' || locale[ii] == '_') &&
		    (language[ii] == '-' || language[ii] == '_')) {
			continue;
		}

		if (g_ascii_tolower (locale[ii]) != g_ascii_tolower (language[ii]))
			break;
	}

	return !locale[ii] && !language[ii];
}

/**
 * e_cal_util_component_find_property_for_locale_filtered:
 * @icalcomp: an #ICalComponent
 * @prop_kind: an #ICalPropertyKind to traverse
 * @locale: (nullable): a locale identifier, or %NULL
 * @func: (scope call) (nullable): an #ECalUtilFilterPropertyFunc, to determine whether a property can be considered
 * @user_data: user data for the @func
 *
 * Searches properties of kind @prop_kind in the @icalcomp, which can
 * be filtered by the @func, and returns one, which is usable for the @locale.
 * When @locale is %NULL, the current locale is assumed. If no such property
 * for the locale exists either the one with no language parameter or the first
 * found is returned.
 *
 * The @func is called before checking of the applicability for the @locale.
 * When the @func is %NULL, all the properties of the @prop_kind are considered.
 *
 * Free the returned non-NULL #ICalProperty with g_object_unref(),
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): a property of kind @prop_kind for the @locale,
 *    %NULL if no such property is set on the @comp.
 *
 * Since: 3.52
 **/
ICalProperty *
e_cal_util_component_find_property_for_locale_filtered (ICalComponent *icalcomp,
							ICalPropertyKind prop_kind,
							const gchar *locale,
							ECalUtilFilterPropertyFunc func,
							gpointer user_data)
{
	ICalProperty *prop;
	ICalProperty *result = NULL;
	ICalProperty *first = NULL;
	ICalProperty *nolang = NULL;
	ICalProperty *best = NULL;
	gint best_index = -1;
	gchar **locale_variants = NULL;
	const gchar *const *locales = NULL;

	g_return_val_if_fail (I_CAL_IS_COMPONENT (icalcomp), NULL);

	if (locale) {
		locale_variants = g_get_locale_variants (locale);
		locales = (const gchar * const *) locale_variants;
	}

	if (!locales)
		locales = g_get_language_names ();

	for (prop = i_cal_component_get_first_property (icalcomp, prop_kind);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icalcomp, prop_kind)) {
		ICalParameter *param;

		if (func != NULL && !func (prop, user_data))
			continue;

		param = i_cal_property_get_first_parameter (prop, I_CAL_LANGUAGE_PARAMETER);
		if (param) {
			const gchar *language = i_cal_parameter_get_language (param);

			if (!language || !*language) {
				if (!best) {
					if (!first)
						first = g_object_ref (prop);
					if (!nolang)
						nolang = g_object_ref (prop);
				}
			} else {
				guint ii;

				for (ii = 0; locales && locales[ii] && (best_index == -1 || ii < best_index); ii++) {
					if (locale_equals_language (locales[ii], language)) {
						g_clear_object (&best);
						best = g_object_ref (prop);
						best_index = ii;
						break;
					}
				}

				if (!ii && best) {
					g_clear_object (&param);
					g_clear_object (&prop);
					break;
				}

				if (!best && !first)
					first = g_object_ref (prop);
			}

			g_clear_object (&param);
		} else if (!best) {
			if (!first)
				first = g_object_ref (prop);
			if (!nolang)
				nolang = g_object_ref (prop);
		}
	}

	if (best)
		result = g_steal_pointer (&best);
	else if (nolang)
		result = g_steal_pointer (&nolang);
	else if (first)
		result = g_steal_pointer (&first);

	g_clear_object (&first);
	g_clear_object (&nolang);
	g_clear_object (&best);
	g_clear_pointer (&locale_variants, g_strfreev);

	return result;
}

/**
 * e_cal_util_component_find_property_for_locale:
 * @icalcomp: an #ICalComponent
 * @prop_kind: an #ICalPropertyKind to traverse
 * @locale: (nullable): a locale identifier, or %NULL
 *
 * Searches properties of kind @prop_kind in the @icalcomp and returns
 * one, which is usable for the @locale. When @locale is %NULL,
 * the current locale is assumed. If no such property for the locale
 * exists either the one with no language parameter or the first
 * found is returned.
 *
 * Free the returned non-NULL #ICalProperty with g_object_unref(),
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): a property of kind @prop_kind for the @locale,
 *    %NULL if no such property is set on the @comp.
 *
 * Since: 3.46
 **/
ICalProperty *
e_cal_util_component_find_property_for_locale (ICalComponent *icalcomp,
					       ICalPropertyKind prop_kind,
					       const gchar *locale)
{
	g_return_val_if_fail (I_CAL_IS_COMPONENT (icalcomp), NULL);

	return e_cal_util_component_find_property_for_locale_filtered (icalcomp, prop_kind, locale, NULL, NULL);
}

/**
 * e_cal_util_foreach_category:
 * @comp: an #ICalComponent
 * @func: (scope call): an #ECalUtilForeachCategoryFunc callback to call for each category
 * @user_data: user data passed to the @func
 *
 * Calls @func for each category stored in the @comp.
 *
 * Since: 3.48
 **/
void
e_cal_util_foreach_category (ICalComponent *comp,
			     ECalUtilForeachCategoryFunc func,
			     gpointer user_data)
{
	ICalProperty *prop;
	const gchar *categories;
	const gchar *p;
	const gchar *cat_start;
	gchar *str;
	gboolean can_continue = TRUE;

	g_return_if_fail (I_CAL_IS_COMPONENT (comp));
	g_return_if_fail (func != NULL);

	for (prop = i_cal_component_get_first_property (comp, I_CAL_CATEGORIES_PROPERTY);
	     prop && can_continue;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (comp, I_CAL_CATEGORIES_PROPERTY)) {
		categories = i_cal_property_get_categories (prop);

		if (!categories)
			continue;

		cat_start = categories;

		for (p = categories; *p && can_continue; p++) {
			if (*p == ',') {
				if (p - cat_start > 0) {
					str = g_strstrip (g_strndup (cat_start, p - cat_start));
					if (*str)
						can_continue = func (comp, &str, user_data);
					g_free (str);
				}

				cat_start = p + 1;
			}
		}

		if (can_continue && p - cat_start > 0) {
			str = g_strstrip (g_strndup (cat_start, p - cat_start));
			if (*str)
				can_continue = func (comp, &str, user_data);
			g_free (str);
		}
	}

	g_clear_object (&prop);
}

static gboolean
e_cal_util_extract_categories_cb (ICalComponent *comp,
				  gchar **inout_category,
				  gpointer user_data)
{
	GHashTable **pcategories = user_data;

	g_return_val_if_fail (pcategories != NULL, FALSE);
	g_return_val_if_fail (inout_category != NULL, FALSE);
	g_return_val_if_fail (*inout_category != NULL, FALSE);

	if (!*pcategories)
		*pcategories = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	g_hash_table_insert (*pcategories, *inout_category, GINT_TO_POINTER (1));

	*inout_category = NULL;

	return TRUE;
}

static GHashTable *
e_cal_util_extract_categories (ICalComponent *comp)
{
	GHashTable *categories = NULL;

	if (!comp)
		return NULL;

	g_return_val_if_fail (I_CAL_IS_COMPONENT (comp), NULL);

	e_cal_util_foreach_category (comp, e_cal_util_extract_categories_cb, &categories);

	return categories;
}

static gboolean
e_cal_util_remove_matching_category_cb (gpointer key,
					gpointer value,
					gpointer user_data)
{
	GHashTable *other_table = user_data;

	/* Remove from both tables those common */
	return g_hash_table_remove (other_table, key);
}

/**
 * e_cal_util_diff_categories:
 * @old_comp: (nullable): an old #ICalComponent, or %NULL
 * @new_comp: (nullable): a new #ICalComponent, or %NULL
 * @out_added: (out) (transfer container) (element-type utf8 int): a #GHashTable with added categories
 * @out_removed: (out) (transfer container) (element-type utf8 int): a #GHashTable with removed categories
 *
 * Compares list of categories on the @old_comp with the list of categories
 * on the @new_comp and fills @out_added categories and @out_removed categories
 * accordingly, as if the @old_comp is replaced with the @new_comp. When either
 * of the components is %NULL, it's considered as having no categories set.
 * Rather than returning empty #GHashTable, the return argument is set to %NULL
 * when there are no added/removed categories.
 *
 * The key of the hash table is the category string, the value is an integer (1).
 * There is used the hash table only for speed.
 *
 * The returned #GHashTable-s should be freed with g_hash_table_unref(),
 * when no longer needed.
 *
 * Since: 3.48
 **/
void
e_cal_util_diff_categories (ICalComponent *old_comp,
			    ICalComponent *new_comp,
			    GHashTable **out_added, /* const gchar *category ~> 1 */
			    GHashTable **out_removed) /* const gchar *category ~> 1 */
{
	if (old_comp)
		g_return_if_fail (I_CAL_IS_COMPONENT (old_comp));
	if (new_comp)
		g_return_if_fail (I_CAL_IS_COMPONENT (new_comp));
	g_return_if_fail (out_added != NULL);
	g_return_if_fail (out_removed != NULL);

	*out_added = e_cal_util_extract_categories (new_comp);
	*out_removed = e_cal_util_extract_categories (old_comp);

	if (*out_added && *out_removed) {
		g_hash_table_foreach_remove (*out_added, e_cal_util_remove_matching_category_cb, *out_removed);

		if (!g_hash_table_size (*out_added)) {
			g_hash_table_unref (*out_added);
			*out_added = NULL;
		}

		if (!g_hash_table_size (*out_removed)) {
			g_hash_table_unref (*out_removed);
			*out_removed = NULL;
		}
	}
}

/**
 * e_cal_util_strip_mailto:
 * @address: (nullable): an address with or without "mailto:" prefix
 *
 * Strips "mailto:" prefix from the @address, if present. The returned
 * pointer is either the @address or a shifted position within the @address.
 *
 * Returns: the @address without the "mailto:" prefix
 *
 * Since: 3.50
 **/
const gchar *
e_cal_util_strip_mailto (const gchar *address)
{
	if (!address)
		return NULL;

	if (!g_ascii_strncasecmp (address, "mailto:", 7))
		address += 7;

	return address;
}

/**
 * e_cal_util_email_addresses_equal:
 * @email1: (nullable): the first email
 * @email2: (nullable): the second email
 *
 * Compares two email addresses and returns whether they are equal.
 * Each address can contain a "mailto:" prefix. The two addresses
 * match only if they are non-NULL and non-empty. The address itself
 * is compared case insensitively.
 *
 * Returns: %TRUE, when the @email1 equals to @email2
 *
 * Since: 3.50
 **/
gboolean
e_cal_util_email_addresses_equal (const gchar *email1,
				  const gchar *email2)
{
	if (!email1 || !email2)
		return FALSE;

	email1 = e_cal_util_strip_mailto (email1);
	email2 = e_cal_util_strip_mailto (email2);

	if (!email1 || !*email1 || !email2 || !*email2)
		return FALSE;

	return g_ascii_strcasecmp (email1, email2) == 0;
}

/**
 * e_cal_util_get_default_name_and_address:
 * @registry: an #ESourceRegistry
 * @out_name: (out callee-allocates) (optional): return location for the user's real name, or %NULL
 * @out_address: (out callee-allocates) (optional): return location for the user's email address, or %NULL
 *
 * Returns the real name and email address of the default mail identity,
 * if available.  If no default mail identity is available, @out_name and
 * @out_address are set to %NULL and the function returns %FALSE.
 *
 * Returns: %TRUE if @out_name and/or @out_address were set
 *
 * Since: 3.50
 **/
gboolean
e_cal_util_get_default_name_and_address (ESourceRegistry *registry,
					 gchar **out_name,
					 gchar **out_address)
{
	ESource *source;
	ESourceExtension *extension;
	const gchar *extension_name;
	gboolean success;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), FALSE);

	source = e_source_registry_ref_default_mail_identity (registry);

	if (source) {
		extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
		extension = e_source_get_extension (source, extension_name);

		if (out_name)
			*out_name = e_source_mail_identity_dup_name (E_SOURCE_MAIL_IDENTITY (extension));

		if (out_address)
			*out_address = e_source_mail_identity_dup_address (E_SOURCE_MAIL_IDENTITY (extension));

		g_object_unref (source);

		success = TRUE;
	} else {
		if (out_name)
			*out_name = NULL;

		if (out_address)
			*out_address = NULL;

		success = FALSE;
	}

	return success;
}

static const gchar *
e_cal_util_get_property_value_email (const gchar *value,
				     ECalComponentParameterBag *params)
{
	const gchar *address = NULL;

	if (params) {
		guint email_index;

		#ifdef HAVE_I_CAL_EMAIL_PARAMETER
		email_index = e_cal_component_parameter_bag_get_first_by_kind (params, I_CAL_EMAIL_PARAMETER);
		#else
		email_index = e_cal_component_parameter_bag_get_first_by_kind (params, (ICalParameterKind) ICAL_EMAIL_PARAMETER);
		#endif

		if (email_index < e_cal_component_parameter_bag_get_count (params)) {
			ICalParameter *param;

			param = e_cal_component_parameter_bag_get (params, email_index);

			if (param) {
				#ifdef HAVE_I_CAL_EMAIL_PARAMETER
				address = i_cal_parameter_get_email (param);
				#else
				address = icalparameter_get_email (i_cal_object_get_native (I_CAL_OBJECT (param)));
				#endif

				if (address && !*address)
					address = NULL;
			}
		}
	}

	if (!address)
		address = value;

	if (address)
		address = e_cal_util_strip_mailto (address);

	if (address && !*address)
		address = NULL;

	return address;
}

/**
 * e_cal_util_get_organizer_email:
 * @organizer: (nullable): an #ECalComponentOrganizer
 *
 * Returns an organizer email, without the "mailto:" prefix, if
 * the @organizer has it set. The email can be read from an "EMAIL"
 * parameter, if present.
 *
 * Returns: (nullable): email of the @organizer, or %NULL
 *
 * Since: 3.50
 **/
const gchar *
e_cal_util_get_organizer_email (const ECalComponentOrganizer *organizer)
{
	if (!organizer)
		return NULL;

	return e_cal_util_get_property_value_email (
		e_cal_component_organizer_get_value (organizer),
		e_cal_component_organizer_get_parameter_bag (organizer));
}

/**
 * e_cal_util_get_attendee_email:
 * @attendee: (nullable): an ECalComponentAttendee
 *
 * Returns an attendee email, without the "mailto:" prefix, if
 * the @attendee has it set. The email can be read from an "EMAIL"
 * parameter, if present.
 *
 * Returns: (nullable): email of the @attendee, or %NULL
 *
 * Since: 3.50
 **/
const gchar *
e_cal_util_get_attendee_email (const ECalComponentAttendee *attendee)
{
	if (!attendee)
		return NULL;

	return e_cal_util_get_property_value_email (
		e_cal_component_attendee_get_value (attendee),
		e_cal_component_attendee_get_parameter_bag (attendee));
}

/**
 * e_cal_util_get_property_email:
 * @prop: an #ICalProperty
 *
 * Returns an @prop email, without the "mailto:" prefix, if
 * the @prop has it set. The email can be read from an "EMAIL"
 * parameter, if present. Otherwise the @prop can be only of
 * type %I_CAL_ORGANIZER_PROPERTY or %I_CAL_ATTENDEE_PROPERTY.
 *
 * See also: e_cal_util_get_organizer_email(), e_cal_util_get_attendee_email()
 *
 * Returns: (nullable): email of the @prop, or %NULL
 *
 * Since: 3.50
 **/
const gchar *
e_cal_util_get_property_email (ICalProperty *prop)
{
	ICalParameter *param;
	const gchar *email = NULL;

	if (!prop)
		return NULL;

	#ifdef HAVE_I_CAL_EMAIL_PARAMETER
	param = i_cal_property_get_first_parameter (prop, I_CAL_EMAIL_PARAMETER);

	if (param) {
		email = i_cal_parameter_get_email (param);
		if (email)
			email = e_cal_util_strip_mailto (email);

		g_clear_object (&param);
	}
	#else
	param = i_cal_property_get_first_parameter (prop, (ICalParameterKind) ICAL_EMAIL_PARAMETER);

	if (param) {
		email = icalparameter_get_email (i_cal_object_get_native (I_CAL_OBJECT (param)));
		if (email)
			email = e_cal_util_strip_mailto (email);

		g_clear_object (&param);
	}
	#endif /* HAVE_I_CAL_EMAIL_PARAMETER */

	if (!email || !*email) {
		if (i_cal_property_isa (prop) == I_CAL_ORGANIZER_PROPERTY)
			email = i_cal_property_get_organizer (prop);
		else if (i_cal_property_isa (prop) == I_CAL_ATTENDEE_PROPERTY)
			email = i_cal_property_get_attendee (prop);
		else
			g_warn_if_reached ();

		email = e_cal_util_strip_mailto (email);
	}

	if (email && !*email)
		email = NULL;

	return email;
}

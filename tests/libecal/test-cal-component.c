/*
 * Copyright (C) 2019 Red Hat, Inc. (www.redhat.com)
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "evolution-data-server-config.h"

#include <glib.h>
#include <libecal/libecal.h>

static void
verify_ical_attach_equal (ICalAttach *expected,
			  ICalAttach *received)
{
	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);
	g_assert_cmpint (i_cal_attach_get_is_url (expected) ? 1 : 0, ==, i_cal_attach_get_is_url (received) ? 1 : 0);

	if (i_cal_attach_get_is_url (expected)) {
		g_assert_cmpstr (i_cal_attach_get_url (expected), ==, i_cal_attach_get_url (received));
	} else {
		const gchar *data_expected, *data_received;

		data_expected = i_cal_attach_get_data (expected);
		data_received = i_cal_attach_get_data (received);

		g_assert_nonnull (data_expected);
		g_assert_nonnull (data_received);
		g_assert_cmpmem (data_expected, strlen (data_expected), data_received, strlen (data_received));
	}
}

static void
verify_ical_attach_list_equal (GSList *expected, /* ICalAttach * */
			       GSList *received) /* ICalAttach * */
{
	GSList *link1, *link2;

	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);
	g_assert_cmpint (g_slist_length (expected), ==, g_slist_length (received));

	for (link1 = expected, link2 = received; link1 && link2; link1 = g_slist_next (link1), link2 = g_slist_next (link2)) {
		verify_ical_attach_equal (link1->data, link2->data);
	}

	g_assert_true (link1 == link2);
}

static void
verify_ical_durationtype_equal (ICalDuration *expected,
				ICalDuration *received)
{
	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	g_assert_cmpint (i_cal_duration_as_int (expected), ==, i_cal_duration_as_int (received));
}

static void
verify_ical_timetype_equal (ICalTime *expected,
			    ICalTime *received)
{
	ICalTimezone *zone_expected, *zone_received;

	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	g_assert_cmpint (i_cal_time_get_year (expected), ==, i_cal_time_get_year (received));
	g_assert_cmpint (i_cal_time_get_month (expected), ==, i_cal_time_get_month (received));
	g_assert_cmpint (i_cal_time_get_day (expected), ==, i_cal_time_get_day (received));
	g_assert_cmpint (i_cal_time_get_hour (expected), ==, i_cal_time_get_hour (received));
	g_assert_cmpint (i_cal_time_get_minute (expected), ==, i_cal_time_get_minute (received));
	g_assert_cmpint (i_cal_time_get_second (expected), ==, i_cal_time_get_second (received));
	g_assert_cmpint (i_cal_time_is_date (expected) ? 1 : 0, ==, i_cal_time_is_date (received) ? 1 : 0);
	g_assert_cmpint (i_cal_time_is_daylight (expected) ? 1 : 0, ==, i_cal_time_is_daylight (received) ? 1 : 0);

	zone_expected = i_cal_time_get_timezone (expected);
	zone_received = i_cal_time_get_timezone (received);

	if (!zone_expected) {
		g_assert_null (zone_received);
	} else if (zone_received) {
		g_assert_cmpstr (i_cal_timezone_get_location (zone_expected), ==, i_cal_timezone_get_location (zone_received));
	}
}

static void
verify_struct_parameter_bag_equal (const ECalComponentParameterBag *expected,
				   const ECalComponentParameterBag *received)
{
	gint ii, count;

	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	g_assert_cmpint (e_cal_component_parameter_bag_get_count (expected), ==, e_cal_component_parameter_bag_get_count (received));

	count = e_cal_component_parameter_bag_get_count (expected);
	for (ii = 0; ii < count; ii++) {
		ICalParameter *param_expected, *param_received;
		gchar *value_expected, *value_received;

		param_expected = e_cal_component_parameter_bag_get (expected, ii);
		param_received = e_cal_component_parameter_bag_get (received, ii);

		g_assert_nonnull (param_expected);
		g_assert_nonnull (param_received);
		g_assert_cmpint (i_cal_parameter_isa (param_expected), ==, i_cal_parameter_isa (param_received));

		value_expected = i_cal_parameter_as_ical_string (param_expected);
		value_received = i_cal_parameter_as_ical_string (param_received);

		g_assert_cmpstr (value_expected, ==, value_received);

		g_free (value_expected);
		g_free (value_received);
	}
}

static void
verify_struct_property_bag_equal (const ECalComponentPropertyBag *expected,
				  const ECalComponentPropertyBag *received)
{
	gint ii, count;

	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	g_assert_cmpint (e_cal_component_property_bag_get_count (expected), ==, e_cal_component_property_bag_get_count (received));

	count = e_cal_component_property_bag_get_count (expected);
	for (ii = 0; ii < count; ii++) {
		ICalProperty *prop_expected, *prop_received;
		gchar *value_expected, *value_received;

		prop_expected = e_cal_component_property_bag_get (expected, ii);
		prop_received = e_cal_component_property_bag_get (received, ii);

		g_assert_nonnull (prop_expected);
		g_assert_nonnull (prop_received);
		g_assert_cmpint (i_cal_property_isa (prop_expected), ==, i_cal_property_isa (prop_received));

		value_expected = i_cal_property_as_ical_string (prop_expected);
		value_received = i_cal_property_as_ical_string (prop_received);

		g_assert_cmpstr (value_expected, ==, value_received);

		g_free (value_expected);
		g_free (value_received);
	}
}

static void
verify_struct_attendee_equal (const ECalComponentAttendee *expected,
			      const ECalComponentAttendee *received)
{
	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	g_assert_cmpstr (e_cal_component_attendee_get_value (expected), ==, e_cal_component_attendee_get_value (received));
	g_assert_cmpstr (e_cal_component_attendee_get_member (expected), ==, e_cal_component_attendee_get_member (received));
	g_assert_cmpint (e_cal_component_attendee_get_cutype (expected), ==, e_cal_component_attendee_get_cutype (received));
	g_assert_cmpint (e_cal_component_attendee_get_role (expected), ==, e_cal_component_attendee_get_role (received));
	g_assert_cmpint (e_cal_component_attendee_get_partstat (expected), ==, e_cal_component_attendee_get_partstat (received));
	g_assert_cmpint (e_cal_component_attendee_get_rsvp (expected) ? 1 : 0, ==, e_cal_component_attendee_get_rsvp (received) ? 1 : 0);
	g_assert_cmpstr (e_cal_component_attendee_get_delegatedfrom (expected), ==, e_cal_component_attendee_get_delegatedfrom (received));
	g_assert_cmpstr (e_cal_component_attendee_get_delegatedto (expected), ==, e_cal_component_attendee_get_delegatedto (received));
	g_assert_cmpstr (e_cal_component_attendee_get_sentby (expected), ==, e_cal_component_attendee_get_sentby (received));
	g_assert_cmpstr (e_cal_component_attendee_get_cn (expected), ==, e_cal_component_attendee_get_cn (received));
	g_assert_cmpstr (e_cal_component_attendee_get_language (expected), ==, e_cal_component_attendee_get_language (received));

	verify_struct_parameter_bag_equal (e_cal_component_attendee_get_parameter_bag (expected),
					   e_cal_component_attendee_get_parameter_bag (received));
}

static void
verify_struct_attendee_list_equal (GSList *expected, /* ECalComponentAttendee * */
				   GSList *received) /* ECalComponentAttendee * */
{
	GSList *link1, *link2;

	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);
	g_assert_cmpint (g_slist_length (expected), ==, g_slist_length (received));

	for (link1 = expected, link2 = received; link1 && link2; link1 = g_slist_next (link1), link2 = g_slist_next (link2)) {
		verify_struct_attendee_equal (link1->data, link2->data);
	}

	g_assert_true (link1 == link2);
}

static void
verify_struct_datetime_equal (const ECalComponentDateTime *expected,
			      const ECalComponentDateTime *received)
{
	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	verify_ical_timetype_equal (e_cal_component_datetime_get_value (expected),
				    e_cal_component_datetime_get_value (received));

	g_assert_cmpstr (e_cal_component_datetime_get_tzid (expected), ==, e_cal_component_datetime_get_tzid (received));
}

static void
verify_struct_id_equal (const ECalComponentId *expected,
			const ECalComponentId *received)
{
	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	g_assert_cmpstr (e_cal_component_id_get_uid (expected), ==, e_cal_component_id_get_uid (received));
	g_assert_cmpstr (e_cal_component_id_get_rid (expected), ==, e_cal_component_id_get_rid (received));
	g_assert_cmpint (e_cal_component_id_hash (expected), ==, e_cal_component_id_hash (received));
	g_assert_true (e_cal_component_id_equal (expected, received));
}

static void
verify_struct_organizer_equal (const ECalComponentOrganizer *expected,
			       const ECalComponentOrganizer *received)
{
	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	g_assert_cmpstr (e_cal_component_organizer_get_value (expected), ==, e_cal_component_organizer_get_value (received));
	g_assert_cmpstr (e_cal_component_organizer_get_sentby (expected), ==, e_cal_component_organizer_get_sentby (received));
	g_assert_cmpstr (e_cal_component_organizer_get_cn (expected), ==, e_cal_component_organizer_get_cn (received));
	g_assert_cmpstr (e_cal_component_organizer_get_language (expected), ==, e_cal_component_organizer_get_language (received));

	verify_struct_parameter_bag_equal (e_cal_component_organizer_get_parameter_bag (expected),
					   e_cal_component_organizer_get_parameter_bag (received));
}

static void
verify_struct_period_equal (const ECalComponentPeriod *expected,
			    const ECalComponentPeriod *received)
{
	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	g_assert_cmpint (e_cal_component_period_get_kind (expected), ==, e_cal_component_period_get_kind (received));

	verify_ical_timetype_equal (e_cal_component_period_get_start (expected),
				    e_cal_component_period_get_start (received));

	switch (e_cal_component_period_get_kind (expected)) {
	case E_CAL_COMPONENT_PERIOD_DATETIME:
		verify_ical_timetype_equal (e_cal_component_period_get_end (expected),
					    e_cal_component_period_get_end (received));
		break;
	case E_CAL_COMPONENT_PERIOD_DURATION:
		verify_ical_durationtype_equal (e_cal_component_period_get_duration (expected),
						e_cal_component_period_get_duration (received));
		break;
	}
}

static void
verify_struct_range_equal (const ECalComponentRange *expected,
			   const ECalComponentRange *received)
{
	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	g_assert_cmpint (e_cal_component_range_get_kind (expected), ==, e_cal_component_range_get_kind (received));

	verify_struct_datetime_equal (e_cal_component_range_get_datetime (expected),
				      e_cal_component_range_get_datetime (received));
}

static void
verify_struct_text_equal (const ECalComponentText *expected,
			  const ECalComponentText *received)
{
	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	g_assert_cmpstr (e_cal_component_text_get_value (expected), ==, e_cal_component_text_get_value (received));
	g_assert_cmpstr (e_cal_component_text_get_altrep (expected), ==, e_cal_component_text_get_altrep (received));
	g_assert_cmpstr (e_cal_component_text_get_language (expected), ==, e_cal_component_text_get_language (received));
}

static void
verify_struct_alarm_repeat_equal (const ECalComponentAlarmRepeat *expected,
				  const ECalComponentAlarmRepeat *received)
{
	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	g_assert_cmpint (e_cal_component_alarm_repeat_get_repetitions (expected), ==, e_cal_component_alarm_repeat_get_repetitions (received));
	verify_ical_durationtype_equal (e_cal_component_alarm_repeat_get_interval (expected),
					e_cal_component_alarm_repeat_get_interval (received));
	g_assert_cmpint (e_cal_component_alarm_repeat_get_interval_seconds (expected), ==, e_cal_component_alarm_repeat_get_interval_seconds (received));
	g_assert_cmpint (e_cal_component_alarm_repeat_get_interval_seconds (expected), ==,
			 i_cal_duration_as_int (e_cal_component_alarm_repeat_get_interval (expected)));
	g_assert_cmpint (e_cal_component_alarm_repeat_get_interval_seconds (received), ==,
			 i_cal_duration_as_int (e_cal_component_alarm_repeat_get_interval (received)));
	g_assert_cmpint (e_cal_component_alarm_repeat_get_interval_seconds (expected), ==,
			 i_cal_duration_as_int (e_cal_component_alarm_repeat_get_interval (received)));
}

static void
verify_struct_alarm_trigger_equal (const ECalComponentAlarmTrigger *expected,
				   const ECalComponentAlarmTrigger *received)
{
	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	g_assert_cmpint (e_cal_component_alarm_trigger_get_kind (expected), ==, e_cal_component_alarm_trigger_get_kind (received));

	switch (e_cal_component_alarm_trigger_get_kind (expected)) {
	case E_CAL_COMPONENT_ALARM_TRIGGER_NONE:
		break;
	case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START:
	case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END:
		verify_ical_durationtype_equal (e_cal_component_alarm_trigger_get_duration (expected),
						e_cal_component_alarm_trigger_get_duration (received));
		break;
	case E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE:
		verify_ical_timetype_equal (e_cal_component_alarm_trigger_get_absolute_time (expected),
					    e_cal_component_alarm_trigger_get_absolute_time (received));
		break;
	}

	verify_struct_parameter_bag_equal (e_cal_component_alarm_trigger_get_parameter_bag (expected),
					   e_cal_component_alarm_trigger_get_parameter_bag (received));
}

static void
verify_struct_alarm_equal (const ECalComponentAlarm *expected,
			   const ECalComponentAlarm *received)
{
	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	g_assert_cmpstr (e_cal_component_alarm_get_uid (expected), ==, e_cal_component_alarm_get_uid (received));
	g_assert_cmpint (e_cal_component_alarm_get_action (expected), ==, e_cal_component_alarm_get_action (received));

	verify_struct_text_equal (e_cal_component_alarm_get_summary (expected),
				  e_cal_component_alarm_get_summary (received));

	verify_struct_text_equal (e_cal_component_alarm_get_description (expected),
				  e_cal_component_alarm_get_description (received));

	verify_struct_alarm_repeat_equal (e_cal_component_alarm_get_repeat (expected),
					  e_cal_component_alarm_get_repeat (received));

	verify_struct_alarm_trigger_equal (e_cal_component_alarm_get_trigger (expected),
					   e_cal_component_alarm_get_trigger (received));

	g_assert_cmpint (e_cal_component_alarm_has_attendees (expected) ? 1 : 0, ==, e_cal_component_alarm_has_attendees (received) ? 1 : 0);

	verify_struct_attendee_list_equal (e_cal_component_alarm_get_attendees (expected),
					   e_cal_component_alarm_get_attendees (received));

	g_assert_cmpint (e_cal_component_alarm_has_attachments (expected) ? 1 : 0, ==, e_cal_component_alarm_has_attachments (received) ? 1 : 0);

	verify_ical_attach_list_equal (e_cal_component_alarm_get_attachments (expected),
				       e_cal_component_alarm_get_attachments (received));

	verify_struct_property_bag_equal (e_cal_component_alarm_get_property_bag (expected),
					  e_cal_component_alarm_get_property_bag (received));

	verify_ical_timetype_equal (e_cal_component_alarm_get_acknowledged (expected),
				    e_cal_component_alarm_get_acknowledged (received));
}

static void
verify_struct_alarm_instance_equal (const ECalComponentAlarmInstance *expected,
				    const ECalComponentAlarmInstance *received)
{
	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	g_assert_cmpstr (e_cal_component_alarm_instance_get_uid (expected), ==, e_cal_component_alarm_instance_get_uid (received));
	g_assert_cmpstr (e_cal_component_alarm_instance_get_rid (expected), ==, e_cal_component_alarm_instance_get_rid (received));
	g_assert_cmpint (e_cal_component_alarm_instance_get_time (expected), ==, e_cal_component_alarm_instance_get_time (received));
	g_assert_cmpint (e_cal_component_alarm_instance_get_occur_start (expected), ==, e_cal_component_alarm_instance_get_occur_start (received));
	g_assert_cmpint (e_cal_component_alarm_instance_get_occur_end (expected), ==, e_cal_component_alarm_instance_get_occur_end (received));
	g_assert_true (e_cal_component_alarm_instance_get_component (expected) == e_cal_component_alarm_instance_get_component (received));
}

static void
verify_struct_alarms_equal (const ECalComponentAlarms *expected,
			    const ECalComponentAlarms *received)
{
	GSList *inst_expected, *inst_received, *link1, *link2;
	const gchar *uid_expected, *uid_received;
	gchar *rid_expected, *rid_received;

	if (!expected) {
		g_assert_null (received);
		return;
	}

	g_assert_nonnull (received);

	uid_expected = e_cal_component_get_uid (e_cal_component_alarms_get_component (expected));
	uid_received = e_cal_component_get_uid (e_cal_component_alarms_get_component (received));
	g_assert_cmpstr (uid_expected, ==, uid_received);

	rid_expected = e_cal_component_get_recurid_as_string (e_cal_component_alarms_get_component (expected));
	rid_received = e_cal_component_get_recurid_as_string (e_cal_component_alarms_get_component (received));
	g_assert_cmpstr (rid_expected, ==, rid_received);
	g_free (rid_expected);
	g_free (rid_received);

	inst_expected = e_cal_component_alarms_get_instances (expected);
	inst_received = e_cal_component_alarms_get_instances (received);

	g_assert_cmpint (g_slist_length (inst_expected), ==, g_slist_length (inst_received));

	for (link1 = inst_expected, link2 = inst_received; link1 && link2; link1 = g_slist_next (link1), link2 = g_slist_next (link2)) {
		verify_struct_alarm_instance_equal (link1->data, link2->data);
	}

	g_assert_true (link1 == link2);
}

static void
test_component_struct_alarm (void)
{
	struct _values {
		const gchar *uid;
		ECalComponentAlarmAction action;
		const gchar *summary;
		const gchar *description;
		gboolean with_repeat;
		gboolean with_trigger;
		gboolean with_attendees;
		gboolean with_attachments;
		gboolean with_properties;
		gboolean with_acknowledged;
	} values[] = {
		{ "1", E_CAL_COMPONENT_ALARM_AUDIO, NULL, NULL, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE },
		{ "2", E_CAL_COMPONENT_ALARM_DISPLAY, NULL, "display text", FALSE, FALSE, FALSE, FALSE, FALSE, TRUE },
		{ "3", E_CAL_COMPONENT_ALARM_EMAIL, "summary text", NULL, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE },
		{ "4", E_CAL_COMPONENT_ALARM_PROCEDURE, NULL, "procedure", FALSE, TRUE, FALSE, FALSE, TRUE, TRUE },
		{ "5", E_CAL_COMPONENT_ALARM_AUDIO, NULL, NULL, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE },
		{ "6", E_CAL_COMPONENT_ALARM_DISPLAY, NULL, "display text", FALSE, FALSE, FALSE, TRUE, TRUE, TRUE },
		{ "7", E_CAL_COMPONENT_ALARM_EMAIL, "summary", "description", TRUE, FALSE, TRUE, FALSE, TRUE, FALSE },
		{ "8", E_CAL_COMPONENT_ALARM_PROCEDURE, NULL, "procedure", TRUE, TRUE, TRUE, TRUE, TRUE, TRUE }
	};
	gint ii, nth_summary = 0, nth_description = 0, nth_repeat = 0, nth_trigger = 0, nth_attendees = 0, nth_attachments = 0, nth_properties = 0, nth_acknowledged = 0;

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		ECalComponentAlarm *expected, *received;
		ICalComponent *icalcomp;

		expected = e_cal_component_alarm_new ();
		e_cal_component_alarm_set_uid (expected, values[ii].uid);
		e_cal_component_alarm_set_action (expected, values[ii].action);

		if (values[ii].summary) {
			nth_summary++;

			if ((nth_summary & 1) != 0) {
				ECalComponentText *txt;

				txt = e_cal_component_text_new (values[ii].summary, "alt-representation");
				e_cal_component_alarm_set_summary (expected, txt);
				e_cal_component_text_free (txt);
			} else {
				e_cal_component_alarm_take_summary (expected, e_cal_component_text_new (values[ii].summary, NULL));
			}
		}

		if (values[ii].description) {
			nth_description++;

			if ((nth_description & 1) != 0) {
				ECalComponentText *txt;

				txt = e_cal_component_text_new (values[ii].description, "alt-representation");
				e_cal_component_alarm_set_description (expected, txt);
				e_cal_component_text_free (txt);
			} else {
				e_cal_component_alarm_take_description (expected, e_cal_component_text_new (values[ii].description, NULL));
			}
		}

		if (values[ii].with_repeat) {
			nth_repeat++;

			if ((nth_repeat & 1) != 0) {
				ECalComponentAlarmRepeat *rpt;

				rpt = e_cal_component_alarm_repeat_new_seconds (3, 6);
				e_cal_component_alarm_set_repeat (expected, rpt);
				e_cal_component_alarm_repeat_free (rpt);
			} else {
				e_cal_component_alarm_take_repeat (expected, e_cal_component_alarm_repeat_new_seconds (9, 12));
			}
		}

		if (values[ii].with_trigger) {
			nth_trigger++;

			if ((nth_trigger & 1) != 0) {
				ECalComponentAlarmTrigger *trg;
				ICalTime *tt;

				tt = i_cal_time_new_from_string ("20201030T102030");
				g_assert_nonnull (tt);

				trg = e_cal_component_alarm_trigger_new_absolute (tt);
				e_cal_component_alarm_set_trigger (expected, trg);
				e_cal_component_alarm_trigger_free (trg);
				g_object_unref (tt);
			} else {
				ICalTime *tt;

				tt = i_cal_time_new_from_string ("21211129T122233");
				g_assert_nonnull (tt);

				e_cal_component_alarm_take_trigger (expected, e_cal_component_alarm_trigger_new_absolute (tt));

				g_object_unref (tt);
			}
		}

		if (values[ii].with_attendees) {
			GSList *attendees = NULL;
			gint jj;

			nth_attendees++;

			for (jj = 0; jj < nth_attendees; jj++) {
				ECalComponentAttendee *att;
				gchar *value;

				value = g_strdup_printf ("mailto:attendee-%d", jj);

				att = e_cal_component_attendee_new ();
				e_cal_component_attendee_set_value (att, value);
				g_free (value);

				attendees = g_slist_prepend (attendees, att);
			}

			if ((nth_attendees & 1) != 0) {
				e_cal_component_alarm_set_attendees (expected, attendees);
				g_slist_free_full (attendees, e_cal_component_attendee_free);
			} else {
				e_cal_component_alarm_take_attendees (expected, attendees);
			}
		}

		if (values[ii].with_attachments) {
			GSList *attachments = NULL;
			gint jj;

			nth_attachments++;

			for (jj = 0; jj < nth_attachments; jj++) {
				ICalAttach *attach;
				gchar *value;

				if ((jj & 1) != 0) {
					value = g_strdup_printf ("https://www.no.where/link%d", jj);
					attach = i_cal_attach_new_from_url (value);
				} else {
					value = g_strdup_printf ("Content*%d", jj);
					attach = i_cal_attach_new_from_data (value, NULL, NULL);
				}

				g_free (value);

				g_assert_nonnull (attach);

				attachments = g_slist_prepend (attachments, attach);
			}

			if ((nth_attachments & 1) != 0) {
				e_cal_component_alarm_set_attachments (expected, attachments);
				g_slist_free_full (attachments, g_object_unref);
			} else {
				e_cal_component_alarm_take_attachments (expected, attachments);
			}
		}

		if (values[ii].with_properties) {
			ECalComponentPropertyBag *bag;
			gint jj;

			nth_properties++;

			bag = e_cal_component_alarm_get_property_bag (expected);

			g_assert_nonnull (bag);

			for (jj = nth_properties; jj > 0; jj--) {
				ICalProperty *prop;

				if (jj == 0) {
					prop = i_cal_property_new_url ("https://www.gnome.org");
				} else if (jj == 1) {
					prop = i_cal_property_new_voter ("mailto:user@no.where");
				} else {
					gchar *x_name;

					x_name = g_strdup_printf ("X-CUSTOM-PROP-%d", jj);
					prop = i_cal_property_new_x (x_name + 2);
					i_cal_property_set_x_name (prop, x_name);
					g_free (x_name);
				}

				g_assert_nonnull (prop);

				e_cal_component_property_bag_take (bag, prop);
			}

			g_assert_cmpint (e_cal_component_property_bag_get_count (bag), ==, nth_properties);
		}

		if (values[ii].with_acknowledged) {
			ICalTime *tt;

			nth_acknowledged++;

			tt = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());

			if ((nth_acknowledged & 1) != 0) {
				e_cal_component_alarm_set_acknowledged (expected, tt);
				g_object_unref (tt);
			} else {
				e_cal_component_alarm_take_acknowledged (expected, tt);
			}

			if (nth_acknowledged == 3)
				e_cal_component_alarm_set_acknowledged (expected, NULL);
		}

		received = e_cal_component_alarm_copy (expected);
		verify_struct_alarm_equal (expected, received);
		e_cal_component_alarm_free (received);

		icalcomp = e_cal_component_alarm_get_as_component (expected);
		g_assert_nonnull (icalcomp);
		received = e_cal_component_alarm_new_from_component (icalcomp);
		verify_struct_alarm_equal (expected, received);
		e_cal_component_alarm_free (received);
		g_object_unref (icalcomp);

		icalcomp = i_cal_component_new_valarm ();
		g_assert_nonnull (icalcomp);
		e_cal_component_alarm_fill_component (expected, icalcomp);
		received = e_cal_component_alarm_new ();
		e_cal_component_alarm_set_from_component (received, icalcomp);
		g_object_unref (icalcomp);
		verify_struct_alarm_equal (expected, received);
		e_cal_component_alarm_free (received);

		e_cal_component_alarm_free (expected);
	}

	g_assert_cmpint (nth_summary, >, 1);
	g_assert_cmpint (nth_description, >, 1);
	g_assert_cmpint (nth_repeat, >, 1);
	g_assert_cmpint (nth_trigger, >, 1);
	g_assert_cmpint (nth_attendees, >, 1);
	g_assert_cmpint (nth_attachments, >, 1);
	g_assert_cmpint (nth_properties, >, 1);
	g_assert_cmpint (nth_acknowledged, >, 3);
}

static void
test_component_struct_alarms (void)
{
	ECalComponent *comp;
	ECalComponentAlarms *expected, *received;
	ECalComponentAlarmInstance *instance;
	GSList *instances = NULL;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	e_cal_component_set_uid (comp, "123");

	expected = e_cal_component_alarms_new (comp);
	g_assert_nonnull (e_cal_component_alarms_get_component (expected));
	g_assert_cmpstr ("123", ==, e_cal_component_get_uid (e_cal_component_alarms_get_component (expected)));

	received = e_cal_component_alarms_copy (expected);
	verify_struct_alarms_equal (expected, received);
	e_cal_component_alarms_free (received);

	instance = e_cal_component_alarm_instance_new ("1", (time_t) 1, (time_t) 2, (time_t) 3);
	e_cal_component_alarms_add_instance (expected, instance);

	g_assert_cmpint (1, ==, g_slist_length (e_cal_component_alarms_get_instances (expected)));
	verify_struct_alarm_instance_equal (instance, e_cal_component_alarms_get_instances (expected)->data);
	e_cal_component_alarm_instance_free (instance);

	g_object_unref (comp);

	received = e_cal_component_alarms_copy (expected);
	verify_struct_alarms_equal (expected, received);
	e_cal_component_alarms_free (received);

	instance = e_cal_component_alarm_instance_new ("2", (time_t) 2, (time_t) 3, (time_t) 4);
	e_cal_component_alarm_instance_set_rid (instance, "r1");
	e_cal_component_alarms_take_instance (expected, instance);
	g_assert_cmpint (2, ==, g_slist_length (e_cal_component_alarms_get_instances (expected)));

	received = e_cal_component_alarms_copy (expected);
	verify_struct_alarms_equal (expected, received);
	e_cal_component_alarms_free (received);

	g_assert_true (!e_cal_component_alarms_remove_instance (expected, GINT_TO_POINTER (123)));
	g_assert_true (e_cal_component_alarms_remove_instance (expected, instance));

	received = e_cal_component_alarms_copy (expected);
	verify_struct_alarms_equal (expected, received);
	e_cal_component_alarms_free (received);

	e_cal_component_alarms_set_instances (expected, NULL);
	g_assert_cmpint (0, ==, g_slist_length (e_cal_component_alarms_get_instances (expected)));

	received = e_cal_component_alarms_copy (expected);
	verify_struct_alarms_equal (expected, received);
	e_cal_component_alarms_free (received);

	instances = NULL;
	instances = g_slist_prepend (instances, e_cal_component_alarm_instance_new ("3", (time_t) 3, (time_t) 4, (time_t) 5));
	instances = g_slist_prepend (instances, e_cal_component_alarm_instance_new ("4", (time_t) 4, (time_t) 5, (time_t) 6));
	instances = g_slist_prepend (instances, e_cal_component_alarm_instance_new ("5", (time_t) 5, (time_t) 6, (time_t) 7));

	e_cal_component_alarms_set_instances (expected, instances);
	g_assert_cmpint (3, ==, g_slist_length (e_cal_component_alarms_get_instances (expected)));
	g_slist_free_full (instances, e_cal_component_alarm_instance_free);

	received = e_cal_component_alarms_copy (expected);
	verify_struct_alarms_equal (expected, received);
	e_cal_component_alarms_free (received);

	instances = NULL;
	instances = g_slist_prepend (instances, e_cal_component_alarm_instance_new ("6", (time_t) 6, (time_t) 7, (time_t) 8));
	instances = g_slist_prepend (instances, e_cal_component_alarm_instance_new ("7", (time_t) 7, (time_t) 8, (time_t) 9));

	e_cal_component_alarms_take_instances (expected, instances);
	g_assert_cmpint (2, ==, g_slist_length (e_cal_component_alarms_get_instances (expected)));

	received = e_cal_component_alarms_copy (expected);
	verify_struct_alarms_equal (expected, received);
	e_cal_component_alarms_free (received);

	e_cal_component_alarms_free (expected);
}

static void
test_component_struct_alarm_instance (void)
{
	struct _values {
		const gchar *uid;
		const gchar *rid;
		gboolean with_component;
		gint instance_time;
		gint occur_start;
		gint occur_end;
	} values[] = {
		{ "1", NULL, TRUE,  1, 2, 3 },
		{ "2", "r1", FALSE, 2, 3, 4 },
		{ "3", "r2", TRUE,  3, 4, 6 },
		{ "4", NULL, FALSE, 4, 5, 6 },
		{ "5", "r3", FALSE, 5, 6, 7 }
	};
	ECalComponent *comp;
	gint ii;

	comp = e_cal_component_new ();

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		ECalComponentAlarmInstance *expected, *received;

		if ((ii & 1) != 0) {
			expected = e_cal_component_alarm_instance_new (values[ii].uid,
								       (time_t) values[ii].instance_time,
								       (time_t) values[ii].occur_start,
								       (time_t) values[ii].occur_end);
		} else {
			expected = e_cal_component_alarm_instance_new ("X", (time_t) 100, (time_t) 200, (time_t) 300);
			e_cal_component_alarm_instance_set_uid (expected, values[ii].uid);
			e_cal_component_alarm_instance_set_time (expected, (time_t) values[ii].instance_time);
			e_cal_component_alarm_instance_set_occur_start (expected, (time_t) values[ii].occur_start);
			e_cal_component_alarm_instance_set_occur_end (expected, (time_t) values[ii].occur_end);
		}

		e_cal_component_alarm_instance_set_rid (expected, values[ii].rid);

		if (values[ii].with_component)
			e_cal_component_alarm_instance_set_component (expected, comp);
		else if (ii < 3)
			e_cal_component_alarm_instance_set_component (expected, NULL);

		g_assert_nonnull (expected);

		g_assert_cmpstr (e_cal_component_alarm_instance_get_uid (expected), ==, values[ii].uid);
		g_assert_cmpstr (e_cal_component_alarm_instance_get_rid (expected), ==, values[ii].rid);
		g_assert_cmpint (e_cal_component_alarm_instance_get_time (expected), ==, (time_t) values[ii].instance_time);
		g_assert_cmpint (e_cal_component_alarm_instance_get_occur_start (expected), ==, (time_t) values[ii].occur_start);
		g_assert_cmpint (e_cal_component_alarm_instance_get_occur_end (expected), ==, (time_t) values[ii].occur_end);

		if (values[ii].with_component)
			g_assert_true (e_cal_component_alarm_instance_get_component (expected) == comp);
		else
			g_assert_null (e_cal_component_alarm_instance_get_component (expected));

		received = e_cal_component_alarm_instance_copy (expected);
		verify_struct_alarm_instance_equal (expected, received);
		e_cal_component_alarm_instance_free (received);

		if (values[ii].with_component && ii < 3) {
			e_cal_component_alarm_instance_set_component (expected, NULL);
			g_assert_null (e_cal_component_alarm_instance_get_component (expected));

			received = e_cal_component_alarm_instance_copy (expected);
			verify_struct_alarm_instance_equal (expected, received);
			e_cal_component_alarm_instance_free (received);
		}

		e_cal_component_alarm_instance_free (expected);
	}

	g_assert_cmpint (ii, >, 1);

	g_clear_object (&comp);
}

static void
test_component_add_params_to_bag (ECalComponentParameterBag *bag,
				  gint n_params)
{
	gint ii;

	g_assert_nonnull (bag);

	for (ii = 0; ii < n_params; ii++) {
		ICalParameter *param;

		if (ii == 0) {
			param = i_cal_parameter_new_local (I_CAL_LOCAL_TRUE);
		} else if (ii == 1) {
			param = i_cal_parameter_new_localize ("en_US");
		} else {
			gchar *x_name;

			x_name = g_strdup_printf ("X-CUSTOM-PARAM-%d", ii);
			param = i_cal_parameter_new_x (x_name + 2);
			i_cal_parameter_set_xname (param, x_name);
			g_free (x_name);
		}

		g_assert_nonnull (param);

		e_cal_component_parameter_bag_take (bag, param);
	}

	g_assert_cmpint (e_cal_component_parameter_bag_get_count (bag), ==, n_params);
}

static void
test_component_struct_alarm_repeat (void)
{
	struct _values {
		gint repetitions;
		gint interval;
	} values[] = {
		{ 10, 20 },
		{ 30, 40 },
		{ 50, 60 },
		{ 70, 80 },
		{ 90, 55 }
	};
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		ECalComponentAlarmRepeat *expected, *received;
		ICalDuration *dur;

		if ((ii % 4) == 0) {
			dur = i_cal_duration_new_from_int (values[ii].interval);
			expected = e_cal_component_alarm_repeat_new (values[ii].repetitions, dur);
			g_object_unref (dur);
		} else if ((ii % 4) == 1) {
			expected = e_cal_component_alarm_repeat_new_seconds (values[ii].repetitions, values[ii].interval);
		} else if ((ii % 4) == 2) {
			expected = e_cal_component_alarm_repeat_new_seconds (1000, 2000);
			e_cal_component_alarm_repeat_set_repetitions (expected, values[ii].repetitions);
			e_cal_component_alarm_repeat_set_interval_seconds (expected, values[ii].interval);
		} else {
			dur = i_cal_duration_new_from_int (values[ii].interval);

			expected = e_cal_component_alarm_repeat_new_seconds (1000, 2000);
			e_cal_component_alarm_repeat_set_repetitions (expected, values[ii].repetitions);
			e_cal_component_alarm_repeat_set_interval (expected, dur);

			g_object_unref (dur);
		}

		g_assert_nonnull (expected);

		dur = i_cal_duration_new_from_int (values[ii].interval);
		g_assert_nonnull (dur);
		g_assert_cmpint (e_cal_component_alarm_repeat_get_repetitions (expected), ==, values[ii].repetitions);
		g_assert_cmpint (e_cal_component_alarm_repeat_get_interval_seconds (expected), ==, values[ii].interval);
		verify_ical_durationtype_equal (dur, e_cal_component_alarm_repeat_get_interval (expected));
		g_object_unref (dur);

		received = e_cal_component_alarm_repeat_copy (expected);
		verify_struct_alarm_repeat_equal (expected, received);
		e_cal_component_alarm_repeat_free (received);

		e_cal_component_alarm_repeat_free (expected);
	}

	g_assert_cmpint (ii, >, 4);
}

static void
test_component_struct_alarm_trigger (void)
{
	struct _values {
		ECalComponentAlarmTriggerKind kind;
		gint duration;
		const gchar *abs_time;
	} values[] = {
		{ E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START, -100, NULL },
		{ E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE, 0, "21090807T001122Z" },
		{ E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END, 200, NULL },
		{ E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE, 0, "21090807T213243Z" }
	};
	gint ii, set_kind;

	for (set_kind = 0; set_kind < 3; set_kind++) {
		for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
			ECalComponentAlarmTrigger *expected = NULL, *received;
			ICalDuration *dur;
			ICalTime *tt;
			ICalProperty *prop;

			if (set_kind == 0) {
				/* nothing, create it as it should be */
			} else if (set_kind == 1) {
				dur = i_cal_duration_new_from_int (33);
				expected = e_cal_component_alarm_trigger_new_relative (E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END, dur);
				g_object_unref (dur);

				g_assert_nonnull (expected);
			} else if (set_kind == 2) {
				tt = i_cal_time_new_today ();
				expected = e_cal_component_alarm_trigger_new_absolute (tt);
				g_object_unref (tt);

				g_assert_nonnull (expected);
			} else {
				g_assert_not_reached ();
			}

			if (expected) {
				if (values[ii].kind == e_cal_component_alarm_trigger_get_kind (expected)) {
					if (values[ii].kind == E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE) {
						tt = i_cal_time_new_from_string (values[ii].abs_time);
						e_cal_component_alarm_trigger_set_absolute_time (expected, tt);
						g_object_unref (tt);
					} else {
						dur = i_cal_duration_new_from_int (values[ii].duration);
						e_cal_component_alarm_trigger_set_kind (expected, values[ii].kind);
						e_cal_component_alarm_trigger_set_duration (expected, dur);
						g_object_unref (dur);
					}
				} else if (values[ii].kind == E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE) {
					tt = i_cal_time_new_from_string (values[ii].abs_time);
					e_cal_component_alarm_trigger_set_absolute (expected, tt);
					g_object_unref (tt);
				} else {
					dur = i_cal_duration_new_from_int (values[ii].duration);
					e_cal_component_alarm_trigger_set_relative (expected, values[ii].kind, dur);
					g_object_unref (dur);
				}
			} else {
				if (values[ii].kind == E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE) {
					tt = i_cal_time_new_from_string (values[ii].abs_time);
					expected = e_cal_component_alarm_trigger_new_absolute (tt);
					g_assert_nonnull (expected);
					g_assert_nonnull (e_cal_component_alarm_trigger_get_absolute_time (expected));
					verify_ical_timetype_equal (tt, e_cal_component_alarm_trigger_get_absolute_time (expected));
					g_object_unref (tt);
				} else {
					dur = i_cal_duration_new_from_int (values[ii].duration);
					expected = e_cal_component_alarm_trigger_new_relative (values[ii].kind, dur);
					g_assert_nonnull (expected);
					g_object_unref (dur);
				}
			}

			g_assert_nonnull (expected);

			if (set_kind == 1)
				test_component_add_params_to_bag (e_cal_component_alarm_trigger_get_parameter_bag (expected), ii + 1);

			g_assert_cmpint (values[ii].kind, ==, e_cal_component_alarm_trigger_get_kind (expected));
			if (values[ii].kind == E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE) {
				tt = i_cal_time_new_from_string (values[ii].abs_time);
				g_assert_nonnull (tt);
				verify_ical_timetype_equal (tt, e_cal_component_alarm_trigger_get_absolute_time (expected));
				g_object_unref (tt);
			} else {
				dur = i_cal_duration_new_from_int (values[ii].duration);
				g_assert_nonnull (dur);
				verify_ical_durationtype_equal (dur, e_cal_component_alarm_trigger_get_duration (expected));
				g_object_unref (dur);
			}

			received = e_cal_component_alarm_trigger_copy (expected);
			verify_struct_alarm_trigger_equal (expected, received);
			e_cal_component_alarm_trigger_free (received);

			prop = e_cal_component_alarm_trigger_get_as_property (expected);
			g_assert_nonnull (prop);

			received = e_cal_component_alarm_trigger_new_from_property (prop);
			verify_struct_alarm_trigger_equal (expected, received);
			e_cal_component_alarm_trigger_free (received);

			dur = i_cal_duration_new_from_int (33);
			received = e_cal_component_alarm_trigger_new_relative (E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END, dur);
			g_object_unref (dur);
			e_cal_component_alarm_trigger_set_from_property (received, prop);
			verify_struct_alarm_trigger_equal (expected, received);
			e_cal_component_alarm_trigger_free (received);

			tt = i_cal_time_new_today ();
			received = e_cal_component_alarm_trigger_new_absolute (tt);
			g_object_unref (tt);
			e_cal_component_alarm_trigger_set_from_property (received, prop);
			verify_struct_alarm_trigger_equal (expected, received);
			e_cal_component_alarm_trigger_free (received);

			g_object_unref (prop);

			prop = i_cal_property_new (I_CAL_TRIGGER_PROPERTY);
			e_cal_component_alarm_trigger_fill_property (expected, prop);
			received = e_cal_component_alarm_trigger_new_from_property (prop);
			verify_struct_alarm_trigger_equal (expected, received);
			e_cal_component_alarm_trigger_free (received);
			g_object_unref (prop);

			e_cal_component_alarm_trigger_free (expected);
		}
	}
}

static void
test_component_struct_attendee (void)
{
	struct _values {
		const gchar *value;
		const gchar *member;
		ICalParameterCutype cutype;
		ICalParameterRole role;
		ICalParameterPartstat partstat;
		gboolean rsvp;
		const gchar *delegatedfrom;
		const gchar *delegatedto;
		const gchar *sentby;
		const gchar *cn;
		const gchar *language;
	} values[] = {
		{ "mailto:att1",
		   "member",
		   I_CAL_CUTYPE_INDIVIDUAL,
		   I_CAL_ROLE_CHAIR,
		   I_CAL_PARTSTAT_NEEDSACTION,
		   FALSE,
		   "mailto:delgfrom",
		   "mailto:delgto",
		   "mailto:sentby",
		   "First attendee",
		   "en_US" },
		{ "mailto:room",
		   NULL,
		   I_CAL_CUTYPE_ROOM,
		   I_CAL_ROLE_REQPARTICIPANT,
		   I_CAL_PARTSTAT_ACCEPTED,
		   FALSE,
		   NULL,
		   NULL,
		   NULL,
		   "Meeting room",
		   NULL },
		{ "mailto:att2",
		   NULL,
		   I_CAL_CUTYPE_INDIVIDUAL,
		   I_CAL_ROLE_REQPARTICIPANT,
		   I_CAL_PARTSTAT_TENTATIVE,
		   TRUE,
		   NULL,
		   NULL,
		   NULL,
		   NULL,
		   "en_US" }
	};
	gint ii, set_kind;

	for (set_kind = 0; set_kind < 4; set_kind++) {
		for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
			ECalComponentAttendee *expected = NULL, *received;
			ICalProperty *prop;

			if (set_kind == 1) {
				expected = e_cal_component_attendee_new ();
			} else if (set_kind == 2) {
				expected = e_cal_component_attendee_new_full ("value", "member",
					I_CAL_CUTYPE_INDIVIDUAL,
					I_CAL_ROLE_CHAIR,
					I_CAL_PARTSTAT_DECLINED,
					TRUE,
					"delegatedfrom", "delegatedto",
					"sentby", "cn", "language");
			}

			if (expected) {
				e_cal_component_attendee_set_value (expected, values[ii].value);
				e_cal_component_attendee_set_member (expected, values[ii].member);
				e_cal_component_attendee_set_cutype (expected, values[ii].cutype);
				e_cal_component_attendee_set_role (expected, values[ii].role);
				e_cal_component_attendee_set_partstat (expected, values[ii].partstat);
				e_cal_component_attendee_set_rsvp (expected, values[ii].rsvp);
				e_cal_component_attendee_set_delegatedfrom (expected, values[ii].delegatedfrom);
				e_cal_component_attendee_set_delegatedto (expected, values[ii].delegatedto);
				e_cal_component_attendee_set_sentby (expected, values[ii].sentby);
				e_cal_component_attendee_set_cn (expected, values[ii].cn);
				e_cal_component_attendee_set_language (expected, values[ii].language);
			} else {
				expected = e_cal_component_attendee_new_full (values[ii].value,
					values[ii].member,
					values[ii].cutype,
					values[ii].role,
					values[ii].partstat,
					values[ii].rsvp,
					values[ii].delegatedfrom,
					values[ii].delegatedto,
					values[ii].sentby,
					values[ii].cn,
					values[ii].language);
			}

			g_assert_nonnull (expected);

			if (set_kind == 1)
				test_component_add_params_to_bag (e_cal_component_attendee_get_parameter_bag (expected), ii + 1);

			g_assert_cmpstr (e_cal_component_attendee_get_value (expected), ==, values[ii].value);
			g_assert_cmpstr (e_cal_component_attendee_get_member (expected), ==, values[ii].member);
			g_assert_cmpint (e_cal_component_attendee_get_cutype (expected), ==, values[ii].cutype);
			g_assert_cmpint (e_cal_component_attendee_get_role (expected), ==, values[ii].role);
			g_assert_cmpint (e_cal_component_attendee_get_partstat (expected), ==, values[ii].partstat);
			g_assert_cmpint (e_cal_component_attendee_get_rsvp (expected) ? 1 : 0, ==, values[ii].rsvp ? 1 : 0);
			g_assert_cmpstr (e_cal_component_attendee_get_delegatedfrom (expected), ==, values[ii].delegatedfrom);
			g_assert_cmpstr (e_cal_component_attendee_get_delegatedto (expected), ==, values[ii].delegatedto);
			g_assert_cmpstr (e_cal_component_attendee_get_sentby (expected), ==, values[ii].sentby);
			g_assert_cmpstr (e_cal_component_attendee_get_cn (expected), ==, values[ii].cn);
			g_assert_cmpstr (e_cal_component_attendee_get_language (expected), ==, values[ii].language);

			received = e_cal_component_attendee_copy (expected);
			verify_struct_attendee_equal (expected, received);
			e_cal_component_attendee_free (received);

			prop = e_cal_component_attendee_get_as_property (expected);
			g_assert_nonnull (prop);

			received = e_cal_component_attendee_new_from_property (prop);
			verify_struct_attendee_equal (expected, received);
			e_cal_component_attendee_free (received);

			received = e_cal_component_attendee_new_full ("value", "member",
				I_CAL_CUTYPE_INDIVIDUAL,
				I_CAL_ROLE_CHAIR,
				I_CAL_PARTSTAT_DECLINED,
				TRUE,
				"delegatedfrom", "delegatedto",
				"sentby", "cn", "language");
			e_cal_component_attendee_set_from_property (received, prop);
			verify_struct_attendee_equal (expected, received);
			e_cal_component_attendee_free (received);

			g_object_unref (prop);

			prop = i_cal_property_new (I_CAL_ATTENDEE_PROPERTY);
			e_cal_component_attendee_fill_property (expected, prop);
			received = e_cal_component_attendee_new_from_property (prop);
			verify_struct_attendee_equal (expected, received);
			e_cal_component_attendee_free (received);
			g_object_unref (prop);

			e_cal_component_attendee_free (expected);
		}
	}
}

static void
test_component_struct_datetime (void)
{
	struct _values {
		const gchar *time;
		const gchar *tzid;
	} values[] = {
		{ "20181215T111213Z", NULL },
		{ "20190131T121314", "Europe/Berlin" },
		{ "20200708T010305Z", NULL },
		{ "20211215T101112", "America/New_York" },
		{ "20221110T090807", "UTC" },
		{ "20231009", NULL }
	};
	gint ii, set_kind;

	for (set_kind = 0; set_kind < 4; set_kind++) {
		for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
			ECalComponentDateTime *expected = NULL, *received;
			ICalTime *tt;

			if (set_kind == 2) {
				tt = i_cal_time_new_from_string ("19981019");
				expected = e_cal_component_datetime_new (tt, NULL);
				g_object_unref (tt);
			} else if (set_kind == 3) {
				tt = i_cal_time_new_from_string ("19981019");
				expected = e_cal_component_datetime_new_take (tt, g_strdup ("Unknown"));
			}

			tt = i_cal_time_new_from_string (values[ii].time);
			g_assert_nonnull (tt);

			if (expected) {
				if (((set_kind + ii) % 3) == 0) {
					e_cal_component_datetime_set (expected, tt, values[ii].tzid);
				} else if (((set_kind + ii) % 3) == 1) {
					ICalTime *ttcopy;

					ttcopy = i_cal_time_clone (tt);
					g_assert_nonnull (ttcopy);

					e_cal_component_datetime_take_value (expected, ttcopy);
					e_cal_component_datetime_take_tzid (expected, g_strdup (values[ii].tzid));
				} else {
					e_cal_component_datetime_set_value (expected, tt);
					e_cal_component_datetime_set_tzid (expected, values[ii].tzid);
				}
			} else {
				if (set_kind == 0) {
					expected = e_cal_component_datetime_new (tt, values[ii].tzid);
				} else {
					ICalTime *ttcopy;

					ttcopy = i_cal_time_clone (tt);
					g_assert_nonnull (ttcopy);

					expected = e_cal_component_datetime_new_take (ttcopy, g_strdup (values[ii].tzid));
				}
			}

			g_assert_nonnull (expected);

			verify_ical_timetype_equal (e_cal_component_datetime_get_value (expected), tt);
			g_assert_cmpstr (e_cal_component_datetime_get_tzid (expected), ==, values[ii].tzid);

			g_object_unref (tt);

			received = e_cal_component_datetime_copy (expected);
			verify_struct_datetime_equal (expected, received);
			e_cal_component_datetime_free (received);

			e_cal_component_datetime_free (expected);
		}
	}
}

static void
test_component_struct_id (void)
{
	struct _values {
		const gchar *uid;
		const gchar *rid;
	} values[] = {
		{ "123", "20181215T111213Z" },
		{ "222", NULL },
		{ "333", NULL },
		{ "44", "20211215T101112" }
	};
	gint ii, set_kind;

	for (set_kind = 0; set_kind < 4; set_kind++) {
		for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
			ECalComponentId *expected = NULL, *received;

			if (set_kind == 2) {
				expected = e_cal_component_id_new ("4567", "123");
			} else if (set_kind == 3) {
				expected = e_cal_component_id_new_take (g_strdup ("5678"), g_strdup ("Unknown"));
			}

			if (expected) {
				e_cal_component_id_set_uid (expected, values[ii].uid);
				e_cal_component_id_set_rid (expected, values[ii].rid);
			} else {
				if (set_kind == 0) {
					expected = e_cal_component_id_new (values[ii].uid, values[ii].rid);
				} else {
					expected = e_cal_component_id_new_take (g_strdup (values[ii].uid), g_strdup (values[ii].rid));
				}
			}

			g_assert_nonnull (expected);

			g_assert_cmpstr (e_cal_component_id_get_uid (expected), ==, values[ii].uid);
			g_assert_cmpstr (e_cal_component_id_get_rid (expected), ==, values[ii].rid);

			received = e_cal_component_id_copy (expected);
			verify_struct_id_equal (expected, received);
			g_assert_cmpint (e_cal_component_id_hash (expected), ==, e_cal_component_id_hash (received));
			g_assert_true (e_cal_component_id_equal (expected, received));
			e_cal_component_id_free (received);

			e_cal_component_id_free (expected);
		}
	}
}

static void
test_component_struct_organizer (void)
{
	struct _values {
		const gchar *value;
		const gchar *sentby;
		const gchar *cn;
		const gchar *language;
	} values[] = {
		{ "mailto:org1",
		   "mailto:sentby",
		   "First organizer",
		   "en_US" },
		{ "mailto:room",
		   NULL,
		   "Meeting room",
		   NULL },
		{ "mailto:org2",
		   NULL,
		   NULL,
		   "en_US" }
	};
	gint ii, set_kind;

	for (set_kind = 0; set_kind < 4; set_kind++) {
		for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
			ECalComponentOrganizer *expected = NULL, *received;
			ICalProperty *prop;

			if (set_kind == 1) {
				expected = e_cal_component_organizer_new ();
			} else if (set_kind == 2) {
				expected = e_cal_component_organizer_new_full ("value", "sentby", "cn", "language");
			}

			if (expected) {
				e_cal_component_organizer_set_value (expected, values[ii].value);
				e_cal_component_organizer_set_sentby (expected, values[ii].sentby);
				e_cal_component_organizer_set_cn (expected, values[ii].cn);
				e_cal_component_organizer_set_language (expected, values[ii].language);
			} else {
				expected = e_cal_component_organizer_new_full (values[ii].value,
					values[ii].sentby,
					values[ii].cn,
					values[ii].language);
			}

			g_assert_nonnull (expected);

			if (set_kind == 1)
				test_component_add_params_to_bag (e_cal_component_organizer_get_parameter_bag (expected), ii + 1);

			g_assert_cmpstr (e_cal_component_organizer_get_value (expected), ==, values[ii].value);
			g_assert_cmpstr (e_cal_component_organizer_get_sentby (expected), ==, values[ii].sentby);
			g_assert_cmpstr (e_cal_component_organizer_get_cn (expected), ==, values[ii].cn);
			g_assert_cmpstr (e_cal_component_organizer_get_language (expected), ==, values[ii].language);

			received = e_cal_component_organizer_copy (expected);
			verify_struct_organizer_equal (expected, received);
			e_cal_component_organizer_free (received);

			prop = e_cal_component_organizer_get_as_property (expected);
			g_assert_nonnull (prop);

			received = e_cal_component_organizer_new_from_property (prop);
			verify_struct_organizer_equal (expected, received);
			e_cal_component_organizer_free (received);

			received = e_cal_component_organizer_new_full ("value", "sentby", "cn", "language");
			e_cal_component_organizer_set_from_property (received, prop);
			verify_struct_organizer_equal (expected, received);
			e_cal_component_organizer_free (received);

			g_object_unref (prop);

			prop = i_cal_property_new (I_CAL_ORGANIZER_PROPERTY);
			e_cal_component_organizer_fill_property (expected, prop);
			received = e_cal_component_organizer_new_from_property (prop);
			verify_struct_organizer_equal (expected, received);
			e_cal_component_organizer_free (received);
			g_object_unref (prop);

			e_cal_component_organizer_free (expected);
		}
	}
}

#define X_PARAM_NAME "X-PARAM"
#define X_PARAM_VALUE "xVaLuE"

static gboolean
test_parameter_bag_filter_cb (ICalParameter *param,
			      gpointer user_data)
{
	ICalParameterKind *expected = user_data, kind;
	gint ii;

	g_return_val_if_fail (expected != NULL, FALSE);

	kind = i_cal_parameter_isa (param);

	for (ii = 0; expected[ii] != I_CAL_ANY_PARAMETER; ii++) {
		if (kind == expected[ii])
			return TRUE;
	}

	return FALSE;
}

static void
test_check_parameter_bag (const ECalComponentParameterBag *bag,
			  const ICalParameterKind *expected)
{
	ICalParameter *param;
	gint ii;

	g_assert_nonnull (bag);
	g_assert_nonnull (expected);

	for (ii = 0; expected[ii] != I_CAL_ANY_PARAMETER; ii++) {
		param = e_cal_component_parameter_bag_get (bag, ii);

		g_assert_nonnull (param);
		g_assert_cmpint (i_cal_parameter_isa (param), ==, expected[ii]);
		if (i_cal_parameter_isa (param) == I_CAL_X_PARAMETER) {
			g_assert_cmpstr (i_cal_parameter_get_xname (param), ==, X_PARAM_NAME);
			g_assert_cmpstr (i_cal_parameter_get_xvalue (param), ==, X_PARAM_VALUE);
		}
	}

	/* Out of bounds */
	param = e_cal_component_parameter_bag_get (bag, ii);
	g_assert_null (param);
}

static void
test_component_struct_parameter_bag (void)
{
	const gchar *prop_str =
		"ATTENDEE;CHARSET=utf-8;CN=User;CUTYPE=INDIVIDUAL;" X_PARAM_NAME "=" X_PARAM_VALUE ";LANGUAGE=en-US:mailto:user@no.where";
	ICalParameterKind expected_unfiltered[] = {
		I_CAL_CHARSET_PARAMETER,
		I_CAL_CN_PARAMETER,
		I_CAL_CUTYPE_PARAMETER,
		I_CAL_X_PARAMETER,
		I_CAL_LANGUAGE_PARAMETER,
		I_CAL_ANY_PARAMETER /* sentinel */
	},
	expected_filtered[] = {
		I_CAL_CN_PARAMETER,
		I_CAL_CUTYPE_PARAMETER,
		I_CAL_X_PARAMETER,
		I_CAL_ANY_PARAMETER /* sentinel */
	};
	ICalProperty *prop;
	ICalParameter *param;
	ECalComponentParameterBag *bag, *bag2;
	gint ii;

	prop = i_cal_property_new_from_string (prop_str);
	g_assert_nonnull (prop);
	g_assert_cmpint (i_cal_property_count_parameters (prop), ==, 5);

	bag = e_cal_component_parameter_bag_new ();
	g_assert_nonnull (bag);
	e_cal_component_parameter_bag_set_from_property (bag, prop, NULL, NULL);
	g_assert_cmpint (e_cal_component_parameter_bag_get_count (bag), ==, G_N_ELEMENTS (expected_unfiltered) - 1);
	test_check_parameter_bag (bag, expected_unfiltered);

	bag2 = e_cal_component_parameter_bag_copy (bag);
	g_assert_nonnull (bag2);
	g_assert_cmpint (e_cal_component_parameter_bag_get_count (bag2), ==, G_N_ELEMENTS (expected_unfiltered) - 1);
	test_check_parameter_bag (bag2, expected_unfiltered);
	e_cal_component_parameter_bag_free (bag2);
	e_cal_component_parameter_bag_free (bag);

	bag = e_cal_component_parameter_bag_new_from_property (prop, NULL, NULL);
	g_assert_nonnull (bag);
	g_assert_cmpint (e_cal_component_parameter_bag_get_count (bag), ==, G_N_ELEMENTS (expected_unfiltered) - 1);
	test_check_parameter_bag (bag, expected_unfiltered);
	e_cal_component_parameter_bag_free (bag);

	bag = e_cal_component_parameter_bag_new ();
	g_assert_nonnull (bag);
	e_cal_component_parameter_bag_set_from_property (bag, prop, test_parameter_bag_filter_cb, expected_filtered);
	g_assert_cmpint (e_cal_component_parameter_bag_get_count (bag), ==, G_N_ELEMENTS (expected_filtered) - 1);
	test_check_parameter_bag (bag, expected_filtered);
	e_cal_component_parameter_bag_free (bag);

	bag = e_cal_component_parameter_bag_new_from_property (prop, test_parameter_bag_filter_cb, expected_filtered);
	g_assert_nonnull (bag);
	g_assert_cmpint (e_cal_component_parameter_bag_get_count (bag), ==, G_N_ELEMENTS (expected_filtered) - 1);
	test_check_parameter_bag (bag, expected_filtered);

	g_object_unref (prop);

	prop = i_cal_property_new (I_CAL_COMMENT_PROPERTY);
	g_assert_nonnull (prop);
	e_cal_component_parameter_bag_fill_property (bag, prop);
	g_assert_cmpint (i_cal_property_count_parameters (prop), ==, e_cal_component_parameter_bag_get_count (bag));
	g_object_unref (prop);

	bag2 = e_cal_component_parameter_bag_copy (bag);

	while (e_cal_component_parameter_bag_get_count (bag) > 1) {
		e_cal_component_parameter_bag_remove (bag, 1);
	}

	e_cal_component_parameter_bag_assign (bag2, bag);
	g_assert_cmpint (e_cal_component_parameter_bag_get_count (bag), ==, e_cal_component_parameter_bag_get_count (bag2));
	e_cal_component_parameter_bag_free (bag2);

	prop = i_cal_property_new (I_CAL_ATTENDEE_PROPERTY);
	g_assert_nonnull (prop);
	e_cal_component_parameter_bag_fill_property (bag, prop);
	g_assert_cmpint (i_cal_property_count_parameters (prop), ==, 1);
	g_object_unref (prop);

	e_cal_component_parameter_bag_clear (bag);

	prop = i_cal_property_new (I_CAL_ATTENDEE_PROPERTY);
	g_assert_nonnull (prop);
	e_cal_component_parameter_bag_fill_property (bag, prop);
	g_assert_cmpint (i_cal_property_count_parameters (prop), ==, 0);
	g_object_unref (prop);

	param = i_cal_parameter_new_cn ("234");
	e_cal_component_parameter_bag_add (bag, param);
	g_object_unref (param);

	param = i_cal_parameter_new_cutype (I_CAL_CUTYPE_ROOM);
	e_cal_component_parameter_bag_take (bag, param);

	g_assert_cmpint (e_cal_component_parameter_bag_get_count (bag), ==, 2);

	for (ii = 0; ii < 2; ii++) {
		ICalParameter *param2;

		param2 = e_cal_component_parameter_bag_get (bag, ii);
		if (ii == 0) {
			g_assert_true (param != param2);
			g_assert_cmpint (i_cal_parameter_isa (param2), ==, I_CAL_CN_PARAMETER);
			g_assert_cmpstr (i_cal_parameter_get_cn (param2), ==, "234");
		} else {
			g_assert_true (param == param2);
			g_assert_cmpint (i_cal_parameter_isa (param2), ==, I_CAL_CUTYPE_PARAMETER);
			g_assert_cmpint (i_cal_parameter_get_cutype (param2), ==, I_CAL_CUTYPE_ROOM);
		}
	}

	prop = i_cal_property_new (I_CAL_DESCRIPTION_PROPERTY);
	g_assert_nonnull (prop);
	e_cal_component_parameter_bag_fill_property (bag, prop);
	g_assert_cmpint (i_cal_property_count_parameters (prop), ==, e_cal_component_parameter_bag_get_count (bag));
	g_object_unref (prop);

	e_cal_component_parameter_bag_clear (bag);

	e_cal_component_parameter_bag_take (bag, i_cal_parameter_new_cutype (I_CAL_CUTYPE_ROOM));
	e_cal_component_parameter_bag_take (bag, i_cal_parameter_new_cn ("111"));
	e_cal_component_parameter_bag_take (bag, i_cal_parameter_new_cutype (I_CAL_CUTYPE_ROOM));
	e_cal_component_parameter_bag_take (bag, i_cal_parameter_new_cutype (I_CAL_CUTYPE_RESOURCE));

	g_assert_cmpint (e_cal_component_parameter_bag_get_count (bag), ==, 4);
	g_assert_cmpint (e_cal_component_parameter_bag_get_first_by_kind (bag, I_CAL_CUTYPE_PARAMETER), ==, 0);
	g_assert_cmpint (e_cal_component_parameter_bag_get_first_by_kind (bag, I_CAL_CN_PARAMETER), ==, 1);
	g_assert_cmpint (e_cal_component_parameter_bag_remove_by_kind (bag, I_CAL_TZID_PARAMETER, FALSE), ==, 0);
	g_assert_cmpint (e_cal_component_parameter_bag_get_count (bag), ==, 4);
	g_assert_cmpint (e_cal_component_parameter_bag_remove_by_kind (bag, I_CAL_TZID_PARAMETER, TRUE), ==, 0);
	g_assert_cmpint (e_cal_component_parameter_bag_get_count (bag), ==, 4);
	g_assert_cmpint (e_cal_component_parameter_bag_remove_by_kind (bag, I_CAL_CUTYPE_PARAMETER, FALSE), ==, 1);
	g_assert_cmpint (e_cal_component_parameter_bag_get_count (bag), ==, 3);
	g_assert_cmpint (e_cal_component_parameter_bag_get_first_by_kind (bag, I_CAL_CN_PARAMETER), ==, 0);
	g_assert_cmpint (e_cal_component_parameter_bag_get_first_by_kind (bag, I_CAL_CUTYPE_PARAMETER), ==, 1);
	g_assert_cmpint (e_cal_component_parameter_bag_remove_by_kind (bag, I_CAL_CUTYPE_PARAMETER, TRUE), ==, 2);
	g_assert_cmpint (e_cal_component_parameter_bag_get_count (bag), ==, 1);
	g_assert_cmpint (e_cal_component_parameter_bag_get_first_by_kind (bag, I_CAL_CN_PARAMETER), ==, 0);
	g_assert_cmpint (e_cal_component_parameter_bag_get_first_by_kind (bag, I_CAL_CUTYPE_PARAMETER), >=, e_cal_component_parameter_bag_get_count (bag));

	e_cal_component_parameter_bag_free (bag);
}

#undef X_PARAM_NAME
#undef X_PARAM_VALUE

static void
test_component_struct_period (void)
{
	struct _values {
		const gchar *start;
		const gchar *end;
		gint duration;
	} values[] = {
		{ "20181215T111213Z", "20181215T121314Z", -1 },
		{ "20190131T121314Z", NULL, 10 * 24 * 60 * 60 },
		{ "20200708T010305Z", NULL, 2 * 60 * 60 },
		{ "20211215T101112", "20211215T121314", -1 },
		{ "20221110T090807", "20221110T100908", -1 },
		{ "20231009", "20241110", -1 },
		{ "20240908T070605", NULL, -1 },
		{ "20250807", NULL, -1 }
	};
	gint ii, set_kind, flipflop1 = 0, flipflop2 = 0;

	for (set_kind = 0; set_kind < 6; set_kind++) {
		for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
			ECalComponentPeriod *expected = NULL, *received;
			ICalTime *start, *end = NULL;
			ICalDuration *duration = NULL;

			start = i_cal_time_new_from_string (values[ii].start);
			g_assert_nonnull (start);
			if (values[ii].duration == -1) {
				if (values[ii].end) {
					end = i_cal_time_new_from_string (values[ii].end);
					g_assert_nonnull (end);
				}
			} else {
				duration = i_cal_duration_new_from_int (values[ii].duration);
				g_assert_nonnull (duration);
			}

			if ((set_kind % 3) == 1) {
				ICalTime *ttstart, *ttend;

				ttstart = i_cal_time_new_from_string ("19981019");
				ttend = i_cal_time_new_from_string ("19981019");

				g_assert_nonnull (ttstart);

				expected = e_cal_component_period_new_datetime (ttstart, ttend);

				g_clear_object (&ttstart);
				g_clear_object (&ttend);
			} else if ((set_kind % 3) == 2) {
				ICalTime *ttstart;
				ICalDuration *ttduration;

				ttstart = i_cal_time_new_from_string ("19981019");
				ttduration = i_cal_duration_new_from_int (123456);
				g_assert_nonnull (ttstart);
				g_assert_nonnull (ttduration);

				expected = e_cal_component_period_new_duration (ttstart, ttduration);

				g_clear_object (&ttstart);
				g_clear_object (&ttduration);
			}

			if (expected) {
				if (duration) {
					if (e_cal_component_period_get_kind (expected) == E_CAL_COMPONENT_PERIOD_DURATION) {
						flipflop1++;
						if ((flipflop1 & 1) != 0) {
							e_cal_component_period_set_duration_full (expected, start, duration);
						} else {
							e_cal_component_period_set_start (expected, start);
							e_cal_component_period_set_duration (expected, duration);
						}
					} else {
						e_cal_component_period_set_duration_full (expected, start, duration);
					}
				} else if (e_cal_component_period_get_kind (expected) == E_CAL_COMPONENT_PERIOD_DATETIME) {
					flipflop2++;
					if ((flipflop2 & 1) != 0) {
						e_cal_component_period_set_datetime_full (expected, start, end);
					} else {
						e_cal_component_period_set_start (expected, start);
						e_cal_component_period_set_end (expected, end);
					}
				} else {
					e_cal_component_period_set_datetime_full (expected, start, end);
				}
			} else {
				if (duration)
					expected = e_cal_component_period_new_duration (start, duration);
				else
					expected = e_cal_component_period_new_datetime (start, end);
			}

			g_assert_nonnull (expected);
			verify_ical_timetype_equal (start, e_cal_component_period_get_start (expected));
			if (duration) {
				g_assert_cmpint (e_cal_component_period_get_kind (expected), ==, E_CAL_COMPONENT_PERIOD_DURATION);
				verify_ical_durationtype_equal (duration, e_cal_component_period_get_duration (expected));
			} else {
				g_assert_cmpint (e_cal_component_period_get_kind (expected), ==, E_CAL_COMPONENT_PERIOD_DATETIME);
				verify_ical_timetype_equal (end, e_cal_component_period_get_end (expected));
			}

			g_clear_object (&start);
			g_clear_object (&end);
			g_clear_object (&duration);

			received = e_cal_component_period_copy (expected);
			verify_struct_period_equal (expected, received);
			e_cal_component_period_free (received);

			e_cal_component_period_free (expected);
		}
	}

	g_assert_cmpint (flipflop1, >, 2);
	g_assert_cmpint (flipflop2, >, 2);
}

#define X_PROP_NAME "X-PROP"
#define X_PROP_VALUE "xVaLuE"

static gboolean
test_property_bag_filter_cb (ICalProperty *prop,
			     gpointer user_data)
{
	ICalPropertyKind *expected = user_data, kind;
	gint ii;

	g_return_val_if_fail (expected != NULL, FALSE);

	kind = i_cal_property_isa (prop);

	for (ii = 0; expected[ii] != I_CAL_ANY_PROPERTY; ii++) {
		if (kind == expected[ii])
			return TRUE;
	}

	return FALSE;
}

static void
test_check_property_bag (const ECalComponentPropertyBag *bag,
			 const ICalPropertyKind *expected)
{
	ICalProperty *prop;
	gint ii;

	g_assert_nonnull (bag);
	g_assert_nonnull (expected);

	for (ii = 0; expected[ii] != I_CAL_ANY_PROPERTY; ii++) {
		prop = e_cal_component_property_bag_get (bag, ii);

		g_assert_nonnull (prop);
		g_assert_cmpint (i_cal_property_isa (prop), ==, expected[ii]);
		if (i_cal_property_isa (prop) == I_CAL_X_PROPERTY) {
			g_assert_cmpstr (i_cal_property_get_x_name (prop), ==, X_PROP_NAME);
			g_assert_cmpstr (i_cal_property_get_x (prop), ==, X_PROP_VALUE);
		}
	}

	/* Out of bounds */
	prop = e_cal_component_property_bag_get (bag, ii);
	g_assert_null (prop);
}

static void
test_component_struct_property_bag (void)
{
	const gchar *comp_str =
		"BEGIN:VTODO\r\n"
		"UID:1\r\n"
		"STATUS:CANCELLED\r\n"
		X_PROP_NAME ":" X_PROP_VALUE "\r\n"
		"END:VTODO\r\n";
	ICalPropertyKind expected_unfiltered[] = {
		I_CAL_UID_PROPERTY,
		I_CAL_STATUS_PROPERTY,
		I_CAL_X_PROPERTY,
		I_CAL_ANY_PROPERTY /* sentinel */
	},
	expected_filtered[] = {
		I_CAL_STATUS_PROPERTY,
		I_CAL_X_PROPERTY,
		I_CAL_ANY_PROPERTY /* sentinel */
	};
	ICalComponent *icomp;
	ICalProperty *prop;
	ECalComponentPropertyBag *bag, *bag2;
	gint ii;

	icomp = i_cal_component_new_from_string (comp_str);
	g_assert_nonnull (icomp);
	g_assert_cmpint (i_cal_component_count_properties (icomp, I_CAL_ANY_PROPERTY), ==, 3);

	bag = e_cal_component_property_bag_new ();
	g_assert_nonnull (bag);
	e_cal_component_property_bag_set_from_component (bag, icomp, NULL, NULL);
	g_assert_cmpint (e_cal_component_property_bag_get_count (bag), ==, G_N_ELEMENTS (expected_unfiltered) - 1);
	test_check_property_bag (bag, expected_unfiltered);

	bag2 = e_cal_component_property_bag_copy (bag);
	g_assert_nonnull (bag2);
	g_assert_cmpint (e_cal_component_property_bag_get_count (bag2), ==, G_N_ELEMENTS (expected_unfiltered) - 1);
	test_check_property_bag (bag2, expected_unfiltered);
	e_cal_component_property_bag_free (bag2);
	e_cal_component_property_bag_free (bag);

	bag = e_cal_component_property_bag_new_from_component (icomp, NULL, NULL);
	g_assert_nonnull (bag);
	g_assert_cmpint (e_cal_component_property_bag_get_count (bag), ==, G_N_ELEMENTS (expected_unfiltered) - 1);
	test_check_property_bag (bag, expected_unfiltered);
	e_cal_component_property_bag_free (bag);

	bag = e_cal_component_property_bag_new ();
	g_assert_nonnull (bag);
	e_cal_component_property_bag_set_from_component (bag, icomp, test_property_bag_filter_cb, expected_filtered);
	g_assert_cmpint (e_cal_component_property_bag_get_count (bag), ==, G_N_ELEMENTS (expected_filtered) - 1);
	test_check_property_bag (bag, expected_filtered);
	e_cal_component_property_bag_free (bag);

	bag = e_cal_component_property_bag_new_from_component (icomp, test_property_bag_filter_cb, expected_filtered);
	g_assert_nonnull (bag);
	g_assert_cmpint (e_cal_component_property_bag_get_count (bag), ==, G_N_ELEMENTS (expected_filtered) - 1);
	test_check_property_bag (bag, expected_filtered);

	g_object_unref (icomp);

	icomp = i_cal_component_new_vevent ();
	g_assert_nonnull (icomp);
	e_cal_component_property_bag_fill_component (bag, icomp);
	g_assert_cmpint (i_cal_component_count_properties (icomp, I_CAL_ANY_PROPERTY), ==, e_cal_component_property_bag_get_count (bag));
	g_object_unref (icomp);

	bag2 = e_cal_component_property_bag_copy (bag);

	while (e_cal_component_property_bag_get_count (bag) > 1) {
		e_cal_component_property_bag_remove (bag, 1);
	}

	e_cal_component_property_bag_assign (bag2, bag);
	g_assert_cmpint (e_cal_component_property_bag_get_count (bag), ==, e_cal_component_property_bag_get_count (bag2));
	e_cal_component_property_bag_free (bag2);

	icomp = i_cal_component_new_vevent ();
	g_assert_nonnull (icomp);
	e_cal_component_property_bag_fill_component (bag, icomp);
	g_assert_cmpint (i_cal_component_count_properties (icomp, I_CAL_ANY_PROPERTY), ==, 1);
	g_object_unref (icomp);

	e_cal_component_property_bag_clear (bag);

	icomp = i_cal_component_new_vevent ();
	g_assert_nonnull (icomp);
	e_cal_component_property_bag_fill_component (bag, icomp);
	g_assert_cmpint (i_cal_component_count_properties (icomp, I_CAL_ANY_PROPERTY), ==, 0);
	g_object_unref (icomp);

	prop = i_cal_property_new_uid ("234");
	e_cal_component_property_bag_add (bag, prop);
	g_object_unref (prop);

	prop = i_cal_property_new_status (I_CAL_STATUS_CANCELLED);
	e_cal_component_property_bag_take (bag, prop);

	g_assert_cmpint (e_cal_component_property_bag_get_count (bag), ==, 2);

	for (ii = 0; ii < 2; ii++) {
		ICalProperty *prop2;

		prop2 = e_cal_component_property_bag_get (bag, ii);
		if (ii == 0) {
			g_assert_true (prop != prop2);
			g_assert_cmpint (i_cal_property_isa (prop2), ==, I_CAL_UID_PROPERTY);
			g_assert_cmpstr (i_cal_property_get_uid (prop2), ==, "234");
		} else {
			g_assert_true (prop == prop2);
			g_assert_cmpint (i_cal_property_isa (prop2), ==, I_CAL_STATUS_PROPERTY);
			g_assert_cmpint (i_cal_property_get_status (prop2), ==, I_CAL_STATUS_CANCELLED);
		}
	}

	icomp = i_cal_component_new_vevent ();
	g_assert_nonnull (icomp);
	e_cal_component_property_bag_fill_component (bag, icomp);
	g_assert_cmpint (i_cal_component_count_properties (icomp, I_CAL_ANY_PROPERTY), ==, e_cal_component_property_bag_get_count (bag));
	g_object_unref (icomp);

	e_cal_component_property_bag_clear (bag);

	e_cal_component_property_bag_take (bag, i_cal_property_new_status (I_CAL_STATUS_CANCELLED));
	e_cal_component_property_bag_take (bag, i_cal_property_new_uid ("111"));
	e_cal_component_property_bag_take (bag, i_cal_property_new_status (I_CAL_STATUS_COMPLETED));
	e_cal_component_property_bag_take (bag, i_cal_property_new_status (I_CAL_STATUS_INPROCESS));

	g_assert_cmpint (e_cal_component_property_bag_get_count (bag), ==, 4);
	g_assert_cmpint (e_cal_component_property_bag_get_first_by_kind (bag, I_CAL_STATUS_PROPERTY), ==, 0);
	g_assert_cmpint (e_cal_component_property_bag_get_first_by_kind (bag, I_CAL_UID_PROPERTY), ==, 1);
	g_assert_cmpint (e_cal_component_property_bag_remove_by_kind (bag, I_CAL_TZID_PROPERTY, FALSE), ==, 0);
	g_assert_cmpint (e_cal_component_property_bag_get_count (bag), ==, 4);
	g_assert_cmpint (e_cal_component_property_bag_remove_by_kind (bag, I_CAL_TZID_PROPERTY, TRUE), ==, 0);
	g_assert_cmpint (e_cal_component_property_bag_get_count (bag), ==, 4);
	g_assert_cmpint (e_cal_component_property_bag_remove_by_kind (bag, I_CAL_STATUS_PROPERTY, FALSE), ==, 1);
	g_assert_cmpint (e_cal_component_property_bag_get_count (bag), ==, 3);
	g_assert_cmpint (e_cal_component_property_bag_get_first_by_kind (bag, I_CAL_UID_PROPERTY), ==, 0);
	g_assert_cmpint (e_cal_component_property_bag_get_first_by_kind (bag, I_CAL_STATUS_PROPERTY), ==, 1);
	g_assert_cmpint (e_cal_component_property_bag_remove_by_kind (bag, I_CAL_STATUS_PROPERTY, TRUE), ==, 2);
	g_assert_cmpint (e_cal_component_property_bag_get_count (bag), ==, 1);
	g_assert_cmpint (e_cal_component_property_bag_get_first_by_kind (bag, I_CAL_UID_PROPERTY), ==, 0);
	g_assert_cmpint (e_cal_component_property_bag_get_first_by_kind (bag, I_CAL_STATUS_PROPERTY), >=, e_cal_component_property_bag_get_count (bag));

	e_cal_component_property_bag_free (bag);
}

#undef X_PROP_NAME
#undef X_PROP_VALUE

static void
test_component_struct_range (void)
{
	struct _values {
		const gchar *time;
		const gchar *tzid;
	} values[] = {
		{ "20181215T111213Z", NULL },
		{ "20190131T121314", "Europe/Berlin" },
		{ "20200708T010305Z", NULL },
		{ "20211215T101112", "America/New_York" },
		{ "20221110T090807", "UTC" },
		{ "20231009", NULL }
	};
	ECalComponentRangeKind range_kinds[] = {
		E_CAL_COMPONENT_RANGE_SINGLE,
		E_CAL_COMPONENT_RANGE_THISPRIOR,
		E_CAL_COMPONENT_RANGE_THISFUTURE
	};
	gint ii, jj, set_kind;

	for (set_kind = 0; set_kind < 4; set_kind++) {
		for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
			for (jj = 0; jj < G_N_ELEMENTS (range_kinds); jj++) {
				ECalComponentRange *expected = NULL, *received;
				ECalComponentDateTime *dt;

				if (set_kind == 2) {
					dt = e_cal_component_datetime_new_take (i_cal_time_new_from_string ("19981019"), NULL);
					expected = e_cal_component_range_new (E_CAL_COMPONENT_RANGE_SINGLE, dt);
					e_cal_component_datetime_free (dt);
				} else if (set_kind == 3) {
					dt = e_cal_component_datetime_new_take (i_cal_time_new_from_string ("19981019"), NULL);
					expected = e_cal_component_range_new_take (E_CAL_COMPONENT_RANGE_SINGLE, dt);
				}

				dt = e_cal_component_datetime_new_take (i_cal_time_new_from_string (values[ii].time), g_strdup (values[ii].tzid));
				g_assert_nonnull (dt);

				if (expected) {
					e_cal_component_range_set_kind (expected, range_kinds[jj]);

					if (((set_kind + ii) & 1) != 0) {
						e_cal_component_range_set_datetime (expected, dt);
					} else {
						ECalComponentDateTime *dtcopy;

						dtcopy = e_cal_component_datetime_copy (dt);
						g_assert_nonnull (dtcopy);

						e_cal_component_range_take_datetime (expected, dtcopy);
					}
				} else {
					if (set_kind == 0) {
						expected = e_cal_component_range_new (range_kinds[jj], dt);
					} else {
						ECalComponentDateTime *dtcopy;

						dtcopy = e_cal_component_datetime_copy (dt);
						g_assert_nonnull (dtcopy);

						expected = e_cal_component_range_new_take (range_kinds[jj], dtcopy);
					}
				}

				g_assert_nonnull (expected);

				verify_struct_datetime_equal (e_cal_component_range_get_datetime (expected), dt);
				g_assert_cmpint (e_cal_component_range_get_kind (expected), ==, range_kinds[jj]);

				e_cal_component_datetime_free (dt);

				received = e_cal_component_range_copy (expected);
				verify_struct_range_equal (expected, received);
				e_cal_component_range_free (received);

				e_cal_component_range_free (expected);
			}
		}
	}
}

static void
test_component_struct_text (void)
{
	struct _values {
		const gchar *value;
		const gchar *altrep;
		const gchar *language;
	} values[] = {
		{ "value1", NULL, NULL },
		{ "value2", "altrep1", NULL },
		{ "value3", "altrep2", NULL },
		{ "value4", NULL, NULL },
		{ "value1", NULL, "en_US" },
		{ "value2", "altrep1", "en" },
		{ "value3", "altrep2", "en_GB" },
		{ "value4", NULL, "en" }
	};
	ICalProperty *prop;
	ECalComponentText *expected, *received;
	gint ii, set_kind;

	for (set_kind = 0; set_kind < 3; set_kind++) {
		for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
			expected = NULL;

			if (set_kind == 1) {
				expected = e_cal_component_text_new ("non-empty", NULL);
			} else if (set_kind == 2) {
				expected = e_cal_component_text_new ("non-empty-text", "non-empty-altrep");
			}

			if (expected) {
				e_cal_component_text_set_value (expected, values[ii].value);
				e_cal_component_text_set_altrep (expected, values[ii].altrep);
				e_cal_component_text_set_language (expected, values[ii].language);
			} else {
				expected = e_cal_component_text_new (values[ii].value, values[ii].altrep);
				e_cal_component_text_set_language (expected, values[ii].language);
			}

			g_assert_nonnull (expected);
			g_assert_cmpstr (e_cal_component_text_get_value (expected), ==, values[ii].value);
			g_assert_cmpstr (e_cal_component_text_get_altrep (expected), ==, values[ii].altrep);
			g_assert_cmpstr (e_cal_component_text_get_language (expected), ==, values[ii].language);

			received = e_cal_component_text_copy (expected);
			verify_struct_text_equal (expected, received);
			e_cal_component_text_free (received);

			e_cal_component_text_free (expected);
		}
	}

	prop = i_cal_property_new_summary ("summary1");

	expected = e_cal_component_text_new ("summary1", NULL);

	received = e_cal_component_text_new_from_property (prop);
	g_assert_nonnull (received);
	g_assert_cmpstr (e_cal_component_text_get_value (received), ==, "summary1");
	g_assert_null (e_cal_component_text_get_altrep (received));
	g_assert_null (e_cal_component_text_get_language (received));
	verify_struct_text_equal (expected, received);

	e_cal_component_text_set_value (expected, "summary2");
	e_cal_component_text_set_altrep (expected, "altrep");
	e_cal_component_text_fill_property (expected, prop);

	e_cal_component_text_set_from_property (received, prop);
	g_assert_cmpstr (e_cal_component_text_get_value (received), ==, "summary2");
	g_assert_cmpstr (e_cal_component_text_get_altrep (received), ==, "altrep");
	g_assert_null (e_cal_component_text_get_language (received));
	verify_struct_text_equal (expected, received);

	e_cal_component_text_set_value (expected, "summary3");
	e_cal_component_text_set_altrep (expected, NULL);
	e_cal_component_text_set_language (expected, "en");
	e_cal_component_text_fill_property (expected, prop);

	e_cal_component_text_set_from_property (received, prop);
	g_assert_cmpstr (e_cal_component_text_get_value (received), ==, "summary3");
	g_assert_null (e_cal_component_text_get_altrep (received));
	g_assert_cmpstr (e_cal_component_text_get_language (received), ==, "en");
	verify_struct_text_equal (expected, received);

	e_cal_component_text_set_value (expected, "summary4");
	e_cal_component_text_set_altrep (expected, "altrep2");
	e_cal_component_text_set_language (expected, "en_GB");
	e_cal_component_text_fill_property (expected, prop);

	e_cal_component_text_set_from_property (received, prop);
	g_assert_cmpstr (e_cal_component_text_get_value (received), ==, "summary4");
	g_assert_cmpstr (e_cal_component_text_get_altrep (received), ==, "altrep2");
	g_assert_cmpstr (e_cal_component_text_get_language (received), ==, "en_GB");
	verify_struct_text_equal (expected, received);

	e_cal_component_text_set_value (expected, "summary5");
	e_cal_component_text_set_altrep (expected, NULL);
	e_cal_component_text_set_language (expected, NULL);
	e_cal_component_text_fill_property (expected, prop);

	e_cal_component_text_set_from_property (received, prop);
	g_assert_cmpstr (e_cal_component_text_get_value (received), ==, "summary5");
	g_assert_null (e_cal_component_text_get_altrep (received));
	g_assert_null (e_cal_component_text_get_language (received));
	verify_struct_text_equal (expected, received);

	e_cal_component_text_free (received);
	e_cal_component_text_free (expected);
	g_clear_object (&prop);
}

static void
verify_changes (ECalComponent *comp,
		void (* verify_func) (ECalComponent *comp,
				      gpointer user_data),
		gpointer user_data)
{
	ECalComponent *clone;
	ICalComponent *icalcomp;
	gchar *icalstr;

	e_cal_component_commit_sequence (comp);

	clone = e_cal_component_clone (comp);
	g_assert_nonnull (clone);
	verify_func (clone, user_data);
	g_object_unref (clone);

	icalstr = e_cal_component_get_as_string (comp);
	g_assert_nonnull (icalstr);
	clone = e_cal_component_new_from_string (icalstr);
	g_free (icalstr);
	g_assert_nonnull (clone);
	verify_func (clone, user_data);
	g_object_unref (clone);

	icalcomp = e_cal_component_get_icalcomponent (comp);
	g_assert_nonnull (icalcomp);
	clone = e_cal_component_new_from_icalcomponent (i_cal_component_clone (icalcomp));
	g_assert_nonnull (clone);
	verify_func (clone, user_data);
	g_object_unref (clone);

	icalcomp = e_cal_component_get_icalcomponent (comp);
	g_assert_nonnull (icalcomp);
	icalcomp = i_cal_component_clone (icalcomp);
	g_assert_nonnull (icalcomp);
	clone = e_cal_component_new ();
	g_assert_nonnull (clone);
	g_assert_true (e_cal_component_set_icalcomponent (clone, icalcomp));
	verify_func (clone, user_data);
	g_object_unref (clone);

	verify_func (comp, user_data);
}

static void
verify_component_vtype (ECalComponent *comp,
			gpointer user_data)
{
	g_assert_cmpint (e_cal_component_get_vtype (comp), ==, GPOINTER_TO_INT (user_data));
}

static void
test_component_vtype (void)
{
	ECalComponentVType values[] = {
		E_CAL_COMPONENT_EVENT,
		E_CAL_COMPONENT_TODO,
		E_CAL_COMPONENT_JOURNAL,
		E_CAL_COMPONENT_FREEBUSY,
		E_CAL_COMPONENT_TIMEZONE
	};
	ECalComponent *comp;
	gint ii;

	comp = e_cal_component_new ();
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		e_cal_component_set_new_vtype (comp, values[ii]);
		verify_changes (comp, verify_component_vtype, GINT_TO_POINTER (values[ii]));
	}

	g_object_unref (comp);
}

static void
verify_component_uid (ECalComponent *comp,
		      gpointer user_data)
{
	const gchar *uid = user_data;
	ECalComponentId *id;

	g_assert_cmpstr (e_cal_component_get_uid (comp), ==, uid);

	id = e_cal_component_get_id (comp);
	g_assert_nonnull (id);
	g_assert_nonnull (e_cal_component_id_get_uid (id));
	g_assert_null (e_cal_component_id_get_rid (id));

	g_assert_cmpstr (e_cal_component_id_get_uid (id), ==, uid);

	e_cal_component_id_free (id);
}

static void
test_component_uid (void)
{
	ECalComponent *comp;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	e_cal_component_set_uid (comp, "123");
	verify_changes (comp, verify_component_uid, (gpointer) "123");

	e_cal_component_set_uid (comp, "456");
	verify_changes (comp, verify_component_uid, (gpointer) "456");

	g_object_unref (comp);
}

static GSList * /* gchar * */
test_split_categories (const gchar *categories)
{
	GSList *list = NULL;
	gchar **split;
	gint ii;

	if (!categories)
		return NULL;

	split = g_strsplit (categories, ",", -1);
	if (split) {
		for (ii = 0; split[ii]; ii++) {
			list = g_slist_prepend (list, g_strdup (split[ii]));
		}

		g_strfreev (split);
	}

	return g_slist_reverse (list);
}

static void
verify_component_categories (ECalComponent *comp,
			     gpointer user_data)
{
	const gchar *categories = user_data;
	gchar *categories_str;
	GSList *expected, *received, *link1, *link2;

	categories_str = e_cal_component_get_categories (comp);
	g_assert_cmpstr (categories_str, ==, categories);
	g_free (categories_str);

	expected = test_split_categories (categories);
	received = e_cal_component_get_categories_list (comp);

	g_assert_cmpint (g_slist_length (expected), ==, g_slist_length (received));

	for (link1 = expected, link2 = received;
	     link1 && link2;
	     link1 = g_slist_next (link1), link2 = g_slist_next (link2)) {
		g_assert_cmpstr (link1->data, ==, link2->data);
	}

	g_assert_true (link1 == link2);

	g_slist_free_full (expected, g_free);
	g_slist_free_full (received, g_free);
}

static void
test_component_categories (void)
{
	const gchar *values[] = {
		"cat01",
		"cat02",
		NULL,
		"cat03,cat04,cat05",
		"cat06",
		"cat07,cat08",
		NULL
	};
	ECalComponent *comp;
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		GSList *list;

		comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
		g_assert_nonnull (comp);

		e_cal_component_set_categories (comp, values[ii]);
		verify_changes (comp, verify_component_categories, (gpointer) values[ii]);

		g_object_unref (comp);

		comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
		g_assert_nonnull (comp);

		list = test_split_categories (values[ii]);
		e_cal_component_set_categories_list (comp, list);
		g_slist_free_full (list, g_free);

		verify_changes (comp, verify_component_categories, (gpointer) values[ii]);

		g_object_unref (comp);
	}
}

static void
verify_component_classification (ECalComponent *comp,
				 gpointer user_data)
{
	g_assert_cmpint (e_cal_component_get_classification (comp), ==, GPOINTER_TO_INT (user_data));
}

static void
test_component_classification (void)
{
	ECalComponentClassification values[] = {
		E_CAL_COMPONENT_CLASS_NONE,
		E_CAL_COMPONENT_CLASS_PUBLIC,
		E_CAL_COMPONENT_CLASS_PRIVATE,
		E_CAL_COMPONENT_CLASS_CONFIDENTIAL
	};
	ECalComponent *comp;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		e_cal_component_set_classification (comp, values[ii]);
		verify_changes (comp, verify_component_classification, GINT_TO_POINTER (values[ii]));
	}

	g_object_unref (comp);
}

/* Each part is separated by ':', text from its altrep is separated by '|' */
static GSList * /* ECalComponentText * */
test_split_texts (const gchar *value)
{
	GSList *result = NULL;
	gchar **textsv;
	gint ii;

	if (!value)
		return NULL;

	textsv = g_strsplit (value, ":", -1);

	if (textsv) {
		for (ii = 0; textsv[ii]; ii++) {
			ECalComponentText *comptext;
			gchar *altrep, *language = NULL;

			altrep = strchr (textsv[ii], '|');
			if (altrep) {
				*altrep = '\0';
				altrep++;

				language = strchr (altrep, '|');
				if (language) {
					*language = '\0';
					language++;

					if (!*language)
						language = NULL;
				}

				if (!*altrep)
					altrep = NULL;
			}

			comptext = e_cal_component_text_new (textsv[ii], altrep);
			g_assert_nonnull (comptext);
			if (language)
				e_cal_component_text_set_language (comptext, language);
			result = g_slist_prepend (result, comptext);
		}

		g_strfreev (textsv);
	}

	return g_slist_reverse (result);
}

static void
verify_component_text_list (GSList * (* get_func) (ECalComponent *comp),
			    ECalComponent *comp,
			    gpointer user_data)
{
	GSList *expected, *received, *link1, *link2;

	g_assert_true (get_func != NULL);

	expected = user_data;
	received = get_func (comp);

	g_assert_cmpint (g_slist_length (expected), ==, g_slist_length (received));

	for (link1 = expected, link2 = received;
	     link1 && link2;
	     link1 = g_slist_next (link1), link2 = g_slist_next (link2)) {
		ECalComponentText *text1 = link1->data, *text2 = link2->data;

		g_assert_nonnull (text1);
		g_assert_nonnull (text2);

		verify_struct_text_equal (text1, text2);
	}

	g_assert_true (link1 == link2);

	g_slist_free_full (received, e_cal_component_text_free);
}

static void
test_component_text_list (void (* set_func) (ECalComponent *comp,
					     const GSList *values),
			  void (* verify_func) (ECalComponent *comp,
						gpointer user_data))
{
	const gchar *values[] = {
		"text",
		"line1\nline2|altrep",
		"text|altrep",
		"text1:text2|altrep2:text3a\ntext3b|altrep3",
		"text||en",
		"line1\nline2|altrep|en_US",
		"text|altrep|en_GB",
		"text1:text2|altrep2|en_GB:text3a\ntext3b|altrep3|en",
		NULL
	};
	ECalComponent *comp;
	gint ii;

	g_assert_true (set_func != NULL);
	g_assert_true (verify_func != NULL);

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		GSList *expected = test_split_texts (values[ii]);

		set_func (comp, expected);
		verify_changes (comp, verify_func, expected);

		g_slist_free_full (expected, e_cal_component_text_free);
	}

	g_object_unref (comp);
}

static void
verify_component_icaltime (ICalTime * (* get_func) (ECalComponent *comp),
			   ECalComponent *comp,
			   gpointer user_data)
{
	ICalTime *expected, *received;

	g_assert_true (get_func != NULL);

	expected = user_data;
	received = get_func (comp);

	verify_ical_timetype_equal (expected, received);

	g_clear_object (&received);
}

static void
test_component_icaltime (void (* set_func) (ECalComponent *comp,
					    const ICalTime *tt),
			 void (* verify_func) (ECalComponent *comp,
					       gpointer user_data),
			 gboolean can_null_value)
{
	const gchar *values[] = {
		"20181215T111213Z",
		"20190131T121314Z",
		NULL,
		"20200708T010305Z"
	};
	ECalComponent *comp;
	gint ii;

	g_assert_true (set_func != NULL);
	g_assert_true (verify_func != NULL);

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		ICalTime *tt;

		if (values[ii]) {
			tt = i_cal_time_new_from_string (values[ii]);
			g_assert_nonnull (tt);
		} else if (!can_null_value) {
			continue;
		} else {
			tt = NULL;
		}

		set_func (comp, tt);
		verify_changes (comp, verify_func, tt);

		g_clear_object (&tt);
	}

	g_object_unref (comp);
}

static void
verify_component_datetime (ECalComponentDateTime * (* get_func) (ECalComponent *comp),
			   ECalComponent *comp,
			   gpointer user_data)
{
	ECalComponentDateTime *expected, *received;

	g_assert_true (get_func != NULL);

	expected = user_data;
	received = get_func (comp);

	verify_struct_datetime_equal (expected, received);

	e_cal_component_datetime_free (received);
}

static void
test_component_datetime (void (* set_func) (ECalComponent *comp,
					    const ECalComponentDateTime *dt),
			 void (* verify_func) (ECalComponent *comp,
					       gpointer user_data))
{
	struct _values {
		const gchar *time;
		const gchar *tzid;
	} values[] = {
		{ "20181215T111213Z", NULL },
		{ "20190131T121314Z", NULL },
		{ NULL, NULL },
		{ "20200708T010305Z", NULL },
		{ "20211215T101112", "America/New_York" },
		{ "20221110T090807", "UTC" },
		{ "20231009", NULL }
	};
	ECalComponent *comp;
	gint ii;

	g_assert_true (set_func != NULL);
	g_assert_true (verify_func != NULL);

	comp = e_cal_component_new_vtype (set_func == e_cal_component_set_due ? E_CAL_COMPONENT_TODO : E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		ECalComponentDateTime *dt;

		if (values[ii].time) {
			dt = e_cal_component_datetime_new_take (i_cal_time_new_from_string (values[ii].time), g_strdup (values[ii].tzid));
			g_assert_nonnull (dt);

			if (values[ii].tzid) {
				ICalTime *tt;
				ICalTimezone *zone;

				zone = i_cal_timezone_get_builtin_timezone (values[ii].tzid);
				if (zone) {
					tt = e_cal_component_datetime_get_value (dt);
					i_cal_time_set_timezone (tt, zone);
				}
			}
		} else {
			dt = NULL;
		}

		set_func (comp, dt);

		verify_changes (comp, verify_func, dt);

		e_cal_component_datetime_free (dt);
	}

	g_object_unref (comp);
}

static void
verify_component_rules (GSList * (* get_func) (ECalComponent *comp),
			GSList * (* get_props_func) (ECalComponent *comp),
			gboolean is_exception,
			ECalComponent *comp,
			gpointer user_data)
{
	GSList *expected, *received, *received_props;

	expected = user_data;
	received = get_func (comp);
	received_props = get_props_func (comp);

	if (!expected) {
		g_assert_null (received);
		g_assert_null (received_props);
		if (is_exception) {
			g_assert_true (!e_cal_component_has_exrules (comp));
			g_assert_true (!e_cal_component_has_exceptions (comp));
		} else {
			g_assert_true (!e_cal_component_has_rrules (comp));
			g_assert_true (!e_cal_component_has_recurrences (comp));
			g_assert_true (e_cal_component_has_simple_recurrence (comp));
		}
	} else {
		GSList *link1, *link2, *link3;

		if (is_exception) {
			g_assert_true (e_cal_component_has_exrules (comp));
			g_assert_true (e_cal_component_has_exceptions (comp));
			g_assert_true (!e_cal_component_has_rrules (comp));
		} else {
			g_assert_true (!e_cal_component_has_exrules (comp));
			g_assert_true (!e_cal_component_has_exceptions (comp));
			g_assert_true (e_cal_component_has_rrules (comp));
			g_assert_true (e_cal_component_has_recurrences (comp));
			if (expected->next)
				g_assert_true (!e_cal_component_has_simple_recurrence (comp));
			else
				g_assert_true (e_cal_component_has_simple_recurrence (comp));
		}

		g_assert_cmpint (g_slist_length (expected), ==, g_slist_length (received));
		g_assert_cmpint (g_slist_length (expected), ==, g_slist_length (received_props));

		for (link1 = expected, link2 = received, link3 = received_props;
		     link1 && link2 && link3;
		     link1 = g_slist_next (link1), link2 = g_slist_next (link2), link3 = g_slist_next (link3)) {
			ICalRecurrence *rt_expected, *rt_received, *rt_received_prop;
			ICalProperty *prop_received;
			gchar *str_expected, *str_received, *str_received_prop;

			rt_expected = link1->data;
			rt_received = link2->data;
			prop_received = link3->data;

			if (is_exception) {
				rt_received_prop = i_cal_property_get_exrule (prop_received);
			} else {
				rt_received_prop = i_cal_property_get_rrule (prop_received);
			}

			str_expected = i_cal_recurrence_to_string (rt_expected);
			str_received = i_cal_recurrence_to_string (rt_received);
			str_received_prop = i_cal_recurrence_to_string (rt_received_prop);

			g_assert_cmpstr (str_expected, ==, str_received);
			g_assert_cmpstr (str_expected, ==, str_received_prop);

			g_free (str_expected);
			g_free (str_received);
			g_free (str_received_prop);
			g_clear_object (&rt_received_prop);
		}

		g_assert_true (link1 == link2);
		g_assert_true (link2 == link3);
	}

	g_slist_free_full (received, g_object_unref);
	g_slist_free_full (received_props, g_object_unref);
}

static void
test_component_rules (void (* set_func) (ECalComponent *comp,
					 const GSList *recur_list),
		      void (* verify_func) (ECalComponent *comp,
					    gpointer user_data))
{
	const gchar *values[] = {
		"FREQ=DAILY;COUNT=10;INTERVAL=6",
		NULL, /* terminator */
		NULL, /* terminator */
		"FREQ=YEARLY;BYDAY=-1SU;BYMONTH=3",
		"FREQ=DAILY;BYDAY=1SU",
		NULL /* terminator */
	};
	ECalComponent *comp;
	GSList *rules = NULL;
	gint ii;

	g_assert_nonnull (set_func);
	g_assert_nonnull (verify_func);

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		if (values[ii]) {
			ICalRecurrence *rt;

			rt = i_cal_recurrence_new_from_string (values[ii]);
			g_assert_nonnull (rt);

			rules = g_slist_prepend (rules, rt);
		} else {
			rules = g_slist_reverse (rules);

			set_func (comp, rules);
			verify_changes (comp, verify_func, rules);

			g_slist_free_full (rules, g_object_unref);
			rules = NULL;
		}
	}

	g_object_unref (comp);

	g_assert_null (rules);
}

static void
verify_component_comments (ECalComponent *comp,
			   gpointer user_data)
{
	verify_component_text_list (e_cal_component_get_comments, comp, user_data);
}

static void
test_component_comments (void)
{
	test_component_text_list (e_cal_component_set_comments, verify_component_comments);
}

static void
test_component_comments_locale (void)
{
	const gchar *comp_str =
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"COMMENT;LANGUAGE=en-US:desc-en-US\r\n"
		"COMMENT;LANGUAGE=en:desc-en\r\n"
		"COMMENT;LANGUAGE=en-GB:desc-en-GB\r\n"
		"COMMENT:desc\r\n"
		"END:VEVENT\r\n";
	ECalComponent *comp;
	ECalComponentText *text;

	comp = e_cal_component_new_from_string (comp_str);
	g_assert_nonnull (comp);

	text = e_cal_component_dup_comment_for_locale (comp, NULL);
	g_assert_nonnull (text);
	e_cal_component_text_free (text);

	text = e_cal_component_dup_comment_for_locale (comp, "en_US");
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "desc-en-US");
	e_cal_component_text_free (text);

	text = e_cal_component_dup_comment_for_locale (comp, "en_GB");
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "desc-en-GB");
	e_cal_component_text_free (text);

	text = e_cal_component_dup_comment_for_locale (comp, "en");
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "desc-en");
	e_cal_component_text_free (text);

	text = e_cal_component_dup_comment_for_locale (comp, "xxx");
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "desc");
	e_cal_component_text_free (text);

	g_clear_object (&comp);

	comp_str =
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"COMMENT;LANGUAGE=en-US:desc-en-US\r\n"
		"COMMENT;LANGUAGE=en:desc-en\r\n"
		"COMMENT;LANGUAGE=en-GB:desc-en-GB\r\n"
		"END:VEVENT\r\n";

	comp = e_cal_component_new_from_string (comp_str);
	g_assert_nonnull (comp);

	text = e_cal_component_dup_comment_for_locale (comp, "xxx");
	g_assert_nonnull (text);
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "desc-en-US");
	e_cal_component_text_free (text);

	g_clear_object (&comp);
}

static void
verify_component_completed (ECalComponent *comp,
			    gpointer user_data)
{
	verify_component_icaltime (e_cal_component_get_completed, comp, user_data);
}

static void
test_component_completed (void)
{
	test_component_icaltime (e_cal_component_set_completed, verify_component_completed, TRUE);
}

static void
verify_component_contacts (ECalComponent *comp,
			   gpointer user_data)
{
	verify_component_text_list (e_cal_component_get_contacts, comp, user_data);
}

static void
test_component_contacts (void)
{
	test_component_text_list (e_cal_component_set_contacts, verify_component_contacts);
}

static void
verify_component_created (ECalComponent *comp,
			  gpointer user_data)
{
	verify_component_icaltime (e_cal_component_get_created, comp, user_data);
}

static void
test_component_created (void)
{
	test_component_icaltime (e_cal_component_set_created, verify_component_created, TRUE);
}

static void
verify_component_descriptions (ECalComponent *comp,
			       gpointer user_data)
{
	verify_component_text_list (e_cal_component_get_descriptions, comp, user_data);
}

static void
test_component_descriptions (void)
{
	test_component_text_list (e_cal_component_set_descriptions, verify_component_descriptions);
}

static void
test_component_descriptions_locale (void)
{
	const gchar *comp_str =
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"DESCRIPTION;LANGUAGE=en-US:desc-en-US\r\n"
		"DESCRIPTION;LANGUAGE=en:desc-en\r\n"
		"DESCRIPTION;LANGUAGE=en-GB:desc-en-GB\r\n"
		"DESCRIPTION:desc\r\n"
		"END:VEVENT\r\n";
	ECalComponent *comp;
	ECalComponentText *text;

	comp = e_cal_component_new_from_string (comp_str);
	g_assert_nonnull (comp);

	text = e_cal_component_dup_description_for_locale (comp, NULL);
	g_assert_nonnull (text);
	e_cal_component_text_free (text);

	text = e_cal_component_dup_description_for_locale (comp, "en_US");
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "desc-en-US");
	e_cal_component_text_free (text);

	text = e_cal_component_dup_description_for_locale (comp, "en_GB");
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "desc-en-GB");
	e_cal_component_text_free (text);

	text = e_cal_component_dup_description_for_locale (comp, "en");
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "desc-en");
	e_cal_component_text_free (text);

	text = e_cal_component_dup_description_for_locale (comp, "xxx");
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "desc");
	e_cal_component_text_free (text);

	g_clear_object (&comp);

	comp_str =
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"DESCRIPTION;LANGUAGE=en-US:desc-en-US\r\n"
		"DESCRIPTION;LANGUAGE=en:desc-en\r\n"
		"DESCRIPTION;LANGUAGE=en-GB:desc-en-GB\r\n"
		"END:VEVENT\r\n";

	comp = e_cal_component_new_from_string (comp_str);
	g_assert_nonnull (comp);

	text = e_cal_component_dup_description_for_locale (comp, "xxx");
	g_assert_nonnull (text);
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "desc-en-US");
	e_cal_component_text_free (text);

	g_clear_object (&comp);
}

static void
verify_component_dtend (ECalComponent *comp,
			gpointer user_data)
{
	verify_component_datetime (e_cal_component_get_dtend, comp, user_data);
}

static void
test_component_dtend (void)
{
	test_component_datetime (e_cal_component_set_dtend, verify_component_dtend);
}

static void
verify_component_dtstamp (ECalComponent *comp,
			  gpointer user_data)
{
	verify_component_icaltime (e_cal_component_get_dtstamp, comp, user_data);
}

static void
test_component_dtstamp (void)
{
	test_component_icaltime (e_cal_component_set_dtstamp, verify_component_dtstamp, FALSE);
}

static void
verify_component_dtstart (ECalComponent *comp,
			  gpointer user_data)
{
	verify_component_datetime (e_cal_component_get_dtstart, comp, user_data);
}

static void
test_component_dtstart (void)
{
	test_component_datetime (e_cal_component_set_dtstart, verify_component_dtstart);
}

static void
verify_component_due (ECalComponent *comp,
		      gpointer user_data)
{
	verify_component_datetime (e_cal_component_get_due, comp, user_data);
}

static void
test_component_due (void)
{
	test_component_datetime (e_cal_component_set_due, verify_component_due);
}

static void
verify_component_exdates (ECalComponent *comp,
			  gpointer user_data)
{
	GSList *expected, *received;

	expected = user_data;
	received = e_cal_component_get_exdates (comp);

	if (!expected) {
		g_assert_null (received);
		g_assert_true (!e_cal_component_has_exdates (comp));
		g_assert_true (!e_cal_component_has_exceptions (comp));
	} else {
		GSList *link1, *link2;

		g_assert_true (e_cal_component_has_exdates (comp));
		g_assert_true (e_cal_component_has_exceptions (comp));
		g_assert_cmpint (g_slist_length (expected), ==, g_slist_length (received));

		for (link1 = expected, link2 = received; link1 && link2; link1 = g_slist_next (link1), link2 = g_slist_next (link2)) {
			ECalComponentDateTime *dt_expected = link1->data, *dt_received = link2->data;

			verify_struct_datetime_equal (dt_expected, dt_received);
		}

		g_assert_true (link1 == link2);
	}

	g_slist_free_full (received, e_cal_component_datetime_free);
}

static void
test_component_exdates (void)
{
	struct _values {
		const gchar *time;
		const gchar *tzid;
	} values[] = {
		{ "20181215T111213Z", NULL },
		{ "20190131T121314Z", NULL },
		{ NULL, NULL }, /* terminator */
		{ NULL, NULL }, /* terminator */
		{ "20200708T010305Z", NULL },
		{ "20211215T101112", "America/New_York" },
		{ "20221110T090807", "UTC" },
		{ "20231009", NULL },
		{ NULL, NULL } /* terminator */
	};
	ECalComponent *comp;
	GSList *exdates = NULL;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		if (values[ii].time) {
			ECalComponentDateTime *dt;

			dt = e_cal_component_datetime_new_take (i_cal_time_new_from_string (values[ii].time), g_strdup (values[ii].tzid));
			g_assert_nonnull (dt);

			if (values[ii].tzid) {
				ICalTime *tt;
				ICalTimezone *zone;

				zone = i_cal_timezone_get_builtin_timezone (values[ii].tzid);
				if (zone) {
					tt = e_cal_component_datetime_get_value (dt);
					i_cal_time_set_timezone (tt, zone);
				}
			}

			exdates = g_slist_prepend (exdates, dt);
		} else {
			exdates = g_slist_reverse (exdates);

			e_cal_component_set_exdates (comp, exdates);
			verify_changes (comp, verify_component_exdates, exdates);

			g_slist_free_full (exdates, e_cal_component_datetime_free);
			exdates = NULL;
		}
	}

	g_object_unref (comp);

	g_assert_null (exdates);
}

static void
verify_component_exrules (ECalComponent *comp,
			  gpointer user_data)
{
	verify_component_rules (e_cal_component_get_exrules, e_cal_component_get_exrule_properties, TRUE, comp, user_data);
}

static void
test_component_exrules (void)
{
	test_component_rules (e_cal_component_set_exrules, verify_component_exrules);
}

static void
verify_component_geo (ECalComponent *comp,
		      gpointer user_data)
{
	ICalGeo *expected, *received;

	expected = user_data;
	received = e_cal_component_get_geo (comp);

	if (!expected) {
		g_assert_null (received);
	} else {
		g_assert_nonnull (received);
		g_assert_cmpfloat (i_cal_geo_get_lat (expected), ==, i_cal_geo_get_lat (received));
		g_assert_cmpfloat (i_cal_geo_get_lon (expected), ==, i_cal_geo_get_lon (received));
	}

	g_clear_object (&received);
}

static void
test_component_geo (void)
{
	struct _values {
		gdouble lat;
		gdouble lon;
	} values[] = {
		{ 10.0, 20.0 },
		{ -1.0, -1.0 },
		{ 50.0, 30.0 }
	};
	ECalComponent *comp;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		ICalGeo *geo = NULL;

		if (values[ii].lat > 0.0)
			geo = i_cal_geo_new (values[ii].lat, values[ii].lon);

		e_cal_component_set_geo (comp, geo);
		verify_changes (comp, verify_component_geo, geo);

		g_clear_object (&geo);
	}

	g_object_unref (comp);
}

static void
verify_component_lastmodified (ECalComponent *comp,
			       gpointer user_data)
{
	verify_component_icaltime (e_cal_component_get_last_modified, comp, user_data);
}

static void
test_component_lastmodified (void)
{
	test_component_icaltime (e_cal_component_set_last_modified, verify_component_lastmodified, TRUE);
}

static void
verify_component_organizer (ECalComponent *comp,
			    gpointer user_data)
{
	ECalComponentOrganizer *expected, *received;

	expected = user_data;
	received = e_cal_component_get_organizer (comp);

	if (!expected) {
		g_assert_null (received);
		g_assert_true (!e_cal_component_has_organizer (comp));
	} else {
		g_assert_nonnull (received);
		g_assert_true (e_cal_component_has_organizer (comp));
		verify_struct_organizer_equal (expected, received);
	}

	e_cal_component_organizer_free (received);
}

static void
test_component_organizer (void)
{
	struct _values {
		const gchar *value;
		const gchar *sentby;
		const gchar *cn;
		const gchar *language;
	} values[] = {
		{ "mailto:org1", NULL, "First Organizer", NULL },
		{ NULL, NULL, NULL, NULL },
		{ "mailto:org2","mailto:sentby2", "Second Organizer", "en_US" }
	};
	ECalComponent *comp;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		ECalComponentOrganizer *org = NULL;

		if (values[ii].value) {
			org = e_cal_component_organizer_new_full (values[ii].value, values[ii].sentby, values[ii].cn, values[ii].language);

			if (ii == 0)
				test_component_add_params_to_bag (e_cal_component_organizer_get_parameter_bag (org), ii + 1);
		}

		e_cal_component_set_organizer (comp, org);
		verify_changes (comp, verify_component_organizer, org);

		e_cal_component_organizer_free (org);
	}

	g_object_unref (comp);
}

static void
verify_component_percentcomplete (ECalComponent *comp,
				  gpointer user_data)
{
	gint expected, received;

	expected = GPOINTER_TO_INT (user_data);
	received = e_cal_component_get_percent_complete (comp);

	g_assert_cmpint (expected, ==, received);
}

static void
test_component_percentcomplete (void)
{
	gint values[] = { 10, -1, 13, 0, 78, 99, -1, 100 };
	ECalComponent *comp;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		e_cal_component_set_percent_complete (comp, values[ii]);
		verify_changes (comp, verify_component_percentcomplete, GINT_TO_POINTER (values[ii]));
	}

	g_object_unref (comp);
}

static void
verify_component_priority (ECalComponent *comp,
			   gpointer user_data)
{
	gint expected, received;

	expected = GPOINTER_TO_INT (user_data);
	received = e_cal_component_get_priority (comp);

	g_assert_cmpint (expected, ==, received);
}

static void
test_component_priority (void)
{
	gint values[] = { 8, 7, 6, 5, -1, 4, 3, 2, 1, 0, -1, 9 };
	ECalComponent *comp;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		e_cal_component_set_priority (comp, values[ii]);
		verify_changes (comp, verify_component_priority, GINT_TO_POINTER (values[ii]));
	}

	g_object_unref (comp);
}

static void
verify_component_recurid (ECalComponent *comp,
			  gpointer user_data)
{
	ECalComponentRange *expected, *received;
	ECalComponentId *id;
	gchar *rid_str;

	expected = user_data;
	received = e_cal_component_get_recurid (comp);
	rid_str = e_cal_component_get_recurid_as_string (comp);
	id = e_cal_component_get_id (comp);

	g_assert_nonnull (id);

	if (!expected) {
		g_assert_null (received);
		g_assert_null (rid_str);
		g_assert_null (e_cal_component_id_get_rid (id));
		g_assert_true (!e_cal_component_is_instance (comp));
	} else {
		g_assert_nonnull (rid_str);
		g_assert_nonnull (e_cal_component_id_get_rid (id));
		g_assert_true (e_cal_component_is_instance (comp));
		g_assert_cmpstr (e_cal_component_id_get_rid (id), ==, rid_str);

		verify_struct_range_equal (expected, received);
	}

	e_cal_component_range_free (received);
	e_cal_component_id_free (id);
	g_free (rid_str);
}

static void
test_component_recurid (void)
{
	struct _values {
		const gchar *time;
		const gchar *tzid;
		ECalComponentRangeKind range_kind;
	} values[] = {
		{ "20181215T111213Z",	NULL,			E_CAL_COMPONENT_RANGE_SINGLE },
		{ "20190131T121314Z",	NULL,			E_CAL_COMPONENT_RANGE_THISFUTURE },
		{ NULL,			NULL,			E_CAL_COMPONENT_RANGE_SINGLE },
		{ "20200708T010305Z",	NULL,			E_CAL_COMPONENT_RANGE_SINGLE },
		{ "20211215T101112",	"America/New_York",	E_CAL_COMPONENT_RANGE_SINGLE },
		{ "20221110T090807",	"UTC",			E_CAL_COMPONENT_RANGE_THISFUTURE },
		{ "20231009",		NULL,			E_CAL_COMPONENT_RANGE_THISFUTURE }
	};
	ECalComponent *comp;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		ECalComponentRange *rid = NULL;

		if (values[ii].time) {
			ECalComponentDateTime *dt;

			dt = e_cal_component_datetime_new_take (i_cal_time_new_from_string (values[ii].time), g_strdup (values[ii].tzid));
			g_assert_nonnull (dt);

			if (values[ii].tzid) {
				ICalTime *tt;
				ICalTimezone *zone;

				zone = i_cal_timezone_get_builtin_timezone (values[ii].tzid);
				if (zone) {
					tt = e_cal_component_datetime_get_value (dt);
					i_cal_time_set_timezone (tt, zone);
				}
			}

			rid = e_cal_component_range_new_take (values[ii].range_kind, dt);
		}

		e_cal_component_set_recurid (comp, rid);
		verify_changes (comp, verify_component_recurid, rid);

		e_cal_component_range_free (rid);
	}

	g_object_unref (comp);
}

static void
verify_component_rdates (ECalComponent *comp,
			 gpointer user_data)
{
	GSList *expected, *received;

	expected = user_data;
	received = e_cal_component_get_rdates (comp);

	if (!expected) {
		g_assert_null (received);
		g_assert_true (!e_cal_component_has_rdates (comp));
		g_assert_true (!e_cal_component_has_recurrences (comp));
	} else {
		GSList *link1, *link2;

		g_assert_true (e_cal_component_has_rdates (comp));
		g_assert_true (e_cal_component_has_recurrences (comp));
		g_assert_cmpint (g_slist_length (expected), ==, g_slist_length (received));

		for (link1 = expected, link2 = received; link1 && link2; link1 = g_slist_next (link1), link2 = g_slist_next (link2)) {
			ECalComponentPeriod *period_expected = link1->data, *period_received = link2->data;

			verify_struct_period_equal (period_expected, period_received);
		}

		g_assert_true (link1 == link2);
	}

	g_slist_free_full (received, e_cal_component_period_free);
}

static void
test_component_rdates (void)
{
	struct _values {
		const gchar *start;
		const gchar *end;
		gint duration;
	} values[] = {
		{ "20181215T111213Z", "20181215T121314Z", -1 },
		{ "20190131T121314Z", NULL, 10 * 24 * 60 * 60 },
		{ NULL, NULL, -1 }, /* terminator */
		{ NULL, NULL, -1 }, /* terminator */
		{ "20200708T010305Z", NULL, 2 * 60 * 60 },
		{ "20211215T101112", "20211215T121314", -1 },
		{ "20221110T090807", "20221110T100908", -1 },
		{ "20231009", "20241110", -1 },
		{ NULL, NULL, -1 }, /* terminator */
		{ "20240908T070605", NULL, -1 },
		{ "20250807", NULL, -1 },
		{ NULL, NULL, -1 } /* terminator */
	};
	ECalComponent *comp;
	GSList *rdates = NULL;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		if (values[ii].start) {
			ICalTime *start, *end = NULL;
			ICalDuration *duration = NULL;
			ECalComponentPeriod *period;

			start = i_cal_time_new_from_string (values[ii].start);
			g_assert_nonnull (start);
			if (values[ii].duration == -1) {
				if (values[ii].end) {
					end = i_cal_time_new_from_string (values[ii].end);
					g_assert_nonnull (end);
				}
				period = e_cal_component_period_new_datetime (start, end);
			} else {
				duration = i_cal_duration_new_from_int (values[ii].duration);
				g_assert_nonnull (duration);
				period = e_cal_component_period_new_duration (start, duration);
			}

			g_assert_nonnull (period);

			rdates = g_slist_prepend (rdates, period);

			g_clear_object (&start);
			g_clear_object (&end);
			g_clear_object (&duration);
		} else {
			rdates = g_slist_reverse (rdates);

			e_cal_component_set_rdates (comp, rdates);
			verify_changes (comp, verify_component_rdates, rdates);

			g_slist_free_full (rdates, e_cal_component_period_free);
			rdates = NULL;
		}
	}

	g_object_unref (comp);

	g_assert_null (rdates);
}

static void
verify_component_rrules (ECalComponent *comp,
			 gpointer user_data)
{
	verify_component_rules (e_cal_component_get_rrules, e_cal_component_get_rrule_properties, FALSE, comp, user_data);
}

static void
test_component_rrules (void)
{
	test_component_rules (e_cal_component_set_rrules, verify_component_rrules);
}

static void
verify_component_sequence (ECalComponent *comp,
			   gpointer user_data)
{
	gint expected, received;

	expected = GPOINTER_TO_INT (user_data);
	received = e_cal_component_get_sequence (comp);

	g_assert_cmpint (expected, ==, received);
}

static void
test_component_sequence (void)
{
	gint values[] = { 8, 7, 6, 5, -1, 4, 3, 2, 1, 0, -1, 9 };
	ECalComponent *comp;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		e_cal_component_set_sequence (comp, values[ii]);
		e_cal_component_abort_sequence (comp);
		verify_changes (comp, verify_component_sequence, GINT_TO_POINTER (values[ii]));
	}

	g_object_unref (comp);
}

static void
verify_component_status (ECalComponent *comp,
			 gpointer user_data)
{
	ICalPropertyStatus expected, received;

	expected = GPOINTER_TO_INT (user_data);
	received = e_cal_component_get_status (comp);

	g_assert_cmpint (expected, ==, received);
}

static void
test_component_status (void)
{
	ICalPropertyStatus values[] = {
		I_CAL_STATUS_TENTATIVE,
		I_CAL_STATUS_CONFIRMED,
		I_CAL_STATUS_COMPLETED,
		I_CAL_STATUS_NEEDSACTION,
		I_CAL_STATUS_CANCELLED,
		I_CAL_STATUS_INPROCESS,
		I_CAL_STATUS_DRAFT,
		I_CAL_STATUS_FINAL,
		I_CAL_STATUS_NONE
	};
	ECalComponent *comp;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		e_cal_component_set_status (comp, values[ii]);
		verify_changes (comp, verify_component_status, GINT_TO_POINTER (values[ii]));
	}

	g_object_unref (comp);
}

static void
verify_component_summary (ECalComponent *comp,
			  gpointer user_data)
{
	ECalComponentText *expected, *received;

	expected = user_data;
	received = e_cal_component_get_summary (comp);

	if (expected) {
		g_assert_nonnull (received);
		verify_struct_text_equal (expected, received);
	} else {
		g_assert_null (received);
	}

	e_cal_component_text_free (received);
}

static void
test_component_summary (void)
{
	const gchar *values[] = {
		"text",
		"line1\nline2|altrep",
		"text|altrep",
		NULL,
		"text1:text2|altrep2:text3a\ntext3b|altrep3",
		"text||en",
		"line1\nline2|altrep|en_US",
		"text|altrep|en_GB",
		"text1:text2|altrep2|en:text3a\ntext3b|altrep3|en_US"
	};
	ECalComponent *comp;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		GSList *expected = test_split_texts (values[ii]), *link;

		if (expected) {
			for (link = expected; link; link = g_slist_next (link)) {
				ECalComponentText *text = link->data;

				e_cal_component_set_summary (comp, text);
				verify_changes (comp, verify_component_summary, text);
			}
		} else {
			e_cal_component_set_summary (comp, NULL);
			verify_changes (comp, verify_component_summary, NULL);
		}

		g_slist_free_full (expected, e_cal_component_text_free);
	}

	g_object_unref (comp);
}

static void
test_component_summary_locale (void)
{
	const gchar *comp_str =
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"SUMMARY;LANGUAGE=en-US:summ-en-US\r\n"
		"SUMMARY;LANGUAGE=en:summ-en\r\n"
		"SUMMARY;LANGUAGE=en-GB:summ-en-GB\r\n"
		"SUMMARY:summ\r\n"
		"END:VEVENT\r\n";
	ECalComponent *comp;
	ECalComponentText *text;

	comp = e_cal_component_new_from_string (comp_str);
	g_assert_nonnull (comp);

	text = e_cal_component_dup_summary_for_locale (comp, NULL);
	g_assert_nonnull (text);
	e_cal_component_text_free (text);

	text = e_cal_component_dup_summary_for_locale (comp, "en_US");
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "summ-en-US");
	e_cal_component_text_free (text);

	text = e_cal_component_dup_summary_for_locale (comp, "en_GB");
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "summ-en-GB");
	e_cal_component_text_free (text);

	text = e_cal_component_dup_summary_for_locale (comp, "en");
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "summ-en");
	e_cal_component_text_free (text);

	text = e_cal_component_dup_summary_for_locale (comp, "xxx");
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "summ");
	e_cal_component_text_free (text);

	g_clear_object (&comp);

	comp_str =
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"SUMMARY;LANGUAGE=en-US:summ-en-US\r\n"
		"SUMMARY;LANGUAGE=en:summ-en\r\n"
		"SUMMARY;LANGUAGE=en-GB:summ-en-GB\r\n"
		"END:VEVENT\r\n";

	comp = e_cal_component_new_from_string (comp_str);
	g_assert_nonnull (comp);

	text = e_cal_component_dup_summary_for_locale (comp, "xxx");
	g_assert_nonnull (text);
	g_assert_cmpstr (e_cal_component_text_get_value (text), ==, "summ-en-US");
	e_cal_component_text_free (text);

	g_clear_object (&comp);
}

static void
verify_component_summaries (ECalComponent *comp,
			    gpointer user_data)
{
	verify_component_text_list (e_cal_component_dup_summaries, comp, user_data);
}

static void
test_component_summaries (void)
{
	const gchar *comp_str =
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"SUMMARY;LANGUAGE=en-US:summ-en-US\r\n"
		"SUMMARY;LANGUAGE=en:summ-en\r\n"
		"SUMMARY;LANGUAGE=en-GB:summ-en-GB\r\n"
		"SUMMARY:summ\r\n"
		"END:VEVENT\r\n";
	ECalComponent *comp;
	ECalComponentText *text;
	GSList *slist1, *slist2;

	comp = e_cal_component_new_from_string (comp_str);
	g_assert_nonnull (comp);

	slist1 = e_cal_component_dup_summaries (comp);
	g_assert_cmpint (g_slist_length (slist1), ==, 4);

	text = e_cal_component_text_new ("summary", NULL);
	e_cal_component_set_summary (comp, text);
	slist2 = e_cal_component_dup_summaries (comp);
	g_assert_cmpint (g_slist_length (slist2), ==, 1);
	verify_struct_text_equal (text, slist2->data);
	g_slist_free_full (slist2, e_cal_component_text_free);
	e_cal_component_text_free (text);

	e_cal_component_set_summaries (comp, slist1);
	verify_changes (comp, verify_component_summaries, slist1);

	g_slist_free_full (slist1, e_cal_component_text_free);
	g_clear_object (&comp);
}

static void
verify_component_transparency (ECalComponent *comp,
			       gpointer user_data)
{
	ECalComponentTransparency expected, received;

	expected = GPOINTER_TO_INT (user_data);
	received = e_cal_component_get_transparency (comp);

	g_assert_cmpint (expected, ==, received);
}

static void
test_component_transparency (void)
{
	ECalComponentTransparency values[] = {
		E_CAL_COMPONENT_TRANSP_TRANSPARENT,
		E_CAL_COMPONENT_TRANSP_NONE,
		E_CAL_COMPONENT_TRANSP_OPAQUE
	};
	ECalComponent *comp;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		e_cal_component_set_transparency (comp, values[ii]);
		verify_changes (comp, verify_component_transparency, GINT_TO_POINTER (values[ii]));
	}

	g_object_unref (comp);
}

static void
verify_component_url (ECalComponent *comp,
		      gpointer user_data)
{
	gchar *expected, *received;

	expected = user_data;
	received = e_cal_component_get_url (comp);

	g_assert_cmpstr (expected, ==, received);

	g_free (received);
}

static void
test_component_url (void)
{
	const gchar *values[] = {
		"https://www.gnome.org",
		NULL,
		"https://gitlab.gnome.org/GNOME/evolution/-/wikis/home"
	};
	ECalComponent *comp;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		e_cal_component_set_url (comp, values[ii]);
		verify_changes (comp, verify_component_url, (gpointer) values[ii]);
	}

	g_object_unref (comp);
}

static void
verify_component_attendees (ECalComponent *comp,
			    gpointer user_data)
{
	GSList *expected, *received;

	expected = user_data;
	received = e_cal_component_get_attendees (comp);

	if (!expected) {
		g_assert_null (received);
		g_assert_true (!e_cal_component_has_attendees (comp));
	} else {
		GSList *link1, *link2;

		g_assert_nonnull (received);
		g_assert_true (e_cal_component_has_attendees (comp));

		g_assert_cmpint (g_slist_length (expected), ==, g_slist_length (received));

		for (link1 = expected, link2 = received; link1 && link2; link1 = g_slist_next (link1), link2 = g_slist_next (link2)) {
			ECalComponentAttendee *att_expected = link1->data, *att_received = link2->data;

			verify_struct_attendee_equal (att_expected, att_received);
		}

		g_assert_true (link1 == link2);
	}

	g_slist_free_full (received, e_cal_component_attendee_free);
}

static void
test_component_attendees (void)
{
	struct _values {
		const gchar *value;
		const gchar *member;
		ICalParameterCutype cutype;
		ICalParameterRole role;
		ICalParameterPartstat partstat;
		gboolean rsvp;
		const gchar *delegatedfrom;
		const gchar *delegatedto;
		const gchar *sentby;
		const gchar *cn;
		const gchar *language;
		gboolean with_parameters;
	} values[] = {
		{ "mailto:att1",
		   "member",
		   I_CAL_CUTYPE_INDIVIDUAL,
		   I_CAL_ROLE_CHAIR,
		   I_CAL_PARTSTAT_NEEDSACTION,
		   FALSE,
		   "mailto:delgfrom",
		   "mailto:delgto",
		   "mailto:sentby",
		   "First attendee",
		   "en_US",
		   FALSE },
		{ NULL, NULL, I_CAL_CUTYPE_NONE, I_CAL_ROLE_NONE, I_CAL_PARTSTAT_NONE, FALSE, NULL, NULL, NULL, NULL, NULL, FALSE }, /* terminator */
		{ NULL, NULL, I_CAL_CUTYPE_NONE, I_CAL_ROLE_NONE, I_CAL_PARTSTAT_NONE, FALSE, NULL, NULL, NULL, NULL, NULL, FALSE }, /* terminator */
		{ "mailto:room",
		   NULL,
		   I_CAL_CUTYPE_ROOM,
		   I_CAL_ROLE_REQPARTICIPANT,
		   I_CAL_PARTSTAT_ACCEPTED,
		   FALSE,
		   NULL,
		   NULL,
		   NULL,
		   "Meeting room",
		   NULL,
		   TRUE },
		{ "mailto:att2",
		   NULL,
		   I_CAL_CUTYPE_INDIVIDUAL,
		   I_CAL_ROLE_REQPARTICIPANT,
		   I_CAL_PARTSTAT_TENTATIVE,
		   TRUE,
		   NULL,
		   NULL,
		   NULL,
		   NULL,
		   "en_US",
		   FALSE },
		{ NULL, NULL, I_CAL_CUTYPE_NONE, I_CAL_ROLE_NONE, I_CAL_PARTSTAT_NONE, FALSE, NULL, NULL, NULL, NULL, NULL, FALSE } /* terminator */
	};
	ECalComponent *comp;
	GSList *attendees = NULL;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		if (values[ii].value) {
			ECalComponentAttendee *att;

			att = e_cal_component_attendee_new_full (values[ii].value,
				values[ii].member,
				values[ii].cutype,
				values[ii].role,
				values[ii].partstat,
				values[ii].rsvp,
				values[ii].delegatedfrom,
				values[ii].delegatedto,
				values[ii].sentby,
				values[ii].cn,
				values[ii].language);

			if (values[ii].with_parameters)
				test_component_add_params_to_bag (e_cal_component_attendee_get_parameter_bag (att), ii + 1);

			attendees = g_slist_prepend (attendees, att);
		} else {
			attendees = g_slist_reverse (attendees);

			e_cal_component_set_attendees (comp, attendees);
			verify_changes (comp, verify_component_attendees, attendees);

			g_slist_free_full (attendees, e_cal_component_attendee_free);
			attendees = NULL;
		}
	}

	g_object_unref (comp);

	g_assert_null (attendees);
}

static void
verify_component_location (ECalComponent *comp,
			   gpointer user_data)
{
	gchar *expected, *received;

	expected = user_data;
	received = e_cal_component_get_location (comp);

	g_assert_cmpstr (expected, ==, received);

	g_free (received);
}

static void
test_component_location (void)
{
	const gchar *values[] = {
		"Headquarter",
		NULL,
		"Phone call, +123-456-7890"
	};
	ECalComponent *comp;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		e_cal_component_set_location (comp, values[ii]);
		verify_changes (comp, verify_component_location, (gpointer) values[ii]);
	}

	g_object_unref (comp);
}

static void
verify_component_attachments (ECalComponent *comp,
			      gpointer user_data)
{
	GSList *expected, *received;

	expected = user_data;
	received = e_cal_component_get_attachments (comp);

	if (!expected) {
		g_assert_null (received);
		g_assert_true (!e_cal_component_has_attachments (comp));
	} else {
		ECalComponent *clone;

		g_assert_nonnull (received);
		g_assert_true (e_cal_component_has_attachments (comp));

		verify_ical_attach_list_equal (expected, received);

		/* Also test whether the attachments can be accessed after the component is freed */
		clone = e_cal_component_clone (comp);
		g_assert_nonnull (clone);

		g_slist_free_full (received, g_object_unref);
		received = e_cal_component_get_attachments (clone);

		g_object_unref (clone);

		g_assert_nonnull (received);
		g_assert_cmpint (g_slist_length (expected), ==, g_slist_length (received));

		verify_ical_attach_list_equal (expected, received);
	}

	g_slist_free_full (received, g_object_unref);
}

static void
test_component_attachments (void)
{
	struct _values {
		gboolean is_url;
		const gchar *content;
	} values[] = {
		{ TRUE, "https://www.gnome.org/index.html" },
		{ FALSE, NULL }, /* terminator */
		{ FALSE, NULL }, /* terminator */
		{ FALSE, "0123456789ABCDEF" },
		{ TRUE, "http://www.example.com/files/data.dat" },
		{ FALSE, NULL } /* terminator */
	};
	ECalComponent *comp;
	GSList *attachments = NULL;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		if (values[ii].content) {
			ICalAttach *attach;

			if (values[ii].is_url)
				attach = i_cal_attach_new_from_url (values[ii].content);
			else
				attach = i_cal_attach_new_from_data (values[ii].content, NULL, NULL);

			g_assert_nonnull (attach);

			attachments = g_slist_prepend (attachments, attach);
		} else {
			attachments = g_slist_reverse (attachments);

			e_cal_component_set_attachments (comp, attachments);
			verify_changes (comp, verify_component_attachments, attachments);

			g_slist_free_full (attachments, g_object_unref);
			attachments = NULL;
		}
	}

	g_object_unref (comp);

	g_assert_null (attachments);
}

static void
verify_component_alarms (ECalComponent *comp,
			 gpointer user_data)
{
	GSList *expected, *received, *received_uids;

	expected = user_data;
	received = e_cal_component_get_all_alarms (comp);
	received_uids = e_cal_component_get_alarm_uids (comp);

	if (!expected) {
		g_assert_null (received);
		g_assert_null (received_uids);
		g_assert_true (!e_cal_component_has_alarms (comp));
	} else {
		GSList *link1, *link2, *link3;

		g_assert_nonnull (received);
		g_assert_nonnull (received_uids);
		g_assert_true (e_cal_component_has_alarms (comp));

		g_assert_cmpint (g_slist_length (expected), ==, g_slist_length (received));
		g_assert_cmpint (g_slist_length (expected), ==, g_slist_length (received_uids));

		for (link1 = expected, link2 = received, link3 = received_uids;
		     link1 && link2 && link3;
		     link1 = g_slist_next (link1), link2 = g_slist_next (link2), link3 = g_slist_next (link3)) {
			const ECalComponentAlarm *al_expected = link1->data;
			const ECalComponentAlarm *al_received = link2->data;
			const gchar *uid_received = link3->data;

			g_assert_nonnull (al_expected);
			g_assert_nonnull (al_received);
			g_assert_nonnull (uid_received);

			g_assert_cmpstr (e_cal_component_alarm_get_uid (al_expected), ==, uid_received);
			g_assert_cmpstr (e_cal_component_alarm_get_uid (al_expected), ==, e_cal_component_alarm_get_uid (al_received));

			verify_struct_alarm_equal (al_expected, al_received);
		}

		g_assert_true (link1 == link2);
		g_assert_true (link1 == link3);

		if (expected->next) {
			ECalComponentAlarm *al_expected;
			ECalComponentAlarm *al_received;

			al_expected = expected->next->data;
			al_received = e_cal_component_get_alarm (comp, e_cal_component_alarm_get_uid (al_expected));

			g_assert_nonnull (al_expected);
			g_assert_nonnull (al_received);
			g_assert_cmpstr (e_cal_component_alarm_get_uid (al_expected), ==, e_cal_component_alarm_get_uid (al_received));

			e_cal_component_remove_alarm (comp, e_cal_component_alarm_get_uid (al_expected));

			e_cal_component_alarm_free (al_received);

			g_slist_free_full (received, e_cal_component_alarm_free);
			g_slist_free_full (received_uids, g_free);

			g_assert_true (e_cal_component_has_alarms (comp));

			received = e_cal_component_get_all_alarms (comp);
			received_uids = e_cal_component_get_alarm_uids (comp);

			g_assert_cmpint (g_slist_length (expected) - 1, ==, g_slist_length (received));
			g_assert_cmpint (g_slist_length (expected) - 1, ==, g_slist_length (received_uids));

			for (link1 = received, link2 = received_uids;
			     link1 && link2;
			     link1 = g_slist_next (link1), link2 = g_slist_next (link2)) {
				const gchar *uid_received;

				al_received = link1->data;
				uid_received = link2->data;

				g_assert_nonnull (al_received);
				g_assert_nonnull (uid_received);
				g_assert_cmpstr (e_cal_component_alarm_get_uid (al_received), ==, uid_received);
				g_assert_cmpstr (e_cal_component_alarm_get_uid (al_expected), !=, uid_received);
				g_assert_cmpstr (e_cal_component_alarm_get_uid (al_expected), !=, e_cal_component_alarm_get_uid (al_received));
			}

			g_assert_true (link1 == link2);
		} else {
			e_cal_component_remove_alarm (comp, e_cal_component_alarm_get_uid (expected->data));

			g_assert_true (!e_cal_component_has_alarms (comp));
			g_assert_null (e_cal_component_get_all_alarms (comp));
			g_assert_null (e_cal_component_get_alarm_uids (comp));
		}
	}

	g_slist_free_full (received, e_cal_component_alarm_free);
	g_slist_free_full (received_uids, g_free);
}

static void
test_component_alarms (void)
{
	struct _values {
		const gchar *uid;
		ECalComponentAlarmAction action;
		const gchar *description;
		gint trigger;
		gboolean trigger_with_parameters;
	} values[] = {
		{ "alarm1", E_CAL_COMPONENT_ALARM_DISPLAY, "display reminder", -5, TRUE },
		{ NULL, E_CAL_COMPONENT_ALARM_NONE, NULL, 0, FALSE }, /* terminator */
		{ NULL, E_CAL_COMPONENT_ALARM_NONE, NULL, 0, FALSE }, /* terminator */
		{ "alarm2", E_CAL_COMPONENT_ALARM_AUDIO, "audio reminder", 10, FALSE },
		{ "alarm3", E_CAL_COMPONENT_ALARM_EMAIL, "email reminder", 0, TRUE },
		{ "alarm4", E_CAL_COMPONENT_ALARM_PROCEDURE, "procedure reminder", -30, FALSE },
		{ NULL, E_CAL_COMPONENT_ALARM_NONE, NULL, 0, FALSE }, /* terminator */
	};
	ECalComponent *comp;
	GSList *alarms = NULL;
	gint ii;

	comp = e_cal_component_new_vtype (E_CAL_COMPONENT_EVENT);
	g_assert_nonnull (comp);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		if (values[ii].uid) {
			ECalComponentAlarm *alarm;
			ECalComponentAlarmTriggerKind kind;
			ICalDuration *duration;

			duration = i_cal_duration_new_from_int (values[ii].trigger * 60);
			if (values[ii].trigger < 0)
				kind = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;
			else
				kind = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END;

			alarm = e_cal_component_alarm_new ();
			e_cal_component_alarm_set_uid (alarm, values[ii].uid);
			e_cal_component_alarm_set_action (alarm, values[ii].action);
			e_cal_component_alarm_take_description (alarm, e_cal_component_text_new (values[ii].description, NULL));
			e_cal_component_alarm_take_trigger (alarm, e_cal_component_alarm_trigger_new_relative (kind, duration));

			if (values[ii].trigger_with_parameters) {
				ECalComponentAlarmTrigger *trigger;

				trigger = e_cal_component_alarm_get_trigger (alarm);
				test_component_add_params_to_bag (e_cal_component_alarm_trigger_get_parameter_bag (trigger), ii + 1);
			}

			if (values[ii].trigger < 0) {
				ECalComponentPropertyBag *bag;
				ICalProperty *prop;

				bag = e_cal_component_alarm_get_property_bag (alarm);

				prop = i_cal_property_new_url ("https://www.gnome.org");
				e_cal_component_property_bag_take (bag, prop);

				prop = i_cal_property_new_carlevel (I_CAL_CARLEVEL_CARFULL1);
				e_cal_component_property_bag_take (bag, prop);

				g_assert_cmpint (e_cal_component_property_bag_get_count (bag), ==, 2);
			}

			e_cal_component_add_alarm (comp, alarm);
			alarms = g_slist_prepend (alarms, alarm);

			g_object_unref (duration);
		} else {
			alarms = g_slist_reverse (alarms);

			verify_changes (comp, verify_component_alarms, alarms);

			e_cal_component_remove_all_alarms (comp);
			g_slist_free_full (alarms, e_cal_component_alarm_free);
			alarms = NULL;
		}
	}

	g_object_unref (comp);

	g_assert_null (alarms);
}

static void
test_component_striperrors (void)
{
	const gchar *vevent =
		"BEGIN:VEVENT\r\n"
		"DTSTART:20150102T100000Z\r\n"
		"DTEND:20150102T100100Z\r\n"
		"DTSTAMP:20150108T132939Z\r\n"
		"UID:123\r\n"
		"CREATED:20150102T181646Z\r\n"
		"X-LIC-ERROR;X-LIC-ERRORTYPE=VALUE-PARSE-ERROR:No value for DESCRIPTION \r\n"
		" property. Removing entire property:\r\n"
		"LAST-MODIFIED:20150102T181722Z\r\n"
		"X-LIC-ERROR;X-LIC-ERRORTYPE=VALUE-PARSE-ERROR:No value for LOCATION \r\n"
		" property. Removing entire property:\r\n"
		"SUMMARY:with errors\r\n"
		"END:VEVENT\r\n";
	ECalComponent *comp;
	ICalComponent *icalcomp;

	comp = e_cal_component_new_from_string (vevent);
	g_assert_nonnull (comp);

	icalcomp = e_cal_component_get_icalcomponent (comp);
	g_assert_nonnull (icalcomp);

	g_assert_cmpint (i_cal_component_count_properties (icalcomp, I_CAL_XLICERROR_PROPERTY), ==, 2);

	e_cal_component_strip_errors (comp);

	g_assert_cmpint (i_cal_component_count_properties (icalcomp, I_CAL_XLICERROR_PROPERTY), ==, 0);

	g_object_unref (comp);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://bugzilla.gnome.org/");

	g_test_add_func ("/ECalComponent/struct/Alarm", test_component_struct_alarm);
	g_test_add_func ("/ECalComponent/struct/Alarms", test_component_struct_alarms);
	g_test_add_func ("/ECalComponent/struct/AlarmInstance", test_component_struct_alarm_instance);
	g_test_add_func ("/ECalComponent/struct/AlarmRepeat", test_component_struct_alarm_repeat);
	g_test_add_func ("/ECalComponent/struct/AlarmTrigger", test_component_struct_alarm_trigger);
	g_test_add_func ("/ECalComponent/struct/Attendee", test_component_struct_attendee);
	g_test_add_func ("/ECalComponent/struct/DateTime", test_component_struct_datetime);
	g_test_add_func ("/ECalComponent/struct/Id", test_component_struct_id);
	g_test_add_func ("/ECalComponent/struct/Organizer", test_component_struct_organizer);
	g_test_add_func ("/ECalComponent/struct/ParameterBag", test_component_struct_parameter_bag);
	g_test_add_func ("/ECalComponent/struct/Period", test_component_struct_period);
	g_test_add_func ("/ECalComponent/struct/PropertyBag", test_component_struct_property_bag);
	g_test_add_func ("/ECalComponent/struct/Range", test_component_struct_range);
	g_test_add_func ("/ECalComponent/struct/Text", test_component_struct_text);
	g_test_add_func ("/ECalComponent/vtype", test_component_vtype);
	g_test_add_func ("/ECalComponent/uid", test_component_uid);
	g_test_add_func ("/ECalComponent/categories", test_component_categories);
	g_test_add_func ("/ECalComponent/classification", test_component_classification);
	g_test_add_func ("/ECalComponent/comments", test_component_comments);
	g_test_add_func ("/ECalComponent/comments-locale", test_component_comments_locale);
	g_test_add_func ("/ECalComponent/completed", test_component_completed);
	g_test_add_func ("/ECalComponent/contacts", test_component_contacts);
	g_test_add_func ("/ECalComponent/created", test_component_created);
	g_test_add_func ("/ECalComponent/descriptions", test_component_descriptions);
	g_test_add_func ("/ECalComponent/descriptions-locale", test_component_descriptions_locale);
	g_test_add_func ("/ECalComponent/dtend", test_component_dtend);
	g_test_add_func ("/ECalComponent/dtstamp", test_component_dtstamp);
	g_test_add_func ("/ECalComponent/dtstart", test_component_dtstart);
	g_test_add_func ("/ECalComponent/due", test_component_due);
	g_test_add_func ("/ECalComponent/exdates", test_component_exdates);
	g_test_add_func ("/ECalComponent/exrules", test_component_exrules);
	g_test_add_func ("/ECalComponent/geo", test_component_geo);
	g_test_add_func ("/ECalComponent/lastmodified", test_component_lastmodified);
	g_test_add_func ("/ECalComponent/organizer", test_component_organizer);
	g_test_add_func ("/ECalComponent/percentcomplete", test_component_percentcomplete);
	g_test_add_func ("/ECalComponent/priority", test_component_priority);
	g_test_add_func ("/ECalComponent/recurid", test_component_recurid);
	g_test_add_func ("/ECalComponent/rdates", test_component_rdates);
	g_test_add_func ("/ECalComponent/rrules", test_component_rrules);
	g_test_add_func ("/ECalComponent/sequence", test_component_sequence);
	g_test_add_func ("/ECalComponent/status", test_component_status);
	g_test_add_func ("/ECalComponent/summary", test_component_summary);
	g_test_add_func ("/ECalComponent/summaries", test_component_summaries);
	g_test_add_func ("/ECalComponent/summary-locale", test_component_summary_locale);
	g_test_add_func ("/ECalComponent/transparency", test_component_transparency);
	g_test_add_func ("/ECalComponent/url", test_component_url);
	g_test_add_func ("/ECalComponent/attendees", test_component_attendees);
	g_test_add_func ("/ECalComponent/location", test_component_location);
	g_test_add_func ("/ECalComponent/attachments", test_component_attachments);
	g_test_add_func ("/ECalComponent/alarms", test_component_alarms);
	g_test_add_func ("/ECalComponent/striperrors", test_component_striperrors);

	return g_test_run ();
}

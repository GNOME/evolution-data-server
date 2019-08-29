/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2019 Red Hat (www.redhat.com)
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

typedef struct _ExpectedInstance {
	const gchar *instance_time_str;
	const gchar *occur_start_str;
	const gchar *occur_end_str;
	gboolean filled;
	time_t instance_time;
	time_t occur_start;
	time_t occur_end;
} ExpectedInstance;

static ICalTimezone *
resolve_tzid (const gchar *tzid,
	      gpointer user_data,
	      GCancellable *cancellable,
	      GError **error)
{
	if (!tzid || !*tzid)
		return NULL;

	return i_cal_timezone_get_builtin_timezone (tzid);
}

static time_t
test_str_to_timet (const gchar *str,
		   ICalTimezone *zone)
{
	ICalTime *itt;
	time_t tt;

	g_assert_nonnull (str);

	itt = i_cal_time_new_from_string (str);
	tt = i_cal_time_as_timet_with_zone (itt, zone);
	g_clear_object (&itt);

	return tt;
}

static void
test_reminders_verify (const gchar *comp_str,
		       const gchar *interval_start,
		       const gchar *interval_end,
		       ExpectedInstance *expected_instances, /* in/out, the function modifies the 'filled' part */
		       guint n_expected_instances)
{
	ICalTimezone *default_zone;
	ECalComponent *ecomp;
	ECalComponentAlarms *alarms;
	ECalComponentAlarmAction omit[] = {-1};
	time_t start, end;

	g_assert_nonnull (comp_str);
	g_assert_nonnull (interval_start);
	g_assert_nonnull (interval_end);

	if (n_expected_instances)
		g_assert_nonnull (expected_instances);

	ecomp = e_cal_component_new_from_string (comp_str);
	g_assert_nonnull (ecomp);

	default_zone = i_cal_timezone_get_utc_timezone ();

	start = test_str_to_timet (interval_start, default_zone);
	end = test_str_to_timet (interval_end, default_zone);

	alarms = e_cal_util_generate_alarms_for_comp (
		ecomp, start, end, omit,
		resolve_tzid, NULL, default_zone);

	if (!alarms) {
		g_assert_cmpint (n_expected_instances, ==, 0);
	} else {
		GHashTable *used_indexes; /* GUINT_TO_POINTER(index) ~> NULL */
		GSList *received_instances, *link;

		received_instances = e_cal_component_alarms_get_instances (alarms);
		g_assert_cmpint (n_expected_instances, ==, g_slist_length (received_instances));

		used_indexes = g_hash_table_new (g_direct_hash, g_direct_equal);

		for (link = received_instances; link; link = g_slist_next (link)) {
			ECalComponentAlarmInstance *instance = link->data;
			gboolean found = FALSE;
			guint ii;

			g_assert_nonnull (instance);

			for (ii = 0; ii < n_expected_instances && !found; ii++) {
				if (g_hash_table_contains (used_indexes, GUINT_TO_POINTER (ii)))
					continue;

				if (!expected_instances[ii].filled) {
					expected_instances[ii].filled = TRUE;
					expected_instances[ii].instance_time = test_str_to_timet (expected_instances[ii].instance_time_str, default_zone);
					expected_instances[ii].occur_start = test_str_to_timet (expected_instances[ii].occur_start_str, default_zone);
					expected_instances[ii].occur_end = test_str_to_timet (expected_instances[ii].occur_end_str, default_zone);
				}

				found = expected_instances[ii].instance_time == e_cal_component_alarm_instance_get_time (instance) &&
					expected_instances[ii].occur_start == e_cal_component_alarm_instance_get_occur_start (instance) &&
					expected_instances[ii].occur_end == e_cal_component_alarm_instance_get_occur_end (instance);

				if (found)
					g_hash_table_insert (used_indexes, GUINT_TO_POINTER (ii), NULL);
			}

			g_assert (found);
		}

		g_assert_cmpint (n_expected_instances, ==, g_hash_table_size (used_indexes));

		g_hash_table_destroy (used_indexes);
	}

	e_cal_component_alarms_free (alarms);
	g_clear_object (&ecomp);

}

static const gchar *vevent_none =
	"BEGIN:VEVENT\r\n"
	"UID:123\r\n"
	"CREATED:20190829T000000Z\r\n"
	"LAST-MODIFIED:20190829T000000Z\r\n"
	"DTSTAMP:20190829T000000Z\r\n"
	"SUMMARY:test\r\n"
	"DTSTART:20190829T100000Z\r\n"
	"DTEND:20190829T100100Z\r\n"
	"END:VEVENT\r\n";

static const gchar *vtodo_none =
	"BEGIN:VTODO\r\n"
	"UID:123\r\n"
	"CREATED:20190829T000000Z\r\n"
	"LAST-MODIFIED:20190829T000000Z\r\n"
	"DTSTAMP:20190829T000000Z\r\n"
	"SUMMARY:test\r\n"
	"DTSTART:20190829T100000Z\r\n"
	"DUE:20190829T100100Z\r\n"
	"END:VEVENT\r\n";

#define DECLARE_COMPONENT(_var_name, _kind, _props, _trigger) \
	static const gchar *(_var_name) = \
		"BEGIN:" _kind "\r\n" \
		"UID:123\r\n" \
		"CREATED:20190829T000000Z\r\n" \
		"LAST-MODIFIED:20190829T000000Z\r\n" \
		"DTSTAMP:20190829T000000Z\r\n" \
		"SUMMARY:test\r\n" \
		_props \
		"BEGIN:VALARM\r\n" \
		"X-EVOLUTION-ALARM-UID:123-1\r\n" \
		"ACTION:DISPLAY\r\n" \
		"DESCRIPTION:test\r\n" \
		"TRIGGER" _trigger "\r\n" \
		"END:VALARM\r\n" \
		"END:" _kind "\r\n"

static void
test_reminders_start (void)
{
#define DECLARE_COMPONENT_AFTER_START(_var_name, _kind, _props) \
	DECLARE_COMPONENT(_var_name, _kind, _props, ";RELATED=START:PT1H")
#define DECLARE_COMPONENT_BEFORE_START(_var_name, _kind, _props) \
	DECLARE_COMPONENT(_var_name, _kind, _props, ";RELATED=START:-PT1H")

	DECLARE_COMPONENT_BEFORE_START (vevent_b, "VEVENT",
		"DTSTART:20190829T100000Z\r\n"
		"DTEND:20190829T100100Z\r\n");
	ExpectedInstance vevent_b_ei[] = {
		{ "20190829T090000Z", "20190829T100000Z", "20190829T100100Z", FALSE, }
	};

	DECLARE_COMPONENT_BEFORE_START (vtodo_no_time_b, "VTODO", "");

	DECLARE_COMPONENT_BEFORE_START (vtodo_start_b, "VTODO",
		"DTSTART:20190829T100000Z\r\n");
	ExpectedInstance vtodo_start_b_ei[] = {
		{ "20190829T090000Z", "20190829T100000Z", "20190829T100000Z", FALSE, }
	};

	DECLARE_COMPONENT_BEFORE_START (vtodo_due_b, "VTODO",
		"DUE:20190829T100000Z\r\n");
	ExpectedInstance vtodo_due_b_ei[] = {
		{ "20190829T090000Z", "20190829T100000Z", "20190829T100000Z", FALSE, }
	};

	DECLARE_COMPONENT_BEFORE_START (vtodo_start_due_b, "VTODO",
		"DTSTART:20190829T110000Z\r\n"
		"DUE:20190830T100000Z\r\n");
	ExpectedInstance vtodo_start_due_b_ei[] = {
		{ "20190829T100000Z", "20190829T110000Z", "20190830T100000Z", FALSE, }
	};

	DECLARE_COMPONENT_AFTER_START (vevent_a, "VEVENT",
		"DTSTART:20190829T100000Z\r\n"
		"DTEND:20190829T100100Z\r\n");
	ExpectedInstance vevent_a_ei[] = {
		{ "20190829T110000Z", "20190829T100000Z", "20190829T100100Z", FALSE, }
	};

	DECLARE_COMPONENT_AFTER_START (vtodo_no_time_a, "VTODO", "");

	DECLARE_COMPONENT_AFTER_START (vtodo_start_a, "VTODO",
		"DTSTART:20190829T100000Z\r\n");
	ExpectedInstance vtodo_start_a_ei[] = {
		{ "20190829T110000Z", "20190829T100000Z", "20190829T100000Z", FALSE, }
	};

	DECLARE_COMPONENT_AFTER_START (vtodo_due_a, "VTODO",
		"DUE:20190829T100000Z\r\n");
	ExpectedInstance vtodo_due_a_ei[] = {
		{ "20190829T110000Z", "20190829T100000Z", "20190829T100000Z", FALSE, }
	};

	DECLARE_COMPONENT_AFTER_START (vtodo_start_due_a, "VTODO",
		"DTSTART:20190829T110000Z\r\n"
		"DUE:20190830T100000Z\r\n");
	ExpectedInstance vtodo_start_due_a_ei[] = {
		{ "20190829T120000Z", "20190829T110000Z", "20190830T100000Z", FALSE, }
	};

	test_reminders_verify (vevent_none, "20190820T000000Z", "20190830T000000Z", NULL, 0);
	test_reminders_verify (vtodo_none, "20190820T000000Z", "20190830T000000Z", NULL, 0);

	test_reminders_verify (vevent_b, "20190828T090000Z", "20190829T090000Z", NULL, 0);
	test_reminders_verify (vevent_b, "20190829T090100Z", "20190829T100000Z", NULL, 0);
	test_reminders_verify (vevent_b, "20190829T000000Z", "20190830T000000Z", vevent_b_ei, G_N_ELEMENTS (vevent_b_ei));
	test_reminders_verify (vevent_b, "20190828T090000Z", "20190829T090100Z", vevent_b_ei, G_N_ELEMENTS (vevent_b_ei));
	test_reminders_verify (vevent_b, "20190829T085900Z", "20190829T100000Z", vevent_b_ei, G_N_ELEMENTS (vevent_b_ei));
	test_reminders_verify (vevent_b, "20190829T085900Z", "20190829T090100Z", vevent_b_ei, G_N_ELEMENTS (vevent_b_ei));
	test_reminders_verify (vtodo_no_time_b, "20190828T000000Z", "20190830T000000Z", NULL, 0);
	test_reminders_verify (vtodo_start_b, "20190828T090000Z", "20190829T090000Z", NULL, 0);
	test_reminders_verify (vtodo_start_b, "20190829T090100Z", "20190829T100000Z", NULL, 0);
	test_reminders_verify (vtodo_start_b, "20190829T000000Z", "20190830T000000Z", vtodo_start_b_ei, G_N_ELEMENTS (vtodo_start_b_ei));
	test_reminders_verify (vtodo_start_b, "20190828T090000Z", "20190829T090100Z", vtodo_start_b_ei, G_N_ELEMENTS (vtodo_start_b_ei));
	test_reminders_verify (vtodo_start_b, "20190829T085900Z", "20190829T100000Z", vtodo_start_b_ei, G_N_ELEMENTS (vtodo_start_b_ei));
	test_reminders_verify (vtodo_start_b, "20190829T085900Z", "20190829T090100Z", vtodo_start_b_ei, G_N_ELEMENTS (vtodo_start_b_ei));
	test_reminders_verify (vtodo_due_b, "20190828T090000Z", "20190829T090000Z", NULL, 0);
	test_reminders_verify (vtodo_due_b, "20190829T090100Z", "20190829T100000Z", NULL, 0);
	test_reminders_verify (vtodo_due_b, "20190829T000000Z", "20190830T000000Z", vtodo_due_b_ei, G_N_ELEMENTS (vtodo_due_b_ei));
	test_reminders_verify (vtodo_due_b, "20190828T090000Z", "20190829T090100Z", vtodo_due_b_ei, G_N_ELEMENTS (vtodo_due_b_ei));
	test_reminders_verify (vtodo_due_b, "20190829T085900Z", "20190829T100000Z", vtodo_due_b_ei, G_N_ELEMENTS (vtodo_due_b_ei));
	test_reminders_verify (vtodo_due_b, "20190829T085900Z", "20190829T090100Z", vtodo_due_b_ei, G_N_ELEMENTS (vtodo_due_b_ei));
	test_reminders_verify (vtodo_start_due_b, "20190828T090000Z", "20190829T090000Z", NULL, 0);
	test_reminders_verify (vtodo_start_due_b, "20190829T090100Z", "20190829T100000Z", NULL, 0);
	test_reminders_verify (vtodo_start_due_b, "20190829T000000Z", "20190830T000000Z", vtodo_start_due_b_ei, G_N_ELEMENTS (vtodo_start_due_b_ei));
	test_reminders_verify (vtodo_start_due_b, "20190828T090000Z", "20190829T100100Z", vtodo_start_due_b_ei, G_N_ELEMENTS (vtodo_start_due_b_ei));
	test_reminders_verify (vtodo_start_due_b, "20190829T095900Z", "20190829T110000Z", vtodo_start_due_b_ei, G_N_ELEMENTS (vtodo_start_due_b_ei));
	test_reminders_verify (vtodo_start_due_b, "20190829T095900Z", "20190829T100100Z", vtodo_start_due_b_ei, G_N_ELEMENTS (vtodo_start_due_b_ei));

	test_reminders_verify (vevent_a, "20190828T110000Z", "20190829T110000Z", NULL, 0);
	test_reminders_verify (vevent_a, "20190829T110100Z", "20190829T120000Z", NULL, 0);
	test_reminders_verify (vevent_a, "20190829T000000Z", "20190830T000000Z", vevent_a_ei, G_N_ELEMENTS (vevent_a_ei));
	test_reminders_verify (vevent_a, "20190828T110000Z", "20190829T110100Z", vevent_a_ei, G_N_ELEMENTS (vevent_a_ei));
	test_reminders_verify (vevent_a, "20190829T105900Z", "20191029T120000Z", vevent_a_ei, G_N_ELEMENTS (vevent_a_ei));
	test_reminders_verify (vevent_a, "20190829T105900Z", "20191029T110100Z", vevent_a_ei, G_N_ELEMENTS (vevent_a_ei));
	test_reminders_verify (vtodo_no_time_a, "20190828T000000Z", "20190830T000000Z", NULL, 0);
	test_reminders_verify (vtodo_start_a, "20190828T110000Z", "20190829T110000Z", NULL, 0);
	test_reminders_verify (vtodo_start_a, "20190829T110100Z", "20190829T120000Z", NULL, 0);
	test_reminders_verify (vtodo_start_a, "20190829T000000Z", "20190830T000000Z", vtodo_start_a_ei, G_N_ELEMENTS (vtodo_start_a_ei));
	test_reminders_verify (vtodo_start_a, "20190828T090000Z", "20190829T110100Z", vtodo_start_a_ei, G_N_ELEMENTS (vtodo_start_a_ei));
	test_reminders_verify (vtodo_start_a, "20190829T105900Z", "20190829T120000Z", vtodo_start_a_ei, G_N_ELEMENTS (vtodo_start_a_ei));
	test_reminders_verify (vtodo_start_a, "20190829T105900Z", "20190829T110100Z", vtodo_start_a_ei, G_N_ELEMENTS (vtodo_start_a_ei));
	test_reminders_verify (vtodo_due_a, "20190828T110000Z", "20190829T110000Z", NULL, 0);
	test_reminders_verify (vtodo_due_a, "20190829T110100Z", "20190829T120000Z", NULL, 0);
	test_reminders_verify (vtodo_due_a, "20190829T000000Z", "20190830T000000Z", vtodo_due_a_ei, G_N_ELEMENTS (vtodo_due_a_ei));
	test_reminders_verify (vtodo_due_a, "20190828T110000Z", "20190829T110100Z", vtodo_due_a_ei, G_N_ELEMENTS (vtodo_due_a_ei));
	test_reminders_verify (vtodo_due_a, "20190829T105900Z", "20190829T120000Z", vtodo_due_a_ei, G_N_ELEMENTS (vtodo_due_a_ei));
	test_reminders_verify (vtodo_due_a, "20190829T105900Z", "20190829T110100Z", vtodo_due_a_ei, G_N_ELEMENTS (vtodo_due_a_ei));
	test_reminders_verify (vtodo_start_due_a, "20190828T120000Z", "20190829T120000Z", NULL, 0);
	test_reminders_verify (vtodo_start_due_a, "20190829T120100Z", "20190829T130000Z", NULL, 0);
	test_reminders_verify (vtodo_start_due_a, "20190829T000000Z", "20190830T000000Z", vtodo_start_due_a_ei, G_N_ELEMENTS (vtodo_start_due_a_ei));
	test_reminders_verify (vtodo_start_due_a, "20190828T120000Z", "20190829T120100Z", vtodo_start_due_a_ei, G_N_ELEMENTS (vtodo_start_due_a_ei));
	test_reminders_verify (vtodo_start_due_a, "20190829T115900Z", "20190829T130000Z", vtodo_start_due_a_ei, G_N_ELEMENTS (vtodo_start_due_a_ei));
	test_reminders_verify (vtodo_start_due_a, "20190829T115900Z", "20190829T120100Z", vtodo_start_due_a_ei, G_N_ELEMENTS (vtodo_start_due_a_ei));

#undef DECLARE_COMPONENT_AFTER_START
#undef DECLARE_COMPONENT_BEFORE_START
}

static void
test_reminders_end (void)
{
#define DECLARE_COMPONENT_AFTER_END(_var_name, _kind, _props) \
	DECLARE_COMPONENT(_var_name, _kind, _props, ";RELATED=END:PT1H")
#define DECLARE_COMPONENT_BEFORE_END(_var_name, _kind, _props) \
	DECLARE_COMPONENT(_var_name, _kind, _props, ";RELATED=END:-PT1H")

	DECLARE_COMPONENT_BEFORE_END (vevent_b, "VEVENT",
		"DTSTART:20190829T100000Z\r\n"
		"DTEND:20190829T100100Z\r\n");
	ExpectedInstance vevent_b_ei[] = {
		{ "20190829T090100Z", "20190829T100000Z", "20190829T100100Z", FALSE, }
	};

	DECLARE_COMPONENT_BEFORE_END (vtodo_no_time_b, "VTODO", "");

	DECLARE_COMPONENT_BEFORE_END (vtodo_start_b, "VTODO",
		"DTSTART:20190829T100000Z\r\n");
	ExpectedInstance vtodo_start_b_ei[] = {
		{ "20190829T090000Z", "20190829T100000Z", "20190829T100000Z", FALSE, }
	};

	DECLARE_COMPONENT_BEFORE_END (vtodo_due_b, "VTODO",
		"DUE:20190829T100000Z\r\n");
	ExpectedInstance vtodo_due_b_ei[] = {
		{ "20190829T090000Z", "20190829T100000Z", "20190829T100000Z", FALSE, }
	};

	DECLARE_COMPONENT_BEFORE_END (vtodo_start_due_b, "VTODO",
		"DTSTART:20190829T110000Z\r\n"
		"DUE:20190830T100000Z\r\n");
	ExpectedInstance vtodo_start_due_b_ei[] = {
		{ "20190830T090000Z", "20190829T110000Z", "20190830T100000Z", FALSE, }
	};

	DECLARE_COMPONENT_AFTER_END (vevent_a, "VEVENT",
		"DTSTART:20190829T100000Z\r\n"
		"DTEND:20190829T100100Z\r\n");
	ExpectedInstance vevent_a_ei[] = {
		{ "20190829T110100Z", "20190829T100000Z", "20190829T100100Z", FALSE, }
	};

	DECLARE_COMPONENT_AFTER_END (vtodo_no_time_a, "VTODO", "");

	DECLARE_COMPONENT_AFTER_END (vtodo_start_a, "VTODO",
		"DTSTART:20190829T100000Z\r\n");
	ExpectedInstance vtodo_start_a_ei[] = {
		{ "20190829T110000Z", "20190829T100000Z", "20190829T100000Z", FALSE, }
	};

	DECLARE_COMPONENT_AFTER_END (vtodo_due_a, "VTODO",
		"DUE:20190829T100000Z\r\n");
	ExpectedInstance vtodo_due_a_ei[] = {
		{ "20190829T110000Z", "20190829T100000Z", "20190829T100000Z", FALSE, }
	};

	DECLARE_COMPONENT_AFTER_END (vtodo_start_due_a, "VTODO",
		"DTSTART:20190829T110000Z\r\n"
		"DUE:20190830T100000Z\r\n");
	ExpectedInstance vtodo_start_due_a_ei[] = {
		{ "20190830T110000Z", "20190829T110000Z", "20190830T100000Z", FALSE, }
	};

	test_reminders_verify (vevent_none, "20190820T000000Z", "20190830T000000Z", NULL, 0);
	test_reminders_verify (vtodo_none, "20190820T000000Z", "20190830T000000Z", NULL, 0);

	test_reminders_verify (vevent_b, "20190828T090100Z", "20190829T090100Z", NULL, 0);
	test_reminders_verify (vevent_b, "20190829T090200Z", "20190829T100000Z", NULL, 0);
	test_reminders_verify (vevent_b, "20190829T000000Z", "20190830T000000Z", vevent_b_ei, G_N_ELEMENTS (vevent_b_ei));
	test_reminders_verify (vevent_b, "20190828T090100Z", "20190829T090200Z", vevent_b_ei, G_N_ELEMENTS (vevent_b_ei));
	test_reminders_verify (vevent_b, "20190829T090000Z", "20190829T100000Z", vevent_b_ei, G_N_ELEMENTS (vevent_b_ei));
	test_reminders_verify (vevent_b, "20190829T090000Z", "20190829T090200Z", vevent_b_ei, G_N_ELEMENTS (vevent_b_ei));
	test_reminders_verify (vtodo_no_time_b, "20190828T000000Z", "20190830T000000Z", NULL, 0);
	test_reminders_verify (vtodo_start_b, "20190828T090000Z", "20190829T090000Z", NULL, 0);
	test_reminders_verify (vtodo_start_b, "20190829T090100Z", "20190829T100000Z", NULL, 0);
	test_reminders_verify (vtodo_start_b, "20190829T000000Z", "20190830T000000Z", vtodo_start_b_ei, G_N_ELEMENTS (vtodo_start_b_ei));
	test_reminders_verify (vtodo_start_b, "20190828T090000Z", "20190829T090100Z", vtodo_start_b_ei, G_N_ELEMENTS (vtodo_start_b_ei));
	test_reminders_verify (vtodo_start_b, "20190829T085900Z", "20190829T100000Z", vtodo_start_b_ei, G_N_ELEMENTS (vtodo_start_b_ei));
	test_reminders_verify (vtodo_start_b, "20190829T085900Z", "20190829T090100Z", vtodo_start_b_ei, G_N_ELEMENTS (vtodo_start_b_ei));
	test_reminders_verify (vtodo_due_b, "20190828T090000Z", "20190829T090000Z", NULL, 0);
	test_reminders_verify (vtodo_due_b, "20190829T090100Z", "20190829T100000Z", NULL, 0);
	test_reminders_verify (vtodo_due_b, "20190829T000000Z", "20190830T000000Z", vtodo_due_b_ei, G_N_ELEMENTS (vtodo_due_b_ei));
	test_reminders_verify (vtodo_due_b, "20190828T090000Z", "20190829T090100Z", vtodo_due_b_ei, G_N_ELEMENTS (vtodo_due_b_ei));
	test_reminders_verify (vtodo_due_b, "20190829T085900Z", "20190829T100000Z", vtodo_due_b_ei, G_N_ELEMENTS (vtodo_due_b_ei));
	test_reminders_verify (vtodo_due_b, "20190829T085900Z", "20190829T090100Z", vtodo_due_b_ei, G_N_ELEMENTS (vtodo_due_b_ei));
	test_reminders_verify (vtodo_start_due_b, "20190829T090000Z", "20190830T090000Z", NULL, 0);
	test_reminders_verify (vtodo_start_due_b, "20190830T090100Z", "20190830T100000Z", NULL, 0);
	test_reminders_verify (vtodo_start_due_b, "20190830T000000Z", "20190831T000000Z", vtodo_start_due_b_ei, G_N_ELEMENTS (vtodo_start_due_b_ei));
	test_reminders_verify (vtodo_start_due_b, "20190829T090000Z", "20190830T100100Z", vtodo_start_due_b_ei, G_N_ELEMENTS (vtodo_start_due_b_ei));
	test_reminders_verify (vtodo_start_due_b, "20190830T085900Z", "20190830T100000Z", vtodo_start_due_b_ei, G_N_ELEMENTS (vtodo_start_due_b_ei));
	test_reminders_verify (vtodo_start_due_b, "20190830T085900Z", "20190830T100100Z", vtodo_start_due_b_ei, G_N_ELEMENTS (vtodo_start_due_b_ei));

	test_reminders_verify (vevent_a, "20190828T110100Z", "20190829T110100Z", NULL, 0);
	test_reminders_verify (vevent_a, "20190829T110200Z", "20190829T120000Z", NULL, 0);
	test_reminders_verify (vevent_a, "20190829T000000Z", "20190830T000000Z", vevent_a_ei, G_N_ELEMENTS (vevent_a_ei));
	test_reminders_verify (vevent_a, "20190828T110100Z", "20190829T110200Z", vevent_a_ei, G_N_ELEMENTS (vevent_a_ei));
	test_reminders_verify (vevent_a, "20190829T110000Z", "20191029T120000Z", vevent_a_ei, G_N_ELEMENTS (vevent_a_ei));
	test_reminders_verify (vevent_a, "20190829T110000Z", "20191029T110200Z", vevent_a_ei, G_N_ELEMENTS (vevent_a_ei));
	test_reminders_verify (vtodo_no_time_a, "20190828T000000Z", "20190830T000000Z", NULL, 0);
	test_reminders_verify (vtodo_start_a, "20190828T110000Z", "20190829T110000Z", NULL, 0);
	test_reminders_verify (vtodo_start_a, "20190829T110100Z", "20190829T120000Z", NULL, 0);
	test_reminders_verify (vtodo_start_a, "20190829T000000Z", "20190830T000000Z", vtodo_start_a_ei, G_N_ELEMENTS (vtodo_start_a_ei));
	test_reminders_verify (vtodo_start_a, "20190828T090000Z", "20190829T110100Z", vtodo_start_a_ei, G_N_ELEMENTS (vtodo_start_a_ei));
	test_reminders_verify (vtodo_start_a, "20190829T105900Z", "20190829T120000Z", vtodo_start_a_ei, G_N_ELEMENTS (vtodo_start_a_ei));
	test_reminders_verify (vtodo_start_a, "20190829T105900Z", "20190829T110100Z", vtodo_start_a_ei, G_N_ELEMENTS (vtodo_start_a_ei));
	test_reminders_verify (vtodo_due_a, "20190828T110000Z", "20190829T110000Z", NULL, 0);
	test_reminders_verify (vtodo_due_a, "20190829T110100Z", "20190829T120000Z", NULL, 0);
	test_reminders_verify (vtodo_due_a, "20190829T000000Z", "20190830T000000Z", vtodo_due_a_ei, G_N_ELEMENTS (vtodo_due_a_ei));
	test_reminders_verify (vtodo_due_a, "20190828T110000Z", "20190829T110100Z", vtodo_due_a_ei, G_N_ELEMENTS (vtodo_due_a_ei));
	test_reminders_verify (vtodo_due_a, "20190829T105900Z", "20190829T120000Z", vtodo_due_a_ei, G_N_ELEMENTS (vtodo_due_a_ei));
	test_reminders_verify (vtodo_due_a, "20190829T105900Z", "20190829T110100Z", vtodo_due_a_ei, G_N_ELEMENTS (vtodo_due_a_ei));
	test_reminders_verify (vtodo_start_due_a, "20190829T120000Z", "20190830T110000Z", NULL, 0);
	test_reminders_verify (vtodo_start_due_a, "20190830T110100Z", "20190830T130000Z", NULL, 0);
	test_reminders_verify (vtodo_start_due_a, "20190830T000000Z", "20190831T000000Z", vtodo_start_due_a_ei, G_N_ELEMENTS (vtodo_start_due_a_ei));
	test_reminders_verify (vtodo_start_due_a, "20190829T110000Z", "20190830T110100Z", vtodo_start_due_a_ei, G_N_ELEMENTS (vtodo_start_due_a_ei));
	test_reminders_verify (vtodo_start_due_a, "20190830T105900Z", "20190830T130000Z", vtodo_start_due_a_ei, G_N_ELEMENTS (vtodo_start_due_a_ei));
	test_reminders_verify (vtodo_start_due_a, "20190830T105900Z", "20190830T110100Z", vtodo_start_due_a_ei, G_N_ELEMENTS (vtodo_start_due_a_ei));

#undef DECLARE_COMPONENT_AFTER_END
#undef DECLARE_COMPONENT_BEFORE_END
}

static void
test_reminders_absolute (void)
{
#define DECLARE_COMPONENT_ABSOLUTE(_var_name, _kind, _props) \
	DECLARE_COMPONENT(_var_name, _kind, _props, ";VALUE=DATE-TIME:20190829T150500Z")

	DECLARE_COMPONENT_ABSOLUTE (vevent1, "VEVENT",
		"DTSTART:20190829T100000Z\r\n"
		"DTEND:20190829T100100Z\r\n");
	ExpectedInstance vevent1_ei[] = {
		{ "20190829T150500Z", "20190829T100000Z", "20190829T100100Z", FALSE, }
	};

	DECLARE_COMPONENT_ABSOLUTE (vevent2, "VEVENT",
		"DTSTART:20190829T180000Z\r\n"
		"DTEND:20190829T180100Z\r\n");
	ExpectedInstance vevent2_ei[] = {
		{ "20190829T150500Z", "20190829T180000Z", "20190829T180100Z", FALSE, }
	};

	DECLARE_COMPONENT_ABSOLUTE (vtodo_no_time, "VTODO", "");
	ExpectedInstance vtodo_no_time_ei[] = {
		{ "20190829T150500Z", "19691231T235959Z", "19691231T235959Z", FALSE, }
	};

	DECLARE_COMPONENT_ABSOLUTE (vtodo_start, "VTODO",
		"DTSTART:20190829T100000Z\r\n");
	ExpectedInstance vtodo_start_ei[] = {
		{ "20190829T150500Z", "20190829T100000Z", "19691231T235959Z", FALSE, }
	};

	DECLARE_COMPONENT_ABSOLUTE (vtodo_due, "VTODO",
		"DUE:20190829T100000Z\r\n");
	ExpectedInstance vtodo_due_ei[] = {
		{ "20190829T150500Z", "19691231T235959Z", "20190829T100000Z", FALSE, }
	};

	DECLARE_COMPONENT_ABSOLUTE (vtodo_start_due, "VTODO",
		"DTSTART:20190829T170000Z\r\n"
		"DUE:20190830T170000Z\r\n");
	ExpectedInstance vtodo_start_due_ei[] = {
		{ "20190829T150500Z", "20190829T170000Z", "20190830T170000Z", FALSE, }
	};

	struct _intervals {
		const gchar *start;
		const gchar *end;
		gboolean expects_result;
	} intervals[] = {
		{ "20190828T150500Z", "20190829T150500Z", FALSE },
		{ "20190829T150600Z", "20190829T160000Z", FALSE },
		{ "20190829T000000Z", "20190830T000000Z", TRUE },
		{ "20190828T150500Z", "20190829T150600Z", TRUE },
		{ "20190829T150500Z", "20190829T160000Z", TRUE },
		{ "20190829T150400Z", "20190829T150600Z", TRUE }
	};
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (intervals); ii++) {
		const gchar *start, *end;
		gboolean expects_result;

		start = intervals[ii].start;
		end = intervals[ii].end;
		expects_result = intervals[ii].expects_result;

		test_reminders_verify (vevent_none, start, end, NULL, 0);
		test_reminders_verify (vevent1, start, end, vevent1_ei, expects_result ? G_N_ELEMENTS (vevent1_ei) : 0);
		test_reminders_verify (vevent2, start, end, vevent2_ei, expects_result ? G_N_ELEMENTS (vevent2_ei) : 0);

		test_reminders_verify (vtodo_none,      start, end, NULL, 0);
		test_reminders_verify (vtodo_no_time,   start, end, vtodo_no_time_ei,   expects_result ? G_N_ELEMENTS (vtodo_no_time_ei) : 0);
		test_reminders_verify (vtodo_start,     start, end, vtodo_start_ei,     expects_result ? G_N_ELEMENTS (vtodo_start_ei) : 0);
		test_reminders_verify (vtodo_due,       start, end, vtodo_due_ei,       expects_result ? G_N_ELEMENTS (vtodo_due_ei) : 0);
		test_reminders_verify (vtodo_start_due, start, end, vtodo_start_due_ei, expects_result ? G_N_ELEMENTS (vtodo_start_due_ei) : 0);
	}

#undef DECLARE_COMPONENT_ABSOLUTE
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/issues/");

	g_test_add_func ("/ECal/Reminders/Start", test_reminders_start);
	g_test_add_func ("/ECal/Reminders/End", test_reminders_end);
	g_test_add_func ("/ECal/Reminders/Absolute", test_reminders_absolute);

	return g_test_run ();
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
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

#include <stdlib.h>
#include <string.h>
#include <libecal/libecal.h>

#include "e-test-server-utils.h"

static ETestServerClosure test_closure = { E_TEST_SERVER_NONE, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, FALSE };

#define DEF_SUBCOMP(x, dt) \
	"BEGIN:" x "\r\n" \
	"TZNAME:NM" x "\r\n" \
	"DTSTART:" dt "T230000\r\n" \
	"TZOFFSETFROM:+0100\r\n" \
	"TZOFFSETTO:+0200\r\n" \
	"RRULE:FREQ=YEARLY;UNTIL=" dt "T220000Z;BYDAY=-1SU;BYMONTH=4\r\n" \
	"END:" x "\r\n"

#define DEF_VTIMEZONE(location, content) \
	"BEGIN:VTIMEZONE\r\n" \
	"TZID:/id.no.where/" location "\r\n" \
	"X-LIC-LOCATION:" location "\r\n" \
	content \
	"END:VTIMEZONE\r\n"

static void
test_clamp_vtimezone (ETestServerFixture *fixture,
		      gconstpointer user_data)
{
	const gchar *vtimezone_str =
		DEF_VTIMEZONE ("Some/Location",
			DEF_SUBCOMP ("DAYLIGHT", "19810301")
			DEF_SUBCOMP ("STANDARD", "19811001")
			DEF_SUBCOMP ("DAYLIGHT", "19820301")
			DEF_SUBCOMP ("STANDARD", "19821001")
			DEF_SUBCOMP ("DAYLIGHT", "19830301")
			DEF_SUBCOMP ("STANDARD", "19831001")
			DEF_SUBCOMP ("DAYLIGHT", "19840301")
			DEF_SUBCOMP ("STANDARD", "19841001")
			DEF_SUBCOMP ("DAYLIGHT", "19850301")
			DEF_SUBCOMP ("STANDARD", "19851001")
		);
	ICalComponent *comp, *vtimezone;
	ICalTime *from, *to;

	vtimezone = i_cal_component_new_from_string (vtimezone_str);

	g_assert_nonnull (vtimezone);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XDAYLIGHT_COMPONENT), ==, 5);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XSTANDARD_COMPONENT), ==, 5);

	from = i_cal_time_new_from_string ("19830101T000000Z");
	to = i_cal_time_new_from_string ("19830815T000000Z");

	g_assert_nonnull (from);
	g_assert_nonnull (to);

	e_cal_util_clamp_vtimezone (vtimezone, from, NULL);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XDAYLIGHT_COMPONENT), ==, 4);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XSTANDARD_COMPONENT), ==, 4);

	g_object_unref (vtimezone);
	vtimezone = i_cal_component_new_from_string (vtimezone_str);

	e_cal_util_clamp_vtimezone (vtimezone, from, to);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XDAYLIGHT_COMPONENT), ==, 2);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XSTANDARD_COMPONENT), ==, 1);

	g_object_unref (vtimezone);
	vtimezone = i_cal_component_new_from_string (vtimezone_str);

	comp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"DTSTART;VALUE=DATE:19821003\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (comp);

	e_cal_util_clamp_vtimezone_by_component (vtimezone, comp);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XDAYLIGHT_COMPONENT), ==, 1);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XSTANDARD_COMPONENT), ==, 1);

	g_object_unref (comp);
	g_object_unref (vtimezone);
	vtimezone = i_cal_component_new_from_string (vtimezone_str);

	comp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"DTSTART;VALUE=DATE:19820903\r\n"
		"DTEND;VALUE=DATE:19831103\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (comp);

	e_cal_util_clamp_vtimezone_by_component (vtimezone, comp);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XDAYLIGHT_COMPONENT), ==, 2);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XSTANDARD_COMPONENT), ==, 3);

	g_object_unref (comp);
	g_object_unref (vtimezone);
	vtimezone = i_cal_component_new_from_string (vtimezone_str);

	comp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"DTSTART:19820903T080000Z\r\n"
		"DTEND:19820903T090000Z\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (comp);

	e_cal_util_clamp_vtimezone_by_component (vtimezone, comp);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XDAYLIGHT_COMPONENT), ==, 1);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XSTANDARD_COMPONENT), ==, 1);

	g_object_unref (comp);
	g_object_unref (vtimezone);
	vtimezone = i_cal_component_new_from_string (vtimezone_str);

	comp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"DTSTART:19820903T080000Z\r\n"
		"DTEND:19820903T090000Z\r\n"
		"RRULE:FREQ=DAILY;UNTIL=19840101T010000Z\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (comp);

	e_cal_util_clamp_vtimezone_by_component (vtimezone, comp);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XDAYLIGHT_COMPONENT), ==, 4);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XSTANDARD_COMPONENT), ==, 5);

	g_object_unref (comp);
	g_object_unref (vtimezone);
	vtimezone = i_cal_component_new_from_string (vtimezone_str);

	comp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"DTSTART:19821004T080000Z\r\n"
		"DTEND:19821004T090000Z\r\n"
		"RRULE:FREQ=DAILY;UNTIL=20000101T010000Z\r\n"
		"RECURRENCE-ID:19841004T090000Z\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (comp);

	e_cal_util_clamp_vtimezone_by_component (vtimezone, comp);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XDAYLIGHT_COMPONENT), ==, 3);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XSTANDARD_COMPONENT), ==, 3);

	g_object_unref (comp);
	g_object_unref (vtimezone);
	vtimezone = i_cal_component_new_from_string (vtimezone_str);

	comp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"DTSTART:20200104T080000Z\r\n"
		"DTEND:20200104T090000Z\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (comp);

	e_cal_util_clamp_vtimezone_by_component (vtimezone, comp);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XDAYLIGHT_COMPONENT), ==, 1);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XSTANDARD_COMPONENT), ==, 1);
	g_object_unref (comp);

	g_object_unref (from);
	g_object_unref (to);

	comp = i_cal_component_get_first_component (vtimezone, I_CAL_XDAYLIGHT_COMPONENT);
	g_assert_nonnull (comp);
	from = i_cal_component_get_dtstart (comp);
	g_assert_nonnull (from);
	g_assert_cmpint (i_cal_time_get_year (from), ==, 1985);
	g_assert_cmpint (i_cal_time_get_month (from), ==, 3);
	g_assert_cmpint (i_cal_time_get_day (from), ==, 1);
	g_object_unref (from);
	g_object_unref (comp);

	comp = i_cal_component_get_first_component (vtimezone, I_CAL_XSTANDARD_COMPONENT);
	g_assert_nonnull (comp);
	from = i_cal_component_get_dtstart (comp);
	g_assert_nonnull (from);
	g_assert_cmpint (i_cal_time_get_year (from), ==, 1985);
	g_assert_cmpint (i_cal_time_get_month (from), ==, 10);
	g_assert_cmpint (i_cal_time_get_day (from), ==, 1);
	g_object_unref (from);
	g_object_unref (comp);

	g_object_unref (vtimezone);
	vtimezone = i_cal_component_new_from_string (vtimezone_str);

	comp = i_cal_component_new_from_string (
		"BEGIN:VTODO\r\n"
		"UID:1\r\n"
		"DUE:19821004T080000Z\r\n"
		"END:VTODO\r\n");
	g_assert_nonnull (comp);

	e_cal_util_clamp_vtimezone_by_component (vtimezone, comp);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XDAYLIGHT_COMPONENT), ==, 1);
	g_assert_cmpint (i_cal_component_count_components (vtimezone, I_CAL_XSTANDARD_COMPONENT), ==, 1);

	g_object_unref (comp);
	g_object_unref (vtimezone);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/issues/");

	g_test_add ("/ECalUtils/ClampVTIMEZONE", ETestServerFixture, &test_closure,
		e_test_server_utils_setup, test_clamp_vtimezone, e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}

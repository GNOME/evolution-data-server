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
static ETestServerClosure test_closure_calendar = { E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, FALSE };

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

static void
test_categories (ETestServerFixture *fixture,
		 gconstpointer user_data)
{
	ICalComponent *old_comp, *new_comp;
	GHashTable *added, *removed;

	new_comp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"CATEGORIES:cat1,cat2, cat3 \r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (new_comp);
	e_cal_util_diff_categories (NULL, new_comp, &added, &removed);
	g_assert_nonnull (added);
	g_assert_null (removed);
	g_assert_cmpint (g_hash_table_size (added), ==, 3);
	g_assert_true (g_hash_table_contains (added, "cat1"));
	g_assert_true (g_hash_table_contains (added, "cat2"));
	g_assert_true (g_hash_table_contains (added, "cat3"));
	g_clear_pointer (&added, g_hash_table_unref);

	e_cal_util_diff_categories (new_comp, NULL, &added, &removed);
	g_assert_null (added);
	g_assert_nonnull (removed);
	g_assert_cmpint (g_hash_table_size (removed), ==, 3);
	g_assert_true (g_hash_table_contains (removed, "cat1"));
	g_assert_true (g_hash_table_contains (removed, "cat2"));
	g_assert_true (g_hash_table_contains (removed, "cat3"));
	g_clear_pointer (&removed, g_hash_table_unref);

	old_comp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (old_comp);
	e_cal_util_diff_categories (old_comp, NULL, &added, &removed);
	g_assert_null (added);
	g_assert_null (removed);

	e_cal_util_diff_categories (NULL, old_comp, &added, &removed);
	g_assert_null (added);
	g_assert_null (removed);

	e_cal_util_diff_categories (old_comp, new_comp, &added, &removed);
	g_assert_nonnull (added);
	g_assert_null (removed);
	g_assert_cmpint (g_hash_table_size (added), ==, 3);
	g_assert_true (g_hash_table_contains (added, "cat1"));
	g_assert_true (g_hash_table_contains (added, "cat2"));
	g_assert_true (g_hash_table_contains (added, "cat3"));
	g_clear_pointer (&added, g_hash_table_unref);

	e_cal_util_diff_categories (new_comp, old_comp, &added, &removed);
	g_assert_null (added);
	g_assert_nonnull (removed);
	g_assert_cmpint (g_hash_table_size (removed), ==, 3);
	g_assert_true (g_hash_table_contains (removed, "cat1"));
	g_assert_true (g_hash_table_contains (removed, "cat2"));
	g_assert_true (g_hash_table_contains (removed, "cat3"));
	g_clear_pointer (&removed, g_hash_table_unref);
	g_clear_object (&old_comp);

	old_comp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"CATEGORIES:cat1\r\n"
		"CATEGORIES: cat2 \r\n"
		"CATEGORIES:cat3\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (old_comp);
	e_cal_util_diff_categories (old_comp, new_comp, &added, &removed);
	g_assert_null (added);
	g_assert_null (removed);
	g_clear_object (&old_comp);

	old_comp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"CATEGORIES:cat1\r\n"
		"CATEGORIES: cat2 \r\n"
		"CATEGORIES:cat3\r\n"
		"CATEGORIES:cat4\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (old_comp);
	e_cal_util_diff_categories (old_comp, new_comp, &added, &removed);
	g_assert_null (added);
	g_assert_nonnull (removed);
	g_assert_cmpint (g_hash_table_size (removed), ==, 1);
	g_assert_true (g_hash_table_contains (removed, "cat4"));
	g_clear_pointer (&removed, g_hash_table_unref);
	g_clear_object (&old_comp);

	old_comp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"CATEGORIES:cat0\r\n"
		"CATEGORIES:cat3\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (old_comp);
	e_cal_util_diff_categories (old_comp, new_comp, &added, &removed);
	g_assert_nonnull (added);
	g_assert_nonnull (removed);
	g_assert_cmpint (g_hash_table_size (added), ==, 2);
	g_assert_true (g_hash_table_contains (added, "cat1"));
	g_assert_true (g_hash_table_contains (added, "cat2"));
	g_assert_cmpint (g_hash_table_size (removed), ==, 1);
	g_assert_true (g_hash_table_contains (removed, "cat0"));
	g_clear_pointer (&added, g_hash_table_unref);
	g_clear_pointer (&removed, g_hash_table_unref);
	g_clear_object (&old_comp);

	old_comp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"CATEGORIES:bat1\r\n"
		"CATEGORIES:bat2\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (old_comp);
	e_cal_util_diff_categories (old_comp, new_comp, &added, &removed);
	g_assert_nonnull (added);
	g_assert_nonnull (removed);
	g_assert_cmpint (g_hash_table_size (added), ==, 3);
	g_assert_true (g_hash_table_contains (added, "cat1"));
	g_assert_true (g_hash_table_contains (added, "cat2"));
	g_assert_true (g_hash_table_contains (added, "cat3"));
	g_assert_cmpint (g_hash_table_size (removed), ==, 2);
	g_assert_true (g_hash_table_contains (removed, "bat1"));
	g_assert_true (g_hash_table_contains (removed, "bat2"));
	g_clear_pointer (&added, g_hash_table_unref);
	g_clear_pointer (&removed, g_hash_table_unref);
	g_clear_object (&old_comp);

	g_clear_object (&new_comp);
}

static void
test_guess_tz (ETestServerFixture *fixture,
	       gconstpointer user_data)
{
	ICalTimezone *zone;

	zone = e_cal_util_guess_timezone ("some-odd-text");
	g_assert_null (zone);

	zone = e_cal_util_guess_timezone ("Europe/Vienna");
	g_assert_nonnull (zone);
	g_assert_cmpstr (i_cal_timezone_get_location (zone), ==, "Europe/Vienna");

	zone = i_cal_timezone_get_builtin_timezone ("Europe/Vatican");
	g_assert_nonnull (zone);
	zone = e_cal_util_guess_timezone (i_cal_timezone_get_tzid (zone));
	g_assert_nonnull (zone);
	g_assert_cmpstr (i_cal_timezone_get_location (zone), ==, "Europe/Vatican");

	zone = e_cal_util_guess_timezone ("id-looking/Europe/Vaduz");
	g_assert_nonnull (zone);
	g_assert_cmpstr (i_cal_timezone_get_location (zone), ==, "Europe/Vaduz");

	zone = e_cal_util_guess_timezone ("id-looking/America/Kentucky/Louisville");
	g_assert_nonnull (zone);
	g_assert_cmpstr (i_cal_timezone_get_location (zone), ==, "America/Kentucky/Louisville");
}

static void
test_time_convert_tm_back (struct tm *tm)
{
	ICalTime *itt;

	itt = e_cal_util_tm_to_icaltime (tm, FALSE);
	g_assert_nonnull (itt);
	g_assert_cmpint (tm->tm_year, ==, i_cal_time_get_year (itt) - 1900);
	g_assert_cmpint (tm->tm_mon, ==, i_cal_time_get_month (itt) - 1);
	g_assert_cmpint (tm->tm_mday, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (tm->tm_hour, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (tm->tm_min, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (tm->tm_sec, ==, i_cal_time_get_second (itt));
	g_object_unref (itt);

	itt = e_cal_util_tm_to_icaltime (tm, TRUE);
	g_assert_nonnull (itt);
	g_assert_cmpint (tm->tm_year, ==, i_cal_time_get_year (itt) - 1900);
	g_assert_cmpint (tm->tm_mon, ==, i_cal_time_get_month (itt) - 1);
	g_assert_cmpint (tm->tm_mday, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (0, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (0, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (0, ==, i_cal_time_get_second (itt));
	g_object_unref (itt);
}

static void
test_time_convert_tm (ETestServerFixture *fixture,
		      gconstpointer user_data)
{
	ICalTime *itt;
	ICalTimezone *from_zone, *to_zone;
	struct tm tm;

	itt = i_cal_time_new_from_string ("20250326T104025");
	g_assert_nonnull (itt);
	g_assert_null (i_cal_time_get_timezone (itt));
	g_assert_cmpint (i_cal_time_get_year (itt), ==, 2025);
	g_assert_cmpint (i_cal_time_get_month (itt), ==, 3);
	g_assert_cmpint (i_cal_time_get_day (itt), ==, 26);
	g_assert_cmpint (i_cal_time_get_hour (itt), ==, 10);
	g_assert_cmpint (i_cal_time_get_minute (itt), ==, 40);
	g_assert_cmpint (i_cal_time_get_second (itt), ==, 25);

	tm = e_cal_util_icaltime_to_tm (itt);
	g_assert_cmpint (tm.tm_year, ==, i_cal_time_get_year (itt) - 1900);
	g_assert_cmpint (tm.tm_mon, ==, i_cal_time_get_month (itt) - 1);
	g_assert_cmpint (tm.tm_mday, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (tm.tm_hour, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (tm.tm_min, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (tm.tm_sec, ==, i_cal_time_get_second (itt));

	test_time_convert_tm_back (&tm);

	from_zone = i_cal_timezone_get_builtin_timezone ("Europe/Berlin");
	g_assert_nonnull (from_zone);
	i_cal_time_set_timezone (itt, from_zone);
	g_assert_true (from_zone == i_cal_time_get_timezone (itt));

	/* check it does not matter what timezone the itt has set */
	tm = e_cal_util_icaltime_to_tm (itt);
	g_assert_cmpint (tm.tm_year, ==, i_cal_time_get_year (itt) - 1900);
	g_assert_cmpint (tm.tm_mon, ==, i_cal_time_get_month (itt) - 1);
	g_assert_cmpint (tm.tm_mday, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (tm.tm_hour, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (tm.tm_min, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (tm.tm_sec, ==, i_cal_time_get_second (itt));

	test_time_convert_tm_back (&tm);

	to_zone = from_zone;
	tm = e_cal_util_icaltime_to_tm_with_zone (itt, from_zone, to_zone);
	g_assert_cmpint (tm.tm_year, ==, i_cal_time_get_year (itt) - 1900);
	g_assert_cmpint (tm.tm_mon, ==, i_cal_time_get_month (itt) - 1);
	g_assert_cmpint (tm.tm_mday, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (tm.tm_hour, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (tm.tm_min, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (tm.tm_sec, ==, i_cal_time_get_second (itt));

	test_time_convert_tm_back (&tm);

	to_zone = i_cal_timezone_get_builtin_timezone ("America/Winnipeg");
	g_assert_nonnull (to_zone);

	tm = e_cal_util_icaltime_to_tm_with_zone (itt, from_zone, to_zone);
	g_assert_cmpint (tm.tm_year, ==, i_cal_time_get_year (itt) - 1900);
	g_assert_cmpint (tm.tm_mon, ==, i_cal_time_get_month (itt) - 1);
	g_assert_cmpint (tm.tm_mday, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (tm.tm_hour, ==, i_cal_time_get_hour (itt) - 6);
	g_assert_cmpint (tm.tm_min, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (tm.tm_sec, ==, i_cal_time_get_second (itt));

	test_time_convert_tm_back (&tm);

	i_cal_time_set_timezone (itt, NULL);

	tm = e_cal_util_icaltime_to_tm_with_zone (itt, from_zone, to_zone);
	g_assert_cmpint (tm.tm_year, ==, i_cal_time_get_year (itt) - 1900);
	g_assert_cmpint (tm.tm_mon, ==, i_cal_time_get_month (itt) - 1);
	g_assert_cmpint (tm.tm_mday, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (tm.tm_hour, ==, i_cal_time_get_hour (itt) - 6);
	g_assert_cmpint (tm.tm_min, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (tm.tm_sec, ==, i_cal_time_get_second (itt));

	test_time_convert_tm_back (&tm);

	i_cal_time_set_timezone (itt, to_zone);

	to_zone = i_cal_timezone_get_builtin_timezone ("America/New_York");
	g_assert_nonnull (to_zone);

	tm = e_cal_util_icaltime_to_tm_with_zone (itt, from_zone, to_zone);
	g_assert_cmpint (tm.tm_year, ==, i_cal_time_get_year (itt) - 1900);
	g_assert_cmpint (tm.tm_mon, ==, i_cal_time_get_month (itt) - 1);
	g_assert_cmpint (tm.tm_mday, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (tm.tm_hour, ==, i_cal_time_get_hour (itt) - 5);
	g_assert_cmpint (tm.tm_min, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (tm.tm_sec, ==, i_cal_time_get_second (itt));

	test_time_convert_tm_back (&tm);

	i_cal_time_set_timezone (itt, NULL);

	tm = e_cal_util_icaltime_to_tm_with_zone (itt, from_zone, to_zone);
	g_assert_cmpint (tm.tm_year, ==, i_cal_time_get_year (itt) - 1900);
	g_assert_cmpint (tm.tm_mon, ==, i_cal_time_get_month (itt) - 1);
	g_assert_cmpint (tm.tm_mday, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (tm.tm_hour, ==, i_cal_time_get_hour (itt) - 5);
	g_assert_cmpint (tm.tm_min, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (tm.tm_sec, ==, i_cal_time_get_second (itt));

	test_time_convert_tm_back (&tm);

	g_clear_object (&itt);
}

static void test_zone_cache_iface_init (ETimezoneCacheInterface *iface);
#define TEST_TYPE_ZONE_CACHE test_zone_cache_get_type ()
G_DECLARE_FINAL_TYPE (TestZoneCache, test_zone_cache, TEST, ZONE_CACHE, GObject)

struct _TestZoneCache {
	GObject object;
};

G_DEFINE_TYPE_WITH_CODE (TestZoneCache, test_zone_cache, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (E_TYPE_TIMEZONE_CACHE, test_zone_cache_iface_init))

static void
test_zone_cache_add_timezone (ETimezoneCache *cache,
			      ICalTimezone *zone)
{
	g_assert_not_reached ();
}

static ICalTimezone *
test_zone_cache_get_timezone (ETimezoneCache *cache,
			      const gchar *tzid)
{
	if (!tzid || !*tzid)
		return NULL;

	if (g_strcmp0 ("special/vienna/id", tzid) == 0)
		return i_cal_timezone_get_builtin_timezone ("Europe/Vienna");
	if (g_strcmp0 ("special/winnipeg/id", tzid) == 0)
		return i_cal_timezone_get_builtin_timezone ("America/Winnipeg");

	return NULL;
}

static GList *
test_zone_cache_list_timezones (ETimezoneCache *cache)
{
	g_assert_not_reached ();
	return NULL;
}

static void
test_zone_cache_iface_init (ETimezoneCacheInterface *iface)
{
	iface->tzcache_add_timezone = test_zone_cache_add_timezone;
	iface->tzcache_get_timezone = test_zone_cache_get_timezone;
	iface->tzcache_list_timezones = test_zone_cache_list_timezones;
}

static void
test_zone_cache_class_init (TestZoneCacheClass *klass)
{
}

static void
test_zone_cache_init (TestZoneCache *self)
{
}

static ICalComponent *
test_time_convert_create_vcalendar (void)
{
	ICalTimezone *zone;
	ICalComponent *comp, *clone, *vcalendar;
	ICalProperty *prop;

	vcalendar = i_cal_component_new_vcalendar ();

	#define add_zone_as(tzid, new_tzid) \
		zone = i_cal_timezone_get_builtin_timezone (tzid); \
		g_assert_nonnull (zone); \
		comp = i_cal_timezone_get_component (zone); \
		g_assert_nonnull (comp); \
		clone = i_cal_component_clone (comp); \
		g_assert_nonnull (clone); \
		g_clear_object (&comp); \
		e_cal_util_component_remove_x_property (clone, "X-LIC-LOCATION"); \
		prop = i_cal_component_get_first_property (clone, I_CAL_TZID_PROPERTY); \
		g_assert_nonnull (prop); \
		i_cal_property_set_tzid (prop, new_tzid); \
		g_clear_object (&prop); \
		i_cal_component_take_component (vcalendar, clone);

	add_zone_as ("America/Winnipeg", "vcal winnipeg.id");
	add_zone_as ("America/New_York", "vcal newyork.id");
	add_zone_as ("Europe/Berlin", "vcal berlin.id");

	#undef add_zone_as

	return vcalendar;
}

static void
test_time_convert_props (ETestServerFixture *fixture,
			 gconstpointer user_data)
{
	ICalComponent *icomp, *vcalendar;
	ICalTime *itt = NULL;
	ICalTimezone *to_zone;
	ICalTimezone *berlin_tz = i_cal_timezone_get_builtin_timezone ("Europe/Berlin");
	ICalTimezone *winnipeg_tz = i_cal_timezone_get_builtin_timezone ("America/Winnipeg");
	ICalTimezone *newyork_tz = i_cal_timezone_get_builtin_timezone ("America/New_York");
	ETimezoneCache *zone_cache;
	time_t tt;

	icomp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"SUMMARY:Summary\r\n"
		"DTSTART;TZID=Europe/Vienna:20250326T104025\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (icomp);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_UID_PROPERTY, NULL, NULL, NULL, NULL);
	g_assert_cmpint (tt, ==, -1);
	g_assert_null (itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_UID_PROPERTY, NULL, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, -1);
	g_assert_null (itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, NULL, NULL, NULL, NULL);
	g_assert_cmpint (tt, ==, -1);
	g_assert_null (itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, NULL, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, -1);
	g_assert_null (itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTSTART_PROPERTY, NULL, NULL, NULL, NULL);
	g_assert_cmpint (tt, ==, 1742982025);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTSTART_PROPERTY, NULL, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) + 1);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_null (i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	/* NULL and UTC to_zone should produce the same time */
	to_zone = i_cal_timezone_get_utc_timezone ();
	g_assert_nonnull (to_zone);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTSTART_PROPERTY, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) + 1);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_nonnull (i_cal_time_get_timezone (itt));
	g_clear_object (&itt);


	to_zone = berlin_tz;
	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTSTART_PROPERTY, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTSTART_PROPERTY, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) + 6);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	g_assert_nonnull (to_zone);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTSTART_PROPERTY, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) + 5);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&icomp);

	/* date value */
	icomp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"SUMMARY:Summary\r\n"
		"DTEND;VALUE=DATE:20250327\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (icomp);
	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743048000);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (0, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (0, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (0, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (1, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_null (i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = berlin_tz;
	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743030000);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (0, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (0, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (0, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (1, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_null (i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&icomp);

	/* floating time */
	icomp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"SUMMARY:Summary\r\n"
		"DTEND:20250327T173210\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (icomp);

	to_zone = i_cal_timezone_get_builtin_timezone ("Europe/Vatican");
	g_assert_nonnull (to_zone);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743093130);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;
	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743114730);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743111130);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&icomp);

	/* UTC */
	icomp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"SUMMARY:Summary\r\n"
		"DTEND:20250327T173210Z\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (icomp);

	to_zone = berlin_tz;

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743096730);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) - 1);
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;
	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743096730);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) + 5);
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743096730);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) + 4);
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&icomp);

	zone_cache = g_object_new (TEST_TYPE_ZONE_CACHE, NULL);
	g_assert_nonnull (zone_cache);

	icomp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"SUMMARY:Summary\r\n"
		"DTSTART;TZID=special/vienna/id:20250326T104025\r\n"
		"DTEND;TZID=special/winnipeg/id:20250327T173015\r\n"
		"DUE;TZID=special/unknown/id:20250328T132333\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (icomp);

	to_zone = berlin_tz;
	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTSTART_PROPERTY, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743114615);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) - 6);
	g_assert_cmpint (30, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (15, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DUE_PROPERTY, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743164613);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (28, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (13, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (23, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (33, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;
	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTSTART_PROPERTY, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) + 6);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743114615);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (30, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (15, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DUE_PROPERTY, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743186213);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (28, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (13, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (23, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (33, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTSTART_PROPERTY, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) + 5);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743114615);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) - 1);
	g_assert_cmpint (30, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (15, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DUE_PROPERTY, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743182613);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (28, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (13, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (23, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (33, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&icomp);

	vcalendar = test_time_convert_create_vcalendar ();

	icomp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"SUMMARY:Summary\r\n"
		"DTEND;TZID=special/unknown/id:20250327T203015\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (icomp);

	to_zone = berlin_tz;

	/* cannot find the zone anywhere */
	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, vcalendar, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743103815);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (20, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (30, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (15, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&icomp);

	icomp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"SUMMARY:Summary\r\n"
		"DTSTART;TZID=vcal newyork.id:20250326T104025\r\n"
		"DTEND;TZID=special/winnipeg/id:20250327T173015\r\n"
		"DUE;TZID=Europe/Vienna:20250328T132333\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (icomp);

	to_zone = berlin_tz;
	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTSTART_PROPERTY, to_zone, vcalendar, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743000025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) - 5);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, vcalendar, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743114615);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) - 6);
	g_assert_cmpint (30, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (15, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DUE_PROPERTY, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743164613);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (28, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (13, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (23, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (33, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;
	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTSTART_PROPERTY, to_zone, vcalendar, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743000025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) + 1);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, vcalendar, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743114615);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (30, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (15, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DUE_PROPERTY, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743164613);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (28, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (13, ==, i_cal_time_get_hour (itt) + 6);
	g_assert_cmpint (23, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (33, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTSTART_PROPERTY, to_zone, vcalendar, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743000025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, to_zone, vcalendar, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743114615);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) - 1);
	g_assert_cmpint (30, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (15, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	tt = e_cal_util_comp_time_to_zone (icomp, I_CAL_DUE_PROPERTY, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743164613);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (28, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (13, ==, i_cal_time_get_hour (itt) + 5);
	g_assert_cmpint (23, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (33, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&icomp);

	g_clear_object (&zone_cache);
	g_clear_object (&vcalendar);
}

static void
test_time_convert_icaltime (ETestServerFixture *fixture,
			    gconstpointer user_data)
{
	ICalComponent *vcalendar;
	ICalTime *itt = NULL, *src_itt;
	ICalTimezone *to_zone;
	ICalTimezone *berlin_tz = i_cal_timezone_get_builtin_timezone ("Europe/Berlin");
	ICalTimezone *winnipeg_tz = i_cal_timezone_get_builtin_timezone ("America/Winnipeg");
	ICalTimezone *newyork_tz = i_cal_timezone_get_builtin_timezone ("America/New_York");
	ETimezoneCache *zone_cache;
	const gchar *src_tzid = NULL;
	time_t tt;

	src_itt = i_cal_time_new_null_time ();

	tt = e_cal_util_time_to_zone (src_itt, src_tzid, NULL, NULL, NULL, NULL);
	g_assert_cmpint (tt, ==, -1);

	g_assert_null (itt);
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, NULL, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, -1);
	g_assert_null (itt);

	g_clear_object (&src_itt);

	src_itt = i_cal_time_new_from_string ("20250326T104025");
	g_assert_nonnull (src_itt);
	src_tzid = "Europe/Vienna";

	tt = e_cal_util_time_to_zone (src_itt, src_tzid, NULL, NULL, NULL, NULL);
	g_assert_cmpint (tt, ==, 1742982025);

	tt = e_cal_util_time_to_zone (src_itt, src_tzid, NULL, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) + 1);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_null (i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	/* NULL and UTC to_zone should produce the same time */
	to_zone = i_cal_timezone_get_utc_timezone ();
	g_assert_nonnull (to_zone);

	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) + 1);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_nonnull (i_cal_time_get_timezone (itt));
	g_clear_object (&itt);


	to_zone = berlin_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) + 6);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) + 5);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&src_itt);

	/* date value */
	src_itt = i_cal_time_new_from_string ("20250327");
	g_assert_nonnull (src_itt);
	g_assert_true (i_cal_time_is_date (src_itt));
	src_tzid = NULL;

	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743048000);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (0, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (0, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (0, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (1, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_null (i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&src_itt);

	/* floating time */
	src_itt = i_cal_time_new_from_string ("20250327T173210");
	g_assert_nonnull (src_itt);
	src_tzid = NULL;

	to_zone = i_cal_timezone_get_builtin_timezone ("Europe/Vatican");
	g_assert_nonnull (to_zone);

	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743093130);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743114730);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743111130);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&src_itt);

	/* UTC */
	src_itt = i_cal_time_new_from_string ("20250327T173210Z");
	g_assert_nonnull (src_itt);
	g_assert_true (i_cal_time_is_utc (src_itt));
	src_tzid = NULL;

	to_zone = berlin_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743096730);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) - 1);
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743096730);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) + 5);
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743096730);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) + 4);
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	/* case insensitive compare */
	src_tzid = "uTc";

	to_zone = berlin_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743096730);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) - 1);
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743096730);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) + 5);
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, NULL, &itt);
	g_assert_cmpint (tt, ==, 1743096730);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) + 4);
	g_assert_cmpint (32, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_second (itt));
	g_assert_cmpint (0, ==, (i_cal_time_is_date (itt) ? 1 : 0));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&src_itt);

	zone_cache = g_object_new (TEST_TYPE_ZONE_CACHE, NULL);
	g_assert_nonnull (zone_cache);

	src_itt = i_cal_time_new_from_string ("20250326T104025");
	g_assert_nonnull (src_itt);
	src_tzid = "special/vienna/id";

	to_zone = berlin_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) + 6);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1742982025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) + 5);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&src_itt);

	src_itt = i_cal_time_new_from_string ("20250327T173015");
	g_assert_nonnull (src_itt);
	src_tzid = "special/winnipeg/id";

	to_zone = berlin_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743114615);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) - 6);
	g_assert_cmpint (30, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (15, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743114615);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (30, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (15, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743114615);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) - 1);
	g_assert_cmpint (30, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (15, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&src_itt);

	src_itt = i_cal_time_new_from_string ("20250328T132333");
	g_assert_nonnull (src_itt);
	src_tzid = "special/unknown/id";

	to_zone = berlin_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743164613);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (28, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (13, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (23, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (33, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743186213);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (28, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (13, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (23, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (33, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743182613);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (28, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (13, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (23, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (33, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&src_itt);

	vcalendar = test_time_convert_create_vcalendar ();

	src_itt = i_cal_time_new_from_string ("20250327T203015");
	g_assert_nonnull (src_itt);
	src_tzid = "special/unknown/id";

	to_zone = berlin_tz;

	/* cannot find the zone anywhere */
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, vcalendar, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743103815);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (20, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (30, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (15, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&src_itt);

	src_itt = i_cal_time_new_from_string ("20250326T104025");
	g_assert_nonnull (src_itt);
	src_tzid = "vcal newyork.id";

	to_zone = berlin_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, vcalendar, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743000025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) - 5);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, vcalendar, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743000025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt) + 1);
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, vcalendar, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743000025);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (26, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (10, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (40, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (25, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&src_itt);

	src_itt = i_cal_time_new_from_string ("20250327T173015");
	g_assert_nonnull (src_itt);
	src_tzid = "special/winnipeg/id";

	to_zone = berlin_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, vcalendar, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743114615);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) - 6);
	g_assert_cmpint (30, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (15, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, vcalendar, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743114615);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (30, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (15, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, vcalendar, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743114615);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (27, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (17, ==, i_cal_time_get_hour (itt) - 1);
	g_assert_cmpint (30, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (15, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&src_itt);

	src_itt = i_cal_time_new_from_string ("20250328T132333");
	g_assert_nonnull (src_itt);
	src_tzid = "Europe/Vienna";

	to_zone = berlin_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743164613);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (28, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (13, ==, i_cal_time_get_hour (itt));
	g_assert_cmpint (23, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (33, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = winnipeg_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743164613);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (28, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (13, ==, i_cal_time_get_hour (itt) + 6);
	g_assert_cmpint (23, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (33, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	to_zone = newyork_tz;
	tt = e_cal_util_time_to_zone (src_itt, src_tzid, to_zone, NULL, zone_cache, &itt);
	g_assert_cmpint (tt, ==, 1743164613);
	g_assert_nonnull (itt);
	g_assert_cmpint (2025, ==, i_cal_time_get_year (itt));
	g_assert_cmpint (3, ==, i_cal_time_get_month (itt));
	g_assert_cmpint (28, ==, i_cal_time_get_day (itt));
	g_assert_cmpint (13, ==, i_cal_time_get_hour (itt) + 5);
	g_assert_cmpint (23, ==, i_cal_time_get_minute (itt));
	g_assert_cmpint (33, ==, i_cal_time_get_second (itt));
	g_assert_true (to_zone == i_cal_time_get_timezone (itt));
	g_clear_object (&itt);

	g_clear_object (&src_itt);

	g_clear_object (&zone_cache);
	g_clear_object (&vcalendar);
}

static void
test_remove_as_all (ETestServerFixture *fixture,
		    gconstpointer user_data)
{
	struct _zones {
		const gchar *location;
		const gchar *tzid_param;
		const gchar *time_suffix;
	} zones[] = {
		{ "UTC",		"",			  "Z" },
		{ "Europe/Berlin",	";TZID=Europe/Berlin",	  "" },
		{ "America/New_York",	";TZID=America/New_York", "" }
	};
	struct _instances_def {
		const gchar *insts[3];
	} instances_def[] = {
		{ { NULL } }, /* no detached instance, not a sentinel */
		{ { "20250805T100000", NULL } }, /* first */
		{ { "20250807T100000", NULL } }, /* mid */
		{ { "20250809T100000", NULL } }, /* last */
		{ { "20250805T100000", "20250806T100000", NULL } }, /* first - multiple */
		{ { "20250806T100000", "20250808T100000", NULL } }, /* mid - multiple */
		{ { "20250808T100000", "20250809T100000", NULL } }, /* last - multiple */
	};
	ECalClient *client;
	guint ii, jj;

	client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	for (jj = 0; jj < G_N_ELEMENTS (instances_def); jj++) {
		const gchar **instances = instances_def[jj].insts;

		for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
			ECalComponent *comp;
			ICalTime *rid;
			ICalTimezone *zone;
			GSList *detached_instances = NULL;
			GString *exdates = NULL;
			gchar *str;
			guint kk;

			zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
			g_assert_nonnull (zone);

			for (kk = 0; instances[kk]; kk++) {
				const gchar *instance_time_str = instances[kk];
				ECalComponent *instance;

				if (!exdates)
					exdates = g_string_new ("");

				g_string_append_printf (exdates, "EXDATE%s:%s%s\r\n", zones[ii].tzid_param, instance_time_str, zones[ii].time_suffix);

				str = g_strdup_printf (
					"BEGIN:VEVENT\r\n"
					"UID:1\r\n"
					"DTSTART%s:20250811T111100%s\r\n"
					"DTEND%s:20250811T112200%s\r\n"
					"RECURRENCE-ID%s:%s%s\r\n"
					"SUMMARY:test - detached\r\n"
					"END:VEVENT\r\n",
					zones[ii].tzid_param, zones[ii].time_suffix,
					zones[ii].tzid_param, zones[ii].time_suffix,
					zones[ii].tzid_param, instance_time_str, zones[ii].time_suffix);

				instance = e_cal_component_new_from_string (str);
				g_assert_nonnull (instance);

				g_free (str);

				detached_instances = g_slist_prepend (detached_instances, instance);
			}

			/* using COUNT */
			str = g_strdup_printf (
				"BEGIN:VEVENT\r\n"
				"UID:1\r\n"
				"DTSTART%s:20250805T100000%s\r\n"
				"DTEND%s:20250805T101500%s\r\n"
				"RRULE:FREQ=DAILY;COUNT=5\r\n"
				"%s"
				"SUMMARY:test\r\n"
				"END:VEVENT\r\n",
				zones[ii].tzid_param, zones[ii].time_suffix,
				zones[ii].tzid_param, zones[ii].time_suffix,
				exdates ? exdates->str : "");

			comp = e_cal_component_new_from_string (str);
			g_assert_nonnull (comp);

			rid = i_cal_time_new_from_string ("20250805T100000");
			i_cal_time_set_timezone (rid, zone);
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ONLY_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_true (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR, e_cal_client_tzlookup_cb, client));
			g_assert_true (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ALL, e_cal_client_tzlookup_cb, client));
			g_clear_object (&rid);

			rid = i_cal_time_new_from_string ("20250806T100000");
			i_cal_time_set_timezone (rid, zone);
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ONLY_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR, e_cal_client_tzlookup_cb, client));
			g_assert_true (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ALL, e_cal_client_tzlookup_cb, client));
			g_clear_object (&rid);

			rid = i_cal_time_new_from_string ("20250807T100000");
			i_cal_time_set_timezone (rid, zone);
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ONLY_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR, e_cal_client_tzlookup_cb, client));
			g_assert_true (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ALL, e_cal_client_tzlookup_cb, client));
			g_clear_object (&rid);

			rid = i_cal_time_new_from_string ("20250808T100000");
			i_cal_time_set_timezone (rid, zone);
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ONLY_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR, e_cal_client_tzlookup_cb, client));
			g_assert_true (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ALL, e_cal_client_tzlookup_cb, client));
			g_clear_object (&rid);

			rid = i_cal_time_new_from_string ("20250809T100000");
			i_cal_time_set_timezone (rid, zone);
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ONLY_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE, e_cal_client_tzlookup_cb, client));
			g_assert_true (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR, e_cal_client_tzlookup_cb, client));
			g_assert_true (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ALL, e_cal_client_tzlookup_cb, client));
			g_clear_object (&rid);

			g_clear_object (&comp);
			g_free (str);

			/* using UNTIL */
			str = g_strdup_printf (
				"BEGIN:VEVENT\r\n"
				"UID:1\r\n"
				"DTSTART%s:20250805T100000%s\r\n"
				"DTEND%s:20250805T101500%s\r\n"
				"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
				"%s"
				"SUMMARY:test\r\n"
				"END:VEVENT\r\n",
				zones[ii].tzid_param, zones[ii].time_suffix,
				zones[ii].tzid_param, zones[ii].time_suffix,
				exdates ? exdates->str : "");

			comp = e_cal_component_new_from_string (str);
			g_assert_nonnull (comp);

			rid = i_cal_time_new_from_string ("20250805T100000");
			i_cal_time_set_timezone (rid, zone);
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ONLY_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_true (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR, e_cal_client_tzlookup_cb, client));
			g_assert_true (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ALL, e_cal_client_tzlookup_cb, client));
			g_clear_object (&rid);

			rid = i_cal_time_new_from_string ("20250806T100000");
			i_cal_time_set_timezone (rid, zone);
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ONLY_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR, e_cal_client_tzlookup_cb, client));
			g_assert_true (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ALL, e_cal_client_tzlookup_cb, client));
			g_clear_object (&rid);

			rid = i_cal_time_new_from_string ("20250807T100000");
			i_cal_time_set_timezone (rid, zone);
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ONLY_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR, e_cal_client_tzlookup_cb, client));
			g_assert_true (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ALL, e_cal_client_tzlookup_cb, client));
			g_clear_object (&rid);

			rid = i_cal_time_new_from_string ("20250808T100000");
			i_cal_time_set_timezone (rid, zone);
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ONLY_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR, e_cal_client_tzlookup_cb, client));
			g_assert_true (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ALL, e_cal_client_tzlookup_cb, client));
			g_clear_object (&rid);

			rid = i_cal_time_new_from_string ("20250809T100000");
			i_cal_time_set_timezone (rid, zone);
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ONLY_THIS, e_cal_client_tzlookup_cb, client));
			g_assert_false (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE, e_cal_client_tzlookup_cb, client));
			g_assert_true (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR, e_cal_client_tzlookup_cb, client));
			g_assert_true (e_cal_util_check_may_remove_all (comp, detached_instances, rid, E_CAL_OBJ_MOD_ALL, e_cal_client_tzlookup_cb, client));
			g_clear_object (&rid);

			g_clear_object (&comp);
			g_free (str);

			g_slist_free_full (detached_instances, g_object_unref);
			if (exdates)
				g_string_free (exdates, TRUE);
		}
	}
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/issues/");

	g_test_add ("/ECalUtils/ClampVTIMEZONE", ETestServerFixture, &test_closure,
		e_test_server_utils_setup, test_clamp_vtimezone, e_test_server_utils_teardown);
	g_test_add ("/ECalUtils/Categories", ETestServerFixture, &test_closure,
		e_test_server_utils_setup, test_categories, e_test_server_utils_teardown);
	g_test_add ("/ECalUtils/GuessTZ", ETestServerFixture, &test_closure,
		e_test_server_utils_setup, test_guess_tz, e_test_server_utils_teardown);
	g_test_add ("/ECalUtils/TimeConvertTm", ETestServerFixture, &test_closure,
		e_test_server_utils_setup, test_time_convert_tm, e_test_server_utils_teardown);
	g_test_add ("/ECalUtils/TimeConvertProps", ETestServerFixture, &test_closure,
		e_test_server_utils_setup, test_time_convert_props, e_test_server_utils_teardown);
	g_test_add ("/ECalUtils/TimeConvertICalTime", ETestServerFixture, &test_closure,
		e_test_server_utils_setup, test_time_convert_icaltime, e_test_server_utils_teardown);
	g_test_add ("/ECalUtils/RemoveAsAll", ETestServerFixture, &test_closure_calendar,
		e_test_server_utils_setup, test_remove_as_all, e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}

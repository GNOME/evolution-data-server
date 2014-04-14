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
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };

#define TZID_NEW "XYZ"
#define TZNAME_NEW "Ex Wye Zee"
#define EVENT_SUMMARY "Creation of new test event in the " TZID_NEW " timezone"

static void
test_set_default_timezone (ETestServerFixture *fixture,
                           gconstpointer user_data)
{
	ECal *cal;
	icalproperty *property;
	icalcomponent *component;
	icaltimezone *zone;
	icaltimezone *zone_final;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECal);

	/* Build up new timezone */
	component = icalcomponent_new_vtimezone ();
	property = icalproperty_new_tzid (TZID_NEW);
	icalcomponent_add_property (component, property);
	property = icalproperty_new_tzname (TZNAME_NEW);
	icalcomponent_add_property (component, property);
	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, component);

	/* set default; must be done before opening the calendar */
	ecal_test_utils_cal_add_timezone (cal, zone);
	ecal_test_utils_cal_set_default_timezone (cal, zone);

	/* verify */
	/* FIXME: enhance the validation; confirm that the timezone was actually
	 * set as the default */
	zone_final = ecal_test_utils_cal_get_timezone (cal, TZID_NEW);
	g_assert_cmpstr (icaltimezone_get_tzid (zone), ==, icaltimezone_get_tzid (zone_final));
	g_assert_cmpstr (icaltimezone_get_tznames (zone), ==, icaltimezone_get_tznames (zone_final));

	icaltimezone_free (zone, TRUE);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECal/SetDefaultTimezone",
		ETestServerFixture,
		&cal_closure,
		e_test_server_utils_setup,
		test_set_default_timezone,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

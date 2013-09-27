/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };

#define TZID_NEW "XYZ"
#define TZNAME_NEW "Ex Wye Zee"

static void
test_add_timezone (ETestServerFixture *fixture,
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

	/* add */
	ecal_test_utils_cal_add_timezone (cal, zone);

	/* verify */
	zone_final = ecal_test_utils_cal_get_timezone (cal, TZID_NEW);
	g_assert_cmpstr (icaltimezone_get_tzid (zone), ==, icaltimezone_get_tzid (zone_final));
	g_assert_cmpstr (icaltimezone_get_tznames (zone), ==,icaltimezone_get_tznames (zone_final));

	icaltimezone_free (zone, TRUE);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECal/AddTimezone",
		ETestServerFixture,
		&cal_closure,
		e_test_server_utils_setup,
		test_add_timezone,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

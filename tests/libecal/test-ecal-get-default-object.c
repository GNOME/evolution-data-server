/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };

static void
test_get_default_object (ETestServerFixture *fixture,
                         gconstpointer user_data)
{
	ECal *cal;
	icalcomponent *component;
	gchar *component_string;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECal);

	component = ecal_test_utils_cal_get_default_object (cal);
	component_string = icalcomponent_as_ical_string (component);
	test_print ("default object:\n%s", component_string);

	g_free (component_string);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECal/GetDefaultObject",
		ETestServerFixture,
		&cal_closure,
		e_test_server_utils_setup,
		test_get_default_object,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

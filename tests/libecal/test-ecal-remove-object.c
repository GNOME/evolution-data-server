/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };

static void
test_remove_object (ETestServerFixture *fixture,
                    gconstpointer user_data)
{
	ECal *cal;
	icalcomponent *component;
	icalcomponent *component_final;
	gchar *uid;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECal);

	component = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	uid = ecal_test_utils_cal_create_object (cal, component);

	/* FIXME: In the same way test-ecal-create-object.c,
	 * this part of the test is broken, need to fix this.
	 */
	component_final = ecal_test_utils_cal_get_object (cal, uid);
	/* ecal_test_utils_cal_assert_objects_equal_shallow (component, component_final); */

	ecal_test_utils_cal_remove_object (cal, uid);

	g_free (uid);
	icalcomponent_free (component);
	icalcomponent_free (component_final);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECal/RemoveObject",
		ETestServerFixture,
		&cal_closure,
		e_test_server_utils_setup,
		test_remove_object,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };

static void
test_receive_objects (ETestServerFixture *fixture,
                      gconstpointer user_data)
{
	ECal *cal;
	ECalComponent *e_component = NULL;
	icalcomponent *component = NULL;
	gchar *uid = NULL;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECal);

	ecal_test_utils_create_component (
		cal,
		"20040109T090000Z", "UTC",
		"20040109T103000", "UTC",
		"meeting request", &e_component, &uid);

	component = e_cal_component_get_icalcomponent (e_component);
	ecal_test_utils_cal_receive_objects (cal, component);

	g_object_unref (e_component);
	g_free (uid);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECal/ReceiveObjects",
		ETestServerFixture,
		&cal_closure,
		e_test_server_utils_setup,
		test_receive_objects,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

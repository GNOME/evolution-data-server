/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"
#include "e-test-server-utils.h"

#define EVENT_SUMMARY "Creation of new test event"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };

static void
test_get_object_list (ETestServerFixture *fixture,
                      gconstpointer user_data)
{
	ECal *cal;
	ECalComponent *e_component;
	icalcomponent *component;
	icalcomponent *component_final;
	gchar *uid;
	GList *components;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECal);

	ecal_test_utils_create_component (
		cal,
		"20040109T090000Z", "UTC",
		"20040109T103000", "UTC",
		EVENT_SUMMARY, &e_component, &uid);
	component = e_cal_component_get_icalcomponent (e_component);

	component_final = ecal_test_utils_cal_get_object (cal, uid);
	ecal_test_utils_cal_assert_objects_equal_shallow (
		component,
			component_final);
	icalcomponent_free (component_final);

	components = ecal_test_utils_cal_get_object_list (
		cal, "(contains? \"summary\" \"" EVENT_SUMMARY "\")");
	g_assert (g_list_length (components) == 1);
	component_final = components->data;
	ecal_test_utils_cal_assert_objects_equal_shallow (
		component, component_final);

	e_cal_free_object_list (components);
	g_free (uid);
	icalcomponent_free (component);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECal/GetObjectList",
		ETestServerFixture,
		&cal_closure,
		e_test_server_utils_setup,
		test_get_object_list,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

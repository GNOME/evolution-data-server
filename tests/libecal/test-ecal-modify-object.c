/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"
#include "e-test-server-utils.h"

#define EVENT_SUMMARY "Creation of new test event"
#define INITIAL_BEGIN_TIME     "20040109T090000Z"
#define INITIAL_BEGIN_TIMEZONE "UTC"
#define INITIAL_END_TIME       "20040109T103000"
#define INITIAL_END_TIMEZONE   "UTC"
#define FINAL_BEGIN_TIME       "20091221T090000Z"
#define FINAL_BEGIN_TIMEZONE   "UTC"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };

static void
test_modify_object (ETestServerFixture *fixture,
                    gconstpointer user_data)
{
	ECal *cal;
	ECalComponent *e_component;
	ECalComponent *e_component_final;
	icalcomponent *component;
	icalcomponent *component_final;
	struct icaltimetype icaltime;
	gchar *uid;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECal);

	ecal_test_utils_create_component (
		cal,
		INITIAL_BEGIN_TIME, INITIAL_BEGIN_TIMEZONE,
		INITIAL_END_TIME, INITIAL_END_TIMEZONE,
		EVENT_SUMMARY, &e_component, &uid);
	component = e_cal_component_get_icalcomponent (e_component);

	component_final = ecal_test_utils_cal_get_object (cal, uid);
	ecal_test_utils_cal_assert_objects_equal_shallow (
		component, component_final);
	icalcomponent_free (component_final);

	/* make and commit changes */
	icaltime = icaltime_from_string (FINAL_BEGIN_TIME);
	icalcomponent_set_dtstart (component, icaltime);
	ecal_test_utils_cal_component_set_icalcomponent (
		e_component, component);
	ecal_test_utils_cal_modify_object (cal, component, CALOBJ_MOD_ALL);

	/* verify */
	component_final = ecal_test_utils_cal_get_object (cal, uid);
	e_component_final = e_cal_component_new ();
	ecal_test_utils_cal_component_set_icalcomponent (
		e_component_final, component_final);

	ecal_test_utils_cal_assert_e_cal_components_equal (
		e_component, e_component_final);

	g_object_unref (e_component_final);
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
		"/ECal/ModifyObject",
		ETestServerFixture,
		&cal_closure,
		e_test_server_utils_setup,
		test_modify_object,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

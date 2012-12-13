/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };

static void
test_get_objects_for_uid (ETestServerFixture *fixture,
			  gconstpointer       user_data)
{
	ECal *cal;
	icalcomponent *component;
	icalcomponent *component_final;
	ECalComponent *e_component_final;
	gchar *uid;
	GList *components;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECal);

	component = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	uid = ecal_test_utils_cal_create_object (cal, component);

	/* FIXME: In the same way test-ecal-create-object.c,
	 * this part of the test is broken, need to fix this.
	 */
	component_final = ecal_test_utils_cal_get_object (cal, uid);
	ecal_test_utils_cal_assert_objects_equal_shallow (component, component_final);
	icalcomponent_free (component_final);

	/* The list of component and all subcomponents should just contain the
	 * component itself (wrapped in an ECalComponent) */
	components = ecal_test_utils_cal_get_objects_for_uid (cal, uid);
	g_assert (g_list_length (components) == 1);
	e_component_final = components->data;
	component_final = e_cal_component_get_icalcomponent (e_component_final);
	ecal_test_utils_cal_assert_objects_equal_shallow (component, component_final);

	g_list_foreach (components, (GFunc) g_object_unref, NULL);
	g_list_free (components);
	g_free (uid);
	icalcomponent_free (component);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	g_test_add ("/ECal/GetObjectsForUid", ETestServerFixture, &cal_closure,
		    e_test_server_utils_setup, test_get_objects_for_uid, e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

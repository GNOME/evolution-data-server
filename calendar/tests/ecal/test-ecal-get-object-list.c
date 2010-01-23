/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"

#define EVENT_SUMMARY "Creation of new test event"

gint
main (gint argc, gchar **argv)
{
	ECal *cal;
	gchar *uri = NULL;
	ECalComponent *e_component;
	icalcomponent *component;
	icalcomponent *component_final;
	gchar *uid;
	GList *components;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);

	ecal_test_utils_create_component (cal, "20040109T090000Z", "UTC",
			"20040109T103000", "UTC", EVENT_SUMMARY, &e_component,
			&uid);
        component = e_cal_component_get_icalcomponent (e_component);

	component_final = ecal_test_utils_cal_get_object (cal, uid);
	ecal_test_utils_cal_assert_objects_equal_shallow (component,
			component_final);
	icalcomponent_free (component_final);

	components = ecal_test_utils_cal_get_object_list (cal,
			"(contains? \"summary\" \"" EVENT_SUMMARY "\")");
	g_assert (g_list_length (components) == 1);
	component_final = components->data;
	ecal_test_utils_cal_assert_objects_equal_shallow (component,
			component_final);

	ecal_test_utils_cal_remove (cal);

	e_cal_free_object_list (components);
	g_free (uid);
	icalcomponent_free (component);

	return 0;
}

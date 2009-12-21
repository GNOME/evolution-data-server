/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"

gint
main (gint argc, gchar **argv)
{
	ECal *cal;
	char *uri = NULL;
	icalcomponent *component;
	icalcomponent *component_final;
	ECalComponent *e_component_final;
	char *uid;
	GList *components;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);

	component = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	uid = ecal_test_utils_cal_create_object (cal, component);

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

	ecal_test_utils_cal_remove (cal);

	g_list_foreach (components, (GFunc) g_object_unref, NULL);
	g_list_free (components);
	g_free (uid);
	icalcomponent_free (component);

	return 0;
}

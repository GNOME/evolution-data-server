/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"

gint
main (gint argc, gchar **argv)
{
	ECal *cal;
	gchar *uri = NULL;
	icalcomponent *component;
	icalcomponent *component_final;
	gchar *uid;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);

	component = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	uid = ecal_test_utils_cal_create_object (cal, component);

	component_final = ecal_test_utils_cal_get_object (cal, uid);
	ecal_test_utils_cal_assert_objects_equal_shallow (component, component_final);
	ecal_test_utils_cal_remove_object (cal, uid);
	ecal_test_utils_cal_remove (cal);

	g_free (uid);
	icalcomponent_free (component);
	icalcomponent_free (component_final);

	return 0;
}

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
        ECalComponent *e_component, *e_component_final;
        icalcomponent *icalcomponent_final;
	gchar *uid;

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);
        ecal_test_utils_create_component (cal, "20040109T090000Z", "UTC",
                        "20040109T103000", "UTC", EVENT_SUMMARY, &e_component,
                        &uid);

	icalcomponent_final = ecal_test_utils_cal_get_object (cal, uid);
        e_component_final = e_cal_component_new ();
	ecal_test_utils_cal_component_set_icalcomponent (e_component_final,
				icalcomponent_final);
	ecal_test_utils_cal_assert_e_cal_components_equal (e_component,
			e_component_final);

        g_object_unref (e_component_final);
	g_object_unref (e_component);
	g_free (uid);

	return 0;
}

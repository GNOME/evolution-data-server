/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"

#define EVENT_SUMMARY "Creation of new test event"
#define INITIAL_BEGIN_TIME     "20040109T090000Z"
#define INITIAL_BEGIN_TIMEZONE "UTC"
#define INITIAL_END_TIME       "20040109T103000"
#define INITIAL_END_TIMEZONE   "UTC"
#define FINAL_BEGIN_TIME       "20091221T090000Z"
#define FINAL_BEGIN_TIMEZONE   "UTC"

gint
main (gint argc, gchar **argv)
{
	ECal *cal;
	char *uri = NULL;
	ECalComponent *e_component;
	ECalComponent *e_component_final;
	icalcomponent *component;
	icalcomponent *component_final;
	struct icaltimetype icaltime;
	char *uid;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);

	ecal_test_utils_create_component (cal, INITIAL_BEGIN_TIME,
			INITIAL_BEGIN_TIMEZONE, INITIAL_END_TIME,
			INITIAL_END_TIMEZONE, EVENT_SUMMARY, &e_component,
			&uid);
        component = e_cal_component_get_icalcomponent (e_component);

	component_final = ecal_test_utils_cal_get_object (cal, uid);
	ecal_test_utils_cal_assert_objects_equal_shallow (component,
			component_final);
	icalcomponent_free (component_final);

	/* make and commit changes */
	icaltime = icaltime_from_string (FINAL_BEGIN_TIME);
	icalcomponent_set_dtstart (component, icaltime);
	ecal_test_utils_cal_component_set_icalcomponent (e_component,
			component);
	ecal_test_utils_cal_modify_object (cal, component, CALOBJ_MOD_ALL);

	/* verify */
        component_final = ecal_test_utils_cal_get_object (cal, uid);
        e_component_final = e_cal_component_new ();
        ecal_test_utils_cal_component_set_icalcomponent (e_component_final,
                                component_final);

        ecal_test_utils_cal_assert_e_cal_components_equal (e_component,
                        e_component_final);

	/* Clean-up */
	ecal_test_utils_cal_remove (cal);

	g_object_unref (e_component_final);
	g_free (uid);
	icalcomponent_free (component);

	return 0;
}

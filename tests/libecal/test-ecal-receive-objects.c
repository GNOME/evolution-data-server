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
	ECalComponent *e_component = NULL;
	icalcomponent *component = NULL;
	gchar *uid = NULL;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);

        ecal_test_utils_create_component (cal, "20040109T090000Z", "UTC",
			"20040109T103000", "UTC", "meeting request",
			&e_component, &uid);

	component = e_cal_component_get_icalcomponent (e_component);
	ecal_test_utils_cal_receive_objects (cal, component);

	ecal_test_utils_cal_remove (cal);

	g_object_unref (e_component);
	g_free (uid);

	return 0;
}

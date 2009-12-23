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
        GList *users = NULL;
	ECalComponent *e_component = NULL;
	icalcomponent *component = NULL;
	icalcomponent *modified_component = NULL;
	char *uid = NULL;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);

        ecal_test_utils_create_component (cal, "20040109T090000Z", "UTC",
                        "20040109T103000", "UTC", "new event", &e_component,
                        &uid);

	component = e_cal_component_get_icalcomponent (e_component);
	ecal_test_utils_cal_send_objects (cal, component, &users, &modified_component);

	ecal_test_utils_cal_remove (cal);

	g_list_foreach (users, (GFunc) g_free, NULL);
	g_list_free (users);

	g_object_unref (e_component);
	g_free (uid);

	return 0;
}

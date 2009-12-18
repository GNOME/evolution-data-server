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
	char *component_string;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);

	component = ecal_test_utils_cal_get_default_object (cal);
	component_string = icalcomponent_as_ical_string (component);
	g_print ("default object:\n%s", component_string);

	ecal_test_utils_cal_remove (cal);

	g_free (component_string);

	return 0;
}

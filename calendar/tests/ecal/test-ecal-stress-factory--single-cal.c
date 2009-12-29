/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>

#include "ecal-test-utils.h"

#define NUM_OPENS 200

gint
main (gint argc, gchar **argv)
{
	char *uri = NULL;
	ECal *cal;
	gint i;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	g_object_unref (cal);

	/* open and close the same calendar repeatedly */
	for (i = 0; i < NUM_OPENS-1; i++) {
		cal = ecal_test_utils_cal_new_from_uri (uri,
				E_CAL_SOURCE_TYPE_EVENT);
		ecal_test_utils_cal_open (cal, FALSE);
		g_object_unref (cal);
	}

	cal = ecal_test_utils_cal_new_from_uri (uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_remove (cal);

	g_free (uri);

	return 0;
}

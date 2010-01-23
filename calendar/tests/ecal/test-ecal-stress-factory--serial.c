/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>

#include "ecal-test-utils.h"

#define NUM_CALS 200

gint
main (gint argc, gchar **argv)
{
	gchar *uri = NULL;
	gint i;

	g_type_init ();

	/* Serially create, open, (close), and remove many calendars */
	for (i = 0; i < NUM_CALS; i++) {
		ECal *cal;

		cal = ecal_test_utils_cal_new_temp (&uri,
				E_CAL_SOURCE_TYPE_EVENT);
		ecal_test_utils_cal_open (cal, FALSE);
		ecal_test_utils_cal_remove (cal);

		g_free (uri);
	}

	return 0;
}

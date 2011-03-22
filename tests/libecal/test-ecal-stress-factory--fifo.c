/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>

#include "ecal-test-utils.h"

#define NUM_CALS 200

gint
main (gint argc, gchar **argv)
{
	gchar *uri = NULL;
	ECal *cals[NUM_CALS];
	gint i;

	g_type_init ();

	/* Create and open many calendars; then remove each of them */

	for (i = 0; i < NUM_CALS; i++) {
		cals[i] = ecal_test_utils_cal_new_temp (&uri,
				E_CAL_SOURCE_TYPE_EVENT);
		ecal_test_utils_cal_open (cals[i], FALSE);

		g_free (uri);
	}

	for (i = 0; i < NUM_CALS; i++) {
		ecal_test_utils_cal_remove (cals[i]);
	}

	return 0;
}

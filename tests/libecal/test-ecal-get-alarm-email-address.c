/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>

#include "ecal-test-utils.h"

gint
main (gint argc, gchar **argv)
{
	ECal *cal;
	gchar *uri = NULL;
	gchar *address;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);

	address = ecal_test_utils_cal_get_alarm_email_address (cal);
	test_print ("alarm email address: '%s'\n", address);

	ecal_test_utils_cal_remove (cal);

	g_free (address);

	return 0;
}

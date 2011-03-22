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
	icaltimezone *zone;
	icaltimezone *utc_zone;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);

	zone = ecal_test_utils_cal_get_timezone (cal, "UTC");
	utc_zone = icaltimezone_get_utc_timezone ();

	g_assert (!g_strcmp0 (icaltimezone_get_tzid (zone),
			icaltimezone_get_tzid (utc_zone)));

	ecal_test_utils_cal_remove (cal);

	return 0;
}

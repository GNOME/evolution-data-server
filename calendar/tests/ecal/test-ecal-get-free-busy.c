/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>
#include <libecal/e-cal-time-util.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"

gint
main (gint argc, gchar **argv)
{
	ECal *cal;
	gchar *uri = NULL;
        GList *users = NULL;
        icaltimezone *utc;
        time_t start = time (NULL), end;
	GList *free_busy;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);

        utc = icaltimezone_get_utc_timezone ();
        start = time_from_isodate ("20040212T000000Z");
        end = time_add_day_with_zone (start, 2, utc);
	/* XXX: create dummy list, which the file backend will ignore */
	users = g_list_prepend (users, NULL);

	free_busy = ecal_test_utils_cal_get_free_busy (cal, users, start, end);

	ecal_test_utils_cal_remove (cal);

	g_list_foreach (free_busy, (GFunc) g_object_unref, NULL);
	g_list_free (free_busy);

	return 0;
}

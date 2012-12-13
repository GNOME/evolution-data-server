/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };


static void
test_get_free_busy (ETestServerFixture *fixture,
		    gconstpointer       user_data)
{
	ECal *cal;
	GList *users = NULL;
	icaltimezone *utc;
	time_t start, end;
	GList *free_busy;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECal);

	utc = icaltimezone_get_utc_timezone ();
	start = time_from_isodate ("20040212T000000Z");
	end = time_add_day_with_zone (start, 2, utc);
	users = g_list_prepend (users, (gpointer) "user@example.com");

	free_busy = ecal_test_utils_cal_get_free_busy (cal, users, start, end);

	g_list_foreach (free_busy, (GFunc) g_object_unref, NULL);
	g_list_free (free_busy);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	g_test_add ("/ECal/GetFreeBusy", ETestServerFixture, &cal_closure,
		    e_test_server_utils_setup, test_get_free_busy, e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

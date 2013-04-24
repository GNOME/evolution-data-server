/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <string.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "e-test-server-utils.h"

static ETestServerClosure cal_closure_sync =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, FALSE };
static ETestServerClosure cal_closure_async =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, TRUE };

static void
setup_cal (ECalClient *cal_client)
{
	GError *error = NULL;
	icalcomponent *icalcomp;
	struct icaltimetype now;
	gchar *uid = NULL;

	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	icalcomponent_set_summary (icalcomp, "Test event summary");
	icalcomponent_set_dtstart (icalcomp, now);
	icalcomponent_set_dtend   (icalcomp, icaltime_from_timet (icaltime_as_timet (now) + 60 * 60 * 60, 0));

	if (!e_cal_client_create_object_sync (cal_client, icalcomp, &uid, NULL, &error))
		g_error ("create object sync: %s", error->message);

	icalcomponent_free (icalcomp);
	g_free (uid);
}

static void
test_refresh_sync (ETestServerFixture *fixture,
                   gconstpointer user_data)
{
	ECalClient *cal;
	GError *error = NULL;
	gboolean supported;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	supported = e_client_check_refresh_supported (E_CLIENT (cal));

	g_print ("Refresh supported: %s\n", supported ? "yes" : "no");
	if (!supported)
		return;

	setup_cal (cal);
	if (!e_client_refresh_sync (E_CLIENT (cal), NULL, &error))
		g_error ("refresh sync: %s", error->message);
}

static void
async_refresh_result_ready (GObject *source_object,
                            GAsyncResult *result,
                            gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_client_refresh_finish (E_CLIENT (cal_client), result, &error))
		g_error ("refresh finish: %s", error->message);

	g_main_loop_quit (loop);
}

static void
test_refresh_async (ETestServerFixture *fixture,
                    gconstpointer user_data)
{
	ECalClient *cal;
	gboolean supported;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	supported = e_client_check_refresh_supported (E_CLIENT (cal));
	if (!supported)
		return;

	setup_cal (cal);
	e_client_refresh (E_CLIENT (cal), NULL, async_refresh_result_ready, fixture->loop);
	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECalClient/Refresh/Sync",
		ETestServerFixture,
		&cal_closure_sync,
		e_test_server_utils_setup,
		test_refresh_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/Refresh/Async",
		ETestServerFixture,
		&cal_closure_async,
		e_test_server_utils_setup,
		test_refresh_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}


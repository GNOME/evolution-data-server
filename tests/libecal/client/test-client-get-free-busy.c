/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS };

#define USER_EMAIL "user@example.com"

static void
free_busy_data_cb (ECalClient *client,
                   const GSList *free_busy,
                   const gchar *func_name)
{
	g_print ("   Received %d Free/Busy components from %s\n", g_slist_length ((GSList *) free_busy), func_name);
}

static void
test_get_free_busy_sync (ETestServerFixture *fixture,
                         gconstpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icaltimezone *utc;
	GSList *users = NULL;
	time_t start, end;
	gulong sig_id;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	utc = icaltimezone_get_utc_timezone ();
	start = time_from_isodate ("20040212T000000Z");
	end = time_add_day_with_zone (start, 2, utc);
	users = g_slist_append (users, (gpointer) USER_EMAIL);

	sig_id = g_signal_connect (cal_client, "free-busy-data", G_CALLBACK (free_busy_data_cb), (gpointer) G_STRFUNC);

	if (!e_cal_client_get_free_busy_sync (cal_client, start, end, users, NULL, &error))
		g_error ("get free busy sync: %s", error->message);

	g_signal_handler_disconnect (cal_client, sig_id);

	g_slist_free (users);
}

static void
async_get_free_busy_result_ready (GObject *source_object,
                                  GAsyncResult *result,
                                  gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_free_busy_finish (cal_client, result, &error))
		g_error ("create object finish: %s", error->message);

	if (!e_client_remove_sync (E_CLIENT (cal_client), NULL, &error))
		g_error ("client remove sync: %s", error->message);

	g_main_loop_quit (loop);
}

static void
test_get_free_busy_async (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	ECalClient *cal_client;
	icaltimezone *utc;
	GSList *users = NULL;
	time_t start, end;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	utc = icaltimezone_get_utc_timezone ();
	start = time_from_isodate ("20040212T000000Z");
	end = time_add_day_with_zone (start, 2, utc);
	users = g_slist_append (users, (gpointer) USER_EMAIL);

	/* here is all Free/Busy information received */
	g_signal_connect (cal_client, "free-busy-data", G_CALLBACK (free_busy_data_cb), (gpointer) G_STRFUNC);

	e_cal_client_get_free_busy (cal_client, start, end, users, NULL, async_get_free_busy_result_ready, fixture->loop);
	g_slist_free (users);

	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	g_test_add (
		"/ECalClient/GetFreeBusy/Sync", ETestServerFixture, &cal_closure,
		e_test_server_utils_setup, test_get_free_busy_sync, e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/GetFreeBusy/Async", ETestServerFixture, &cal_closure,
		e_test_server_utils_setup, test_get_free_busy_async, e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

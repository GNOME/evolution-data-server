/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal-client.h>
#include <libecal/e-cal-time-util.h>
#include <libical/ical.h>

#include "client-test-utils.h"

#define USER_EMAIL "user@example.com"

static void
free_busy_data_cb (ECalClient *client, const GSList *free_busy, const gchar *func_name)
{
	g_print ("   Received %d Free/Busy components from %s\n", g_slist_length ((GSList *) free_busy), func_name);
}

static gboolean
test_sync (void)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icaltimezone *utc;
	GSList *users = NULL;
	time_t start, end;
	gulong sig_id;

	cal_client = new_temp_client (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, NULL);
	g_return_val_if_fail (cal_client != NULL, FALSE);

	if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	utc = icaltimezone_get_utc_timezone ();
	start = time_from_isodate ("20040212T000000Z");
	end = time_add_day_with_zone (start, 2, utc);
	users = g_slist_append (users, (gpointer) USER_EMAIL);

	sig_id = g_signal_connect (cal_client, "free-busy-data", G_CALLBACK (free_busy_data_cb), (gpointer) G_STRFUNC);

	if (!e_cal_client_get_free_busy_sync (cal_client, start, end, users, NULL, &error)) {
		report_error ("get free busy sync", &error);
		g_signal_handler_disconnect (cal_client, sig_id);
		g_object_unref (cal_client);
		g_slist_free (users);
		return FALSE;
	}

	g_signal_handler_disconnect (cal_client, sig_id);

	g_slist_free (users);

	if (!e_client_remove_sync (E_CLIENT (cal_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	g_object_unref (cal_client);

	return TRUE;
}

/* asynchronous get_free_busy callback with a main-loop running */
static void
async_get_free_busy_result_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_free_busy_finish (cal_client, result, &error)) {
		report_error ("create object finish", &error);
		g_object_unref (cal_client);
		stop_main_loop (1);
		return;
	}

	if (!e_client_remove_sync (E_CLIENT (cal_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (cal_client);
		stop_main_loop (1);
		return;
	}

	g_object_unref (cal_client);

	stop_main_loop (0);
}

/* synchronously in idle with main-loop running */
static gboolean
test_async_in_idle (gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icaltimezone *utc;
	GSList *users = NULL;
	time_t start, end;

	if (!test_sync ()) {
		stop_main_loop (1);
		return FALSE;
	}

	cal_client = new_temp_client (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, NULL);
	g_return_val_if_fail (cal_client != NULL, FALSE);

	if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (cal_client);
		stop_main_loop (1);
		return FALSE;
	}

	utc = icaltimezone_get_utc_timezone ();
	start = time_from_isodate ("20040212T000000Z");
	end = time_add_day_with_zone (start, 2, utc);
	users = g_slist_append (users, (gpointer) USER_EMAIL);

	/* here is all Free/Busy information received */
	g_signal_connect (cal_client, "free-busy-data", G_CALLBACK (free_busy_data_cb), (gpointer) G_STRFUNC);

	e_cal_client_get_free_busy (cal_client, start, end, users, NULL, async_get_free_busy_result_ready, NULL);

	g_slist_free (users);

	return FALSE;
}

/* synchronously in a dedicated thread with main-loop running */
static gpointer
test_sync_in_thread (gpointer user_data)
{
	if (!test_sync ()) {
		stop_main_loop (1);
		return NULL;
	}

	g_idle_add (test_async_in_idle, NULL);

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	main_initialize ();

	/* synchronously without main-loop */
	if (!test_sync ())
		return 1;

	start_in_thread_with_main_loop (test_sync_in_thread, NULL);

	if (get_main_loop_stop_result () == 0)
		g_print ("Test finished successfully.\n");

	return get_main_loop_stop_result ();
}

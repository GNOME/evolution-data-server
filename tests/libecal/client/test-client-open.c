/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal-client.h>
#include <libical/ical.h>

#include "client-test-utils.h"

static gboolean
test_sync (void)
{
	ECalClient *cal_client;
	GError *error = NULL;

	cal_client = new_temp_client (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, NULL);
	g_return_val_if_fail (cal_client != NULL, FALSE);

	if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	if (!e_client_remove_sync (E_CLIENT (cal_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	g_object_unref (cal_client);

	return TRUE;
}

/* asynchronous remove callback with a main-loop running */
static void
async_remove_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_client_remove_finish (E_CLIENT (cal_client), result, &error)) {
		report_error ("remove finish", &error);
		g_object_unref (cal_client);
		stop_main_loop (1);
		return;
	}

	g_object_unref (cal_client);

	stop_main_loop (0);
}

/* asynchronous open callback with a main-loop running */
static void
async_open_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_client_open_finish (E_CLIENT (cal_client), result, &error)) {
		report_error ("open finish", &error);
		g_object_unref (cal_client);
		stop_main_loop (1);
		return;
	}

	e_client_remove (E_CLIENT (cal_client), NULL, async_remove_ready, NULL);
}

/* synchronously in idle with main-loop running */
static gboolean
test_sync_in_idle (gpointer user_data)
{
	ECalClient *cal_client;

	if (!test_sync ()) {
		stop_main_loop (1);
		return FALSE;
	}

	cal_client = new_temp_client (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, NULL);
	g_return_val_if_fail (cal_client != NULL, FALSE);

	e_client_open (E_CLIENT (cal_client), FALSE, NULL, async_open_ready, NULL);

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

	g_idle_add (test_sync_in_idle, NULL);

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	main_initialize ();

	/* synchronously without main-loop */
	if (!test_sync ()) {
		return 1;
	}

	start_in_thread_with_main_loop (test_sync_in_thread, NULL);

	if (get_main_loop_stop_result () == 0)
		g_print ("Test finished successfully.\n");

	return get_main_loop_stop_result ();
}

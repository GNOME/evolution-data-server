/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal-client.h>
#include <libical/ical.h>

#include "client-test-utils.h"

#define EVENT_SUMMARY "Creation of new test event"

static gboolean
test_result (icalcomponent *icalcomp)
{
	g_return_val_if_fail (icalcomp != NULL, FALSE);
	g_return_val_if_fail (g_strcmp0 (icalcomponent_get_summary (icalcomp), EVENT_SUMMARY) == 0, FALSE);

	return TRUE;
}

static gboolean
test_sync (ECalClient *cal_client)
{
	GError *error = NULL;
	icalcomponent *icalcomp = NULL;
	const gchar *uid = g_object_get_data (G_OBJECT (cal_client), "use-uid");

	g_return_val_if_fail (uid != NULL, FALSE);

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icalcomp, NULL, &error)) {
		report_error ("get object sync", &error);
		return FALSE;
	}

	icalcomponent_set_summary (icalcomp, EVENT_SUMMARY);

	if (!e_cal_client_modify_object_sync (cal_client, icalcomp, CALOBJ_MOD_ALL, NULL, &error)) {
		report_error ("modify object sync", &error);
		icalcomponent_free (icalcomp);
		return FALSE;
	}

	icalcomponent_free (icalcomp);

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icalcomp, NULL, &error)) {
		report_error ("get object sync after modification", &error);
		return FALSE;
	}

	if (!test_result (icalcomp)) {
		icalcomponent_free (icalcomp);
		return FALSE;
	}

	icalcomponent_free (icalcomp);

	return TRUE;
}

/* asynchronous callback with a main-loop running */
static void
async_modify_result_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icalcomponent *icalcomp = NULL;
	const gchar *uid;

	cal_client = E_CAL_CLIENT (source_object);
	uid = g_object_get_data (G_OBJECT (cal_client), "use-uid");

	if (!e_cal_client_modify_object_finish (cal_client, result, &error)) {
		report_error ("modify object finish", &error);
		stop_main_loop (1);
		return;
	}

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icalcomp, NULL, &error)) {
		report_error ("get object sync after async modification", &error);
		return;
	}

	if (!test_result (icalcomp)) {
		stop_main_loop (1);
	} else {
		stop_main_loop (0);
	}

	icalcomponent_free (icalcomp);
}

/* synchronously in idle with main-loop running */
static gboolean
test_sync_in_idle (gpointer user_data)
{
	ECalClient *cal_client = user_data;
	const gchar *uid = g_object_get_data (G_OBJECT (cal_client), "use-uid");
	icalcomponent *icalcomp = NULL;
	GError *error = NULL;

	g_return_val_if_fail (cal_client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_CLIENT (cal_client), FALSE);

	if (!test_sync (cal_client)) {
		stop_main_loop (1);
		return FALSE;
	}

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icalcomp, NULL, &error)) {
		report_error ("get object sync", &error);
		stop_main_loop (1);
		return FALSE;
	}

	icalcomponent_set_summary (icalcomp, EVENT_SUMMARY);

	e_cal_client_modify_object (cal_client, icalcomp, CALOBJ_MOD_ALL, NULL, async_modify_result_ready, NULL);

	icalcomponent_free (icalcomp);

	return FALSE;
}

/* synchronously in a dedicated thread with main-loop running */
static gpointer
test_sync_in_thread (gpointer user_data)
{
	if (!test_sync (user_data)) {
		stop_main_loop (1);
		return NULL;
	}

	g_idle_add (test_sync_in_idle, user_data);

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icalcomponent *icalcomp;
	struct icaltimetype now;
	gchar *uid = NULL;

	main_initialize ();

	cal_client = new_temp_client (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, NULL);
	g_return_val_if_fail (cal_client != NULL, FALSE);

	if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (cal_client);
		return 1;
	}

	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	icalcomponent_set_summary (icalcomp, "Initial" EVENT_SUMMARY);
	icalcomponent_set_dtstart (icalcomp, now);
	icalcomponent_set_dtend   (icalcomp, icaltime_from_timet (icaltime_as_timet (now) + 60 * 60 * 60, 0));

	if (!e_cal_client_create_object_sync (cal_client, icalcomp, &uid, NULL, &error)) {
		report_error ("create object sync", &error);
		g_object_unref (cal_client);
		icalcomponent_free (icalcomp);
		return 1;
	}

	icalcomponent_free (icalcomp);

	g_object_set_data_full (G_OBJECT (cal_client), "use-uid", uid, g_free);

	/* synchronously without main-loop */
	if (!test_sync (cal_client)) {
		g_object_unref (cal_client);
		return 1;
	}

	start_in_thread_with_main_loop (test_sync_in_thread, cal_client);

	if (!e_client_remove_sync (E_CLIENT (cal_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (cal_client);
		return 1;
	}

	g_object_unref (cal_client);

	if (get_main_loop_stop_result () == 0)
		g_print ("Test finished successfully.\n");

	return get_main_loop_stop_result ();
}

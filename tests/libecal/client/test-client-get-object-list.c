/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal-client.h>
#include <libical/ical.h>

#include "client-test-utils.h"

#define EVENT_SUMMARY "Creation of new test event"
#define EVENT_QUERY "(contains? \"summary\" \"" EVENT_SUMMARY "\")"

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
	GSList *icalcomps = NULL, *ecalcomps = NULL;
	gboolean res = TRUE;

	if (!e_cal_client_get_object_list_sync (cal_client, EVENT_QUERY, &icalcomps, NULL, &error)) {
		report_error ("get object list sync", &error);
		return FALSE;
	}

	if (g_slist_length (icalcomps) != 1) {
		g_printerr ("Failure: expected 1 item returned in icalcomps, got %d\n", g_slist_length (icalcomps));
		res = FALSE;
	} else {
		res = res && test_result (icalcomps->data);
	}

	e_cal_client_free_icalcomp_slist (icalcomps);

	if (!e_cal_client_get_object_list_as_comps_sync (cal_client, EVENT_QUERY, &ecalcomps, NULL, &error)) {
		report_error ("get object list as comps sync", &error);
		return FALSE;
	}

	if (g_slist_length (ecalcomps) != 1) {
		g_printerr ("Failure: expected 1 item returned in ecalcomps, got %d\n", g_slist_length (ecalcomps));
		res = FALSE;
	} else {
		res = res && test_result (e_cal_component_get_icalcomponent (ecalcomps->data));
	}

	e_cal_client_free_ecalcomp_slist (ecalcomps);

	return res;
}

/* asynchronous callback with a main-loop running */
static void
async_get_object_list_as_comps_result_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GSList *ecalcomps = NULL;
	gboolean res = TRUE;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_object_list_as_comps_finish (cal_client, result, &ecalcomps, &error)) {
		report_error ("get object list as comps finish", &error);
		stop_main_loop (1);
		return;
	}

	if (g_slist_length (ecalcomps) != 1) {
		g_printerr ("Failure: expected 1 item returned in ecalcomps, got %d\n", g_slist_length (ecalcomps));
		res = FALSE;
	} else {
		res = res && test_result (e_cal_component_get_icalcomponent (ecalcomps->data));
	}

	e_cal_client_free_ecalcomp_slist (ecalcomps);
	stop_main_loop (res ? 0 : 1);
}

/* asynchronous callback with a main-loop running */
static void
async_get_object_list_result_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GSList *icalcomps = NULL;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_object_list_finish (cal_client, result, &icalcomps, &error)) {
		report_error ("get object list finish", &error);
		stop_main_loop (1);
		return;
	}

	if (g_slist_length (icalcomps) != 1) {
		g_printerr ("Failure: expected 1 item returned in icalcomps, got %d\n", g_slist_length (icalcomps));
	} else {
		test_result (icalcomps->data);
	}

	e_cal_client_free_icalcomp_slist (icalcomps);

	e_cal_client_get_object_list_as_comps (cal_client, EVENT_QUERY, NULL, async_get_object_list_as_comps_result_ready, NULL);
}

/* synchronously in idle with main-loop running */
static gboolean
test_sync_in_idle (gpointer user_data)
{
	ECalClient *cal_client = user_data;

	g_return_val_if_fail (cal_client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_CLIENT (cal_client), FALSE);

	if (!test_sync (cal_client)) {
		stop_main_loop (1);
		return FALSE;
	}

	e_cal_client_get_object_list (cal_client, EVENT_QUERY, NULL, async_get_object_list_result_ready, NULL);

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
	icalcomponent_set_summary (icalcomp, EVENT_SUMMARY);
	icalcomponent_set_dtstart (icalcomp, now);
	icalcomponent_set_dtend   (icalcomp, icaltime_from_timet (icaltime_as_timet (now) + 60 * 60 * 60, 0));

	if (!e_cal_client_create_object_sync (cal_client, icalcomp, &uid, NULL, &error)) {
		report_error ("create object sync", &error);
		g_object_unref (cal_client);
		icalcomponent_free (icalcomp);
		return 1;
	}

	icalcomponent_free (icalcomp);
	g_free (uid);

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

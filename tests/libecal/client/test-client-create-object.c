/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal-client.h>
#include <libical/ical.h>

#include "client-test-utils.h"

static gboolean
test_icalcomps (icalcomponent *icalcomp1, icalcomponent *icalcomp2)
{
	struct icaltimetype t1, t2;

	if (!icalcomp2) {
		g_printerr ("Failure: get object returned NULL\n");
		return FALSE;
	}

	if (g_strcmp0 (icalcomponent_get_uid (icalcomp1), icalcomponent_get_uid (icalcomp2)) != 0) {
		g_printerr ("Failure: uid doesn't match, expected '%s', got '%s'\n", icalcomponent_get_uid (icalcomp1), icalcomponent_get_uid (icalcomp2));
		return FALSE;
	}

	if (g_strcmp0 (icalcomponent_get_summary (icalcomp1), icalcomponent_get_summary (icalcomp2)) != 0) {
		g_printerr ("Failure: summary doesn't match, expected '%s', got '%s'\n", icalcomponent_get_summary (icalcomp1), icalcomponent_get_summary (icalcomp2));
		return FALSE;
	}

	t1 = icalcomponent_get_dtstart (icalcomp1);
	t2 = icalcomponent_get_dtstart (icalcomp2);

	if (icaltime_compare (t1, t2) != 0) {
		g_printerr ("Failure: dtend doesn't match, expected '%s', got '%s'\n", icaltime_as_ical_string (t1), icaltime_as_ical_string (t2));
		return FALSE;
	}

	t1 = icalcomponent_get_dtend (icalcomp1);
	t2 = icalcomponent_get_dtend (icalcomp2);

	if (icaltime_compare (t1, t2) != 0) {
		g_printerr ("Failure: dtend doesn't match, expected '%s', got '%s'\n", icaltime_as_ical_string (t1), icaltime_as_ical_string (t2));
		return FALSE;
	}

	return TRUE;
}

static gboolean
test_sync (icalcomponent *icalcomp)
{
	ECalClient *cal_client;
	icalcomponent *icalcomp2 = NULL, *clone;
	GError *error = NULL;
	gchar *uid = NULL;
	gboolean res;

	g_return_val_if_fail (icalcomp != NULL, FALSE);

	cal_client = new_temp_client (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, NULL);
	g_return_val_if_fail (cal_client != NULL, FALSE);

	if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	if (!e_cal_client_create_object_sync (cal_client, icalcomp, &uid, NULL, &error)) {
		report_error ("create object sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icalcomp2, NULL, &error)) {
		report_error ("get object sync", &error);
		g_free (uid);
		g_object_unref (cal_client);
		return FALSE;
	}

	clone = icalcomponent_new_clone (icalcomp);
	icalcomponent_set_uid (clone, uid);

	res = test_icalcomps (clone, icalcomp2);

	icalcomponent_free (icalcomp2);

	if (res) {
		GSList *ecalcomps = NULL;
	
		if (!e_cal_client_get_objects_for_uid_sync (cal_client, uid, &ecalcomps, NULL, &error)) {
			report_error ("get objects for uid sync", &error);
			res = FALSE;
		}

		if (g_slist_length (ecalcomps) != 1) {
			g_printerr ("Failure: expected 1 component, bug got %d\n", g_slist_length (ecalcomps));
			res = FALSE;
		} else {
			ECalComponent *ecalcomp = ecalcomps->data;

			res = test_icalcomps (clone, e_cal_component_get_icalcomponent (ecalcomp));
		}

		e_cal_client_free_ecalcomp_slist (ecalcomps);
	}

	icalcomponent_free (clone);
	g_free (uid);

	if (!e_client_remove_sync (E_CLIENT (cal_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	g_object_unref (cal_client);

	return res;
}

/* asynchronous read2 callback with a main-loop running */
static void
async_read2_result_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icalcomponent *icalcomp1 = user_data;
	GSList *ecalcomps = NULL;
	gboolean res;

	g_return_if_fail (icalcomp1 != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_objects_for_uid_finish (cal_client, result, &ecalcomps, &error)) {
		report_error ("get objects for uid finish", &error);
		g_object_unref (cal_client);
		icalcomponent_free (icalcomp1);
		stop_main_loop (1);
		return;
	}

	if (g_slist_length (ecalcomps) != 1) {
		g_printerr ("Failure: expected 1 component, bug got %d\n", g_slist_length (ecalcomps));
		res = FALSE;
	} else {
		ECalComponent *ecalcomp = ecalcomps->data;

		res = test_icalcomps (icalcomp1, e_cal_component_get_icalcomponent (ecalcomp));
	}

	e_cal_client_free_ecalcomp_slist (ecalcomps);

	if (!e_client_remove_sync (E_CLIENT (cal_client), NULL, &error)) {
		report_error ("client remove sync", &error);
	}

	g_object_unref (cal_client);
	icalcomponent_free (icalcomp1);

	stop_main_loop (res ? 0 : 1);
}

/* asynchronous read callback with a main-loop running */
static void
async_read_result_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icalcomponent *icalcomp1 = user_data, *icalcomp2 = NULL;

	g_return_if_fail (icalcomp1 != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_object_finish (cal_client, result, &icalcomp2, &error)) {
		report_error ("get object finish", &error);
		g_object_unref (cal_client);
		icalcomponent_free (icalcomp1);
		stop_main_loop (1);
		return;
	}

	test_icalcomps (icalcomp1, icalcomp2);

	icalcomponent_free (icalcomp2);

	e_cal_client_get_objects_for_uid (cal_client, icalcomponent_get_uid (icalcomp1), NULL, async_read2_result_ready, icalcomp1);
}

/* asynchronous write callback with a main-loop running */
static void
async_write_result_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	gchar *uid = NULL;
	icalcomponent *clone, *icalcomp = user_data;

	g_return_if_fail (icalcomp != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_create_object_finish (cal_client, result, &uid, &error)) {
		report_error ("create object finish", &error);
		g_object_unref (cal_client);
		stop_main_loop (1);
		return;
	}

	clone = icalcomponent_new_clone (icalcomp);
	icalcomponent_set_uid (clone, uid);

	e_cal_client_get_object (cal_client, uid, NULL, NULL, async_read_result_ready, clone);

	g_free (uid);
}

/* synchronously in idle with main-loop running */
static gboolean
test_sync_in_idle (gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icalcomponent *icalcomp = user_data;

	g_return_val_if_fail (icalcomp != NULL, FALSE);

	if (!test_sync (icalcomp)) {
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

	e_cal_client_create_object (cal_client, icalcomp, NULL, async_write_result_ready, icalcomp);

	return FALSE;
}

/* synchronously in a dedicated thread with main-loop running */
static gpointer
test_sync_in_thread (gpointer user_data)
{
	icalcomponent *icalcomp = user_data;

	g_return_val_if_fail (icalcomp != NULL, NULL);

	if (!test_sync (icalcomp)) {
		stop_main_loop (1);
		return NULL;
	}

	g_idle_add (test_sync_in_idle, icalcomp);

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	icalcomponent *icalcomp;
	struct icaltimetype now;

	main_initialize ();

	/* Build up new component */
	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	icalcomponent_set_summary (icalcomp, "Test event summary");
	icalcomponent_set_dtstart (icalcomp, now);
	icalcomponent_set_dtend   (icalcomp, icaltime_from_timet (icaltime_as_timet (now) + 60 * 60 * 60, 0));

	/* synchronously without main-loop */
	if (!test_sync (icalcomp)) {
		icalcomponent_free (icalcomp);
		return 1;
	}

	start_in_thread_with_main_loop (test_sync_in_thread, icalcomp);

	icalcomponent_free (icalcomp);

	if (get_main_loop_stop_result () == 0)
		g_print ("Test finished successfully.\n");

	return get_main_loop_stop_result ();
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal-client.h>
#include <libical/ical.h>

#include "client-test-utils.h"

#define TZID_NEW "XYZ"
#define TZNAME_NEW "Ex Wye Zee"

static gboolean
test_zones (icaltimezone *zone1, icaltimezone *zone2)
{
	if (!zone2) {
		g_printerr ("Failure: get timezone returned NULL\n");
		return FALSE;
	}

	if (g_strcmp0 (icaltimezone_get_tzid (zone1), icaltimezone_get_tzid (zone2)) != 0) {
		g_printerr ("Failure: tzid doesn't match, expected '%s', got '%s'\n", icaltimezone_get_tzid (zone1), icaltimezone_get_tzid (zone2));
		return FALSE;
	}

	if (g_strcmp0 (icaltimezone_get_tznames (zone1), icaltimezone_get_tznames (zone2)) != 0) {
		g_printerr ("Failure: tznames doesn't match, expected '%s', got '%s'\n", icaltimezone_get_tznames (zone1), icaltimezone_get_tznames (zone2));
		return FALSE;
	}

	return TRUE;
}

static gboolean
test_sync (icaltimezone *zone)
{
	ECalClient *cal_client;
	icaltimezone *zone2 = NULL;
	GError *error = NULL;
	gboolean res;

	g_return_val_if_fail (zone != NULL, FALSE);

	cal_client = new_temp_client (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, NULL);
	g_return_val_if_fail (cal_client != NULL, FALSE);

	if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	if (!e_cal_client_add_timezone_sync (cal_client, zone, NULL, &error)) {
		report_error ("add timezone sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	if (!e_cal_client_get_timezone_sync (cal_client, TZID_NEW, &zone2, NULL, &error)) {
		report_error ("get timezone sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	res = test_zones (zone, zone2);

	if (!e_client_remove_sync (E_CLIENT (cal_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	g_object_unref (cal_client);

	return res;
}

/* asynchronous read callback with a main-loop running */
static void
async_read_result_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icaltimezone *zone1 = user_data, *zone2 = NULL;
	gboolean res;

	g_return_if_fail (zone1 != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_timezone_finish (cal_client, result, &zone2, &error)) {
		report_error ("get timezone finish", &error);
		g_object_unref (cal_client);
		stop_main_loop (1);
		return;
	}

	res = test_zones (zone1, zone2);

	if (!e_client_remove_sync (E_CLIENT (cal_client), NULL, &error)) {
		report_error ("client remove sync", &error);
	}

	g_object_unref (cal_client);

	stop_main_loop (res ? 0 : 1);
}

/* asynchronous write callback with a main-loop running */
static void
async_write_result_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;

	g_return_if_fail (user_data != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_add_timezone_finish (cal_client, result, &error)) {
		report_error ("add timezone finish", &error);
		g_object_unref (cal_client);
		stop_main_loop (1);
		return;
	}

	e_cal_client_get_timezone (cal_client, TZID_NEW, NULL, async_read_result_ready, user_data);
}

/* synchronously in idle with main-loop running */
static gboolean
test_sync_in_idle (gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icaltimezone *zone = user_data;

	g_return_val_if_fail (zone != NULL, FALSE);

	if (!test_sync (zone)) {
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

	e_cal_client_add_timezone (cal_client, zone, NULL, async_write_result_ready, zone);

	return FALSE;
}

/* synchronously in a dedicated thread with main-loop running */
static gpointer
test_sync_in_thread (gpointer user_data)
{
	icaltimezone *zone = user_data;

	g_return_val_if_fail (zone != NULL, NULL);

	if (!test_sync (zone)) {
		stop_main_loop (1);
		return NULL;
	}

	g_idle_add (test_sync_in_idle, zone);

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	icalproperty *property;
	icalcomponent *component;
	icaltimezone *zone;

	main_initialize ();

	/* Build up new timezone */
	component = icalcomponent_new_vtimezone ();
	property = icalproperty_new_tzid (TZID_NEW);
	icalcomponent_add_property (component, property);
	property = icalproperty_new_tzname (TZNAME_NEW);
	icalcomponent_add_property (component, property);
	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, component);

	/* synchronously without main-loop */
	if (!test_sync (zone)) {
		icaltimezone_free (zone, TRUE);
		return 1;
	}

	start_in_thread_with_main_loop (test_sync_in_thread, zone);

	icaltimezone_free (zone, TRUE);

	if (get_main_loop_stop_result () == 0)
		g_print ("Test finished successfully.\n");

	return get_main_loop_stop_result ();
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal-client.h>
#include <libical/ical.h>

#include "client-test-utils.h"

typedef enum {
	SUBTEST_OBJECTS_ADDED,
	SUBTEST_OBJECTS_MODIFIED,
	SUBTEST_OBJECTS_REMOVED,
	SUBTEST_VIEW_DONE,
	NUM_SUBTESTS,
	SUBTEST_RESET
} SubTestId;

static void
subtest_passed (SubTestId id)
{
	static guint subtests_complete = 0;

	if (id == SUBTEST_RESET) {
		subtests_complete = 0;
		return;
	}

	subtests_complete |= (1 << id);

	if (subtests_complete == ((1 << NUM_SUBTESTS) - 1))
		stop_main_loop (0);
}

static void
objects_added_cb (GObject *object, const GSList *objects, gpointer data)
{
	const GSList *l;

	for (l = objects; l; l = l->next)
                g_print ("Object added %s (%s)\n", icalcomponent_get_uid (l->data), icalcomponent_get_summary (l->data));

	subtest_passed (SUBTEST_OBJECTS_ADDED);
}

static void
objects_modified_cb (GObject *object, const GSList *objects, gpointer data)
{
	const GSList *l;

	for (l = objects; l; l = l->next)
                g_print ("Object modified %s (%s)\n", icalcomponent_get_uid (l->data), icalcomponent_get_summary (l->data));

	subtest_passed (SUBTEST_OBJECTS_MODIFIED);
}

static void
objects_removed_cb (GObject *object, const GSList *objects, gpointer data)
{
	const GSList *l;

	for (l = objects; l; l = l->next) {
		ECalComponentId *id = l->data;

                g_print ("Object removed: uid: %s, rid: %s\n", id->uid, id->rid);
	}

	subtest_passed (SUBTEST_OBJECTS_REMOVED);
}

static void
complete_cb (GObject *object, const GError *error, gpointer data)
{
        g_print ("View complete (status: %d, error_msg:%s)\n", error ? error->code : 0, error ? error->message : "NULL");

	subtest_passed (SUBTEST_VIEW_DONE);
}

static gpointer
alter_cal_client (gpointer user_data)
{
	ECalClient *cal_client = user_data;
	GError *error = NULL;
	icalcomponent *icalcomp;
	struct icaltimetype now;
	gchar *uid = NULL;

	g_return_val_if_fail (cal_client != NULL, NULL);

	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	icalcomponent_set_summary (icalcomp, "Initial event summary");
	icalcomponent_set_dtstart (icalcomp, now);
	icalcomponent_set_dtend   (icalcomp, icaltime_from_timet (icaltime_as_timet (now) + 60 * 60 * 60, 0));

	if (!e_cal_client_create_object_sync (cal_client, icalcomp, &uid, NULL, &error)) {
		report_error ("create object sync", &error);
		icalcomponent_free (icalcomp);
		stop_main_loop (1);
		return NULL;
	}

	icalcomponent_set_uid (icalcomp, uid);
	icalcomponent_set_summary (icalcomp, "Modified event summary");

	if (!e_cal_client_modify_object_sync (cal_client, icalcomp, CALOBJ_MOD_ALL, NULL, &error)) {
		report_error ("modify object sync", &error);
		icalcomponent_free (icalcomp);
		g_free (uid);
		stop_main_loop (1);
		return NULL;
	}

	if (!e_cal_client_remove_object_sync (cal_client, uid, NULL, CALOBJ_MOD_ALL, NULL, &error)) {
		report_error ("remove object sync", &error);
		icalcomponent_free (icalcomp);
		g_free (uid);
		stop_main_loop (1);
		return NULL;
	}

	g_free (uid);
	icalcomponent_free (icalcomp);

	return NULL;
}

static void
async_get_view_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ECalClient *cal_client = E_CAL_CLIENT (source_object);
	ECalClientView *view = NULL;
	GError *error = NULL;

	g_return_if_fail (cal_client != NULL);

	if (!e_cal_client_get_view_finish (cal_client, result, &view, &error)) {
		report_error ("get view finish", &error);
		stop_main_loop (1);
		return;
	}

	subtest_passed (SUBTEST_RESET);
	g_signal_connect (view, "objects_added", G_CALLBACK (objects_added_cb), cal_client);
	g_signal_connect (view, "objects_modified", G_CALLBACK (objects_modified_cb), cal_client);
	g_signal_connect (view, "objects_removed", G_CALLBACK (objects_removed_cb), cal_client);
	g_signal_connect (view, "complete", G_CALLBACK (complete_cb), cal_client);

	g_object_set_data_full (G_OBJECT (cal_client), "cal-view", view, g_object_unref);

	e_cal_client_view_set_fields_of_interest (view, NULL, &error);
	if (error)
		report_error ("set fields of interest", &error);
	e_cal_client_view_start (view, NULL);

	alter_cal_client (cal_client);
}

static gpointer
get_view_async (gpointer user_data)
{
	ECalClient *cal_client = user_data;

	g_return_val_if_fail (user_data != NULL, NULL);

	e_cal_client_get_view (cal_client, "(contains? \"any\" \"event\")", NULL, async_get_view_ready, NULL);

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	ECalClientView *view = NULL;
	ECalClient *cal_client;
	GError *error = NULL;

	main_initialize ();

	cal_client = new_temp_client (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, NULL);
	g_return_val_if_fail (cal_client != NULL, FALSE);

	if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (cal_client);
		return 1;
	}

	if (!e_cal_client_get_view_sync (cal_client, "(contains? \"any\" \"event\")", &view, NULL, &error)) {
		report_error ("get view sync", &error);
		g_object_unref (cal_client);
		return 1;
	}

	subtest_passed (SUBTEST_RESET);
	g_signal_connect (view, "objects_added", G_CALLBACK (objects_added_cb), cal_client);
	g_signal_connect (view, "objects_modified", G_CALLBACK (objects_modified_cb), cal_client);
	g_signal_connect (view, "objects_removed", G_CALLBACK (objects_removed_cb), cal_client);
	g_signal_connect (view, "complete", G_CALLBACK (complete_cb), cal_client);

	e_cal_client_view_set_fields_of_interest (view, NULL, &error);
	if (error)
		report_error ("set fields of interest", &error);
	e_cal_client_view_start (view, NULL);

	start_in_thread_with_main_loop (alter_cal_client, cal_client);

	g_object_unref (view);

	if (get_main_loop_stop_result () != 0) {
		g_object_unref (cal_client);
		return get_main_loop_stop_result ();
	}

	start_in_idle_with_main_loop (get_view_async, cal_client);

	g_object_set_data (G_OBJECT (cal_client), "cal-view", NULL);

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

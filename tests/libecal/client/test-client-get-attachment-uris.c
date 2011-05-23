/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <string.h>
#include <libecal/e-cal-client.h>
#include <libical/ical.h>

#include "client-test-utils.h"

#define ATTACH1 "file:///tmp/file1.x"
#define ATTACH2 "file:///tmp/file2"
#define ATTACH3 "file:///tmp/dir/fileěščřžýáíé3"

static gboolean
manage_result (GSList *attachment_uris)
{
	gboolean res;

	g_return_val_if_fail (attachment_uris != NULL, FALSE);
	g_return_val_if_fail (g_slist_length (attachment_uris) == 3, FALSE);

	res = g_slist_find_custom (attachment_uris, ATTACH1, g_str_equal)
	   && g_slist_find_custom (attachment_uris, ATTACH2, g_str_equal)
	   && g_slist_find_custom (attachment_uris, ATTACH3, g_str_equal);

	if (!res) {
		GSList *au;

		g_printerr ("Failed: didn't return same three attachment uris, got instead:\n");
		for (au = attachment_uris; au; au = au->next)
			g_printerr ("\t'%s'\n", (const gchar *) au->data);
	}

	e_client_util_free_string_slist (attachment_uris);

	return res;
}

static gboolean
test_sync (ECalClient *cal_client)
{
	GError *error = NULL;
	GSList *attachment_uris = NULL;
	const gchar *uid = g_object_get_data (G_OBJECT (cal_client), "use-uid");

	g_return_val_if_fail (uid != NULL, FALSE);

	if (!e_cal_client_get_attachment_uris_sync (cal_client, uid, NULL, &attachment_uris, NULL, &error)) {
		report_error ("get attachment uris sync", &error);
		return FALSE;
	}

	return manage_result (attachment_uris);
}

/* asynchronous callback with a main-loop running */
static void
async_attachment_uris_result_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GSList *attachment_uris = NULL;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_attachment_uris_finish (cal_client, result, &attachment_uris, &error)) {
		report_error ("get attachment uris finish", &error);
		stop_main_loop (1);
		return;
	}

	stop_main_loop (manage_result (attachment_uris) ? 0 : 1);
}

/* synchronously in idle with main-loop running */
static gboolean
test_sync_in_idle (gpointer user_data)
{
	ECalClient *cal_client = user_data;
	const gchar *uid;

	g_return_val_if_fail (cal_client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_CLIENT (cal_client), FALSE);

	if (!test_sync (cal_client)) {
		stop_main_loop (1);
		return FALSE;
	}

	uid = g_object_get_data (G_OBJECT (cal_client), "use-uid");

	e_cal_client_get_attachment_uris (cal_client, uid, NULL, NULL, async_attachment_uris_result_ready, NULL);

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

static void
add_attach (icalcomponent *icalcomp, const gchar *uri)
{
	gsize buf_size;
	gchar *buf;
	icalproperty *prop;
	icalattach *attach;

	g_return_if_fail (icalcomp != NULL);
	g_return_if_fail (uri != NULL);

	buf_size = 2 * strlen (uri);
	buf = g_malloc0 (buf_size);
	icalvalue_encode_ical_string (uri, buf, buf_size);
	attach = icalattach_new_from_url (uri);
	prop = icalproperty_new_attach (attach);
	icalcomponent_add_property (icalcomp, prop);
	icalattach_unref (attach);
	g_free (buf);
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
	icalcomponent_set_summary (icalcomp, "Test event summary");
	icalcomponent_set_dtstart (icalcomp, now);
	icalcomponent_set_dtend   (icalcomp, icaltime_from_timet (icaltime_as_timet (now) + 60 * 60 * 60, 0));
	add_attach (icalcomp, ATTACH1);
	add_attach (icalcomp, ATTACH2);
	add_attach (icalcomp, ATTACH3);

	if (!e_cal_client_create_object_sync (cal_client, icalcomp, &uid, NULL, &error)) {
		report_error ("create object sync", &error);
		icalcomponent_free (icalcomp);
		g_object_unref (cal_client);
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

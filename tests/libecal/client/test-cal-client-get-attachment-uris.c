/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <string.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "e-test-server-utils.h"

#define ATTACH1 "file:///tmp/file1.x"
#define ATTACH2 "file:///tmp/file2"
#define ATTACH3 "file:///tmp/dir/fileěščřžýáíé3"

static ETestServerClosure cal_closure_sync =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, FALSE };
static ETestServerClosure cal_closure_async =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, TRUE };

static void
add_attach (icalcomponent *icalcomp,
            const gchar *uri)
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

static const gchar *
setup_cal (ECalClient *cal_client)
{
	icalcomponent *icalcomp;
	struct icaltimetype now;
	gchar *uid = NULL;
	GError *error = NULL;

	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	icalcomponent_set_summary (icalcomp, "Test event summary");
	icalcomponent_set_dtstart (icalcomp, now);
	icalcomponent_set_dtend   (icalcomp, icaltime_from_timet (icaltime_as_timet (now) + 60 * 60 * 60, 0));
	add_attach (icalcomp, ATTACH1);
	add_attach (icalcomp, ATTACH2);
	add_attach (icalcomp, ATTACH3);

	if (!e_cal_client_create_object_sync (cal_client, icalcomp, &uid, NULL, &error))
		g_error ("create object sync: %s", error->message);

	icalcomponent_free (icalcomp);
	g_object_set_data_full (G_OBJECT (cal_client), "use-uid", uid, g_free);

	return uid;
}

static void
manage_result (GSList *attachment_uris)
{
	gboolean res;

	g_assert (attachment_uris != NULL);
	g_assert_cmpint (g_slist_length (attachment_uris), ==, 3);

	res = g_slist_find_custom (attachment_uris, ATTACH1, g_str_equal)
	   && g_slist_find_custom (attachment_uris, ATTACH2, g_str_equal)
	   && g_slist_find_custom (attachment_uris, ATTACH3, g_str_equal);

	if (!res) {
		GSList *au;

		g_printerr ("Failed: didn't return same three attachment uris, got instead:\n");
		for (au = attachment_uris; au; au = au->next)
			g_printerr ("\t'%s'\n", (const gchar *) au->data);

		g_assert_not_reached ();
	}

	e_client_util_free_string_slist (attachment_uris);
}

static void
test_get_attachment_uris_sync (ETestServerFixture *fixture,
                               gconstpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GSList *attachment_uris = NULL;
	const gchar *uid;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);
	uid = setup_cal (cal_client);

	if (!e_cal_client_get_attachment_uris_sync (cal_client, uid, NULL, &attachment_uris, NULL, &error))
		g_error ("get attachment uris sync: %s", error->message);

	manage_result (attachment_uris);
}

static void
async_attachment_uris_result_ready (GObject *source_object,
                                    GAsyncResult *result,
                                    gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GSList *attachment_uris = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_attachment_uris_finish (cal_client, result, &attachment_uris, &error))
		g_error ("get attachment uris finish: %s", error->message);

	manage_result (attachment_uris);

	g_main_loop_quit (loop);
}

static void
test_get_attachment_uris_async (ETestServerFixture *fixture,
                                gconstpointer user_data)
{
	ECalClient *cal_client;
	const gchar *uid;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);
	uid = setup_cal (cal_client);

	e_cal_client_get_attachment_uris (cal_client, uid, NULL, NULL, async_attachment_uris_result_ready, fixture->loop);
	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECalClient/GetAttachmentUris/Sync",
		ETestServerFixture,
		&cal_closure_sync,
		e_test_server_utils_setup,
		test_get_attachment_uris_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/GetAttachmentUris/Async",
		ETestServerFixture,
		&cal_closure_async,
		e_test_server_utils_setup,
		test_get_attachment_uris_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

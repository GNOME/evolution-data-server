/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "e-test-server-utils.h"

static ETestServerClosure cal_closure_sync =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, FALSE };
static ETestServerClosure cal_closure_async =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, TRUE };

static gchar *
create_object (ECalClient *cal_client)
{
	icalcomponent *icalcomp;
	struct icaltimetype now;
	gchar *uid = NULL;
	GError *error = NULL;

	g_return_val_if_fail (cal_client != NULL, NULL);

	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	icalcomponent_set_summary (icalcomp, "To-be-removed event summary");
	icalcomponent_set_dtstart (icalcomp, now);
	icalcomponent_set_dtend   (icalcomp, icaltime_from_timet (icaltime_as_timet (now) + 60 * 60 * 60, 0));

	if (!e_cal_client_create_object_sync (cal_client, icalcomp, &uid, NULL, &error))
		g_error ("create object sync: %s", error->message);

	icalcomponent_free (icalcomp);

	return uid;
}

static void
test_remove_object_sync (ETestServerFixture *fixture,
                         gconstpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	gchar *uid;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	uid = create_object (cal_client);
	g_assert (uid != NULL);

	if (!e_cal_client_remove_object_sync (cal_client, uid, NULL, E_CAL_OBJ_MOD_ALL, NULL, &error))
		g_error ("remove object sync: %s", error->message);

	g_free (uid);
}

static void
async_remove_result_ready (GObject *source_object,
                           GAsyncResult *result,
                           gpointer user_data)
{
	ECalClient *cal_client;
	GMainLoop *loop = (GMainLoop *) user_data;
	GError *error = NULL;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_remove_object_finish (cal_client, result, &error))
		g_error ("remove object finish: %s", error->message);

	g_main_loop_quit (loop);
}

static void
test_remove_object_async (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	ECalClient *cal_client;
	gchar *uid;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	uid = create_object (cal_client);
	g_assert (uid != NULL);

	e_cal_client_remove_object (cal_client, uid, NULL, E_CAL_OBJ_MOD_ALL, NULL, async_remove_result_ready, fixture->loop);
	g_free (uid);
	g_main_loop_run (fixture->loop);
}

static void
test_remove_object_empty_uid (ETestServerFixture *fixture,
                              gconstpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;

	g_test_bug ("697705");

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	e_cal_client_remove_object_sync (
		cal_client, "", NULL, E_CAL_OBJ_MOD_ALL, NULL, &error);
	g_assert_error (
		error, E_CAL_CLIENT_ERROR,
		E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECalClient/RemoveObject/Sync",
		ETestServerFixture,
		&cal_closure_sync,
		e_test_server_utils_setup,
		test_remove_object_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/RemoveObject/Async",
		ETestServerFixture,
		&cal_closure_async,
		e_test_server_utils_setup,
		test_remove_object_async,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/RemoveObject/EmptyUid",
		ETestServerFixture,
		&cal_closure_sync,
		e_test_server_utils_setup,
		test_remove_object_empty_uid,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

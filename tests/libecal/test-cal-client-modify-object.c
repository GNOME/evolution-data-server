/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdlib.h>
#include <libecal/libecal.h>

#include "e-test-server-utils.h"

static ETestServerClosure cal_closure_sync =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, FALSE };
static ETestServerClosure cal_closure_async =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, TRUE };

#define EVENT_SUMMARY "Creation of new test event"

static void
setup_cal (ECalClient *cal_client)
{
	ICalTime *dtstart, *dtend;
	ICalComponent *icomp;
	gchar *uid = NULL;
	GError *error = NULL;

	dtstart = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());
	dtend = i_cal_time_clone (dtstart);
	i_cal_time_adjust (dtend, 0, 1, 0, 0);

	icomp = i_cal_component_new (I_CAL_VEVENT_COMPONENT);
	i_cal_component_set_summary (icomp, "Initial" EVENT_SUMMARY);
	i_cal_component_set_dtstart (icomp, dtstart);
	i_cal_component_set_dtend (icomp, dtend);

	g_clear_object (&dtstart);
	g_clear_object (&dtend);

	if (!e_cal_client_create_object_sync (cal_client, icomp, E_CAL_OPERATION_FLAG_NONE, &uid, NULL, &error))
		g_error ("create object sync: %s", error->message);

	g_object_unref (icomp);
	g_object_set_data_full (G_OBJECT (cal_client), "use-uid", uid, g_free);
}

static void
test_result (ICalComponent *icomp)
{
	g_assert_nonnull (icomp);
	g_assert_cmpstr (i_cal_component_get_summary (icomp), ==, EVENT_SUMMARY);
}

static void
test_modify_object_sync (ETestServerFixture *fixture,
                         gconstpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	ICalComponent *icomp = NULL;
	const gchar *uid;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	setup_cal (cal_client);
	uid = g_object_get_data (G_OBJECT (cal_client), "use-uid");
	g_assert_true (uid != NULL);

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icomp, NULL, &error))
		g_error ("get object sync: %s", error->message);

	i_cal_component_set_summary (icomp, EVENT_SUMMARY);

	if (!e_cal_client_modify_object_sync (cal_client, icomp, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, &error))
		g_error ("modify object sync: %s", error->message);

	g_object_unref (icomp);

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icomp, NULL, &error))
		g_error ("get object sync after modification: %s", error->message);

	test_result (icomp);
	g_object_unref (icomp);
}

static void
async_modify_result_ready (GObject *source_object,
                           GAsyncResult *result,
                           gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	ICalComponent *icomp = NULL;
	const gchar *uid;
	GMainLoop *loop = (GMainLoop *) user_data;

	cal_client = E_CAL_CLIENT (source_object);
	uid = g_object_get_data (G_OBJECT (cal_client), "use-uid");

	if (!e_cal_client_modify_object_finish (cal_client, result, &error))
		g_error ("modify object finish: %s", error->message);

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icomp, NULL, &error))
		g_error ("get object sync after async modification: %s", error->message);

	test_result (icomp);
	g_object_unref (icomp);

	g_main_loop_quit (loop);
}

static void
test_modify_object_async (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	ICalComponent *icomp = NULL;
	const gchar *uid;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	setup_cal (cal_client);
	uid = g_object_get_data (G_OBJECT (cal_client), "use-uid");
	g_assert_true (uid != NULL);

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icomp, NULL, &error))
		g_error ("get object sync: %s", error->message);

	i_cal_component_set_summary (icomp, EVENT_SUMMARY);

	e_cal_client_modify_object (cal_client, icomp, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, async_modify_result_ready, fixture->loop);
	g_object_unref (icomp);

	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECalClient/ModifyObject/Sync",
		ETestServerFixture,
		&cal_closure_sync,
		e_test_server_utils_setup,
		test_modify_object_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/ModifyObject/Async",
		ETestServerFixture,
		&cal_closure_async,
		e_test_server_utils_setup,
		test_modify_object_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}

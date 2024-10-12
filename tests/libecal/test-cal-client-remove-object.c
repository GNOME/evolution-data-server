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

static gchar *
create_object (ECalClient *cal_client)
{
	ICalComponent *icomp;
	ICalTime *dtstart, *dtend;
	gchar *uid = NULL;
	GError *error = NULL;

	g_return_val_if_fail (cal_client != NULL, NULL);

	dtstart = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());
	dtend = i_cal_time_clone (dtstart);
	i_cal_time_adjust (dtend, 0, 1, 0, 0);

	icomp = i_cal_component_new (I_CAL_VEVENT_COMPONENT);
	i_cal_component_set_summary (icomp, "To-be-removed event summary");
	i_cal_component_set_dtstart (icomp, dtstart);
	i_cal_component_set_dtend (icomp, dtend);

	g_clear_object (&dtstart);
	g_clear_object (&dtend);

	if (!e_cal_client_create_object_sync (cal_client, icomp, E_CAL_OPERATION_FLAG_NONE, &uid, NULL, &error))
		g_error ("create object sync: %s", error->message);

	g_object_unref (icomp);

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
	g_assert_true (uid != NULL);

	if (!e_cal_client_remove_object_sync (cal_client, uid, NULL, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, &error))
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
	g_assert_true (uid != NULL);

	e_cal_client_remove_object (cal_client, uid, NULL, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, async_remove_result_ready, fixture->loop);
	g_free (uid);
	g_main_loop_run (fixture->loop);
}

static void
test_remove_object_empty_uid (ETestServerFixture *fixture,
                              gconstpointer user_data)
{
	ECalClient *cal_client;
	gboolean success;
	GError *error = NULL;

	g_test_bug ("697705");

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	success = e_cal_client_remove_object_sync (cal_client, "", NULL, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, &error);
	g_assert_error (
		error, E_CAL_CLIENT_ERROR,
		E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND);
	g_assert_true (!success);
	g_clear_error (&error);
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

	return e_test_server_utils_run (argc, argv);
}

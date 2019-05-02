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

static ICalComponent *
create_object (void)
{
	ICalTime *dtstart, *dtend;
	ICalComponent *icomp;

	dtstart = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());
	dtend = i_cal_time_clone (dtstart);
	i_cal_time_adjust (dtend, 0, 1, 0, 0);

	icomp = i_cal_component_new (I_CAL_VEVENT_COMPONENT);
	i_cal_component_set_summary (icomp, "To-be-received event summary");
	i_cal_component_set_dtstart (icomp, dtstart);
	i_cal_component_set_dtend (icomp, dtend);

	g_clear_object (&dtstart);
	g_clear_object (&dtend);

	return icomp;
}

static void
test_receive_objects_sync (ETestServerFixture *fixture,
                           gconstpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	ICalComponent *icomp;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	icomp = create_object ();
	g_assert_nonnull (icomp);

	if (!e_cal_client_receive_objects_sync (cal_client, icomp, E_CAL_OPERATION_FLAG_NONE, NULL, &error))
		g_error ("receive objects sync: %s", error->message);

	g_object_unref (icomp);
}

static void
async_receive_result_ready (GObject *source_object,
                            GAsyncResult *result,
                            gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_receive_objects_finish (cal_client, result, &error))
		g_error ("receive objects finish: %s", error->message);

	g_main_loop_quit (loop);
}

static void
test_receive_objects_async (ETestServerFixture *fixture,
                            gconstpointer user_data)
{
	ECalClient *cal_client;
	ICalComponent *icomp;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	icomp = create_object ();
	g_assert_nonnull (icomp);

	e_cal_client_receive_objects (cal_client, icomp, E_CAL_OPERATION_FLAG_NONE, NULL, async_receive_result_ready, fixture->loop);
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
		"/ECalClient/ReceiveObjects/Sync",
		ETestServerFixture,
		&cal_closure_sync,
		e_test_server_utils_setup,
		test_receive_objects_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/ReceiveObjects/Async",
		ETestServerFixture,
		&cal_closure_async,
		e_test_server_utils_setup,
		test_receive_objects_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

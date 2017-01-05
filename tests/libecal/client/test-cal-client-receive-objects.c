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
#include <libical/ical.h>

#include "e-test-server-utils.h"

static ETestServerClosure cal_closure_sync =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, FALSE };
static ETestServerClosure cal_closure_async =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, TRUE };

static icalcomponent *
create_object (void)
{
	icalcomponent *icalcomp;
	struct icaltimetype now;

	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	icalcomponent_set_summary (icalcomp, "To-be-received event summary");
	icalcomponent_set_dtstart (icalcomp, now);
	icalcomponent_set_dtend   (icalcomp, icaltime_from_timet_with_zone (icaltime_as_timet (now) + 60 * 60 * 60, 0, NULL));

	return icalcomp;
}

static void
test_receive_objects_sync (ETestServerFixture *fixture,
                           gconstpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icalcomponent *icalcomp;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	icalcomp = create_object ();
	g_assert (icalcomp != NULL);

	if (!e_cal_client_receive_objects_sync (cal_client, icalcomp, NULL, &error))
		g_error ("receive objects sync: %s", error->message);

	icalcomponent_free (icalcomp);
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
	icalcomponent *icalcomp;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	icalcomp = create_object ();
	g_assert (icalcomp != NULL);

	e_cal_client_receive_objects (cal_client, icalcomp, NULL, async_receive_result_ready, fixture->loop);
	icalcomponent_free (icalcomp);

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

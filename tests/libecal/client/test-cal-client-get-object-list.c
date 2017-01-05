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

#define EVENT_SUMMARY "Creation of new test event"
#define EVENT_QUERY "(contains? \"summary\" \"" EVENT_SUMMARY "\")"

static ETestServerClosure cal_closure_sync =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, FALSE };
static ETestServerClosure cal_closure_async =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, TRUE };

static void
setup_cal (ECalClient *cal_client)
{
	struct icaltimetype now;
	icalcomponent *icalcomp;
	gchar *uid = NULL;
	GError *error = NULL;

	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	icalcomponent_set_summary (icalcomp, EVENT_SUMMARY);
	icalcomponent_set_dtstart (icalcomp, now);
	icalcomponent_set_dtend   (icalcomp, icaltime_from_timet_with_zone (icaltime_as_timet (now) + 60 * 60 * 60, 0, NULL));

	if (!e_cal_client_create_object_sync (cal_client, icalcomp, &uid, NULL, &error))
		g_error ("create object sync: %s", error->message);

	icalcomponent_free (icalcomp);
	g_free (uid);
}

static void
test_result (icalcomponent *icalcomp)
{
	g_assert (icalcomp != NULL);
	g_assert_cmpstr (icalcomponent_get_summary (icalcomp), ==, EVENT_SUMMARY);
}

static void
test_get_object_list_sync (ETestServerFixture *fixture,
                           gconstpointer user_data)
{
	ECalClient *cal_client;
	GSList *icalcomps = NULL, *ecalcomps = NULL;
	GError *error = NULL;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);
	setup_cal (cal_client);

	if (!e_cal_client_get_object_list_sync (cal_client, EVENT_QUERY, &icalcomps, NULL, &error))
		g_error ("get object list sync: %s", error->message);

	g_assert_cmpint (g_slist_length (icalcomps), ==, 1);
	test_result (icalcomps->data);

	e_cal_client_free_icalcomp_slist (icalcomps);

	if (!e_cal_client_get_object_list_as_comps_sync (cal_client, EVENT_QUERY, &ecalcomps, NULL, &error))
		g_error ("get object list as comps sync: %s", error->message);

	g_assert_cmpint (g_slist_length (ecalcomps), ==, 1);
	test_result (e_cal_component_get_icalcomponent (ecalcomps->data));

	e_cal_client_free_ecalcomp_slist (ecalcomps);
}

static void
async_get_object_list_as_comps_result_ready (GObject *source_object,
                                             GAsyncResult *result,
                                             gpointer user_data)
{
	ECalClient *cal_client;
	GMainLoop *loop = (GMainLoop *) user_data;
	GError *error = NULL;
	GSList *ecalcomps = NULL;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_object_list_as_comps_finish (cal_client, result, &ecalcomps, &error))
		g_error ("get object list as comps finish: %s", error->message);

	g_assert_cmpint (g_slist_length (ecalcomps), ==, 1);
	test_result (e_cal_component_get_icalcomponent (ecalcomps->data));

	e_cal_client_free_ecalcomp_slist (ecalcomps);
	g_main_loop_quit (loop);
}

static void
async_get_object_list_result_ready (GObject *source_object,
                                    GAsyncResult *result,
                                    gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GSList *icalcomps = NULL;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_object_list_finish (cal_client, result, &icalcomps, &error))
		g_error ("get object list finish: %s", error->message);

	g_assert_cmpint (g_slist_length (icalcomps), ==, 1);
	test_result (icalcomps->data);
	e_cal_client_free_icalcomp_slist (icalcomps);

	e_cal_client_get_object_list_as_comps (
		cal_client, EVENT_QUERY, NULL,
		async_get_object_list_as_comps_result_ready, user_data);
}

static void
test_get_object_list_async (ETestServerFixture *fixture,
                            gconstpointer user_data)
{
	ECalClient *cal_client;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);
	setup_cal (cal_client);

	e_cal_client_get_object_list (cal_client, EVENT_QUERY, NULL, async_get_object_list_result_ready, fixture->loop);
	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECalClient/GetObjectList/Sync",
		ETestServerFixture,
		&cal_closure_sync,
		e_test_server_utils_setup,
		test_get_object_list_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/GetObjectList/Async",
		ETestServerFixture,
		&cal_closure_async,
		e_test_server_utils_setup,
		test_get_object_list_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

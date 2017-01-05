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

static void
test_icalcomps (icalcomponent *icalcomp1,
                icalcomponent *icalcomp2)
{
	struct icaltimetype t1, t2;

	if (!icalcomp2)
		g_error ("Failure: get object returned NULL");

	g_assert_cmpstr (icalcomponent_get_uid (icalcomp1), ==, icalcomponent_get_uid (icalcomp2));
	g_assert_cmpstr (icalcomponent_get_summary (icalcomp1), ==, icalcomponent_get_summary (icalcomp2));

	t1 = icalcomponent_get_dtstart (icalcomp1);
	t2 = icalcomponent_get_dtstart (icalcomp2);

	if (icaltime_compare (t1, t2) != 0)
		g_error (
			"Failure: dtend doesn't match, expected '%s', got '%s'\n",
			icaltime_as_ical_string (t1), icaltime_as_ical_string (t2));

	t1 = icalcomponent_get_dtend (icalcomp1);
	t2 = icalcomponent_get_dtend (icalcomp2);

	if (icaltime_compare (t1, t2) != 0)
		g_error (
			"Failure: dtend doesn't match, expected '%s', got '%s'\n",
			icaltime_as_ical_string (t1), icaltime_as_ical_string (t2));
}

static void
test_create_object_sync (ETestServerFixture *fixture,
                         gconstpointer user_data)
{
	ECalClient *cal_client;
	icalcomponent *icalcomp;
	icalcomponent *icalcomp2 = NULL, *clone;
	struct icaltimetype now;
	GError *error = NULL;
	GSList *ecalcomps = NULL;
	gchar *uid = NULL;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	/* Build up new component */
	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	icalcomponent_set_summary (icalcomp, "Test event summary");
	icalcomponent_set_dtstart (icalcomp, now);
	icalcomponent_set_dtend   (icalcomp, icaltime_from_timet_with_zone (icaltime_as_timet (now) + 60 * 60 * 60, 0, NULL));

	if (!e_cal_client_create_object_sync (cal_client, icalcomp, &uid, NULL, &error))
		g_error ("create object sync: %s", error->message);

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icalcomp2, NULL, &error))
		g_error ("get object sync: %s", error->message);

	clone = icalcomponent_new_clone (icalcomp);
	icalcomponent_set_uid (clone, uid);

	test_icalcomps (clone, icalcomp2);

	icalcomponent_free (icalcomp2);

	if (!e_cal_client_get_objects_for_uid_sync (cal_client, uid, &ecalcomps, NULL, &error))
		g_error ("get objects for uid sync: %s", error->message);

	if (g_slist_length (ecalcomps) != 1)
		g_error ("Failure: expected 1 component, bug got %d", g_slist_length (ecalcomps));
	else {
		ECalComponent *ecalcomp = ecalcomps->data;

		test_icalcomps (clone, e_cal_component_get_icalcomponent (ecalcomp));
	}
	e_cal_client_free_ecalcomp_slist (ecalcomps);

	icalcomponent_free (clone);
	g_free (uid);
	icalcomponent_free (icalcomp);
}

typedef struct {
	icalcomponent *icalcomp;
	icalcomponent *clone;
	GMainLoop *loop;
} AsyncData;

static void
async_read2_result_ready (GObject *source_object,
                          GAsyncResult *result,
                          gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	AsyncData *data = (AsyncData *) user_data;
	icalcomponent *icalcomp1 = data->clone;
	GSList *ecalcomps = NULL;

	g_return_if_fail (icalcomp1 != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_objects_for_uid_finish (cal_client, result, &ecalcomps, &error))
		g_error ("get objects for uid finish: %s", error->message);

	if (g_slist_length (ecalcomps) != 1)
		g_error ("Failure: expected 1 component, bug got %d", g_slist_length (ecalcomps));
	else {
		ECalComponent *ecalcomp = ecalcomps->data;

		test_icalcomps (icalcomp1, e_cal_component_get_icalcomponent (ecalcomp));
	}
	e_cal_client_free_ecalcomp_slist (ecalcomps);

	icalcomponent_free (icalcomp1);
	g_main_loop_quit (data->loop);
}

static void
async_read_result_ready (GObject *source_object,
                         GAsyncResult *result,
                         gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	AsyncData *data = (AsyncData *) user_data;
	icalcomponent *icalcomp1 = data->clone, *icalcomp2 = NULL;

	g_return_if_fail (icalcomp1 != NULL);

	cal_client = E_CAL_CLIENT (source_object);
	if (!e_cal_client_get_object_finish (cal_client, result, &icalcomp2, &error))
		g_error ("get object finish: %s", error->message);

	test_icalcomps (icalcomp1, icalcomp2);
	icalcomponent_free (icalcomp2);

	e_cal_client_get_objects_for_uid (cal_client,
					  icalcomponent_get_uid (icalcomp1), NULL,
					  async_read2_result_ready, data);
}

static void
async_write_result_ready (GObject *source_object,
                          GAsyncResult *result,
                          gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	gchar *uid = NULL;
	AsyncData *data = (AsyncData *) user_data;
	icalcomponent *clone, *icalcomp = data->icalcomp;

	g_return_if_fail (icalcomp != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_create_object_finish (cal_client, result, &uid, &error))
		g_error ("create object finish: %s", error->message);

	clone = icalcomponent_new_clone (icalcomp);
	icalcomponent_set_uid (clone, uid);

	data->clone = clone;
	e_cal_client_get_object (cal_client, uid, NULL, NULL, async_read_result_ready, data);
	g_free (uid);
}

static void
test_create_object_async (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	ECalClient *cal_client;
	icalcomponent *icalcomp;
	struct icaltimetype now;
	AsyncData data = { 0, };

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	/* Build up new component */
	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	icalcomponent_set_summary (icalcomp, "Test event summary");
	icalcomponent_set_dtstart (icalcomp, now);
	icalcomponent_set_dtend   (icalcomp, icaltime_from_timet_with_zone (icaltime_as_timet (now) + 60 * 60 * 60, 0, NULL));

	data.icalcomp = icalcomp;
	data.loop = fixture->loop;
	e_cal_client_create_object (cal_client, icalcomp, NULL, async_write_result_ready, &data);
	g_main_loop_run (fixture->loop);

	icalcomponent_free (icalcomp);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECalClient/CreateObject/Sync",
		ETestServerFixture,
		&cal_closure_sync,
		e_test_server_utils_setup,
		test_create_object_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/CreateObject/Async",
		ETestServerFixture,
		&cal_closure_async,
		e_test_server_utils_setup,
		test_create_object_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

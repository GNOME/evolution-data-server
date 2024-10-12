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

static void
test_icomps (ICalComponent *icomp1,
	     ICalComponent *icomp2)
{
	ICalTime *t1, *t2;

	if (!icomp2)
		g_error ("Failure: get object returned NULL");

	g_assert_cmpstr (i_cal_component_get_uid (icomp1), ==, i_cal_component_get_uid (icomp2));
	g_assert_cmpstr (i_cal_component_get_summary (icomp1), ==, i_cal_component_get_summary (icomp2));

	t1 = i_cal_component_get_dtstart (icomp1);
	t2 = i_cal_component_get_dtstart (icomp2);

	if (i_cal_time_compare (t1, t2) != 0) {
		gchar *str1, *str2;

		str1 = i_cal_time_as_ical_string (t1);
		str2 = i_cal_time_as_ical_string (t2);

		g_error ("Failure: dtend doesn't match, expected '%s', got '%s'\n", str1, str2);

		g_free (str1);
		g_free (str2);
	}

	g_clear_object (&t1);
	g_clear_object (&t2);

	t1 = i_cal_component_get_dtend (icomp1);
	t2 = i_cal_component_get_dtend (icomp2);

	if (i_cal_time_compare (t1, t2) != 0) {
		gchar *str1, *str2;

		str1 = i_cal_time_as_ical_string (t1);
		str2 = i_cal_time_as_ical_string (t2);

		g_error ("Failure: dtend doesn't match, expected '%s', got '%s'\n", str1, str2);

		g_free (str1);
		g_free (str2);
	}

	g_clear_object (&t1);
	g_clear_object (&t2);
}

static void
test_create_object_sync (ETestServerFixture *fixture,
                         gconstpointer user_data)
{
	ECalClient *cal_client;
	ICalComponent *icomp;
	ICalComponent *icomp2 = NULL, *clone;
	ICalTime *dtstart, *dtend;
	GError *error = NULL;
	GSList *ecalcomps = NULL;
	gchar *uid = NULL;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	/* Build up new component */
	dtstart = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());
	dtend = i_cal_time_clone (dtstart);
	i_cal_time_adjust (dtend, 0, 1, 0, 0);

	icomp = i_cal_component_new (I_CAL_VEVENT_COMPONENT);
	i_cal_component_set_summary (icomp, "Test event summary");
	i_cal_component_set_dtstart (icomp, dtstart);
	i_cal_component_set_dtend (icomp, dtend);

	g_clear_object (&dtstart);
	g_clear_object (&dtend);

	if (!e_cal_client_create_object_sync (cal_client, icomp, E_CAL_OPERATION_FLAG_NONE, &uid, NULL, &error))
		g_error ("create object sync: %s", error->message);

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icomp2, NULL, &error))
		g_error ("get object sync: %s", error->message);

	clone = i_cal_component_clone (icomp);
	i_cal_component_set_uid (clone, uid);

	test_icomps (clone, icomp2);

	g_object_unref (icomp2);

	if (!e_cal_client_get_objects_for_uid_sync (cal_client, uid, &ecalcomps, NULL, &error))
		g_error ("get objects for uid sync: %s", error->message);

	if (g_slist_length (ecalcomps) != 1)
		g_error ("Failure: expected 1 component, bug got %d", g_slist_length (ecalcomps));
	else {
		ECalComponent *ecalcomp = ecalcomps->data;

		test_icomps (clone, e_cal_component_get_icalcomponent (ecalcomp));
	}
	e_util_free_nullable_object_slist (ecalcomps);

	g_object_unref (clone);
	g_object_unref (icomp);
	g_free (uid);
}

typedef struct {
	ICalComponent *icomp;
	ICalComponent *clone;
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
	ICalComponent *icomp1 = data->clone;
	GSList *ecalcomps = NULL;

	g_return_if_fail (icomp1 != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_objects_for_uid_finish (cal_client, result, &ecalcomps, &error))
		g_error ("get objects for uid finish: %s", error->message);

	if (g_slist_length (ecalcomps) != 1)
		g_error ("Failure: expected 1 component, bug got %d", g_slist_length (ecalcomps));
	else {
		ECalComponent *ecalcomp = ecalcomps->data;

		test_icomps (icomp1, e_cal_component_get_icalcomponent (ecalcomp));
	}
	e_util_free_nullable_object_slist (ecalcomps);

	g_object_unref (icomp1);
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
	ICalComponent *icomp1 = data->clone, *icomp2 = NULL;

	g_return_if_fail (icomp1 != NULL);

	cal_client = E_CAL_CLIENT (source_object);
	if (!e_cal_client_get_object_finish (cal_client, result, &icomp2, &error))
		g_error ("get object finish: %s", error->message);

	test_icomps (icomp1, icomp2);
	g_object_unref (icomp2);

	e_cal_client_get_objects_for_uid (cal_client,
					  i_cal_component_get_uid (icomp1), NULL,
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
	ICalComponent *clone, *icomp = data->icomp;

	g_return_if_fail (icomp != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_create_object_finish (cal_client, result, &uid, &error))
		g_error ("create object finish: %s", error->message);

	clone = i_cal_component_clone (icomp);
	i_cal_component_set_uid (clone, uid);

	data->clone = clone;
	e_cal_client_get_object (cal_client, uid, NULL, NULL, async_read_result_ready, data);
	g_free (uid);
}

static void
test_create_object_async (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	ECalClient *cal_client;
	ICalComponent *icomp;
	ICalTime *dtstart, *dtend;
	AsyncData data = { 0, };

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	/* Build up new component */
	dtstart = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());
	dtend = i_cal_time_clone (dtstart);
	i_cal_time_adjust (dtend, 0, 1, 0, 0);

	icomp = i_cal_component_new (I_CAL_VEVENT_COMPONENT);
	i_cal_component_set_summary (icomp, "Test event summary");
	i_cal_component_set_dtstart (icomp, dtstart);
	i_cal_component_set_dtend (icomp, dtend);

	g_clear_object (&dtstart);
	g_clear_object (&dtend);

	data.icomp = icomp;
	data.loop = fixture->loop;
	e_cal_client_create_object (cal_client, icomp, E_CAL_OPERATION_FLAG_NONE, NULL, async_write_result_ready, &data);
	g_main_loop_run (fixture->loop);

	g_object_unref (icomp);
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

	return e_test_server_utils_run (argc, argv);
}

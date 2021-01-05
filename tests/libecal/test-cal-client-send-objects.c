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

static void
print_ecomp (ECalComponent *ecalcomp)
{
	const gchar *uid;
	ECalComponentText *summary;

	g_return_if_fail (ecalcomp != NULL);

	uid = e_cal_component_get_uid (ecalcomp);
	summary = e_cal_component_get_summary (ecalcomp);

	g_print ("   Component: %s\n", uid ? uid : "no-uid");
	g_print ("   Summary: %s\n", summary && e_cal_component_text_get_value (summary) ? e_cal_component_text_get_value (summary) : "NULL");
	g_print ("\n");

	e_cal_component_text_free (summary);
}

static void
print_icomp (ICalComponent *icomp)
{
	ECalComponent *ecomp;

	g_assert_nonnull (icomp);

	ecomp = e_cal_component_new_from_icalcomponent (i_cal_component_clone (icomp));
	g_assert_nonnull (ecomp);

	print_ecomp (ecomp);

	g_object_unref (ecomp);
}

static ICalComponent *
create_object (void)
{
	ICalComponent *icomp;
	ICalTime *dtstart, *dtend;

	dtstart = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());
	dtend = i_cal_time_clone (dtstart);
	i_cal_time_adjust (dtend, 0, 1, 0, 0);

	icomp = i_cal_component_new (I_CAL_VEVENT_COMPONENT);
	i_cal_component_set_summary (icomp, "To-be-sent event summary");
	i_cal_component_set_dtstart (icomp, dtstart);
	i_cal_component_set_dtend (icomp, dtend);

	g_clear_object (&dtstart);
	g_clear_object (&dtend);

	return icomp;
}

static void
manage_result (GSList *users,
	       ICalComponent *modified_icomp)
{
	g_print ("Wishes to send to %d users", g_slist_length (users));
	if (users) {
		GSList *u;

		g_print (": ");

		for (u = users; u; u = u->next)
			g_print ("%s%s", u == users ? "" : ", ", (const gchar *) u->data);
	}
	g_print ("\n");

	if (!modified_icomp)
		g_print ("No modified iCalendar component, would send the same\n");
	else
		print_icomp (modified_icomp);

	e_client_util_free_string_slist (users);
	g_clear_object (&modified_icomp);
}

static void
test_send_objects_sync (ETestServerFixture *fixture,
                        gconstpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	ICalComponent *icomp, *modified_icomp = NULL;
	GSList *users = NULL;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	icomp = create_object ();
	if (!e_cal_client_send_objects_sync (cal_client, icomp, E_CAL_OPERATION_FLAG_NONE, &users, &modified_icomp, NULL, &error))
		g_error ("send objects sync: %s", error->message);

	g_object_unref (icomp);
	manage_result (users, modified_icomp);
}

static void
async_send_result_ready (GObject *source_object,
                         GAsyncResult *result,
                         gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GSList *users = NULL;
	ICalComponent *modified_icomp = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_send_objects_finish (cal_client, result, &users, &modified_icomp, &error))
		g_error ("send objects finish: %s", error->message);

	manage_result (users, modified_icomp);
	g_main_loop_quit (loop);
}

static void
test_send_objects_async (ETestServerFixture *fixture,
                        gconstpointer user_data)
{
	ECalClient *cal_client;
	ICalComponent *icomp;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	icomp = create_object ();
	g_assert_nonnull (icomp);

	e_cal_client_send_objects (cal_client, icomp, E_CAL_OPERATION_FLAG_NONE, NULL, async_send_result_ready, fixture->loop);
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
		"/ECalClient/SendObjects/Sync",
		ETestServerFixture,
		&cal_closure_sync,
		e_test_server_utils_setup,
		test_send_objects_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/SendObjects/Async",
		ETestServerFixture,
		&cal_closure_async,
		e_test_server_utils_setup,
		test_send_objects_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}

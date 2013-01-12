/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "e-test-server-utils.h"
#include "client-test-utils.h" /* For print_icomp() */

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS };

static icalcomponent *
create_object (void)
{
	icalcomponent *icalcomp;
	struct icaltimetype now;

	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	icalcomponent_set_summary (icalcomp, "To-be-sent event summary");
	icalcomponent_set_dtstart (icalcomp, now);
	icalcomponent_set_dtend   (icalcomp, icaltime_from_timet (icaltime_as_timet (now) + 60 * 60 * 60, 0));

	return icalcomp;
}

static void
manage_result (GSList *users,
               icalcomponent *modified_icalcomp)
{
	g_print ("Wishes to send to %d users", g_slist_length (users));
	if (users) {
		GSList *u;

		g_print (": ");

		for (u = users; u; u = u->next)
			g_print ("%s%s", u == users ? "" : ", ", (const gchar *) u->data);
	}
	g_print ("\n");

	if (!modified_icalcomp)
		g_print ("No modified icalcomp, would send the same\n");
	else
		print_icomp (modified_icalcomp);

	e_client_util_free_string_slist (users);
	if (modified_icalcomp)
		icalcomponent_free (modified_icalcomp);
}

static void
test_send_objects_sync (ETestServerFixture *fixture,
                        gconstpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icalcomponent *icalcomp, *modified_icalcomp = NULL;
	GSList *users = NULL;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	icalcomp = create_object ();
	if (!e_cal_client_send_objects_sync (cal_client, icalcomp, &users, &modified_icalcomp, NULL, &error))
		g_error ("send objects sync: %s", error->message);

	icalcomponent_free (icalcomp);
	manage_result (users, modified_icalcomp);
}

static void
async_send_result_ready (GObject *source_object,
                         GAsyncResult *result,
                         gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GSList *users = NULL;
	icalcomponent *modified_icalcomp = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_send_objects_finish (cal_client, result, &users, &modified_icalcomp, &error))
		g_error ("send objects finish: %s", error->message);

	manage_result (users, modified_icalcomp);
	g_main_loop_quit (loop);
}

static void
test_send_objects_async (ETestServerFixture *fixture,
                        gconstpointer user_data)
{
	ECalClient *cal_client;
	icalcomponent *icalcomp;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	icalcomp = create_object ();
	g_assert (icalcomp);

	e_cal_client_send_objects (cal_client, icalcomp, NULL, async_send_result_ready, fixture->loop);
	icalcomponent_free (icalcomp);

	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	g_test_add (
		"/ECalClient/SendObjects/Sync", ETestServerFixture, &cal_closure,
		e_test_server_utils_setup, test_send_objects_sync, e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/SendObjects/Async", ETestServerFixture, &cal_closure,
		e_test_server_utils_setup, test_send_objects_async, e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

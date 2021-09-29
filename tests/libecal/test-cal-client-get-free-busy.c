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

#define MAIL_ACCOUNT_UID   "test-email-account"
#define MAIL_IDENTITY_UID  "test-email-identity"
#define USER_EMAIL         "user@example.com"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS };

static gboolean received_free_busy_data = FALSE;

static void
setup_fixture (ETestServerFixture *fixture,
               gconstpointer user_data)
{
	GError *error = NULL;
	ESource *scratch;
	ESourceMailAccount *mail_account;
	ESourceMailIdentity *mail_identity;

	e_test_server_utils_setup (fixture, user_data);

	/* Create the mail identity */
	scratch = e_source_new_with_uid (MAIL_IDENTITY_UID, NULL, &error);
	if (!scratch)
		g_error ("Failed to create scratch source for a mail identity: %s", error->message);

	mail_identity = e_source_get_extension (scratch, E_SOURCE_EXTENSION_MAIL_IDENTITY);
	e_source_mail_identity_set_address (mail_identity, USER_EMAIL);

	if (!e_source_registry_commit_source_sync (fixture->registry, scratch, NULL, &error))
		g_error ("Unable to add new scratch mail identity source to the registry: %s", error->message);

	g_object_unref (scratch);

	/* Create the mail account */
	scratch = e_source_new_with_uid (MAIL_ACCOUNT_UID, NULL, &error);
	if (!scratch)
		g_error ("Failed to create scratch source for a mail account: %s", error->message);

	mail_account = e_source_get_extension (scratch, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
	e_source_mail_account_set_identity_uid (mail_account, MAIL_IDENTITY_UID);

	if (!e_source_registry_commit_source_sync (fixture->registry, scratch, NULL, &error))
		g_error ("Unable to add new scratch mail account source to the registry: %s", error->message);

	g_object_unref (scratch);
}

static void
teardown_fixture (ETestServerFixture *fixture,
                  gconstpointer user_data)
{
	GError *error = NULL;
	ESource *source;

	/* Remove the account */
	source = e_source_registry_ref_source (fixture->registry, MAIL_ACCOUNT_UID);
	if (!source)
		g_error ("Unable to fetch mail account");

	if (!e_source_remove_sync (source, NULL, &error))
		g_error ("Unable to remove mail account: %s", error->message);

	/* Remove the identity */
	source = e_source_registry_ref_source (fixture->registry, MAIL_IDENTITY_UID);
	if (!source)
		g_error ("Unable to fetch mail identity");

	if (!e_source_remove_sync (source, NULL, &error))
		g_error ("Unable to remove mail identity: %s", error->message);

	e_test_server_utils_teardown (fixture, user_data);
}

static void
add_component_sync (ECalClient *cal_client)
{
	const gchar *comp_str =
		"BEGIN:VEVENT\r\n"
		"UID:test-fb-event-1\r\n"
		"DTSTAMP:20040212T000000Z\r\n"
		"DTSTART:20040213T060000Z\r\n"
		"DTEND:20040213T080000Z\r\n"
		"SUMMARY:Test event\r\n"
		"TRANSP:OPAQUE\r\n"
		"CLASS:PUBLIC\r\n"
		"CREATED:20040211T080000Z\r\n"
		"LAST-MODIFIED:20040211T080000Z\r\n"
		"END:VEVENT\r\n";
	ICalComponent *icomp;
	GError *error = NULL;

	icomp = i_cal_component_new_from_string (comp_str);
	g_assert_nonnull (icomp);

	if (!e_cal_client_create_object_sync (cal_client, icomp, E_CAL_OPERATION_FLAG_NONE, NULL, NULL, &error))
		g_error ("Failed to add component: %s", error ? error->message : "Unknown error");

	g_object_unref (icomp);
	g_clear_error (&error);
}

static void
wait_for_dbus_signal (GMainLoop *loop)
{
	GMainContext *main_context;
	gint retries = 0;

	main_context = g_main_loop_get_context (loop);

	while (!received_free_busy_data && retries < 5) {
		retries++;

		g_main_context_iteration (main_context, TRUE);
	}
}

static void
free_busy_data_cb (ECalClient *client,
                   const GSList *free_busy,
                   const gchar *func_name)
{
	if (g_slist_length ((GSList *) free_busy) > 0)
		received_free_busy_data = TRUE;
}

static void
test_get_free_busy_sync (ETestServerFixture *fixture,
                         gconstpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	ICalTimezone *utc;
	GSList *users = NULL, *freebusy_data = NULL;
	time_t start, end;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	add_component_sync (cal_client);

	/* This is set by the free-busy-data callback */
	received_free_busy_data = FALSE;

	g_signal_connect (cal_client, "free-busy-data", G_CALLBACK (free_busy_data_cb), (gpointer) G_STRFUNC);

	utc = i_cal_timezone_get_utc_timezone ();
	start = time_from_isodate ("20040212T000000Z");
	end = time_add_day_with_zone (start, 2, utc);
	users = g_slist_append (users, (gpointer) USER_EMAIL);

	if (!e_cal_client_get_free_busy_sync (cal_client, start, end, users, &freebusy_data, NULL, &error))
		g_error ("get free busy sync: %s", error->message);

	g_slist_free (users);

	wait_for_dbus_signal (fixture->loop);

	g_assert_true (received_free_busy_data);
	g_assert_true (freebusy_data);

	g_slist_free_full (freebusy_data, g_object_unref);
}

static void
async_get_free_busy_result_ready (GObject *source_object,
                                  GAsyncResult *result,
                                  gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;
	GSList *freebusy_data = NULL;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_free_busy_finish (cal_client, result, &freebusy_data, &error))
		g_error ("create object finish: %s", error->message);

	wait_for_dbus_signal (loop);

	g_assert_true (received_free_busy_data);
	g_assert_true (freebusy_data);

	g_slist_free_full (freebusy_data, g_object_unref);

	g_main_loop_quit (loop);
}

static void
test_get_free_busy_async (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	ECalClient *cal_client;
	ICalTimezone *utc;
	GSList *users = NULL;
	time_t start, end;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	add_component_sync (cal_client);

	/* This is set by the free-busy-data callback */
	received_free_busy_data = FALSE;

	utc = i_cal_timezone_get_utc_timezone ();
	start = time_from_isodate ("20040212T000000Z");
	end = time_add_day_with_zone (start, 2, utc);
	users = g_slist_append (users, (gpointer) USER_EMAIL);

	/* here is all Free/Busy information received */
	g_signal_connect (cal_client, "free-busy-data", G_CALLBACK (free_busy_data_cb), (gpointer) G_STRFUNC);

	e_cal_client_get_free_busy (cal_client, start, end, users, NULL, async_get_free_busy_result_ready, fixture->loop);
	g_slist_free (users);

	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECalClient/GetFreeBusy/Sync",
		ETestServerFixture,
		&cal_closure,
		setup_fixture,
		test_get_free_busy_sync,
		teardown_fixture);
	g_test_add (
		"/ECalClient/GetFreeBusy/Async",
		ETestServerFixture,
		&cal_closure,
		setup_fixture,
		test_get_free_busy_async,
		teardown_fixture);

	return e_test_server_utils_run (argc, argv);
}

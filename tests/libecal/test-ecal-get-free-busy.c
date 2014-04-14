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

#include "ecal-test-utils.h"
#include "e-test-server-utils.h"

#define MAIL_ACCOUNT_UID   "test-email-account"
#define MAIL_IDENTITY_UID  "test-email-identity"
#define USER_EMAIL         "user@example.com"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };

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
		g_error ("Failed to create scratch source for an email user: %s", error->message);

	mail_identity = e_source_get_extension (scratch, E_SOURCE_EXTENSION_MAIL_IDENTITY);
	e_source_mail_identity_set_address (mail_identity, USER_EMAIL);

	if (!e_source_registry_commit_source_sync (fixture->registry, scratch, NULL, &error))
		g_error ("Unable to add new addressbook source to the registry: %s", error->message);

	g_object_unref (scratch);

	/* Create the mail account */
	scratch = e_source_new_with_uid (MAIL_ACCOUNT_UID, NULL, &error);
	if (!scratch)
		g_error ("Failed to create scratch source for an email user: %s", error->message);

	mail_account = e_source_get_extension (scratch, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
	e_source_mail_account_set_identity_uid (mail_account, MAIL_IDENTITY_UID);

	if (!e_source_registry_commit_source_sync (fixture->registry, scratch, NULL, &error))
		g_error ("Unable to add new addressbook source to the registry: %s", error->message);

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
test_get_free_busy (ETestServerFixture *fixture,
                    gconstpointer user_data)
{
	ECal *cal;
	GList *users = NULL;
	icaltimezone *utc;
	time_t start, end;
	GList *free_busy;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECal);

	utc = icaltimezone_get_utc_timezone ();
	start = time_from_isodate ("20040212T000000Z");
	end = time_add_day_with_zone (start, 2, utc);
	users = g_list_prepend (users, (gpointer) USER_EMAIL);

	free_busy = ecal_test_utils_cal_get_free_busy (cal, users, start, end);

	g_list_foreach (free_busy, (GFunc) g_object_unref, NULL);
	g_list_free (free_busy);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECal/GetFreeBusy",
		ETestServerFixture,
		&cal_closure,
		setup_fixture,
		test_get_free_busy,
		teardown_fixture);

	return e_test_server_utils_run ();
}

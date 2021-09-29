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
#include <string.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0 };

#define CYCLES 10

static void
get_revision_compare_cycle (EBookClient *client)
{
	gchar    *revision_before = NULL, *revision_after = NULL;
	EContact *contact = NULL;
	GError   *error = NULL;

	if (!e_client_get_backend_property_sync (E_CLIENT (client), CLIENT_BACKEND_PROPERTY_REVISION, &revision_before, NULL, &error))
		g_error ("Error getting book revision: %s", error->message);

	if (!add_contact_from_test_case_verify (client, "simple-1", &contact)) {
		g_object_unref (client);
		exit (1);
	}

	if (!e_book_client_remove_contact_sync (client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, &error))
		g_error ("Unable to remove contact: %s", error->message);

	g_object_unref (contact);

	if (!e_client_get_backend_property_sync (E_CLIENT (client), CLIENT_BACKEND_PROPERTY_REVISION, &revision_after, NULL, &error))
		g_error ("Error getting book revision: %s", error->message);

	/* Sometimes, kind of rarely, the D-Bus property change is not delivered on time,
	   thus give it some time to be received and processed. */
	if (g_strcmp0 (revision_before, revision_after) == 0) {
		g_message ("   D-Bus property 'revision' change not received yet, trying to wait a bit");

		g_usleep (G_USEC_PER_SEC / 2);

		g_clear_pointer (&revision_after, g_free);

		if (!e_client_get_backend_property_sync (E_CLIENT (client), CLIENT_BACKEND_PROPERTY_REVISION, &revision_after, NULL, &error))
			g_error ("Error getting book revision: %s", error->message);
	}

	g_assert_true (revision_before);
	g_assert_true (revision_after);
	g_assert_cmpstr (revision_before, !=, revision_after);

	g_message (
		"Passed cycle, revision before '%s' revision after '%s'",
		revision_before, revision_after);

	g_free (revision_before);
	g_free (revision_after);
}

static void
test_get_revision (ETestServerFixture *fixture,
                   gconstpointer user_data)
{
	EBookClient *book_client;
	gint i;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	/* Test that modifications make the revisions increment */
	for (i = 0; i < CYCLES; i++)
		get_revision_compare_cycle (book_client);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	client_test_utils_read_args (argc, argv);

	g_test_add (
		"/EBookClient/GetRevision",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_get_revision,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}

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
#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure_sync = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_async = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };

static void
test_add_contact_sync (ETestServerFixture *fixture,
                       gconstpointer user_data)
{
	EBookClient *book_client;
	EContact *contact;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact)) {
		g_error ("Failed to add contact sync");
	}
	g_object_unref (contact);
}

static void
add_contact_cb (GObject *source_object,
                GAsyncResult *result,
                gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	GError *error = NULL;
	gchar *uid;

	if (!e_book_client_add_contact_finish (E_BOOK_CLIENT (source_object), result, &uid, &error)) {
		g_error ("add contact finish: %s", error->message);
	}

	printf ("Contact added as '%s'\n", uid);
	g_free (uid);
	g_main_loop_quit (loop);
}

static void
test_add_contact_async (ETestServerFixture *fixture,
                        gconstpointer user_data)
{
	EBookClient *book_client;
	EContact *contact;
	gchar *vcard;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	vcard = new_vcard_from_test_case ("simple-1");
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);

	e_book_client_add_contact (book_client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, add_contact_cb, fixture->loop);
	g_object_unref (contact);

	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	client_test_utils_read_args (argc, argv);

	g_test_add (
		"/EBookClient/AddContact/Sync",
		ETestServerFixture,
		&book_closure_sync,
		e_test_server_utils_setup,
		test_add_contact_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/AddContact/Async",
		ETestServerFixture,
		&book_closure_async,
		e_test_server_utils_setup,
		test_add_contact_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}

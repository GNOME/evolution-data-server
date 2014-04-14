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

#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure_sync = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_async = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };

static void
test_remove_contact_by_uid_sync (ETestServerFixture *fixture,
                                 gconstpointer user_data)
{
	EBookClient *book_client;
	GError *error = NULL;
	EContact *contact;
	gchar *uid;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact))
		g_error ("Failed to add contact");

	uid = e_contact_get (contact, E_CONTACT_UID);

	if (!e_book_client_remove_contact_by_uid_sync (book_client, uid, NULL, &error))
		g_error ("remove contact sync: %s", error->message);

	if (!e_book_client_get_contact_sync (book_client, uid, &contact, NULL, &error) &&
	    g_error_matches (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND)) {
		g_clear_error (&error);
	} else
		g_error ("fail with get contact sync on removed contact: %s", error->message);

	g_free (uid);
}

typedef struct {
	const gchar *uid;
	GMainLoop *loop;
} RemoveData;

static void
remove_contact_by_uid_cb (GObject *source_object,
                          GAsyncResult *result,
                          gpointer user_data)
{
	RemoveData *data = (RemoveData *) user_data;
	GError *error = NULL;
	EContact *contact = NULL;

	if (!e_book_client_remove_contact_by_uid_finish (E_BOOK_CLIENT (source_object), result, &error))
		g_error ("remove contact by uid finish: %s", error->message);

	if (!e_book_client_get_contact_sync (E_BOOK_CLIENT (source_object), data->uid, &contact, NULL, &error) &&
	    g_error_matches (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND)) {
		g_clear_error (&error);
	} else
		g_error ("fail with get contact on removed contact: %s", error->message);

	g_main_loop_quit (data->loop);
}

static void
test_remove_contact_by_uid_async (ETestServerFixture *fixture,
                                  gconstpointer user_data)
{
	EBookClient *book_client;
	EContact *contact = NULL;
	gchar *uid;
	RemoveData data;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact))
		g_error ("Failed to add contact");

	uid = e_contact_get (contact, E_CONTACT_UID);

	data.uid = uid;
	data.loop = fixture->loop;
	e_book_client_remove_contact_by_uid (book_client, uid, NULL, remove_contact_by_uid_cb, &data);

	g_object_unref (contact);

	g_main_loop_run (fixture->loop);
	g_free (uid);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/EBookClient/RemoveContactByUid/Sync",
		ETestServerFixture,
		&book_closure_sync,
		e_test_server_utils_setup,
		test_remove_contact_by_uid_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/RemoveContactByUid/Async",
		ETestServerFixture,
		&book_closure_async,
		e_test_server_utils_setup,
		test_remove_contact_by_uid_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

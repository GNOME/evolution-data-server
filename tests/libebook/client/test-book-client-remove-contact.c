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

#include <locale.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure_sync = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_async = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };
static ETestServerClosure book_closure_direct_sync = { E_TEST_SERVER_DIRECT_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_direct_async = { E_TEST_SERVER_DIRECT_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };

static void
check_removed_contact (EBookClient *book_client,
                       const gchar *uid)
{
	GError *error = NULL;
	EContact *contact = NULL;

	if (e_book_client_get_contact_sync (book_client, uid, &contact, NULL, &error))
		g_error ("succeeded to fetch removed contact");
	else if (!g_error_matches (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND))
		g_error (
			"Wrong error in get contact sync on removed contact: %s (domain: %s, code: %d)",
			error->message, g_quark_to_string (error->domain), error->code);
	else
		g_clear_error (&error);

	if (e_book_client_remove_contact_by_uid_sync (book_client, uid, E_BOOK_OPERATION_FLAG_NONE, NULL, &error))
		g_error ("succeeded to remove the already removed contact");
	else if (!g_error_matches (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND))
		g_error (
			"Wrong error in remove contact sync on removed contact: %s (domain: %s, code: %d)",
			error->message, g_quark_to_string (error->domain), error->code);
	else
		g_clear_error (&error);
}

static void
test_remove_contact_sync (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	EBookClient *book_client;
	GError *error = NULL;
	EContact *contact = NULL;
	gchar *uid;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact))
		g_error ("Failed to add contact");

	uid = e_contact_get (contact, E_CONTACT_UID);

	if (!e_book_client_remove_contact_sync (book_client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, &error))
		g_error ("remove contact sync: %s", error->message);

	g_object_unref (contact);

	check_removed_contact (book_client, uid);

	g_free (uid);
}

typedef struct {
	const gchar *uid;
	GMainLoop *loop;
} RemoveData;

static void
remove_contact_cb (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
	RemoveData *data = (RemoveData *) user_data;
	GError *error = NULL;

	if (!e_book_client_remove_contact_finish (E_BOOK_CLIENT (source_object), result, &error))
		g_error ("remove contact finish: %s", error->message);

	check_removed_contact (E_BOOK_CLIENT (source_object), data->uid);

	g_main_loop_quit (data->loop);
}

static void
test_remove_contact_async (ETestServerFixture *fixture,
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
	e_book_client_remove_contact (book_client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, remove_contact_cb, &data);

	g_object_unref (contact);

	g_main_loop_run (fixture->loop);
	g_free (uid);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	client_test_utils_read_args (argc, argv);

	setlocale (LC_ALL, "en_US.UTF-8");

	g_test_add (
		"/EBookClient/RemoveContact/Sync",
		ETestServerFixture,
		&book_closure_sync,
		e_test_server_utils_setup,
		test_remove_contact_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/RemoveContact/Async",
		ETestServerFixture,
		&book_closure_async,
		e_test_server_utils_setup,
		test_remove_contact_async,
		e_test_server_utils_teardown);

	/* We run the direct access variants here because
	 * we're interested in testing the error code from
	 * e_book_client_get_contact(removed_contact_uid). */
	g_test_add (
		"/EBookClient/DirectAccess/RemoveContact/Sync",
		ETestServerFixture,
		&book_closure_direct_sync,
		e_test_server_utils_setup,
		test_remove_contact_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/DirectAccess/RemoveContact/Async",
		ETestServerFixture,
		&book_closure_direct_async,
		e_test_server_utils_setup,
		test_remove_contact_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}

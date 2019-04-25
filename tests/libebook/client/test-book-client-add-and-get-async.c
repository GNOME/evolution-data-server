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

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };

#define N_CONTACTS 5

static gint fetched_contacts = 0;
static gint fetched_uids = 0;
static gint added_contacts = 0;

static void
count_all_uids_cb (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
	EBookClient *book_client;
	GSList *uids = NULL;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	book_client = E_BOOK_CLIENT (source_object);
	g_return_if_fail (book_client != NULL);

	if (!e_book_client_get_contacts_uids_finish (book_client, result, &uids, &error))
		g_error ("get contacts uids finish: %s", error->message);

	fetched_uids = g_slist_length (uids);

	g_slist_foreach (uids, (GFunc) g_free, NULL);
	g_slist_free (uids);

	g_main_loop_quit (loop);
}

static void
count_all_contacts_cb (GObject *source_object,
                       GAsyncResult *result,
                       gpointer user_data)
{
	EBookClient *book_client;
	EBookQuery *query;
	gchar *sexp;
	GSList *contacts = NULL;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	book_client = E_BOOK_CLIENT (source_object);
	g_return_if_fail (book_client != NULL);

	if (!e_book_client_get_contacts_finish (book_client, result, &contacts, &error))
		g_error ("get contacts finish: %s", error->message);

	fetched_contacts = g_slist_length (contacts);

	g_slist_foreach (contacts, (GFunc) g_object_unref, NULL);
	g_slist_free (contacts);

	query = e_book_query_field_exists (E_CONTACT_FULL_NAME);
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	e_book_client_get_contacts_uids (book_client, sexp, NULL, count_all_uids_cb, loop);

	g_free (sexp);
}

static void
count_all_contacts (EBookClient *book_client,
                    GMainLoop *loop)
{
	EBookQuery *query;
	gchar *sexp;

	query = e_book_query_field_exists (E_CONTACT_FULL_NAME);
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	e_book_client_get_contacts (book_client, sexp, NULL, count_all_contacts_cb, loop);

	g_free (sexp);
}

static void
get_contact_cb (GObject *source_object,
                GAsyncResult *result,
                gpointer user_data)
{
	EBookClient *book_client;
	EContact *contact = NULL;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	book_client = E_BOOK_CLIENT (source_object);
	g_return_if_fail (book_client != NULL);

	if (!e_book_client_get_contact_finish (book_client, result, &contact, &error)) {
		g_error ("get contact finish: %s", error->message);
	} else {
		g_object_unref (contact);
	}

	count_all_contacts (book_client, loop);
}

static void
get_contact_async (EBookClient *book_client,
                   GSList *uids,
                   GMainLoop *loop)
{
	const gchar *uid = uids->data;

	e_book_client_get_contact (book_client, uid, NULL, get_contact_cb, loop);
}

static void
contacts_added_cb (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
	EBookClient *book_client;
	GError *error = NULL;
	GSList *uids = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	book_client = E_BOOK_CLIENT (source_object);

	if (!e_book_client_add_contacts_finish (book_client, result, &uids, &error))
		g_error ("client open finish: %s", error->message);

	added_contacts = g_slist_length (uids);
	get_contact_async (book_client, uids, loop);

	e_util_free_string_slist (uids);
}

static void
add_contacts (EBookClient *book_client,
              GMainLoop *loop)
{
	GSList *contacts = NULL;
	EContact *contact;
	gchar *vcard;

	vcard = new_vcard_from_test_case ("custom-1");
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);
	contacts = g_slist_prepend (contacts, contact);

	vcard = new_vcard_from_test_case ("custom-2");
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);
	contacts = g_slist_prepend (contacts, contact);

	vcard = new_vcard_from_test_case ("custom-3");
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);
	contacts = g_slist_prepend (contacts, contact);

	vcard = new_vcard_from_test_case ("custom-4");
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);
	contacts = g_slist_prepend (contacts, contact);

	vcard = new_vcard_from_test_case ("custom-5");
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);
	contacts = g_slist_prepend (contacts, contact);

	e_book_client_add_contacts (book_client, contacts, E_BOOK_OPERATION_FLAG_NONE, NULL, contacts_added_cb, loop);

	e_util_free_object_slist (contacts);
}

static void
test_async (ETestServerFixture *fixture,
            gconstpointer user_data)
{
	EBookClient *book_client;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	add_contacts (book_client, fixture->loop);
	g_main_loop_run (fixture->loop);

	g_assert_cmpint (added_contacts, ==, N_CONTACTS);
	g_assert_cmpint (fetched_contacts, ==, N_CONTACTS);
	g_assert_cmpint (fetched_uids, ==, N_CONTACTS);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/EBookClient/AddAndGet/Async",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

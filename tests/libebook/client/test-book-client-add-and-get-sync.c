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

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };

#define N_CONTACTS 6

static gint fetched_contacts = 0;
static gint fetched_uids = 0;

static void
count_all_uids (EBookClient *book)
{
	GError *error = NULL;
	EBookQuery *query;
	gchar *sexp;
	GSList *uids;

	query = e_book_query_field_exists (E_CONTACT_FULL_NAME);
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	if (!e_book_client_get_contacts_uids_sync (book, sexp, &uids, NULL, &error))
		g_error ("Error getting contact uids: %s", error->message);

	g_free (sexp);

	fetched_uids = g_slist_length (uids);

	g_slist_foreach (uids, (GFunc) g_free, NULL);
	g_slist_free (uids);
}

static void
count_all_contacts (EBookClient *book)
{
	GError *error = NULL;
	EBookQuery *query;
	gchar *sexp;
	GSList *cards;

	query = e_book_query_field_exists (E_CONTACT_FULL_NAME);
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	if (!e_book_client_get_contacts_sync (book, sexp, &cards, NULL, &error))
		g_error ("Error getting contacts: %s", error->message);

	g_free (sexp);

	fetched_contacts = g_slist_length (cards);

	g_slist_foreach (cards, (GFunc) g_object_unref, NULL);
	g_slist_free (cards);
}

static void
test_client (ETestServerFixture *fixture,
             gconstpointer user_data)
{
	EBookClient *book_client;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	/* Add some contacts */
	if (!add_contact_from_test_case_verify (book_client, "custom-1", NULL) ||
	    !add_contact_from_test_case_verify (book_client, "custom-2", NULL) ||
	    !add_contact_from_test_case_verify (book_client, "custom-3", NULL) ||
	    !add_contact_from_test_case_verify (book_client, "custom-4", NULL) ||
	    !add_contact_from_test_case_verify (book_client, "custom-5", NULL) ||
	    !add_contact_from_test_case_verify (book_client, "custom-6", NULL)) {
		g_object_unref (book_client);
		g_error ("Failed to add contacts");
	}

	count_all_contacts (book_client);
	count_all_uids (book_client);

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
		"/EBookClient/AddAndGet/Sync",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_client,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

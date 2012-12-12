/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0 };

static void
print_all_uids (EBookClient *book)
{
	GError *error = NULL;
	EBookQuery *query;
	gchar *sexp;
	gboolean result;
	GSList *uids, *u;

	query = e_book_query_field_exists (E_CONTACT_FULL_NAME);
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	result = e_book_client_get_contacts_uids_sync (book, sexp, &uids, NULL, &error);

	g_free (sexp);

	if (!result) {
		fprintf (stderr, "Error getting uid list: %s\n", error ? error->message : "Unknown error");
		if (error)
			g_error_free (error);
		exit (1);
	}

	for (u = uids; u; u = u->next) {
		const gchar *uid = u->data;

		g_print ("   uid:'%s'\n", uid);
	}

	g_slist_foreach (uids, (GFunc) g_free, NULL);
	g_slist_free (uids);
}

static void
print_all_emails (EBookClient *book)
{
	GError *error = NULL;
	EBookQuery *query;
	gchar *sexp;
	gboolean result;
	GSList *cards, *c;

	query = e_book_query_field_exists (E_CONTACT_FULL_NAME);
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	result = e_book_client_get_contacts_sync (book, sexp, &cards, NULL, &error);

	g_free (sexp);

	if (!result) {
		fprintf (stderr, "Error getting card list: %s\n", error ? error->message : "Unknown error");
		if (error)
			g_error_free (error);
		exit (1);
	}

	for (c = cards; c; c = c->next) {
		EContact *contact = E_CONTACT (c->data);

		print_email (contact);

		g_object_unref (contact);
	}

	g_slist_free (cards);
}

static void
test_client (ETestServerFixture *fixture,
	     gconstpointer       user_data)
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

	printf ("printing all contacts\n");
	print_all_emails (book_client);

	printf ("printing all uids\n");
	print_all_uids (book_client);
}


gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	g_test_add ("/EBookClient/BasicTest", ETestServerFixture, &book_closure,
		    e_test_server_utils_setup, test_client, e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}

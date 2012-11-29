/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"

static void
contacts_ready_cb (GObject *source_object,
		   GAsyncResult *result,
		   gpointer user_data)
{
	GError *error = NULL;
	GSList *contacts = NULL;

	if (!e_book_client_get_contacts_uids_finish (E_BOOK_CLIENT (source_object), result, &contacts, &error)) {
		report_error ("get contact finish", &error);
		stop_main_loop (1);
	} else {

		g_assert (g_slist_length (contacts) == 1);
		e_util_free_string_slist (contacts);

		stop_main_loop (0);
	}
}

gint
main (gint argc,
      gchar **argv)
{
	EBookClient *book_client;
	EContact *contact_final;
	GError *error = NULL;
	EBookQuery *query;
	gchar *sexp;
	GSList *contacts = NULL;

	main_initialize ();

	/*
	 * Setup
	 */
	book_client = new_temp_client (NULL);
	g_return_val_if_fail (book_client != NULL, 1);

	if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	/* Add contact */
	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact_final)) {
		g_object_unref (book_client);
		return 1;
	}
	g_object_unref (contact_final);

	/*
	 * Sync version
	 */
	query = e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "Foo Bar");
	sexp = e_book_query_to_string (query);

	if (!e_book_client_get_contacts_uids_sync (book_client, sexp, &contacts, NULL, &error)) {
		report_error ("get contacts uids", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_assert (g_slist_length (contacts) == 1);
	e_util_free_string_slist (contacts);

	/*
	 * Async version
	 */
	e_book_client_get_contacts_uids (book_client, sexp, NULL, contacts_ready_cb, NULL);

	e_book_query_unref (query);
	g_free (sexp);

	start_main_loop (NULL, NULL);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_object_unref (book_client);

	return get_main_loop_stop_result ();
}

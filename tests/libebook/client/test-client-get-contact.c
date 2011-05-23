/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book-client.h>

#include "client-test-utils.h"

static void
contact_ready_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	EContact *contact;
	GError *error = NULL;

	if (!e_book_client_get_contact_finish (E_BOOK_CLIENT (source_object), result, &contact, &error)) {
		report_error ("get contact finish", &error);
		stop_main_loop (1);
	} else {
		g_object_unref (contact);
		stop_main_loop (0);
	}
}

gint
main (gint argc, gchar **argv)
{
	EBookClient *book_client;
	EContact *contact_final;
	GError *error = NULL;

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

	/*
	 * Sync version
	 */
	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact_final)) {
		g_object_unref (book_client);
		return 1;
	}

	/*
	 * Async version
	 */
	e_book_client_get_contact (book_client, e_contact_get_const (contact_final, E_CONTACT_UID), NULL, contact_ready_cb, NULL);

	g_object_unref (contact_final);

	start_main_loop (NULL, NULL);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_object_unref (book_client);

	return get_main_loop_stop_result ();
}

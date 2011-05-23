/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book-client.h>

#include "client-test-utils.h"

static void
add_contact_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	GError *error = NULL;
	gchar *uid;

	if (!e_book_client_add_contact_finish (E_BOOK_CLIENT (source_object), result, &uid, &error)) {
		report_error ("add contact finish", &error);
		stop_main_loop (1);
		return;
	}

	printf ("Contact added as '%s'\n", uid);
	g_free (uid);
	stop_main_loop (0);
}

gint
main (gint argc, gchar **argv)
{
	EBookClient *book_client;
	GError *error = NULL;
	EContact *contact;
	gchar *vcard;

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
	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact)) {
		g_object_unref (book_client);
		return 1;
	}

	g_object_unref (contact);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_object_unref (book_client);

	/*
	 * Async version
	 */
	book_client = new_temp_client (NULL);
	g_return_val_if_fail (book_client != NULL, 1);

	if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	vcard = new_vcard_from_test_case ("simple-1");
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);

	e_book_client_add_contact (book_client, contact, NULL, add_contact_cb, NULL);
	g_object_unref (contact);

	start_main_loop (NULL, NULL);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_object_unref (book_client);

	return get_main_loop_stop_result ();
}

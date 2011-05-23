/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libebook/e-book-client.h>

#include "client-test-utils.h"

static void
remove_contact_cb (GObject *source_object, GAsyncResult *result, gpointer uid)
{
	GError *error = NULL;
	EContact *contact = NULL;

	if (!e_book_client_remove_contact_finish (E_BOOK_CLIENT (source_object), result, &error)) {
		report_error ("remove contact finish", &error);
		stop_main_loop (1);
		return;
	}

	if (!e_book_client_get_contact_sync (E_BOOK_CLIENT (source_object), uid, &contact, NULL, &error) &&
	    g_error_matches (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND)) {
		g_clear_error (&error);
		stop_main_loop (0);
	} else {
		report_error ("fail with get contact on removed contact", &error);
		if (contact)
			g_object_unref (contact);
		stop_main_loop (1);
	}
}

gint
main (gint argc, gchar **argv)
{
	EBookClient *book_client;
	GError *error = NULL;
	EContact *contact;
	gchar *uid;

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

	uid = e_contact_get (contact, E_CONTACT_UID);

	if (!e_book_client_remove_contact_sync (book_client, contact, NULL, &error)) {
		report_error ("remove contact sync", &error);
		g_object_unref (contact);
		g_object_unref (book_client);
		g_free (uid);
		return 1;
	}

	g_object_unref (contact);

	if (!e_book_client_get_contact_sync (book_client, uid, &contact, NULL, &error) &&
	    g_error_matches (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND)) {
		g_clear_error (&error);
	} else {
		report_error ("fail with get contact sync on removed contact", &error);
		g_object_unref (book_client);
		g_free (uid);
		return 1;
	}

	g_free (uid);

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

	contact = NULL;

	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact)) {
		g_object_unref (book_client);
		return 1;
	}

	uid = e_contact_get (contact, E_CONTACT_UID);
	e_book_client_remove_contact (book_client, contact, NULL, remove_contact_cb, uid);

	g_object_unref (contact);

	start_main_loop (NULL, NULL);

	g_free (uid);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_object_unref (book_client);

	return 0;
}

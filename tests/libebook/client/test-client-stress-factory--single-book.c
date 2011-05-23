/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book-client.h>

#include "client-test-utils.h"

#define NUM_OPENS 200

gint
main (gint argc, gchar **argv)
{
	gchar *uri = NULL;
	EBookClient *book_client;
	GError *error = NULL;
	gint ii;

	main_initialize ();

	book_client = new_temp_client (&uri);
	g_return_val_if_fail (book_client != NULL, 1);
	g_return_val_if_fail (uri != NULL, 1);

	g_object_unref (book_client);

	/* open and close the same book repeatedly */
	for (ii = 0; ii < NUM_OPENS; ii++) {
		book_client = e_book_client_new_from_uri (uri, &error);
		if (!book_client) {
			report_error ("new from uri", &error);
			break;
		}

		if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
			report_error ("client open sync", &error);
			g_object_unref (book_client);
			break;
		}

		g_object_unref (book_client);
	}

	book_client = e_book_client_new_from_uri (uri, &error);
	if (!book_client) {
		g_clear_error (&error);
	} else if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (book_client);
		g_free (uri);
		return 1;
	} else 	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);
		g_free (uri);
		return 1;
	}

	g_free (uri);
	g_object_unref (book_client);

	return ii == NUM_OPENS ? 0 : 1;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libebook/e-book-client.h>

#include "client-test-utils.h"

#define NUM_CLIENTS 200

gint
main (gint argc, gchar **argv)
{
	GError *error = NULL;
	gint ii;

	main_initialize ();

	/* Serially create, open, (close), and remove many books */
	for (ii = 0; ii < NUM_CLIENTS; ii++) {
		EBookClient *book_client = new_temp_client (NULL);
		g_return_val_if_fail (book_client != NULL, 1);

		if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
			report_error ("client open sync", &error);
			return 1;
		}

		if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
			report_error ("client remove sync", &error);
			g_object_unref (book_client);
			return 1;
		}

		g_object_unref (book_client);
	}

	return 0;
}

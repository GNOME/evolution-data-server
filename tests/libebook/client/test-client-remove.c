/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book-client.h>

#include "client-test-utils.h"

static void
client_removed_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	GError *error = NULL;

	if (!e_client_remove_finish (E_CLIENT (source_object), result, &error)) {
		report_error ("client remove finish", &error);
		stop_main_loop (1);
	} else {
		stop_main_loop (0);
	}
}

gint
main (gint argc, gchar **argv)
{
	EBookClient *book_client;
	GError *error = NULL;

	main_initialize ();

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

	e_client_remove (E_CLIENT (book_client), NULL, client_removed_cb, NULL);

	start_main_loop (NULL, NULL);

	g_object_unref (book_client);

	return get_main_loop_stop_result ();
}

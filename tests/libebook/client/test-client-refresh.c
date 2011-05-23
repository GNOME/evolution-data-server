/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <string.h>
#include <libebook/e-book-client.h>

#include "client-test-utils.h"

static gboolean
test_sync (EBookClient *book_client)
{
	GError *error = NULL;

	g_print ("Refresh supported: %s\n", e_client_check_refresh_supported (E_CLIENT (book_client)) ? "yes" : "no");

	if (!e_client_refresh_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("refresh sync", &error);
		return FALSE;
	}

	return TRUE;
}

/* asynchronous callback with a main-loop running */
static void
async_refresh_result_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	EBookClient *book_client;
	GError *error = NULL;

	book_client = E_BOOK_CLIENT (source_object);

	if (!e_client_refresh_finish (E_CLIENT (book_client), result, &error)) {
		report_error ("refresh finish", &error);
		stop_main_loop (1);
		return;
	}

	stop_main_loop (0);
}

/* synchronously in idle with main-loop running */
static gboolean
test_sync_in_idle (gpointer user_data)
{
	EBookClient *book_client = user_data;

	g_return_val_if_fail (book_client != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (book_client), FALSE);

	if (!test_sync (book_client)) {
		stop_main_loop (1);
		return FALSE;
	}

	g_print ("Refresh supported: %s\n", e_client_check_refresh_supported (E_CLIENT (book_client)) ? "yes" : "no");

	e_client_refresh (E_CLIENT (book_client), NULL, async_refresh_result_ready, NULL);

	return FALSE;
}

/* synchronously in a dedicated thread with main-loop running */
static gpointer
test_sync_in_thread (gpointer user_data)
{
	if (!test_sync (user_data)) {
		stop_main_loop (1);
		return NULL;
	}

	g_idle_add (test_sync_in_idle, user_data);

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	EBookClient *book_client;
	GError *error = NULL;

	main_initialize ();

	book_client = new_temp_client (NULL);
	g_return_val_if_fail (book_client != NULL, FALSE);

	if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	/* synchronously without main-loop */
	if (!test_sync (book_client)) {
		g_object_unref (book_client);
		return 1;
	}

	start_in_thread_with_main_loop (test_sync_in_thread, book_client);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_object_unref (book_client);

	if (get_main_loop_stop_result () == 0)
		g_print ("Test finished successfully.\n");

	return get_main_loop_stop_result ();
}

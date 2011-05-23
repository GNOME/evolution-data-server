/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book-client.h>
#include <libebook/e-book-query.h>

#include "client-test-utils.h"

static void
objects_added (EBookClientView *view, const GSList *contacts)
{
	const GSList *l;

	for (l = contacts; l; l = l->next) {
		print_email (l->data);
	}
}

static void
objects_removed (EBookClientView *view, const GSList *ids)
{
	const GSList *l;

	for (l = ids; l; l = l->next) {
		printf ("   Removed contact: %s\n", (gchar *) l->data);
	}
}

static void
complete (EBookClientView *view, const GError *error)
{
	e_book_client_view_stop (view, NULL);
	g_object_unref (view);

	stop_main_loop (0);
}

static void
setup_and_start_view (EBookClientView *view)
{
	GError *error = NULL;

	g_signal_connect (view, "objects-added", G_CALLBACK (objects_added), NULL);
	g_signal_connect (view, "objects-removed", G_CALLBACK (objects_removed), NULL);
	g_signal_connect (view, "complete", G_CALLBACK (complete), NULL);

	e_book_client_view_set_fields_of_interest (view, NULL, &error);
	if (error)
		report_error ("set fields of interest", &error);

	e_book_client_view_start (view, &error);
	if (error)
		report_error ("start view", &error);
}

static void
get_view_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	EBookClientView *view;
	GError *error = NULL;

	if (!e_book_client_get_view_finish (E_BOOK_CLIENT (source_object), result, &view, &error)) {
		report_error ("get view finish", &error);
		stop_main_loop (1);

		return;
	}

	setup_and_start_view (view);
}

static gboolean
setup_book (EBookClient **book_client)
{
	GError *error = NULL;

	g_return_val_if_fail (book_client != NULL, FALSE);

	*book_client = new_temp_client (NULL);
	g_return_val_if_fail (*book_client != NULL, FALSE);

	if (!e_client_open_sync (E_CLIENT (*book_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (*book_client);
		return FALSE;
	}

	if (!add_contact_from_test_case_verify (*book_client, "simple-1", NULL) ||
	    !add_contact_from_test_case_verify (*book_client, "simple-2", NULL) ||
	    !add_contact_from_test_case_verify (*book_client, "name-only", NULL)) {
		g_object_unref (*book_client);
		return FALSE;
	}

	return TRUE;
}

static gpointer
call_get_view (gpointer user_data)
{
	EBookQuery *query;
	EBookClient *book_client = user_data;
	gchar *sexp;

	g_return_val_if_fail (book_client != NULL, NULL);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (book_client), NULL);

	query = e_book_query_any_field_contains ("");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	e_book_client_get_view (book_client, sexp, NULL, get_view_cb, NULL);

	g_free (sexp);

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	EBookClient *book_client;
	EBookQuery *query;
	EBookClientView *view;
	gchar *sexp;
	GError *error = NULL;

	main_initialize ();

	/*
	 * Sync version
	 */
	if (!setup_book (&book_client))
		return 1;

	query = e_book_query_any_field_contains ("");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);
	if (!e_book_client_get_view_sync (book_client, sexp, &view, NULL, &error)) {
		report_error ("get book view sync", &error);
		g_free (sexp);
		g_object_unref (book_client);

		return 1;
	}

	g_free (sexp);

	setup_and_start_view (view);

	start_main_loop (NULL, NULL);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);

		return 1;
	}

	g_object_unref (book_client);

	/*
	 * Async version
	 */
	if (!setup_book (&book_client))
		return 1;

	start_in_idle_with_main_loop (call_get_view, book_client);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);

		return 1;
	}

	g_object_unref (book_client);

	return get_main_loop_stop_result ();
}

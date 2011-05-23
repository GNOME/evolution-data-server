/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book-client.h>
#include <libebook/e-book-query.h>

#include "client-test-utils.h"

#define NUM_VIEWS 200

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
	printf ("view_complete (status == %d, error_msg == %s%s%s)\n", error ? error->code : 0, error ? "'" : "", error ? error->message : "NULL", error ? "'" : "");
}

static gint
stress_book_views (EBookClient *book_client, gboolean in_thread)
{
	EBookQuery *query;
	EBookClientView *view = NULL;
	EBookClientView *new_view;
	gchar *sexp;
	gint i;

	g_return_val_if_fail (book_client != NULL, -1);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (book_client), -1);

	query = e_book_query_any_field_contains ("");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	for (i = 0; i < NUM_VIEWS; i++) {
		GError *error = NULL;

		if (!e_book_client_get_view_sync (book_client, sexp, &new_view, NULL, &error)) {
			report_error ("get book view sync", &error);
			g_object_unref (view);
			g_free (sexp);
			return 1;
		}

		g_signal_connect (new_view, "objects-added", G_CALLBACK (objects_added), NULL);
		g_signal_connect (new_view, "objects-removed", G_CALLBACK (objects_removed), NULL);
		g_signal_connect (new_view, "complete", G_CALLBACK (complete), NULL);

		e_book_client_view_start (new_view, NULL);

		if (view) {
			/* wait 100 ms when in a thread */
			if (in_thread)
				g_usleep (100000);

			e_book_client_view_stop (view, NULL);
			g_object_unref (view);
		}

		view = new_view;
	}

	e_book_client_view_stop (view, NULL);
	g_object_unref (view);

	g_free (sexp);

	return 0;
}

static gpointer
stress_book_views_thread (gpointer user_data)
{
	stop_main_loop (stress_book_views (user_data, TRUE));

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	EBookClient *book_client;
	GError *error = NULL;

	main_initialize ();

	printf ("loading addressbook\n");

	book_client = e_book_client_new_system (&error);
	if (!book_client) {
		report_error ("create local addressbook", &error);
		return 1;
	}

	if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
		g_object_unref (book_client);
		report_error ("open client sync", &error);
		return 1;
	}

	/* test from main thread */
	stress_book_views (book_client, FALSE);

	/* test from dedicated thread */
	start_in_thread_with_main_loop (stress_book_views_thread, book_client);

	g_object_unref (book_client);

	return get_main_loop_stop_result ();
}

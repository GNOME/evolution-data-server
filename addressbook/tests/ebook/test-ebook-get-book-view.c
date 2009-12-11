/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

static GMainLoop *loop;

static void
print_contact (EContact *contact)
{
	GList *emails, *e;

	g_print ("Contact: %s\n", (gchar *)e_contact_get_const (contact, E_CONTACT_FULL_NAME));
	g_print ("UID: %s\n", (gchar *)e_contact_get_const (contact, E_CONTACT_UID));
	g_print ("Email addresses:\n");

	emails = e_contact_get (contact, E_CONTACT_EMAIL);
	for (e = emails; e; e = e->next) {
		g_print ("\t%s\n",  (gchar *)e->data);
	}
	g_list_foreach (emails, (GFunc)g_free, NULL);
	g_list_free (emails);

	g_print ("\n");
}

static void
contacts_added (EBookView *book_view, const GList *contacts)
{
	GList *l;

	for (l = (GList*)contacts; l; l = l->next) {
		print_contact (l->data);
	}
}

static void
contacts_removed (EBookView *book_view, const GList *ids)
{
	GList *l;

	for (l = (GList*)ids; l; l = l->next) {
		g_print ("Removed contact: %s\n", (gchar *)l->data);
	}
}

static void
sequence_complete (EBookView *book_view, EBookViewStatus status)
{
	g_main_loop_quit (loop);
}

static void
setup_and_start_view (EBookView *view)
{
	g_signal_connect (view, "contacts_added", G_CALLBACK (contacts_added), NULL);   
	g_signal_connect (view, "contacts_removed", G_CALLBACK (contacts_removed), NULL);
	g_signal_connect (view, "sequence_complete", G_CALLBACK (sequence_complete), NULL);

	e_book_view_start (view);
}

static void
get_book_view_cb (EBookTestClosure *closure)
{
	g_assert (closure->view);

	setup_and_start_view (closure->view);
}

static void
setup_book (EBook     **book_out)
{
	EBook *book;

	book = ebook_test_utils_book_new_temp (NULL);
	ebook_test_utils_book_open (book, FALSE);

	ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-1", NULL);
	ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-2", NULL);
	ebook_test_utils_book_add_contact_from_test_case_verify (book, "name-only", NULL);

	*book_out = book;
}

gint
main (gint argc, gchar **argv)
{
	EBook *book;
	EBookQuery *query;
	EBookView *view;

	g_type_init ();

	/*
	 * Sync version
	 */
	setup_book (&book);
	query = e_book_query_any_field_contains ("");
	ebook_test_utils_book_get_book_view (book, query, &view);
	setup_and_start_view (view);

	g_print ("successfully set up the book view\n");

	loop = g_main_loop_new (NULL, TRUE);
	g_main_loop_run (loop);

        e_book_query_unref (query);
	ebook_test_utils_book_remove (book);

	/*
	 * Async version
	 */
	setup_book (&book);
	query = e_book_query_any_field_contains ("");

	loop = g_main_loop_new (NULL, TRUE);
	ebook_test_utils_book_async_get_book_view (book, query,
			(GSourceFunc) get_book_view_cb, loop);

	g_main_loop_run (loop);

        e_book_query_unref (query);
	ebook_test_utils_book_remove (book);

	return 0;
}

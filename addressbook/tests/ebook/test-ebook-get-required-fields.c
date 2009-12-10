/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

static void
list_member_print_and_free (char     *member,
		            gpointer  user_data)
{
	g_print ("    %s\n", member);
	g_free (member);
}

static void
get_required_fields_cb (EBookTestClosure *closure)
{
	/* XXX: assuming an empty list is valid, we'll just print out anything
	 * we do get */
	if (closure->list) {
		EIterator *iter;
		const char *field;

		g_print ("required fields:\n");
		iter = e_list_get_iterator (closure->list);
		while ((field = e_iterator_get (iter))) {
			g_print ("    %s\n", field);
			e_iterator_next (iter);
		}
		g_print ("----------------\n");
	}

	g_object_unref (closure->list);

        g_main_loop_quit ((GMainLoop*) (closure->user_data));
}

gint
main (gint argc, gchar **argv)
{
	EBook *book;
	GMainLoop *loop;
	GList *fields;

	g_type_init ();

	/*
	 * Setup
	 */
	book = ebook_test_utils_book_new_temp (NULL);
	ebook_test_utils_book_open (book, FALSE);

	/*
	 * Sync version
	 */
	fields = ebook_test_utils_book_get_required_fields (book);

	g_print ("successfully retrieved required fields:\n");
	g_list_foreach (fields, (GFunc) list_member_print_and_free, NULL);
	g_list_free (fields);

	/*
	 * Async version
	 */
	loop = g_main_loop_new (NULL, TRUE);
	ebook_test_utils_book_async_get_required_fields (book,
			(GSourceFunc) get_required_fields_cb, loop);

	g_main_loop_run (loop);

	ebook_test_utils_book_remove (book);

	return 0;
}

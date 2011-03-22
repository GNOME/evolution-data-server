/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

gint
main (gint argc, gchar **argv)
{
	EBook *book;
	gchar *uri = NULL;
	GMainLoop *loop;

	g_type_init ();

	/* Sync version */
	book = ebook_test_utils_book_new_temp (&uri);
	ebook_test_utils_book_open (book, FALSE);
	ebook_test_utils_book_remove (book);

	/* Async version */
	book = ebook_test_utils_book_new_temp (&uri);
	ebook_test_utils_book_open (book, FALSE);

	loop = g_main_loop_new (NULL, TRUE);
	ebook_test_utils_book_async_remove (book,
			ebook_test_utils_callback_quit, loop);

	g_main_loop_run (loop);

	return 0;
}

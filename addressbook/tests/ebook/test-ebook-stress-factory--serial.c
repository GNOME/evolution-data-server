/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libebook/e-book.h>

#include "ebook-test-utils.h"

#define NUM_BOOKS 200

gint
main (gint argc, gchar **argv)
{
	gchar *uri = NULL;
	gint i;

	g_type_init ();

	/* Serially create, open, (close), and remove many books */
	for (i = 0; i < NUM_BOOKS; i++) {
		EBook *book;

		book = ebook_test_utils_book_new_temp (&uri);
		ebook_test_utils_book_open (book, FALSE);
		ebook_test_utils_book_remove (book);

		g_free (uri);
	}

	return 0;
}

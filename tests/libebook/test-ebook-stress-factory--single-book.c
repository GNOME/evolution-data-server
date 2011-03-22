/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

#define NUM_OPENS 200

gint
main (gint argc, gchar **argv)
{
	gchar *uri = NULL;
	EBook *book;
	gint i;

	g_type_init ();

	book = ebook_test_utils_book_new_temp (&uri);
	g_object_unref (book);

	/* open and close the same book repeatedly */
	for (i = 0; i < NUM_OPENS-1; i++) {
		book = ebook_test_utils_book_new_from_uri (uri);
		ebook_test_utils_book_open (book, FALSE);
		g_object_unref (book);
	}

	book = ebook_test_utils_book_new_from_uri (uri);
	ebook_test_utils_book_remove (book);

	g_free (uri);

	return 0;
}

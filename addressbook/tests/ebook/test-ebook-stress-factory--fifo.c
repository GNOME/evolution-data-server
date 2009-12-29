/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

#define NUM_BOOKS 200

gint
main (gint argc, gchar **argv)
{
	char *uri = NULL;
	EBook *books[NUM_BOOKS];
	gint i;

	g_type_init ();

	/* Create and open many books; then remove each of them */

	for (i = 0; i < NUM_BOOKS; i++) {
		books[i] = ebook_test_utils_book_new_temp (&uri);
		ebook_test_utils_book_open (books[i], FALSE);

		g_free (uri);
	}

	for (i = 0; i < NUM_BOOKS; i++) {
		ebook_test_utils_book_remove (books[i]);
	}

	return 0;
}

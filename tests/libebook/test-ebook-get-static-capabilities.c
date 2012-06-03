/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "ebook-test-utils.h"

gint
main (gint argc,
      gchar **argv)
{
	EBook *book;
	const gchar *caps;

	g_type_init ();

	/*
	 * Setup
	 */
	book = ebook_test_utils_book_new_temp (NULL);
	ebook_test_utils_book_open (book, FALSE);

	/*
	 * Sync version
	 */
	caps = ebook_test_utils_book_get_static_capabilities (book);

	test_print ("successfully retrieved static capabilities: '%s'\n", caps);

	return 0;
}

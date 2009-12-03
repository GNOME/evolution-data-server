/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

EBook*
ebook_test_utils_create_temp_addressbook (char **uri)
{
        EBook *book;
	GError *error = NULL;
	gchar *file_template;

	file_template = g_build_filename (g_get_tmp_dir (),
					  "ebook-test-XXXXXX/",
					  NULL);
	g_mkstemp (file_template);

	*uri = g_filename_to_uri (file_template, NULL, &error);
	if (!*uri) {
		printf ("failed to convert %s to an URI: %s\n",
			file_template, error->message);
		exit (1);
	}
	g_free (file_template);

	/* create a temp addressbook in /tmp */
	printf ("loading addressbook\n");
	book = e_book_new_from_uri (*uri, &error);
	if (!book) {
		printf ("failed to create addressbook: `%s': %s\n",
			*uri, error->message);
		exit(1);
	}

        return book;
}

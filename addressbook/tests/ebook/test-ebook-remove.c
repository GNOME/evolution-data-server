/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

static GMainLoop *loop;

static void
remove_cb (EBook *book, EBookStatus status, gpointer closure)
{
        if (status != E_BOOK_ERROR_OK) {
                printf ("failed to asynchronously remove the book: status %d\n",
                                status);
                exit (1);
        }

        printf ("successfully asynchronously removed the temporary addressbook\n");
        g_main_loop_quit (loop);
}

gint
main (gint argc, gchar **argv)
{
	EBook *book;
        char *uri = NULL;
	GError *error = NULL;

	g_type_init ();

        /* Sync version */
        book = ebook_test_utils_create_temp_addressbook (&uri);

	if (!e_book_open (book, FALSE, &error)) {
		printf ("failed to open addressbook: `%s': %s\n",
			uri, error->message);
		exit(1);
	}

	if (!e_book_remove (book, &error)) {
		printf ("failed to remove book; %s\n", error->message);
		exit(1);
	}
	printf ("successfully removed the temporary addressbook\n");

	g_object_unref (book);

        /* Async version */
        book = ebook_test_utils_create_temp_addressbook (&uri);

	if (!e_book_open (book, FALSE, &error)) {
		printf ("failed to open addressbook: `%s': %s\n",
			uri, error->message);
		exit(1);
	}

	if (e_book_async_remove (book, remove_cb, NULL)) {
		printf ("failed to set up book removal\n");
		exit(1);
	}

        loop = g_main_loop_new (NULL, TRUE);
        g_main_loop_run (loop);

	return 0;
}

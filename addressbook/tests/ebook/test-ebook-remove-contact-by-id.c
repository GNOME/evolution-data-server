/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libebook/e-book.h>

#include "ebook-test-utils.h"

gint
main (gint argc, gchar **argv)
{
        EBook *book;
        GMainLoop *loop;
        gchar *uid;

        g_type_init ();

        /*
         * Async version
         */
        book = ebook_test_utils_book_new_temp (NULL);
        ebook_test_utils_book_open (book, FALSE);

	uid = ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-1", NULL);

        loop = g_main_loop_new (NULL, TRUE);
        ebook_test_utils_book_async_remove_contact_by_id (book, uid,
			ebook_test_utils_callback_quit, loop);

        g_main_loop_run (loop);

        ebook_test_utils_book_remove (book);
	g_free (uid);

        return 0;
}

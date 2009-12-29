/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libebook/e-book.h>

#include "ebook-test-utils.h"

gint
main (gint argc, gchar **argv)
{
        EBook *book;
        GMainLoop *loop;
        EContact *contact_final;
        char *uid;

        g_type_init ();

        /*
         * Setup
         */
        book = ebook_test_utils_book_new_temp (NULL);
        ebook_test_utils_book_open (book, FALSE);

        /*
         * Sync version
         */
	uid = ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-1", NULL);
        ebook_test_utils_book_remove_contact (book, uid);
	contact_final = NULL;
	e_book_get_contact (book, uid, &contact_final, NULL);

        g_assert (contact_final == NULL);
        test_print ("successfully added and removed contact '%s'\n", uid);

        ebook_test_utils_book_remove (book);
        g_free (uid);

        /*
         * Async version
         */
        book = ebook_test_utils_book_new_temp (NULL);
        ebook_test_utils_book_open (book, FALSE);

	contact_final = NULL;
	/* contact_final has 2 refs by the end of this */
	uid = ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-1", &contact_final);

        loop = g_main_loop_new (NULL, TRUE);
	/* contact_final is unref'd by e_book_remove_contact() here */
        ebook_test_utils_book_async_remove_contact (book, contact_final,
			ebook_test_utils_callback_quit, loop);


        g_main_loop_run (loop);

        ebook_test_utils_book_remove (book);
	g_object_unref (contact_final);
	g_free (uid);

        return 0;
}

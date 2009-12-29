/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libebook/e-book.h>

#include "ebook-test-utils.h"

gint 
main (gint argc, gchar **argv)
{       
        EBook *book;
        GMainLoop *loop;
        EContact *contact_final;
        char *uid_1, *uid_2; 
	GList *uids = NULL;
        
        g_type_init ();
        
        /*
         * Setup
         */
        book = ebook_test_utils_book_new_temp (NULL);
        ebook_test_utils_book_open (book, FALSE);

        /*
         * Sync version
         */
	uid_1 = ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-1", NULL);
	uid_2 = ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-2", NULL);
	uids = g_list_prepend (uids, uid_1);
	uids = g_list_prepend (uids, uid_2);
        ebook_test_utils_book_remove_contacts (book, uids);

	contact_final = NULL;
	e_book_get_contact (book, uid_1, &contact_final, NULL);
        g_assert (contact_final == NULL);

	e_book_get_contact (book, uid_2, &contact_final, NULL);
        g_assert (contact_final == NULL);

        g_print ("successfully added and removed contacts\n");

        ebook_test_utils_book_remove (book);
        g_free (uid_1);
        g_free (uid_2);
	g_list_free (uids);
        
        /*
         * Async version
         */
        book = ebook_test_utils_book_new_temp (NULL);
        ebook_test_utils_book_open (book, FALSE);

	uid_1 = ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-1", NULL);
	uid_2 = ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-2", NULL);
	uids = NULL;
	uids = g_list_prepend (uids, uid_1);
	uids = g_list_prepend (uids, uid_2);

        loop = g_main_loop_new (NULL, TRUE);
        ebook_test_utils_book_async_remove_contacts (book, uids,
			ebook_test_utils_callback_quit, loop);
        
        g_main_loop_run (loop);

        ebook_test_utils_book_remove (book);
        g_free (uid_1);
        g_free (uid_2);
	g_list_free (uids);

        return 0;
}

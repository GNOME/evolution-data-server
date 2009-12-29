/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

gint
main (gint argc, gchar **argv)
{
	EBook *book;
	GMainLoop *loop;
	char *vcard;
	EContact *contact;
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
	uid = ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-1", &contact_final);

	g_print ("successfully added and retrieved contact '%s'\n", uid);
	g_object_unref (contact_final);

	ebook_test_utils_book_remove (book);

	/*
	 * Async version
	 */
	book = ebook_test_utils_book_new_temp (NULL);
	ebook_test_utils_book_open (book, FALSE);
	vcard = ebook_test_utils_new_vcard_from_test_case ("simple-1");
	contact = e_contact_new_from_vcard (vcard);

	loop = g_main_loop_new (NULL, TRUE);
	ebook_test_utils_book_async_add_contact (book, contact,
			ebook_test_utils_callback_quit, loop);

	g_free (uid);
	g_main_loop_run (loop);

	ebook_test_utils_book_remove (book);
	g_free (vcard);

	return 0;
}

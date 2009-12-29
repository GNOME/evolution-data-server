/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

gint
main (gint argc, gchar **argv)
{
	EBook *book;
	GMainLoop *loop;
	EContact *contact;
	EContact *contact_final;
	const char *uid;
	const char *contact_final_uid;

	g_type_init ();

	/*
	 * Setup
	 */
	book = ebook_test_utils_book_new_temp (NULL);
	ebook_test_utils_book_open (book, FALSE);

	contact = e_contact_new_from_vcard (EBOOK_TEST_UTILS_VCARD_SIMPLE);
	uid = ebook_test_utils_book_add_contact (book, contact);

	/*
	 * Sync version
	 */
	contact_final = ebook_test_utils_book_get_contact (book, uid);
	contact_final_uid = e_contact_get_const (contact_final, E_CONTACT_UID);

	/* This is not a thorough comparison (which is difficult, assuming we
	 * give the back-ends leniency in implementation), since that's better
	 * suited to more advanced tests */
	if (g_strcmp0 (uid, contact_final_uid)) {
		const char *uri;

		uri = e_book_get_uri (book);

		g_warning ("retrieved contact uid '%s' does not match added "
				"contact uid '%s'", contact_final_uid, uid);
		exit(1);
	}

	g_print ("successfully added and retrieved contact '%s'\n", uid);
	g_object_unref (contact);
	g_object_unref (contact_final);

	/*
	 * Async version
	 */
	loop = g_main_loop_new (NULL, TRUE);
	ebook_test_utils_book_async_get_contact (book, uid,
			(GSourceFunc) g_main_loop_quit, loop);
	g_main_loop_run (loop);

	ebook_test_utils_book_remove (book);

	return 0;
}

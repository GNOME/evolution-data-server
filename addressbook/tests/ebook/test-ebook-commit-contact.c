/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

#define EMAIL_ADD "foo@bar.com"

static EBook *book;
static char *uid;

static void
verify_precommit_and_prepare_contact (EContact *contact)
{
	EVCardAttribute *attr;

	/* ensure there is no email address to begin with, then add one */
	g_assert (!e_vcard_get_attribute (E_VCARD (contact), EVC_EMAIL));
	attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
	e_vcard_add_attribute_with_value (E_VCARD (contact), attr, EMAIL_ADD);
}

static void
verify_commit (EContact *contact)
{
	EVCardAttribute *attr;
	char *email_value;

	g_assert ((attr = e_vcard_get_attribute (E_VCARD (contact), EVC_EMAIL)));
	g_assert (e_vcard_attribute_is_single_valued (attr));
	email_value = e_vcard_attribute_get_value (attr);
	g_assert (!g_strcmp0 (email_value, EMAIL_ADD));
}

static gboolean
commit_verify_cb (EBookTestClosure *closure)
{
	EContact *contact;

	contact = ebook_test_utils_book_get_contact (book, uid);
	verify_commit (contact);

	g_main_loop_quit ((GMainLoop*) (closure->user_data));

	return FALSE;
}

gint
main (gint argc, gchar **argv)
{
	GMainLoop *loop;
	EContact *contact;

	g_type_init ();

	/*
	 * Setup
	 */
	book = ebook_test_utils_book_new_temp (NULL);
	ebook_test_utils_book_open (book, FALSE);

	/*
	 * Sync version
	 */
	uid = ebook_test_utils_book_add_contact_from_test_case_verify (book, "name-only", &contact);
	verify_precommit_and_prepare_contact (contact);
	ebook_test_utils_book_commit_contact (book, contact);

	verify_commit (contact);

	test_print ("successfully committed changes to contact contact '%s'\n", uid);
	g_object_unref (contact);
	g_free (uid);

	ebook_test_utils_book_remove (book);

	/*
	 * Async version
	 */
	book = ebook_test_utils_book_new_temp (NULL);
	ebook_test_utils_book_open (book, FALSE);
	uid = ebook_test_utils_book_add_contact_from_test_case_verify (book, "name-only", &contact);

	verify_precommit_and_prepare_contact (contact);

	loop = g_main_loop_new (NULL, TRUE);
	ebook_test_utils_book_async_commit_contact (book, contact,
			(GSourceFunc) commit_verify_cb, loop);

	g_main_loop_run (loop);

	g_free (uid);
	ebook_test_utils_book_remove (book);

	return 0;
}

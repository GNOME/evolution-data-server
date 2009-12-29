/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

#define NEW_VCARD "BEGIN:VCARD\n\
X-EVOLUTION-FILE-AS:Toshok, Chris\n\
FN:Chris Toshok\n\
EMAIL;INTERNET:toshok@ximian.com\n\
ORG:Ximian, Inc.;\n\
END:VCARD"

gint
main (gint argc, gchar **argv)
{
	EBook *book;
	EContact *contact;
	GList *changes;
	GError *error = NULL;
	EBookChange *change;
	gchar *uri;

	g_type_init ();

	book = ebook_test_utils_book_new_temp (&uri);
        ebook_test_utils_book_open (book, FALSE);

	/* get an initial change set */
	if (!e_book_get_changes (book, "changeidtest", &changes, &error)) {
		printf ("failed to get changes: %s\n", error->message);
		exit(0);
	}

	/* make a change to the book */
	contact = e_contact_new_from_vcard (NEW_VCARD);
	ebook_test_utils_book_add_contact (book, contact);

	/* get another change set */
	if (!e_book_get_changes (book, "changeidtest", &changes, &error)) {
		printf ("failed to get second set of changes: %s\n", error->message);
		exit(0);
	}

	/* make sure that 1 change has occurred */
	if (g_list_length (changes) != 1) {
		printf ("got back %d changes, was expecting 1\n", g_list_length (changes));
		exit(0);
	}

	change = changes->data;
	if (change->change_type != E_BOOK_CHANGE_CARD_ADDED) {
		printf ("was expecting a CARD_ADDED change, but didn't get it.\n");
		exit(0);
	}

	printf ("got changed vcard back: %s\n", (gchar *)e_contact_get_const (change->contact, E_CONTACT_UID));

	e_book_free_change_list (changes);

	g_object_unref (contact);
	ebook_test_utils_book_remove (book);

	return 0;
}

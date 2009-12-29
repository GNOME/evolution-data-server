/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

static void
print_email (EContact *contact)
{
	const gchar *file_as = e_contact_get_const (contact, E_CONTACT_FILE_AS);
	const gchar *name_or_org = e_contact_get_const (contact, E_CONTACT_NAME_OR_ORG);
	GList *emails, *e;

	printf ("Contact: %s\n", file_as);
	printf ("Name or org: %s\n", name_or_org);
	printf ("Email addresses:\n");
	emails = e_contact_get (contact, E_CONTACT_EMAIL);
	for (e = emails; e; e = e->next) {
		printf ("\t%s\n",  (gchar *)e->data);
	}
	g_list_foreach (emails, (GFunc)g_free, NULL);
	g_list_free (emails);

	printf ("\n");
}

static void
print_all_emails (EBook *book)
{
	EBookQuery *query;
	gboolean status;
	GList *cards, *c;

	query = e_book_query_field_exists (E_CONTACT_FULL_NAME);

	status = e_book_get_contacts (book, query, &cards, NULL);

	e_book_query_unref (query);

	if (status == FALSE) {
		printf ("error %d getting card list\n", status);
		exit(0);
	}

	for (c = cards; c; c = c->next) {
		EContact *contact = E_CONTACT (c->data);

		print_email (contact);

		g_object_unref (contact);
	}
	g_list_free (cards);
}

static void
print_one_email (EBook *book)
{
	EContact *contact;
	GError *error = NULL;

	if (!e_book_get_contact (book, "pas-id-0002023", &contact, &error)) {
		printf ("error %d getting card: %s\n", error->code, error->message);
		g_clear_error (&error);
		return;
	}

	print_email (contact);

	g_object_unref (contact);
}

gint
main (gint argc, gchar **argv)
{
	EBook *book;

	g_type_init ();

	/*
	** the actual ebook foo
	*/

	printf ("loading addressbook\n");
	book = e_book_new_system_addressbook (NULL);
	if (!book) {
		printf ("failed to create local addressbook\n");
		exit(0);
	}

        ebook_test_utils_book_open (book, FALSE);

	printf ("printing one contact\n");
	print_one_email (book);

	printf ("printing all contacts\n");
	print_all_emails (book);

	g_object_unref (book);

	return 0;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libgnome/gnome-init.h>
#include <bonobo/bonobo-main.h>
#include <stdlib.h>
#include <libebook/e-book.h>

static void
print_email (EContact *contact)
{
	char *file_as = e_contact_get_const (contact, E_CONTACT_FILE_AS);
	char *name_or_org = e_contact_get_const (contact, E_CONTACT_NAME_OR_ORG);
	GList *emails, *e;

	printf ("Contact: %s\n", file_as);
	printf ("Name or org: %s\n", name_or_org);
	printf ("Email addresses:\n");
	emails = e_contact_get (contact, E_CONTACT_EMAIL);
	for (e = emails; e; e = e->next) {
		printf ("\t%s\n",  (char*)e->data);
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

int
main (int argc, char **argv)
{
	EBook *book;
	gboolean status;

	gnome_program_init("test-ebook", "0.0", LIBGNOME_MODULE, argc, argv, NULL);

	if (bonobo_init (&argc, argv) == FALSE)
		g_error ("Could not initialize Bonobo");

	/*
	** the actual ebook foo
	*/

	printf ("loading addressbook\n");
	book = e_book_new_system_addressbook (NULL);
	if (!book) {
		printf ("failed to create local addressbook\n");
		exit(0);
	}

	status = e_book_open (book, FALSE, NULL);
	if (status == FALSE) {
		printf ("failed to open local addressbook\n");
		exit(0);
	}

	printf ("printing one contact\n");
	print_one_email (book);

	printf ("printing all contacts\n");
	print_all_emails (book);

	g_object_unref (book);

	return 0;
}

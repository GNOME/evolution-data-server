/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libgnome/gnome-init.h>
#include <bonobo/bonobo-main.h>
#include <stdlib.h>
#include <libebook/e-book-async.h>

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
print_all_emails_cb (EBook *book, EBookStatus status, GList *contacts, gpointer closure)
{
	GList *c;

	if (status == E_BOOK_ERROR_OK) {
		for (c = contacts; c; c = c->next) {
			EContact *contact = E_CONTACT (c->data);

			print_email (contact);
		}
	}

	bonobo_main_quit ();
}

static void
print_all_emails (EBook *book)
{
	EBookQuery *query;

	query = e_book_query_field_exists (E_CONTACT_FULL_NAME);

	e_book_async_get_contacts (book, query, print_all_emails_cb, NULL);

	e_book_query_unref (query);
}

static void
print_email_cb (EBook *book, EBookStatus status, EContact *contact, gpointer closure)
{
	if (status == E_BOOK_ERROR_OK)
		print_email (contact);

	printf ("printing all contacts\n");
	print_all_emails (book);
}

static void
print_one_email (EBook *book)
{
	e_book_async_get_contact (book, "pas-id-0002023", print_email_cb, NULL);
}

static void
book_loaded_cb (EBook *book, EBookStatus status, gpointer data)
{
	if (status != E_BOOK_ERROR_OK)
		return;

	printf ("printing one contact\n");
	print_one_email (book);
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

	book = e_book_new ();

	printf ("loading addressbook\n");
	e_book_async_load_local_addressbook (book, book_loaded_cb, book);

	bonobo_main();

	return 0;
}

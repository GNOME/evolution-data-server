/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

gint
main (gint argc,
      gchar **argv)
{
#if 0  /* ACCOUNT_MGMT */
	EBook *book;
	gboolean status;
	EBookQuery *query;
	GList *c, *contacts;

	g_type_init ();

	if (argc < 3) {
		printf ("usage: test-search <addressbook uri> <query>\n");
		exit (0);
	}

	query = e_book_query_from_string (argv[2]);
	if (!query) {
		printf ("failed to parse query string '%s'\n", argv[2]);
		exit (0);
	}

	book = e_book_new_system_addressbook (NULL);
	if (!book) {
		printf ("failed to create ebook\n");
		exit (0);
	}

	status = e_book_open (book, TRUE, NULL);
	if (status == FALSE) {
		printf ("failed to open addressbook\n");
		exit (0);
	}

	status = e_book_get_contacts (book, query, &contacts, NULL);
	if (status == FALSE) {
		printf ("failed to get contacts\n");
		exit (0);
	}

	for (c = contacts; c; c = c->next) {
		EContact *contact = E_CONTACT (c->data);

		printf ("%s\n", e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30));

		g_object_unref (contact);
	}

	g_list_free (contacts);

	g_object_unref (book);
#endif /* ACCOUNT_MGMT */

	return 0;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book-client.h>
#include <libebook/e-book-query.h>

#include "client-test-utils.h"

static void
print_all_emails (EBookClient *book)
{
	GError *error = NULL;
	EBookQuery *query;
	gchar *sexp;
	gboolean result;
	GSList *cards, *c;

	query = e_book_query_field_exists (E_CONTACT_FULL_NAME);
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	result = e_book_client_get_contacts_sync (book, sexp, &cards, NULL, &error);

	g_free (sexp);

	if (!result) {
		fprintf (stderr, "Error getting card list: %s\n", error ? error->message : "Unknown error");
		if (error)
			g_error_free (error);
		exit (1);
	}

	for (c = cards; c; c = c->next) {
		EContact *contact = E_CONTACT (c->data);

		print_email (contact);

		g_object_unref (contact);
	}

	g_slist_free (cards);
}

static void
print_one_email (EBookClient *book_client)
{
	EContact *contact;
	GError *error = NULL;

	if (!e_book_client_get_contact_sync (book_client, "pas-id-0002023", &contact, NULL, &error)) {
		report_error ("get_contact_sync", &error);
		return;
	}

	print_email (contact);

	g_object_unref (contact);
}

gint
main (gint argc, gchar **argv)
{
	EBookClient *book_client;

	main_initialize ();

	printf ("loading addressbook\n");

	book_client = open_system_book (FALSE);
	if (!book_client)
		return 1;

	printf ("printing one contact\n");
	print_one_email (book_client);

	printf ("printing all contacts\n");
	print_all_emails (book_client);

	g_object_unref (book_client);

	return 0;
}

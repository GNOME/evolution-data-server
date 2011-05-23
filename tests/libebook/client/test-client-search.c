/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book-client.h>
#include <libebook/e-book-query.h>

#include "client-test-utils.h"

gint
main (gint argc, gchar **argv)
{
	EBookClient *book_client;
	const gchar *query_string;
	EBookQuery *query;
	gchar *sexp;
	GSList *c, *contacts;
	GError *error = NULL;

	main_initialize ();

	if (argc != 2) {
		query_string = "contains \"full_name\" \"a\"";
		printf ("usage: test-search <query>\n");
		printf ("   using default query \"%s\"\n", query_string);
	} else {
		query_string = argv[1];
	}

	query = e_book_query_from_string (query_string);
	if (!query) {
		fprintf (stderr, " * Failed to parse query string '%s'\n", query_string);
		return 1;
	}

	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	book_client = open_system_book (FALSE);
	if (!book_client) {
		g_free (sexp);
		return 1;
	}

	if (!e_book_client_get_contacts_sync (book_client, sexp, &contacts, NULL, &error)) {
		report_error ("get contacts sync", &error);
		g_free (sexp);
		g_object_unref (book_client);
		return 1;
	}

	for (c = contacts; c; c = c->next) {
		EContact *contact = E_CONTACT (c->data);
		gchar *vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

		printf ("%s\n\n", vcard);

		g_free (vcard);
	}

	g_slist_foreach (contacts, (GFunc) g_object_unref, NULL);
	g_slist_free (contacts);

	g_free (sexp);
	g_object_unref (book_client);

	return 0;
}

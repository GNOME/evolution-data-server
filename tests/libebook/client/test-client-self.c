/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book-client.h>

#include "client-test-utils.h"

gint
main (gint argc, gchar **argv)
{
	EBookClient *book_client = NULL;
	EContact *contact = NULL;
	GError *error = NULL;
	gchar *vcard;

	main_initialize ();

	printf ("getting the self contact\n");

	if (!e_book_client_get_self (&contact, &book_client, &error)) {
		report_error ("get self", &error);
		return 1;
	}

	if (!contact) {
		fprintf (stderr, " * Self contact not set\n");
		if (book_client)
			g_object_unref (book_client);
		return 0;
	}

	if (!book_client) {
		fprintf (stderr, " * Book client for a self contact not returned\n");
		g_object_unref (contact);
		return 1;
	}

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	printf ("self contact = \n%s\n", vcard);
	g_free (vcard);

	g_object_unref (contact);
	g_object_unref (book_client);

	return 0;
}

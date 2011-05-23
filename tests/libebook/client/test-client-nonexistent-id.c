/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libebook/e-book-client.h>

#include "client-test-utils.h"

gint
main (gint argc, gchar **argv)
{
	GError *error = NULL;
	EBookClient *book_client = NULL;

	main_initialize ();

	printf ("loading addressbook\n");

	book_client = open_system_book (FALSE);
	if (!book_client)
		return 1;

	printf ("removing nonexistent contact\n");
	if (!e_book_client_remove_contact_by_uid_sync (book_client, "ii", NULL, &error)) {
		if (!g_error_matches (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND)) {
			report_error ("remove contact sync", &error);
			g_object_unref (book_client);
			return 1;
		}

		printf ("\tOK, ended with expected Not Found error\n");
		g_error_free (error);
	} else if (error) {
		report_error ("remove contact sync returned error, but success", &error);
		g_object_unref (book_client);
		return 1;
	} else {
		report_error ("remove contact sync returned success, but should return error", NULL);
		g_object_unref (book_client);
		return 1;
	}

	g_object_unref (book_client);

	return 0;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <string.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"

#define CYCLES 10

static void
get_revision_compare_cycle (EBookClient *client)
{
       gchar    *revision_before = NULL, *revision_after = NULL;
       EContact *contact = NULL;
       GError   *error = NULL;

       if (!e_client_get_backend_property_sync (E_CLIENT (client), CLIENT_BACKEND_PROPERTY_REVISION, &revision_before, NULL, &error))
	       g_error ("Error getting book revision: %s", error->message);

	if (!add_contact_from_test_case_verify (client, "simple-1", &contact)) {
		g_object_unref (client);
		exit (1);
	}

	if (!e_book_client_remove_contact_sync (client, contact, NULL, &error))
		g_error ("Unable to remove contact: %s", error->message);

	g_object_unref (contact);

       if (!e_client_get_backend_property_sync (E_CLIENT (client), CLIENT_BACKEND_PROPERTY_REVISION, &revision_after, NULL, &error))
	       g_error ("Error getting book revision: %s", error->message);

       g_assert (revision_before);
       g_assert (revision_after);
       g_assert (strcmp (revision_before, revision_after) != 0);

       g_message ("Passed cycle, revision before '%s' revision after '%s'",
		  revision_before, revision_after);

       g_free (revision_before);
       g_free (revision_after);
}

gint
main (gint argc,
      gchar **argv)
{
	EBookClient *book_client;
	GError      *error = NULL;
	gint         i;

	g_type_init ();

	/*
	 * Setup
	 */
	book_client = new_temp_client (NULL);

	g_return_val_if_fail (book_client != NULL, 1);

	if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	/* Test that modifications make the revisions increment */
	for (i = 0; i < CYCLES; i++)
		get_revision_compare_cycle (book_client);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_object_unref (book_client);

	return 0;
}

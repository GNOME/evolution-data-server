/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"

static void
print_all_uids (EBookClient *book)
{
	GError *error = NULL;
	EBookQuery *query;
	gchar *sexp;
	gboolean result;
	GSList *uids, *u;

	query = e_book_query_field_exists (E_CONTACT_FULL_NAME);
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	result = e_book_client_get_contacts_uids_sync (book, sexp, &uids, NULL, &error);

	g_free (sexp);

	if (!result) {
		fprintf (stderr, "Error getting uid list: %s\n", error ? error->message : "Unknown error");
		if (error)
			g_error_free (error);
		exit (1);
	}

	for (u = uids; u; u = u->next) {
		const gchar *uid = u->data;

		g_print ("   uid:'%s'\n", uid);
	}

	g_slist_foreach (uids, (GFunc) g_free, NULL);
	g_slist_free (uids);
}

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
main (gint argc,
      gchar **argv)
{
	EBookClient *book_client;
	ESourceRegistry *registry;
	GError *error = NULL;

	main_initialize ();

	registry = e_source_registry_new_sync (NULL, &error);
	if (error != NULL)
		g_error ("%s", error->message);

	printf ("loading addressbook\n");

	book_client = open_system_book (registry, FALSE);
	if (!book_client)
		return 1;

	printf ("printing one contact\n");
	print_one_email (book_client);

	printf ("printing all contacts\n");
	print_all_emails (book_client);

	printf ("printing all uids\n");
	print_all_uids (book_client);

	g_object_unref (book_client);

	return 0;
}

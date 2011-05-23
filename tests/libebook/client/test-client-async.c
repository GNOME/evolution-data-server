/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book-client.h>
#include <libebook/e-book-query.h>

#include "client-test-utils.h"

static void
print_all_emails_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	EBookClient *book_client;
	GSList *contacts = NULL, *c;
	GError *error = NULL;

	book_client = E_BOOK_CLIENT (source_object);
	g_return_if_fail (book_client != NULL);

	if (!e_book_client_get_contacts_finish (book_client, result, &contacts, &error)) {
		report_error ("get contacts finish", &error);
		stop_main_loop (1);
		return;
	}

	for (c = contacts; c; c = c->next) {
		EContact *contact = E_CONTACT (c->data);

		print_email (contact);
	}

	g_slist_foreach (contacts, (GFunc) g_object_unref, NULL);
	g_slist_free (contacts);

	stop_main_loop (0);
}

static void
print_all_emails (EBookClient *book_client)
{
	EBookQuery *query;
	gchar *sexp;

	query = e_book_query_field_exists (E_CONTACT_FULL_NAME);
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	e_book_client_get_contacts (book_client, sexp, NULL, print_all_emails_cb, NULL);

	g_free (sexp);
}

static void
print_email_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	EBookClient *book_client;
	EContact *contact = NULL;
	GError *error = NULL;

	book_client = E_BOOK_CLIENT (source_object);
	g_return_if_fail (book_client != NULL);

	if (!e_book_client_get_contact_finish (book_client, result, &contact, &error)) {
		report_error ("get contact finish", &error);
	} else {
		print_email (contact);
		g_object_unref (contact);
	}

	printf ("printing all contacts\n");
	print_all_emails (book_client);
}

static void
print_one_email (EBookClient *book_client)
{
	e_book_client_get_contact (book_client, "pas-id-0002023", NULL, print_email_cb, NULL);
}

static void
client_loaded_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	EBookClient *book_client;
	GError *error = NULL;

	book_client = E_BOOK_CLIENT (source_object);
	g_return_if_fail (book_client != NULL);

	if (!e_client_open_finish (E_CLIENT (book_client), result, &error)) {
		report_error ("client open finish", &error);
		stop_main_loop (1);
		return;
	}

	printf ("printing one contact\n");
	print_one_email (book_client);
}

gint
main (gint argc, gchar **argv)
{
	EBookClient *book_client;
	GError *error = NULL;

	main_initialize ();

	book_client = e_book_client_new_system (&error);
	if (error) {
		report_error ("create system addressbook", &error);
		return 1;
	}

	printf ("loading addressbook\n");

	e_client_open (E_CLIENT (book_client), FALSE, NULL, client_loaded_cb, NULL);

	start_main_loop (NULL, NULL);

	g_object_unref (book_client);

	return get_main_loop_stop_result ();
}

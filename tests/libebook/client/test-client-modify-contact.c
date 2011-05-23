/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book-client.h>

#include "client-test-utils.h"

#define EMAIL_ADD "foo@bar.com"

static void
verify_premodify_and_prepare_contact (EContact *contact)
{
	EVCardAttribute *attr;

	/* ensure there is no email address to begin with, then add one */
	g_assert (!e_vcard_get_attribute (E_VCARD (contact), EVC_EMAIL));
	attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
	e_vcard_add_attribute_with_value (E_VCARD (contact), attr, EMAIL_ADD);
}

static void
verify_modify (EContact *contact)
{
	EVCardAttribute *attr;
	gchar *email_value;

	g_assert ((attr = e_vcard_get_attribute (E_VCARD (contact), EVC_EMAIL)));
	g_assert (e_vcard_attribute_is_single_valued (attr));
	email_value = e_vcard_attribute_get_value (attr);
	g_assert (!g_strcmp0 (email_value, EMAIL_ADD));
	g_free (email_value);
}

static void
contact_ready_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	EContact *contact;
	GError *error = NULL;

	if (!e_book_client_get_contact_finish (E_BOOK_CLIENT (source_object), result, &contact, &error)) {
		report_error ("get contact finish", &error);
		stop_main_loop (1);
		return;
	}

	verify_modify (contact);

	g_object_unref (contact);

	stop_main_loop (0);
}

static void
contact_modified_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	EContact *contact = user_data;
	GError *error = NULL;

	if (!e_book_client_modify_contact_finish (E_BOOK_CLIENT (source_object), result, &error)) {
		report_error ("modify contact finish", &error);
		stop_main_loop (1);
		return;
	}

	e_book_client_get_contact (E_BOOK_CLIENT (source_object), e_contact_get_const (contact, E_CONTACT_UID), NULL, contact_ready_cb, NULL);
}

gint
main (gint argc, gchar **argv)
{
	EBookClient *book_client;
	GError *error = NULL;
	EContact *contact, *book_contact;

	main_initialize ();

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

	/*
	 * Sync version
	 */
	if (!add_contact_from_test_case_verify (book_client, "name-only", &contact)) {
		g_object_unref (book_client);
		return 1;
	}

	verify_premodify_and_prepare_contact (contact);

	if (!e_book_client_modify_contact_sync (book_client, contact, NULL, &error)) {
		report_error ("modify contact sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	if (!e_book_client_get_contact_sync (book_client, e_contact_get_const (contact, E_CONTACT_UID), &book_contact, NULL, &error)) {
		report_error ("get contact sync", &error);
		g_object_unref (contact);
		g_object_unref (book_client);
		return 1;
	}

	verify_modify (book_contact);

	g_object_unref (book_contact);
	g_object_unref (contact);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_object_unref (book_client);

	/*
	 * Async version
	 */
	book_client = new_temp_client (NULL);
	g_return_val_if_fail (book_client != NULL, 1);

	if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	if (!add_contact_from_test_case_verify (book_client, "name-only", &contact)) {
		g_object_unref (book_client);
		return 1;
	}

	verify_premodify_and_prepare_contact (contact);

	e_book_client_modify_contact (book_client, contact, NULL, contact_modified_cb, contact);

	start_main_loop (NULL, NULL);

	g_object_unref (contact);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_object_unref (book_client);

	return get_main_loop_stop_result ();
}

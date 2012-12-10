/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright Copyright (C) 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 *          Mathias Hasselmann <mathias@openismus.com>
 */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"


/* This forces the GType to be registered in a way that
 * avoids a "statement with no effect" compiler warning.
 * FIXME Use g_type_ensure() once we require GLib 2.34. */
#define REGISTER_TYPE(type) \
	(g_type_class_unref (g_type_class_ref (type)))

static void
setup_custom_summary (ESource *scratch)
{
	ESourceBackendSummarySetup *setup;

	REGISTER_TYPE (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP);
	setup = e_source_get_extension (scratch, E_SOURCE_EXTENSION_BACKEND_SUMMARY_SETUP);
	e_source_backend_summary_setup_set_summary_fields (setup,
							   E_CONTACT_FULL_NAME,
							   E_CONTACT_FAMILY_NAME,
							   E_CONTACT_EMAIL_1,
							   E_CONTACT_TEL,
							   E_CONTACT_EMAIL,
							   0);
	e_source_backend_summary_setup_set_indexed_fields (setup,
							   E_CONTACT_TEL, E_BOOK_INDEX_SUFFIX,
							   E_CONTACT_TEL, E_BOOK_INDEX_PHONE,
							   E_CONTACT_FULL_NAME, E_BOOK_INDEX_PREFIX,
							   E_CONTACT_FULL_NAME, E_BOOK_INDEX_SUFFIX,
							   E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_PREFIX,
							   E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_SUFFIX,
							   0);
}

typedef struct {
	GTestDataFunc func;
	EBookClient *client;
	EBookQuery *query;
	gint num_contacts;
} ClientTestData;

static void
client_test_data_free (gpointer p)
{
	ClientTestData *const data = p;
	g_object_unref (data->client);

	if (data->query)
		e_book_query_unref (data->query);
	g_slice_free (ClientTestData, data);
}

static void
search_test (gconstpointer p)
{
	const ClientTestData *const data = p;
	GSList *results = NULL;
	GError *error = NULL;
	gchar *sexp;

	sexp = e_book_query_to_string (data->query);

	if (!e_book_client_get_contacts_sync (data->client, sexp, &results, NULL, &error)) {
		report_error ("get contacts", &error);
		g_test_fail ();
		return;
	}

	g_assert_cmpint (g_slist_length (results), ==, data->num_contacts);
	e_util_free_object_slist (results);
	g_free (sexp);
}

static void
uid_test (gconstpointer p)
{
	const ClientTestData *const data = p;
	GSList *results = NULL;
	GError *error = NULL;
	gchar *sexp;

	sexp = e_book_query_to_string (data->query);

	if (!e_book_client_get_contacts_uids_sync (data->client, sexp, &results, NULL, &error)) {
		report_error ("get contact uids", &error);
		g_test_fail ();
		return;
	}

	g_assert_cmpint (g_slist_length (results), ==, data->num_contacts);
	e_util_free_string_slist (results);
	g_free (sexp);
}

static void
remove_test (gconstpointer p)
{
	const ClientTestData *const data = p;
	GError *error = NULL;

	if (!e_client_remove_sync (E_CLIENT (data->client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_test_fail ();
		return;
	}
}

/* Temporary hack not in master, we need to support
 * earlier versions of GLib (< 2.34) without g_test_add_data_func_full()
 *
 *
 */
static void
test_and_free (gconstpointer p)
{
	ClientTestData *data = (ClientTestData *)p;

	data->func (p);
	client_test_data_free (data);
}

static void
add_client_test (const gchar *path,
                 GTestDataFunc func,
                 EBookClient *client,
                 EBookQuery *query,
                 gint num_contacts)
{
	ClientTestData *data = g_slice_new (ClientTestData);

	data->func = func;
	data->client = g_object_ref (client);
	data->query = query;
	data->num_contacts = num_contacts;

	g_test_add_data_func (path, data, test_and_free);
}

gint
main (gint argc,
      gchar **argv)
{
	EBookClient *book_client;
	EContact *contact_final;
	GError *error = NULL;

	g_test_init (&argc, &argv, NULL);
	main_initialize ();

	/* Setup */
	book_client = new_custom_temp_client (NULL, setup_custom_summary);
	g_return_val_if_fail (book_client != NULL, 1);

	if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	/* Add contacts */
	if (!add_contact_from_test_case_verify (book_client, "custom-1", &contact_final)) {
		g_object_unref (book_client);
		return 1;
	}
	if (!add_contact_from_test_case_verify (book_client, "custom-2", &contact_final)) {
		g_object_unref (book_client);
		return 1;
	}
	if (!add_contact_from_test_case_verify (book_client, "custom-3", &contact_final)) {
		g_object_unref (book_client);
		return 1;
	}
	if (!add_contact_from_test_case_verify (book_client, "custom-4", &contact_final)) {
		g_object_unref (book_client);
		return 1;
	}
	if (!add_contact_from_test_case_verify (book_client, "custom-5", &contact_final)) {
		g_object_unref (book_client);
		return 1;
	}

	/* Add search tests that fetch contacts */
	add_client_test ("/client/search/exact/fn", search_test, book_client,
	                 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "James Brown"),
	                 1);
	add_client_test ("/client/search/prefix/fn", search_test, book_client,
	                 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_BEGINS_WITH, "B"),
	                 2);
	add_client_test ("/client/search/suffix/phone", search_test, book_client,
	                 e_book_query_field_test (E_CONTACT_TEL, E_BOOK_QUERY_ENDS_WITH, "999"),
	                 2);
	add_client_test ("/client/search/suffix/email", search_test, book_client,
	                 e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, "jackson.com"),
	                 2);
	add_client_test ("/client/search/exact/name", search_test, book_client,
	                 e_book_query_vcard_field_test(EVC_N, E_BOOK_QUERY_IS, "Janet"),
	                 1);
	add_client_test ("/client/search/eqphone/exact/phone", search_test, book_client,
	                 e_book_query_field_test(E_CONTACT_TEL, E_BOOK_QUERY_EQUALS_PHONE_NUMBER, "+1 221.542.3789"),
	                 1);
	add_client_test ("/client/search/eqphone/national/phone", search_test, book_client,
	                 e_book_query_field_test(E_CONTACT_TEL, E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER, "221.542.3789"),
	                 1);
	add_client_test ("/client/search/eqphone/short/phone", search_test, book_client,
	                 e_book_query_field_test(E_CONTACT_TEL, E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER, "5423789"),
	                 1);
	add_client_test ("/client/search/eqphone/exact/tel", search_test, book_client,
	                 e_book_query_vcard_field_test(EVC_TEL, E_BOOK_QUERY_EQUALS_PHONE_NUMBER, "+1 221.542.3789"),
	                 1);
	add_client_test ("/client/search/eqphone/national/tel", search_test, book_client,
	                 e_book_query_vcard_field_test(EVC_TEL, E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER, "221.542.3789"),
	                 1);
	add_client_test ("/client/search/eqphone/short/tel", search_test, book_client,
	                 e_book_query_vcard_field_test(EVC_TEL, E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER, "5423789"),
	                 1);

	/* Add search tests that fetch uids */
	add_client_test ("/client/search-uid/exact/name", uid_test, book_client,
	                 e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, "jackson.com"),
	                 2);

	/* Test remove operation */
	add_client_test ("/client/remove", remove_test, book_client, NULL, 0);

	/* Roll dices */
	g_object_unref (book_client);

	return g_test_run ();
}

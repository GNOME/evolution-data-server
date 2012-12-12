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
#include "e-test-server-utils.h"


typedef struct {
	ETestServerClosure closure;
	EBookQuery *query;
	gint num_contacts;
} ClientTestData;

/* Cleanup the closures, just for the hell of it... */
static GList *closures = NULL;

static void
client_test_data_free (gpointer p)
{
	ClientTestData *data = (ClientTestData *)p;

	if (data->query)
		e_book_query_unref (data->query);
	g_slice_free (ClientTestData, data);
}

static void
setup_custom_book (ESource            *scratch,
		   ETestServerClosure *closure)
{
	ESourceBackendSummarySetup *setup;

	g_type_ensure (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP);
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
							   E_CONTACT_FULL_NAME, E_BOOK_INDEX_PREFIX,
							   E_CONTACT_FULL_NAME, E_BOOK_INDEX_SUFFIX,
							   E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_PREFIX,
							   E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_SUFFIX,
							   0);
}

static void
add_client_test (const gchar *path,
                 gpointer func,
                 EBookQuery *query,
                 gint num_contacts)
{
	ClientTestData *data = g_slice_new0 (ClientTestData);

	data->closure.type = E_TEST_SERVER_ADDRESS_BOOK;
	data->closure.customize = setup_custom_book;
	data->query = query;
	data->num_contacts = num_contacts;

	g_test_add (path, ETestServerFixture, data, e_test_server_utils_setup, func, e_test_server_utils_teardown);
	closures = g_list_prepend (closures, data);
}

static void
setup_book (EBookClient *book_client)
{
	/* Add contacts */
	if (!add_contact_from_test_case_verify (book_client, "custom-1", NULL) ||
	    !add_contact_from_test_case_verify (book_client, "custom-2", NULL) ||
	    !add_contact_from_test_case_verify (book_client, "custom-3", NULL) ||
	    !add_contact_from_test_case_verify (book_client, "custom-4", NULL) ||
	    !add_contact_from_test_case_verify (book_client, "custom-5", NULL) ||
	    !add_contact_from_test_case_verify (book_client, "custom-6", NULL)) {
		g_error ("Failed to add contacts");
	}
}

static void
search_test (ETestServerFixture *fixture,
	     gconstpointer       user_data)
{
	EBookClient *book_client;
	GSList *results = NULL;
	GError *error = NULL;
	gchar *sexp;
	const ClientTestData *const data = user_data;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	setup_book (book_client);

	sexp = e_book_query_to_string (data->query);

	if (!e_book_client_get_contacts_sync (book_client, sexp, &results, NULL, &error)) {
		g_error ("get contacts: %s", error->message);
	}

	g_assert_cmpint (g_slist_length (results), ==, data->num_contacts);
	e_util_free_object_slist (results);
	g_free (sexp);
}

static void
uid_test (ETestServerFixture *fixture,
	  gconstpointer       user_data)
{
	EBookClient *book_client;
	GSList *results = NULL;
	GError *error = NULL;
	gchar *sexp;
	const ClientTestData *const data = user_data;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	setup_book (book_client);

	sexp = e_book_query_to_string (data->query);

	if (!e_book_client_get_contacts_uids_sync (book_client, sexp, &results, NULL, &error)) {
		g_error ("get contact uids: %s", error->message);
	}

	g_assert_cmpint (g_slist_length (results), ==, data->num_contacts);
	e_util_free_string_slist (results);
	g_free (sexp);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);


	/* Add search tests that fetch contacts */
	add_client_test ("/client/search/exact/fn", search_test,
	                 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "James Brown"),
	                 1);
	add_client_test ("/client/search/exact/name", search_test,
	                 e_book_query_vcard_field_test(EVC_N, E_BOOK_QUERY_IS, "Janet"),
	                 1);
	add_client_test ("/client/search/prefix/fn", search_test,
	                 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_BEGINS_WITH, "B"),
	                 2);
	add_client_test ("/client/search/prefix/fn/percent", search_test,
	                 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_BEGINS_WITH, "%"),
	                 1);
	add_client_test ("/client/search/suffix/phone", search_test,
	                 e_book_query_field_test (E_CONTACT_TEL, E_BOOK_QUERY_ENDS_WITH, "999"),
	                 2);
	add_client_test ("/client/search/suffix/email", search_test,
	                 e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, "jackson.com"),
	                 2);

	/* Add search tests that fetch uids */
	add_client_test ("/client/search-uid/exact/fn", uid_test,
	                 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "James Brown"),
	                 1);
	add_client_test ("/client/search-uid/exact/name", uid_test,
	                 e_book_query_vcard_field_test(EVC_N, E_BOOK_QUERY_IS, "Janet"),
	                 1);
	add_client_test ("/client/search-uid/prefix/fn", uid_test,
	                 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_BEGINS_WITH, "B"),
	                 2);
	add_client_test ("/client/search-uid/prefix/fn/percent", uid_test,
	                 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_BEGINS_WITH, "%"),
	                 1);
	add_client_test ("/client/search-uid/suffix/phone", uid_test,
	                 e_book_query_field_test (E_CONTACT_TEL, E_BOOK_QUERY_ENDS_WITH, "999"),
	                 2);
	add_client_test ("/client/search-uid/suffix/email", uid_test,
	                 e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, "jackson.com"),
	                 2);

	ret = e_test_server_utils_run ();

	g_list_free_full (closures, client_test_data_free);

	return ret;
}

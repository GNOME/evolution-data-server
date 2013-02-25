/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2012,2013 Intel Corporation
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

#include <config.h>
#include <stdlib.h>
#include <libebook/libebook.h>
#include <locale.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

typedef struct {
	ETestServerClosure parent;
	EBookQuery *query;
	gint num_contacts;
} ClientTestData;

typedef struct {
	ETestServerFixture parent;
	EContact *contacts[9];
} ClientTestFixture;

static void
client_test_data_free (gpointer p)
{
	ClientTestData *data = (ClientTestData *) p;

	if (data->query)
		e_book_query_unref (data->query);

	g_slice_free (ClientTestData, data);
}

static void
setup_custom_book (ESource *scratch,
                   ETestServerClosure *closure)
{
	ESourceBackendSummarySetup *setup;

	g_type_ensure (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP);
	setup = e_source_get_extension (scratch, E_SOURCE_EXTENSION_BACKEND_SUMMARY_SETUP);
	e_source_backend_summary_setup_set_summary_fields (
		setup,
		E_CONTACT_FULL_NAME,
		E_CONTACT_FAMILY_NAME,
		E_CONTACT_EMAIL_1,
		E_CONTACT_TEL,
		E_CONTACT_EMAIL,
		0);
	e_source_backend_summary_setup_set_indexed_fields (
		setup,
		E_CONTACT_TEL, E_BOOK_INDEX_SUFFIX,
#ifdef ENABLE_PHONENUMBER
		E_CONTACT_TEL, E_BOOK_INDEX_PHONE,
#endif /* ENABLE_PHONENUMBER */
		E_CONTACT_FULL_NAME, E_BOOK_INDEX_PREFIX,
		E_CONTACT_FULL_NAME, E_BOOK_INDEX_SUFFIX,
		E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_PREFIX,
		E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_SUFFIX,
		0);
}

static void
client_test_setup (ClientTestFixture *fixture,
		   gconstpointer user_data)
{
	e_test_server_utils_setup (&fixture->parent, user_data);
}

static void
client_test_teardown (ClientTestFixture *fixture,
		      gconstpointer user_data)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (fixture->contacts); ++i) {
		if (fixture->contacts[i])
			g_object_unref (fixture->contacts[i]);
	}

	e_test_server_utils_teardown (&fixture->parent, user_data);
}

static void
add_client_test (const gchar *prefix,
		 const gchar *test_case_name,
                 gpointer func,
                 EBookQuery *query,
                 gint num_contacts,
		 gboolean direct,
		 gboolean custom)
{
	ClientTestData *data = g_slice_new0 (ClientTestData);
	gchar *path = g_strconcat (prefix, test_case_name, NULL);

	data->parent.type = direct ? E_TEST_SERVER_DIRECT_ADDRESS_BOOK : E_TEST_SERVER_ADDRESS_BOOK;

	if (custom)
		data->parent.customize = setup_custom_book;

	data->parent.destroy_closure_func = client_test_data_free;
	data->query = query;
	data->num_contacts = num_contacts;

	g_test_add (path, ClientTestFixture, data,
		    client_test_setup, func, client_test_teardown);

	g_free (path);
}

static void
setup_book (ClientTestFixture *fixture)
{
	EContact **it = fixture->contacts;
	EBookClient *book_client;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	/* Add contacts */
	if (!add_contact_from_test_case_verify (book_client, "custom-1", it++) ||
	    !add_contact_from_test_case_verify (book_client, "custom-2", it++) ||
	    !add_contact_from_test_case_verify (book_client, "custom-3", it++) ||
	    !add_contact_from_test_case_verify (book_client, "custom-4", it++) ||
	    !add_contact_from_test_case_verify (book_client, "custom-5", it++) ||
	    !add_contact_from_test_case_verify (book_client, "custom-6", it++) ||
	    !add_contact_from_test_case_verify (book_client, "custom-7", it++) ||
	    !add_contact_from_test_case_verify (book_client, "custom-8", it++) ||
	    !add_contact_from_test_case_verify (book_client, "custom-9", it++)) {
		g_error ("Failed to add contacts");
	}

	g_assert_cmpint (it - fixture->contacts, <=, G_N_ELEMENTS (fixture->contacts));
}

static void
search_test (ClientTestFixture *fixture,
             gconstpointer user_data)
{
	const ClientTestData *const data = user_data;
	EBookClient *book_client;
	GSList *results = NULL;
	GError *error = NULL;
	gchar *sexp;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	setup_book (fixture);

	sexp = e_book_query_to_string (data->query);

	if (!e_book_client_get_contacts_sync (book_client, sexp, &results, NULL, &error)) {
		g_error ("get contacts: %s", error->message);
	}

	g_assert_cmpint (g_slist_length (results), ==, data->num_contacts);
	e_util_free_object_slist (results);
	g_free (sexp);
}

static void
uid_test (ClientTestFixture *fixture,
          gconstpointer user_data)
{
	const ClientTestData *const data = user_data;
	EBookClient *book_client;
	GSList *results = NULL;
	GError *error = NULL;
	gchar *sexp;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	setup_book (fixture);

	sexp = e_book_query_to_string (data->query);

	if (!e_book_client_get_contacts_uids_sync (book_client, sexp, &results, NULL, &error)) {
		g_error ("get contact uids: %s", error->message);
	}

	g_assert_cmpint (g_slist_length (results), ==, data->num_contacts);
	e_util_free_string_slist (results);
	g_free (sexp);
}

#ifdef ENABLE_PHONENUMBER
#if 0 /* FIXME: This test is broken */

static void
locale_change_test (ClientTestFixture *fixture,
		     gconstpointer user_data)
{
	EBookClient *book_client;
	GSList *results = NULL;
	GError *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	setup_book (fixture);

	if (!e_book_client_get_contacts_uids_sync (
		book_client, "(eqphone \"phone\" \"221-5423789\" \"en_US.UTF-8\")",
				&results, NULL, &error)) {
		g_error ("get contact uids: %s", error->message);
	}

	g_assert_cmpint (g_slist_length (results), ==, 1);

	g_assert_cmpstr (
		results->data, ==,
		e_contact_get_const (fixture->contacts[0], E_CONTACT_UID));

	e_util_free_string_slist (results);

	if (!e_book_client_get_contacts_uids_sync (
		book_client, "(eqphone \"phone\" \"221-5423789\" \"en_GB.UTF-8\")",
				&results, NULL, &error)) {
		g_error ("get contact uids: %s", error->message);
	}

	g_assert_cmpint (g_slist_length (results), ==, 0);
	e_util_free_string_slist (results);
}

#endif
#endif /* ENABLE_PHONENUMBER */

typedef struct {
	gpointer func;
	gboolean direct;
	gboolean custom;
	const gchar *prefix;
} SuiteType;

gint
main (gint argc,
      gchar **argv)
{
	gint ret, i;
	SuiteType suites[] = {
		{ search_test, FALSE, FALSE, "/EBookClient/Default/Search" },
		{ uid_test,    FALSE, FALSE, "/EBookClient/Default/SearchUID" },
		{ search_test, TRUE,  FALSE, "/EBookClient/Default/DirectAccess/Search" },
		{ uid_test,    TRUE,  FALSE, "/EBookClient/Default/DirectAccess/SearchUID" },
		{ search_test, FALSE, TRUE,  "/EBookClient/Custom/Search" },
		{ uid_test,    FALSE, TRUE,  "/EBookClient/Custom/SearchUID" },
		{ search_test, TRUE,  TRUE,  "/EBookClient/Custom/DirectAccess/Search" },
		{ uid_test,    TRUE,  TRUE,  "/EBookClient/Custom/DirectAccess/SearchUID" }
	};

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	setlocale (LC_ALL, "en_US.UTF-8");

	/* Test all queries in 8 different combinations specified by the 'suites'
	 */
	for (i = 0; i < G_N_ELEMENTS (suites); i++) {

		/* Add search tests that fetch contacts */
		add_client_test (suites[i].prefix, "/Exact/FullName", suites[i].func,
				 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "James Brown"),
				 1, suites[i].direct, suites[i].custom);

		add_client_test (suites[i].prefix, "/Exact/Name", suites[i].func,
				 e_book_query_vcard_field_test (EVC_N, E_BOOK_QUERY_IS, "Janet"),
				 1, suites[i].direct, suites[i].custom);

		add_client_test (suites[i].prefix, "/Prefix/FullName", suites[i].func,
				 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_BEGINS_WITH, "B"),
				 2, suites[i].direct, suites[i].custom);

		add_client_test (suites[i].prefix, "/Prefix/FullName/Percent", suites[i].func,
				 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_BEGINS_WITH, "%"),
				 1, suites[i].direct, suites[i].custom);

		add_client_test (suites[i].prefix, "/Suffix/Phone", suites[i].func,
				 e_book_query_field_test (E_CONTACT_TEL, E_BOOK_QUERY_ENDS_WITH, "999"),
				 2, suites[i].direct, suites[i].custom);

		/* This test proves that we do not get any results for custom-7.vcf, which contains
		 * a phone number ending with "88 99", if this were accidentally normalized, we would
		 * get a result for it.
		 */
		add_client_test (suites[i].prefix, "/Suffix/Phone/NotNormalized", suites[i].func,
				 e_book_query_field_test (E_CONTACT_TEL, E_BOOK_QUERY_ENDS_WITH, "8899"),
				 0, suites[i].direct, suites[i].custom);

		add_client_test (suites[i].prefix, "/Suffix/Email", suites[i].func,
				 e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, "jackson.com"),
				 2, suites[i].direct, suites[i].custom);

#ifdef ENABLE_PHONENUMBER

		/* These queries will do an index lookup with a custom summary, and a full table scan
		 * matching with EBookBackendSexp when the default summary is used
		 */
		add_client_test (suites[i].prefix, "/EqPhone/Exact", suites[i].func,
				 e_book_query_field_test (E_CONTACT_TEL, E_BOOK_QUERY_EQUALS_PHONE_NUMBER, "+1 221.542.3789"),
				 1, suites[i].direct, suites[i].custom);

		/*********************************************
		 * E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER *
		 *********************************************/

		/* Test that a query term with no specified country returns only vCards that 
		 * are specifically in the active country code.
		 *
		 * | Active Country Code: +1 | Query: 221.542.3789 | vCard Data: +1-221-5423789 | Matches: yes |
		 * | Active Country Code: +1 | Query: 221.542.3789 | vCard Data: +31-221-5423789 | Matches: no  |
		 */
		add_client_test (suites[i].prefix, "/EqPhone/National", suites[i].func,
				 e_book_query_field_test (E_CONTACT_TEL, E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER, "221.542.3789"),
				 2, suites[i].direct, suites[i].custom);

		/* Test that a query term with a specified country returns only vCards that 
		 * are specifically in the specified country code.
		 *
		 * | Active Country Code: +1 | Query: +49 221.542.3789 | vCard Data: +1-221-5423789 | Matches: no |
		 * | Active Country Code: +1 | Query: +49 221.542.3789 | vCard Data: +31-221-5423789 | Matches: no |
		 */
		add_client_test (suites[i].prefix, "/EqPhone/National/CountryMismatch", suites[i].func,
				 e_book_query_field_test (E_CONTACT_TEL,
							  E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER, "+49 221.542.3789"),
				 0, suites[i].direct, suites[i].custom);


		/* Test that a query term with the active country code specified returns a vCard with an unspecified
		 * country code.
		 *
		 * | Active Country Code: +1 | Query: +1 514-845-8436 | vCard Data: 514-845-8436 | Matches: yes |
		 */
		add_client_test (suites[i].prefix, "/EqPhone/National/CountryAbsent/QueryWithCountry", suites[i].func,
				 e_book_query_field_test (E_CONTACT_TEL,
							  E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER, "+1 514-845-8436"),
				 1, suites[i].direct, suites[i].custom);

#if 0
		/* FIXME: This test passes with the default summary, but fails in the custom summary.
		 *
		 * Because EBookBackendSexp uses a straiht-forward e_phone_number_compare_strings() method
		 * of comparison, it's my feeling that the method used in EBookBackendSqliteDB needs to be
		 * fixed (possibly by just using e_phone_number_compare_strings() directly in it's collation rule).
		 */

		/* Test that a query term with an arbitrary country code specified returns a vCard with an unspecified
		 * country code.
		 *
		 * | Active Country Code: +1 | Query: +49 514-845-8436 | vCard Data: 514-845-8436 | Matches: yes |
		 */
		add_client_test (suites[i].prefix, "/EqPhone/National/CountryAbsent/QueryOtherCountry", suites[i].func,
				 e_book_query_field_test (E_CONTACT_TEL,
							  E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER, "+49 514-845-8436"),
				 1, suites[i].direct, suites[i].custom);
#endif

		add_client_test (suites[i].prefix, "/EqPhone/Short", suites[i].func,
				 e_book_query_field_test (E_CONTACT_TEL, E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER, "5423789"),
				 2, suites[i].direct, suites[i].custom);

#endif /* ENABLE_PHONENUMBER */

	}

#ifdef ENABLE_PHONENUMBER
#if 0 /* FIXME: This test is broken */

	add_client_test (
	        "/EBookClient", "/EqPhone/LocaleChange", locale_change_test,
		NULL, 0, FALSE, TRUE);

	add_client_test (
	        "/EBookClient/DirectAccess", "/EqPhone/LocaleChange", locale_change_test,
		NULL, 0, TRUE, TRUE);
#endif
#endif /* ENABLE_PHONENUMBER */

	ret = e_test_server_utils_run ();

	return ret;
}

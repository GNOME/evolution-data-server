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
		E_CONTACT_TEL, E_BOOK_INDEX_PHONE,
		E_CONTACT_FULL_NAME, E_BOOK_INDEX_PREFIX,
		E_CONTACT_FULL_NAME, E_BOOK_INDEX_SUFFIX,
		E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_PREFIX,
		E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_SUFFIX,
		0);
}

static ETestServerClosure setup_custom_closure  = { E_TEST_SERVER_ADDRESS_BOOK, setup_custom_book, 0, TRUE, NULL };
static ETestServerClosure setup_default_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, TRUE, NULL };

/* Define this macro to expect E_CLIENT_ERROR_NOT_SUPPORTED
 * only on phone number queries when EDS is built with no
 * phone number support.
 */
#ifdef ENABLE_PHONENUMBER
#  define CHECK_UNSUPPORTED_ERROR(data) (FALSE)
#else
#  define CHECK_UNSUPPORTED_ERROR(data) (((ClientTestData *)(data))->phone_number_query != FALSE)
#endif

#define N_CONTACTS 16

typedef struct {
	ETestServerClosure parent;
	gchar *sexp;
	gint num_contacts;
	gboolean phone_number_query;
} ClientTestData;

typedef struct {
	ETestServerFixture parent;
} ClientTestFixture;

static void
client_test_data_free (gpointer p)
{
	ClientTestData *data = (ClientTestData *) p;

	g_free (data->sexp);
	g_slice_free (ClientTestData, data);
}

static void
client_test_setup_custom (ClientTestFixture *fixture,
                          gconstpointer user_data)
{
	fixture->parent.source_name = g_strdup ("custom-book");
	e_test_server_utils_setup (&fixture->parent, user_data);
}

static void
client_test_setup_default (ClientTestFixture *fixture,
                           gconstpointer user_data)
{
	fixture->parent.source_name = g_strdup ("default-book");
	e_test_server_utils_setup (&fixture->parent, user_data);
}

static void
client_test_teardown (ClientTestFixture *fixture,
                      gconstpointer user_data)
{
	e_test_server_utils_teardown (&fixture->parent, user_data);
}

static void
add_client_test_sexp (const gchar *prefix,
                      const gchar *test_case_name,
                      gpointer func,
                      gchar *sexp, /* sexp is 'given' */
                      gint num_contacts,
                      gboolean direct,
                      gboolean custom,
                      gboolean phone_number_query)
{
	ClientTestData *data = g_slice_new0 (ClientTestData);
	gchar *path = g_strconcat (prefix, test_case_name, NULL);

	data->parent.type = direct ? E_TEST_SERVER_DIRECT_ADDRESS_BOOK : E_TEST_SERVER_ADDRESS_BOOK;

	data->parent.destroy_closure_func = client_test_data_free;
	data->parent.keep_work_directory = TRUE;
	data->sexp = sexp;
	data->num_contacts = num_contacts;
	data->phone_number_query = phone_number_query;

	if (custom)
		g_test_add (
			path, ClientTestFixture, data,
			client_test_setup_custom, func,
			client_test_teardown);
	else
		g_test_add (
			path, ClientTestFixture, data,
			client_test_setup_default, func,
			client_test_teardown);

	g_free (path);
}

static void
add_client_test (const gchar *prefix,
                 const gchar *test_case_name,
                 gpointer func,
                 EBookQuery *query,
                 gint num_contacts,
                 gboolean direct,
                 gboolean custom,
                 gboolean phone_number_query)
{
	add_client_test_sexp (
		prefix, test_case_name, func,
		e_book_query_to_string (query),
		num_contacts, direct, custom, phone_number_query);
	e_book_query_unref (query);
}

static void
setup_book (ClientTestFixture *fixture)
{
	EBookClient *book_client;
	GSList *contacts = NULL;
	GError *error = NULL;
	gint i;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	for (i = 0; i < N_CONTACTS; i++) {
		gchar *case_name = g_strdup_printf ("custom-%d", i + 1);
		gchar *vcard;
		EContact *contact;

		vcard    = new_vcard_from_test_case (case_name);
		contact  = e_contact_new_from_vcard (vcard);
		contacts = g_slist_prepend (contacts, contact);
		g_free (vcard);
		g_free (case_name);
	}

	if (!e_book_client_add_contacts_sync (book_client, contacts, NULL, NULL, &error))
		g_error ("Failed to add test contacts");

	g_slist_free_full (contacts, (GDestroyNotify) g_object_unref);
}

static void
setup_test (ClientTestFixture *fixture,
            gconstpointer user_data)
{
	setup_book (fixture);
}

static void
search_test (ClientTestFixture *fixture,
             gconstpointer user_data)
{
	const ClientTestData *const data = user_data;
	EBookClient *book_client;
	GSList *results = NULL;
	GError *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (CHECK_UNSUPPORTED_ERROR (data)) {
		/* Expect unsupported query (no phone number support in a phone number query) */
		if (e_book_client_get_contacts_sync (book_client, data->sexp, &results, NULL, &error))
			g_error ("Succeeded to query contacts when phone numbers are not supported");
		else if (!g_error_matches (error, E_CLIENT_ERROR, E_CLIENT_ERROR_NOT_SUPPORTED))
			g_error (
				"Wrong error when querying with unsupported query: %s (domain: %s, code: %d)",
				error->message, g_quark_to_string (error->domain), error->code);
		else
			g_clear_error (&error);

	} else if (data->num_contacts < 0) {
		/* Expect E_CLIENT_ERROR_INVALID_QUERY */
		if (e_book_client_get_contacts_sync (book_client, data->sexp, &results, NULL, &error))
			g_error ("Succeeded to query contacts with an invalid query type");
		else if (!g_error_matches (error, E_CLIENT_ERROR, E_CLIENT_ERROR_INVALID_QUERY))
			g_error (
				"Wrong error when querying with an invalid query: %s (domain: %s, code: %d)",
				error->message, g_quark_to_string (error->domain), error->code);
		else
			g_clear_error (&error);
	} else {
		/* Expect successful query */
		if (!e_book_client_get_contacts_sync (book_client, data->sexp, &results, NULL, &error)) {
			g_error ("get contacts: %s", error->message);
		}

		g_assert_cmpint (g_slist_length (results), ==, data->num_contacts);
	}

	e_util_free_object_slist (results);
}

static void
uid_test (ClientTestFixture *fixture,
          gconstpointer user_data)
{
	const ClientTestData *const data = user_data;
	EBookClient *book_client;
	GSList *results = NULL;
	GError *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (CHECK_UNSUPPORTED_ERROR (data)) {
		/* Expect unsupported query (no phone number support in a phone number query) */
		if (e_book_client_get_contacts_uids_sync (book_client, data->sexp, &results, NULL, &error))
			g_error ("Succeeded to query contact uids when phone numbers are not supported");
		else if (!g_error_matches (error, E_CLIENT_ERROR, E_CLIENT_ERROR_NOT_SUPPORTED))
			g_error (
				"Wrong error when querying uids with unsupported query: %s (domain: %s, code: %d)",
				error->message, g_quark_to_string (error->domain), error->code);
		else
			g_clear_error (&error);

	} else if (data->num_contacts < 0) {
		/* Expect E_CLIENT_ERROR_INVALID_QUERY */
		if (e_book_client_get_contacts_uids_sync (book_client, data->sexp, &results, NULL, &error))
			g_error ("Succeeded to query contacts with an invalid query type");
		else if (!g_error_matches (error, E_CLIENT_ERROR, E_CLIENT_ERROR_INVALID_QUERY))
			g_error (
				"Wrong error when querying with an invalid query: %s (domain: %s, code: %d)",
				error->message, g_quark_to_string (error->domain), error->code);
		else
			g_clear_error (&error);
	} else {
		/* Expect successful query */
		if (!e_book_client_get_contacts_uids_sync (book_client, data->sexp, &results, NULL, &error)) {
			g_error ("get contact uids: %s", error->message);
		}

		g_assert_cmpint (g_slist_length (results), ==, data->num_contacts);
	}

	e_util_free_string_slist (results);
}

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

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	/* Change environment so that the addressbook factory inherits this setting */
	g_setenv ("LC_ALL", "en_US.UTF-8", TRUE);
	setlocale (LC_ALL, "");

#if defined (LC_ADDRESS)
	/* LC_ADDRESS is a GNU extension. */
	g_assert_cmpstr (setlocale (LC_ADDRESS, NULL), ==, "en_US.UTF-8");
#endif

	/* Before beginning, setup two books and populate them with contacts, one with
	 * a customized summary and another without a customized summary
	 */
	g_test_add (
		"/EBookClient/SetupDefaultBook", ClientTestFixture, &setup_default_closure,
		client_test_setup_default, setup_test, client_test_teardown);
	g_test_add (
		"/EBookClient/SetupCustomBook", ClientTestFixture, &setup_custom_closure,
		client_test_setup_custom, setup_test, client_test_teardown);

	/* Test all queries in 8 different combinations specified by the 'suites'
	 */
	for (i = 0; i < G_N_ELEMENTS (suites); i++) {

		/* A query that will cause e_sexp_parse() to report an error */
		add_client_test_sexp (
			suites[i].prefix,
			"/InvalidQuery",
			suites[i].func,
			g_strdup ("(invalid \"query\" \"term\")"),
			-1,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		/* Add search tests that fetch contacts */
		add_client_test (
			suites[i].prefix,
			"/Exact/FullName",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_FULL_NAME,
				E_BOOK_QUERY_IS,
				"James Brown"),
			1,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Exact/Name",
			suites[i].func,
			e_book_query_vcard_field_test (
				EVC_N,
				E_BOOK_QUERY_IS,
				"Janet"),
			1,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Prefix/FullName",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_FULL_NAME,
				E_BOOK_QUERY_BEGINS_WITH,
				"B"),
			3,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Prefix/FullName/Percent",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_FULL_NAME,
				E_BOOK_QUERY_BEGINS_WITH,
				"%"),
			1,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		/* Query the E_CONTACT_TEL field for something that is not a
		 * phone number, user is searching for all the contacts when
		 * they noted they must ask Jenny for the phone number. */
		add_client_test (
			suites[i].prefix,
			"/Prefix/Phone/NotAPhoneNumber",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_BEGINS_WITH,
				"ask Jenny"),
			1,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Suffix/Phone",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_ENDS_WITH,
				"999"),
			2,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		/* This test proves that we do not get any results for
		 * custom-7.vcf, which contains a phone number ending with
		 * "88 99", if this were accidentally normalized, we would
		 * get a result for it. */
		add_client_test (
			suites[i].prefix,
			"/Suffix/Phone/NotNormalized",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_ENDS_WITH,
				"8899"),
			0,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Suffix/Email",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_EMAIL,
				E_BOOK_QUERY_ENDS_WITH,
				"jackson.com"),
			2,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		/*********************************************
		 *         PHONE NUMBER QUERIES FOLLOW       *
		 *********************************************/

		/* Expect E_CLIENT_ERROR_INVALID_QUERY, "ask Jenny for
		 * Lisa's number" was entered for contact-6.vcf, it can
		 * be searched using normal E_BOOK_QUERY_* queries but
		 * treating it as a phone number is an invalid query. */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/InvalidPhoneNumber",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_PHONE_NUMBER,
				"ask Jenny for Lisa's number"),
			-1,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/* These queries will do an index lookup with a custom summary,
		 * and a full table scan matching with EBookBackendSexp when
		 * the default summary is used. */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/Exact/Common",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_PHONE_NUMBER,
				"+1 221.542.3789"),
			1,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/* This test checks that phone number matching works when
		 * deeply nested into a query, when ENABLE_PHONENUMBER is
		 * not defined, then it ensures that the query is refused
		 * while being deeply nested. */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/Exact/Nested",
			suites[i].func,
			e_book_query_orv (
				e_book_query_field_test (
					E_CONTACT_FULL_NAME,
					E_BOOK_QUERY_IS,
					"Not In The Summary"),
				e_book_query_field_test (
					E_CONTACT_EMAIL,
					E_BOOK_QUERY_IS,
					"not.in.the@summary.com"),
				e_book_query_andv (
					e_book_query_field_test (
						E_CONTACT_FULL_NAME,
						E_BOOK_QUERY_BEGINS_WITH,
						"Micheal"),
					e_book_query_field_test (
						E_CONTACT_TEL,
						E_BOOK_QUERY_EQUALS_PHONE_NUMBER,
						"+1 221.542.3789"),
					NULL),
				NULL),
			1,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/*********************************************
		 * E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER *
		 *********************************************/

		add_client_test (
			suites[i].prefix,
			"/EqPhone/National/WithoutCountry",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER,
				"408 765-5050"),
			2,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		add_client_test (
			suites[i].prefix,
			"/EqPhone/National/en_US",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER,
				"+1 408 765-5050"),
			2,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		add_client_test (
			suites[i].prefix,
			"/EqPhone/National/de_DE",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER,
				"+49 408 765-5050"),
			1,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/* Test that a query term with no specified country returns
		 * all vCards that have the same national number regardless
		 * of country codes.
		 *
		 * | Active Country Code: +1 | Query: 221.542.3789 | vCard Data: +1-221-5423789 | Matches: yes |
		 * | Active Country Code: +1 | Query: 221.542.3789 | vCard Data: +31-221-5423789 | Matches: no  |
		 */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/National/Common",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER,
				"221.542.3789"),
			1,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/* Test that a query term with a specified country returns
		 * only vCards that are specifically in the specified country
		 * code.
		 *
		 * | Active Country Code: +1 | Query: +49 221.542.3789 | vCard Data: +1-221-5423789 | Matches: no |
		 * | Active Country Code: +1 | Query: +49 221.542.3789 | vCard Data: +31-221-5423789 | Matches: no |
		 */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/National/CountryMismatch",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER,
				"+49 221.542.3789"),
			0,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/* Test that a query term with the active country code
		 * specified returns a vCard with an unspecified country code.
		 *
		 * | Active Country Code: +1 | Query: +1 514-845-8436 | vCard Data: 514-845-8436 | Matches: yes |
		 */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/National/CountryAbsent/QueryWithCountry",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER,
				"+1 514-845-8436"),
			1,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/* Test that a query term with an arbitrary country code
		 * specified returns a vCard with an unspecified country code.
		 *
		 * | Active Country Code: +1 | Query: +49 514-845-8436 | vCard Data: 514-845-8436 | Matches: yes |
		 */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/National/CountryAbsent/QueryOtherCountry",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER,
				"+49 514-845-8436"),
			0,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		add_client_test (
			suites[i].prefix,
			"/EqPhone/Short",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER,
				"5423789"),
			1,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/*********************************************
		 *             REGEX QUERIES FOLLOW          *
		 *********************************************/
		add_client_test (
			suites[i].prefix,
			"/Regex/Normal/.*jack.*",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_FULL_NAME,
				E_BOOK_QUERY_REGEX_NORMAL,
				".*jack.*"),
			2,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Regex/Normal/Keypad/^[jkl5][ghi4][mno6].*",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_FULL_NAME,
				E_BOOK_QUERY_REGEX_NORMAL,
				"^[jkl5][ghi4][mno6].*"),
			3,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Regex/Normal/Fuzzy/VanityNumber/ELEPHANT",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_REGEX_NORMAL,
				".*[def3]"           /* E */
				"[^\\da-z]*[jkl5]"    /* L */
				"[^\\da-z]*[def3]"    /* E */
				"[^\\da-z]*[pqrs7]"   /* P */
				"[^\\da-z]*[ghi4]"    /* H */
				"[^\\da-z]*[abc2]"    /* A */
				"[^\\da-z]*[mno6]"    /* N */
				"[^\\da-z]*[tuv8].*"),/* T */
			3,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Regex/Raw/.*Jack.*",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_FULL_NAME,
				E_BOOK_QUERY_REGEX_RAW,
				".*Jack.*"),
			2,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Regex/Raw/Keypad/^[jkl5][ghi4][mno6].*",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_FULL_NAME,
				E_BOOK_QUERY_REGEX_RAW,
				"^[jklJKL5][ghiGHI4][mnoMNO6].*"),
			3,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Regex/Raw/Fuzzy/VanityNumber/ELEPHANT",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_REGEX_RAW,
				".*[defDEF3]"            /* E */
				"[^\\da-z]*[jklJKL5]"    /* L */
				"[^\\da-z]*[defDEF3]"    /* E */
				"[^\\da-z]*[pqrsPQRS7]"  /* P */
				"[^\\da-z]*[ghiGHI4]"    /* H */
				"[^\\da-z]*[abcABC2]"    /* A */
				"[^\\da-z]*[mnoMNO6]"    /* N */
				"[^\\da-z]*[tuvTUV8].*"),/* T */
			3,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		/*********************************************
		 *      TRANSLITERATION QUERIES FOLLOW       *
		 *********************************************/

		/* We have one contact that is "Jim Morrison" and
		 * another which is "警察 懂吗" (which transliterates
		 * to "Jing cha Dong ma", which means "Police do you understand").
		 *
		 * This test tries to fetch 2 contacts beginning with '几' which
		 * transliterates to 'jǐ'.
		 *
		 * So here we assert that when searching for contacts which begin
		 * with a transliterated "几" match both "Jim Morrison" and
		 * "警察 懂吗"
		 */
		add_client_test (
			suites[i].prefix,
			"/Transliterated/Prefix/FullName/几",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_FULL_NAME,
				E_BOOK_QUERY_TRANSLIT_BEGINS_WITH,
				"几"),
			2,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		/* Same results as above, only the query input is provided in Latin script */
		add_client_test (
			suites[i].prefix,
			"/Transliterated/Prefix/FullName/Ji",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_FULL_NAME,
				E_BOOK_QUERY_TRANSLIT_BEGINS_WITH,
				"Ji"),
			2,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		/* Check if anything contains "Ma", this should
		 * only find one match for "警察 懂吗" */
		add_client_test (
			suites[i].prefix,
			"/Transliterated/Contains/FullName/Ma",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_FULL_NAME,
				E_BOOK_QUERY_TRANSLIT_CONTAINS,
				"Ma"),
			1,
			suites[i].direct,
			suites[i].custom,
			FALSE);


		add_client_test (
			suites[i].prefix,
			"/Transliterated/Contains/FullName/Ma",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_FULL_NAME,
				E_BOOK_QUERY_TRANSLIT_CONTAINS,
				"Ma"),
			1,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Transliterated/Suffix/FullName/Cha",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_FULL_NAME,
				E_BOOK_QUERY_TRANSLIT_ENDS_WITH,
				"Cha"),
			1,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Transliterated/Prefix/Email/几",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_EMAIL,
				E_BOOK_QUERY_TRANSLIT_BEGINS_WITH,
				"几"),
			2,
			suites[i].direct,
			suites[i].custom,
			FALSE);

	}

	ret = e_test_server_utils_run ();

	return ret;
}

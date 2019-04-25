/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2012,2013 Intel Corporation
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 *          Mathias Hasselmann <mathias@openismus.com>
 */

#include "evolution-data-server-config.h"

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
		E_CONTACT_EMAIL, E_BOOK_INDEX_PREFIX,
		E_CONTACT_TEL, E_BOOK_INDEX_SUFFIX,
		E_CONTACT_TEL, E_BOOK_INDEX_PHONE,
		E_CONTACT_FULL_NAME, E_BOOK_INDEX_PREFIX,
		E_CONTACT_FULL_NAME, E_BOOK_INDEX_SUFFIX,
		E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_PREFIX,
		E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_SUFFIX,
		0);
}

static ETestServerClosure setup_custom_closure = { E_TEST_SERVER_ADDRESS_BOOK, setup_custom_book, 0, TRUE, NULL };
static ETestServerClosure setup_default_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, TRUE, NULL };

/* Filter which tests we want to try with a regexp */
static GRegex *test_regex = NULL;
static gchar  *test_filter = NULL;

static GOptionEntry entries[] = {

	{ "filter", 'f', 0, G_OPTION_ARG_STRING, &test_filter,
	  "A regular expression to filter which tests should be added", NULL },
	{ NULL }
};

/* Define this macro to expect E_CLIENT_ERROR_NOT_SUPPORTED
 * only on phone number queries when EDS is built with no
 * phone number support.
 */
#ifdef ENABLE_PHONENUMBER
#  define CHECK_UNSUPPORTED_ERROR(data) (FALSE)
#else
#  define CHECK_UNSUPPORTED_ERROR(data) (((ClientTestData *)(data))->phone_number_query != FALSE)
#endif

#define N_CONTACTS 15

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

		vcard = new_vcard_from_test_case (case_name);
		contact = e_contact_new_from_vcard (vcard);
		contacts = g_slist_prepend (contacts, contact);
		g_free (vcard);
		g_free (case_name);
	}

	if (!e_book_client_add_contacts_sync (book_client, contacts, E_BOOK_OPERATION_FLAG_NONE, NULL, NULL, &error)) {

		/* Dont complain here, we may re-use the same addressbook for multiple tests
		 * and we can't add the same contacts twice
		 */
		if (g_error_matches (error, E_BOOK_CLIENT_ERROR,
				     E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS))
			g_clear_error (&error);
		else
			g_error ("Failed to add test contacts: %s", error->message);

	}

	g_slist_free_full (contacts, (GDestroyNotify) g_object_unref);
}

static void
setup_test (ClientTestFixture *fixture,
            gconstpointer user_data)
{
	setup_book (fixture);
}

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

	if (test_regex)
		setup_book (fixture);
}

static void
client_test_setup_default (ClientTestFixture *fixture,
                           gconstpointer user_data)
{
	fixture->parent.source_name = g_strdup ("default-book");
	e_test_server_utils_setup (&fixture->parent, user_data);

	if (test_regex)
		setup_book (fixture);
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

	/* Filter out anything that was not specified in the test filter */
	if (test_regex && !g_regex_match (test_regex, path, 0, NULL)) {
		g_free (path);
		return;
	}

	if (custom)
		data->parent.customize = setup_custom_book;

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
	GOptionContext *context;
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

	/* Parse our regex first */
	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (test_filter)
		test_regex = g_regex_new (test_filter, 0, 0, NULL);

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	/* Change environment so that the addressbook factory inherits this setting */
	if (!g_setenv ("LC_ALL", "en_US.UTF-8", TRUE)) {
		g_warn_if_reached ();
		return 1;
	}
	setlocale (LC_ALL, "");

#if defined (LC_ADDRESS)
	/* LC_ADDRESS is a GNU extension. */
	g_assert_cmpstr (setlocale (LC_ADDRESS, NULL), ==, "en_US.UTF-8");
#endif

	/* Before beginning, setup two books and populate them with contacts, one with
	 * a customized summary and another without a customized summary
	 */
	if (test_regex == NULL) {
		g_test_add (
			"/EBookClient/SetupDefaultBook", ClientTestFixture, &setup_default_closure,
			client_test_setup_default, setup_test, client_test_teardown);
		g_test_add (
			"/EBookClient/SetupCustomBook", ClientTestFixture, &setup_custom_closure,
			client_test_setup_custom, setup_test, client_test_teardown);
	}

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

		/* Special case should not be a fallback query */
		add_client_test (
			suites[i].prefix,
			"/AnyFieldContains/NULL",
			suites[i].func,
			e_book_query_any_field_contains (NULL),
			N_CONTACTS,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		/* Special case should not be a fallback query */
		add_client_test (
			suites[i].prefix,
			"/AnyFieldContains/\"\"",
			suites[i].func,
			e_book_query_any_field_contains (""),
			N_CONTACTS,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		/* Evolution's addressbook autocompletion search.
		 * This should ideally be indexed, and should *definitely*
		 * not be a fallback query. It should also correctly
		 * return results for which there is no email address
		 * listed. */
		add_client_test (
			suites[i].prefix,
			"/Autocomplete",
			suites[i].func,
			e_book_query_orv (
				e_book_query_field_test (
					E_CONTACT_NICKNAME,
					E_BOOK_QUERY_BEGINS_WITH,
					"P"),
				e_book_query_field_test (
					E_CONTACT_EMAIL,
					E_BOOK_QUERY_BEGINS_WITH,
					"P"),
				e_book_query_field_test (
					E_CONTACT_FULL_NAME,
					E_BOOK_QUERY_BEGINS_WITH,
					"P"),
				e_book_query_field_test (
					E_CONTACT_FILE_AS,
					E_BOOK_QUERY_BEGINS_WITH,
					"P"),
				NULL),
			3,
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

		add_client_test (
			suites[i].prefix,
			"/Prefix/FullName/Underscore",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_FULL_NAME,
				E_BOOK_QUERY_CONTAINS,
				"ran_ge"),
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

		add_client_test (
			suites[i].prefix,
			"/Or/SearchByUID",
			suites[i].func,
			e_book_query_orv (
				e_book_query_field_test (
					E_CONTACT_UID,
					E_BOOK_QUERY_IS,
					"custom-1"),
				e_book_query_field_test (
					E_CONTACT_UID,
					E_BOOK_QUERY_IS,
					"custom-2"),
				e_book_query_field_test (
					E_CONTACT_UID,
					E_BOOK_QUERY_IS,
					"custom-3"),
				/* This one has a capital C, test will fail
				 * if the backend mistakenly normalizes the
				 * UID for comparison.
				 */
				e_book_query_field_test (
					E_CONTACT_UID,
					E_BOOK_QUERY_IS,
					"Custom-4"),
				e_book_query_field_test (
					E_CONTACT_UID,
					E_BOOK_QUERY_IS,
					"custom-5"),
				NULL),
			5,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Or/TwoEmails",
			suites[i].func,
			e_book_query_orv (
				e_book_query_field_test (
					E_CONTACT_EMAIL,
					E_BOOK_QUERY_ENDS_WITH,
					"jackson.com"),
				e_book_query_field_test (
					E_CONTACT_EMAIL,
					E_BOOK_QUERY_ENDS_WITH,
					".org"),
				NULL),
			4,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Exact/Email",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_EMAIL,
				E_BOOK_QUERY_IS,
				"micheal@jackson.com"),
			1,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Not/JacksonFamily",
			suites[i].func,
			e_book_query_not (
				e_book_query_field_test (
					E_CONTACT_FULL_NAME,
					E_BOOK_QUERY_ENDS_WITH,
					"jackson"),
				TRUE),
			/* There are 2 jackson contacts */
			N_CONTACTS - 2,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Not/DotComEmail",
			suites[i].func,
			e_book_query_not (
				e_book_query_field_test (
					E_CONTACT_EMAIL,
					E_BOOK_QUERY_ENDS_WITH,
					".com"),
				TRUE),
			/* There are 9 contacts with emails ending in ".com"  */
			N_CONTACTS - 9,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/And/EmailFullName",
			suites[i].func,
			e_book_query_andv (
				/* There are 9 contacts with emails ending in ".com"  */
				e_book_query_field_test (
					E_CONTACT_EMAIL,
					E_BOOK_QUERY_ENDS_WITH,
					".com"),
				/* Contacts custom-1 and custom-2 have Jackson in the full name */
				e_book_query_field_test (
					E_CONTACT_FULL_NAME,
					E_BOOK_QUERY_CONTAINS,
					"Jackson"),
				NULL),
			2,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/And/EmailEmail",
			suites[i].func,
			e_book_query_andv (
				/* There are 9 contacts with emails ending in ".com"  */
				e_book_query_field_test (
					E_CONTACT_EMAIL,
					E_BOOK_QUERY_ENDS_WITH,
					".com"),
				/* Contacts custom-1 and custom-2 have Jackson in the email */
				e_book_query_field_test (
					E_CONTACT_EMAIL,
					E_BOOK_QUERY_CONTAINS,
					"jackson"),
				NULL),
			2,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/And/EmailPhone",
			suites[i].func,
			e_book_query_andv (
				/* custom-13 begins with eddie */
				e_book_query_field_test (
					E_CONTACT_EMAIL,
					E_BOOK_QUERY_BEGINS_WITH,
					"eddie"),
				/* custom-13, custom-14 & custom-15 end with 5050 */
				e_book_query_field_test (
					E_CONTACT_TEL,
					E_BOOK_QUERY_ENDS_WITH,
					"5050"),
				NULL),
			1,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/And/EmailPhoneFamiliy",
			suites[i].func,
			e_book_query_andv (
				/* custom-13 family name is Murphey */
				e_book_query_field_test (
					E_CONTACT_FAMILY_NAME,
					E_BOOK_QUERY_IS,
					"Murphey"),
				/* custom-13 begins with eddie */
				e_book_query_field_test (
					E_CONTACT_EMAIL,
					E_BOOK_QUERY_BEGINS_WITH,
					"eddie"),
				/* custom-13, custom-14 & custom-15 end with 5050 */
				e_book_query_field_test (
					E_CONTACT_TEL,
					E_BOOK_QUERY_ENDS_WITH,
					"5050"),
				NULL),
			1,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/And/Familiy/Or/EmailEmail",
			suites[i].func,
			e_book_query_andv (
				/* Contacts custom-1 and custom-2 have Jackson in the full name */
				e_book_query_field_test (
					E_CONTACT_FULL_NAME,
					E_BOOK_QUERY_CONTAINS,
					"Jackson"),
				e_book_query_orv (
					/* There are 9 contacts with emails ending in ".com"  */
					e_book_query_field_test (
						E_CONTACT_EMAIL,
						E_BOOK_QUERY_ENDS_WITH,
						".com"),
					/* Contacts custom-1 and custom-2 have Jackson in the email */
					e_book_query_field_test (
						E_CONTACT_EMAIL,
						E_BOOK_QUERY_CONTAINS,
						"jackson"),
					NULL),
				NULL),
			2,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Exists/Email",
			suites[i].func,
			e_book_query_field_exists (
				E_CONTACT_EMAIL),
			/* There are 13 contacts with email addresses */
			13,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		add_client_test (
			suites[i].prefix,
			"/Exists/X509",
			suites[i].func,
			e_book_query_field_exists (
				E_CONTACT_X509_CERT),
			/* There is 1 contact with a cert listed */
			1,
			suites[i].direct,
			suites[i].custom,
			FALSE);

		/*********************************************
		 *                PHONE NUMBERS              *
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

		/* This test checks that phone number matching works when
		 * deeply nested into a query, when ENABLE_PHONENUMBER is
		 * not defined, then it ensures that the query is refused
		 * while being deeply nested. */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/NestedQuery",
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
		 *      E_BOOK_QUERY_EQUALS_PHONE_NUMBER     *
		 *********************************************/
		/* Only exact matches are returned.
		 *
		 * Query: +1 221.542.3789
		 * +------------------------------+--------------------+
		 * | vCard Data:   +1-221-5423789 | Matches: Exact     |
		 * | vCard Data:  +31-221-5423789 | Matches: None      |
		 * +------------------------------+--------------------+
		 */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/Exact",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_PHONE_NUMBER,
				"+1 221.542.3789"),
			1,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/*
		 * Query: +49 408.765.5050 (one exact match)
		 * +------------------------------+--------------------+
		 * | vCard Data:     408 765-5050 | Matches: National  |
		 * | vCard Data:  +1 408 765-5050 | Matches: None      |
		 * | vCard Data: +49 408 765-5050 | Matches: Exact     |
		 * +------------------------------+--------------------+
		 */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/Exact/Another",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_PHONE_NUMBER,
				"+49 408.765.5050"),
			1,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/*
		 * Query: 408.765.5050 (no exact match)
		 * +------------------------------+--------------------+
		 * | vCard Data:     408 765-5050 | Matches: National  |
		 * | vCard Data:  +1 408 765-5050 | Matches: National  |
		 * | vCard Data: +49 408 765-5050 | Matches: National  |
		 * +------------------------------+--------------------+
		 */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/Exact/MissingCountryInput",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_PHONE_NUMBER,
				"408.765.5050"),
			0,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/*********************************************
		 * E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER *
		 *********************************************/
		/* Test that a query term with no specified country returns
		 * all vCards that have the same national number regardless
		 * of country codes (including contacts which have no
		 * national number specified)
		 *
		 * Query: 408 765-5050
		 * +------------------------------+--------------------+
		 * | vCard Data:     408 765-5050 | Matches: National  |
		 * | vCard Data:  +1 408 765-5050 | Matches: National  |
		 * | vCard Data: +49 408 765-5050 | Matches: National  |
		 * +------------------------------+--------------------+
		 */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/National/WithoutCountry",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER,
				"408 765-5050"),
			3,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/* Test that a query term with no specified country returns
		 * all vCards that have the same national number regardless
		 * of country codes.
		 *
		 * Query: 221.542.3789
		 * +------------------------------+--------------------+
		 * | vCard Data:   +1-221-5423789 | Matches: National  |
		 * | vCard Data:  +31-221-5423789 | Matches: National  |
		 * +------------------------------+--------------------+
		 */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/National/WithoutCountry2",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER,
				"221.542.3789"),
			2,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/* Test that querying with an explicit country code reports
		 * national number matches for numbers without a country
		 * code, and not for numbers with explicitly different
		 * country codes.
		 *
		 * Query: +1 408 765-5050
		 * +------------------------------+--------------------+
		 * | vCard Data:     408 765-5050 | Matches: National  |
		 * | vCard Data:  +1 408 765-5050 | Matches: Exact     |
		 * | vCard Data: +49 408 765-5050 | Matches: None      |
		 * +------------------------------+--------------------+
		 */
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

		/* Query: +49 408 765-5050
		 * +------------------------------+--------------------+
		 * | vCard Data:     408 765-5050 | Matches: National  |
		 * | vCard Data:  +1 408 765-5050 | Matches: None      |
		 * | vCard Data: +49 408 765-5050 | Matches: Exact     |
		 * +------------------------------+--------------------+
		 */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/National/de_DE",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER,
				"+49 408 765-5050"),
			2,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/* Test that a query term with a specified country returns
		 * only vCards that are specifically in the specified country
		 * code.
		 *
		 * Query: +49 221.542.3789
		 * +------------------------------+--------------------+
		 * | vCard Data:   +1-221-5423789 | Matches: None      |
		 * | vCard Data:  +31-221-5423789 | Matches: None      |
		 * +------------------------------+--------------------+
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

		/* Test that a query term with a country code
		 * specified returns a vCard with an unspecified country code.
		 *
		 * Query: +1 514-845-8436
		 * +------------------------------+--------------------+
		 * | vCard Data:     514-845-8436 | Matches: National  |
		 * +------------------------------+--------------------+
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

		/* Test that a query term with another country code
		 * specified again returns a vCard with an unspecified
		 * country code.
		 *
		 * This test can help make sure that we are properly
		 * ignoring whatever country code is active by default
		 * in our locale.
		 *
		 * Query: +49 514-845-8436
		 * +------------------------------+--------------------+
		 * | vCard Data:     514-845-8436 | Matches: National  |
		 * +------------------------------+--------------------+
		 */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/National/CountryAbsent/QueryOtherCountry",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER,
				"+49 514-845-8436"),
			1,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/********************************************
		 *  E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER  *
		 ********************************************/
		/*
		 * Query: 5423789
		 * +------------------------------+--------------------+
		 * | vCard Data:   +1-221-5423789 | Matches: Short     |
		 * | vCard Data:  +31-221-5423789 | Matches: Short     |
		 * +------------------------------+--------------------+
		 */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/Short",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER,
				"5423789"),
			2,
			suites[i].direct,
			suites[i].custom,
			TRUE);

		/*
		 * Query: 765-5050
		 * +------------------------------+--------------------+
		 * | vCard Data:     408 765-5050 | Matches: Short     |
		 * | vCard Data:  +1 408 765-5050 | Matches: Short     |
		 * | vCard Data: +49 408 765-5050 | Matches: Short     |
		 * +------------------------------+--------------------+
		 */
		add_client_test (
			suites[i].prefix,
			"/EqPhone/Short/Another",
			suites[i].func,
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER,
				"765-5050"),
			3,
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
	}

	ret = e_test_server_utils_run ();

	return ret;
}

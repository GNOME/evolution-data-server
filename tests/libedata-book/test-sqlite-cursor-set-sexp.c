/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "data-test-utils.h"

/* Optimize queries, just so we can run with BOOKSQL_DEBUG=2 and check the
 * indexes are properly leverage for cursor queries
 */
static void
setup_custom_book (ESource            *scratch,
		   ETestServerClosure *closure)
{
	ESourceBackendSummarySetup *setup;

	g_type_class_unref (g_type_class_ref (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP));
	setup = e_source_get_extension (scratch, E_SOURCE_EXTENSION_BACKEND_SUMMARY_SETUP);
	e_source_backend_summary_setup_set_summary_fields (setup,
							   E_CONTACT_FAMILY_NAME,
							   E_CONTACT_GIVEN_NAME,
							   E_CONTACT_EMAIL,
							   0);
	e_source_backend_summary_setup_set_indexed_fields (setup,
							   E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_PREFIX,
							   E_CONTACT_GIVEN_NAME, E_BOOK_INDEX_PREFIX,
							   E_CONTACT_EMAIL, E_BOOK_INDEX_PREFIX,
							   0);
}

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, setup_custom_book, 0 };

typedef struct {
	ESqliteDBFixture parent_fixture;

	EbSdbCursor  *cursor;

	EContact *contacts[11];
} CursorFixture;

static void
cursor_fixture_setup (CursorFixture *fixture,
		      gconstpointer  user_data)
{
	EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
	EBookSortType sort_types[] = { E_BOOK_SORT_ASCENDING, E_BOOK_SORT_ASCENDING };
	GError       *error = NULL;
	EBookClient  *book_client;
	EContact    **it = fixture->contacts;

	e_sqlitedb_fixture_setup ((ESqliteDBFixture *)fixture, user_data);

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	/* Add contacts... */
	if (/* N:Jackson;Micheal */
	    !add_contact_from_test_case_verify (book_client, "sorted-1", it++) ||
	    /* N:Jackson;Janet */
	    !add_contact_from_test_case_verify (book_client, "sorted-2", it++) ||
	    /* N:Brown;Bobby */
	    !add_contact_from_test_case_verify (book_client, "sorted-3", it++) ||
	    /* N:Brown;Big Bobby */
	    !add_contact_from_test_case_verify (book_client, "sorted-4", it++) ||
	    /* N:Brown;James */
	    !add_contact_from_test_case_verify (book_client, "sorted-5", it++) ||
	    /* N:%Strange Name;Mister */
	    !add_contact_from_test_case_verify (book_client, "sorted-6", it++) ||
	    /* N:Goose;Purple */
	    !add_contact_from_test_case_verify (book_client, "sorted-7", it++) ||
	    /* N:Pony;Purple */
	    !add_contact_from_test_case_verify (book_client, "sorted-8", it++) ||
	    /* N:Pony;Pink */
	    !add_contact_from_test_case_verify (book_client, "sorted-9", it++) ||
	    /* N:J;Mister */
	    !add_contact_from_test_case_verify (book_client, "sorted-10", it++) ||
	    /* FN:Ye Nameless One */
	    !add_contact_from_test_case_verify (book_client, "sorted-11", it++)) {
		g_error ("Failed to add contacts");
	}

	fixture->cursor = e_book_backend_sqlitedb_cursor_new (((ESqliteDBFixture *) fixture)->ebsdb,
							      SQLITEDB_FOLDER_ID,
							      NULL, sort_fields, sort_types, 2, &error);

	g_assert (fixture->cursor != NULL);
}

static void
cursor_fixture_teardown (CursorFixture *fixture,
			 gconstpointer  user_data)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (fixture->contacts); ++i) {
		if (fixture->contacts[i])
			g_object_unref (fixture->contacts[i]);
	}

	e_book_backend_sqlitedb_cursor_free (((ESqliteDBFixture *) fixture)->ebsdb, fixture->cursor);
	e_sqlitedb_fixture_teardown ((ESqliteDBFixture *)fixture, user_data);
}

static void
test_cursor_sexp_invalid (CursorFixture *fixture,
			  gconstpointer  user_data)
{
	GError *error = NULL;
	EBookQuery *query;
	gchar *sexp = NULL;

	query = e_book_query_field_test (E_CONTACT_NICKNAME, E_BOOK_QUERY_BEGINS_WITH, "Kung Fu");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	if (e_book_backend_sqlitedb_cursor_set_sexp (((ESqliteDBFixture *) fixture)->ebsdb,
						     fixture->cursor, sexp, &error))
		g_error ("Succeeded in setting non-summarized field in the cursor query expression");

	g_assert (error);
	g_assert (g_error_matches (error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_INVALID_QUERY));
}

static void
test_cursor_sexp_calculate_position (CursorFixture *fixture,
				     gconstpointer  user_data)
{
	GError *error = NULL;
	EBookQuery *query;
	gint    position = 0, total = 0;
	gchar *sexp = NULL;

	/* Set the cursor to point exactly to Bobby Brown */
	e_book_backend_sqlitedb_cursor_set_target_contact (((ESqliteDBFixture *) fixture)->ebsdb,
							   fixture->cursor,
							   fixture->contacts[2]);

	/* Check position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* Bobby Brown is at position 3, contacts <= Bobby Brown are:
	 *   N:Brown;Bobby
	 *   N:Brown;Big Bobby
	 *   FN:Ye Nameless One
	 */
	g_assert_cmpint (position, ==, 3);
	g_assert_cmpint (total, ==, 11);

	/* Set new sexp, only contacts with .com email addresses */
	query = e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, ".com");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	if (!e_book_backend_sqlitedb_cursor_set_sexp (((ESqliteDBFixture *) fixture)->ebsdb,
						      fixture->cursor, sexp, &error))
		g_error ("Failed to set sexp: %s", error->message);

	/* Check new position after modified sexp */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* Bobby Brown is at position 2, filtered contacts <= Bobby Brown are:
	 *   "Bobby Brown"
	 *   "Ye Nameless One"
	 */
	g_assert_cmpint (position, ==, 2);
	g_assert_cmpint (total, ==, 8);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	/* Ensure that the client and server get the same locale */
	g_assert (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

	g_test_add ("/EbSdbCursor/SetSexp/Invalid", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_sexp_invalid, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/SetSexp/CalculatePosition", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_sexp_calculate_position, cursor_fixture_teardown);

	return e_test_server_utils_run ();
}

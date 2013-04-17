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

	EBookQuery *query;
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
	gchar        *sexp = NULL;

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

	/* Allow a surrounding fixture setup to add a query here */
	if (fixture->query) {
		sexp = e_book_query_to_string (fixture->query);
		e_book_query_unref (fixture->query);
		fixture->query = NULL;
	}

	fixture->cursor = e_book_backend_sqlitedb_cursor_new (((ESqliteDBFixture *) fixture)->ebsdb,
							      SQLITEDB_FOLDER_ID,
							      sexp, sort_fields, sort_types, 2, &error);


	g_free (sexp);

	g_assert (fixture->cursor != NULL);
}

static void
test_cursor_move_filtered_setup (CursorFixture *fixture,
				 gconstpointer  user_data)
{
	fixture->query = e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, ".com");

	cursor_fixture_setup (fixture, user_data);
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
test_cursor_calculate_initial (CursorFixture *fixture,
			       gconstpointer  user_data)
{
	GError *error = NULL;
	gint    position = 0, total = 0;

	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	g_assert_cmpint (position, ==, 0);
	g_assert_cmpint (total, ==, 11);
}

static void
test_cursor_calculate_move_forward (CursorFixture *fixture,
				    gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;
	gint    position = 0, total = 0;

	/* Move cursor */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	g_assert_cmpint (g_slist_length (results), ==, 5);
	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	/* results 0 + 5 = position 5, result index 4 (results[0, 1, 2, 3, 4]) */
	g_assert_cmpint (position, ==, 5);
	g_assert_cmpint (total, ==, 11);
}

static void
test_cursor_calculate_move_backwards (CursorFixture *fixture,
				      gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;
	gint    position = 0, total = 0;

	/* Move cursor */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, -5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	g_assert_cmpint (g_slist_length (results), ==, 5);
	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	/* results 11 - 5 = position 7 result index 6 (results[10, 9, 8, 7, 6]) */
	g_assert_cmpint (position, ==, 7);
	g_assert_cmpint (total, ==, 11);
}

static void
test_cursor_calculate_back_and_forth (CursorFixture *fixture,
				      gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;
	gint    position = 0, total = 0;

	/* Move cursor */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 7, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	g_assert_cmpint (g_slist_length (results), ==, 7);
	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	/* results 0 + 7 = position 7 result index 6 (results[0, 1, 2, 3, 4, 5, 6]) */
	g_assert_cmpint (position, ==, 7);
	g_assert_cmpint (total, ==, 11);

	/* Move cursor */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, -4, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	g_assert_cmpint (g_slist_length (results), ==, 4);
	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	/* results 7 - 4 = position 3 result index 2 (results[5, 4, 3, 2]) */
	g_assert_cmpint (position, ==, 3);
	g_assert_cmpint (total, ==, 11);

	/* Move cursor */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	g_assert_cmpint (g_slist_length (results), ==, 5);
	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	/* results 3 + 5 = position 8 result index 7 (results[3, 4, 5, 6, 7]) */
	g_assert_cmpint (position, ==, 8);
	g_assert_cmpint (total, ==, 11);
}

static void
test_cursor_calculate_exact_target (CursorFixture *fixture,
				    gconstpointer  user_data)
{
	GError *error = NULL;
	gint    position = 0, total = 0;

	/* Set the cursor to point exactly to Bobby Brown */
	e_book_backend_sqlitedb_cursor_set_target_contact (((ESqliteDBFixture *) fixture)->ebsdb,
							   fixture->cursor,
							   fixture->contacts[2]);

	/* Check new position */
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
}

static void
test_cursor_calculate_partial_target (CursorFixture *fixture,
				      gconstpointer  user_data)
{
	GError *error = NULL;
	gint    position = 0, total = 0;

	/* Set the cursor to point to the beginning of "J" */
	e_book_backend_sqlitedb_cursor_set_target (((ESqliteDBFixture *) fixture)->ebsdb,
						   fixture->cursor, "J", NULL);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* Position is 5, contacts before the letter J are:
	 *   N:Goose;Purple
	 *   N:Brown;Big Bobby
	 *   N:Brown;Bobby
	 *   N:Brown;James
	 *   FN:Ye Nameless One
	 */
	g_assert_cmpint (position, ==, 5);
	g_assert_cmpint (total, ==, 11);
}

static void
test_cursor_calculate_after_modification (CursorFixture *fixture,
					  gconstpointer  user_data)
{
	EBookClient  *book_client;
	GError *error = NULL;
	gint    position = 0, total = 0;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	/* Set the cursor to point exactly to Bobby Brown */
	e_book_backend_sqlitedb_cursor_set_target_contact (((ESqliteDBFixture *) fixture)->ebsdb,
							   fixture->cursor,
							   fixture->contacts[2]);

	/* Rename Micheal Jackson -> Jacob Appelbaum */
	e_contact_set (fixture->contacts[0], E_CONTACT_FAMILY_NAME, "Appelbaum");
	e_contact_set (fixture->contacts[0], E_CONTACT_GIVEN_NAME, "Jacob");
	if (!e_book_client_modify_contact_sync (book_client, fixture->contacts[0], NULL, &error))
		g_error ("modify contact sync: %s", error->message);

	/* Rename Janet Jackson -> Sade Adu */
	e_contact_set (fixture->contacts[1], E_CONTACT_FAMILY_NAME, "Adu");
	e_contact_set (fixture->contacts[1], E_CONTACT_GIVEN_NAME, "Sade");
	if (!e_book_client_modify_contact_sync (book_client, fixture->contacts[1], NULL, &error))
		g_error ("modify contact sync: %s", error->message);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* Bobby Brown is at position 5, contacts <= Bobby Brown are:
	 *   N:Brown;Bobby
	 *   N:Brown;Big Bobby
	 *   N:Jacob;Appelbaum
	 *   N:Sade;Adu
	 *   FN:Ye Nameless One
	 */
	g_assert_cmpint (position, ==, 5);
	g_assert_cmpint (total, ==, 11);
}

static void
test_cursor_calculate_filtered_initial (CursorFixture *fixture,
					gconstpointer  user_data)
{
	GError *error = NULL;
	gint    position = 0, total = 0;

	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	g_assert_cmpint (position, ==, 0);
	g_assert_cmpint (total, ==, 8);
}

static void
test_cursor_calculate_filtered_move_forward (CursorFixture *fixture,
					     gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;
	gint    position = 0, total = 0;

	/* Move cursor */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	g_assert_cmpint (g_slist_length (results), ==, 5);
	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	/* results 0 + 5 = position 5, result index 4 (results[0, 1, 2, 3, 4]) */
	g_assert_cmpint (position, ==, 5);
	g_assert_cmpint (total, ==, 8);
}

static void
test_cursor_calculate_filtered_move_backwards (CursorFixture *fixture,
					       gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;
	gint    position = 0, total = 0;

	/* Move cursor */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, -5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	g_assert_cmpint (g_slist_length (results), ==, 5);
	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	/* results 8 - 5 = position 4 result index 3 (results[7, 6, 5, 4, 3]) */
	g_assert_cmpint (position, ==, 4);
	g_assert_cmpint (total, ==, 8);
}

static void
test_cursor_calculate_filtered_exact_target (CursorFixture *fixture,
					     gconstpointer  user_data)
{
	GError *error = NULL;
	gint    position = 0, total = 0;

	/* Set the cursor to point exactly to Bobby Brown */
	e_book_backend_sqlitedb_cursor_set_target_contact (((ESqliteDBFixture *) fixture)->ebsdb,
							   fixture->cursor,
							   fixture->contacts[2]);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* Bobby Brown is at position 2, filtered contacts <= Bobby Brown are:
	 *   Bobby Brown
	 *   Ye Nameless One
	 */
	g_assert_cmpint (position, ==, 2);
	g_assert_cmpint (total, ==, 8);
}

static void
test_cursor_calculate_filtered_partial_target (CursorFixture *fixture,
					       gconstpointer  user_data)
{
	GError *error = NULL;
	gint    position = 0, total = 0;

	/* Set the cursor to point to the beginning of "J" */
	e_book_backend_sqlitedb_cursor_set_target (((ESqliteDBFixture *) fixture)->ebsdb,
						   fixture->cursor, "J", NULL);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* Position is 3, filtered contacts before the letter J are:
	 *   FN:Ye Nameless One
	 *   N:Brown;Bobby
	 *   N:Brown;James
	 */
	g_assert_cmpint (position, ==, 3);
	g_assert_cmpint (total, ==, 8);
}

static void
test_cursor_calculate_filtered_after_modification (CursorFixture *fixture,
						   gconstpointer  user_data)
{
	EBookClient  *book_client;
	GError *error = NULL;
	gint    position = 0, total = 0;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	/* Set the cursor to point exactly to Bobby Brown */
	e_book_backend_sqlitedb_cursor_set_target_contact (((ESqliteDBFixture *) fixture)->ebsdb,
							   fixture->cursor,
							   fixture->contacts[2]);

	/* Rename Micheal Jackson -> Jacob Appelbaum */
	e_contact_set (fixture->contacts[0], E_CONTACT_FAMILY_NAME, "Appelbaum");
	e_contact_set (fixture->contacts[0], E_CONTACT_GIVEN_NAME, "Jacob");
	if (!e_book_client_modify_contact_sync (book_client, fixture->contacts[0], NULL, &error))
		g_error ("modify contact sync: %s", error->message);

	/* Rename Janet Jackson -> Sade Adu */
	e_contact_set (fixture->contacts[1], E_CONTACT_FAMILY_NAME, "Adu");
	e_contact_set (fixture->contacts[1], E_CONTACT_GIVEN_NAME, "Sade");
	if (!e_book_client_modify_contact_sync (book_client, fixture->contacts[1], NULL, &error))
		g_error ("modify contact sync: %s", error->message);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* Bobby Brown is at position 4, filtered contacts <= Bobby Brown are:
	 *   N:Brown;Bobby
	 *   N:Jacob;Appelbaum
	 *   N:Sade;Adu
	 *   FN:Ye Nameless One
	 */
	g_assert_cmpint (position, ==, 4);
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

	g_test_add ("/EbSdbCursor/Calculate/Initial", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_calculate_initial, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/MoveForward", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_calculate_move_forward, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/MoveBackwards", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_calculate_move_backwards, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/BackAndForth", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_calculate_back_and_forth, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/ExactTarget", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_calculate_exact_target, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/PartialTarget", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_calculate_partial_target, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/AfterModification", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_calculate_after_modification, cursor_fixture_teardown);

	g_test_add ("/EbSdbCursor/Calculate/Filtered/Initial", CursorFixture, &book_closure,
		    test_cursor_move_filtered_setup,
		    test_cursor_calculate_filtered_initial, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/Filtered/MoveForward", CursorFixture, &book_closure,
		    test_cursor_move_filtered_setup,
		    test_cursor_calculate_filtered_move_forward, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/Filtered/MoveBackwards", CursorFixture, &book_closure,
		    test_cursor_move_filtered_setup,
		    test_cursor_calculate_filtered_move_backwards, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/Filtered/ExactTarget", CursorFixture, &book_closure,
		    test_cursor_move_filtered_setup,
		    test_cursor_calculate_filtered_exact_target, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/Filtered/PartialTarget", CursorFixture, &book_closure,
		    test_cursor_move_filtered_setup,
		    test_cursor_calculate_filtered_partial_target, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/Filtered/AfterModification", CursorFixture, &book_closure,
		    test_cursor_move_filtered_setup,
		    test_cursor_calculate_filtered_after_modification, cursor_fixture_teardown);

	return e_test_server_utils_run ();
}

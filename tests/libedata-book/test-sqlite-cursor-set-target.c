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

/*****************************************************
 *          Expect the same results twice            *
 *****************************************************/
static void
test_cursor_set_target_reset_cursor (CursorFixture *fixture,
				     gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;

	/* First batch */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	g_assert_cmpint (g_slist_length (results), ==, 5);

	/* Assert that we got the results ordered as:
	 *   "Big Bobby Brown"
	 *   "Bobby Brown"
	 *   "James Brown"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[3], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[2], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[4], E_CONTACT_UID),
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Reset cursor */
	e_book_backend_sqlitedb_cursor_set_target (((ESqliteDBFixture *) fixture)->ebsdb,
						   fixture->cursor,
						   NULL);

	/* Second batch */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	g_assert_cmpint (g_slist_length (results), ==, 5);

	/* Assert that we got the results ordered as:
	 *   "Big Bobby Brown"
	 *   "Bobby Brown"
	 *   "James Brown"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[3], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[2], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[4], E_CONTACT_UID),
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);
}

/*****************************************************
 * Expect results with family name starting with 'J' *
 *****************************************************/
static void
test_cursor_set_target_j_next_results (CursorFixture *fixture,
				       gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;

	/* Set the cursor at the start of family names beginning with 'J' */
	e_book_backend_sqlitedb_cursor_set_target (((ESqliteDBFixture *) fixture)->ebsdb,
						   fixture->cursor, "J", NULL);

	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 3, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	g_assert_cmpint (g_slist_length (results), ==, 3);

	/* Assert that we got the results ordered as:
	 *   "Mister J"
	 *   "Janet Jackson"
	 *   "Micheal Jackson"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[9], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[1], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[0], E_CONTACT_UID),
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);
}

/*****************************************************
 *       Expect results before the letter 'J'        *
 *****************************************************/
static void
test_cursor_set_target_j_prev_results (CursorFixture *fixture,
				       gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;

	/* Set the cursor at the start of family names beginning with 'J' */
	e_book_backend_sqlitedb_cursor_set_target (((ESqliteDBFixture *) fixture)->ebsdb,
						   fixture->cursor, "J", NULL);

	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, -5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	g_assert_cmpint (g_slist_length (results), ==, 5);

	/* Assert that we got the results ordered as:
	 *   "James Brown"
	 *   "Bobby Brown"
	 *   "Big Bobby Brown"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[4], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[2], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[3], E_CONTACT_UID),
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);
}

/*****************************************************
 *       Expect results after Bobby Brown            *
 *****************************************************/
static void
test_cursor_set_target_bobby_brown_next_results (CursorFixture *fixture,
						 gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;

	/* Set the cursor to point exactly to Bobby Brown */
	e_book_backend_sqlitedb_cursor_set_target_contact (((ESqliteDBFixture *) fixture)->ebsdb,
							   fixture->cursor,
							   fixture->contacts[2]);

	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	g_assert_cmpint (g_slist_length (results), ==, 5);

	/* Assert that we got the results ordered as:
	 *   "James Brown"
	 *   "Purple Goose"
	 *   "Mister J"
	 *   "Janet Jackson"
	 *   "Micheal Jackson"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[4], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[6], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[9], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[1], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[0], E_CONTACT_UID),
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);
}

/*****************************************************
 *       Expect results before Bobby Brown           *
 *****************************************************/
static void
test_cursor_set_target_bobby_brown_prev_results (CursorFixture *fixture,
						 gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;

	/* Set the cursor to point exactly to Bobby Brown */
	e_book_backend_sqlitedb_cursor_set_target_contact (((ESqliteDBFixture *) fixture)->ebsdb,
							   fixture->cursor,
							   fixture->contacts[2]);

	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, -5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	g_assert_cmpint (g_slist_length (results), ==, 2);

	/* Assert that we got the results ordered as:
	 *   "Big Bobby Brown"
	 *   "Ye Nameless One"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[3], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[10], E_CONTACT_UID),
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);
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

	g_test_add ("/EbSdbCursor/SetTarget/ResetCursor", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_set_target_reset_cursor, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/SetTarget/Partial-J/NextResults", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_set_target_j_next_results, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/SetTarget/Partial-J/PreviousResults", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_set_target_j_prev_results, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/SetTarget/Exactly-Bobby-Brown/NextResults", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_set_target_bobby_brown_next_results, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/SetTarget/Exactly-Bobby-Brown/PreviousResults", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_set_target_bobby_brown_prev_results, cursor_fixture_teardown);

	return e_test_server_utils_run ();
}

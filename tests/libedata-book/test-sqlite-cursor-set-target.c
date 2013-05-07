/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "data-test-utils.h"

static EbSdbCursorClosure book_closure = { { E_TEST_SERVER_ADDRESS_BOOK, e_sqlitedb_cursor_fixture_setup_book, 0 }, FALSE };

/*****************************************************
 *          Expect the same results twice            *
 *****************************************************/
static void
test_cursor_set_target_reset_cursor (EbSdbCursorFixture *fixture,
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

	/* Assert the first 5 contacts in en_US order */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	assert_contacts_order (results,
			       "sorted-11",
			       "sorted-1",
			       "sorted-2",
			       "sorted-5",
			       "sorted-6",
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Reset cursor */
	e_book_backend_sqlitedb_cursor_set_target (((ESqliteDBFixture *) fixture)->ebsdb,
						   fixture->cursor, NULL);

	/* Second batch */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	/* Assert the first 5 contacts in en_US order again */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	assert_contacts_order (results,
			       "sorted-11",
			       "sorted-1",
			       "sorted-2",
			       "sorted-5",
			       "sorted-6",
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);
}

/*****************************************************
 * Expect results with family name starting with 'C' *
 *****************************************************/
static void
test_cursor_set_target_c_next_results (EbSdbCursorFixture *fixture,
				       gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;

	/* Set the cursor at the start of family names beginning with 'C' */
	e_book_backend_sqlitedb_cursor_set_target (((ESqliteDBFixture *) fixture)->ebsdb,
						   fixture->cursor, "C", NULL);

	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	/* Assert that we got the results starting at C */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	assert_contacts_order (results,
			       "sorted-10",
			       "sorted-14",
			       "sorted-12",
			       "sorted-13",
			       "sorted-9",
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);
}

/*****************************************************
 *       Expect results before the letter 'C'        *
 *****************************************************/
static void
test_cursor_set_target_c_prev_results (EbSdbCursorFixture *fixture,
				       gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;

	/* Set the cursor at the start of family names beginning with 'J' */
	e_book_backend_sqlitedb_cursor_set_target (((ESqliteDBFixture *) fixture)->ebsdb,
						   fixture->cursor, "C", NULL);

	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, -5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	/* Assert that we got the results before C */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	assert_contacts_order (results,
			       "sorted-18",
			       "sorted-16",
			       "sorted-17",
			       "sorted-15",
			       "sorted-8",
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);
}

/*****************************************************
 *       Expect results after 'blackbird'            *
 *****************************************************/
static void
test_cursor_set_target_blackbird_next_results (EbSdbCursorFixture *fixture,
					       gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;

	/* Set the cursor to point exactly to Bobby Brown */
	e_book_backend_sqlitedb_cursor_set_target_contact (((ESqliteDBFixture *) fixture)->ebsdb,
							   fixture->cursor, fixture->contacts[16 -1]);

	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	/* Assert that we got the results after blackbird */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	assert_contacts_order (results,
			       "sorted-18",
			       "sorted-10",
			       "sorted-14",
			       "sorted-12",
			       "sorted-13",
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);
}

/*****************************************************
 *       Expect results before 'blackbird'           *
 *****************************************************/
static void
test_cursor_set_target_blackbird_prev_results (EbSdbCursorFixture *fixture,
					       gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;

	/* Set the cursor to point exactly to Bobby Brown */
	e_book_backend_sqlitedb_cursor_set_target_contact (((ESqliteDBFixture *) fixture)->ebsdb,
							   fixture->cursor, fixture->contacts[16 -1]);

	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, -5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	/* Assert that we got the results before blackbird */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	assert_contacts_order (results,
			       "sorted-17",
			       "sorted-15",
			       "sorted-8",
			       "sorted-7",
			       "sorted-3",
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
	g_assert (g_setenv ("EDS_COLLATE", "en_US.UTF-8", TRUE));

	g_test_add ("/EbSdbCursor/SetTarget/ResetCursor", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_setup,
		    test_cursor_set_target_reset_cursor,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/SetTarget/Partial/C/NextResults", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_setup,
		    test_cursor_set_target_c_next_results,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/SetTarget/Partial/C/PreviousResults", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_setup,
		    test_cursor_set_target_c_prev_results,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/SetTarget/Exact/blackbird/NextResults", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_setup,
		    test_cursor_set_target_blackbird_next_results,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/SetTarget/Exact/blackbird/PreviousResults", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_setup,
		    test_cursor_set_target_blackbird_prev_results,
		    e_sqlitedb_cursor_fixture_teardown);

	return e_test_server_utils_run ();
}

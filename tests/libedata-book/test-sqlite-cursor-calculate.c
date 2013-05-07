/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "data-test-utils.h"

static EbSdbCursorClosure book_closure = { { E_TEST_SERVER_ADDRESS_BOOK, e_sqlitedb_cursor_fixture_setup_book, 0 }, FALSE };

static void
test_cursor_calculate_initial (EbSdbCursorFixture *fixture,
			       gconstpointer  user_data)
{
	GError *error = NULL;
	gint    position = 0, total = 0;

	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	g_assert_cmpint (position, ==, 0);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_move_forward (EbSdbCursorFixture *fixture,
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

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	/* results 0 + 5 = position 5, result index 4 (results[0, 1, 2, 3, 4]) */
	g_assert_cmpint (position, ==, 5);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_move_backwards (EbSdbCursorFixture *fixture,
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

	/* Assert the last 5 contacts in en_US order */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	assert_contacts_order (results,
			       "sorted-20",
			       "sorted-19",
			       "sorted-9",
			       "sorted-13",
			       "sorted-12",
			       NULL);
	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	/* results 20 - 5 = position 16 result index 15 (results[20, 19, 18, 17, 16]) */
	g_assert_cmpint (position, ==, 16);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_back_and_forth (EbSdbCursorFixture *fixture,
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
	g_assert_cmpint (total, ==, 20);

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
	g_assert_cmpint (total, ==, 20);

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
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_exact_target (EbSdbCursorFixture *fixture,
				    gconstpointer  user_data)
{
	GError *error = NULL;
	gint    position = 0, total = 0;

	/* Set the cursor to point exactly to blackbird */
	e_book_backend_sqlitedb_cursor_set_target_contact (((ESqliteDBFixture *) fixture)->ebsdb,
							   fixture->cursor, fixture->contacts[16 - 1]);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* blackbird is at position 12 in en_US locale */
	g_assert_cmpint (position, ==, 12);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_partial_target (EbSdbCursorFixture *fixture,
				      gconstpointer  user_data)
{
	GError *error = NULL;
	gint    position = 0, total = 0;

	/* Set the cursor to point to the beginning of "C" */
	e_book_backend_sqlitedb_cursor_set_target (((ESqliteDBFixture *) fixture)->ebsdb,
						   fixture->cursor, "C", NULL);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* Position is 13, there are 13 contacts before the letter 'C' in en_US locale */
	g_assert_cmpint (position, ==, 13);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_after_modification (EbSdbCursorFixture *fixture,
					  gconstpointer  user_data)
{
	EBookClient  *book_client;
	GError *error = NULL;
	gint    position = 0, total = 0;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	/* Set the cursor to point exactly to 'blackbird' */
	e_book_backend_sqlitedb_cursor_set_target_contact (((ESqliteDBFixture *) fixture)->ebsdb,
							   fixture->cursor, fixture->contacts[16 - 1]);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* blackbird is at position 12 in en_US locale */
	g_assert_cmpint (position, ==, 12);
	g_assert_cmpint (total, ==, 20);

	/* Rename Muffler -> Jacob Appelbaum */
	e_contact_set (fixture->contacts[19 - 1], E_CONTACT_FAMILY_NAME, "Appelbaum");
	e_contact_set (fixture->contacts[19 - 1], E_CONTACT_GIVEN_NAME, "Jacob");
	if (!e_book_client_modify_contact_sync (book_client, fixture->contacts[19 - 1], NULL, &error))
		g_error ("modify contact sync: %s", error->message);

	/* Rename Müller -> Sade Adu */
	e_contact_set (fixture->contacts[20 - 1], E_CONTACT_FAMILY_NAME, "Adu");
	e_contact_set (fixture->contacts[20 - 1], E_CONTACT_GIVEN_NAME, "Sade");
	if (!e_book_client_modify_contact_sync (book_client, fixture->contacts[20 - 1], NULL, &error))
		g_error ("modify contact sync: %s", error->message);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* blackbird is now at position 14 after moving 2 later contacts to begin with 'A' */
	g_assert_cmpint (position, ==, 14);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_filtered_initial (EbSdbCursorFixture *fixture,
					gconstpointer  user_data)
{
	GError *error = NULL;
	gint    position = 0, total = 0;

	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	g_assert_cmpint (position, ==, 0);
	g_assert_cmpint (total, ==, 13);
}

static void
test_cursor_calculate_filtered_move_forward (EbSdbCursorFixture *fixture,
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
	g_assert_cmpint (total, ==, 13);
}

static void
test_cursor_calculate_filtered_move_backwards (EbSdbCursorFixture *fixture,
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

	/* results 13 - 5 = position 9 (results[13, 12, 11, 10, 9]) */
	g_assert_cmpint (position, ==, 9);
	g_assert_cmpint (total, ==, 13);
}

static void
test_cursor_calculate_filtered_exact_target (EbSdbCursorFixture *fixture,
					     gconstpointer  user_data)
{
	GError *error = NULL;
	gint    position = 0, total = 0;

	/* Set the cursor to point exactly to 'blackbird' */
	e_book_backend_sqlitedb_cursor_set_target_contact (((ESqliteDBFixture *) fixture)->ebsdb,
							   fixture->cursor, fixture->contacts[16 - 1]);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* 'blackbird' is the 8th contact with a .com email address in en_US locale */
	g_assert_cmpint (position, ==, 8);
	g_assert_cmpint (total, ==, 13);
}

static void
test_cursor_calculate_filtered_partial_target (EbSdbCursorFixture *fixture,
					       gconstpointer  user_data)
{
	GError *error = NULL;
	gint    position = 0, total = 0;

	/* Set the cursor to point to the beginning of "C" */
	e_book_backend_sqlitedb_cursor_set_target (((ESqliteDBFixture *) fixture)->ebsdb,
						   fixture->cursor, "C", NULL);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* There are 9 contacts before the letter 'C' in the en_US locale */
	g_assert_cmpint (position, ==, 9);
	g_assert_cmpint (total, ==, 13);
}

static void
test_cursor_calculate_filtered_after_modification (EbSdbCursorFixture *fixture,
						   gconstpointer  user_data)
{
	EBookClient  *book_client;
	GError *error = NULL;
	gint    position = 0, total = 0;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	/* Set the cursor to point exactly to 'blackbird' */
	e_book_backend_sqlitedb_cursor_set_target_contact (((ESqliteDBFixture *) fixture)->ebsdb,
							   fixture->cursor, fixture->contacts[16 - 1]);

	/* 'blackbirds' -> Jacob Appelbaum */
	e_contact_set (fixture->contacts[18 - 1], E_CONTACT_FAMILY_NAME, "Appelbaum");
	e_contact_set (fixture->contacts[18 - 1], E_CONTACT_GIVEN_NAME, "Jacob");
	if (!e_book_client_modify_contact_sync (book_client, fixture->contacts[18 - 1], NULL, &error))
		g_error ("modify contact sync: %s", error->message);

	/* 'black-birds' -> Sade Adu */
	e_contact_set (fixture->contacts[17 - 1], E_CONTACT_FAMILY_NAME, "Adu");
	e_contact_set (fixture->contacts[17 - 1], E_CONTACT_GIVEN_NAME, "Sade");
	if (!e_book_client_modify_contact_sync (book_client, fixture->contacts[17 - 1], NULL, &error))
		g_error ("modify contact sync: %s", error->message);

	/* Check new position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* blackbird is now at position 11 after moving 2 later contacts to begin with 'A' */
	g_assert_cmpint (position, ==, 9);
	g_assert_cmpint (total, ==, 13);
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

	g_test_add ("/EbSdbCursor/Calculate/Initial", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_setup,
		    test_cursor_calculate_initial,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/MoveForward", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_setup,
		    test_cursor_calculate_move_forward,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/MoveBackwards", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_setup,
		    test_cursor_calculate_move_backwards,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/BackAndForth", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_setup,
		    test_cursor_calculate_back_and_forth,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/ExactTarget", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_setup,
		    test_cursor_calculate_exact_target,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/PartialTarget", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_setup,
		    test_cursor_calculate_partial_target,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/AfterModification", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_setup,
		    test_cursor_calculate_after_modification,
		    e_sqlitedb_cursor_fixture_teardown);

	g_test_add ("/EbSdbCursor/Calculate/Filtered/Initial", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_filtered_setup,
		    test_cursor_calculate_filtered_initial,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/Filtered/MoveForward", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_filtered_setup,
		    test_cursor_calculate_filtered_move_forward,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/Filtered/MoveBackwards", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_filtered_setup,
		    test_cursor_calculate_filtered_move_backwards,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/Filtered/ExactTarget", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_filtered_setup,
		    test_cursor_calculate_filtered_exact_target,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/Filtered/PartialTarget", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_filtered_setup,
		    test_cursor_calculate_filtered_partial_target,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/Calculate/Filtered/AfterModification", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_filtered_setup,
		    test_cursor_calculate_filtered_after_modification,
		    e_sqlitedb_cursor_fixture_teardown);

	return e_test_server_utils_run ();
}

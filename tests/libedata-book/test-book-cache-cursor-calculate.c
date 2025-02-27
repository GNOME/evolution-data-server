/*
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
 */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "e-test-server-utils.h"
#include "test-book-cache-utils.h"

static TCUCursorClosure ascending_closure = {
	{ NULL },
	NULL, E_BOOK_CURSOR_SORT_ASCENDING
};

static TCUCursorClosure descending_closure = {
	{ NULL },
	NULL, E_BOOK_CURSOR_SORT_DESCENDING
};

static void
test_cursor_calculate_initial (TCUCursorFixture *fixture,
			       gconstpointer user_data)
{
	GError *error = NULL;
	gint position = 0, total = 0;

	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	g_assert_cmpint (position, ==, 0);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_move_forward (TCUCursorFixture *fixture,
				    gconstpointer user_data)
{
	GSList *results = NULL;
	GError *error = NULL;
	gint position = 0, total = 0;

	/* Move cursor */
	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE | E_BOOK_CACHE_CURSOR_STEP_FETCH,
				       E_BOOK_CACHE_CURSOR_ORIGIN_CURRENT,
				       5,
				       &results, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	/* Assert the first 5 contacts in en_US order */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	tcu_assert_contacts_order (
		results,
		"sorted-11",
		"sorted-1",
		"sorted-2",
		"sorted-5",
		"sorted-6",
		NULL);
	g_slist_free_full (results, e_book_cache_search_data_free);

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* results 0 + 5 = position 5, result index 4 (results[0, 1, 2, 3, 4]) */
	g_assert_cmpint (position, ==, 5);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_move_backwards (TCUCursorFixture *fixture,
				      gconstpointer user_data)
{
	GSList *results = NULL;
	GError *error = NULL;
	gint position = 0, total = 0;

	/* Move cursor */
	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE | E_BOOK_CACHE_CURSOR_STEP_FETCH,
				       E_BOOK_CACHE_CURSOR_ORIGIN_END,
				       -5,
				       &results, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	/* Assert the last 5 contacts in en_US order */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	tcu_assert_contacts_order (
		results,
		"sorted-20",
		"sorted-19",
		"sorted-9",
		"sorted-13",
		"sorted-12",
		NULL);
	g_slist_free_full (results, e_book_cache_search_data_free);

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	/* results 20 - 5 = position 16 result index 15 (results[20, 19, 18, 17, 16]) */
	g_assert_cmpint (position, ==, 16);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_back_and_forth (TCUCursorFixture *fixture,
				      gconstpointer user_data)
{
	GSList *results = NULL;
	GError *error = NULL;
	gint position = 0, total = 0;

	/* Move cursor */
	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE | E_BOOK_CACHE_CURSOR_STEP_FETCH,
				       E_BOOK_CACHE_CURSOR_ORIGIN_BEGIN,
				       7,
				       &results, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	g_assert_cmpint (g_slist_length (results), ==, 7);
	g_slist_free_full (results, e_book_cache_search_data_free);
	results = NULL;

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* results 0 + 7 = position 7 result index 6 (results[0, 1, 2, 3, 4, 5, 6]) */
	g_assert_cmpint (position, ==, 7);
	g_assert_cmpint (total, ==, 20);

	/* Move cursor */
	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE | E_BOOK_CACHE_CURSOR_STEP_FETCH,
				       E_BOOK_CACHE_CURSOR_ORIGIN_CURRENT,
				       -4,
				       &results, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	g_assert_cmpint (g_slist_length (results), ==, 4);
	g_slist_free_full (results, e_book_cache_search_data_free);
	results = NULL;

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* results 7 - 4 = position 3 result index 2 (results[5, 4, 3, 2]) */
	g_assert_cmpint (position, ==, 3);
	g_assert_cmpint (total, ==, 20);

	/* Move cursor */
	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE | E_BOOK_CACHE_CURSOR_STEP_FETCH,
				       E_BOOK_CACHE_CURSOR_ORIGIN_CURRENT,
				       5,
				       &results, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	g_assert_cmpint (g_slist_length (results), ==, 5);
	g_slist_free_full (results, e_book_cache_search_data_free);
	results = NULL;

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* results 3 + 5 = position 8 result index 7 (results[3, 4, 5, 6, 7]) */
	g_assert_cmpint (position, ==, 8);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_partial_target (TCUCursorFixture *fixture,
				      gconstpointer user_data)
{
	GError *error = NULL;
	gint position = 0, total = 0;
	ECollator *collator;
	gint n_labels;
	const gchar *const *labels;

	/* First verify our test... in en_US locale the label 'C' should exist with the index 3 */
	collator = e_book_cache_ref_collator (((TCUFixture *) fixture)->book_cache);
	labels = e_collator_get_index_labels (collator, &n_labels, NULL, NULL, NULL);
	g_assert_cmpstr (labels[3], ==, "C");
	e_collator_unref (collator);

	/* Set the cursor at the start of family names beginning with 'C' */
	e_book_cache_cursor_set_target_alphabetic_index (
		((TCUFixture *) fixture)->book_cache,
		fixture->cursor, 3);

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* Position is 13, there are 13 contacts before the letter 'C' in en_US locale */
	g_assert_cmpint (position, ==, 13);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_after_modification (TCUCursorFixture *fixture,
					  gconstpointer user_data)
{
	GError *error = NULL;
	gint position = 0, total = 0;

	/* Set the cursor to point exactly 'blackbird' (which is the 12th contact) */
	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE,
				       E_BOOK_CACHE_CURSOR_ORIGIN_CURRENT,
				       12, NULL, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* blackbird is at position 12 in en_US locale */
	g_assert_cmpint (position, ==, 12);
	g_assert_cmpint (total, ==, 20);

	/* Rename Muffler -> Jacob Appelbaum */
	e_contact_set (fixture->contacts[19 - 1], E_CONTACT_FAMILY_NAME, "Appelbaum");
	e_contact_set (fixture->contacts[19 - 1], E_CONTACT_GIVEN_NAME, "Jacob");
	if (!e_book_cache_put_contact (((TCUFixture *) fixture)->book_cache,
					fixture->contacts[19 - 1],
					e_contact_get_const (fixture->contacts[19 - 1], E_CONTACT_UID),
					0, E_CACHE_IS_ONLINE, NULL, &error))
		g_error ("Failed to modify contact: %s", error->message);

	/* Rename Müller -> Sade Adu */
	e_contact_set (fixture->contacts[20 - 1], E_CONTACT_FAMILY_NAME, "Adu");
	e_contact_set (fixture->contacts[20 - 1], E_CONTACT_GIVEN_NAME, "Sade");
	if (!e_book_cache_put_contact (((TCUFixture *) fixture)->book_cache,
					fixture->contacts[20 - 1],
					e_contact_get_const (fixture->contacts[20 - 1], E_CONTACT_UID),
					0, E_CACHE_IS_ONLINE, NULL, &error))
		g_error ("Failed to modify contact: %s", error->message);

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* blackbird is now at position 14 after moving 2 later contacts to begin with 'A' */
	g_assert_cmpint (position, ==, 14);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_filtered_initial (TCUCursorFixture *fixture,
					gconstpointer user_data)
{
	GError *error = NULL;
	gint position = 0, total = 0;

	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	g_assert_cmpint (position, ==, 0);
	g_assert_cmpint (total, ==, 13);
}

static void
test_cursor_calculate_filtered_move_forward (TCUCursorFixture *fixture,
					     gconstpointer user_data)
{
	GSList *results = NULL;
	GError *error = NULL;
	gint position = 0, total = 0;

	/* Move cursor */
	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE | E_BOOK_CACHE_CURSOR_STEP_FETCH,
				       E_BOOK_CACHE_CURSOR_ORIGIN_CURRENT,
				       5, &results, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	g_assert_cmpint (g_slist_length (results), ==, 5);
	g_slist_free_full (results, e_book_cache_search_data_free);
	results = NULL;

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	/* results 0 + 5 = position 5, result index 4 (results[0, 1, 2, 3, 4]) */
	g_assert_cmpint (position, ==, 5);
	g_assert_cmpint (total, ==, 13);
}

static void
test_cursor_calculate_filtered_move_backwards (TCUCursorFixture *fixture,
					       gconstpointer user_data)
{
	GSList *results = NULL;
	GError *error = NULL;
	gint position = 0, total = 0;

	/* Move cursor */
	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE | E_BOOK_CACHE_CURSOR_STEP_FETCH,
				       E_BOOK_CACHE_CURSOR_ORIGIN_END,
				       -5,
				       &results, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	g_assert_cmpint (g_slist_length (results), ==, 5);
	g_slist_free_full (results, e_book_cache_search_data_free);
	results = NULL;

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* results 13 - 5 = position 9 (results[13, 12, 11, 10, 9]) */
	g_assert_cmpint (position, ==, 9);
	g_assert_cmpint (total, ==, 13);
}

static void
test_cursor_calculate_filtered_partial_target (TCUCursorFixture *fixture,
					       gconstpointer user_data)
{
	GError *error = NULL;
	gint position = 0, total = 0;
	ECollator *collator;
	gint n_labels;
	const gchar *const *labels;

	/* First verify our test... in en_US locale the label 'C' should exist with the index 3 */
	collator = e_book_cache_ref_collator (((TCUFixture *) fixture)->book_cache);
	labels = e_collator_get_index_labels (collator, &n_labels, NULL, NULL, NULL);
	g_assert_cmpstr (labels[3], ==, "C");
	e_collator_unref (collator);

	/* Set the cursor at the start of family names beginning with 'C' */
	e_book_cache_cursor_set_target_alphabetic_index (
		((TCUFixture *) fixture)->book_cache,
		fixture->cursor, 3);

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* There are 9 contacts before the letter 'C' in the en_US locale */
	g_assert_cmpint (position, ==, 9);
	g_assert_cmpint (total, ==, 13);
}

static void
test_cursor_calculate_filtered_after_modification (TCUCursorFixture *fixture,
						   gconstpointer user_data)
{
	GError *error = NULL;
	gint position = 0, total = 0;

	/* Set the cursor to point exactly 'blackbird' (which is the 8th contact when filtered) */
	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE,
				       E_BOOK_CACHE_CURSOR_ORIGIN_BEGIN,
				       8, NULL, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	/* 'blackbirds' -> Jacob Appelbaum */
	e_contact_set (fixture->contacts[18 - 1], E_CONTACT_FAMILY_NAME, "Appelbaum");
	e_contact_set (fixture->contacts[18 - 1], E_CONTACT_GIVEN_NAME, "Jacob");
	if (!e_book_cache_put_contact (((TCUFixture *) fixture)->book_cache,
					fixture->contacts[18 - 1],
					e_contact_get_const (fixture->contacts[18 - 1], E_CONTACT_UID),
					0, E_CACHE_IS_ONLINE, NULL, &error))
		g_error ("Failed to modify contact: %s", error->message);

	/* 'black-birds' -> Sade Adu */
	e_contact_set (fixture->contacts[17 - 1], E_CONTACT_FAMILY_NAME, "Adu");
	e_contact_set (fixture->contacts[17 - 1], E_CONTACT_GIVEN_NAME, "Sade");
	if (!e_book_cache_put_contact (((TCUFixture *) fixture)->book_cache,
					fixture->contacts[17 - 1],
					e_contact_get_const (fixture->contacts[17 - 1], E_CONTACT_UID),
					0, E_CACHE_IS_ONLINE, NULL, &error))
		g_error ("Failed to modify contact: %s", error->message);

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* blackbird is now at position 11 after moving 2 later contacts to begin with 'A' */
	g_assert_cmpint (position, ==, 9);
	g_assert_cmpint (total, ==, 13);
}

static void
test_cursor_calculate_descending_move_forward (TCUCursorFixture *fixture,
                                               gconstpointer user_data)
{
	GSList *results = NULL;
	GError *error = NULL;
	gint position = 0, total = 0;

	/* Move cursor */
	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE | E_BOOK_CACHE_CURSOR_STEP_FETCH,
				       E_BOOK_CACHE_CURSOR_ORIGIN_BEGIN,
				       5,
				       &results, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	/* Assert the first 5 contacts in en_US order */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	tcu_assert_contacts_order (
		results,
		"sorted-20",
		"sorted-19",
		"sorted-9",
		"sorted-13",
		"sorted-12",
		NULL);
	g_slist_free_full (results, e_book_cache_search_data_free);
	results = NULL;

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	/* results 0 + 5 = position 5, result index 4 (results[0, 1, 2, 3, 4]) */
	g_assert_cmpint (position, ==, 5);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_descending_move_backwards (TCUCursorFixture *fixture,
						 gconstpointer user_data)
{
	GSList *results = NULL;
	GError *error = NULL;
	gint position = 0, total = 0;

	/* Move cursor */
	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE | E_BOOK_CACHE_CURSOR_STEP_FETCH,
				       E_BOOK_CACHE_CURSOR_ORIGIN_END,
				       -5, &results, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	/* Assert the last 5 contacts in en_US order */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	tcu_assert_contacts_order (
		results,
		"sorted-11",
		"sorted-1",
		"sorted-2",
		"sorted-5",
		"sorted-6",
		NULL);
	g_slist_free_full (results, e_book_cache_search_data_free);
	results = NULL;

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
	    g_error ("Error calculating cursor: %s", error->message);

	/* results 20 - 5 = position 16 result index 15 (results[20, 19, 18, 17, 16]) */
	g_assert_cmpint (position, ==, 16);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_descending_partial_target (TCUCursorFixture *fixture,
						 gconstpointer user_data)
{
	GError *error = NULL;
	gint position = 0, total = 0;
	ECollator *collator;
	gint n_labels;
	const gchar *const *labels;

	/* First verify our test... in en_US locale the label 'C' should exist with the index 3 */
	collator = e_book_cache_ref_collator (((TCUFixture *) fixture)->book_cache);
	labels = e_collator_get_index_labels (collator, &n_labels, NULL, NULL, NULL);
	g_assert_cmpstr (labels[3], ==, "C");
	e_collator_unref (collator);

	/* Set the cursor at the start of family names beginning with 'C' */
	e_book_cache_cursor_set_target_alphabetic_index (
		((TCUFixture *) fixture)->book_cache,
		fixture->cursor, 3);

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* Position is 7, there are 7 contacts leading up to the last 'C' in en_US locale
	 * (when sorting in descending order) */
	g_assert_cmpint (position, ==, 7);
	g_assert_cmpint (total, ==, 20);
}

static void
test_cursor_calculate_descending_after_modification (TCUCursorFixture *fixture,
						     gconstpointer user_data)
{
	GError *error = NULL;
	gint position = 0, total = 0;

	/* Set the cursor to point exactly 'Bät' (which is the 12th contact in descending order) */
	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE,
				       E_BOOK_CACHE_CURSOR_ORIGIN_BEGIN,
				       12, NULL, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* 'Bät' is at position 12 in en_US locale (descending order) */
	g_assert_cmpint (position, ==, 12);
	g_assert_cmpint (total, ==, 20);

	/* Rename Muffler -> Jacob Appelbaum */
	e_contact_set (fixture->contacts[19 - 1], E_CONTACT_FAMILY_NAME, "Appelbaum");
	e_contact_set (fixture->contacts[19 - 1], E_CONTACT_GIVEN_NAME, "Jacob");
	if (!e_book_cache_put_contact (((TCUFixture *) fixture)->book_cache,
					fixture->contacts[19 - 1],
					e_contact_get_const (fixture->contacts[19 - 1], E_CONTACT_UID),
					0, E_CACHE_IS_ONLINE, NULL, &error))
		g_error ("Failed to modify contact: %s", error->message);

	/* Rename Müller -> Sade Adu */
	e_contact_set (fixture->contacts[20 - 1], E_CONTACT_FAMILY_NAME, "Adu");
	e_contact_set (fixture->contacts[20 - 1], E_CONTACT_GIVEN_NAME, "Sade");
	if (!e_book_cache_put_contact (((TCUFixture *) fixture)->book_cache,
					fixture->contacts[20 - 1],
					e_contact_get_const (fixture->contacts[20 - 1], E_CONTACT_UID),
					0, E_CACHE_IS_ONLINE, NULL, &error))
		g_error ("Failed to modify contact: %s", error->message);

	/* Check new position */
	if (!e_book_cache_cursor_calculate (((TCUFixture *) fixture)->book_cache,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* 'Bät' is now at position 10 in descending order after moving 2 contacts to begin with 'A' */
	g_assert_cmpint (position, ==, 10);
	g_assert_cmpint (total, ==, 20);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	tcu_read_args (argc, argv);

	g_test_add (
		"/EBookCacheCursor/Calculate/Initial", TCUCursorFixture, &ascending_closure,
		tcu_cursor_fixture_setup,
		test_cursor_calculate_initial,
		tcu_cursor_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Calculate/MoveForward", TCUCursorFixture, &ascending_closure,
		tcu_cursor_fixture_setup,
		test_cursor_calculate_move_forward,
		tcu_cursor_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Calculate/MoveBackwards", TCUCursorFixture, &ascending_closure,
		tcu_cursor_fixture_setup,
		test_cursor_calculate_move_backwards,
		tcu_cursor_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Calculate/BackAndForth", TCUCursorFixture, &ascending_closure,
		tcu_cursor_fixture_setup,
		test_cursor_calculate_back_and_forth,
		tcu_cursor_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Calculate/AlphabeticTarget", TCUCursorFixture, &ascending_closure,
		tcu_cursor_fixture_setup,
		test_cursor_calculate_partial_target,
		tcu_cursor_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Calculate/AfterModification", TCUCursorFixture, &ascending_closure,
		tcu_cursor_fixture_setup,
		test_cursor_calculate_after_modification,
		tcu_cursor_fixture_teardown);

	g_test_add (
		"/EBookCacheCursor/Calculate/Filtered/Initial", TCUCursorFixture, &ascending_closure,
		tcu_cursor_fixture_filtered_setup,
		test_cursor_calculate_filtered_initial,
		tcu_cursor_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Calculate/Filtered/MoveForward", TCUCursorFixture, &ascending_closure,
		tcu_cursor_fixture_filtered_setup,
		test_cursor_calculate_filtered_move_forward,
		tcu_cursor_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Calculate/Filtered/MoveBackwards", TCUCursorFixture, &ascending_closure,
		tcu_cursor_fixture_filtered_setup,
		test_cursor_calculate_filtered_move_backwards,
		tcu_cursor_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Calculate/Filtered/AlphabeticTarget", TCUCursorFixture, &ascending_closure,
		tcu_cursor_fixture_filtered_setup,
		test_cursor_calculate_filtered_partial_target,
		tcu_cursor_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Calculate/Filtered/AfterModification", TCUCursorFixture, &ascending_closure,
		tcu_cursor_fixture_filtered_setup,
		test_cursor_calculate_filtered_after_modification,
		tcu_cursor_fixture_teardown);

	g_test_add (
		"/EBookCacheCursor/Calculate/Descending/Initial", TCUCursorFixture, &descending_closure,
		tcu_cursor_fixture_setup,
		test_cursor_calculate_initial,
		tcu_cursor_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Calculate/Descending/MoveForward", TCUCursorFixture, &descending_closure,
		tcu_cursor_fixture_setup,
		test_cursor_calculate_descending_move_forward,
		tcu_cursor_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Calculate/Descending/MoveBackwards", TCUCursorFixture, &descending_closure,
		tcu_cursor_fixture_setup,
		test_cursor_calculate_descending_move_backwards,
		tcu_cursor_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Calculate/Descending/BackAndForth", TCUCursorFixture, &descending_closure,
		tcu_cursor_fixture_setup,
		test_cursor_calculate_back_and_forth,
		tcu_cursor_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Calculate/Descending/AlphabeticTarget", TCUCursorFixture, &descending_closure,
		tcu_cursor_fixture_setup,
		test_cursor_calculate_descending_partial_target,
		tcu_cursor_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Calculate/Descending/AfterModification", TCUCursorFixture, &descending_closure,
		tcu_cursor_fixture_setup,
		test_cursor_calculate_descending_after_modification,
		tcu_cursor_fixture_teardown);

	return e_test_server_utils_run_full (argc, argv, 0);
}

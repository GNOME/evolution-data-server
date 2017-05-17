/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#include "test-book-cache-utils.h"

/*****************************************************
 *          Expect the same results twice            *
 *****************************************************/
static void
test_cursor_set_target_reset_cursor (TCUCursorFixture *fixture,
				     gconstpointer user_data)
{
	GSList *results = NULL;
	GError *error = NULL;

	/* First batch */
	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE | E_BOOK_CACHE_CURSOR_STEP_FETCH,
				       E_BOOK_CACHE_CURSOR_ORIGIN_BEGIN,
				       5, &results, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	tcu_print_results (results);

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
	results = NULL;

	/* Second batch reset (same results) */
	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE | E_BOOK_CACHE_CURSOR_STEP_FETCH,
				       E_BOOK_CACHE_CURSOR_ORIGIN_BEGIN,
				       5, &results, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	tcu_print_results (results);

	/* Assert the first 5 contacts in en_US order again */
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
}

/*****************************************************
 * Expect results with family name starting with 'C' *
 *****************************************************/
static void
test_cursor_set_target_c_next_results (TCUCursorFixture *fixture,
				       gconstpointer user_data)
{
	GSList *results = NULL;
	GError *error = NULL;
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

	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE | E_BOOK_CACHE_CURSOR_STEP_FETCH,
				       E_BOOK_CACHE_CURSOR_ORIGIN_CURRENT,
				       5, &results, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	tcu_print_results (results);

	/* Assert that we got the results starting at C */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	tcu_assert_contacts_order (
		results,
		"sorted-10",
		"sorted-14",
		"sorted-12",
		"sorted-13",
		"sorted-9",
		NULL);

	g_slist_free_full (results, e_book_cache_search_data_free);
}

/*****************************************************
 *       Expect results before the letter 'C'        *
 *****************************************************/
static void
test_cursor_set_target_c_prev_results (TCUCursorFixture *fixture,
                                       gconstpointer user_data)
{
	GSList *results = NULL;
	GError *error = NULL;
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

	if (e_book_cache_cursor_step (((TCUFixture *) fixture)->book_cache,
				       fixture->cursor,
				       E_BOOK_CACHE_CURSOR_STEP_MOVE | E_BOOK_CACHE_CURSOR_STEP_FETCH,
				       E_BOOK_CACHE_CURSOR_ORIGIN_CURRENT,
				       -5, &results, NULL, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	tcu_print_results (results);

	/* Assert that we got the results before C */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	tcu_assert_contacts_order (
		results,
		"sorted-18",
		"sorted-16",
		"sorted-17",
		"sorted-15",
		"sorted-8",
		NULL);

	g_slist_free_full (results, e_book_cache_search_data_free);
}

static TCUCursorClosure closures[] = {
	{ { NULL }, NULL, E_BOOK_CURSOR_SORT_ASCENDING },
	{ { tcu_setup_empty_book }, NULL, E_BOOK_CURSOR_SORT_ASCENDING }
};

static const gchar *prefixes[] = {
	"/EBookCache/DefaultSummary",
	"/EBookCache/EmptySummary"
};

gint
main (gint argc,
      gchar **argv)
{
	gint ii;

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	for (ii = 0; ii < G_N_ELEMENTS (closures); ii++) {
		gchar *path;

		path = g_strconcat (prefixes[ii], "/SetTarget/ResetCursor", NULL);
		g_test_add (
			path, TCUCursorFixture, &closures[ii],
			tcu_cursor_fixture_setup,
			test_cursor_set_target_reset_cursor,
			tcu_cursor_fixture_teardown);
		g_free (path);

		path = g_strconcat (prefixes[ii], "/SetTarget/Alphabetic/C/NextResults", NULL);
		g_test_add (
			path, TCUCursorFixture, &closures[ii],
			tcu_cursor_fixture_setup,
			test_cursor_set_target_c_next_results,
			tcu_cursor_fixture_teardown);
		g_free (path);

		path = g_strconcat (prefixes[ii], "/SetTarget/Alphabetic/C/PreviousResults", NULL);
		g_test_add (
			path, TCUCursorFixture, &closures[ii],
			tcu_cursor_fixture_setup,
			test_cursor_set_target_c_prev_results,
			tcu_cursor_fixture_teardown);
		g_free (path);
	}

	return g_test_run ();
}

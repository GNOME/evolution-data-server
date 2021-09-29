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

#include "e-test-server-utils.h"
#include "data-test-utils.h"

static EbSqlCursorClosure book_closure = { { FALSE, NULL }, NULL, E_BOOK_CURSOR_SORT_ASCENDING };

static void
test_cursor_sexp_calculate_position (EbSqlCursorFixture *fixture,
                                     gconstpointer user_data)
{
	GError *error = NULL;
	EBookQuery *query;
	gint    position = 0, total = 0;
	gchar *sexp = NULL;
	GSList *results = NULL, *node;
	EbSqlSearchData *data;

	/* Set the cursor to point exactly to 'blackbirds', which is the 12th contact in en_US */
	if (!e_book_sqlite_cursor_step (((EbSqlFixture *) fixture)->ebsql,
					fixture->cursor,
					EBSQL_CURSOR_STEP_MOVE | EBSQL_CURSOR_STEP_FETCH,
					EBSQL_CURSOR_ORIGIN_BEGIN,
					12, &results, NULL, &error))
		g_error ("Error fetching cursor results: %s", error->message);

	/* Ensure we moved to the right contact */
	node = g_slist_last (results);
	g_assert_true (node);
	data = node->data;
	g_assert_cmpstr (data->uid, ==, "sorted-16");
	g_slist_foreach (results, (GFunc) e_book_sqlite_search_data_free, NULL);
	g_slist_free (results);

	/* Check position */
	if (!e_book_sqlite_cursor_calculate (((EbSqlFixture *) fixture)->ebsql,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* blackbird is at position 12 in an unfiltered en_US locale */
	g_assert_cmpint (position, ==, 12);
	g_assert_cmpint (total, ==, 20);

	/* Set new sexp, only contacts with .com email addresses */
	query = e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, ".com");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	if (!e_book_sqlite_cursor_set_sexp (((EbSqlFixture *) fixture)->ebsql,
					    fixture->cursor, sexp, &error))
		g_error ("Failed to set sexp: %s", error->message);

	/* Check new position after modified sexp */
	if (!e_book_sqlite_cursor_calculate (((EbSqlFixture *) fixture)->ebsql,
					     fixture->cursor, &total, &position, NULL, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* 'blackbird' is now at position 8 out of 13, with a filtered set of contacts in en_US locale */
	g_assert_cmpint (position, ==, 8);
	g_assert_cmpint (total, ==, 13);
}

static void
test_cursor_sexp_and_step (EbSqlCursorFixture *fixture,
                           gconstpointer user_data)
{
	GError *error = NULL;
	EBookQuery *query;
	gchar *sexp = NULL;
	GSList *results = NULL, *node;
	EbSqlSearchData *data;

	/* Set new sexp, only contacts with .com email addresses */
	query = e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, ".com");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	if (!e_book_sqlite_cursor_set_sexp (((EbSqlFixture *) fixture)->ebsql,
					    fixture->cursor, sexp, &error))
		g_error ("Failed to set sexp: %s", error->message);

	/* Step 6 results from the beginning of the filtered list, gets up to contact 'sorted-8' */
	if (!e_book_sqlite_cursor_step (((EbSqlFixture *) fixture)->ebsql,
					fixture->cursor,
					EBSQL_CURSOR_STEP_MOVE | EBSQL_CURSOR_STEP_FETCH,
					EBSQL_CURSOR_ORIGIN_BEGIN,
					6, &results, NULL, &error))
		g_error ("Error fetching cursor results: %s", error->message);

	/* Ensure we moved to the right contact */
	node = g_slist_last (results);
	g_assert_true (node);
	data = node->data;
	g_assert_cmpstr (data->uid, ==, "sorted-8");
	g_slist_foreach (results, (GFunc) e_book_sqlite_search_data_free, NULL);
	g_slist_free (results);
	results = NULL;

	/* Step 6 results more, gets up to contact 'sorted-12' */
	if (!e_book_sqlite_cursor_step (((EbSqlFixture *) fixture)->ebsql,
					fixture->cursor,
					EBSQL_CURSOR_STEP_MOVE | EBSQL_CURSOR_STEP_FETCH,
					EBSQL_CURSOR_ORIGIN_CURRENT,
					6, &results, NULL, &error))
		g_error ("Error fetching cursor results: %s", error->message);

	/* Ensure we moved to the right contact */
	node = g_slist_last (results);
	g_assert_true (node);
	data = node->data;
	g_assert_cmpstr (data->uid, ==, "sorted-12");
	g_slist_foreach (results, (GFunc) e_book_sqlite_search_data_free, NULL);
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
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	data_test_utils_read_args (argc, argv);

	g_test_add (
		"/EbSqlCursor/SetSexp/CalculatePosition", EbSqlCursorFixture, &book_closure,
		e_sqlite_cursor_fixture_setup,
		test_cursor_sexp_calculate_position,
		e_sqlite_cursor_fixture_teardown);
	g_test_add (
		"/EbSqlCursor/SetSexp/Step", EbSqlCursorFixture, &book_closure,
		e_sqlite_cursor_fixture_setup,
		test_cursor_sexp_and_step,
		e_sqlite_cursor_fixture_teardown);

	return e_test_server_utils_run_full (argc, argv, 0);
}

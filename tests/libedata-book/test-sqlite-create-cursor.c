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

#include "data-test-utils.h"

static EbSqlClosure closure = { FALSE, NULL };

static void
test_create_cursor_empty_query (EbSqlFixture *fixture,
                                gconstpointer user_data)
{
	EbSqlCursor  *cursor;
	EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
	EBookCursorSortType sort_types[] = { E_BOOK_CURSOR_SORT_ASCENDING, E_BOOK_CURSOR_SORT_ASCENDING };
	GError       *error = NULL;

	cursor = e_book_sqlite_cursor_new (
		fixture->ebsql, NULL,
		sort_fields, sort_types, 2, &error);

	g_assert (cursor != NULL);
	e_book_sqlite_cursor_free (fixture->ebsql, cursor);
}

static void
test_create_cursor_valid_query (EbSqlFixture *fixture,
                                gconstpointer user_data)
{
	EbSqlCursor  *cursor;
	EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
	EBookCursorSortType sort_types[] = { E_BOOK_CURSOR_SORT_ASCENDING, E_BOOK_CURSOR_SORT_ASCENDING };
	EBookQuery   *query;
	gchar        *sexp;
	GError       *error = NULL;

	query = e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "James Brown");
	sexp = e_book_query_to_string (query);

	cursor = e_book_sqlite_cursor_new (
		fixture->ebsql, sexp,
		sort_fields, sort_types, 2, &error);

	g_assert (cursor != NULL);
	e_book_sqlite_cursor_free (fixture->ebsql, cursor);
	g_free (sexp);
	e_book_query_unref (query);
}

static void
test_create_cursor_invalid_sort (EbSqlFixture *fixture,
                                 gconstpointer user_data)
{
	EbSqlCursor  *cursor;
	EContactField sort_fields[] = { E_CONTACT_TEL };
	EBookCursorSortType sort_types[] = { E_BOOK_CURSOR_SORT_ASCENDING };
	GError       *error = NULL;

	cursor = e_book_sqlite_cursor_new (
		fixture->ebsql, NULL,
		sort_fields, sort_types, 1, &error);

	g_assert (cursor == NULL);
	g_assert_error (error, E_BOOK_SQLITE_ERROR, E_BOOK_SQLITE_ERROR_INVALID_QUERY);
	g_clear_error (&error);
}

static void
test_create_cursor_missing_sort (EbSqlFixture *fixture,
                                 gconstpointer user_data)
{
	EbSqlCursor  *cursor;
	GError       *error = NULL;

	cursor = e_book_sqlite_cursor_new (fixture->ebsql, NULL, NULL, NULL, 0, &error);

	g_assert (cursor == NULL);
	g_assert_error (error, E_BOOK_SQLITE_ERROR, E_BOOK_SQLITE_ERROR_INVALID_QUERY);
	g_clear_error (&error);
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

	g_test_add (
		"/EbSqlCursor/Create/EmptyQuery", EbSqlFixture, &closure,
		e_sqlite_fixture_setup, test_create_cursor_empty_query, e_sqlite_fixture_teardown);
	g_test_add (
		"/EbSqlCursor/Create/ValidQuery", EbSqlFixture, &closure,
		e_sqlite_fixture_setup, test_create_cursor_valid_query, e_sqlite_fixture_teardown);
	g_test_add (
		"/EbSqlCursor/Create/InvalidSort", EbSqlFixture, &closure,
		e_sqlite_fixture_setup, test_create_cursor_invalid_sort, e_sqlite_fixture_teardown);
	g_test_add (
		"/EbSqlCursor/Create/MissingSort", EbSqlFixture, &closure,
		e_sqlite_fixture_setup, test_create_cursor_missing_sort, e_sqlite_fixture_teardown);

	return g_test_run ();
}

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
#include "test-book-cache-utils.h"

static TCUClosure closure = { NULL };

static void
test_create_cursor_empty_query (TCUFixture *fixture,
				gconstpointer user_data)
{
	EBookCacheCursor *cursor;
	EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
	EBookCursorSortType sort_types[] = { E_BOOK_CURSOR_SORT_ASCENDING, E_BOOK_CURSOR_SORT_ASCENDING };
	GError *error = NULL;

	cursor = e_book_cache_cursor_new (
		fixture->book_cache, NULL,
		sort_fields, sort_types, 2, &error);

	g_assert (cursor != NULL);
	e_book_cache_cursor_free (fixture->book_cache, cursor);
}

static void
test_create_cursor_valid_query (TCUFixture *fixture,
				gconstpointer user_data)
{
	EBookCacheCursor *cursor;
	EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
	EBookCursorSortType sort_types[] = { E_BOOK_CURSOR_SORT_ASCENDING, E_BOOK_CURSOR_SORT_ASCENDING };
	EBookQuery *query;
	gchar *sexp;
	GError *error = NULL;

	query = e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "James Brown");
	sexp = e_book_query_to_string (query);

	cursor = e_book_cache_cursor_new (
		fixture->book_cache, sexp,
		sort_fields, sort_types, 2, &error);

	g_assert (cursor != NULL);
	e_book_cache_cursor_free (fixture->book_cache, cursor);
	g_free (sexp);
	e_book_query_unref (query);
}

static void
test_create_cursor_invalid_sort (TCUFixture *fixture,
				 gconstpointer user_data)
{
	EBookCacheCursor *cursor;
	EContactField sort_fields[] = { E_CONTACT_TEL };
	EBookCursorSortType sort_types[] = { E_BOOK_CURSOR_SORT_ASCENDING };
	GError *error = NULL;

	cursor = e_book_cache_cursor_new (
		fixture->book_cache, NULL,
		sort_fields, sort_types, 1, &error);

	g_assert (cursor == NULL);
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_INVALID_QUERY);
	g_clear_error (&error);
}

static void
test_create_cursor_missing_sort (TCUFixture *fixture,
				 gconstpointer user_data)
{
	EBookCacheCursor *cursor;
	GError *error = NULL;

	cursor = e_book_cache_cursor_new (fixture->book_cache, NULL, NULL, NULL, 0, &error);

	g_assert (cursor == NULL);
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_INVALID_QUERY);
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
		"/EBookCacheCursor/Create/EmptyQuery", TCUFixture, &closure,
		tcu_fixture_setup, test_create_cursor_empty_query, tcu_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Create/ValidQuery", TCUFixture, &closure,
		tcu_fixture_setup, test_create_cursor_valid_query, tcu_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Create/InvalidSort", TCUFixture, &closure,
		tcu_fixture_setup, test_create_cursor_invalid_sort, tcu_fixture_teardown);
	g_test_add (
		"/EBookCacheCursor/Create/MissingSort", TCUFixture, &closure,
		tcu_fixture_setup, test_create_cursor_missing_sort, tcu_fixture_teardown);

	return e_test_server_utils_run_full (0);
}

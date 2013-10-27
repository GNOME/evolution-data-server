/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "data-test-utils.h"

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0 };

static void
test_create_cursor_empty_query (ESqliteDBFixture *fixture,
				gconstpointer     user_data)
{
	EbSdbCursor  *cursor;
	EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
	EBookSortType sort_types[] = { E_BOOK_SORT_ASCENDING, E_BOOK_SORT_ASCENDING };
	GError       *error = NULL;

	cursor = e_book_backend_sqlitedb_cursor_new (fixture->ebsdb, SQLITEDB_FOLDER_ID, NULL,
						     sort_fields, sort_types, 2, &error);

	g_assert (cursor != NULL);
	e_book_backend_sqlitedb_cursor_free (fixture->ebsdb, cursor);
}

static void
test_create_cursor_valid_query (ESqliteDBFixture *fixture,
				gconstpointer     user_data)
{
	EbSdbCursor  *cursor;
	EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
	EBookSortType sort_types[] = { E_BOOK_SORT_ASCENDING, E_BOOK_SORT_ASCENDING };
	EBookQuery   *query;
	gchar        *sexp;
	GError       *error = NULL;

	query = e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "James Brown");
	sexp  = e_book_query_to_string (query);

	cursor = e_book_backend_sqlitedb_cursor_new (fixture->ebsdb, SQLITEDB_FOLDER_ID, sexp,
						     sort_fields, sort_types, 2, &error);

	g_assert (cursor != NULL);
	e_book_backend_sqlitedb_cursor_free (fixture->ebsdb, cursor);
	g_free (sexp);
	e_book_query_unref (query);
}

static void
test_create_cursor_invalid_query (ESqliteDBFixture *fixture,
				  gconstpointer     user_data)
{
	EbSdbCursor  *cursor;
	EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
	EBookSortType sort_types[] = { E_BOOK_SORT_ASCENDING, E_BOOK_SORT_ASCENDING };
	EBookQuery   *query;
	gchar        *sexp;
	GError       *error = NULL;

	query = e_book_query_field_test (E_CONTACT_TEL, E_BOOK_QUERY_CONTAINS, "888");
	sexp  = e_book_query_to_string (query);

	cursor = e_book_backend_sqlitedb_cursor_new (fixture->ebsdb, SQLITEDB_FOLDER_ID, sexp,
						     sort_fields, sort_types, 2, &error);

	g_assert (cursor == NULL);
	g_assert (error);
	g_assert (g_error_matches (error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_INVALID_QUERY));

	g_free (sexp);
	e_book_query_unref (query);
}

static void
test_create_cursor_invalid_sort (ESqliteDBFixture *fixture,
				 gconstpointer     user_data)
{
	EbSdbCursor  *cursor;
	EContactField sort_fields[] = { E_CONTACT_TEL };
	EBookSortType sort_types[] = { E_BOOK_SORT_ASCENDING };
	GError       *error = NULL;

	cursor = e_book_backend_sqlitedb_cursor_new (fixture->ebsdb, SQLITEDB_FOLDER_ID, NULL,
						     sort_fields, sort_types, 1, &error);

	g_assert (cursor == NULL);
	g_assert (error);
	g_assert (g_error_matches (error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_INVALID_QUERY));
}

static void
test_create_cursor_missing_sort (ESqliteDBFixture *fixture,
				 gconstpointer     user_data)
{
	EbSdbCursor  *cursor;
	GError       *error = NULL;

	cursor = e_book_backend_sqlitedb_cursor_new (fixture->ebsdb, SQLITEDB_FOLDER_ID, NULL, NULL, NULL, 0, &error);

	g_assert (cursor == NULL);
	g_assert (error);
	g_assert (g_error_matches (error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_INVALID_QUERY));
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

	g_test_add ("/EbSdbCursor/Create/EmptyQuery", ESqliteDBFixture, &book_closure,
		    e_sqlitedb_fixture_setup, test_create_cursor_empty_query, e_sqlitedb_fixture_teardown);
	g_test_add ("/EbSdbCursor/Create/ValidQuery", ESqliteDBFixture, &book_closure,
		    e_sqlitedb_fixture_setup, test_create_cursor_valid_query, e_sqlitedb_fixture_teardown);
	g_test_add ("/EbSdbCursor/Create/InvalidQuery", ESqliteDBFixture, &book_closure,
		    e_sqlitedb_fixture_setup, test_create_cursor_invalid_query, e_sqlitedb_fixture_teardown);
	g_test_add ("/EbSdbCursor/Create/InvalidSort", ESqliteDBFixture, &book_closure,
		    e_sqlitedb_fixture_setup, test_create_cursor_invalid_sort, e_sqlitedb_fixture_teardown);
	g_test_add ("/EbSdbCursor/Create/MissingSort", ESqliteDBFixture, &book_closure,
		    e_sqlitedb_fixture_setup, test_create_cursor_missing_sort, e_sqlitedb_fixture_teardown);

	return e_test_server_utils_run ();
}

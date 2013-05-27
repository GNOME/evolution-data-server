/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "data-test-utils.h"

static EbSdbCursorClosure book_closure = { { E_TEST_SERVER_ADDRESS_BOOK, e_sqlitedb_cursor_fixture_setup_book, 0 }, FALSE };

static void
test_cursor_sexp_invalid (EbSdbCursorFixture *fixture,
			  gconstpointer  user_data)
{
	GError *error = NULL;
	EBookQuery *query;
	gchar *sexp = NULL;

	query = e_book_query_field_test (E_CONTACT_NICKNAME, E_BOOK_QUERY_BEGINS_WITH, "Kung Fu");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	if (e_book_backend_sqlitedb_cursor_set_sexp (((ESqliteDBFixture *) fixture)->ebsdb,
						     fixture->cursor, sexp, &error))
		g_error ("Succeeded in setting non-summarized field in the cursor query expression");

	g_assert (error);
	g_assert (g_error_matches (error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_INVALID_QUERY));
}

static void
test_cursor_sexp_calculate_position (EbSdbCursorFixture *fixture,
				     gconstpointer  user_data)
{
	GError *error = NULL;
	EBookQuery *query;
	gint    position = 0, total = 0;
	gchar *sexp = NULL;

	/* Set the cursor to point exactly to 'blackbirds' */
	e_book_backend_sqlitedb_cursor_set_target_contact (((ESqliteDBFixture *) fixture)->ebsdb,
							   fixture->cursor, fixture->contacts[16 - 1]);

	/* Check position */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* blackbird is at position 12 in an unfiltered en_US locale */
	g_assert_cmpint (position, ==, 12);
	g_assert_cmpint (total, ==, 20);

	/* Set new sexp, only contacts with .com email addresses */
	query = e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, ".com");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	if (!e_book_backend_sqlitedb_cursor_set_sexp (((ESqliteDBFixture *) fixture)->ebsdb,
						      fixture->cursor, sexp, &error))
		g_error ("Failed to set sexp: %s", error->message);

	/* Check new position after modified sexp */
	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, &total, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);

	/* 'blackbird' is now at position 8 out of 13, with a filtered set of contacts in en_US locale */
	g_assert_cmpint (position, ==, 8);
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

	g_test_add ("/EbSdbCursor/SetSexp/Invalid", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_setup,
		    test_cursor_sexp_invalid,
		    e_sqlitedb_cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/SetSexp/CalculatePosition", EbSdbCursorFixture, &book_closure,
		    e_sqlitedb_cursor_fixture_setup,
		    test_cursor_sexp_calculate_position,
		    e_sqlitedb_cursor_fixture_teardown);

	return e_test_server_utils_run ();
}

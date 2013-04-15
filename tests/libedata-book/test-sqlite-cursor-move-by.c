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

	EBookQuery *query;
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
	gchar        *sexp = NULL;

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

	/* Allow a surrounding fixture setup to add a query here */
	if (fixture->query) {
		sexp = e_book_query_to_string (fixture->query);
		e_book_query_unref (fixture->query);
		fixture->query = NULL;
	}

	fixture->cursor = e_book_backend_sqlitedb_cursor_new (((ESqliteDBFixture *) fixture)->ebsdb,
							      SQLITEDB_FOLDER_ID,
							      sexp, sort_fields, sort_types, 2, &error);


	g_free (sexp);

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

/********************** Move forward **********************/
static void
test_cursor_move_forward (CursorFixture *fixture,
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
	 *   "Ye Nameless One"
	 *   "Big Bobby Brown"
	 *   "Bobby Brown"
	 *   "James Brown"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[10], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[3], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[2], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[4], E_CONTACT_UID),
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Second batch */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 6, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	/* We asked for 5, but only expect 4 remaining results in the query */
	g_assert_cmpint (g_slist_length (results), ==, 6);

	/* Assert that we got the results ordered as:
	 *   "Janet Jackson"
	 *   "Micheal Jackson"
	 *   "Pink Pony"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[1], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[0], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[8], E_CONTACT_UID),
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);
}

/********************** Move forward on nameless **********************/
static void
test_cursor_move_forward_on_nameless (CursorFixture *fixture,
				      gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;

	/* First batch */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 1, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	g_assert_cmpint (g_slist_length (results), ==, 1);

	/* Assert that we got the results ordered as:
	 *   "Ye Nameless One"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[10], E_CONTACT_UID),
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Second batch */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 3, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	g_assert_cmpint (g_slist_length (results), ==, 3);

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

/********************** Move backwards **********************/
static void
test_cursor_move_backwards (CursorFixture *fixture,
			    gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;

	/* First batch (starts from the end of the results) */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, -5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	g_assert_cmpint (g_slist_length (results), ==, 5);

	/* Assert that we got the results ordered as:
	 *   "Pink Pony"
	 *   "Micheal Jackson"
	 *   "Janet Jackson"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[8], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[0], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[1], E_CONTACT_UID),
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Second batch */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, -6, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	/* We asked for 5, but only expect 4 remaining results in the query */
	g_assert_cmpint (g_slist_length (results), ==, 6);

	/* Assert that we got the results ordered as:
	 *   "Big Bobby Brown", 
	 *   "Bobby Brown",
	 *   "James Brown"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[4], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[2], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[3], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[10], E_CONTACT_UID),
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);
}

/********************** Move forward filtered **********************/
static void
test_cursor_move_filtered_setup (CursorFixture *fixture,
				 gconstpointer  user_data)
{
	fixture->query = e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, ".com");

	cursor_fixture_setup (fixture, user_data);
}

static void
test_cursor_move_forward_filtered (CursorFixture *fixture,
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
	 *   "Bobby Brown"
	 *   "James Brown"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[2], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[4], E_CONTACT_UID),
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Second batch */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	/* We asked for 5, but only expect 3 remaining filtered result in the query */
	g_assert_cmpint (g_slist_length (results), ==, 3);

	/* Assert the last result is:
	 *   "Micheal Jackson"
	 *   "Pink Pony"
	 *   "Purple Pony"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[0], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[8], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[7], E_CONTACT_UID),
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);
}

/********************** Move backwards filtered **********************/
static void
test_cursor_move_backwards_filtered (CursorFixture *fixture,
				     gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;

	/* First batch */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, -5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	g_assert_cmpint (g_slist_length (results), ==, 5);

	/* Assert that we got the results ordered as:
	 *   "Purple Pony"
	 *   "Pink Pony"
	 *   "Micheal Jackson"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[7], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[8], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[0], E_CONTACT_UID),
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);

	/* Second batch */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, -5, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	/* We asked for 5, but only expect 3 remaining filtered result in the query */
	g_assert_cmpint (g_slist_length (results), ==, 3);

	/* Assert the last result is:
	 *   "James Brown"
	 *   "Bobby Brown"
	 *   "Ye Nameless One"
	 */
	assert_contacts_order (results,
			       e_contact_get_const (fixture->contacts[4], E_CONTACT_UID),
			       e_contact_get_const (fixture->contacts[2], E_CONTACT_UID),
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

	g_test_add ("/EbSdbCursor/MoveBy/MoveForward", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_move_forward, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/MoveBy/MoveForwardOnNameless", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_move_forward_on_nameless, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/MoveBy/MoveBackwards", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_move_backwards, cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/MoveBy/Filtered/MoveForward", CursorFixture, &book_closure,
		    test_cursor_move_filtered_setup,
		    test_cursor_move_forward_filtered,
		    cursor_fixture_teardown);
	g_test_add ("/EbSdbCursor/MoveBy/Filtered/MoveBackwards", CursorFixture, &book_closure,
		    test_cursor_move_filtered_setup,
		    test_cursor_move_backwards_filtered,
		    cursor_fixture_teardown);

	return e_test_server_utils_run ();
}

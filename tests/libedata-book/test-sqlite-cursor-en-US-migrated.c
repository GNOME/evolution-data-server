/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "data-test-utils.h"

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, TRUE, NULL };

typedef struct {
	ESqliteDBFixture parent_fixture;

	EbSdbCursor  *cursor;

	EContact *contacts[11];
} CursorFixture;

static void
cursor_fixture_setup (CursorFixture *fixture,
		      gconstpointer  user_data)
{
	ETestServerFixture *base_fixture = (ETestServerFixture *)fixture;
	EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
	EBookSortType sort_types[] = { E_BOOK_SORT_ASCENDING, E_BOOK_SORT_ASCENDING };
	GError       *error = NULL;
	gint          i = 0;

	base_fixture->source_name = g_strdup ("locale-test-source");

	e_sqlitedb_fixture_setup ((ESqliteDBFixture *)fixture, user_data);

	/* Load contacts... */
	if (/* N:Jackson;Micheal */
	    (fixture->contacts[i++] = new_contact_from_test_case ("sorted-1")) == NULL ||
	    /* N:Jackson;Janet */
	    (fixture->contacts[i++] = new_contact_from_test_case ("sorted-2")) == NULL ||
	    /* N:Brown;Bobby */
	    (fixture->contacts[i++] = new_contact_from_test_case ("sorted-3")) == NULL ||
	    /* N:Brown;Big Bobby */
	    (fixture->contacts[i++] = new_contact_from_test_case ("sorted-4")) == NULL ||
	    /* N:Brown;James */
	    (fixture->contacts[i++] = new_contact_from_test_case ("sorted-5")) == NULL ||
	    /* N:%Strange Name;Mister */
	    (fixture->contacts[i++] = new_contact_from_test_case ("sorted-6")) == NULL ||
	    /* N:Goose;Purple */
	    (fixture->contacts[i++] = new_contact_from_test_case ("sorted-7")) == NULL ||
	    /* N:Pony;Purple */
	    (fixture->contacts[i++] = new_contact_from_test_case ("sorted-8")) == NULL ||
	    /* N:Pony;Pink */
	    (fixture->contacts[i++] = new_contact_from_test_case ("sorted-9")) == NULL ||
	    /* N:J;Mister */
	    (fixture->contacts[i++] = new_contact_from_test_case ("sorted-10")) == NULL ||
	    /* FN:Ye Nameless One */
	    (fixture->contacts[i++] = new_contact_from_test_case ("sorted-11")) == NULL)
		g_error ("Failed to load contacts");

	fixture->cursor = e_book_backend_sqlitedb_cursor_new (((ESqliteDBFixture *) fixture)->ebsdb,
							      SQLITEDB_FOLDER_ID,
							      NULL, sort_fields, sort_types, 2, &error);

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

/********************** FetchResults **********************/
static void
test_cursor_fetch (CursorFixture *fixture,
		   gconstpointer  user_data)
{
	GSList *results;
	GError *error = NULL;

	/* First batch */
	results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							  fixture->cursor, 11, &error);

	if (error)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	g_assert_cmpint (g_slist_length (results), ==, 11);

	/* Assert that we got the results ordered in POSIX locale:
	 */
	assert_contacts_order (results,
			       /* FN:Ye Nameless One */
			       e_contact_get_const (fixture->contacts[10], E_CONTACT_UID),
			       /* N:Brown;Big Bobby */
			       e_contact_get_const (fixture->contacts[3], E_CONTACT_UID),
			       /* N:Brown;Bobby */
			       e_contact_get_const (fixture->contacts[2], E_CONTACT_UID),
			       /* N:Brown;James */
			       e_contact_get_const (fixture->contacts[4], E_CONTACT_UID),
			       /* N:Goose;Purple */
			       e_contact_get_const (fixture->contacts[6], E_CONTACT_UID),
			       /* N:J;Mister */
			       e_contact_get_const (fixture->contacts[9], E_CONTACT_UID),
			       /* N:Jackson;Janet */
			       e_contact_get_const (fixture->contacts[1], E_CONTACT_UID),
			       /* N:Jackson;Micheal */
			       e_contact_get_const (fixture->contacts[0], E_CONTACT_UID),
			       /* N:Pony;Pink */
			       e_contact_get_const (fixture->contacts[8], E_CONTACT_UID),
			       /* N:Pony;Purple */
			       e_contact_get_const (fixture->contacts[7], E_CONTACT_UID),
			       /* N:%Strange Name;Mister */
			       e_contact_get_const (fixture->contacts[5], E_CONTACT_UID),
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

	/* Run the addressbook in en_US-UTF-8 locale, causing a migration
	 * of collation keys and a changed sort order
	 */
	g_assert (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

	g_test_add ("/EbSdbCursor/Locale/en_US/Migrated", CursorFixture, &book_closure,
		    cursor_fixture_setup, test_cursor_fetch, cursor_fixture_teardown);

	return e_test_server_utils_run_full (E_TEST_SERVER_KEEP_WORK_DIRECTORY);
}

/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <stdlib.h>
#include <locale.h>
#include <glib/gstdio.h>
#include <libebook/libebook.h>

#include "e-test-server-utils.h"
#include "test-book-cache-utils.h"

static void
test_get_contact (TCUFixture *fixture,
		  gconstpointer user_data)
{
	EContact *contact = NULL;
	EContact *other = NULL;
	GError *error = NULL;

	tcu_add_contact_from_test_case (fixture, "simple-1", &contact);

	if (!e_book_cache_get_contact (fixture->book_cache,
		(const gchar *) e_contact_get_const (contact, E_CONTACT_UID),
		FALSE, &other, NULL, &error)) {
		g_error (
			"Failed to get contact with uid '%s': %s",
			(const gchar *) e_contact_get_const (contact, E_CONTACT_UID),
			error->message);
	}

	g_object_unref (contact);
	g_object_unref (other);
}


static void
test_search_result (TCUFixture *fixture,
		    const gchar *sexp,
		    const gchar *expected_uid)
{
	gboolean success;
	GSList *uids = NULL;
	GError *error = NULL;

	success = e_book_cache_search_uids (fixture->book_cache, sexp, &uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	if (expected_uid) {
		g_assert_cmpint (g_slist_length (uids), ==, 1);
		g_assert_cmpstr (expected_uid, ==, uids->data);
	} else {
		g_assert_cmpint (g_slist_length (uids), ==, 0);
	}

	g_slist_free_full (uids, g_free);
}

static void
test_search_phone (TCUFixture *fixture,
		   const gchar *expected_uid)
{
	test_search_result (fixture, "(contains \"phone\" \"not found\")", NULL);
	test_search_result (fixture, "(contains \"phone\" \"1-221-54237\")", expected_uid);
	test_search_result (fixture, "(contains \"phone\" \"122154237\")", expected_uid);
	test_search_result (fixture, "(contains \"phone\" \"21542\")", expected_uid);
	test_search_result (fixture, "(is \"phone\" \"215423\")", NULL);
	test_search_result (fixture, "(is \"phone\" \"+1-221-5423789\")", expected_uid);
	test_search_result (fixture, "(is \"phone\" \"12215423789\")", expected_uid);
	test_search_result (fixture, "(beginswith \"phone\" \"007\")", NULL);
	test_search_result (fixture, "(beginswith \"phone\" \"+1-221\")", expected_uid);
	test_search_result (fixture, "(beginswith \"phone\" \"1221\")", expected_uid);
	test_search_result (fixture, "(endswith \"phone\" \"789\")", expected_uid);
	test_search_result (fixture, "(endswith \"phone\" \"221-5423789\")", expected_uid);
	test_search_result (fixture, "(endswith \"phone\" \"-221-5423789\")", expected_uid);
	test_search_result (fixture, "(endswith \"phone\" \"2215423789\")", expected_uid);
	test_search_result (fixture, "(endswith \"phone\" \"+1-221-5423789\")", expected_uid);
	test_search_result (fixture, "(endswith \"phone\" \"12215423789\")", expected_uid);
}

static void
test_search (TCUFixture *fixture,
	     gconstpointer user_data)
{
	tcu_add_contact_from_test_case (fixture, "simple-1", NULL);

	test_search_result (fixture, "(exists \"wants_html\")", "simple-1");
	test_search_phone (fixture, NULL);

	tcu_add_contact_from_test_case (fixture, "custom-1", NULL);

	test_search_result (fixture, "(exists \"wants_html\")", "simple-1");
	test_search_phone (fixture, "custom-1");
}

static TCUClosure closures[] = {
	{ NULL },
	{ tcu_setup_empty_book },
	{ NULL },
	{ tcu_setup_empty_book }
};

static const gchar *paths[] = {
	"/EBookCache/DefaultSummary/GetContact",
	"/EBookCache/EmptySummary/GetContact",
	"/EBookCache/DefaultSummary/Search",
	"/EBookCache/EmptySummary/Search"
};

static void
test_init_broken_objects_table (void)
{
	EBookCache *book_cache;
	GError *error = NULL;
	gchar *dirname, *filename;

	dirname = g_dir_make_tmp ("test-book-cache-XXXXXX", &error);
	g_assert_no_error (error);
	filename = g_build_filename (dirname, "cache.db", NULL);

	book_cache = e_book_cache_new (filename, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (book_cache);

	/* a view of a dropped table makes "PRAGMA table_info" fail on the next open */
	e_cache_sqlite_exec (E_CACHE (book_cache),
		"DROP TABLE " E_CACHE_TABLE_OBJECTS ";"
		"CREATE TABLE t (x);"
		"CREATE VIEW " E_CACHE_TABLE_OBJECTS " AS SELECT * FROM t;"
		"DROP TABLE t;", NULL, &error);
	g_assert_no_error (error);
	g_clear_object (&book_cache);

	book_cache = e_book_cache_new (filename, NULL, NULL, &error);
	g_assert_null (book_cache);
	g_assert_nonnull (error);
	g_clear_error (&error);

	g_unlink (filename);
	g_rmdir (dirname);
	g_free (filename);
	g_free (dirname);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ii;

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	tcu_read_args (argc, argv);

	/* Ensure that the client and server get the same locale */
	g_assert_true (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

	for (ii = 0; ii < G_N_ELEMENTS (closures); ii++) {
		g_test_add (
			paths[ii], TCUFixture, &closures[ii],
			tcu_fixture_setup, ii < 2 ? test_get_contact : test_search, tcu_fixture_teardown);
	}

	g_test_add_func ("/EBookCache/InitBrokenObjectsTable", test_init_broken_objects_table);

	return e_test_server_utils_run_full (argc, argv, 0);
}

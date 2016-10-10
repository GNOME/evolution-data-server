/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
 *
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
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include "evolution-data-server-config.h"

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

typedef struct {
	ETestServerFixture parent_fixture;
} MigrationFixture;

typedef struct {
	ETestServerClosure  parent;
	gchar              *version;
} MigrationClosure;

typedef void ( *MigrationTestFunc) (MigrationFixture *fixture,
				    gconstpointer     user_data);

static const gchar *arbitrary_vcard =
	"BEGIN:VCARD\n"
	"UID:arbitrary-vcard\n"
	"FN:Bobby Brown\n"
	"TEL;HOME:+9999999\n"
	"EMAIL;TYPE=work:bobby@brown.org\n"
	"EMAIL;TYPE=home,work:bobby@brown.com\n"
	"END:VCARD\n";

/***********************************************************
 *                         Fixture                         *
 ***********************************************************/
static void
setup_migration_sandbox (const gchar *version)
{
	gchar *dest_dir, *dest_bdb, *dest;
	gchar *src_bdb, *src;
	GFile *src_file, *dest_file;
	GError *error = NULL;

	dest_dir = g_build_filename (EDS_TEST_WORK_DIR, "evolution", "addressbook", version, NULL);
	dest_bdb = g_build_filename (dest_dir, "addressbook.db", NULL);
	dest = g_build_filename (dest_dir, "contacts.db", NULL);

	src_bdb = g_build_filename (EDS_TEST_BUILT_BOOKS, version, "addressbook.db", NULL);
	src = g_build_filename (EDS_TEST_SQLITE_BOOKS, version, "contacts.db", NULL);

	/* Create the directory for the database files */
	g_assert (g_mkdir_with_parents (dest_dir, 0755) == 0);

	/* If there is a BDB for this version, copy it over */
	if (g_file_test (src_bdb, G_FILE_TEST_IS_REGULAR)) {
		src_file = g_file_new_for_path (src_bdb);
		dest_file = g_file_new_for_path (dest_bdb);

		if (!g_file_copy (src_file, dest_file,
				  G_FILE_COPY_OVERWRITE |
				  G_FILE_COPY_TARGET_DEFAULT_PERMS,
				  NULL, NULL, NULL, &error))
			g_error (
				"Failed to setup sandbox for %s migration test: %s",
				version, error->message);

		g_object_unref (src_file);
		g_object_unref (dest_file);
	}

	/* Setup the contacts.db for migration */
	if (g_file_test (src, G_FILE_TEST_IS_REGULAR)) {
		src_file = g_file_new_for_path (src);
		dest_file = g_file_new_for_path (dest);

		if (!g_file_copy (src_file, dest_file,
				  G_FILE_COPY_OVERWRITE |
				  G_FILE_COPY_TARGET_DEFAULT_PERMS,
				  NULL, NULL, NULL, &error))
			g_error (
				"Failed to setup sandbox for %s migration test: %s",
				version, error->message);

		g_object_unref (src_file);
		g_object_unref (dest_file);
	}

	g_free (dest_dir);
	g_free (dest_bdb);
	g_free (dest);

	g_free (src_bdb);
	g_free (src);
}

static void
migration_fixture_setup (MigrationFixture *fixture,
                         gconstpointer user_data)
{
	ETestServerFixture *parent = (ETestServerFixture *) fixture;
	MigrationClosure   *closure = (MigrationClosure *) user_data;

	parent->source_name = g_strdup (closure->version);

	setup_migration_sandbox (closure->version);

	e_test_server_utils_setup ((ETestServerFixture *) parent, user_data);
}

static void
migration_fixture_teardown (MigrationFixture *fixture,
                            gconstpointer user_data)
{
	e_test_server_utils_teardown ((ETestServerFixture *) fixture, user_data);
}

/***********************************************************
 *                          Tests                          *
 ***********************************************************/
static void
test_open (MigrationFixture *fixture,
           gconstpointer user_data)
{

}

static void
test_fetch_contacts (MigrationFixture *fixture,
                     gconstpointer user_data)
{
	EBookClient *book_client;
	GSList *contacts = NULL;
	GError *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!e_book_client_get_contacts_sync (book_client,
					      "",
					      &contacts,
					      NULL, &error))
		g_error ("Failed to fetch contacts: %s", error->message);

	/* Assert some more things related to the actually expected contacts here... */
	g_assert_cmpint (g_slist_length (contacts), ==, 20);

	g_slist_free_full (contacts, g_object_unref);
}

static void
test_add_remove_contact (MigrationFixture *fixture,
                         gconstpointer user_data)
{
	EBookClient *book_client;
	EContact *contact = NULL;
	GError *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	contact = e_contact_new_from_vcard (arbitrary_vcard);

	if (!e_book_client_add_contact_sync (book_client, contact, NULL, NULL, &error))
		g_error ("Failed to add contact: %s", error->message);

	if (!e_book_client_remove_contact_sync (book_client, contact, NULL, &error))
		g_error ("Failed to remove contact: %s", error->message);

	g_object_unref (contact);
}

static GSList *
test_query (EBookClient *book_client,
            gint expected_results,
            EBookQuery *query)
{
	GSList *contacts = NULL;
	GError *error = NULL;
	gchar *sexp;

	sexp = e_book_query_to_string (query);

	if (!e_book_client_get_contacts_sync (book_client, sexp,
					      &contacts, NULL, &error))
		g_error ("Failed to fetch contacts: %s", error->message);

	g_free (sexp);
	e_book_query_unref (query);

	g_assert_cmpint (g_slist_length (contacts), ==, expected_results);

	return contacts;
}

static void
test_query_email (MigrationFixture *fixture,
                  gconstpointer user_data)
{
	EBookClient *book_client;
	GSList *contacts = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	contacts = test_query (
		book_client, 13,
		e_book_query_field_test (
			E_CONTACT_EMAIL,
			E_BOOK_QUERY_ENDS_WITH,
			".com"));

	g_slist_free_full (contacts, g_object_unref);
}

static void
test_query_name (MigrationFixture *fixture,
                  gconstpointer user_data)
{
	EBookClient *book_client;
	GSList *contacts = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	contacts = test_query (
		book_client, 4,
		e_book_query_field_test (
			E_CONTACT_FULL_NAME,
			E_BOOK_QUERY_CONTAINS,
			"cote"));

	g_slist_free_full (contacts, g_object_unref);
}

static void
test_query_phone (MigrationFixture *fixture,
                  gconstpointer user_data)
{
	EBookClient *book_client;
	GSList *contacts = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	contacts = test_query (
		book_client, 4,
		e_book_query_field_test (
			E_CONTACT_TEL,
			E_BOOK_QUERY_CONTAINS,
			"221"));

	g_slist_free_full (contacts, g_object_unref);
}

static EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
static EBookCursorSortType sort_types[] = { E_BOOK_CURSOR_SORT_ASCENDING, E_BOOK_CURSOR_SORT_ASCENDING };

/* For pre-cursor default summary configurations, the
 * E_CONTACT_FAMILY_NAME and E_CONTACT_GIVEN_NAME fields should
 * have been given an E_BOOK_INDEX_SORT_KEY during the upgrade
 * process.
 */
static void
test_cursor_step (MigrationFixture *fixture,
                  gconstpointer user_data)
{
	EBookClient *book_client;
	EBookClientCursor *cursor;
	GError *error = NULL;
	GSList *contacts = NULL;
	gint    n_reported_results;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!e_book_client_get_cursor_sync (book_client,
					    NULL,
					    sort_fields,
					    sort_types,
					    2,
					    &cursor,
					    NULL, &error))
		g_error ("Failed to create a cursor from a migrated book: %s", error->message);

	n_reported_results = e_book_client_cursor_step_sync (
		cursor,
		E_BOOK_CURSOR_STEP_MOVE |
		E_BOOK_CURSOR_STEP_FETCH,
		E_BOOK_CURSOR_ORIGIN_BEGIN,
		10,
		&contacts,
		NULL, &error);
	g_assert_cmpint (n_reported_results, ==, g_slist_length (contacts));
	g_assert_cmpint (e_book_client_cursor_get_position (cursor), ==, 10);
	g_slist_free_full (contacts, g_object_unref);

	n_reported_results = e_book_client_cursor_step_sync (
		cursor,
		E_BOOK_CURSOR_STEP_MOVE |
		E_BOOK_CURSOR_STEP_FETCH,
		E_BOOK_CURSOR_ORIGIN_CURRENT,
		10,
		&contacts,
		NULL, &error);
	g_assert_cmpint (n_reported_results, ==, g_slist_length (contacts));
	g_assert_cmpint (e_book_client_cursor_get_position (cursor), ==, 20);
	g_slist_free_full (contacts, g_object_unref);

	g_object_unref (cursor);
}

/***********************************************************
 *                          Main                           *
 ***********************************************************/
static GList *
list_migration_sandboxes (void)
{
	GDir *dir;
	GError *error = NULL;
	const gchar *filename;
	GList *sandboxes = NULL;

	dir = g_dir_open (EDS_TEST_SQLITE_BOOKS, 0, &error);
	if (!dir)
		g_error (
			"Failed to open migration sandbox directory '%s': %s",
			EDS_TEST_SQLITE_BOOKS, error->message);

	while ((filename = g_dir_read_name (dir)) != NULL) {

		gchar *fullpath = g_build_filename (EDS_TEST_SQLITE_BOOKS, filename, NULL);

		if (g_file_test (fullpath, G_FILE_TEST_IS_DIR)) {

#if defined (TEST_VERSIONS_WITH_BDB)
			sandboxes = g_list_prepend (sandboxes, g_strdup (filename));
#else
			/* We allow compilation of EDS on a system without the db_load utility, if this
			 * is the case then we skip the migration tests from versions of EDS where we
			 * used Berkeley DB
			 */
			gchar *old_bdb = g_build_filename (EDS_TEST_SQLITE_BOOKS, filename, "addressbook.dump", NULL);

			if (!g_file_test (old_bdb, G_FILE_TEST_EXISTS))
				sandboxes = g_list_prepend (sandboxes, g_strdup (filename));

			g_free (old_bdb);
#endif
		}

		g_free (fullpath);
	}

	g_dir_close (dir);

	return sandboxes;
}

static void
migration_closure_free (MigrationClosure *closure)
{
	g_free (closure->version);
	g_slice_free (MigrationClosure, closure);
}

static void
add_test (const gchar *version,
          const gchar *test_name,
          MigrationTestFunc test_func)
{
	MigrationClosure *closure;
	gchar *path;

	closure = g_slice_new0 (MigrationClosure);

	closure->parent.type = E_TEST_SERVER_ADDRESS_BOOK;
	closure->parent.destroy_closure_func = (GDestroyNotify) migration_closure_free;
	closure->parent.keep_work_directory = TRUE;
	closure->version = g_strdup (version);

	path = g_strdup_printf (
		"/Migration/From-%s/%s",
		version, test_name);

	g_test_add (
		path, MigrationFixture, closure,
		migration_fixture_setup,
		test_func,
		migration_fixture_teardown);

	g_free (path);
}

gint
main (gint argc,
      gchar **argv)
{
	GList *sandboxes, *l;
	gint ret;

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	sandboxes = list_migration_sandboxes ();

	for (l = sandboxes; l; l = l->next) {
		gchar *version = l->data;

		add_test (version, "Open", test_open);
		add_test (version, "FetchContacts", test_fetch_contacts);
		add_test (version, "AddRemoveContact", test_add_remove_contact);
		add_test (version, "Query/FullName", test_query_name);
		add_test (version, "Query/Phone", test_query_phone);
		add_test (version, "Query/Email", test_query_email);
		add_test (version, "Cursor/Step", test_cursor_step);
	}

	g_list_free_full (sandboxes, g_free);

	ret = e_test_server_utils_run ();

	return ret;
}

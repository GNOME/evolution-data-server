/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright Copyright (C) 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Mathias Hasselmann <mathias@openismus.com>
 */

#include "config.h"

#include <errno.h>
#include <locale.h>
#include <langinfo.h>
#include <string.h>

#include <libebook/libebook.h>
#include <glib/gstdio.h>

#include "e-test-server-utils.h"

typedef struct _TestFixture TestFixture;
typedef struct _TestParams TestParams;

typedef enum {
	INDIRECT_ACCESS,
	DIRECT_ACCESS
} AccessMode;

typedef void (* TestFunc)(TestFixture   *fixture,
                          gconstpointer  params);

struct _TestFixture {
	ETestServerFixture base_fixture;
	GSList            *contacts;
};

struct _TestParams {
	ETestServerClosure base_closure;

	gchar             *db_version;
	AccessMode         access_mode;
};

static void setup_book_directory (ESource            *scratch,
				  ETestServerClosure *closure);


/****************************************************************
 *                       Fixture handling                       *
 ****************************************************************/
static TestParams *
create_params (const gchar              *db_version,
               AccessMode                access_mode,
               ETestSourceCustomizeFunc  setup_source)
{
	TestParams *params = g_slice_new0 (TestParams);

	params->base_closure.type      = E_TEST_SERVER_ADDRESS_BOOK;
	params->base_closure.customize = setup_source;

	params->access_mode = access_mode;
	params->db_version  = g_strdup (db_version);

	return params;
}

static void
free_params (TestParams *params)
{
	g_free (params->db_version);

	g_slice_free (TestParams, params);
}

static void
setup (TestFixture    *fixture,
       gconstpointer   data)
{
	e_test_server_utils_setup (&fixture->base_fixture, data);
}

static void
teardown (TestFixture   *fixture,
          gconstpointer  data)
{
	const TestParams *const params = data;

	e_test_server_utils_teardown (&fixture->base_fixture, data);

	g_slist_free_full (fixture->contacts, g_object_unref);
	fixture->contacts = NULL;

	free_params ((TestParams *) params);
}

static void
add_test (const gchar             *path,
          const gchar             *db_version,
          AccessMode               access_mode,
          ETestSourceCustomizeFunc setup_source,
          TestFunc                 test_func)
{
	TestParams *params;

	params = create_params (db_version, access_mode, setup_source);
	g_test_add (path, TestFixture, params, setup, test_func, teardown);
}

static gchar *
get_bookdir (void)
{
	return g_build_filename (g_get_user_data_dir (), "evolution", "addressbook", "test-address-book", NULL);
}

static gchar *
get_datadir (const TestParams *params)
{
	return g_build_filename (SRCDIR, "../data/dumps", params->db_version, NULL);
}

/****************************************************************
 *                      Addressbook mocking                     *
 ****************************************************************/
static void
setup_book_directory (ESource            *scratch,
		      ETestServerClosure *closure)
{
	const TestParams *const params = (const TestParams *) closure;

	gchar *const bookdir = get_bookdir ();
	gchar *const photodir = g_build_filename (bookdir, "photos", NULL);

	gchar *const datadir = get_datadir (params);
	gchar *const bdb_filename = g_build_filename (datadir, "addressbook.db_dump", NULL);
	gchar *const sqlite_filename = g_build_filename (datadir, "contacts.sql", NULL);

	GError *error = NULL;

	if (params->access_mode == DIRECT_ACCESS)
		g_setenv ("DEBUG_DIRECT", "1", TRUE);
	else
		g_unsetenv ("DEBUG_DIRECT");

	if (g_test_verbose ())
		g_print ("Creating mock addressbook at \"%s\".\n", bookdir);
	if (g_mkdir_with_parents (photodir, 0700) != 0)
		g_error ("Cannot create \"%s\": %s", bookdir, g_strerror (errno));

	{
		/* Load the Berkley DB portion from db_dump file */
		const gchar *argv[] = { "db_load", "-f", bdb_filename, "addressbook.db", NULL };
		gint exit_status;

		if (g_test_verbose ())
			g_print ("Initializing Berkeley database from \"%s\".\n", bdb_filename);

		if (!g_spawn_sync (bookdir, (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH,
		                   NULL, NULL, NULL, NULL, &exit_status, &error))
			g_error ("Cannot run db_load: %s", error->message);

		g_assert (WIFEXITED (exit_status));
		g_assert_cmpint (WEXITSTATUS (exit_status), ==, 0);
	}

	{
		/* Load the QSLite portion from SQL dump */
		const gchar *argv[] = { "sqlite3", "-batch", "-bail", "-init", sqlite_filename, "contacts.db", NULL };
		gint exit_status;

		if (g_test_verbose ())
			g_print ("Initializing SQLite database from \"%s\".\n", sqlite_filename);

		if (!g_spawn_sync (bookdir, (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH,
		                   NULL, NULL, NULL, NULL, &exit_status, &error))
			g_error ("Cannot run sqlite3: %s", error->message);

		g_assert (WIFEXITED (exit_status));
		g_assert_cmpint (WEXITSTATUS (exit_status), ==, 0);
	}

	g_free (sqlite_filename);
	g_free (bdb_filename);
	g_free (datadir);

	g_free (photodir);
	g_free (bookdir);
}

/****************************************************************
 *                       The actual tests                       *
 ****************************************************************/

static void
verify_contacts (GSList *contacts)
{
	GSList *l;

	g_assert_cmpint (g_slist_length (contacts), ==, 5);

	for (l = contacts; l; l = l->next) {
		EVCardAttribute *const rev = e_vcard_get_attribute (l->data, EVC_REV);
		gboolean is_date;
		GList *values;
		GTimeVal tv;

		g_assert (rev != NULL);

		values = e_vcard_attribute_get_values (rev);
		g_assert_cmpint (g_list_length (values), ==, 1);

		is_date = g_time_val_from_iso8601 (values->data, &tv);
		g_assert (is_date);
	}
}

static void
contacts_added_cb (EBookClientView *view,
                   GSList          *contacts,
                   TestFixture     *fixture)
{
	ETestServerFixture *base = (ETestServerFixture *)fixture;
	GSList *l;

	for (l = contacts; l; l = l->next)
		fixture->contacts = g_slist_prepend (fixture->contacts, g_object_ref (l->data));

	if (g_slist_length (fixture->contacts) >= 5)
		g_main_loop_quit (base->loop);
}

static void
view_complete_cb (EBookClientView *view,
                  const GError    *error,
                  TestFixture     *fixture)
{
	ETestServerFixture *base = (ETestServerFixture *)fixture;

	if (error)
		g_error ("View failed: %s", error->message);

	g_main_loop_quit (base->loop);
}

static gboolean
timeout_cb (gpointer data)
{
	ETestServerFixture *fixture = data;

	g_assert_not_reached ();
	g_main_loop_quit (fixture->loop);

	return FALSE;
}

static void
test_book_client_view (TestFixture   *fixture,
                       gconstpointer  params)
{
	GError *error = NULL;
	EBookClientView *view;
	EBookClient *client;
	guint timeout_id;
	GSList *fields;
	ETestServerFixture *base = (ETestServerFixture *)fixture;

	client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!e_book_client_get_view_sync (client,
	                                  "(contains \"x-evolution-any-field\" \"\")",
	                                  &view, NULL, &error))
		g_error ("Failed to create the view: %s", error->message);

	fields = NULL;
	fields = g_slist_prepend (fields, (gchar *) e_contact_field_name (E_CONTACT_UID));
	fields = g_slist_prepend (fields, (gchar *) e_contact_field_name (E_CONTACT_REV));

	e_book_client_view_set_fields_of_interest (view, fields, &error);

	if (error)
		g_error ("Failed to set fields of interest: %s", error->message);

	g_signal_connect (view, "objects-added", G_CALLBACK (contacts_added_cb), fixture);
	g_signal_connect (view, "complete", G_CALLBACK (view_complete_cb), fixture);
	timeout_id = g_timeout_add_seconds (5, timeout_cb, fixture);

	e_book_client_view_start (view, &error);

	if (error)
		g_error ("Failed to start view: %s", error->message);

	g_main_loop_run (base->loop);
	g_source_remove (timeout_id);

	g_signal_handlers_disconnect_by_data (view, fixture);
	e_book_client_view_stop (view, &error);

	if (error)
		g_error ("Failed to stop view: %s", error->message);

	g_object_unref (view);

	verify_contacts (fixture->contacts);
}

static void
test_book_client (TestFixture   *fixture,
                  gconstpointer  params)
{
	GError *error = NULL;
	EBookClient *client;

	client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!e_book_client_get_contacts_sync (client,
	                                      "(contains \"x-evolution-any-field\" \"\")",
	                                      &fixture->contacts, NULL, &error))
		g_error ("Failed to read contacts: %s", error->message);

	verify_contacts (fixture->contacts);
}

static void
setup_old_default_summary (ESource            *scratch,
                           ETestServerClosure *closure)
{
	ESourceBackendSummarySetup *setup;

	g_type_ensure (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP);
	setup = e_source_get_extension (scratch, E_SOURCE_EXTENSION_BACKEND_SUMMARY_SETUP);
	e_source_backend_summary_setup_set_summary_fields (setup,
	                                                   E_CONTACT_UID,
	                                                   E_CONTACT_REV,
	                                                   E_CONTACT_FILE_AS,
	                                                   E_CONTACT_NICKNAME,
	                                                   E_CONTACT_FULL_NAME,
	                                                   E_CONTACT_GIVEN_NAME,
	                                                   E_CONTACT_FAMILY_NAME,
	                                                   E_CONTACT_EMAIL_1,
	                                                   E_CONTACT_EMAIL_2,
	                                                   E_CONTACT_EMAIL_3,
	                                                   E_CONTACT_EMAIL_4,
	                                                   E_CONTACT_IS_LIST,
	                                                   E_CONTACT_LIST_SHOW_ADDRESSES,
	                                                   E_CONTACT_WANTS_HTML,
	                                                   0);
}

static void
test_column_names (TestFixture   *fixture,
                   gconstpointer  data)
{
	const TestParams *const params = data;

	gchar *const bookdir = get_bookdir ();
	gchar *const datadir = get_datadir (params);
	gchar *const script_filename = g_build_filename (datadir, "contacts.sql", NULL);
	const gchar *argv[] = { "sqlite3", "-batch", "contacts.db", "pragma table_info(folder_id)", NULL };

	gint exit_status;
	GError *error = NULL;
	gchar *table_info = NULL;
	gchar *script = NULL, *stmt;
	GMatchInfo *match = NULL;
	GRegex *regex = NULL;
	gint start, end;

	if (!g_spawn_sync (bookdir, (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH,
		           NULL, NULL, &table_info, NULL, &exit_status, &error))
		g_error ("Cannot run SQLite: %s", error->message);

	g_assert (WIFEXITED (exit_status));
	g_assert_cmpint (WEXITSTATUS (exit_status), ==, 0);

	if (g_test_verbose ())
		g_print ("%s", table_info);

	if (!g_file_get_contents (script_filename, &script, NULL, &error))
		g_error ("Cannot SQLite database script: %s.", error->message);

	g_assert (script != NULL);

	regex = g_regex_new ("^CREATE\\s+TABLE\\s+([\"']?)folder_id\\1([^;]*);",
	                     G_REGEX_CASELESS | G_REGEX_MULTILINE, 0, NULL);

	if (!g_regex_match (regex, script, 0, &match)
		|| !g_match_info_fetch_pos (match, 2, &start, &end))
		g_error ("Cannot find \"CREATE TABLE\" statement for for folder_id table.");

	g_regex_unref (regex);
	stmt = script + start;
	script[end] = '\0';

	regex = g_regex_new ("[(,]\\s+(\\w+)\\s+(\\w+)\\b", 0, 0, NULL);

	while (g_regex_match (regex, stmt, 0, &match)) {
		const gchar *colname, *coltype;
		gchar *pattern;

		if (!g_match_info_fetch_pos (match, 1, &start, &end))
			break;

		colname = stmt + start;
		stmt[end] = '\0';

		if (!g_match_info_fetch_pos (match, 2, &start, &end))
			break;

		coltype = stmt + start;
		stmt[end] = '\0';
		stmt += end + 1;

		pattern = g_strdup_printf ("|%s|%s|", colname, coltype);

		if (!strstr (table_info, pattern))
			g_error ("Cannot find \"%s\" column.", colname);

		g_free (pattern);
	}

	g_regex_unref (regex);
	g_free (table_info);
	g_free (script_filename);
	g_free (datadir);
	g_free (bookdir);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	/* Setting with U.S. locale for addresses */
	g_setenv ("LC_ADDRESS", "en_US.UTF-8", TRUE);
	setlocale (LC_ADDRESS, "");

	add_test ("/upgrade/0.2/dbus/book-client", "0.2", INDIRECT_ACCESS, setup_book_directory, test_book_client);
	add_test ("/upgrade/0.2/dbus/book-client-view", "0.2", INDIRECT_ACCESS, setup_book_directory, test_book_client_view);
	add_test ("/upgrade/0.2/direct/book-client", "0.2", DIRECT_ACCESS, setup_book_directory, test_book_client);
	add_test ("/upgrade/0.2/direct/book-client-view", "0.2", DIRECT_ACCESS, setup_book_directory, test_book_client_view);
	add_test ("/upgrade/0.2/column-names", "0.2", INDIRECT_ACCESS, setup_old_default_summary, test_column_names);

	return e_test_server_utils_run ();
}


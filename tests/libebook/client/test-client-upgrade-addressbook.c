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

#include "client-test-utils.h"

typedef struct _TestFixture TestFixture;
typedef struct _TestParams TestParams;

typedef enum {
	INDIRECT_ACCESS,
	DIRECT_ACCESS
} AccessMode;

typedef void (* TestFunc)(TestFixture   *fixture,
                          gconstpointer  params);

struct _TestFixture {
	GMainLoop *loop;
	ESourceRegistry *registry;
	EBookClient *client;
	GSList *contacts;
};

struct _TestParams {
	gchar      *db_version;
	AccessMode  access_mode;
};

/****************************************************************
 *                      Addressbook mocking                     *
 ****************************************************************/

static void
create_bookdir (const gchar *const uid,
                const gchar *const db_version)
{
	gchar *const bookdir = g_build_filename (g_get_user_data_dir (), "evolution", "addressbook", uid, NULL);
	gchar *const photodir = g_build_filename (bookdir, "photos", NULL);

	gchar *const datadir = g_build_filename (SRCDIR, "../data/dumps", db_version, NULL);
	gchar *const bdb_filename = g_build_filename (datadir, "addressbook.db_dump", NULL);
	gchar *const sqlite_filename = g_build_filename (datadir, "contacts.sql", NULL);

	GError *error = NULL;

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

static gboolean
wait_cb (gpointer data)
{
	TestFixture *fixture = data;
	g_main_loop_quit (fixture->loop);
	return FALSE;
}

static void
create_source (TestFixture *fixture,
               const gchar *uid)
{
	GError *error = NULL;
	ESourceBackend  *backend;
	ESource *scratch;

	fixture->registry = e_source_registry_new_sync (NULL, &error);

	if (!fixture->registry)
		g_error ("Unable to create the registry: %s", error->message);

	scratch = e_source_new_with_uid (uid, NULL, &error);

	if (!scratch)
		g_error ("Failed to create source with uid \"%s\": %s", uid, error->message);

	e_source_set_display_name (scratch, "Mock Addressbook");

	backend = e_source_get_extension (scratch, E_SOURCE_EXTENSION_ADDRESS_BOOK);
	e_source_backend_set_backend_name (backend, "local");

	if (!e_source_registry_commit_source_sync (fixture->registry, scratch, NULL, &error))
		g_error ("Unable to add new source to the registry for uid \"%s\": %s", uid, error->message);

	g_object_unref (scratch);

	/* Give the backend a chance to see the source */
	g_timeout_add (250, wait_cb, fixture);
	g_main_loop_run (fixture->loop);
}

/****************************************************************
 *                       Fixture handling                       *
 ****************************************************************/

static void
setup (TestFixture    *fixture,
       gconstpointer   data)
{
	const TestParams *params = data;

	guint64 now = g_get_real_time ();
	gchar *const uid = g_strdup_printf ("mock-book-%" G_GINT64_FORMAT, now);
	GError *error = NULL;
	ESource *source;

	fixture->loop = g_main_loop_new (NULL, FALSE);

	create_bookdir (uid, params->db_version);
	create_source (fixture, uid);

	source = e_source_new_with_uid (uid, NULL, &error);

	if (!source)
		g_error ("Failed to create source with uid \"%s\": %s", uid, error->message);

	if (params->access_mode == DIRECT_ACCESS) {
		fixture->client = e_book_client_new_direct (fixture->registry, source, &error);
	} else {
		fixture->client = e_book_client_new (source, &error);
	}

	if (!fixture->client)
		g_error ("Failed to create addressbook client: %s", error->message);
	if (!e_client_open_sync (E_CLIENT (fixture->client), TRUE, NULL, &error))
		g_error ("Failed to open addressbook client: %s", error->message);

	g_object_unref (source);
	g_free (uid);
}

static void
teardown (TestFixture   *fixture,
          gconstpointer  data)
{
	const TestParams *const params = data;

	if (fixture->client)
		g_object_unref (fixture->client);
	if (fixture->registry)
		g_object_unref (fixture->registry);

	g_slist_free_full (fixture->contacts, g_object_unref);
	g_main_loop_unref (fixture->loop);

	g_free (params->db_version);
	g_slice_free (TestParams, (TestParams *) params);
}

static void
add_test (const gchar *path,
          const gchar *db_version,
          AccessMode   access_mode,
          TestFunc     test_func)
{
	TestParams *params = g_slice_new0 (TestParams);
	params->db_version = g_strdup (db_version);
	params->access_mode = access_mode;
	g_test_add (path, TestFixture, params, setup, test_func, teardown);
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
	GSList *l;

	for (l = contacts; l; l = l->next)
		fixture->contacts = g_slist_prepend (fixture->contacts, g_object_ref (l->data));

	if (g_slist_length (fixture->contacts) >= 5)
		g_main_loop_quit (fixture->loop);
}

static void
view_complete_cb (EBookClientView *view,
                  const GError    *error,
                  TestFixture     *fixture)
{
	if (error)
		g_error ("View failed: %s", error->message);

	g_main_loop_quit (fixture->loop);
}

static gboolean
timeout_cb (gpointer data)
{
	TestFixture *fixture = data;

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
	guint timeout_id;
	GSList *fields;

	if (!e_book_client_get_view_sync (fixture->client,
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

	g_main_loop_run (fixture->loop);
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

	if (!e_book_client_get_contacts_sync (fixture->client,
	                                      "(contains \"x-evolution-any-field\" \"\")",
	                                      &fixture->contacts, NULL, &error))
		g_error ("Failed to read contacts: %s", error->message);

	verify_contacts (fixture->contacts);
}

gint
main (gint    argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);

	main_initialize ();

	add_test ("/upgrade/0.2/dbus/book-client", "0.2", INDIRECT_ACCESS, test_book_client);
	add_test ("/upgrade/0.2/dbus/book-client-view", "0.2", INDIRECT_ACCESS, test_book_client_view);
	add_test ("/upgrade/0.2/direct/book-client", "0.2", DIRECT_ACCESS, test_book_client);
	add_test ("/upgrade/0.2/direct/book-client-view", "0.2", DIRECT_ACCESS, test_book_client_view);

	return g_test_run ();
}


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

#include <glib.h>

#include <libedataserver/eds-version.h>

#if EDS_CHECK_VERSION(3,6,0)
#  include <libebook/libebook.h>
/* In 3.10, we support the --use-test-sandbox option */
#  if EDS_CHECK_VERSION(3,10,0)
#    include "client-test-utils.h"
#    include "e-test-server-utils.h"
#  endif
#else
#  include <libebook/e-contact.h>
#  include <libebook/e-book.h>
#endif

#if EDS_CHECK_VERSION(3,6,0)
typedef EBookClient Book;
#else
typedef EBook Book;
#endif

static Book *create_book  (const gchar *book_id);
static void  add_contacts (Book *book, GSList *contacts);

static gchar    *book_id = NULL;
static gchar    *contacts_directory = NULL;
static gboolean  test_sandbox = FALSE;

static GOptionEntry option_entries[] = {
	{"book-id", 'b', 0, G_OPTION_ARG_STRING, &book_id,
	 "The book identifier string", NULL },
	{"contacts-directory", 'd', 0, G_OPTION_ARG_FILENAME, &contacts_directory,
	 "The directory from where to read the contact files", NULL },
	{"use-test-sandbox", 't', 0, G_OPTION_ARG_NONE, &test_sandbox,
	 "Whether to use the test case sandbox to create the test book "
	 "(Only available after EDS 3.10)", NULL },
	{ NULL }
};

/********************************************************
 *            Loading contacts from directory           *
 ********************************************************/
static EContact *
contact_from_file (const gchar *vcard_file)
{
	EContact *contact;
	GError *error;
	gchar *vcard = NULL;

	if (!g_file_get_contents (vcard_file, &vcard, NULL, &error))
		g_error ("Failed to load vcard: %s", error->message);

	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);

	return contact;
}

static GSList *
load_contacts (const gchar *vcard_directory)
{
	GDir *dir;
	GError *error = NULL;
	const gchar *filename;
	GSList *contacts = NULL;

	dir = g_dir_open (vcard_directory, 0, &error);
	if (!dir)
		g_error ("Failed to open vcard directory '%s': %s", vcard_directory, error->message);

	while ((filename = g_dir_read_name (dir)) != NULL) {

		if (g_str_has_suffix (filename, ".vcf")) {
			gchar *fullpath = g_build_filename (vcard_directory, filename, NULL);
			EContact *contact;

			contact = contact_from_file (fullpath);
			contacts = g_slist_prepend (contacts, contact);

			g_free (fullpath);
		}
	}

	g_dir_close (dir);

	return g_slist_reverse (contacts);
}

/********************************************************
 *                   Creating the Book                  *
 ********************************************************/
#if EDS_CHECK_VERSION(3,6,0)

typedef struct {
	GMainLoop *loop;
	const gchar *book_id;
	EBookClient *book;
} SourceAddedData;

static void
source_added (ESourceRegistry *registry,
              ESource *source,
              gpointer data)
{
	SourceAddedData *added_data = (SourceAddedData *) data;
	GError *error = NULL;

	if (g_strcmp0 (e_source_get_uid (source), added_data->book_id) != 0)
		return;

	/* Open the address book */
#if EDS_CHECK_VERSION(3,8,0)
	added_data->book = (EBookClient *) e_book_client_connect_sync (source, (guint32) -1, NULL, &error);
#else
	/* With 3.6 it's a bit more tricky */
	added_data->book = e_book_client_new (source, &error);
	if (added_data->book &&
	    !e_client_open_sync (E_CLIENT (added_data->book), FALSE, NULL, &error))
		g_error ("Failed to open addressbook: %s", error->message);
#endif

	if (!added_data->book)
		g_error ("Failed to create addressbook: %s", error->message);

	if (added_data->loop)
		g_main_loop_quit (added_data->loop);
}

static gboolean
create_source_timeout (gpointer user_data)
{
	g_error ("Timed out while waiting for ESource creation from the registry");

	return FALSE;
}

static EBookClient *
create_book (const gchar *book_id)
{
	ESourceRegistry *registry;
	ESource *scratch;
	ESourceBackend *backend = NULL;
	GError  *error = NULL;
	SourceAddedData data = { NULL, NULL, NULL };

	g_return_val_if_fail (book_id != NULL, NULL);

	data.loop = g_main_loop_new (NULL, FALSE);

	registry = e_source_registry_new_sync (NULL, &error);
	if (!registry)
		g_error ("Unable to create the registry: %s", error->message);

	/* Listen to the registry for our added source */
	data.book_id = book_id;
	g_signal_connect (
		registry, "source-added",
		G_CALLBACK (source_added), &data);

	/* Now create a scratch source for our addressbook */
	scratch = e_source_new_with_uid (book_id, NULL, &error);
	if (!scratch)
		g_error ("Failed to create scratch source: %s", error->message);

	/* Ensure the new ESource will be a local addressbook source */
	backend = e_source_get_extension (scratch, E_SOURCE_EXTENSION_ADDRESS_BOOK);
	e_source_backend_set_backend_name (backend, "local");

	/* Commit the source to the registry */
	if (!e_source_registry_commit_source_sync (registry, scratch, NULL, &error))
		g_error ("Unable to add new addressbook source to the registry: %s", error->message);

	g_object_unref (scratch);

	if (data.book == NULL) {
		g_timeout_add_seconds (20, create_source_timeout, NULL);
		g_main_loop_run (data.loop);

		/* By now we aborted or we have an addressbook created */
		g_warn_if_fail (data.book != NULL);
	}

	g_main_loop_unref (data.loop);
	g_object_unref (registry);

	return data.book;
}

#else

static EBook *
create_book (const gchar *book_id)
{
	EBook *book;
	GError *error = NULL;

	book = e_book_new_from_uri (book_id, &error);

	if (!book)
		g_error ("Error creating book: %s", error->message);

	if (!e_book_open (book, FALSE, &error))
		g_error ("Error opening book: %s", error->message);

	return book;
}

#endif

/********************************************************
 *                   Adding the Contacts                *
 ********************************************************/
#if EDS_CHECK_VERSION(3,6,0)

static void
add_contacts (Book *book,
              GSList *contacts)
{
	GError *error = NULL;

	if (!e_book_client_add_contacts_sync (book, contacts, NULL, NULL, &error))
		g_error ("Failed to add contacts: %s", error->message);
}

#else

static void
add_contacts (Book *book,
              GSList *contacts)
{
	GError *error = NULL;
	GSList *l;

	for (l = contacts; l; l = l->next) {
		EContact *contact = l->data;

		if (!e_book_add_contact (book, contact, &error))
			g_error ("Failed to add a contact: %s", error->message);
	}
}

#endif

/* Support running this in a test case in 3.10 and later */
#if EDS_CHECK_VERSION(3,10,0)

static ETestServerClosure book_closure = {
	E_TEST_SERVER_ADDRESS_BOOK,
	NULL, /* Source customization function */
	0,    /* Calendar Type */
	TRUE, /* Keep the working sandbox after the test, don't remove it */
	NULL, /* Destroy Notify function */
};

static void
setup_migration_setup (ETestServerFixture *fixture,
                       gconstpointer user_data)
{
	fixture->source_name = g_strdup_printf ("%d.%d", EDS_MAJOR_VERSION, EDS_MINOR_VERSION);
	e_test_server_utils_setup (fixture, user_data);
}

static void
setup_migration_run (ETestServerFixture *fixture,
                     gconstpointer user_data)
{
	EBookClient *book_client;
	GSList      *contacts;

	contacts = load_contacts (contacts_directory);

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	add_contacts (book_client, contacts);
	g_slist_free_full (contacts, g_object_unref);
}

#endif

/********************************************************
 *                        main()                        *
 ********************************************************/
gint
main (gint argc,
      gchar *argv[])
{
	GOptionContext *option_context;
	GOptionGroup *option_group;
	GError *error = NULL;
	GSList *contacts;
	Book *book;

	option_context = g_option_context_new (NULL);
	g_option_context_set_summary (
		option_context,
		"Populate a database for migration tests.");

	option_group = g_option_group_new (
		"setup migration test",
		"Setup Migration options",
		"Setup Migration options", NULL, NULL);
	g_option_group_add_entries (option_group, option_entries);
	g_option_context_set_main_group (option_context, option_group);

	if (!g_option_context_parse (option_context, &argc, &argv, &error))
		g_error ("Failed to parse program arguments: %s", error->message);

	if (!book_id || !contacts_directory)
		g_error (
			"Must provide the book identifier and contacts directory\n%s",
			g_option_context_get_help (option_context, TRUE, NULL));

	if (test_sandbox) {

#if EDS_CHECK_VERSION(3,10,0)
		g_test_init (&argc, &argv, NULL);
		g_test_add (
			"/SettingUpMigrationTest",
			ETestServerFixture,
			&book_closure,
			setup_migration_setup,
			setup_migration_run,
			e_test_server_utils_teardown);

		return e_test_server_utils_run ();
#else
		g_error (
			"Requested sandboxed setup but that is not available until EDS 3.10, current version is %d.%d",
			EDS_MAJOR_VERSION, EDS_MINOR_VERSION);
#endif
	} else {

		contacts = load_contacts (contacts_directory);

		book = create_book (book_id);
		add_contacts (book, contacts);

		g_object_unref (book);

		g_slist_free_full (contacts, g_object_unref);
	}

	return 0;
}

/*
 * Copyright (C) 2013 Intel Corporation
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include "cursor-data.h"

#define CURSOR_DATA_SOURCE_ID "cursor-example-book"

/* We need to spin the main loop a bit to wait for the ESource
 * to be created. This particular API is admittedly cumbersome,
 * hopefully this will be fixed in a later version so that
 * the ESource can be synchronously created.
 *
 * Just an example so lets just use some global variables here...
 */
static EBookClient *address_book = NULL;
static ESource     *address_book_source = NULL;

static void
cursor_data_source_added (ESourceRegistry *registry,
                          ESource *source,
                          gpointer data)
{
	GError    *error = NULL;
	GMainLoop *loop = (GMainLoop *) data;

	if (g_strcmp0 (e_source_get_uid (source), CURSOR_DATA_SOURCE_ID) != 0)
		return;

	/* Open the address book */
	address_book = (EBookClient *) e_book_client_connect_sync (source, 30, NULL, &error);
	if (!address_book)
		g_error ("Unable to create the test book: %s", error->message);

	address_book_source = g_object_ref (source);

	if (loop)
		g_main_loop_quit (loop);
}

static gboolean
cursor_data_source_timeout (gpointer user_data)
{
	g_error ("Timed out while waiting for ESource creation from the registry");

	return FALSE;
}

/*
 * Helper function to load a contact from a vcard file.
 */
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

/*
 * Load all the contacts from 'vcard_directory', and add them to 'client'.
 */
static void
load_contacts (EBookClient *client,
               const gchar *vcard_directory)
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

	if (contacts != NULL) {

		if (!e_book_client_add_contacts_sync (client, contacts, E_BOOK_OPERATION_FLAG_NONE, NULL, NULL, &error)) {

			/* If they were already added, ignore the error */
			if (g_error_matches (error, E_BOOK_CLIENT_ERROR,
					     E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS))
				g_clear_error (&error);
			else
				g_error ("Failed to add test contacts: %s", error->message);
		}
	}
}

/*
 * Create an EBookClient cursor sorted by family name, and then by given name as
 * the secondary sort key.
 */
static EBookClientCursor *
get_cursor (EBookClient *book_client)
{
  EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
  EBookCursorSortType sort_types[] = { E_BOOK_CURSOR_SORT_ASCENDING, E_BOOK_CURSOR_SORT_ASCENDING };
  EBookClientCursor *cursor = NULL;
  GError *error = NULL;

  if (!e_book_client_get_cursor_sync (book_client,
				      NULL,
				      sort_fields,
				      sort_types,
				      2,
				      &cursor,
				      NULL,
				      &error)) {
	  g_warning ("Unable to create cursor");
	  g_clear_error (&error);
  }

  return cursor;
}

/* Entry point for this file, here we take care of
 * creating the addressbook if it doesn't exist,
 * getting an EBookClient, and creating our EBookClientCursor.
 */
EBookClient *
cursor_load_data (const gchar *vcard_path,
                  EBookClientCursor **ret_cursor)
{
	ESourceRegistry *registry;
	ESource *scratch;
	ESourceBackend *backend = NULL;
	GMainLoop *loop;
	GError  *error = NULL;
	EBookClient *ret_book;

	g_return_val_if_fail (vcard_path != NULL, NULL);
	g_return_val_if_fail (ret_cursor != NULL, NULL);

	g_print ("Cursor loading data from %s\n", vcard_path);

	loop = g_main_loop_new (NULL, FALSE);

	registry = e_source_registry_new_sync (NULL, &error);
	if (!registry)
		g_error ("Unable to create the registry: %s", error->message);

	/* Listen to the registry for our added source */
	g_signal_connect (
		registry, "source-added",
		G_CALLBACK (cursor_data_source_added), loop);

	/* Now create a scratch source for our addressbook */
	scratch = e_source_new_with_uid (CURSOR_DATA_SOURCE_ID, NULL, &error);

	/* Ensure the new ESource will be a local addressbook source */
	backend = e_source_get_extension (scratch, E_SOURCE_EXTENSION_ADDRESS_BOOK);
	e_source_backend_set_backend_name (backend, "local");

	/* Now is the right time to use the ESourceBackendSummarySetup to configure
	 * your newly created addressbook. This configuration should happen on the
	 * scratch source before calling e_source_registry_commit_source_sync().
	 */

	/* Commit the source to the registry */
	if (!e_source_registry_commit_source_sync (registry, scratch, NULL, &error)) {

		/* It's possible the source already exists if we already ran the example with this data server */
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
			/* If so... then just call our callback early */
			ESource *source = e_source_registry_ref_source (registry, CURSOR_DATA_SOURCE_ID);

			g_clear_error (&error);
			g_return_val_if_fail (E_IS_SOURCE (source), NULL);

			/* Run the callback which creates the addressbook client connection */
			cursor_data_source_added (registry, source, NULL);
			g_object_unref (source);
		} else
			g_error ("Unable to add new addressbook source to the registry: %s", error->message);
	}

	g_object_unref (scratch);

	/* Give EDS a little time to actually create the ESource remotely and
	 * also have a copy if it cached locally, wait for the "source-added"
	 * signal.
	 */
	if (address_book == NULL) {
		g_timeout_add_seconds (20, cursor_data_source_timeout, NULL);
		g_main_loop_run (loop);

		/* By now we aborted or we have an addressbook created */
		g_return_val_if_fail (address_book != NULL, NULL);
	}

	/**********************************************************
	 * Ok, done with creating an addressbook, let's add data  *
	 **********************************************************/
	load_contacts (address_book, vcard_path);

	/* Addressbook should have contacts now, let's create the cursor */
	*ret_cursor = get_cursor (address_book);

	/* Cleanup some resources we used to populate the addressbook */
	g_main_loop_unref (loop);
	g_object_unref (address_book_source);
	g_object_unref (registry);

	/* Give the ref through the return value*/
	ret_book = address_book;

	address_book_source = NULL;
	address_book = NULL;

	/* Return the addressbook */
	return ret_book;
}

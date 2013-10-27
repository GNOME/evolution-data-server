/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
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
 * Author: Tristan Van Berkom <tristanvb@openismus.com>
 */
#include "cursor-data.h"

#define CURSOR_DATA_SOURCE_ID "cursor-example-book"

static void                  load_contacts (EBookClient          *client,
					    const gchar          *vcard_directory);
static EBookClientCursor    *get_cursor    (EBookClient          *book_client);

/* Just an example, we need to spin the main loop
 * a bit to wait for the ESource to be created,
 *
 * So lets just use some global variables here
 */
static EBookClient *address_book = NULL;
static ESource     *address_book_source = NULL;

static void
cursor_data_source_added (ESourceRegistry *registry,
			  ESource *source,
			  gpointer data)
{
	GError    *error = NULL;
	GMainLoop *loop = (GMainLoop *)data;

	if (g_strcmp0 (e_source_get_uid (source), CURSOR_DATA_SOURCE_ID) != 0)
		return;

	/* Open the address book */
	address_book = (EBookClient *)e_book_client_connect_sync (source, NULL, &error);
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

static void
setup_custom_book (ESource *scratch)
{
	ESourceBackendSummarySetup *setup;

	g_type_ensure (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP);
	setup = e_source_get_extension (scratch, E_SOURCE_EXTENSION_BACKEND_SUMMARY_SETUP);
	e_source_backend_summary_setup_set_summary_fields (
		setup,
		E_CONTACT_FULL_NAME,
		E_CONTACT_FAMILY_NAME,
		E_CONTACT_GIVEN_NAME,
		E_CONTACT_NICKNAME,
		E_CONTACT_TEL,
		E_CONTACT_EMAIL,
		0);
	e_source_backend_summary_setup_set_indexed_fields (
		setup,
		E_CONTACT_FULL_NAME, E_BOOK_INDEX_PREFIX,
		E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_PREFIX,
		E_CONTACT_GIVEN_NAME, E_BOOK_INDEX_PREFIX,
		E_CONTACT_NICKNAME, E_BOOK_INDEX_PREFIX,
		E_CONTACT_TEL, E_BOOK_INDEX_PREFIX,
		E_CONTACT_TEL, E_BOOK_INDEX_PHONE,
		E_CONTACT_EMAIL, E_BOOK_INDEX_PREFIX,
		0);
}

/* This ensures that all of the test contacts are
 * installed in the CURSOR_DATA_SOURCE_ID test book.
 *
 * Then it opens an EBookClientCursor.
 *
 * The cursor has no filter on the results and is
 * ordered by family name (ascending) and then given name (ascending).
 */
EBookClient *
cursor_load_data (const gchar        *vcard_path,
		  EBookClientCursor **ret_cursor)
{
	ESourceRegistry *registry;
	ESource *scratch;
	ESourceBackend *backend = NULL;
	ESourceBackendSummarySetup *setup = NULL;
	EBookClientCursor *cursor = NULL;
	GMainLoop *loop;
	GError  *error = NULL;
	GSList *contacts = NULL;
	EBookClient *ret_book;

	g_return_val_if_fail (vcard_path != NULL, NULL);
	g_return_val_if_fail (ret_cursor != NULL, NULL);

	loop = g_main_loop_new (NULL, FALSE);

	registry = e_source_registry_new_sync (NULL, &error);
	if (!registry)
		g_error ("Unable to create the registry: %s", error->message);

	/* Listen to the registry for our added source */
	g_signal_connect (registry, "source-added",
			  G_CALLBACK (cursor_data_source_added), loop);

	/* Now create a scratch source for our addressbook */
	scratch = e_source_new_with_uid (CURSOR_DATA_SOURCE_ID, NULL, &error);

	/* Ensure the new ESource will be a local addressbook source */
	backend = e_source_get_extension (scratch, E_SOURCE_EXTENSION_ADDRESS_BOOK);
	e_source_backend_set_backend_name (backend, "local");

	/* Setup custom summary fields, so that we can use those fields with the cursor */
	setup_custom_book (scratch);

	/* Commit the source to the registry */
	if (!e_source_registry_commit_source_sync (registry, scratch, NULL, &error)) {

		/* It's possible the source already exists if we already ran the example with this data server */
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
			/* If so... then just call our callback early */
			ESource *source = e_source_registry_ref_source (registry, CURSOR_DATA_SOURCE_ID);

			g_clear_error (&error);
			g_assert (E_IS_SOURCE (source));

			/* Run the callback which creates the addressbook client connection */
			cursor_data_source_added (registry, source, NULL);
			g_object_unref (source);
		} else
			g_error ("Unable to add new addressbook source to the registry: %s", error->message);
	}

	g_object_unref (scratch);

	if (address_book == NULL) {
		g_timeout_add (20 * 1000, cursor_data_source_timeout, NULL);
		g_main_loop_run (loop);

		/* By now we aborted or we have an addressbook created */
		g_assert (address_book != NULL);
	}

	/**********************************************************
	 * Ok, done with creating an addressbook, let's add data  *
	 **********************************************************/

	/* First check if there are already some contacts, if so then
	 * avoid adding them again
	 */
	if (!e_book_client_get_contacts_uids_sync (address_book,
						   "", &contacts, NULL, &error))
		g_error ("Failed to query addressbook for existing contacts");

	if (contacts != NULL) {
		/* We already have contacts, no need to add them */
		g_slist_free_full (contacts, (GDestroyNotify)g_free);
		contacts = NULL;
	} else {
		load_contacts (address_book, vcard_path);
	}

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

			g_print ("Loading contact from: %s\n", fullpath);

			contact = contact_from_file (fullpath);
			contacts = g_slist_prepend (contacts, contact);

			g_free (fullpath);
		}
	}

	g_dir_close (dir);

	if (contacts != NULL) {

		if (!e_book_client_add_contacts_sync (client, contacts, NULL, NULL, &error))
			g_error ("Failed to add contacts");
	} else
		g_error ("No contacts found in vcard directory: %s", vcard_directory);
}



static gchar *
get_addressbook_directory (ESourceRegistry *registry,
			   ESource         *source)
{
	ESource *builtin_source;
	const gchar *user_data_dir;
	const gchar *uid;
	gchar *filename = NULL;

	uid = e_source_get_uid (source);
	g_return_val_if_fail (uid != NULL, NULL);

	user_data_dir = e_get_user_data_dir ();

	builtin_source = e_source_registry_ref_builtin_address_book (registry);

	/* Special case directory for the builtin addressbook source */
	if (builtin_source != NULL && e_source_equal (source, builtin_source))
		uid = "system";

	filename = g_build_filename (user_data_dir, "addressbook", uid, NULL);

	if (builtin_source)
		g_object_unref (builtin_source);

	return filename;
}

static EBookClientCursor *
get_cursor (EBookClient *book_client)
{
  EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
  EBookSortType sort_types[] = { E_BOOK_SORT_ASCENDING, E_BOOK_SORT_ASCENDING };
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

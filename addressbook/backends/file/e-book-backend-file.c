/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-file.c - File contact backend.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2012 Intel Corporation
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
 * Authors: Nat Friedman <nat@novell.com>
 *          Chris Toshok <toshok@ximian.com>
 *          Hans Petter Jansson <hpj@novell.com>
 *          Tristan Van Berkom <tristanvb@openismus.com>
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "e-book-backend-file.h"
#include "e-book-backend-file-migrate-bdb.h"

#define E_BOOK_BACKEND_FILE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BOOK_BACKEND_FILE, EBookBackendFilePrivate))

#define d(x)

#define E_BOOK_BACKEND_FILE_VERSION_NAME "PAS-DB-VERSION"
#define E_BOOK_BACKEND_FILE_VERSION "0.2"

#define E_BOOK_BACKEND_FILE_REVISION_NAME "PAS-DB-REVISION"

#define PAS_ID_PREFIX "pas-id-"

#define SQLITEDB_EMAIL_ID    "addressbook@localbackend.com"
#define SQLITEDB_FOLDER_ID   "folder_id"
#define SQLITEDB_FOLDER_NAME "folder"

/* Forward Declarations */
static void	e_book_backend_file_initable_init
						(GInitableIface *interface);

G_DEFINE_TYPE_WITH_CODE (
	EBookBackendFile,
	e_book_backend_file,
	E_TYPE_BOOK_BACKEND,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_book_backend_file_initable_init))

struct _EBookBackendFilePrivate {
	gchar     *base_directory;
	gchar     *photo_dirname;
	gchar     *revision;
	gchar     *locale;
	gint       rev_counter;
	gboolean   revision_guards;
	GRWLock    lock;
	GList     *cursors;

	EBookBackendSqliteDB *sqlitedb;
};

/****************************************************************
 *                   File Management helper APIs                *
 ****************************************************************/
typedef enum {
	GET_PATH_DB_DIR,
	GET_PATH_PHOTO_DIR
} GetPathType;

typedef enum {
	STATUS_NORMAL = 0,
	STATUS_MODIFIED,
	STATUS_ERROR
} PhotoModifiedStatus;

static gboolean
remove_file (const gchar *filename,
             GError **error)
{
	if (-1 == g_unlink (filename)) {
		if (errno == EACCES || errno == EPERM) {
			g_set_error_literal (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_PERMISSION_DENIED,
				e_client_error_to_string (
				E_CLIENT_ERROR_PERMISSION_DENIED));
		} else {
			g_set_error (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Failed to remove file '%s': %s"),
				filename, g_strerror (errno));
		}
		return FALSE;
	}

	return TRUE;
}

static gboolean
create_directory (const gchar *dirname,
                  GError **error)
{
	gint rv;

	rv = g_mkdir_with_parents (dirname, 0700);
	if (rv == -1 && errno != EEXIST) {
		g_warning ("failed to make directory %s: %s", dirname, g_strerror (errno));
		if (errno == EACCES || errno == EPERM)
			g_set_error_literal (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_PERMISSION_DENIED,
				e_client_error_to_string (
				E_CLIENT_ERROR_PERMISSION_DENIED));
		else
			g_set_error (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Failed to make directory %s: %s"),
				dirname, g_strerror (errno));
		return FALSE;
	}
	return TRUE;
}

static gchar *
check_remove_uri_for_field (EContact *old_contact,
                            EContact *new_contact,
                            EContactField field)
{
	EContactPhoto *old_photo = NULL, *new_photo = NULL;
	gchar         *uri = NULL;

	old_photo = e_contact_get (old_contact, field);
	if (!old_photo)
		return NULL;

	if (new_contact) {

		new_photo = e_contact_get (new_contact, field);

		if (new_photo == NULL ||
		    g_ascii_strcasecmp (old_photo->data.uri, new_photo->data.uri))
			uri = g_strdup (old_photo->data.uri);
	} else {
		uri = g_strdup (old_photo->data.uri);
	}

	e_contact_photo_free (old_photo);
	e_contact_photo_free (new_photo);

	return uri;
}

static void
maybe_delete_uri (EBookBackendFile *bf,
                  const gchar *uri)
{
	GError *error = NULL;
	gchar  *filename;

	/* A uri that does not give us a filename is certainly not
	 * a uri that we created for a local file, just skip it */
	if ((filename = g_filename_from_uri (uri, NULL, NULL)) == NULL)
		return;

	/* If the file is in our path it belongs to us and we need to delete it.
	 */
	if (bf->priv->photo_dirname &&
	    !strncmp (bf->priv->photo_dirname, filename, strlen (bf->priv->photo_dirname))) {

		d (g_print ("Deleting uri file: %s\n", filename));

		/* Deleting uris should not cause the backend to fail to update
		 * a contact so the best we can do from here is log warnings
		 * when we fail to unlink a file from the disk.
		 */
		if (!remove_file (filename, &error)) {
			g_warning ("Unable to cleanup photo uri: %s", error->message);
			g_error_free (error);
		}
	}

	g_free (filename);
}

static void
maybe_delete_unused_uris (EBookBackendFile *bf,
                          EContact *old_contact,
                          EContact *new_contact)
{
	gchar             *uri_photo, *uri_logo;

	g_return_if_fail (old_contact != NULL);

	/* If there is no new contact, collect all the uris to delete from old_contact
	 *
	 * Otherwise, if any of the photo uri fields have changed in new_contact, then collect the
	 * old uris for those fields from old_contact to delete
	 */
	uri_photo = check_remove_uri_for_field (old_contact, new_contact, E_CONTACT_PHOTO);
	uri_logo  = check_remove_uri_for_field (old_contact, new_contact, E_CONTACT_LOGO);

	if (uri_photo) {
		maybe_delete_uri (bf, uri_photo);
		g_free (uri_photo);
	}

	if (uri_logo) {
		maybe_delete_uri (bf, uri_logo);
		g_free (uri_logo);
	}
}

static gchar *
e_book_backend_file_extract_path_from_source (ESourceRegistry *registry,
                                              ESource *source,
                                              GetPathType path_type)
{
	ESource *builtin_source;
	const gchar *user_data_dir;
	const gchar *uid;
	gchar *filename = NULL;

	uid = e_source_get_uid (source);
	g_return_val_if_fail (uid != NULL, NULL);

	user_data_dir = e_get_user_data_dir ();

	builtin_source = e_source_registry_ref_builtin_address_book (registry);

	/* XXX Backward-compatibility hack:
	 *
	 * The special built-in "Personal" data source UIDs are now named
	 * "system-$COMPONENT" but since the data directories are already
	 * split out by component, we'll continue to use the old "system"
	 * directories for these particular data sources. */
	if (e_source_equal (source, builtin_source))
		uid = "system";

	switch (path_type) {
		case GET_PATH_DB_DIR:
			filename = g_build_filename (
				user_data_dir, "addressbook", uid, NULL);
			break;
		case GET_PATH_PHOTO_DIR:
			filename = g_build_filename (
				user_data_dir, "addressbook", uid, "photos", NULL);
			break;
		default:
			g_warn_if_reached ();
	}

	g_object_unref (builtin_source);

	return filename;
}

static gchar *
safe_name_for_photo (EBookBackendFile *bf,
                     EContact *contact,
                     EContactPhoto *photo,
                     EContactField field)
{
	gchar         *fullname = NULL, *name, *str;
	gchar         *suffix = NULL;
	gint           i = 0;

	g_assert (photo->type == E_CONTACT_PHOTO_TYPE_INLINED);

	/* Get a suitable filename extension */
	if (photo->data.inlined.mime_type != NULL &&
	    photo->data.inlined.mime_type[0] != '\0') {
		suffix = g_uri_escape_string (
			photo->data.inlined.mime_type,
			NULL, TRUE);
	} else {
		gchar *mime_type = NULL;
		gchar *content_type = NULL;

		content_type = g_content_type_guess (
			NULL,
			photo->data.inlined.data,
			photo->data.inlined.length,
			NULL);

		if (content_type)
			mime_type = g_content_type_get_mime_type (content_type);

		if (mime_type)
			suffix = g_uri_escape_string (mime_type, NULL, TRUE);
		else
			suffix = g_strdup ("data");

		g_free (mime_type);
		g_free (content_type);
	}

	/* Create a filename based on the uid/field */
	name = g_strconcat (
		e_contact_get_const (contact, E_CONTACT_UID), "_",
		e_contact_field_name (field), NULL);
	name = g_strdelimit (name, NULL, '_');

	do {
		g_free (fullname);

		str      = e_filename_mkdir_encoded (bf->priv->photo_dirname, name, NULL, i);
		fullname = g_strdup_printf ("%s.%s", str, suffix);
		g_free (str);

		i++;
	} while (g_file_test (fullname, G_FILE_TEST_EXISTS));

	g_free (name);
	g_free (suffix);

	return fullname;
}

static gchar *
hard_link_photo (EBookBackendFile *bf,
                 EContact *contact,
                 EContactField field,
                 const gchar *src_filename,
                 GError **error)
{
	gchar *fullname = NULL, *name, *str;
	gint   i = 0, ret;
	const gchar *suffix;

	/* Copy over the file suffix */
	suffix = strrchr (src_filename, '.');
	if (suffix)
		suffix++;

	if (!suffix)
		suffix = "data";

	/* Create a filename based on uid/field */
	name = g_strconcat (
		e_contact_get_const (contact, E_CONTACT_UID), "_",
		e_contact_field_name (field), NULL);
	name = g_strdelimit (name, NULL, '_');

	do {
		g_free (fullname);

		str      = e_filename_mkdir_encoded (bf->priv->photo_dirname, name, NULL, i);
		fullname = g_strdup_printf ("%s.%s", str, suffix);
		g_free (str);

		i++;

		ret = link (src_filename, fullname);

	} while (ret < 0 && errno == EEXIST);

	if (ret < 0) {
		if (errno == EACCES || errno == EPERM) {
			g_set_error_literal (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_PERMISSION_DENIED,
				e_client_error_to_string (
				E_CLIENT_ERROR_PERMISSION_DENIED));
		} else {
			g_set_error (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Failed to create hardlink for resource '%s': %s"),
				src_filename, g_strerror (errno));
		}
		g_free (fullname);
		fullname = NULL;
	}

	g_free (name);

	return fullname;
}

static gboolean
is_backend_owned_uri (EBookBackendFile *bf,
                      const gchar *uri)
{
	gchar     *filename;
	gchar     *dirname;
	gboolean   owned_uri;

	/* Errors converting from uri definitily indicate it was
	 * not our uri to begin with, so just disregard this error. */
	filename = g_filename_from_uri (uri, NULL, NULL);
	if (!filename)
		return FALSE;

	dirname = g_path_get_dirname (filename);

	owned_uri = bf->priv->photo_dirname && (strcmp (dirname, bf->priv->photo_dirname) == 0);

	g_free (filename);
	g_free (dirname);

	return owned_uri;
}

static PhotoModifiedStatus
maybe_transform_vcard_field_for_photo (EBookBackendFile *bf,
                                       EContact *old_contact,
                                       EContact *contact,
                                       EContactField field,
                                       GError **error)
{
	PhotoModifiedStatus  status = STATUS_NORMAL;
	EContactPhoto       *photo;

	if (field != E_CONTACT_PHOTO && field != E_CONTACT_LOGO)
		return status;

	photo = e_contact_get (contact, field);
	if (!photo)
		return status;

	if (photo->type == E_CONTACT_PHOTO_TYPE_INLINED) {
		EContactPhoto *new_photo;
		gchar         *new_photo_path;
		gchar         *uri;

		/* Create a unique filename with an extension (hopefully) based on the mime type */
		new_photo_path = safe_name_for_photo (bf, contact, photo, field);

		if ((uri =
		     g_filename_to_uri (new_photo_path, NULL, error)) == NULL) {

			status = STATUS_ERROR;
		} else if (!g_file_set_contents (new_photo_path,
						 (const gchar *) photo->data.inlined.data,
						 photo->data.inlined.length,
						 error)) {

			status = STATUS_ERROR;
		} else {
			new_photo           = e_contact_photo_new ();
			new_photo->type     = E_CONTACT_PHOTO_TYPE_URI;
			new_photo->data.uri = g_strdup (uri);

			e_contact_set (contact, field, new_photo);

			d (g_print ("Backend modified incomming binary blob to be %s:\n", uri));

			status = STATUS_MODIFIED;

			e_contact_photo_free (new_photo);
		}

		g_free (uri);
		g_free (new_photo_path);

	} else { /* E_CONTACT_PHOTO_TYPE_URI */
		const gchar       *uid;
		EContactPhoto     *old_photo = NULL, *new_photo;

		/* First determine that the new contact uri points to our 'photos' directory,
		 * if not then we do nothing
		 */
		if (!is_backend_owned_uri (bf, photo->data.uri))
			goto done;

		/* Now check if the uri is changed from the BDB copy
		 */
		uid = e_contact_get_const (contact, E_CONTACT_UID);
		if (uid == NULL) {
			g_set_error_literal (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("No UID in the contact"));
			status = STATUS_ERROR;
			goto done;
		}

		if (old_contact)
			old_photo = e_contact_get (old_contact, field);

		/* Unless we are receiving the same uri that we already have
		 * stored in the BDB... */
		if (!old_photo || old_photo->type == E_CONTACT_PHOTO_TYPE_INLINED ||
		    g_ascii_strcasecmp (old_photo->data.uri, photo->data.uri) != 0) {
			gchar *filename;
			gchar *new_filename;
			gchar *new_uri = NULL;

			/* ... Assume that the incomming uri belongs to another contact
			 * still in the BDB. Lets go ahead and create a hard link to the 
			 * photo file and create a new name for the incomming uri, and
			 * use that in the incomming contact to save in place.
			 *
			 * This piece of code is here to ensure there are no problems if
			 * the libebook user decides to cross-reference and start "sharing"
			 * uris that we've previously stored in the photo directory.
			 *
			 * We use the hard-link here to off-load the necessary ref-counting
			 * logic to the file-system.
			 */
			filename = g_filename_from_uri (photo->data.uri, NULL, NULL);
			g_assert (filename); /* we already checked this with 'is_backend_owned_uri ()' */

			new_filename = hard_link_photo (bf, contact, field, filename, error);

			if (!new_filename)
				status = STATUS_ERROR;
			else if ((new_uri = g_filename_to_uri (new_filename, NULL, error)) == NULL) {
				/* If we fail here... we need to clean up the hardlink we just created */
				GError *local_err = NULL;
				if (!remove_file (new_filename, &local_err)) {
					g_warning ("Unable to cleanup photo uri: %s", local_err->message);
					g_error_free (local_err);
				}
				status = STATUS_ERROR;
			} else {

				new_photo           = e_contact_photo_new ();
				new_photo->type     = E_CONTACT_PHOTO_TYPE_URI;
				new_photo->data.uri = new_uri;

				e_contact_set (contact, field, new_photo);

				d (g_print ("Backend modified incomming shared uri to be %s:\n", new_uri));

				e_contact_photo_free (new_photo);
				status = STATUS_MODIFIED;
			}
			g_free (new_filename);
			g_free (filename);
		}

		if (old_photo)
			e_contact_photo_free (old_photo);

	}

 done:
	e_contact_photo_free (photo);

	return status;
}

/*
 * When a contact is added or modified we receive a vCard,
 * this function checks if we've received inline data
 * and replaces it with a uri notation.
 *
 * If this function modifies 'contact' then it will
 * return the 'modified' status and 'vcard_ret' (if specified)
 * will be set to a newly allocated vcard string.
 */
static PhotoModifiedStatus
maybe_transform_vcard_for_photo (EBookBackendFile *bf,
                                 EContact *old_contact,
                                 EContact *contact,
                                 GError **error)
{
	PhotoModifiedStatus status;
	gboolean            modified = FALSE;

	status = maybe_transform_vcard_field_for_photo (
		bf, old_contact, contact,
		E_CONTACT_PHOTO, error);
	modified = (status == STATUS_MODIFIED);

	if (status != STATUS_ERROR) {
		status = maybe_transform_vcard_field_for_photo (
			bf, old_contact, contact,
			E_CONTACT_LOGO, error);
		modified = modified || (status == STATUS_MODIFIED);
	}

	if (status != STATUS_ERROR && modified)
		status = STATUS_MODIFIED;

	return status;
}

/****************************************************************
 *                     Global Revisioning Tools                 *
 ****************************************************************/
static gchar *
e_book_backend_file_create_unique_id (void)
{
	/* use a 32 counter and the 32 bit timestamp to make an id.
	 * it's doubtful 2^32 id's will be created in a second, so we
	 * should be okay. */
	static guint c = 0;
	return g_strdup_printf (PAS_ID_PREFIX "%08lX%08X", (glong) time (NULL), c++);
}

static gchar *
e_book_backend_file_new_revision (EBookBackendFile *bf)
{
	gchar time_string[100] = {0};
	const struct tm *tm = NULL;
	time_t t;

	t = time (NULL);
	tm = gmtime (&t);
	if (tm)
		strftime (time_string, 100, "%Y-%m-%dT%H:%M:%SZ", tm);

	return g_strdup_printf ("%s(%d)", time_string, bf->priv->rev_counter++);
}

/* For now just bump the revision and set it in the DB every
 * time the revision bumps, this is the safest approach and
 * its unclear so far if bumping the revision string for 
 * every DB modification is going to really be an overhead.
 */
static gboolean
e_book_backend_file_bump_revision (EBookBackendFile *bf,
				   GError **error)
{
	GError *local_error = NULL;
	gchar *new_revision;
	gboolean success;

	new_revision = e_book_backend_file_new_revision (bf);

	success = e_book_backend_sqlitedb_set_revision (bf->priv->sqlitedb,
							SQLITEDB_FOLDER_ID,
							new_revision,
							&local_error);

	if (success) {
		g_free (bf->priv->revision);
		bf->priv->revision = new_revision;

		e_book_backend_notify_property_changed (E_BOOK_BACKEND (bf),
							BOOK_BACKEND_PROPERTY_REVISION,
							bf->priv->revision);
	} else {
		g_free (new_revision);
		g_warning (
			G_STRLOC ": Error setting database revision: %s",
			local_error->message);
		g_propagate_error (error, local_error);
	}

	return success;
}

static void
e_book_backend_file_load_revision (EBookBackendFile *bf)
{
	GError *error = NULL;

	if (!e_book_backend_sqlitedb_get_revision (bf->priv->sqlitedb,
						   SQLITEDB_FOLDER_ID,
						   &bf->priv->revision,
						   &error)) {
		g_warning (
			G_STRLOC ": Error loading database revision: %s",
			error ? error->message : "Unknown error");
		g_clear_error (&error);
	} else if (bf->priv->revision == NULL) {
		e_book_backend_file_bump_revision (bf, NULL);
	}
}

static void
e_book_backend_file_load_locale (EBookBackendFile *bf)
{
	GError *error = NULL;

	if (!e_book_backend_sqlitedb_get_locale (bf->priv->sqlitedb,
						 SQLITEDB_FOLDER_ID,
						 &bf->priv->locale,
						 &error)) {
		g_warning (
			G_STRLOC ": Error loading database locale setting: %s",
			error ? error->message : "Unknown error");
		g_clear_error (&error);
	}
}

static void
set_revision (EBookBackendFile *bf,
              EContact *contact)
{
	gchar *rev;

	rev = e_book_backend_file_new_revision (bf);
	e_contact_set (contact, E_CONTACT_REV, rev);
	g_free (rev);
}


/****************************************************************
 *                   Dealing with cursor updates                *
 ****************************************************************/
static void
cursors_locale_changed (EBookBackendFile *bf)
{
	GList *l;

	for (l = bf->priv->cursors; l; l = l->next) {
		EDataBookCursor *cursor = l->data;
		GError *error = NULL;

		if (!e_data_book_cursor_load_locale (cursor, NULL, &error)) {
			g_warning ("Error loading cursor locale: %s", error->message);
			g_clear_error (&error);
		}
	}
}

static void
cursors_contact_added (EBookBackendFile *bf,
		       EContact         *contact)
{
	GList *l;

	for (l = bf->priv->cursors; l; l = l->next) {
		EDataBookCursor *cursor = l->data;

		e_data_book_cursor_contact_added (cursor, contact);
	}
}

static void
cursors_contact_removed (EBookBackendFile *bf,
			 EContact         *contact)
{
	GList *l;

	for (l = bf->priv->cursors; l; l = l->next) {
		EDataBookCursor *cursor = l->data;

		e_data_book_cursor_contact_removed (cursor, contact);
	}
}

/****************************************************************
 *                   Main Backend Implementation                *
 ****************************************************************/

/**
 * This method will return TRUE if all the contacts were properly created.
 * If at least one contact fails, the method will return FALSE, all
 * changes will be reverted (the @contacts list will stay empty) and
 * @perror will be set.
 */
static gboolean
do_create (EBookBackendFile *bf,
           const gchar * const *vcards,
           GQueue *out_contacts,
           GError **error)
{
	PhotoModifiedStatus status = STATUS_NORMAL;
	GQueue queue = G_QUEUE_INIT;
	guint ii, length;
	GError *local_error = NULL;

	length = g_strv_length ((gchar **) vcards);

	for (ii = 0; ii < length; ii++) {
		gchar           *id;
		const gchar     *rev;
		EContact        *contact;

		contact = e_contact_new_from_vcard (vcards[ii]);

		/* Preserve original UID, create a unique UID if needed */
		if (e_contact_get_const (contact, E_CONTACT_UID) == NULL) {
			id = e_book_backend_file_create_unique_id ();
			e_contact_set (contact, E_CONTACT_UID, id);
			g_free (id);
		}

		rev = e_contact_get_const (contact, E_CONTACT_REV);
		if (!(rev && *rev))
			set_revision (bf, contact);

		status = maybe_transform_vcard_for_photo (bf, NULL, contact, error);

		if (status != STATUS_ERROR) {

			/* Contact was added successfully. */
			g_queue_push_tail (&queue, contact);
		} else {
			/* Contact could not be transformed */
			g_warning (
				G_STRLOC ": Error transforming vcard with image data %s",
				(error && *error) ? (*error)->message :
				"Unknown error transforming vcard");
			g_object_unref (contact);

			/* Abort as soon as an error occurs */
			break;
		}
	}

	if (status != STATUS_ERROR) {
		GList *tail, *link;
		GSList *slist = NULL, *l;

		/* XXX EBookBackendSqliteDB still uses GSList. */
		tail = g_queue_peek_tail_link (&queue);
		for (link = tail; link != NULL; link = g_list_previous (link))
			slist = g_slist_prepend (slist, link->data);

		if (!e_book_backend_sqlitedb_new_contacts (bf->priv->sqlitedb,
							   SQLITEDB_FOLDER_ID,
							   slist, FALSE,
							   &local_error)) {

			g_warning ("Failed to add contacts: %s", local_error->message);

			if (g_error_matches (local_error,
					     E_BOOK_SDB_ERROR,
					     E_BOOK_SDB_ERROR_CONSTRAINT)) {
				g_set_error (
					error, E_BOOK_CLIENT_ERROR,
					E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS,
					_("Conflicting UIDs found in added contacts"));
				g_clear_error (&local_error);
			} else
				g_propagate_error (error, local_error);

			status = STATUS_ERROR;
		}

		/* After adding any contacts, notify any cursors that the new contacts are added */
		for (l = slist; l; l = l->next) {
			cursors_contact_added (bf, E_CONTACT (l->data));
		}

		g_slist_free (slist);
	}

	if (status != STATUS_ERROR && out_contacts != NULL)
		e_queue_transfer (&queue, out_contacts);

	while (!g_queue_is_empty (&queue))
		g_object_unref (g_queue_pop_head (&queue));

	return (status != STATUS_ERROR);
}

typedef struct {
	EBookBackendFile *bf;
	GThread *thread;
	EFlag *running;
} FileBackendSearchClosure;

static void
closure_destroy (FileBackendSearchClosure *closure)
{
	d (printf ("destroying search closure\n"));
	e_flag_free (closure->running);
	if (closure->thread)
		g_thread_unref (closure->thread);
	g_free (closure);
}

static FileBackendSearchClosure *
init_closure (EDataBookView *book_view,
              EBookBackendFile *bf)
{
	FileBackendSearchClosure *closure = g_new (FileBackendSearchClosure, 1);

	closure->bf = bf;
	closure->thread = NULL;
	closure->running = e_flag_new ();

	g_object_set_data_full (
		G_OBJECT (book_view),
		"EBookBackendFile.BookView::closure",
		closure, (GDestroyNotify) closure_destroy);

	return closure;
}

static FileBackendSearchClosure *
get_closure (EDataBookView *book_view)
{
	return g_object_get_data (
		G_OBJECT (book_view),
		"EBookBackendFile.BookView::closure");
}

static void
notify_update_vcard (EDataBookView *book_view,
                     gboolean prefiltered,
                     const gchar *id,
                     const gchar *vcard)
{
	if (prefiltered)
		e_data_book_view_notify_update_prefiltered_vcard (book_view, id, vcard);
	else
		e_data_book_view_notify_update_vcard (book_view, id, vcard);
}

static gpointer
book_view_thread (gpointer data)
{
	EDataBookView *book_view;
	FileBackendSearchClosure *closure;
	EBookBackendFile *bf;
	EBookBackendSExp *sexp;
	const gchar *query;
	GSList *summary_list, *l;
	GHashTable *fields_of_interest;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (data), NULL);

	book_view = data;
	closure = get_closure (book_view);
	if (!closure) {
		g_warning (G_STRLOC ": NULL closure in book view thread");
		return NULL;
	}
	bf = closure->bf;

	d (printf ("starting initial population of book view\n"));

	/* ref the book view because it'll be removed and unrefed
	 * when/if it's stopped */
	g_object_ref (book_view);

	sexp = e_data_book_view_get_sexp (book_view);
	query = e_book_backend_sexp_text (sexp);

	fields_of_interest = e_data_book_view_get_fields_of_interest (book_view);

	if ( !strcmp (query, "(contains \"x-evolution-any-field\" \"\")")) {
		e_data_book_view_notify_progress (book_view, -1, _("Loading..."));
	} else {
		e_data_book_view_notify_progress (book_view, -1, _("Searching..."));
	}

	d (printf ("signalling parent thread\n"));
	e_flag_set (closure->running);

	g_rw_lock_reader_lock (&(bf->priv->lock));
	summary_list = e_book_backend_sqlitedb_search (
		bf->priv->sqlitedb,
		SQLITEDB_FOLDER_ID,
		query, fields_of_interest,
		NULL, NULL, &local_error);
	g_rw_lock_reader_unlock (&(bf->priv->lock));

	if (!summary_list && local_error != NULL) {
		g_warning (G_STRLOC ": Failed to query initial contacts: %s", local_error->message);
		g_error_free (local_error);
		e_data_book_view_notify_complete (
			book_view,
			g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_NOT_OPENED,
				e_client_error_to_string (
				E_CLIENT_ERROR_NOT_OPENED)));
		g_object_unref (book_view);
		return NULL;
	}

	for (l = summary_list; l; l = l->next) {
		EbSdbSearchData *data = l->data;
		gchar *vcard = NULL;

		vcard = data->vcard;
		data->vcard = NULL;

		notify_update_vcard (book_view, TRUE, data->uid, vcard);
		g_free (vcard);
	}

	g_slist_foreach (summary_list, (GFunc) e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (summary_list);

	if (e_flag_is_set (closure->running))
		e_data_book_view_notify_complete (book_view, NULL /* Success */);

	g_object_unref (book_view);

	d (printf ("finished population of book view\n"));

	return NULL;
}

static void
view_notify_update (EBookBackendFile *backend,
                    EDataBookView *view,
                    EContact *contact)
{
	EBookBackendSExp *sexp;
	GHashTable *fields   = e_data_book_view_get_fields_of_interest (view);
	const gchar *query;
	gboolean    notified = FALSE;
	gboolean    with_all_required_fields = FALSE;

	sexp = e_data_book_view_get_sexp (view);
	query = e_book_backend_sexp_text (sexp);

	if (e_book_backend_sqlitedb_check_summary_query (backend->priv->sqlitedb, query, NULL) &&
	    e_book_backend_sqlitedb_check_summary_fields (backend->priv->sqlitedb, fields)) {

		const gchar *uid = e_contact_get_const (contact, E_CONTACT_UID);
		gchar       *vcard;

		vcard = e_book_backend_sqlitedb_get_vcard_string (
			backend->priv->sqlitedb,
			SQLITEDB_FOLDER_ID, uid,
			fields, &with_all_required_fields, NULL);

		if (vcard) {
			if (with_all_required_fields) {
				e_data_book_view_notify_update_prefiltered_vcard (view, uid, vcard);
				notified = TRUE;
			}
			g_free (vcard);
		}
	}

	if (!notified)
		e_data_book_view_notify_update (view, contact);
}

static void
book_backend_file_dispose (GObject *object)
{
	EBookBackendFile *bf;

	bf = E_BOOK_BACKEND_FILE (object);

	g_rw_lock_writer_lock (&(bf->priv->lock));

	if (bf->priv->cursors) {
		g_list_free_full (bf->priv->cursors, g_object_unref);
		bf->priv->cursors = NULL;
	}

	if (bf->priv->sqlitedb) {
		g_object_unref (bf->priv->sqlitedb);
		bf->priv->sqlitedb = NULL;
	}

	g_rw_lock_writer_unlock (&(bf->priv->lock));

	G_OBJECT_CLASS (e_book_backend_file_parent_class)->dispose (object);
}

static void
book_backend_file_finalize (GObject *object)
{
	EBookBackendFilePrivate *priv;

	priv = E_BOOK_BACKEND_FILE_GET_PRIVATE (object);

	g_free (priv->photo_dirname);
	g_free (priv->revision);
	g_free (priv->locale);
	g_free (priv->base_directory);
	g_rw_lock_clear (&(priv->lock));

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_backend_file_parent_class)->finalize (object);
}

static gchar *
book_backend_file_get_backend_property (EBookBackend *backend,
                                        const gchar *prop_name)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);

	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strdup ("local,do-initial-query,bulk-adds,bulk-modifies,bulk-removes,contact-lists");

	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		return g_strdup (e_contact_field_name (E_CONTACT_FILE_AS));

	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		GString *fields;
		gint ii;

		fields = g_string_sized_new (1024);

		/* XXX we need a way to say "we support everything", since the
		 * file backend does */
		for (ii = 1; ii < E_CONTACT_FIELD_LAST; ii++) {
			if (fields->len > 0)
				g_string_append_c (fields, ',');
			g_string_append (fields, e_contact_field_name (ii));
		}

		return g_string_free (fields, FALSE);

	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REVISION)) {
		gchar *prop_value;

		g_rw_lock_reader_lock (&(bf->priv->lock));
		prop_value = g_strdup (bf->priv->revision);
		g_rw_lock_reader_unlock (&(bf->priv->lock));

		return prop_value;
	}

	/* Chain up to parent's get_backend_property() method. */
	return E_BOOK_BACKEND_CLASS (e_book_backend_file_parent_class)->
		get_backend_property (backend, prop_name);
}

static gboolean
book_backend_file_open_sync (EBookBackend *backend,
                             GCancellable *cancellable,
                             GError **error)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	ESource          *source;
	ESourceRevisionGuards *guards;

	source = e_backend_get_source (E_BACKEND (backend));

	g_type_ensure (E_TYPE_SOURCE_REVISION_GUARDS);
	guards = e_source_get_extension (source, E_SOURCE_EXTENSION_REVISION_GUARDS);

	bf->priv->revision_guards = e_source_revision_guards_get_enabled (guards);

	g_rw_lock_writer_lock (&(bf->priv->lock));
	if (!bf->priv->revision) {
		e_book_backend_file_load_revision (bf);
		e_book_backend_notify_property_changed (E_BOOK_BACKEND (backend),
							BOOK_BACKEND_PROPERTY_REVISION,
							bf->priv->revision);
	}

	g_rw_lock_writer_unlock (&(bf->priv->lock));

	e_backend_set_online (E_BACKEND (backend), TRUE);
	e_book_backend_set_writable (E_BOOK_BACKEND (backend), TRUE);

	return TRUE;
}

static gboolean
book_backend_file_create_contacts_sync (EBookBackend *backend,
                                        const gchar * const *vcards,
                                        GQueue *out_contacts,
                                        GCancellable *cancellable,
                                        GError **error)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	gboolean success;

	g_rw_lock_writer_lock (&(bf->priv->lock));

	success = e_book_backend_sqlitedb_lock_updates (bf->priv->sqlitedb, error);

	if (success)
		success = do_create (bf, vcards, out_contacts, error);

	if (success)
		success = e_book_backend_file_bump_revision (bf, error);

	if (success)
		success = e_book_backend_sqlitedb_unlock_updates (bf->priv->sqlitedb,
								  TRUE, error);
	else {
		GError *local_error = NULL;

		/* Rollback transaction */
		if (!e_book_backend_sqlitedb_unlock_updates (bf->priv->sqlitedb, FALSE, &local_error)) {
			g_warning ("Failed to rollback transaction after failing to add contacts: %s",
				   local_error->message);
			g_clear_error (&local_error);
		}
	}

	g_rw_lock_writer_unlock (&(bf->priv->lock));

	return success;
}

static gboolean
book_backend_file_modify_contacts_sync (EBookBackend *backend,
                                        const gchar * const *vcards,
                                        GQueue *out_contacts,
                                        GCancellable *cancellable,
                                        GError **error)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	GSList           *ids = NULL;
	GError           *local_error = NULL;
	PhotoModifiedStatus status = STATUS_NORMAL;
	GQueue old_contact_queue = G_QUEUE_INIT;
	GQueue mod_contact_queue = G_QUEUE_INIT;
	guint ii, length;

	length = g_strv_length ((gchar **) vcards);

	g_rw_lock_writer_lock (&(bf->priv->lock));

	if (!e_book_backend_sqlitedb_lock_updates (bf->priv->sqlitedb, error))
		status = STATUS_ERROR;

	for (ii = 0; ii < length && status != STATUS_ERROR; ii++) {
		gchar *id;
		EContact *mod_contact, *old_contact;
		const gchar *mod_contact_rev, *old_contact_rev;

		mod_contact = e_contact_new_from_vcard (vcards[ii]);
		id = e_contact_get (mod_contact, E_CONTACT_UID);

		if (id == NULL) {
			status = STATUS_ERROR;

			g_set_error_literal (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("No UID in the contact"));
			g_object_unref (mod_contact);
			break;
		}

		old_contact = e_book_backend_sqlitedb_get_contact (
			bf->priv->sqlitedb,
			SQLITEDB_FOLDER_ID, id,
			NULL, NULL, &local_error);
		if (!old_contact) {
			g_warning (G_STRLOC ": Failed to load contact %s: %s", id, local_error->message);
			g_propagate_error (error, local_error);
			local_error = NULL;

			status = STATUS_ERROR;

			g_free (id);
			g_object_unref (mod_contact);
			break;
		}

		if (bf->priv->revision_guards) {
			mod_contact_rev = e_contact_get_const (mod_contact, E_CONTACT_REV);
			old_contact_rev = e_contact_get_const (old_contact, E_CONTACT_REV);

			if (!mod_contact_rev || !old_contact_rev ||
			    strcmp (mod_contact_rev, old_contact_rev) != 0) {
				g_set_error (
					error, E_CLIENT_ERROR,
					E_CLIENT_ERROR_OUT_OF_SYNC,
					_("Tried to modify contact '%s' with out of sync revision"),
					(gchar *) e_contact_get_const (mod_contact, E_CONTACT_UID));

				status = STATUS_ERROR;

				g_free (id);
				g_object_unref (mod_contact);
				g_object_unref (old_contact);
				break;
			}
		}

		/* Transform incomming photo blobs to uris before storing this to the DB */
		status = maybe_transform_vcard_for_photo (bf, old_contact, mod_contact, &local_error);
		if (status == STATUS_ERROR) {
			g_warning (G_STRLOC ": Error transforming contact %s: %s", id, local_error->message);
			g_propagate_error (error, local_error);
			local_error = NULL;

			g_free (id);
			g_object_unref (old_contact);
			g_object_unref (mod_contact);
			break;
		}

		/* update the revision (modified time of contact) */
		set_revision (bf, mod_contact);

		g_queue_push_tail (&old_contact_queue, old_contact);
		g_queue_push_tail (&mod_contact_queue, mod_contact);

		ids = g_slist_prepend (ids, id);
	}

	if (status != STATUS_ERROR) {
		GList *old_link;
		GList *mod_link;
		GSList *slist = NULL;

		/* Delete old photo file uris if need be (this will compare the new contact
		 * with the current copy in the BDB to extract the uris to delete) */
		old_link = g_queue_peek_head_link (&old_contact_queue);
		mod_link = g_queue_peek_head_link (&mod_contact_queue);

		while (old_link != NULL && mod_link != NULL) {
			maybe_delete_unused_uris (
				bf,
				E_CONTACT (old_link->data),
				E_CONTACT (mod_link->data));
			old_link = g_list_next (old_link);
			mod_link = g_list_next (mod_link);
		}

		/* XXX EBookBackendSqliteDB still uses GSList. */
		mod_link = g_queue_peek_tail_link (&mod_contact_queue);
		while (mod_link != NULL) {
			slist = g_slist_prepend (slist, mod_link->data);
			mod_link = g_list_previous (mod_link);
		}

		/* Update summary as well */
		if (!e_book_backend_sqlitedb_new_contacts (bf->priv->sqlitedb,
							   SQLITEDB_FOLDER_ID,
							   slist, TRUE,
							   &local_error)) {
			g_warning ("Failed to modify contacts: %s", local_error->message);
			g_propagate_error (error, local_error);
			local_error = NULL;

			status = STATUS_ERROR;
		}

		g_slist_free (slist);
	}

	/* Bump the revision atomically in the same transaction */
	if (status != STATUS_ERROR) {
		if (!e_book_backend_file_bump_revision (bf, error))
			status = STATUS_ERROR;
	}

	/* Commit or rollback transaction */
	if (status != STATUS_ERROR) {

		if (!e_book_backend_sqlitedb_unlock_updates (bf->priv->sqlitedb,
							     TRUE, error))
			status = STATUS_ERROR;

	} else {
		/* Rollback transaction */
		if (!e_book_backend_sqlitedb_unlock_updates (bf->priv->sqlitedb, FALSE, &local_error)) {
			g_warning ("Failed to rollback transaction after failing to modify contacts: %s",
				   local_error->message);
			g_clear_error (&local_error);
		}
	}

	if (status != STATUS_ERROR)
		e_queue_transfer (&mod_contact_queue, out_contacts);

	/* Now that we've modified the contact(s), notify cursors of the changes
	 */
	if (status != STATUS_ERROR) {
		GList *l;

		for (l = g_queue_peek_head_link (&old_contact_queue);
		     l; l = l->next) {
			cursors_contact_removed (bf, E_CONTACT (l->data));
		}

		for (l = g_queue_peek_head_link (&mod_contact_queue);
		     l; l = l->next) {
			cursors_contact_added (bf, E_CONTACT (l->data));
		}
	}

	g_rw_lock_writer_unlock (&(bf->priv->lock));

	while (!g_queue_is_empty (&old_contact_queue))
		g_object_unref (g_queue_pop_head (&old_contact_queue));

	while (!g_queue_is_empty (&mod_contact_queue))
		g_object_unref (g_queue_pop_head (&mod_contact_queue));

	g_slist_free_full (ids, (GDestroyNotify) g_free);

	return (status != STATUS_ERROR);
}

static gboolean
book_backend_file_remove_contacts_sync (EBookBackend *backend,
                                        const gchar * const *uids,
                                        GCancellable *cancellable,
                                        GError **error)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	GSList           *removed_ids = NULL, *removed_contacts = NULL;
	GError           *local_error = NULL;
	const GSList     *l;
	gboolean success = TRUE;
	guint ii, length;

	length = g_strv_length ((gchar **) uids);

	g_rw_lock_writer_lock (&(bf->priv->lock));

	success = e_book_backend_sqlitedb_lock_updates (bf->priv->sqlitedb, error);

	for (ii = 0; ii < length && success; ii++) {
		EContact *contact;

		/* First load the EContacts which need to be removed, we might delete some
		 * photos from disk because of this...
		 *
		 * Note: sqlitedb backend can probably make this faster by executing a
		 * single query to fetch a list of contacts for a list of ids, the
		 * current method makes a query for each UID.
		 */
		contact = e_book_backend_sqlitedb_get_contact (
			bf->priv->sqlitedb,
			SQLITEDB_FOLDER_ID, uids[ii],
			NULL, NULL, &local_error);

		if (contact) {
			removed_ids      = g_slist_prepend (removed_ids, g_strdup (uids[ii]));
			removed_contacts = g_slist_prepend (removed_contacts, contact);
		} else {
			g_warning ("Failed to fetch contact to be removed: %s", local_error->message);

			if (g_error_matches (local_error,
					     E_BOOK_SDB_ERROR,
					     E_BOOK_SDB_ERROR_CONTACT_NOT_FOUND)) {
				g_set_error (
					error, E_BOOK_CLIENT_ERROR,
					E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND,
					_("Contact '%s' not found"), uids[ii]);
				g_error_free (local_error);
			} else {
				g_propagate_error (error, local_error);
				local_error = NULL;
			}
			/* Abort as soon as missing contact is to be deleted */
			success = FALSE;
			break;
		}
	}

	if (success) {

		/* Delete URI associated to those contacts */
		for (l = removed_contacts; l; l = l->next) {
			maybe_delete_unused_uris (bf, E_CONTACT (l->data), NULL);
		}

		/* Remove from summary as well */
		if (!e_book_backend_sqlitedb_remove_contacts (bf->priv->sqlitedb,
						      SQLITEDB_FOLDER_ID,
						      removed_ids, &local_error)) {
			g_warning ("Failed to remove contacts: %s", local_error->message);
			g_propagate_error (error, local_error);
		}

		e_book_backend_file_bump_revision (bf, NULL);
	}

	/* Commit or rollback transaction */
	if (success) {
		success = e_book_backend_sqlitedb_unlock_updates (bf->priv->sqlitedb,
								  TRUE, error);
	} else {
		/* Rollback transaction */
		if (!e_book_backend_sqlitedb_unlock_updates (bf->priv->sqlitedb, FALSE, &local_error)) {
			g_warning ("Failed to rollback transaction after failing to modify contacts: %s",
				   local_error->message);
			g_clear_error (&local_error);
		}
	}

	/* After removing any contacts, notify any cursors that the new contacts are added */
	if (success) {
		for (l = removed_contacts; l; l = l->next) {
			cursors_contact_removed (bf, E_CONTACT (l->data));
		}
	}

	g_rw_lock_writer_unlock (&(bf->priv->lock));

	g_slist_free_full (removed_ids, (GDestroyNotify) g_free);
	g_slist_free_full (removed_contacts, (GDestroyNotify) g_object_unref);

	return success;
}

static EContact *
book_backend_file_get_contact_sync (EBookBackend *backend,
                                    const gchar *uid,
                                    GCancellable *cancellable,
                                    GError **error)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	EContact *contact;
	GError *local_error = NULL;

	g_rw_lock_reader_lock (&(bf->priv->lock));

	contact = e_book_backend_sqlitedb_get_contact (
		bf->priv->sqlitedb,
		SQLITEDB_FOLDER_ID, uid,
		NULL, NULL, &local_error);

	g_rw_lock_reader_unlock (&(bf->priv->lock));

	/* Sanity check. */
	g_return_val_if_fail (
		((contact != NULL) && (local_error == NULL)) ||
		((contact == NULL) && (local_error != NULL)), NULL);

	if (local_error) {

		if (g_error_matches (local_error,
				     E_BOOK_SDB_ERROR,
				     E_BOOK_SDB_ERROR_CONTACT_NOT_FOUND)) {
			g_set_error (
				error, E_BOOK_CLIENT_ERROR,
				E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND,
				_("Contact '%s' not found"), uid);
			g_error_free (local_error);
		} else
			g_propagate_error (error, local_error);

	}

	return contact;
}

static gboolean
book_backend_file_get_contact_list_sync (EBookBackend *backend,
                                         const gchar *query,
                                         GQueue *out_contacts,
                                         GCancellable *cancellable,
                                         GError **error)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	GSList *summary_list;
	GSList *link;
	gboolean success = TRUE;
	GError *local_error = NULL;

	d (printf ("book_backend_file_get_contact_list_sync (%s)\n", query));

	g_rw_lock_reader_lock (&(bf->priv->lock));
	summary_list = e_book_backend_sqlitedb_search (
		bf->priv->sqlitedb, SQLITEDB_FOLDER_ID,
		query, NULL,
		NULL, NULL, &local_error);
	g_rw_lock_reader_unlock (&(bf->priv->lock));

	if (local_error != NULL) {

		g_warn_if_fail (summary_list == NULL);

		if (g_error_matches (local_error,
				     E_BOOK_SDB_ERROR,
				     E_BOOK_SDB_ERROR_NOT_SUPPORTED)) {
			g_set_error (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_NOT_SUPPORTED,
				_("Query '%s' not supported"), query);
			g_error_free (local_error);

		} else if (g_error_matches (local_error,
				     E_BOOK_SDB_ERROR,
				     E_BOOK_SDB_ERROR_INVALID_QUERY)) {
			g_set_error (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_INVALID_QUERY,
				_("Invalid Query '%s'"), query);
			g_error_free (local_error);

		} else {
			g_warning ("Failed to fetch contact ids: %s", local_error->message);
			g_propagate_error (error, local_error);
		}

		success = FALSE;
	}

	for (link = summary_list; link != NULL; link = g_slist_next (link)) {
		EbSdbSearchData *data = link->data;
		EContact *contact;

		contact = e_contact_new_from_vcard (data->vcard);
		g_queue_push_tail (out_contacts, contact);
	}

	g_slist_free_full (
		summary_list, (GDestroyNotify)
		e_book_backend_sqlitedb_search_data_free);

	return success;
}

static gboolean
book_backend_file_get_contact_list_uids_sync (EBookBackend *backend,
                                              const gchar *query,
                                              GQueue *out_uids,
                                              GCancellable *cancellable,
                                              GError **error)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	GSList *uids;
	GSList *link;
	gboolean success = TRUE;
	GError *local_error = NULL;

	d (printf ("book_backend_file_get_contact_list_sync (%s)\n", query));

	g_rw_lock_reader_lock (&(bf->priv->lock));
	uids = e_book_backend_sqlitedb_search_uids (
		bf->priv->sqlitedb,
		SQLITEDB_FOLDER_ID,
		query, NULL, &local_error);
	g_rw_lock_reader_unlock (&(bf->priv->lock));

	if (local_error != NULL) {

		g_warn_if_fail (uids == NULL);

		if (g_error_matches (local_error,
				     E_BOOK_SDB_ERROR,
				     E_BOOK_SDB_ERROR_NOT_SUPPORTED)) {
			g_set_error (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_NOT_SUPPORTED,
				_("Query '%s' not supported"), query);
			g_error_free (local_error);

		} else if (g_error_matches (local_error,
				     E_BOOK_SDB_ERROR,
				     E_BOOK_SDB_ERROR_INVALID_QUERY)) {
			g_set_error (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_INVALID_QUERY,
				_("Invalid Query '%s'"), query);
			g_error_free (local_error);

		} else {
			g_warning ("Failed to fetch contact ids: %s", local_error->message);
			g_propagate_error (error, local_error);
		}

		success = FALSE;
	}

	/* Transfer UID strings to the GQueue. */
	for (link = uids; link != NULL; link = g_slist_next (link))
		g_queue_push_tail (out_uids, link->data);

	g_slist_free (uids);

	return success;
}

static void
book_backend_file_start_view (EBookBackend *backend,
                              EDataBookView *book_view)
{
	FileBackendSearchClosure *closure = init_closure (book_view, E_BOOK_BACKEND_FILE (backend));

	d (printf ("starting book view thread\n"));
	closure->thread = g_thread_new (NULL, book_view_thread, book_view);

	e_flag_wait (closure->running);

	/* at this point we know the book view thread is actually running */
	d (printf ("returning from start_view\n"));
}

static void
book_backend_file_stop_view (EBookBackend *backend,
                             EDataBookView *book_view)
{
	FileBackendSearchClosure *closure = get_closure (book_view);
	gboolean need_join;

	if (!closure)
		return;

	d (printf ("stopping query\n"));
	need_join = e_flag_is_set (closure->running);
	e_flag_clear (closure->running);

	if (need_join) {
		g_thread_join (closure->thread);
		closure->thread = NULL;
	}
}

static void
book_backend_file_notify_update (EBookBackend *backend,
                                 const EContact *contact)
{
	GList *list, *link;

	list = e_book_backend_list_views (backend);

	for (link = list; link != NULL; link = g_list_next (link))
		view_notify_update (
			E_BOOK_BACKEND_FILE (backend),
			E_DATA_BOOK_VIEW (link->data),
			(EContact *) contact);

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
}

static EDataBookDirect *
book_backend_file_get_direct_book (EBookBackend *backend)
{
	EDataBookDirect *direct;
	ESourceRegistry *registry;
	ESource *source;
	gchar *backend_path;
	gchar *dirname;
	const gchar *modules_env = NULL;

	modules_env = g_getenv (EDS_ADDRESS_BOOK_MODULES);

	source = e_backend_get_source (E_BACKEND (backend));
	registry = e_book_backend_get_registry (backend);
	dirname = e_book_backend_file_extract_path_from_source (
		registry, source, GET_PATH_DB_DIR);

	/* Support in-tree testing / relocated modules */
	if (modules_env)
		backend_path = g_build_filename (modules_env, "libebookbackendfile.so", NULL);
	else
		backend_path = g_build_filename (BACKENDDIR, "libebookbackendfile.so", NULL);
	direct = e_data_book_direct_new (backend_path, "EBookBackendFileFactory", dirname);

	g_free (backend_path);
	g_free (dirname);

	return direct;
}

static void
book_backend_file_configure_direct (EBookBackend *backend,
                                    const gchar *config)
{
	EBookBackendFilePrivate *priv;

	priv = E_BOOK_BACKEND_FILE_GET_PRIVATE (backend);
	priv->base_directory = g_strdup (config);
}

static void
book_backend_file_sync (EBookBackend *backend)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);

	g_return_if_fail (bf != NULL);

	/* FIXME: Tell sqlite to dump NOW ! */
}

static void
book_backend_file_set_locale (EBookBackend *backend,
			      const gchar  *locale)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	gboolean success;
	GError *error = NULL;

	g_rw_lock_writer_lock (&(bf->priv->lock));

	success = e_book_backend_sqlitedb_lock_updates (bf->priv->sqlitedb,
							&error);

	if (!success) {
		g_warning ("Failed to start SQLite transaction: %s", error->message);
		g_clear_error (&error);
	}

	if (success) {
		success = e_book_backend_sqlitedb_set_locale (bf->priv->sqlitedb,
							      SQLITEDB_FOLDER_ID,
							      locale,
							      &error);
		if (!success) {
			g_warning ("Failed to set locale on SQLiteDB: %s", error->message);
			g_clear_error (&error);
		}
	}

	if (success) {
		success = e_book_backend_file_bump_revision (bf, &error);

		if (!success) {
			g_warning ("Failed to set locale on SQLiteDB: %s", error->message);
			g_clear_error (&error);
		}
	}

	if (success) {
		success = e_book_backend_sqlitedb_unlock_updates (bf->priv->sqlitedb,
								  TRUE, &error);

		if (!success) {
			g_warning ("Failed to commit SQLite transaction: %s", error->message);
			g_clear_error (&error);
		}

	} else {
		GError *error = NULL;

		/* Rollback transaction */
		if (!e_book_backend_sqlitedb_unlock_updates (bf->priv->sqlitedb, FALSE, &error)) {
			g_warning ("Failed to rollback transaction after failing to add contacts: %s",
				   error->message);
			g_clear_error (&error);
		}
	}

	cursors_locale_changed (bf);

	/* We set the new locale, now update our local variable */
	if (success) {
		g_free (bf->priv->locale);
		bf->priv->locale = g_strdup (locale);
	}

	g_rw_lock_writer_unlock (&(bf->priv->lock));
}

static gchar *
book_backend_file_dup_locale (EBookBackend *backend)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	gchar *locale;

	g_rw_lock_reader_lock (&(bf->priv->lock));
	locale = g_strdup (bf->priv->locale);
	g_rw_lock_reader_unlock (&(bf->priv->lock));

	return locale;
}

static EDataBookCursor *
book_backend_file_create_cursor (EBookBackend *backend,
				 EContactField *sort_fields,
				 EBookCursorSortType *sort_types,
				 guint n_fields,
				 GError **error)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	EDataBookCursor  *cursor;

	g_rw_lock_writer_lock (&(bf->priv->lock));

	cursor = e_data_book_cursor_sqlite_new (backend,
						bf->priv->sqlitedb,
						SQLITEDB_FOLDER_ID,
						sort_fields,
						sort_types,
						n_fields,
						error);

	if (cursor)
		bf->priv->cursors =
			g_list_prepend (bf->priv->cursors, cursor);

	g_rw_lock_writer_unlock (&(bf->priv->lock));

	return cursor;
}

static gboolean
book_backend_file_delete_cursor (EBookBackend *backend,
				 EDataBookCursor *cursor,
				 GError **error)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	GList *link;

	g_rw_lock_writer_lock (&(bf->priv->lock));

	link = g_list_find (bf->priv->cursors, cursor);

	if (link != NULL) {
		bf->priv->cursors = g_list_delete_link (bf->priv->cursors, link);
		g_object_unref (cursor);
	} else {
		g_set_error_literal (error,
				     E_CLIENT_ERROR,
				     E_CLIENT_ERROR_INVALID_ARG,
				     _("Requested to delete an unrelated cursor"));
	}

	g_rw_lock_writer_unlock (&(bf->priv->lock));

	return link != NULL;
}

static gboolean
book_backend_file_initable_init (GInitable *initable,
                                 GCancellable *cancellable,
                                 GError **error)
{
	EBookBackendFilePrivate *priv;
	ESourceBackendSummarySetup *setup_extension;
	ESourceRegistry *registry;
	ESource *source;
	const gchar *extension_name;
	gchar *backup;
	gchar *dirname;
	gchar *filename;
	gboolean success = TRUE;

	priv = E_BOOK_BACKEND_FILE_GET_PRIVATE (initable);

	source = e_backend_get_source (E_BACKEND (initable));
	registry = e_book_backend_get_registry (E_BOOK_BACKEND (initable));

	g_type_ensure (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP);
	extension_name = E_SOURCE_EXTENSION_BACKEND_SUMMARY_SETUP;
	setup_extension = e_source_get_extension (source, extension_name);

	if (priv->base_directory)
		dirname = g_strdup (priv->base_directory);
	else
		dirname = e_book_backend_file_extract_path_from_source (
			registry, source, GET_PATH_DB_DIR);

	filename = g_build_filename (dirname, "addressbook.db", NULL);
	backup = g_build_filename (dirname, "addressbook.db.old", NULL);

	/* The old BDB exists, lets migrate that to sqlite right away. */
	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		priv->sqlitedb = e_book_backend_sqlitedb_new_full (
			dirname,
			SQLITEDB_EMAIL_ID,
			SQLITEDB_FOLDER_ID,
			SQLITEDB_FOLDER_NAME,
			TRUE, setup_extension,
			error);

		if (priv->sqlitedb == NULL) {
			success = FALSE;
			goto exit;
		}

		success = e_book_backend_file_migrate_bdb (
			priv->sqlitedb,
			SQLITEDB_FOLDER_ID,
			dirname, filename, error);

		if (!success)
			goto exit;

		/* Now that we've migrated the database,
		 * lets rename it instead of unlinking it. */
		if (g_rename (filename, backup) < 0) {
			g_set_error (
				error, G_FILE_ERROR,
				g_file_error_from_errno (errno),
				_("Failed to rename old database from "
				"'%s' to '%s': %s"), filename, backup,
				g_strerror (errno));
			success = FALSE;
			goto exit;
		}
	}

	/* If we already have a handle on this, it means there
	 * was an old BDB migrated and no need to reopen it. */
	if (priv->sqlitedb == NULL) {
		gboolean populated;
		GError *local_error = NULL;

		/* Ensure the directory exists first. */
		success = create_directory (dirname, error);

		if (!success)
			goto exit;

		/* Create the sqlitedb. */
		priv->sqlitedb = e_book_backend_sqlitedb_new_full (
			dirname,
			SQLITEDB_EMAIL_ID,
			SQLITEDB_FOLDER_ID,
			SQLITEDB_FOLDER_NAME,
			TRUE, setup_extension,
			error);

		if (priv->sqlitedb == NULL) {
			success = FALSE;
			goto exit;
		}

		/* An sqlite DB only 'exists' if the populated flag is set. */
		populated = e_book_backend_sqlitedb_get_is_populated (
			priv->sqlitedb,
			SQLITEDB_FOLDER_ID,
			&local_error);

		if (local_error != NULL) {
			g_propagate_error (error, local_error);
			success = FALSE;
			goto exit;
		}

		if (!populated) {
			/* Set the populated flag. */
			success = e_book_backend_sqlitedb_set_is_populated (
				priv->sqlitedb,
				SQLITEDB_FOLDER_ID,
				TRUE, error);

			if (!success)
				goto exit;
		}
	}

	/* Load the locale */
	e_book_backend_file_load_locale (E_BOOK_BACKEND_FILE (initable));

	/* Resolve the photo directory here. */
	priv->photo_dirname =
		e_book_backend_file_extract_path_from_source (
		registry, source, GET_PATH_PHOTO_DIR);
	success = create_directory (priv->photo_dirname, error);

exit:
	g_free (dirname);
	g_free (filename);
	g_free (backup);

	return success;
}

static void
e_book_backend_file_class_init (EBookBackendFileClass *class)
{
	GObjectClass *object_class;
	EBookBackendClass *backend_class;

	g_type_class_add_private (class, sizeof (EBookBackendFilePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = book_backend_file_dispose;
	object_class->finalize = book_backend_file_finalize;

	backend_class = E_BOOK_BACKEND_CLASS (class);
	backend_class->get_backend_property = book_backend_file_get_backend_property;
	backend_class->open_sync = book_backend_file_open_sync;
	backend_class->create_contacts_sync = book_backend_file_create_contacts_sync;
	backend_class->modify_contacts_sync = book_backend_file_modify_contacts_sync;
	backend_class->remove_contacts_sync = book_backend_file_remove_contacts_sync;
	backend_class->get_contact_sync = book_backend_file_get_contact_sync;
	backend_class->get_contact_list_sync = book_backend_file_get_contact_list_sync;
	backend_class->get_contact_list_uids_sync = book_backend_file_get_contact_list_uids_sync;
	backend_class->start_view = book_backend_file_start_view;
	backend_class->stop_view = book_backend_file_stop_view;
	backend_class->notify_update = book_backend_file_notify_update;
	backend_class->get_direct_book = book_backend_file_get_direct_book;
	backend_class->configure_direct = book_backend_file_configure_direct;
	backend_class->sync = book_backend_file_sync;
	backend_class->set_locale = book_backend_file_set_locale;
	backend_class->dup_locale = book_backend_file_dup_locale;
	backend_class->create_cursor = book_backend_file_create_cursor;
	backend_class->delete_cursor = book_backend_file_delete_cursor;
}

static void
e_book_backend_file_initable_init (GInitableIface *interface)
{
	interface->init = book_backend_file_initable_init;
}

static void
e_book_backend_file_init (EBookBackendFile *backend)
{
	backend->priv = E_BOOK_BACKEND_FILE_GET_PRIVATE (backend);

	g_rw_lock_init (&(backend->priv->lock));
}


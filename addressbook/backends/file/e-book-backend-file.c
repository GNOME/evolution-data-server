/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-file.c - File contact backend.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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

#define EDB_ERROR(_code)          e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, NULL)
#define EDB_ERROR_EX(_code, _msg) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, _msg)
#define EDB_NOT_OPENED_ERROR      EDB_ERROR(NOT_OPENED)


/* This forces the GType to be registered in a way that
 * avoids a "statement with no effect" compiler warning.
 * FIXME Use g_type_ensure() once we require GLib 2.34. */
#define REGISTER_TYPE(type) \
	(g_type_class_unref (g_type_class_ref (type)))

G_DEFINE_TYPE (EBookBackendFile, e_book_backend_file, E_TYPE_BOOK_BACKEND_SYNC)

struct _EBookBackendFilePrivate {
	gchar     *base_directory;
	gchar     *photo_dirname;
	gchar     *revision;
	gint       rev_counter;
	gboolean   revision_guards;
	GRWLock    lock;

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
			g_propagate_error (error, EDB_ERROR (PERMISSION_DENIED));
		} else {
			g_propagate_error (
				error, e_data_book_create_error_fmt (
				E_DATA_BOOK_STATUS_OTHER_ERROR,
				_("Failed to remove file '%s': %s"),
				filename, g_strerror (errno)));
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
			g_propagate_error (error, EDB_ERROR (PERMISSION_DENIED));
		else
			g_propagate_error (
				error, e_data_book_create_error_fmt (
				E_DATA_BOOK_STATUS_OTHER_ERROR,
				_("Failed to make directory %s: %s"),
				dirname, g_strerror (errno)));
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
	if (builtin_source != NULL && e_source_equal (source, builtin_source))
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

	if (builtin_source)
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
			g_propagate_error (error, EDB_ERROR (PERMISSION_DENIED));
		} else {
			g_propagate_error (
				error, e_data_book_create_error_fmt (
				E_DATA_BOOK_STATUS_OTHER_ERROR,
				_("Failed to create hardlink for resource '%s': %s"),
				src_filename, g_strerror (errno)));
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
			g_propagate_error (error, EDB_ERROR_EX (OTHER_ERROR, _("No UID in the contact")));
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
	return g_strdup_printf (PAS_ID_PREFIX "%08lX%08X", time (NULL), c++);
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
static void
e_book_backend_file_bump_revision_locked (EBookBackendFile *bf)
{
	GError *error = NULL;

	g_free (bf->priv->revision);
	bf->priv->revision = e_book_backend_file_new_revision (bf);

	if (!e_book_backend_sqlitedb_set_revision (bf->priv->sqlitedb,
						   SQLITEDB_FOLDER_ID,
						   bf->priv->revision,
						   &error)) {
		g_warning (G_STRLOC ": Error setting database revision: %s",
			   error->message);
		g_error_free (error);
	}

	e_book_backend_notify_property_changed (E_BOOK_BACKEND (bf),
						BOOK_BACKEND_PROPERTY_REVISION,
						bf->priv->revision);
}

static void
e_book_backend_file_bump_revision (EBookBackendFile *bf)
{
	g_rw_lock_writer_lock (&(bf->priv->lock));
	e_book_backend_file_bump_revision_locked (bf);
	g_rw_lock_writer_unlock (&(bf->priv->lock));
}

static void
e_book_backend_file_load_revision (EBookBackendFile *bf)
{
	GError *error = NULL;

	if (!e_book_backend_sqlitedb_get_revision (bf->priv->sqlitedb,
						   SQLITEDB_FOLDER_ID,
						   &bf->priv->revision,
						   &error)) {
		g_warning (G_STRLOC ": Error loading database revision: %s",
			   error->message);
		g_error_free (error);
	} else if (bf->priv->revision == NULL) {
		e_book_backend_file_bump_revision (bf);
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
	   const GSList *vcards_req,
	   GSList **contacts,
	   GError **perror)
{
	GSList *slist = NULL;
	const GSList *l;
	PhotoModifiedStatus status = STATUS_NORMAL;
	GError *local_error = NULL;

	g_assert (bf);
	g_assert (vcards_req);

	if (!bf || !bf->priv || !bf->priv->sqlitedb) {
		g_propagate_error (perror, EDB_NOT_OPENED_ERROR);
		return FALSE;
	}

	for (l = vcards_req; l != NULL; l = l->next) {
		gchar           *id;
		const gchar     *rev;
		const gchar     *vcard_req;
		EContact        *contact;

		vcard_req = (const gchar *) l->data;

		id      = e_book_backend_file_create_unique_id ();
		contact = e_contact_new_from_vcard_with_uid (vcard_req, id);

		rev = e_contact_get_const (contact, E_CONTACT_REV);
		if (!(rev && *rev))
			set_revision (bf, contact);

		status = maybe_transform_vcard_for_photo (bf, NULL, contact, perror);

		g_free (id);

		if (status != STATUS_ERROR) {

			/* Contact was added successfully, add it to the local list */
			slist = g_slist_prepend (slist, contact);
		} else {
			/* Contact could not be transformed */
			g_warning (G_STRLOC ": Error transforming vcard with image data %s",
				(perror && *perror) ? (*perror)->message :
				"Unknown error transforming vcard");
			g_object_unref (contact);

			/* Abort as soon as an error occurs */
			break;
		}
	}

	if (status != STATUS_ERROR) {

		if (!e_book_backend_sqlitedb_add_contacts (bf->priv->sqlitedb,
							   SQLITEDB_FOLDER_ID,
							   slist, FALSE,
							   &local_error)) {
			
			g_warning ("Failed to add contacts: %s", local_error->message);
			g_propagate_error (perror, local_error);

			status = STATUS_ERROR;
		}
	}

	if (status != STATUS_ERROR && contacts != NULL) {
		*contacts = g_slist_reverse (slist);
	} else {

		if (contacts)
			*contacts = NULL;

		e_util_free_object_slist (slist);
	}

	return (status != STATUS_ERROR);
}

static void
e_book_backend_file_create_contacts (EBookBackendSync *backend,
                                     EDataBook *book,
                                     GCancellable *cancellable,
                                     const GSList *vcards,
                                     GSList **added_contacts,
                                     GError **perror)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);

	g_rw_lock_writer_lock (&(bf->priv->lock));

	if (do_create (bf, vcards, added_contacts, perror)) {
		e_book_backend_file_bump_revision_locked (bf);
	}

	g_rw_lock_writer_unlock (&(bf->priv->lock));
}

static void
e_book_backend_file_remove_contacts (EBookBackendSync *backend,
                                     EDataBook *book,
                                     GCancellable *cancellable,
                                     const GSList *id_list,
                                     GSList **ids,
                                     GError **perror)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	GSList           *removed_ids = NULL, *removed_contacts = NULL;
	GError           *local_error = NULL;
	gboolean          delete_failed = FALSE;
	const GSList     *l;

	g_rw_lock_writer_lock (&(bf->priv->lock));

	for (l = id_list; l != NULL; l = l->next) {
		const gchar *id;
		EContact *contact;

		id = l->data;

		/* First load the EContacts which need to be removed, we might delete some
		 * photos from disk because of this...
		 *
		 * Note: sqlitedb backend can probably make this faster by executing a
		 * single query to fetch a list of contacts for a list of ids, the
		 * current method makes a query for each UID.
		 */
		contact = e_book_backend_sqlitedb_get_contact (bf->priv->sqlitedb,
							       SQLITEDB_FOLDER_ID, id,
							       NULL, NULL, &local_error);

		if (contact) {
			removed_ids      = g_slist_prepend (removed_ids, g_strdup (id));
			removed_contacts = g_slist_prepend (removed_contacts, contact);
		} else {
			g_warning ("Failed to fetch contact to be removed: %s", local_error->message);
			g_propagate_error (perror, local_error);

			/* Abort as soon as missing contact is to be deleted */
			delete_failed = TRUE;
			break;
		}
	}

	if (!delete_failed) {

		/* Delete URI associated to those contacts */
		for (l = removed_contacts; l; l = l->next) {
			maybe_delete_unused_uris (bf, E_CONTACT (l->data), NULL);
		}

		/* Remove from summary as well */
		if (!e_book_backend_sqlitedb_remove_contacts (bf->priv->sqlitedb,
						      SQLITEDB_FOLDER_ID,
						      removed_ids, &local_error)) {
			g_warning ("Failed to remove contacts: %s", local_error->message);
			g_propagate_error (perror, local_error);
		}

		e_book_backend_file_bump_revision_locked (bf);

		*ids = removed_ids;
	} else {
		*ids = NULL;
		e_util_free_string_slist (removed_ids);
	}

	g_rw_lock_writer_unlock (&(bf->priv->lock));

	e_util_free_object_slist (removed_contacts);
}

static void
e_book_backend_file_modify_contacts (EBookBackendSync *backend,
                                     EDataBook *book,
                                     GCancellable *cancellable,
                                     const GSList *vcards,
                                     GSList **contacts,
                                     GError **perror)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	const GSList     *lold, *l;
	GSList           *old_contacts = NULL, *modified_contacts = NULL;
	GSList           *ids = NULL;
	GError           *local_error = NULL;
	PhotoModifiedStatus status = STATUS_NORMAL;

	if (!bf || !bf->priv || !bf->priv->sqlitedb) {
		g_propagate_error (perror, EDB_NOT_OPENED_ERROR);
		return;
	}

	g_rw_lock_writer_lock (&(bf->priv->lock));

	for (l = vcards; l != NULL; l = l->next) {
		gchar *id;
		EContact *contact, *old_contact;
		const gchar *contact_rev, *old_contact_rev;

		contact = e_contact_new_from_vcard (l->data);
		id = e_contact_get (contact, E_CONTACT_UID);

		if (id == NULL) {
			status = STATUS_ERROR;

			g_propagate_error (perror, EDB_ERROR_EX (OTHER_ERROR, _("No UID in the contact")));
			g_object_unref (contact);
			break;
		}

		old_contact = e_book_backend_sqlitedb_get_contact (bf->priv->sqlitedb,
								   SQLITEDB_FOLDER_ID, id,
								   NULL, NULL, &local_error);
		if (!old_contact) {
			g_warning (G_STRLOC ": Failed to load contact %s: %s", id, local_error->message);
			g_propagate_error (perror, local_error);

			status = STATUS_ERROR;

			g_free (id);
			g_object_unref (contact);
			break;
		}

		if (bf->priv->revision_guards) {
			contact_rev = e_contact_get_const (contact, E_CONTACT_REV);
			old_contact_rev = e_contact_get_const (old_contact, E_CONTACT_REV);

			if (!contact_rev || !old_contact_rev ||
			    strcmp (contact_rev, old_contact_rev) != 0) {
				g_set_error (perror, E_CLIENT_ERROR,
					     E_CLIENT_ERROR_OUT_OF_SYNC,
					     _("Tried to modify contact '%s' with out of sync revision"),
					     e_contact_get_const (contact, E_CONTACT_UID));

				status = STATUS_ERROR;

				g_free (id);
				g_object_unref (contact);
				g_object_unref (old_contact);
				break;
			}
		}

		/* Transform incomming photo blobs to uris before storing this to the DB */
		status = maybe_transform_vcard_for_photo (bf, old_contact, contact, &local_error);
		if (status == STATUS_ERROR) {
			g_warning (G_STRLOC ": Error transforming contact %s: %s", id, local_error->message);
			g_propagate_error (perror, local_error);

			g_free (id);
			g_object_unref (old_contact);
			g_object_unref (contact);
			break;
		}

		/* update the revision (modified time of contact) */
		set_revision (bf, contact);

		old_contacts      = g_slist_prepend (old_contacts, old_contact);
		modified_contacts = g_slist_prepend (modified_contacts, contact);
		ids               = g_slist_prepend (ids, id);
	}

	if (status != STATUS_ERROR) {

		/* Delete old photo file uris if need be (this will compare the new contact
		 * with the current copy in the BDB to extract the uris to delete) */
		lold = old_contacts;
		l    = modified_contacts;
		while (lold && l) {
			maybe_delete_unused_uris (bf, E_CONTACT (lold->data), E_CONTACT (l->data));
			lold = lold->next;
			l = l->next;
		}

		/* Update summary as well */
		if (!e_book_backend_sqlitedb_add_contacts (bf->priv->sqlitedb,
							   SQLITEDB_FOLDER_ID,
							   modified_contacts, FALSE, &local_error)) {
			g_warning ("Failed to modify contacts: %s", local_error->message);
			g_propagate_error (perror, local_error);

			status = STATUS_ERROR;
		}
	}

	if (status != STATUS_ERROR)
		e_book_backend_file_bump_revision_locked (bf);

	g_rw_lock_writer_unlock (&(bf->priv->lock));

	if (status != STATUS_ERROR) {
		*contacts = g_slist_reverse (modified_contacts);
	} else {
		*contacts = NULL;
		e_util_free_object_slist (modified_contacts);
	}

	e_util_free_string_slist (ids);
	g_slist_free_full (old_contacts, g_object_unref);
}

static void
e_book_backend_file_get_contact (EBookBackendSync *backend,
                                 EDataBook *book,
                                 GCancellable *cancellable,
                                 const gchar *id,
                                 gchar **vcard,
                                 GError **perror)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);

	if (!bf || !bf->priv || !bf->priv->sqlitedb) {
		g_propagate_error (perror, EDB_NOT_OPENED_ERROR);
		return;
	}

	g_rw_lock_reader_lock (&(bf->priv->lock));

	*vcard = e_book_backend_sqlitedb_get_vcard_string (bf->priv->sqlitedb,
							   SQLITEDB_FOLDER_ID, id,
							   NULL, NULL, perror);
	g_rw_lock_reader_unlock (&(bf->priv->lock));
}

static void
e_book_backend_file_get_contact_list (EBookBackendSync *backend,
                                      EDataBook *book,
                                      GCancellable *cancellable,
                                      const gchar *query,
                                      GSList **contacts,
                                      GError **perror)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	GSList           *contact_list = NULL, *l;
	GSList           *summary_list = NULL;
	GError           *local_error = NULL;

	d (printf ("e_book_backend_file_get_contact_list (%s)\n", query));

	if (!bf || !bf->priv || !bf->priv->sqlitedb) {
		g_propagate_error (perror, EDB_NOT_OPENED_ERROR);
		return;
	}

	g_rw_lock_reader_lock (&(bf->priv->lock));
	summary_list = e_book_backend_sqlitedb_search (
		bf->priv->sqlitedb, SQLITEDB_FOLDER_ID,
		query, NULL,
		NULL, NULL, &local_error);
	g_rw_lock_reader_unlock (&(bf->priv->lock));

	if (summary_list) {

		for (l = summary_list; l; l = l->next) {
			EbSdbSearchData *data = l->data;

			/* Steal the memory directly from the returned query */
			contact_list = g_slist_prepend (contact_list, data->vcard);
			data->vcard  = NULL;
		}

		g_slist_foreach (summary_list, (GFunc) e_book_backend_sqlitedb_search_data_free, NULL);
		g_slist_free (summary_list);

	} else if (local_error != NULL) {
		g_warning ("Failed to fetch contacts: %s", local_error->message);
		g_propagate_error (perror, local_error);
	}

	*contacts = contact_list;
}

static void
e_book_backend_file_get_contact_list_uids (EBookBackendSync *backend,
                                           EDataBook *book,
                                           GCancellable *cancellable,
                                           const gchar *query,
                                           GSList **contacts_uids,
                                           GError **perror)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	GSList           *uids = NULL;
	GError           *local_error = NULL;

	d (printf ("e_book_backend_file_get_contact_list (%s)\n", query));

	if (!bf || !bf->priv || !bf->priv->sqlitedb) {
		g_propagate_error (perror, EDB_NOT_OPENED_ERROR);
		return;
	}

	g_rw_lock_reader_lock (&(bf->priv->lock));
	uids = e_book_backend_sqlitedb_search_uids (
		bf->priv->sqlitedb,
		SQLITEDB_FOLDER_ID,
		query, NULL, &local_error);
	g_rw_lock_reader_unlock (&(bf->priv->lock));

	if (uids == NULL && local_error != NULL) {
		g_warning ("Failed to fetch contact ids: %s", local_error->message);
		g_propagate_error (perror, local_error);
	}

	*contacts_uids = g_slist_reverse (uids);
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
	e_data_book_view_ref (book_view);

	query = e_data_book_view_get_card_query (book_view);
	fields_of_interest = e_data_book_view_get_fields_of_interest (book_view);

	if (!bf->priv->sqlitedb) {
		e_data_book_view_notify_complete (book_view, EDB_NOT_OPENED_ERROR);
		e_data_book_view_unref (book_view);
		return NULL;
	}

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
		e_data_book_view_notify_complete (book_view, EDB_NOT_OPENED_ERROR);
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

	e_data_book_view_unref (book_view);

	d (printf ("finished population of book view\n"));

	return NULL;
}

static void
e_book_backend_file_start_book_view (EBookBackend *backend,
                                     EDataBookView *book_view)
{
	FileBackendSearchClosure *closure = init_closure (book_view, E_BOOK_BACKEND_FILE (backend));

	d (printf ("starting book view thread\n"));
	closure->thread = g_thread_create (book_view_thread, book_view, TRUE, NULL);

	e_flag_wait (closure->running);

	/* at this point we know the book view thread is actually running */
	d (printf ("returning from start_book_view\n"));
}

static void
e_book_backend_file_stop_book_view (EBookBackend *backend,
                                    EDataBookView *book_view)
{
	FileBackendSearchClosure *closure = get_closure (book_view);
	gboolean need_join;

	if (!closure)
		return;

	d (printf ("stopping query\n"));
	need_join = e_flag_is_set (closure->running);
	e_flag_clear (closure->running);

	if (need_join)
		g_thread_join (closure->thread);
}


#ifdef CREATE_DEFAULT_VCARD
# include <libedata-book/ximian-vcard.h>
#endif

static void
e_book_backend_file_open (EBookBackendSync *backend,
                          EDataBook *book,
                          GCancellable *cancellable,
                          gboolean only_if_exists,
                          GError **perror)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	gchar            *dirname, *filename, *backup;
	ESourceRegistry  *registry;
	ESource          *source;
	GError           *local_error = NULL;
	gboolean          populated;
	ESourceBackendSummarySetup *setup;
	ESourceAddressBookConfig *config;

	source = e_backend_get_source (E_BACKEND (backend));
	registry = e_book_backend_get_registry (E_BOOK_BACKEND (backend));

	if (bf->priv->base_directory)
		dirname = g_strdup (bf->priv->base_directory);
	else
		dirname = e_book_backend_file_extract_path_from_source (
		        registry, source, GET_PATH_DB_DIR);

	filename = g_build_filename (dirname, "addressbook.db", NULL);
	backup   = g_build_filename (dirname, "addressbook.db.old", NULL);

	REGISTER_TYPE (E_TYPE_SOURCE_ADDRESS_BOOK_CONFIG);
	REGISTER_TYPE (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP);

	config         = e_source_get_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK_CONFIG);
	bf->priv->revision_guards = e_source_address_book_config_get_revision_guards_enabled (config);

	setup = e_source_get_extension (source, E_SOURCE_EXTENSION_BACKEND_SUMMARY_SETUP);
 
	/* The old BDB exists, lets migrate that to sqlite right away
	 */
	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		bf->priv->sqlitedb = e_book_backend_sqlitedb_new_full (
                        dirname,
			SQLITEDB_EMAIL_ID,
			SQLITEDB_FOLDER_ID,
			SQLITEDB_FOLDER_NAME,
			TRUE, setup,
			&local_error);

		if (!bf->priv->sqlitedb) {
			g_warning (G_STRLOC ": Failed to open sqlitedb: %s", local_error->message);
			g_propagate_error (perror, local_error);
			g_free (dirname);
			g_free (filename);
			g_free (backup);
			return;
		}

		if (!e_book_backend_file_migrate_bdb (bf->priv->sqlitedb,
						      SQLITEDB_FOLDER_ID,
						      dirname, filename, &local_error)) {

			/* Perhaps this error should not be fatal */
			g_warning (G_STRLOC ": Failed to migrate old BDB to sqlitedb: %s", local_error->message);
			g_propagate_error (perror, local_error);
			g_free (dirname);
			g_free (filename);
			g_free (backup);
 
			g_object_unref (bf->priv->sqlitedb);
			bf->priv->sqlitedb = NULL;
			return;
		}

		/* Now we've migrated the database, lets rename it instead of unlinking it */
		if (g_rename (filename, backup) < 0) {

			g_warning (G_STRLOC ": Failed to rename old database from '%s' to '%s': %s",
				   filename, backup, g_strerror (errno));

			g_propagate_error (perror, e_data_book_create_error_fmt
					   (E_DATA_BOOK_STATUS_OTHER_ERROR,
					    _("Failed to rename old database from '%s' to '%s': %s"),
					    filename, backup, g_strerror (errno)));

			g_free (dirname);
			g_free (filename);
			g_free (backup);
			bf->priv->sqlitedb = NULL;
			return;
 		}
	}

	/* If we already have a handle on this, it means there was an old BDB migrated
	 * and no need to reopen it
	 */
	if (bf->priv->sqlitedb == NULL) {

		/* ensure the directory exists first */
		if (!only_if_exists && !create_directory (dirname, &local_error)) {

			g_warning (G_STRLOC ": Failed to create directory for sqlite db: %s", local_error->message);
			g_propagate_error (perror, local_error);

 			g_free (dirname);
 			g_free (filename);
			g_free (backup);
 			return;
 		}

		/* Create the sqlitedb */
		bf->priv->sqlitedb = e_book_backend_sqlitedb_new_full (
                        dirname,
			SQLITEDB_EMAIL_ID,
			SQLITEDB_FOLDER_ID,
			SQLITEDB_FOLDER_NAME,
			TRUE, setup,
			&local_error);

		if (!bf->priv->sqlitedb) {
			g_warning (G_STRLOC ": Failed to open sqlitedb: %s", local_error->message);
			g_propagate_error (perror, local_error);
 			g_free (dirname);
 			g_free (filename);
			g_free (backup);
 			return;
 		}

		/* An sqlite DB only 'exists' if the populated flag is set */
		populated = e_book_backend_sqlitedb_get_is_populated (bf->priv->sqlitedb,
								      SQLITEDB_FOLDER_ID,
								      &local_error);

		if (local_error != NULL) {
			/* Perhaps this error should not be fatal */
			g_warning (G_STRLOC ": Failed to check populated flag in sqlite db: %s", local_error->message);
			g_propagate_error (perror, local_error);
 			g_free (dirname);
 			g_free (filename);
			g_free (backup);

			g_object_unref (bf->priv->sqlitedb);
			bf->priv->sqlitedb = NULL;
			return;
 		}

		if (!populated) {
			/* Shutdown, no such book ! */
			if (only_if_exists) {
 				g_free (dirname);
 				g_free (filename);
				g_free (backup);
				g_object_unref (bf->priv->sqlitedb);
				bf->priv->sqlitedb = NULL;
				g_propagate_error (perror, EDB_ERROR (NO_SUCH_BOOK));
				return;
 			}

#ifdef CREATE_DEFAULT_VCARD
			{
				GSList l;
				l.data = XIMIAN_VCARD;
				l.next = NULL;

				if (!do_create (bf, &l, NULL, NULL))
					g_warning ("Cannot create default contact");
			}
#endif

			/* Set the populated flag */
			if (!e_book_backend_sqlitedb_set_is_populated (bf->priv->sqlitedb,
								       SQLITEDB_FOLDER_ID,
								       TRUE,
								       &local_error)) {
				g_warning (G_STRLOC ": Failed to set populated flag in sqlite db: %s",
					   local_error->message);
				g_propagate_error (perror, local_error);
				g_free (dirname);
				g_free (filename);
				g_free (backup);
				g_object_unref (bf->priv->sqlitedb);
				bf->priv->sqlitedb = NULL;
				return;
			}
 		}
 	}

	g_free (dirname);
	g_free (filename);
	g_free (backup);

	/* Resolve the photo directory here (no need for photo directory
	 * in read access mode, it's only used for write access methods)
	 */
	dirname = e_book_backend_file_extract_path_from_source (
		registry, source, GET_PATH_PHOTO_DIR);
	if (!only_if_exists && !create_directory (dirname, perror))
		return;
	bf->priv->photo_dirname = dirname;

	e_book_backend_file_load_revision (bf);

	e_book_backend_notify_online (E_BOOK_BACKEND (backend), TRUE);
	e_book_backend_notify_readonly (E_BOOK_BACKEND (backend), FALSE);
	e_book_backend_notify_opened (E_BOOK_BACKEND (backend), NULL /* Success */);

	e_book_backend_notify_property_changed (E_BOOK_BACKEND (backend),
						BOOK_BACKEND_PROPERTY_REVISION,
						bf->priv->revision);
}

static gboolean
e_book_backend_file_get_backend_property (EBookBackendSync *backend,
                                          EDataBook *book,
                                          GCancellable *cancellable,
                                          const gchar *prop_name,
                                          gchar **prop_value,
                                          GError **error)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	gboolean processed = TRUE;

	g_return_val_if_fail (prop_name != NULL, FALSE);
	g_return_val_if_fail (prop_value != NULL, FALSE);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		*prop_value = g_strdup ("local,do-initial-query,bulk-adds,bulk-modifies,bulk-removes,contact-lists");
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		*prop_value = g_strdup (e_contact_field_name (E_CONTACT_FILE_AS));
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		GSList *fields = NULL;
		gint i;

		/* XXX we need a way to say "we support everything", since the
		 * file backend does */
		for (i = 1; i < E_CONTACT_FIELD_LAST; i++)
			fields = g_slist_append (fields, (gpointer) e_contact_field_name (i));

		*prop_value = e_data_book_string_slist_to_comma_string (fields);
		g_slist_free (fields);
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_AUTH_METHODS)) {
		*prop_value = NULL;
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REVISION)) {
		*prop_value = g_strdup (bf->priv->revision);
	} else {
		processed = FALSE;
	}

	return processed;
}

static void
e_book_backend_file_notify_online_cb (EBookBackend *backend,
                                      GParamSpec *pspec)
{
	if (e_book_backend_is_opened (backend))
		e_book_backend_notify_online (backend, TRUE);
}

static void
e_book_backend_file_sync (EBookBackend *backend)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);

	g_return_if_fail (bf != NULL);

	/* FIXME: Tell sqlite to dump NOW ! */
}

typedef struct {
	EContact         *contact;
	EBookBackendFile *bf;
} NotifyData;

static gboolean
view_notify_update (EDataBookView *view,
                    gpointer data)
{
	NotifyData *ndata    = data;
	GHashTable *fields   = e_data_book_view_get_fields_of_interest (view);
	gboolean    notified = FALSE;
	gboolean    with_all_required_fields = FALSE;

	if (e_book_backend_sqlitedb_is_summary_query (e_data_book_view_get_card_query (view)) &&
	    e_book_backend_sqlitedb_is_summary_fields (fields)) {

		const gchar *uid = e_contact_get_const (ndata->contact, E_CONTACT_UID);
		gchar       *vcard;

		vcard = e_book_backend_sqlitedb_get_vcard_string (
			ndata->bf->priv->sqlitedb,
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
		e_data_book_view_notify_update (view, ndata->contact);

	return TRUE;
}

static void
e_book_backend_file_notify_update (EBookBackend *backend,
                                   const EContact *contact)
{
	NotifyData data = { (EContact *) contact, E_BOOK_BACKEND_FILE (backend) };

	e_book_backend_foreach_view (backend, view_notify_update, &data);
}

static void
e_book_backend_file_dispose (GObject *object)
{
	EBookBackendFile *bf;

	bf = E_BOOK_BACKEND_FILE (object);

	if (bf->priv->sqlitedb) {
		g_object_unref (bf->priv->sqlitedb);
		bf->priv->sqlitedb = NULL;
	}

	G_OBJECT_CLASS (e_book_backend_file_parent_class)->dispose (object);
}

static void
e_book_backend_file_finalize (GObject *object)
{
	EBookBackendFilePrivate *priv;

	priv = E_BOOK_BACKEND_FILE_GET_PRIVATE (object);

	g_free (priv->photo_dirname);
	g_free (priv->revision);
	g_free (priv->base_directory);
	g_rw_lock_clear (&(priv->lock));

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_backend_file_parent_class)->finalize (object);
}


static EDataBookDirect *
e_book_backend_file_get_direct_book (EBookBackend *backend)
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
e_book_backend_file_configure_direct (EBookBackend *backend,
				      const gchar  *config)
{
	EBookBackendFilePrivate *priv;

	priv = E_BOOK_BACKEND_FILE_GET_PRIVATE (backend);
	priv->base_directory = g_strdup (config);
}

static void
e_book_backend_file_class_init (EBookBackendFileClass *class)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (class);
	EBookBackendSyncClass *sync_class;
	EBookBackendClass *backend_class;

	g_type_class_add_private (class, sizeof (EBookBackendFilePrivate));

	sync_class = E_BOOK_BACKEND_SYNC_CLASS (class);
	backend_class = E_BOOK_BACKEND_CLASS (class);

	/* Set the virtual methods. */
	backend_class->start_book_view		= e_book_backend_file_start_book_view;
	backend_class->stop_book_view		= e_book_backend_file_stop_book_view;
	backend_class->sync			= e_book_backend_file_sync;
	backend_class->notify_update            = e_book_backend_file_notify_update;
	backend_class->get_direct_book          = e_book_backend_file_get_direct_book;
	backend_class->configure_direct         = e_book_backend_file_configure_direct;

	sync_class->open_sync			= e_book_backend_file_open;
	sync_class->get_backend_property_sync	= e_book_backend_file_get_backend_property;
	sync_class->create_contacts_sync	= e_book_backend_file_create_contacts;
	sync_class->remove_contacts_sync	= e_book_backend_file_remove_contacts;
	sync_class->modify_contacts_sync	= e_book_backend_file_modify_contacts;
	sync_class->get_contact_sync		= e_book_backend_file_get_contact;
	sync_class->get_contact_list_sync	= e_book_backend_file_get_contact_list;
	sync_class->get_contact_list_uids_sync	= e_book_backend_file_get_contact_list_uids;

	object_class->dispose = e_book_backend_file_dispose;
	object_class->finalize = e_book_backend_file_finalize;
}

static void
e_book_backend_file_init (EBookBackendFile *backend)
{
	backend->priv = E_BOOK_BACKEND_FILE_GET_PRIVATE (backend);

	g_rw_lock_init (&(backend->priv->lock));

	g_signal_connect (
		backend, "notify::online",
		G_CALLBACK (e_book_backend_file_notify_online_cb), NULL);
}


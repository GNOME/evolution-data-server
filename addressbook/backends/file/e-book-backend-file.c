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
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include "db.h"
#include <sys/stat.h>
#include <sys/time.h>

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "e-book-backend-file.h"

#define E_BOOK_BACKEND_FILE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BOOK_BACKEND_FILE, EBookBackendFilePrivate))

#define d(x)

#define CHANGES_DB_SUFFIX ".changes.db"

#define E_BOOK_BACKEND_FILE_VERSION_NAME "PAS-DB-VERSION"
#define E_BOOK_BACKEND_FILE_VERSION "0.2"

#define E_BOOK_BACKEND_FILE_REVISION_NAME "PAS-DB-REVISION"

#define PAS_ID_PREFIX "pas-id-"

#define SQLITEDB_EMAIL_ID    "addressbook@localbackend.com"
#define SQLITEDB_FOLDER_ID   "folder_id"
#define SQLITEDB_FOLDER_NAME "folder"

#define EDB_ERROR(_code) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, NULL)
#define EDB_ERROR_EX(_code, _msg) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, _msg)
#define EDB_NOT_OPENED_ERROR EDB_ERROR(NOT_OPENED)

G_DEFINE_TYPE (EBookBackendFile, e_book_backend_file, E_TYPE_BOOK_BACKEND_SYNC)

struct _EBookBackendFilePrivate {
	gchar     *dirname;
	gchar     *filename;
	gchar     *photo_dirname;
	gchar     *revision;
	gint       rev_counter;
	DB        *file_db;
	DB_ENV    *env;

	EBookBackendSqliteDB *sqlitedb;
};

typedef enum {
	GET_PATH_DB_DIR,
	GET_PATH_PHOTO_DIR
} GetPathType;

typedef enum {
	STATUS_NORMAL = 0,
	STATUS_MODIFIED,
	STATUS_ERROR
} PhotoModifiedStatus;

G_LOCK_DEFINE_STATIC (db_environments);
static GHashTable *db_environments = NULL;
typedef struct {
	gint ref_count;
	DB_ENV *env;
} global_env;

static void
db_error_to_gerror (const gint db_error,
                    GError **perror)
{
	if (db_error && perror && *perror)
		g_clear_error (perror);

	switch (db_error) {
	case 0:
		return;
	case DB_NOTFOUND:
		g_propagate_error (perror, EDB_ERROR (CONTACT_NOT_FOUND));
		return;
	case EACCES:
		g_propagate_error (perror, EDB_ERROR (PERMISSION_DENIED));
		return;
	default:
		g_propagate_error (perror, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_OTHER_ERROR, "db error 0x%x (%s)", db_error, db_strerror (db_error) ? db_strerror (db_error) : _("Unknown error")));
		return;
	}
}

static void
string_to_dbt (const gchar *str,
               DBT *dbt)
{
	memset (dbt, 0, sizeof (*dbt));
	dbt->data = (gpointer) str;
	dbt->size = strlen (str) + 1;
	dbt->flags = DB_DBT_USERMEM;
}

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

static EContact *
create_contact (const gchar *uid,
                const gchar *vcard)
{
	return e_contact_new_from_vcard_with_uid (vcard, uid);
}

static gchar *
load_vcard (EBookBackendFile *bf,
            DB_TXN *txn,
            const gchar *uid,
            GError **error)
{
	DB     *db = bf->priv->file_db;
	DBT     id_dbt, vcard_dbt;
	gchar  *vcard;
	gint    db_error;

	/* Get the old contact from the db and compare the photo fields */
	string_to_dbt (uid, &id_dbt);
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, txn, &id_dbt, &vcard_dbt, 0);

	if (db_error == 0) {
		vcard = vcard_dbt.data;
	} else {
		g_warning (G_STRLOC ": db->get failed with %s", db_strerror (db_error));
		g_propagate_error (error, EDB_ERROR (CONTACT_NOT_FOUND));
		return NULL;
	}

	return vcard;
}

static EContact *
load_contact (EBookBackendFile *bf,
              DB_TXN *txn,
              const gchar *uid,
              GError **error)
{
	EContact *contact = NULL;
	gchar    *vcard;

	if ((vcard = load_vcard (bf, txn, uid, error)) != NULL) {
		contact = create_contact (uid, vcard);
		g_free (vcard);
	}

	return contact;
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
                                 gchar **vcard_ret,
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

	if (status != STATUS_ERROR) {
		if (modified) {
			if (vcard_ret)
				*vcard_ret = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
			status = STATUS_MODIFIED;
		}
	}

	return status;
}

static gboolean
build_sqlitedb (EBookBackendFilePrivate *bfpriv)
{
	DB             *db = bfpriv->file_db;
	DBC            *dbc;
	gint            db_error;
	DBT             id_dbt, vcard_dbt;
	GSList         *contacts = NULL;
	GError         *error = NULL;
	gboolean        skipped_version = FALSE;
	gboolean        skipped_revision = FALSE;

	if (!db) {
		g_warning (G_STRLOC ": Not opened yet");
		return FALSE;
	}

	db_error = db->cursor (db, NULL, &dbc, 0);

	if (db_error != 0) {
		g_warning (G_STRLOC ": db->cursor failed with %s", db_strerror (db_error));
		return FALSE;
	}

	memset (&vcard_dbt, 0, sizeof (vcard_dbt));
	memset (&id_dbt, 0, sizeof (id_dbt));
	db_error = dbc->c_get (dbc, &id_dbt, &vcard_dbt, DB_FIRST);

	while (db_error == 0) {
		gboolean skip = FALSE;

		/* don't include the version and revision in the list of cards */
		if (!skipped_version && !strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_VERSION_NAME)) {
			skipped_version = TRUE;
			skip = TRUE;
		} else if (!skipped_revision && !strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_REVISION_NAME)) {
			skipped_revision = TRUE;
			skip = TRUE;
		}

		if (!skip) {
			EContact *contact = create_contact (id_dbt.data, vcard_dbt.data);

			contacts = g_slist_prepend (contacts, contact);
		}

		db_error = dbc->c_get (dbc, &id_dbt, &vcard_dbt, DB_NEXT);

	}

	dbc->c_close (dbc);

	/* Detect error case */
	if (db_error != DB_NOTFOUND) {
		g_warning (G_STRLOC ": dbc->c_get failed with %s", db_strerror (db_error));
		e_util_free_object_slist (contacts);
		return FALSE;
	}

	if (contacts && !e_book_backend_sqlitedb_add_contacts (bfpriv->sqlitedb,
						   SQLITEDB_FOLDER_ID,
						   contacts, FALSE, &error)) {
		g_warning ("Failed to build contact summary: %s", error ? error->message : "Unknown error");
		g_clear_error (&error);
		e_util_free_object_slist (contacts);
		return FALSE;
	}

	e_util_free_object_slist (contacts);

	if (!e_book_backend_sqlitedb_set_is_populated (bfpriv->sqlitedb, SQLITEDB_FOLDER_ID, TRUE, &error)) {
		g_warning ("Failed to set the sqlitedb populated flag: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	return TRUE;
}

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
e_book_backend_file_bump_revision (EBookBackendFile *bf)
{
	DB   *db = bf->priv->file_db;
	DBT  revision_name_dbt, revision_dbt;
	gint  db_error;

	g_free (bf->priv->revision);
	bf->priv->revision = e_book_backend_file_new_revision (bf);

	string_to_dbt (E_BOOK_BACKEND_FILE_REVISION_NAME, &revision_name_dbt);
	string_to_dbt (bf->priv->revision,                &revision_dbt);
	db_error = db->put (db, NULL, &revision_name_dbt, &revision_dbt, 0);

	if (db_error != 0)
		g_warning (
			G_STRLOC ": db->put failed while bumping the revision string: %s",
			db_strerror (db_error));

	e_book_backend_notify_property_changed (E_BOOK_BACKEND (bf),
						BOOK_BACKEND_PROPERTY_REVISION,
						bf->priv->revision);
}

static void
e_book_backend_file_load_revision (EBookBackendFile *bf)
{
	DB   *db = bf->priv->file_db;
	DBT  version_name_dbt, version_dbt;
	gint  db_error;

	string_to_dbt (E_BOOK_BACKEND_FILE_REVISION_NAME, &version_name_dbt);
	memset (&version_dbt, 0, sizeof (version_dbt));
	version_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &version_name_dbt, &version_dbt, 0);
	if (db_error == 0) {
		/* success */
		bf->priv->revision = version_dbt.data;
	}
	else {
		/* key was not in file */
		bf->priv->revision = e_book_backend_file_new_revision (bf);
	}
}

static void
set_revision (EContact *contact)
{
	gchar time_string[100] = {0};
	const struct tm *tm = NULL;
	time_t t;

	t = time (NULL);
	tm = gmtime (&t);
	if (tm)
		strftime (time_string, 100, "%Y-%m-%dT%H:%M:%SZ", tm);
	e_contact_set (contact, E_CONTACT_REV, time_string);

}

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
	DB *db = bf->priv->file_db;
	DB_ENV *env = bf->priv->env;
	DB_TXN *txn = NULL;
	GSList *slist = NULL;
	const GSList *l;
	gint db_error = 0;
	PhotoModifiedStatus status = STATUS_NORMAL;

	g_assert (bf);
	g_assert (vcards_req);

	if (!db) {
		g_propagate_error (perror, EDB_NOT_OPENED_ERROR);
		return FALSE;
	}

	/* Begin transaction */
	db_error = env->txn_begin (env, NULL, &txn, 0);
	if (db_error != 0) {
		g_warning (G_STRLOC ": env->txn_begin failed with %s", db_strerror (db_error));
		db_error_to_gerror (db_error, perror);
		return FALSE;
	}

	for (l = vcards_req; l != NULL; l = l->next) {
		DBT              id_dbt, vcard_dbt;
		gchar           *id;
		gchar           *vcard;
		const gchar     *rev;
		const gchar     *vcard_req;
		EContact        *contact;

		vcard_req = (const gchar *) l->data;

		id      = e_book_backend_file_create_unique_id ();
		contact = e_contact_new_from_vcard_with_uid (vcard_req, id);

		rev = e_contact_get_const (contact, E_CONTACT_REV);
		if (!(rev && *rev))
			set_revision (contact);

		status = maybe_transform_vcard_for_photo (bf, NULL, contact, NULL, perror);

		if (status != STATUS_ERROR) {
			vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

			string_to_dbt (id,    &id_dbt);
			string_to_dbt (vcard, &vcard_dbt);

			db_error = db->put (db, txn, &id_dbt, &vcard_dbt, 0);

			g_free (vcard);
		}

		g_free (id);

		if (db_error == 0 && status != STATUS_ERROR) {
			/* Contact was added successfully, add it to the return list */
			if (contacts != NULL)
				slist = g_slist_prepend (slist, contact);
		} else if (db_error != 0) {
			/* Contact could not be added */
			g_warning (G_STRLOC ": db->put failed with %s", db_strerror (db_error));
			g_object_unref (contact);
			db_error_to_gerror (db_error, perror);

			/* Abort as soon as an error occurs */
			break;
		} else if (status == STATUS_ERROR) {
			/* Contact could not be added */
			g_warning (
				G_STRLOC ": db->put failed with %s",
				(perror && *perror) ? (*perror)->message :
				"Unknown error transforming vcard");
			g_object_unref (contact);

			/* Abort as soon as an error occurs */
			break;
		}
	}

	if (db_error == 0 && status != STATUS_ERROR) {
		/* Commit transaction */
		db_error = txn->commit (txn, 0);
		if (db_error == 0) {
			/* Flush cache information to disk */
			if (db->sync (db, 0) != 0) {
				g_warning ("db->sync failed with %s", db_strerror (db_error));
			}
		} else {
			g_warning (G_STRLOC ": txn->commit failed with %s", db_strerror (db_error));
			db_error_to_gerror (db_error, perror);
		}

	} else {
		/* Rollback transaction */
		txn->abort (txn);
	}

	if (db_error == 0 && status != STATUS_ERROR) {
		if (contacts != NULL)
			*contacts = g_slist_reverse (slist);

		return TRUE;
	} else {
		if (contacts != NULL)
			*contacts = NULL;

		e_util_free_object_slist (slist);
		return FALSE;
	}
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

	if (do_create (bf, vcards, added_contacts, perror)) {
		GError *error = NULL;

		if (!e_book_backend_sqlitedb_add_contacts (bf->priv->sqlitedb,
							  SQLITEDB_FOLDER_ID,
							  *added_contacts, FALSE, &error)) {
			g_warning ("Failed to add contacts to summary: %s", error->message);
			g_error_free (error);
		}

		e_book_backend_file_bump_revision (bf);
	}
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
	DB               *db = bf->priv->file_db;
	DB_ENV           *env = bf->priv->env;
	DB_TXN           *txn = NULL;
	gint              db_error;
	GSList           *removed_ids = NULL, *removed_contacts = NULL;
	const GSList     *l;

	if (!db) {
		g_propagate_error (perror, EDB_NOT_OPENED_ERROR);
		return;
	}

	/* Begin transaction */
	db_error = env->txn_begin (env, NULL, &txn, 0);
	if (db_error != 0) {
		g_warning (G_STRLOC ": env->txn_begin failed with %s", db_strerror (db_error));
		db_error_to_gerror (db_error, perror);
		return;
	}

	for (l = id_list; l != NULL; l = l->next) {
		const gchar *id;
		EContact *contact;
		DBT id_dbt;

		id = l->data;

		contact = load_contact (bf, txn, id, NULL);
		if (contact)
			removed_contacts = g_slist_prepend (removed_contacts, contact);

		/* Then go on to delete from the db */
		string_to_dbt (id, &id_dbt);

		db_error = db->del (db, txn, &id_dbt, 0);
		if (db_error != 0) {
			g_warning (G_STRLOC ": db->del failed with %s", db_strerror (db_error));
			db_error_to_gerror (db_error, perror);
			/* Abort as soon as a removal fails */
			break;
		}

		removed_ids = g_slist_prepend (removed_ids, g_strdup (id));
	}

	if (db_error == 0) {
		/* Commit transaction */
		db_error = txn->commit (txn, 0);
		if (db_error == 0) {
			/* Flush cache information to disk */
			if (db->sync (db, 0) != 0) {
				g_warning ("db->sync failed with %s", db_strerror (db_error));
			}
		} else {
			g_warning (G_STRLOC ": txn->commit failed with %s", db_strerror (db_error));
			db_error_to_gerror (db_error, perror);
		}
	} else {
		/* Rollback transaction */
		txn->abort (txn);
	}

	if (db_error == 0) {
		GError *error = NULL;

		/* Delete URI associated to those contacts */
		for (l = removed_contacts; l; l = l->next) {
			maybe_delete_unused_uris (bf, E_CONTACT (l->data), NULL);
		}

		/* Remove from summary as well */
		if (!e_book_backend_sqlitedb_remove_contacts (bf->priv->sqlitedb,
						      SQLITEDB_FOLDER_ID,
						      removed_ids, &error)) {
			g_warning ("Failed to remove contacts from the summary: %s", error->message);
			g_error_free (error);
		}

		*ids = removed_ids;
	} else {
		*ids = NULL;
		e_util_free_string_slist (removed_ids);
	}

	e_book_backend_file_bump_revision (bf);
	g_slist_free_full (removed_contacts, g_object_unref);
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
	DB               *db = bf->priv->file_db;
	DB_ENV           *env = bf->priv->env;
	DB_TXN           *txn = NULL;
	gint              db_error;
	const GSList     *lold, *l;
	GSList           *old_contacts = NULL, *modified_contacts = NULL;
	GSList           *ids = NULL;
	PhotoModifiedStatus status = STATUS_NORMAL;

	if (!db) {
		g_propagate_error (perror, EDB_NOT_OPENED_ERROR);
		return;
	}

	/* Begin transaction */
	db_error = env->txn_begin (env, NULL, &txn, 0);
	if (db_error != 0) {
		g_warning (G_STRLOC ": env->txn_begin failed with %s", db_strerror (db_error));
		db_error_to_gerror (db_error, perror);
		return;
	}

	for (l = vcards; l != NULL; l = l->next) {
		gchar *id, *lookup_id;
		gchar *vcard_with_rev;
		DBT id_dbt, vcard_dbt;
		EContact *contact, *old_contact;

		contact = e_contact_new_from_vcard (l->data);
		id = e_contact_get (contact, E_CONTACT_UID);

		if (id == NULL) {
			g_propagate_error (perror, EDB_ERROR_EX (OTHER_ERROR, _("No UID in the contact")));
			g_object_unref (contact);
			break;
		}

		old_contact = load_contact (bf, txn, id, perror);
		if (!old_contact) {
			g_warning (G_STRLOC ": Failed to load contact %s", id);
			status = STATUS_ERROR;

			g_free (id);
			g_object_unref (contact);
			break;
		}
		old_contacts = g_slist_prepend (old_contacts, old_contact);

		/* Transform incomming photo blobs to uris before storing this to the DB */
		status = maybe_transform_vcard_for_photo (bf, old_contact, contact, NULL, perror);
		if (status == STATUS_ERROR) {
			g_warning (
				G_STRLOC ": Error transforming contact %s: %s",
				id, (perror && *perror) ? (*perror)->message : "Unknown Error");

			g_free (id);
			g_object_unref (old_contact);
			g_object_unref (contact);
			break;
		}

		/* update the revision (modified time of contact) */
		set_revision (contact);
		vcard_with_rev = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

		/* This is disgusting, but for a time cards were added with
		 * ID's that are no longer used (they contained both the uri
		 * and the id.) If we recognize it as a uri (file:///...) trim
		 * off everything before the last '/', and use that as the
		 * id.*/
		if (!strncmp (id, "file:///", strlen ("file:///"))) {
			lookup_id = strrchr (id, '/') + 1;
		}
		else
			lookup_id = id;

		string_to_dbt (lookup_id,      &id_dbt);
		string_to_dbt (vcard_with_rev, &vcard_dbt);

		db_error = db->put (db, txn, &id_dbt, &vcard_dbt, 0);
		g_free (vcard_with_rev);

		if (db_error != 0) {
			g_warning (G_STRLOC ": db->put failed with %s", db_strerror (db_error));
			db_error_to_gerror (db_error, perror);

			/* Abort as soon as a modification fails */
			g_free (id);
			g_object_unref (contact);
			break;
		}

		modified_contacts = g_slist_prepend (modified_contacts, contact);
		ids = g_slist_prepend (ids, id);
	}

	if (db_error == 0 && status != STATUS_ERROR) {
		/* Commit transaction */
		db_error = txn->commit (txn, 0);
		if (db_error == 0) {
			/* Flush cache information to disk */
			if (db->sync (db, 0) != 0) {
				g_warning ("db->sync failed with %s", db_strerror (db_error));
			}
		} else {
			g_warning (G_STRLOC ": txn->commit failed with %s", db_strerror (db_error));
			db_error_to_gerror (db_error, perror);
		}
	} else {
		/* Rollback transaction */
		txn->abort (txn);
	}

	if (db_error == 0 && status != STATUS_ERROR) {
		GError *error = NULL;

		/* Delete old photo file uris if need be (this will compare the new contact
		 * with the current copy in the BDB to extract the uris to delete) */
		lold = old_contacts;
		l = modified_contacts;
		while (lold && l) {
			maybe_delete_unused_uris (bf, E_CONTACT (lold->data), E_CONTACT (l->data));
			lold = lold->next;
			l = l->next;
		}

		/* Update summary as well */
		if (!e_book_backend_sqlitedb_remove_contacts (bf->priv->sqlitedb,
							      SQLITEDB_FOLDER_ID,
							      ids, &error)) {
			g_warning ("Failed to remove contacts from the summary: %s", error->message);
			g_error_free (error);
		} else if (!e_book_backend_sqlitedb_add_contacts (bf->priv->sqlitedb,
								  SQLITEDB_FOLDER_ID,
								  modified_contacts, FALSE, &error)) {
			g_warning ("Failed to add contacts to summary: %s", error->message);
			g_error_free (error);
		}

		*contacts = g_slist_reverse (modified_contacts);
	} else {
		*contacts = NULL;
		e_util_free_object_slist (modified_contacts);
	}

	e_util_free_string_slist (ids);
	g_slist_free_full (old_contacts, g_object_unref);

	e_book_backend_file_bump_revision (bf);
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

	if (!bf || !bf->priv || !bf->priv->file_db) {
		g_propagate_error (perror, EDB_NOT_OPENED_ERROR);
		return;
	}

	*vcard = load_vcard (bf, NULL, id, perror);
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
	DB             *db = bf->priv->file_db;
	DBC            *dbc;
	gint            db_error;
	DBT  id_dbt, vcard_dbt;
	EBookBackendSExp *card_sexp = NULL;
	gboolean search_needed;
	const gchar *search = query;
	GSList *contact_list = NULL, *l;
	GSList *summary_list = NULL;
	gboolean searched_summary = FALSE;
	gboolean with_all_required_fields = FALSE;

	d (printf ("e_book_backend_file_get_contact_list (%s)\n", search));

	if (!db) {
		g_propagate_error (perror, EDB_NOT_OPENED_ERROR);
		return;
	}

	summary_list = e_book_backend_sqlitedb_search (
		bf->priv->sqlitedb,
		SQLITEDB_FOLDER_ID,
		search, NULL,
		&searched_summary,
		&with_all_required_fields, NULL);

	if (summary_list) {

		for (l = summary_list; l; l = l->next) {
			EbSdbSearchData *data = l->data;

			if (with_all_required_fields) {
				contact_list = g_slist_prepend (contact_list, data->vcard);
				data->vcard  = NULL;
			} else {
				/* In this case the sqlitedb helped us with the query, but
				 * the return information is incomplete so we need to load it up.
				 */
				gchar *vcard;

				vcard = load_vcard (bf, NULL, data->uid, perror);

				/* Break out on the first BDB error */
				if (!vcard)
					break;

				contact_list = g_slist_prepend (contact_list, vcard);
			}
		}

		g_slist_foreach (summary_list, (GFunc) e_book_backend_sqlitedb_search_data_free, NULL);
		g_slist_free (summary_list);

	} else {
		search_needed = TRUE;
		if (!strcmp (search, "(contains \"x-evolution-any-field\" \"\")"))
			search_needed = FALSE;

		card_sexp = e_book_backend_sexp_new (search);
		if (!card_sexp) {
			g_propagate_error (perror, EDB_ERROR (INVALID_QUERY));
			return;
		}

		db_error = db->cursor (db, NULL, &dbc, 0);

		if (db_error != 0) {
			g_warning (G_STRLOC ": db->cursor failed with %s", db_strerror (db_error));
			/* XXX this needs to be some CouldNotOpen error */
			db_error_to_gerror (db_error, perror);
			return;
		}

		memset (&vcard_dbt, 0, sizeof (vcard_dbt));
		vcard_dbt.flags = DB_DBT_MALLOC;
		memset (&id_dbt, 0, sizeof (id_dbt));
		db_error = dbc->c_get (dbc, &id_dbt, &vcard_dbt, DB_FIRST);

		while (db_error == 0) {

			/* don't include the version or revision in the list of cards */
			if ((id_dbt.size != strlen (E_BOOK_BACKEND_FILE_VERSION_NAME) + 1
			     || strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_VERSION_NAME)) &&
			    (id_dbt.size != strlen (E_BOOK_BACKEND_FILE_REVISION_NAME) + 1
			     || strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_REVISION_NAME))) {

				if ((!search_needed) || (card_sexp != NULL && e_book_backend_sexp_match_vcard  (card_sexp, vcard_dbt.data))) {
					contact_list = g_slist_prepend (contact_list, vcard_dbt.data);
				} else {
					free (vcard_dbt.data);
				}
			} else {
				free (vcard_dbt.data);
			}

			db_error = dbc->c_get (dbc, &id_dbt, &vcard_dbt, DB_NEXT);

		}
		g_object_unref (card_sexp);

		if (db_error == DB_NOTFOUND) {
			/* Success */
		} else {
			g_warning (G_STRLOC ": dbc->c_get failed with %s", db_strerror (db_error));
			db_error_to_gerror (db_error, perror);
		}

		db_error = dbc->c_close (dbc);
		if (db_error != 0) {
			g_warning (G_STRLOC ": dbc->c_close failed with %s", db_strerror (db_error));
		}
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
	DB             *db = bf->priv->file_db;
	DBC            *dbc;
	gint            db_error;
	DBT  id_dbt, vcard_dbt;
	EBookBackendSExp *card_sexp = NULL;
	gboolean search_needed;
	const gchar *search = query;
	GSList *uids = NULL;
	gboolean searched = FALSE;

	d (printf ("e_book_backend_file_get_contact_list (%s)\n", search));

	if (!db) {
		g_propagate_error (perror, EDB_NOT_OPENED_ERROR);
		return;
	}

	uids = e_book_backend_sqlitedb_search_uids (
		bf->priv->sqlitedb,
		SQLITEDB_FOLDER_ID,
		search, &searched, NULL);

	if (!searched) {
		search_needed = TRUE;
		if (!strcmp (search, "(contains \"x-evolution-any-field\" \"\")"))
			search_needed = FALSE;

		card_sexp = e_book_backend_sexp_new (search);
		if (!card_sexp) {
			g_propagate_error (perror, EDB_ERROR (INVALID_QUERY));
			return;
		}

		db_error = db->cursor (db, NULL, &dbc, 0);

		if (db_error != 0) {
			g_warning (G_STRLOC ": db->cursor failed with %s", db_strerror (db_error));
			/* XXX this needs to be some CouldNotOpen error */
			db_error_to_gerror (db_error, perror);
			return;
		}

		memset (&vcard_dbt, 0, sizeof (vcard_dbt));
		vcard_dbt.flags = DB_DBT_MALLOC;
		memset (&id_dbt, 0, sizeof (id_dbt));
		db_error = dbc->c_get (dbc, &id_dbt, &vcard_dbt, DB_FIRST);

		while (db_error == 0) {

			/* don't include the version or revision in the list of cards */
			if ((id_dbt.size != strlen (E_BOOK_BACKEND_FILE_VERSION_NAME) + 1
			     || strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_VERSION_NAME)) &&
			    (id_dbt.size != strlen (E_BOOK_BACKEND_FILE_REVISION_NAME) + 1
			     || strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_REVISION_NAME))) {

				if ((!search_needed) || (card_sexp != NULL && e_book_backend_sexp_match_vcard  (card_sexp, vcard_dbt.data))) {
					uids = g_slist_prepend (uids, g_strdup (id_dbt.data));
				}
			}

			g_free (vcard_dbt.data);

			db_error = dbc->c_get (dbc, &id_dbt, &vcard_dbt, DB_NEXT);

		}
		g_object_unref (card_sexp);

		if (db_error == DB_NOTFOUND) {
			/* Success */
		} else {
			g_warning (G_STRLOC ": dbc->c_get failed with %s", db_strerror (db_error));
			db_error_to_gerror (db_error, perror);
		}

		db_error = dbc->c_close (dbc);
		if (db_error != 0) {
			g_warning (G_STRLOC ": dbc->c_close failed with %s", db_strerror (db_error));
		}
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
	DB  *db;
	DBT id_dbt, vcard_dbt;
	gint db_error;
	gboolean allcontacts;
	GSList *summary_list, *l;
	GHashTable *fields_of_interest;
	gboolean searched = FALSE;
	gboolean with_all_required_fields = FALSE;

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

	db                 = bf->priv->file_db;
	query              = e_data_book_view_get_card_query (book_view);
	fields_of_interest = e_data_book_view_get_fields_of_interest (book_view);

	if (!db) {
		e_data_book_view_notify_complete (book_view, EDB_NOT_OPENED_ERROR);
		e_data_book_view_unref (book_view);
		return NULL;
	}

	if ( !strcmp (query, "(contains \"x-evolution-any-field\" \"\")")) {
		e_data_book_view_notify_progress (book_view, -1, _("Loading..."));
		allcontacts = TRUE;
	} else {
		e_data_book_view_notify_progress (book_view, -1, _("Searching..."));
		allcontacts = FALSE;
	}

	d (printf ("signalling parent thread\n"));
	e_flag_set (closure->running);

	summary_list = e_book_backend_sqlitedb_search (
		bf->priv->sqlitedb,
		SQLITEDB_FOLDER_ID,
		query, fields_of_interest,
		&searched, &with_all_required_fields, NULL);

	if (searched) {

		for (l = summary_list; l; l = l->next) {
			EbSdbSearchData *data = l->data;
			gchar *vcard = NULL;

			if (with_all_required_fields) {
				vcard = data->vcard;
				data->vcard = NULL;
			} else {
				GError *error = NULL;

				/* The sqlitedb summary did not satisfy 'fields-of-interest',
				 * load the complete vcard here. */
				vcard = load_vcard (bf, NULL, data->uid, &error);

				if (error) {
					g_warning (
						"Error loading contact %s: %s",
						data->uid, error->message);
					g_error_free (error);
				}

				if (!vcard)
					continue;

			}

			notify_update_vcard (book_view, TRUE, data->uid, vcard);
			g_free (vcard);
		}

		g_slist_foreach (summary_list, (GFunc) e_book_backend_sqlitedb_search_data_free, NULL);
		g_slist_free (summary_list);
	} else {
		/* iterate over the db and do the query there */
		DBC    *dbc;

		memset (&id_dbt, 0, sizeof (id_dbt));
		memset (&vcard_dbt, 0, sizeof (vcard_dbt));
		vcard_dbt.flags = DB_DBT_MALLOC;

		db_error = db->cursor (db, NULL, &dbc, 0);
		if (db_error == 0) {

			db_error = dbc->c_get (dbc, &id_dbt, &vcard_dbt, DB_FIRST);
			while (db_error == 0) {

				if (!e_flag_is_set (closure->running))
					break;

				/* don't include the version in the list of cards */
				if (strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_VERSION_NAME) &&
				    strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_REVISION_NAME)) {
					notify_update_vcard (book_view, allcontacts,
							     id_dbt.data, vcard_dbt.data);
				}

				g_free (vcard_dbt.data);
				db_error = dbc->c_get (dbc, &id_dbt, &vcard_dbt, DB_NEXT);
			}

			dbc->c_close (dbc);
			if (db_error && db_error != DB_NOTFOUND)
				g_warning (
					"e_book_backend_file_search: error building list: %s",
					db_strerror (db_error));
		}
		else if (db_error == DB_RUNRECOVERY) {
			g_warning (
				"e_book_backend_file_search: error getting the cursor for %s",
				bf->priv->filename);
			abort ();
		}

	}

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

/*
** versions:
**
** 0.0 just a list of cards
**
** 0.1 same as 0.0, but with the version tag
**
** 0.2 not a real format upgrade, just a hack to fix broken ids caused
**     by a bug in early betas, but we only need to convert them if
**     the previous version is 0.1, since the bug existed after 0.1
**     came about.
*/
static gboolean
e_book_backend_file_upgrade_db (EBookBackendFile *bf,
                                gchar *old_version)
{
	DB  *db = bf->priv->file_db;
	gint db_error;
	DBT version_name_dbt, version_dbt;

	if (!db) {
		g_warning (G_STRLOC ": No DB opened");
		return FALSE;
	}

	if (strcmp (old_version, "0.0")
	    && strcmp (old_version, "0.1")) {
		g_warning (
			"unsupported version '%s' found in PAS backend file\n",
			old_version);
		return FALSE;
	}

	if (!strcmp (old_version, "0.1")) {
		/* we just loop through all the cards in the db,
		 * giving them valid ids if they don't have them */
		DBT  id_dbt, vcard_dbt;
		DBC *dbc;
		gint  card_failed = 0;

		db_error = db->cursor (db, NULL, &dbc, 0);
		if (db_error != 0) {
			g_warning (G_STRLOC ": db->cursor failed with %s", db_strerror (db_error));
			return FALSE;
		}

		memset (&id_dbt, 0, sizeof (id_dbt));
		memset (&vcard_dbt, 0, sizeof (vcard_dbt));

		db_error = dbc->c_get (dbc, &id_dbt, &vcard_dbt, DB_FIRST);

		while (db_error == 0) {
			if ((id_dbt.size != strlen (E_BOOK_BACKEND_FILE_VERSION_NAME) + 1
			     || strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_VERSION_NAME)) &&
			    (id_dbt.size != strlen (E_BOOK_BACKEND_FILE_REVISION_NAME) + 1
			     || strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_REVISION_NAME))) {
				EContact *contact;

				contact = create_contact (id_dbt.data, vcard_dbt.data);

				/* the cards we're looking for are
				 * created with a normal id dbt, but
				 * with the id field in the vcard set
				 * to something that doesn't match.
				 * so, we need to modify the card to
				 * have the same id as the the dbt. */
				if (strcmp (id_dbt.data, e_contact_get_const (contact, E_CONTACT_UID))) {
					gchar *vcard;

					e_contact_set (contact, E_CONTACT_UID, id_dbt.data);

					vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
					string_to_dbt (vcard, &vcard_dbt);

					db_error = db->put (db, NULL,
							    &id_dbt, &vcard_dbt, 0);

					g_free (vcard);

					if (db_error != 0)
						card_failed++;
				}

				g_object_unref (contact);
			}

			db_error = dbc->c_get (dbc, &id_dbt, &vcard_dbt, DB_NEXT);
		}

		dbc->c_close (dbc);

		if (card_failed) {
			g_warning ("failed to update %d cards", card_failed);
			return FALSE;
		}
	}

	string_to_dbt (E_BOOK_BACKEND_FILE_VERSION_NAME, &version_name_dbt);
	string_to_dbt (E_BOOK_BACKEND_FILE_VERSION, &version_dbt);

	db_error = db->put (db, NULL, &version_name_dbt, &version_dbt, 0);
	if (db_error == 0)
		return TRUE;
	else
		return FALSE;
}

static gboolean
e_book_backend_file_maybe_upgrade_db (EBookBackendFile *bf)
{
	DB   *db = bf->priv->file_db;
	DBT  version_name_dbt, version_dbt;
	gint  db_error;
	gchar *version;
	gboolean ret_val = TRUE;

	if (!db) {
		g_warning (G_STRLOC ": No DB opened");
		return FALSE;
	}

	string_to_dbt (E_BOOK_BACKEND_FILE_VERSION_NAME, &version_name_dbt);
	memset (&version_dbt, 0, sizeof (version_dbt));
	version_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &version_name_dbt, &version_dbt, 0);
	if (db_error == 0) {
		/* success */
		version = version_dbt.data;
	}
	else {
		/* key was not in file */
		version = g_strdup ("0.0");
	}

	if (strcmp (version, E_BOOK_BACKEND_FILE_VERSION))
		ret_val = e_book_backend_file_upgrade_db (bf, version);

	g_free (version);

	return ret_val;
}

#ifdef CREATE_DEFAULT_VCARD
# include <libedata-book/ximian-vcard.h>
#endif

static void
#if (DB_VERSION_MAJOR > 4) || (DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR >= 3)
file_errcall (const DB_ENV *env,
              const gchar *buf1,
              const gchar *buf2)
#else
file_errcall (const gchar *buf1,
              gchar *buf2)
#endif
{
	g_warning ("libdb error: %s", buf2);
}

static void
e_book_backend_file_open (EBookBackendSync *backend,
                          EDataBook *book,
                          GCancellable *cancellable,
                          gboolean only_if_exists,
                          GError **perror)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	gchar            *dirname, *filename;
	gboolean          readonly = TRUE;
	ESourceRegistry  *registry;
	ESource          *source;
	gint              db_error;
	DB               *db;
	DB_ENV           *env;
	GError           *local_error = NULL;
	global_env       *genv = NULL;

#ifdef CREATE_DEFAULT_VCARD
	gboolean create_default_vcard = FALSE;
#endif

	source = e_backend_get_source (E_BACKEND (backend));
	registry = e_book_backend_get_registry (E_BOOK_BACKEND (backend));
	dirname = e_book_backend_file_extract_path_from_source (
		registry, source, GET_PATH_DB_DIR);

	if (only_if_exists && !g_file_test (dirname, G_FILE_TEST_IS_DIR)) {
		g_free (dirname);
		g_propagate_error (perror, EDB_ERROR (NO_SUCH_BOOK));
		return;
	}

	filename = g_build_filename (dirname, "addressbook.db", NULL);

	db_error = e_db3_utils_maybe_recover (filename);
	if (db_error != 0) {
		g_warning ("db recovery failed with %s", db_strerror (db_error));
		g_free (dirname);
		g_free (filename);
		db_error_to_gerror (db_error, perror);
		return;
	}

	G_LOCK (db_environments);
	if (db_environments) {
		genv = g_hash_table_lookup (db_environments, dirname);
	}
	if (genv && genv->ref_count > 0) {
		genv->ref_count++;
		env = genv->env;
	} else {
		db_error = db_env_create (&env, 0);
		if (db_error != 0) {
			g_warning ("db_env_create failed with %s", db_strerror (db_error));
			G_UNLOCK (db_environments);
			g_free (dirname);
			g_free (filename);
			db_error_to_gerror (db_error, perror);
			return;
		}

		env->set_errcall (env, file_errcall);

		/* Set the allocation routines to the non-aborting GLib functions */
		env->set_alloc (env, (gpointer (*)(gsize)) g_try_malloc,
				(gpointer (*)(gpointer , gsize)) g_try_realloc,
				g_free);

		/* Make sure the database directory is created
		 * or env->open will fail */
		if (!only_if_exists) {
			if (!create_directory (dirname, perror)) {
				g_warning ("failed to create directory at %s", dirname);
				G_UNLOCK (db_environments);
				g_free (dirname);
				g_free (filename);
				return;
			}
		}

		/*
		 * DB_INIT_TXN enables transaction support. It requires DB_INIT_LOCK to
		 * initialize the locking subsystem and DB_INIT_LOG for the logging
		 * subsystem.
		 *
		 * DB_INIT_MPOOL enables the in-memory cache.
		 *
		 * Note that we need either DB_INIT_CDB or DB_INIT_LOCK, because we will
		 * have multiple threads reading and writing concurrently without
		 * any locking above libdb. Right now DB_INIT_LOCK is used because
		 * DB_INIT_TXN conflicts with DB_INIT_CDB.
		 */
		db_error = (*env->open) (env, dirname, DB_INIT_LOCK | DB_INIT_TXN | DB_INIT_LOG | DB_CREATE | DB_INIT_MPOOL | DB_PRIVATE | DB_THREAD, 0);
		if (db_error != 0) {
			env->close (env, 0);
			g_warning ("db_env_open failed with %s", db_strerror (db_error));
			G_UNLOCK (db_environments);
			g_free (dirname);
			g_free (filename);
			db_error_to_gerror (db_error, perror);
			return;
		}

		/* Insert in the db_environments hash table */
		if (!db_environments) {
			db_environments = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		}
		genv = g_malloc0 (sizeof (global_env));
		genv->ref_count = 1;
		genv->env = env;
		g_hash_table_insert (db_environments, g_strdup (dirname), genv);
	}
	G_UNLOCK (db_environments);

	bf->priv->env = env;

	db_error = db_create (&db, env, 0);
	if (db_error != 0) {
		g_warning ("db_create failed with %s", db_strerror (db_error));
		g_free (dirname);
		g_free (filename);
		db_error_to_gerror (db_error, perror);
		return;
	}

	db_error = (*db->open) (db, NULL, filename, NULL, DB_HASH, DB_THREAD | DB_AUTO_COMMIT, 0666);

	if (db_error == DB_OLD_VERSION) {
		db_error = e_db3_utils_upgrade_format (filename);

		if (db_error != 0) {
			g_warning ("db format upgrade failed with %s", db_strerror (db_error));
			g_free (dirname);
			g_free (filename);
			db_error_to_gerror (db_error, perror);
			return;
		}

		db->close (db, 0);
		db_error = db_create (&db, env, 0);
		if (db_error != 0) {
			g_warning ("db_create failed with %s", db_strerror (db_error));
			g_free (dirname);
			g_free (filename);
			db_error_to_gerror (db_error, perror);
			return;
		}

		db_error = (*db->open) (db, NULL, filename, NULL, DB_HASH, DB_THREAD | DB_AUTO_COMMIT, 0666);
	}

	if (db_error == 0) {
		readonly = FALSE;
	} else {
		db->close (db, 0);
		db_error = db_create (&db, env, 0);
		if (db_error != 0) {
			g_warning ("db_create failed with %s", db_strerror (db_error));
			g_free (dirname);
			g_free (filename);
			db_error_to_gerror (db_error, perror);
			return;
		}

		db_error = (*db->open) (db, NULL, filename, NULL, DB_HASH, DB_RDONLY | DB_THREAD | DB_AUTO_COMMIT, 0666);

		if (db_error != 0 && !only_if_exists) {

			/* the database didn't exist, so we create the
			 * directory then the .db */
			db->close (db, 0);

			if (!create_directory (dirname, perror)) {
				g_free (dirname);
				g_free (filename);
				return;
			}

			db_error = db_create (&db, env, 0);
			if (db_error != 0) {
				g_warning ("db_create failed with %s", db_strerror (db_error));
				g_free (dirname);
				g_free (filename);
				db_error_to_gerror (db_error, perror);
				return;
			}

			db_error = (*db->open) (db, NULL, filename, NULL, DB_HASH, DB_CREATE | DB_THREAD | DB_AUTO_COMMIT, 0666);
			if (db_error != 0) {
				db->close (db, 0);
				g_warning ("db->open (... %s ... DB_CREATE ...) failed with %s", filename, db_strerror (db_error));
			}
			else {
#ifdef CREATE_DEFAULT_VCARD
				create_default_vcard = TRUE;
#endif

				readonly = FALSE;
			}
		}
	}

	bf->priv->file_db = db;

	if (db_error != 0) {
		bf->priv->file_db = NULL;
		g_free (dirname);
		g_free (filename);
		db_error_to_gerror (db_error, perror);
		return;
	}

#ifdef CREATE_DEFAULT_VCARD
	if (create_default_vcard) {
		GSList l;
		l.data = XIMIAN_VCARD;
		l.next = NULL;

		if (!do_create (bf, &l, NULL, NULL))
			g_warning ("Cannot create default contact");
	}
#endif

	if (!e_book_backend_file_maybe_upgrade_db (bf)) {
		db->close (db, 0);
		bf->priv->file_db = NULL;
		g_free (dirname);
		g_free (filename);
		g_propagate_error (perror, EDB_ERROR_EX (OTHER_ERROR, "e_book_backend_file_maybe_upgrade_db failed"));
		return;
	}

	g_free (bf->priv->dirname);
	g_free (bf->priv->filename);
	bf->priv->dirname = dirname;
	bf->priv->filename = filename;

	bf->priv->sqlitedb = e_book_backend_sqlitedb_new (
		bf->priv->dirname,
		SQLITEDB_EMAIL_ID,
		SQLITEDB_FOLDER_ID,
		SQLITEDB_FOLDER_NAME,
		FALSE,
		perror);
	if (!bf->priv->sqlitedb)
		return;

	if (!e_book_backend_sqlitedb_get_is_populated (bf->priv->sqlitedb,
						       SQLITEDB_FOLDER_ID,
						       &local_error)) {
		if (local_error) {
			g_propagate_error (perror, local_error);
			return;
		} else if (!build_sqlitedb (bf->priv)) {
			g_propagate_error (
				perror, e_data_book_create_error_fmt (
				E_DATA_BOOK_STATUS_OTHER_ERROR,
				_("Failed to build summary for an address book %s"),
				bf->priv->filename));
		}
	}

	/* Resolve the photo directory here */
	dirname = e_book_backend_file_extract_path_from_source (
		registry, source, GET_PATH_PHOTO_DIR);
	if (!only_if_exists && !create_directory (dirname, perror))
		return;
	bf->priv->photo_dirname = dirname;

	e_book_backend_file_load_revision (bf);

	e_book_backend_notify_online (E_BOOK_BACKEND (backend), TRUE);
	e_book_backend_notify_readonly (E_BOOK_BACKEND (backend), readonly);
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
	gint db_error;

	g_return_if_fail (bf != NULL);

	if (bf->priv->file_db) {
		db_error = bf->priv->file_db->sync (bf->priv->file_db, 0);
		if (db_error != 0)
			g_warning (G_STRLOC ": db->sync failed with %s", db_strerror (db_error));
	}
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
	global_env *genv;

	bf = E_BOOK_BACKEND_FILE (object);

	if (bf->priv->file_db) {
		bf->priv->file_db->close (bf->priv->file_db, 0);
		bf->priv->file_db = NULL;
	}

	G_LOCK (db_environments);
	if (bf->priv->dirname) {
		genv = g_hash_table_lookup (db_environments, bf->priv->dirname);
		if (genv) {
			genv->ref_count--;
			if (genv->ref_count == 0) {
				genv->env->close (genv->env, 0);
				g_free (genv);
				g_hash_table_remove (db_environments, bf->priv->dirname);
			}
			if (g_hash_table_size (db_environments) == 0) {
				g_hash_table_destroy (db_environments);
				db_environments = NULL;
			}
		}
	}
	G_UNLOCK (db_environments);

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

	g_free (priv->filename);
	g_free (priv->dirname);
	g_free (priv->photo_dirname);
	g_free (priv->revision);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_backend_file_parent_class)->finalize (object);
}

#ifdef G_OS_WIN32
/* Avoid compiler warning by providing a function with exactly the
 * prototype that db_env_set_func_open() wants for the open method.
 */

static gint
my_open (const gchar *name,
         gint oflag,
         ...)
{
	gint mode = 0;

	if (oflag & O_CREAT) {
		va_list arg;
		va_start (arg, oflag);
		mode = va_arg (arg, gint);
		va_end (arg);
	}

	return g_open (name, oflag, mode);
}

gint
my_rename (const gchar *oldname,
           const gchar *newname)
{
	return g_rename (oldname, newname);
}

gint
my_exists (const gchar *name,
           gint *isdirp)
{
	if (!g_file_test (name, G_FILE_TEST_EXISTS))
		return ENOENT;
	if (isdirp != NULL)
		*isdirp = g_file_test (name, G_FILE_TEST_IS_DIR);
	return 0;
}

gint
my_unlink (const gchar *name)
{
	return g_unlink (name);
}

#endif

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

#ifdef G_OS_WIN32
	/* Use the gstdio wrappers to open, check, rename and unlink
	 * files from libdb.
	 */
	db_env_set_func_open (my_open);
	db_env_set_func_close (close);
	db_env_set_func_exists (my_exists);
	db_env_set_func_rename (my_rename);
	db_env_set_func_unlink (my_unlink);
#endif
}

static void
e_book_backend_file_init (EBookBackendFile *backend)
{
	backend->priv = E_BOOK_BACKEND_FILE_GET_PRIVATE (backend);

	g_signal_connect (
		backend, "notify::online",
		G_CALLBACK (e_book_backend_file_notify_online_cb), NULL);
}


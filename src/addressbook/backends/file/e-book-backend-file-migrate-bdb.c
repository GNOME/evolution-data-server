/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-file-migrate-bdb.c - Migration of old BDB database to the new sqlite DB.
 *
 * Copyright (C) 2012 Intel Corporation
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
 *
 * Based on work by Nat Friedman, Chris Toshok and Hans Petter Jansson.
 */

#include "evolution-data-server-config.h"

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

#include <libedata-book/libedata-book.h>
#include <libebackend/e-db3-utils.h>

#include "e-book-backend-file-migrate-bdb.h"

#define E_BOOK_BACKEND_FILE_REVISION_NAME         "PAS-DB-REVISION"
#define E_BOOK_BACKEND_FILE_VERSION_NAME          "PAS-DB-VERSION"
#define E_BOOK_BACKEND_FILE_LAST_BDB_VERSION      "0.2"

#define EC_ERROR(_code)          e_client_error_create (_code, NULL)
#define EC_ERROR_EX(_code, _msg) e_client_error_create (_code, _msg)
#define EBC_ERROR(_code)         e_book_client_error_create (_code, NULL)

G_LOCK_DEFINE_STATIC (db_env);
static DB_ENV *db_env = NULL;

#ifdef G_OS_WIN32
static gboolean db_env_initialized = FALSE;
#endif

/**************************************************************
 *             Low level BDB interfacing tools                *
 **************************************************************/

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
db_error_to_gerror (const gint db_error,
                    GError **perror)
{
	if (db_error && perror && *perror)
		g_clear_error (perror);

	switch (db_error) {
	case 0:
		return;
	case DB_NOTFOUND:
		g_propagate_error (perror, EBC_ERROR (E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND));
		return;
	case EACCES:
		g_propagate_error (perror, EC_ERROR (E_CLIENT_ERROR_PERMISSION_DENIED));
		return;
	default:
		g_propagate_error (
			perror,
			e_client_error_create_fmt (
				E_CLIENT_ERROR_OTHER_ERROR,
				"db error 0x%x (%s)", db_error,
				db_strerror (db_error) ?
					db_strerror (db_error) :
					_("Unknown error")));
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

/**************************************************************
 *       Include the previous migration routines first        *
 **************************************************************/
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
e_book_backend_file_upgrade_db (DB *db,
                                gchar *old_version)
{
	gint db_error;
	DBT  version_name_dbt, version_dbt;

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

				contact = e_contact_new_from_vcard_with_uid (vcard_dbt.data, id_dbt.data);

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
	string_to_dbt (E_BOOK_BACKEND_FILE_LAST_BDB_VERSION, &version_dbt);

	db_error = db->put (db, NULL, &version_name_dbt, &version_dbt, 0);
	if (db_error == 0)
		return TRUE;
	else
		return FALSE;
}

static gboolean
e_book_backend_file_maybe_upgrade_db (DB *db)
{
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

	if (strcmp (version, E_BOOK_BACKEND_FILE_LAST_BDB_VERSION))
		ret_val = e_book_backend_file_upgrade_db (db, version);

	g_free (version);

	return ret_val;
}

static gboolean
migrate_bdb_to_sqlite (EBookSqlite *sqlitedb,
                       DB *db,
                       GCancellable *cancellable,
                       GError **error)
{
	DBC            *dbc;
	DBT             id_dbt, vcard_dbt;
	gint            db_error;
	gboolean        skipped_version = FALSE;
	gboolean        skipped_revision = FALSE;
	GSList         *contacts = NULL;

	db_error = db->cursor (db, NULL, &dbc, 0);

	if (db_error != 0) {
		g_warning (G_STRLOC ": db->cursor failed with %s", db_strerror (db_error));
		db_error_to_gerror (db_error, error);
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
			EContact *contact = e_contact_new_from_vcard_with_uid (vcard_dbt.data, id_dbt.data);

			contacts = g_slist_prepend (contacts, contact);
		}

		db_error = dbc->c_get (dbc, &id_dbt, &vcard_dbt, DB_NEXT);
	}

	dbc->c_close (dbc);

	/* Detect error case */
	if (db_error != DB_NOTFOUND) {
		g_warning (G_STRLOC ": dbc->c_get failed with %s", db_strerror (db_error));
		g_slist_free_full (contacts, (GDestroyNotify) g_object_unref);
		db_error_to_gerror (db_error, error);
		return FALSE;
	}

	/* Add the contacts to the SQLite (only if there are any contacts to add) */
	if (contacts &&
	    !e_book_sqlite_add_contacts (sqlitedb, contacts, NULL, TRUE, cancellable, error)) {
		if (error && *error) {
			g_warning ("Failed to add contacts to sqlite db: %s", (*error)->message);
		} else {
			g_warning ("Failed to add contacts to sqlite db: unknown error");
		}

		g_slist_free_full (contacts, (GDestroyNotify) g_object_unref);
		return FALSE;
	}

	g_slist_free_full (contacts, (GDestroyNotify) g_object_unref);

	if (!e_book_sqlite_set_key_value_int (sqlitedb,
					      E_BOOK_SQL_IS_POPULATED_KEY,
					      TRUE,
					      error)) {
		if (error && *error) {
			g_warning ("Failed to set the sqlitedb populated flag: %s", (*error)->message);
		} else {
			g_warning ("Failed to set the sqlitedb populated flag: unknown error");
		}
		return FALSE;
	}

	return TRUE;
}

gboolean
e_book_backend_file_migrate_bdb (EBookSqlite *sqlitedb,
                                 const gchar *dirname,
                                 const gchar *filename,
                                 GCancellable *cancellable,
                                 GError **error)
{
	DB        *db = NULL;
	gint       db_error;
	gboolean   status = FALSE;

	G_LOCK (db_env);

#ifdef G_OS_WIN32
	if (!db_env_initialized) {
		/* Use the gstdio wrappers to open, check, rename and unlink
		 * files from libdb.
		 */
		db_env_set_func_open (my_open);
		db_env_set_func_close (close);
		db_env_set_func_exists (my_exists);
		db_env_set_func_rename (my_rename);
		db_env_set_func_unlink (my_unlink);

		db_env_initialized = TRUE;
	}
#endif

	db_error = e_db3_utils_maybe_recover (filename);
	if (db_error != 0) {
		g_warning ("db recovery failed with %s", db_strerror (db_error));
		db_error_to_gerror (db_error, error);
		goto unlock_env;
	}

	db_error = db_env_create (&db_env, 0);
	if (db_error != 0) {
		g_warning ("db_env_create failed with %s", db_strerror (db_error));
		db_error_to_gerror (db_error, error);
		goto unlock_env;
	}

	db_env->set_errcall (db_env, file_errcall);

	/* Set the allocation routines to the non-aborting GLib functions */
	db_env->set_alloc (db_env, (gpointer (*)(gsize)) g_try_malloc,
			   (gpointer (*)(gpointer , gsize)) g_try_realloc,
			   g_free);

	/*
	 * DB_INIT_TXN enables transaction support. It requires DB_INIT_LOCK to
	 * initialize the locking subsystem and DB_INIT_LOG for the logging
	 * subsystem.
	 *
	 * DB_INIT_MPOOL enables the in-memory cache.
	 *
	 * Note that historically we needed either DB_INIT_CDB or DB_INIT_LOCK,
	 * because we had multiple threads reading and writing concurrently
	 * without any locking above libdb. DB_INIT_LOCK was used because
	 * DB_INIT_TXN conflicts with DB_INIT_CDB.
	 *
	 * Currently we leave this in place because it is known to work.
	 */
	db_error = (*db_env->open) (
		db_env, dirname,
		DB_INIT_LOCK | DB_INIT_TXN | DB_INIT_LOG | DB_CREATE |
		DB_INIT_MPOOL | DB_PRIVATE | DB_THREAD, 0);
	if (db_error != 0) {
		g_warning ("db_env_open failed with %s", db_strerror (db_error));
		db_error_to_gerror (db_error, error);
		goto close_env;
	}

	db_error = db_create (&db, db_env, 0);
	if (db_error != 0) {
		g_warning ("db_create failed with %s", db_strerror (db_error));
		db_error_to_gerror (db_error, error);
		goto close_env;
	}

	/* First try opening the DB.  We want write permission because it's
	 * possible we need to update an out of date DB before actually
	 * migrating it to the SQLite DB. */
	db_error = (*db->open) (db, NULL, filename, NULL, DB_HASH, DB_THREAD | DB_AUTO_COMMIT, 0666);

	/* Was an old version,  lets upgrade the format ... */
	if (db_error == DB_OLD_VERSION) {
		db_error = e_db3_utils_upgrade_format (filename);

		if (db_error != 0) {
			g_warning ("db format upgrade failed with %s", db_strerror (db_error));
			db_error_to_gerror (db_error, error);
			goto close_env;
		}

		db->close (db, 0);
		db_error = db_create (&db, db_env, 0);
		if (db_error != 0) {
			g_warning ("db_create failed with %s", db_strerror (db_error));
			db_error_to_gerror (db_error, error);
			goto close_env;
		}

		/* Try again after the upgrade... */
		db_error = (*db->open) (db, NULL, filename, NULL, DB_HASH, DB_THREAD | DB_AUTO_COMMIT, 0666);

		if (db_error != 0) {
			/* Another error, clean up and get out of here */
			goto close_db;
		}
	}

	/* Unable to open the DB for some reason */
	if (db_error != 0) {
		db_error_to_gerror (db_error, error);
		goto close_env;
	}

	/* Try another upgrade */
	if (!e_book_backend_file_maybe_upgrade_db (db)) {
		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, "e_book_backend_file_maybe_upgrade_db failed"));
		goto close_db;
	}

	/* Now we have our old BDB up and running and migrated to the latest known BDB version,
	 * lets go ahead and now migrate it to the sqlite DB
	 */
	if (migrate_bdb_to_sqlite (sqlitedb, db, cancellable, error))
		status = TRUE;

 close_db:
	db->close (db, 0);
	db = NULL;

 close_env:
	db_env->close (db_env, 0);
	db_env = NULL;

 unlock_env:

	G_UNLOCK (db_env);

	return status;
}

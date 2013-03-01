/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   JP Rosevear (jpr@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

/**
 * SECTION: e-dbhash
 * @short_description: Simple DB-based hash table for strings
 *
 * An #EDbHash is a simple hash table of strings backed by a Berkeley DB
 * file for permanent storage.
 **/

#include <config.h>

#include "e-dbhash.h"

#include <string.h>
#include <fcntl.h>
#include "db.h"

struct _EDbHashPrivate {
	DB *db;
};

/**
 * e_dbhash_new:
 * @filename: path to a Berkeley DB file
 *
 * Creates a new #EDbHash structure and opens the given Berkeley DB file,
 * creating the DB file if necessary.
 *
 * Returns: a new #EDbHash
 **/
EDbHash *
e_dbhash_new (const gchar *filename)
{
	EDbHash *edbh;
	DB *db;
	gint rv;

	/* Attempt to open the database */
	rv = db_create (&db, NULL, 0);
	if (rv != 0) {
		return NULL;
	}

	rv = (*db->open) (db, NULL, filename, NULL, DB_HASH, 0, 0666);
	if (rv != 0) {
		/* Close and re-create the db handle to avoid memory leak */
		db->close (db, 0);
		rv = db_create (&db, NULL, 0);
		if (rv != 0) {
			return NULL;
		}

		rv = (*db->open) (
			db, NULL, filename, NULL, DB_HASH, DB_CREATE, 0666);

		if (rv != 0) {
			db->close (db, 0);
			return NULL;
		}
	}

	edbh = g_new (EDbHash, 1);
	edbh->priv = g_new (EDbHashPrivate, 1);
	edbh->priv->db = db;

	return edbh;
}

static void
string_to_dbt (const gchar *str,
               DBT *dbt)
{
	memset (dbt, 0, sizeof (DBT));
	dbt->data = (gpointer) str;
	dbt->size = strlen (str) + 1;
}

static void
md5_to_dbt (const guint8 str[16],
            DBT *dbt)
{
	memset (dbt, 0, sizeof (DBT));
	dbt->data = (gpointer) str;
	dbt->size = 16;
}

/**
 * e_dbhash_add:
 * @edbh: an #EDbHash
 * @key: a database key
 * @data: a database object for @key
 *
 * Adds a database object for @key.
 **/
void
e_dbhash_add (EDbHash *edbh,
              const gchar *key,
              const gchar *data)
{
	DB *db;
	DBT dkey;
	DBT ddata;
	GChecksum *checksum;
	guint8 *digest;
	gsize length;

	g_return_if_fail (edbh != NULL);
	g_return_if_fail (edbh->priv != NULL);
	g_return_if_fail (edbh->priv->db != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (data != NULL);

	length = g_checksum_type_get_length (G_CHECKSUM_MD5);
	digest = g_alloca (length);

	db = edbh->priv->db;

	/* Key dbt */
	string_to_dbt (key, &dkey);

	/* Compute MD5 checksum */
	checksum = g_checksum_new (G_CHECKSUM_MD5);
	g_checksum_update (checksum, (guchar *) data, -1);
	g_checksum_get_digest (checksum, digest, &length);
	g_checksum_free (checksum);

	/* Data dbt */
	md5_to_dbt (digest, &ddata);

	/* Add to database */
	db->put (db, NULL, &dkey, &ddata, 0);
}

/**
 * e_dbhash_remove:
 * @edbh: an #EDbHash
 * @key: a database key
 *
 * Removes the database object corresponding to @key.
 **/
void
e_dbhash_remove (EDbHash *edbh,
                 const gchar *key)
{
	DB *db;
	DBT dkey;

	g_return_if_fail (edbh != NULL);
	g_return_if_fail (edbh->priv != NULL);
	g_return_if_fail (key != NULL);

	db = edbh->priv->db;

	/* Key dbt */
	string_to_dbt (key, &dkey);

	/* Remove from database */
	db->del (db, NULL, &dkey, 0);
}

/**
 * e_dbhash_foreach_key:
 * @edbh: an #EDbHash
 * @func: a callback function
 * @user_data: data to pass to @func
 *
 * Calls @func for each database object.
 **/
void
e_dbhash_foreach_key (EDbHash *edbh,
                      EDbHashFunc func,
                      gpointer user_data)
{
	DB *db;
	DBT dkey;
	DBT ddata;
	DBC *dbc;
	gint db_error = 0;

	g_return_if_fail (edbh != NULL);
	g_return_if_fail (edbh->priv != NULL);
	g_return_if_fail (func != NULL);

	db = edbh->priv->db;

	db_error = db->cursor (db, NULL, &dbc, 0);

	if (db_error != 0) {
		return;
	}

	memset (&dkey, 0, sizeof (DBT));
	memset (&ddata, 0, sizeof (DBT));
	db_error = dbc->c_get (dbc, &dkey, &ddata, DB_FIRST);

	while (db_error == 0) {
		(*func) ((const gchar *) dkey.data, user_data);

		db_error = dbc->c_get (dbc, &dkey, &ddata, DB_NEXT);
	}
	dbc->c_close (dbc);
}

/**
 * e_dbhash_compare:
 * @edbh: an #EDbHash
 * @key: a database key
 * @compare_data: data to compare against the database
 *
 * Compares @compare_data to the database object corresponding to
 * @key using an MD5 checksum.  Returns #E_DBHASH_STATUS_SAME if the
 * checksums match, #E_DBHASH_STATUS_DIFFERENT if the checksums differ,
 * or #E_DBHASH_STATUS_NOT_FOUND if @key is not present in the database.
 *
 * Returns: a checksum comparison status
 **/
EDbHashStatus
e_dbhash_compare (EDbHash *edbh,
                  const gchar *key,
                  const gchar *compare_data)
{
	DB *db;
	DBT dkey;
	DBT ddata;
	guint8 compare_hash[16];
	gsize length = sizeof (compare_hash);

	g_return_val_if_fail (edbh != NULL, FALSE);
	g_return_val_if_fail (edbh->priv != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (compare_hash != NULL, FALSE);

	db = edbh->priv->db;

	/* Key dbt */
	string_to_dbt (key, &dkey);

	/* Lookup in database */
	memset (&ddata, 0, sizeof (DBT));
	db->get (db, NULL, &dkey, &ddata, 0);

	/* Compare */
	if (ddata.data) {
		GChecksum *checksum;

		checksum = g_checksum_new (G_CHECKSUM_MD5);
		g_checksum_update (checksum, (guchar *) compare_data, -1);
		g_checksum_get_digest (checksum, compare_hash, &length);
		g_checksum_free (checksum);

		if (memcmp (ddata.data, compare_hash, sizeof (guchar) * 16))
			return E_DBHASH_STATUS_DIFFERENT;
	} else {
		return E_DBHASH_STATUS_NOT_FOUND;
	}

	return E_DBHASH_STATUS_SAME;
}

/**
 * e_dbhash_write:
 * @edbh: an #EDbHash
 *
 * Flushes database changes to disk.
 **/
void
e_dbhash_write (EDbHash *edbh)
{
	DB *db;

	g_return_if_fail (edbh != NULL);
	g_return_if_fail (edbh->priv != NULL);

	db = edbh->priv->db;

	/* Flush database to disk */
	db->sync (db, 0);
}

/**
 * e_dbhash_destroy:
 * @edbh: an #EDbHash
 *
 * Closes the database file and frees the #EDbHash.
 **/
void
e_dbhash_destroy (EDbHash *edbh)
{
	DB *db;

	g_return_if_fail (edbh != NULL);
	g_return_if_fail (edbh->priv != NULL);

	db = edbh->priv->db;

	/* Close datbase */
	db->close (db, 0);

	g_free (edbh->priv);
	g_free (edbh);
}

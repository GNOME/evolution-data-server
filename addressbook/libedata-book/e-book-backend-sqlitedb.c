/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-book-backend-sqlitedb.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Chenthill Palanisamy <pchenthill@novell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <sqlite3.h>
#include <libebackend/libebackend.h>

#include "e-book-backend-sexp.h"
#include "e-book-backend-sqlitedb.h"

#define E_BOOK_BACKEND_SQLITEDB_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BOOK_BACKEND_SQLITEDB, EBookBackendSqliteDBPrivate))

#define d(x)

#define DB_FILENAME "contacts.db"
#define FOLDER_VERSION 3

#define READER_LOCK(ebsdb) g_rw_lock_reader_lock (&ebsdb->priv->rwlock)
#define READER_UNLOCK(ebsdb) g_rw_lock_reader_unlock (&ebsdb->priv->rwlock)
#define WRITER_LOCK(ebssdb) g_rw_lock_writer_lock (&ebsdb->priv->rwlock)
#define WRITER_UNLOCK(ebssdb) g_rw_lock_writer_unlock (&ebsdb->priv->rwlock)

typedef enum {
	INDEX_PREFIX = (1 << 0),
	INDEX_SUFFIX = (1 << 1)
} IndexFlags;

typedef struct {
	EContactField field;   /* The EContact field */
	GType         type;    /* The GType (only support string or gboolean) */
	const gchar  *dbname;  /* The key for this field in the sqlite3 table */
	IndexFlags    index;   /* Whether this summary field should have an index in the SQLite DB */
} SummaryField;

struct _EBookBackendSqliteDBPrivate {
	sqlite3 *db;
	gchar *path;
	gchar *hash_key;

	gboolean store_vcard;
	GRWLock rwlock;

	GMutex in_transaction_lock;
	guint32 in_transaction;

	SummaryField   *summary_fields;
	gint            n_summary_fields;
	guint           have_attr_list : 1;
	guint           have_attr_list_prefix : 1;
	guint           have_attr_list_suffix : 1;
};

G_DEFINE_TYPE (EBookBackendSqliteDB, e_book_backend_sqlitedb, G_TYPE_OBJECT)

#define E_BOOK_SDB_ERROR \
	(e_book_backend_sqlitedb_error_quark ())

static GHashTable *db_connections = NULL;
static GMutex dbcon_lock;

static EContactField default_summary_fields[] = {
	E_CONTACT_UID,
	E_CONTACT_REV,
	E_CONTACT_FILE_AS,
	E_CONTACT_NICKNAME,
	E_CONTACT_FULL_NAME,
	E_CONTACT_GIVEN_NAME,
	E_CONTACT_FAMILY_NAME,
	E_CONTACT_EMAIL,
	E_CONTACT_IS_LIST,
	E_CONTACT_LIST_SHOW_ADDRESSES,
	E_CONTACT_WANTS_HTML
};

/* Create indexes on full_name and email fields as autocompletion queries would mainly
 * rely on this.
 */
static EContactField default_indexed_fields[] = {
	E_CONTACT_FULL_NAME,
	E_CONTACT_EMAIL
};

static EBookIndexType default_index_types[] = {
	E_BOOK_INDEX_PREFIX,
	E_BOOK_INDEX_PREFIX
};

static gboolean append_summary_field (GArray         *array,
				      EContactField   field,
				      gboolean       *have_attr_list,
				      GError        **error);

static const gchar *
summary_dbname_from_field (EBookBackendSqliteDB *ebsdb,
			   EContactField         field)
{
	gint i;

	for (i = 0; i < ebsdb->priv->n_summary_fields; i++) {
		if (ebsdb->priv->summary_fields[i].field == field)
			return ebsdb->priv->summary_fields[i].dbname;
	}

	return NULL;
}

static gint
summary_index_from_field_name (EBookBackendSqliteDB *ebsdb,
			       const gchar          *field_name)
{
	gint i;
	EContactField field;

	field = e_contact_field_id (field_name);

	for (i = 0; i < ebsdb->priv->n_summary_fields; i++) {
		if (ebsdb->priv->summary_fields[i].field == field)
			return i;
	}

	return -1;
}

typedef struct {
	EBookBackendSqliteDB *ebsdb;
	GSList *list;
} StoreVCardData;

static GQuark
e_book_backend_sqlitedb_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "e-book-backend-sqlitedb-error-quark";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

static void
e_book_backend_sqlitedb_dispose (GObject *object)
{
	EBookBackendSqliteDBPrivate *priv;

	priv = E_BOOK_BACKEND_SQLITEDB_GET_PRIVATE (object);

	g_mutex_lock (&dbcon_lock);
	if (db_connections != NULL) {
		if (priv->hash_key != NULL) {
			g_hash_table_remove (db_connections, priv->hash_key);

			if (g_hash_table_size (db_connections) == 0) {
				g_hash_table_destroy (db_connections);
				db_connections = NULL;
			}

			g_free (priv->hash_key);
			priv->hash_key = NULL;
		}
	}
	g_mutex_unlock (&dbcon_lock);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_book_backend_sqlitedb_parent_class)->dispose (object);
}

static void
e_book_backend_sqlitedb_finalize (GObject *object)
{
	EBookBackendSqliteDBPrivate *priv;

	priv = E_BOOK_BACKEND_SQLITEDB_GET_PRIVATE (object);

	g_rw_lock_clear (&priv->rwlock);

	sqlite3_close (priv->db);

	g_free (priv->path);
	g_free (priv->summary_fields);

	g_mutex_clear (&priv->in_transaction_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_backend_sqlitedb_parent_class)->finalize (object);
}

static void
e_book_backend_sqlitedb_class_init (EBookBackendSqliteDBClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EBookBackendSqliteDBPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = e_book_backend_sqlitedb_dispose;
	object_class->finalize = e_book_backend_sqlitedb_finalize;
}

static void
e_book_backend_sqlitedb_init (EBookBackendSqliteDB *ebsdb)
{
	ebsdb->priv = E_BOOK_BACKEND_SQLITEDB_GET_PRIVATE (ebsdb);

	ebsdb->priv->store_vcard = TRUE;
	g_rw_lock_init (&ebsdb->priv->rwlock);

	ebsdb->priv->in_transaction = 0;
	g_mutex_init (&ebsdb->priv->in_transaction_lock);
}

static gint
get_string_cb (gpointer ref,
               gint col,
               gchar **cols,
               gchar **name)
{
	gchar **ret = ref;

	*ret = g_strdup (cols [0]);

	return 0;
}

static gint
get_bool_cb (gpointer ref,
             gint col,
             gchar **cols,
             gchar **name)
{
	gboolean *ret = ref;

	*ret = cols [0] ? strtoul (cols [0], NULL, 10) : 0;

	return 0;
}

/**
 * e_book_sql_exec
 * @db:
 * @stmt:
 * @callback:
 * @data:
 * @error:
 *
 * Callers should hold the rw lock depending on read or write operation
 * Returns:
 **/
static gboolean
book_backend_sql_exec_real (sqlite3 *db,
                            const gchar *stmt,
                            gint (*callback)(gpointer ,gint,gchar **,gchar **),
                            gpointer data,
                            GError **error)
{
	gchar *errmsg = NULL;
	gint ret = -1;

	ret = sqlite3_exec (db, stmt, callback, data, &errmsg);
	while (ret == SQLITE_BUSY || ret == SQLITE_LOCKED || ret == -1) {
		if (errmsg) {
			sqlite3_free (errmsg);
			errmsg = NULL;
		}
		ret = sqlite3_exec (db, stmt, NULL, NULL, &errmsg);
	}

	if (ret != SQLITE_OK) {
		d (g_print ("Error in SQL EXEC statement: %s [%s].\n", stmt, errmsg));
		g_set_error_literal (
			error, E_BOOK_SDB_ERROR, 0, errmsg);
		sqlite3_free (errmsg);
		errmsg = NULL;
		return FALSE;
	}

	if (errmsg) {
		sqlite3_free (errmsg);
		errmsg = NULL;
	}

	return TRUE;
}

static gint
print_debug_cb (gpointer ref,
                gint col,
                gchar **cols,
                gchar **name)
{
	gint i;

	g_print ("  DEBUG BEGIN: %d results\n", col);

	for (i = 0; i < col; i++)
		g_print ("    NAME: '%s' COL: %s\n", name[i], cols[i]);

	g_print ("  DEBUG END\n");

	return 0;
}

static void
book_backend_sql_debug (sqlite3 *db,
			const gchar *stmt,
			gint (*callback)(gpointer ,gint,gchar **,gchar **),
			gpointer data,
			GError **error)
{
	gchar *debug;
	GError *local_error = NULL;
	debug = g_strconcat ("EXPLAIN QUERY PLAN ", stmt, NULL);

	g_print ("DEBUG STATEMENT: %s\n", stmt);
	book_backend_sql_exec_real (db, debug, print_debug_cb, NULL, &local_error);
	g_print ("DEBUG STATEMENT END: %s%s\n", local_error ? "Error: " : "", local_error ? local_error->message : "Success");
	g_free (debug);

	g_clear_error (&local_error);
}

static gboolean
book_backend_sql_exec (sqlite3 *db,
                       const gchar *stmt,
                       gint (*callback)(gpointer ,gint,gchar **,gchar **),
                       gpointer data,
                       GError **error)
{
	static gint booksql_debug = -1;

	if (booksql_debug == -1) {
		booksql_debug = g_getenv ("BOOKSQL_DEBUG") != NULL ? 1 : 0;
	}

	if (booksql_debug)
		book_backend_sql_debug (db, stmt, callback, data, error);

	return book_backend_sql_exec_real (db, stmt, callback, data, error);
}

/* the first caller holds the writer lock too */
static gboolean
book_backend_sqlitedb_start_transaction (EBookBackendSqliteDB *ebsdb,
                                         GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (ebsdb != NULL, FALSE);
	g_return_val_if_fail (ebsdb->priv != NULL, FALSE);
	g_return_val_if_fail (ebsdb->priv->db != NULL, FALSE);

	g_mutex_lock (&ebsdb->priv->in_transaction_lock);

	ebsdb->priv->in_transaction++;
	if (ebsdb->priv->in_transaction == 0) {
		g_mutex_unlock (&ebsdb->priv->in_transaction_lock);

		g_return_val_if_fail (ebsdb->priv->in_transaction != 0, FALSE);
		return FALSE;
	}

	if (ebsdb->priv->in_transaction == 1) {
		WRITER_LOCK (ebsdb);

		success = book_backend_sql_exec (
			ebsdb->priv->db, "BEGIN", NULL, NULL, error);
	}

	g_mutex_unlock (&ebsdb->priv->in_transaction_lock);

	return success;
}

/* the last caller releases the writer lock too */
static gboolean
book_backend_sqlitedb_commit_transaction (EBookBackendSqliteDB *ebsdb,
                                          GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (ebsdb != NULL, FALSE);
	g_return_val_if_fail (ebsdb->priv != NULL, FALSE);
	g_return_val_if_fail (ebsdb->priv->db != NULL, FALSE);

	g_mutex_lock (&ebsdb->priv->in_transaction_lock);

	if (ebsdb->priv->in_transaction == 0) {
		g_mutex_unlock (&ebsdb->priv->in_transaction_lock);

		g_return_val_if_fail (ebsdb->priv->in_transaction > 0, FALSE);
		return FALSE;
	}

	ebsdb->priv->in_transaction--;

	if (ebsdb->priv->in_transaction == 0) {
		success = book_backend_sql_exec (
			ebsdb->priv->db, "COMMIT", NULL, NULL, error);

		WRITER_UNLOCK (ebsdb);
	}

	g_mutex_unlock (&ebsdb->priv->in_transaction_lock);

	return success;
}

/* the last caller releases the writer lock too */
static gboolean
book_backend_sqlitedb_rollback_transaction (EBookBackendSqliteDB *ebsdb,
                                            GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (ebsdb != NULL, FALSE);
	g_return_val_if_fail (ebsdb->priv != NULL, FALSE);
	g_return_val_if_fail (ebsdb->priv->db != NULL, FALSE);

	g_mutex_lock (&ebsdb->priv->in_transaction_lock);

	if (ebsdb->priv->in_transaction == 0) {
		g_mutex_unlock (&ebsdb->priv->in_transaction_lock);

		g_return_val_if_fail (ebsdb->priv->in_transaction > 0, FALSE);
		return FALSE;
	}

	ebsdb->priv->in_transaction--;

	if (ebsdb->priv->in_transaction == 0) {
		success = book_backend_sql_exec (
			ebsdb->priv->db, "ROLLBACK", NULL, NULL, error);

		WRITER_UNLOCK (ebsdb);
	}

	g_mutex_unlock (&ebsdb->priv->in_transaction_lock);

	return success;
}

static gint
collect_versions_cb (gpointer ref,
                     gint col,
                     gchar **cols,
                     gchar **name)
{
	gint *ret = ref;

	/* Just collect the first result, all folders
	 * should always have the same DB version. */
	*ret = cols [0] ? strtoul (cols [0], NULL, 10) : 0;

	return 0;
}

static gboolean
create_folders_table (EBookBackendSqliteDB *ebsdb,
                      GError **error)
{
	gboolean success;
	gint version = 0;

	/* sync_data points to syncronization data, it could be last_modified
	 * time or a sequence number or some text depending on the backend.
	 *
	 * partial_content says whether the contents are partially downloaded
	 * for auto-completion or if it has the complete content.
	 *
	 * Have not included a bdata here since the keys table should suffice
	 * any additional need that arises.
	 */
	const gchar *stmt =
		"CREATE TABLE IF NOT EXISTS folders"
		"( folder_id  TEXT PRIMARY KEY,"
		" folder_name TEXT,"
		"  sync_data TEXT,"
		" is_populated INTEGER,"
		"  partial_content INTEGER,"
		" version INTEGER,"
		"  revision TEXT,"
		" multivalues TEXT,"
		"  reverse_multivalues INTEGER )";

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error))
		return FALSE;

	if (!book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error))
		goto rollback;

	/* Create a child table to store key/value pairs for a folder. */
	stmt =	"CREATE TABLE IF NOT EXISTS keys"
		"( key TEXT PRIMARY KEY, value TEXT,"
		" folder_id TEXT REFERENCES folders)";
	if (!book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error))
		goto rollback;

	stmt = "CREATE INDEX IF NOT EXISTS keysindex ON keys(folder_id)";
	if (!book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error))
		goto rollback;

	/* Fetch the version, it should be the
	 * same for all folders (hence the LIMIT). */
	stmt = "SELECT version FROM folders LIMIT 1";
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, collect_versions_cb, &version, error);

	if (!success)
		goto rollback;

	/* Upgrade DB to version 2, add revision column
	 *
	 * (version = 0 indicates that it did not exist and we just
	 * created the table)
	 */
	if (version >= 1 && version < 2) {
		stmt = "ALTER TABLE folders ADD COLUMN revision TEXT";
		success = book_backend_sql_exec (
			ebsdb->priv->db, stmt, NULL, NULL, error);

		if (!success)
			goto rollback;
	}

	/* Upgrade DB to version 3, add multivalues introspection columns
	 */
	if (version >= 1 && version < 3) {

		stmt = "ALTER TABLE folders ADD COLUMN multivalues TEXT";
		success = book_backend_sql_exec (
			ebsdb->priv->db, stmt, NULL, NULL, error);

		if (!success)
			goto rollback;

		stmt = "ALTER TABLE folders ADD COLUMN reverse_multivalues INTEGER";
		success = book_backend_sql_exec (
			ebsdb->priv->db, stmt, NULL, NULL, error);

		if (!success)
			goto rollback;
	}

	if (version >= 1 && version < FOLDER_VERSION) {
		gchar *version_update_stmt =
			sqlite3_mprintf ("UPDATE folders SET version = %d", FOLDER_VERSION);

		success = book_backend_sql_exec (
			ebsdb->priv->db, version_update_stmt, NULL, NULL, error);

		sqlite3_free (version_update_stmt);
	}

	if (!success)
		goto rollback;

	return book_backend_sqlitedb_commit_transaction (ebsdb, error);

rollback:
	/* The GError is already set. */
	book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	return FALSE;
}


static gchar *
format_multivalues (EBookBackendSqliteDB *ebsdb,
		    gboolean             *reverse_multivalues)
{
	gint i;
	GString *string;
	gboolean first = TRUE;
	gboolean has_reverse = FALSE;

	string = g_string_new (NULL);

	for (i = 0; i < ebsdb->priv->n_summary_fields; i++) {
		if (ebsdb->priv->summary_fields[i].type == E_TYPE_CONTACT_ATTR_LIST) {
			if (first)
				first = FALSE;
			else
				g_string_append_c (string, ':');

			g_string_append (string, ebsdb->priv->summary_fields[i].dbname);

			if ((ebsdb->priv->summary_fields[i].index & INDEX_SUFFIX) != 0)
				has_reverse = TRUE;
		}
	}

	if (reverse_multivalues)
		*reverse_multivalues = has_reverse;

	return g_string_free (string, FALSE);
}

static gboolean
add_folder_into_db (EBookBackendSqliteDB *ebsdb,
                    const gchar *folderid,
                    const gchar *folder_name,
                    GError **error)
{
	gchar *stmt;
	gboolean success;
	gboolean has_reverse = FALSE;
	gchar *multivalues;

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error))
		return FALSE;

	multivalues = format_multivalues (ebsdb, &has_reverse);

	stmt = sqlite3_mprintf (
		"INSERT OR IGNORE INTO folders VALUES "
		"( %Q, %Q, %Q, %d, %d, %d, %Q, %Q, %d ) ",
		folderid, folder_name, NULL, 0, 0, FOLDER_VERSION,
		NULL, multivalues, has_reverse);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);
	g_free (multivalues);

	if (!success)
		goto rollback;

	return book_backend_sqlitedb_commit_transaction (ebsdb, error);

rollback:
	book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	return FALSE;
}

static gint
collect_columns_cb (gpointer ref,
		    gint col,
		    gchar **cols,
		    gchar **name)
{
	GList **columns = (GList **)ref;
	gint i;

	for (i = 0; i < col; i++) {

		if (strcmp (name[i], "name") == 0) {

			if (strcmp (cols[i], "vcard") != 0 &&
			    strcmp (cols[i], "bdata") != 0) {

				gchar *column = g_strdup (cols[i]);

				*columns = g_list_prepend (*columns, column);
			}

			break;
		}
	}

	return 0;
}

static gboolean
introspect_summary (EBookBackendSqliteDB *ebsdb,
		    const gchar *folderid,
		    GError **error)
{
	gboolean success;
	gchar *stmt;
	GList *summary_columns = NULL, *l;
	GArray *summary_fields = NULL;
	gchar *multivalues = NULL;
	gboolean reverse_multivalues = FALSE;
	gchar **split;
	gint i;

	stmt = sqlite3_mprintf ("PRAGMA table_info (%Q);", folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, collect_columns_cb, &summary_columns, error);
	sqlite3_free (stmt);

	if (!success)
		goto introspect_summary_finish;

	summary_columns = g_list_reverse (summary_columns);
	summary_fields = g_array_new (FALSE, FALSE, sizeof (SummaryField));

	/* Introspect the normal summary fields */
	for (l = summary_columns; l; l = l->next) {
		EContactField field;
		gchar *col = l->data;
		gchar *p;
		gboolean reverse = FALSE;

		/* Check if we're parsing a reverse field */
		p = strstr (col, "_reverse");
		if (p) {
			*p = '\0';
			reverse = TRUE;
		}

		/* First check exception fields */
		if (strcmp (col, "uid") == 0)
			field = E_CONTACT_UID;
		else if (strcmp (col, "is_list") == 0)
			field = E_CONTACT_IS_LIST;
		else
			field = e_contact_field_id (col);

		/* Check for parse error */
		if (field == 0) {
			g_set_error (
				error, E_BOOK_SDB_ERROR, 0,
				_("Error introspecting unknown summary field '%s'"), col);
			success = FALSE;
			break;
		}

		/* Reverse columns are always declared after the normal columns,
		 * if a reverse field is encountered we need to set the suffix
		 * index on the coresponding summary field
		 */
		if (reverse) {
			for (i = 0; i < summary_fields->len; i++) {
				SummaryField *iter = &g_array_index (summary_fields, SummaryField, i);

				if (iter->field == field) {
					iter->index |= INDEX_SUFFIX;
					break;
				}
			}
		} else {
			append_summary_field (summary_fields, field, NULL, NULL);
		}
	}

	if (!success)
		goto introspect_summary_finish;

	/* Introspect the multivalied summary fields */
	stmt = sqlite3_mprintf (
		"SELECT multivalues FROM folders WHERE folder_id = %Q", folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_string_cb, &multivalues, error);
	sqlite3_free (stmt);

	if (!success)
		goto introspect_summary_finish;

	stmt = sqlite3_mprintf (
		"SELECT reverse_multivalues FROM folders WHERE folder_id = %Q", folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_bool_cb, &reverse_multivalues, error);
	sqlite3_free (stmt);

	if (!success)
		goto introspect_summary_finish;

	if (multivalues) {
		split = g_strsplit (multivalues, ":", 0);

		for (i = 0; split[i] != NULL; i++) {
			EContactField field;

			field = e_contact_field_id (split[i]);
			append_summary_field (summary_fields, field, NULL, NULL);
		}
		g_strfreev (split);
	}

	/* If there is a reverse multivalue column, enable lookups for every multivalue field in reverse */
	if (reverse_multivalues) {

		for (i = 0; i < summary_fields->len; i++) {
			SummaryField *iter = &g_array_index (summary_fields, SummaryField, i);

			if (iter->type == E_TYPE_CONTACT_ATTR_LIST)
				iter->index |= INDEX_SUFFIX;
		}
	}

 introspect_summary_finish:

	g_list_free_full (summary_columns, (GDestroyNotify)g_free);
	g_free (multivalues);

	/* Apply the introspected summary fields */
	if (success) {
		g_free (ebsdb->priv->summary_fields);
		ebsdb->priv->n_summary_fields = summary_fields->len;
		ebsdb->priv->summary_fields = (SummaryField *)g_array_free (summary_fields, FALSE);
	} else if (summary_fields) {
		g_array_free (summary_fields, TRUE);
	}

	return success;
}


/* The column names match the fields used in book-backend-sexp */
static gboolean
create_contacts_table (EBookBackendSqliteDB *ebsdb,
                       const gchar *folderid,
                       GError **error)
{
	gint i;
	gboolean success;
	gchar *stmt, *tmp;
	GString *string;

	/* Construct the create statement from the summary fields table */
	string = g_string_new (
		"CREATE TABLE IF NOT EXISTS %Q ( uid TEXT PRIMARY KEY, ");

	for (i = 1; i < ebsdb->priv->n_summary_fields; i++) {
		if (ebsdb->priv->summary_fields[i].type == G_TYPE_STRING) {
			g_string_append (string, ebsdb->priv->summary_fields[i].dbname);
			g_string_append (string, " TEXT, ");
		} else if (ebsdb->priv->summary_fields[i].type == G_TYPE_BOOLEAN) {
			g_string_append (string, ebsdb->priv->summary_fields[i].dbname);
			g_string_append (string, " INTEGER, ");
		} else if (ebsdb->priv->summary_fields[i].type != E_TYPE_CONTACT_ATTR_LIST)
			g_warn_if_reached ();

		/* Additional columns holding normalized reverse values for suffix matching */
		if (ebsdb->priv->summary_fields[i].type == G_TYPE_STRING &&
		    (ebsdb->priv->summary_fields[i].index & INDEX_SUFFIX) != 0) {
			g_string_append  (string, ebsdb->priv->summary_fields[i].dbname);
			g_string_append  (string, "_reverse TEXT, ");
		}
	}
	g_string_append (string, "vcard TEXT, bdata TEXT)");

	stmt = sqlite3_mprintf (string->str, folderid);
	g_string_free (string, TRUE);

	WRITER_LOCK (ebsdb);

	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL , error);

	sqlite3_free (stmt);

	/* Create indexes on the summary fields configured for indexing */
	for (i = 0; success && i < ebsdb->priv->n_summary_fields; i++) {
		if ((ebsdb->priv->summary_fields[i].index & INDEX_PREFIX) != 0 &&
		    ebsdb->priv->summary_fields[i].type != E_TYPE_CONTACT_ATTR_LIST) {
			/* Derive index name from field & folder */
			tmp = g_strdup_printf ("INDEX_%s_%s",
					       summary_dbname_from_field (ebsdb, ebsdb->priv->summary_fields[i].field),
					       folderid);
			stmt = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS %Q ON %Q (%s)", tmp, folderid,
						summary_dbname_from_field (ebsdb, ebsdb->priv->summary_fields[i].field));
			success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
			sqlite3_free (stmt);
			g_free (tmp);
		}

		if (success &&
		    (ebsdb->priv->summary_fields[i].index & INDEX_SUFFIX) != 0 &&
		    ebsdb->priv->summary_fields[i].type != E_TYPE_CONTACT_ATTR_LIST) {
			/* Derive index name from field & folder */
			tmp = g_strdup_printf ("RINDEX_%s_%s",
					       summary_dbname_from_field (ebsdb, ebsdb->priv->summary_fields[i].field),
					       folderid);
			stmt = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS %Q ON %Q (%s_reverse)", tmp, folderid,
						summary_dbname_from_field (ebsdb, ebsdb->priv->summary_fields[i].field));
			success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
			sqlite3_free (stmt);
			g_free (tmp);
		}
	}

	/* Construct the create statement from the attribute list summary table */
	if (success && ebsdb->priv->have_attr_list) {
		string = g_string_new ("CREATE TABLE IF NOT EXISTS %Q ( uid TEXT NOT NULL REFERENCES %Q(uid), "
				       "field TEXT, value TEXT");

		if (ebsdb->priv->have_attr_list_suffix)
			g_string_append (string, ", value_reverse TEXT");

		g_string_append_c (string, ')');

		tmp = g_strdup_printf ("%s_lists", folderid);
		stmt = sqlite3_mprintf (string->str, tmp, folderid);
		g_string_free (string, TRUE);

		success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
		sqlite3_free (stmt);

		/* Give the UID an index in this table, always */
		stmt = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS LISTINDEX ON %Q (uid)", tmp);
		success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
		sqlite3_free (stmt);

		/* Create indexes if specified */
		if (success && ebsdb->priv->have_attr_list_prefix) {
			stmt = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS VALINDEX ON %Q (value)", tmp);
			success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
			sqlite3_free (stmt);
		}

		if (success && ebsdb->priv->have_attr_list_suffix) {
			stmt = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS RVALINDEX ON %Q (value_reverse)", tmp);
			success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
			sqlite3_free (stmt);
		}

		g_free (tmp);

	}

	WRITER_UNLOCK (ebsdb);

	if (success)
		success = introspect_summary (ebsdb, folderid, error);

	return success;
}

static gboolean
book_backend_sqlitedb_load (EBookBackendSqliteDB *ebsdb,
                            const gchar *filename,
                            GError **error)
{
	gint ret;

	e_sqlite3_vfs_init ();

	ret = sqlite3_open (filename, &ebsdb->priv->db);
	if (ret) {
		if (!ebsdb->priv->db) {
			g_set_error (
				error, E_BOOK_SDB_ERROR, 0,
				_("Insufficient memory"));
		} else {
			const gchar *errmsg;
			errmsg = sqlite3_errmsg (ebsdb->priv->db);
			d (g_print ("Can't open database %s: %s\n", path, errmsg));
			g_set_error_literal (
				error, E_BOOK_SDB_ERROR, 0, errmsg);
			sqlite3_close (ebsdb->priv->db);
		}
		return FALSE;
	}

	WRITER_LOCK (ebsdb);

	book_backend_sql_exec (
		ebsdb->priv->db,
		"ATTACH DATABASE ':memory:' AS mem",
		NULL, NULL, NULL);
	book_backend_sql_exec (
		ebsdb->priv->db,
		"PRAGMA foreign_keys = ON",
		NULL, NULL, NULL);
	book_backend_sql_exec (
		ebsdb->priv->db,
		"PRAGMA case_sensitive_like = ON",
		NULL, NULL, NULL);

	WRITER_UNLOCK (ebsdb);

	return create_folders_table (ebsdb, error);
}

static EBookBackendSqliteDB *
e_book_backend_sqlitedb_new_internal (const gchar *path,
				      const gchar *emailid,
				      const gchar *folderid,
				      const gchar *folder_name,
				      gboolean store_vcard,
				      SummaryField *fields,
				      gint n_fields,
				      gboolean have_attr_list,
				      gboolean have_attr_list_prefix,
				      gboolean have_attr_list_suffix,
				      GError **error)
{
	EBookBackendSqliteDB *ebsdb;
	gchar *hash_key, *filename;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (emailid != NULL, NULL);
	g_return_val_if_fail (folderid != NULL, NULL);
	g_return_val_if_fail (folder_name != NULL, NULL);

	g_mutex_lock (&dbcon_lock);

	hash_key = g_strdup_printf ("%s@%s", emailid, path);
	if (db_connections != NULL) {
		ebsdb = g_hash_table_lookup (db_connections, hash_key);

		if (ebsdb) {
			g_object_ref (ebsdb);
			g_mutex_unlock (&dbcon_lock);
			g_free (hash_key);
			goto exit;
		}
	}

	ebsdb = g_object_new (E_TYPE_BOOK_BACKEND_SQLITEDB, NULL);
	ebsdb->priv->path = g_strdup (path);
	ebsdb->priv->summary_fields = fields;
	ebsdb->priv->n_summary_fields = n_fields;
	ebsdb->priv->have_attr_list = have_attr_list;
	ebsdb->priv->have_attr_list_prefix = have_attr_list_prefix;
	ebsdb->priv->have_attr_list_suffix = have_attr_list_suffix;
	ebsdb->priv->store_vcard = store_vcard;
	if (g_mkdir_with_parents (path, 0777) < 0) {
		g_mutex_unlock (&dbcon_lock);
		g_set_error (
			error, E_BOOK_SDB_ERROR, 0,
			"Can not make parent directory: errno %d", errno);
		return NULL;
	}
	filename = g_build_filename (path, DB_FILENAME, NULL);

	if (!book_backend_sqlitedb_load (ebsdb, filename, error)) {
		g_mutex_unlock (&dbcon_lock);
		g_object_unref (ebsdb);
		g_free (filename);
		return NULL;
	}
	g_free (filename);

	if (db_connections == NULL)
		db_connections = g_hash_table_new_full (
			(GHashFunc) g_str_hash,
			(GEqualFunc) g_str_equal,
			(GDestroyNotify) g_free,
			(GDestroyNotify) NULL);
	g_hash_table_insert (db_connections, hash_key, ebsdb);
	ebsdb->priv->hash_key = g_strdup (hash_key);

	g_mutex_unlock (&dbcon_lock);

exit:
	if (!add_folder_into_db (ebsdb, folderid, folder_name, error)) {
		g_object_unref (ebsdb);
		return NULL;
	}

	if (!create_contacts_table (ebsdb, folderid, error)) {
		g_object_unref (ebsdb);
		return NULL;
	}

	return ebsdb;
}

static gboolean
append_summary_field (GArray         *array,
		      EContactField   field,
		      gboolean       *have_attr_list,
		      GError        **error)
{
	const gchar *dbname = NULL;
	GType        type = G_TYPE_INVALID;
	gint         i;
	SummaryField new_field = { 0, };

	if (field < 1 || field >= E_CONTACT_FIELD_LAST) {
		g_set_error (error, E_BOOK_SDB_ERROR,
			     0, _("Invalid contact field '%d' specified in summary"), field);
		return FALSE;
	}

	/* Avoid including the same field twice in the summary */
	for (i = 0; i < array->len; i++) {
		SummaryField *iter = &g_array_index (array, SummaryField, i);
		if (field == iter->field)
			return TRUE;
	}

	/* Resolve some exceptions, we store these
	 * specific contact fields with different names
	 * than those found in the EContactField table
	 */
	switch (field) {
	case E_CONTACT_UID:
		dbname = "uid";
		break;
	case E_CONTACT_IS_LIST:
		dbname = "is_list";
		break;
	default:
		dbname = e_contact_field_name (field);
		break;
	}

	type = e_contact_field_type (field);

	if (type != G_TYPE_STRING &&
	    type != G_TYPE_BOOLEAN &&
	    type != E_TYPE_CONTACT_ATTR_LIST) {
		g_set_error (error, E_BOOK_SDB_ERROR, 0,
			     _("Contact field '%s' of type '%s' specified in summary, "
				  "but only boolean, string and string list field types are supported"),
			     e_contact_pretty_name (field), g_type_name (type));
		return FALSE;
	}

	if (type == E_TYPE_CONTACT_ATTR_LIST && have_attr_list)
		*have_attr_list = TRUE;

	new_field.field  = field;
	new_field.dbname = dbname;
	new_field.type   = type;
	g_array_append_val (array, new_field);

	return TRUE;
}

static void
summary_fields_add_indexes (GArray         *array,
			    EContactField  *indexes,
			    EBookIndexType *index_types,
			    gint            n_indexes,
			    gboolean       *have_attr_list_prefix,
			    gboolean       *have_attr_list_suffix)
{
	gint i, j;

	for (i = 0; i < array->len; i++) {
		SummaryField *sfield = &g_array_index (array, SummaryField, i);

		for (j = 0; j < n_indexes; j++) {
			if (sfield->field == indexes[j]) {
				switch (index_types[j]) {
				case E_BOOK_INDEX_PREFIX:
					sfield->index |= INDEX_PREFIX;

					if (sfield->type == E_TYPE_CONTACT_ATTR_LIST)
						*have_attr_list_prefix = TRUE;
					break;
				case E_BOOK_INDEX_SUFFIX:
					sfield->index |= INDEX_SUFFIX;

					if (sfield->type == E_TYPE_CONTACT_ATTR_LIST)
						*have_attr_list_suffix = TRUE;
					break;
				default:
					g_warn_if_reached ();
					break;
				}
			}
		}
	}
}


/**
 * e_book_backend_sqlitedb_new_full:
 * @path: location where the db would be created
 * @emailid: email id of the user
 * @folderid: folder id of the address-book
 * @folder_name: name of the address-book
 * @store_vcard: True if the vcard should be stored inside db, if FALSE only the summary fields would be stored inside db.
 * @setup: an #ESourceBackendSummarySetup describing how the summary should be setup
 * @error: A location to store any error that may have occurred
 *
 * Like e_book_backend_sqlitedb_new(), but allows configuration of which contact fields
 * will be stored for quick reference in the summary. The configuration indicated by
 * @setup will only be taken into account when initially creating the underlying table,
 * further configurations will be ignored.
 *
 * The fields %E_CONTACT_UID and %E_CONTACT_REV are not optional,
 * they will be stored in the summary regardless of this function's parameters
 *
 * <note><para>Only #EContactFields with the type #G_TYPE_STRING, #G_TYPE_BOOLEAN or
 * #E_TYPE_CONTACT_ATTR_LIST are currently supported.</para></note>
 *
 * Returns: (transfer full): The newly created #EBookBackendSqliteDB
 *
 * Since: 3.8
 **/
EBookBackendSqliteDB *
e_book_backend_sqlitedb_new_full (const gchar *path,
				  const gchar *emailid,
				  const gchar *folderid,
				  const gchar *folder_name,
				  gboolean store_vcard,
				  ESourceBackendSummarySetup *setup,
				  GError **error)
{
	EBookBackendSqliteDB *ebsdb = NULL;
	EContactField *fields;
	EContactField *indexed_fields;
	EBookIndexType *index_types = NULL;
	gboolean have_attr_list = FALSE;
	gboolean have_attr_list_prefix = FALSE;
	gboolean have_attr_list_suffix = FALSE;
	gboolean had_error = FALSE;
	GArray *summary_fields;
	gint n_fields = 0, n_indexed_fields = 0, i;

	fields         = e_source_backend_summary_setup_get_summary_fields (setup, &n_fields);
	indexed_fields = e_source_backend_summary_setup_get_indexed_fields (setup, &index_types, &n_indexed_fields);

	/* No specified summary fields indicates the default summary configuration should be used */
	if (n_fields <= 0) {
		ebsdb = e_book_backend_sqlitedb_new (path, emailid, folderid, folder_name, store_vcard, error);
		g_free (fields);
		g_free (index_types);
		g_free (indexed_fields);
		
		return ebsdb;
	}

	summary_fields = g_array_new (FALSE, FALSE, sizeof (SummaryField));

	/* Ensure the non-optional fields first */
	append_summary_field (summary_fields, E_CONTACT_UID, &have_attr_list, error);
	append_summary_field (summary_fields, E_CONTACT_REV, &have_attr_list, error);

	for (i = 0; i < n_fields; i++) {
		if (!append_summary_field (summary_fields, fields[i], &have_attr_list, error)) {
			had_error = TRUE;
			break;
		}
	}

	if (had_error) {
		g_array_free (summary_fields, TRUE);
		g_free (fields);
		g_free (index_types);
		g_free (indexed_fields);
		return NULL;
	}

	/* Add the 'indexed' flag to the SummaryField structs */
	summary_fields_add_indexes (summary_fields, indexed_fields, index_types, n_indexed_fields,
				    &have_attr_list_prefix, &have_attr_list_suffix);

	ebsdb = e_book_backend_sqlitedb_new_internal (path, emailid, folderid, folder_name,
						      store_vcard,
						      (SummaryField *) summary_fields->data,
						      summary_fields->len,
						      have_attr_list,
						      have_attr_list_prefix,
						      have_attr_list_suffix,
						      error);

	g_free (fields);
	g_free (index_types);
	g_free (indexed_fields);
	g_array_free (summary_fields, FALSE);

	return ebsdb;
}



/**
 * e_book_backend_sqlitedb_new
 * @path: location where the db would be created
 * @emailid: email id of the user
 * @folderid: folder id of the address-book
 * @folder_name: name of the address-book
 * @store_vcard: True if the vcard should be stored inside db, if FALSE only the summary fields would be stored inside db.
 * @error:
 *
 * If the path for multiple addressbooks are same, the contacts from all addressbooks
 * would be stored in same db in different tables.
 *
 * Returns:
 *
 * Since: 3.2
 **/
EBookBackendSqliteDB *
e_book_backend_sqlitedb_new (const gchar *path,
                             const gchar *emailid,
                             const gchar *folderid,
                             const gchar *folder_name,
                             gboolean store_vcard,
                             GError **error)
{
	EBookBackendSqliteDB *ebsdb;
	GArray *summary_fields;
	gboolean have_attr_list = FALSE;
	gboolean have_attr_list_prefix = FALSE;
	gboolean have_attr_list_suffix = FALSE;
	gint i;

	/* Create the default summary structs */
	summary_fields = g_array_new (FALSE, FALSE, sizeof (SummaryField));
	for (i = 0; i < G_N_ELEMENTS (default_summary_fields); i++)
		append_summary_field (summary_fields, default_summary_fields[i], &have_attr_list, NULL);

	/* Add the default index flags */
	summary_fields_add_indexes (summary_fields,
				    default_indexed_fields,
				    default_index_types,
				    G_N_ELEMENTS (default_indexed_fields),
				    &have_attr_list_prefix, &have_attr_list_suffix);

	ebsdb = e_book_backend_sqlitedb_new_internal (path, emailid, folderid, folder_name,
						      store_vcard,
						      (SummaryField *) summary_fields->data,
						      summary_fields->len,
						      have_attr_list,
						      have_attr_list_prefix,
						      have_attr_list_suffix,
						      error);
	g_array_free (summary_fields, FALSE);

	return ebsdb;
}

gboolean
e_book_backend_sqlitedb_lock_updates (EBookBackendSqliteDB *ebsdb,
                                      GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);

	return book_backend_sqlitedb_start_transaction (ebsdb, error);
}

gboolean
e_book_backend_sqlitedb_unlock_updates (EBookBackendSqliteDB *ebsdb,
                                        gboolean do_commit,
                                        GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);

	return do_commit ?
		book_backend_sqlitedb_commit_transaction (ebsdb, error) :
		book_backend_sqlitedb_rollback_transaction (ebsdb, error);
}

/* Add Contact (free the result with g_free() ) */
static gchar *
insert_stmt_from_contact (EBookBackendSqliteDB *ebsdb,
			  EContact *contact,
                          gboolean partial_content,
                          const gchar *folderid,
                          gboolean store_vcard)
{
	GString *string;
	gchar *str, *vcard_str;
	gint i;

	str = sqlite3_mprintf ("INSERT or REPLACE INTO %Q VALUES (", folderid);
	string = g_string_new (str);
	sqlite3_free (str);

	for (i = 0; i < ebsdb->priv->n_summary_fields; i++) {

		if (ebsdb->priv->summary_fields[i].type == G_TYPE_STRING) {
			gchar *val;
			gchar *normal;

			if (i > 0)
				g_string_append (string, ", ");

			val = e_contact_get (contact, ebsdb->priv->summary_fields[i].field);

			/* Special exception, never normalize the UID or REV string */
			if (ebsdb->priv->summary_fields[i].field != E_CONTACT_UID &&
			    ebsdb->priv->summary_fields[i].field != E_CONTACT_REV)
				normal = e_util_utf8_normalize (val);
			else
				normal = g_strdup (val);

			str = sqlite3_mprintf ("%Q", normal);
			g_string_append (string, str);
			sqlite3_free (str);

			if ((ebsdb->priv->summary_fields[i].index & INDEX_SUFFIX) != 0) {
				gchar *reverse = normal ? g_utf8_strreverse (normal, -1) : NULL;

				str = sqlite3_mprintf ("%Q", reverse);
				g_string_append (string, ", ");
				g_string_append (string, str);
				sqlite3_free (str);
				g_free (reverse);
			}

			g_free (normal);
			g_free (val);

		} else if (ebsdb->priv->summary_fields[i].type == G_TYPE_BOOLEAN) {
			gboolean val;

			if (i > 0)
				g_string_append (string, ", ");

			val = e_contact_get (contact, ebsdb->priv->summary_fields[i].field) ? TRUE : FALSE;
			g_string_append_printf (string, "%d", val ? 1 : 0);

		} else if (ebsdb->priv->summary_fields[i].type != E_TYPE_CONTACT_ATTR_LIST)
			g_warn_if_reached ();
	}

	vcard_str = store_vcard ? e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30) : NULL;
	str = sqlite3_mprintf (", %Q, %Q)", vcard_str, NULL);

	g_string_append (string, str);

	sqlite3_free (str);
	g_free (vcard_str);

	return g_string_free (string, FALSE);
}

static gboolean
insert_contact (EBookBackendSqliteDB *ebsdb,
		EContact *contact,
		gboolean partial_content,
		const gchar *folderid,
		GError **error)
{
	EBookBackendSqliteDBPrivate *priv;
	gboolean success;
	gchar *stmt;

	priv = ebsdb->priv;

	/* Update main summary table */
	stmt = insert_stmt_from_contact (ebsdb, contact, partial_content, folderid, priv->store_vcard);
	success = book_backend_sql_exec (priv->db, stmt, NULL, NULL, error);
	g_free (stmt);

	/* Update attribute list table */
	if (success && priv->have_attr_list) {
		gchar *list_folder = g_strdup_printf ("%s_lists", folderid);
		gchar *uid;
		gint   i;
		GList *values, *l;

		/* First remove all entries for this UID */
		uid = e_contact_get (contact, E_CONTACT_UID);
		stmt = sqlite3_mprintf ("DELETE FROM %Q WHERE uid = %Q", list_folder, uid);
		success = book_backend_sql_exec (priv->db, stmt, NULL, NULL, error);
		sqlite3_free (stmt);

		for (i = 0; success && i < priv->n_summary_fields; i++) {

			if (priv->summary_fields[i].type != E_TYPE_CONTACT_ATTR_LIST)
				continue;

			values = e_contact_get (contact, priv->summary_fields[i].field);

			for (l = values; success && l != NULL; l = l->next) {
				gchar *value = (gchar *)l->data;
				gchar *normal = e_util_utf8_normalize (value);

				if (priv->have_attr_list_suffix) {
					gchar *reverse = normal ? g_utf8_strreverse (normal, -1) : NULL;

					stmt = sqlite3_mprintf ("INSERT INTO %Q (uid, field, value, value_reverse) "
								"VALUES (%Q, %Q, %Q, %Q)",
								list_folder, uid,
								priv->summary_fields[i].dbname,
								normal, reverse);

					g_free (reverse);
				} else {
					stmt = sqlite3_mprintf ("INSERT INTO %Q (uid, field, value) "
								"VALUES (%Q, %Q, %Q)",
								list_folder, uid,
								priv->summary_fields[i].dbname,
								normal);
				}

				success = book_backend_sql_exec (priv->db, stmt, NULL, NULL, error);
				sqlite3_free (stmt);
				g_free (normal);
			}

			/* Free the list of allocated strings */
			e_contact_attr_list_free (values);
		}

		g_free (list_folder);
		g_free (uid);
	}

	return success;
}


/**
 * e_book_backend_sqlitedb_add_contact
 * @ebsdb:
 * @folderid: folder id
 * @contact: EContact to be added
 * @partial_content: contact does not contain full information. Used when
 * the backend cache's partial information for auto-completion.
 * @error:
 *
 * This is a convenience wrapper for e_book_backend_sqlitedb_add_contacts,
 * which is the preferred means to add multiple contacts when possible.
 *
 * Returns: TRUE on success.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_add_contact (EBookBackendSqliteDB *ebsdb,
                                     const gchar *folderid,
                                     EContact *contact,
                                     gboolean partial_content,
                                     GError **error)
{
	GSList l;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	l.data = contact;
	l.next = NULL;

	return e_book_backend_sqlitedb_add_contacts (
		ebsdb, folderid, &l,
		partial_content, error);
}

/**
 * e_book_backend_sqlitedb_add_contacts
 * @ebsdb:
 * @folderid: folder id
 * @contacts: list of EContacts
 * @partial_content: contact does not contain full information. Used when
 * the backend cache's partial information for auto-completion.
 * @error:
 *
 *
 * Returns: TRUE on success.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_add_contacts (EBookBackendSqliteDB *ebsdb,
                                      const gchar *folderid,
                                      GSList *contacts,
                                      gboolean partial_content,
                                      GError **error)
{
	GSList *l;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);
	g_return_val_if_fail (contacts != NULL, FALSE);

	success = book_backend_sqlitedb_start_transaction (ebsdb, error);

	for (l = contacts; success && l != NULL; l = g_slist_next (l)) {
		EContact *contact = (EContact *) l->data;

		success = insert_contact (ebsdb, contact, partial_content, folderid, error);
	}

	if (success)
		return book_backend_sqlitedb_commit_transaction (ebsdb, error);

	/* The GError is already set. */
	book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	return FALSE;
}

/**
 * e_book_backend_sqlitedb_remove_contact:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_remove_contact (EBookBackendSqliteDB *ebsdb,
                                        const gchar *folderid,
                                        const gchar *uid,
                                        GError **error)
{
	GSList l;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	l.data = (gchar *) uid; /* Won't modify it, I promise :) */
	l.next = NULL;

	return e_book_backend_sqlitedb_remove_contacts (
		ebsdb, folderid, &l, error);
}

static gchar *
generate_uid_list_for_stmt (GSList *uids)
{
	GString *str = g_string_new (NULL);
	GSList  *l;
	gboolean first = TRUE;

	for (l = uids; l; l = l->next) {
		gchar *uid = (gchar *) l->data;
		gchar *tmp;

		/* First uid with no comma */
		if (!first)
			g_string_append_printf (str, ", ");
		else
			first = FALSE;

		tmp = sqlite3_mprintf ("%Q", uid);
		g_string_append (str, tmp);
		sqlite3_free (tmp);
	}

	return g_string_free (str, FALSE);
}

static gchar *
generate_delete_stmt (const gchar *table, GSList *uids)
{
	GString *str = g_string_new (NULL);
	gchar *tmp;

	tmp = sqlite3_mprintf ("DELETE FROM %Q WHERE uid IN (", table);
	g_string_append (str, tmp);
	sqlite3_free (tmp);

	tmp = generate_uid_list_for_stmt (uids);
	g_string_append (str, tmp);
	g_free (tmp);
	g_string_append_c (str, ')');

	return g_string_free (str, FALSE);	
}

/**
 * e_book_backend_sqlitedb_remove_contacts:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_remove_contacts (EBookBackendSqliteDB *ebsdb,
                                         const gchar *folderid,
                                         GSList *uids,
                                         GError **error)
{
	gboolean success;
	gchar *stmt;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

 	success = book_backend_sqlitedb_start_transaction (ebsdb, error);

	/* Delete the auxillary contact infos first */
 	if (success && ebsdb->priv->have_attr_list) {
 		gchar *lists_folder = g_strdup_printf ("%s_lists", folderid);

 		stmt = generate_delete_stmt (lists_folder, uids);
 		g_free (lists_folder);

 		success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
 		g_free (stmt);
 	}

	if (success) {
		stmt = generate_delete_stmt (folderid, uids);
		success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
		g_free (stmt);
	}

	if (success)
		return book_backend_sqlitedb_commit_transaction (ebsdb, error);

	/* The GError is already set. */
	book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	return FALSE;
}

struct _contact_info {
	gboolean exists;
	gboolean partial_content;
};

static gint
contact_found_cb (gpointer ref,
                  gint col,
                  gchar **cols,
                  gchar **name)
{
	struct _contact_info *cinfo = ref;

	cinfo->exists = TRUE;
	cinfo->partial_content = cols[0] ? strtoul (cols[0], NULL, 10) : 0;

	return 0;
}

/**
 * e_book_backend_sqlitedb_has_contact:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_has_contact (EBookBackendSqliteDB *ebsdb,
                                     const gchar *folderid,
                                     const gchar *uid,
                                     gboolean *partial_content,
                                     GError **error)
{
	struct _contact_info cinfo;
	gboolean success;
	gchar *stmt;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	cinfo.exists = FALSE;
	cinfo.partial_content = FALSE;

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf (
		"SELECT partial_content FROM %Q WHERE uid = %Q",
		folderid, uid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, contact_found_cb , &cinfo, error);
	sqlite3_free (stmt);

	if (success)
		*partial_content = cinfo.partial_content;

	READER_UNLOCK (ebsdb);

	/* FIXME Returning FALSE can mean either "contact not found" or
	 *       "error occurred".  Add a boolean (out) "exists" parameter. */
	return success && cinfo.exists;
}

static gint
get_vcard_cb (gpointer ref,
              gint col,
              gchar **cols,
              gchar **name)
{
	gchar **vcard_str = ref;

	if (cols[0])
		*vcard_str = g_strdup (cols [0]);

	return 0;
}

/**
 * e_book_backend_sqlitedb_get_contact:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
EContact *
e_book_backend_sqlitedb_get_contact (EBookBackendSqliteDB *ebsdb,
                                     const gchar *folderid,
                                     const gchar *uid,
                                     GHashTable *fields_of_interest,
                                     gboolean *with_all_required_fields,
                                     GError **error)
{
	EContact *contact = NULL;
	gchar *vcard;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), NULL);
	g_return_val_if_fail (folderid != NULL, NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	vcard = e_book_backend_sqlitedb_get_vcard_string (
		ebsdb, folderid, uid,
		fields_of_interest, with_all_required_fields, error);

	if (vcard != NULL) {
		contact = e_contact_new_from_vcard_with_uid (vcard, uid);
		g_free (vcard);
	}

	return contact;
}

/**
 * e_book_backend_sqlitedb_is_summary_fields:
 * @fields_of_interest: A hash table containing the fields of interest
 * 
 * This only checks if all the fields are part of the default summary fields,
 * not part of the configured summary fields.
 *
 * Since: 3.2
 *
 * Deprecated: 3.8: Use e_book_backend_sqlitedb_check_summary_fields() instead.
 **/
gboolean
e_book_backend_sqlitedb_is_summary_fields (GHashTable *fields_of_interest)
{
	gboolean summary_fields = TRUE;
	GHashTableIter iter;
	gpointer key, value;
	gint     i;

	if (!fields_of_interest)
		return FALSE;

	g_hash_table_iter_init (&iter, fields_of_interest);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar  *field_name = key;
		EContactField field      = e_contact_field_id (field_name);
		gboolean      found      = FALSE;

		for (i = 0; i < G_N_ELEMENTS (default_summary_fields); i++) {
			if (field == default_summary_fields[i]) {
				found = TRUE;
				break;
			}
		}

		if (!found) {
			summary_fields = FALSE;
			break;
		}
	}

	return summary_fields;
}

/**
 * e_book_backend_sqlitedb_check_summary_fields:
 * @ebsdb: An #EBookBackendSqliteDB
 * @fields_of_interest: A hash table containing the fields of interest
 * 
 * Checks if all the specified fields are part of the configured summary
 * fields for @ebsdb
 *
 * Since: 3.8
 **/
gboolean
e_book_backend_sqlitedb_check_summary_fields (EBookBackendSqliteDB *ebsdb,
					      GHashTable *fields_of_interest)
{
	gboolean summary_fields = TRUE;
	GHashTableIter iter;
	gpointer key, value;

	if (!fields_of_interest)
		return FALSE;

	g_hash_table_iter_init (&iter, fields_of_interest);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar  *field_name = key;
		EContactField field      = e_contact_field_id (field_name);

		if (summary_dbname_from_field (ebsdb, field) == NULL) {
			summary_fields = FALSE;
			break;
		}
	}

	return summary_fields;
}

/**
 * e_book_backend_sqlitedb_get_vcard_string:
 * @ebsdb: An #EBookBackendSqliteDB
 * @folderid: The folder id
 * @uid: The uid to fetch a vcard for
 * @fields_of_interest: The required fields for this vcard, or %NULL to require all fields.
 * @with_all_required_fields: (allow none) (out): Whether all the required fields are present in the returned vcard.
 * @error: A location to store any error that may have occurred.
 *
 * Searches @ebsdb in the context of @folderid for @uid.
 *
 * If @ebsdb is configured to store the whole vcards, the whole vcard will be returned.
 * Otherwise the summary cache will be searched and the virtual vcard will be built
 * from the summary cache.
 *
 * In either case, @with_all_required_fields if specified, will be updated to reflect whether
 * the returned vcard string satisfies the passed 'fields_of_interest' parameter.
 * 
 * Returns: (transfer full): The vcard string for @uid or %NULL if @uid was not found.
 *
 * Since: 3.2
 */
gchar *
e_book_backend_sqlitedb_get_vcard_string (EBookBackendSqliteDB *ebsdb,
                                          const gchar *folderid,
                                          const gchar *uid,
                                          GHashTable *fields_of_interest,
                                          gboolean *with_all_required_fields,
                                          GError **error)
{
	gchar *stmt;
	gchar *vcard_str = NULL;
	gboolean local_with_all_required_fields = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), NULL);
	g_return_val_if_fail (folderid != NULL, NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	READER_LOCK (ebsdb);

	if (!ebsdb->priv->store_vcard) {
 		g_set_error (error, E_BOOK_SDB_ERROR,
 			     0, _("Full search_contacts are not stored in cache. vcards cannot be returned."));

	} else {
		stmt = sqlite3_mprintf (
			"SELECT vcard FROM %Q WHERE uid = %Q", folderid, uid);
		book_backend_sql_exec (
			ebsdb->priv->db, stmt,
			get_vcard_cb , &vcard_str, error);
		sqlite3_free (stmt);

		local_with_all_required_fields = TRUE;
	}

	READER_UNLOCK (ebsdb);

	if (with_all_required_fields)
		*with_all_required_fields = local_with_all_required_fields;

	/* Is is an error to not find a contact ?? */
	if (!vcard_str && error && !*error)
		g_set_error (
			error, E_BOOK_SDB_ERROR, 0,
			_("Contact '%s' not found"), uid ? uid : "NULL");

	return vcard_str;
}


enum {
	CHECK_IS_SUMMARY   = (1 << 0),
	CHECK_IS_LIST_ATTR = (1 << 1),
};

static ESExpResult *
func_check_subset (ESExp *f,
		   gint argc,
		   struct _ESExpTerm **argv,
		   gpointer data)
{
	ESExpResult *r, *r1;
	gboolean one_non_summary_query = FALSE;
	gint result = 0;
	gint i;

	for (i = 0; i < argc; i++) {
		r1 = e_sexp_term_eval (f, argv[i]);

		if (r1->type != ESEXP_RES_INT) {
			e_sexp_result_free (f, r1);
			continue;
		}

		result |= r1->value.number;

		if ((r1->value.number & CHECK_IS_SUMMARY) == 0)
			one_non_summary_query = TRUE;

		e_sexp_result_free (f, r1);
	}

	/* If at least one subset is not a summary query,
	 * then the whole query is not a summary query and
	 * thus cannot be done with an SQL statement
	 */
	if (one_non_summary_query)
		result = 0;

	r = e_sexp_result_new (f, ESEXP_RES_INT);
	r->value.number = result;

	return r;
}

static ESExpResult *
func_check (struct _ESExp *f,
            gint argc,
            struct _ESExpResult **argv,
            gpointer data)
{
	EBookBackendSqliteDB *ebsdb = data;
	ESExpResult *r;
	gint ret_val = 0;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		const gchar *query_name = argv[0]->value.string;
		const gchar *query_value = argv[1]->value.string;
		gint i;

		/* Special case, when testing the special symbolic 'any field' we can
		 * consider it a summary query (it's similar to a 'no query'). */
		if (g_strcmp0 (query_name, "x-evolution-any-field") == 0 &&
		    g_strcmp0 (query_value, "") == 0) {
			ret_val |= CHECK_IS_SUMMARY;
			goto check_finish;
		}

		if (ebsdb) {
			for (i = 0; i < ebsdb->priv->n_summary_fields; i++) {
				if (!strcmp (e_contact_field_name (ebsdb->priv->summary_fields[i].field), query_name)) {
					ret_val |= CHECK_IS_SUMMARY;

					if (ebsdb->priv->summary_fields[i].type == E_TYPE_CONTACT_ATTR_LIST)
						ret_val |= CHECK_IS_LIST_ATTR;
				}
			}
		} else {
			for (i = 0; i < G_N_ELEMENTS (default_summary_fields); i++) {

				if (!strcmp (e_contact_field_name (default_summary_fields[i]), query_name)) {
					ret_val |= CHECK_IS_SUMMARY;

					if (e_contact_field_type (default_summary_fields[i]) == E_TYPE_CONTACT_ATTR_LIST)
						ret_val |= CHECK_IS_LIST_ATTR;
				}
			}
		}
	}

 check_finish:

	r = e_sexp_result_new (f, ESEXP_RES_INT);
	r->value.number = ret_val;

	return r;
}

/* 'builtin' functions */
static const struct {
	const gchar *name;
	ESExpFunc *func;
	gint type;		/* set to 1 if a function can perform shortcut evaluation, or
				   doesn't execute everything, 0 otherwise */
} check_symbols[] = {
	{ "and", (ESExpFunc *) func_check_subset, 1},
	{ "or", (ESExpFunc *) func_check_subset, 1},

	{ "contains", func_check, 0 },
	{ "is", func_check, 0 },
	{ "beginswith", func_check, 0 },
	{ "endswith", func_check, 0 },
	{ "exists", func_check, 0 }
};

/**
 * e_book_backend_sqlitedb_check_summary_query:
 * @ebsdb: an #EBookBackendSqliteDB
 * @query: the query to check
 * @with_list_attrs: Return location to store whether the query touches upon list attributes
 *
 * Checks whether @query contains only checks for the summary fields
 * configured in @ebsdb
 *
 * Since: 3.8
 **/
gboolean
e_book_backend_sqlitedb_check_summary_query (EBookBackendSqliteDB *ebsdb,
					     const gchar *query,
					     gboolean *with_list_attrs)
{
	ESExp *sexp;
	ESExpResult *r;
	gboolean retval = FALSE;
	gint i;
	gint esexp_error;

	g_return_val_if_fail (query != NULL, FALSE);
	g_return_val_if_fail (*query != '\0', FALSE);

	sexp = e_sexp_new ();

	for (i = 0; i < G_N_ELEMENTS (check_symbols); i++) {
		if (check_symbols[i].type == 1) {
			e_sexp_add_ifunction (
			        sexp, 0, check_symbols[i].name,
				(ESExpIFunc *) check_symbols[i].func, ebsdb);
		} else {
			e_sexp_add_function (
				sexp, 0, check_symbols[i].name,
				check_symbols[i].func, ebsdb);
		}
	}

	e_sexp_input_text (sexp, query, strlen (query));
	esexp_error = e_sexp_parse (sexp);

	if (esexp_error == -1) {
		return FALSE;
	}

	r = e_sexp_eval (sexp);
	if (r && r->type == ESEXP_RES_INT) {
		retval = (r->value.number & CHECK_IS_SUMMARY) != 0;

		if ((r->value.number & CHECK_IS_LIST_ATTR) != 0 && with_list_attrs)
			*with_list_attrs = TRUE;
	}

	e_sexp_result_free (sexp, r);
	e_sexp_unref (sexp);

	return retval;
}


/**
 * e_book_backend_sqlitedb_is_summary_query:
 *
 * Checks whether the query contains only checks for the default summary fields
 *
 * Since: 3.2
 *
 * Deprecated: 3.8: Use e_book_backend_sqlitedb_check_summary_query() instead
 **/
gboolean
e_book_backend_sqlitedb_is_summary_query (const gchar *query)
{
	return e_book_backend_sqlitedb_check_summary_query (NULL, query, NULL);
}

static ESExpResult *
func_and (ESExp *f,
          gint argc,
          struct _ESExpTerm **argv,
          gpointer data)
{
	ESExpResult *r, *r1;
	GString *string;
	gint i;

	string = g_string_new ("( ");
	for (i = 0; i < argc; i++) {
		r1 = e_sexp_term_eval (f, argv[i]);

		if (r1->type != ESEXP_RES_STRING) {
			e_sexp_result_free (f, r1);
			continue;
		}
		if (r1->value.string && *r1->value.string)
			g_string_append_printf (string, "%s%s", r1->value.string, ((argc > 1) && (i != argc - 1)) ? " AND ":"");
		e_sexp_result_free (f, r1);
	}
	g_string_append (string, " )");
	r = e_sexp_result_new (f, ESEXP_RES_STRING);

	if (strlen (string->str) == 4) {
		r->value.string = g_strdup ("");
		g_string_free (string, TRUE);
	} else {
		r->value.string = g_string_free (string, FALSE);
	}

	return r;
}

static ESExpResult *
func_or (ESExp *f,
         gint argc,
         struct _ESExpTerm **argv,
         gpointer data)
{
	ESExpResult *r, *r1;
	GString *string;
	gint i;

	string = g_string_new ("( ");
	for (i = 0; i < argc; i++) {
		r1 = e_sexp_term_eval (f, argv[i]);

		if (r1->type != ESEXP_RES_STRING) {
			e_sexp_result_free (f, r1);
			continue;
		}
		if (r1->value.string && *r1->value.string)
			g_string_append_printf (string, "%s%s", r1->value.string, ((argc > 1) && (i != argc - 1)) ? " OR ":"");
		e_sexp_result_free (f, r1);
	}
	g_string_append (string, " )");

	r = e_sexp_result_new (f, ESEXP_RES_STRING);
	if (strlen (string->str) == 4) {
		r->value.string = g_strdup ("");
		g_string_free (string, TRUE);
	} else {
		r->value.string = g_string_free (string, FALSE);
	}

	return r;
}

typedef enum {
	MATCH_CONTAINS,
	MATCH_IS,
	MATCH_BEGINS_WITH,
	MATCH_ENDS_WITH
} match_type;

static gchar *
convert_string_value (const gchar *value,
		      gboolean normalize,
		      gboolean reverse,
                      match_type match)
{
	GString *str;
	size_t len;
	gchar c;
	gboolean escape_modifier_needed = FALSE;
	const gchar *escape_modifier = " ESCAPE '^'";
	gchar *reverse_val = NULL;
	gchar *normal;
	const gchar *ptr;

	g_return_val_if_fail (value != NULL, NULL);

	if (normalize)
		normal = e_util_utf8_normalize (value);
	else
		normal = g_strdup (value);

	/* Just assume each character must be escaped. The result of this function
	 * is discarded shortly after calling this function. Therefore it's
	 * acceptable to possibly allocate twice the memory needed.
	 */
	len = strlen (normal);
	str = g_string_sized_new (2 * len + 4 + strlen (escape_modifier) - 1);
	g_string_append_c (str, '\'');

	switch (match) {
	case MATCH_CONTAINS:
	case MATCH_ENDS_WITH:
		g_string_append_c (str, '%');
		break;

	case MATCH_BEGINS_WITH:
	case MATCH_IS:
		break;
	}

	if (reverse) {
		reverse_val = g_utf8_strreverse (normal, -1);
		ptr = reverse_val;
	} else {
		ptr = normal;
	}

	while ((c = *ptr++)) {
		if (c == '\'') {
			g_string_append_c (str, '\'');
		} else if (c == '%' || c == '^') {
			g_string_append_c (str, '^');
			escape_modifier_needed = TRUE;
		}

		g_string_append_c (str, c);
	}

	switch (match) {
	case MATCH_CONTAINS:
	case MATCH_BEGINS_WITH:
		g_string_append_c (str, '%');
		break;

	case MATCH_ENDS_WITH:
	case MATCH_IS:
		break;
	}

	g_string_append_c (str, '\'');

	if (escape_modifier_needed)
		g_string_append (str, escape_modifier);

	g_free (reverse_val);
	g_free (normal);

	return g_string_free (str, FALSE);
}

static gchar *
field_name_and_query_term (EBookBackendSqliteDB *ebsdb,
			   const gchar          *folderid,
			   const gchar          *field_name_input,
			   const gchar          *query_term_input,
			   match_type            match,
			   gboolean             *is_list_attr,
			   gchar               **query_term)
{
	gint summary_index;
	gchar *field_name = NULL;
	gchar *value = NULL;
	gboolean list_attr = FALSE;

	summary_index = summary_index_from_field_name (ebsdb, field_name_input);

	if (summary_index < 0) {
		g_critical ("Only summary field matches should be converted to sql queries");
		field_name = g_strconcat (folderid, ".", field_name_input, NULL);
		value = convert_string_value (query_term_input, TRUE, FALSE, match);
	} else {
		gboolean suffix_search = FALSE;

		/* If its a suffix search and we have reverse data to search... */
		if (match == MATCH_ENDS_WITH &&
		    (ebsdb->priv->summary_fields[summary_index].index & INDEX_SUFFIX) != 0)
			suffix_search = TRUE;

		/* Or also if its an exact match, and we *only* have reverse data which is indexed,
		 * then prefer the indexed reverse search. */
		else if (match == MATCH_IS && 
			 (ebsdb->priv->summary_fields[summary_index].index & INDEX_SUFFIX) != 0 &&
			 (ebsdb->priv->summary_fields[summary_index].index & INDEX_PREFIX) == 0)
			suffix_search = TRUE;

		if (suffix_search) {
			/* Special case for suffix matching:
			 *  o Reverse the string
			 *  o Check the reversed column instead
			 *  o Make it a prefix search
			 */
			if (ebsdb->priv->summary_fields[summary_index].type == E_TYPE_CONTACT_ATTR_LIST) {
				field_name = g_strdup ("multi.value_reverse");
				list_attr = TRUE;
			} else
				field_name = g_strconcat ("summary.", 
							  ebsdb->priv->summary_fields[summary_index].dbname,
							  "_reverse", NULL);

			if (ebsdb->priv->summary_fields[summary_index].field == E_CONTACT_UID ||
			    ebsdb->priv->summary_fields[summary_index].field == E_CONTACT_REV)
				value = convert_string_value (query_term_input, FALSE, TRUE,
							      (match == MATCH_ENDS_WITH) ? MATCH_BEGINS_WITH : MATCH_IS);
			else
				value = convert_string_value (query_term_input, TRUE, TRUE,
							      (match == MATCH_ENDS_WITH) ? MATCH_BEGINS_WITH : MATCH_IS);
		} else {

			if (ebsdb->priv->summary_fields[summary_index].type == E_TYPE_CONTACT_ATTR_LIST) {
				field_name = g_strdup ("multi.value");
				list_attr = TRUE;
			} else
				field_name = g_strconcat ("summary.",
							  ebsdb->priv->summary_fields[summary_index].dbname, NULL);

			if (ebsdb->priv->summary_fields[summary_index].field == E_CONTACT_UID ||
			    ebsdb->priv->summary_fields[summary_index].field == E_CONTACT_REV)
				value = convert_string_value (query_term_input, FALSE, FALSE, match);
			else
				value = convert_string_value (query_term_input, TRUE, FALSE, match);
		}
	}

	if (is_list_attr)
		*is_list_attr = list_attr;

	*query_term = value;

	return field_name;
}

typedef struct {
	EBookBackendSqliteDB *ebsdb;
	const gchar          *folderid;
} BuildQueryData;

static ESExpResult *
convert_match_exp (struct _ESExp *f,
                   gint argc,
                   struct _ESExpResult **argv,
                   gpointer data,
                   match_type match)
{
	BuildQueryData *qdata = (BuildQueryData *)data;
	EBookBackendSqliteDB *ebsdb = qdata->ebsdb;
	ESExpResult *r;
	gchar *str = NULL;

	/* are we inside a match-all? */
	if (argc > 1 && argv[0]->type == ESEXP_RES_STRING) {
		const gchar *field;

		/* only a subset of headers are supported .. */
		field = argv[0]->value.string;

		if (argv[1]->type == ESEXP_RES_STRING && argv[1]->value.string[0] != 0) {
			const gchar *oper = "LIKE";
			gchar *field_name, *query_term;

			if (match == MATCH_IS)
				oper = "=";

			if (!strcmp (field, "full_name")) {
				GString *names = g_string_new (NULL);

				field_name = field_name_and_query_term (ebsdb, qdata->folderid, "full_name",
									argv[1]->value.string,
									match, NULL, &query_term);
				g_string_append_printf (names, "(%s IS NOT NULL AND %s %s %s)",
							field_name, field_name, oper, query_term);
				g_free (field_name);
				g_free (query_term);

				if (summary_dbname_from_field (ebsdb, E_CONTACT_FAMILY_NAME)) {

					field_name = field_name_and_query_term (ebsdb, qdata->folderid, "family_name",
										argv[1]->value.string,
										match, NULL, &query_term);
					g_string_append_printf
						(names, " OR (%s IS NOT NULL AND %s %s %s)",
						 field_name, field_name, oper, query_term);
					g_free (field_name);
					g_free (query_term);
				}

				if (summary_dbname_from_field (ebsdb, E_CONTACT_GIVEN_NAME)) {

					field_name = field_name_and_query_term (ebsdb, qdata->folderid, "given_name",
										argv[1]->value.string,
										match, NULL, &query_term);
					g_string_append_printf
						(names, " OR (%s IS NOT NULL AND %s %s %s)",
						 field_name, field_name, oper, query_term);
					g_free (field_name);
					g_free (query_term);
				}

				if (summary_dbname_from_field (ebsdb, E_CONTACT_NICKNAME)) {

					field_name = field_name_and_query_term (ebsdb, qdata->folderid, "nickname",
										argv[1]->value.string,
										match, NULL, &query_term);
					g_string_append_printf
						(names, " OR (%s IS NOT NULL AND %s %s %s)",
						 field_name, field_name, oper, query_term);
					g_free (field_name);
					g_free (query_term);
				}

				str = names->str;
				g_string_free (names, FALSE);

			} else {
				gboolean is_list = FALSE;

				/* This should ideally be the only valid case from all the above special casing, but oh well... */
				field_name = field_name_and_query_term (ebsdb, qdata->folderid, field,
									argv[1]->value.string,
									match, &is_list, &query_term);

				if (is_list) {
					gchar *tmp;

					tmp = sqlite3_mprintf ("summary.uid = multi.uid AND multi.field = %Q", field);
					str = g_strdup_printf ("(%s AND %s %s %s)",
							       tmp, field_name, oper, query_term);
					sqlite3_free (tmp);
				} else
					str = g_strdup_printf ("(%s IS NOT NULL AND %s %s %s)",
							       field_name, field_name, oper, query_term);

				g_free (field_name);
				g_free (query_term);
			}
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_STRING);
	r->value.string = str;

	return r;
}

static ESExpResult *
func_contains (struct _ESExp *f,
               gint argc,
               struct _ESExpResult **argv,
               gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_CONTAINS);
}

static ESExpResult *
func_is (struct _ESExp *f,
         gint argc,
         struct _ESExpResult **argv,
         gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_IS);
}

static ESExpResult *
func_beginswith (struct _ESExp *f,
                 gint argc,
                 struct _ESExpResult **argv,
                 gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_BEGINS_WITH);
}

static ESExpResult *
func_endswith (struct _ESExp *f,
               gint argc,
               struct _ESExpResult **argv,
               gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_ENDS_WITH);
}

/* 'builtin' functions */
static struct {
	const gchar *name;
	ESExpFunc *func;
	guint immediate :1;
} symbols[] = {
	{ "and", (ESExpFunc *) func_and, 1},
	{ "or", (ESExpFunc *) func_or, 1},

	{ "contains", func_contains, 0 },
	{ "is", func_is, 0 },
	{ "beginswith", func_beginswith, 0 },
	{ "endswith", func_endswith, 0 },
};

static gchar *
sexp_to_sql_query (EBookBackendSqliteDB *ebsdb,
		   const gchar          *folderid,
		   const gchar          *query)
{
	BuildQueryData data = { ebsdb, folderid };
	ESExp *sexp;
	ESExpResult *r;
	gint i;
	gchar *res;

	sexp = e_sexp_new ();

	for (i = 0; i < G_N_ELEMENTS (symbols); i++) {
		if (symbols[i].immediate)
			e_sexp_add_ifunction (
                                sexp, 0, symbols[i].name,
				(ESExpIFunc *) symbols[i].func, &data);
		else
			e_sexp_add_function (
				sexp, 0, symbols[i].name,
				symbols[i].func, &data);
	}

	e_sexp_input_text (sexp, query, strlen (query));
	e_sexp_parse (sexp);

	r = e_sexp_eval (sexp);
	if (!r)
		return NULL;
	if (r->type == ESEXP_RES_STRING) {
		if (r->value.string && *r->value.string)
			res = g_strdup (r->value.string);
		else
			res = NULL;
	} else {
		g_warn_if_reached ();
		res = NULL;
	}

	e_sexp_result_free (sexp, r);
	e_sexp_unref (sexp);

	return res;
}

static gint
addto_vcard_list_cb (gpointer ref,
                     gint col,
                     gchar **cols,
                     gchar **name)
{
	GSList **vcard_data = ref;
	EbSdbSearchData *s_data = g_new0 (EbSdbSearchData, 1);

	if (cols[0])
		s_data->uid = g_strdup (cols[0]);

	if (cols[1])
		s_data->vcard = g_strdup (cols[1]);

	if (cols[2])
		s_data->bdata = g_strdup (cols[2]);

	*vcard_data = g_slist_prepend (*vcard_data, s_data);

	return 0;
}

static gint
addto_slist_cb (gpointer ref,
                gint col,
                gchar **cols,
                gchar **name)
{
	GSList **uids = ref;

	if (cols[0])
		*uids = g_slist_prepend (*uids, g_strdup (cols [0]));

	return 0;
}

static GSList *
book_backend_sqlitedb_search_query (EBookBackendSqliteDB *ebsdb,
                                    const gchar *sql,
                                    const gchar *folderid,
                                    GHashTable *fields_of_interest,
                                    gboolean *with_all_required_fields,
				    gboolean query_with_list_attrs,
                                    GError **error)
{
	GSList *vcard_data = NULL;
	gchar  *stmt;
	gboolean local_with_all_required_fields = FALSE;
	gboolean success = TRUE;

	READER_LOCK (ebsdb);

	if (!ebsdb->priv->store_vcard) {

		g_set_error (error, E_BOOK_SDB_ERROR,
			     0, _("Full search_contacts are not stored in cache. vcards cannot be returned."));

	} else {
		if (sql && sql[0]) {

			if (query_with_list_attrs) {
				gchar *list_table = g_strconcat (folderid, "_lists", NULL);

				stmt = sqlite3_mprintf ("SELECT DISTINCT summary.uid, vcard, bdata "
							"FROM %Q AS summary, %Q AS multi WHERE %s",
							folderid, list_table, sql);
				g_free (list_table);
			} else {
				stmt = sqlite3_mprintf ("SELECT uid, vcard, bdata FROM %Q as summary WHERE %s", folderid, sql);
			}

			success = book_backend_sql_exec (
                                ebsdb->priv->db, stmt,
				addto_vcard_list_cb , &vcard_data, error);

			sqlite3_free (stmt);
		} else {
			stmt = sqlite3_mprintf (
				"SELECT uid, vcard, bdata FROM %Q", folderid);
			success = book_backend_sql_exec (
				ebsdb->priv->db, stmt,
				addto_vcard_list_cb , &vcard_data, error);
			sqlite3_free (stmt);
		}

		local_with_all_required_fields = TRUE;
	}

	READER_UNLOCK (ebsdb);

	if (!success) {
		g_warn_if_fail (vcard_data == NULL);
		return NULL;
	}

	if (with_all_required_fields)
		*with_all_required_fields = local_with_all_required_fields;

	return g_slist_reverse (vcard_data);
}

static GSList *
book_backend_sqlitedb_search_full (EBookBackendSqliteDB *ebsdb,
                                   const gchar *sexp,
                                   const gchar *folderid,
                                   gboolean return_uids,
                                   GError **error)
{
	GSList *r_list = NULL, *all = NULL, *l;
	EBookBackendSExp *bsexp = NULL;
	gboolean success;
	gchar *stmt;

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT uid, vcard, bdata FROM %Q", folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, addto_vcard_list_cb , &all, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	if (!success) {
		g_warn_if_fail (all == NULL);
		return NULL;
	}

	bsexp = e_book_backend_sexp_new (sexp);

	for (l = all; l != NULL; l = g_slist_next (l)) {
		EbSdbSearchData *s_data = (EbSdbSearchData *) l->data;

		if (e_book_backend_sexp_match_vcard (bsexp, s_data->vcard)) {
			if (!return_uids)
				r_list = g_slist_prepend (r_list, s_data);
			else {
				r_list = g_slist_prepend (r_list, g_strdup (s_data->uid));
				e_book_backend_sqlitedb_search_data_free (s_data);
			}
		} else
			e_book_backend_sqlitedb_search_data_free (s_data);
	}

	g_object_unref (bsexp);

	g_slist_free (all);

	return r_list;
}

/**
 * e_book_backend_sqlitedb_search 
 * @ebsdb: 
 * @folderid: 
 * @sexp: search expression; use NULL or an empty string to get all stored
 * contacts.
 * @fields_of_interest: a #GHashTable containing the names of fields to return,
 * or NULL for all.  At the moment if this is non-null, the vcard will be
 * populated with summary fields, else it would return the whole vcard if
 * its stored in the db. [not implemented fully]
 * @searched: (allow none) (out): Whether @ebsdb was capable of searching
 * for the provided query @sexp.
 * @with_all_required_fields: (allow none) (out): Whether all the required
 * fields are present in the returned vcards.
 * @error: 
 *
 * Searching with summary fields is always supported. Search expressions
 * containing any other field is supported only if backend chooses to store
 * the vcard inside the db.
 *
 * Summary fields - uid, rev, nickname, given_name, family_name, file_as
 * email_1, email_2, email_3, email_4, is_list, list_show_addresses, wants_html
 *
 * If @ebsdb was incapable of returning vcards with results that satisfy
 * @fields_of_interest, then @with_all_required_fields will be updated to
 * @FALSE and only uid fields will be present in the returned vcards. This
 * can be useful when a summary query succeeds and the returned list can be
 * used to iterate and fetch for full required data from another persistance.
 *
 * Returns: List of EbSdbSearchData.
 *
 * Since: 3.2
 **/
GSList *
e_book_backend_sqlitedb_search (EBookBackendSqliteDB *ebsdb,
                                const gchar *folderid,
                                const gchar *sexp,
                                GHashTable *fields_of_interest,
                                gboolean *searched,
                                gboolean *with_all_required_fields,
                                GError **error)
{
	GSList *search_contacts = NULL;
	gboolean local_searched = FALSE;
	gboolean local_with_all_required_fields = FALSE;
	gboolean query_with_list_attrs = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), NULL);
	g_return_val_if_fail (folderid != NULL, NULL);

	if (sexp && !*sexp)
		sexp = NULL;

	if (!sexp || e_book_backend_sqlitedb_check_summary_query (ebsdb, sexp,
								  &query_with_list_attrs)) {
		gchar *sql_query;

		sql_query = sexp ? sexp_to_sql_query (ebsdb, folderid, sexp) : NULL;
		search_contacts = book_backend_sqlitedb_search_query (
			ebsdb, sql_query, folderid,
			fields_of_interest,
			&local_with_all_required_fields,
			query_with_list_attrs, error);
		g_free (sql_query);

		local_searched = TRUE;

	} else if (ebsdb->priv->store_vcard) {
		search_contacts = book_backend_sqlitedb_search_full (
			ebsdb, sexp, folderid, FALSE, error);

		local_searched = TRUE;
		local_with_all_required_fields = TRUE;

	} else {
		g_set_error (
			error, E_BOOK_SDB_ERROR, 0,
			_("Full search_contacts are not stored in cache. "
			  "Hence only summary query is supported."));
	}

	if (searched)
		*searched = local_searched;
	if (with_all_required_fields)
		*with_all_required_fields = local_with_all_required_fields;

	return search_contacts;
}

/**
 * e_book_backend_sqlitedb_search_uids:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
GSList *
e_book_backend_sqlitedb_search_uids (EBookBackendSqliteDB *ebsdb,
                                     const gchar *folderid,
                                     const gchar *sexp,
                                     gboolean *searched,
                                     GError **error)
{
	GSList *uids = NULL;
	gboolean local_searched = FALSE;
	gboolean query_with_list_attrs = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), NULL);
	g_return_val_if_fail (folderid != NULL, NULL);

	if (sexp && !*sexp)
		sexp = NULL;

	if (!sexp || e_book_backend_sqlitedb_check_summary_query (ebsdb, sexp, &query_with_list_attrs)) {
		gchar *stmt;
		gchar *sql_query = sexp ? sexp_to_sql_query (ebsdb, folderid, sexp) : NULL;

		READER_LOCK (ebsdb);

		if (sql_query && sql_query[0]) {

			if (query_with_list_attrs) {
				gchar *list_table = g_strconcat (folderid, "_lists", NULL);

				stmt = sqlite3_mprintf ("SELECT DISTINCT summary.uid FROM %Q AS summary, %Q AS multi %s",
							folderid, list_table, sql_query);

				g_free (list_table);
			} else
				stmt = sqlite3_mprintf ("SELECT uid FROM %Q WHERE %s", folderid, sql_query);

			book_backend_sql_exec (ebsdb->priv->db, stmt, addto_slist_cb, &uids, error);
			sqlite3_free (stmt);

		} else {
			stmt = sqlite3_mprintf ("SELECT uid FROM %Q", folderid);
			book_backend_sql_exec (ebsdb->priv->db, stmt, addto_slist_cb, &uids, error);
			sqlite3_free (stmt);
		}

		READER_UNLOCK (ebsdb);

		local_searched = TRUE;

		g_free (sql_query);

	} else if (ebsdb->priv->store_vcard) {
		uids = book_backend_sqlitedb_search_full (
			ebsdb, sexp, folderid, TRUE, error);

		local_searched = TRUE;

	} else {
		g_set_error (
			error, E_BOOK_SDB_ERROR, 0,
			_("Full vcards are not stored in cache. "
			  "Hence only summary query is supported."));
	}

	if (searched)
		*searched = local_searched;

	return uids;
}

static gint
get_uids_and_rev_cb (gpointer user_data,
                     gint col,
                     gchar **cols,
                     gchar **name)
{
	GHashTable *uids_and_rev = user_data;

	if (col == 2 && cols[0])
		g_hash_table_insert (uids_and_rev, g_strdup (cols[0]), g_strdup (cols[1] ? cols[1] : ""));

	return 0;
}

/**
 * e_book_backend_sqlitedb_get_uids_and_rev:
 *
 * Gets hash table of all uids (key) and rev (value) pairs stored
 * for each contact in the cache. The hash table should be freed
 * with g_hash_table_destroy(), if not needed anymore. Each key
 * and value is a newly allocated string.
 *
 * Since: 3.4
 **/
GHashTable *
e_book_backend_sqlitedb_get_uids_and_rev (EBookBackendSqliteDB *ebsdb,
                                          const gchar *folderid,
                                          GError **error)
{
	GHashTable *uids_and_rev;
	gchar *stmt;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), NULL);
	g_return_val_if_fail (folderid != NULL, NULL);

	uids_and_rev = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT uid,rev FROM %Q", folderid);
	book_backend_sql_exec (
		ebsdb->priv->db, stmt,
		get_uids_and_rev_cb, uids_and_rev, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	return uids_and_rev;
}

/**
 * e_book_backend_sqlitedb_get_is_populated:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_get_is_populated (EBookBackendSqliteDB *ebsdb,
                                          const gchar *folderid,
                                          GError **error)
{
	gchar *stmt;
	gboolean ret = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf (
		"SELECT is_populated FROM folders WHERE folder_id = %Q",
		folderid);
	book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_bool_cb , &ret, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	return ret;

}

/**
 * e_book_backend_sqlitedb_set_is_populated:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_set_is_populated (EBookBackendSqliteDB *ebsdb,
                                          const gchar *folderid,
                                          gboolean populated,
                                          GError **error)
{
	gchar *stmt = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error))
		return FALSE;

	stmt = sqlite3_mprintf (
		"UPDATE folders SET is_populated = %d "
		"WHERE folder_id = %Q", populated, folderid);
	success =book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (!success)
		goto rollback;

	return book_backend_sqlitedb_commit_transaction (ebsdb, error);

rollback:
	/* The GError is already set. */
	book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	return FALSE;
}

/**
 * e_book_backend_sqlitedb_get_revision:
 * @ebsdb: An #EBookBackendSqliteDB
 * @folderid: folder id of the address-book
 * @revision_out: (out) (transfer full): The location to return the current
 * revision
 * @error: A location to store any error that may have occurred
 *
 * Fetches the current revision for the address-book indicated by @folderid.
 *
 * Upon success, @revision_out will hold the returned revision, otherwise
 * %FALSE will be returned and @error will be updated accordingly.
 *
 * Returns: Whether the revision was successfully fetched.
 *
 * Since: 3.8
 */
gboolean
e_book_backend_sqlitedb_get_revision (EBookBackendSqliteDB *ebsdb,
                                      const gchar *folderid,
                                      gchar **revision_out,
                                      GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid && folderid[0], FALSE);
	g_return_val_if_fail (revision_out != NULL && *revision_out == NULL, FALSE);

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf (
		"SELECT revision FROM folders WHERE folder_id = %Q", folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_string_cb, &revision_out, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	return success;
}

/**
 * e_book_backend_sqlitedb_set_revision:
 * @ebsdb: An #EBookBackendSqliteDB
 * @folderid: folder id of the address-book
 * @revision: The new revision
 * @error: A location to store any error that may have occurred
 *
 * Sets the current revision for the address-book indicated by @folderid to be @revision.
 *
 * Returns: Whether the revision was successfully set.
 *
 * Since: 3.8
 */
gboolean
e_book_backend_sqlitedb_set_revision (EBookBackendSqliteDB *ebsdb,
                                      const gchar *folderid,
                                      const gchar *revision,
                                      GError **error)
{
	gchar *stmt = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid && folderid[0], FALSE);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error))
		return FALSE;

	stmt = sqlite3_mprintf (
		"UPDATE folders SET revision = %Q "
		"WHERE folder_id = %Q", revision, folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (!success)
		goto rollback;

	return book_backend_sqlitedb_commit_transaction (ebsdb, error);

rollback:
	/* The GError is already set. */
	book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	return FALSE;
}

/**
 * e_book_backend_sqlitedb_get_has_partial_content 
 * @ebsdb: 
 * @folderid: 
 * @error: 
 * 
 * 
 * Returns: TRUE if the vcards stored in the db were downloaded partially. It is to indicate
 * the stored vcards does not contain the full data.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_get_has_partial_content (EBookBackendSqliteDB *ebsdb,
                                                 const gchar *folderid,
                                                 GError **error)
{
	gchar *stmt;
	gboolean ret = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf (
		"SELECT partial_content FROM folders "
		"WHERE folder_id = %Q", folderid);
	book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_bool_cb , &ret, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	return ret;
}

/**
 * e_book_backend_sqlitedb_set_has_partial_content:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_set_has_partial_content (EBookBackendSqliteDB *ebsdb,
                                                 const gchar *folderid,
                                                 gboolean partial_content,
                                                 GError **error)
{
	gchar *stmt = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error))
		return FALSE;

	stmt = sqlite3_mprintf (
		"UPDATE folders SET partial_content = %d "
		"WHERE folder_id = %Q", partial_content, folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (!success)
		goto rollback;

	return book_backend_sqlitedb_commit_transaction (ebsdb, error);

rollback:
	/* The GError is already set. */
	book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	return FALSE;
}

/**
 * e_book_backend_sqlitedb_get_contact_bdata:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gchar *
e_book_backend_sqlitedb_get_contact_bdata (EBookBackendSqliteDB *ebsdb,
                                           const gchar *folderid,
                                           const gchar *uid,
                                           GError **error)
{
	gchar *stmt, *ret = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), NULL);
	g_return_val_if_fail (folderid != NULL, NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf (
		"SELECT bdata FROM %Q WHERE uid = %Q", folderid, uid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_string_cb , &ret, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	if (!success) {
		g_warn_if_fail (ret == NULL);
		return NULL;
	}

	return ret;
}

/**
 * e_book_backend_sqlitedb_set_contact_bdata:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_set_contact_bdata (EBookBackendSqliteDB *ebsdb,
                                           const gchar *folderid,
                                           const gchar *uid,
                                           const gchar *value,
                                           GError **error)
{
	gchar *stmt = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error))
		return FALSE;

	stmt = sqlite3_mprintf (
		"UPDATE %Q SET bdata = %Q WHERE uid = %Q",
		folderid, value, uid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (!success)
		goto rollback;

	return book_backend_sqlitedb_commit_transaction (ebsdb, error);

rollback:
	/* The GError is already set. */
	book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	return FALSE;
}

/**
 * e_book_backend_sqlitedb_get_sync_data:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gchar *
e_book_backend_sqlitedb_get_sync_data (EBookBackendSqliteDB *ebsdb,
                                       const gchar *folderid,
                                       GError **error)
{
	gchar *stmt, *ret = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), NULL);
	g_return_val_if_fail (folderid != NULL, NULL);

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf (
		"SELECT sync_data FROM folders WHERE folder_id = %Q",
		folderid);
	book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_string_cb , &ret, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	return ret;
}

/**
 * e_book_backend_sqlitedb_set_sync_data:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_set_sync_data (EBookBackendSqliteDB *ebsdb,
                                       const gchar *folderid,
                                       const gchar *sync_data,
                                       GError **error)
{
	gchar *stmt = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);
	g_return_val_if_fail (sync_data != NULL, FALSE);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error))
		return FALSE;

	stmt = sqlite3_mprintf (
		"UPDATE folders SET sync_data = %Q "
		"WHERE folder_id = %Q", sync_data, folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (!success)
		goto rollback;

	return book_backend_sqlitedb_commit_transaction (ebsdb, error);

rollback:
	/* The GError is already set. */
	book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	return FALSE;
}

/**
 * e_book_backend_sqlitedb_get_key_value:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gchar *
e_book_backend_sqlitedb_get_key_value (EBookBackendSqliteDB *ebsdb,
                                       const gchar *folderid,
                                       const gchar *key,
                                       GError **error)
{
	gchar *stmt, *ret = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), NULL);
	g_return_val_if_fail (folderid != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf (
		"SELECT value FROM keys WHERE folder_id = %Q AND key = %Q",
		folderid, key);
	book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_string_cb , &ret, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	return ret;
}

/**
 * e_book_backend_sqlitedb_set_key_value:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_set_key_value (EBookBackendSqliteDB *ebsdb,
                                       const gchar *folderid,
                                       const gchar *key,
                                       const gchar *value,
                                       GError **error)
{
	gchar *stmt = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error))
		return FALSE;

	stmt = sqlite3_mprintf (
		"INSERT or REPLACE INTO keys (key, value, folder_id) "
		"values (%Q, %Q, %Q)", key, value, folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (!success)
		goto rollback;

	return book_backend_sqlitedb_commit_transaction (ebsdb, error);

rollback:
	/* The GError is already set. */
	book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	return FALSE;
}

/**
 * e_book_backend_sqlitedb_get_partially_cached_ids:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
GSList *
e_book_backend_sqlitedb_get_partially_cached_ids (EBookBackendSqliteDB *ebsdb,
                                                  const gchar *folderid,
                                                  GError **error)
{
	gchar *stmt;
	GSList *uids = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), NULL);
	g_return_val_if_fail (folderid != NULL, NULL);

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf (
		"SELECT uid FROM %Q WHERE partial_content = 1",
		folderid);
	book_backend_sql_exec (
		ebsdb->priv->db, stmt, addto_slist_cb, &uids, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	return uids;
}

/**
 * e_book_backend_sqlitedb_delete_addressbook:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_delete_addressbook (EBookBackendSqliteDB *ebsdb,
                                            const gchar *folderid,
                                            GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error))
		return FALSE;

	/* delete the contacts table */
	stmt = sqlite3_mprintf ("DROP TABLE %Q ", folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (!success)
		goto rollback;

	/* delete the key/value pairs corresponding to this table */
	stmt = sqlite3_mprintf (
		"DELETE FROM keys WHERE folder_id = %Q", folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (!success)
		goto rollback;

	/* delete the folder from the folders table */
	stmt = sqlite3_mprintf (
		"DELETE FROM folders WHERE folder_id = %Q", folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (!success)
		goto rollback;

	return book_backend_sqlitedb_commit_transaction (ebsdb, error);

rollback:
	/* The GError is already set. */
	book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	return FALSE;
}

/**
 * e_book_backend_sqlitedb_search_data_free:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
void
e_book_backend_sqlitedb_search_data_free (EbSdbSearchData *s_data)
{
	if (s_data) {
		g_free (s_data->uid);
		g_free (s_data->vcard);
		g_free (s_data->bdata);
		g_free (s_data);
	}
}

/**
 * e_book_backend_sqlitedb_remove:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_remove (EBookBackendSqliteDB *ebsdb,
                                GError **error)
{
	gchar *filename;
	gint ret;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);

	WRITER_LOCK (ebsdb);

	sqlite3_close (ebsdb->priv->db);

	filename = g_build_filename (ebsdb->priv->path, DB_FILENAME, NULL);
	ret = g_unlink (filename);
	g_free (filename);

	WRITER_UNLOCK (ebsdb);

	if (ret == -1) {
		g_set_error (
			error, E_BOOK_SDB_ERROR, 0,
			_("Unable to remove the db file: errno %d"), errno);
		return FALSE;
	}

	return TRUE;
}

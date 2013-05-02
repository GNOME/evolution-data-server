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

#include "e-book-backend-sqlitedb.h"

#include <locale.h>
#include <string.h>
#include <errno.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <sqlite3.h>
#include <libebackend/libebackend.h>

#include "e-book-backend-sexp.h"

#define E_BOOK_BACKEND_SQLITEDB_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BOOK_BACKEND_SQLITEDB, EBookBackendSqliteDBPrivate))

#define d(x)

#if d(1)+0
#  define LOCK_MUTEX(mutex)					\
	G_STMT_START {						\
		g_message ("%s: DB Locking ", G_STRFUNC);	\
		g_mutex_lock (mutex);				\
		g_message ("%s: DB Locked ", G_STRFUNC);	\
	} G_STMT_END

#  define UNLOCK_MUTEX(mutex)					\
	G_STMT_START {						\
		g_message ("%s: Unlocking ", G_STRFUNC);	\
		g_mutex_unlock (mutex);				\
		g_message ("%s: DB Unlocked ", G_STRFUNC);	\
	} G_STMT_END
#else
#  define LOCK_MUTEX(mutex)   g_mutex_lock (mutex)
#  define UNLOCK_MUTEX(mutex) g_mutex_unlock (mutex)
#endif

#define DB_FILENAME "contacts.db"
#define FOLDER_VERSION 7

typedef enum {
	INDEX_PREFIX = (1 << 0),
	INDEX_SUFFIX = (1 << 1),
	INDEX_PHONE  = (1 << 2)
} IndexFlags;

typedef struct {
	EContactField field;   /* The EContact field */
	GType         type;    /* The GType (only support string or gboolean) */
	const gchar  *dbname;  /* The key for this field in the sqlite3 table */
	IndexFlags    index;   /* Whether this summary field should have an index in the SQLite DB */
	ECollator    *collator;/* The ECollator to create sort keys for this field */
} SummaryField;

struct _EBookBackendSqliteDBPrivate {
	sqlite3 *db;
	gchar *path;
	gchar *hash_key;

	GMutex lock;
	GMutex updates_lock; /* This is for deprecated e_book_backend_sqlitedb_lock_updates () */

	gboolean store_vcard;
	guint32 in_transaction;

	SummaryField   *summary_fields;
	gint            n_summary_fields;
	guint           have_attr_list : 1;
	IndexFlags      attr_list_indexes;
};

G_DEFINE_TYPE (EBookBackendSqliteDB, e_book_backend_sqlitedb, G_TYPE_OBJECT)

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

static void
destroy_search_data (gpointer data)
{
	e_book_backend_sqlitedb_search_data_free (data);
}

static SummaryField * append_summary_field (GArray         *array,
					    EContactField   field,
					    gboolean       *have_attr_list,
					    GError        **error);

static gboolean upgrade_contacts_table (EBookBackendSqliteDB  *ebsdb,
					 const gchar           *folderid,
					 const gchar           *region,
					 GError               **error);

static gboolean summary_fields_add_collations (SummaryField   *summary_fields,
					       gint            n_summary_fields,
					       EContactField  *fields,
					       gchar         **collations,
					       gint            n_fields,
					       GError        **error);
static void clear_collations (SummaryField *fields,
			      gint          n_fields);


static const gchar *
get_current_collation_locale (void)
{
	const gchar *lc_collate;

	/* Allow overriding the locale name explicitly, this is mainly
	 * for test cases since we support locales with ICU that
	 * may not be supported by a given system's setlocale()
	 */
	lc_collate = g_getenv ("EDS_COLLATE");

	if (!lc_collate)
		lc_collate = setlocale (LC_COLLATE, NULL);

	return lc_collate;
}

static const gchar *
summary_dbname_from_field (EBookBackendSqliteDB *ebsdb,
                           EContactField field)
{
	gint i;

	for (i = 0; i < ebsdb->priv->n_summary_fields; i++) {
		if (ebsdb->priv->summary_fields[i].field == field)
			return ebsdb->priv->summary_fields[i].dbname;
	}

	return NULL;
}

static gint
summary_index_from_field (EBookBackendSqliteDB *ebsdb,
			  EContactField         field)
{
	gint i;

	for (i = 0; i < ebsdb->priv->n_summary_fields; i++) {
		if (ebsdb->priv->summary_fields[i].field == field)
			return i;
	}

	return -1;
}

static gint
summary_index_from_field_name (EBookBackendSqliteDB *ebsdb,
                               const gchar *field_name)
{
	EContactField field;

	field = e_contact_field_id (field_name);

	return summary_index_from_field (ebsdb, field);
}

typedef struct {
	EBookBackendSqliteDB *ebsdb;
	GSList *list;
} StoreVCardData;

G_DEFINE_QUARK (
	e-book-backend-sqlitedb-error-quark,
	e_book_backend_sqlitedb_error)

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

	sqlite3_close (priv->db);

	g_free (priv->path);

	clear_collations (priv->summary_fields, priv->n_summary_fields);
	g_free (priv->summary_fields);

	g_mutex_clear (&priv->lock);
	g_mutex_clear (&priv->updates_lock);

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

	ebsdb->priv->in_transaction = 0;
	g_mutex_init (&ebsdb->priv->lock);
	g_mutex_init (&ebsdb->priv->updates_lock);
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
		g_thread_yield ();
		ret = sqlite3_exec (db, stmt, callback, data, &errmsg);
	}

	if (ret != SQLITE_OK) {
		d (g_printerr ("Error in SQL EXEC statement: %s [%s].\n", stmt, errmsg));
		g_set_error_literal (
			error, E_BOOK_SDB_ERROR,
			ret == SQLITE_CONSTRAINT ?
			E_BOOK_SDB_ERROR_CONSTRAINT : E_BOOK_SDB_ERROR_OTHER,
			errmsg);
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
                gint n_cols,
                gchar **cols,
                gchar **name)
{
	gint i;

	g_printerr ("  DEBUG BEGIN:\n");

	for (i = 0; i < n_cols; i++)
		g_printerr ("    NAME: '%s' VALUE: %s\n", name[i], cols[i]);

	g_printerr ("  DEBUG END\n");

	return 0;
}

static gint G_GNUC_CONST
booksql_debug (void)
{
	static gint booksql_debug = -1;

	if (booksql_debug == -1) {
		const gchar *const tmp = g_getenv ("BOOKSQL_DEBUG");
		booksql_debug = (tmp != NULL ? MAX (0, atoi (tmp)) : 0);
	}

	return booksql_debug;
}

static void
book_backend_sql_debug (sqlite3 *db,
                        const gchar *stmt,
                        gint (*callback)(gpointer ,gint,gchar **,gchar **),
                        gpointer data,
                        GError **error)
{
	GError *local_error = NULL;

	g_printerr ("DEBUG STATEMENT: %s\n", stmt);

	if (booksql_debug () > 1) {
		gchar *debug = g_strconcat ("EXPLAIN QUERY PLAN ", stmt, NULL);
		book_backend_sql_exec_real (db, debug, print_debug_cb, NULL, &local_error);
		g_free (debug);
	}

	if (local_error) {
		g_printerr ("DEBUG STATEMENT END: Error: %s\n", local_error->message);
	} else if (booksql_debug () > 1) {
		g_printerr ("DEBUG STATEMENT END: Success\n");
	}

	g_clear_error (&local_error);
}

static gboolean
book_backend_sql_exec (sqlite3 *db,
                       const gchar *stmt,
                       gint (*callback)(gpointer ,gint,gchar **,gchar **),
                       gpointer data,
                       GError **error)
{
	if (booksql_debug ())
		book_backend_sql_debug (db, stmt, callback, data, error);

	return book_backend_sql_exec_real (db, stmt, callback, data, error);
}

/* This function must always be called with the priv->lock held */
static gboolean
book_backend_sqlitedb_start_transaction (EBookBackendSqliteDB *ebsdb,
                                         GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (ebsdb != NULL, FALSE);
	g_return_val_if_fail (ebsdb->priv != NULL, FALSE);
	g_return_val_if_fail (ebsdb->priv->db != NULL, FALSE);

	ebsdb->priv->in_transaction++;
	g_return_val_if_fail (ebsdb->priv->in_transaction > 0, FALSE);

	if (ebsdb->priv->in_transaction == 1) {

		success = book_backend_sql_exec (
			ebsdb->priv->db, "BEGIN", NULL, NULL, error);
	}

	return success;
}

/* This function must always be called with the priv->lock held */
static gboolean
book_backend_sqlitedb_commit_transaction (EBookBackendSqliteDB *ebsdb,
                                          GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (ebsdb != NULL, FALSE);
	g_return_val_if_fail (ebsdb->priv != NULL, FALSE);
	g_return_val_if_fail (ebsdb->priv->db != NULL, FALSE);

	g_return_val_if_fail (ebsdb->priv->in_transaction > 0, FALSE);

	ebsdb->priv->in_transaction--;

	if (ebsdb->priv->in_transaction == 0) {
		success = book_backend_sql_exec (
			ebsdb->priv->db, "COMMIT", NULL, NULL, error);
	}

	return success;
}

/* This function must always be called with the priv->lock held */
static gboolean
book_backend_sqlitedb_rollback_transaction (EBookBackendSqliteDB *ebsdb,
                                            GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (ebsdb != NULL, FALSE);
	g_return_val_if_fail (ebsdb->priv != NULL, FALSE);
	g_return_val_if_fail (ebsdb->priv->db != NULL, FALSE);

	g_return_val_if_fail (ebsdb->priv->in_transaction > 0, FALSE);

	ebsdb->priv->in_transaction--;

	if (ebsdb->priv->in_transaction == 0) {
		success = book_backend_sql_exec (
			ebsdb->priv->db, "ROLLBACK", NULL, NULL, error);

	}
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

typedef struct {
	gboolean has_countrycode;
	gboolean has_lc_collate;
	gboolean has_collations;
} LocaleColumns;

static gint
find_locale_columns (gpointer data,
		     gint n_cols,
		     gchar **cols,
		     gchar **name)
{
	LocaleColumns *columns = (LocaleColumns *)data;
	gint i;

	for (i = 0; i < n_cols; i++) {
		if (g_strcmp0 (cols[i], "countrycode") == 0)
			columns->has_countrycode = TRUE;
		else if (g_strcmp0 (cols[i], "lc_collate") == 0)
			columns->has_lc_collate = TRUE;
		else if (g_strcmp0 (cols[i], "collations") == 0)
			columns->has_collations = TRUE;
	}

	return 0;
}

static gboolean
create_folders_table (EBookBackendSqliteDB *ebsdb,
                      gint *previous_schema,
                      GError **error)
{
	gboolean success;
	gint version = 0;
	LocaleColumns locale_columns = { FALSE, FALSE, FALSE };

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
		" is_populated INTEGER DEFAULT 0,"
		"  partial_content INTEGER DEFAULT 0,"
		" version INTEGER,"
		"  revision TEXT,"
		" multivalues TEXT )";

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
	}

	/* Upgrade DB to version 4: Nothing to do. The country-code column it
	 * added got redundant already.
	 */

	/* Upgrade DB to version 5: Drop the reverse_multivalues column, but
	 * wait with converting phone summary values to new format until
	 * create_contacts_table() as we need introspection details for doing
	 * that.
	 */
	if (version >= 3 && version < FOLDER_VERSION) {
		stmt = "UPDATE folders SET "
				"multivalues = REPLACE(RTRIM(REPLACE("
					"multivalues || ':', ':', "
					"CASE reverse_multivalues "
						"WHEN 0 THEN ';prefix ' "
						"ELSE ';prefix;suffix ' "
					"END)), ' ', ':'), "
				"reverse_multivalues = NULL";

		success = book_backend_sql_exec (
			ebsdb->priv->db, stmt, NULL, NULL, error);

		if (!success)
			goto rollback;
	}

	/* Finish the eventual upgrade by storing the current schema version.
	 */
	if (version >= 1 && version < FOLDER_VERSION) {
		gchar *version_update_stmt =
			sqlite3_mprintf ("UPDATE folders SET version = %d", FOLDER_VERSION);

		success = book_backend_sql_exec (
			ebsdb->priv->db, version_update_stmt, NULL, NULL, error);

		sqlite3_free (version_update_stmt);
	}

	if (!success)
		goto rollback;

	/* Adding countrycode column again to track the addressbook's country code.
	 * Detecting presence of this column by reflection instead of assigning a
	 * dedicated schema version number, because upstream is not going to accept
	 * a locale bound addressbook database. See discussion in bug 689622.
	 */
	stmt = "PRAGMA table_info(folders)";
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, find_locale_columns, &locale_columns, error);

	if (!success)
		goto rollback;

	if (!locale_columns.has_countrycode) {
		stmt = "ALTER TABLE folders ADD COLUMN countrycode VARCHAR(2)";
		success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);

		if (!success)
			goto rollback;

	}

	if (!locale_columns.has_lc_collate) {
		stmt = "ALTER TABLE folders ADD COLUMN lc_collate TEXT";
		success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);

		if (!success)
			goto rollback;
	}

	if (!locale_columns.has_collations) {
		stmt = "ALTER TABLE folders ADD COLUMN collations TEXT";
		success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);

		if (!success)
			goto rollback;
	}

	/* Remember the schema version for later use and finish the transaction. */
	*previous_schema = version;
	return book_backend_sqlitedb_commit_transaction (ebsdb, error);

rollback:
	/* The GError is already set. */
	book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	*previous_schema = 0;
	return FALSE;
}

static gchar *
format_multivalues (EBookBackendSqliteDB *ebsdb)
{
	gint i;
	GString *string;
	gboolean first = TRUE;

	string = g_string_new (NULL);

	for (i = 0; i < ebsdb->priv->n_summary_fields; i++) {
		if (ebsdb->priv->summary_fields[i].type == E_TYPE_CONTACT_ATTR_LIST) {
			if (first)
				first = FALSE;
			else
				g_string_append_c (string, ':');

			g_string_append (string, ebsdb->priv->summary_fields[i].dbname);

			if ((ebsdb->priv->summary_fields[i].index & INDEX_PREFIX) != 0)
				g_string_append (string, ";prefix");
			if ((ebsdb->priv->summary_fields[i].index & INDEX_SUFFIX) != 0)
				g_string_append (string, ";suffix");
			if ((ebsdb->priv->summary_fields[i].index & INDEX_PHONE) != 0)
				g_string_append (string, ";phone");
		}
	}

	return g_string_free (string, FALSE);
}

static gint
get_count_cb (gpointer ref,
              gint n_cols,
              gchar **cols,
              gchar **name)
{
	gint64 count = 0;
	gint *ret = ref;
	gint i;

	for (i = 0; i < n_cols; i++) {
		if (name[i] && strncmp (name[i], "count", 5) == 0) {
			count = g_ascii_strtoll (cols[i], NULL, 10);
		}
	}

	*ret = count;

	return 0;
}

static gboolean
add_collations_from_introspection (SummaryField *summary_fields,
				   gint          n_summary_fields,
				   const gchar  *collation_desc)
{

	gchar **fields;
	GArray *coll_fields;
	GArray *collations;
	gboolean success = TRUE;
	gchar *collation;
	gint i;

	/* Construct an array of collation fields and names */
	coll_fields = g_array_new (FALSE, FALSE, sizeof (EContactField));
	collations = g_array_new (TRUE, FALSE, sizeof (gchar *));

	fields = g_strsplit (collation_desc, ":", 0);
	for (i = 0; success && fields[i] != NULL; i++) {
		EContactField  field;
		gchar **rule;

		rule = g_strsplit (fields[i], "=", 0);
		/* Sanity check */
		if (!rule[0] || !rule[1]) {
			success = FALSE;
		} else {
			/* 0 is the invalid EContactField id */
			field = e_contact_field_id (rule[0]);
			if (field == 0)
				success = FALSE;
		}

		if (success == FALSE) {
			g_warning ("Malformed collation rule '%s'", fields[i]);
			g_strfreev (rule);
			break;
		}
		/* Add a value to the arrays */
		collation = g_strdup (rule[1]);
		g_array_append_val (coll_fields, field);
		g_array_append_val (collations, collation);
		
		g_strfreev (rule);
	}
	g_strfreev (fields);

	if (success)
		success = summary_fields_add_collations (summary_fields,
							 n_summary_fields,
							 (EContactField *)coll_fields->data,
							 (gchar **)collations->data,
							 coll_fields->len,
							 NULL);

	for (i = 0; i < collations->len; i++) {
		collation = g_array_index (collations, gchar *, i);
		g_free (collation);
	}
	g_array_free (collations, TRUE);
	g_array_free (coll_fields, TRUE);

 	return success;
}

static gboolean
check_folderid_exists (EBookBackendSqliteDB *ebsdb,
                       const gchar *folderid,
                       gboolean *exists,
                       GError **error)
{
	gboolean success;
	gint count = 0;
	gchar *stmt;

	stmt = sqlite3_mprintf ("SELECT count(*) FROM sqlite_master WHERE type='table' AND name=%Q;", folderid);

	success = book_backend_sql_exec (ebsdb->priv->db, stmt, get_count_cb, &count, error);
	sqlite3_free (stmt);

	*exists = (count > 0);

	return success;
}

static gboolean
add_folder_into_db (EBookBackendSqliteDB *ebsdb,
                    const gchar *folderid,
                    const gchar *folder_name,
		    const gchar *initial_collations,
		    gboolean *already_exists,
                    GError **error)
{
	gchar *stmt;
	gboolean success;
	gchar *multivalues;
	gboolean exists = FALSE;

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error))
		return FALSE;

	success = check_folderid_exists (ebsdb, folderid, &exists, error);
	if (!success)
		goto rollback;

	if (!exists) {
		multivalues = format_multivalues (ebsdb);

		stmt = sqlite3_mprintf (
			"INSERT OR IGNORE INTO "
			"folders ( folder_id, folder_name, version, multivalues, collations ) "
			"VALUES ( %Q, %Q, %d, %Q, %Q ) ",
			folderid, folder_name, FOLDER_VERSION, multivalues, initial_collations);
		success = book_backend_sql_exec (
			ebsdb->priv->db, stmt, NULL, NULL, error);
		sqlite3_free (stmt);
		g_free (multivalues);

		if (!success)
			goto rollback;
	}

	if (already_exists)
		*already_exists = exists;

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
	GList **columns = (GList **) ref;
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
	gchar *collations = NULL;
	gint i, j;

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
		IndexFlags computed = 0;

		/* Ignore the 'localized' columns */
		if (g_str_has_suffix (col, "_localized"))
			continue;

		/* Check if we're parsing a reverse field */
		if ((p = strstr (col, "_reverse")) != NULL) {
			computed = INDEX_SUFFIX;
			*p = '\0';
		} else  if ((p = strstr (col, "_phone")) != NULL) {
			computed = INDEX_PHONE;
			*p = '\0';
		}

		/* First check exception fields */
		if (g_ascii_strcasecmp (col, "uid") == 0)
			field = E_CONTACT_UID;
		else if (g_ascii_strcasecmp (col, "is_list") == 0)
			field = E_CONTACT_IS_LIST;
		else
			field = e_contact_field_id (col);

		/* Check for parse error */
		if (field == 0) {
			g_set_error (
				error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_OTHER,
				_("Error introspecting unknown summary field '%s'"), col);
			success = FALSE;
			break;
		}

		/* Computed columns are always declared after the normal columns,
		 * if a reverse field is encountered we need to set the suffix
		 * index on the coresponding summary field
		 */
		if (computed) {
			for (i = 0; i < summary_fields->len; i++) {
				SummaryField *iter = &g_array_index (summary_fields, SummaryField, i);

				if (iter->field == field) {
					iter->index |= computed;
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

	ebsdb->priv->attr_list_indexes = 0;

	if (multivalues) {
		gchar **fields = g_strsplit (multivalues, ":", 0);

		for (i = 0; fields[i] != NULL; i++) {
			EContactField field;
			SummaryField *iter;
			gchar **params;

			params = g_strsplit (fields[i], ";", 0);
			field = e_contact_field_id (params[0]);
			iter = append_summary_field (summary_fields, field, NULL, NULL);

			if (iter) {
				for (j = 1; params[j]; ++j) {
					if (strcmp (params[j], "prefix") == 0) {
						iter->index |= INDEX_PREFIX;
					} else if (strcmp (params[j], "suffix") == 0) {
						iter->index |= INDEX_SUFFIX;
					} else if (strcmp (params[j], "phone") == 0) {
						iter->index |= INDEX_PHONE;
					}
				}

				ebsdb->priv->attr_list_indexes |= iter->index;
			}

			g_strfreev (params);
		}

		g_strfreev (fields);
	}

	/* Introspect custom collation fields, here we setup the custom ECollators
	 * on the SummaryFields if we need to.
	 */
	stmt = sqlite3_mprintf (
		"SELECT collations FROM folders WHERE folder_id = %Q", folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_string_cb, &collations, error);
	sqlite3_free (stmt);

	success = add_collations_from_introspection ((SummaryField *)summary_fields->data,
						     summary_fields->len,
						     collations);

 introspect_summary_finish:

	g_list_free_full (summary_columns, (GDestroyNotify) g_free);
	g_free (multivalues);

	/* Apply the introspected summary fields */
	if (success) {
		clear_collations (ebsdb->priv->summary_fields, ebsdb->priv->n_summary_fields);
		g_free (ebsdb->priv->summary_fields);

		ebsdb->priv->n_summary_fields = summary_fields->len;
		ebsdb->priv->summary_fields = (SummaryField *) g_array_free (summary_fields, FALSE);
	} else if (summary_fields) {
		clear_collations ((SummaryField *)summary_fields->data, summary_fields->len);
		g_array_free (summary_fields, TRUE);
	}

	return success;
}

/* The column names match the fields used in book-backend-sexp */
static gboolean
create_contacts_table (EBookBackendSqliteDB *ebsdb,
                       const gchar *folderid,
                       gint previous_schema,
		       gboolean already_exists,
                       GError **error)
{
	gint i;
	gboolean success;
	gchar *stmt, *tmp;
	GString *string;
	gchar *current_region = NULL;
	gboolean relocalized = FALSE;

	if (e_phone_number_is_supported ())
		current_region = e_phone_number_get_default_region ();

	string = g_string_new (
		"CREATE TABLE IF NOT EXISTS %Q ( uid TEXT PRIMARY KEY, ");

	for (i = 1; i < ebsdb->priv->n_summary_fields; i++) {
		if (ebsdb->priv->summary_fields[i].type == G_TYPE_STRING) {
			g_string_append (string, ebsdb->priv->summary_fields[i].dbname);
			g_string_append (string, " TEXT, ");

			/* For any string columns (not multivalued columns), also create a localized
			 * data column for sort ordering
			 */
			if (ebsdb->priv->summary_fields[i].field != E_CONTACT_REV) {
				g_string_append  (string, ebsdb->priv->summary_fields[i].dbname);
				g_string_append  (string, "_localized TEXT, ");
			}
		} else if (ebsdb->priv->summary_fields[i].type == G_TYPE_BOOLEAN) {
			g_string_append (string, ebsdb->priv->summary_fields[i].dbname);
			g_string_append (string, " INTEGER, ");
		} else if (ebsdb->priv->summary_fields[i].type != E_TYPE_CONTACT_ATTR_LIST)
			g_warn_if_reached ();

		/* Additional columns holding normalized reverse values for suffix matching */
		if (ebsdb->priv->summary_fields[i].type == G_TYPE_STRING) {
			if (ebsdb->priv->summary_fields[i].index & INDEX_SUFFIX) {
				g_string_append  (string, ebsdb->priv->summary_fields[i].dbname);
				g_string_append  (string, "_reverse TEXT, ");
			}

			if (ebsdb->priv->summary_fields[i].index & INDEX_PHONE) {
				g_string_append  (string, ebsdb->priv->summary_fields[i].dbname);
				g_string_append  (string, "_phone TEXT, ");
			}
		}
	}
	g_string_append (string, "vcard TEXT, bdata TEXT)");

	stmt = sqlite3_mprintf (string->str, folderid);
	g_string_free (string, TRUE);

	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL , error);

	sqlite3_free (stmt);

	/* Now, if we're upgrading from < version 7, we need to add the _localized columns */
	if (success && previous_schema >= 1 && previous_schema < 7) {

		tmp = sqlite3_mprintf ("ALTER TABLE %Q ADD COLUMN ", folderid);

		for (i = 1; i < ebsdb->priv->n_summary_fields && success; i++) {

			if (ebsdb->priv->summary_fields[i].type == G_TYPE_STRING &&
			    ebsdb->priv->summary_fields[i].field != E_CONTACT_REV) {
				string = g_string_new (tmp);

				g_string_append (string, ebsdb->priv->summary_fields[i].dbname);
				g_string_append (string, "_localized TEXT, ");

				success = book_backend_sql_exec (
				        ebsdb->priv->db, string->str, NULL, NULL , error);

				g_string_free (string, TRUE);
			}
		}
		sqlite3_free (tmp);
	}

	/* Construct the create statement from the attribute list summary table */
	if (success && ebsdb->priv->have_attr_list) {
		string = g_string_new ("CREATE TABLE IF NOT EXISTS %Q ( uid TEXT NOT NULL REFERENCES %Q(uid), "
			"field TEXT, value TEXT");

		if ((ebsdb->priv->attr_list_indexes & INDEX_SUFFIX) != 0)
			g_string_append (string, ", value_reverse TEXT");
		if ((ebsdb->priv->attr_list_indexes & INDEX_PHONE) != 0)
			g_string_append (string, ", value_phone TEXT");

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
		if (success && (ebsdb->priv->attr_list_indexes & INDEX_PREFIX) != 0) {
			stmt = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS VALINDEX ON %Q (value)", tmp);
			success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
			sqlite3_free (stmt);
		}

		if (success && (ebsdb->priv->attr_list_indexes & INDEX_SUFFIX) != 0) {
			stmt = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS RVALINDEX ON %Q (value_reverse)", tmp);
			success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
			sqlite3_free (stmt);
		}

		if (success && (ebsdb->priv->attr_list_indexes & INDEX_PHONE) != 0) {
			stmt = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS PVALINDEX ON %Q (value_phone)", tmp);
			success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
			sqlite3_free (stmt);
		}

		g_free (tmp);
	}

	/* Dont introspect the summary if the table did not yet exist */
	if (success && already_exists)
		success = introspect_summary (ebsdb, folderid, error);

	/* Create indexes on the summary fields configured for indexing */
	for (i = 0; success && i < ebsdb->priv->n_summary_fields; i++) {
		if ((ebsdb->priv->summary_fields[i].index & INDEX_PREFIX) != 0 &&
		    ebsdb->priv->summary_fields[i].type != E_TYPE_CONTACT_ATTR_LIST) {
			/* Derive index name from field & folder */
			tmp = g_strdup_printf (
				"INDEX_%s_%s",
				summary_dbname_from_field (ebsdb, ebsdb->priv->summary_fields[i].field),
				folderid);
			stmt = sqlite3_mprintf (
				"CREATE INDEX IF NOT EXISTS %Q ON %Q (%s)", tmp, folderid,
						summary_dbname_from_field (ebsdb, ebsdb->priv->summary_fields[i].field));
			success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
			sqlite3_free (stmt);
			g_free (tmp);

			/* For any indexed column, also index the localized column */
			if (ebsdb->priv->summary_fields[i].field != E_CONTACT_REV) {
				tmp = g_strdup_printf (
				        "INDEX_%s_localized_%s",
					summary_dbname_from_field (ebsdb, ebsdb->priv->summary_fields[i].field),
					folderid);
				stmt = sqlite3_mprintf (
				        "CREATE INDEX IF NOT EXISTS %Q ON %Q (%s_localized)", tmp, folderid,
					summary_dbname_from_field (ebsdb, ebsdb->priv->summary_fields[i].field));
				success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
				sqlite3_free (stmt);
				g_free (tmp);
			}
		}

		if (success &&
		    (ebsdb->priv->summary_fields[i].index & INDEX_SUFFIX) != 0 &&
		    ebsdb->priv->summary_fields[i].type != E_TYPE_CONTACT_ATTR_LIST) {
			/* Derive index name from field & folder */
			tmp = g_strdup_printf (
				"RINDEX_%s_%s",
				summary_dbname_from_field (ebsdb, ebsdb->priv->summary_fields[i].field),
				folderid);
			stmt = sqlite3_mprintf (
				"CREATE INDEX IF NOT EXISTS %Q ON %Q (%s_reverse)", tmp, folderid,
						summary_dbname_from_field (ebsdb, ebsdb->priv->summary_fields[i].field));
			success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
			sqlite3_free (stmt);
			g_free (tmp);
		}

		if ((ebsdb->priv->summary_fields[i].index & INDEX_PHONE) != 0 &&
		    ebsdb->priv->summary_fields[i].type != E_TYPE_CONTACT_ATTR_LIST) {
			/* Derive index name from field & folder */
			tmp = g_strdup_printf ("PINDEX_%s_%s",
					       summary_dbname_from_field (ebsdb, ebsdb->priv->summary_fields[i].field),
					       folderid);
			stmt = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS %Q ON %Q (%s_phone)", tmp, folderid,
						summary_dbname_from_field (ebsdb, ebsdb->priv->summary_fields[i].field));
			success = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, error);
			sqlite3_free (stmt);
			g_free (tmp);
		}
	}

	/* Need to relocalize the whole thing if the schema has been upgraded to version 7 */
	if (success && previous_schema >= 1 && previous_schema < 7) {
		success = upgrade_contacts_table (ebsdb, folderid, current_region, error);
		relocalized = TRUE;
	}

	/* We may need to relocalize for a country code change */
	if (success && relocalized == FALSE && e_phone_number_is_supported ()) {
		gchar *stored_region = NULL;

		stmt = sqlite3_mprintf ("SELECT countrycode FROM folders WHERE folder_id = %Q", folderid);
		success = book_backend_sql_exec (ebsdb->priv->db, stmt, get_string_cb, &stored_region, error);
		sqlite3_free (stmt);

		if (success && g_strcmp0 (current_region, stored_region) != 0) {
			success = upgrade_contacts_table (ebsdb, folderid, current_region, error);
			relocalized = TRUE;
		}

		g_free (stored_region);
	}

	/* We may need to relocalize for an LC_COLLATE locale change */
	if (success && relocalized == FALSE) {
		const gchar *lc_collate = NULL;
		gchar *stored_lc_collate;

		stmt = sqlite3_mprintf ("SELECT lc_collate FROM folders WHERE folder_id = %Q", folderid);
		success = book_backend_sql_exec (ebsdb->priv->db, stmt, get_string_cb, &stored_lc_collate, error);
		sqlite3_free (stmt);

		lc_collate = get_current_collation_locale ();

		if (success && g_strcmp0 (stored_lc_collate, lc_collate) != 0) {
			success = upgrade_contacts_table (ebsdb, folderid, current_region, error);
			relocalized = TRUE;
		}

		g_free (stored_lc_collate);
	}

	g_free (current_region);

	return success;
}

typedef struct {
	sqlite3 *db;
	const gchar *collation;
	const gchar *table;
} CollationInfo;

static gint
create_phone_indexes_for_columns (gpointer data,
                                  gint n_cols,
                                  gchar **cols,
                                  gchar **name)
{
	const gchar *column_name = cols[1];
	CollationInfo *info = data;

	if (g_str_has_suffix (column_name, "_phone")) {
		gchar *index_name, *stmt;
		GError *error = NULL;

		index_name = g_strdup_printf (
			"PINDEX_%s_ON_%s_WITH_%s", column_name, info->table, info->collation);
		stmt = sqlite3_mprintf (
			"CREATE INDEX IF NOT EXISTS %Q ON %Q (%s COLLATE %Q)",
			index_name, info->table, column_name, info->collation);

		if (!book_backend_sql_exec (info->db, stmt, NULL, NULL, &error)) {
			g_warning ("%s: %s", G_STRFUNC, error->message);
			g_error_free (error);
		}

		sqlite3_free (stmt);
		g_free (index_name);
	}

	return 0;
}

static gint
create_phone_indexes_for_tables (gpointer data,
                                 gint n_cols,
                                 gchar **cols,
                                 gchar **name)
{
	CollationInfo *info = data;
	GError *error = NULL;
	gchar *tmp, *stmt;

	info->table = cols[0];
	stmt = sqlite3_mprintf ("PRAGMA table_info(%Q)", info->table);

	if (!book_backend_sql_exec (
		info->db, stmt, create_phone_indexes_for_columns, info, &error)) {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_clear_error (&error);
	}

	sqlite3_free (stmt);

	info->table = tmp = g_strconcat (info->table, "_lists", NULL);
	stmt = sqlite3_mprintf ("PRAGMA table_info(%Q)", info->table);

	if (!book_backend_sql_exec (
		info->db, stmt, create_phone_indexes_for_columns, info, &error)) {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_clear_error (&error);
	}

	sqlite3_free (stmt);
	g_free (tmp);

	return 0;
}

static GString *
ixphone_str (gint country_code,
             const gchar *const national_str,
             gint national_len)
{
	GString *const str = g_string_sized_new (6 + national_len);
	g_string_append_printf (str, "+%d|", country_code);
	g_string_append_len (str, national_str, national_len);
	return str;
}

static gint
e_strcmp2n (const gchar *str1,
            size_t len1,
            const gchar *str2,
            size_t len2)
{
	const gint cmp = memcmp (str1, str2, MIN (len1, len2));

	return (cmp != 0 ? cmp :
		len1 == len2 ? 0 :
		len1 < len2 ? -1 : 1);
}

static gint
ixphone_compare_for_country (gpointer data,
                             gint len1,
                             gconstpointer arg1,
                             gint len2,
                             gconstpointer arg2)
{
	const gchar *const str1 = arg1;
	const gchar *const str2 = arg2;
	const gchar *const sep1 = memchr (str1, '|', len1);
	const gchar *const sep2 = memchr (str2, '|', len2);
	const gint country_code = GPOINTER_TO_INT (data);

	g_return_val_if_fail (sep1 != NULL, 0);
	g_return_val_if_fail (sep2 != NULL, 0);

	if ((str1 == sep1) == (str2 == sep2))
		return e_strcmp2n (str1, len1, str2, len2);

	if (str1 == sep1) {
		GString *const tmp = ixphone_str (country_code, str1, len1);
		const gint cmp = e_strcmp2n (tmp->str, tmp->len, str2, len2);
		g_string_free (tmp, TRUE);
		return cmp;
	} else {
		GString *const tmp = ixphone_str (country_code, str2, len2);
		const gint cmp = e_strcmp2n (str1, len1, tmp->str, tmp->len);
		g_string_free (tmp, TRUE);
		return cmp;
	}
}

static gint
ixphone_compare_national (gpointer data,
                          gint len1,
                          gconstpointer arg1,
                          gint len2,
                          gconstpointer arg2)
{
	const gchar *const country_code = data;
	const gchar *const str1 = arg1;
	const gchar *const str2 = arg2;
	const gchar *sep1 = memchr (str1, '|', len1);
	const gchar *sep2 = memchr (str2, '|', len2);

	gint cmp;

	g_return_val_if_fail (sep1 != NULL, 0);
	g_return_val_if_fail (sep2 != NULL, 0);

	/* First only check national portions */
	cmp = e_strcmp2n (
		sep1 + 1, len1 - (sep1 + 1 - str1),
		sep2 + 1, len2 - (sep2 + 1 - str2));

	/* On match we also have to check for potential country codes.
	 * Note that we can't just compare the full phone number string
	 * in the case that both numbers have a country code, because
	 * this would break the collations sorting order. As a result
	 * the binary search performed on the index would miss matches.
	 * Consider the index contains "|2215423789" and "+31|2215423789"
	 * while we look for "+1|2215423789". By performing full string
	 * compares in case of fully qualified numbers, we might check
	 * "+31|2215423789" first and then miss "|2215423789" because
	 * we traverse the binary tree in wrong direction.
	 */
	if (cmp == 0) {
		if (sep1 == str1) {
			if (sep2 != str2)
				cmp = e_strcmp2n (country_code, strlen (country_code), str2, sep2 - str2);
		} else if (sep2 == str2) {
			cmp = e_strcmp2n (str1, sep1 - str1, country_code, 2);
		} else {
			/* Also compare the country code if the national number
			 * matches and both numbers have a country code. */
			cmp = e_strcmp2n (str1, sep1 - str1, str2, sep2 - str2);
		}
	}

	if (booksql_debug ()) {
		gchar *const tmp1 = g_strndup (str1, len1);
		gchar *const tmp2 = g_strndup (str2, len2);

		g_printerr
			("  DEBUG %s('%s', '%s') = %d\n",
			 G_STRFUNC, tmp1, tmp2, cmp);

		g_free (tmp2);
		g_free (tmp1);
	}

	return cmp;
}

static void
create_collation (gpointer data,
                  sqlite3 *db,
                  gint encoding,
                  const gchar *name)
{
	gint ret = SQLITE_DONE;
	gint country_code;

	g_warn_if_fail (encoding == SQLITE_UTF8);

	if  (1 == sscanf (name, "ixphone_%d", &country_code)) {
		ret = sqlite3_create_collation (
			db, name, SQLITE_UTF8, GINT_TO_POINTER (country_code),
			ixphone_compare_for_country);
	} else if (strcmp (name, "ixphone_national") == 0) {
		country_code = e_phone_number_get_country_code_for_region (NULL);

		ret = sqlite3_create_collation_v2 (
			db, name, SQLITE_UTF8,
			g_strdup_printf ("+%d", country_code),
			ixphone_compare_national, g_free);
	}

	if (ret == SQLITE_OK) {
		CollationInfo info = { db, name };
		GError *error = NULL;

		if (!book_backend_sql_exec (
			db, "SELECT folder_id FROM folders",
			create_phone_indexes_for_tables, &info, &error)) {
			g_warning ("%s(%s): %s", G_STRFUNC, name, error->message);
			g_error_free (error);
		}
	} else if (ret != SQLITE_DONE) {
		g_warning ("%s(%s): %s", G_STRFUNC, name, sqlite3_errmsg (db));
	}
}

static void
ebsdb_regexp (sqlite3_context *context, 
	      int argc, 
	      sqlite3_value **argv)
{
	GRegex *regex;
	const gchar *expression;
	const gchar *text;

	/* Reuse the same GRegex for all REGEXP queries with the same expression */
	regex = sqlite3_get_auxdata (context, 0);
	if (!regex) {
		GError *error = NULL;

		expression = (const gchar *)sqlite3_value_text (argv[0]);

		regex = g_regex_new (expression, 0, 0, &error);

		if (!regex) {
			sqlite3_result_error (context, error->message, -1);
			g_error_free (error);
			return;
		}

		/* SQLite will take care of freeing the GRegex when we're done with the query */
		sqlite3_set_auxdata (context, 0, regex, (void(*)(void*))g_regex_unref);
	}

	/* Now perform the comparison */
	text = (const gchar *)sqlite3_value_text (argv[1]);
	if (text != NULL) {
		gboolean match;

		match = g_regex_match (regex, text, 0, NULL);
		sqlite3_result_int (context, match ? 1 : 0);
	}
}

static gboolean
book_backend_sqlitedb_load (EBookBackendSqliteDB *ebsdb,
                            const gchar *filename,
                            gint *previous_schema,
                            GError **error)
{
	gint ret;

	e_sqlite3_vfs_init ();

	ret = sqlite3_open (filename, &ebsdb->priv->db);

	if (ret == SQLITE_OK)
		ret = sqlite3_collation_needed (ebsdb->priv->db, ebsdb, create_collation);

	if (ret == SQLITE_OK)
		ret = sqlite3_create_function (ebsdb->priv->db, "regexp", 2, SQLITE_UTF8, ebsdb,
					       ebsdb_regexp, NULL, NULL);

	if (ret != SQLITE_OK) {
		if (!ebsdb->priv->db) {
			g_set_error (
				error, E_BOOK_SDB_ERROR,
				E_BOOK_SDB_ERROR_OTHER,
				_("Insufficient memory"));
		} else {
			const gchar *errmsg;
			errmsg = sqlite3_errmsg (ebsdb->priv->db);
			d (g_printerr ("Can't open database %s: %s\n", path, errmsg));
			g_set_error_literal (
				error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_OTHER, errmsg);
			sqlite3_close (ebsdb->priv->db);
		}
		return FALSE;
	}

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

	return create_folders_table (ebsdb, previous_schema, error);
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
                                      IndexFlags attr_list_indexes,
				      const gchar *initial_collations,
                                      GError **error)
{
	EBookBackendSqliteDB *ebsdb;
	gchar *hash_key, *filename;
	gint previous_schema = 0;
	gboolean already_exists = FALSE;

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
			g_free (hash_key);
			goto exit;
		}
	}

	ebsdb = g_object_new (E_TYPE_BOOK_BACKEND_SQLITEDB, NULL);
	ebsdb->priv->path = g_strdup (path);
	ebsdb->priv->summary_fields = fields;
	ebsdb->priv->n_summary_fields = n_fields;
	ebsdb->priv->have_attr_list = have_attr_list;
	ebsdb->priv->attr_list_indexes = attr_list_indexes;
	ebsdb->priv->store_vcard = store_vcard;

	if (g_mkdir_with_parents (path, 0777) < 0) {
		g_mutex_unlock (&dbcon_lock);
		g_object_unref (ebsdb);
		g_set_error (
			error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_OTHER,
			"Can not make parent directory: errno %d", errno);
		return NULL;
	}
	filename = g_build_filename (path, DB_FILENAME, NULL);

	if (!book_backend_sqlitedb_load (ebsdb, filename, &previous_schema, error)) {
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

 exit:
	/* While the global dbcon_lock is held, hold the ebsdb specific lock and
	 * prolong the locking on that instance until the folders are all set up
	 */
	LOCK_MUTEX (&ebsdb->priv->lock);
	g_mutex_unlock (&dbcon_lock);

	if (!add_folder_into_db (ebsdb, folderid, folder_name, initial_collations,
				 &already_exists, error)) {
		UNLOCK_MUTEX (&ebsdb->priv->lock);
		g_object_unref (ebsdb);
		return NULL;
	}

	if (!create_contacts_table (ebsdb, folderid, previous_schema, already_exists, error)) {
		UNLOCK_MUTEX (&ebsdb->priv->lock);
		g_object_unref (ebsdb);
		return NULL;
	}

	UNLOCK_MUTEX (&ebsdb->priv->lock);

	return ebsdb;
}

static SummaryField *
append_summary_field (GArray *array,
                      EContactField field,
                      gboolean *have_attr_list,
                      GError **error)
{
	const gchar *dbname = NULL;
	GType        type = G_TYPE_INVALID;
	gint         i;
	SummaryField new_field = { 0, };

	if (field < 1 || field >= E_CONTACT_FIELD_LAST) {
		g_set_error (
			error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_OTHER,
			_("Invalid contact field '%d' specified in summary"), field);
		return NULL;
	}

	/* Avoid including the same field twice in the summary */
	for (i = 0; i < array->len; i++) {
		SummaryField *iter = &g_array_index (array, SummaryField, i);
		if (field == iter->field)
			return iter;
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
		g_set_error (
			error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_OTHER,
			_("Contact field '%s' of type '%s' specified in summary, "
			"but only boolean, string and string list field types are supported"),
			e_contact_pretty_name (field), g_type_name (type));
		return NULL;
	}

	if (type == E_TYPE_CONTACT_ATTR_LIST && have_attr_list)
		*have_attr_list = TRUE;

	new_field.field  = field;
	new_field.dbname = dbname;
	new_field.type   = type;
	new_field.index  = 0;
	g_array_append_val (array, new_field);

	return &g_array_index (array, SummaryField, array->len - 1);
}

static void
summary_fields_add_indexes (GArray *array,
                            EContactField *indexes,
                            EBookIndexType *index_types,
                            gint n_indexes,
                            IndexFlags *attr_list_indexes)
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
						*attr_list_indexes |= INDEX_PREFIX;
					break;
				case E_BOOK_INDEX_SUFFIX:
					sfield->index |= INDEX_SUFFIX;

					if (sfield->type == E_TYPE_CONTACT_ATTR_LIST)
						*attr_list_indexes |= INDEX_SUFFIX;
					break;
				case E_BOOK_INDEX_PHONE:
					sfield->index |= INDEX_PHONE;

					if (sfield->type == E_TYPE_CONTACT_ATTR_LIST)
						*attr_list_indexes |= INDEX_PHONE;
					break;
				default:
					g_warn_if_reached ();
					break;
				}
			}
		}
	}
}

static gboolean
ensure_collation (SummaryField  *field,
		  GHashTable    *col_hash,
		  const gchar   *collation_name,
		  GError       **error)
{
	ECollator *collator;
	const gchar *lc_collate;

	collator = g_hash_table_lookup (col_hash, collation_name);

	if (!collator) {
		lc_collate = get_current_collation_locale ();

		collator = e_collator_new (lc_collate, collation_name, error);
		if (collator)
			g_hash_table_insert (col_hash,
					     g_strdup (collation_name),
					     collator);
	}

	if (collator)
		field->collator = e_collator_ref (collator);

	return (collator != NULL);
}

static gboolean
summary_fields_add_collations (SummaryField   *summary_fields,
			       gint            n_summary_fields,
			       EContactField  *fields,
			       gchar         **collations,
			       gint            n_fields,
			       GError        **error)
{
	GHashTable *col_hash;
	gboolean  success = TRUE;
	gint i, j;
	ECollator *collator;
	const gchar *lc_collate;

	col_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
					  (GDestroyNotify)g_free,
					  (GDestroyNotify)e_collator_unref);

	lc_collate = get_current_collation_locale ();
	collator = e_collator_new (lc_collate, NULL, error);

	if (!collator)
		success = FALSE;

	for (i = 0; success && i < n_summary_fields; i++) {
		for (j = 0; j < n_fields; j++) {

			if (summary_fields[i].field == fields[j]) {
				success = ensure_collation (&(summary_fields[i]),
							    col_hash,
							    collations[j],
							    error);
				break;
			}
		}
	}

	/* Fill any gaps with the default collator */
	for (i = 0; success && i < n_summary_fields; i++) {

		if (summary_fields[i].collator == NULL)
			summary_fields[i].collator = e_collator_ref (collator);
	}

	g_hash_table_destroy (col_hash);
	if (collator)
		e_collator_unref (collator);

	return success;
}

static void
clear_collations (SummaryField *fields,
		  gint          n_fields)
{
	gint i;

	for (i = 0; i < n_fields; i++) {
		if (fields[i].collator)
			e_collator_unref (fields[i].collator);
	}
}

static gchar *
format_collations (EContactField *coll_fields,
		   gchar        **collations,
		   gint           n_fields)
{
	GString *string = g_string_new ("");
	gint i;

	for (i = 0; i < n_fields; i++) {
		if (i > 0)
			g_string_append_c (string, ':');

		g_string_append_printf (string, "%s=%s",
					e_contact_field_name (coll_fields[i]),
					collations[i]);
	}

	return g_string_free (string, FALSE);
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
	EContactField *coll_fields;
	EBookIndexType *index_types = NULL;
	gchar **collations = NULL, *initial_collations;
	gboolean have_attr_list = FALSE;
	IndexFlags attr_list_indexes = 0;
	gboolean had_error = FALSE;
	GArray *summary_fields;
	gint n_fields = 0, n_indexed_fields = 0, n_coll_fields = 0, i;

	fields         = e_source_backend_summary_setup_get_summary_fields (setup, &n_fields);
	indexed_fields = e_source_backend_summary_setup_get_indexed_fields (setup, &index_types, &n_indexed_fields);
	coll_fields    = e_source_backend_summary_setup_get_collations     (setup, &collations, &n_coll_fields);

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

	/* Add the specialized collation types, populate the ECollators */
	if (!had_error)
		had_error = !summary_fields_add_collations ((SummaryField *)summary_fields->data,
							    summary_fields->len,
							    coll_fields,
							    collations,
							    n_coll_fields,
							    error);

	if (had_error) {

		clear_collations ((SummaryField *)summary_fields->data, summary_fields->len);
		g_array_free (summary_fields, TRUE);
		g_free (fields);
		g_free (index_types);
		g_free (indexed_fields);
		return NULL;
	}

	/* Add the 'indexed' flag to the SummaryField structs */
	summary_fields_add_indexes (
		summary_fields, indexed_fields, index_types, n_indexed_fields,
		&attr_list_indexes);

	initial_collations = format_collations (coll_fields, collations, n_coll_fields);

	ebsdb = e_book_backend_sqlitedb_new_internal (
		path, emailid, folderid, folder_name,
		store_vcard,
		(SummaryField *) summary_fields->data,
		summary_fields->len,
		have_attr_list,
		attr_list_indexes,
		initial_collations,
		error);

	g_free (fields);
	g_free (index_types);
	g_free (indexed_fields);
	g_free (initial_collations);
	g_free (coll_fields);
	g_strfreev (collations);
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
	IndexFlags attr_list_indexes = 0;
	gint i;

	/* Create the default summary structs */
	summary_fields = g_array_new (FALSE, FALSE, sizeof (SummaryField));
	for (i = 0; i < G_N_ELEMENTS (default_summary_fields); i++)
		append_summary_field (summary_fields, default_summary_fields[i], &have_attr_list, NULL);

	/* Add the default index flags */
	summary_fields_add_indexes (
		summary_fields,
		default_indexed_fields,
		default_index_types,
		G_N_ELEMENTS (default_indexed_fields),
		&attr_list_indexes);

	/* Add the default collator to all columns */
	summary_fields_add_collations ((SummaryField *)summary_fields->data,
				       summary_fields->len, NULL, NULL, 0, NULL);

	ebsdb = e_book_backend_sqlitedb_new_internal (
		path, emailid, folderid, folder_name,
		store_vcard,
		(SummaryField *) summary_fields->data,
		summary_fields->len,
		have_attr_list,
		attr_list_indexes,
		"", error);

	g_array_free (summary_fields, FALSE);

	return ebsdb;
}

gboolean
e_book_backend_sqlitedb_lock_updates (EBookBackendSqliteDB *ebsdb,
                                      GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);

	LOCK_MUTEX (&ebsdb->priv->updates_lock);

	LOCK_MUTEX (&ebsdb->priv->lock);
	success = book_backend_sqlitedb_start_transaction (ebsdb, error);
	UNLOCK_MUTEX (&ebsdb->priv->lock);

	return success;
}

gboolean
e_book_backend_sqlitedb_unlock_updates (EBookBackendSqliteDB *ebsdb,
                                        gboolean do_commit,
                                        GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);

	LOCK_MUTEX (&ebsdb->priv->lock);
	success = do_commit ?
		book_backend_sqlitedb_commit_transaction (ebsdb, error) :
		book_backend_sqlitedb_rollback_transaction (ebsdb, error);
	UNLOCK_MUTEX (&ebsdb->priv->lock);

	UNLOCK_MUTEX (&ebsdb->priv->updates_lock);

	return success;
}

static gchar *
mprintf_suffix (const gchar *normal)
{
	gchar *reverse = normal ? g_utf8_strreverse (normal, -1) : NULL;
	gchar *stmt = sqlite3_mprintf ("%Q", reverse);

	g_free (reverse);
	return stmt;
}

static EPhoneNumber *
phone_number_from_string (const gchar *normal,
                          const gchar *default_region)
{
	EPhoneNumber *number = NULL;

	/* Don't warn about erronous phone number strings, it's a perfectly normal
	 * use case for users to enter notes instead of phone numbers in the phone
	 * number contact fields, such as "Ask Jenny for Lisa's phone number"
	 */
	if (normal && e_phone_number_is_supported ())
		number = e_phone_number_from_string (normal, default_region, NULL);

	return number;
}

static gchar *
convert_phone (const gchar *normal,
               const gchar *default_region)
{
	EPhoneNumber *number = phone_number_from_string (normal, default_region);
	gchar *indexed_phone_number = NULL;
	gchar *national_number = NULL;
	gint country_code = 0;

	if (number) {
		EPhoneNumberCountrySource source;

		national_number = e_phone_number_get_national_number (number);
		country_code = e_phone_number_get_country_code (number, &source);
		e_phone_number_free (number);

		if (source == E_PHONE_NUMBER_COUNTRY_FROM_DEFAULT)
			country_code = 0;
	}

	if (national_number) {
		indexed_phone_number = country_code
			? g_strdup_printf ("+%d|%s", country_code, national_number)
			: g_strconcat ("|", national_number, NULL);

		g_free (national_number);
	}

	return indexed_phone_number;
}

static gchar *
mprintf_phone (const gchar *normal,
               const gchar *default_region)
{
	gchar *phone = convert_phone (normal, default_region);
	gchar *stmt = NULL;

	if (phone) {
		stmt = sqlite3_mprintf ("%Q", phone);
		g_free (phone);
	}

	return stmt;
}

/* Add Contact (free the result with g_free() ) */
static gchar *
insert_stmt_from_contact (EBookBackendSqliteDB *ebsdb,
                          EContact *contact,
                          const gchar *folderid,
                          gboolean store_vcard,
                          gboolean replace_existing,
                          const gchar *default_region)
{
	GString *string;
	gchar *str, *vcard_str;
	gint i;

	str = sqlite3_mprintf ("INSERT or %s INTO %Q (",
			       replace_existing ? "REPLACE" : "FAIL", folderid);
	string = g_string_new (str);
	sqlite3_free (str);

	/*
	 * First specify the column names for the insert, since it's possible we
	 * upgraded the DB and cannot be sure the order of the columns are ordered
	 * just how we like them to be.
	 */
	for (i = 0; i < ebsdb->priv->n_summary_fields; i++) {

		/* Multi values go into a separate table/statement */
		if (ebsdb->priv->summary_fields[i].type != E_TYPE_CONTACT_ATTR_LIST) {

			if (i > 0)
				g_string_append (string, ", ");

			g_string_append (string, ebsdb->priv->summary_fields[i].dbname);
		}

		if (ebsdb->priv->summary_fields[i].type == G_TYPE_STRING) {

			if (ebsdb->priv->summary_fields[i].field != E_CONTACT_UID &&
			    ebsdb->priv->summary_fields[i].field != E_CONTACT_REV) {
				g_string_append (string, ", ");
				g_string_append (string, ebsdb->priv->summary_fields[i].dbname);
				g_string_append (string, "_localized");
			}

			if ((ebsdb->priv->summary_fields[i].index & INDEX_SUFFIX) != 0) {
				g_string_append (string, ", ");
				g_string_append (string, ebsdb->priv->summary_fields[i].dbname);
				g_string_append (string, "_reverse");
			}

			if ((ebsdb->priv->summary_fields[i].index & INDEX_PHONE) != 0) {
				g_string_append (string, ", ");
				g_string_append (string, ebsdb->priv->summary_fields[i].dbname);
				g_string_append (string, "_phone");
			}
		}
	}
	g_string_append (string, ", vcard, bdata)");

	/*
	 * Now specify values for all of the column names we specified.
	 */
	g_string_append (string, " VALUES (");
	for (i = 0; i < ebsdb->priv->n_summary_fields; i++) {
		if (ebsdb->priv->summary_fields[i].type == G_TYPE_STRING) {
			gchar *val;
			gchar *normal;
			gchar *localized = NULL;

			if (i > 0)
				g_string_append (string, ", ");

			val = e_contact_get (contact, ebsdb->priv->summary_fields[i].field);

			/* Special exception, never normalize/localize the UID or REV string */
			if (ebsdb->priv->summary_fields[i].field != E_CONTACT_UID &&
			    ebsdb->priv->summary_fields[i].field != E_CONTACT_REV) {
				normal = e_util_utf8_normalize (val);

				if (val)
					localized = e_collator_generate_key (ebsdb->priv->summary_fields[i].collator, val, NULL);
				else
					localized = g_strdup ("");
			} else
				normal = g_strdup (val);

			str = sqlite3_mprintf ("%Q", normal);
			g_string_append (string, str);
			sqlite3_free (str);

			if (ebsdb->priv->summary_fields[i].field != E_CONTACT_UID &&
			    ebsdb->priv->summary_fields[i].field != E_CONTACT_REV) {
				str = sqlite3_mprintf ("%Q", localized);
				g_string_append (string, ", ");
				g_string_append (string, str);
				sqlite3_free (str);
			}

			if ((ebsdb->priv->summary_fields[i].index & INDEX_SUFFIX) != 0) {
				str = mprintf_suffix (normal);
				g_string_append (string, ", ");
				g_string_append (string, str);
				sqlite3_free (str);
			}

			if ((ebsdb->priv->summary_fields[i].index & INDEX_PHONE) != 0) {
				str = mprintf_phone (normal, default_region);
				g_string_append (string, ", ");
				g_string_append (string, str ? str : "NULL");
				sqlite3_free (str);
			}

			g_free (normal);
			g_free (val);
			g_free (localized);
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

static void
update_e164_attribute_params (EVCard *vcard,
			      const gchar *default_region)
{
	GList *attr_list;

	for (attr_list = e_vcard_get_attributes (vcard); attr_list; attr_list = attr_list->next) {
		EVCardAttribute *const attr = attr_list->data;
		EVCardAttributeParam *param = NULL;
		gchar *e164 = NULL, *cc, *nn;
		GList *param_list, *values;

		/* We only attach E164 parameters to TEL attributes. */
		if (strcmp (e_vcard_attribute_get_name (attr), EVC_TEL) != 0)
			continue;

		/* Compute E164 number. */
		values = e_vcard_attribute_get_values (attr);

		e164 = values && values->data
			? convert_phone (values->data, default_region)
			: NULL;

		if (e164 == NULL) {
			e_vcard_attribute_remove_param (attr, EVC_X_E164);
			continue;
		}

		/* Find already exisiting parameter, so that we can reuse it. */
		for (param_list = e_vcard_attribute_get_params (attr); param_list; param_list = param_list->next) {
			if (strcmp (e_vcard_attribute_param_get_name (param_list->data), EVC_X_E164) == 0) {
				param = param_list->data;
				break;
			}
		}

		/* Create a new parameter instance if needed. Otherwise clean
		 * the existing parameter's values: This is much cheaper than
		 * checking for modifications. */
		if (param == NULL) {
			param = e_vcard_attribute_param_new (EVC_X_E164);
			e_vcard_attribute_add_param (attr, param);
		} else {
			e_vcard_attribute_param_remove_values (param);
		}

		/* Split the phone number into country calling code and
		 * national number code. */
		nn = strchr (e164, '|');

		if (nn == NULL) {
			g_warn_if_reached ();
			continue;
		}

		*nn++ = '\0';
		cc = e164;

		/* Assign the parameter values. It seems odd that we revert
		 * the order of NN and CC, but at least EVCard's parser doesn't
		 * permit an empty first param value. Which of course could be
		 * fixed - in order to create a nice potential IOP problem with
		 ** other vCard parsers. */
		e_vcard_attribute_param_add_values (param, nn, cc, NULL);

		g_free (e164);
	}
}

static gboolean
insert_contact (EBookBackendSqliteDB *ebsdb,
                EContact *contact,
                const gchar *folderid,
                gboolean replace_existing,
                const gchar *default_region,
                GError **error)
{
	EBookBackendSqliteDBPrivate *priv;
	gboolean success;
	gchar *stmt;

	priv = ebsdb->priv;

	/* Update E.164 parameters in vcard if needed */
	if (priv->store_vcard)
		update_e164_attribute_params (E_VCARD (contact), default_region);

	/* Update main summary table */
	stmt = insert_stmt_from_contact (ebsdb, contact, folderid, priv->store_vcard, replace_existing, default_region);
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
				gchar *value = (gchar *) l->data;
				gchar *normal = e_util_utf8_normalize (value);
				gchar *stmt_suffix = NULL;
				gchar *stmt_phone = NULL;

				if ((priv->attr_list_indexes & INDEX_SUFFIX) != 0
					&& (priv->summary_fields[i].index & INDEX_SUFFIX) != 0)
					stmt_suffix = mprintf_suffix (normal);

				if ((priv->attr_list_indexes & INDEX_PHONE) != 0
					&& (priv->summary_fields[i].index & INDEX_PHONE) != 0)
					stmt_phone = mprintf_phone (normal, default_region);

				stmt = sqlite3_mprintf (
					"INSERT INTO %Q (uid, field, value%s%s) "
					"VALUES (%Q, %Q, %Q%s%s%s%s)",
					list_folder,
					stmt_suffix ? ", value_reverse" : "",
					stmt_phone ? ", value_phone" : "",
					uid, priv->summary_fields[i].dbname, normal,
					stmt_suffix ? ", " : "",
					stmt_suffix ? stmt_suffix : "",
					stmt_phone ? ", " : "",
					stmt_phone ? stmt_phone : "");

				if (stmt_suffix)
					sqlite3_free (stmt_suffix);
				if (stmt_phone)
					sqlite3_free (stmt_phone);

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
 * e_book_backend_sqlitedb_new_contact
 * @ebsdb: An #EBookBackendSqliteDB
 * @folderid: folder id
 * @contact: EContact to be added
 * @replace_existing: Whether this contact should replace another contact with the same UID.
 * @error: A location to store any error that may have occurred.
 *
 * This is a convenience wrapper for e_book_backend_sqlitedb_new_contacts,
 * which is the preferred means to add or modify multiple contacts when possible.
 *
 * Returns: TRUE on success.
 *
 * Since: 3.8
 **/
gboolean
e_book_backend_sqlitedb_new_contact (EBookBackendSqliteDB *ebsdb,
                                     const gchar *folderid,
                                     EContact *contact,
                                     gboolean replace_existing,
                                     GError **error)
{
	GSList l;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	l.data = contact;
	l.next = NULL;

	return e_book_backend_sqlitedb_new_contacts (
		ebsdb, folderid, &l,
		replace_existing, error);
}

/**
 * e_book_backend_sqlitedb_new_contacts
 * @ebsdb: An #EBookBackendSqliteDB
 * @folderid: folder id
 * @contacts: list of EContacts
 * @replace_existing: Whether this contact should replace another contact with the same UID.
 * @error: A location to store any error that may have occurred.
 *
 * Adds or replaces contacts in @ebsdb. If @replace_existing is specified then existing
 * contacts with the same UID will be replaced, otherwise adding an existing contact
 * will return an error.
 *
 * Returns: TRUE on success.
 *
 * Since: 3.8
 **/
gboolean
e_book_backend_sqlitedb_new_contacts (EBookBackendSqliteDB *ebsdb,
                                      const gchar *folderid,
                                      GSList *contacts,
                                      gboolean replace_existing,
                                      GError **error)
{
	GSList *l;
	gboolean success = TRUE;
	gchar *default_region = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);
	g_return_val_if_fail (contacts != NULL, FALSE);

	LOCK_MUTEX (&ebsdb->priv->lock);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error)) {
		UNLOCK_MUTEX (&ebsdb->priv->lock);
		return FALSE;
	}

	if (e_phone_number_is_supported ())
		default_region = e_phone_number_get_default_region ();

	for (l = contacts; success && l != NULL; l = g_slist_next (l)) {
		EContact *contact = (EContact *) l->data;

		success = insert_contact (
			ebsdb, contact, folderid, replace_existing,
			default_region, error);
	}

	g_free (default_region);

	if (success)
		success = book_backend_sqlitedb_commit_transaction (ebsdb, error);
	else
		/* The GError is already set. */
		book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

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
 *
 * Deprecated: 3.8: Use e_book_backend_sqlitedb_new_contact() instead.
 **/
gboolean
e_book_backend_sqlitedb_add_contact (EBookBackendSqliteDB *ebsdb,
                                     const gchar *folderid,
                                     EContact *contact,
                                     gboolean partial_content,
                                     GError **error)
{
	return e_book_backend_sqlitedb_new_contact (ebsdb, folderid, contact, TRUE, error);
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
 *
 * Deprecated: 3.8: Use e_book_backend_sqlitedb_new_contacts() instead.
 **/
gboolean
e_book_backend_sqlitedb_add_contacts (EBookBackendSqliteDB *ebsdb,
                                      const gchar *folderid,
                                      GSList *contacts,
                                      gboolean partial_content,
                                      GError **error)
{
	return e_book_backend_sqlitedb_new_contacts (ebsdb, folderid, contacts, TRUE, error);
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
generate_delete_stmt (const gchar *table,
                      GSList *uids)
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
	gboolean success = TRUE;
	gchar *stmt;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (folderid != NULL, FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	LOCK_MUTEX (&ebsdb->priv->lock);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error)) {
		UNLOCK_MUTEX (&ebsdb->priv->lock);
		return FALSE;
	}

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
		success = book_backend_sqlitedb_commit_transaction (ebsdb, error);
	else
		/* The GError is already set. */
		book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

	return success;
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

	LOCK_MUTEX (&ebsdb->priv->lock);

	stmt = sqlite3_mprintf (
		"SELECT partial_content FROM %Q WHERE uid = %Q",
		folderid, uid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, contact_found_cb , &cinfo, error);
	sqlite3_free (stmt);

	if (success)
		*partial_content = cinfo.partial_content;

	UNLOCK_MUTEX (&ebsdb->priv->lock);

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

static gboolean
uid_rev_fields (GHashTable *fields_of_interest)
{
	GHashTableIter iter;
	gpointer key, value;

	if (!fields_of_interest || g_hash_table_size (fields_of_interest) > 2)
		return FALSE;

	g_hash_table_iter_init (&iter, fields_of_interest);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar *field_name = key;
		EContactField field = e_contact_field_id (field_name);

		if (field != E_CONTACT_UID &&
		    field != E_CONTACT_REV)
			return FALSE;
	}

	return TRUE;
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

	LOCK_MUTEX (&ebsdb->priv->lock);

	g_hash_table_iter_init (&iter, fields_of_interest);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar  *field_name = key;
		EContactField field      = e_contact_field_id (field_name);

		if (summary_dbname_from_field (ebsdb, field) == NULL) {
			summary_fields = FALSE;
			break;
		}
	}

	UNLOCK_MUTEX (&ebsdb->priv->lock);

	return summary_fields;
}

/* free return value with g_free */
static gchar *
summary_select_stmt (GHashTable *fields_of_interest,
                     gboolean distinct)
{
	GString *string;

	if (distinct)
		string = g_string_new ("SELECT DISTINCT summary.uid");
	else
		string = g_string_new ("SELECT summary.uid");

	/* Add the E_CONTACT_REV field if they are both requested */
	if (g_hash_table_size (fields_of_interest) == 2)
		g_string_append (string, ", Rev");

	return g_string_free (string, FALSE);
}

static gint
store_data_to_vcard (gpointer ref,
                     gint ncol,
                     gchar **cols,
                     gchar **name)
{
	GSList **vcard_data = ref;
	EbSdbSearchData *search_data = g_slice_new0 (EbSdbSearchData);
	EContact *contact = e_contact_new ();
	gchar *vcard;
	gint i;

	/* parse through cols, this will be useful if the api starts supporting field restrictions */
	for (i = 0; i < ncol; i++)
	{
		if (!name[i] || !cols[i])
			continue;

		/* Only UID & REV can be used to create contacts from the summary columns */
		if (!g_ascii_strcasecmp (name[i], "uid")) {
			e_contact_set (contact, E_CONTACT_UID, cols[i]);
			search_data->uid = g_strdup (cols[i]);
		} else if (!g_ascii_strcasecmp (name[i], "Rev")) {
			e_contact_set (contact, E_CONTACT_REV, cols[i]);
		} else if (!g_ascii_strcasecmp (name[i], "bdata"))
			search_data->bdata = g_strdup (cols[i]);
	}

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	search_data->vcard = vcard;
	*vcard_data = g_slist_prepend (*vcard_data, search_data);

	g_object_unref (contact);
	return 0;
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

	LOCK_MUTEX (&ebsdb->priv->lock);

	/* Try constructing contacts from only UID/REV first if that's requested */
	if (uid_rev_fields (fields_of_interest)) {
		GSList *vcards = NULL;
		gchar *select_portion;

		select_portion = summary_select_stmt (fields_of_interest, FALSE);

		stmt = sqlite3_mprintf (
			"%s FROM %Q AS summary WHERE summary.uid = %Q",
			select_portion, folderid, uid);
		book_backend_sql_exec (ebsdb->priv->db, stmt, store_data_to_vcard, &vcards, error);
		sqlite3_free (stmt);
		g_free (select_portion);

		if (vcards) {
			EbSdbSearchData *s_data = (EbSdbSearchData *) vcards->data;

			vcard_str = s_data->vcard;
			s_data->vcard = NULL;

			g_slist_free_full (vcards, destroy_search_data);
			vcards = NULL;
		}

		local_with_all_required_fields = TRUE;
	} else if (ebsdb->priv->store_vcard) {
		stmt = sqlite3_mprintf (
			"SELECT vcard FROM %Q WHERE uid = %Q", folderid, uid);
		book_backend_sql_exec (
			ebsdb->priv->db, stmt,
			get_vcard_cb , &vcard_str, error);
		sqlite3_free (stmt);

		local_with_all_required_fields = TRUE;
	} else {
		g_set_error (
			error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_OTHER,
			_("Full search_contacts are not stored in cache. vcards cannot be returned."));

	}

	UNLOCK_MUTEX (&ebsdb->priv->lock);

	if (with_all_required_fields)
		*with_all_required_fields = local_with_all_required_fields;

	if (!vcard_str && error && !*error)
		g_set_error (
			error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_CONTACT_NOT_FOUND,
			_("Contact '%s' not found"), uid ? uid : "NULL");

	return vcard_str;
}

enum {
	CHECK_IS_SUMMARY   = (1 << 0),
	CHECK_IS_LIST_ATTR = (1 << 1),
	CHECK_UNSUPPORTED  = (1 << 2),
	CHECK_INVALID      = (1 << 3)
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
		result &= ~CHECK_IS_SUMMARY;

	r = e_sexp_result_new (f, ESEXP_RES_INT);
	r->value.number = result;

	return r;
}

static gint
func_check_field_test (EBookBackendSqliteDB *ebsdb,
                        const gchar *query_name,
                        const gchar *query_value)
{
	gint i;
	gint ret_val = 0;

	if (ebsdb) {
		for (i = 0; i < ebsdb->priv->n_summary_fields; i++) {
			if (!g_ascii_strcasecmp (e_contact_field_name (ebsdb->priv->summary_fields[i].field), query_name)) {
				ret_val |= CHECK_IS_SUMMARY;

				if (ebsdb->priv->summary_fields[i].type == E_TYPE_CONTACT_ATTR_LIST)
					ret_val |= CHECK_IS_LIST_ATTR;

				break;
			}
		}
	} else {
		for (i = 0; i < G_N_ELEMENTS (default_summary_fields); i++) {
			if (!g_ascii_strcasecmp (e_contact_field_name (default_summary_fields[i]), query_name)) {
				ret_val |= CHECK_IS_SUMMARY;

				if (e_contact_field_type (default_summary_fields[i]) == E_TYPE_CONTACT_ATTR_LIST)
					ret_val |= CHECK_IS_LIST_ATTR;

				break;
			}
		}
	}

	return ret_val;
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

		/* Special case, when testing the special symbolic 'any field' we can
		 * consider it a summary query (it's similar to a 'no query'). */
		if (g_strcmp0 (query_name, "x-evolution-any-field") == 0 &&
		    g_strcmp0 (query_value, "") == 0) {
			ret_val |= CHECK_IS_SUMMARY;
			goto check_finish;
		}

		ret_val |= func_check_field_test (ebsdb, query_name, query_value);
	} else if (argc == 3
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING
	    && argv[2]->type == ESEXP_RES_STRING) {
		const gchar *query_name = argv[0]->value.string;
		const gchar *query_value = argv[1]->value.string;
		ret_val |= func_check_field_test (ebsdb, query_name, query_value);
	}

 check_finish:
	r = e_sexp_result_new (f, ESEXP_RES_INT);
	r->value.number = ret_val;

	return r;
}

static ESExpResult *
func_check_phone (struct _ESExp *f,
                  gint argc,
                  struct _ESExpResult **argv,
                  gpointer data)
{
	ESExpResult *const r = func_check (f, argc, argv, data);
	const gchar *const query_value = argv[1]->value.string;
	EPhoneNumber *number;

	/* Here we need to catch unsupported queries and invalid queries
	 * so we perform validity checks even if func_check() reports this
	 * as not a part of the summary.
	 */
	if (!e_phone_number_is_supported ()) {
		r->value.number |= CHECK_UNSUPPORTED;
		return r;
	}

	number = e_phone_number_from_string (query_value, NULL, NULL);

	if (number == NULL) {
		/* Could not construct a phone number from the query input,
		 * an invalid query error will be propagated to the client.
		 */
		r->value.number |= CHECK_INVALID;
	} else {
		e_phone_number_free (number);
	}

	return r;
}

static ESExpResult *
func_check_regex_raw (struct _ESExp         *f,
		      gint                  argc,
		      struct _ESExpResult **argv,
		      gpointer              data)
{
	/* Raw REGEX queries are not in the summary, we only keep
	 * normalized data in the summary
	 */
	ESExpResult *r;

	r = e_sexp_result_new (f, ESEXP_RES_INT);
	r->value.number = 0;

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
	{ "exists", func_check, 0 },
	{ "eqphone", func_check_phone, 0 },
	{ "eqphone_national", func_check_phone, 0 },
	{ "eqphone_short", func_check_phone, 0 },
	{ "regex_normal", func_check, 0 },
	{ "regex_raw", func_check_regex_raw, 0 },
};

static gboolean
e_book_backend_sqlitedb_check_summary_query_locked (EBookBackendSqliteDB *ebsdb,
                                                    const gchar *query,
                                                    gboolean *with_list_attrs,
                                                    gboolean *unsupported_query,
                                                    gboolean *invalid_query)
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
		if (invalid_query)
			*invalid_query = TRUE;

		return FALSE;
	}

	r = e_sexp_eval (sexp);
	if (r && r->type == ESEXP_RES_INT) {
		retval = (r->value.number & CHECK_IS_SUMMARY) != 0;

		if (with_list_attrs)
			*with_list_attrs = (r->value.number & CHECK_IS_LIST_ATTR) != 0;

		if (unsupported_query)
			*unsupported_query = (r->value.number & CHECK_UNSUPPORTED) != 0;

		if (invalid_query)
			*invalid_query = (r->value.number & CHECK_INVALID) != 0;
	}

	e_sexp_result_free (sexp, r);
	e_sexp_unref (sexp);

	return retval;
}

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
	gboolean is_summary;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);

	LOCK_MUTEX (&ebsdb->priv->lock);
	is_summary = e_book_backend_sqlitedb_check_summary_query_locked (ebsdb, query, with_list_attrs, NULL, NULL);
	UNLOCK_MUTEX (&ebsdb->priv->lock);

	return is_summary;
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
	return e_book_backend_sqlitedb_check_summary_query_locked (NULL, query, NULL, NULL, NULL);
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
	MATCH_ENDS_WITH,
	MATCH_PHONE_NUMBER,
	MATCH_NATIONAL_PHONE_NUMBER,
	MATCH_SHORT_PHONE_NUMBER,
	MATCH_REGEX
} MatchType;

typedef enum {
	CONVERT_NOTHING   =  0,
	CONVERT_NORMALIZE = (1 << 0),
	CONVERT_REVERSE   = (1 << 1),
	CONVERT_PHONE     = (1 << 2)
} ConvertFlags;

static gchar *
extract_digits (const gchar *normal)
{
	gchar *digits = g_new (char, strlen (normal) + 1);
	const gchar *src = normal;
	gchar *dst = digits;

	/* extract digits also considering eastern arabic numerals */
	for (src = normal; *src; src = g_utf8_next_char (src)) {
		const gunichar uc = g_utf8_get_char_validated (src, -1);
		const gint value = g_unichar_digit_value (uc);

		if (uc == -1)
			break;

		if (value != -1)
			*dst++ = '0' + value;
	}

	*dst = '\0';

	return digits;
}

static gchar *
convert_string_value (EBookBackendSqliteDB *ebsdb,
		      const gchar          *value,
		      const gchar          *region,
		      ConvertFlags          flags,
		      MatchType             match)
{
	GString *str;
	size_t len;
	gchar c;
	gboolean escape_modifier_needed = FALSE;
	const gchar *escape_modifier = " ESCAPE '^'";
	gchar *computed = NULL;
	gchar *normal;
	const gchar *ptr;

	g_return_val_if_fail (value != NULL, NULL);

	if ((flags & CONVERT_NORMALIZE) && match != MATCH_REGEX)
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
	case MATCH_SHORT_PHONE_NUMBER:
		g_string_append_c (str, '%');
		break;

	case MATCH_BEGINS_WITH:
	case MATCH_IS:
	case MATCH_PHONE_NUMBER:
	case MATCH_NATIONAL_PHONE_NUMBER:
	case MATCH_REGEX:
		break;
	}

	if (flags & CONVERT_REVERSE) {
		computed = g_utf8_strreverse (normal, -1);
		ptr = computed;
	} else if (flags & CONVERT_PHONE) {
		computed = convert_phone (normal, region);
		ptr = computed;
	} else {
		ptr = normal;
	}

	while ((c = *ptr++)) {
		if (c == '\'') {
			g_string_append_c (str, '\'');
		} else if ((c == '%' || c == '^') && match != MATCH_REGEX) {
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
	case MATCH_PHONE_NUMBER:
	case MATCH_NATIONAL_PHONE_NUMBER:
	case MATCH_SHORT_PHONE_NUMBER:
	case MATCH_REGEX:
		break;
	}

	g_string_append_c (str, '\'');

	if (escape_modifier_needed)
		g_string_append (str, escape_modifier);

	g_free (computed);
	g_free (normal);

	return g_string_free (str, FALSE);
}

static gchar *
field_name_and_query_term (EBookBackendSqliteDB *ebsdb,
			   const gchar          *folderid,
			   const gchar          *field_name_input,
			   const gchar          *query_term_input,
			   const gchar          *region,
			   MatchType             match,
			   gboolean             *is_list_attr,
			   gchar               **query_term,
			   gchar               **extra_term)
{
	gint summary_index;
	gchar *field_name = NULL;
	gchar *value = NULL;
	gchar *extra = NULL;
	gboolean list_attr = FALSE;

	summary_index = summary_index_from_field_name (ebsdb, field_name_input);

	if (summary_index < 0) {
		g_critical ("Only summary field matches should be converted to sql queries");
		field_name = g_strconcat (folderid, ".", field_name_input, NULL);
		value = convert_string_value (
			ebsdb, query_term_input, region,
			CONVERT_NORMALIZE, match);
	} else {
		gboolean suffix_search = FALSE;
		gboolean phone_search = FALSE;

		/* If its a suffix search and we have reverse data to search... */
		if (match == MATCH_ENDS_WITH &&
		    (ebsdb->priv->summary_fields[summary_index].index & INDEX_SUFFIX) != 0)
			suffix_search = TRUE;

		/* If its a phone-number search and we have E.164 data to search... */
		else if ((match == MATCH_PHONE_NUMBER ||
				match == MATCH_NATIONAL_PHONE_NUMBER ||
				match == MATCH_SHORT_PHONE_NUMBER) &&
		    (ebsdb->priv->summary_fields[summary_index].index & INDEX_PHONE) != 0)
			phone_search = TRUE;

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
				field_name = g_strconcat (
					"summary.",
					ebsdb->priv->summary_fields[summary_index].dbname,
					"_reverse", NULL);

			if (ebsdb->priv->summary_fields[summary_index].field == E_CONTACT_UID ||
			    ebsdb->priv->summary_fields[summary_index].field == E_CONTACT_REV)
				value = convert_string_value (
					ebsdb, query_term_input, region, CONVERT_REVERSE,
					(match == MATCH_ENDS_WITH) ? MATCH_BEGINS_WITH : MATCH_IS);
			else
				value = convert_string_value (
					ebsdb, query_term_input, region,
					CONVERT_REVERSE | CONVERT_NORMALIZE,
					(match == MATCH_ENDS_WITH) ? MATCH_BEGINS_WITH : MATCH_IS);
		} else if (phone_search) {
			/* Special case for E.164 matching:
			 *  o Normalize the string
			 *  o Check the E.164 column instead
			 */
			const gint country_code = e_phone_number_get_country_code_for_region (region);

			if (ebsdb->priv->summary_fields[summary_index].type == E_TYPE_CONTACT_ATTR_LIST) {
				field_name = g_strdup ("multi.value_phone");
				list_attr = TRUE;
			} else {
				field_name = g_strdup_printf ("summary.%s_phone",
					ebsdb->priv->summary_fields[summary_index].dbname);
			}

			if (match == MATCH_PHONE_NUMBER) {
				value = convert_string_value (
					ebsdb, query_term_input, region,
					CONVERT_NORMALIZE | CONVERT_PHONE, match);

				extra = sqlite3_mprintf (" COLLATE ixphone_%d", country_code);
			} else {
				if (match == MATCH_NATIONAL_PHONE_NUMBER) {
					value = convert_string_value (
						ebsdb, query_term_input, region,
						CONVERT_PHONE, MATCH_NATIONAL_PHONE_NUMBER);

					extra = sqlite3_mprintf (" COLLATE ixphone_national");
				} else {
					gchar *const digits = extract_digits (query_term_input);
					value = convert_string_value (
						ebsdb, digits, region,
						CONVERT_NOTHING, MATCH_ENDS_WITH);
					g_free (digits);

					extra = sqlite3_mprintf (
						" AND (%q LIKE '|%%' OR %q LIKE '+%d|%%')",
						field_name, field_name, country_code);
				}

			}
		} else {
			if (ebsdb->priv->summary_fields[summary_index].type == E_TYPE_CONTACT_ATTR_LIST) {
				field_name = g_strdup ("multi.value");
				list_attr = TRUE;
			} else
				field_name = g_strconcat (
					"summary.",
					ebsdb->priv->summary_fields[summary_index].dbname, NULL);

			if (ebsdb->priv->summary_fields[summary_index].field == E_CONTACT_UID ||
			    ebsdb->priv->summary_fields[summary_index].field == E_CONTACT_REV) {
				value = convert_string_value (
					ebsdb, query_term_input, region,
					CONVERT_NOTHING, match);
			} else {
				value = convert_string_value (
					ebsdb, query_term_input, region,
					CONVERT_NORMALIZE, match);
			}
		}
	}

	if (is_list_attr)
		*is_list_attr = list_attr;

	*query_term = value;

	if (extra_term)
		*extra_term = extra;

	return field_name;
}

typedef struct {
	EBookBackendSqliteDB *ebsdb;
	const gchar          *folderid;
} BuildQueryData;

static const gchar *
field_oper (MatchType match)
{
	switch (match) {
	case MATCH_IS:
	case MATCH_PHONE_NUMBER:
	case MATCH_NATIONAL_PHONE_NUMBER:
		return "=";

	case MATCH_REGEX:
		return "REGEXP";

	case MATCH_CONTAINS:
	case MATCH_BEGINS_WITH:
	case MATCH_ENDS_WITH:
	case MATCH_SHORT_PHONE_NUMBER:
		break;
	}

	return "LIKE";
}

static ESExpResult *
convert_match_exp (struct _ESExp *f,
                   gint argc,
                   struct _ESExpResult **argv,
                   gpointer data,
                   MatchType match)
{
	BuildQueryData *qdata = (BuildQueryData *) data;
	EBookBackendSqliteDB *ebsdb = qdata->ebsdb;
	ESExpResult *r;
	gchar *str = NULL;

	/* are we inside a match-all? */
	if (argc > 1 && argv[0]->type == ESEXP_RES_STRING) {
		const gchar *field;

		/* only a subset of headers are supported .. */
		field = argv[0]->value.string;

		if (argv[1]->type == ESEXP_RES_STRING && argv[1]->value.string[0] != 0) {
			const gchar *const oper = field_oper (match);
			gchar *field_name, *query_term, *extra_term;

			if (!g_ascii_strcasecmp (field, "full_name")) {
				GString *names = g_string_new (NULL);

				field_name = field_name_and_query_term (
					ebsdb, qdata->folderid, "full_name",
					argv[1]->value.string, NULL,
					match, NULL, &query_term, NULL);
				g_string_append_printf (
					names, "(%s IS NOT NULL AND %s %s %s)",
					field_name, field_name, oper, query_term);
				g_free (field_name);
				g_free (query_term);

				if (summary_dbname_from_field (ebsdb, E_CONTACT_FAMILY_NAME)) {
					field_name = field_name_and_query_term (
						ebsdb, qdata->folderid, "family_name",
						argv[1]->value.string, NULL,
						match, NULL, &query_term, NULL);
					g_string_append_printf (
						names, " OR (%s IS NOT NULL AND %s %s %s)",
						field_name, field_name, oper, query_term);
					g_free (field_name);
					g_free (query_term);
				}

				if (summary_dbname_from_field (ebsdb, E_CONTACT_GIVEN_NAME)) {
					field_name = field_name_and_query_term (
						ebsdb, qdata->folderid, "given_name",
						argv[1]->value.string, NULL,
						match, NULL, &query_term, NULL);
					g_string_append_printf (
						names, " OR (%s IS NOT NULL AND %s %s %s)",
						field_name, field_name, oper, query_term);
					g_free (field_name);
					g_free (query_term);
				}

				if (summary_dbname_from_field (ebsdb, E_CONTACT_NICKNAME)) {
					field_name = field_name_and_query_term (
						ebsdb, qdata->folderid, "nickname",
						argv[1]->value.string, NULL,
						match, NULL, &query_term, NULL);
					g_string_append_printf (
						names, " OR (%s IS NOT NULL AND %s %s %s)",
						field_name, field_name, oper, query_term);
					g_free (field_name);
					g_free (query_term);
				}

				str = names->str;
				g_string_free (names, FALSE);

			} else {
				const gchar *const region =
					argc > 2 && argv[2]->type == ESEXP_RES_STRING ?
					argv[2]->value.string : NULL;

				gboolean is_list = FALSE;

				/* This should ideally be the only valid case from all the above special casing, but oh well... */
				field_name = field_name_and_query_term (
					ebsdb, qdata->folderid, field,
					argv[1]->value.string, region,
					match, &is_list, &query_term, &extra_term);

				/* User functions like eqphone_national() cannot utilize indexes. Therefore we
				 * should reduce the result set first before applying any user functions. This
				 * is done by applying a seemingly redundant suffix match first.
				 */
				if (is_list) {
					gchar *tmp;

					tmp = sqlite3_mprintf ("multi.field = %Q", field);
					str = g_strdup_printf (
						"(%s AND (%s %s %s%s))",
						tmp, field_name, oper, query_term,
						extra_term ? extra_term : "");
					sqlite3_free (tmp);
				} else
					str = g_strdup_printf (
						"(%s IS NOT NULL AND (%s %s %s%s))",
						field_name, field_name, oper, query_term,
						extra_term ? extra_term : "");

				g_free (field_name);
				g_free (query_term);

				sqlite3_free (extra_term);
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

static ESExpResult *
func_eqphone (struct _ESExp *f,
              gint argc,
              struct _ESExpResult **argv,
              gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_PHONE_NUMBER);
}

static ESExpResult *
func_eqphone_national (struct _ESExp *f,
                       gint argc,
                       struct _ESExpResult **argv,
                       gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_NATIONAL_PHONE_NUMBER);
}

static ESExpResult *
func_eqphone_short (struct _ESExp *f,
                    gint argc,
                    struct _ESExpResult **argv,
                    gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_SHORT_PHONE_NUMBER);
}

static ESExpResult *
func_regex (struct _ESExp *f,
	    gint argc,
	    struct _ESExpResult **argv,
	    gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_REGEX);
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
	{ "eqphone", func_eqphone, 0 },
	{ "eqphone_national", func_eqphone_national, 0 },
	{ "eqphone_short", func_eqphone_short, 0 },
	{ "regex_normal", func_regex, 0 }
};

static gchar *
sexp_to_sql_query (EBookBackendSqliteDB *ebsdb,
                   const gchar *folderid,
                   const gchar *query)
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
	EbSdbSearchData *s_data = g_slice_new0 (EbSdbSearchData);

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

	/* Try constructing contacts from only UID/REV first if that's requested */
	if (uid_rev_fields (fields_of_interest)) {
		gchar *select_portion;

		select_portion = summary_select_stmt (
			fields_of_interest, query_with_list_attrs);

		if (sql && sql[0]) {

			if (query_with_list_attrs) {
				gchar *list_table = g_strconcat (folderid, "_lists", NULL);

				stmt = sqlite3_mprintf (
					"%s FROM %Q AS summary "
					"LEFT OUTER JOIN %Q AS multi ON summary.uid = multi.uid WHERE %s",
					select_portion, folderid, list_table, sql);
				g_free (list_table);
			} else {
				stmt = sqlite3_mprintf (
					"%s FROM %Q AS summary WHERE %s",
					select_portion, folderid, sql);
			}

			success = book_backend_sql_exec (
				ebsdb->priv->db, stmt,
				store_data_to_vcard, &vcard_data, error);

			sqlite3_free (stmt);
		} else {
			stmt = sqlite3_mprintf ("%s FROM %Q AS summary", select_portion, folderid);
			success = book_backend_sql_exec (
				ebsdb->priv->db, stmt,
				store_data_to_vcard, &vcard_data, error);
			sqlite3_free (stmt);
		}

		local_with_all_required_fields = TRUE;
		g_free (select_portion);

	} else if (ebsdb->priv->store_vcard) {

		if (sql && sql[0]) {

			if (query_with_list_attrs) {
				gchar *list_table = g_strconcat (folderid, "_lists", NULL);

				stmt = sqlite3_mprintf (
					"SELECT DISTINCT summary.uid, vcard, bdata FROM %Q AS summary "
					"LEFT OUTER JOIN %Q AS multi ON summary.uid = multi.uid WHERE %s",
					folderid, list_table, sql);
				g_free (list_table);
			} else {
				stmt = sqlite3_mprintf (
					"SELECT uid, vcard, bdata FROM %Q as summary WHERE %s", folderid, sql);
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
	} else {
		g_set_error (
			error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_OTHER,
			_("Full search_contacts are not stored in cache. vcards cannot be returned."));
	}

	if (!success) {
		/* If sqlite failed after returning some results, we need to free them here. */
#if d(1)+0
		GSList *list = vcard_data;
		g_debug ("sqlite3 backend in %s: search query failed: %s", ebsdb->priv->path, (error && *error) ? (*error)->message : "???");

		while (list) {
			EbSdbSearchData *s_data = (EbSdbSearchData *)list->data;
			g_debug ("partial search result: %s = %s", s_data->uid, s_data->vcard);
			list = list->next;
		}
#endif
		g_slist_free_full (vcard_data, destroy_search_data);
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

	stmt = sqlite3_mprintf ("SELECT uid, vcard, bdata FROM %Q", folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, addto_vcard_list_cb , &all, error);
	sqlite3_free (stmt);

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
	gboolean query_unsupported = FALSE;
	gboolean query_invalid = FALSE;
	gboolean summary_query = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), NULL);
	g_return_val_if_fail (folderid != NULL, NULL);

	if (sexp && !*sexp)
		sexp = NULL;

	LOCK_MUTEX (&ebsdb->priv->lock);

	if (sexp)
		summary_query = e_book_backend_sqlitedb_check_summary_query_locked (
			ebsdb, sexp,
			&query_with_list_attrs,
			&query_unsupported, &query_invalid);

	if (query_unsupported)
		g_set_error (
			error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_NOT_SUPPORTED,
			_("Query contained unsupported elements"));
	else if (query_invalid)
		g_set_error (
			error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_INVALID_QUERY,
			_("Invalid Query"));
	else if (!sexp || summary_query) {
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
			error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_OTHER,
			_("Full search_contacts are not stored in cache. "
			"Hence only summary query is supported."));
	}

	UNLOCK_MUTEX (&ebsdb->priv->lock);

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
	gboolean query_unsupported = FALSE;
	gboolean summary_query = FALSE;
	gboolean query_invalid = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), NULL);
	g_return_val_if_fail (folderid != NULL, NULL);

	if (sexp && !*sexp)
		sexp = NULL;

	LOCK_MUTEX (&ebsdb->priv->lock);

	if (sexp)
		summary_query = e_book_backend_sqlitedb_check_summary_query_locked (
			ebsdb, sexp,
			&query_with_list_attrs,
			&query_unsupported,
			&query_invalid);

	if (query_unsupported)
		g_set_error (
			error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_NOT_SUPPORTED,
			_("Query contained unsupported elements"));
	else if (query_invalid)
		g_set_error (
			error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_INVALID_QUERY,
			_("Invalid query"));
	else if (!sexp || summary_query) {
		gchar *stmt;
		gchar *sql_query = sexp ? sexp_to_sql_query (ebsdb, folderid, sexp) : NULL;

		if (sql_query && sql_query[0]) {

			if (query_with_list_attrs) {
				gchar *list_table = g_strconcat (folderid, "_lists", NULL);

				stmt = sqlite3_mprintf (
					"SELECT DISTINCT summary.uid FROM %Q AS summary "
					"LEFT OUTER JOIN %Q AS multi ON summary.uid = multi.uid WHERE %s",
					folderid, list_table, sql_query);

				g_free (list_table);
			} else
				stmt = sqlite3_mprintf (
					"SELECT summary.uid FROM %Q AS summary WHERE %s",
					folderid, sql_query);

			book_backend_sql_exec (ebsdb->priv->db, stmt, addto_slist_cb, &uids, error);
			sqlite3_free (stmt);

		} else {
			stmt = sqlite3_mprintf ("SELECT uid FROM %Q", folderid);
			book_backend_sql_exec (ebsdb->priv->db, stmt, addto_slist_cb, &uids, error);
			sqlite3_free (stmt);
		}

		local_searched = TRUE;

		g_free (sql_query);

	} else if (ebsdb->priv->store_vcard) {
		uids = book_backend_sqlitedb_search_full (
			ebsdb, sexp, folderid, TRUE, error);

		local_searched = TRUE;

	} else {
		g_set_error (
			error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_OTHER,
			_("Full vcards are not stored in cache. "
			"Hence only summary query is supported."));
	}

	UNLOCK_MUTEX (&ebsdb->priv->lock);

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

	LOCK_MUTEX (&ebsdb->priv->lock);

	stmt = sqlite3_mprintf ("SELECT uid,rev FROM %Q", folderid);
	book_backend_sql_exec (
		ebsdb->priv->db, stmt,
		get_uids_and_rev_cb, uids_and_rev, error);
	sqlite3_free (stmt);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

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

	LOCK_MUTEX (&ebsdb->priv->lock);

	stmt = sqlite3_mprintf (
		"SELECT is_populated FROM folders WHERE folder_id = %Q",
		folderid);
	book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_bool_cb , &ret, error);
	sqlite3_free (stmt);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

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

	LOCK_MUTEX (&ebsdb->priv->lock);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error)) {
		UNLOCK_MUTEX (&ebsdb->priv->lock);
		return FALSE;
	}

	stmt = sqlite3_mprintf (
		"UPDATE folders SET is_populated = %d "
		"WHERE folder_id = %Q", populated, folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (success)
		success = book_backend_sqlitedb_commit_transaction (ebsdb, error);
	else
		/* The GError is already set. */
		book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

	return success;
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

	LOCK_MUTEX (&ebsdb->priv->lock);

	stmt = sqlite3_mprintf (
		"SELECT revision FROM folders WHERE folder_id = %Q", folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_string_cb, revision_out, error);
	sqlite3_free (stmt);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

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

	LOCK_MUTEX (&ebsdb->priv->lock);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error)) {
		UNLOCK_MUTEX (&ebsdb->priv->lock);
		return FALSE;
	}

	stmt = sqlite3_mprintf (
		"UPDATE folders SET revision = %Q "
		"WHERE folder_id = %Q", revision, folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (success)
		success = book_backend_sqlitedb_commit_transaction (ebsdb, error);
	else
		/* The GError is already set. */
		book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

	return success;
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

	LOCK_MUTEX (&ebsdb->priv->lock);

	stmt = sqlite3_mprintf (
		"SELECT partial_content FROM folders "
		"WHERE folder_id = %Q", folderid);
	book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_bool_cb , &ret, error);
	sqlite3_free (stmt);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

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

	LOCK_MUTEX (&ebsdb->priv->lock);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error)) {
		UNLOCK_MUTEX (&ebsdb->priv->lock);
		return FALSE;
	}

	stmt = sqlite3_mprintf (
		"UPDATE folders SET partial_content = %d "
		"WHERE folder_id = %Q", partial_content, folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (success)
		success = book_backend_sqlitedb_commit_transaction (ebsdb, error);
	else
		/* The GError is already set. */
		book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

	return success;
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

	LOCK_MUTEX (&ebsdb->priv->lock);

	stmt = sqlite3_mprintf (
		"SELECT bdata FROM %Q WHERE uid = %Q", folderid, uid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_string_cb , &ret, error);
	sqlite3_free (stmt);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

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

	LOCK_MUTEX (&ebsdb->priv->lock);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error)) {
		UNLOCK_MUTEX (&ebsdb->priv->lock);
		return FALSE;
	}

	stmt = sqlite3_mprintf (
		"UPDATE %Q SET bdata = %Q WHERE uid = %Q",
		folderid, value, uid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (success)
		success = book_backend_sqlitedb_commit_transaction (ebsdb, error);
	else
		/* The GError is already set. */
		book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

	return success;
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

	LOCK_MUTEX (&ebsdb->priv->lock);

	stmt = sqlite3_mprintf (
		"SELECT sync_data FROM folders WHERE folder_id = %Q",
		folderid);
	book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_string_cb , &ret, error);
	sqlite3_free (stmt);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

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

	LOCK_MUTEX (&ebsdb->priv->lock);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error)) {
		UNLOCK_MUTEX (&ebsdb->priv->lock);
		return FALSE;
	}

	stmt = sqlite3_mprintf (
		"UPDATE folders SET sync_data = %Q "
		"WHERE folder_id = %Q", sync_data, folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (success)
		success = book_backend_sqlitedb_commit_transaction (ebsdb, error);
	else
		/* The GError is already set. */
		book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

	return success;
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

	LOCK_MUTEX (&ebsdb->priv->lock);

	stmt = sqlite3_mprintf (
		"SELECT value FROM keys WHERE folder_id = %Q AND key = %Q",
		folderid, key);
	book_backend_sql_exec (
		ebsdb->priv->db, stmt, get_string_cb , &ret, error);
	sqlite3_free (stmt);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

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

	LOCK_MUTEX (&ebsdb->priv->lock);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error)) {
		UNLOCK_MUTEX (&ebsdb->priv->lock);
		return FALSE;
	}

	stmt = sqlite3_mprintf (
		"INSERT or REPLACE INTO keys (key, value, folder_id) "
		"values (%Q, %Q, %Q)", key, value, folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, NULL, NULL, error);
	sqlite3_free (stmt);

	if (success)
		success = book_backend_sqlitedb_commit_transaction (ebsdb, error);
	else
		/* The GError is already set. */
		book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

	return success;
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

	LOCK_MUTEX (&ebsdb->priv->lock);

	stmt = sqlite3_mprintf (
		"SELECT uid FROM %Q WHERE partial_content = 1",
		folderid);
	book_backend_sql_exec (
		ebsdb->priv->db, stmt, addto_slist_cb, &uids, error);
	sqlite3_free (stmt);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

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

	LOCK_MUTEX (&ebsdb->priv->lock);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error)) {
		UNLOCK_MUTEX (&ebsdb->priv->lock);
		return FALSE;
	}

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

	success = book_backend_sqlitedb_commit_transaction (ebsdb, error);
	UNLOCK_MUTEX (&ebsdb->priv->lock);

	return success;

rollback:
	/* The GError is already set. */
	book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

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
		g_slice_free (EbSdbSearchData, s_data);
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

	LOCK_MUTEX (&ebsdb->priv->lock);

	sqlite3_close (ebsdb->priv->db);

	filename = g_build_filename (ebsdb->priv->path, DB_FILENAME, NULL);
	ret = g_unlink (filename);
	g_free (filename);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

	if (ret == -1) {
		g_set_error (
			error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_OTHER,
			_("Unable to remove the db file: errno %d"), errno);
		return FALSE;
	}

	return TRUE;
}

static gboolean
upgrade_contacts_table (EBookBackendSqliteDB  *ebsdb,
			 const gchar          *folderid,
			 const gchar          *region,
			 GError              **error)
{
	gchar *stmt;
	const gchar *lc_collate;
	gboolean success = FALSE;
	GSList *vcard_data = NULL;
	GSList *l;

	stmt = sqlite3_mprintf ("SELECT uid, vcard, NULL FROM %Q", folderid);
	success = book_backend_sql_exec (
		ebsdb->priv->db, stmt, addto_vcard_list_cb, &vcard_data, error);
	sqlite3_free (stmt);

	if (vcard_data == NULL)
		return TRUE;

	for (l = vcard_data; success && l; l = l->next) {
		EbSdbSearchData *const s_data = l->data;
		EContact *contact = e_contact_new_from_vcard_with_uid (s_data->vcard, s_data->uid);

		if (contact == NULL)
			continue;

		success = insert_contact (ebsdb, contact, folderid, TRUE, region, error);

		g_object_unref (contact);
	}

	g_slist_free_full (vcard_data, destroy_search_data);

	if (success) {
		lc_collate = get_current_collation_locale ();

		stmt = sqlite3_mprintf (
			"UPDATE folders SET countrycode = %Q WHERE folder_id = %Q",
			region, folderid);
		success = book_backend_sql_exec (
			ebsdb->priv->db, stmt, NULL, NULL, error);
		sqlite3_free (stmt);

		stmt = sqlite3_mprintf (
			"UPDATE folders SET lc_collate = %Q WHERE folder_id = %Q",
			lc_collate, folderid);
		success = book_backend_sql_exec (
			ebsdb->priv->db, stmt, NULL, NULL, error);
		sqlite3_free (stmt);
	}

	return success;
}

/******************************************************************
 *                          EbSdbCursor apis                       *
 ******************************************************************/
struct _EbSdbCursor {
	gchar         *folderid;      /* The folderid for this cursor */

	gchar         *select_vcards; /* The first fragment when querying results */
	gchar         *select_count;  /* The first fragment when querying contact counts */
	gchar         *query;         /* The SQL query expression derived from the passed search expression */
	gchar         *order;         /* The normal order SQL query fragment to append at the end, containing ORDER BY etc */
	gchar         *reverse_order; /* The reverse order SQL query fragment to append at the end, containing ORDER BY etc */

	EContactField *sort_fields;   /* The fields to sort in a query in the order or sort priority */
	EBookSortType *sort_types;    /* The sort method to use for each field */
	gchar        **values;        /* The current cursor position, results will be returned after this position */

	gint           n_sort_fields; /* The amound of sort fields */

	gchar         *last_uid;      /* The current cursor contact UID position, used as a tie breaker */
};

static void
ebsdb_cursor_setup_query (EBookBackendSqliteDB *ebsdb,
			  EbSdbCursor          *cursor,
			  const gchar          *sexp,
			  gboolean              query_with_list_attrs)
{
	gchar *stmt;
	gchar *count_stmt;

	g_free (cursor->select_vcards);
	g_free (cursor->select_count);
	g_free (cursor->query);

	if (query_with_list_attrs) {
		gchar *list_table = g_strconcat (cursor->folderid, "_lists", NULL);

		stmt = sqlite3_mprintf ("SELECT DISTINCT summary.uid, vcard, bdata FROM %Q AS summary "
					"LEFT OUTER JOIN %Q AS multi ON summary.uid = multi.uid",
					cursor->folderid, list_table);

		count_stmt = sqlite3_mprintf ("SELECT count(DISTINCT summary.uid), vcard, bdata FROM %Q AS summary "
					      "LEFT OUTER JOIN %Q AS multi ON summary.uid = multi.uid",
					      cursor->folderid, list_table);
		g_free (list_table);
	} else {
		stmt = sqlite3_mprintf ("SELECT uid, vcard, bdata FROM %Q AS summary", cursor->folderid);
		count_stmt = sqlite3_mprintf ("SELECT count(*) FROM %Q AS summary", cursor->folderid);
	}

	cursor->select_vcards = g_strdup (stmt);
	cursor->select_count = g_strdup (count_stmt);
	sqlite3_free (stmt);
	sqlite3_free (count_stmt);

	cursor->query = sexp ? sexp_to_sql_query (ebsdb, cursor->folderid, sexp) : NULL;
}

static gchar *
ebsdb_cursor_order_by_fragment (EBookBackendSqliteDB *ebsdb,
				EContactField        *sort_fields,
				EBookSortType        *sort_types,
				guint                 n_sort_fields,
				gboolean              reverse)
{
	GString *string;
	const gchar *field_name;
	gint i;

	string = g_string_new ("ORDER BY ");

	for (i = 0; i < n_sort_fields; i++) {

		field_name = summary_dbname_from_field (ebsdb, sort_fields[i]);

		if (i > 0)
			g_string_append (string, ", ");

		g_string_append_printf (string, "summary.%s_localized %s", field_name,
					reverse ?
					(sort_types[i] == E_BOOK_SORT_ASCENDING ? "DESC" : "ASC") :
					(sort_types[i] == E_BOOK_SORT_ASCENDING ? "ASC"  : "DESC"));
	}

	return g_string_free (string, FALSE);
}

static EbSdbCursor *
ebsdb_cursor_new (EBookBackendSqliteDB *ebsdb,
		  const gchar          *folderid,
		  const gchar          *sexp,
		  gboolean              query_with_list_attrs,
		  EContactField        *sort_fields,
		  EBookSortType        *sort_types,
		  guint                 n_sort_fields)
{
	EbSdbCursor *cursor = g_slice_new0 (EbSdbCursor);

	cursor->folderid = g_strdup (folderid);

	/* Setup the initial query fragments */
	ebsdb_cursor_setup_query (ebsdb, cursor, sexp, query_with_list_attrs);

	cursor->order = ebsdb_cursor_order_by_fragment (ebsdb,
							sort_fields,
							sort_types,
							n_sort_fields,
							FALSE);
	cursor->reverse_order = ebsdb_cursor_order_by_fragment (ebsdb,
								sort_fields,
								sort_types,
								n_sort_fields,
								TRUE);

	cursor->n_sort_fields = n_sort_fields;
	cursor->sort_fields   = g_memdup (sort_fields, sizeof (EContactField) * n_sort_fields);
	cursor->sort_types    = g_memdup (sort_types,  sizeof (EBookSortType) * n_sort_fields);
	cursor->values        = g_new0 (gchar *, n_sort_fields);

	return cursor;
}

static void
ebsdb_cursor_clear_state (EbSdbCursor *cursor)
{
	gint i;

	for (i = 0; i < cursor->n_sort_fields; i++) {
		g_free (cursor->values[i]);
		cursor->values[i] = NULL;
	}

	g_free (cursor->last_uid);
	cursor->last_uid = NULL;
}

static void
ebsdb_cursor_free (EbSdbCursor *cursor)
{
	if (cursor) {
		ebsdb_cursor_clear_state (cursor);

		g_free (cursor->folderid);
		g_free (cursor->select_vcards);
		g_free (cursor->select_count);
		g_free (cursor->query);
		g_free (cursor->order);
		g_free (cursor->reverse_order);
		g_free (cursor->sort_fields);
		g_free (cursor->sort_types);
		g_free (cursor->values);

		g_slice_free (EbSdbCursor, cursor);
	}
}

static void
ebsdb_cursor_set_state_from_contact (EBookBackendSqliteDB *ebsdb,
				     EbSdbCursor          *cursor,
				     EContact             *contact)
{
	gint i, index;

	ebsdb_cursor_clear_state (cursor);

	for (i = 0; i < cursor->n_sort_fields; i++) {
		const gchar *string = e_contact_get_const (contact, cursor->sort_fields[i]);

		index = summary_index_from_field (ebsdb, cursor->sort_fields[i]);

		if (string)
			cursor->values[i] =
				e_collator_generate_key (ebsdb->priv->summary_fields[index].collator,
							 string, NULL);
		else
			cursor->values[i] = g_strdup ("");
	}

	cursor->last_uid = e_contact_get (contact, E_CONTACT_UID);
}

static void
ebsdb_cursor_set_state (EBookBackendSqliteDB *ebsdb,
			EbSdbCursor          *cursor,
			const gchar          *vcard)
{
	EContact *contact;

	if (vcard) {
		contact = e_contact_new_from_vcard (vcard);
		ebsdb_cursor_set_state_from_contact (ebsdb, cursor, contact);
		g_object_unref (contact);
	} else {
		ebsdb_cursor_clear_state (cursor);
	}
}

#define GREATER_OR_LESS(cursor, index, reverse)				\
	(reverse ?							\
	 (((EbSdbCursor *)cursor)->sort_types[index == E_BOOK_SORT_ASCENDING] ? '>' : '<') : \
	 (((EbSdbCursor *)cursor)->sort_types[index == E_BOOK_SORT_ASCENDING] ? '<' : '>'))

static gchar *
ebsdb_cursor_constraints (EBookBackendSqliteDB *ebsdb,
			  EbSdbCursor          *cursor,
			  gboolean              reverse,
			  gboolean              include_current_uid)
{
	GString *string;
	const gchar *field_name;
	gint i, j;

	/* Example for:
	 *    ORDER BY family_name ASC, given_name DESC
	 *
	 * Where current cursor values are:
	 *    family_name = Jackson
	 *    given_name  = Micheal
	 *
	 * With reverse = FALSE
	 *
	 *    (summary.family_name > 'Jackson') OR
	 *    (summary.family_name = 'Jackson' AND summary.given_name < 'Micheal') OR
	 *    (summary.family_name = 'Jackson' AND summary.given_name = 'Micheal' AND summary.uid > 'last-uid')
	 *
	 * With reverse = TRUE (needed for moving the cursor backwards through results)
	 *
	 *    (summary.family_name < 'Jackson') OR
	 *    (summary.family_name = 'Jackson' AND summary.given_name > 'Micheal') OR
	 *    (summary.family_name = 'Jackson' AND summary.given_name = 'Micheal' AND summary.uid < 'last-uid')
	 *
	 */

	string = g_string_new (NULL);

	for (i = 0; i < (cursor->n_sort_fields + 1); i++) {
		gchar   *stmt;

		/* Break once we hit a NULL value */
		if ((i  < cursor->n_sort_fields && cursor->values[i] == NULL) ||
		    (i == cursor->n_sort_fields && cursor->last_uid  == NULL))
			break;

		/* Between each qualifier, add an 'OR' */
		if (i > 0)
			g_string_append (string, " OR ");

		/* Begin qualifier */
		g_string_append_c (string, '(');

		/* Create the '=' statements leading up to the current tie breaker */
		for (j = 0; j < i; j++) {
			field_name = summary_dbname_from_field (ebsdb, cursor->sort_fields[j]);

			stmt = sqlite3_mprintf ("summary.%s_localized = %Q",
						field_name, cursor->values[j]);

			g_string_append (string, stmt);
			g_string_append (string, " AND ");

			sqlite3_free (stmt);

		}

		if (i == cursor->n_sort_fields) {

			/* The 'include_current_uid' clause is used for calculating
			 * the current position of the cursor, inclusive of the
			 * current position.
			 */
			if (include_current_uid)
				g_string_append_c (string, '(');

			/* Append the UID tie breaker */
			stmt = sqlite3_mprintf ("summary.uid %c %Q",
						reverse ? '<' : '>',
						cursor->last_uid);
			g_string_append (string, stmt);
			sqlite3_free (stmt);

			if (include_current_uid) {
				stmt = sqlite3_mprintf (" OR summary.uid = %Q",
							cursor->last_uid);
				g_string_append (string, stmt);
				g_string_append_c (string, ')');
				sqlite3_free (stmt);
			}

		} else {

			/* SPECIAL CASE: If we have a parially set cursor state, then we must
			 * report next results that are inclusive of the final qualifier.
			 *
			 * This allows one to set the cursor with the family name set to 'J'
			 * and include the results for contact's Mr & Miss 'J'.
			 */
			gboolean include_exact_match =
				(reverse == FALSE &&
				 ((i + 1 < cursor->n_sort_fields && cursor->values[i + 1] == NULL) ||
				  (i + 1 == cursor->n_sort_fields && cursor->last_uid == NULL)));

			if (include_exact_match)
				g_string_append_c (string, '(');

			/* Append the final qualifier for this field */
			field_name = summary_dbname_from_field (ebsdb, cursor->sort_fields[i]);

			stmt = sqlite3_mprintf ("summary.%s_localized %c %Q",
						field_name,
						GREATER_OR_LESS (cursor, i, reverse),
						cursor->values[i]);

			g_string_append (string, stmt);
			sqlite3_free (stmt);

			if (include_exact_match) {

				stmt = sqlite3_mprintf (" OR summary.%s_localized = %Q",
							field_name, cursor->values[i]);

				g_string_append (string, stmt);
				g_string_append_c (string, ')');
				sqlite3_free (stmt);
			}
		}

		/* End qualifier */
		g_string_append_c (string, ')');
	}

	return g_string_free (string, FALSE);
}

static gboolean
cursor_count_total_locked (EBookBackendSqliteDB *ebsdb,
			   EbSdbCursor          *cursor,
			   gint                 *total,
			   GError              **error)
{
	GString *query;
	gboolean success;

	query = g_string_new (cursor->select_count);

	/* Add the filter constraints (if any) */
	if (cursor->query) {
		g_string_append (query, " WHERE ");

		g_string_append_c (query, '(');
		g_string_append (query, cursor->query);
		g_string_append_c (query, ')');
	}

	/* Execute the query */
	success = book_backend_sql_exec (ebsdb->priv->db, query->str,
					 get_count_cb, total, error);

	g_string_free (query, TRUE);

	return success;
}

static gboolean
cursor_count_position_locked (EBookBackendSqliteDB *ebsdb,
			      EbSdbCursor          *cursor,
			      gint                 *position,
			      GError              **error)
{
	GString *query;
	gboolean success;

	query = g_string_new (cursor->select_count);

	/* Add the filter constraints (if any) */
	if (cursor->query) {
		g_string_append (query, " WHERE ");

		g_string_append_c (query, '(');
		g_string_append (query, cursor->query);
		g_string_append_c (query, ')');
	}

	/* Add the cursor constraints (if any) */
	if (cursor->values[0] != NULL) {
		gchar  *constraints = NULL;

		if (!cursor->query)
			g_string_append (query, " WHERE ");
		else
			g_string_append (query, " AND ");

		/* Here we do a reverse query, we're looking for all the
		 * results leading up to the current cursor value, including
		 * the cursor value
		 */
		constraints = ebsdb_cursor_constraints (ebsdb, cursor, TRUE, TRUE);

		g_string_append_c (query, '(');
		g_string_append (query, constraints);
		g_string_append_c (query, ')');

		g_free (constraints);
	}

	/* Execute the query */
	success = book_backend_sql_exec (ebsdb->priv->db, query->str,
					 get_count_cb, position, error);

	g_string_free (query, TRUE);

	return success;
}

/**
 * e_book_backend_sqlitedb_cursor_new:
 * @ebsdb: An #EBookBackendSqliteDB
 * @folderid: folder id of the address-book
 * @sexp: search expression; use NULL or an empty string to get all stored contacts.
 * @sort_fields: (array length=n_sort_fields): An array of #EContactFields as sort keys in order of priority
 * @sort_types: (array length=n_sort_fields): An array of #EBookSortTypes, one for each field in @sort_fields
 * @n_sort_fields: The number of fields to sort results by.
 * @error: A return location to story any error that might be reported.
 *
 * Creates a new #EbSdbCursor.
 *
 * The cursor should be freed with e_book_backend_sqlitedb_cursor_free().
 *
 * Returns: (transfer full): A newly created #EbSdbCursor
 *
 * Since: 3.10
 */
EbSdbCursor *
e_book_backend_sqlitedb_cursor_new (EBookBackendSqliteDB *ebsdb,
				    const gchar          *folderid,
				    const gchar          *sexp,
				    EContactField        *sort_fields,
				    EBookSortType        *sort_types,
				    guint                 n_sort_fields,
				    GError              **error)
{
	gboolean query_with_list_attrs = FALSE;
	gint i;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), NULL);
	g_return_val_if_fail (folderid && folderid[0], NULL);

	/* We only support cursors for summary fields in the query */
	if (sexp && !e_book_backend_sqlitedb_check_summary_query (ebsdb, sexp, &query_with_list_attrs)) {
		g_set_error (error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_INVALID_QUERY,
			     _("Only summary queries are supported by EbSdbCursor"));
		return NULL;
	}

	if (n_sort_fields == 0) {
		g_set_error (error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_INVALID_QUERY,
			     _("At least one sort field must be specified to use an EbSdbCursor"));
		return NULL;
	}

	/* We only support summarized sort keys which are not multi value fields */
	for (i = 0; i < n_sort_fields; i++) {

		gint support;

		support = func_check_field_test (ebsdb, e_contact_field_name (sort_fields[i]), NULL);

		if ((support & CHECK_IS_SUMMARY) == 0) {
			g_set_error (error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_INVALID_QUERY,
				     _("Cannot sort by a field that is not in the summary"));
			return NULL;
		}

		if ((support & CHECK_IS_LIST_ATTR) != 0) {
			g_set_error (error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_INVALID_QUERY,
				     _("Cannot sort by a field which may have multiple values"));
			return NULL;
		}
	}

	return ebsdb_cursor_new (ebsdb, folderid, sexp, query_with_list_attrs,
				 sort_fields, sort_types, n_sort_fields);
}

/**
 * e_book_backend_sqlitedb_cursor_free:
 * @ebsdb: An #EBookBackendSqliteDB
 * @cursor: The #EbSdbCursor to free
 *
 * Frees @cursor.
 *
 * Since: 3.10
 */
void
e_book_backend_sqlitedb_cursor_free (EBookBackendSqliteDB *ebsdb,
				     EbSdbCursor          *cursor)
{
	g_return_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb));

	ebsdb_cursor_free (cursor);
}

/**
 * e_book_backend_sqlitedb_cursor_move_by:
 * @ebsdb: An #EBookBackendSqliteDB
 * @cursor: The #EbSdbCursor to use
 * @count: A positive or negative amount of contacts to try and fetch
 * @error: A return location to story any error that might be reported.
 *
 * Moves @cursor through the @ebsdb by @count and fetch a maximum of @count contacts.
 *
 * If @count is negative, then the cursor will move backwards.
 *
 * If @cursor is in an empty state, then @count contacts will be fetched
 * from the beginning of the cursor's query results, or from the ending
 * of the query results for a negative value of @count.
 *
 * If @cursor reaches the beginning or end of the query results, then the
 * returned list might not contain the amount of desired contacts, or might
 * return no results. This is not considered an error condition.
 *
 * Returns: (element-type EbSdbSearchData) (transfer full):
 *   A list of #EbSdbSearchData, the list should be freed with g_slist_free()
 *   and all elements freed with e_book_backend_sqlitedb_search_data_free().
 *
 * Since: 3.10
 */
GSList *
e_book_backend_sqlitedb_cursor_move_by (EBookBackendSqliteDB *ebsdb,
					EbSdbCursor          *cursor,
					gint                  count,
					GError              **error)
{
	GSList *results = NULL;
	GString *query;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), NULL);
	g_return_val_if_fail (cursor != NULL, NULL);
	g_return_val_if_fail (count != 0, NULL);

	query = g_string_new (cursor->select_vcards);

	/* Add the filter constraints (if any) */
	if (cursor->query) {
		g_string_append (query, " WHERE ");

		g_string_append_c (query, '(');
		g_string_append (query, cursor->query);
		g_string_append_c (query, ')');
	}

	/* Add the cursor constraints (if any) */
	if (cursor->values[0] != NULL) {
		gchar  *constraints = NULL;

		if (!cursor->query)
			g_string_append (query, " WHERE ");
		else
			g_string_append (query, " AND ");

		constraints = ebsdb_cursor_constraints (ebsdb, cursor, count < 0, FALSE);

		g_string_append_c (query, '(');
		g_string_append (query, constraints);
		g_string_append_c (query, ')');

		g_free (constraints);
	}

	/* Add the sort order */
	g_string_append_c (query, ' ');
	if (count > 0)
		g_string_append (query, cursor->order);
	else
		g_string_append (query, cursor->reverse_order);

	/* Add the limit */
	g_string_append_printf (query, " LIMIT %d", ABS (count));

	/* Execute the query */
	LOCK_MUTEX (&ebsdb->priv->lock);
	success = book_backend_sql_exec (ebsdb->priv->db, query->str,
					 addto_vcard_list_cb , &results,
					 error);
	UNLOCK_MUTEX (&ebsdb->priv->lock);

	g_string_free (query, TRUE);

	/* Correct the order of results, since
	 * addto_vcard_list_cb() prepends them (as it should)
	 */
	results = g_slist_reverse (results);

	/* If there was no error, update the internal cursor state */
	if (success) {

		if (g_slist_length (results) < ABS (count))
			/* We've reached the end, clear the state */
			ebsdb_cursor_clear_state (cursor);
		else {
			/* Set the cursor state to the last result */
			GSList *last = g_slist_last (results);
			EbSdbSearchData *data = last->data;

			ebsdb_cursor_set_state (ebsdb, cursor, data->vcard);
		}
	}

	return results;
}

/**
 * e_book_backend_sqlitedb_cursor_set_targetv:
 * @ebsdb: An #EBookBackendSqliteDB
 * @cursor: The #EbSdbCursor to modify
 * @values: (allow-none) (array length=n_values): An array of values to set the cursor position with
 * @n_values: The length of the passed @values
 *
 * Set's the current cursor target position.
 *
 * The passed values set the relative @cursor position in it's result set
 * with the passed @values. Each member of the passed @values represents
 * a target position for it's corresponding sort field which the cursor
 * was created for (See the @sort_fields argument of e_book_backend_sqlitedb_cursor_new()).
 *
 * The @values array passed to this function need not be as long as
 * the array of @sort_fields originally passed to e_book_backend_sqlitedb_cursor_new().
 * A shorter array of @values indicates that the cursor's target is set with
 * less specificity.
 *
 * In addition to the @cursor's @sort_fields, a single extra value can
 * be passed which is an %E_CONTACT_UID. If the uid is given as an additional
 * value in @values, it will be used to specify exactly which contact the
 * cursor should currently point to.
 *
 * Note that if the final %E_CONTACT_UID is not specified, then the cursor
 * is said to be in an 'incomplete state' or a 'partial state'. If the cursor
 * is in a partial state, then the next call to e_book_backend_sqlitedb_cursor_move_by()
 * with a positive @count will include any exact matches for the given values.
 *
 * A %NULL value for @values resets the internal state of @cursor completely,
 * so that any further calls to e_book_backend_sqlitedb_cursor_move_by() will
 * report results from the beginning or ending of the @cursor's query.
 *
 * Since: 3.10
 */
void
e_book_backend_sqlitedb_cursor_set_targetv (EBookBackendSqliteDB *ebsdb,
					    EbSdbCursor          *cursor,
					    const gchar         **values,
					    gint                  n_values)
{
	gint i, index;

	g_return_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb));
	g_return_if_fail (cursor != NULL);
	g_return_if_fail (n_values <= cursor->n_sort_fields + 1);

	ebsdb_cursor_clear_state (cursor);

	for (i = 0; i < MIN (cursor->n_sort_fields, n_values); i++) {

		index = summary_index_from_field (ebsdb, cursor->sort_fields[i]);

		cursor->values[i] = e_collator_generate_key (ebsdb->priv->summary_fields[index].collator, values[i], NULL);
	}

	if (n_values > cursor->n_sort_fields)
		cursor->last_uid = g_strdup (values[n_values - 1]);
}

/**
 * e_book_backend_sqlitedb_cursor_set_target:
 * @ebsdb: An #EBookBackendSqliteDB
 * @cursor: The #EbSdbCursor to modify
 * @...: A null terminated list of values
 *
 * A convenience function for calling e_book_backend_sqlitedb_cursor_set_targetv().
 *
 * Since: 3.10
 */
void
e_book_backend_sqlitedb_cursor_set_target (EBookBackendSqliteDB *ebsdb,
					   EbSdbCursor          *cursor,
					   ...)
{
	GArray *array;
	gchar *value = NULL;
	va_list args;

	g_return_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb));
	g_return_if_fail (cursor != NULL);

	array = g_array_new (FALSE, FALSE, sizeof (gchar *));

	va_start (args, cursor);
	value = va_arg (args, gchar*);
	while (value) {
		g_array_append_val (array, value);
		value = va_arg (args, gchar*);
	}
	va_end (args);

	e_book_backend_sqlitedb_cursor_set_targetv (ebsdb, cursor,
						    (const gchar **)array->data,
						    array->len);
	g_array_free (array, TRUE);
}

/**
 * e_book_backend_sqlitedb_cursor_set_target_contact:
 * @ebsdb: An #EBookBackendSqliteDB
 * @cursor: The #EbSdbCursor to modify
 * @contact: (allow-none): An #EContact
 *
 * A convenience function for calling e_book_backend_sqlitedb_cursor_set_targetv(),
 *
 * This function will set the cursor values automatically from @contact.
 *
 * Since: 3.10
 */
void
e_book_backend_sqlitedb_cursor_set_target_contact (EBookBackendSqliteDB *ebsdb,
						   EbSdbCursor          *cursor,
						   EContact             *contact)
{
	g_return_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb));
	g_return_if_fail (contact == NULL || E_IS_CONTACT (contact));
	g_return_if_fail (cursor != NULL);

	if (contact)
		ebsdb_cursor_set_state_from_contact (ebsdb, cursor, contact);
	else
		ebsdb_cursor_clear_state (cursor);
}

/**
 * e_book_backend_sqlitedb_cursor_set_sexp:
 * @ebsdb: An #EBookBackendSqliteDB
 * @cursor: The #EbSdbCursor
 * @sexp: The new query expression for @cursor
 * @error: A return location to story any error that might be reported.
 *
 * Modifies the current query expression for @cursor. This will not
 * modify @cursor's state, but will change the outcome of any further
 * calls to e_book_backend_sqlitedb_cursor_calculate() or
 * e_book_backend_sqlitedb_cursor_move_by().
 *
 * Returns: %TRUE if the expression was valid and accepted by @ebsdb
 *
 * Since: 3.10
 */
gboolean
e_book_backend_sqlitedb_cursor_set_sexp (EBookBackendSqliteDB *ebsdb,
					 EbSdbCursor          *cursor,
					 const gchar          *sexp,
					 GError              **error)
{
	gboolean query_with_list_attrs = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (cursor != NULL, FALSE);

	/* We only support cursors for summary fields in the query */
	if (sexp && !e_book_backend_sqlitedb_check_summary_query (ebsdb, sexp, &query_with_list_attrs)) {
		g_set_error (error, E_BOOK_SDB_ERROR, E_BOOK_SDB_ERROR_INVALID_QUERY,
			     _("Only summary queries are supported by EbSdbCursor"));
		return FALSE;
	}

	ebsdb_cursor_setup_query (ebsdb, cursor, sexp, query_with_list_attrs);

	return TRUE;
}

/**
 * e_book_backend_sqlitedb_cursor_calculate:
 * @ebsdb: An #EBookBackendSqliteDB
 * @cursor: The #EbSdbCursor
 * @total: (out) (allow-none): A return location to store the total result set for this cursor
 * @position: (out) (allow-none): A return location to store the total results before the cursor value
 * @error: (allow-none): A return location to story any error that might be reported.
 *
 * Calculates the @total amount of results for the @cursor's query expression,
 * as well as the current @position of @cursor in the results. @position is
 * represented as the amount of results which lead up to the current value
 * of @cursor, if @cursor currently points to an exact contact, the position
 * also includes the cursor contact.
 *
 * Returns: Whether @total and @position were successfully calculated.
 *
 * Since: 3.10
 */
gboolean
e_book_backend_sqlitedb_cursor_calculate (EBookBackendSqliteDB *ebsdb,
					  EbSdbCursor          *cursor,
					  gint                 *total,
					  gint                 *position,
					  GError              **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), FALSE);
	g_return_val_if_fail (cursor != NULL, FALSE);

	/* If we're in a clear cursor state, then the position is 0 */
	if (position && cursor->values[0] == NULL) {
		*position = 0;

		/* Mark the local pointer NULL, no need to calculate this anymore */
		position = NULL;
	}

	/* Early return if there is nothing to do */
	if (!total && !position)
		return TRUE;

	LOCK_MUTEX (&ebsdb->priv->lock);

	if (!book_backend_sqlitedb_start_transaction (ebsdb, error)) {
		UNLOCK_MUTEX (&ebsdb->priv->lock);
		return FALSE;
	}

	if (total)
		success = cursor_count_total_locked (ebsdb, cursor, total, error);

	if (success && position)
		success = cursor_count_position_locked (ebsdb, cursor, position, error);

	if (success)
		success = book_backend_sqlitedb_commit_transaction (ebsdb, error);
	else
		/* The GError is already set. */
		book_backend_sqlitedb_rollback_transaction (ebsdb, NULL);

	UNLOCK_MUTEX (&ebsdb->priv->lock);

	return success;
}

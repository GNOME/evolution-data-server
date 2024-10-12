/*
 * Copyright (C) 2022 Red Hat (www.redhat.com)
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
 */

/* This is a rough copy of the ECacheKeys, modified for EBookSqlite */

#include "evolution-data-server-config.h"

#include "libebackend/libebackend.h"
#include "libedata-book/libedata-book.h"

#include "e-book-sqlite-keys.h"

#define REFS_COLUMN_NAME "refs"

struct _EBookSqliteKeysPrivate {
	EBookSqlite *bsql; /* referenced */
	gchar *table_name;
	gchar *key_column_name;
	gchar *value_column_name;
};

enum {
	CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_PRIVATE (EBookSqliteKeys, e_book_sqlite_keys, G_TYPE_OBJECT)

static void
e_book_sqlite_keys_finalize (GObject *object)
{
	EBookSqliteKeys *self = E_BOOK_SQLITE_KEYS (object);

	g_clear_object (&self->priv->bsql);
	g_clear_pointer (&self->priv->table_name, g_free);
	g_clear_pointer (&self->priv->key_column_name, g_free);
	g_clear_pointer (&self->priv->value_column_name, g_free);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_sqlite_keys_parent_class)->finalize (object);
}

static void
e_book_sqlite_keys_class_init (EBookSqliteKeysClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = e_book_sqlite_keys_finalize;

	signals[CHANGED] = g_signal_new (
		"changed",
		G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EBookSqliteKeysClass, changed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 0,
		G_TYPE_NONE);
}

static void
e_book_sqlite_keys_init (EBookSqliteKeys *self)
{
	self->priv = e_book_sqlite_keys_get_instance_private (self);
}

EBookSqliteKeys *
e_book_sqlite_keys_new (EBookSqlite *bsql,
			const gchar *table_name,
			const gchar *key_column_name,
			const gchar *value_column_name)
{
	EBookSqliteKeys *self;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (bsql), NULL);
	g_return_val_if_fail (table_name && *table_name, NULL);
	g_return_val_if_fail (key_column_name && *key_column_name, NULL);
	g_return_val_if_fail (g_ascii_strcasecmp (key_column_name, REFS_COLUMN_NAME) != 0, NULL);
	g_return_val_if_fail (value_column_name && *value_column_name, NULL);
	g_return_val_if_fail (g_ascii_strcasecmp (value_column_name, REFS_COLUMN_NAME) != 0, NULL);

	self = g_object_new (E_TYPE_BOOK_SQLITE_KEYS, NULL);

	self->priv->bsql = g_object_ref (bsql);
	self->priv->table_name = g_strdup (table_name);
	self->priv->key_column_name = g_strdup (key_column_name);
	self->priv->value_column_name = g_strdup (value_column_name);

	return self;
}

static gboolean
e_book_sqlite_keys_get_string (EBookSqlite *bsql,
			       gint ncols,
			       const gchar **column_names,
			       const gchar **column_values,
			       gpointer user_data)
{
	gchar **pvalue = user_data;

	g_return_val_if_fail (ncols == 1, FALSE);
	g_return_val_if_fail (column_names != NULL, FALSE);
	g_return_val_if_fail (column_values != NULL, FALSE);
	g_return_val_if_fail (pvalue != NULL, FALSE);

	if (!*pvalue)
		*pvalue = g_strdup (column_values[0]);

	return TRUE;
}

static gboolean
e_book_sqlite_keys_get_int64_cb (EBookSqlite *bsql,
				 gint ncols,
				 const gchar **column_names,
				 const gchar **column_values,
				 gpointer user_data)
{
	gint64 *pi64 = user_data;

	g_return_val_if_fail (pi64 != NULL, FALSE);

	if (ncols == 1) {
		*pi64 = column_values[0] ? g_ascii_strtoll (column_values[0], NULL, 10) : 0;
	} else {
		*pi64 = 0;
	}

	return TRUE;
}

static gint
e_book_sqlite_keys_get_current_refs (EBookSqliteKeys *self,
				     const gchar *key,
				     GCancellable *cancellable,
				     GError **error)
{
	gint64 existing_refs = -1;
	gchar *stmt;

	g_return_val_if_fail (E_IS_BOOK_SQLITE_KEYS (self), -1);
	g_return_val_if_fail (key != NULL, -1);

	stmt = e_cache_sqlite_stmt_printf ("SELECT " REFS_COLUMN_NAME " FROM %s WHERE %s=%Q",
		self->priv->table_name, self->priv->key_column_name, key);

	if (!e_book_sqlite_select (self->priv->bsql, stmt, e_book_sqlite_keys_get_int64_cb, &existing_refs, cancellable, error))
		existing_refs = -1;

	e_cache_sqlite_stmt_free (stmt);

	return (gint) existing_refs;
}

static void
e_book_sqlite_keys_emit_changed (EBookSqliteKeys *self)
{
	g_signal_emit (self, signals[CHANGED], 0, NULL);
}

gboolean
e_book_sqlite_keys_init_table_sync (EBookSqliteKeys *self,
				    GCancellable *cancellable,
				    GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_SQLITE_KEYS (self), FALSE);

	stmt = e_cache_sqlite_stmt_printf ("CREATE TABLE IF NOT EXISTS %Q ("
		"%s TEXT PRIMARY KEY, "
		"%s TEXT, "
		"%s INTEGER)",
		self->priv->table_name,
		self->priv->key_column_name,
		self->priv->value_column_name,
		REFS_COLUMN_NAME);
	success = e_book_sqlite_exec (self->priv->bsql, stmt, cancellable, error);
	e_cache_sqlite_stmt_free (stmt);

	return success;
}

gboolean
e_book_sqlite_keys_count_keys_sync (EBookSqliteKeys *self,
				    gint64 *out_n_stored,
				    GCancellable *cancellable,
				    GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_SQLITE_KEYS (self), FALSE);
	g_return_val_if_fail (out_n_stored != NULL, FALSE);

	*out_n_stored = 0;

	stmt = e_cache_sqlite_stmt_printf ("SELECT COUNT(*) FROM %s", self->priv->table_name);
	success = e_book_sqlite_select (self->priv->bsql, stmt,
		e_book_sqlite_keys_get_int64_cb, out_n_stored, cancellable, error);
	e_cache_sqlite_stmt_free (stmt);

	return success;
}

gboolean
e_book_sqlite_keys_put_sync (EBookSqliteKeys *self,
			     const gchar *key,
			     const gchar *value,
			     guint inc_ref_counts,
			     GCancellable *cancellable,
			     GError **error)
{
	gboolean success;
	gint current_refs;
	gchar *stmt;

	g_return_val_if_fail (E_IS_BOOK_SQLITE_KEYS (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	/*if (!e_book_sqlite_lock (self->priv->bsql, EBSQL_LOCK_WRITE, cancellable, error))
		return FALSE;*/

	current_refs = e_book_sqlite_keys_get_current_refs (self, key, cancellable, NULL);

	if (inc_ref_counts > 0) {
		/* Zero means keep forever */
		if (current_refs == 0)
			inc_ref_counts = 0;
		else if (current_refs > 0)
			inc_ref_counts += current_refs;
	}

	stmt = e_cache_sqlite_stmt_printf (
		"INSERT or REPLACE INTO %s (%s, %s, " REFS_COLUMN_NAME ") VALUES (%Q, %Q, %u)",
		self->priv->table_name,
		self->priv->key_column_name,
		self->priv->value_column_name,
		key, value, inc_ref_counts);

	success = e_book_sqlite_exec (self->priv->bsql, stmt, cancellable, error);

	e_cache_sqlite_stmt_free (stmt);

	/*if (!e_book_sqlite_unlock (self->priv->bsql, success ? EBSQL_UNLOCK_COMMIT : EBSQL_UNLOCK_ROLLBACK, success ? error : NULL))
		success = FALSE;*/

	/* a new row had been inserted */
	if (success && current_refs < 0)
		e_book_sqlite_keys_emit_changed (self);

	return success;
}

gboolean
e_book_sqlite_keys_get_sync (EBookSqliteKeys *self,
			     const gchar *key,
			     gchar **out_value,
			     GCancellable *cancellable,
			     GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_SQLITE_KEYS (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (out_value != NULL, FALSE);

	*out_value = NULL;

	stmt = e_cache_sqlite_stmt_printf ("SELECT %s FROM %s WHERE %s=%Q",
		self->priv->value_column_name,
		self->priv->table_name,
		self->priv->key_column_name,
		key);

	success = e_book_sqlite_select (self->priv->bsql, stmt, e_book_sqlite_keys_get_string, out_value, cancellable, error) &&
		*out_value != NULL;

	e_cache_sqlite_stmt_free (stmt);

	return success;
}

gboolean
e_book_sqlite_keys_get_ref_count_sync (EBookSqliteKeys *self,
				       const gchar *key,
				       guint *out_ref_count,
				       GCancellable *cancellable,
				       GError **error)
{
	gint ref_count;

	g_return_val_if_fail (E_IS_BOOK_SQLITE_KEYS (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (out_ref_count != NULL, FALSE);

	ref_count = e_book_sqlite_keys_get_current_refs (self, key, cancellable, error);

	if (ref_count >= 0)
		*out_ref_count = (guint) ref_count;
	else
		*out_ref_count = 0;

	return ref_count >= 0;
}

typedef struct _ForeachData {
	EBookSqliteKeys *self;
	EBookSqliteKeysForeachFunc func;
	gpointer user_data;
	gboolean columns_tested;
} ForeachData;

static gboolean
e_book_sqlite_keys_foreach_cb (EBookSqlite *bsql,
			       gint ncols,
			       const gchar *column_names[],
			       const gchar *column_values[],
			       gpointer user_data)
{
	ForeachData *fd = user_data;
	guint ref_count;

	g_return_val_if_fail (fd != NULL, FALSE);

	if (!fd->columns_tested) {
		if (ncols != 3) {
			g_warning ("%s: Expects 3 columns, received %d", G_STRFUNC, ncols);
			return FALSE;
		}

		if (!column_names[0] || g_ascii_strcasecmp (column_names[0], fd->self->priv->key_column_name) != 0) {
			g_warning ("%s: First column name (%s) doesn't match expected (%s)", G_STRFUNC, column_names[0], fd->self->priv->key_column_name);
			return FALSE;
		}

		if (!column_names[1] || g_ascii_strcasecmp (column_names[1], fd->self->priv->value_column_name) != 0) {
			g_warning ("%s: Second column name (%s) doesn't match expected (%s)", G_STRFUNC, column_names[1], fd->self->priv->value_column_name);
			return FALSE;
		}

		if (!column_names[2] || g_ascii_strcasecmp (column_names[2], REFS_COLUMN_NAME) != 0) {
			g_warning ("%s: Third column name (%s) doesn't match expected (%s)", G_STRFUNC, column_names[2], REFS_COLUMN_NAME);
			return FALSE;
		}

		fd->columns_tested = TRUE;
	} else {
		g_return_val_if_fail (ncols == 3, FALSE);
	}

	ref_count = column_values[2] ? (guint) g_ascii_strtoull (column_values[2], NULL, 10) : 0;

	return fd->func (fd->self, column_values[0], column_values[1], ref_count, fd->user_data);
}

gboolean
e_book_sqlite_keys_foreach_sync (EBookSqliteKeys *self,
				 EBookSqliteKeysForeachFunc func,
				 gpointer user_data,
				 GCancellable *cancellable,
				 GError **error)
{
	ForeachData fd;
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_SQLITE_KEYS (self), FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	fd.self = self;
	fd.func = func;
	fd.user_data = user_data;
	fd.columns_tested = FALSE;

	stmt = e_cache_sqlite_stmt_printf ("SELECT %s, %s, %s FROM %Q",
		self->priv->key_column_name,
		self->priv->value_column_name,
		REFS_COLUMN_NAME,
		self->priv->table_name);
	success = e_book_sqlite_select (self->priv->bsql, stmt,
		e_book_sqlite_keys_foreach_cb, &fd, cancellable, error);
	e_cache_sqlite_stmt_free (stmt);

	return success;
}

gboolean
e_book_sqlite_keys_remove_sync (EBookSqliteKeys *self,
				const gchar *key,
				guint dec_ref_counts,
				GCancellable *cancellable,
				GError **error)
{
	gint current_refs;
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_SQLITE_KEYS (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/*if (!e_book_sqlite_lock (self->priv->bsql, EBSQL_LOCK_WRITE, cancellable, error))
		return FALSE;*/

	current_refs = e_book_sqlite_keys_get_current_refs (self, key, cancellable, NULL);
	if (current_refs <= 0)
		return TRUE; /*e_book_sqlite_unlock (self->priv->bsql, EBSQL_UNLOCK_COMMIT, error); */

	if (dec_ref_counts) {
		if (current_refs >= dec_ref_counts)
			dec_ref_counts = current_refs - dec_ref_counts;
		else
			dec_ref_counts = 0;
	}

	if (dec_ref_counts) {
		stmt = e_cache_sqlite_stmt_printf ("UPDATE %Q SET %s=%u WHERE %s=%Q",
			self->priv->table_name,
			REFS_COLUMN_NAME, dec_ref_counts,
			self->priv->key_column_name, key);
	} else {
		stmt = e_cache_sqlite_stmt_printf ("DELETE FROM %s WHERE %s=%Q",
			self->priv->table_name,
			self->priv->key_column_name, key);
	}

	success = e_book_sqlite_exec (self->priv->bsql, stmt, cancellable, error);

	e_cache_sqlite_stmt_free (stmt);

	/*if (!e_book_sqlite_unlock (self->priv->bsql, success ? EBSQL_UNLOCK_COMMIT : EBSQL_UNLOCK_ROLLBACK, success ? error : NULL))
		success = FALSE;*/

	/* a row had been deleted */
	if (success && !dec_ref_counts)
		e_book_sqlite_keys_emit_changed (self);

	return success;
}

gboolean
e_book_sqlite_keys_remove_all_sync (EBookSqliteKeys *self,
				    GCancellable *cancellable,
				    GError **error)
{
	gchar *stmt;
	gint64 n_stored = 0;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_SQLITE_KEYS (self), FALSE);

	/*if (!e_book_sqlite_lock (self->priv->bsql, EBSQL_LOCK_WRITE, cancellable, error))
		return FALSE;*/

	success = e_book_sqlite_keys_count_keys_sync (self, &n_stored, cancellable, error);

	if (success && n_stored == 0)
		return TRUE; /*e_book_sqlite_unlock (self->priv->bsql, EBSQL_UNLOCK_COMMIT, error);*/

	stmt = e_cache_sqlite_stmt_printf ("DELETE FROM %s", self->priv->table_name);
	success = e_book_sqlite_exec (self->priv->bsql, stmt, cancellable, error);
	e_cache_sqlite_stmt_free (stmt);

	/*if (!e_book_sqlite_unlock (self->priv->bsql, success ? EBSQL_UNLOCK_COMMIT : EBSQL_UNLOCK_ROLLBACK, success ? error : NULL))
		success = FALSE;*/

	if (success)
		e_book_sqlite_keys_emit_changed (self);

	return success;
}

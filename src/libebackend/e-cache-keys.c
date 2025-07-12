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

/**
 * SECTION: e-cache-keys
 * @include: libebackend/libebackend.h
 * @short_description: A table wrapper for key-value reference-counted data
 *
 * The #ECacheKeys represents a table, which holds key-value data
 * with keys being reference-counted, thus the table changes keys
 * as they are added and removed.
 **/

#include "evolution-data-server-config.h"

#include "e-cache.h"

#include "e-cache-keys.h"

#define REFS_COLUMN_NAME "refs"

struct _ECacheKeysPrivate {
	ECache *cache; /* not referenced */
	gchar *table_name;
	gchar *key_column_name;
	gchar *value_column_name;
};

enum {
	PROP_0,
	PROP_CACHE,
	PROP_TABLE_NAME,
	PROP_KEY_COLUMN_NAME,
	PROP_VALUE_COLUMN_NAME,
	N_PROPS
};

enum {
	CHANGED,
	LAST_SIGNAL
};

static GParamSpec *properties[N_PROPS] = { NULL, };

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_PRIVATE (ECacheKeys, e_cache_keys, G_TYPE_OBJECT)

static void
e_cache_keys_set_property (GObject *object,
			   guint property_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	ECacheKeys *self = E_CACHE_KEYS (object);

	switch (property_id) {
		case PROP_CACHE:
			g_return_if_fail (self->priv->cache == NULL);
			self->priv->cache = g_value_get_object (value);
			return;

		case PROP_TABLE_NAME:
			g_return_if_fail (self->priv->table_name == NULL);
			self->priv->table_name = g_value_dup_string (value);
			g_return_if_fail (self->priv->table_name != NULL);
			return;

		case PROP_KEY_COLUMN_NAME:
			g_return_if_fail (self->priv->key_column_name == NULL);
			self->priv->key_column_name = g_value_dup_string (value);
			g_return_if_fail (self->priv->key_column_name != NULL);
			g_return_if_fail (g_ascii_strcasecmp (self->priv->key_column_name, REFS_COLUMN_NAME) != 0);
			return;

		case PROP_VALUE_COLUMN_NAME:
			g_return_if_fail (self->priv->value_column_name == NULL);
			self->priv->value_column_name = g_value_dup_string (value);
			g_return_if_fail (self->priv->value_column_name != NULL);
			g_return_if_fail (g_ascii_strcasecmp (self->priv->value_column_name, REFS_COLUMN_NAME) != 0);
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_cache_keys_get_property (GObject *object,
			   guint property_id,
			   GValue *value,
			   GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CACHE:
			g_value_set_object (value,
				e_cache_keys_get_cache (E_CACHE_KEYS (object)));
			return;

		case PROP_TABLE_NAME:
			g_value_set_string (value,
				e_cache_keys_get_table_name (E_CACHE_KEYS (object)));
			return;

		case PROP_KEY_COLUMN_NAME:
			g_value_set_string (value,
				e_cache_keys_get_key_column_name (E_CACHE_KEYS (object)));
			return;

		case PROP_VALUE_COLUMN_NAME:
			g_value_set_string (value,
				e_cache_keys_get_value_column_name (E_CACHE_KEYS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_cache_keys_finalize (GObject *object)
{
	ECacheKeys *self = E_CACHE_KEYS (object);

	g_clear_pointer (&self->priv->table_name, g_free);
	g_clear_pointer (&self->priv->key_column_name, g_free);
	g_clear_pointer (&self->priv->value_column_name, g_free);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cache_keys_parent_class)->finalize (object);
}

static void
e_cache_keys_class_init (ECacheKeysClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->set_property = e_cache_keys_set_property;
	object_class->get_property = e_cache_keys_get_property;
	object_class->finalize = e_cache_keys_finalize;

	/**
	 * ECacheKeys:cache:
	 *
	 * The #ECache being used for this keys table.
	 *
	 * Since: 3.48
	 **/
	properties[PROP_CACHE] =
		g_param_spec_object (
			"cache",
			NULL, NULL,
			E_TYPE_CACHE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * ECacheKeys:table-name:
	 *
	 * The table name of this keys table.
	 *
	 * Since: 3.48
	 **/
	properties[PROP_TABLE_NAME] =
		g_param_spec_string (
			"table-name",
			NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * ECacheKeys:key-column-name:
	 *
	 * The column name for the keys.
	 *
	 * Since: 3.48
	 **/
	properties[PROP_KEY_COLUMN_NAME] =
		g_param_spec_string (
			"key-column-name",
			NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * ECacheKeys:value-column-name:
	 *
	 * The column name for the values.
	 *
	 * Since: 3.48
	 **/
	properties[PROP_VALUE_COLUMN_NAME] =
		g_param_spec_string (
			"value-column-name",
			NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * ECacheKeys::changed:
	 *
	 * A signal emitted when the stored keys changed, aka when a new
	 * key is added or when an existing key is removed. It's not emitted
	 * when only a reference count changes for a key.
	 *
	 * Since: 3.48
	 **/
	signals[CHANGED] = g_signal_new (
		"changed",
		G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ECacheKeysClass, changed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 0,
		G_TYPE_NONE);
}

static void
e_cache_keys_init (ECacheKeys *self)
{
	self->priv = e_cache_keys_get_instance_private (self);
}

/**
 * e_cache_keys_new:
 * @cache: (transfer none): an #ECache
 * @table_name: a table name to operate with
 * @key_column_name: column name for the keys
 * @value_column_name: column name for the values
 *
 * Creates a new #ECacheKeys, which will operate with @table_name,
 * using column @key_column_name to store keys and @value_column_name
 * to store values.
 *
 * The created #ECacheKeys doesn't hold a reference to the @cache,
 * the caller is supposed to make sure the @cache won't be freed before
 * the #ECacheKeys is freed. This is to avoid circular dependency between
 * the @cache and the #ECacheKeys, when the #ECacheKey is created
 * by the @cache itself (which is the expected use case).
 *
 * Returns: (transfer full): a new #ECacheKeys
 *
 * Since: 3.48
 **/
ECacheKeys *
e_cache_keys_new (ECache *cache,
		  const gchar *table_name,
		  const gchar *key_column_name,
		  const gchar *value_column_name)
{
	g_return_val_if_fail (E_IS_CACHE (cache), NULL);
	g_return_val_if_fail (table_name && *table_name, NULL);
	g_return_val_if_fail (key_column_name && *key_column_name, NULL);
	g_return_val_if_fail (g_ascii_strcasecmp (key_column_name, REFS_COLUMN_NAME) != 0, NULL);
	g_return_val_if_fail (value_column_name && *value_column_name, NULL);
	g_return_val_if_fail (g_ascii_strcasecmp (value_column_name, REFS_COLUMN_NAME) != 0, NULL);

	return g_object_new (E_TYPE_CACHE_KEYS,
		"cache", cache,
		"table-name", table_name,
		"key-column-name", key_column_name,
		"value-column-name", value_column_name,
		NULL);
}

/**
 * e_cache_keys_get_cache:
 * @self: an #ECacheKeys
 *
 * Gets an #ECache, with which the @self had been created.
 *
 * Returns: (transfer none): an #ECache
 *
 * Since: 3.48
 **/
ECache *
e_cache_keys_get_cache (ECacheKeys *self)
{
	g_return_val_if_fail (E_IS_CACHE_KEYS (self), NULL);

	return self->priv->cache;
}

/**
 * e_cache_keys_get_table_name:
 * @self: an #ECacheKeys
 *
 * Gets a table name, with which the @self had been created.
 *
 * Returns: a table name
 *
 * Since: 3.48
 **/
const gchar *
e_cache_keys_get_table_name (ECacheKeys *self)
{
	g_return_val_if_fail (E_IS_CACHE_KEYS (self), NULL);

	return self->priv->table_name;
}

/**
 * e_cache_keys_get_key_column_name:
 * @self: an #ECacheKeys
 *
 * Gets a key column name, with which the @self had been created.
 *
 * Returns: a key column name
 *
 * Since: 3.48
 **/
const gchar *
e_cache_keys_get_key_column_name (ECacheKeys *self)
{
	g_return_val_if_fail (E_IS_CACHE_KEYS (self), NULL);

	return self->priv->table_name;
}

/**
 * e_cache_keys_get_value_column_name:
 * @self: an #ECacheKeys
 *
 * Get a value column name, with which the @self had been created.
 *
 * Returns: a value column name
 *
 * Since: 3.48
 **/
const gchar *
e_cache_keys_get_value_column_name (ECacheKeys *self)
{
	g_return_val_if_fail (E_IS_CACHE_KEYS (self), NULL);

	return self->priv->table_name;
}

static gboolean
e_cache_keys_get_string (ECache *cache,
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
e_cache_keys_get_int64_cb (ECache *cache,
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
e_cache_keys_get_current_refs (ECacheKeys *self,
			       const gchar *key,
			       GCancellable *cancellable,
			       GError **error)
{
	gint64 existing_refs = -1;
	gchar *stmt;

	g_return_val_if_fail (E_IS_CACHE_KEYS (self), -1);
	g_return_val_if_fail (key != NULL, -1);

	stmt = e_cache_sqlite_stmt_printf ("SELECT " REFS_COLUMN_NAME " FROM %s WHERE %s=%Q",
		self->priv->table_name, self->priv->key_column_name, key);

	if (!e_cache_sqlite_select (self->priv->cache, stmt, e_cache_keys_get_int64_cb, &existing_refs, cancellable, error))
		existing_refs = -1;

	e_cache_sqlite_stmt_free (stmt);

	return (gint) existing_refs;
}

static void
e_cache_keys_emit_changed (ECacheKeys *self)
{
	g_signal_emit (self, signals[CHANGED], 0, NULL);
}

/**
 * e_cache_keys_init_table_sync:
 * @self: an #ECacheKeys
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Initializes table in the corresponding #ECache.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.48
 **/
gboolean
e_cache_keys_init_table_sync (ECacheKeys *self,
			      GCancellable *cancellable,
			      GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_CACHE_KEYS (self), FALSE);

	stmt = e_cache_sqlite_stmt_printf ("CREATE TABLE IF NOT EXISTS %Q ("
		"%s TEXT PRIMARY KEY, "
		"%s TEXT, "
		"%s INTEGER)",
		self->priv->table_name,
		self->priv->key_column_name,
		self->priv->value_column_name,
		REFS_COLUMN_NAME);
	success = e_cache_sqlite_exec (self->priv->cache, stmt, cancellable, error);
	e_cache_sqlite_stmt_free (stmt);

	return success;
}

/**
 * e_cache_keys_count_keys_sync:
 * @self: an #ECacheKeys
 * @out_n_stored: (out): return location to set count of stored keys
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Counts how many keys the @self stores and set it to the @out_n_stored.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.48
 **/
gboolean
e_cache_keys_count_keys_sync (ECacheKeys *self,
			      gint64 *out_n_stored,
			      GCancellable *cancellable,
			      GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_CACHE_KEYS (self), FALSE);
	g_return_val_if_fail (out_n_stored != NULL, FALSE);

	*out_n_stored = 0;

	stmt = e_cache_sqlite_stmt_printf ("SELECT COUNT(*) FROM %s", self->priv->table_name);
	success = e_cache_sqlite_select (self->priv->cache, stmt,
		e_cache_keys_get_int64_cb, out_n_stored, cancellable, error);
	e_cache_sqlite_stmt_free (stmt);

	return success;
}

/**
 * e_cache_keys_put_sync:
 * @self: an #ECacheKeys
 * @key: a key identifier to put
 * @value: a value to put with the @key
 * @inc_ref_counts: how many refs to add, or 0 to have it stored forever
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Puts the @key and @value into the @self. The function adds a new or
 * replaces an existing @key, if any such already exists in the @self.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.48
 **/
gboolean
e_cache_keys_put_sync (ECacheKeys *self,
		       const gchar *key,
		       const gchar *value,
		       guint inc_ref_counts,
		       GCancellable *cancellable,
		       GError **error)
{
	gboolean success;
	gint current_refs;
	gchar *stmt;

	g_return_val_if_fail (E_IS_CACHE_KEYS (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	e_cache_lock (self->priv->cache, E_CACHE_LOCK_WRITE);

	current_refs = e_cache_keys_get_current_refs (self, key, cancellable, NULL);

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

	success = e_cache_sqlite_exec (self->priv->cache, stmt, cancellable, error);

	e_cache_sqlite_stmt_free (stmt);

	e_cache_unlock (self->priv->cache, success ? E_CACHE_UNLOCK_COMMIT : E_CACHE_UNLOCK_ROLLBACK);

	/* a new row had been inserted */
	if (success && current_refs < 0)
		e_cache_keys_emit_changed (self);

	return success;
}

/**
 * e_cache_keys_get_sync:
 * @self: an #ECacheKeys
 * @key: a key to get
 * @out_value: (out) (transfer full): return location for the stored value for the @key
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets a stored value with given @key, which had been previously put
 * into the @self with e_cache_keys_put_sync().
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.48
 **/
gboolean
e_cache_keys_get_sync (ECacheKeys *self,
		       const gchar *key,
		       gchar **out_value,
		       GCancellable *cancellable,
		       GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_CACHE_KEYS (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (out_value != NULL, FALSE);

	*out_value = NULL;

	stmt = e_cache_sqlite_stmt_printf ("SELECT %s FROM %s WHERE %s=%Q",
		self->priv->value_column_name,
		self->priv->table_name,
		self->priv->key_column_name,
		key);

	success = e_cache_sqlite_select (self->priv->cache, stmt, e_cache_keys_get_string, out_value, cancellable, error) &&
		*out_value != NULL;

	e_cache_sqlite_stmt_free (stmt);

	return success;
}

/**
 * e_cache_keys_get_ref_count_sync:
 * @self: an #ECacheKeys
 * @key: a key to get reference count for
 * @out_ref_count: (out): return location for the stored reference count for the @key
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets currently stored reference count for the @key.
 * Note the reference count can be 0, which means the @key
 * is stored forever.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.48
 **/
gboolean
e_cache_keys_get_ref_count_sync (ECacheKeys *self,
				 const gchar *key,
				 guint *out_ref_count,
				 GCancellable *cancellable,
				 GError **error)
{
	gint ref_count;

	g_return_val_if_fail (E_IS_CACHE_KEYS (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (out_ref_count != NULL, FALSE);

	ref_count = e_cache_keys_get_current_refs (self, key, cancellable, error);

	if (ref_count >= 0)
		*out_ref_count = (guint) ref_count;
	else
		*out_ref_count = 0;

	return ref_count >= 0;
}

typedef struct _ForeachData {
	ECacheKeys *self;
	ECacheKeysForeachFunc func;
	gpointer user_data;
	gboolean columns_tested;
} ForeachData;

static gboolean
e_cache_keys_foreach_cb (ECache *cache,
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

/**
 * e_cache_keys_foreach_sync:
 * @self: an #ECacheKeys
 * @func: (scope call): an #ECacheKeysForeachFunc, which is called for each stored key
 * @user_data: user data for the @func
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Calls @func for each stored key in the @self, providing
 * information about its value and reference count.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.48
 **/
gboolean
e_cache_keys_foreach_sync (ECacheKeys *self,
			   ECacheKeysForeachFunc func,
			   gpointer user_data,
			   GCancellable *cancellable,
			   GError **error)
{
	ForeachData fd;
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_CACHE_KEYS (self), FALSE);
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
	success = e_cache_sqlite_select (self->priv->cache, stmt,
		e_cache_keys_foreach_cb, &fd, cancellable, error);
	e_cache_sqlite_stmt_free (stmt);

	return success;
}

/**
 * e_cache_keys_remove_sync:
 * @self: an #ECacheKeys
 * @key: a key to remove/dereference
 * @dec_ref_counts: reference counts to drop, 0 to remove it regardless of the current reference count
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Dereferences use count of the @key by @dec_ref_counts and removes it
 * from the cache when the reference count reaches zero. Special case is
 * with @dec_ref_counts is zero, in which case the key is removed regardless
 * of the current reference count.
 *
 * It's not an error when the key doesn't exist in the cache.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.48
 **/
gboolean
e_cache_keys_remove_sync (ECacheKeys *self,
			  const gchar *key,
			  guint dec_ref_counts,
			  GCancellable *cancellable,
			  GError **error)
{
	gint current_refs;
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_CACHE_KEYS (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	e_cache_lock (self->priv->cache, E_CACHE_LOCK_WRITE);

	current_refs = e_cache_keys_get_current_refs (self, key, cancellable, NULL);
	if (current_refs <= 0) {
		e_cache_unlock (self->priv->cache, E_CACHE_UNLOCK_COMMIT);

		return TRUE;
	}

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

	success = e_cache_sqlite_exec (self->priv->cache, stmt, cancellable, error);

	e_cache_sqlite_stmt_free (stmt);

	e_cache_unlock (self->priv->cache, success ? E_CACHE_UNLOCK_COMMIT : E_CACHE_UNLOCK_ROLLBACK);

	/* a row had been deleted */
	if (success && !dec_ref_counts)
		e_cache_keys_emit_changed (self);

	return success;
}

/**
 * e_cache_keys_remove_all_sync:
 * @self: an #ECacheKeys
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Removes all stored keys from the @self.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.48
 **/
gboolean
e_cache_keys_remove_all_sync (ECacheKeys *self,
			      GCancellable *cancellable,
			      GError **error)
{
	gchar *stmt;
	gint64 n_stored = 0;
	gboolean success;

	g_return_val_if_fail (E_IS_CACHE_KEYS (self), FALSE);

	e_cache_lock (self->priv->cache, E_CACHE_LOCK_WRITE);

	success = e_cache_keys_count_keys_sync (self, &n_stored, cancellable, error);

	if (success && n_stored == 0) {
		e_cache_unlock (self->priv->cache, E_CACHE_UNLOCK_COMMIT);
		return TRUE;
	}

	stmt = e_cache_sqlite_stmt_printf ("DELETE FROM %s", self->priv->table_name);
	success = e_cache_sqlite_exec (self->priv->cache, stmt, cancellable, error);
	e_cache_sqlite_stmt_free (stmt);

	e_cache_unlock (self->priv->cache, success ? E_CACHE_UNLOCK_COMMIT : E_CACHE_UNLOCK_ROLLBACK);

	if (success)
		e_cache_keys_emit_changed (self);

	return success;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2017 Red Hat, Inc. (www.redhat.com)
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

#if !defined (__LIBEBACKEND_H_INSIDE__) && !defined (LIBEBACKEND_COMPILATION)
#error "Only <libebackend/libebackend.h> should be included directly."
#endif

#ifndef E_CACHE_H
#define E_CACHE_H

#include <glib-object.h>
#include <gio/gio.h>
#include <libebackend/e-backend-enums.h>

/* Standard GObject macros */
#define E_TYPE_CACHE \
	(e_cache_get_type ())
#define E_CACHE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CACHE, ECache))
#define E_CACHE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CACHE, ECacheClass))
#define E_IS_CACHE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CACHE))
#define E_IS_CACHE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CACHE))
#define E_CACHE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CACHE, ECacheClass))

G_BEGIN_DECLS

#define E_CACHE_TABLE_OBJECTS	"ECacheObjects"
#define E_CACHE_TABLE_KEYS	"ECacheKeys"

#define E_CACHE_COLUMN_UID	"ECacheUID"
#define E_CACHE_COLUMN_REVISION	"ECacheREV"
#define E_CACHE_COLUMN_OBJECT	"ECacheOBJ"
#define E_CACHE_COLUMN_STATE	"ECacheState"

/**
 * E_CACHE_ERROR:
 *
 * Error domain for #ECache operations.
 *
 * Since: 3.26
 **/
#define E_CACHE_ERROR (e_cache_error_quark ())

GQuark		e_cache_error_quark	(void);

/**
 * ECacheError:
 * @E_CACHE_ERROR_ENGINE: An error was reported from the SQLite engine
 * @E_CACHE_ERROR_CONSTRAINT: The error occurred due to an explicit constraint, like
 *    when attempting to add two objects with the same UID.
 * @E_CACHE_ERROR_NOT_FOUND: An object was not found by UID (this is
 *    different from a query that returns no results, which is not an error).
 * @E_CACHE_ERROR_INVALID_QUERY: A query was invalid.
 * @E_CACHE_ERROR_UNSUPPORTED_FIELD: A field requested for inclusion in summary is not supported.
 * @E_CACHE_ERROR_UNSUPPORTED_QUERY: A query was not supported.
 * @E_CACHE_ERROR_END_OF_LIST: An attempt was made to fetch results past the end of a the list.
 * @E_CACHE_ERROR_LOAD: An error occured while loading or creating the database.
 *
 * Defines the types of possible errors reported by the #ECache
 *
 * Since: 3.26
 */
typedef enum {
	E_CACHE_ERROR_ENGINE,
	E_CACHE_ERROR_CONSTRAINT,
	E_CACHE_ERROR_NOT_FOUND,
	E_CACHE_ERROR_INVALID_QUERY,
	E_CACHE_ERROR_UNSUPPORTED_FIELD,
	E_CACHE_ERROR_UNSUPPORTED_QUERY,
	E_CACHE_ERROR_END_OF_LIST,
	E_CACHE_ERROR_LOAD
} ECacheError;

typedef struct {
	gchar *uid;
	EOfflineState state;
} ECacheOfflineChange;

#define E_TYPE_CACHE_OFFLINE_CHANGE (e_cache_offline_change_get_type ())

GType		e_cache_offline_change_get_type	(void) G_GNUC_CONST;
ECacheOfflineChange *
		e_cache_offline_change_new	(const gchar *uid,
						 EOfflineState state);
ECacheOfflineChange *
		e_cache_offline_change_copy	(const ECacheOfflineChange *change);
void		e_cache_offline_change_free	(/* ECacheOfflineChange */ gpointer change);

typedef struct {
	gchar *name;
	gchar *type;
	gchar *index_name;
} ECacheColumnInfo;

#define E_TYPE_CACHE_COLUMN_INFO (e_cache_column_info_get_type ())
GType		e_cache_column_info_get_type	(void) G_GNUC_CONST;
ECacheColumnInfo *
		e_cache_column_info_new		(const gchar *name,
						 const gchar *type,
						 const gchar *index_name);
ECacheColumnInfo *
		e_cache_column_info_copy	(const ECacheColumnInfo *info);
void		e_cache_column_info_free	(/* ECacheColumnInfo */ gpointer info);

/**
 * ECacheLockType:
 * @E_CACHE_LOCK_READ: Obtain a lock for reading.
 * @E_CACHE_LOCK_WRITE: Obtain a lock for writing. This also starts a transaction.
 *
 * Indicates the type of lock requested in e_cache_lock().
 *
 * Since: 3.24
 **/
typedef enum {
	E_CACHE_LOCK_READ,
	E_CACHE_LOCK_WRITE
} ECacheLockType;

/**
 * ECacheUnlockAction:
 * @E_CACHE_UNLOCK_NONE: Just unlock, this is appropriate for locks which were obtained with %E_CACHE_LOCK_READ.
 * @E_CACHE_UNLOCK_COMMIT: Commit any modifications which were made while the lock was held.
 * @E_CACHE_UNLOCK_ROLLBACK: Rollback any modifications which were made while the lock was held.
 *
 * Indicates what type of action to take while unlocking the cache with e_cache_unlock().
 *
 * Since: 3.24
 **/
typedef enum {
	E_CACHE_UNLOCK_NONE,
	E_CACHE_UNLOCK_COMMIT,
	E_CACHE_UNLOCK_ROLLBACK
} ECacheUnlockAction;

typedef struct _ECache ECache;
typedef struct _ECacheClass ECacheClass;
typedef struct _ECachePrivate ECachePrivate;

/**
 * ECacheForeachFunc:
 * @cache: an #ECache
 * @uid: a unique object identifier
 * @revision: the object revision
 * @object: the object itself
 * @offline_state: objects offline state, one of #EOfflineState
 * @ncols: count of columns, items in column_names and column_values
 * @column_names: column names
 * @column_values: column values
 * @user_data: user data, as used in e_cache_foreach()
 *
 * A callback called for each object row when using e_cache_foreach() function.
 *
 * Returns: %TRUE to continue, %FALSE to stop walk through.
 *
 * Since: 3.26
 **/
typedef gboolean (* ECacheForeachFunc)	(ECache *cache,
					 const gchar *uid,
					 const gchar *revision,
					 const gchar *object,
					 EOfflineState offline_state,
					 gint ncols,
					 const gchar *column_names[],
					 const gchar *column_values[],
					 gpointer user_data);

/**
 * ECacheUpdateFunc:
 * @cache: an #ECache
 * @uid: a unique object identifier
 * @revision: the object revision
 * @object: the object itself
 * @offline_state: objects offline state, one of #EOfflineState
 * @ncols: count of columns, items in column_names and column_values
 * @column_names: column names
 * @column_values: column values
 * @out_revision: (out): the new object revision to set; keep it untouched to not change
 * @out_object: (out): the new object to set; keep it untouched to not change
 * @out_offline_state: (out): the offline state to set; the default is the same as @offline_state
 * @out_other_columns: (out) (element-type utf8 utf8) (transfer full): other columns to set; keep it untouched to not change any
 * @user_data: user data, as used in e_cache_foreach_update()
 *
 * A callback called for each object row when using e_cache_foreach_update() function.
 * When all out parameters are left untouched, then the row is not changed.
 *
 * Returns: %TRUE to continue, %FALSE to stop walk through.
 *
 * Since: 3.26
 **/
typedef gboolean (* ECacheUpdateFunc)	(ECache *cache,
					 const gchar *uid,
					 const gchar *revision,
					 const gchar *object,
					 EOfflineState offline_state,
					 gint ncols,
					 const gchar *column_names[],
					 const gchar *column_values[],
					 gchar **out_revision,
					 gchar **out_object,
					 EOfflineState *out_offline_state,
					 GHashTable **out_other_columns,
					 gpointer user_data);

/**
 * ECacheSelectFunc:
 * @cache: an #ECache
 * @ncols: count of columns, items in column_names and column_values
 * @column_names: column names
 * @column_values: column values
 * @user_data: user data, as used in e_cache_sqlite_select()
 *
 * A callback called for each row of a SELECT statement executed
 * with e_cache_sqlite_select() function.
 *
 * Returns: %TRUE to continue, %FALSE to stop walk through.
 *
 * Since: 3.26
 **/
typedef gboolean (* ECacheSelectFunc)	(ECache *cache,
					 gint ncols,
					 const gchar *column_names[],
					 const gchar *column_values[],
					 gpointer user_data);

/**
 * ECache:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.26
 **/
struct _ECache {
	/*< private >*/
	GObject parent;
	ECachePrivate *priv;
};

struct _ECacheClass {
	GObjectClass parent_class;

	/* Virtual methods */
	gboolean	(* put_locked)		(ECache *cache,
						 const gchar *uid,
						 const gchar *revision,
						 const gchar *object,
						 GHashTable *other_columns,
						 EOfflineState offline_state,
						 gboolean is_replace,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(* remove_locked)	(ECache *cache,
						 const gchar *uid,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(* remove_all_locked)	(ECache *cache,
						 const GSList *uids,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(* clear_offline_changes_locked)
						(ECache *cache,
						 GCancellable *cancellable,
						 GError **error);
	void		(* erase)		(ECache *cache);

	/* Signals */
	gboolean	(* before_put)		(ECache *cache,
						 const gchar *uid,
						 const gchar *revision,
						 const gchar *object,
						 GHashTable *other_columns,
						 gboolean is_replace,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(* before_remove)	(ECache *cache,
						 const gchar *uid,
						 GCancellable *cancellable,
						 GError **error);

	/* Padding for future expansion */
	gpointer reserved[10];
};

GType		e_cache_get_type		(void) G_GNUC_CONST;

gboolean	e_cache_initialize_sync		(ECache *cache,
						 const gchar *filename,
						 const GSList *other_columns, /* ECacheColumnInfo * */
						 GCancellable *cancellable,
						 GError **error);
const gchar *	e_cache_get_filename		(ECache *cache);
gint		e_cache_get_version		(ECache *cache);
void		e_cache_set_version		(ECache *cache,
						 gint version);
gchar *		e_cache_dup_revision		(ECache *cache);
void		e_cache_set_revision		(ECache *cache,
						 const gchar *revision);
void		e_cache_erase			(ECache *cache);
gboolean	e_cache_contains		(ECache *cache,
						 const gchar *uid,
						 gboolean include_deleted);
gchar *		e_cache_get			(ECache *cache,
						 const gchar *uid,
						 gchar **out_revision,
						 GHashTable **out_other_columns,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_put			(ECache *cache,
						 const gchar *uid,
						 const gchar *revision,
						 const gchar *object,
						 GHashTable *other_columns,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_remove			(ECache *cache,
						 const gchar *uid,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_remove_all		(ECache *cache,
						 GCancellable *cancellable,
						 GError **error);
guint		e_cache_count			(ECache *cache,
						 gboolean include_deleted,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_get_uids		(ECache *cache,
						 gboolean include_deleted,
						 GSList **out_uids,
						 GSList **out_revisions,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_get_objects		(ECache *cache,
						 gboolean include_deleted,
						 GSList **out_objects,
						 GSList **out_revisions,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_foreach			(ECache *cache,
						 gboolean include_deleted,
						 const gchar *where_clause,
						 ECacheForeachFunc func,
						 gpointer user_data,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_foreach_update		(ECache *cache,
						 gboolean include_deleted,
						 const gchar *where_clause,
						 ECacheUpdateFunc func,
						 gpointer user_data,
						 GCancellable *cancellable,
						 GError **error);

/* Offline support */
gboolean	e_cache_put_offline		(ECache *cache,
						 const gchar *uid,
						 const gchar *revision,
						 const gchar *object,
						 GHashTable *other_columns,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_remove_offline		(ECache *cache,
						 const gchar *uid,
						 GCancellable *cancellable,
						 GError **error);
EOfflineState	e_cache_get_offline_state	(ECache *cache,
						 const gchar *uid,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_set_offline_state	(ECache *cache,
						 const gchar *uid,
						 EOfflineState state,
						 GCancellable *cancellable,
						 GError **error);
GSList *	e_cache_get_offline_changes	(ECache *cache,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_clear_offline_changes	(ECache *cache,
						 GCancellable *cancellable,
						 GError **error);

/* Custom keys */
gboolean	e_cache_set_key			(ECache *cache,
						 const gchar *key,
						 const gchar *value,
						 GError **error);
gchar *		e_cache_dup_key			(ECache *cache,
						 const gchar *key,
						 GError **error);
gboolean	e_cache_set_key_int		(ECache *cache,
						 const gchar *key,
						 gint value,
						 GError **error);
gint		e_cache_get_key_int		(ECache *cache,
						 const gchar *key,
						 GError **error);

/* Locking */
void		e_cache_lock			(ECache *cache,
						 ECacheLockType lock_type);
void		e_cache_unlock			(ECache *cache,
						 ECacheUnlockAction action);

/* Low-level SQLite functions */
gpointer	e_cache_get_sqlitedb		(ECache *cache);
gboolean	e_cache_sqlite_exec		(ECache *cache,
						 const gchar *sql_stmt,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_sqlite_select		(ECache *cache,
						 const gchar *sql_stmt,
						 ECacheSelectFunc func,
						 gpointer user_data,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_sqlite_maybe_vacuum	(ECache *cache,
						 GCancellable *cancellable,
						 GError **error);

void		e_cache_sqlite_stmt_append_printf
						(GString *stmt,
						 const gchar *format,
						 ...);
gchar *		e_cache_sqlite_stmt_printf	(const gchar *format,
						 ...);
void		e_cache_sqlite_stmt_free	(gchar *stmt);

G_END_DECLS

#endif /* E_CACHE_H */

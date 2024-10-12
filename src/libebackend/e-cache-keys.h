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

#if !defined (__LIBEBACKEND_H_INSIDE__) && !defined (LIBEBACKEND_COMPILATION)
#error "Only <libebackend/libebackend.h> should be included directly."
#endif

#ifndef E_CACHE_KEYS_H
#define E_CACHE_KEYS_H

#include <glib-object.h>
#include <gio/gio.h>
#include <libebackend/e-cache.h>

/* Standard GObject macros */
#define E_TYPE_CACHE_KEYS \
	(e_cache_keys_get_type ())
#define E_CACHE_KEYS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CACHE_KEYS, ECacheKeys))
#define E_CACHE_KEYS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CACHE_KEYS, ECacheKeysClass))
#define E_IS_CACHE_KEYS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CACHE_KEYS))
#define E_IS_CACHE_KEYS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CACHE_KEYS))
#define E_CACHE_KEYS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CACHE_KEYS, ECacheKeysClass))

G_BEGIN_DECLS

typedef struct _ECacheKeys ECacheKeys;
typedef struct _ECacheKeysClass ECacheKeysClass;
typedef struct _ECacheKeysPrivate ECacheKeysPrivate;

/**
 * ECacheKeysForeachFunc:
 * @self: an #ECacheKeys
 * @key: the key
 * @value: the value
 * @ref_count: the reference count for the @key
 * @user_data: user data, as used in e_cache_keys_foreach_sync()
 *
 * A callback called for each row of the @self table when
 * using e_cache_keys_foreach_sync() function.
 *
 * Returns: %TRUE to continue, %FALSE to stop walk through.
 *
 * Since: 3.48
 **/
typedef gboolean (* ECacheKeysForeachFunc)	(ECacheKeys *self,
						 const gchar *key,
						 const gchar *value,
						 guint ref_count,
						 gpointer user_data);

/**
 * ECacheKeys:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.48
 **/
struct _ECacheKeys {
	/*< private >*/
	GObject parent;
	ECacheKeysPrivate *priv;
};

struct _ECacheKeysClass {
	/*< private >*/
	GObjectClass parent_class;

	/* Signals */
	void		(* changed)		(ECacheKeys *self);

	/* Padding for future expansion */
	gpointer reserved[10];
};

GType		e_cache_keys_get_type		(void) G_GNUC_CONST;

ECacheKeys *	e_cache_keys_new		(ECache *cache,
						 const gchar *table_name,
						 const gchar *key_column_name,
						 const gchar *value_column_name);
ECache *	e_cache_keys_get_cache		(ECacheKeys *self);
const gchar *	e_cache_keys_get_table_name	(ECacheKeys *self);
const gchar *	e_cache_keys_get_key_column_name(ECacheKeys *self);
const gchar *	e_cache_keys_get_value_column_name
						(ECacheKeys *self);
gboolean	e_cache_keys_init_table_sync	(ECacheKeys *self,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_keys_count_keys_sync	(ECacheKeys *self,
						 gint64 *out_n_stored,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_keys_put_sync		(ECacheKeys *self,
						 const gchar *key,
						 const gchar *value,
						 guint inc_ref_counts,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_keys_get_sync		(ECacheKeys *self,
						 const gchar *key,
						 gchar **out_value,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_keys_get_ref_count_sync	(ECacheKeys *self,
						 const gchar *key,
						 guint *out_ref_count,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_keys_foreach_sync	(ECacheKeys *self,
						 ECacheKeysForeachFunc func,
						 gpointer user_data,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_keys_remove_sync	(ECacheKeys *self,
						 const gchar *key,
						 guint dec_ref_counts,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cache_keys_remove_all_sync	(ECacheKeys *self,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* E_CACHE_KEYS_H */

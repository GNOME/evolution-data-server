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

#ifndef E_BOOK_SQLITE_KEYS_H
#define E_BOOK_SQLITE_KEYS_H

#include <glib-object.h>
#include <gio/gio.h>
#include <libedata-book/libedata-book.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_SQLITE_KEYS \
	(e_book_sqlite_keys_get_type ())
#define E_BOOK_SQLITE_KEYS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_SQLITE_KEYS, EBookSqliteKeys))
#define E_BOOK_SQLITE_KEYS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_SQLITE_KEYS, EBookSqliteKeysClass))
#define E_IS_BOOK_SQLITE_KEYS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_SQLITE_KEYS))
#define E_IS_BOOK_SQLITE_KEYS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_SQLITE_KEYS))
#define E_BOOK_SQLITE_KEYS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_SQLITE_KEYS, EBookSqliteKeysClass))

G_BEGIN_DECLS

typedef struct _EBookSqliteKeys EBookSqliteKeys;
typedef struct _EBookSqliteKeysClass EBookSqliteKeysClass;
typedef struct _EBookSqliteKeysPrivate EBookSqliteKeysPrivate;

typedef gboolean (* EBookSqliteKeysForeachFunc)	(EBookSqliteKeys *self,
						 const gchar *key,
						 const gchar *value,
						 guint ref_count,
						 gpointer user_data);

struct _EBookSqliteKeys {
	/*< private >*/
	GObject parent;
	EBookSqliteKeysPrivate *priv;
};

struct _EBookSqliteKeysClass {
	/*< private >*/
	GObjectClass parent_class;

	/* Signals */
	void		(* changed)		(EBookSqliteKeys *self);

	/* Padding for future expansion */
	gpointer reserved[10];
};

GType		e_book_sqlite_keys_get_type	(void) G_GNUC_CONST;

EBookSqliteKeys *
		e_book_sqlite_keys_new		(EBookSqlite *ebsql,
						 const gchar *table_name,
						 const gchar *key_column_name,
						 const gchar *value_column_name);
gboolean	e_book_sqlite_keys_init_table_sync
						(EBookSqliteKeys *self,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_book_sqlite_keys_count_keys_sync
						(EBookSqliteKeys *self,
						 gint64 *out_n_stored,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_book_sqlite_keys_put_sync	(EBookSqliteKeys *self,
						 const gchar *key,
						 const gchar *value,
						 guint inc_ref_counts,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_book_sqlite_keys_get_sync	(EBookSqliteKeys *self,
						 const gchar *key,
						 gchar **out_value,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_book_sqlite_keys_get_ref_count_sync	(EBookSqliteKeys *self,
						 const gchar *key,
						 guint *out_ref_count,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_book_sqlite_keys_foreach_sync	(EBookSqliteKeys *self,
						 EBookSqliteKeysForeachFunc func,
						 gpointer user_data,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_book_sqlite_keys_remove_sync	(EBookSqliteKeys *self,
						 const gchar *key,
						 guint dec_ref_counts,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_book_sqlite_keys_remove_all_sync
						(EBookSqliteKeys *self,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* E_BOOK_SQLITE_KEYS_H */

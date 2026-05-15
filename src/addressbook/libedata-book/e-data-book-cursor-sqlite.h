/*
 * SPDX-FileCopyrightText: (C) 2013 Intel Corporation
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Tristan Van Berkom <tristanvb@openismus.com>
 */

#if !defined (__LIBEDATA_BOOK_H_INSIDE__) && !defined (LIBEDATA_BOOK_COMPILATION)
#error "Only <libedata-book/libedata-book.h> should be included directly."
#endif

#ifndef E_DATA_BOOK_CURSOR_SQLITE_H
#define E_DATA_BOOK_CURSOR_SQLITE_H

#include <libedata-book/e-data-book-cursor.h>
#include <libedata-book/e-book-sqlite.h>
#include <libedata-book/e-book-backend.h>

#define E_TYPE_DATA_BOOK_CURSOR_SQLITE        (e_data_book_cursor_sqlite_get_type ())
#define E_DATA_BOOK_CURSOR_SQLITE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DATA_BOOK_CURSOR_SQLITE, EDataBookCursorSqlite))
#define E_DATA_BOOK_CURSOR_SQLITE_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_DATA_BOOK_CURSOR_SQLITE, EDataBookCursorSqliteClass))
#define E_IS_DATA_BOOK_CURSOR_SQLITE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DATA_BOOK_CURSOR_SQLITE))
#define E_IS_DATA_BOOK_CURSOR_SQLITE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DATA_BOOK_CURSOR_SQLITE))
#define E_DATA_BOOK_CURSOR_SQLITE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_DATA_BOOK_CURSOR_SQLITE, EDataBookCursorSqliteClass))

G_BEGIN_DECLS

typedef struct _EDataBookCursorSqlite EDataBookCursorSqlite;
typedef struct _EDataBookCursorSqliteClass EDataBookCursorSqliteClass;
typedef struct _EDataBookCursorSqlitePrivate EDataBookCursorSqlitePrivate;

/**
 * EDataBookCursorSqlite:
 *
 * An opaque handle for the SQLite cursor instance.
 *
 * Since: 3.12
 */
struct _EDataBookCursorSqlite {
	/*< private >*/
	EDataBookCursor parent;
	EDataBookCursorSqlitePrivate *priv;
};

/**
 * EDataBookCursorSqliteClass:
 *
 * The SQLite cursor class structure.
 *
 * Since: 3.12
 */
struct _EDataBookCursorSqliteClass {
	/*< private >*/
	EDataBookCursorClass parent;
};

GType			e_data_book_cursor_sqlite_get_type         (void);
EDataBookCursor        *e_data_book_cursor_sqlite_new              (EBookBackend              *backend,
								    EBookSqlite               *ebsql,
								    const gchar               *revision_key,
								    const EContactField       *sort_fields,
								    const EBookCursorSortType *sort_types,
								    guint                      n_fields,
								    GError                   **error);

G_END_DECLS

#endif /* E_DATA_BOOK_CURSOR_SQLITE_H */

/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#if !defined (__LIBEDATA_BOOK_H_INSIDE__) && !defined (LIBEDATA_BOOK_COMPILATION)
#error "Only <libedata-book/libedata-book.h> should be included directly."
#endif

#ifndef E_DATA_BOOK_VIEW_WATCHER_SQLITE_H
#define E_DATA_BOOK_VIEW_WATCHER_SQLITE_H

#include <libebook-contacts/libebook-contacts.h>
#include <libedata-book/e-book-backend.h>
#include <libedata-book/e-book-sqlite.h>

/* Standard GObject macros */
#define E_TYPE_DATA_BOOK_VIEW_WATCHER_SQLITE \
	(e_data_book_view_watcher_sqlite_get_type ())
#define E_DATA_BOOK_VIEW_WATCHER_SQLITE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_DATA_BOOK_VIEW_WATCHER_SQLITE, EDataBookViewWatcherSqlite))
#define E_DATA_BOOK_VIEW_WATCHER_SQLITE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_DATA_BOOK_VIEW_WATCHER_SQLITE, EDataBookViewWatcherSqliteClass))
#define E_IS_DATA_BOOK_VIEW_WATCHER_SQLITE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_DATA_BOOK_VIEW_WATCHER_SQLITE))
#define E_IS_DATA_BOOK_VIEW_WATCHER_SQLITE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_DATA_BOOK_VIEW_WATCHER_SQLITE))
#define E_DATA_BOOK_VIEW_WATCHER_SQLITE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_DATA_BOOK_VIEW_WATCHER_SQLITE, EDataBookViewWatcherSqliteClass))

G_BEGIN_DECLS

typedef struct _EDataBookViewWatcherSqlite EDataBookViewWatcherSqlite;
typedef struct _EDataBookViewWatcherSqliteClass EDataBookViewWatcherSqliteClass;
typedef struct _EDataBookViewWatcherSqlitePrivate EDataBookViewWatcherSqlitePrivate;

/**
 * EDataBookViewWatcherSqlite:
 *
 * A structure used to handle "manual query" views for #EBookBackend
 * descendants which use #EBookSqlite to store the contacts.
 *
 * Since: 3.50
 */
struct _EDataBookViewWatcherSqlite {
	/*< private >*/
	EBookIndicesUpdater parent;
	EDataBookViewWatcherSqlitePrivate *priv;
};

struct _EDataBookViewWatcherSqliteClass {
	/*< private >*/
	EBookIndicesUpdaterClass parent_class;
};

GType		e_data_book_view_watcher_sqlite_get_type(void) G_GNUC_CONST;
GObject *	e_data_book_view_watcher_sqlite_new	(EBookBackend *backend,
							 EBookSqlite *ebsql,
							 EDataBookView *view);
void		e_data_book_view_watcher_sqlite_take_sort_fields
							(EDataBookViewWatcherSqlite *self,
							 EBookClientViewSortFields *sort_fields);
GPtrArray *	e_data_book_view_watcher_sqlite_dup_contacts /* EContact * */
							(EDataBookViewWatcherSqlite *self,
							 guint range_start,
							 guint range_length);

G_END_DECLS

#endif /* E_DATA_BOOK_VIEW_WATCHER_SQLITE_H */

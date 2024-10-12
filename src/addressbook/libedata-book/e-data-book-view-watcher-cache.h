/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#if !defined (__LIBEDATA_BOOK_H_INSIDE__) && !defined (LIBEDATA_BOOK_COMPILATION)
#error "Only <libedata-book/libedata-book.h> should be included directly."
#endif

#ifndef E_DATA_BOOK_VIEW_WATCHER_CACHE_H
#define E_DATA_BOOK_VIEW_WATCHER_CACHE_H

#include <libebook-contacts/libebook-contacts.h>
#include <libedata-book/e-book-backend.h>
#include <libedata-book/e-book-cache.h>

/* Standard GObject macros */
#define E_TYPE_DATA_BOOK_VIEW_WATCHER_CACHE \
	(e_data_book_view_watcher_cache_get_type ())
#define E_DATA_BOOK_VIEW_WATCHER_CACHE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_DATA_BOOK_VIEW_WATCHER_CACHE, EDataBookViewWatcherCache))
#define E_DATA_BOOK_VIEW_WATCHER_CACHE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_DATA_BOOK_VIEW_WATCHER_CACHE, EDataBookViewWatcherCacheClass))
#define E_IS_DATA_BOOK_VIEW_WATCHER_CACHE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_DATA_BOOK_VIEW_WATCHER_CACHE))
#define E_IS_DATA_BOOK_VIEW_WATCHER_CACHE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_DATA_BOOK_VIEW_WATCHER_CACHE))
#define E_DATA_BOOK_VIEW_WATCHER_CACHE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_DATA_BOOK_VIEW_WATCHER_CACHE, EDataBookViewWatcherCacheClass))

G_BEGIN_DECLS

typedef struct _EDataBookViewWatcherCache EDataBookViewWatcherCache;
typedef struct _EDataBookViewWatcherCacheClass EDataBookViewWatcherCacheClass;
typedef struct _EDataBookViewWatcherCachePrivate EDataBookViewWatcherCachePrivate;

/**
 * EDataBookViewWatcherCache:
 *
 * A structure used to handle "manual query" views for #EBookBackend
 * descendants which use #EBookCache to store the contacts.
 *
 * Since: 3.50
 */
struct _EDataBookViewWatcherCache {
	/*< private >*/
	EBookIndicesUpdater parent;
	EDataBookViewWatcherCachePrivate *priv;
};

struct _EDataBookViewWatcherCacheClass {
	/*< private >*/
	EBookIndicesUpdaterClass parent_class;
};

GType		e_data_book_view_watcher_cache_get_type(void) G_GNUC_CONST;
GObject *	e_data_book_view_watcher_cache_new	(EBookBackend *backend,
							 EBookCache *cache,
							 EDataBookView *view);
void		e_data_book_view_watcher_cache_take_sort_fields
							(EDataBookViewWatcherCache *self,
							 EBookClientViewSortFields *sort_fields);
GPtrArray *	e_data_book_view_watcher_cache_dup_contacts /* EContact * */
							(EDataBookViewWatcherCache *self,
							 guint range_start,
							 guint range_length);

G_END_DECLS

#endif /* E_DATA_BOOK_VIEW_WATCHER_CACHE_H */

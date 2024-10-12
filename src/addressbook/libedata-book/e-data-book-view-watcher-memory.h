/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#if !defined (__LIBEDATA_BOOK_H_INSIDE__) && !defined (LIBEDATA_BOOK_COMPILATION)
#error "Only <libedata-book/libedata-book.h> should be included directly."
#endif

#ifndef E_DATA_BOOK_VIEW_WATCHER_MEMORY_H
#define E_DATA_BOOK_VIEW_WATCHER_MEMORY_H

#include <libebook-contacts/libebook-contacts.h>
#include <libedata-book/e-book-backend.h>

/* Standard GObject macros */
#define E_TYPE_DATA_BOOK_VIEW_WATCHER_MEMORY \
	(e_data_book_view_watcher_memory_get_type ())
#define E_DATA_BOOK_VIEW_WATCHER_MEMORY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_DATA_BOOK_VIEW_WATCHER_MEMORY, EDataBookViewWatcherMemory))
#define E_DATA_BOOK_VIEW_WATCHER_MEMORY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_DATA_BOOK_VIEW_WATCHER_MEMORY, EDataBookViewWatcherMemoryClass))
#define E_IS_DATA_BOOK_VIEW_WATCHER_MEMORY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_DATA_BOOK_VIEW_WATCHER_MEMORY))
#define E_IS_DATA_BOOK_VIEW_WATCHER_MEMORY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_DATA_BOOK_VIEW_WATCHER_MEMORY))
#define E_DATA_BOOK_VIEW_WATCHER_MEMORY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_DATA_BOOK_VIEW_WATCHER_MEMORY, EDataBookViewWatcherMemoryClass))

G_BEGIN_DECLS

typedef struct _EDataBookViewWatcherMemory EDataBookViewWatcherMemory;
typedef struct _EDataBookViewWatcherMemoryClass EDataBookViewWatcherMemoryClass;
typedef struct _EDataBookViewWatcherMemoryPrivate EDataBookViewWatcherMemoryPrivate;

/**
 * EDataBookViewWatcherMemory:
 *
 * A structure used as a default implementation to
 * handle "manual query" views by the #EBookBackend.
 *
 * Since: 3.50
 */
struct _EDataBookViewWatcherMemory {
	/*< private >*/
	EBookIndicesUpdater parent;
	EDataBookViewWatcherMemoryPrivate *priv;
};

struct _EDataBookViewWatcherMemoryClass {
	/*< private >*/
	EBookIndicesUpdaterClass parent_class;
};

GType		e_data_book_view_watcher_memory_get_type(void) G_GNUC_CONST;
GObject *	e_data_book_view_watcher_memory_new	(EBookBackend *backend,
							 EDataBookView *view);
void		e_data_book_view_watcher_memory_set_locale
							(EDataBookViewWatcherMemory *self,
							 const gchar *locale);
void		e_data_book_view_watcher_memory_take_sort_fields
							(EDataBookViewWatcherMemory *self,
							 EBookClientViewSortFields *sort_fields);
GPtrArray *	e_data_book_view_watcher_memory_dup_contacts /* EContact * */
							(EDataBookViewWatcherMemory *self,
							 guint range_start,
							 guint range_length);

G_END_DECLS

#endif /* E_DATA_BOOK_VIEW_WATCHER_MEMORY_H */

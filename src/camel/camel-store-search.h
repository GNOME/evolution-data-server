/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_STORE_SEARCH_H
#define CAMEL_STORE_SEARCH_H

#include <glib-object.h>

#include <camel/camel-folder.h>
#include <camel/camel-store.h>

G_BEGIN_DECLS

typedef struct _CamelStoreSearchItemPrivate CamelStoreSearchItemPrivate;

/**
 * CamelStoreSearchItem:
 * @folder_id: a folder ID
 * @uid: a message UID
 *
 * A search item object, as returned by the camel_store_search_get_items_sync().
 * It can contain additional values, as requested by the camel_store_search_set_additional_columns(),
 * which can be obtained by camel_store_search_item_get_n_additional_values()
 * and camel_store_search_item_get_additional_value().
 *
 * Since: 3.58
 **/
typedef struct _CamelStoreSearchItem {
	guint32 folder_id;
	const gchar *uid;

	/* < private > */
	CamelStoreSearchItemPrivate *priv;
} CamelStoreSearchItem;

guint32		camel_store_search_item_get_n_additional_values	(CamelStoreSearchItem *self);
const gchar *	camel_store_search_item_get_additional_value	(CamelStoreSearchItem *self,
								 guint32 index);

/**
 * CamelStoreSearchThreadItem:
 *
 * A structure holding data necessary to construct message threads
 * using #CamelFolderThread. It's gathered by camel_store_search_add_match_threads_items_sync().
 *
 * Since: 3.58
 **/
typedef struct _CamelStoreSearchThreadItem CamelStoreSearchThreadItem;

CamelStore *	camel_store_search_thread_item_get_store	(const CamelStoreSearchThreadItem *self);
guint32		camel_store_search_thread_item_get_folder_id	(const CamelStoreSearchThreadItem *self);
const gchar *	camel_store_search_thread_item_get_uid		(const CamelStoreSearchThreadItem *self);
const gchar *	camel_store_search_thread_item_get_subject	(const CamelStoreSearchThreadItem *self);
guint64		camel_store_search_thread_item_get_message_id	(const CamelStoreSearchThreadItem *self);
const GArray *	camel_store_search_thread_item_get_references	(const CamelStoreSearchThreadItem *self); /* guint64 */

/**
 * CamelStoreSearchIndex:
 *
 * A structure with search result indexes, holding references to matching records
 * using the #CamelStore, folder ID and message UID triple. Items can be added
 * to the index with camel_store_search_index_add() and checked its existence
 * with camel_store_search_index_contains().
 *
 * Since: 3.58
 **/
typedef struct _CamelStoreSearchIndex CamelStoreSearchIndex;

#define CAMEL_TYPE_STORE_SEARCH_INDEX (camel_store_search_index_get_type ())
GType		camel_store_search_index_get_type		(void) G_GNUC_CONST;
CamelStoreSearchIndex *
		camel_store_search_index_new			(void);
CamelStoreSearchIndex *
		camel_store_search_index_ref			(CamelStoreSearchIndex *self);
void		camel_store_search_index_unref			(CamelStoreSearchIndex *self);
void		camel_store_search_index_add			(CamelStoreSearchIndex *self,
								 CamelStore *store,
								 guint32 folder_id,
								 const gchar *uid);
gboolean	camel_store_search_index_remove			(CamelStoreSearchIndex *self,
								 CamelStore *store,
								 guint32 folder_id,
								 const gchar *uid);
void		camel_store_search_index_move_from_existing	(CamelStoreSearchIndex *self,
								 CamelStoreSearchIndex *src);
gboolean	camel_store_search_index_contains		(CamelStoreSearchIndex *self,
								 CamelStore *store,
								 guint32 folder_id,
								 const gchar *uid);
void		camel_store_search_index_apply_match_threads	(CamelStoreSearchIndex *self,
								 /* const */ GPtrArray *items, /* CamelStoreSearchThreadItem * */
								 CamelMatchThreadsKind kind,
								 CamelFolderThreadFlags flags,
								 GCancellable *cancellable);

/* Standard GObject macros */
#define CAMEL_TYPE_STORE_SEARCH camel_store_search_get_type ()
#define CAMEL_STORE_SEARCH(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), CAMEL_TYPE_STORE_SEARCH, CamelStoreSearch))
#define CAMEL_STORE_SEARCH_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), CAMEL_TYPE_STORE_SEARCH, CamelStoreSearchClass))
#define CAMEL_IS_STORE_SEARCH(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CAMEL_TYPE_STORE_SEARCH))
#define CAMEL_IS_STORE_SEARCH_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE ((cls), CAMEL_TYPE_STORE_SEARCH))
#define CAMEL_STORE_SEARCH_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), CAMEL_TYPE_STORE_SEARCH, CamelStoreSearchClass))

typedef struct _CamelStoreSearch CamelStoreSearch;
typedef struct _CamelStoreSearchClass CamelStoreSearchClass;
typedef struct _CamelStoreSearchPrivate CamelStoreSearchPrivate;

/**
 * CamelStoreSearch:
 *
 * Since: 3.58
 **/
struct _CamelStoreSearch {
	/*< private >*/
	GObject parent;
	CamelStoreSearchPrivate *priv;
};

struct _CamelStoreSearchClass {
	GObjectClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_store_search_get_type	(void) G_GNUC_CONST;
CamelStoreSearch *
		camel_store_search_new		(CamelStore *store);
CamelStore *	camel_store_search_get_store	(CamelStoreSearch *self);
void		camel_store_search_set_expression
						(CamelStoreSearch *self,
						 const gchar *expression);
const gchar *	camel_store_search_get_expression
						(CamelStoreSearch *self);
void		camel_store_search_set_additional_columns
						(CamelStoreSearch *self,
						 const GPtrArray *colnames); /* gchar * */
GPtrArray *	camel_store_search_dup_additional_columns /* gchar * */
						(CamelStoreSearch *self);
void		camel_store_search_add_folder	(CamelStoreSearch *self,
						 CamelFolder *folder);
void		camel_store_search_remove_folder(CamelStoreSearch *self,
						 CamelFolder *folder);
GPtrArray *	camel_store_search_list_folders	(CamelStoreSearch *self); /* CamelFolder * */
gboolean	camel_store_search_rebuild_sync	(CamelStoreSearch *self,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_store_search_get_items_sync
						(CamelStoreSearch *self,
						 GPtrArray **out_items, /* CamelStoreSearchItem * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_store_search_get_uids_sync(CamelStoreSearch *self,
						 const gchar *folder_name,
						 GPtrArray **out_uids, /* gchar * */
						 GCancellable *cancellable,
						 GError **error);
CamelMatchThreadsKind
		camel_store_search_get_match_threads_kind
						(CamelStoreSearch *self,
						 CamelFolderThreadFlags *out_flags);
gboolean	camel_store_search_add_match_threads_items_sync
						(CamelStoreSearch *self,
						 GPtrArray **inout_items, /* CamelStoreSearchThreadItem * */
						 GCancellable *cancellable,
						 GError **error);
CamelStoreSearchIndex *
		camel_store_search_ref_result_index
						(CamelStoreSearch *self);
void		camel_store_search_set_result_index
						(CamelStoreSearch *self,
						 CamelStoreSearchIndex *index);
void		camel_store_search_add_match_index
						(CamelStoreSearch *self,
						 CamelStoreSearchIndex *index);
void		camel_store_search_remove_match_index
						(CamelStoreSearch *self,
						 CamelStoreSearchIndex *index);
GPtrArray *	camel_store_search_list_match_indexes /* CamelStoreSearchIndex * */
						(CamelStoreSearch *self);

G_END_DECLS

#endif /* CAMEL_STORE_SEARCH_H */

/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <glib.h>
#include <glib-object.h>
#include <sqlite3.h>

#include "camel-folder.h"
#include "camel-sexp.h"
#include "camel-search-utils.h"
#include "camel-store.h"
#include "camel-store-search-private.h"
#include "camel-string-utils.h"

#include "camel-store-search.h"

/**
 * SECTION: camel-store-search
 * @include: camel/camel.h
 * @short_description: search between multiple folders of one #CamelStore
 *
 * The #CamelStoreSearch allows to search between multiple folders
 * of a single #CamelStore. It's primarily meant to be used by
 * the search folders (#CamelVeeFolder), but it can be used without
 * it too.
 *
 * It can run only one search at a time, using camel_store_search_rebuild_sync().
 *
 * The #CamelStoreSearch is not thread safe, it's meant to be created, used and
 * freed from within the same thread only.
 *
 * Since: 3.58
 **/

/* private function in camel-message-info.c */
gboolean
_camel_message_info_util_decode_part (const gchar *db_part,
				      guint64 *out_message_id,
				      GArray **out_references); /* guint64 */

struct _CamelStoreSearchThreadItem {
	CamelStore *store;
	guint32 folder_id;
	const gchar *uid; /* from the string pool */
	const gchar *subject; /* from the string pool */
	guint64 message_id;
	GArray *references; /* guint64 */
};

static CamelStoreSearchThreadItem *
camel_store_search_thread_item_new (CamelStore *store,
				    guint32 folder_id,
				    const gchar *uid,
				    const gchar *db_subject,
				    const gchar *db_part)
{
	CamelStoreSearchThreadItem *self;

	self = g_new0 (CamelStoreSearchThreadItem, 1);
	self->store = g_object_ref (store);
	self->folder_id = folder_id;
	self->uid = camel_pstring_strdup (uid);
	self->subject = camel_pstring_strdup (db_subject);

	_camel_message_info_util_decode_part (db_part, &self->message_id, &self->references);

	return self;
}

static void
camel_store_search_thread_item_free (gpointer ptr)
{
	CamelStoreSearchThreadItem *self = ptr;

	if (self) {
		g_object_unref (self->store);
		camel_pstring_free (self->uid);
		camel_pstring_free (self->subject);
		g_clear_pointer (&self->references, g_array_unref);
		g_free (self);
	}
}

static guint
camel_store_search_thread_item_hash (gconstpointer ptr)
{
	const CamelStoreSearchThreadItem *self = ptr;

	if (!self)
		return 0;

	return g_direct_hash (self->store) ^ g_direct_hash (GUINT_TO_POINTER (self->folder_id)) ^ g_direct_hash (self->uid);
}

static gboolean
camel_store_search_thread_item_equal (gconstpointer ptr1,
				      gconstpointer ptr2)
{
	const CamelStoreSearchThreadItem *item1 = ptr1;
	const CamelStoreSearchThreadItem *item2 = ptr2;

	if (!item1 || !item2)
		return item1 == item2;

	return item1->store == item2->store && item1->folder_id == item2->folder_id  && item1->uid == item2->uid;
}

/**
 * camel_store_search_thread_item_get_store:
 * @self: a #CamelStoreSearchThreadItem
 *
 * Gets the #CamelStore for the @self.
 *
 * Returns: (transfer none): the #CamelStore for the @self
 *
 * Since: 3.58
 **/
CamelStore *
camel_store_search_thread_item_get_store (const CamelStoreSearchThreadItem *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->store;
}

/**
 * camel_store_search_thread_item_get_folder_id:
 * @self: a #CamelStoreSearchThreadItem
 *
 * Gets the folder ID for the @self.
 *
 * Returns: the folder ID for the @self
 *
 * Since: 3.58
 **/
guint32
camel_store_search_thread_item_get_folder_id (const CamelStoreSearchThreadItem *self)
{
	g_return_val_if_fail (self != NULL, 0);
	return self->folder_id;
}

/**
 * camel_store_search_thread_item_get_uid:
 * @self: a #CamelStoreSearchThreadItem
 *
 * Gets the message UID for the @self.
 *
 * Returns: the message UID for the @self
 *
 * Since: 3.58
 **/
const gchar *
camel_store_search_thread_item_get_uid (const CamelStoreSearchThreadItem *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->uid;
}

/**
 * camel_store_search_thread_item_get_subject:
 * @self: a #CamelStoreSearchThreadItem
 *
 * Gets the message subject for the @self.
 *
 * Returns: the message subject for the @self
 *
 * Since: 3.58
 **/
const gchar *
camel_store_search_thread_item_get_subject (const CamelStoreSearchThreadItem *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->subject;
}

/**
 * camel_store_search_thread_item_get_message_id:
 * @self: a #CamelStoreSearchThreadItem
 *
 * Gets a hashed value of the Message-ID header for the @self.
 *
 * Returns: a hashed value of the Message-ID header for the @self
 *
 * Since: 3.58
 **/
guint64
camel_store_search_thread_item_get_message_id (const CamelStoreSearchThreadItem *self)
{
	g_return_val_if_fail (self != NULL, 0);
	return self->message_id;
}

/**
 * camel_store_search_thread_item_get_references:
 * @self: a #CamelStoreSearchThreadItem
 *
 * Gets the message In-Reply-To and References values for the @self,
 * hashed the same way the camel_store_search_thread_item_get_message_id() is.
 *
 * Returns: (nullable) (transfer none) (element-type guint64): the message In-Reply-To
 *    and References values for the @self, or %NULL, when none is set
 *
 * Since: 3.58
 **/
const GArray * /* guint64 */
camel_store_search_thread_item_get_references (const CamelStoreSearchThreadItem *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->references;
}

typedef struct _SearchIndexData {
	gpointer store; /* not referenced */
	guint32 folder_id;
	const gchar *uid; /* from the string pool */
} SearchIndexData;

static SearchIndexData *
search_index_data_new (CamelStore *store,
		       guint32 folder_id,
		       const gchar *uid)
{
	SearchIndexData *self;

	self = g_new0 (SearchIndexData, 1);
	self->store = store; /* no reference, compare pointer-wise only */
	self->folder_id = folder_id;
	self->uid = camel_pstring_strdup (uid);

	return self;
}

static void
search_index_data_free (gpointer ptr)
{
	SearchIndexData *self = ptr;

	if (self) {
		camel_pstring_free (self->uid);
		g_free (self);
	}
}

static guint
search_index_data_hash (gconstpointer ptr)
{
	const SearchIndexData *self = ptr;

	if (!self)
		return 0;

	return g_direct_hash (self->store) ^
		g_direct_hash (GUINT_TO_POINTER (self->folder_id)) ^
		g_direct_hash (self->uid); /* from the string pool, can compare pointer-wise */
}

static gboolean
search_index_data_equal (gconstpointer ptr1,
			 gconstpointer ptr2)
{
	const SearchIndexData *self1 = ptr1;
	const SearchIndexData *self2 = ptr2;

	if (self1 == self2)
		return TRUE;

	if (!self1 || !self2)
		return FALSE;

	return self1->store == self2->store &&
		self1->folder_id == self2->folder_id &&
		self1->uid == self2->uid; /* from the string pool, can compare pointer-wise */
}

G_DEFINE_BOXED_TYPE (CamelStoreSearchIndex, camel_store_search_index, camel_store_search_index_ref, camel_store_search_index_unref)

/**
 * camel_store_search_index_new:
 *
 * Creates a new #CamelStoreSearchIndex. Free it with camel_store_search_index_unref(),
 * when no longer needed.
 *
 * Returns: (transfer full): a new #CamelStoreSearchIndex
 *
 * Since: 3.58
 **/
CamelStoreSearchIndex *
camel_store_search_index_new (void)
{
	GHashTable *table;

	table = g_hash_table_new_full (search_index_data_hash, search_index_data_equal, search_index_data_free, NULL);

	return (CamelStoreSearchIndex *) table;
}

/**
 * camel_store_search_index_ref:
 * @self: a #CamelStoreSearchIndex
 *
 * Adds a reference on the @self. Call a pair camel_store_search_index_unref()
 * to remove the added reference.
 *
 * Returns: (transfer full): the @self with added reference
 *
 * Since: 3.58
 **/
CamelStoreSearchIndex *
camel_store_search_index_ref (CamelStoreSearchIndex *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	g_hash_table_ref ((GHashTable *) self);

	return self;
}

/**
 * camel_store_search_index_unref:
 * @self: (transfer full): a #CamelStoreSearchIndex
 *
 * Removes one reference on the @self. When the reference count
 * drops to zero, the @self is freed.
 *
 * Since: 3.58
 **/
void
camel_store_search_index_unref (CamelStoreSearchIndex *self)
{
	g_return_if_fail (self != NULL);

	g_hash_table_unref ((GHashTable *) self);
}

/**
 * camel_store_search_index_add:
 * @self: a #CamelStoreSearchIndex
 * @store: (not nullable): a #CamelStore
 * @folder_id: a folder ID, other than zero
 * @uid: (not nullable): a message UID
 *
 * Adds a message identified by the @store, @folder_id and @uid into the index @self.
 * It can be asked whether the @self contains the message with camel_store_search_index_contains().
 * Note the @store is not referenced, it's compared pointer-wise.
 * See camel_store_search_index_contains() for more information.
 *
 * Since: 3.58
 **/
void
camel_store_search_index_add (CamelStoreSearchIndex *self,
			      CamelStore *store,
			      guint32 folder_id,
			      const gchar *uid)
{
	SearchIndexData *index_data;

	g_return_if_fail (self != NULL);
	g_return_if_fail (store != NULL);
	g_return_if_fail (folder_id != 0);
	g_return_if_fail (uid != NULL);

	index_data = search_index_data_new (store, folder_id, uid);
	g_hash_table_add ((GHashTable *) self, index_data);
}

/**
 * camel_store_search_index_remove:
 * @self: a #CamelStoreSearchIndex
 * @store: (not nullable): a #CamelStore
 * @folder_id: a folder ID, other than zero
 * @uid: (not nullable): a message UID
 *
 * Removes a message identified by the @store, @folder_id and @uid from the index @self.
 * Note the @store is not referenced, it's compared pointer-wise.
 * See camel_store_search_index_add() for more information.
 *
 * Returns: whether the message existed and had been removed
 *
 * Since: 3.58
 **/
gboolean
camel_store_search_index_remove (CamelStoreSearchIndex *self,
				 CamelStore *store,
				 guint32 folder_id,
				 const gchar *uid)
{
	SearchIndexData index_data = { 0, };

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (store != NULL, FALSE);
	g_return_val_if_fail (folder_id != 0, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	index_data.store = store;
	index_data.folder_id = folder_id;
	index_data.uid = camel_pstring_peek (uid);

	if (!index_data.uid)
		return FALSE;

	return g_hash_table_remove ((GHashTable *) self, &index_data);
}

static gboolean
store_search_index_move_from_existing_cb (gpointer key,
					  gpointer value,
					  gpointer user_data)
{
	GHashTable *self = user_data;

	g_hash_table_add (self, key);

	return TRUE;
}

/**
 * camel_store_search_index_move_from_existing:
 * @self: a #CamelStoreSearchIndex
 * @src: a #CamelStoreSearchIndex, to move items from
 *
 * Moves all items from the @src index into the @self. The @self and
 * the @src can be the same, in which case the function does nothing.
 * At the end of the function the @src will contain no items, but it
 * is not freed.
 *
 * Since: 3.58
 **/
void
camel_store_search_index_move_from_existing (CamelStoreSearchIndex *self,
					     CamelStoreSearchIndex *src)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (src != NULL);

	if (self != src)
		g_hash_table_foreach_steal ((GHashTable *) src, store_search_index_move_from_existing_cb, self);
}

/**
 * camel_store_search_index_contains:
 * @self: a #CamelStoreSearchIndex
 * @store: (not nullable): a #CamelStore
 * @folder_id: a folder ID, other than zero
 * @uid: (not nullable): a message UID
 *
 * Checks whether a message identified by the @store, @folder_id and @uid is
 * included in the index @self.
 *
 * Note the @store is not referenced, it's compared pointer-wise, thus when
 * a different instance of the same service as the one used in
 * the camel_store_search_index_add() is used here, the message reference will
 * not be found.
 *
 * Returns: %TRUE when the message reference is included in the index @self
 *
 * Since: 3.58
 **/
gboolean
camel_store_search_index_contains (CamelStoreSearchIndex *self,
				   CamelStore *store,
				   guint32 folder_id,
				   const gchar *uid)
{
	SearchIndexData index_data = { 0, };

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (store != NULL, FALSE);
	g_return_val_if_fail (folder_id != 0, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	index_data.store = store;
	index_data.folder_id = folder_id;
	index_data.uid = camel_pstring_peek (uid);

	if (!index_data.uid)
		return FALSE;

	return g_hash_table_contains ((GHashTable *) self, &index_data);
}

static void
store_search_index_fill_threads_hash (CamelFolderThreadNode *root,
				      GHashTable *threads_hash)
{
	while (root) {
		CamelFolderThreadNode *child;

		g_hash_table_insert (threads_hash, camel_folder_thread_node_get_item (root), root);

		child = camel_folder_thread_node_get_child (root);
		if (child)
			store_search_index_fill_threads_hash (child, threads_hash);

		root = camel_folder_thread_node_get_next (root);
	}
}

static void
store_search_index_add_thread_results (CamelFolderThreadNode *node,
				       GHashTable *results_hash)
{
	while (node) {
		CamelFolderThreadNode *child;

		g_hash_table_add (results_hash, camel_folder_thread_node_get_item (node));

		child = camel_folder_thread_node_get_child (node);
		if (child)
			store_search_index_add_thread_results (child, results_hash);

		node = camel_folder_thread_node_get_next (node);
	}
}

static gboolean
store_search_index_remove_unmatch_threads_cb (gpointer key,
					      gpointer value,
					      gpointer user_data)
{
	SearchIndexData *index_data = key;
	GHashTable *results_hash = user_data; /* CamelStoreSearchThreadItem */
	CamelStoreSearchThreadItem item_stack = { 0, };

	item_stack.store = index_data->store;
	item_stack.folder_id = index_data->folder_id;
	item_stack.uid = index_data->uid;

	return !g_hash_table_contains (results_hash, &item_stack);
}

static void
store_search_index_add_match_threads_cb (gpointer key,
					 gpointer value,
					 gpointer user_data)
{
	CamelStoreSearchThreadItem *item = key;
	CamelStoreSearchIndex *self = user_data;
	SearchIndexData index_data_stack = { 0, };

	index_data_stack.store = camel_store_search_thread_item_get_store (item);
	index_data_stack.folder_id = camel_store_search_thread_item_get_folder_id (item);
	index_data_stack.uid = camel_store_search_thread_item_get_uid (item);

	if (!g_hash_table_contains ((GHashTable *) self, &index_data_stack))
		camel_store_search_index_add (self, index_data_stack.store, index_data_stack.folder_id, index_data_stack.uid);
}

/**
 * camel_store_search_index_apply_match_threads:
 * @self: a #CamelStoreSearchIndex
 * @items: (element-type CamelStoreSearchThreadItem): all items used for thread creation
 * @kind: one of #CamelMatchThreadsKind
 * @flags: a bit-or of #CamelFolderThreadFlags
 * @cancellable: a #GCancellable, or #NULL
 *
 * Constructs the @inout_threads from the @items according to the @flags and then
 * changes the content of the @self to contain only references to messages which
 * satisfy the @kind. When there are no @items or the @kind is %CAMEL_MATCH_THREADS_KIND_NONE,
 * the function does nothing.
 *
 * Since: 3.58
 **/
void
camel_store_search_index_apply_match_threads (CamelStoreSearchIndex *self,
					      /* const */ GPtrArray *items, /* CamelStoreSearchThreadItem * */
					      CamelMatchThreadsKind kind,
					      CamelFolderThreadFlags flags,
					      GCancellable *cancellable)
{
	CamelFolderThread *threads;
	GHashTable *threads_hash; /* CamelStoreSearchThreadItem ~> CamelFolderThreadNode */
	GHashTable *results_hash; /* CamelStoreSearchThreadItem */
	GHashTableIter iter;
	gpointer key = NULL;

	g_return_if_fail (self != NULL);
	g_return_if_fail (items != NULL);

	if (!items->len || kind == CAMEL_MATCH_THREADS_KIND_NONE)
		return;

	threads = camel_folder_thread_new_items (items, flags & (~CAMEL_FOLDER_THREAD_FLAG_SORT),
		(CamelFolderThreadStrFunc) camel_store_search_thread_item_get_uid,
		(CamelFolderThreadStrFunc) camel_store_search_thread_item_get_subject,
		(CamelFolderThreadUint64Func) camel_store_search_thread_item_get_message_id,
		(CamelFolderThreadArrayFunc) camel_store_search_thread_item_get_references,
		NULL, NULL, NULL, NULL);
	g_return_if_fail (threads != NULL);

	threads_hash = g_hash_table_new (camel_store_search_thread_item_hash, camel_store_search_thread_item_equal);
	store_search_index_fill_threads_hash (camel_folder_thread_get_tree (threads), threads_hash);

	results_hash = g_hash_table_new (camel_store_search_thread_item_hash, camel_store_search_thread_item_equal);

	g_hash_table_iter_init (&iter, (GHashTable *) self);

	while (!g_cancellable_is_cancelled (cancellable) && g_hash_table_iter_next (&iter, &key, NULL)) {
		SearchIndexData *index_data = key;
		CamelFolderThreadNode *node;
		CamelStoreSearchThreadItem item_stack = { 0, }, *item;

		if (!index_data)
			continue;

		item_stack.store = index_data->store;
		item_stack.folder_id = index_data->folder_id;
		item_stack.uid = index_data->uid;

		node = g_hash_table_lookup (threads_hash, &item_stack);
		if (!node)
			continue;

		item = camel_folder_thread_node_get_item (node);

		if (kind != CAMEL_MATCH_THREADS_KIND_SINGLE)
			g_hash_table_add (results_hash, item);

		/* select messages in thread according to search criteria */
		if (kind == CAMEL_MATCH_THREADS_KIND_SINGLE) {
			if (!camel_folder_thread_node_get_child (node) && !camel_folder_thread_node_get_parent (node))
				g_hash_table_add (results_hash, item);
		} else {
			CamelFolderThreadNode *child;

			if (kind == CAMEL_MATCH_THREADS_KIND_REPLIES_AND_PARENTS) {
				CamelFolderThreadNode *scan, *parent;

				scan = node;
				/* coverity[check_after_deref] */
				while (scan && (parent = camel_folder_thread_node_get_parent (scan)) != NULL) {
					scan = parent;
					g_hash_table_add (results_hash, camel_folder_thread_node_get_item (scan));
				}
			} else if (kind == CAMEL_MATCH_THREADS_KIND_ALL) {
				CamelFolderThreadNode *parent;

				while (node && (parent = camel_folder_thread_node_get_parent (node)) != NULL) {
					node = parent;
				}
			}

			g_hash_table_add (results_hash, camel_folder_thread_node_get_item (node));

			child = camel_folder_thread_node_get_child (node);
			if (child)
				store_search_index_add_thread_results (child, results_hash);
		}
	}

	g_hash_table_foreach_remove ((GHashTable *) self, store_search_index_remove_unmatch_threads_cb, results_hash);
	g_hash_table_foreach (results_hash, store_search_index_add_match_threads_cb, self);

	g_hash_table_destroy (results_hash);
	g_hash_table_destroy (threads_hash);
	g_clear_object (&threads);
}

/* helper data/object to cover remote search cache */

typedef struct _SearchCacheKey {
	guint32 folder_id;
	const gchar *uid; /* from the string pool */
} SearchCacheKey;

static SearchCacheKey *
search_cache_key_new (guint32 folder_id,
		      const gchar *uid)
{
	SearchCacheKey *key;

	key = g_new0 (SearchCacheKey, 1);
	key->folder_id = folder_id;
	key->uid = camel_pstring_strdup (uid);

	return key;
}

static void
search_cache_key_free (gpointer ptr)
{
	SearchCacheKey *key = ptr;

	if (key) {
		camel_pstring_free (key->uid);
		g_free (key);
	}
}

static guint
search_cache_key_hash (gconstpointer ptr)
{
	const SearchCacheKey *key = ptr;

	if (!key)
		return 0;

	/* as long as the UID-s come from the string pool, they can be compared pointer-wise */
	return g_direct_hash (GUINT_TO_POINTER (key->folder_id)) ^ g_direct_hash (key->uid);
}

static gboolean
search_cache_key_equal (gconstpointer ptr1,
			gconstpointer ptr2)
{
	const SearchCacheKey *key1 = ptr1;
	const SearchCacheKey *key2 = ptr2;

	if (!key1 || !key2 || key1 == key2)
		return key1 == key2;

	/* as long as the UID-s come from the string pool, they can be compared pointer-wise */
	return key1->folder_id == key2->folder_id && key1->uid == key2->uid;
}

#define SEARCH_TYPE_CACHE (search_cache_get_type ())
G_DECLARE_FINAL_TYPE (SearchCache, search_cache, SEARCH, CACHE, GObject)

struct _SearchCache {
	GObject parent_instance;

	GHashTable *cache; /* gchar *search_token ~> SearchCacheData * */
};

G_DEFINE_TYPE (SearchCache, search_cache, G_TYPE_OBJECT)

typedef struct _SearchCacheData {
	GHashTable *matches; /* SearchCacheKey ~> 0 */
	GHashTable *failures; /* guint32 folder_id ~> 0; folder ID-s which failed to get the result */
} SearchCacheData;

static SearchCacheData *
search_cache_data_new (void)
{
	SearchCacheData *self;

	self = g_new0 (SearchCacheData, 1);
	self->matches = g_hash_table_new_full (search_cache_key_hash, search_cache_key_equal, search_cache_key_free, NULL);

	return self;
}

static void
search_cache_data_free (gpointer ptr)
{
	SearchCacheData *self = ptr;

	if (self) {
		g_clear_pointer (&self->matches, g_hash_table_unref);
		g_clear_pointer (&self->failures, g_hash_table_unref);
		g_free (self);
	}
}

static void
search_cache_data_set_folder_failed (SearchCacheData *self,
				     guint32 folder_id)
{
	if (!self->failures)
		self->failures = g_hash_table_new (g_direct_hash, g_direct_equal);
	g_hash_table_add (self->failures, GUINT_TO_POINTER (folder_id));
}

static gboolean
search_cache_data_get_folder_failed (SearchCacheData *self,
				     guint32 folder_id)
{
	return self->failures && g_hash_table_contains (self->failures, GUINT_TO_POINTER (folder_id));
}

static void
search_cache_data_add_matches (SearchCacheData *self,
			       guint32 folder_id,
			       /* const */ GPtrArray *uids) /* const gchar * */
{
	guint ii;

	if (!uids)
		return;

	for (ii = 0; ii < uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (uids, ii);

		if (uid && *uid) {
			SearchCacheKey *key;

			key = search_cache_key_new (folder_id, uid);

			g_hash_table_add (self->matches, key);
		}
	}
}

static gboolean
search_cache_data_has_match (SearchCacheData *self,
			     guint32 folder_id,
			     const gchar *uid)
{
	SearchCacheKey key;
	gboolean has_match;

	if (!folder_id || !uid || !*uid)
		return FALSE;

	/* requires to have the uid from the string pool, to have the hash/equal functions of the 'key' struct quick and working */
	key.folder_id = folder_id;
	key.uid = camel_pstring_peek (uid);

	if (!key.uid)
		return FALSE;

	has_match = g_hash_table_contains (self->matches, &key);

	return has_match;
}

static void
search_cache_finalize (GObject *object)
{
	SearchCache *self = SEARCH_CACHE (object);

	g_clear_pointer (&self->cache, g_hash_table_unref);

	G_OBJECT_CLASS (search_cache_parent_class)->finalize (object);
}

static void
search_cache_class_init (SearchCacheClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = search_cache_finalize;
}

static void
search_cache_init (SearchCache *self)
{
	self->cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, search_cache_data_free);
}

static SearchCache *
search_cache_new (void)
{
	return g_object_new (SEARCH_TYPE_CACHE, NULL);
}

static void
search_cache_clear (SearchCache *self)
{
	g_hash_table_remove_all (self->cache);
}

static void
search_cache_add_token_result (SearchCache *self,
			       const gchar *token,
			       guint32 folder_id,
			       /* const */ GPtrArray *uids, /* gchar * */
			       gboolean is_failure)
{
	SearchCacheData *data;

	data = g_hash_table_lookup (self->cache, token);
	if (!data) {
		data = search_cache_data_new ();
		g_hash_table_insert (self->cache, g_strdup (token), data);
	}

	if (is_failure)
		search_cache_data_set_folder_failed (data, folder_id);
	else
		search_cache_data_add_matches (data, folder_id, uids);
}

static gboolean
search_cache_has_token_result (SearchCache *self,
			       const gchar *token)
{
	return g_hash_table_contains (self->cache, token);
}

static gboolean
search_cache_get_token_result_failed (SearchCache *self,
				      const gchar *token,
				      guint32 folder_id)
{
	SearchCacheData *data;

	data = g_hash_table_lookup (self->cache, token);

	return !data || search_cache_data_get_folder_failed (data, folder_id);
}

static gboolean
search_cache_get_token_result_matches (SearchCache *self,
				       const gchar *token,
				       guint32 folder_id,
				       const gchar *uid)
{
	SearchCacheData *data;

	data = g_hash_table_lookup (self->cache, token);
	if (!data)
		return FALSE;

	return search_cache_data_has_match (data, folder_id, uid);
}

static GPtrArray * /* gchar * */
store_search_decode_words (const gchar *encoded_words)
{
	GPtrArray *words;
	gchar **strv;
	guint ii;

	words = g_ptr_array_new_with_free_func (g_free);

	strv = g_strsplit (encoded_words, " ", -1);
	for (ii = 0; strv && strv[ii]; ii++) {
		gchar *word;

		word = g_uri_unescape_string (strv[ii], NULL);
		if (word)
			g_ptr_array_add (words, word);
	}

	g_strfreev (strv);

	return words;
}

struct _CamelStoreSearchPrivate {
	CamelStore *store; /* referenced */
	CamelStoreDB *store_db; /* not referenced */
	CamelSExp *sexp;
	gchar *expression;
	GPtrArray *additional_columns; /* gchar *colname */
	gchar *additional_columns_stmt;
	GHashTable *folders; /* gchar *full_name ~> CamelFolder */
	GHashTable *folders_by_id; /* GUINT_TO_POINTER(guint32 folder_id) ~> CamelFolder */
	CamelStoreSearchIndex *result_index;
	GHashTable *match_indexes; /* gchar *id~>CamelStoreSearchIndex * */
	gboolean needs_rebuild;
	CamelMatchThreadsKind match_threads_kind;
	CamelFolderThreadFlags match_threads_flags;

	gchar *where_clause_sql;

	/* data related to an ongoing search */
	struct _ongoing_search {
		guint folder_id;
		CamelFolder *folder;
		CamelFolderSummary *folder_summary;
		const gchar *uid; /* camel's string pool */
		CamelNameValueArray *headers;
		CamelMessageInfo *info;
		CamelMimeMessage *message;
		GCancellable *cancellable;
		GError **error;
		SearchCache *search_body;
		gboolean success;

		GHashTable *search_ops_pool; /* SearchOp * ~> SearchOps; only a pool, to save memory */
		GHashTable *todo_search_ops_by_uid; /* gchar *uid ~> GPtrArray { SearchOp * } */
		GHashTable *todo_addr_book;   /* AddrBookOp * ~> NULL */

		GHashTable *done_search_ops; /* SearchOp * ~> SearchOp * */
		GHashTable *done_addr_book;   /* AddrBookOp * ~> GINT_TO_POINTER (found | not) */
	} ongoing_search;
};

enum {
	PROP_0,
	PROP_STORE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (CamelStoreSearch, camel_store_search, G_TYPE_OBJECT)

struct _SearchOp;
typedef gboolean (* SearchOpRunSyncFunc)(CamelStoreSearch *self,
					 const struct _SearchOp *op,
					 const gchar *uid,
					 gboolean *out_matches,
					 GCancellable *cancellable,
					 GError **error);

/* some checks require remote data, like downloading the whole message or headers,
   or consulting the server-side search for some information. These cannot run
   during the SQLite SELECT statement execution, because they can block for a long
   time, thus remember them and re-run the search with gathered data */
typedef struct _SearchOp {
	gint cmp_kind;
	gchar *needle;
	gchar *header_name; /* NULL when is body search; empty string for all headers */

	SearchOpRunSyncFunc run_sync;

	GPtrArray *words; /* for body search, split encoded words */
	GHashTable *results; /* gchar *uid ~> GINT_TO_POINTER (matches) */
} SearchOp;

static SearchOp *
search_op_new (gint cmp_kind,
	       const gchar *needle,
	       const gchar *header_name)
{
	SearchOp *op;

	op = g_new0 (SearchOp, 1);
	op->cmp_kind = cmp_kind;
	op->needle = g_strdup (needle);
	op->header_name = g_strdup (header_name);

	op->results = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) camel_pstring_free, NULL);

	if (!header_name && needle)
		op->words = store_search_decode_words (needle);

	return op;
}

static void
search_op_free (gpointer ptr)
{
	SearchOp *op = ptr;

	if (op) {
		g_free (op->needle);
		g_free (op->header_name);
		g_clear_pointer (&op->results, g_hash_table_unref);
		g_clear_pointer (&op->words, g_ptr_array_unref);
		g_free (op);
	}
}

static guint
search_op_hash (gconstpointer ptr)
{
	const SearchOp *op = ptr;
	guint hash_val;

	hash_val = g_direct_hash (GINT_TO_POINTER (op->cmp_kind));

	if (op->needle)
		hash_val = hash_val ^ g_str_hash (op->needle);

	if (op->header_name)
		hash_val = hash_val ^ g_str_hash (op->header_name);

	return hash_val;
}

static gboolean
search_op_equal (gconstpointer ptr1,
		 gconstpointer ptr2)
{
	const SearchOp *op1 = ptr1;
	const SearchOp *op2 = ptr2;

	return op1->cmp_kind == op2->cmp_kind &&
		g_strcmp0 (op1->needle, op2->needle) == 0 &&
		g_strcmp0 (op1->header_name, op2->header_name) == 0;
}

static void
search_op_run_sync (SearchOp *op,
		    CamelStoreSearch *self,
		    const gchar *uid)
{
	gboolean matches = FALSE;

	if (!self->priv->ongoing_search.success)
		return;

	self->priv->ongoing_search.success = op->run_sync (self, op, uid, &matches, self->priv->ongoing_search.cancellable, self->priv->ongoing_search.error);

	if (!self->priv->ongoing_search.success)
		return;

	g_hash_table_insert (op->results, (gpointer) camel_pstring_strdup (uid), GINT_TO_POINTER (matches ? 1 : 0));

	if (!self->priv->ongoing_search.done_search_ops)
		self->priv->ongoing_search.done_search_ops = g_hash_table_new (search_op_hash, search_op_equal);

	g_hash_table_add (self->priv->ongoing_search.done_search_ops, op);
}

typedef struct _AddrBookOpKey {
	gchar *book_uid;
	gchar *email;
} AddrBookOpKey;

static AddrBookOpKey *
addr_book_op_key_new (const gchar *book_uid,
		      const gchar *email)
{
	AddrBookOpKey *op;

	op = g_new0 (AddrBookOpKey, 1);
	op->book_uid = g_strdup (book_uid);
	op->email = g_strdup (email);

	return op;
}

static void
addr_book_op_key_free (gpointer ptr)
{
	AddrBookOpKey *op = ptr;

	if (op) {
		g_free (op->book_uid);
		g_free (op->email);
		g_free (op);
	}
}

static guint
addr_book_op_key_hash (gconstpointer ptr)
{
	const AddrBookOpKey *op = ptr;

	return g_str_hash (op->book_uid) ^ g_str_hash (op->email);
}

static gboolean
addr_book_op_key_equal (gconstpointer ptr1,
			gconstpointer ptr2)
{
	const AddrBookOpKey *op1 = ptr1;
	const AddrBookOpKey *op2 = ptr2;

	return g_strcmp0 (op1->book_uid, op2->book_uid) == 0 && g_strcmp0 (op1->email, op2->email) == 0;
}

static void
camel_store_search_clear_ongoing_search_data (CamelStoreSearch *self)
{
	search_cache_clear (self->priv->ongoing_search.search_body);

	self->priv->ongoing_search.folder_id = 0;
	self->priv->ongoing_search.folder = NULL;
	self->priv->ongoing_search.folder_summary = NULL;
	g_clear_pointer (&self->priv->ongoing_search.uid, camel_pstring_free);
	g_clear_pointer (&self->priv->ongoing_search.headers, camel_name_value_array_free);
	g_clear_object (&self->priv->ongoing_search.info);
	g_clear_object (&self->priv->ongoing_search.message);
	self->priv->ongoing_search.cancellable = NULL;
	self->priv->ongoing_search.error = NULL;
	self->priv->ongoing_search.success = TRUE;
	g_clear_pointer (&self->priv->ongoing_search.todo_search_ops_by_uid, g_hash_table_unref);
	g_clear_pointer (&self->priv->ongoing_search.todo_addr_book, g_hash_table_unref);
	g_clear_pointer (&self->priv->ongoing_search.done_search_ops, g_hash_table_unref);
	g_clear_pointer (&self->priv->ongoing_search.done_addr_book, g_hash_table_unref);
	g_clear_pointer (&self->priv->ongoing_search.search_ops_pool, g_hash_table_unref);
}

typedef enum _ResultFlags {
	RESULT_IS_SQL = 1 << 0,
	RESULT_NEEDS_CUSTOM_FUNCTION = 1 << 1,
	RESULT_NEEDS_HEADERS = 1 << 2,
	RESULT_NEEDS_MSG_BODY = 1 << 3,
	RESULT_NEEDS_CONTACTS = 1 << 4
} ResultFlags;

static CamelSExpResult *
store_search_create_string_result (CamelSExp *sexp)
{
	CamelSExpResult *res;

	res = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_STRING);
	res->user_data = GUINT_TO_POINTER (RESULT_IS_SQL);

	return res;
}

static ResultFlags
store_search_result_get_flags (const CamelSExpResult *res)
{
	g_return_val_if_fail (res != NULL, 0);

	return GPOINTER_TO_UINT (res->user_data);
}

static void
store_search_result_add_flags (CamelSExpResult *res,
			       ResultFlags flags)
{
	g_return_if_fail (res != NULL);

	res->user_data = GUINT_TO_POINTER (store_search_result_get_flags (res) | flags);
}

static gboolean
store_search_result_is (const CamelSExpResult *res,
			ResultFlags flag)
{
	return res && (store_search_result_get_flags (res) & flag) != 0;
}

static void
store_search_res_to_statement (GString *stmt,
			       CamelSExpResult *res)
{
	if (res->type == CAMEL_SEXP_RES_INT)
		g_string_append_printf (stmt, "%d", res->value.number);
	else if (res->type == CAMEL_SEXP_RES_BOOL)
		g_string_append_printf (stmt, "%d", res->value.boolean);
	else if (res->type == CAMEL_SEXP_RES_TIME)
		g_string_append_printf (stmt, "%" G_GINT64_FORMAT, (gint64) res->value.time);
	else if (res->type == CAMEL_SEXP_RES_STRING && store_search_result_is (res, RESULT_IS_SQL))
		g_string_append_printf (stmt, "%s", res->value.string ? res->value.string : "");
	else if (res->type == CAMEL_SEXP_RES_STRING)
		camel_db_sqlize_to_statement (stmt, res->value.string ? res->value.string : "", CAMEL_DB_SQLIZE_FLAG_FULL);
	else
		g_warn_if_reached ();
}

static gint
store_search_cmp_result_flags (gconstpointer ptr1,
			       gconstpointer ptr2)
{
	const CamelSExpResult *res1 = *((const CamelSExpResult **) ptr1);
	const CamelSExpResult *res2 = *((const CamelSExpResult **) ptr2);

	/* more expensive to the end */
	return GPOINTER_TO_UINT (res1->user_data) - GPOINTER_TO_UINT (res2->user_data);
}

static CamelSExpResult *
store_search_and_or (CamelSExp *sexp,
		     gint argc,
		     CamelSExpTerm **argv,
		     gboolean is_and_op)
{
	CamelSExpResult *res = store_search_create_string_result (sexp);
	CamelSExpResult *res1;
	ResultFlags max_flags = store_search_result_get_flags (res);
	GPtrArray *array; /* CamelSExpResult * */
	GString *stmt = NULL;
	gboolean has_bool_true = FALSE, has_bool_false = FALSE;
	gint ii;

	array = g_ptr_array_sized_new (argc);

	for (ii = 0; ii < argc; ii++) {
		res1 = camel_sexp_term_eval (sexp, argv[ii]);

		if (res1) {
			g_ptr_array_add (array, res1);
			max_flags |= store_search_result_get_flags (res1);

			if (res1->type == CAMEL_SEXP_RES_BOOL) {
				has_bool_true = has_bool_true || res1->value.boolean;
				has_bool_false = has_bool_false || !res1->value.boolean;
			}
		}
	}

	if (is_and_op && has_bool_false)
		res->value.string = g_strdup ("0");
	else if (!is_and_op && has_bool_true)
		res->value.string = g_strdup ("1");

	if (res->value.string) {
		for (ii = 0; ii < (gint) array->len; ii++) {
			res1 = g_ptr_array_index (array, ii);
			camel_sexp_result_free (sexp, res1);
		}

		g_ptr_array_unref (array);
		return res;
	}

	g_ptr_array_sort (array, store_search_cmp_result_flags);

	for (ii = 0; ii < (gint) array->len; ii++) {
		res1 = g_ptr_array_index (array, ii);

		if (res1->type == CAMEL_SEXP_RES_STRING &&
		    res1->value.string && *res1->value.string) {
			if (!stmt) {
				/* this allows short-circuit evaluation */
				stmt = g_string_new ("CASE WHEN ");
			} else {
				g_string_append (stmt, " WHEN ");
			}

			if (is_and_op)
				g_string_append (stmt, "NOT (");
			store_search_res_to_statement (stmt, res1);
			g_string_append_printf (stmt, "%s THEN %d", is_and_op ? ")" : "", is_and_op ? 0 : 1);
		}

		camel_sexp_result_free (sexp, res1);
	}

	g_ptr_array_free (array, TRUE);

	store_search_result_add_flags (res, max_flags);

	if (stmt) {
		if (stmt->len == 1)
			g_string_set_size (stmt, 0);
		else
			g_string_append_printf (stmt, " ELSE %d END", is_and_op ? 1 : 0);

		res->value.string = g_string_free (stmt, FALSE);
	} else {
		res->value.string = g_strdup ("");
	}

	return res;
}

static CamelSExpResult *
store_search_and_cb (CamelSExp *sexp,
		     gint argc,
		     CamelSExpTerm **argv,
		     gpointer user_data)
{
	return store_search_and_or (sexp, argc, argv, TRUE);
}

static CamelSExpResult *
store_search_or_cb (CamelSExp *sexp,
		     gint argc,
		     CamelSExpTerm **argv,
		     gpointer user_data)
{
	return store_search_and_or (sexp, argc, argv, FALSE);
}

static CamelSExpResult *
store_search_not_cb (CamelSExp *sexp,
		     gint argc,
		     CamelSExpTerm **argv,
		     gpointer user_data)
{
	CamelSExpResult *res = store_search_create_string_result (sexp);
	CamelSExpResult *res1;

	g_return_val_if_fail (argc == 1, res);

	res1 = camel_sexp_term_eval (sexp, argv[0]);

	if (res1->type == CAMEL_SEXP_RES_STRING &&
	    res1->value.string && *res1->value.string) {
		GString *stmt;

		stmt = g_string_new ("(NOT (");
		store_search_res_to_statement (stmt, res1);
		g_string_append (stmt, "))");

		res->value.string = g_string_free (stmt, FALSE);

		store_search_result_add_flags (res, store_search_result_get_flags (res1));
	}

	camel_sexp_result_free (sexp, res1);

	return res;
}

static CamelSExpResult *
store_search_lt_gt (CamelSExp *sexp,
		    gint argc,
		    CamelSExpTerm **argv,
		    const gchar *op)
{
	CamelSExpResult *res = store_search_create_string_result (sexp);
	CamelSExpResult *res1, *res2;
	GString *stmt;

	g_return_val_if_fail (argc == 2, res);

	res1 = camel_sexp_term_eval (sexp, argv[0]);
	res2 = camel_sexp_term_eval (sexp, argv[1]);

	stmt = g_string_new ("(");
	store_search_res_to_statement (stmt, res1);
	g_string_append (stmt, op);
	store_search_res_to_statement (stmt, res2);
	g_string_append_c (stmt, ')');

	res->value.string = g_string_free (stmt, FALSE);
	store_search_result_add_flags (res, store_search_result_get_flags (res1) | store_search_result_get_flags (res2));

	camel_sexp_result_free (sexp, res1);
	camel_sexp_result_free (sexp, res2);

	return res;
}

static CamelSExpResult *
store_search_lt_cb (CamelSExp *sexp,
		    gint argc,
		    CamelSExpTerm **argv,
		    gpointer user_data)
{
	return store_search_lt_gt (sexp, argc, argv, "<");
}

static CamelSExpResult *
store_search_gt_cb (CamelSExp *sexp,
		    gint argc,
		    CamelSExpTerm **argv,
		    gpointer user_data)
{
	return store_search_lt_gt (sexp, argc, argv, ">");
}

static CamelSExpResult *
store_search_eq_cb (CamelSExp *sexp,
		    gint argc,
		    CamelSExpTerm **argv,
		    gpointer user_data)
{
	CamelStoreSearch *self = user_data;
	CamelSExpResult *res = store_search_create_string_result (sexp);
	CamelSExpResult *res1, *res2;
	GString *stmt;

	g_return_val_if_fail (argc == 2, res);

	res1 = camel_sexp_term_eval (sexp, argv[0]);
	res2 = camel_sexp_term_eval (sexp, argv[1]);

	if (res1->type == CAMEL_SEXP_RES_STRING || res2->type == CAMEL_SEXP_RES_STRING) {
		stmt = g_string_new ("camelcmptext(");
		g_string_append_printf (stmt, "'%p',uid,'',%d,", self, CMP_HEADER_MATCHES);
		if (res1->type == CAMEL_SEXP_RES_STRING) {
			store_search_res_to_statement (stmt, res1);
		} else {
			g_string_append_c (stmt, '\'');
			store_search_res_to_statement (stmt, res1);
			g_string_append_c (stmt, '\'');
		}

		g_string_append_c (stmt, ',');
		if (res2->type == CAMEL_SEXP_RES_STRING) {
			store_search_res_to_statement (stmt, res2);
		} else {
			g_string_append_c (stmt, '\'');
			store_search_res_to_statement (stmt, res2);
			g_string_append_c (stmt, '\'');
		}
		g_string_append_c (stmt, ')');
		store_search_result_add_flags (res, RESULT_NEEDS_CUSTOM_FUNCTION);
	} else {
		stmt = g_string_new ("(");
		store_search_res_to_statement (stmt, res1);
		g_string_append_c (stmt, '=');
		store_search_res_to_statement (stmt, res2);
		g_string_append_c (stmt, ')');
	}

	store_search_result_add_flags (res, store_search_result_get_flags (res1) | store_search_result_get_flags (res2));
	res->value.string = g_string_free (stmt, FALSE);

	camel_sexp_result_free (sexp, res1);
	camel_sexp_result_free (sexp, res2);

	return res;
}

static CamelSExpResult *
store_search_match_all_cb (CamelSExp *sexp,
			   gint argc,
			   CamelSExpTerm **argv,
			   gpointer user_data)
{
	g_return_val_if_fail (argc == 1, NULL);

	return camel_sexp_term_eval (sexp, argv[0]);
}

static CamelSExpResult *
store_search_match_threads_cb (CamelSExp *sexp,
			       gint argc,
			       CamelSExpTerm **argv,
			       gpointer user_data)
{
	CamelStoreSearch *self = user_data;
	CamelSExpResult *res1, *res2;

	g_return_val_if_fail (argc == 2, store_search_create_string_result (sexp));

	res1 = camel_sexp_term_eval (sexp, argv[0]);
	res2 = camel_sexp_term_eval (sexp, argv[1]);

	if (res1->type == CAMEL_SEXP_RES_STRING) {
		const gchar *str = res1->value.string;

		/* the default is to thread by subject */
		self->priv->match_threads_flags = CAMEL_FOLDER_THREAD_FLAG_SUBJECT;
		self->priv->match_threads_kind = CAMEL_MATCH_THREADS_KIND_NONE;

		if (str) {
			if (g_str_has_prefix (str, "no-subject,")) {
				self->priv->match_threads_flags = self->priv->match_threads_flags & (~CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
				str += strlen ("no-subject,");
			}

			if (g_strcmp0 (str, "all") == 0)
				self->priv->match_threads_kind = CAMEL_MATCH_THREADS_KIND_ALL;
			else if (g_strcmp0 (str, "replies") == 0)
				self->priv->match_threads_kind = CAMEL_MATCH_THREADS_KIND_REPLIES;
			else if (g_strcmp0 (str, "replies_parents") == 0)
				self->priv->match_threads_kind = CAMEL_MATCH_THREADS_KIND_REPLIES_AND_PARENTS;
			else if (g_strcmp0 (str, "single") == 0)
				self->priv->match_threads_kind = CAMEL_MATCH_THREADS_KIND_SINGLE;
			else
				self->priv->match_threads_kind = CAMEL_MATCH_THREADS_KIND_NONE;
		}
	} else {
		g_warn_if_reached ();
	}

	camel_sexp_result_free (sexp, res1);

	return res2;
}

static CamelSExpResult *
store_search_compare_date_cb (CamelSExp *sexp,
			      gint argc,
			      CamelSExpTerm **argv,
			      gpointer user_data)
{
	CamelSExpResult *res = store_search_create_string_result (sexp);
	CamelSExpResult *res1, *res2;
	GString *stmt;

	g_return_val_if_fail (argc == 2, res);

	stmt = g_string_new ("camelcomparedate(");

	res1 = camel_sexp_term_eval (sexp, argv[0]);
	res2 = camel_sexp_term_eval (sexp, argv[1]);

	store_search_res_to_statement (stmt, res1);
	g_string_append_c (stmt, ',');
	store_search_res_to_statement (stmt, res2);
	g_string_append_c (stmt, ')');

	store_search_result_add_flags (res, store_search_result_get_flags (res1) | store_search_result_get_flags (res2));
	store_search_result_add_flags (res, RESULT_NEEDS_CUSTOM_FUNCTION);
	camel_sexp_result_free (sexp, res1);
	camel_sexp_result_free (sexp, res2);

	res->value.string = g_string_free (stmt, FALSE);

	return res;
}

static CamelSExpResult *
store_search_body_contains (CamelSExp *sexp,
			    gint argc,
			    CamelSExpResult **argv,
			    CamelStoreSearch *self,
			    CmpBodyKind cmp_kind)
{
	CamelSExpResult *res = store_search_create_string_result (sexp);
	GString *stmt = NULL;
	ResultFlags max_flags = 0;
	gint ii;

	for (ii = 0; ii < argc; ii++) {
		CamelSExpResult *res1 = argv[ii];

		if (res1->type == CAMEL_SEXP_RES_STRING && res1->value.string && *res1->value.string) {
			gchar *escaped;

			if (!stmt) {
				stmt = g_string_new ("camelsearchbody(");
				g_string_append_printf (stmt, "'%p',uid,%d,'", self, (gint) cmp_kind);
			} else {
				g_string_append_c (stmt, ' ');
			}

			max_flags |= store_search_result_get_flags (res1);

			escaped = g_uri_escape_string (res1->value.string, NULL, FALSE);

			if (escaped) {
				g_string_append (stmt, escaped);

				g_free (escaped);
			}
		}
	}

	if (stmt) {
		g_string_append (stmt, "')");
		store_search_result_add_flags (res, RESULT_NEEDS_MSG_BODY | max_flags);
		res->value.string = g_string_free (stmt, FALSE);
	}

	return res;
}

static CamelSExpResult *
store_search_body_contains_cb (CamelSExp *sexp,
			       gint argc,
			       CamelSExpResult **argv,
			       gpointer user_data)
{
	return store_search_body_contains (sexp, argc, argv, user_data, CMP_BODY_TEXT);
}

static CamelSExpResult *
store_search_body_regex_cb (CamelSExp *sexp,
			    gint argc,
			    CamelSExpResult **argv,
			    gpointer user_data)
{
	return store_search_body_contains (sexp, argc, argv, user_data, CMP_BODY_REGEX);
}

static CamelSExpResult *
store_search_header (CamelSExp *sexp,
		     gint argc,
		     CamelSExpResult **argv,
		     CamelStoreSearch *self,
		     CmpHeaderKind cmp_kind)
{
	CamelSExpResult *res = store_search_create_string_result (sexp);
	GString *stmt;
	const gchar *header_name, *column_name;
	ResultFlags max_flags = 0;
	gint ii;
	gboolean is_message_id_match;
	gboolean is_x_camel_msgid_match;

	if (cmp_kind == CMP_HEADER_EXISTS || cmp_kind == CMP_HEADER_FULL_REGEX)
		g_return_val_if_fail (argc == 1, res);
	else
		g_return_val_if_fail (argc > 1, res);

	if (argv[0]->type != CAMEL_SEXP_RES_STRING)
		return res;

	header_name = argv[0]->value.string;
	column_name = camel_store_db_util_get_column_for_header_name (argv[0]->value.string);

	if (cmp_kind == CMP_HEADER_EXISTS) {
		stmt = g_string_new ("");

		if (column_name) {
			g_string_append (stmt, column_name);
			g_string_append (stmt, " IS NOT NULL");
		} else {
			g_string_append (stmt, "camelsearchheader(");
			g_string_append_printf (stmt, "'%p',uid,", self);
			camel_db_sqlize_to_statement (stmt, header_name, CAMEL_DB_SQLIZE_FLAG_FULL);
			g_string_append_printf (stmt, ",%d,'',NULL)", (gint) cmp_kind);

			store_search_result_add_flags (res, RESULT_NEEDS_HEADERS);
		}

		store_search_result_add_flags (res, store_search_result_get_flags (argv[0]));
		res->value.string = g_string_free (stmt, !stmt->len);

		return res;
	} else if (cmp_kind == CMP_HEADER_FULL_REGEX) {
		stmt = g_string_new ("");

		g_string_append (stmt, "camelsearchheader(");
		/* empty header name to get all headers */
		g_string_append_printf (stmt, "'%p',uid,'',%d,", self, (gint) cmp_kind);
		/* then the actual value is in the `header_name` */
		camel_db_sqlize_to_statement (stmt, header_name, CAMEL_DB_SQLIZE_FLAG_FULL);
		if (column_name)
			g_string_append_printf (stmt, ",camelfromloadedinfoordb('%p',uid,'%s',%s))", self, column_name, column_name);
		else
			g_string_append (stmt, ",NULL)");

		store_search_result_add_flags (res, RESULT_NEEDS_HEADERS);
		res->value.string = g_string_free (stmt, !stmt->len);

		return res;
	}

	/* the difference is that the later is already hashed message id */
	is_message_id_match = cmp_kind == CMP_HEADER_MATCHES && header_name && g_ascii_strcasecmp (header_name, "Message-ID") == 0;
	is_x_camel_msgid_match = cmp_kind == CMP_HEADER_MATCHES && header_name && g_ascii_strcasecmp (header_name, "x-camel-msgid") == 0;

	stmt = g_string_new ("");

	for (ii = 1; ii < argc; ii++) {
		const gchar *value;

		if (argv[ii]->type != CAMEL_SEXP_RES_STRING)
			continue;

		value = argv[ii]->value.string;
		if (!value || !*value)
			continue;

		max_flags |= store_search_result_get_flags (argv[ii]);

		if (stmt->len)
			g_string_append (stmt, " WHEN ");
		else
			g_string_append (stmt, "CASE WHEN ");

		if (is_message_id_match) {
			CamelSummaryMessageID message_id;

			message_id.id.id = camel_search_util_hash_message_id (value, TRUE);

			g_string_append_printf (stmt, "(part IS NOT NULL AND part LIKE '%lu %lu %%') THEN 1",
				(gulong) message_id.id.part.hi, (gulong) message_id.id.part.lo);
		} else if (is_x_camel_msgid_match) {
			g_string_append_printf (stmt, "(part IS NOT NULL AND part LIKE '%s %%') THEN 1", value);
		} else if (column_name) {
			gboolean is_email_column = g_str_equal (column_name, "mail_from") ||
				g_str_equal (column_name, "mail_to") ||
				g_str_equal (column_name, "mail_cc") ||
				g_str_equal (column_name, "mlist");

			if (cmp_kind == CMP_HEADER_CONTAINS ||
			    (!is_email_column && (cmp_kind == CMP_HEADER_MATCHES ||
			    cmp_kind == CMP_HEADER_STARTS_WITH ||
			    cmp_kind == CMP_HEADER_ENDS_WITH))) {
				g_string_append_printf (stmt, "camelfromloadedinfoordb('%p',uid,'%s',%s) LIKE '", self, column_name, column_name);

				if (cmp_kind == CMP_HEADER_CONTAINS || cmp_kind == CMP_HEADER_ENDS_WITH)
					g_string_append_c (stmt, '%');

				camel_db_sqlize_to_statement (stmt, value, CAMEL_DB_SQLIZE_FLAG_ESCAPE_ONLY);

				if (cmp_kind == CMP_HEADER_CONTAINS || cmp_kind == CMP_HEADER_STARTS_WITH)
					g_string_append_c (stmt, '%');

				g_string_append (stmt, "' THEN 1");
			} else {
				g_string_append (stmt, "camelsearchheader(");
				g_string_append_printf (stmt, "'%p',uid,", self);
				camel_db_sqlize_to_statement (stmt, header_name, CAMEL_DB_SQLIZE_FLAG_FULL);
				g_string_append_printf (stmt, ",%d,", (gint) cmp_kind);
				camel_db_sqlize_to_statement (stmt, value, CAMEL_DB_SQLIZE_FLAG_FULL);
				g_string_append_printf (stmt, ",camelfromloadedinfoordb('%p',uid,'%s',%s)", self, column_name, column_name);
				g_string_append (stmt, ") THEN 1");

				store_search_result_add_flags (res, RESULT_NEEDS_HEADERS);
			}
		} else if (!header_name || !*header_name) {
			const gchar *column_names[] = {
				"uid",
				"subject",
				"mail_from",
				"mail_to",
				"mail_cc",
				"mlist",
				"userheaders"
			};
			guint jj;

			for (jj = 0; jj < G_N_ELEMENTS (column_names); jj++) {
				if (jj)
					g_string_append (stmt, " THEN 1 WHEN ");

				g_string_append (stmt, "camelcmptext(");
				g_string_append_printf (stmt, "'%p',uid,", self);
				camel_db_sqlize_to_statement (stmt, column_names[jj], CAMEL_DB_SQLIZE_FLAG_FULL);
				g_string_append_printf (stmt, ",%d,camelfromloadedinfoordb('%p',uid,'%s',%s),", (gint) cmp_kind, self, column_names[jj], column_names[jj]);
				camel_db_sqlize_to_statement (stmt, value, CAMEL_DB_SQLIZE_FLAG_FULL);
				g_string_append_c (stmt, ')');
			}

			g_string_append (stmt, " THEN 1 WHEN ");

			/* last resort, get the headers from the message info or the message itself */
			g_string_append (stmt, "camelsearchheader(");
			g_string_append_printf (stmt, "'%p',uid,'',%d,", self, (gint) cmp_kind);
			camel_db_sqlize_to_statement (stmt, value, CAMEL_DB_SQLIZE_FLAG_FULL);
			g_string_append (stmt, ",NULL) THEN 1");

			store_search_result_add_flags (res, RESULT_NEEDS_HEADERS);
		} else {
			g_string_append (stmt, "camelsearchheader(");
			g_string_append_printf (stmt, "'%p',uid,", self);
			camel_db_sqlize_to_statement (stmt, header_name, CAMEL_DB_SQLIZE_FLAG_FULL);
			g_string_append_printf (stmt, ",%d,", (gint) cmp_kind);
			camel_db_sqlize_to_statement (stmt, value, CAMEL_DB_SQLIZE_FLAG_FULL);
			g_string_append (stmt, ",NULL) THEN 1");

			store_search_result_add_flags (res, RESULT_NEEDS_HEADERS);
		}
	}

	if (stmt->len)
		g_string_append (stmt, " ELSE 0 END");

	store_search_result_add_flags (res, max_flags);

	/* nothing could be applied => everything matches (result is a NULL string) */
	res->value.string = g_string_free (stmt, !stmt->len);

	return res;
}

static CamelSExpResult *
store_search_header_contains_cb (CamelSExp *sexp,
				 gint argc,
				 CamelSExpResult **argv,
				 gpointer user_data)
{
	return store_search_header (sexp, argc, argv, user_data, CMP_HEADER_CONTAINS);
}

static CamelSExpResult *
store_search_header_matches_cb (CamelSExp *sexp,
				gint argc,
				CamelSExpResult **argv,
				gpointer user_data)
{
	return store_search_header (sexp, argc, argv, user_data, CMP_HEADER_MATCHES);
}

static CamelSExpResult *
store_search_header_starts_with_cb (CamelSExp *sexp,
				    gint argc,
				    CamelSExpResult **argv,
				    gpointer user_data)
{
	return store_search_header (sexp, argc, argv, user_data, CMP_HEADER_STARTS_WITH);
}

static CamelSExpResult *
store_search_header_ends_with_cb (CamelSExp *sexp,
				  gint argc,
				  CamelSExpResult **argv,
				  gpointer user_data)
{
	return store_search_header (sexp, argc, argv, user_data, CMP_HEADER_ENDS_WITH);
}

static CamelSExpResult *
store_search_header_exists_cb (CamelSExp *sexp,
			       gint argc,
			       CamelSExpResult **argv,
			       gpointer user_data)
{
	return store_search_header (sexp, argc, argv, user_data, CMP_HEADER_EXISTS);
}

static CamelSExpResult *
store_search_header_soundex_cb (CamelSExp *sexp,
				gint argc,
				CamelSExpResult **argv,
				gpointer user_data)
{
	return store_search_header (sexp, argc, argv, user_data, CMP_HEADER_SOUNDEX);
}

static CamelSExpResult *
store_search_header_regex_cb (CamelSExp *sexp,
			      gint argc,
			      CamelSExpResult **argv,
			      gpointer user_data)
{
	return store_search_header (sexp, argc, argv, user_data, CMP_HEADER_REGEX);
}

static CamelSExpResult *
store_search_header_full_regex_cb (CamelSExp *sexp,
				   gint argc,
				   CamelSExpResult **argv,
				   gpointer user_data)
{
	return store_search_header (sexp, argc, argv, user_data, CMP_HEADER_FULL_REGEX);
}

static CamelSExpResult *
store_search_user_tag_cb (CamelSExp *sexp,
			  gint argc,
			  CamelSExpResult **argv,
			  gpointer user_data)
{
	CamelStoreSearch *self = user_data;
	CamelSExpResult *res = store_search_create_string_result (sexp);
	GString *stmt;

	g_return_val_if_fail (argc == 1, res);
	g_return_val_if_fail (argv[0]->type == CAMEL_SEXP_RES_STRING, res);

	stmt = g_string_new ("camelgetusertag(");
	g_string_append_printf (stmt, "'%p',uid,", self);
	store_search_res_to_statement (stmt, argv[0]);
	g_string_append (stmt, ", usertags)");

	store_search_result_add_flags (res, store_search_result_get_flags (argv[0]));
	store_search_result_add_flags (res, RESULT_NEEDS_CUSTOM_FUNCTION);

	res->value.string = g_string_free (stmt, FALSE);

	return res;
}

static CamelSExpResult *
store_search_user_flag_cb (CamelSExp *sexp,
			   gint argc,
			   CamelSExpResult **argv,
			   gpointer user_data)
{
	CamelStoreSearch *self = user_data;
	CamelSExpResult *res = store_search_create_string_result (sexp);
	GString *stmt;

	g_return_val_if_fail (argc == 1, res);
	g_return_val_if_fail (argv[0]->type == CAMEL_SEXP_RES_STRING, res);

	stmt = g_string_new ("camelchecklabels(");
	g_string_append_printf (stmt, "'%p',uid,", self);
	store_search_res_to_statement (stmt, argv[0]);
	g_string_append (stmt, ",labels)");

	store_search_result_add_flags (res, store_search_result_get_flags (argv[0]));
	res->value.string = g_string_free (stmt, FALSE);

	return res;
}

static CamelSExpResult *
store_search_system_flag_cb (CamelSExp *sexp,
			     gint argc,
			     CamelSExpResult **argv,
			     gpointer user_data)
{
	CamelStoreSearch *self = user_data;
	CamelSExpResult *res = store_search_create_string_result (sexp);

	g_return_val_if_fail (argc == 1, res);
	g_return_val_if_fail (argv[0]->type == CAMEL_SEXP_RES_STRING, res);

	store_search_result_add_flags (res, store_search_result_get_flags (argv[0]));
	res->value.string = g_strdup_printf ("camelcheckflags('%p',uid,%u,flags)", self, camel_system_flag (argv[0]->value.string));

	return res;
}

static CamelSExpResult *
store_search_get_sent_date_cb (CamelSExp *sexp,
			       gint argc,
			       CamelSExpResult **argv,
			       gpointer user_data)
{
	CamelSExpResult *res = store_search_create_string_result (sexp);

	res->value.string = g_strdup ("dsent");

	return res;
}

static CamelSExpResult *
store_search_get_received_date_cb (CamelSExp *sexp,
				   gint argc,
				   CamelSExpResult **argv,
				   gpointer user_data)
{
	CamelSExpResult *res = store_search_create_string_result (sexp);

	res->value.string = g_strdup ("dreceived");

	return res;
}

static CamelSExpResult *
store_search_get_current_date_cb (CamelSExp *sexp,
				  gint argc,
				  CamelSExpResult **argv,
				  gpointer user_data)
{
	CamelSExpResult *res = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_TIME);

	res->value.time = time (NULL);

	return res;
}

static CamelSExpResult *
store_search_get_relative_months_cb (CamelSExp *sexp,
				     gint argc,
				     CamelSExpResult **argv,
				     gpointer user_data)
{
	CamelSExpResult *res = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_TIME);

	g_return_val_if_fail (argc == 1, res);
	g_return_val_if_fail (argv[0]->type == CAMEL_SEXP_RES_INT, res);

	res->value.number = camel_search_util_add_months (time (NULL), argv[0]->value.number);

	return res;
}

static CamelSExpResult *
store_search_get_size_cb (CamelSExp *sexp,
			  gint argc,
			  CamelSExpResult **argv,
			  gpointer user_data)
{
	CamelSExpResult *res = store_search_create_string_result (sexp);

	res->value.string = g_strdup ("size/1024");

	return res;
}

static CamelSExpResult *
store_search_get_uid_cb (CamelSExp *sexp,
			 gint argc,
			 CamelSExpResult **argv,
			 gpointer user_data)
{
	CamelSExpResult *res = store_search_create_string_result (sexp);
	GString *stmt;
	gint ii;

	stmt = g_string_new ("");

	for (ii = 0; ii < argc; ii++) {
		if (argv[ii]->type == CAMEL_SEXP_RES_STRING) {
			const gchar *uid = argv[ii]->value.string;

			if (uid && *uid) {
				if (stmt->len)
					g_string_append_c (stmt, ',');
				camel_db_sqlize_to_statement (stmt, uid, CAMEL_DB_SQLIZE_FLAG_FULL);
			}
		}
	}

	if (stmt->len) {
		g_string_prepend (stmt, "uid IN (");
		g_string_append_c (stmt, ')');
	}

	res->value.string = g_string_free (stmt, !stmt->len);

	return res;
}

static CamelSExpResult *
store_search_get_message_location_cb (CamelSExp *sexp,
				      gint argc,
				      CamelSExpResult **argv,
				      gpointer user_data)
{
	CamelStoreSearch *self = user_data;
	CamelSExpResult *res = store_search_create_string_result (sexp);

	if (argc == 1 && argv[0]->type == CAMEL_SEXP_RES_STRING) {
		GUri *uri;

		uri = g_uri_parse (argv[0]->value.string, G_URI_FLAGS_PARSE_RELAXED, NULL);
		if (uri) {
			gchar *service_uid = NULL;

			if (g_uri_get_user (uri))
				service_uid = g_strconcat (g_uri_get_user (uri), "@", g_uri_get_host (uri), NULL);

			if (g_uri_get_path (uri) &&
			    g_strcmp0 (service_uid ? service_uid : g_uri_get_host (uri), camel_service_get_uid (CAMEL_SERVICE (self->priv->store))) == 0) {
				CamelStoreDB *sdb = self->priv->store_db;

				if (sdb) {
					const gchar *path = g_uri_get_path (uri);
					guint32 folder_id;

					folder_id = camel_store_db_get_folder_id (sdb, path);

					if (!folder_id && *path == '/')
						folder_id = camel_store_db_get_folder_id (sdb, path + 1);

					if (folder_id)
						res->value.string = g_strdup_printf ("camelisfolderid('%p',%u)", self, folder_id);
				}
			}

			g_free (service_uid);
			g_uri_unref (uri);
		}
	}

	if (!res->value.string)
		res->value.string = g_strdup ("0");

	return res;
}

static CamelSExpResult *
store_search_make_time_cb (CamelSExp *sexp,
			   gint argc,
			   CamelSExpResult **argv,
			   gpointer user_data)
{
	CamelSExpResult *res = store_search_create_string_result (sexp);

	if (argc == 1 && argv[0]->type == CAMEL_SEXP_RES_STRING && !store_search_result_is (argv[0], RESULT_IS_SQL)) {
		time_t tt;

		tt = camel_search_util_make_time (argc, argv);
		res->value.string = g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) tt);
	} else {
		GString *stmt;

		stmt = g_string_new ("camelmaketime(");
		store_search_res_to_statement (stmt, argv[0]);
		g_string_append_c (stmt, ')');

		store_search_result_add_flags (res, RESULT_NEEDS_CUSTOM_FUNCTION);
		store_search_result_add_flags (res, store_search_result_get_flags (argv[0]));
		res->value.string = g_string_free (stmt, FALSE);
	}

	return res;
}

static CamelSExpResult *
store_search_addressbook_contains_cb (CamelSExp *sexp,
				      gint argc,
				      CamelSExpResult **argv,
				      gpointer user_data)
{
	CamelSExpResult *res = store_search_create_string_result (sexp);
	CamelStoreSearch *self = user_data;

	if (argc == 2) {
		const gchar *book_uid, *header_name;

		if (argv[0]->type != CAMEL_SEXP_RES_STRING ||
		    argv[1]->type != CAMEL_SEXP_RES_STRING)
			return res;

		book_uid = argv[0]->value.string;
		header_name = argv[1]->value.string;

		if (header_name && (
		    g_ascii_strcasecmp (header_name, "From") == 0 ||
		    g_ascii_strcasecmp (header_name, "To") == 0 ||
		    g_ascii_strcasecmp (header_name, "Cc") == 0)) {
			GString *stmt;

			stmt = g_string_new ("cameladdressbookcontains(");
			g_string_append_printf (stmt, "'%p',", self);
			camel_db_sqlize_to_statement (stmt, book_uid, CAMEL_DB_SQLIZE_FLAG_FULL);
			g_string_append_printf (stmt, ",%s)", camel_store_db_util_get_column_for_header_name (header_name));

			store_search_result_add_flags (res, RESULT_NEEDS_CONTACTS);
			res->value.string = g_string_free (stmt, FALSE);
		}
	}

	return res;
}

static CamelSExpResult *
store_search_header_has_words_cb (CamelSExp *sexp,
				  gint argc,
				  CamelSExpResult **argv,
				  gpointer user_data)
{
	return store_search_header (sexp, argc, argv, user_data, CMP_HEADER_HAS_WORDS);
}

static CamelSExpResult *
store_search_in_match_index_cb (CamelSExp *sexp,
				gint argc,
				CamelSExpResult **argv,
				gpointer user_data)
{
	CamelSExpResult *res = store_search_create_string_result (sexp);
	CamelStoreSearch *self = user_data;

	if (argc == 1) {
		const gchar *index_id;
		GString *stmt;

		if (argv[0]->type != CAMEL_SEXP_RES_STRING)
			return res;

		index_id = argv[0]->value.string;

		stmt = g_string_new ("camelsearchinmatchindex(");
		g_string_append_printf (stmt, "'%p',", self);
		camel_db_sqlize_to_statement (stmt, index_id, CAMEL_DB_SQLIZE_FLAG_FULL);
		g_string_append (stmt, ",uid)");

		store_search_result_add_flags (res, RESULT_NEEDS_CUSTOM_FUNCTION);
		res->value.string = g_string_free (stmt, FALSE);
	}

	return res;
}

static CamelSExp *
camel_store_search_new_sexp (CamelStoreSearch *self)
{
	const struct {
		const gchar *name;
		CamelSExpIFunc func;
	} isymbols[] = {
		{ "and", store_search_and_cb },
		{ "or", store_search_or_cb },
		{ "not", store_search_not_cb },
		{ "<", store_search_lt_cb },
		{ ">", store_search_gt_cb },
		{ "=", store_search_eq_cb },
		{ "match-all", store_search_match_all_cb },
		{ "match-threads", store_search_match_threads_cb },
		{ "compare-date", store_search_compare_date_cb }
	};
	const struct {
		const gchar *name;
		CamelSExpFunc func;
	} symbols[] = {
		{ "body-contains", store_search_body_contains_cb },
		{ "body-regex", store_search_body_regex_cb },
		{ "header-contains", store_search_header_contains_cb },
		{ "header-matches", store_search_header_matches_cb },
		{ "header-starts-with", store_search_header_starts_with_cb },
		{ "header-ends-with", store_search_header_ends_with_cb },
		{ "header-exists", store_search_header_exists_cb },
		{ "header-soundex", store_search_header_soundex_cb },
		{ "header-regex", store_search_header_regex_cb },
		{ "header-full-regex", store_search_header_full_regex_cb },
		{ "user-tag", store_search_user_tag_cb },
		{ "user-flag", store_search_user_flag_cb },
		{ "system-flag", store_search_system_flag_cb },
		{ "get-sent-date", store_search_get_sent_date_cb },
		{ "get-received-date", store_search_get_received_date_cb },
		{ "get-current-date", store_search_get_current_date_cb },
		{ "get-relative-months", store_search_get_relative_months_cb },
		{ "get-size", store_search_get_size_cb },
		{ "uid", store_search_get_uid_cb },
		{ "message-location", store_search_get_message_location_cb },
		{ "make-time", store_search_make_time_cb },
		{ "addressbook-contains", store_search_addressbook_contains_cb },
		{ "header-has-words", store_search_header_has_words_cb },
		{ "in-match-index", store_search_in_match_index_cb }
	};
	CamelSExp *sexp;
	guint ii;

	sexp = camel_sexp_new ();

	for (ii = 0; ii < G_N_ELEMENTS (isymbols); ii++) {
		camel_sexp_add_ifunction (sexp, 0, isymbols[ii].name, isymbols[ii].func, self);
	}

	for (ii = 0; ii < G_N_ELEMENTS (symbols); ii++) {
		camel_sexp_add_function (sexp, 0, symbols[ii].name, symbols[ii].func, self);
	}

	return sexp;
}

static void
camel_store_search_set_property (GObject *object,
				 guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	CamelStoreSearch *self = CAMEL_STORE_SEARCH (object);

	switch (prop_id) {
	case PROP_STORE:
		if (self->priv->store_db) {
			_camel_store_db_unregister_search (self->priv->store_db, self);
			self->priv->store_db = NULL;
		}
		g_clear_object (&self->priv->store);
		self->priv->store = g_value_dup_object (value);
		if (self->priv->store) {
			self->priv->store_db = camel_store_get_db (self->priv->store);
			_camel_store_db_register_search (self->priv->store_db, self);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
camel_store_search_get_property (GObject *object,
				 guint prop_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	CamelStoreSearch *self = CAMEL_STORE_SEARCH (object);

	switch (prop_id) {
	case PROP_STORE:
		g_value_set_object (value, camel_store_search_get_store (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
camel_store_search_finalize (GObject *object)
{
	CamelStoreSearch *self = CAMEL_STORE_SEARCH (object);

	if (self->priv->store_db) {
		_camel_store_db_unregister_search (self->priv->store_db, self);
		self->priv->store_db = NULL;
	}

	camel_store_search_clear_ongoing_search_data (self);

	g_clear_object (&self->priv->store);
	g_clear_object (&self->priv->sexp);
	g_clear_object (&self->priv->ongoing_search.search_body);
	g_clear_pointer (&self->priv->expression, g_free);
	g_clear_pointer (&self->priv->additional_columns, g_ptr_array_unref);
	g_clear_pointer (&self->priv->additional_columns_stmt, g_free);
	g_clear_pointer (&self->priv->folders, g_hash_table_destroy);
	g_clear_pointer (&self->priv->folders_by_id, g_hash_table_destroy);
	g_clear_pointer (&self->priv->where_clause_sql, g_free);
	g_clear_pointer (&self->priv->result_index, camel_store_search_index_unref);
	g_clear_pointer (&self->priv->match_indexes, g_hash_table_unref);

	G_OBJECT_CLASS (camel_store_search_parent_class)->finalize (object);
}

static void
camel_store_search_class_init (CamelStoreSearchClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->set_property = camel_store_search_set_property;
	object_class->get_property = camel_store_search_get_property;
	object_class->finalize = camel_store_search_finalize;

	/**
	 * CamelStoreSearch:store:
	 *
	 * A #CamelStore this search works with.
	 *
	 * Since: 3.58
	 **/
	properties[PROP_STORE] = g_param_spec_object ("store", NULL, NULL, CAMEL_TYPE_STORE,
		G_PARAM_READWRITE |
		G_PARAM_CONSTRUCT_ONLY |
		G_PARAM_STATIC_STRINGS |
		G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (properties), properties);
}

static void
camel_store_search_init (CamelStoreSearch *self)
{
	self->priv = camel_store_search_get_instance_private (self);
	self->priv->folders = g_hash_table_new_full (camel_strcase_hash, camel_strcase_equal, g_free, g_object_unref);
	self->priv->folders_by_id = g_hash_table_new (g_direct_hash, g_direct_equal); /* shares the pointer with the `folders` */
	self->priv->sexp = camel_store_search_new_sexp (self);
	self->priv->ongoing_search.search_body = search_cache_new ();
}

/**
 * camel_store_search_new:
 * @store: a #CamelStore
 *
 * Creates a new #CamelStoreSearch, which will operate
 * on folders from the @store.
 *
 * Returns: (transfer full): a new #CamelStoreSearch
 *
 * Since: 3.58
 **/
CamelStoreSearch *
camel_store_search_new (CamelStore *store)
{
	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);

	return g_object_new (CAMEL_TYPE_STORE_SEARCH,
		"store", store,
		NULL);
}

/**
 * camel_store_search_get_store:
 * @self: a #CamelStoreSearch
 *
 * Gets a #CamelStore the @self was constructed for.
 *
 * Returns: (transfer none): a #CamelStore the @self was constructed for.
 *
 * Since: 3.58
 **/
CamelStore *
camel_store_search_get_store (CamelStoreSearch *self)
{
	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), NULL);

	return self->priv->store;
}

/**
 * camel_store_search_set_expression:
 * @self: a #CamelStoreSearch
 * @expression: (nullable): a search expression, or %NULL
 *
 * Sets the search @expression to be used to search the messages
 * in the selected folders with.
 *
 * The expression can be %NULL, to not search the folders.
 *
 * The content is not updated automatically, call camel_store_search_rebuild_sync()
 * to rebuild the content.
 *
 * Since: 3.58
 **/
void
camel_store_search_set_expression (CamelStoreSearch *self,
				   const gchar *expression)
{
	g_return_if_fail (CAMEL_IS_STORE_SEARCH (self));

	if (expression && !*expression)
		expression = NULL;

	if (g_strcmp0 (self->priv->expression, expression) == 0)
		return;

	g_free (self->priv->expression);
	self->priv->expression = g_strdup (expression);
	self->priv->needs_rebuild = TRUE;
}

/**
 * camel_store_search_get_expression:
 * @self: a #CamelStoreSearch
 *
 * Gets search expression previously set by the camel_store_search_set_expression().
 *
 * Returns: (nullable): current search expression for the @self,
 *    or %NULL, when none is set.
 *
 * Since: 3.58
 **/
const gchar *
camel_store_search_get_expression (CamelStoreSearch *self)
{
	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), NULL);

	return self->priv->expression;
}

/**
 * camel_store_search_set_additional_columns:
 * @self: a #CamelStoreSearch
 * @colnames: (element-type utf8) (nullable): column names, or %NULL to unset
 *
 * Sets what additional column names should be pre-read and provided
 * in the camel_store_search_get_items_sync() result. An empty array is
 * the same as passing %NULL for the @colnames.
 *
 * Make sure to call camel_store_search_rebuild_sync() to have the values
 * read.
 *
 * Since: 3.58
 **/
void
camel_store_search_set_additional_columns (CamelStoreSearch *self,
					   const GPtrArray *colnames)
{
	g_return_if_fail (CAMEL_IS_STORE_SEARCH (self));

	g_clear_pointer (&self->priv->additional_columns, g_ptr_array_unref);
	g_clear_pointer (&self->priv->additional_columns_stmt, g_free);
	self->priv->needs_rebuild = TRUE;

	if (colnames && colnames->len) {
		GString *stmt;
		guint ii;

		stmt = g_string_new ("");

		self->priv->additional_columns = g_ptr_array_new_full (colnames->len, g_free);

		for (ii = 0; ii < colnames->len; ii++) {
			const gchar *colname = g_ptr_array_index (colnames, ii);

			if (!colname || !*colname)
				continue;

			g_ptr_array_add (self->priv->additional_columns, g_strdup (colname));

			g_string_append_c (stmt, ',');
			g_string_append (stmt, colname);
		}

		if (stmt->len) {
			self->priv->additional_columns_stmt = g_string_free (stmt, FALSE);
		} else {
			g_string_free (stmt, TRUE);
			g_clear_pointer (&self->priv->additional_columns, g_ptr_array_unref);
		}
	}
}

/**
 * camel_store_search_dup_additional_columns:
 * @self: a #CamelStoreSearch
 *
 * Gets a new #GPtrArray with additional columns names previously
 * set by the camel_store_search_set_additional_columns(), or %NULL,
 * when none had been set.
 *
 * Free the returned array with g_ptr_array_unref(), when no longer needed.
 *
 * Returns: (transfer container) (element-type utf8) (nullable): previously set additional
 *    column names to read, or %NULL, when none had been set
 *
 * Since: 3.58
 **/
GPtrArray *
camel_store_search_dup_additional_columns (CamelStoreSearch *self)
{
	GPtrArray *array = NULL;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), NULL);

	if (self->priv->additional_columns) {
		guint ii;

		array = g_ptr_array_new_full (self->priv->additional_columns->len, g_free);
		for (ii = 0; ii < self->priv->additional_columns->len; ii++) {
			const gchar *colname = g_ptr_array_index (self->priv->additional_columns, ii);
			g_ptr_array_add (array, g_strdup (colname));
		}
	}

	return array;
}

/**
 * camel_store_search_add_folder:
 * @self: a #CamelStoreSearch
 * @folder: a #CamelFolder
 *
 * Adds the @folder to the list of the folders to be searched in
 * by the @self. The function does nothing when the @folder is
 * already part of the @folder.
 *
 * The content is not updated automatically, call camel_store_search_rebuild_sync()
 * to rebuild the content.
 *
 * It's an error to try to add a @folder which is not owned by
 * the #CamelStore the @self was created with.
 *
 * Since: 3.58
 **/
void
camel_store_search_add_folder (CamelStoreSearch *self,
			       CamelFolder *folder)
{
	CamelStore *parent_store;

	g_return_if_fail (CAMEL_IS_STORE_SEARCH (self));
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	parent_store = camel_folder_get_parent_store (folder);

	if (parent_store && self->priv->store == parent_store) {
		const gchar *full_name = camel_folder_get_full_name (folder);

		if (!g_hash_table_contains (self->priv->folders, full_name)) {
			guint32 folder_id;

			g_hash_table_insert (self->priv->folders, g_strdup (full_name), g_object_ref (folder));

			folder_id = camel_store_db_get_folder_id (self->priv->store_db, full_name);
			g_warn_if_fail (folder_id != 0);

			if (folder_id) {
				g_hash_table_insert (self->priv->folders_by_id, GUINT_TO_POINTER (folder_id), folder);
				self->priv->needs_rebuild = TRUE;
			}
		}
	} else {
		g_warning ("%s: Folder '%s' does not belong to '%s', but to '%s'", G_STRFUNC,
			camel_folder_get_full_name (folder),
			self->priv->store ? camel_service_get_uid (CAMEL_SERVICE (self->priv->store)) : "null",
			parent_store ? camel_service_get_uid (CAMEL_SERVICE (parent_store)) : "null");
	}
}

/**
 * camel_store_search_remove_folder:
 * @self: a #CamelStoreSearch
 * @folder: a #CamelFolder
 *
 * Removes the @folder from the list of the folders the @self
 * should search in. It does nothing when the @folder is not part
 * of the @self.
 *
 * The content is not updated automatically, call camel_store_search_rebuild_sync()
 * to rebuild the content.
 *
 * Since: 3.58
 **/
void
camel_store_search_remove_folder (CamelStoreSearch *self,
				  CamelFolder *folder)
{
	CamelStore *parent_store;

	g_return_if_fail (CAMEL_IS_STORE_SEARCH (self));
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	parent_store = camel_folder_get_parent_store (folder);

	if (parent_store && self->priv->store == parent_store) {
		const gchar *full_name = camel_folder_get_full_name (folder);

		if (g_hash_table_remove (self->priv->folders, full_name)) {
			CamelStoreDB *sdb = self->priv->store_db;
			guint32 folder_id;

			folder_id = camel_store_db_get_folder_id (sdb, full_name);
			if (folder_id)
				g_hash_table_remove (self->priv->folders_by_id, GUINT_TO_POINTER (folder_id));

			self->priv->needs_rebuild = TRUE;
		}
	} else {
		g_warning ("%s: Folder '%s' does not belong to '%s', but to '%s'", G_STRFUNC,
			camel_folder_get_full_name (folder),
			self->priv->store ? camel_service_get_uid (CAMEL_SERVICE (self->priv->store)) : "null",
			parent_store ? camel_service_get_uid (CAMEL_SERVICE (parent_store)) : "null");
	}
}

/**
 * camel_store_search_list_folders:
 * @self: a #CamelStoreSearch
 *
 * Lists the #CamelFolder-s the @self searches in.
 *
 * Free the returned #GPtrArray with g_ptr_array_unref(), when
 * no longer needed.
 *
 * Returns: (transfer container) (element-type CamelFolder): a newly
 *    created #GPtrArray with the #CamelFolder instances the @self
 *    searches in
 *
 * Since: 3.58
 **/
GPtrArray *
camel_store_search_list_folders (CamelStoreSearch *self)
{
	GHashTableIter iter;
	GPtrArray *folders;
	gpointer value = NULL;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), NULL);

	folders = g_ptr_array_new_full (g_hash_table_size (self->priv->folders), g_object_unref);

	g_hash_table_iter_init (&iter, self->priv->folders);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		CamelFolder *folder = value;

		g_ptr_array_add (folders, g_object_ref (folder));
	}

	return folders;
}

/* this is needed to avoid deadlock when two threads are reading from the CamelStoreDB
   and one of them tries to read data from the folder summary during the SELECT execution */
static gboolean
camel_store_search_acquire_folder (CamelFolder *folder,
				   GError **error)
{
	CamelFolderSummary *summary = camel_folder_get_folder_summary (folder);
	gboolean success = TRUE;

	camel_folder_lock (folder);
	if (summary) {
		camel_folder_summary_lock (summary);
		success = camel_folder_summary_save (summary, error);
		success = success && camel_folder_summary_prepare_fetch_all (summary, error);

		if (!success) {
			camel_folder_summary_unlock (summary);
			camel_folder_unlock (folder);
		}
	}

	return success;
}

static void
camel_store_search_release_folder (CamelFolder *folder)
{
	CamelFolderSummary *summary = camel_folder_get_folder_summary (folder);

	if (summary)
		camel_folder_summary_unlock (summary);
	camel_folder_unlock (folder);
}

/* this does not return whether succeeded, but whether it should repeat the search */
static gboolean
camel_store_search_handle_remote_ops_sync (CamelStoreSearch *self,
					   gboolean *out_success,
					   GCancellable *cancellable,
					   GError **error)
{
	#define needs_remote_ops(_ht) ((_ht) && g_hash_table_size ((_ht)))

	gboolean do_repeat = FALSE;

	*out_success = TRUE;

	if (needs_remote_ops (self->priv->ongoing_search.todo_search_ops_by_uid)) {
		GHashTableIter iter;
		gpointer key = NULL, value = NULL;

		g_hash_table_iter_init (&iter, self->priv->ongoing_search.todo_search_ops_by_uid);
		while (*out_success && g_hash_table_iter_next (&iter, &key, &value)) {
			const gchar *uid = key;
			GPtrArray *op_array = value;
			guint ii;

			for (ii = 0; ii < op_array->len && *out_success; ii++) {
				SearchOp *op = g_ptr_array_index (op_array, ii);

				search_op_run_sync (op, self, uid);

				*out_success = self->priv->ongoing_search.success &&
					!g_cancellable_set_error_if_cancelled (cancellable, error);
			}
		}

		g_hash_table_remove_all (self->priv->ongoing_search.todo_search_ops_by_uid);

		do_repeat = TRUE;
	}

	if (needs_remote_ops (self->priv->ongoing_search.todo_addr_book)) {
		CamelSession *session;

		session = camel_service_ref_session (CAMEL_SERVICE (self->priv->store));
		if (session) {
			GHashTableIter iter;
			gpointer key = NULL;

			if (!self->priv->ongoing_search.done_addr_book) {
				self->priv->ongoing_search.done_addr_book = g_hash_table_new_full (addr_book_op_key_hash, addr_book_op_key_equal,
					addr_book_op_key_free, NULL);
			}

			g_hash_table_iter_init (&iter, self->priv->ongoing_search.todo_addr_book);
			while (*out_success && g_hash_table_iter_next (&iter, &key, NULL)) {
				AddrBookOpKey *op = key;
				gboolean contains;

				contains = camel_session_addressbook_contains_sync (session, op->book_uid, op->email, cancellable, NULL);

				g_hash_table_insert (self->priv->ongoing_search.done_addr_book, addr_book_op_key_new (op->book_uid, op->email),
					GINT_TO_POINTER (contains ? 1 : 0));

				*out_success = !g_cancellable_set_error_if_cancelled (cancellable, error);
			}

			g_object_unref (session);
		}

		g_hash_table_remove_all (self->priv->ongoing_search.todo_addr_book);

		do_repeat = TRUE;
	}

	return do_repeat && *out_success;

	#undef needs_remote_ops
}

typedef struct _ResultIndexData {
	CamelStoreSearchIndex *result_index;
	CamelStore *store;
	guint32 folder_id;
} ResultIndexData;

static gboolean
camel_store_search_populate_result_index_cb (gpointer user_data,
					     gint ncol,
					     gchar **cols,
					     gchar **names)
{
	ResultIndexData *rid = user_data;
	const gchar *uid;

	g_return_val_if_fail (ncol == 1, FALSE);

	uid = cols[0];
	if (uid)
		camel_store_search_index_add (rid->result_index, rid->store, rid->folder_id, uid);

	return TRUE;
}

static gboolean
camel_store_search_populate_result_index_sync (CamelStoreSearch *self,
					       GCancellable *cancellable,
					       GError **error)
{
	CamelDB *cdb;
	GHashTableIter iter;
	GString *stmt;
	gpointer key = NULL, value = NULL;
	gboolean success = TRUE;

	self->priv->result_index = camel_store_search_index_new ();

	cdb = CAMEL_DB (self->priv->store_db);

	stmt = g_string_new ("");
	g_hash_table_iter_init (&iter, self->priv->folders_by_id);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		guint32 folder_id = GPOINTER_TO_UINT (key);
		CamelFolder *folder = value;
		ResultIndexData rid;

		if (stmt->len)
			g_string_truncate (stmt, 0);

		g_string_append_printf (stmt, "SELECT uid FROM messages_%u WHERE %s",
			folder_id, self->priv->where_clause_sql);

		camel_store_search_clear_ongoing_search_data (self);
		self->priv->ongoing_search.cancellable = cancellable;
		self->priv->ongoing_search.error = error;
		self->priv->ongoing_search.folder_id = folder_id;
		self->priv->ongoing_search.folder = folder;
		self->priv->ongoing_search.folder_summary = camel_folder_get_folder_summary (folder);

		rid.result_index = NULL;
		rid.store = self->priv->store;
		rid.folder_id = self->priv->ongoing_search.folder_id;

		do {
			if (rid.result_index)
				g_hash_table_remove_all ((GHashTable *) rid.result_index);
			else
				rid.result_index = camel_store_search_index_new ();

			if (!camel_store_search_acquire_folder (folder, error)) {
				success = FALSE;
				break;
			}

			success = camel_db_exec_select (cdb, stmt->str, camel_store_search_populate_result_index_cb, &rid, error);

			camel_store_search_release_folder (folder);

			if (!success || !self->priv->ongoing_search.success)
				break;
		} while (camel_store_search_handle_remote_ops_sync (self, &success, cancellable, error));

		camel_store_search_index_move_from_existing (self->priv->result_index, rid.result_index);
		camel_store_search_index_unref (rid.result_index);

		search_cache_clear (self->priv->ongoing_search.search_body);

		if (!success || !self->priv->ongoing_search.success)
			break;
	}

	g_string_free (stmt, TRUE);

	return success;
}

/**
 * camel_store_search_rebuild_sync:
 * @self: a #CamelStoreSearch
 * @cancellable: a #GCancellable, or #NULL
 * @error: return location for a #GError, or %NULL
 *
 * Rebuilds content of the @self with the current search expression.
 * The function does nothing when no search expression is set. It can
 * be called with no folder set, then it parses the expression and
 * sets the values for the camel_store_search_get_match_threads_kind().
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_store_search_rebuild_sync (CamelStoreSearch *self,
				 GCancellable *cancellable,
				 GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), FALSE);

	self->priv->match_threads_flags = CAMEL_FOLDER_THREAD_FLAG_NONE;
	self->priv->match_threads_kind = CAMEL_MATCH_THREADS_KIND_NONE;
	self->priv->needs_rebuild = FALSE;

	camel_store_search_clear_ongoing_search_data (self);

	if (!self->priv->expression)
		return TRUE;

	g_clear_pointer (&self->priv->where_clause_sql, g_free);
	g_clear_pointer (&self->priv->result_index, camel_store_search_index_unref);

	camel_sexp_input_text (self->priv->sexp, self->priv->expression, strlen (self->priv->expression));

	if (camel_sexp_parse (self->priv->sexp) == 0) {
		CamelSExpResult *sql;

		sql = camel_sexp_eval (self->priv->sexp);
		if (sql) {
			const gchar *stmt;

			/* "(match-all #t)" evaluates into a boolean result */
			if (sql->type == CAMEL_SEXP_RES_BOOL)
				stmt = sql->value.boolean ? "1" : "0";
			else if (sql->type == CAMEL_SEXP_RES_STRING && sql->value.string && *(sql->value.string))
				stmt = sql->value.string;
			else
				stmt = "1";

			self->priv->where_clause_sql = g_strdup (stmt);

			/* printf ("%s: expr:---%s--- SQL where-clause:---%s---\n", G_STRFUNC, self->priv->expression, self->priv->where_clause_sql); */
			if (self->priv->match_threads_kind != CAMEL_MATCH_THREADS_KIND_NONE) {
				success = camel_store_search_populate_result_index_sync (self, cancellable, error);

				if (success) {
					g_free (self->priv->where_clause_sql);
					self->priv->where_clause_sql = g_strdup_printf ("camelsearchinresultindex('%p',uid)", self);
				}
			}

			camel_sexp_result_free (self->priv->sexp, sql);
		} else {
			const gchar *err_msg = camel_sexp_error (self->priv->sexp);
			success = FALSE;
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Cannot evaluate expression “%s”: %s",
				self->priv->expression, err_msg ? err_msg : "Unknown error");
		}
	} else {
		const gchar *err_msg = camel_sexp_error (self->priv->sexp);

		success = FALSE;
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Cannot parse expression “%s”: %s",
			self->priv->expression, err_msg ? err_msg : "Unknown error");
	}

	return success;
}

struct _CamelStoreSearchItemPrivate {
	GPtrArray *additional_values;
};

static CamelStoreSearchItem *
camel_store_search_item_new (guint32 folder_id,
			     const gchar *uid)
{
	CamelStoreSearchItem *item;

	item = g_new0 (CamelStoreSearchItem, 1);
	item->priv = g_new0 (CamelStoreSearchItemPrivate, 1);
	item->folder_id = folder_id;
	item->uid = camel_pstring_strdup (uid);

	return item;
}

static void
camel_store_search_item_free (gpointer ptr)
{
	CamelStoreSearchItem *item = ptr;

	if (item) {
		camel_pstring_free (item->uid);
		g_clear_pointer (&item->priv->additional_values, g_ptr_array_unref);
		g_free (item->priv);
		g_free (item);
	}
}

/**
 * camel_store_search_item_get_n_additional_values:
 * @self: a #CamelStoreSearchItem
 *
 * Gets how many additional column values had been read. These are
 * related to the camel_store_search_set_additional_columns().
 *
 * Returns: how many additional column values had been read
 *
 * Since: 3.58
 **/
guint32
camel_store_search_item_get_n_additional_values (CamelStoreSearchItem *self)
{
	g_return_val_if_fail (self != NULL, 0);

	return self->priv->additional_values ? self->priv->additional_values->len : 0;
}

/**
 * camel_store_search_item_get_additional_value:
 * @self: a #CamelStoreSearchItem
 * @index: an index of the item to get, counting from zero
 *
 * Gets value of an additional column of an index @index, which
 * should be less than camel_store_search_item_get_n_additional_values().
 * The order corresponds to the camel_store_search_get_additional_columns().
 * Asking for an index which out of bounds returns %NULL.
 * Note the actual read value can be also %NULL.
 *
 * Returns: (nullable): additional read value at index @index, or %NULL
 *
 * Since: 3.58
 **/
const gchar *
camel_store_search_item_get_additional_value (CamelStoreSearchItem *self,
					      guint32 index)
{
	g_return_val_if_fail (self != NULL, NULL);

	if (!self->priv->additional_values || index >= self->priv->additional_values->len)
		return NULL;

	return g_ptr_array_index (self->priv->additional_values, index);
}

static gboolean
camel_store_search_read_items_cb (gpointer user_data,
				  gint ncol,
				  gchar **colvalues,
				  gchar **colnames)
{
	GPtrArray *items = user_data;
	CamelStoreSearchItem *item;
	guint32 folder_id;

	g_return_val_if_fail (items != NULL, FALSE);
	g_return_val_if_fail (ncol >= 2, FALSE);

	folder_id = colvalues[0] ? (guint32) g_ascii_strtoull (colvalues[0], NULL, 10) : 0;

	item = camel_store_search_item_new (folder_id, colvalues[1]);

	if (ncol > 2) {
		guint ii;

		item->priv->additional_values = g_ptr_array_new_full (ncol - 2, g_free);

		for (ii = 2; ii < ncol; ii++) {
			g_ptr_array_add (item->priv->additional_values, g_strdup (colvalues[ii]));
		}
	}

	g_ptr_array_add (items, item);

	return TRUE;
}

/**
 * camel_store_search_get_items_sync:
 * @self: a #CamelStoreSearch
 * @out_items: (out) (transfer container) (element-type CamelStoreSearchItem): an output
 *    argument for the #CamelStoreSearchItem items
 * @cancellable: a #GCancellable, or #NULL
 * @error: return location for a #GError, or %NULL
 *
 * Reads all the items from all the set folders satisfying the set expression.
 *
 * Free the @out_items array with g_ptr_array_unref(), when no longer needed.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_store_search_get_items_sync (CamelStoreSearch *self,
				   GPtrArray **out_items,
				   GCancellable *cancellable,
				   GError **error)
{
	CamelDB *cdb;
	GString *stmt;
	GHashTableIter iter;
	gpointer key = NULL, value = NULL;
	gboolean success = FALSE;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), FALSE);
	g_return_val_if_fail (out_items != NULL, FALSE);

	if (self->priv->needs_rebuild) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED, "Cannot get items: Run rebuild first");

		return FALSE;
	}

	if (!g_hash_table_size (self->priv->folders_by_id)) {
		*out_items = g_ptr_array_new ();

		return TRUE;
	}

	*out_items = g_ptr_array_new_full (1024, camel_store_search_item_free);

	cdb = CAMEL_DB (self->priv->store_db);

	stmt = g_string_new ("");
	g_hash_table_iter_init (&iter, self->priv->folders_by_id);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		guint32 folder_id = GPOINTER_TO_UINT (key);
		CamelFolder *folder = value;
		GPtrArray *items = NULL;

		if (stmt->len)
			g_string_truncate (stmt, 0);

		g_string_append_printf (stmt, "SELECT %u AS folder_id,uid", folder_id);
		if (self->priv->additional_columns_stmt)
			g_string_append (stmt, self->priv->additional_columns_stmt);
		g_string_append_printf (stmt, " FROM messages_%u WHERE %s",
			folder_id, self->priv->where_clause_sql);

		camel_store_search_clear_ongoing_search_data (self);
		self->priv->ongoing_search.cancellable = cancellable;
		self->priv->ongoing_search.error = error;
		self->priv->ongoing_search.folder_id = folder_id;
		self->priv->ongoing_search.folder = folder;
		self->priv->ongoing_search.folder_summary = camel_folder_get_folder_summary (folder);

		do {
			g_clear_pointer (&items, g_ptr_array_unref);
			items = g_ptr_array_new_full (1024, camel_store_search_item_free);

			if (!camel_store_search_acquire_folder (folder, error)) {
				success = FALSE;
				break;
			}

			success = camel_db_exec_select (cdb, stmt->str, camel_store_search_read_items_cb, items, error);

			camel_store_search_release_folder (folder);

			if (!success || !self->priv->ongoing_search.success)
				break;
		} while (camel_store_search_handle_remote_ops_sync (self, &success, cancellable, error));

		g_ptr_array_extend_and_steal (*out_items, g_steal_pointer (&items));

		search_cache_clear (self->priv->ongoing_search.search_body);

		if (!success || !self->priv->ongoing_search.success)
			break;
	}

	g_string_free (stmt, TRUE);

	if (success)
		success = self->priv->ongoing_search.success;
	camel_store_search_clear_ongoing_search_data (self);

	if (!success)
		g_clear_pointer (out_items, g_ptr_array_unref);

	return success;
}

static gboolean
camel_store_search_read_uids_cb (gpointer user_data,
				 gint ncol,
				 gchar **colvalues,
				 gchar **colnames)
{
	GPtrArray *uids = user_data;

	g_return_val_if_fail (uids != NULL, FALSE);
	g_return_val_if_fail (ncol == 1, FALSE);

	if (colvalues[0] && *(colvalues[0]))
		g_ptr_array_add (uids, (gpointer) camel_pstring_strdup (colvalues[0]));

	return TRUE;
}

/**
 * camel_store_search_get_uids_sync:
 * @self: a #CamelStoreSearch
 * @folder_name: name of the folder to read UID-s from
 * @out_uids: (out) (transfer container) (element-type utf8): an output
 *    argument for the read message UID-s
 * @cancellable: a #GCancellable, or #NULL
 * @error: return location for a #GError, or %NULL
 *
 * Reads all the message UID-s from the folder @folder_name satisfying
 * the set expression. Sets the @out_uids to %NULL and returns %TRUE
 * when the @folder_name is not part of the @self.
 *
 * Free the @out_uids array with g_ptr_array_unref(), when no longer needed.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_store_search_get_uids_sync (CamelStoreSearch *self,
				  const gchar *folder_name,
				  GPtrArray **out_uids, /* gchar * */
				  GCancellable *cancellable,
				  GError **error)
{
	CamelFolder *folder;
	guint32 folder_id;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (out_uids != NULL, FALSE);

	if (self->priv->needs_rebuild) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED, "Cannot get UID-s: Run rebuild first");
		return FALSE;
	}

	folder_id = camel_store_db_get_folder_id (self->priv->store_db, folder_name);
	if (!folder_id || (folder = g_hash_table_lookup (self->priv->folders_by_id, GUINT_TO_POINTER (folder_id))) == NULL) {
		*out_uids = NULL;

		return TRUE;
	}

	*out_uids = g_ptr_array_new_full (128, (GDestroyNotify) camel_pstring_free);

	g_object_ref (folder);

	/* read data from the index, if available */
	if (self->priv->result_index) {
		GHashTableIter iter;
		gpointer key = NULL;

		g_hash_table_iter_init (&iter, (GHashTable *) self->priv->result_index);
		while (g_hash_table_iter_next (&iter, &key, NULL)) {
			SearchIndexData *index_data = key;

			if (index_data && index_data->store == self->priv->store && index_data->folder_id == folder_id)
				g_ptr_array_add (*out_uids, (gpointer) camel_pstring_strdup (index_data->uid));
		}

		success = TRUE;
	} else {
		CamelDB *cdb;
		GString *stmt;
		GPtrArray *uids = NULL;

		cdb = CAMEL_DB (self->priv->store_db);
		stmt = g_string_new ("SELECT uid");
		g_string_append_printf (stmt, " FROM messages_%u WHERE %s", folder_id, self->priv->where_clause_sql);

		camel_store_search_clear_ongoing_search_data (self);
		self->priv->ongoing_search.cancellable = cancellable;
		self->priv->ongoing_search.error = error;
		self->priv->ongoing_search.folder_id = folder_id;
		self->priv->ongoing_search.folder = folder;
		self->priv->ongoing_search.folder_summary = camel_folder_get_folder_summary (folder);

		do {
			g_clear_pointer (&uids, g_ptr_array_unref);
			uids = g_ptr_array_new_full (128, (GDestroyNotify) camel_pstring_free);

			if (!camel_store_search_acquire_folder (folder, error)) {
				success = FALSE;
				break;
			}

			success = camel_db_exec_select (cdb, stmt->str, camel_store_search_read_uids_cb, uids, error);

			camel_store_search_release_folder (folder);

			if (!success || !self->priv->ongoing_search.success)
				break;
		} while (camel_store_search_handle_remote_ops_sync (self, &success, cancellable, error));

		g_ptr_array_extend_and_steal (*out_uids, g_steal_pointer (&uids));

		g_string_free (stmt, TRUE);

		if (success)
			success = self->priv->ongoing_search.success;
		camel_store_search_clear_ongoing_search_data (self);
	}

	if (!success)
		g_clear_pointer (out_uids, g_ptr_array_unref);
	g_object_unref (folder);

	return success;
}

/**
 * camel_store_search_get_match_threads_kind:
 * @self: a #CamelStoreSearch
 * @out_flags: (out): bit-or of #CamelFolderThreadFlags
 *
 * Gets the kind of the 'match-threads' search statement of the expression
 * as recognized by the last camel_store_search_rebuild_sync() call, as one
 * of the #CamelMatchThreadsKind values.
 *
 * When it's other than %CAMEL_MATCH_THREADS_KIND_NONE, the caller should gather
 * thread-related data with camel_store_search_add_match_threads_items_sync(),
 * after which the caller filters the output to a #CamelStoreSearchIndex, which
 * can be set back with the camel_store_search_set_result_index().
 *
 * Returns: one of the #CamelMatchThreadsKind constants, referencing the requested match-threads search
 *
 * Since: 3.58
 **/
CamelMatchThreadsKind
camel_store_search_get_match_threads_kind (CamelStoreSearch *self,
					   CamelFolderThreadFlags *out_flags)
{
	CamelMatchThreadsKind kind;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), CAMEL_MATCH_THREADS_KIND_NONE);
	g_return_val_if_fail (out_flags != NULL, CAMEL_MATCH_THREADS_KIND_NONE);

	kind = self->priv->match_threads_kind;
	if (kind == CAMEL_MATCH_THREADS_KIND_NONE)
		*out_flags = CAMEL_FOLDER_THREAD_FLAG_NONE;
	else
		*out_flags = self->priv->match_threads_flags;

	return kind;
}

typedef struct _MatchThreadsItemsData {
	CamelStore *store;
	GPtrArray **inout_items;
	GCancellable *cancellable;
	guint32 folder_id;
} MatchThreadsItemsData;

static gboolean
store_search_add_match_threads_items_cb (gpointer user_data,
					 gint ncol,
					 gchar **colvalues,
					 gchar **colnames)
{
	MatchThreadsItemsData *mti = user_data;

	g_return_val_if_fail (ncol == 3, FALSE);

	if (g_cancellable_is_cancelled (mti->cancellable))
		return FALSE;

	if (colvalues[1]) {
		CamelStoreSearchThreadItem * item;

		if (!*mti->inout_items)
			*mti->inout_items = g_ptr_array_new_with_free_func (camel_store_search_thread_item_free);

		item = camel_store_search_thread_item_new (mti->store, mti->folder_id, colvalues[0], colvalues[1], colvalues[2]);
		g_ptr_array_add (*mti->inout_items, item);
	}

	return TRUE;
}

/**
 * camel_store_search_add_match_threads_items_sync:
 * @self: a #CamelStoreSearch
 * @inout_items: (inout) (element-type CamelStoreSearchThreadItem) (transfer container): a #GPtrArray to add the items to
 * @cancellable: a #GCancellable, or #NULL
 * @error: return location for a #GError, or %NULL
 *
 * Adds #CamelStoreSearchThreadItem items into the location pointed
 * to by the @inout_items array. When it points to the %NULL, the array
 * is created if needed. Free the array with g_ptr_array_unref(), when
 * no longer needed.
 *
 * See camel_store_search_get_match_threads_kind().
 *
 * Returns: whether succeeded; note the @inout_items can be still set,
 *    even when the call failed
 *
 * Since: 3.58
 **/
gboolean
camel_store_search_add_match_threads_items_sync (CamelStoreSearch *self,
						 GPtrArray **inout_items, /* CamelStoreSearchThreadItem * */
						 GCancellable *cancellable,
						 GError **error)
{
	MatchThreadsItemsData mti = { 0, };
	GString *stmt;
	GHashTableIter iter;
	gpointer key = NULL;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), FALSE);
	g_return_val_if_fail (inout_items != NULL, FALSE);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	if (self->priv->needs_rebuild) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED, "Cannot add match-threads items: Run rebuild first");
		return FALSE;
	}

	if (!g_hash_table_size (self->priv->folders)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Cannot add match-threads items: No folder to search set");
		return FALSE;
	}

	mti.store = self->priv->store;
	mti.inout_items = inout_items;
	mti.cancellable = cancellable;

	stmt = g_string_new ("");

	g_hash_table_iter_init (&iter, self->priv->folders_by_id);
	while (success && g_hash_table_iter_next (&iter, &key, NULL)) {
		guint32 folder_id = GPOINTER_TO_UINT (key);

		if (stmt->len)
			g_string_truncate (stmt, 0);

		g_string_append_printf (stmt, "SELECT uid,subject,part FROM messages_%u", folder_id);

		mti.folder_id = folder_id;

		success = camel_db_exec_select (CAMEL_DB (self->priv->store_db), stmt->str, store_search_add_match_threads_items_cb, &mti, error);
	}

	if (!success && error && !*error && g_cancellable_set_error_if_cancelled (cancellable, error)) {
		/* do nothing here, the error is set now */
	}

	g_string_free (stmt, TRUE);

	return success;
}

/**
 * camel_store_search_ref_result_index:
 * @self: a #CamelStoreSearch
 *
 * Gets a #CamelStoreSearchIndex being used as a search result index,
 * previously set by camel_store_search_set_result_index(), or %NULL,
 * when none is set.
 *
 * Free the returned index with camel_store_search_index_unref(), when
 * no longer needed.
 *
 * Returns: (nullable) (transfer full): a referenced #CamelStoreSearchIndex
 *    used as a result index, or %NULL, when none is set
 *
 * Since: 3.58
 **/
CamelStoreSearchIndex *
camel_store_search_ref_result_index (CamelStoreSearch *self)
{
	CamelStoreSearchIndex *index;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), NULL);

	index = self->priv->result_index;
	if (index)
		camel_store_search_index_ref (index);

	return index;
}

/**
 * camel_store_search_set_result_index:
 * @self: a #CamelStoreSearch
 * @index: (nullable) (transfer none): a #CamelStoreSearchIndex, or %NULL
 *
 * Sets, or unsets, a #CamelStoreSearchIndex to be used for the search.
 * The index contains all the items satisfying the expression.
 *
 * Since: 3.58
 **/
void
camel_store_search_set_result_index (CamelStoreSearch *self,
				     CamelStoreSearchIndex *index)
{
	g_return_if_fail (CAMEL_IS_STORE_SEARCH (self));

	if (self->priv->result_index != index) {
		g_clear_pointer (&self->priv->result_index, camel_store_search_index_unref);

		self->priv->result_index = index;

		if (self->priv->result_index) {
			camel_store_search_index_ref (self->priv->result_index);
		}
	}
}

/**
 * camel_store_search_add_match_index:
 * @self: a #CamelStoreSearch
 * @index: (transfer none): a #CamelStoreSearchIndex to add
 *
 * Adds a match index @index into the @self. It can be referenced
 * in the search expression with 'in-match-index "index_key"'
 * statement, where the index_key is "%p" of the @index.
 *
 * The @self adds its own reference on the @index.
 *
 * Since: 3.58
 **/
void
camel_store_search_add_match_index (CamelStoreSearch *self,
				    CamelStoreSearchIndex *index)
{
	g_return_if_fail (CAMEL_IS_STORE_SEARCH (self));
	g_return_if_fail (index != NULL);

	if (!self->priv->match_indexes)
		self->priv->match_indexes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) camel_store_search_index_unref);

	g_hash_table_insert (self->priv->match_indexes, g_strdup_printf ("%p", index), camel_store_search_index_ref (index));
}

/**
 * camel_store_search_remove_match_index:
 * @self: a #CamelStoreSearch
 * @index: a #CamelStoreSearchIndex to remove
 *
 * Removes the @index from from list of the match indexes. The function
 * does nothing when the @index is not part of the match indexes.
 *
 * Since: 3.58
 **/
void
camel_store_search_remove_match_index (CamelStoreSearch *self,
				       CamelStoreSearchIndex *index)
{
	g_return_if_fail (CAMEL_IS_STORE_SEARCH (self));
	g_return_if_fail (index != NULL);

	if (self->priv->match_indexes) {
		gchar *key = g_strdup_printf ("%p", index);

		if (g_hash_table_remove (self->priv->match_indexes, key) && !g_hash_table_size (self->priv->match_indexes))
			g_clear_pointer (&self->priv->match_indexes,g_hash_table_unref);

		g_free (key);
	}
}

/**
 * camel_store_search_list_match_indexes:
 * @self: a #CamelStoreSearch
 *
 * Lists all the match indexes added in the @self.
 *
 * Returns: (transfer container) (element-type CamelStoreSearchIndex): all
 *    the match indexes added in the @self
 *
 * Since: 3.58
 **/
GPtrArray *
camel_store_search_list_match_indexes (CamelStoreSearch *self)
{
	GPtrArray *indexes;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), NULL);

	if (self->priv->match_indexes) {
		GHashTableIter iter;
		gpointer value = NULL;

		indexes = g_ptr_array_new_full (g_hash_table_size (self->priv->match_indexes), (GDestroyNotify) camel_store_search_index_unref);

		g_hash_table_iter_init (&iter, self->priv->match_indexes);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			g_ptr_array_add (indexes, camel_store_search_index_ref (value));
		}
	} else {
		indexes = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_store_search_index_unref);
	}

	return indexes;
}

typedef enum _EnsureFlags {
	ENSURE_FLAG_LOADED_INFO = 1 << 0,
	ENSURE_FLAG_INFO = 1 << 1,
	ENSURE_FLAG_HEADERS = 1 << 2,
	ENSURE_FLAG_MESSAGE = 1 << 3
} EnsureFlags;

static void
camel_store_search_ensure_ongoing_search (CamelStoreSearch *self,
					  const gchar *uid,
					  guint32 flags)
{
	if (g_strcmp0 (self->priv->ongoing_search.uid, uid) != 0) {
		g_clear_pointer (&self->priv->ongoing_search.uid, camel_pstring_free);
		g_clear_pointer (&self->priv->ongoing_search.headers, camel_name_value_array_free);
		g_clear_object (&self->priv->ongoing_search.info);
		g_clear_object (&self->priv->ongoing_search.message);

		self->priv->ongoing_search.uid = camel_pstring_strdup (uid);
	}

	if ((flags & (ENSURE_FLAG_INFO | ENSURE_FLAG_LOADED_INFO)) != 0 && !self->priv->ongoing_search.info && self->priv->ongoing_search.folder_summary) {
		self->priv->ongoing_search.info = camel_folder_summary_peek_loaded (self->priv->ongoing_search.folder_summary, uid);
	}

	if ((flags & ENSURE_FLAG_INFO) != 0 && !self->priv->ongoing_search.info && self->priv->ongoing_search.folder) {
		self->priv->ongoing_search.info = camel_folder_get_message_info (self->priv->ongoing_search.folder, uid);
	}

	if ((flags & ENSURE_FLAG_HEADERS) != 0 && !self->priv->ongoing_search.headers && self->priv->ongoing_search.folder) {
		camel_folder_dup_headers_sync (self->priv->ongoing_search.folder, uid, &self->priv->ongoing_search.headers,
			self->priv->ongoing_search.cancellable, NULL);
	}

	if ((flags & ENSURE_FLAG_MESSAGE) != 0 && !self->priv->ongoing_search.message && self->priv->ongoing_search.folder) {
		CamelMimeMessage *message;

		message = camel_folder_get_message_cached (self->priv->ongoing_search.folder, uid, self->priv->ongoing_search.cancellable);
		if (!message) {
			GError *local_error = NULL;

			message = camel_folder_get_message_sync (self->priv->ongoing_search.folder, uid, self->priv->ongoing_search.cancellable, &local_error);

			if (local_error && !g_error_matches (local_error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID_UID)) {
				self->priv->ongoing_search.success = FALSE;
				g_propagate_error (self->priv->ongoing_search.error, local_error);
			} else {
				g_clear_error (&local_error);
			}
		}

		self->priv->ongoing_search.message = message;
	}
}

static void
camel_store_search_ensure_in_todo_ops (CamelStoreSearch *self,
				       const gchar *uid,
				       SearchOp *tmp_op)
{
	GPtrArray *todo_array;
	SearchOp *op;

	if (!self->priv->ongoing_search.search_ops_pool)
		self->priv->ongoing_search.search_ops_pool = g_hash_table_new_full (search_op_hash, search_op_equal, search_op_free, NULL);

	op = g_hash_table_lookup (self->priv->ongoing_search.search_ops_pool, tmp_op);
	if (!op) {
		op = search_op_new (tmp_op->cmp_kind, tmp_op->needle, tmp_op->header_name);
		op->run_sync = tmp_op->run_sync;

		g_hash_table_insert (self->priv->ongoing_search.search_ops_pool, op, op);
	}

	if (!self->priv->ongoing_search.todo_search_ops_by_uid) {
		self->priv->ongoing_search.todo_search_ops_by_uid = g_hash_table_new_full (g_str_hash, g_str_equal,
			(GDestroyNotify) camel_pstring_free, (GDestroyNotify) g_ptr_array_unref);
	}

	todo_array = g_hash_table_lookup (self->priv->ongoing_search.todo_search_ops_by_uid, uid);
	if (!todo_array) {
		todo_array = g_ptr_array_new (); /* SearchOp-s only borrowed from the search_ops_pool */
		g_hash_table_insert (self->priv->ongoing_search.todo_search_ops_by_uid, (gpointer) camel_pstring_strdup (uid), todo_array);
	}

	g_ptr_array_add (todo_array, op);
}

static gboolean
camel_store_search_search_body_run_sync (CamelStoreSearch *self,
					 const SearchOp *op,
					 const gchar *uid,
					 gboolean *out_matches,
					 GCancellable *cancellable,
					 GError **error)
{
	gboolean matches = FALSE;
	gboolean is_regex;

	if (!op->words || !op->words->len) {
		*out_matches = TRUE;
		return TRUE;
	}

	*out_matches = FALSE;

	is_regex = op->cmp_kind == CMP_BODY_REGEX;

	camel_store_search_ensure_ongoing_search (self, uid, ENSURE_FLAG_INFO);

	/* check whether the preview can match, to avoid message download */
	if (self->priv->ongoing_search.info) {
		const gchar *preview;

		camel_message_info_property_lock (self->priv->ongoing_search.info);

		preview = camel_message_info_get_preview (self->priv->ongoing_search.info);
		if (preview && *preview) {
			guint ii;

			matches = TRUE;

			for (ii = 0; matches && op->words && ii < op->words->len; ii++) {
				const gchar *word = g_ptr_array_index (op->words, ii);

				matches = camel_search_header_match (preview, word,
					is_regex ? CAMEL_SEARCH_MATCH_REGEX_SINGLELINE : CAMEL_SEARCH_MATCH_CONTAINS,
					CAMEL_SEARCH_TYPE_ASIS, NULL);
			}
		}

		camel_message_info_property_unlock (self->priv->ongoing_search.info);

		if (matches) {
			*out_matches = matches;
			return TRUE;
		}
	}

	if (!is_regex) {
		SearchCache *cache = self->priv->ongoing_search.search_body;
		guint32 folder_id = self->priv->ongoing_search.folder_id;

		if (!search_cache_has_token_result (cache, op->needle) && self->priv->ongoing_search.folder) {
			GPtrArray *uids = NULL;
			gboolean could_search;

			could_search = camel_folder_search_body_sync (self->priv->ongoing_search.folder, op->words, &uids, cancellable, NULL);
			search_cache_add_token_result (cache, op->needle, folder_id, uids, !could_search);

			g_clear_pointer (&uids, g_ptr_array_unref);
		}

		if (!search_cache_get_token_result_failed (cache, op->needle, folder_id) &&
		    !search_cache_get_token_result_matches (cache, op->needle, folder_id, uid)) {
			return TRUE;
		}

		if (g_cancellable_set_error_if_cancelled (self->priv->ongoing_search.cancellable, self->priv->ongoing_search.error)) {
			self->priv->ongoing_search.success = FALSE;
			return FALSE;
		}
	}

	camel_store_search_ensure_ongoing_search (self, uid, ENSURE_FLAG_MESSAGE);

	if (self->priv->ongoing_search.message) {
		camel_search_flags_t type = CAMEL_SEARCH_MATCH_ICASE;
		regex_t pattern;

		matches = FALSE;

		if (is_regex)
			type |= CAMEL_SEARCH_MATCH_REGEX | CAMEL_SEARCH_MATCH_NEWLINE;

		if (camel_search_build_match_regex_strv (&pattern, CAMEL_SEARCH_MATCH_ICASE,
		    (gint) op->words->len, (const gchar **) op->words->pdata, self->priv->ongoing_search.error)) {
			matches = camel_search_message_body_contains (CAMEL_DATA_WRAPPER (self->priv->ongoing_search.message), &pattern);
			regfree (&pattern);
		} else {
			self->priv->ongoing_search.success = FALSE;
		}

		*out_matches = matches;
	}

	return self->priv->ongoing_search.success;
}

gboolean
_camel_store_search_compare_text (CamelStoreSearch *self,
				  const gchar *uid,
				  const gchar *default_charset,
				  const gchar *header_name,
				  CmpHeaderKind cmp_kind,
				  const gchar *haystack,
				  const gchar *needle)
{
	gboolean matches = FALSE;
	camel_search_match_t how = CAMEL_SEARCH_MATCH_EXACT;
	camel_search_t type;
	gboolean know_kind = TRUE;

	if (camel_search_header_is_address (header_name))
		type = CAMEL_SEARCH_TYPE_ADDRESS;
	else
		type = CAMEL_SEARCH_TYPE_ASIS;

	switch (cmp_kind) {
	case CMP_HEADER_CONTAINS:
		how = CAMEL_SEARCH_MATCH_CONTAINS;
		break;
	case CMP_HEADER_MATCHES:
		how = CAMEL_SEARCH_MATCH_EXACT;
		break;
	case CMP_HEADER_STARTS_WITH:
		how = CAMEL_SEARCH_MATCH_STARTS;
		break;
	case CMP_HEADER_ENDS_WITH:
		how = CAMEL_SEARCH_MATCH_ENDS;
		break;
	case CMP_HEADER_HAS_WORDS:
		how = CAMEL_SEARCH_MATCH_WORD;
		break;
	case CMP_HEADER_SOUNDEX:
		how = CAMEL_SEARCH_MATCH_SOUNDEX;
		break;
	case CMP_HEADER_REGEX:
		how = CAMEL_SEARCH_MATCH_REGEX_SINGLELINE;
		break;
	case CMP_HEADER_FULL_REGEX:
		how = CAMEL_SEARCH_MATCH_REGEX_MULTILINE;
		break;
	case CMP_HEADER_EXISTS:
		return haystack && *haystack;
	default:
		know_kind = FALSE;
		break;
	}

	if (know_kind)
		matches = camel_search_header_match (haystack ? haystack : "", needle ? needle : "", how, type, default_charset);
	else
		g_warning ("%s: Unknown compare kind '%d'", G_STRFUNC, cmp_kind);

	return matches;
}

gboolean
_camel_store_search_search_body (CamelStoreSearch *self,
				 const gchar *uid,
				 CmpBodyKind cmp_kind,
				 const gchar *encoded_words)
{
	SearchOp op_stack = { 0, };
	gboolean contains = FALSE;
	gboolean covered = FALSE;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), FALSE);

	if (!self->priv->ongoing_search.success)
		return FALSE;

	if (!encoded_words || !*encoded_words)
		return TRUE;

	op_stack.cmp_kind = cmp_kind;
	op_stack.needle = (gchar *) encoded_words;
	op_stack.header_name = NULL; /* it's a body search, not a header search */
	op_stack.run_sync = camel_store_search_search_body_run_sync;

	if (self->priv->ongoing_search.done_search_ops) {
		SearchOp *op;
		gpointer value = NULL;

		op = g_hash_table_lookup (self->priv->ongoing_search.done_search_ops, &op_stack);

		if (op && op->results && g_hash_table_lookup_extended (op->results, uid, NULL, &value)) {
			covered = TRUE;
			contains = GPOINTER_TO_INT (value) != 0;
		}
	}

	if (!covered)
		camel_store_search_ensure_in_todo_ops (self, uid, &op_stack);

	return contains;
}

static gboolean
camel_store_search_search_header_run_sync (CamelStoreSearch *self,
					   const SearchOp *op,
					   const gchar *uid,
					   gboolean *out_matches,
					   GCancellable *cancellable,
					   GError **error)
{
	const CamelNameValueArray *headers = NULL;
	gchar *charset = NULL;
	gchar *header_value = NULL;
	gboolean covered = FALSE;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), FALSE);

	if (!self->priv->ongoing_search.success)
		return FALSE;

	*out_matches = FALSE;

	camel_store_search_ensure_ongoing_search (self, uid, ENSURE_FLAG_INFO);

	if (self->priv->ongoing_search.info) {
		camel_message_info_property_lock (self->priv->ongoing_search.info);

		headers = camel_message_info_get_user_headers (self->priv->ongoing_search.info);
		if (headers && op->header_name && *op->header_name) {
			const gchar *value;

			value = camel_name_value_array_get_named (headers, CAMEL_COMPARE_CASE_INSENSITIVE, op->header_name);
			/* cannot catch a case when the header is a user header, but is not set on the message */
			if (value) {
				header_value = camel_search_get_header_decoded (op->header_name, value, NULL);
				covered = TRUE;
			}
		}

		headers = camel_message_info_get_headers (self->priv->ongoing_search.info);
	}

	if (!covered && !headers) {
		if (self->priv->ongoing_search.info)
			camel_message_info_property_unlock (self->priv->ongoing_search.info);

		camel_store_search_ensure_ongoing_search (self, uid, ENSURE_FLAG_HEADERS);

		if (self->priv->ongoing_search.info)
			camel_message_info_property_lock (self->priv->ongoing_search.info);

		headers = self->priv->ongoing_search.headers;
	}

	if (headers)
		charset = g_strdup (camel_search_get_default_charset_from_headers (headers));

	if (!covered && headers) {
		if (op->header_name && *op->header_name) {
			const gchar *value = NULL;

			covered = TRUE;
			value = camel_name_value_array_get_named (headers, CAMEL_COMPARE_CASE_INSENSITIVE, op->header_name);

			if (covered) {
				header_value = camel_search_get_header_decoded (op->header_name, value, charset);
			}
		} else {
			covered = TRUE;
			header_value = camel_search_get_headers_decoded (headers, charset);
		}
	}

	if (self->priv->ongoing_search.info)
		camel_message_info_property_unlock (self->priv->ongoing_search.info);

	if (!covered) {
		camel_store_search_ensure_ongoing_search (self, uid, ENSURE_FLAG_MESSAGE);

		if (self->priv->ongoing_search.message) {
			if (op->header_name && *op->header_name) {
				const gchar *value;

				headers = camel_medium_get_headers (CAMEL_MEDIUM (self->priv->ongoing_search.message));
				value = camel_name_value_array_get_named (headers, CAMEL_COMPARE_CASE_INSENSITIVE, op->header_name);

				if (value && *value) {
					header_value = camel_search_get_header_decoded (op->header_name, value,
						camel_search_get_default_charset_from_headers (headers));
				}
			} else {
				header_value = camel_search_get_all_headers_decoded (self->priv->ongoing_search.message);
			}

			if (!charset) {
				headers = camel_medium_get_headers (CAMEL_MEDIUM (self->priv->ongoing_search.message));
				charset = headers ? g_strdup (camel_search_get_default_charset_from_headers (headers)) : NULL;
			}
		}
	}

	if (header_value) {
		*out_matches = _camel_store_search_compare_text (self, uid, charset, op->header_name, op->cmp_kind, header_value, op->needle);

		g_free (header_value);
	}

	g_free (charset);

	return self->priv->ongoing_search.success;
}

gboolean
_camel_store_search_search_header (CamelStoreSearch *self,
				   const gchar *uid,
				   const gchar *header_name,
				   CmpHeaderKind cmp_kind,
				   const gchar *needle,
				   const gchar *db_value)
{
	SearchOp op_stack = { 0, };
	gboolean matches = FALSE;
	gboolean covered = FALSE;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), FALSE);

	if (!self->priv->ongoing_search.success)
		return FALSE;

	if (cmp_kind != CMP_HEADER_EXISTS && (!needle || !*needle))
		return TRUE;

	if (header_name && camel_store_db_util_get_column_for_header_name (header_name))
		return _camel_store_search_compare_text (self, uid, NULL, header_name, cmp_kind, db_value ? db_value : "", needle);

	op_stack.cmp_kind = cmp_kind;
	op_stack.needle = (gchar *) needle;
	op_stack.header_name = (gchar *) (header_name ? header_name : "");
	op_stack.run_sync = camel_store_search_search_header_run_sync;

	if (self->priv->ongoing_search.done_search_ops) {
		SearchOp *op;
		gpointer value = NULL;

		op = g_hash_table_lookup (self->priv->ongoing_search.done_search_ops, &op_stack);

		if (op && op->results && g_hash_table_lookup_extended (op->results, uid, NULL, &value)) {
			covered = TRUE;
			matches = GPOINTER_TO_INT (value) != 0;
		}
	}

	if (!covered)
		camel_store_search_ensure_in_todo_ops (self, uid, &op_stack);

	return matches;
}

gchar *
_camel_store_search_dup_user_tag (CamelStoreSearch *self,
				  const gchar *uid,
				  const gchar *tag_name,
				  const gchar *dbvalue)
{
	gchar *value = NULL;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), NULL);

	if (!self->priv->ongoing_search.success)
		return NULL;

	/* do not load it, use the in-memory info only if loaded, otherwise use the db value */
	camel_store_search_ensure_ongoing_search (self, uid, ENSURE_FLAG_LOADED_INFO);

	if (self->priv->ongoing_search.info) {
		camel_message_info_property_lock (self->priv->ongoing_search.info);
		value = camel_message_info_dup_user_tag (self->priv->ongoing_search.info, tag_name);
		camel_message_info_property_unlock (self->priv->ongoing_search.info);
	} else {
		/* parse the string only if it looks like the tag is there */
		if (tag_name && *tag_name && dbvalue && *dbvalue && camel_strstrcase (dbvalue, tag_name)) {
			gchar *part = (gchar *) dbvalue;
			gint ii, count;

			count = camel_util_bdata_get_number (&part, 0);

			for (ii = 0; ii < count && !value; ii++) {
				gchar *nname, *nvalue;

				nname = camel_util_bdata_get_string (&part, NULL);
				nvalue = camel_util_bdata_get_string (&part, NULL);

				if (nname && g_ascii_strcasecmp (nname, tag_name) == 0)
					value = g_steal_pointer (&nvalue);

				g_free (nname);
				g_free (nvalue);
			}
		}
	}

	return value;
}

gchar *
_camel_store_search_from_loaded_info_or_db (CamelStoreSearch *self,
					    const gchar *uid,
					    const gchar *column_name,
					    const gchar *dbvalue)
{
	gchar *value = NULL;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), NULL);

	if (!self->priv->ongoing_search.success)
		return NULL;

	/* do not load it, use the in-memory info only if loaded, otherwise use the db value */
	camel_store_search_ensure_ongoing_search (self, uid, ENSURE_FLAG_LOADED_INFO);

	if (self->priv->ongoing_search.info) {
		camel_message_info_property_lock (self->priv->ongoing_search.info);

		if (g_ascii_strcasecmp (column_name, "subject") == 0)
			value = g_strdup (camel_message_info_get_subject (self->priv->ongoing_search.info));
		else if (g_ascii_strcasecmp (column_name, "mail_from") == 0)
			value = g_strdup (camel_message_info_get_from (self->priv->ongoing_search.info));
		else if (g_ascii_strcasecmp (column_name, "mail_cc") == 0)
			value = g_strdup (camel_message_info_get_cc (self->priv->ongoing_search.info));
		else if (g_ascii_strcasecmp (column_name, "mail_to") == 0)
			value = g_strdup (camel_message_info_get_to (self->priv->ongoing_search.info));
		else if (g_ascii_strcasecmp (column_name, "usertags") == 0)
			value = camel_message_info_encode_user_tags (self->priv->ongoing_search.info);
		else if (g_ascii_strcasecmp (column_name, "labels") == 0)
			value = camel_message_info_encode_user_flags (self->priv->ongoing_search.info);
		else if (g_ascii_strcasecmp (column_name, "mlist") == 0)
			value = g_strdup (camel_message_info_get_mlist (self->priv->ongoing_search.info));

		camel_message_info_property_unlock (self->priv->ongoing_search.info);
	} else {
		value = g_strdup (dbvalue);
	}

	return value;
}

gboolean
_camel_store_search_addressbook_contains (CamelStoreSearch *self,
					  const gchar *book_uid,
					  const gchar *email)
{
	AddrBookOpKey heap_op = { 0, };
	gboolean covered = FALSE;
	gboolean contains = FALSE;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), FALSE);

	if (!email || !*email)
		return FALSE;

	heap_op.book_uid = (gchar *) book_uid;
	heap_op.email = (gchar *) email;

	if (self->priv->ongoing_search.done_addr_book) {
		gpointer value = NULL;

		if (g_hash_table_lookup_extended (self->priv->ongoing_search.done_addr_book, &heap_op, NULL, &value)) {
			covered = TRUE;
			contains = GPOINTER_TO_INT (value) != 0;
		}
	}

	if (!covered) {
		if (!self->priv->ongoing_search.todo_addr_book)
			self->priv->ongoing_search.todo_addr_book = g_hash_table_new_full (addr_book_op_key_hash, addr_book_op_key_equal, addr_book_op_key_free, NULL);
		if (!g_hash_table_contains (self->priv->ongoing_search.todo_addr_book, &heap_op))
			g_hash_table_add (self->priv->ongoing_search.todo_addr_book, addr_book_op_key_new (book_uid, email));
		/* it may or may not do more parts of the SELECT; guess it's checked for existence, rather than non-existence */
		contains = TRUE;
	}

	return contains;
}

gboolean
_camel_store_search_check_labels (CamelStoreSearch *self,
				  const gchar *uid,
				  const gchar *label_to_check,
				  const gchar *dbvalue)
{
	gboolean contains = FALSE;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), FALSE);

	if (!self->priv->ongoing_search.success || !label_to_check || !*label_to_check)
		return FALSE;

	/* do not load it, use the in-memory info only if loaded, otherwise use the db value */
	camel_store_search_ensure_ongoing_search (self, uid, ENSURE_FLAG_LOADED_INFO);

	if (self->priv->ongoing_search.info) {
		contains = camel_message_info_get_user_flag (self->priv->ongoing_search.info, label_to_check);
	} else {
		if (dbvalue && *dbvalue) {
			/* checks whether string 'where' contains whole word 'what',
			 * case insensitively (ASCII, not utf8) */
			const gchar *where = dbvalue;
			const gchar *what = label_to_check;

			if (what && where && !*what) {
				contains = TRUE;
			} else if (what && where) {
				gboolean word = TRUE;
				gint i, j;

				for (i = 0, j = 0; where[i] && !contains; i++) {
					gchar c = where[i];

					if (c == ' ') {
						word = TRUE;
						j = 0;
					} else if (word && g_ascii_tolower (c) == g_ascii_tolower (what[j])) {
						j++;
						if (what[j] == 0 && (where[i + 1] == 0 || g_ascii_isspace (where[i + 1])))
							contains = TRUE;
					} else {
						word = FALSE;
					}
				}
			}
		}
	}

	return contains;
}

gboolean
_camel_store_search_check_flags (CamelStoreSearch *self,
				 const gchar *uid,
				 guint32 flags_to_check,
				 guint32 dbvalue)
{
	gboolean contains = FALSE;

	g_return_val_if_fail (CAMEL_IS_STORE_SEARCH (self), FALSE);

	if (!self->priv->ongoing_search.success)
		return FALSE;

	/* do not load it, use the in-memory info only if loaded, otherwise use the db value */
	camel_store_search_ensure_ongoing_search (self, uid, ENSURE_FLAG_LOADED_INFO);

	if (self->priv->ongoing_search.info)
		contains = (camel_message_info_get_flags (self->priv->ongoing_search.info) & flags_to_check) != 0;
	else
		contains = (dbvalue & flags_to_check) != 0;

	return contains;
}

gboolean
_camel_store_search_in_result_index (CamelStoreSearch *self,
				     const gchar *uid)
{
	gboolean matches = FALSE;

	g_warn_if_fail (self->priv->result_index != NULL);

	if (self->priv->result_index)
		matches = camel_store_search_index_contains (self->priv->result_index, self->priv->store, self->priv->ongoing_search.folder_id, uid);

	return matches;
}

gboolean
_camel_store_search_in_match_index (CamelStoreSearch *self,
				    const gchar *index_id,
				    const gchar *uid)
{
	gboolean matches = FALSE;

	g_warn_if_fail (self->priv->match_indexes != NULL);

	if (self->priv->match_indexes) {
		CamelStoreSearchIndex *match_index;

		match_index = g_hash_table_lookup (self->priv->match_indexes, index_id);
		g_warn_if_fail (match_index != NULL);
		if (match_index)
			matches = camel_store_search_index_contains (match_index, self->priv->store, self->priv->ongoing_search.folder_id, uid);
	}

	return matches;
}

gboolean
_camel_store_search_is_folder_id (CamelStoreSearch *self,
				  guint32 folder_id)
{
	gboolean matches = FALSE;

	matches = self->priv->ongoing_search.folder_id == folder_id;

	return matches;
}

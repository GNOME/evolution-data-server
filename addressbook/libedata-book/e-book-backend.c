/*
 * e-book-backend.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#include <config.h>

#include <glib/gi18n-lib.h>

#include "e-data-book-view.h"
#include "e-data-book.h"
#include "e-book-backend.h"

#define E_BOOK_BACKEND_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BOOK_BACKEND, EBookBackendPrivate))

typedef struct _AsyncContext AsyncContext;
typedef struct _DispatchNode DispatchNode;

struct _EBookBackendPrivate {
	ESourceRegistry *registry;
	EDataBook *data_book;

	GMutex views_mutex;
	GList *views;

	GMutex property_lock;
	gchar *cache_dir;
	gboolean writable;

	GMutex operation_lock;
	GHashTable *operation_ids;
	GQueue pending_operations;
	guint32 next_operation_id;
	GSimpleAsyncResult *blocked;
};

struct _AsyncContext {
	/* Inputs */
	gchar *uid;
	gchar *query;
	GSList *string_list;

	/* Outputs */
	GQueue result_queue;

	/* One of these should point to result_queue
	 * so any leftover resources can be released. */
	GQueue *object_queue;
	GQueue *string_queue;
};

struct _DispatchNode {
	/* This is the dispatch function
	 * that invokes the class method. */
	GSimpleAsyncThreadFunc dispatch_func;
	gboolean blocking_operation;

	GSimpleAsyncResult *simple;
	GCancellable *cancellable;
};

enum {
	PROP_0,
	PROP_CACHE_DIR,
	PROP_REGISTRY,
	PROP_WRITABLE
};

enum {
	CLOSED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (EBookBackend, e_book_backend, E_TYPE_BACKEND)

static void
async_context_free (AsyncContext *async_context)
{
	GQueue *queue;

	g_free (async_context->uid);
	g_free (async_context->query);

	g_slist_free_full (
		async_context->string_list,
		(GDestroyNotify) g_free);

	queue = async_context->object_queue;
	while (queue != NULL && !g_queue_is_empty (queue))
		g_object_unref (g_queue_pop_head (queue));

	queue = async_context->string_queue;
	while (queue != NULL && !g_queue_is_empty (queue))
		g_free (g_queue_pop_head (queue));

	g_slice_free (AsyncContext, async_context);
}

static void
dispatch_node_free (DispatchNode *dispatch_node)
{
	g_clear_object (&dispatch_node->simple);
	g_clear_object (&dispatch_node->cancellable);

	g_slice_free (DispatchNode, dispatch_node);
}

static void
book_backend_push_operation (EBookBackend *backend,
                             GSimpleAsyncResult *simple,
                             GCancellable *cancellable,
                             gboolean blocking_operation,
                             GSimpleAsyncThreadFunc dispatch_func)
{
	DispatchNode *node;

	g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple));
	g_return_if_fail (dispatch_func != NULL);

	g_mutex_lock (&backend->priv->operation_lock);

	node = g_slice_new0 (DispatchNode);
	node->dispatch_func = dispatch_func;
	node->blocking_operation = blocking_operation;
	node->simple = g_object_ref (simple);

	if (G_IS_CANCELLABLE (cancellable))
		node->cancellable = g_object_ref (cancellable);

	g_queue_push_tail (&backend->priv->pending_operations, node);

	g_mutex_unlock (&backend->priv->operation_lock);
}

static gboolean
book_backend_dispatch_thread (GIOSchedulerJob *job,
                              GCancellable *cancellable,
                              gpointer user_data)
{
	DispatchNode *node = user_data;
	GError *error = NULL;

	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		g_simple_async_result_take_error (node->simple, error);
		g_simple_async_result_complete_in_idle (node->simple);
	} else {
		GAsyncResult *result;
		GObject *source_object;

		result = G_ASYNC_RESULT (node->simple);
		source_object = g_async_result_get_source_object (result);
		node->dispatch_func (node->simple, source_object, cancellable);
		g_object_unref (source_object);
	}

	return FALSE;
}

static gboolean
book_backend_dispatch_next_operation (EBookBackend *backend)
{
	DispatchNode *node;

	g_mutex_lock (&backend->priv->operation_lock);

	/* We can't dispatch additional operations
	 * while a blocking operation is in progress. */
	if (backend->priv->blocked != NULL) {
		g_mutex_unlock (&backend->priv->operation_lock);
		return FALSE;
	}

	/* Pop the next DispatchNode off the queue. */
	node = g_queue_pop_head (&backend->priv->pending_operations);
	if (node == NULL) {
		g_mutex_unlock (&backend->priv->operation_lock);
		return FALSE;
	}

	/* If this a blocking operation, block any
	 * further dispatching until this finishes. */
	if (node->blocking_operation)
		backend->priv->blocked = g_object_ref (node->simple);

	g_mutex_unlock (&backend->priv->operation_lock);

	g_io_scheduler_push_job (
		book_backend_dispatch_thread,
		node, (GDestroyNotify) dispatch_node_free,
		G_PRIORITY_DEFAULT,
		node->cancellable);

	return TRUE;
}

static guint32
book_backend_stash_operation (EBookBackend *backend,
                              GSimpleAsyncResult *simple)
{
	guint32 opid;

	g_mutex_lock (&backend->priv->operation_lock);

	if (backend->priv->next_operation_id == 0)
		backend->priv->next_operation_id = 1;

	opid = backend->priv->next_operation_id++;

	g_hash_table_insert (
		backend->priv->operation_ids,
		GUINT_TO_POINTER (opid),
		g_object_ref (simple));

	g_mutex_unlock (&backend->priv->operation_lock);

	return opid;
}

static GSimpleAsyncResult *
book_backend_claim_operation (EBookBackend *backend,
                              guint32 opid)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (opid > 0, NULL);

	g_mutex_lock (&backend->priv->operation_lock);

	simple = g_hash_table_lookup (
		backend->priv->operation_ids,
		GUINT_TO_POINTER (opid));

	if (simple != NULL) {
		/* Steal the hash table's reference. */
		g_hash_table_steal (
			backend->priv->operation_ids,
			GUINT_TO_POINTER (opid));
	}

	g_mutex_unlock (&backend->priv->operation_lock);

	return simple;
}

static void
book_backend_set_default_cache_dir (EBookBackend *backend)
{
	ESource *source;
	const gchar *user_cache_dir;
	const gchar *uid;
	gchar *filename;

	user_cache_dir = e_get_user_cache_dir ();
	source = e_backend_get_source (E_BACKEND (backend));

	uid = e_source_get_uid (source);
	g_return_if_fail (uid != NULL);

	filename = g_build_filename (
		user_cache_dir, "addressbook", uid, NULL);
	e_book_backend_set_cache_dir (backend, filename);
	g_free (filename);
}

static gchar *
book_backend_get_backend_property (EBookBackend *backend,
                                   const gchar *prop_name)
{
	gchar *prop_value = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_OPENED)) {
		prop_value = g_strdup ("TRUE");

	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_OPENING)) {
		prop_value = g_strdup ("FALSE");

	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_REVISION)) {
		prop_value = g_strdup ("0");

	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_ONLINE)) {
		gboolean online;

		online = e_backend_get_online (E_BACKEND (backend));
		prop_value = g_strdup (online ? "TRUE" : "FALSE");

	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_READONLY)) {
		gboolean readonly;

		readonly = e_book_backend_is_readonly (backend);
		prop_value = g_strdup (readonly ? "TRUE" : "FALSE");

	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CACHE_DIR)) {
		prop_value = e_book_backend_dup_cache_dir (backend);
	}

	return prop_value;
}

static void
book_backend_set_registry (EBookBackend *backend,
                           ESourceRegistry *registry)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (backend->priv->registry == NULL);

	backend->priv->registry = g_object_ref (registry);
}

static void
book_backend_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CACHE_DIR:
			e_book_backend_set_cache_dir (
				E_BOOK_BACKEND (object),
				g_value_get_string (value));
			return;

		case PROP_REGISTRY:
			book_backend_set_registry (
				E_BOOK_BACKEND (object),
				g_value_get_object (value));
			return;

		case PROP_WRITABLE:
			e_book_backend_set_writable (
				E_BOOK_BACKEND (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
book_backend_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CACHE_DIR:
			g_value_take_string (
				value, e_book_backend_dup_cache_dir (
				E_BOOK_BACKEND (object)));
			return;

		case PROP_REGISTRY:
			g_value_set_object (
				value, e_book_backend_get_registry (
				E_BOOK_BACKEND (object)));
			return;

		case PROP_WRITABLE:
			g_value_set_boolean (
				value, e_book_backend_get_writable (
				E_BOOK_BACKEND (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
book_backend_dispose (GObject *object)
{
	EBookBackendPrivate *priv;

	priv = E_BOOK_BACKEND_GET_PRIVATE (object);

	g_clear_object (&priv->registry);
	g_clear_object (&priv->data_book);

	if (priv->views != NULL) {
		g_list_free (priv->views);
		priv->views = NULL;
	}

	g_hash_table_remove_all (priv->operation_ids);

	while (!g_queue_is_empty (&priv->pending_operations))
		g_object_unref (g_queue_pop_head (&priv->pending_operations));

	g_clear_object (&priv->blocked);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_book_backend_parent_class)->dispose (object);
}

static void
book_backend_finalize (GObject *object)
{
	EBookBackendPrivate *priv;

	priv = E_BOOK_BACKEND_GET_PRIVATE (object);

	g_mutex_clear (&priv->views_mutex);
	g_mutex_clear (&priv->property_lock);

	g_free (priv->cache_dir);

	g_mutex_clear (&priv->operation_lock);
	g_hash_table_destroy (priv->operation_ids);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_backend_parent_class)->finalize (object);
}

static void
book_backend_constructed (GObject *object)
{
	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_book_backend_parent_class)->constructed (object);

	/* Initialize the "cache-dir" property. */
	book_backend_set_default_cache_dir (E_BOOK_BACKEND (object));
}

static gboolean
book_backend_authenticate_sync (EBackend *backend,
                                ESourceAuthenticator *auth,
                                GCancellable *cancellable,
                                GError **error)
{
	EBookBackend *book_backend;
	ESourceRegistry *registry;
	ESource *source;

	book_backend = E_BOOK_BACKEND (backend);
	registry = e_book_backend_get_registry (book_backend);
	source = e_backend_get_source (backend);

	return e_source_registry_authenticate_sync (
		registry, source, auth, cancellable, error);
}

static void
book_backend_notify_update (EBookBackend *backend,
                            const EContact *contact)
{
	GList *list, *link;

	list = e_book_backend_list_views (backend);

	for (link = list; link != NULL; link = g_list_next (link)) {
		EDataBookView *view = E_DATA_BOOK_VIEW (link->data);
		e_data_book_view_notify_update (view, contact);
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
}

static void
e_book_backend_class_init (EBookBackendClass *class)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;

	g_type_class_add_private (class, sizeof (EBookBackendPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = book_backend_set_property;
	object_class->get_property = book_backend_get_property;
	object_class->dispose = book_backend_dispose;
	object_class->finalize = book_backend_finalize;
	object_class->constructed = book_backend_constructed;

	backend_class = E_BACKEND_CLASS (class);
	backend_class->authenticate_sync = book_backend_authenticate_sync;

	class->get_backend_property = book_backend_get_backend_property;
	class->notify_update = book_backend_notify_update;

	g_object_class_install_property (
		object_class,
		PROP_CACHE_DIR,
		g_param_spec_string (
			"cache-dir",
			"Cache Dir",
			"The backend's cache directory",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_REGISTRY,
		g_param_spec_object (
			"registry",
			"Registry",
			"Data source registry",
			E_TYPE_SOURCE_REGISTRY,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_WRITABLE,
		g_param_spec_boolean (
			"writable",
			"Writable",
			"Whether the backend will accept changes",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	/**
	 * EBookBackend::closed:
	 * @backend: the #EBookBackend which emitted the signal
	 * @sender: the bus name that invoked the "close" method
	 *
	 * Emitted when a client destroys its #EBookClient for @backend.
	 *
	 * Since: 3.10
	 **/
	signals[CLOSED] = g_signal_new (
		"closed",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EBookBackendClass, closed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		G_TYPE_STRING);
}

static void
e_book_backend_init (EBookBackend *backend)
{
	backend->priv = E_BOOK_BACKEND_GET_PRIVATE (backend);

	backend->priv->views = NULL;
	g_mutex_init (&backend->priv->views_mutex);
	g_mutex_init (&backend->priv->property_lock);
	g_mutex_init (&backend->priv->operation_lock);

	backend->priv->operation_ids = g_hash_table_new_full (
		(GHashFunc) g_direct_hash,
		(GEqualFunc) g_direct_equal,
		(GDestroyNotify) NULL,
		(GDestroyNotify) g_object_unref);
}

/**
 * e_book_backend_get_cache_dir:
 * @backend: an #EBookBackend
 *
 * Returns the cache directory path used by @backend.
 *
 * Returns: the cache directory path
 *
 * Since: 2.32
 **/
const gchar *
e_book_backend_get_cache_dir (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	return backend->priv->cache_dir;
}

/**
 * e_book_backend_dup_cache_dir:
 * @backend: an #EBookBackend
 *
 * Thread-safe variation of e_book_backend_get_cache_dir().
 * Use this function when accessing @backend from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #EBookBackend:cache-dir
 *
 * Since: 3.10
 **/
gchar *
e_book_backend_dup_cache_dir (EBookBackend *backend)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	g_mutex_lock (&backend->priv->property_lock);

	protected = e_book_backend_get_cache_dir (backend);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&backend->priv->property_lock);

	return duplicate;
}

/**
 * e_book_backend_set_cache_dir:
 * @backend: an #EBookBackend
 * @cache_dir: a local cache directory path
 *
 * Sets the cache directory path for use by @backend.
 *
 * Note that #EBookBackend is initialized with a default cache directory
 * path which should suffice for most cases.  Backends should not override
 * the default path without good reason.
 *
 * Since: 2.32
 **/
void
e_book_backend_set_cache_dir (EBookBackend *backend,
                              const gchar *cache_dir)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (cache_dir != NULL);

	g_mutex_lock (&backend->priv->property_lock);

	if (g_strcmp0 (backend->priv->cache_dir, cache_dir) == 0) {
		g_mutex_unlock (&backend->priv->property_lock);
		return;
	}

	g_free (backend->priv->cache_dir);
	backend->priv->cache_dir = g_strdup (cache_dir);

	g_mutex_unlock (&backend->priv->property_lock);

	g_object_notify (G_OBJECT (backend), "cache-dir");
}

/**
 * e_book_backend_ref_data_book:
 * @backend: an #EBookBackend
 *
 * Returns the #EDataBook for @backend.  The #EDataBook is essentially
 * the glue between incoming D-Bus requests and @backend's native API.
 *
 * An #EDataBook should be set only once after @backend is first created.
 * If an #EDataBook has not yet been set, the function returns %NULL.
 *
 * The returned #EDataBook is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: an #EDataBook, or %NULL
 *
 * Since: 3.10
 **/
EDataBook *
e_book_backend_ref_data_book (EBookBackend *backend)
{
	EDataBook *data_book = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	if (backend->priv->data_book != NULL)
		data_book = g_object_ref (backend->priv->data_book);

	return data_book;
}

/**
 * e_book_backend_set_data_book:
 * @backend: an #EBookBackend
 * @data_book: an #EDataBook
 *
 * Sets the #EDataBook for @backend.  The #EDataBook is essentially the
 * glue between incoming D-Bus requests and @backend's native API.
 *
 * An #EDataBook should be set only once after @backend is first created.
 *
 * Since: 3.10
 **/
void
e_book_backend_set_data_book (EBookBackend *backend,
                              EDataBook *data_book)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (data_book));

	/* This should be set only once.  Warn if not. */
	g_warn_if_fail (backend->priv->data_book == NULL);

	backend->priv->data_book = g_object_ref (data_book);
}

/**
 * e_book_backend_get_registry:
 * @backend: an #EBookBackend
 *
 * Returns the data source registry to which #EBackend:source belongs.
 *
 * Returns: an #ESourceRegistry
 *
 * Since: 3.6
 **/
ESourceRegistry *
e_book_backend_get_registry (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	return backend->priv->registry;
}

/**
 * e_book_backend_get_writable:
 * @backend: an #EBookBackend
 *
 * Returns whether @backend will accept changes to its data content.
 *
 * Returns: whether @backend is writable
 *
 * Since: 3.8
 **/
gboolean
e_book_backend_get_writable (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	return backend->priv->writable;
}

/**
 * e_book_backend_set_writable:
 * @backend: an #EBookBackend
 * @writable: whether @backend is writable
 *
 * Sets whether @backend will accept changes to its data content.
 *
 * Since: 3.8
 **/
void
e_book_backend_set_writable (EBookBackend *backend,
                             gboolean writable)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	if (writable == backend->priv->writable)
		return;

	backend->priv->writable = writable;

	g_object_notify (G_OBJECT (backend), "writable");
}

/**
 * e_book_backend_open_sync:
 * @backend: an #EBookBackend
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * "Opens" the @backend.  Opening a backend is something of an outdated
 * concept, but the operation is hanging around for a little while longer.
 * This usually involves some custom initialization logic, and testing of
 * remote authentication if applicable.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.10
 **/
gboolean
e_book_backend_open_sync (EBookBackend *backend,
                          GCancellable *cancellable,
                          GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	closure = e_async_closure_new ();

	e_book_backend_open (
		backend, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_book_backend_open_finish (backend, result, error);

	e_async_closure_free (closure);

	return success;
}

/* Helper for e_book_backend_open() */
static void
book_backend_open_thread (GSimpleAsyncResult *simple,
                          GObject *source_object,
                          GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;

	backend = E_BOOK_BACKEND (source_object);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class->open != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	if (e_book_backend_is_opened (backend)) {
		g_simple_async_result_complete_in_idle (simple);

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, simple);

		class->open (backend, data_book, opid, cancellable, FALSE);
	}

	g_object_unref (data_book);
}

/**
 * e_book_backend_open:
 * @backend: an #EBookBackend
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously "opens" the @backend.  Opening a backend is something of
 * an outdated concept, but the operation is hanging around for a little
 * while longer.  This usually involves some custom initialization logic,
 * and testing of remote authentication if applicable.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_book_backend_open_finish() to get the result of the operation.
 *
 * Since: 3.10
 **/
void
e_book_backend_open (EBookBackend *backend,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
	GSimpleAsyncResult *simple;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	simple = g_simple_async_result_new (
		G_OBJECT (backend), callback,
		user_data, e_book_backend_open);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	book_backend_push_operation (
		backend, simple, cancellable, TRUE,
		book_backend_open_thread);

	book_backend_dispatch_next_operation (backend);

	g_object_unref (simple);
}

/**
 * e_book_backend_open_finish:
 * @backend: an #EBookBackend
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_book_backend_open().
 *
 * If an error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.10
 **/
gboolean
e_book_backend_open_finish (EBookBackend *backend,
                            GAsyncResult *result,
                            GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (backend),
		e_book_backend_open), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* This operation blocks, so we need to let waiting operations
	 * through.  (FIXME Centralize this for any blocking operation.) */
	g_mutex_lock (&backend->priv->operation_lock);
	if (backend->priv->blocked == simple)
		g_clear_object (&backend->priv->blocked);
	g_mutex_unlock (&backend->priv->operation_lock);
	while (book_backend_dispatch_next_operation (backend))
		;

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/**
 * e_book_backend_refresh_sync:
 * @backend: an #EBookBackend
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Initiates a refresh for @backend, if the @backend supports refreshing.
 * The actual refresh operation completes on its own time.  This function
 * merely initiates the operation.
 *
 * If an error occurs while initiating the refresh, the function will set
 * @error and return %FALSE.  If the @backend does not support refreshing,
 * the function will set an %E_CLIENT_ERROR_NOT_SUPPORTED error and return
 * %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.10
 **/
gboolean
e_book_backend_refresh_sync (EBookBackend *backend,
                             GCancellable *cancellable,
                             GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	closure = e_async_closure_new ();

	e_book_backend_refresh (
		backend, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_book_backend_refresh_finish (backend, result, error);

	e_async_closure_free (closure);

	return success;
}

/* Helper for e_book_backend_refresh() */
static void
book_backend_refresh_thread (GSimpleAsyncResult *simple,
                             GObject *source_object,
                             GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;

	backend = E_BOOK_BACKEND (source_object);

	class = E_BOOK_BACKEND_GET_CLASS (backend);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	if (class->refresh == NULL) {
		g_simple_async_result_set_error (
			simple, E_CLIENT_ERROR,
			E_CLIENT_ERROR_NOT_SUPPORTED,
			"%s", e_client_error_to_string (
			E_CLIENT_ERROR_NOT_SUPPORTED));
		g_simple_async_result_complete_in_idle (simple);

	} else if (!e_book_backend_is_opened (backend)) {
		g_simple_async_result_set_error (
			simple, E_CLIENT_ERROR,
			E_CLIENT_ERROR_NOT_OPENED,
			"%s", e_client_error_to_string (
			E_CLIENT_ERROR_NOT_OPENED));
		g_simple_async_result_complete_in_idle (simple);

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, simple);

		class->refresh (backend, data_book, opid, cancellable);
	}

	g_object_unref (data_book);
}

/**
 * e_book_backend_refresh:
 * @backend: an #EBookBackend
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously initiates a refresh for @backend, if the @backend supports
 * refreshing.  The actual refresh operation completes on its own time.  This
 * function, along with e_book_backend_refresh_finish(), merely initiates the
 * operation.
 *
 * Once the refresh is initiated, @callback will be called.  You can then
 * call e_book_backend_refresh_finish() to get the result of the initiation.
 *
 * Since: 3.10
 **/
void
e_book_backend_refresh (EBookBackend *backend,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	GSimpleAsyncResult *simple;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	simple = g_simple_async_result_new (
		G_OBJECT (backend), callback,
		user_data, e_book_backend_refresh);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	book_backend_push_operation (
		backend, simple, cancellable, FALSE,
		book_backend_refresh_thread);

	book_backend_dispatch_next_operation (backend);

	g_object_unref (simple);
}

/**
 * e_book_backend_refresh_finish:
 * @backend: an #EBookBackend
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the refresh initiation started with e_book_backend_refresh().
 *
 * If an error occurred while initiating the refresh, the function will set
 * @error and return %FALSE.  If the @backend does not support refreshing,
 * the function will set an %E_CLIENT_ERROR_NOT_SUPPORTED error and return
 * %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.10
 **/
gboolean
e_book_backend_refresh_finish (EBookBackend *backend,
                               GAsyncResult *result,
                               GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (backend),
		e_book_backend_refresh), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/**
 * e_book_backend_create_contacts_sync:
 * @backend: an #EBookBackend
 * @vcards: a %NULL-terminated array of vCard strings
 * @out_contacts: a #GQueue in which to deposit results
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates one or more new contacts from @vcards, and deposits an #EContact
 * instance for each newly-created contact in @out_contacts.
 *
 * The returned #EContact instances are referenced for thread-safety and
 * must be unreferenced with g_object_unref() when finished with them.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.10
 **/
gboolean
e_book_backend_create_contacts_sync (EBookBackend *backend,
                                     const gchar * const *vcards,
                                     GQueue *out_contacts,
                                     GCancellable *cancellable,
                                     GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);
	g_return_val_if_fail (vcards != NULL, FALSE);
	g_return_val_if_fail (out_contacts != NULL, FALSE);

	closure = e_async_closure_new ();

	e_book_backend_create_contacts (
		backend, vcards, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_book_backend_create_contacts_finish (
		backend, result, out_contacts, error);

	e_async_closure_free (closure);

	return success;
}

/* Helper for e_book_backend_create_contacts() */
static void
book_backend_create_contacts_thread (GSimpleAsyncResult *simple,
                                     GObject *source_object,
                                     GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;
	AsyncContext *async_context;

	backend = E_BOOK_BACKEND (source_object);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class->create_contacts != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (!e_book_backend_is_opened (backend)) {
		g_simple_async_result_set_error (
			simple, E_DATA_BOOK_ERROR,
			E_CLIENT_ERROR_NOT_OPENED,
			"%s", e_client_error_to_string (
			E_CLIENT_ERROR_NOT_OPENED));
		g_simple_async_result_complete_in_idle (simple);

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, simple);

		class->create_contacts (
			backend, data_book, opid, cancellable,
			async_context->string_list);
	}

	g_object_unref (data_book);
}

/**
 * e_book_backend_create_contacts
 * @backend: an #EBookBackend
 * @vcards: a %NULL-terminated array of vCard strings
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously creates one or more new contacts from @vcards.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_book_backend_create_contacts_finish() to get the result of the
 * operation.
 *
 * Since: 3.10
 **/
void
e_book_backend_create_contacts (EBookBackend *backend,
                                const gchar * const *vcards,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	GSList *list = NULL;
	gint ii;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (vcards != NULL);

	for (ii = 0; vcards[ii] != NULL; ii++)
		list = g_slist_prepend (list, g_strdup (vcards[ii]));

	async_context = g_slice_new0 (AsyncContext);
	async_context->string_list = g_slist_reverse (list);
	async_context->object_queue = &async_context->result_queue;

	simple = g_simple_async_result_new (
		G_OBJECT (backend), callback, user_data,
		e_book_backend_create_contacts);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	book_backend_push_operation (
		backend, simple, cancellable, FALSE,
		book_backend_create_contacts_thread);

	book_backend_dispatch_next_operation (backend);

	g_object_unref (simple);
}

/**
 * e_book_backend_create_contacts_finish:
 * @backend: an #EBookBackend
 * @result: a #GAsyncResult
 * @out_contacts: a #GQueue in which to deposit results
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_book_backend_create_contacts().
 *
 * An #EContact instance for each newly-created contact is deposited in
 * @out_contacts.  The returned #EContact instances are referenced for
 * thread-safety and must be unreferenced with g_object_unref() when
 * finished with them.
 *
 * If an error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.10
 **/
gboolean
e_book_backend_create_contacts_finish (EBookBackend *backend,
                                       GAsyncResult *result,
                                       GQueue *out_contacts,
                                       GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (backend),
		e_book_backend_create_contacts), FALSE);
	g_return_val_if_fail (out_contacts != NULL, FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	while (!g_queue_is_empty (&async_context->result_queue)) {
		EContact *contact;

		contact = g_queue_pop_head (&async_context->result_queue);
		g_queue_push_tail (out_contacts, g_object_ref (contact));
		e_book_backend_notify_update (backend, contact);
		g_object_unref (contact);
	}

	e_book_backend_notify_complete (backend);

	return TRUE;
}

/**
 * e_book_backend_modify_contacts_sync:
 * @backend: an #EBookBackend
 * @vcards: a %NULL-terminated array of vCard strings
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Modifies one or more contacts according to @vcards.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.10
 **/
gboolean
e_book_backend_modify_contacts_sync (EBookBackend *backend,
                                     const gchar * const *vcards,
                                     GCancellable *cancellable,
                                     GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	closure = e_async_closure_new ();

	e_book_backend_modify_contacts (
		backend, vcards, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_book_backend_modify_contacts_finish (
		backend, result, error);

	e_async_closure_free (closure);

	return success;
}

/* Helper for e_book_backend_modify_contacts() */
static void
book_backend_modify_contacts_thread (GSimpleAsyncResult *simple,
                                     GObject *source_object,
                                     GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;
	AsyncContext *async_context;

	backend = E_BOOK_BACKEND (source_object);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class->modify_contacts != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (!e_book_backend_is_opened (backend)) {
		g_simple_async_result_set_error (
			simple, E_CLIENT_ERROR,
			E_CLIENT_ERROR_NOT_OPENED,
			"%s", e_client_error_to_string (
			E_CLIENT_ERROR_NOT_OPENED));
		g_simple_async_result_complete_in_idle (simple);

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, simple);

		class->modify_contacts (
			backend, data_book, opid, cancellable,
			async_context->string_list);
	}

	g_object_unref (data_book);
}

/**
 * e_book_backend_modify_contacts:
 * @backend: an #EBookBackend
 * @vcards: a %NULL-terminated array of vCard strings
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously modifies one or more contacts according to @vcards.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_book_backend_modify_contacts_finish() to get the result of the
 * operation.
 *
 * Since: 3.10
 **/
void
e_book_backend_modify_contacts (EBookBackend *backend,
                                const gchar * const *vcards,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	GSList *list = NULL;
	gint ii;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (vcards != NULL);

	for (ii = 0; vcards[ii] != NULL; ii++)
		list = g_slist_prepend (list, g_strdup (vcards[ii]));

	async_context = g_slice_new0 (AsyncContext);
	async_context->string_list = g_slist_reverse (list);
	async_context->object_queue = &async_context->result_queue;

	simple = g_simple_async_result_new (
		G_OBJECT (backend), callback, user_data,
		e_book_backend_modify_contacts);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	book_backend_push_operation (
		backend, simple, cancellable, FALSE,
		book_backend_modify_contacts_thread);

	book_backend_dispatch_next_operation (backend);

	g_object_unref (simple);
}

/**
 * e_book_backend_modify_contacts_finish:
 * @backend: an #EBookBackend
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_book_backend_modify_contacts().
 *
 * If an error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.10
 **/
gboolean
e_book_backend_modify_contacts_finish (EBookBackend *backend,
                                       GAsyncResult *result,
                                       GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (backend),
		e_book_backend_modify_contacts), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	while (!g_queue_is_empty (&async_context->result_queue)) {
		EContact *contact;

		contact = g_queue_pop_head (&async_context->result_queue);
		e_book_backend_notify_update (backend, contact);
		g_object_unref (contact);
	}

	e_book_backend_notify_complete (backend);

	return TRUE;
}

/**
 * e_book_backend_remove_contacts_sync:
 * @backend: an #EBookBackend
 * @uids: a %NULL-terminated array of contact ID strings
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Removes one or more contacts according to @uids.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.10
 **/
gboolean
e_book_backend_remove_contacts_sync (EBookBackend *backend,
                                     const gchar * const *uids,
                                     GCancellable *cancellable,
                                     GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	closure = e_async_closure_new ();

	e_book_backend_remove_contacts (
		backend, uids, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_book_backend_remove_contacts_finish (
		backend, result, error);

	e_async_closure_free (closure);

	return success;
}

/* Helper for e_book_backend_remove_contacts() */
static void
book_backend_remove_contacts_thread (GSimpleAsyncResult *simple,
                                     GObject *source_object,
                                     GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;
	AsyncContext *async_context;

	backend = E_BOOK_BACKEND (source_object);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class->remove_contacts != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (!e_book_backend_is_opened (backend)) {
		g_simple_async_result_set_error (
			simple, E_CLIENT_ERROR,
			E_CLIENT_ERROR_NOT_OPENED,
			"%s", e_client_error_to_string (
			E_CLIENT_ERROR_NOT_OPENED));
		g_simple_async_result_complete_in_idle (simple);

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, simple);

		class->remove_contacts (
			backend, data_book, opid, cancellable,
			async_context->string_list);
	}

	g_object_unref (data_book);
}

/**
 * e_book_backend_remove_contacts:
 * @backend: an #EBookBackend
 * @uids: a %NULL-terminated array of contact ID strings
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously removes one or more contacts according to @uids.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_book_backend_remove_contacts_finish() to get the result of the
 * operation.
 *
 * Since: 3.10
 **/
void
e_book_backend_remove_contacts (EBookBackend *backend,
                                const gchar * const *uids,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	GSList *list = NULL;
	gint ii;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (uids != NULL);

	for (ii = 0; uids[ii] != NULL; ii++)
		list = g_slist_prepend (list, g_strdup (uids[ii]));

	async_context = g_slice_new0 (AsyncContext);
	async_context->string_list = g_slist_reverse (list);
	async_context->string_queue = &async_context->result_queue;

	simple = g_simple_async_result_new (
		G_OBJECT (backend), callback, user_data,
		e_book_backend_remove_contacts);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	book_backend_push_operation (
		backend, simple, cancellable, FALSE,
		book_backend_remove_contacts_thread);

	book_backend_dispatch_next_operation (backend);

	g_object_unref (simple);
}

/**
 * e_book_backend_remove_contacts_finish:
 * @backend: an #EBookBackend
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_book_backend_remove_contacts().
 *
 * If an error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.10
 **/
gboolean
e_book_backend_remove_contacts_finish (EBookBackend *backend,
                                       GAsyncResult *result,
                                       GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (backend),
		e_book_backend_remove_contacts), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	while (!g_queue_is_empty (&async_context->result_queue)) {
		gchar *uid;

		uid = g_queue_pop_head (&async_context->result_queue);
		e_book_backend_notify_remove (backend, uid);
		g_free (uid);
	}

	e_book_backend_notify_complete (backend);

	return TRUE;
}

/**
 * e_book_backend_get_contact_sync:
 * @backend: an #EBookBackend
 * @uid: a contact ID
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Obtains an #EContact for @uid.
 *
 * The returned #EContact is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * If an error occurs, the function will set @error and return %NULL.
 *
 * Returns: an #EContact, or %NULL
 *
 * Since: 3.10
 **/
EContact *
e_book_backend_get_contact_sync (EBookBackend *backend,
                                 const gchar *uid,
                                 GCancellable *cancellable,
                                 GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	EContact *contact;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	closure = e_async_closure_new ();

	e_book_backend_get_contact (
		backend, uid, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	contact = e_book_backend_get_contact_finish (
		backend, result, error);

	e_async_closure_free (closure);

	return contact;
}

/* Helper for e_book_backend_get_contact() */
static void
book_backend_get_contact_thread (GSimpleAsyncResult *simple,
                                 GObject *source_object,
                                 GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;
	AsyncContext *async_context;

	backend = E_BOOK_BACKEND (source_object);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class->get_contact != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (!e_book_backend_is_opened (backend)) {
		g_simple_async_result_set_error (
			simple, E_CLIENT_ERROR,
			E_CLIENT_ERROR_NOT_OPENED,
			"%s", e_client_error_to_string (
			E_CLIENT_ERROR_NOT_OPENED));
		g_simple_async_result_complete_in_idle (simple);

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, simple);

		class->get_contact (
			backend, data_book, opid, cancellable,
			async_context->uid);
	}

	g_object_unref (data_book);
}

/**
 * e_book_backend_get_contact:
 * @backend: an #EBookBackend
 * @uid: a contact ID
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously obtains an #EContact for @uid.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call e_book_backend_get_contact_finish() to get the result of the
 * operation.
 *
 * Since: 3.10
 **/
void
e_book_backend_get_contact (EBookBackend *backend,
                            const gchar *uid,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (uid != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->uid = g_strdup (uid);
	async_context->object_queue = &async_context->result_queue;

	simple = g_simple_async_result_new (
		G_OBJECT (backend), callback, user_data,
		e_book_backend_get_contact);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	book_backend_push_operation (
		backend, simple, cancellable, FALSE,
		book_backend_get_contact_thread);

	book_backend_dispatch_next_operation (backend);

	g_object_unref (simple);
}

/**
 * e_book_backend_get_contact_finish:
 * @backend: an #EBookBackend
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_book_backend_get_contact_finish().
 *
 * The returned #EContact is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * If an error occurred, the function will set @error and return %NULL.
 *
 * Returns: an #EContact, or %NULL
 *
 * Since: 3.10
 **/
EContact *
e_book_backend_get_contact_finish (EBookBackend *backend,
                                   GAsyncResult *result,
                                   GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	EContact *contact;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (backend),
		e_book_backend_get_contact), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	contact = g_queue_pop_head (&async_context->result_queue);
	g_return_val_if_fail (E_IS_CONTACT (contact), NULL);

	g_warn_if_fail (g_queue_is_empty (&async_context->result_queue));

	return contact;
}

/**
 * e_book_backend_get_contact_list_sync:
 * @backend: an #EBookBackend
 * @query: a search query in S-expression format
 * @out_contacts: a #GQueue in which to deposit results
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Obtains a set of #EContact instances which satisfy the criteria specified
 * in @query, and deposits them in @out_contacts.
 *
 * The returned #EContact instances are referenced for thread-safety and
 * must be unreferenced with g_object_unref() when finished with them.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 * Note that an empty result set does not necessarily imply an error.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.10
 **/
gboolean
e_book_backend_get_contact_list_sync (EBookBackend *backend,
                                      const gchar *query,
                                      GQueue *out_contacts,
                                      GCancellable *cancellable,
                                      GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);
	g_return_val_if_fail (query != NULL, FALSE);
	g_return_val_if_fail (out_contacts != NULL, FALSE);

	closure = e_async_closure_new ();

	e_book_backend_get_contact_list (
		backend, query, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_book_backend_get_contact_list_finish (
		backend, result, out_contacts, error);

	e_async_closure_free (closure);

	return success;
}

/* Helper for e_book_backend_get_contact_list() */
static void
book_backend_get_contact_list_thread (GSimpleAsyncResult *simple,
                                      GObject *source_object,
                                      GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;
	AsyncContext *async_context;

	backend = E_BOOK_BACKEND (source_object);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class->get_contact_list != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (!e_book_backend_is_opened (backend)) {
		g_simple_async_result_set_error (
			simple, E_CLIENT_ERROR,
			E_CLIENT_ERROR_NOT_OPENED,
			"%s", e_client_error_to_string (
			E_CLIENT_ERROR_NOT_OPENED));
		g_simple_async_result_complete_in_idle (simple);

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, simple);

		class->get_contact_list (
			backend, data_book, opid, cancellable,
			async_context->query);
	}

	g_object_unref (data_book);
}

/**
 * e_book_backend_get_contact_list:
 * @backend: an #EBookBackend
 * @query: a search query in S-expression format
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously obtains a set of #EContact instances which satisfy the
 * criteria specified in @query.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_book_backend_get_contact_list_finish() to get the result of the
 * operation.
 *
 * Since: 3.10
 **/
void
e_book_backend_get_contact_list (EBookBackend *backend,
                                 const gchar *query,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (query != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->query = g_strdup (query);
	async_context->object_queue = &async_context->result_queue;

	simple = g_simple_async_result_new (
		G_OBJECT (backend), callback, user_data,
		e_book_backend_get_contact_list);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	book_backend_push_operation (
		backend, simple, cancellable, FALSE,
		book_backend_get_contact_list_thread);

	book_backend_dispatch_next_operation (backend);

	g_object_unref (simple);
}

/**
 * e_book_backend_get_contact_list_finish:
 * @backend: an #EBookBackend
 * @result: a #GAsyncResult
 * @out_contacts: a #GQueue in which to deposit results
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_book_backend_get_contact_list().
 *
 * The matching #EContact instances are deposited in @out_contacts.  The
 * returned #EContact instances are referenced for thread-safety and must
 * be unreferenced with g_object_unref() when finished with them.
 *
 * If an error occurred, the function will set @error and return %FALSE.
 * Note that an empty result set does not necessarily imply an error.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.10
 **/
gboolean
e_book_backend_get_contact_list_finish (EBookBackend *backend,
                                        GAsyncResult *result,
                                        GQueue *out_contacts,
                                        GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (backend),
		e_book_backend_get_contact_list), FALSE);
	g_return_val_if_fail (out_contacts != NULL, FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	e_queue_transfer (&async_context->result_queue, out_contacts);

	return TRUE;
}

/**
 * e_book_backend_get_contact_list_uids_sync:
 * @backend: an #EBookBackend
 * @query: a search query in S-expression format
 * @out_uids: a #GQueue in which to deposit results
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Obtains a set of ID strings for contacts which satisfy the criteria
 * specified in @query, and deposits them in @out_uids.
 *
 * The returned ID strings must be freed with g_free() with finished
 * with them.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 * Note that an empty result set does not necessarily imply an error.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.10
 **/
gboolean
e_book_backend_get_contact_list_uids_sync (EBookBackend *backend,
                                           const gchar *query,
                                           GQueue *out_uids,
                                           GCancellable *cancellable,
                                           GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);
	g_return_val_if_fail (query != NULL, FALSE);
	g_return_val_if_fail (out_uids != NULL, FALSE);

	closure = e_async_closure_new ();

	e_book_backend_get_contact_list_uids (
		backend, query, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_book_backend_get_contact_list_uids_finish (
		backend, result, out_uids, error);

	e_async_closure_free (closure);

	return success;
}

/* Helper for e_book_backend_get_contact_list_uids() */
static void
book_backend_get_contact_list_uids_thread (GSimpleAsyncResult *simple,
                                           GObject *source_object,
                                           GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;
	AsyncContext *async_context;

	backend = E_BOOK_BACKEND (source_object);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class->get_contact_list_uids != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (!e_book_backend_is_opened (backend)) {
		g_simple_async_result_set_error (
			simple, E_CLIENT_ERROR,
			E_CLIENT_ERROR_NOT_OPENED,
			"%s", e_client_error_to_string (
			E_CLIENT_ERROR_NOT_OPENED));
		g_simple_async_result_complete_in_idle (simple);

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, simple);

		class->get_contact_list_uids (
			backend, data_book, opid, cancellable,
			async_context->query);
	}

	g_object_unref (data_book);
}

/**
 * e_book_backend_get_contact_list_uids:
 * @backend: an #EBookBackend
 * @query: a search query in S-expression format
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously obtains a set of ID strings for contacts which satisfy
 * the criteria specified in @query.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_book_backend_get_contact_list_uids_finish() to get the result of
 * the operation.
 *
 * Since: 3.10
 **/
void
e_book_backend_get_contact_list_uids (EBookBackend *backend,
                                      const gchar *query,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (query != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->query = g_strdup (query);
	async_context->string_queue = &async_context->result_queue;

	simple = g_simple_async_result_new (
		G_OBJECT (backend), callback, user_data,
		e_book_backend_get_contact_list_uids);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	book_backend_push_operation (
		backend, simple, cancellable, FALSE,
		book_backend_get_contact_list_uids_thread);

	book_backend_dispatch_next_operation (backend);

	g_object_unref (simple);
}

/**
 * e_book_backend_get_contact_list_uids_finish:
 * @backend: an #EBookBackend
 * @result: a #GAsyncResult
 * @out_uids: a #GQueue in which to deposit results
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with
 * e_book_backend_get_contact_list_uids_finish().
 *
 * ID strings for the matching contacts are deposited in @out_uids, and
 * must be freed with g_free() when finished with them.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 * Note that an empty result set does not necessarily imply an error.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.10
 **/
gboolean
e_book_backend_get_contact_list_uids_finish (EBookBackend *backend,
                                             GAsyncResult *result,
                                             GQueue *out_uids,
                                             GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (backend),
		e_book_backend_get_contact_list_uids), FALSE);
	g_return_val_if_fail (out_uids != NULL, FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	e_queue_transfer (&async_context->result_queue, out_uids);

	return TRUE;
}

/**
 * e_book_backend_start_view:
 * @backend: an #EBookBackend
 * @view: the #EDataBookView to start
 *
 * Starts running the query specified by @view, emitting signals for
 * matching contacts.
 **/
void
e_book_backend_start_view (EBookBackend *backend,
                           EDataBookView *view)
{
	EBookBackendClass *class;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (view));

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class->start_view);

	class->start_view (backend, view);
}

/**
 * e_book_backend_stop_view:
 * @backend: an #EBookBackend
 * @view: the #EDataBookView to stop
 *
 * Stops running the query specified by @view, emitting no more signals.
 **/
void
e_book_backend_stop_view (EBookBackend *backend,
                          EDataBookView *view)
{
	EBookBackendClass *class;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (view));

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class->stop_view != NULL);

	class->stop_view (backend, view);
}

/**
 * e_book_backend_add_view:
 * @backend: an #EBookBackend
 * @view: an #EDataBookView
 *
 * Adds @view to @backend for querying.
 **/
void
e_book_backend_add_view (EBookBackend *backend,
                         EDataBookView *view)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_mutex_lock (&backend->priv->views_mutex);

	g_object_ref (view);
	backend->priv->views = g_list_append (backend->priv->views, view);

	g_mutex_unlock (&backend->priv->views_mutex);
}

/**
 * e_book_backend_remove_view:
 * @backend: an #EBookBackend
 * @view: an #EDataBookView
 *
 * Removes @view from @backend.
 **/
void
e_book_backend_remove_view (EBookBackend *backend,
                            EDataBookView *view)
{
	GList *list, *link;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_mutex_lock (&backend->priv->views_mutex);

	list = backend->priv->views;

	link = g_list_find (list, view);
	if (link != NULL) {
		g_object_unref (view);
		list = g_list_delete_link (list, link);
	}

	backend->priv->views = list;

	g_mutex_unlock (&backend->priv->views_mutex);
}

/**
 * e_book_backend_list_views:
 * @backend: an #EBookBackend
 *
 * Returns a list of #EDataBookView instances added with
 * e_book_backend_add_view().
 *
 * The views returned in the list are referenced for thread-safety.
 * They must each be unreferenced with g_object_unref() when finished
 * with them.  Free the returned list itself with g_list_free().
 *
 * An easy way to free the list properly in one step is as follows:
 *
 * |[
 *   g_list_free_full (list, g_object_unref);
 * ]|
 *
 * Returns: a list of book views
 *
 * Since: 3.8
 **/
GList *
e_book_backend_list_views (EBookBackend *backend)
{
	GList *list;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	g_mutex_lock (&backend->priv->views_mutex);

	/* XXX Use g_list_copy_deep() once we require GLib >= 2.34. */
	list = g_list_copy (backend->priv->views);
	g_list_foreach (list, (GFunc) g_object_ref, NULL);

	g_mutex_unlock (&backend->priv->views_mutex);

	return list;
}

/**
 * e_book_backend_get_backend_property:
 * @backend: an #EBookBackend
 * @prop_name: a backend property name
 *
 * Obtains the value of the backend property named @prop_name.
 * Freed the returned string with g_free() when finished with it.
 *
 * Returns: the value for @prop_name
 *
 * Since: 3.10
 **/
gchar *
e_book_backend_get_backend_property (EBookBackend *backend,
                                     const gchar *prop_name)
{
	EBookBackendClass *class;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_val_if_fail (class->get_backend_property != NULL, NULL);

	return class->get_backend_property (backend, prop_name);
}

/**
 * e_book_backend_is_opened:
 * @backend: an #EBookBackend
 *
 * Checks if @backend's storage has been opened (and
 * authenticated, if necessary) and the backend itself
 * is ready for accessing. This property is changed automatically
 * within call of e_book_backend_notify_opened().
 *
 * Returns: %TRUE if fully opened, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_is_opened (EBookBackend *backend)
{
	EDataBook *data_book;
	gboolean opened = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	data_book = e_book_backend_ref_data_book (backend);

	if (data_book != NULL) {
		opened = e_data_book_is_opened (data_book);
		g_object_unref (data_book);
	}

	return opened;
}

/**
 * e_book_backend_is_readonly:
 * @backend: an #EBookBackend
 *
 * Checks if we can write to @backend.
 *
 * Returns: %TRUE if read-only, %FALSE if not.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_is_readonly (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	return !e_book_backend_get_writable (backend);
}

/**
 * e_book_backend_get_direct_book:
 * @backend: an #EBookBackend
 *
 * Tries to create an #EDataBookDirect for @backend if
 * backend supports direct read access.
 *
 * Returns: (transfer full): A new #EDataBookDirect object, or %NULL if @backend does not support direct access
 *
 * Since: 3.8
 */
EDataBookDirect *
e_book_backend_get_direct_book (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	if (E_BOOK_BACKEND_GET_CLASS (backend)->get_direct_book)
		return E_BOOK_BACKEND_GET_CLASS (backend)->get_direct_book (backend);

	return NULL;
}

/**
 * e_book_backend_configure_direct:
 * @backend: an #EBookBackend
 * @config: The configuration string for the given backend
 *
 * This method is called on @backend in direct read access mode.
 * The @config argument is the same configuration string which
 * the same backend reported in the #EDataBookDirect returned
 * by e_book_backend_get_direct_book().
 *
 * The configuration string is optional and is used to ensure
 * that direct access backends are properly configured to
 * interface with the same data as the running server side backend.
 *
 * Since: 3.8
 */
void
e_book_backend_configure_direct (EBookBackend *backend,
                                 const gchar *config)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	if (E_BOOK_BACKEND_GET_CLASS (backend)->configure_direct)
		E_BOOK_BACKEND_GET_CLASS (backend)->configure_direct (backend, config);
}

/**
 * e_book_backend_sync:
 * @backend: an #EBookbackend
 *
 * Write all pending data to disk.  This is only required under special
 * circumstances (for example before a live backup) and should not be used in
 * normal use.
 *
 * Since: 1.12
 */
void
e_book_backend_sync (EBookBackend *backend)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_object_ref (backend);

	if (E_BOOK_BACKEND_GET_CLASS (backend)->sync)
		(* E_BOOK_BACKEND_GET_CLASS (backend)->sync) (backend);

	g_object_unref (backend);
}

/**
 * e_book_backend_notify_update:
 * @backend: an #EBookBackend
 * @contact: a new or modified contact
 *
 * Notifies all of @backend's book views about the new or modified
 * contacts @contact.
 *
 * e_data_book_respond_create_contacts() and e_data_book_respond_modify_contacts() call this
 * function for you. You only need to call this from your backend if
 * contacts are created or modified by another (non-PAS-using) client.
 **/
void
e_book_backend_notify_update (EBookBackend *backend,
                              const EContact *contact)
{
	EBookBackendClass *class;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_CONTACT (contact));

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class->notify_update != NULL);

	class->notify_update (backend, contact);
}

/**
 * e_book_backend_notify_remove:
 * @backend: an #EBookBackend
 * @id: a contact id
 *
 * Notifies all of @backend's book views that the contact with UID
 * @id has been removed.
 *
 * e_data_book_respond_remove_contacts() calls this function for you. You
 * only need to call this from your backend if contacts are removed by
 * another (non-PAS-using) client.
 **/
void
e_book_backend_notify_remove (EBookBackend *backend,
                              const gchar *id)
{
	GList *list, *link;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (id != NULL);

	list = e_book_backend_list_views (backend);

	for (link = list; link != NULL; link = g_list_next (link)) {
		EDataBookView *view = E_DATA_BOOK_VIEW (link->data);
		e_data_book_view_notify_remove (view, id);
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
}

/**
 * e_book_backend_notify_complete:
 * @backend: an #EBookbackend
 *
 * Notifies all of @backend's book views that the current set of
 * notifications is complete; use this after a series of
 * e_book_backend_notify_update() and e_book_backend_notify_remove() calls.
 **/
void
e_book_backend_notify_complete (EBookBackend *backend)
{
	GList *list, *link;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	list = e_book_backend_list_views (backend);

	for (link = list; link != NULL; link = g_list_next (link)) {
		EDataBookView *view = E_DATA_BOOK_VIEW (link->data);
		e_data_book_view_notify_complete (view, NULL /* SUCCESS */);
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
}

/**
 * e_book_backend_notify_error:
 * @backend: an #EBookBackend
 * @message: an error message
 *
 * Notifies each backend listener about an error. This is meant to be used
 * for cases where is no GError return possibility, to notify user about
 * an issue.
 *
 * Since: 3.2
 **/
void
e_book_backend_notify_error (EBookBackend *backend,
                             const gchar *message)
{
	EDataBook *data_book;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (message != NULL);

	data_book = e_book_backend_ref_data_book (backend);

	if (data_book != NULL) {
		e_data_book_report_error (data_book, message);
		g_object_unref (data_book);
	}
}

/**
 * e_book_backend_notify_property_changed:
 * @backend: an #EBookBackend
 * @prop_name: property name, which changed
 * @prop_value: new property value
 *
 * Notifies clients about property value change.
 *
 * Since: 3.2
 **/
void
e_book_backend_notify_property_changed (EBookBackend *backend,
                                        const gchar *prop_name,
                                        const gchar *prop_value)
{
	EDataBook *data_book;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (prop_value != NULL);

	data_book = e_book_backend_ref_data_book (backend);

	if (data_book != NULL) {
		e_data_book_report_backend_property_changed (
			data_book, prop_name, prop_value);
		g_object_unref (data_book);
	}
}

/**
 * e_book_backend_prepare_for_completion:
 * @backend: an #EBookBackend
 * @opid: an operation ID given to #EDataBook
 * @result_queue: return location for a #GQueue, or %NULL
 *
 * Obtains the #GSimpleAsyncResult for @opid and sets @result_queue as a
 * place to deposit results prior to completing the #GSimpleAsyncResult.
 *
 * <note>
 *   <para>
 *     This is a temporary function to serve #EDataBook's "respond"
 *     functions until they can be removed.  Nothing else should be
 *     calling this function.
 *   </para>
 * </note>
 *
 * Returns: (transfer full): a #GSimpleAsyncResult
 *
 * Since: 3.10
 **/
GSimpleAsyncResult *
e_book_backend_prepare_for_completion (EBookBackend *backend,
                                       guint32 opid,
                                       GQueue **result_queue)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);
	g_return_val_if_fail (opid > 0, NULL);

	simple = book_backend_claim_operation (backend, opid);
	g_return_val_if_fail (simple != NULL, NULL);

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (result_queue != NULL) {
		if (async_context != NULL)
			*result_queue = &async_context->result_queue;
		else
			*result_queue = NULL;
	}

	return simple;
}


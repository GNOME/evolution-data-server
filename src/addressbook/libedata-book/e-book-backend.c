/*
 * e-book-backend.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2012 Intel Corporation
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
 *
 * Authors: Nat Friedman (nat@ximian.com)
 *          Tristan Van Berkom <tristanvb@openismus.com>
 */

/**
 * SECTION: e-book-backend
 * @include: libedata-book/libedata-book.h
 * @short_description: An abstract class for implementing addressbook backends
 *
 * This is the main server facing API for interfacing with addressbook backends,
 * addressbook backends must implement methods on this class.
 **/

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include "e-data-book-view.h"
#include "e-data-book.h"
#include "e-data-book-view-watcher-memory.h"
#include "e-book-backend.h"
#include "e-book-backend-private.h"

typedef struct _AsyncContext AsyncContext;
typedef struct _CustomOpFuncData CustomOpFuncData;
typedef struct _DispatchNode DispatchNode;

struct _EBookBackendPrivate {
	ESourceRegistry *registry;
	EDataBook *data_book;

	gboolean opened;

	GMutex views_mutex;
	GHashTable *views; /* EDataBookView::id ~> ViewData * */

	GMutex property_lock;
	GProxyResolver *proxy_resolver;
	gchar *cache_dir;
	gboolean writable;

	ESource *authentication_source;
	gulong auth_source_changed_handler_id;

	GMutex operation_lock;
	GThreadPool *thread_pool;
	GHashTable *operation_ids;
	GQueue pending_operations;
	guint32 next_operation_id;
	GTask *blocked;
};

struct _AsyncContext {
	/* Inputs */
	gchar **strv;

	guint32 opflags; /* bit-or of EBookOperationFlags */
};

struct _DispatchNode {
	/* This is the dispatch function
	 * that invokes the class method. */
	GTaskThreadFunc dispatch_func;
	gboolean blocking_operation;

	GTask *task;
};

struct _CustomOpFuncData {
	EBookBackendCustomOpFunc custom_func;
	gpointer custom_func_user_data;
	GDestroyNotify custom_func_user_data_free;
};

enum {
	PROP_0,
	PROP_CACHE_DIR,
	PROP_PROXY_RESOLVER,
	PROP_REGISTRY,
	PROP_WRITABLE,
	N_PROPS
};

enum {
	CLOSED,
	SHUTDOWN,
	LAST_SIGNAL
};

static GParamSpec *properties[N_PROPS] = { NULL, };

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_PRIVATE (EBookBackend, e_book_backend, E_TYPE_BACKEND)

typedef struct _ViewData {
	EDataBookView *view;
	GObject *user_data;
	EBookClientViewSortFields *sort_fields;
	EBookIndices *indices;
	guint n_total;
} ViewData;

static void
view_data_free (gpointer ptr)
{
	ViewData *vd = ptr;

	if (vd) {
		g_clear_object (&vd->view);
		g_clear_object (&vd->user_data);
		e_book_client_view_sort_fields_free (vd->sort_fields);
		e_book_indices_free (vd->indices);
		g_free (vd);
	}
}

static void
async_context_free (AsyncContext *async_context)
{
	g_strfreev (async_context->strv);

	g_slice_free (AsyncContext, async_context);
}

static void
dispatch_node_free (DispatchNode *dispatch_node)
{
	g_clear_object (&dispatch_node->task);

	g_slice_free (DispatchNode, dispatch_node);
}

static void
book_backend_push_operation (EBookBackend *backend,
                             GTask *task,
                             gboolean blocking_operation,
                             GTaskThreadFunc dispatch_func)
{
	DispatchNode *node;

	g_return_if_fail (G_IS_TASK (task));
	g_return_if_fail (dispatch_func != NULL);

	g_mutex_lock (&backend->priv->operation_lock);

	node = g_slice_new0 (DispatchNode);
	node->dispatch_func = dispatch_func;
	node->blocking_operation = blocking_operation;
	node->task = g_steal_pointer (&task);

	g_queue_push_tail (&backend->priv->pending_operations, node);

	g_mutex_unlock (&backend->priv->operation_lock);
}

static void book_backend_unblock_operations (EBookBackend *backend, GTask *task);

static void
book_backend_dispatch_thread (DispatchNode *node)
{
	GCancellable *cancellable = g_task_get_cancellable (node->task);
	if (!g_task_return_error_if_cancelled (node->task)) {
		GObject *source_object = g_task_get_source_object (node->task);
		gpointer task_data = g_task_get_task_data (node->task);

		node->dispatch_func (node->task, source_object, task_data, cancellable);
	}

	dispatch_node_free (node);
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
	if (node->blocking_operation) {
		backend->priv->blocked = g_object_ref (node->task);
	}

	g_mutex_unlock (&backend->priv->operation_lock);

	/* An error here merely indicates a thread could not be
	 * created, and so the node was queued.  We don't care. */
	g_thread_pool_push (backend->priv->thread_pool, node, NULL);

	return TRUE;
}

static void
book_backend_unblock_operations (EBookBackend *backend,
                                 GTask *task)
{
	/* If the GTask was blocking the dispatch queue,
	 * unblock the dispatch queue.  Then dispatch as many waiting
	 * operations as we can. */

	g_mutex_lock (&backend->priv->operation_lock);
	if (backend->priv->blocked == task)
		g_clear_object (&backend->priv->blocked);
	g_mutex_unlock (&backend->priv->operation_lock);

	while (book_backend_dispatch_next_operation (backend))
		;
}

static guint32
book_backend_stash_operation (EBookBackend *backend,
                              GTask *task)
{
	guint32 opid;

	g_mutex_lock (&backend->priv->operation_lock);

	if (backend->priv->next_operation_id == 0)
		backend->priv->next_operation_id = 1;

	opid = backend->priv->next_operation_id++;

	g_hash_table_insert (
		backend->priv->operation_ids,
		GUINT_TO_POINTER (opid),
		g_object_ref (task));

	g_mutex_unlock (&backend->priv->operation_lock);

	return opid;
}

static GTask *
book_backend_claim_operation (EBookBackend *backend,
                              guint32 opid)
{
	GTask *task;

	g_return_val_if_fail (opid > 0, NULL);

	g_mutex_lock (&backend->priv->operation_lock);

	task = g_hash_table_lookup (
		backend->priv->operation_ids,
		GUINT_TO_POINTER (opid));

	if (task != NULL) {
		/* Steal the hash table's reference. */
		g_hash_table_steal (
			backend->priv->operation_ids,
			GUINT_TO_POINTER (opid));
	}

	g_mutex_unlock (&backend->priv->operation_lock);

	return task;
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

static void
book_backend_update_proxy_resolver (EBookBackend *backend)
{
	GProxyResolver *proxy_resolver = NULL;
	ESourceAuthentication *extension;
	ESource *source = NULL;
	gboolean notify = FALSE;
	gchar *uid;

	extension = e_source_get_extension (
		backend->priv->authentication_source,
		E_SOURCE_EXTENSION_AUTHENTICATION);

	uid = e_source_authentication_dup_proxy_uid (extension);
	if (uid != NULL) {
		ESourceRegistry *registry;

		registry = e_book_backend_get_registry (backend);
		source = e_source_registry_ref_source (registry, uid);
		g_free (uid);
	}

	if (source != NULL) {
		proxy_resolver = G_PROXY_RESOLVER (source);
		if (!g_proxy_resolver_is_supported (proxy_resolver))
			proxy_resolver = NULL;
	}

	g_mutex_lock (&backend->priv->property_lock);

	/* Emitting a "notify" signal unnecessarily might have
	 * unwanted side effects like cancelling a SoupMessage.
	 * Only emit if we now have a different GProxyResolver. */

	if (proxy_resolver != backend->priv->proxy_resolver) {
		g_clear_object (&backend->priv->proxy_resolver);
		backend->priv->proxy_resolver = proxy_resolver;

		if (proxy_resolver != NULL)
			g_object_ref (proxy_resolver);

		notify = TRUE;
	}

	g_mutex_unlock (&backend->priv->property_lock);

	if (notify)
		g_object_notify_by_pspec (G_OBJECT (backend), properties[PROP_PROXY_RESOLVER]);

	g_clear_object (&source);
}

static void
book_backend_auth_source_changed_cb (ESource *authentication_source,
                                     GWeakRef *backend_weak_ref)
{
	EBookBackend *backend;

	backend = g_weak_ref_get (backend_weak_ref);

	if (backend != NULL) {
		book_backend_update_proxy_resolver (backend);
		g_object_unref (backend);
	}
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

		case PROP_PROXY_RESOLVER:
			g_value_take_object (
				value, e_book_backend_ref_proxy_resolver (
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

	priv = E_BOOK_BACKEND (object)->priv;

	if (priv->auth_source_changed_handler_id > 0) {
		g_signal_handler_disconnect (
			priv->authentication_source,
			priv->auth_source_changed_handler_id);
		priv->auth_source_changed_handler_id = 0;
	}

	g_clear_object (&priv->registry);
	g_clear_object (&priv->data_book);
	g_clear_object (&priv->proxy_resolver);
	g_clear_object (&priv->authentication_source);

	g_mutex_lock (&priv->views_mutex);
	g_clear_pointer (&priv->views, g_hash_table_unref);
	g_mutex_unlock (&priv->views_mutex);

	g_mutex_lock (&priv->operation_lock);

	g_hash_table_remove_all (priv->operation_ids);

	while (!g_queue_is_empty (&priv->pending_operations))
		dispatch_node_free (g_queue_pop_head (&priv->pending_operations));

	g_mutex_unlock (&priv->operation_lock);

	g_clear_object (&priv->blocked);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_book_backend_parent_class)->dispose (object);
}

static void
book_backend_finalize (GObject *object)
{
	EBookBackendPrivate *priv;

	priv = E_BOOK_BACKEND (object)->priv;

	g_mutex_clear (&priv->views_mutex);
	g_mutex_clear (&priv->property_lock);

	g_free (priv->cache_dir);

	g_warn_if_fail (g_queue_is_empty (&priv->pending_operations));
	g_mutex_clear (&priv->operation_lock);
	g_hash_table_destroy (priv->operation_ids);

	/* Return immediately, do not wait. */
	g_thread_pool_free (priv->thread_pool, TRUE, FALSE);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_backend_parent_class)->finalize (object);

	e_util_call_malloc_trim ();
}

static void
book_backend_constructed (GObject *object)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	ESourceRegistry *registry;
	ESource *source;
	gint max_threads = -1;
	gboolean exclusive = FALSE;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_book_backend_parent_class)->constructed (object);

	backend = E_BOOK_BACKEND (object);
	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);

	registry = e_book_backend_get_registry (backend);
	source = e_backend_get_source (E_BACKEND (backend));

	/* If the backend specifies a serial dispatch queue, create
	 * a thread pool with one exclusive thread.  The thread pool
	 * will serialize operations for us. */
	if (class->use_serial_dispatch_queue) {
		max_threads = 1;
		exclusive = TRUE;
	}

	/* XXX If creating an exclusive thread pool, technically there's
	 *     a small chance of error here but we'll risk it since it's
	 *     only for one exclusive thread. */
	backend->priv->thread_pool = g_thread_pool_new (
		(GFunc) book_backend_dispatch_thread,
		NULL, max_threads, exclusive, NULL);

	/* Initialize the "cache-dir" property. */
	book_backend_set_default_cache_dir (backend);

	/* Track the proxy resolver for this backend. */
	backend->priv->authentication_source =
		e_source_registry_find_extension (
		registry, source, E_SOURCE_EXTENSION_AUTHENTICATION);
	if (backend->priv->authentication_source != NULL) {
		gulong handler_id;

		handler_id = g_signal_connect_data (
			backend->priv->authentication_source, "changed",
			G_CALLBACK (book_backend_auth_source_changed_cb),
			e_weak_ref_new (backend),
			(GClosureNotify) e_weak_ref_free, 0);
		backend->priv->auth_source_changed_handler_id = handler_id;

		book_backend_update_proxy_resolver (backend);
	}
}

static void
book_backend_prepare_shutdown (EBackend *backend)
{
	GList *list, *l;

	list = e_book_backend_list_views (E_BOOK_BACKEND (backend));

	for (l = list; l != NULL; l = g_list_next (l)) {
		EDataBookView *view = l->data;

		e_book_backend_remove_view (E_BOOK_BACKEND (backend), view);
	}

	g_list_free_full (list, g_object_unref);

	/* Chain up to parent's prepare_shutdown() method. */
	E_BACKEND_CLASS (e_book_backend_parent_class)->prepare_shutdown (backend);
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

	} else if (g_str_equal (prop_name, E_BOOK_BACKEND_PROPERTY_PREFER_VCARD_VERSION)) {
		/* use 3.0 for backward compatibility, to not force the backends to implement this property */
		prop_value = g_strdup (e_vcard_version_to_string (E_VCARD_VERSION_30));
	}

	return prop_value;
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
book_backend_shutdown (EBookBackend *backend)
{
	ESource *source;

	source = e_backend_get_source (E_BACKEND (backend));

	e_source_registry_debug_print (
		"The %s instance for \"%s\" is shutting down.\n",
		G_OBJECT_TYPE_NAME (backend),
		e_source_get_display_name (source));

	e_util_call_malloc_trim ();
}

/* Should have acquired views_mutex */
static ViewData *
e_book_backend_get_view_data_by_id_locked (EBookBackend *backend,
					   gsize view_id)
{
	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);

	return g_hash_table_lookup (backend->priv->views, GSIZE_TO_POINTER (view_id));
}

static void
book_backend_set_view_sort_fields (EBookBackend *backend,
				   gsize view_id,
				   const EBookClientViewSortFields *fields)
{
	GObject *view_watcher;
	ViewData *vd;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_mutex_lock (&backend->priv->views_mutex);

	vd = e_book_backend_get_view_data_by_id_locked (backend, view_id);

	if (vd && vd->sort_fields != fields) {
		e_book_client_view_sort_fields_free (vd->sort_fields);
		vd->sort_fields = e_book_client_view_sort_fields_copy (fields);
	}

	g_mutex_unlock (&backend->priv->views_mutex);

	view_watcher = e_book_backend_ref_view_user_data (backend, view_id);

	if (E_IS_DATA_BOOK_VIEW_WATCHER_MEMORY (view_watcher)) {
		e_data_book_view_watcher_memory_take_sort_fields (E_DATA_BOOK_VIEW_WATCHER_MEMORY (view_watcher),
			e_book_client_view_sort_fields_copy (fields));
	}

	g_clear_object (&view_watcher);
}

static guint
book_backend_get_view_n_total (EBookBackend *backend,
			       gsize view_id)
{
	ViewData *vd;
	guint n_total = 0;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), 0);

	g_mutex_lock (&backend->priv->views_mutex);

	vd = e_book_backend_get_view_data_by_id_locked (backend, view_id);

	if (vd)
		n_total = vd->n_total;

	g_mutex_unlock (&backend->priv->views_mutex);

	return n_total;
}

static EBookIndices *
book_backend_dup_view_indices (EBookBackend *backend,
			       gsize view_id)
{
	EBookIndices *indices = NULL;
	ViewData *vd;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	g_mutex_lock (&backend->priv->views_mutex);

	vd = e_book_backend_get_view_data_by_id_locked (backend, view_id);

	if (vd)
		indices = e_book_indices_copy (vd->indices);

	g_mutex_unlock (&backend->priv->views_mutex);

	return indices;
}

static GPtrArray *
book_backend_dup_view_contacts (EBookBackend *backend,
				gsize view_id,
				guint range_start,
				guint range_length)
{
	GObject *view_watcher;
	GPtrArray *contacts = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	view_watcher = e_book_backend_ref_view_user_data (backend, view_id);

	if (E_IS_DATA_BOOK_VIEW_WATCHER_MEMORY (view_watcher))
		contacts = e_data_book_view_watcher_memory_dup_contacts (E_DATA_BOOK_VIEW_WATCHER_MEMORY (view_watcher), range_start, range_length);

	g_clear_object (&view_watcher);

	return contacts;
}

static void
e_book_backend_class_init (EBookBackendClass *class)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = book_backend_set_property;
	object_class->get_property = book_backend_get_property;
	object_class->dispose = book_backend_dispose;
	object_class->finalize = book_backend_finalize;
	object_class->constructed = book_backend_constructed;

	backend_class = E_BACKEND_CLASS (class);
	backend_class->prepare_shutdown = book_backend_prepare_shutdown;

	class->use_serial_dispatch_queue = TRUE;
	class->impl_get_backend_property = book_backend_get_backend_property;
	class->impl_notify_update = book_backend_notify_update;
	class->shutdown = book_backend_shutdown;
	class->impl_set_view_sort_fields = book_backend_set_view_sort_fields;
	class->impl_get_view_n_total = book_backend_get_view_n_total;
	class->impl_dup_view_indices = book_backend_dup_view_indices;
	class->impl_dup_view_contacts = book_backend_dup_view_contacts;

	/**
	 * EBookBackend:cache-dir
	 *
	 * The backend's cache directory
	 **/
	properties[PROP_CACHE_DIR] =
		g_param_spec_string (
			"cache-dir",
			NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * EBookBackend:proxy-resolver
	 *
	 * The proxy resolver for this backend
	 **/
	properties[PROP_PROXY_RESOLVER] =
		g_param_spec_object (
			"proxy-resolver",
			NULL, NULL,
			G_TYPE_PROXY_RESOLVER,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS);

	/**
	 * EBookBackend:registry
	 *
	 * Data source registry
	 **/
	properties[PROP_REGISTRY] =
		g_param_spec_object (
			"registry",
			NULL, NULL,
			E_TYPE_SOURCE_REGISTRY,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * EBookBackend:writable
	 *
	 * Whether the backend will accept changes
	 **/
	properties[PROP_WRITABLE] =
		g_param_spec_boolean (
			"writable",
			NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

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

	/**
	 * EBookBackend::shutdown:
	 * @backend: the #EBookBackend which emitted the signal
	 *
	 * Emitted when the last client destroys its #EBookClient for
	 * @backend.  This signals the @backend to begin final cleanup
	 * tasks such as synchronizing data to permanent storage.
	 *
	 * Since: 3.10
	 **/
	signals[SHUTDOWN] = g_signal_new (
		"shutdown",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EBookBackendClass, shutdown),
		NULL, NULL, NULL,
		G_TYPE_NONE, 0);
}

static void
e_book_backend_init (EBookBackend *backend)
{
	backend->priv = e_book_backend_get_instance_private (backend);

	backend->priv->views = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, view_data_free);
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

	g_object_notify_by_pspec (G_OBJECT (backend), properties[PROP_CACHE_DIR]);
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
 * Returns: (transfer full) (nullable): an #EDataBook, or %NULL
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
 * e_book_backend_ref_proxy_resolver:
 * @backend: an #EBookBackend
 *
 * Returns the #GProxyResolver for @backend (if applicable), as indicated
 * by the #ESourceAuthentication:proxy-uid of @backend's #EBackend:source
 * or one of its ancestors.
 *
 * The returned #GProxyResolver is referenced for thread-safety and must
 * be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full) (nullable): a #GProxyResolver, or %NULL
 *
 * Since: 3.12
 **/
GProxyResolver *
e_book_backend_ref_proxy_resolver (EBookBackend *backend)
{
	GProxyResolver *proxy_resolver = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	g_mutex_lock (&backend->priv->property_lock);

	if (backend->priv->proxy_resolver != NULL)
		proxy_resolver = g_object_ref (backend->priv->proxy_resolver);

	g_mutex_unlock (&backend->priv->property_lock);

	return proxy_resolver;
}

/**
 * e_book_backend_get_registry:
 * @backend: an #EBookBackend
 *
 * Returns the data source registry to which #EBackend:source belongs.
 *
 * Returns: (transfer none): an #ESourceRegistry
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

	g_object_notify_by_pspec (G_OBJECT (backend), properties[PROP_WRITABLE]);
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
book_backend_open_thread (GTask *task,
                          gpointer source_object,
                          gpointer task_data,
                          GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;

	backend = E_BOOK_BACKEND (source_object);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->impl_open != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	if (e_book_backend_is_opened (backend)) {
		g_task_return_boolean (task, TRUE);

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, task);

		e_backend_ensure_online_state_updated (E_BACKEND (backend), cancellable);

		class->impl_open (backend, data_book, opid, cancellable);
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
	EBookBackendClass *class;
	GTask *task;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);

	task = g_task_new (backend, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_book_backend_open);

	if (class->impl_open != NULL) {
		book_backend_push_operation (
			backend, g_steal_pointer (&task), TRUE, book_backend_open_thread);
		book_backend_dispatch_next_operation (backend);

	} else {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));
		g_object_unref (task);
	}
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
	GTask *task;

	g_return_val_if_fail (g_task_is_valid (result, backend), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_book_backend_open), FALSE);

	task = G_TASK (result);

	book_backend_unblock_operations (backend, task);

	if (g_task_propagate_boolean (task, error)) {
		backend->priv->opened = TRUE;
		return TRUE;
	}

	return FALSE;
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
book_backend_refresh_thread (GTask *task,
                             gpointer source_object,
                             gpointer task_data,
                             GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;

	backend = E_BOOK_BACKEND (source_object);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->impl_refresh != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	if (!e_book_backend_is_opened (backend)) {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_OPENED, NULL));

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, task);

		class->impl_refresh (backend, data_book, opid, cancellable);
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
	EBookBackendClass *class;
	GTask *task;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);

	task = g_task_new (backend, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_book_backend_refresh);

	if (class->impl_refresh != NULL) {
		book_backend_push_operation (
			backend, g_steal_pointer (&task), FALSE, book_backend_refresh_thread);
		book_backend_dispatch_next_operation (backend);

	} else {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));
		g_object_unref (task);
	}
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
	GTask *task;

	g_return_val_if_fail (g_task_is_valid (result, backend), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_book_backend_refresh), FALSE);

	task = G_TASK (result);

	book_backend_unblock_operations (backend, task);

	return g_task_propagate_boolean (task, error);
}

/**
 * e_book_backend_create_contacts_sync:
 * @backend: an #EBookBackend
 * @vcards: a %NULL-terminated array of vCard strings
 * @opflags: bit-or of #EBookOperationFlags
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
				     guint32 opflags,
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
		backend, vcards, opflags, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_book_backend_create_contacts_finish (
		backend, result, out_contacts, error);

	e_async_closure_free (closure);

	return success;
}

/* Helper for e_book_backend_create_contacts() */
static void
book_backend_create_contacts_thread (GTask *task,
                                     gpointer source_object,
                                     gpointer task_data,
                                     GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;
	AsyncContext *async_context;

	backend = E_BOOK_BACKEND (source_object);
	async_context = task_data;

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->impl_create_contacts != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	if (!e_book_backend_is_opened (backend)) {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_OPENED, NULL));

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, task);

		class->impl_create_contacts (
			backend, data_book, opid, cancellable, (const gchar * const *) async_context->strv, async_context->opflags);
	}

	g_object_unref (data_book);
}

/**
 * e_book_backend_create_contacts:
 * @backend: an #EBookBackend
 * @vcards: a %NULL-terminated array of vCard strings
 * @opflags: bit-or of #EBookOperationFlags
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
				guint32 opflags,
				GCancellable *cancellable,
				GAsyncReadyCallback callback,
				gpointer user_data)
{
	EBookBackendClass *class;
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (vcards != NULL);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->strv = g_strdupv ((gchar **) vcards);
	async_context->opflags = opflags;

	task = g_task_new (backend, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_book_backend_create_contacts);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	if (class->impl_create_contacts != NULL) {
		book_backend_push_operation (
			backend, g_steal_pointer (&task), FALSE, book_backend_create_contacts_thread);
		book_backend_dispatch_next_operation (backend);

	} else {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));
		g_object_unref (task);
	}
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
	GTask *task;
	GQueue *queue;

	g_return_val_if_fail (g_task_is_valid (result, backend), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_book_backend_create_contacts), FALSE);
	g_return_val_if_fail (out_contacts != NULL, FALSE);

	task = G_TASK (result);

	book_backend_unblock_operations (backend, task);

	queue = g_task_propagate_pointer (task, error);
	if (!queue)
		return FALSE;

	while (!g_queue_is_empty (queue)) {
		EContact *contact;

		contact = g_queue_pop_head (queue);
		e_book_backend_notify_update (backend, contact);
		g_queue_push_tail (out_contacts, g_steal_pointer (&contact));
	}

	e_book_backend_notify_complete (backend);
	g_queue_free (queue);

	return TRUE;
}

/**
 * e_book_backend_modify_contacts_sync:
 * @backend: an #EBookBackend
 * @vcards: a %NULL-terminated array of vCard strings
 * @opflags: bit-or of #EBookOperationFlags
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
				     guint32 opflags,
				     GCancellable *cancellable,
				     GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	closure = e_async_closure_new ();

	e_book_backend_modify_contacts (
		backend, vcards, opflags, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_book_backend_modify_contacts_finish (
		backend, result, error);

	e_async_closure_free (closure);

	return success;
}

/* Helper for e_book_backend_modify_contacts() */
static void
book_backend_modify_contacts_thread (GTask *task,
                                     gpointer source_object,
                                     gpointer task_data,
                                     GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;
	AsyncContext *async_context;

	backend = E_BOOK_BACKEND (source_object);
	async_context = task_data;

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->impl_modify_contacts != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	if (!e_book_backend_is_opened (backend)) {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_OPENED, NULL));

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, task);

		class->impl_modify_contacts (
			backend, data_book, opid, cancellable, (const gchar * const *) async_context->strv, async_context->opflags);
	}

	g_object_unref (data_book);
}

/**
 * e_book_backend_modify_contacts:
 * @backend: an #EBookBackend
 * @vcards: a %NULL-terminated array of vCard strings
 * @opflags: bit-or of #EBookOperationFlags
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
				guint32 opflags,
				GCancellable *cancellable,
				GAsyncReadyCallback callback,
				gpointer user_data)
{
	EBookBackendClass *class;
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (vcards != NULL);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->strv = g_strdupv ((gchar **) vcards);
	async_context->opflags = opflags;

	task = g_task_new (backend, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_book_backend_modify_contacts);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	if (class->impl_modify_contacts != NULL) {
		book_backend_push_operation (
			backend, g_steal_pointer (&task), FALSE, book_backend_modify_contacts_thread);
		book_backend_dispatch_next_operation (backend);

	} else {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));
		g_object_unref (task);
	}
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
	GTask *task;
	GQueue *queue;

	g_return_val_if_fail (g_task_is_valid (result, backend), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_book_backend_modify_contacts), FALSE);

	task = G_TASK (result);

	book_backend_unblock_operations (backend, task);

	queue = g_task_propagate_pointer (task, error);
	if (!queue)
		return FALSE;

	while (!g_queue_is_empty (queue)) {
		EContact *contact;

		contact = g_queue_pop_head (queue);
		e_book_backend_notify_update (backend, contact);
		g_object_unref (contact);
	}

	e_book_backend_notify_complete (backend);
	g_queue_free (queue);

	return TRUE;
}

/**
 * e_book_backend_remove_contacts_sync:
 * @backend: an #EBookBackend
 * @uids: a %NULL-terminated array of contact ID strings
 * @opflags: bit-or of #EBookOperationFlags
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
				     guint32 opflags,
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
		backend, uids, opflags, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_book_backend_remove_contacts_finish (
		backend, result, error);

	e_async_closure_free (closure);

	return success;
}

/* Helper for e_book_backend_remove_contacts() */
static void
book_backend_remove_contacts_thread (GTask *task,
                                     gpointer source_object,
                                     gpointer task_data,
                                     GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;
	AsyncContext *async_context;

	backend = E_BOOK_BACKEND (source_object);
	async_context = task_data;

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->impl_remove_contacts != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	if (!e_book_backend_is_opened (backend)) {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_OPENED, NULL));

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, task);

		class->impl_remove_contacts (
			backend, data_book, opid, cancellable, (const gchar * const *) async_context->strv, async_context->opflags);
	}

	g_object_unref (data_book);
}

/**
 * e_book_backend_remove_contacts:
 * @backend: an #EBookBackend
 * @uids: (array zero-terminated=1): a %NULL-terminated array of contact ID strings
 * @opflags: bit-or of #EBookOperationFlags
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
				guint32 opflags,
				GCancellable *cancellable,
				GAsyncReadyCallback callback,
				gpointer user_data)
{
	EBookBackendClass *class;
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (uids != NULL);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->strv = g_strdupv ((gchar **) uids);
	async_context->opflags = opflags;

	task = g_task_new (backend, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_book_backend_remove_contacts);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	if (class->impl_remove_contacts != NULL) {
		book_backend_push_operation (
			backend, g_steal_pointer (&task), FALSE, book_backend_remove_contacts_thread);
		book_backend_dispatch_next_operation (backend);

	} else {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));
		g_object_unref (task);
	}
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
	GTask *task;
	GQueue *queue;

	g_return_val_if_fail (g_task_is_valid (result, backend), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_book_backend_remove_contacts), FALSE);

	task = G_TASK (result);

	book_backend_unblock_operations (backend, task);

	queue = g_task_propagate_pointer (task, error);
	if (!queue)
		return FALSE;

	while (!g_queue_is_empty (queue)) {
		gchar *uid;

		uid = g_queue_pop_head (queue);
		e_book_backend_notify_remove (backend, uid);
		g_free (uid);
	}

	e_book_backend_notify_complete (backend);
	g_queue_free (queue);

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
 * Returns: (transfer full): an #EContact, or %NULL on error
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
book_backend_get_contact_thread (GTask *task,
                                 gpointer source_object,
                                 gpointer task_data,
                                 GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;
	const gchar *uid;

	backend = E_BOOK_BACKEND (source_object);
	uid = task_data;

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->impl_get_contact != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	if (!e_book_backend_is_opened (backend)) {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_OPENED, NULL));

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, task);

		class->impl_get_contact (backend, data_book, opid, cancellable, uid);
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
	EBookBackendClass *class;
	GTask *task;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (uid != NULL);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);

	task = g_task_new (backend, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_book_backend_get_contact);
	g_task_set_task_data (task, g_strdup (uid), g_free);

	if (class->impl_get_contact != NULL) {
		book_backend_push_operation (
			backend, g_steal_pointer (&task), FALSE, book_backend_get_contact_thread);
		book_backend_dispatch_next_operation (backend);

	} else {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));
		g_object_unref (task);
	}
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
 * Returns: (transfer full): an #EContact, or %NULL on error
 *
 * Since: 3.10
 **/
EContact *
e_book_backend_get_contact_finish (EBookBackend *backend,
                                   GAsyncResult *result,
                                   GError **error)
{
	GTask *task;

	g_return_val_if_fail (g_task_is_valid (result, backend), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_book_backend_get_contact), FALSE);

	task = G_TASK (result);

	book_backend_unblock_operations (backend, task);

	return g_task_propagate_pointer (task, error);
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
book_backend_get_contact_list_thread (GTask *task,
                                      gpointer source_object,
                                      gpointer task_data,
                                      GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;
	const gchar *query;

	backend = E_BOOK_BACKEND (source_object);
	query = task_data;

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->impl_get_contact_list != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	if (!e_book_backend_is_opened (backend)) {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_OPENED, NULL));

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, task);

		class->impl_get_contact_list (backend, data_book, opid, cancellable, query);
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
	EBookBackendClass *class;
	GTask *task;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (query != NULL);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);

	task = g_task_new (backend, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_book_backend_get_contact_list);
	g_task_set_task_data (task, g_strdup (query), g_free);

	if (class->impl_get_contact_list != NULL) {
		book_backend_push_operation (
			backend, g_steal_pointer (&task), FALSE, book_backend_get_contact_list_thread);
		book_backend_dispatch_next_operation (backend);

	} else {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));
		g_object_unref (task);
	}
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
	GTask *task;
	GQueue *queue;

	g_return_val_if_fail (g_task_is_valid (result, backend), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_book_backend_get_contact_list), FALSE);
	g_return_val_if_fail (out_contacts != NULL, FALSE);

	task = G_TASK (result);

	book_backend_unblock_operations (backend, task);

	queue = g_task_propagate_pointer (task, error);
	if (!queue)
		return FALSE;

	e_queue_transfer (queue, out_contacts);
	g_queue_free (queue);

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
book_backend_get_contact_list_uids_thread (GTask *task,
                                           gpointer source_object,
                                           gpointer task_data,
                                           GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *class;
	EDataBook *data_book;
	const gchar *query;

	backend = E_BOOK_BACKEND (source_object);
	query = task_data;

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->impl_get_contact_list_uids != NULL);

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	if (!e_book_backend_is_opened (backend)) {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_OPENED, NULL));

	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, task);

		class->impl_get_contact_list_uids (backend, data_book, opid, cancellable, query);
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
	EBookBackendClass *class;
	GTask *task;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (query != NULL);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);

	task = g_task_new (backend, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_book_backend_get_contact_list_uids);
	g_task_set_task_data (task, g_strdup (query), g_free);

	if (class->impl_get_contact_list_uids != NULL) {
		book_backend_push_operation (
			backend, g_steal_pointer (&task), FALSE, book_backend_get_contact_list_uids_thread);
		book_backend_dispatch_next_operation (backend);

	} else {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));
		g_object_unref (task);
	}
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
	GTask *task;
	GQueue *queue;

	g_return_val_if_fail (g_task_is_valid (result, backend), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_book_backend_get_contact_list_uids), FALSE);
	g_return_val_if_fail (out_uids != NULL, FALSE);

	task = G_TASK (result);

	book_backend_unblock_operations (backend, task);

	queue = g_task_propagate_pointer (task, error);
	if (!queue)
		return FALSE;

	e_queue_transfer (queue, out_uids);
	g_queue_free (queue);

	return TRUE;
}

/**
 * e_book_backend_contains_email_sync:
 * @backend: an #EBookBackend
 * @email_address: an email address
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Checks whether contains an @email_address. When the @email_address
 * contains multiple addresses, then returns %TRUE when at least one
 * address exists in the address book.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE when found the @email_address, %FALSE on failure
 *
 * Since: 3.44
 **/
gboolean
e_book_backend_contains_email_sync (EBookBackend *backend,
				    const gchar *email_address,
				    GCancellable *cancellable,
				    GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);
	g_return_val_if_fail (email_address != NULL, FALSE);

	closure = e_async_closure_new ();

	e_book_backend_contains_email (backend, email_address, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_book_backend_contains_email_finish (backend, result, error);

	e_async_closure_free (closure);

	return success;
}

/* Helper for e_book_backend_contains_email() */
static void
book_backend_contains_email_thread (GTask *task,
                                    gpointer source_object,
                                    gpointer task_data,
                                    GCancellable *cancellable)
{
	EBookBackend *backend;
	EBookBackendClass *klass;
	EDataBook *data_book;
	const gchar *email_address;

	backend = E_BOOK_BACKEND (source_object);
	email_address = task_data;

	klass = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (klass != NULL);

	if (!klass->impl_contains_email) {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));
		return;
	}

	data_book = e_book_backend_ref_data_book (backend);
	g_return_if_fail (data_book != NULL);

	if (!e_book_backend_is_opened (backend)) {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_OPENED, NULL));
	} else {
		guint32 opid;

		opid = book_backend_stash_operation (backend, task);

		klass->impl_contains_email (backend, data_book, opid, cancellable, email_address);
	}

	g_object_unref (data_book);
}

/**
 * e_book_backend_contains_email:
 * @backend: an #EBookBackend
 * @email_address: an email address
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously checks whether contains an @email_address. When the @email_address
 * contains multiple addresses, then returns %TRUE when at least one
 * address exists in the address book.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_book_backend_contains_email_finish() to get the result of the
 * operation.
 *
 * Since: 3.44
 **/
void
e_book_backend_contains_email (EBookBackend *backend,
			       const gchar *email_address,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer user_data)
{
	EBookBackendClass *klass;
	GTask *task;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (email_address != NULL);

	klass = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (klass != NULL);

	task = g_task_new (backend, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_book_backend_contains_email);
	g_task_set_task_data (task, g_strdup (email_address), g_free);

	if (klass->impl_contains_email != NULL) {
		book_backend_push_operation (
			backend, g_steal_pointer (&task), FALSE, book_backend_contains_email_thread);
		book_backend_dispatch_next_operation (backend);

	} else {
		g_task_return_error (task, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));
		g_object_unref (task);
	}
}

/**
 * e_book_backend_contains_email_finish:
 * @backend: an #EBookBackend
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_book_backend_contains_email().
 *
 * If an error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.44
 **/
gboolean
e_book_backend_contains_email_finish (EBookBackend *backend,
				      GAsyncResult *result,
				      GError **error)
{
	GTask *task;

	g_return_val_if_fail (g_task_is_valid (result, backend), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_book_backend_contains_email), FALSE);

	task = G_TASK (result);

	book_backend_unblock_operations (backend, task);

	return g_task_propagate_boolean (task, error);
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
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->impl_start_view);

	if ((e_data_book_view_get_flags (view) & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY) != 0 &&
	    class->impl_dup_view_contacts == book_backend_dup_view_contacts) {
		EBookClientViewSortFields *sort_fields;
		GObject *view_watcher;
		gsize view_id;

		view_id = e_data_book_view_get_id (view);
		sort_fields = e_book_backend_dup_view_sort_fields (backend, view_id);

		view_watcher = e_data_book_view_watcher_memory_new (backend, view);
		e_data_book_view_watcher_memory_take_sort_fields (E_DATA_BOOK_VIEW_WATCHER_MEMORY (view_watcher), sort_fields);

		e_book_backend_take_view_user_data (backend, view_id, view_watcher);
	}

	class->impl_start_view (backend, view);

	e_util_call_malloc_trim ();
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
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->impl_stop_view != NULL);

	class->impl_stop_view (backend, view);

	if ((e_data_book_view_get_flags (view) & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY) != 0 &&
	    class->impl_dup_view_contacts == book_backend_dup_view_contacts) {
		e_book_backend_take_view_user_data (backend, e_data_book_view_get_id (view), NULL);
	}

	e_util_call_malloc_trim ();
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
	ViewData *vd;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_mutex_lock (&backend->priv->views_mutex);

	vd = g_new0 (ViewData, 1);
	vd->view = g_object_ref (view);

	g_hash_table_insert (backend->priv->views, GSIZE_TO_POINTER (e_data_book_view_get_id (view)), vd);

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
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	/* In case the view holds the last reference to backend */
	g_object_ref (backend);

	g_mutex_lock (&backend->priv->views_mutex);

	g_hash_table_remove (backend->priv->views, GSIZE_TO_POINTER (e_data_book_view_get_id (view)));

	g_mutex_unlock (&backend->priv->views_mutex);

	g_object_unref (backend);
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
 * Returns: (transfer full) (element-type EDataBookView): a list of book views
 *
 * Since: 3.8
 **/
GList *
e_book_backend_list_views (EBookBackend *backend)
{
	GList *list = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	g_mutex_lock (&backend->priv->views_mutex);

	if (backend->priv->views) {
		GHashTableIter iter;
		gpointer value;

		g_hash_table_iter_init (&iter, backend->priv->views);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			ViewData *vd = value;

			list = g_list_prepend (list, g_object_ref (vd->view));
		}
	}

	g_mutex_unlock (&backend->priv->views_mutex);

	return g_list_reverse (list);
}

/**
 * EBookBackendForeachViewFunc:
 * @backend: an #EBookBackend
 * @view: an #EDataBookView
 * @user_data: user data for the function
 *
 * Callback function used by e_book_backend_foreach_view().
 *
 * Returns: %TRUE, to continue, %FALSE to stop further processing.
 *
 * Since: 3.34
 **/

/**
 * e_book_backend_foreach_view:
 * @backend: an #EBookBackend
 * @func: (scope call) (closure user_data): an #EBookBackendForeachViewFunc function to call
 * @user_data: user data to pass to @func
 *
 * Calls @func for each existing view (as returned by e_book_backend_list_views()).
 * The @func can return %FALSE to stop early.
 *
 * Returns: whether the call had been stopped by @func
 *
 * Since: 3.34
 **/
gboolean
e_book_backend_foreach_view (EBookBackend *backend,
			     EBookBackendForeachViewFunc func,
			     gpointer user_data)
{
	GList *views, *link;
	gboolean stopped = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	views = e_book_backend_list_views (backend);

	for (link = views; link && !stopped; link = g_list_next (link)) {
		stopped = !func (backend, link->data, user_data);
	}

	g_list_free_full (views, g_object_unref);

	return stopped;
}

struct NotifyProgressData {
	gboolean only_completed_views;
	gint percent;
	const gchar *message;
};

static gboolean
ebb_notify_progress_cb (EBookBackend *backend,
			EDataBookView *view,
			gpointer user_data)
{
	struct NotifyProgressData *npd = user_data;

	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (view), FALSE);
	g_return_val_if_fail (npd != NULL, FALSE);

	if (!npd->only_completed_views || e_data_book_view_is_completed (view))
		e_data_book_view_notify_progress (view, npd->percent, npd->message);

	return TRUE;
}

/**
 * e_book_backend_foreach_view_notify_progress:
 * @backend: an #EBookBackend
 * @only_completed_views: whether notify in completed views only
 * @percent: percent complete
 * @message: (nullable): message describing the operation in progress, or %NULL
 *
 * Notifies each view of the @backend about progress. When @only_completed_views
 * is %TRUE, notifies only completed views.
 *
 * Since: 3.34
 **/
void
e_book_backend_foreach_view_notify_progress (EBookBackend *backend,
					     gboolean only_completed_views,
					     gint percent,
					     const gchar *message)
{
	struct NotifyProgressData npd;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	npd.only_completed_views = only_completed_views;
	npd.percent = percent;
	npd.message = message;

	e_book_backend_foreach_view (backend, ebb_notify_progress_cb, &npd);
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
	g_return_val_if_fail (class != NULL, NULL);
	g_return_val_if_fail (class->impl_get_backend_property != NULL, NULL);

	return class->impl_get_backend_property (backend, prop_name);
}

/**
 * e_book_backend_is_opened:
 * @backend: an #EBookBackend
 *
 * Checks if @backend's storage has been opened (and
 * authenticated, if necessary) and the backend itself
 * is ready for accessing. This property is changed automatically
 * after the @backend is successfully opened.
 *
 * Returns: %TRUE if fully opened, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_is_opened (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	return backend->priv->opened;
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
 * Returns: (transfer full) (nullable): A new #EDataBookDirect object, or %NULL if
 *          @backend does not support direct access
 *
 * Since: 3.8
 */
EDataBookDirect *
e_book_backend_get_direct_book (EBookBackend *backend)
{
	EBookBackendClass *class;
	EDataBookDirect *direct_book = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_val_if_fail (class != NULL, NULL);

	if (class->impl_get_direct_book != NULL)
		direct_book = class->impl_get_direct_book (backend);

	return direct_book;
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
	EBookBackendClass *class;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class != NULL);

	if (class->impl_configure_direct)
		class->impl_configure_direct (backend, config);
}

/**
 * e_book_backend_set_locale:
 * @backend: an #EBookBackend
 * @locale: the new locale for the addressbook
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Notify the addressbook backend that the current locale has
 * changed, this is important for backends which support
 * ordered result lists which are locale sensitive.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.12
 */
gboolean
e_book_backend_set_locale (EBookBackend *backend,
                           const gchar *locale,
                           GCancellable *cancellable,
                           GError **error)
{
	EBookBackendClass *class;
	/* If the backend does not support locales, just happily return */
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_val_if_fail (class != NULL, FALSE);

	if (class->impl_set_locale) {
		g_object_ref (backend);

		success = class->impl_set_locale (backend, locale, cancellable, error);

		if (success)
			e_book_backend_notify_complete (backend);

		g_object_unref (backend);
	}

	return success;
}

/**
 * e_book_backend_dup_locale:
 * @backend: an #EBookBackend
 *
 * Fetches a copy of the currently configured locale for the addressbook
 *
 * Returns: A copy of the currently configured locale for the addressbook.
 *   Free with g_free() when done with it.
 *
 * Since: 3.12
 */
gchar *
e_book_backend_dup_locale (EBookBackend *backend)
{
	EBookBackendClass *class;
	gchar *locale = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_val_if_fail (class != NULL, NULL);

	if (class->impl_dup_locale) {
		g_object_ref (backend);

		locale = class->impl_dup_locale (backend);

		g_object_unref (backend);
	}

	return locale;
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
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->impl_notify_update != NULL);

	class->impl_notify_update (backend, contact);
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
 * @backend: an #EBookBackend
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
 * @prop_value: (nullable): new property value
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

	data_book = e_book_backend_ref_data_book (backend);

	if (data_book != NULL) {
		e_data_book_report_backend_property_changed (data_book, prop_name, prop_value ? prop_value : "");
		g_object_unref (data_book);
	}
}

/**
 * e_book_backend_prepare_for_completion:
 * @backend: an #EBookBackend
 * @opid: an operation ID given to #EDataBook
 *
 * Obtains the #GTask for @opid.
 *
 * <note>
 *   <para>
 *     This is a temporary function to serve #EDataBook's "respond"
 *     functions until they can be removed.  Nothing else should be
 *     calling this function.
 *   </para>
 * </note>
 *
 * Returns: (transfer full): a #GTask
 *
 * Since: 3.10
 **/
GTask *
e_book_backend_prepare_for_completion (EBookBackend *backend,
                                       guint32 opid)
{
	GTask *task;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);
	g_return_val_if_fail (opid > 0, NULL);

	task = book_backend_claim_operation (backend, opid);
	g_return_val_if_fail (task != NULL, NULL);

	return task;
}

/**
 * e_book_backend_create_cursor:
 * @backend: an #EBookBackend
 * @sort_fields: the #EContactFields to sort by
 * @sort_types: the #EBookCursorSortTypes for the sorted fields
 * @n_fields: the number of fields in the @sort_fields and @sort_types
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #EDataBookCursor for the given backend if the backend
 * has cursor support. If the backend does not support cursors then
 * an %E_CLIENT_ERROR_NOT_SUPPORTED error will be set in @error.
 *
 * Backends can also refuse to create cursors for some values of @sort_fields
 * and report more specific errors.
 *
 * The returned cursor belongs to @backend and should be destroyed
 * with e_book_backend_delete_cursor() when no longer needed.
 *
 * Returns: (transfer none): A newly created cursor, the cursor belongs
 *    to the backend and should not be unreffed, or %NULL on error
 *
 * Since: 3.12
 */
EDataBookCursor *
e_book_backend_create_cursor (EBookBackend *backend,
                              EContactField *sort_fields,
                              EBookCursorSortType *sort_types,
                              guint n_fields,
                              GError **error)
{
	EBookBackendClass *class;
	EDataBookCursor *cursor = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_val_if_fail (class != NULL, NULL);

	if (class->impl_create_cursor) {
		g_object_ref (backend);

		cursor = class->impl_create_cursor (backend, sort_fields, sort_types, n_fields, error);

		g_object_unref (backend);
	} else {
		g_set_error (
			error,
			E_CLIENT_ERROR,
			E_CLIENT_ERROR_NOT_SUPPORTED,
			_("Addressbook backend does not support cursors"));
	}

	return cursor;
}

/**
 * e_book_backend_delete_cursor:
 * @backend: an #EBookBackend
 * @cursor: the #EDataBookCursor to destroy
 * @error: return location for a #GError, or %NULL
 *
 * Requests @backend to release and destroy @cursor, this
 * will trigger an %E_CLIENT_ERROR_INVALID_ARG error if @cursor
 * is not owned by @backend.
 *
 * Returns: Whether @cursor was successfully deleted.
 *
 * Since: 3.12
 */
gboolean
e_book_backend_delete_cursor (EBookBackend *backend,
                              EDataBookCursor *cursor,
                              GError **error)
{
	EBookBackendClass *class;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	class = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_val_if_fail (class != NULL, FALSE);

	g_object_ref (backend);

	if (class->impl_delete_cursor)
		success = class->impl_delete_cursor (backend, cursor, error);
	else
		g_warning ("Backend asked to delete a cursor, but does not support cursors");

	g_object_unref (backend);

	return success;
}


static void
custom_op_func_data_free (CustomOpFuncData *data)
{
	if (!data)
		return;

	if (data->custom_func_user_data_free)
		g_clear_pointer (&data->custom_func_user_data, data->custom_func_user_data_free);

	g_free (data);
}

static void
on_custom_operation_finished (GObject *source_object,
                              GAsyncResult *res,
                              gpointer user_data)
{
	EBookBackend *backend = E_BOOK_BACKEND (source_object);
	GTask *task = G_TASK (res);
	GError *local_error = NULL;

	book_backend_unblock_operations (backend, task);

	if (!g_task_propagate_boolean (task, &local_error)) {
		if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			e_book_backend_notify_error (backend, local_error->message);
	}

	g_clear_error (&local_error);
}

static void
e_book_backend_custom_operation_thread (GTask *task,
                                        gpointer source_object,
                                        gpointer task_data,
                                        GCancellable *cancellable)
{
	EBookBackend *backend = source_object;
	CustomOpFuncData *data = task_data;

	if (!g_task_return_error_if_cancelled (task)) {
		GError *local_error = NULL;
		data->custom_func (backend, data->custom_func_user_data, cancellable, &local_error);
		if (!local_error) {
			g_task_return_boolean (task, TRUE);
		} else {
			g_task_return_error (task, g_steal_pointer (&local_error));
		}

	}
}

/**
 * e_book_backend_schedule_custom_operation:
 * @book_backend: an #EBookBackend
 * @use_cancellable: (nullable): an optional #GCancellable to use for @func
 * @func: a function to call in a dedicated thread
 * @user_data: user data being passed to @func
 * @user_data_free: (nullable): optional destroy call back for @user_data
 *
 * Schedules user function @func to be run in a dedicated thread as
 * a blocking operation.
 *
 * The function adds its own reference to @use_cancellable, if not %NULL.
 *
 * The error returned from @func is propagated to client using
 * e_book_backend_notify_error() function. If it's not desired,
 * then left the error unchanged and notify about errors manually.
 *
 * Since: 3.26
 **/
void
e_book_backend_schedule_custom_operation (EBookBackend *book_backend,
					  GCancellable *use_cancellable,
					  EBookBackendCustomOpFunc func,
					  gpointer user_data,
					  GDestroyNotify user_data_free)
{
	GTask *task;
	CustomOpFuncData *data;

	g_return_if_fail (E_IS_BOOK_BACKEND (book_backend));
	g_return_if_fail (func != NULL);

	data = g_new0 (CustomOpFuncData, 1);
	data->custom_func = func;
	data->custom_func_user_data = user_data;
	data->custom_func_user_data_free = user_data_free;

	task = g_task_new (book_backend, use_cancellable, on_custom_operation_finished, NULL);
	g_task_set_source_tag (task, e_book_backend_schedule_custom_operation);
	g_task_set_task_data (task, g_steal_pointer (&data), (GDestroyNotify) custom_op_func_data_free);

	book_backend_push_operation (
		book_backend, g_steal_pointer (&task), TRUE, e_book_backend_custom_operation_thread);

	book_backend_dispatch_next_operation (book_backend);
}

/**
 * e_book_backend_ref_view:
 * @backend: an #EBookBackend
 * @view_id: a view identifier
 *
 * References an #EDataBookView by its identifier.
 *
 * Unref the returned non-NULL view with g_object_unref(),
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): a referenced #EDataBookView corresponding
 *    to the given @view_id, or %NULL, when it cannot be found
 *
 * Since: 3.50
 **/
EDataBookView *
e_book_backend_ref_view (EBookBackend *backend,
			 gsize view_id)
{
	ViewData *vd;
	EDataBookView *view = NULL;

	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);

	g_mutex_lock (&backend->priv->views_mutex);

	vd = e_book_backend_get_view_data_by_id_locked (backend,view_id);

	if (vd && vd->view)
		view = g_object_ref (vd->view);

	g_mutex_unlock (&backend->priv->views_mutex);

	return view;
}

/**
 * e_book_backend_ref_view_user_data:
 * @backend: an #EBookBackend
 * @view_id: a view identifier
 *
 * References user data previously set by e_book_backend_take_view_user_data()
 * for the @view_id.
 *
 * Free the returned non-NULL object with g_object_unref(),
 * when no longer needed.
 *
 * Returns: (transfer full) (type GObject): previously set user data for the @view_id,
 *   or %NULL when none had been set yet or when the view does not exist.
 *
 * Since: 3.50
 **/
gpointer
e_book_backend_ref_view_user_data (EBookBackend *backend,
				   gsize view_id)
{
	ViewData *vd;
	GObject *user_data = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	g_mutex_lock (&backend->priv->views_mutex);

	vd = e_book_backend_get_view_data_by_id_locked (backend, view_id);

	if (vd && vd->user_data)
		user_data = g_object_ref (vd->user_data);

	g_mutex_unlock (&backend->priv->views_mutex);

	return user_data;
}

/**
 * e_book_backend_take_view_user_data:
 * @backend: an #EBookBackend
 * @view_id: a view identifier
 * @user_data: (nullable) (transfer full): user data to set
 *
 * Sets the user data for the @view_id. The function assumes ownership
 * of the @user_data. The @user_data can be %NULL, which unsets
 * the current user data for the view.
 *
 * This is primarily aimed as a helper for backend implementations
 * of the manual query views (%E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY).
 *
 * Since: 3.50
 **/
void
e_book_backend_take_view_user_data (EBookBackend *backend,
				    gsize view_id,
				    GObject *user_data)
{
	ViewData *vd;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	if (user_data)
		g_return_if_fail (G_IS_OBJECT (user_data));

	g_mutex_lock (&backend->priv->views_mutex);

	vd = e_book_backend_get_view_data_by_id_locked (backend, view_id);

	if (vd && vd->user_data != user_data) {
		g_clear_object (&vd->user_data);
		vd->user_data = user_data;
	} else {
		g_clear_object (&user_data);
	}

	g_mutex_unlock (&backend->priv->views_mutex);
}

/**
 * e_book_backend_dup_view_sort_fields:
 * @backend: an #EBookBackend
 * @view_id: a view identifier
 *
 * Returns currently used sort fields for manual query views. The returned
 * array is NULL only if the view could not be found. The default sort is
 * by the file-as filed in ascending order.
 *
 * The array is terminated by an item with an %E_CONTACT_FIELD_LAST field.
 *
 * Free the returned array with e_book_client_view_sort_fields_free(),
 * when no longer needed.
 *
 * Note: This function should be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY views.
 *
 * Returns: (transfer full): current sort fields for the @view_id, as an #EBookClientViewSortFields
 *    array, or %NULL, when the view could not be found.
 *
 * Since: 3.50
 **/
EBookClientViewSortFields *
e_book_backend_dup_view_sort_fields (EBookBackend *backend,
				     gsize view_id)
{
	EBookClientViewSortFields *fields = NULL;
	ViewData *vd;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	g_mutex_lock (&backend->priv->views_mutex);

	vd = e_book_backend_get_view_data_by_id_locked (backend, view_id);

	if (vd && vd->sort_fields)
		fields = e_book_client_view_sort_fields_copy (vd->sort_fields);

	if (vd && !fields) {
		EBookClientViewSortFields tmp_fields[] = {
			{ E_CONTACT_FILE_AS, E_BOOK_CURSOR_SORT_ASCENDING },
			{ E_CONTACT_FIELD_LAST, E_BOOK_CURSOR_SORT_ASCENDING }
		};

		fields = e_book_client_view_sort_fields_copy (tmp_fields);
	}

	g_mutex_unlock (&backend->priv->views_mutex);

	return fields;
}

/**
 * e_book_backend_set_view_sort_fields:
 * @backend: an #EBookBackend
 * @view_id: a view identifier
 * @fields: (nullable): an array of #EBookClientViewSortFields, or %NULL
 *
 * Sets the sort fields for the view identified by the @view_id.
 * The @fields array should be terminated by an item, which has
 * the field member set to %E_CONTACT_FIELD_LAST.
 *
 * When the @fields is %NULL, the sort by file-as in ascending order
 * is used instead.
 *
 * The default implementation of this virtual method stores
 * the @fields into the internal structure for the @backend,
 * to be available by e_book_backend_dup_view_sort_fields().
 *
 * Note: This function should be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY views.
 *
 * Since: 3.50
 **/
void
e_book_backend_set_view_sort_fields (EBookBackend *backend,
				     gsize view_id,
				     const EBookClientViewSortFields *fields)
{
	EBookBackendClass *klass;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	klass = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_if_fail (klass != NULL);
	g_return_if_fail (klass->impl_set_view_sort_fields != NULL);

	klass->impl_set_view_sort_fields (backend, view_id, fields);
}

/**
 * e_book_backend_get_view_n_total:
 * @backend: an #EBookBackend
 * @view_id: a view identifier
 *
 * Returns how many contacts the view identified by @view_id
 * contains.
 *
 * The default implementation of this virtual method returns
 * the value previously set by e_book_backend_set_view_n_total().
 *
 * Note: This function should be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY views.
 *
 * Returns: how many contacts the view identified by @view_id
 *    contains.
 *
 * Since: 3.50
 **/
guint
e_book_backend_get_view_n_total (EBookBackend *backend,
				 gsize view_id)
{
	EBookBackendClass *klass;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), 0);

	klass = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_val_if_fail (klass != NULL, 0);
	g_return_val_if_fail (klass->impl_get_view_n_total != NULL, 0);

	return klass->impl_get_view_n_total (backend, view_id);
}

/**
 * e_book_backend_set_view_n_total:
 * @backend: an #EBookBackend
 * @view_id: a view identifier
 * @n_total: the value to set
 *
 * Stores how many contacts the view identified by @view_id
 * contains. It also sets the @n_total to the corresponding
 * #EDataBookView, if such exists. The function does nothing
 * when the view cannot be found.
 *
 * Note: This function should be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY views.
 *
 * Since: 3.50
 **/
void
e_book_backend_set_view_n_total (EBookBackend *backend,
				 gsize view_id,
				 guint n_total)
{
	ViewData *vd;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_mutex_lock (&backend->priv->views_mutex);

	vd = e_book_backend_get_view_data_by_id_locked (backend, view_id);

	if (vd && vd->n_total != n_total) {
		vd->n_total = n_total;

		e_data_book_view_set_n_total (vd->view, vd->n_total);
	}

	g_mutex_unlock (&backend->priv->views_mutex);
}

/**
 * e_book_backend_dup_view_indices:
 * @backend: an #EBookBackend
 * @view_id: a view identifier
 *
 * Returns a list of #EBookIndices holding indices of the contacts
 * in the view identified by @view_id. The array is terminated by an item
 * with chr member being %NULL.
 *
 * The default implementation returns an array previously set
 * by e_book_backend_set_view_indices().
 *
 * Free the returned array with e_book_indices_free(), when no longer needed.
 *
 * Note: This function should be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY views.
 *
 * Returns: (transfer full) (nullable): an array of #EBookIndices, or %NULL
 *
 * Since: 3.50
 **/
EBookIndices *
e_book_backend_dup_view_indices (EBookBackend *backend,
				 gsize view_id)
{
	EBookBackendClass *klass;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	klass = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_val_if_fail (klass != NULL, NULL);
	g_return_val_if_fail (klass->impl_dup_view_indices != NULL, NULL);

	return klass->impl_dup_view_indices (backend, view_id);
}

/**
 * e_book_backend_set_view_indices:
 * @backend: an #EBookBackend
 * @view_id: a view identifier
 * @indices: (nullable): an array of #EBookIndices, or %NULL
 *
 * Stores current @indices for the view identified by the @view_id and,
 * if such exists, notifies about it also the corresponding #EDataBookView.
 * The array is terminated by an item with chr member being %NULL.
 *
 * Note: This function should be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY views.
 *
 * Since: 3.50
 **/
void
e_book_backend_set_view_indices (EBookBackend *backend,
				 gsize view_id,
				 const EBookIndices *indices)
{
	ViewData *vd;

	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_mutex_lock (&backend->priv->views_mutex);

	vd = e_book_backend_get_view_data_by_id_locked (backend, view_id);

	if (vd && vd->indices != indices) {
		e_book_indices_free (vd->indices);
		vd->indices = e_book_indices_copy (indices);

		e_data_book_view_set_indices (vd->view, vd->indices);
	}

	g_mutex_unlock (&backend->priv->views_mutex);
}

/**
 * e_book_backend_dup_view_contacts:
 * @backend: an #EBookBackend
 * @view_id: a view identifier
 * @range_start: 0-based range start to retrieve the contacts for
 * @range_length: how many contacts to retrieve
 *
 * Returns @range_length contacts from 0-based index @range_start
 * in the view identified by the @view_id.
 * When there are asked more than e_book_backend_get_view_n_total()
 * contacts only those up to the total number of contacts are read.
 * Asking for out of range contacts results in an error, though
 * it can return less than @range_length contacts.
 *
 * The default implementation tracks the view's content in memory
 * and returns the contacts as needed. The subclasses can do more
 * efficient implementation.
 *
 * Note: This function should be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY views.
 *
 * Returns: (transfer container) (element-type EContact) (nullable):
 *    an array of the contacts, or %NULL, when the view cannot be found
 *    or when the @range_start is out of bounds.
 *
 * Since: 3.50
 **/
GPtrArray *
e_book_backend_dup_view_contacts (EBookBackend *backend,
				  gsize view_id,
				  guint range_start,
				  guint range_length)
{
	EBookBackendClass *klass;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	klass = E_BOOK_BACKEND_GET_CLASS (backend);
	g_return_val_if_fail (klass != NULL, NULL);
	g_return_val_if_fail (klass->impl_dup_view_contacts != NULL, NULL);

	return klass->impl_dup_view_contacts (backend, view_id, range_start, range_length);
}

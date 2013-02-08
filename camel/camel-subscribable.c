/*
 * camel-subscribable.c
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

#include "camel-subscribable.h"

#include <config.h>
#include <glib/gi18n-lib.h>

#include "camel-debug.h"
#include "camel-session.h"
#include "camel-vtrash-folder.h"

typedef struct _AsyncContext AsyncContext;
typedef struct _SignalData SignalData;

struct _AsyncContext {
	gchar *folder_name;
};

struct _SignalData {
	CamelSubscribable *subscribable;
	CamelFolderInfo *folder_info;
};

enum {
	FOLDER_SUBSCRIBED,
	FOLDER_UNSUBSCRIBED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_INTERFACE (CamelSubscribable, camel_subscribable, CAMEL_TYPE_STORE)

static void
async_context_free (AsyncContext *async_context)
{
	g_free (async_context->folder_name);

	g_slice_free (AsyncContext, async_context);
}

static void
signal_data_free (SignalData *signal_data)
{
	if (signal_data->subscribable != NULL)
		g_object_unref (signal_data->subscribable);

	if (signal_data->folder_info != NULL)
		camel_folder_info_free (signal_data->folder_info);

	g_slice_free (SignalData, signal_data);
}

static gboolean
subscribable_emit_folder_subscribed_cb (gpointer user_data)
{
	SignalData *signal_data = user_data;

	g_signal_emit (
		signal_data->subscribable,
		signals[FOLDER_SUBSCRIBED], 0,
		signal_data->folder_info);

	return FALSE;
}

static gboolean
subscribable_emit_folder_unsubscribed_cb (gpointer user_data)
{
	SignalData *signal_data = user_data;

	g_signal_emit (
		signal_data->subscribable,
		signals[FOLDER_UNSUBSCRIBED], 0,
		signal_data->folder_info);

	return FALSE;
}

static void
subscribable_delete_cached_folder (CamelStore *store,
                                   const gchar *folder_name)
{
	CamelFolder *folder;
	CamelVeeFolder *vfolder;

	/* XXX Copied from camel-store.c.  Should this be public? */

	if (store->folders == NULL)
		return;

	folder = camel_object_bag_get (store->folders, folder_name);
	if (folder == NULL)
		return;

	if (store->flags & CAMEL_STORE_VTRASH) {
		folder_name = CAMEL_VTRASH_NAME;
		vfolder = camel_object_bag_get (store->folders, folder_name);
		if (vfolder != NULL) {
			camel_vee_folder_remove_folder (vfolder, folder, NULL);
			g_object_unref (vfolder);
		}
	}

	if (store->flags & CAMEL_STORE_VJUNK) {
		folder_name = CAMEL_VJUNK_NAME;
		vfolder = camel_object_bag_get (store->folders, folder_name);
		if (vfolder != NULL) {
			camel_vee_folder_remove_folder (vfolder, folder, NULL);
			g_object_unref (vfolder);
		}
	}

	camel_folder_delete (folder);

	camel_object_bag_remove (store->folders, folder);
	g_object_unref (folder);
}

static void
subscribable_subscribe_folder_thread (GSimpleAsyncResult *simple,
                                      GObject *object,
                                      GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_subscribable_subscribe_folder_sync (
		CAMEL_SUBSCRIBABLE (object),
		async_context->folder_name,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
subscribable_subscribe_folder (CamelSubscribable *subscribable,
                               const gchar *folder_name,
                               gint io_priority,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->folder_name = g_strdup (folder_name);

	simple = g_simple_async_result_new (
		G_OBJECT (subscribable), callback,
		user_data, subscribable_subscribe_folder);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, subscribable_subscribe_folder_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
subscribable_subscribe_folder_finish (CamelSubscribable *subscribable,
                                      GAsyncResult *result,
                                      GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (subscribable),
		subscribable_subscribe_folder), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
subscribable_unsubscribe_folder_thread (GSimpleAsyncResult *simple,
                                        GObject *object,
                                        GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_subscribable_unsubscribe_folder_sync (
		CAMEL_SUBSCRIBABLE (object),
		async_context->folder_name,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
subscribable_unsubscribe_folder (CamelSubscribable *subscribable,
                                 const gchar *folder_name,
                                 gint io_priority,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->folder_name = g_strdup (folder_name);

	simple = g_simple_async_result_new (
		G_OBJECT (subscribable), callback,
		user_data, subscribable_unsubscribe_folder);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, subscribable_unsubscribe_folder_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
subscribable_unsubscribe_folder_finish (CamelSubscribable *subscribable,
                                        GAsyncResult *result,
                                        GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (subscribable),
		subscribable_unsubscribe_folder), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
camel_subscribable_default_init (CamelSubscribableInterface *interface)
{
	interface->subscribe_folder = subscribable_subscribe_folder;
	interface->subscribe_folder_finish = subscribable_subscribe_folder_finish;
	interface->unsubscribe_folder = subscribable_unsubscribe_folder;
	interface->unsubscribe_folder_finish = subscribable_unsubscribe_folder_finish;

	signals[FOLDER_SUBSCRIBED] = g_signal_new (
		"folder-subscribed",
		G_OBJECT_CLASS_TYPE (interface),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (
			CamelSubscribableInterface,
			folder_subscribed),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	signals[FOLDER_UNSUBSCRIBED] = g_signal_new (
		"folder-unsubscribed",
		G_OBJECT_CLASS_TYPE (interface),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (
			CamelSubscribableInterface,
			folder_unsubscribed),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);
}

/**
 * camel_subscribable_folder_is_subscribed:
 * @subscribable: a #CamelSubscribable
 * @folder_name: full path of the folder
 *
 * Find out if a folder has been subscribed to.
 *
 * Returns: %TRUE if the folder has been subscribed to or %FALSE otherwise
 *
 * Since: 3.2
 **/
gboolean
camel_subscribable_folder_is_subscribed (CamelSubscribable *subscribable,
                                         const gchar *folder_name)
{
	CamelSubscribableInterface *interface;
	gboolean is_subscribed;

	g_return_val_if_fail (CAMEL_IS_SUBSCRIBABLE (subscribable), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);

	interface = CAMEL_SUBSCRIBABLE_GET_INTERFACE (subscribable);
	g_return_val_if_fail (interface->folder_is_subscribed != NULL, FALSE);

	camel_store_lock (
		CAMEL_STORE (subscribable),
		CAMEL_STORE_FOLDER_LOCK);

	is_subscribed = interface->folder_is_subscribed (
		subscribable, folder_name);

	camel_store_unlock (
		CAMEL_STORE (subscribable),
		CAMEL_STORE_FOLDER_LOCK);

	return is_subscribed;
}

/**
 * camel_subscribable_subscribe_folder_sync:
 * @subscribable: a #CamelSubscribable
 * @folder_name: full path of the folder
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Subscribes to the folder described by @folder_name.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.2
 **/
gboolean
camel_subscribable_subscribe_folder_sync (CamelSubscribable *subscribable,
                                          const gchar *folder_name,
                                          GCancellable *cancellable,
                                          GError **error)
{
	CamelSubscribableInterface *interface;
	const gchar *message;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_SUBSCRIBABLE (subscribable), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);

	interface = CAMEL_SUBSCRIBABLE_GET_INTERFACE (subscribable);
	g_return_val_if_fail (interface->subscribe_folder_sync != NULL, FALSE);

	camel_store_lock (
		CAMEL_STORE (subscribable),
		CAMEL_STORE_FOLDER_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		success = FALSE;
		goto exit;
	}

	/* Need to establish a connection before subscribing. */
	success = camel_service_connect_sync (
		CAMEL_SERVICE (subscribable), cancellable, error);
	if (!success)
		goto exit;

	message = _("Subscribing to folder '%s'");
	camel_operation_push_message (cancellable, message, folder_name);

	success = interface->subscribe_folder_sync (
		subscribable, folder_name, cancellable, error);
	CAMEL_CHECK_GERROR (
		subscribable, subscribe_folder_sync, success, error);

	camel_operation_pop_message (cancellable);

exit:
	camel_store_unlock (
		CAMEL_STORE (subscribable),
		CAMEL_STORE_FOLDER_LOCK);

	return success;
}

/**
 * camel_subscribable_subscribe_folder:
 * @subscribable: a #CamelSubscribable
 * @folder_name: full path of the folder
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously subscribes to the folder described by @folder_name.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_subscribable_subscribe_folder_finish() to get the result of
 * the operation.
 *
 * Since: 3.2
 **/
void
camel_subscribable_subscribe_folder (CamelSubscribable *subscribable,
                                     const gchar *folder_name,
                                     gint io_priority,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	CamelSubscribableInterface *interface;

	g_return_if_fail (CAMEL_IS_SUBSCRIBABLE (subscribable));
	g_return_if_fail (folder_name != NULL);

	interface = CAMEL_SUBSCRIBABLE_GET_INTERFACE (subscribable);
	g_return_if_fail (interface->subscribe_folder != NULL);

	interface->subscribe_folder (
		subscribable, folder_name, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_subscribable_subscribe_folder_finish:
 * @subscribable: a #CamelSubscribable
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_subscribable_subscribe_folder().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.2
 **/
gboolean
camel_subscribable_subscribe_folder_finish (CamelSubscribable *subscribable,
                                            GAsyncResult *result,
                                            GError **error)
{
	CamelSubscribableInterface *interface;

	g_return_val_if_fail (CAMEL_IS_SUBSCRIBABLE (subscribable), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	interface = CAMEL_SUBSCRIBABLE_GET_INTERFACE (subscribable);
	g_return_val_if_fail (
		interface->subscribe_folder_finish != NULL, FALSE);

	return interface->subscribe_folder_finish (
		subscribable, result, error);
}

/**
 * camel_subscribable_unsubscribe_folder_sync:
 * @subscribable: a #CamelSubscribable
 * @folder_name: full path of the folder
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Unsubscribes from the folder described by @folder_name.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.2
 **/
gboolean
camel_subscribable_unsubscribe_folder_sync (CamelSubscribable *subscribable,
                                            const gchar *folder_name,
                                            GCancellable *cancellable,
                                            GError **error)
{
	CamelSubscribableInterface *interface;
	const gchar *message;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_SUBSCRIBABLE (subscribable), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);

	interface = CAMEL_SUBSCRIBABLE_GET_INTERFACE (subscribable);
	g_return_val_if_fail (
		interface->unsubscribe_folder_sync != NULL, FALSE);

	camel_store_lock (
		CAMEL_STORE (subscribable),
		CAMEL_STORE_FOLDER_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		success = FALSE;
		goto exit;
	}

	/* Need to establish a connection before unsubscribing. */
	success = camel_service_connect_sync (
		CAMEL_SERVICE (subscribable), cancellable, error);
	if (!success)
		goto exit;

	message = _("Unsubscribing from folder '%s'");
	camel_operation_push_message (cancellable, message, folder_name);

	success = interface->unsubscribe_folder_sync (
		subscribable, folder_name, cancellable, error);
	CAMEL_CHECK_GERROR (
		subscribable, unsubscribe_folder_sync, success, error);

	if (success)
		subscribable_delete_cached_folder (
			CAMEL_STORE (subscribable), folder_name);

	camel_operation_pop_message (cancellable);

exit:
	camel_store_unlock (
		CAMEL_STORE (subscribable),
		CAMEL_STORE_FOLDER_LOCK);

	return success;
}

/**
 * camel_subscribable_unsubscribe_folder:
 * @subscribable: a #CamelSubscribable
 * @folder_name: full path of the folder
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously unsubscribes from the folder described by @folder_name.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_subscribable_unsubscribe_folder_finish() to get the result of
 * the operation.
 *
 * Since: 3.2
 **/
void
camel_subscribable_unsubscribe_folder (CamelSubscribable *subscribable,
                                       const gchar *folder_name,
                                       gint io_priority,
                                       GCancellable *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
	CamelSubscribableInterface *interface;

	g_return_if_fail (CAMEL_IS_SUBSCRIBABLE (subscribable));
	g_return_if_fail (folder_name != NULL);

	interface = CAMEL_SUBSCRIBABLE_GET_INTERFACE (subscribable);
	g_return_if_fail (interface->unsubscribe_folder != NULL);

	interface->unsubscribe_folder (
		subscribable, folder_name, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_subscribable_unsubscribe_folder_finish:
 * @subscribable: a #CamelSubscribable
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_subscribable_unsubscribe_folder().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.2
 **/
gboolean
camel_subscribable_unsubscribe_folder_finish (CamelSubscribable *subscribable,
                                              GAsyncResult *result,
                                              GError **error)
{
	CamelSubscribableInterface *interface;

	g_return_val_if_fail (CAMEL_IS_SUBSCRIBABLE (subscribable), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	interface = CAMEL_SUBSCRIBABLE_GET_INTERFACE (subscribable);
	g_return_val_if_fail (
		interface->unsubscribe_folder_finish != NULL, FALSE);

	return interface->unsubscribe_folder_finish (
		subscribable, result, error);
}

/**
 * camel_subscribable_folder_subscribed:
 * @subscribable: a #CamelSubscribable
 * @folder_info: information about the subscribed folder
 *
 * Emits the #CamelSubscribable::folder-subscribed signal from an idle source
 * on the main loop.  The idle source's priority is #G_PRIORITY_DEFAULT_IDLE.
 *
 * This function is only intended for Camel providers.
 *
 * Since: 3.2
 **/
void
camel_subscribable_folder_subscribed (CamelSubscribable *subscribable,
                                      CamelFolderInfo *folder_info)
{
	CamelService *service;
	CamelSession *session;
	SignalData *signal_data;

	g_return_if_fail (CAMEL_IS_SUBSCRIBABLE (subscribable));
	g_return_if_fail (folder_info != NULL);

	service = CAMEL_SERVICE (subscribable);
	session = camel_service_get_session (service);

	signal_data = g_slice_new0 (SignalData);
	signal_data->subscribable = g_object_ref (subscribable);
	signal_data->folder_info = camel_folder_info_clone (folder_info);

	camel_session_idle_add (
		session, G_PRIORITY_DEFAULT_IDLE,
		subscribable_emit_folder_subscribed_cb,
		signal_data, (GDestroyNotify) signal_data_free);
}

/**
 * camel_subscribable_folder_unsubscribed:
 * @subscribable: a #CamelSubscribable
 * @folder_info: information about the unsubscribed folder
 *
 * Emits the #CamelSubscribable::folder-unsubscribed signal from an idle source
 * on the main loop.  The idle source's priority is #G_PRIORITY_DEFAULT_IDLE.
 *
 * This function is only intended for Camel providers.
 *
 * Since: 3.2
 **/
void
camel_subscribable_folder_unsubscribed (CamelSubscribable *subscribable,
                                        CamelFolderInfo *folder_info)
{
	CamelService *service;
	CamelSession *session;
	SignalData *signal_data;

	g_return_if_fail (CAMEL_IS_SUBSCRIBABLE (subscribable));
	g_return_if_fail (folder_info != NULL);

	service = CAMEL_SERVICE (subscribable);
	session = camel_service_get_session (service);

	signal_data = g_slice_new0 (SignalData);
	signal_data->subscribable = g_object_ref (subscribable);
	signal_data->folder_info = camel_folder_info_clone (folder_info);

	camel_session_idle_add (
		session, G_PRIORITY_DEFAULT_IDLE,
		subscribable_emit_folder_unsubscribed_cb,
		signal_data, (GDestroyNotify) signal_data_free);
}


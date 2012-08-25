/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-service.c : Abstract class for an email service */

/*
 *
 * Author :
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "camel-debug.h"
#include "camel-enumtypes.h"
#include "camel-local-settings.h"
#include "camel-network-settings.h"
#include "camel-operation.h"
#include "camel-service.h"
#include "camel-session.h"

#define d(x)
#define w(x)

#define CAMEL_SERVICE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_SERVICE, CamelServicePrivate))

typedef struct _AsyncClosure AsyncClosure;
typedef struct _AsyncContext AsyncContext;
typedef struct _ConnectionOp ConnectionOp;

struct _CamelServicePrivate {
	gpointer session;  /* weak pointer */

	CamelSettings *settings;
	GMutex *settings_lock;

	CamelProvider *provider;

	gchar *display_name;
	gchar *user_data_dir;
	gchar *user_cache_dir;
	gchar *uid;
	gchar *password;

	GMutex *connection_lock;
	ConnectionOp *connection_op;
	CamelServiceConnectionStatus status;
};

/* This is copied from EAsyncClosure in libedataserver.
 * If this proves useful elsewhere in Camel we may want
 * to split this out and make it part of the public API. */
struct _AsyncClosure {
	GMainLoop *loop;
	GMainContext *context;
	GAsyncResult *result;
};

struct _AsyncContext {
	GList *auth_types;
	gchar *auth_mechanism;
	CamelAuthenticationResult auth_result;
};

/* The GQueue is only modified while CamelService's
 * connection_lock is held, so it does not need its
 * own mutex. */
struct _ConnectionOp {
	volatile gint ref_count;
	GQueue pending;
	GMutex simple_lock;
	GSimpleAsyncResult *simple;
	GCancellable *cancellable;
	gulong cancel_id;
};

enum {
	PROP_0,
	PROP_CONNECTION_STATUS,
	PROP_DISPLAY_NAME,
	PROP_PASSWORD,
	PROP_PROVIDER,
	PROP_SESSION,
	PROP_SETTINGS,
	PROP_UID
};

/* Forward Declarations */
static void camel_service_initable_init (GInitableIface *interface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (
	CamelService, camel_service, CAMEL_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, camel_service_initable_init))

static AsyncClosure *
async_closure_new (void)
{
	AsyncClosure *closure;

	closure = g_slice_new0 (AsyncClosure);
	closure->context = g_main_context_new ();
	closure->loop = g_main_loop_new (closure->context, FALSE);

	g_main_context_push_thread_default (closure->context);

	return closure;
}

static GAsyncResult *
async_closure_wait (AsyncClosure *closure)
{
	g_return_val_if_fail (closure != NULL, NULL);

	g_main_loop_run (closure->loop);

	return closure->result;
}

static void
async_closure_free (AsyncClosure *closure)
{
	g_return_if_fail (closure != NULL);

	g_main_context_pop_thread_default (closure->context);

	g_main_loop_unref (closure->loop);
	g_main_context_unref (closure->context);

	if (closure->result != NULL)
		g_object_unref (closure->result);

	g_slice_free (AsyncClosure, closure);
}

static void
async_closure_callback (GObject *object,
                        GAsyncResult *result,
                        gpointer closure)
{
	AsyncClosure *real_closure;

	g_return_if_fail (G_IS_OBJECT (object));
	g_return_if_fail (G_IS_ASYNC_RESULT (result));
	g_return_if_fail (closure != NULL);

	real_closure = closure;

	/* Replace any previous result. */
	if (real_closure->result != NULL)
		g_object_unref (real_closure->result);
	real_closure->result = g_object_ref (result);

	g_main_loop_quit (real_closure->loop);
}

static void
async_context_free (AsyncContext *async_context)
{
	g_list_free (async_context->auth_types);

	g_free (async_context->auth_mechanism);

	g_slice_free (AsyncContext, async_context);
}

static ConnectionOp *
connection_op_new (GSimpleAsyncResult *simple,
                   GCancellable *cancellable)
{
	ConnectionOp *op;

	op = g_slice_new0 (ConnectionOp);
	op->ref_count = 1;
	g_mutex_init (&op->simple_lock);
	op->simple = g_object_ref (simple);

	if (G_IS_CANCELLABLE (cancellable))
		op->cancellable = g_object_ref (cancellable);

	return op;
}

static ConnectionOp *
connection_op_ref (ConnectionOp *op)
{
	g_return_val_if_fail (op != NULL, NULL);
	g_return_val_if_fail (op->ref_count > 0, NULL);

	g_atomic_int_inc (&op->ref_count);

	return op;
}

static void
connection_op_unref (ConnectionOp *op)
{
	g_return_if_fail (op != NULL);
	g_return_if_fail (op->ref_count > 0);

	if (g_atomic_int_dec_and_test (&op->ref_count)) {

		/* The pending queue should be empty. */
		g_warn_if_fail (g_queue_is_empty (&op->pending));

		g_mutex_clear (&op->simple_lock);

		if (op->simple != NULL)
			g_object_unref (op->simple);

		if (op->cancel_id > 0)
			g_cancellable_disconnect (
				op->cancellable, op->cancel_id);

		if (op->cancellable != NULL)
			g_object_unref (op->cancellable);

		g_slice_free (ConnectionOp, op);
	}
}

static void
connection_op_complete (ConnectionOp *op,
                        const GError *error)
{
	g_mutex_lock (&op->simple_lock);

	if (op->simple != NULL && error != NULL)
		g_simple_async_result_set_from_error (op->simple, error);

	if (op->simple != NULL) {
		g_simple_async_result_complete_in_idle (op->simple);
		g_object_unref (op->simple);
		op->simple = NULL;
	}

	g_mutex_unlock (&op->simple_lock);
}

static void
connection_op_cancelled (GCancellable *cancellable,
                         ConnectionOp *op)
{
	/* Because we called g_simple_async_result_set_check_cancellable()
	 * we don't need to explicitly set a G_IO_ERROR_CANCELLED here. */
	connection_op_complete (op, NULL);
}

static void
connection_op_add_pending (ConnectionOp *op,
                           GSimpleAsyncResult *simple,
                           GCancellable *cancellable)
{
	ConnectionOp *pending_op;

	g_return_if_fail (op != NULL);

	pending_op = connection_op_new (simple, cancellable);

	if (pending_op->cancellable != NULL)
		pending_op->cancel_id = g_cancellable_connect (
			pending_op->cancellable,
			G_CALLBACK (connection_op_cancelled),
			pending_op, (GDestroyNotify) NULL);

	g_queue_push_tail (&op->pending, pending_op);
}

static void
connection_op_complete_pending (ConnectionOp *op,
                                const GError *error)
{
	ConnectionOp *pending_op;

	g_return_if_fail (op != NULL);

	while (!g_queue_is_empty (&op->pending)) {
		pending_op = g_queue_pop_head (&op->pending);
		connection_op_complete (pending_op, error);
		connection_op_unref (pending_op);
	}
}

static gchar *
service_find_old_data_dir (CamelService *service)
{
	CamelProvider *provider;
	CamelSession *session;
	CamelURL *url;
	GString *path;
	gboolean allows_host;
	gboolean allows_user;
	gboolean needs_host;
	gboolean needs_path;
	gboolean needs_user;
	const gchar *base_dir;
	gchar *old_data_dir;

	provider = camel_service_get_provider (service);
	session = camel_service_get_session (service);
	url = camel_service_new_camel_url (service);

	allows_host = CAMEL_PROVIDER_ALLOWS (provider, CAMEL_URL_PART_HOST);
	allows_user = CAMEL_PROVIDER_ALLOWS (provider, CAMEL_URL_PART_USER);

	needs_host = CAMEL_PROVIDER_NEEDS (provider, CAMEL_URL_PART_HOST);
	needs_path = CAMEL_PROVIDER_NEEDS (provider, CAMEL_URL_PART_PATH);
	needs_user = CAMEL_PROVIDER_NEEDS (provider, CAMEL_URL_PART_USER);

	/* This function reproduces the way service data directories used
	 * to be determined before we moved to just using the UID.  If the
	 * old data directory exists, try renaming it to the new form.
	 *
	 * A virtual class method was used to determine the directory path,
	 * but no known CamelProviders ever overrode the default algorithm
	 * below.  So this should work for everyone. */

	path = g_string_new (provider->protocol);

	if (allows_user) {
		g_string_append_c (path, '/');
		if (url->user != NULL)
			g_string_append (path, url->user);
		if (allows_host) {
			g_string_append_c (path, '@');
			if (url->host != NULL)
				g_string_append (path, url->host);
			if (url->port) {
				g_string_append_c (path, ':');
				g_string_append_printf (path, "%d", url->port);
			}
		} else if (!needs_user) {
			g_string_append_c (path, '@');
		}

	} else if (allows_host) {
		g_string_append_c (path, '/');
		if (!needs_host)
			g_string_append_c (path, '@');
		if (url->host != NULL)
			g_string_append (path, url->host);
		if (url->port) {
			g_string_append_c (path, ':');
			g_string_append_printf (path, "%d", url->port);
		}
	}

	if (needs_path && url->path) {
		if (*url->path != '/')
			g_string_append_c (path, '/');
		g_string_append (path, url->path);
	}

	base_dir = camel_session_get_user_data_dir (session);
	old_data_dir = g_build_filename (base_dir, path->str, NULL);

	g_string_free (path, TRUE);

	if (!g_file_test (old_data_dir, G_FILE_TEST_IS_DIR)) {
		g_free (old_data_dir);
		old_data_dir = NULL;
	}

	camel_url_free (url);

	return old_data_dir;
}

static gboolean
service_notify_connection_status_cb (gpointer user_data)
{
	CamelService *service = CAMEL_SERVICE (user_data);

	g_object_notify (G_OBJECT (service), "connection-status");

	return FALSE;
}

static void
service_queue_notify_connection_status (CamelService *service)
{
	CamelSession *session;

	session = camel_service_get_session (service);

	camel_session_idle_add (
		session, G_PRIORITY_DEFAULT_IDLE,
		service_notify_connection_status_cb,
		g_object_ref (service),
		(GDestroyNotify) g_object_unref);
}

static void
service_shared_connect_cb (GObject *source_object,
                           GAsyncResult *result,
                           gpointer user_data)
{
	CamelService *service;
	CamelServiceClass *class;
	ConnectionOp *op = user_data;
	gboolean success;
	GError *error = NULL;

	/* This avoids a compiler warning
	 * in the CAMEL_CHECK_GERROR macro. */
	GError **p_error = &error;

	service = CAMEL_SERVICE (source_object);
	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_if_fail (class->connect_finish != NULL);

	success = class->connect_finish (service, result, &error);
	CAMEL_CHECK_GERROR (service, connect_sync, success, p_error);

	g_mutex_lock (service->priv->connection_lock);

	if (service->priv->connection_op == op) {
		connection_op_unref (service->priv->connection_op);
		service->priv->connection_op = NULL;
		if (success)
			service->priv->status = CAMEL_SERVICE_CONNECTED;
		else
			service->priv->status = CAMEL_SERVICE_DISCONNECTED;
		service_queue_notify_connection_status (service);
	}

	connection_op_complete (op, error);
	connection_op_complete_pending (op, error);

	g_mutex_unlock (service->priv->connection_lock);

	connection_op_unref (op);
	g_clear_error (&error);
}

static void
service_shared_disconnect_cb (GObject *source_object,
                              GAsyncResult *result,
                              gpointer user_data)
{
	CamelService *service;
	CamelServiceClass *class;
	ConnectionOp *op = user_data;
	gboolean success;
	GError *error = NULL;

	/* This avoids a compiler warning
	 * in the CAMEL_CHECK_GERROR macro. */
	GError **p_error = &error;

	service = CAMEL_SERVICE (source_object);
	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_if_fail (class->disconnect_finish != NULL);

	success = class->disconnect_finish (service, result, &error);
	CAMEL_CHECK_GERROR (service, disconnect_sync, success, p_error);

	g_mutex_lock (service->priv->connection_lock);

	if (service->priv->connection_op == op) {
		connection_op_unref (service->priv->connection_op);
		service->priv->connection_op = NULL;
		if (success)
			service->priv->status = CAMEL_SERVICE_DISCONNECTED;
		else
			service->priv->status = CAMEL_SERVICE_CONNECTED;
		service_queue_notify_connection_status (service);
	}

	connection_op_complete (op, error);
	connection_op_complete_pending (op, error);

	g_mutex_unlock (service->priv->connection_lock);

	connection_op_unref (op);
	g_clear_error (&error);
}

static void
service_set_provider (CamelService *service,
                      CamelProvider *provider)
{
	g_return_if_fail (provider != NULL);
	g_return_if_fail (service->priv->provider == NULL);

	service->priv->provider = provider;
}

static void
service_set_session (CamelService *service,
                     CamelSession *session)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));
	g_return_if_fail (service->priv->session == NULL);

	service->priv->session = session;

	g_object_add_weak_pointer (
		G_OBJECT (session), &service->priv->session);
}

static void
service_set_uid (CamelService *service,
                 const gchar *uid)
{
	g_return_if_fail (uid != NULL);
	g_return_if_fail (service->priv->uid == NULL);

	service->priv->uid = g_strdup (uid);
}

static void
service_set_property (GObject *object,
                      guint property_id,
                      const GValue *value,
                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_DISPLAY_NAME:
			camel_service_set_display_name (
				CAMEL_SERVICE (object),
				g_value_get_string (value));
			return;

		case PROP_PASSWORD:
			camel_service_set_password (
				CAMEL_SERVICE (object),
				g_value_get_string (value));
			return;

		case PROP_PROVIDER:
			service_set_provider (
				CAMEL_SERVICE (object),
				g_value_get_pointer (value));
			return;

		case PROP_SESSION:
			service_set_session (
				CAMEL_SERVICE (object),
				g_value_get_object (value));
			return;

		case PROP_SETTINGS:
			camel_service_set_settings (
				CAMEL_SERVICE (object),
				g_value_get_object (value));
			return;

		case PROP_UID:
			service_set_uid (
				CAMEL_SERVICE (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
service_get_property (GObject *object,
                      guint property_id,
                      GValue *value,
                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTION_STATUS:
			g_value_set_enum (
				value, camel_service_get_connection_status (
				CAMEL_SERVICE (object)));
			return;

		case PROP_DISPLAY_NAME:
			g_value_set_string (
				value, camel_service_get_display_name (
				CAMEL_SERVICE (object)));
			return;

		case PROP_PASSWORD:
			g_value_set_string (
				value, camel_service_get_password (
				CAMEL_SERVICE (object)));
			return;

		case PROP_PROVIDER:
			g_value_set_pointer (
				value, camel_service_get_provider (
				CAMEL_SERVICE (object)));
			return;

		case PROP_SESSION:
			g_value_set_object (
				value, camel_service_get_session (
				CAMEL_SERVICE (object)));
			return;

		case PROP_SETTINGS:
			g_value_take_object (
				value, camel_service_ref_settings (
				CAMEL_SERVICE (object)));
			return;

		case PROP_UID:
			g_value_set_string (
				value, camel_service_get_uid (
				CAMEL_SERVICE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
service_dispose (GObject *object)
{
	CamelServicePrivate *priv;

	priv = CAMEL_SERVICE_GET_PRIVATE (object);

	if (priv->session != NULL) {
		g_object_remove_weak_pointer (
			G_OBJECT (priv->session), &priv->session);
		priv->session = NULL;
	}

	if (priv->settings != NULL) {
		g_object_unref (priv->settings);
		priv->settings = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_service_parent_class)->dispose (object);
}

static void
service_finalize (GObject *object)
{
	CamelServicePrivate *priv;

	priv = CAMEL_SERVICE_GET_PRIVATE (object);

	if (priv->status == CAMEL_SERVICE_CONNECTED)
		CAMEL_SERVICE_GET_CLASS (object)->disconnect_sync (
			CAMEL_SERVICE (object), TRUE, NULL, NULL);

	g_mutex_free (priv->settings_lock);

	g_free (priv->display_name);
	g_free (priv->user_data_dir);
	g_free (priv->user_cache_dir);
	g_free (priv->uid);
	g_free (priv->password);

	/* There should be no outstanding connection operations. */
	g_warn_if_fail (priv->connection_op == NULL);
	g_mutex_free (priv->connection_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_service_parent_class)->finalize (object);
}

static void
service_constructed (GObject *object)
{
	CamelService *service;
	CamelSession *session;
	const gchar *base_dir;
	const gchar *uid;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (camel_service_parent_class)->constructed (object);

	service = CAMEL_SERVICE (object);
	session = camel_service_get_session (service);

	uid = camel_service_get_uid (service);

	base_dir = camel_session_get_user_data_dir (session);
	service->priv->user_data_dir = g_build_filename (base_dir, uid, NULL);

	base_dir = camel_session_get_user_cache_dir (session);
	service->priv->user_cache_dir = g_build_filename (base_dir, uid, NULL);
}

static gchar *
service_get_name (CamelService *service,
                  gboolean brief)
{
	g_warning (
		"%s does not implement CamelServiceClass::get_name()",
		G_OBJECT_TYPE_NAME (service));

	return g_strdup (G_OBJECT_TYPE_NAME (service));
}

static gboolean
service_connect_sync (CamelService *service,
                      GCancellable *cancellable,
                      GError **error)
{
	/* Default behavior for local storage providers. */
	return TRUE;
}

static gboolean
service_disconnect_sync (CamelService *service,
                         gboolean clean,
                         GCancellable *cancellable,
                         GError **error)
{
	/* Default behavior for local storage providers. */
	return TRUE;
}

static GList *
service_query_auth_types_sync (CamelService *service,
                               GCancellable *cancellable,
                               GError **error)
{
	return NULL;
}

static void
service_connect_thread (GSimpleAsyncResult *simple,
                        GObject *object,
                        GCancellable *cancellable)
{
	CamelService *service;
	CamelServiceClass *class;
	GError *error = NULL;

	/* Note we call the class method directly here. */

	service = CAMEL_SERVICE (object);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_if_fail (class->connect_sync != NULL);

	class->connect_sync (service, cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
service_connect (CamelService *service,
                 gint io_priority,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (
		G_OBJECT (service), callback, user_data, service_connect);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_run_in_thread (
		simple, service_connect_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
service_connect_finish (CamelService *service,
                        GAsyncResult *result,
                        GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (service), service_connect), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
service_disconnect_thread (GSimpleAsyncResult *simple,
                           GObject *object,
                           GCancellable *cancellable)
{
	CamelService *service;
	CamelServiceClass *class;
	gboolean clean;
	GError *error = NULL;

	/* Note we call the class method directly here. */

	service = CAMEL_SERVICE (object);
	clean = g_simple_async_result_get_op_res_gboolean (simple);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_if_fail (class->disconnect_sync != NULL);

	class->disconnect_sync (service, clean, cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
service_disconnect (CamelService *service,
                    gboolean clean,
                    gint io_priority,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (
		G_OBJECT (service), callback, user_data, service_disconnect);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gboolean (simple, clean);

	g_simple_async_result_run_in_thread (
		simple, service_disconnect_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
service_disconnect_finish (CamelService *service,
                           GAsyncResult *result,
                           GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (service), service_disconnect), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
service_authenticate_thread (GSimpleAsyncResult *simple,
                             GObject *object,
                             GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->auth_result = camel_service_authenticate_sync (
		CAMEL_SERVICE (object), async_context->auth_mechanism,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
service_authenticate (CamelService *service,
                      const gchar *mechanism,
                      gint io_priority,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->auth_mechanism = g_strdup (mechanism);

	simple = g_simple_async_result_new (
		G_OBJECT (service), callback, user_data, service_authenticate);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, service_authenticate_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static CamelAuthenticationResult
service_authenticate_finish (CamelService *service,
                             GAsyncResult *result,
                             GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (service), service_authenticate),
		CAMEL_AUTHENTICATION_REJECTED);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return CAMEL_AUTHENTICATION_ERROR;

	return async_context->auth_result;
}

static void
service_query_auth_types_thread (GSimpleAsyncResult *simple,
                                 GObject *object,
                                 GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->auth_types = camel_service_query_auth_types_sync (
		CAMEL_SERVICE (object), cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
service_query_auth_types (CamelService *service,
                          gint io_priority,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);

	simple = g_simple_async_result_new (
		G_OBJECT (service), callback,
		user_data, service_query_auth_types);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, service_query_auth_types_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static GList *
service_query_auth_types_finish (CamelService *service,
                                 GAsyncResult *result,
                                 GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (service),
		service_query_auth_types), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_list_copy (async_context->auth_types);
}

static gboolean
service_initable_init (GInitable *initable,
                       GCancellable *cancellable,
                       GError **error)
{
	/* Nothing to do here, but we may need add something in the future.
	 * For now this is a placeholder so subclasses can safely chain up. */

	return TRUE;
}

static void
camel_service_class_init (CamelServiceClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelServicePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = service_set_property;
	object_class->get_property = service_get_property;
	object_class->dispose = service_dispose;
	object_class->finalize = service_finalize;
	object_class->constructed = service_constructed;

	class->settings_type = CAMEL_TYPE_SETTINGS;
	class->get_name = service_get_name;
	class->connect_sync = service_connect_sync;
	class->disconnect_sync = service_disconnect_sync;
	class->query_auth_types_sync = service_query_auth_types_sync;

	class->connect = service_connect;
	class->connect_finish = service_connect_finish;
	class->disconnect = service_disconnect;
	class->disconnect_finish = service_disconnect_finish;
	class->authenticate = service_authenticate;
	class->authenticate_finish = service_authenticate_finish;
	class->query_auth_types = service_query_auth_types;
	class->query_auth_types_finish = service_query_auth_types_finish;

	g_object_class_install_property (
		object_class,
		PROP_CONNECTION_STATUS,
		g_param_spec_enum (
			"connection-status",
			"Connection Status",
			"The connection status for the service",
			CAMEL_TYPE_SERVICE_CONNECTION_STATUS,
			CAMEL_SERVICE_DISCONNECTED,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_DISPLAY_NAME,
		g_param_spec_string (
			"display-name",
			"Display Name",
			"The display name for the service",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_PASSWORD,
		g_param_spec_string (
			"password",
			"Password",
			"The password for the service",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_PROVIDER,
		g_param_spec_pointer (
			"provider",
			"Provider",
			"The CamelProvider for the service",
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SESSION,
		g_param_spec_object (
			"session",
			"Session",
			"A CamelSession instance",
			CAMEL_TYPE_SESSION,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SETTINGS,
		g_param_spec_object (
			"settings",
			"Settings",
			"A CamelSettings instance",
			CAMEL_TYPE_SETTINGS,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_UID,
		g_param_spec_string (
			"uid",
			"UID",
			"The unique identity of the service",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_service_initable_init (GInitableIface *interface)
{
	interface->init = service_initable_init;
}

static void
camel_service_init (CamelService *service)
{
	service->priv = CAMEL_SERVICE_GET_PRIVATE (service);

	service->priv->settings_lock = g_mutex_new ();
	service->priv->connection_lock = g_mutex_new ();
	service->priv->status = CAMEL_SERVICE_DISCONNECTED;
}

GQuark
camel_service_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "camel-service-error-quark";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

/**
 * camel_service_migrate_files:
 * @service: a #CamelService
 *
 * Performs any necessary file migrations for @service.  This should be
 * called after installing or configuring the @service's #CamelSettings,
 * since it requires building a URL string for @service.
 *
 * Since: 3.4
 **/
void
camel_service_migrate_files (CamelService *service)
{
	const gchar *new_data_dir;
	gchar *old_data_dir;

	g_return_if_fail (CAMEL_IS_SERVICE (service));

	new_data_dir = camel_service_get_user_data_dir (service);
	old_data_dir = service_find_old_data_dir (service);

	/* If the old data directory name exists, try renaming
	 * it to the new data directory.  Failure is non-fatal. */
	if (old_data_dir != NULL) {
		g_rename (old_data_dir, new_data_dir);
		g_free (old_data_dir);
	}
}

/**
 * camel_service_new_camel_url:
 * @service: a #CamelService
 *
 * Returns a new #CamelURL representing @service.
 * Free the returned #CamelURL with camel_url_free().
 *
 * Returns: a new #CamelURL
 *
 * Since: 3.2
 **/
CamelURL *
camel_service_new_camel_url (CamelService *service)
{
	CamelURL *url;
	CamelProvider *provider;
	CamelSettings *settings;
	gchar *host = NULL;
	gchar *user = NULL;
	gchar *path = NULL;
	guint16 port = 0;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	provider = camel_service_get_provider (service);
	g_return_val_if_fail (provider != NULL, NULL);

	settings = camel_service_ref_settings (service);

	/* Allocate as camel_url_new_with_base() does. */
	url = g_new0 (CamelURL, 1);

	if (CAMEL_IS_NETWORK_SETTINGS (settings)) {
		CamelNetworkSettings *network_settings;

		network_settings = CAMEL_NETWORK_SETTINGS (settings);
		host = camel_network_settings_dup_host (network_settings);
		port = camel_network_settings_get_port (network_settings);
		user = camel_network_settings_dup_user (network_settings);
	}

	if (CAMEL_IS_LOCAL_SETTINGS (settings)) {
		CamelLocalSettings *local_settings;

		local_settings = CAMEL_LOCAL_SETTINGS (settings);
		path = camel_local_settings_dup_path (local_settings);
	}

	camel_url_set_protocol (url, provider->protocol);
	camel_url_set_host (url, host);
	camel_url_set_port (url, port);
	camel_url_set_user (url, user);
	camel_url_set_path (url, path);

	g_free (host);
	g_free (user);
	g_free (path);

	g_object_unref (settings);

	return url;
}

/**
 * camel_service_get_connection_status:
 * @service: a #CamelService
 *
 * Returns the connection status for @service.
 *
 * Returns: the connection status
 *
 * Since: 3.2
 **/
CamelServiceConnectionStatus
camel_service_get_connection_status (CamelService *service)
{
	g_return_val_if_fail (
		CAMEL_IS_SERVICE (service),
		CAMEL_SERVICE_DISCONNECTED);

	return service->priv->status;
}

/**
 * camel_service_get_display_name:
 * @service: a #CamelService
 *
 * Returns the display name for @service, or %NULL if @service has not
 * been given a display name.  The display name is intended for use in
 * a user interface and should generally be given a user-defined name.
 *
 * Compare this with camel_service_get_name(), which returns a built-in
 * description of the type of service (IMAP, SMTP, etc.).
 *
 * Returns: the display name for @service, or %NULL
 *
 * Since: 3.2
 **/
const gchar *
camel_service_get_display_name (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	return service->priv->display_name;
}

/**
 * camel_service_set_display_name:
 * @service: a #CamelService
 * @display_name: a valid UTF-8 string, or %NULL
 *
 * Assigns a UTF-8 display name to @service.  The display name is intended
 * for use in a user interface and should generally be given a user-defined
 * name.
 *
 * Compare this with camel_service_get_name(), which returns a built-in
 * description of the type of service (IMAP, SMTP, etc.).
 *
 * Since: 3.2
 **/
void
camel_service_set_display_name (CamelService *service,
                                const gchar *display_name)
{
	g_return_if_fail (CAMEL_IS_SERVICE (service));

	if (g_strcmp0 (service->priv->display_name, display_name) == 0)
		return;

	if (display_name != NULL)
		g_return_if_fail (g_utf8_validate (display_name, -1, NULL));

	g_free (service->priv->display_name);
	service->priv->display_name = g_strdup (display_name);

	g_object_notify (G_OBJECT (service), "display-name");
}

/**
 * camel_service_get_password:
 * @service: a #CamelService
 *
 * Returns the password for @service.  Some SASL mechanisms use this
 * when attempting to authenticate.
 *
 * Returns: the password for @service
 *
 * Since: 3.4
 **/
const gchar *
camel_service_get_password (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	return service->priv->password;
}

/**
 * camel_service_set_password:
 * @service: a #CamelService
 * @password: the password for @service
 *
 * Sets the password for @service.  Use this function to cache the password
 * in memory after obtaining it through camel_session_get_password().  Some
 * SASL mechanisms use this when attempting to authenticate.
 *
 * Since: 3.4
 **/
void
camel_service_set_password (CamelService *service,
                            const gchar *password)
{
	g_return_if_fail (CAMEL_IS_SERVICE (service));

	if (g_strcmp0 (service->priv->password, password) == 0)
		return;

	g_free (service->priv->password);
	service->priv->password = g_strdup (password);

	g_object_notify (G_OBJECT (service), "password");
}

/**
 * camel_service_get_user_data_dir:
 * @service: a #CamelService
 *
 * Returns the base directory under which to store user-specific data
 * for @service.  The directory is formed by appending the directory
 * returned by camel_session_get_user_data_dir() with the service's
 * #CamelService:uid value.
 *
 * Returns: the base directory for @service
 *
 * Since: 3.2
 **/
const gchar *
camel_service_get_user_data_dir (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	return service->priv->user_data_dir;
}

/**
 * camel_service_get_user_cache_dir:
 * @service: a #CamelService
 *
 * Returns the base directory under which to store cache data
 * for @service.  The directory is formed by appending the directory
 * returned by camel_session_get_user_cache_dir() with the service's
 * #CamelService:uid value.
 *
 * Returns: the base cache directory for @service
 *
 * Since: 3.4
 **/
const gchar *
camel_service_get_user_cache_dir (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	return service->priv->user_cache_dir;
}

/**
 * camel_service_get_name:
 * @service: a #CamelService
 * @brief: whether or not to use a briefer form
 *
 * This gets the name of the service in a "friendly" (suitable for
 * humans) form. If @brief is %TRUE, this should be a brief description
 * such as for use in the folder tree. If @brief is %FALSE, it should
 * be a more complete and mostly unambiguous description.
 *
 * Returns: a description of the service which the caller must free
 **/
gchar *
camel_service_get_name (CamelService *service,
                        gboolean brief)
{
	CamelServiceClass *class;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->get_name != NULL, NULL);

	return class->get_name (service, brief);
}

/**
 * camel_service_get_provider:
 * @service: a #CamelService
 *
 * Gets the #CamelProvider associated with the service.
 *
 * Returns: the #CamelProvider
 **/
CamelProvider *
camel_service_get_provider (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	return service->priv->provider;
}

/**
 * camel_service_get_session:
 * @service: a #CamelService
 *
 * Gets the #CamelSession associated with the service.
 *
 * Returns: the #CamelSession
 **/
CamelSession *
camel_service_get_session (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	return CAMEL_SESSION (service->priv->session);
}

/**
 * camel_service_ref_settings:
 * @service: a #CamelService
 *
 * Returns the #CamelSettings instance associated with the service.
 *
 * The returned #CamelSettings is referenced for thread-safety and must
 * be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: the #CamelSettings
 *
 * Since: 3.6
 **/
CamelSettings *
camel_service_ref_settings (CamelService *service)
{
	CamelSettings *settings;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	/* Every service should have a settings object. */
	g_return_val_if_fail (service->priv->settings != NULL, NULL);

	g_mutex_lock (service->priv->settings_lock);

	settings = g_object_ref (service->priv->settings);

	g_mutex_unlock (service->priv->settings_lock);

	return settings;
}

/**
 * camel_service_set_settings:
 * @service: a #CamelService
 * @settings: an instance derviced from #CamelSettings, or %NULL
 *
 * Associates a new #CamelSettings instance with the service.
 * The @settings instance must match the settings type defined in
 * #CamelServiceClass.  If @settings is %NULL, a new #CamelSettings
 * instance of the appropriate type is created with all properties
 * set to defaults.
 *
 * Since: 3.2
 **/
void
camel_service_set_settings (CamelService *service,
                            CamelSettings *settings)
{
	CamelServiceClass *class;

	g_return_if_fail (CAMEL_IS_SERVICE (service));

	class = CAMEL_SERVICE_GET_CLASS (service);

	if (settings != NULL) {
		g_return_if_fail (
			g_type_is_a (
				G_OBJECT_TYPE (settings),
				class->settings_type));
		g_object_ref (settings);

	} else {
		g_return_if_fail (
			g_type_is_a (
				class->settings_type,
				CAMEL_TYPE_SETTINGS));
		settings = g_object_new (class->settings_type, NULL);
	}

	g_mutex_lock (service->priv->settings_lock);

	if (service->priv->settings != NULL)
		g_object_unref (service->priv->settings);

	service->priv->settings = settings;  /* takes ownership */

	g_mutex_unlock (service->priv->settings_lock);

	g_object_notify (G_OBJECT (service), "settings");
}

/**
 * camel_service_get_uid:
 * @service: a #CamelService
 *
 * Gets the unique identifier string associated with the service.
 *
 * Returns: the UID string
 *
 * Since: 3.2
 **/
const gchar *
camel_service_get_uid (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	return service->priv->uid;
}

/**
 * camel_service_connect_sync:
 * @service: a #CamelService
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Connects @service to a remote server using the information in its
 * #CamelService:settings instance.
 *
 * If a connect operation is already in progress when this function is
 * called, its results will be reflected in this connect operation.
 *
 * Returns: %TRUE if the connection is made or %FALSE otherwise
 *
 * Since: 3.6
 **/
gboolean
camel_service_connect_sync (CamelService *service,
                            GCancellable *cancellable,
                            GError **error)
{
	AsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), FALSE);

	closure = async_closure_new ();

	camel_service_connect (
		service, G_PRIORITY_DEFAULT, cancellable,
		async_closure_callback, closure);

	result = async_closure_wait (closure);

	success = camel_service_connect_finish (service, result, error);

	async_closure_free (closure);

	return success;
}

/**
 * camel_service_connect:
 * @service: a #CamelService
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously connects @service to a remote server using the information
 * in its #CamelService:settings instance.
 *
 * If a connect operation is already in progress when this function is
 * called, its results will be reflected in this connect operation.
 *
 * If any disconnect operations are in progress when this function is
 * called, they will be cancelled.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_service_connect_finish() to get the result of the
 * operation.
 *
 * Since: 3.6
 **/
void
camel_service_connect (CamelService *service,
                       gint io_priority,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	ConnectionOp *op;
	CamelServiceClass *class;
	GSimpleAsyncResult *simple;

	g_return_if_fail (CAMEL_IS_SERVICE (service));

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_if_fail (class->connect != NULL);

	simple = g_simple_async_result_new (
		G_OBJECT (service), callback,
		user_data, camel_service_connect);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_mutex_lock (service->priv->connection_lock);

	switch (service->priv->status) {

		/* If a connect operation is already in progress,
		 * queue this operation so it completes at the same
		 * time the first connect operation completes. */
		case CAMEL_SERVICE_CONNECTING:
			connection_op_add_pending (
				service->priv->connection_op,
				simple, cancellable);
			break;

		/* If we're already connected, just report success. */
		case CAMEL_SERVICE_CONNECTED:
			g_simple_async_result_complete_in_idle (simple);
			break;

		/* If a disconnect operation is currently in progress,
		 * cancel it and make room for the connect operation. */
		case CAMEL_SERVICE_DISCONNECTING:
			g_return_if_fail (
				service->priv->connection_op != NULL);
			g_cancellable_cancel (
				service->priv->connection_op->cancellable);
			connection_op_unref (service->priv->connection_op);
			service->priv->connection_op = NULL;
			/* fall through */

		/* Start a new connect operation.  Subsequent connect
		 * operations are queued until this operation completes
		 * and will share this operation's result. */
		case CAMEL_SERVICE_DISCONNECTED:
			g_return_if_fail (
				service->priv->connection_op == NULL);

			op = connection_op_new (simple, cancellable);
			service->priv->connection_op = op;

			service->priv->status = CAMEL_SERVICE_CONNECTING;
			service_queue_notify_connection_status (service);

			class->connect (
				service,
				io_priority,
				cancellable,
				service_shared_connect_cb,
				connection_op_ref (op));
			break;

		default:
			g_warn_if_reached ();
	}

	g_mutex_unlock (service->priv->connection_lock);

	g_object_unref (simple);
}

/**
 * camel_service_connect_finish:
 * @service: a #CamelService
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_service_connect().
 *
 * Returns: %TRUE if the connection was made or %FALSE otherwise
 *
 * Since: 3.6
 **/
gboolean
camel_service_connect_finish (CamelService *service,
                              GAsyncResult *result,
                              GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (service), camel_service_connect), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/**
 * camel_service_disconnect_sync:
 * @service: a #CamelService
 * @clean: whether or not to try to disconnect cleanly
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Disconnect from the service. If @clean is %FALSE, it should not
 * try to do any synchronizing or other cleanup of the connection.
 *
 * If a disconnect operation is already in progress when this function is
 * called, its results will be reflected in this disconnect operation.
 *
 * If any connect operations are in progress when this function is called,
 * they will be cancelled.
 *
 * Returns: %TRUE if the connection was severed or %FALSE otherwise
 *
 * Since: 3.6
 **/
gboolean
camel_service_disconnect_sync (CamelService *service,
                               gboolean clean,
                               GCancellable *cancellable,
                               GError **error)
{
	AsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), FALSE);

	closure = async_closure_new ();

	camel_service_disconnect (
		service, clean, G_PRIORITY_DEFAULT,
		cancellable, async_closure_callback, closure);

	result = async_closure_wait (closure);

	success = camel_service_disconnect_finish (service, result, error);

	async_closure_free (closure);

	return success;
}

/**
 * camel_service_disconnect:
 * @service: a #CamelService
 * @clean: whether or not to try to disconnect cleanly
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * If a disconnect operation is already in progress when this function is
 * called, its results will be reflected in this disconnect operation.
 *
 * If any connect operations are in progress when this function is called,
 * they will be cancelled.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_service_disconnect_finish() to get the result of the
 * operation.
 *
 * Since: 3.6
 **/
void
camel_service_disconnect (CamelService *service,
                          gboolean clean,
                          gint io_priority,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	ConnectionOp *op;
	CamelServiceClass *class;
	GSimpleAsyncResult *simple;

	g_return_if_fail (CAMEL_IS_SERVICE (service));

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_if_fail (class->disconnect != NULL);

	simple = g_simple_async_result_new (
		G_OBJECT (service), callback,
		user_data, camel_service_disconnect);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_mutex_lock (service->priv->connection_lock);

	switch (service->priv->status) {

		/* If a connect operation is currently in progress,
		 * cancel it and make room for the disconnect operation. */
		case CAMEL_SERVICE_CONNECTING:
			g_return_if_fail (
				service->priv->connection_op != NULL);
			g_cancellable_cancel (
				service->priv->connection_op->cancellable);
			connection_op_unref (service->priv->connection_op);
			service->priv->connection_op = NULL;
			/* fall through */

		/* Start a new disconnect operation.  Subsequent disconnect
		 * operations are queued until this operation completes and
		 * will share this operation's result. */
		case CAMEL_SERVICE_CONNECTED:
			g_return_if_fail (
				service->priv->connection_op == NULL);

			op = connection_op_new (simple, cancellable);
			service->priv->connection_op = op;

			service->priv->status = CAMEL_SERVICE_DISCONNECTING;
			service_queue_notify_connection_status (service);

			class->disconnect (
				service, clean,
				io_priority,
				cancellable,
				service_shared_disconnect_cb,
				connection_op_ref (op));
			break;

		/* If a disconnect operation is already in progress,
		 * queue this operation so it completes at the same
		 * time the first disconnect operation completes. */
		case CAMEL_SERVICE_DISCONNECTING:
			connection_op_add_pending (
				service->priv->connection_op,
				simple, cancellable);
			break;

		/* If we're already disconnected, just report success. */
		case CAMEL_SERVICE_DISCONNECTED:
			g_simple_async_result_complete_in_idle (simple);
			break;

		default:
			g_warn_if_reached ();
	}

	g_mutex_unlock (service->priv->connection_lock);

	g_object_unref (simple);
}

/**
 * camel_service_disconnect_finish:
 * @service: a #CamelService
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_service_disconnect().
 *
 * Returns: %TRUE if the connection was severed or %FALSE otherwise
 *
 * Since: 3.6
 **/
gboolean
camel_service_disconnect_finish (CamelService *service,
                                 GAsyncResult *result,
                                 GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (service), camel_service_disconnect), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/**
 * camel_service_authenticate_sync:
 * @service: a #CamelService
 * @mechanism: a SASL mechanism name, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Attempts to authenticate @service using @mechanism and, if necessary,
 * @service's #CamelService:password property.  The function makes only
 * ONE attempt at authentication and does not loop.
 *
 * If the authentication attempt completed and the server accepted the
 * credentials, the function returns #CAMEL_AUTHENTICATION_ACCEPTED.
 *
 * If the authentication attempt completed but the server rejected the
 * credentials, the function returns #CAMEL_AUTHENTICATION_REJECTED.
 *
 * If the authentication attempt failed to complete due to a network
 * communication issue or some other mishap, the function sets @error
 * and returns #CAMEL_AUTHENTICATION_ERROR.
 *
 * Generally this function should only be called from a #CamelSession
 * subclass in order to implement its own authentication loop.
 *
 * Returns: the authentication result
 *
 * Since: 3.4
 **/
CamelAuthenticationResult
camel_service_authenticate_sync (CamelService *service,
                                 const gchar *mechanism,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelServiceClass *class;
	CamelAuthenticationResult result;

	g_return_val_if_fail (
		CAMEL_IS_SERVICE (service),
		CAMEL_AUTHENTICATION_REJECTED);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (
		class->authenticate_sync != NULL,
		CAMEL_AUTHENTICATION_REJECTED);

	result = class->authenticate_sync (
		service, mechanism, cancellable, error);
	CAMEL_CHECK_GERROR (
		service, authenticate_sync,
		result != CAMEL_AUTHENTICATION_ERROR, error);

	return result;
}

/**
 * camel_service_authenticate:
 * @service: a #CamelService
 * @mechanism: a SASL mechanism name, or %NULL
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously attempts to authenticate @service using @mechanism and,
 * if necessary, @service's #CamelService:password property.  The function
 * makes only ONE attempt at authentication and does not loop.
 *
 * Generally this function should only be called from a #CamelSession
 * subclass in order to implement its own authentication loop.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_service_authenticate_finish() to get the result of
 * the operation.
 *
 * Since: 3.4
 **/
void
camel_service_authenticate (CamelService *service,
                            const gchar *mechanism,
                            gint io_priority,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	CamelServiceClass *class;

	g_return_if_fail (CAMEL_IS_SERVICE (service));

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_if_fail (class->authenticate != NULL);

	class->authenticate (
		service, mechanism, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_service_authenticate_finish:
 * @service: a #CamelService
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_service_authenticate().
 *
 * If the authentication attempt completed and the server accepted the
 * credentials, the function returns #CAMEL_AUTHENTICATION_ACCEPTED.
 *
 * If the authentication attempt completed but the server rejected the
 * credentials, the function returns #CAMEL_AUTHENTICATION_REJECTED.
 *
 * If the authentication attempt failed to complete due to a network
 * communication issue or some other mishap, the function sets @error
 * and returns #CAMEL_AUTHENTICATION_ERROR.
 *
 * Returns: the authentication result
 *
 * Since: 3.4
 **/
CamelAuthenticationResult
camel_service_authenticate_finish (CamelService *service,
                                   GAsyncResult *result,
                                   GError **error)
{
	CamelServiceClass *class;

	g_return_val_if_fail (
		CAMEL_IS_SERVICE (service),
		CAMEL_AUTHENTICATION_REJECTED);
	g_return_val_if_fail (
		G_IS_ASYNC_RESULT (result),
		CAMEL_AUTHENTICATION_REJECTED);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (
		class->authenticate_finish,
		CAMEL_AUTHENTICATION_REJECTED);

	return class->authenticate_finish (service, result, error);
}

/**
 * camel_service_query_auth_types_sync:
 * @service: a #CamelService
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Obtains a list of authentication types supported by @service.
 * Free the returned list with g_list_free().
 *
 * Returns: a list of #CamelServiceAuthType structs
 **/
GList *
camel_service_query_auth_types_sync (CamelService *service,
                                     GCancellable *cancellable,
                                     GError **error)
{
	CamelServiceClass *class;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->query_auth_types_sync != NULL, NULL);

	return class->query_auth_types_sync (service, cancellable, error);
}

/**
 * camel_service_query_auth_types:
 * @service: a #CamelService
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously obtains a list of authentication types supported by
 * @service.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_service_query_auth_types_finish() to get the result
 * of the operation.
 *
 * Since: 3.2
 **/
void
camel_service_query_auth_types (CamelService *service,
                                gint io_priority,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	CamelServiceClass *class;

	g_return_if_fail (CAMEL_IS_SERVICE (service));

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_if_fail (class->query_auth_types != NULL);

	class->query_auth_types (
		service, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_service_query_auth_types_finish:
 * @service: a #CamelService
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_service_query_auth_types().
 * Free the returned list with g_list_free().
 *
 * Returns: a list of #CamelServiceAuthType structs
 *
 * Since: 3.2
 **/
GList *
camel_service_query_auth_types_finish (CamelService *service,
                                       GAsyncResult *result,
                                       GError **error)
{
	CamelServiceClass *class;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->query_auth_types_finish != NULL, NULL);

	return class->query_auth_types_finish (service, result, error);
}


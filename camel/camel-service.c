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

#include <libedataserver/e-data-server-util.h>

#include "camel-debug.h"
#include "camel-operation.h"
#include "camel-service.h"
#include "camel-session.h"

#define d(x)
#define w(x)

#define CAMEL_SERVICE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_SERVICE, CamelServicePrivate))

typedef struct _AsyncContext AsyncContext;

struct _CamelServicePrivate {
	CamelProvider *provider;
	CamelSession *session;
	CamelURL *url;

	gchar *user_data_dir;
	gchar *uid;

	GCancellable *connect_op;
	CamelServiceConnectionStatus status;

	GStaticRecMutex connect_lock;	/* for locking connection operations */
	GStaticMutex connect_op_lock;	/* for locking the connection_op */
};

struct _AsyncContext {
	GList *auth_types;
};

enum {
	PROP_0,
	PROP_PROVIDER,
	PROP_SESSION,
	PROP_UID,
	PROP_URL
};

/* Forward Declarations */
static void camel_service_initable_init (GInitableIface *interface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (
	CamelService, camel_service, CAMEL_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, camel_service_initable_init))

static void
async_context_free (AsyncContext *async_context)
{
	g_list_free (async_context->auth_types);

	g_slice_free (AsyncContext, async_context);
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
	url = camel_service_get_camel_url (service);

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

	if (needs_path) {
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

	return old_data_dir;
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

	service->priv->session = g_object_ref (session);
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
service_set_url (CamelService *service,
                 CamelURL *url)
{
	g_return_if_fail (url != NULL);
	g_return_if_fail (service->priv->url == NULL);

	service->priv->url = camel_url_copy (url);
}

static void
service_set_property (GObject *object,
                      guint property_id,
                      const GValue *value,
                      GParamSpec *pspec)
{
	switch (property_id) {
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

		case PROP_UID:
			service_set_uid (
				CAMEL_SERVICE (object),
				g_value_get_string (value));
			return;

		case PROP_URL:
			service_set_url (
				CAMEL_SERVICE (object),
				g_value_get_boxed (value));
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

		case PROP_UID:
			g_value_set_string (
				value, camel_service_get_uid (
				CAMEL_SERVICE (object)));
			return;

		case PROP_URL:
			g_value_set_boxed (
				value, camel_service_get_url (
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
		g_object_unref (priv->session);
		priv->session = NULL;
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

	if (priv->url != NULL)
		camel_url_free (priv->url);

	g_free (priv->user_data_dir);
	g_free (priv->uid);

	g_static_rec_mutex_free (&priv->connect_lock);
	g_static_mutex_free (&priv->connect_op_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_service_parent_class)->finalize (object);
}

static void
service_constructed (GObject *object)
{
	CamelService *service;
	CamelSession *session;
	const gchar *base_data_dir;
	const gchar *uid;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (camel_service_parent_class)->constructed (object);

	service = CAMEL_SERVICE (object);
	session = camel_service_get_session (service);

	uid = camel_service_get_uid (service);
	base_data_dir = camel_session_get_user_data_dir (session);

	service->priv->user_data_dir =
		g_build_filename (base_data_dir, uid, NULL);
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

static void
service_cancel_connect (CamelService *service)
{
	g_cancellable_cancel (service->priv->connect_op);
}

static gboolean
service_connect_sync (CamelService *service,
                      GCancellable *cancellable,
                      GError **error)
{
	/* Things like the CamelMboxStore can validly
	 * not define a connect function. */
	 return TRUE;
}

static gboolean
service_disconnect_sync (CamelService *service,
                         gboolean clean,
                         GCancellable *cancellable,
                         GError **error)
{
	/* We let people get away with not having a disconnect
	 * function -- CamelMboxStore, for example. */
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
service_query_auth_types_thread (GSimpleAsyncResult *simple,
                                 GObject *object,
                                 GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->auth_types = camel_service_query_auth_types_sync (
		CAMEL_SERVICE (object), cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
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
	CamelProvider *provider;
	CamelService *service;
	CamelURL *url;
	gboolean success = FALSE;
	const gchar *new_data_dir;
	gchar *old_data_dir;
	gchar *url_string;

	service = CAMEL_SERVICE (initable);
	url = camel_service_get_camel_url (service);
	provider = camel_service_get_provider (service);

	url_string = camel_url_to_string (url, CAMEL_URL_HIDE_PASSWORD);

	if (CAMEL_PROVIDER_NEEDS (provider, CAMEL_URL_PART_USER)) {
		if (url->user == NULL || *url->user == '\0') {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_URL_INVALID,
				_("URL '%s' needs a user component"),
				url_string);
			goto exit;
		}
	}

	if (CAMEL_PROVIDER_NEEDS (provider, CAMEL_URL_PART_HOST)) {
		if (url->host == NULL || *url->host == '\0') {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_URL_INVALID,
				_("URL '%s' needs a host component"),
				url_string);
			goto exit;
		}
	}

	if (CAMEL_PROVIDER_NEEDS (provider, CAMEL_URL_PART_PATH)) {
		if (url->path == NULL || *url->path == '\0') {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_URL_INVALID,
				_("URL '%s' needs a path component"),
				url_string);
			goto exit;
		}
	}

	new_data_dir = camel_service_get_user_data_dir (service);
	old_data_dir = service_find_old_data_dir (service);

	/* If the old data directory name exists, try renaming
	 * it to the new data directory.  Failure is non-fatal. */
	if (old_data_dir != NULL) {
		g_rename (old_data_dir, new_data_dir);
		g_free (old_data_dir);
	}

	success = TRUE;

exit:
	g_free (url_string);

	return success;
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

	class->get_name = service_get_name;
	class->cancel_connect = service_cancel_connect;
	class->connect_sync = service_connect_sync;
	class->disconnect_sync = service_disconnect_sync;
	class->query_auth_types_sync = service_query_auth_types_sync;

	class->query_auth_types = service_query_auth_types;
	class->query_auth_types_finish = service_query_auth_types_finish;

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
		PROP_UID,
		g_param_spec_string (
			"uid",
			"UID",
			"The unique identity of the service",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_URL,
		g_param_spec_boxed (
			"url",
			"URL",
			"The CamelURL for the service",
			CAMEL_TYPE_URL,
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

	service->priv->status = CAMEL_SERVICE_DISCONNECTED;

	g_static_rec_mutex_init (&service->priv->connect_lock);
	g_static_mutex_init (&service->priv->connect_op_lock);
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
 * camel_service_cancel_connect:
 * @service: a #CamelService
 *
 * If @service is currently attempting to connect to or disconnect
 * from a server, this causes it to stop and fail. Otherwise it is a
 * no-op.
 **/
void
camel_service_cancel_connect (CamelService *service)
{
	CamelServiceClass *class;

	g_return_if_fail (CAMEL_IS_SERVICE (service));

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_if_fail (class->cancel_connect != NULL);

	camel_service_lock (service, CAMEL_SERVICE_CONNECT_OP_LOCK);
	if (service->priv->connect_op)
		class->cancel_connect (service);
	camel_service_unlock (service, CAMEL_SERVICE_CONNECT_OP_LOCK);
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
	g_return_val_if_fail (service->priv->url, NULL);

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

	return service->priv->session;
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
 * camel_service_get_camel_url:
 * @service: a #CamelService
 *
 * Returns the #CamelURL representing @service.
 *
 * Returns: the #CamelURL representing @service
 *
 * Since: 3.2
 **/
CamelURL *
camel_service_get_camel_url (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	return service->priv->url;
}

/**
 * camel_service_get_url:
 * @service: a #CamelService
 *
 * Gets the URL representing @service. The returned URL must be
 * freed when it is no longer needed. For security reasons, this
 * routine does not return the password.
 *
 * Returns: the URL representing @service
 **/
gchar *
camel_service_get_url (CamelService *service)
{
	CamelURL *url;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	url = camel_service_get_camel_url (service);

	return camel_url_to_string (url, CAMEL_URL_HIDE_PASSWORD);
}

/**
 * camel_service_connect_sync:
 * @service: a #CamelService
 * @error: return location for a #GError, or %NULL
 *
 * Connect to the service using the parameters it was initialized
 * with.
 *
 * Returns: %TRUE if the connection is made or %FALSE otherwise
 **/
gboolean
camel_service_connect_sync (CamelService *service,
                            GError **error)
{
	CamelServiceClass *class;
	GCancellable *op;
	gboolean ret = FALSE;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), FALSE);
	g_return_val_if_fail (service->priv->session != NULL, FALSE);
	g_return_val_if_fail (service->priv->url != NULL, FALSE);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->connect_sync != NULL, FALSE);

	camel_service_lock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (service->priv->status == CAMEL_SERVICE_CONNECTED) {
		camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
		return TRUE;
	}

	/* Register a separate operation for connecting, so that
	 * the offline code can cancel it. */
	camel_service_lock (service, CAMEL_SERVICE_CONNECT_OP_LOCK);
	service->priv->connect_op = camel_operation_new ();
	op = service->priv->connect_op;
	camel_service_unlock (service, CAMEL_SERVICE_CONNECT_OP_LOCK);

	service->priv->status = CAMEL_SERVICE_CONNECTING;
	ret = class->connect_sync (service, service->priv->connect_op, error);
	CAMEL_CHECK_GERROR (service, connect_sync, ret, error);
	service->priv->status =
		ret ? CAMEL_SERVICE_CONNECTED : CAMEL_SERVICE_DISCONNECTED;

	camel_service_lock (service, CAMEL_SERVICE_CONNECT_OP_LOCK);
	g_object_unref (op);
	if (op == service->priv->connect_op)
		service->priv->connect_op = NULL;
	camel_service_unlock (service, CAMEL_SERVICE_CONNECT_OP_LOCK);

	camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	return ret;
}

/**
 * camel_service_disconnect_sync:
 * @service: a #CamelService
 * @clean: whether or not to try to disconnect cleanly
 * @error: return location for a #GError, or %NULL
 *
 * Disconnect from the service. If @clean is %FALSE, it should not
 * try to do any synchronizing or other cleanup of the connection.
 *
 * Returns: %TRUE if the disconnect was successful or %FALSE otherwise
 **/
gboolean
camel_service_disconnect_sync (CamelService *service,
                               gboolean clean,
                               GError **error)
{
	CamelServiceClass *class;
	gboolean res = TRUE;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), FALSE);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->disconnect_sync != NULL, FALSE);

	camel_service_lock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (service->priv->status != CAMEL_SERVICE_DISCONNECTED
	    && service->priv->status != CAMEL_SERVICE_DISCONNECTING) {
		GCancellable *op;
		camel_service_lock (service, CAMEL_SERVICE_CONNECT_OP_LOCK);
		service->priv->connect_op = camel_operation_new ();
		op = service->priv->connect_op;
		camel_service_unlock (service, CAMEL_SERVICE_CONNECT_OP_LOCK);

		service->priv->status = CAMEL_SERVICE_DISCONNECTING;
		res = class->disconnect_sync (
			service, clean, service->priv->connect_op, error);
		CAMEL_CHECK_GERROR (service, disconnect_sync, res, error);
		service->priv->status = CAMEL_SERVICE_DISCONNECTED;

		camel_service_lock (service, CAMEL_SERVICE_CONNECT_OP_LOCK);
		g_object_unref (op);
		if (op == service->priv->connect_op)
			service->priv->connect_op = NULL;
		camel_service_unlock (service, CAMEL_SERVICE_CONNECT_OP_LOCK);
	}

	camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	service->priv->status = CAMEL_SERVICE_DISCONNECTED;

	return res;
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
		CAMEL_IS_SERVICE (service), CAMEL_SERVICE_DISCONNECTED);

	return service->priv->status;
}

/**
 * camel_service_lock:
 * @service: a #CamelService
 * @lock: lock type to lock
 *
 * Locks @service's @lock. Unlock it with camel_service_unlock().
 *
 * Since: 2.32
 **/
void
camel_service_lock (CamelService *service,
                    CamelServiceLock lock)
{
	g_return_if_fail (CAMEL_IS_SERVICE (service));

	switch (lock) {
		case CAMEL_SERVICE_REC_CONNECT_LOCK:
			g_static_rec_mutex_lock (&service->priv->connect_lock);
			break;
		case CAMEL_SERVICE_CONNECT_OP_LOCK:
			g_static_mutex_lock (&service->priv->connect_op_lock);
			break;
		default:
			g_return_if_reached ();
	}
}

/**
 * camel_service_unlock:
 * @service: a #CamelService
 * @lock: lock type to unlock
 *
 * Unlocks @service's @lock, previously locked with camel_service_lock().
 *
 * Since: 2.32
 **/
void
camel_service_unlock (CamelService *service,
                      CamelServiceLock lock)
{
	g_return_if_fail (CAMEL_IS_SERVICE (service));

	switch (lock) {
		case CAMEL_SERVICE_REC_CONNECT_LOCK:
			g_static_rec_mutex_unlock (&service->priv->connect_lock);
			break;
		case CAMEL_SERVICE_CONNECT_OP_LOCK:
			g_static_mutex_unlock (&service->priv->connect_op_lock);
			break;
		default:
			g_return_if_reached ();
	}
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
	GList *list;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->query_auth_types_sync != NULL, NULL);

	/* Note that we get the connect lock here, which means the
	 * callee must not call the connect functions itself. */
	camel_service_lock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
	list = class->query_auth_types_sync (service, cancellable, error);
	camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	return list;
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


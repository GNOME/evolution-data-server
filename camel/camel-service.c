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

#include <glib/gi18n-lib.h>

#include "camel-debug.h"
#include "camel-operation.h"
#include "camel-service.h"
#include "camel-session.h"

#define d(x)
#define w(x)

struct _CamelServicePrivate {
	CamelSession *session;
	CamelProvider *provider;
	CamelURL *url;

	GCancellable *connect_op;
	CamelServiceConnectionStatus status;

	GStaticRecMutex connect_lock;	/* for locking connection operations */
	GStaticMutex connect_op_lock;	/* for locking the connection_op */
};

G_DEFINE_ABSTRACT_TYPE (CamelService, camel_service, CAMEL_TYPE_OBJECT)

static void
service_finalize (GObject *object)
{
	CamelService *service = CAMEL_SERVICE (object);

	if (service->priv->status == CAMEL_SERVICE_CONNECTED)
		CAMEL_SERVICE_GET_CLASS (service)->disconnect_sync (
			service, TRUE, NULL, NULL);

	if (service->priv->url)
		camel_url_free (service->priv->url);

	if (service->priv->session)
		g_object_unref (service->priv->session);

	g_static_rec_mutex_free (&service->priv->connect_lock);
	g_static_mutex_free (&service->priv->connect_op_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_service_parent_class)->finalize (object);
}

static gboolean
service_construct (CamelService *service,
                   CamelSession *session,
                   CamelProvider *provider,
                   CamelURL *url,
                   GError **error)
{
	gchar *err, *url_string;

	if (CAMEL_PROVIDER_NEEDS (provider, CAMEL_URL_PART_USER) &&
	    (url->user == NULL || url->user[0] == '\0')) {
		err = _("URL '%s' needs a username component");
		goto fail;
	} else if (CAMEL_PROVIDER_NEEDS (provider, CAMEL_URL_PART_HOST) &&
		   (url->host == NULL || url->host[0] == '\0')) {
		err = _("URL '%s' needs a host component");
		goto fail;
	} else if (CAMEL_PROVIDER_NEEDS (provider, CAMEL_URL_PART_PATH) &&
		   (url->path == NULL || url->path[0] == '\0')) {
		err = _("URL '%s' needs a path component");
		goto fail;
	}

	service->priv->provider = provider;
	service->priv->url = camel_url_copy (url);
	service->priv->session = g_object_ref (session);

	service->priv->status = CAMEL_SERVICE_DISCONNECTED;

	return TRUE;

fail:
	url_string = camel_url_to_string (url, CAMEL_URL_HIDE_PASSWORD);
	g_set_error (
		error, CAMEL_SERVICE_ERROR,
		CAMEL_SERVICE_ERROR_URL_INVALID,
		err, url_string);
	g_free (url_string);

	return FALSE;
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

static gchar *
service_get_path (CamelService *service)
{
	CamelProvider *prov = service->priv->provider;
	CamelURL *url = service->priv->url;
	GString *gpath;
	gchar *path;

	/* A sort of ad-hoc default implementation that works for our
	 * current set of services.
	 */

	gpath = g_string_new (service->priv->provider->protocol);
	if (CAMEL_PROVIDER_ALLOWS (prov, CAMEL_URL_PART_USER)) {
		if (CAMEL_PROVIDER_ALLOWS (prov, CAMEL_URL_PART_HOST)) {
			g_string_append_printf (gpath, "/%s@%s",
						url->user ? url->user : "",
						url->host ? url->host : "");

			if (url->port)
				g_string_append_printf (gpath, ":%d", url->port);
		} else {
			g_string_append_printf (gpath, "/%s%s", url->user ? url->user : "",
						CAMEL_PROVIDER_NEEDS (prov, CAMEL_URL_PART_USER) ? "" : "@");
		}
	} else if (CAMEL_PROVIDER_ALLOWS (prov, CAMEL_URL_PART_HOST)) {
		g_string_append_printf (gpath, "/%s%s",
					CAMEL_PROVIDER_NEEDS (prov, CAMEL_URL_PART_HOST) ? "" : "@",
					url->host ? url->host : "");

		if (url->port)
			g_string_append_printf (gpath, ":%d", url->port);
	}

	if (CAMEL_PROVIDER_NEEDS (prov, CAMEL_URL_PART_PATH))
		g_string_append_printf (gpath, "%s%s", *url->path == '/' ? "" : "/", url->path);

	path = gpath->str;
	g_string_free (gpath, FALSE);

	return path;
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
camel_service_class_init (CamelServiceClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelServicePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = service_finalize;

	class->construct = service_construct;
	class->get_name = service_get_name;
	class->get_path = service_get_path;
	class->cancel_connect = service_cancel_connect;
	class->connect_sync = service_connect_sync;
	class->disconnect_sync = service_disconnect_sync;
	class->query_auth_types_sync = service_query_auth_types_sync;
}

static void
camel_service_init (CamelService *service)
{
	service->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		service, CAMEL_TYPE_SERVICE, CamelServicePrivate);

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
 * camel_service_construct:
 * @service: a #CamelService
 * @session: the #CamelSession for @service
 * @provider: the #CamelProvider associated with @service
 * @url: the default URL for the service (may be %NULL)
 * @error: return location for a #GError, or %NULL
 *
 * Constructs a #CamelService initialized with the given parameters.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_service_construct (CamelService *service,
                         CamelSession *session,
                         CamelProvider *provider,
                         CamelURL *url,
                         GError **error)
{
	CamelServiceClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), FALSE);
	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->construct != NULL, FALSE);

	success = class->construct (service, session, provider, url, error);
	CAMEL_CHECK_GERROR (service, construct, success, error);

	return success;
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
 * camel_service_get_path:
 * @service: a #CamelService
 *
 * This gets a valid UNIX relative path describing @service, which
 * is guaranteed to be different from the path returned for any
 * different service. This path MUST start with the name of the
 * provider, followed by a "/", but after that, it is up to the
 * provider.
 *
 * Returns: the path, which the caller must free
 **/
gchar *
camel_service_get_path (CamelService *service)
{
	CamelServiceClass *class;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);
	g_return_val_if_fail (service->priv->url, NULL);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->get_path != NULL, NULL);

	return class->get_path (service);
}

/**
 * camel_service_get_session:
 * @service: a #CamelService
 *
 * Gets the #CamelSession associated with the service.
 *
 * Returns: the session
 **/
CamelSession *
camel_service_get_session (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	return service->priv->session;
}

/**
 * camel_service_get_provider:
 * @service: a #CamelService
 *
 * Gets the #CamelProvider associated with the service.
 *
 * Returns: the provider
 **/
CamelProvider *
camel_service_get_provider (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	return service->priv->provider;
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
 * camel_service_query_auth_types_sync:
 * @service: a #CamelService
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * This is used by the mail source wizard to get the list of
 * authentication types supported by the protocol, and information
 * about them.
 *
 * Returns: a list of #CamelServiceAuthType records. The caller
 * must free the list with #g_list_free when it is done with it.
 **/
GList *
camel_service_query_auth_types_sync (CamelService *service,
                                     GCancellable *cancellable,
                                     GError **error)
{
	CamelServiceClass *class;
	GList *ret;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->query_auth_types_sync != NULL, NULL);

	/* Note that we get the connect lock here, which means the
	 * callee must not call the connect functions itself. */
	camel_service_lock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
	ret = class->query_auth_types_sync (service, cancellable, error);
	camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	return ret;
}

/**
 * camel_service_lock:
 * @service: a #CamelService
 * @lock: lock type to lock
 *
 * Locks #service's #lock. Unlock it with camel_service_unlock().
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
 * Unlocks #service's #lock, previously locked with camel_service_lock().
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

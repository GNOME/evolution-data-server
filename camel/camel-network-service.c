/*
 * camel-network-service.c
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

#include "camel-network-service.h"

#include <config.h>
#include <glib/gi18n-lib.h>

#include <camel/camel-enumtypes.h>
#include <camel/camel-network-settings.h>
#include <camel/camel-service.h>
#include <camel/camel-session.h>
#include <camel/camel-tcp-stream-raw.h>

#include <camel/camel-tcp-stream-ssl.h>

#define PRIVATE_KEY "CamelNetworkService:private"

#define CAMEL_NETWORK_SERVICE_GET_PRIVATE(obj) \
	(g_object_get_data (G_OBJECT (obj), PRIVATE_KEY))

typedef struct _CamelNetworkServicePrivate CamelNetworkServicePrivate;

struct _CamelNetworkServicePrivate {
	GMutex property_lock;
	GSocketConnectable *connectable;
};

/* Forward Declarations */
void		camel_network_service_init	(CamelNetworkService *service);

G_DEFINE_INTERFACE (
	CamelNetworkService,
	camel_network_service,
	CAMEL_TYPE_SERVICE)

static CamelNetworkServicePrivate *
network_service_private_new (CamelNetworkService *service)
{
	CamelNetworkServicePrivate *priv;

	priv = g_slice_new0 (CamelNetworkServicePrivate);

	g_mutex_init (&priv->property_lock);

	return priv;
}

static void
network_service_private_free (CamelNetworkServicePrivate *priv)
{
	g_clear_object (&priv->connectable);

	g_mutex_clear (&priv->property_lock);

	g_slice_free (CamelNetworkServicePrivate, priv);
}

static CamelStream *
network_service_connect_sync (CamelNetworkService *service,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelNetworkSecurityMethod method;
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelSession *session;
	CamelStream *stream;
	const gchar *service_name;
	guint16 default_port;
	guint16 port;
	gchar *socks_host;
	gint socks_port;
	gchar *host;
	gint status;

	session = camel_service_ref_session (CAMEL_SERVICE (service));
	settings = camel_service_ref_settings (CAMEL_SERVICE (service));
	g_return_val_if_fail (CAMEL_IS_NETWORK_SETTINGS (settings), NULL);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	method = camel_network_settings_get_security_method (network_settings);
	host = camel_network_settings_dup_host (network_settings);
	port = camel_network_settings_get_port (network_settings);

	g_object_unref (settings);

	service_name = camel_network_service_get_service_name (service, method);
	default_port = camel_network_service_get_default_port (service, method);

	/* If the URL explicitly gives a port number, make
	 * it override the service name and default port. */
	if (port > 0) {
		service_name = g_alloca (16);
		sprintf ((gchar *) service_name, "%u", port);
		default_port = 0;
	}

	switch (method) {
		case CAMEL_NETWORK_SECURITY_METHOD_NONE:
			stream = camel_tcp_stream_raw_new ();
			break;

		case CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT:
			stream = camel_tcp_stream_ssl_new_raw (
				session, host,
				CAMEL_TCP_STREAM_SSL_ENABLE_TLS);
			break;

		case CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT:
			stream = camel_tcp_stream_ssl_new (
				session, host,
				CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 |
				CAMEL_TCP_STREAM_SSL_ENABLE_SSL3);
			break;

		default:
			g_return_val_if_reached (NULL);
	}

	camel_session_get_socks_proxy (session, host, &socks_host, &socks_port);

	if (socks_host != NULL) {
		camel_tcp_stream_set_socks_proxy (
			CAMEL_TCP_STREAM (stream),
			socks_host, socks_port);
		g_free (socks_host);
	}

	status = camel_tcp_stream_connect (
		CAMEL_TCP_STREAM (stream), host,
		service_name, default_port, cancellable, error);

	if (status == -1) {
		/* Translators: The first '%s' is replaced with a host name, the second '%s' with service name or port number */
		g_prefix_error (
			error, _("Could not connect to '%s:%s': "), host, service_name ? service_name : "???");
		g_object_unref (stream);
		stream = NULL;
	}

	g_free (host);

	g_object_unref (session);

	return stream;
}

static GSocketConnectable *
network_service_new_connectable (CamelNetworkService *service)
{
	GSocketConnectable *connectable = NULL;
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	guint16 port;
	gchar *host;

	/* Some services might want to override this method to
	 * create a GNetworkService instead of a GNetworkAddress. */

	settings = camel_service_ref_settings (CAMEL_SERVICE (service));
	g_return_val_if_fail (CAMEL_IS_NETWORK_SETTINGS (settings), NULL);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	port = camel_network_settings_get_port (network_settings);

	if (host != NULL)
		connectable = g_network_address_new (host, port);

	g_free (host);

	g_object_unref (settings);

	return connectable;
}

static void
camel_network_service_default_init (CamelNetworkServiceInterface *interface)
{
	interface->connect_sync = network_service_connect_sync;
	interface->new_connectable = network_service_new_connectable;

	g_object_interface_install_property (
		interface,
		g_param_spec_object (
			"connectable",
			"Connectable",
			"Socket endpoint of a network service",
			G_TYPE_SOCKET_CONNECTABLE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));
}

void
camel_network_service_init (CamelNetworkService *service)
{
	/* This is called from CamelService during instance
	 * construction.  It is not part of the public API. */

	g_return_if_fail (CAMEL_IS_NETWORK_SERVICE (service));

	g_object_set_data_full (
		G_OBJECT (service), PRIVATE_KEY,
		network_service_private_new (service),
		(GDestroyNotify) network_service_private_free);
}

/**
 * camel_network_service_get_service_name:
 * @service: a #CamelNetworkService
 * @method: a #CamelNetworkSecurityMethod
 *
 * Returns the standard network service name for @service and the security
 * method @method, as defined in /etc/services.  For example, the service
 * name for unencrypted IMAP or encrypted IMAP using STARTTLS is "imap",
 * but the service name for IMAP over SSL is "imaps".
 *
 * Returns: the network service name for @service and @method, or %NULL
 *
 * Since: 3.2
 **/
const gchar *
camel_network_service_get_service_name (CamelNetworkService *service,
                                        CamelNetworkSecurityMethod method)
{
	CamelNetworkServiceInterface *interface;
	const gchar *service_name = NULL;

	g_return_val_if_fail (CAMEL_IS_NETWORK_SERVICE (service), NULL);

	interface = CAMEL_NETWORK_SERVICE_GET_INTERFACE (service);

	if (interface->get_service_name != NULL)
		service_name = interface->get_service_name (service, method);

	return service_name;
}

/**
 * camel_network_service_get_default_port:
 * @service: a #CamelNetworkService
 * @method: a #CamelNetworkSecurityMethod
 *
 * Returns the default network port number for @service and the security
 * method @method, as defined in /etc/services.  For example, the default
 * port for unencrypted IMAP or encrypted IMAP using STARTTLS is 143, but
 * the default port for IMAP over SSL is 993.
 *
 * Returns: the default port number for @service and @method
 *
 * Since: 3.2
 **/
guint16
camel_network_service_get_default_port (CamelNetworkService *service,
                                        CamelNetworkSecurityMethod method)
{
	CamelNetworkServiceInterface *interface;
	guint16 default_port = 0;

	g_return_val_if_fail (CAMEL_IS_NETWORK_SERVICE (service), 0);

	interface = CAMEL_NETWORK_SERVICE_GET_INTERFACE (service);

	if (interface->get_default_port != NULL)
		default_port = interface->get_default_port (service, method);

	return default_port;
}

/**
 * camel_network_service_ref_connectable:
 * @service: a #CamelNetworkService
 *
 * Returns the socket endpoint for the network service to which @service
 * is a client.
 *
 * The returned #GSocketConnectable is referenced for thread-safety and
 * must be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: a #GSocketConnectable
 *
 * Since: 3.8
 **/
GSocketConnectable *
camel_network_service_ref_connectable (CamelNetworkService *service)
{
	CamelNetworkServicePrivate *priv;
	GSocketConnectable *connectable = NULL;

	g_return_val_if_fail (CAMEL_IS_NETWORK_SERVICE (service), NULL);

	priv = CAMEL_NETWORK_SERVICE_GET_PRIVATE (service);
	g_return_val_if_fail (priv != NULL, NULL);

	g_mutex_lock (&priv->property_lock);

	if (priv->connectable != NULL)
		connectable = g_object_ref (priv->connectable);

	g_mutex_unlock (&priv->property_lock);

	return connectable;
}

/**
 * camel_network_service_set_connectable:
 * @service: a #CamelNetworkService
 * @connectable: a #GSocketConnectable, or %NULL
 *
 * Sets the socket endpoint for the network service to which @service is
 * a client.  If @connectable is %NULL, a #GSocketConnectable is derived
 * from the @service's #CamelNetworkSettings.
 *
 * Since: 3.8
 **/
void
camel_network_service_set_connectable (CamelNetworkService *service,
                                       GSocketConnectable *connectable)
{
	CamelNetworkServiceInterface *interface;
	CamelNetworkServicePrivate *priv;

	g_return_if_fail (CAMEL_IS_NETWORK_SERVICE (service));

	interface = CAMEL_NETWORK_SERVICE_GET_INTERFACE (service);
	g_return_if_fail (interface->new_connectable != NULL);

	priv = CAMEL_NETWORK_SERVICE_GET_PRIVATE (service);
	g_return_if_fail (priv != NULL);

	if (connectable != NULL) {
		g_return_if_fail (G_IS_SOCKET_CONNECTABLE (connectable));
		g_object_ref (connectable);
	} else {
		/* This may return NULL if we don't have valid network
		 * settings from which to create a GSocketConnectable. */
		connectable = interface->new_connectable (service);
	}

	g_mutex_lock (&priv->property_lock);

	if (priv->connectable != NULL)
		g_object_unref (priv->connectable);

	priv->connectable = connectable;

	g_mutex_unlock (&priv->property_lock);

	g_object_notify (G_OBJECT (service), "connectable");
}

/**
 * camel_network_service_connect_sync:
 * @service: a #CamelNetworkService
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Attempts to establish a network connection to the server described by
 * @service, using the preferred #CamelNetworkSettings:security-method to
 * secure the connection.  If a connection cannot be established, or the
 * connection attempt is cancelled, the function sets @error and returns
 * %NULL.
 *
 * Returns: a #CamelStream, or %NULL
 *
 * Since: 3.2
 **/
CamelStream *
camel_network_service_connect_sync (CamelNetworkService *service,
                                    GCancellable *cancellable,
                                    GError **error)
{
	CamelNetworkServiceInterface *interface;

	g_return_val_if_fail (CAMEL_IS_NETWORK_SERVICE (service), NULL);

	interface = CAMEL_NETWORK_SERVICE_GET_INTERFACE (service);
	g_return_val_if_fail (interface->connect_sync != NULL, NULL);

	return interface->connect_sync (service, cancellable, error);
}

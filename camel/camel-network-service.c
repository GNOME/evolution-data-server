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

G_DEFINE_INTERFACE (
	CamelNetworkService,
	camel_network_service,
	CAMEL_TYPE_SERVICE)

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

	session = camel_service_get_session (CAMEL_SERVICE (service));
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
		g_prefix_error (
			error, _("Could not connect to %s: "), host);
		g_object_unref (stream);
		stream = NULL;
	}

	g_free (host);

	return stream;
}

static void
camel_network_service_default_init (CamelNetworkServiceInterface *interface)
{
	interface->connect_sync = network_service_connect_sync;
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

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

#include <camel/camel-service.h>
#include <camel/camel-session.h>
#include <camel/camel-tcp-stream-raw.h>

#if CAMEL_HAVE_SSL
#include <camel/camel-tcp-stream-ssl.h>
#endif

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
	CamelSession *session;
	CamelStream *stream;
	CamelURL *url;
	const gchar *service_name;
	guint16 default_port;
	gchar *socks_host;
	gint socks_port;
	gint status;

	url = camel_service_get_camel_url (CAMEL_SERVICE (service));
	session = camel_service_get_session (CAMEL_SERVICE (service));

	method = camel_network_service_get_security_method (service);
	service_name = camel_network_service_get_service_name (service);
	default_port = camel_network_service_get_default_port (service);

	/* If the URL explicitly gives a port number, make
	 * it override the service name and default port. */
	if (url->port > 0) {
		service_name = g_alloca (16);
		sprintf ((gchar *) service_name, "%d", url->port);
		default_port = 0;
	}

	switch (method) {
		case CAMEL_NETWORK_SECURITY_METHOD_NONE:
			stream = camel_tcp_stream_raw_new ();
			break;

#ifdef CAMEL_HAVE_SSL
		case CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT:
			stream = camel_tcp_stream_ssl_new_raw (
				session, url->host,
				CAMEL_TCP_STREAM_SSL_ENABLE_TLS);
			break;

		case CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT:
			stream = camel_tcp_stream_ssl_new (
				session, url->host,
				CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 |
				CAMEL_TCP_STREAM_SSL_ENABLE_SSL3);
			break;
#else
		case CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT:
		case CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT:
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("Could not connect to %s: %s"),
				url->host, _("SSL unavailable"));
			return NULL;
#endif

		default:
			g_return_val_if_reached (NULL);
	}

	camel_session_get_socks_proxy (session, &socks_host, &socks_port);

	if (socks_host != NULL) {
		camel_tcp_stream_set_socks_proxy (
			CAMEL_TCP_STREAM (stream),
			socks_host, socks_port);
		g_free (socks_host);
	}

	status = camel_tcp_stream_connect (
		CAMEL_TCP_STREAM (stream), url->host,
		service_name, default_port, cancellable, error);

	if (status == -1) {
		g_prefix_error (
			error, _("Could not connect to %s: "), url->host);
		g_object_unref (stream);
		stream = NULL;
	}

	return stream;
}

static void
camel_network_service_default_init (CamelNetworkServiceInterface *interface)
{
	interface->connect_sync = network_service_connect_sync;

	g_object_interface_install_property (
		interface,
		g_param_spec_uint (
			"default-port",
			"Default Port",
			"Default IP port",
			0,
			G_MAXUINT16,
			0,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));

	g_object_interface_install_property (
		interface,
		g_param_spec_uint (
			"security-method",
			"Security Method",
			"Method used to establish a network connection",
			CAMEL_NETWORK_SECURITY_METHOD_NONE,
			CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT,
			CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_interface_install_property (
		interface,
		g_param_spec_string (
			"service-name",
			"Service Name",
			"Network service name",
			NULL,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));
}

/**
 * camel_network_service_get_service_name:
 * @service: a #CamelNetworkService
 *
 * Returns the standard network service name for @service as defined
 * in /etc/services.  The service name may depend on the value of the
 * #CamelNetworkService:security-method property.  For example, the
 * service name for unencrypted IMAP or encrypted IMAP using STARTTLS
 * is "imap", but the service name for IMAP over SSL is "imaps".
 *
 * Returns: the network service name for @service, or %NULL
 *
 * Since: 3.2
 **/
const gchar *
camel_network_service_get_service_name (CamelNetworkService *service)
{
	CamelNetworkServiceInterface *interface;
	const gchar *service_name = NULL;

	g_return_val_if_fail (CAMEL_IS_NETWORK_SERVICE (service), NULL);

	interface = CAMEL_NETWORK_SERVICE_GET_INTERFACE (service);

	if (interface->get_service_name != NULL)
		service_name = interface->get_service_name (service);

	return service_name;
}

/**
 * camel_network_service_get_default_port:
 * @service: a #CamelNetworkService
 *
 * Returns the default network port number of @service as defined
 * in /etc/services.  The default port may depend on the value of the
 * #CamelNetworkService:security-method property.  For example, the
 * default port for unencrypted IMAP or encrypted IMAP using STARTTLS
 * is 143, but the default port for IMAP over SSL is 993.
 *
 * Returns: the default port number for @service
 *
 * Since: 3.2
 **/
guint16
camel_network_service_get_default_port (CamelNetworkService *service)
{
	CamelNetworkServiceInterface *interface;
	guint16 default_port = 0;

	g_return_val_if_fail (CAMEL_IS_NETWORK_SERVICE (service), 0);

	interface = CAMEL_NETWORK_SERVICE_GET_INTERFACE (service);

	if (interface->get_default_port != NULL)
		default_port = interface->get_default_port (service);

	return default_port;
}

/**
 * camel_network_service_get_security_method:
 * @service: a #CamelNetworkService
 *
 * Return the method used to establish a secure (or unsecure) network
 * connection.
 *
 * Returns: the security method
 *
 * Since: 3.2
 **/
CamelNetworkSecurityMethod
camel_network_service_get_security_method (CamelNetworkService *service)
{
	gpointer data;

	g_return_val_if_fail (
		CAMEL_IS_NETWORK_SERVICE (service),
		CAMEL_NETWORK_SECURITY_METHOD_NONE);

	data = g_object_get_data (
		G_OBJECT (service), "CamelNetworkService:security-method");

	return (CamelNetworkSecurityMethod) GPOINTER_TO_INT (data);
}

/**
 * camel_network_service_set_security_method:
 * @service: a #CamelNetworkService
 * @method: the security method
 *
 * Sets the method used to establish a secure (or unsecure) network
 * connection.  Note that changing this setting has no effect on an
 * already-established network connection.
 *
 * Since: 3.2
 **/
void
camel_network_service_set_security_method (CamelNetworkService *service,
                                           CamelNetworkSecurityMethod method)
{
	GObject *object;

	g_return_if_fail (CAMEL_IS_NETWORK_SERVICE (service));

	object = G_OBJECT (service);

	g_object_set_data (
		object, "CamelNetworkService:security-method",
		GINT_TO_POINTER (method));

	g_object_freeze_notify (object);
	g_object_notify (object, "default-port");
	g_object_notify (object, "security-method");
	g_object_notify (object, "service-name");
	g_object_thaw_notify (object);
}

/**
 * camel_network_service_connect_sync:
 * @service: a #CamelNetworkService
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Attempts to establish a network connection to the server described by
 * @service, using the preferred #CamelNetworkService:security-method to
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

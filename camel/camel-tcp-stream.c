/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "camel-debug.h"
#include "camel-tcp-stream.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define w(x)

#define CAMEL_TCP_STREAM_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_TCP_STREAM, CamelTcpStreamPrivate))

struct _CamelTcpStreamPrivate {
	gchar *socks_host;
	gint socks_port;
};

G_DEFINE_TYPE (CamelTcpStream, camel_tcp_stream, CAMEL_TYPE_STREAM)

static void
camel_tcp_stream_finalize (GObject *object)
{
	CamelTcpStream *stream = CAMEL_TCP_STREAM (object);

	g_free (stream->priv->socks_host);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_tcp_stream_parent_class)->finalize (object);
}

static void
camel_tcp_stream_class_init (CamelTcpStreamClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelTcpStreamPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = camel_tcp_stream_finalize;
}

static void
camel_tcp_stream_init (CamelTcpStream *tcp_stream)
{
	tcp_stream->priv = CAMEL_TCP_STREAM_GET_PRIVATE (tcp_stream);
}

/**
 * camel_tcp_stream_connect:
 * @stream: a #CamelTcpStream object
 * @host: Hostname for connection
 * @service: Service name or port number in string form
 * @fallback_port: Port number to retry if @service is not present
 * in the system's services database, or 0 to avoid retrying
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Create a socket and connect based upon the data provided.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_tcp_stream_connect (CamelTcpStream *stream,
                          const gchar *host,
                          const gchar *service,
                          gint fallback_port,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelTcpStreamClass *class;
	gint retval;

	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), -1);
	g_return_val_if_fail (host != NULL, -1);
	g_return_val_if_fail (service != NULL, -1);
	g_return_val_if_fail (error == NULL || *error == NULL, -1);

	class = CAMEL_TCP_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->connect != NULL, -1);

	retval = class->connect (
		stream, host, service, fallback_port, cancellable, error);
	CAMEL_CHECK_GERROR (stream, connect, retval == 0, error);

	return retval;
}

/**
 * camel_tcp_stream_getsockopt:
 * @stream: a #CamelTcpStream object
 * @data: socket option data
 *
 * Get the socket options set on the stream and populate @data.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_tcp_stream_getsockopt (CamelTcpStream *stream,
                             CamelSockOptData *data)
{
	CamelTcpStreamClass *class;

	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), -1);

	class = CAMEL_TCP_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->getsockopt != NULL, -1);

	return class->getsockopt (stream, data);
}

/**
 * camel_tcp_stream_setsockopt:
 * @stream: a #CamelTcpStream object
 * @data: socket option data
 *
 * Set the socket options contained in @data on the stream.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_tcp_stream_setsockopt (CamelTcpStream *stream,
                             const CamelSockOptData *data)
{
	CamelTcpStreamClass *class;

	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), -1);

	class = CAMEL_TCP_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->setsockopt != NULL, -1);

	return class->setsockopt (stream, data);
}

/**
 * camel_tcp_stream_get_local_address:
 * @stream: a #CamelTcpStream object
 * @len: pointer to address length which must be supplied
 *
 * Get the local address of @stream.
 *
 * Returns: the stream's local address (which must be freed with
 * g_free()) if the stream is connected, or %NULL if not
 *
 * Since: 2.22
 **/
struct sockaddr *
camel_tcp_stream_get_local_address (CamelTcpStream *stream,
                                    socklen_t *len)
{
	CamelTcpStreamClass *class;

	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), NULL);
	g_return_val_if_fail (len != NULL, NULL);

	class = CAMEL_TCP_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->get_local_address != NULL, NULL);

	return class->get_local_address (stream, len);
}

/**
 * camel_tcp_stream_get_remote_address:
 * @stream: a #CamelTcpStream object
 * @len: pointer to address length, which must be supplied
 *
 * Get the remote address of @stream.
 *
 * Returns: the stream's remote address (which must be freed with
 * g_free()) if the stream is connected, or %NULL if not.
 *
 * Since: 2.22
 **/
struct sockaddr *
camel_tcp_stream_get_remote_address (CamelTcpStream *stream,
                                     socklen_t *len)
{
	CamelTcpStreamClass *class;

	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), NULL);
	g_return_val_if_fail (len != NULL, NULL);

	class = CAMEL_TCP_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->get_remote_address != NULL, NULL);

	return class->get_remote_address (stream, len);
}

/**
 * camel_tcp_stream_get_file_desc:
 * @stream: a #CamelTcpStream
 *
 * Since: 2.32
 **/
PRFileDesc *
camel_tcp_stream_get_file_desc (CamelTcpStream *stream)
{
	CamelTcpStreamClass *class;

	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), NULL);

	class = CAMEL_TCP_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->get_file_desc != NULL, NULL);

	return class->get_file_desc (stream);
}

/**
 * camel_tcp_stream_set_socks_proxy:
 * @stream: a #CamelTcpStream object
 * @socks_host: hostname to use for the SOCKS proxy
 * @socks_port: port number to use for the SOCKS proxy
 *
 * Configures a SOCKS proxy for the specified @stream.  Instead of
 * direct connections, this @stream will instead go through the proxy.
 *
 * Since: 2.32
 */
void
camel_tcp_stream_set_socks_proxy (CamelTcpStream *stream,
                                  const gchar *socks_host,
                                  gint socks_port)
{
	g_return_if_fail (CAMEL_IS_TCP_STREAM (stream));

	g_free (stream->priv->socks_host);

	if (socks_host != NULL && socks_host[0] != '\0') {
		stream->priv->socks_host = g_strdup (socks_host);
		stream->priv->socks_port = socks_port;
	} else {
		stream->priv->socks_host = NULL;
		stream->priv->socks_port = 0;
	}
}

/**
 * camel_tcp_stream_peek_socks_proxy:
 * @stream: a #CamelTcpStream
 * @socks_host_ret: location to return the name of the SOCKS host
 * @socks_port_ret: location to return the port number in the SOCKS host
 *
 * Queries the SOCKS proxy that is configured for a @stream.  This will
 * return %NULL in @socks_host_ret if no proxy is configured.
 *
 * Since: 2.32
 */
void
camel_tcp_stream_peek_socks_proxy (CamelTcpStream *stream,
                                   const gchar **socks_host_ret,
                                   gint *socks_port_ret)
{
	g_return_if_fail (CAMEL_IS_TCP_STREAM (stream));

	if (socks_host_ret != NULL)
		*socks_host_ret = stream->priv->socks_host;

	if (socks_port_ret != NULL)
		*socks_port_ret = stream->priv->socks_port;
}

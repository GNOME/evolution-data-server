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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#include <nspr.h>
#include <prio.h>
#include <prerror.h>
#include <prerr.h>
#include <prthread.h>

#include <glib/gi18n-lib.h>

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "camel-file-utils.h"
#include "camel-net-utils.h"
#include "camel-operation.h"
#include "camel-service.h"
#include "camel-tcp-stream-raw.h"

#define d(x)

#define IO_TIMEOUT (PR_TicksPerSecond() * 1 * 60)
#define CONNECT_TIMEOUT (PR_TicksPerSecond () * 1 * 60)

typedef struct _CamelTcpStreamRawPrivate {
	PRFileDesc *sockfd;
} CamelTcpStreamRawPrivate;

G_DEFINE_TYPE (CamelTcpStreamRaw, camel_tcp_stream_raw, CAMEL_TYPE_TCP_STREAM)

#ifdef SIMULATE_FLAKY_NETWORK
static gssize
flaky_tcp_write (gint fd,
                 const gchar *buffer,
                 gsize buflen)
{
	gsize len = buflen;
	gssize nwritten;
	gint val;

	if (buflen == 0)
		return 0;

	val = 1 + (gint) (10.0 * rand () / (RAND_MAX + 1.0));

	switch (val) {
	case 1:
		printf ("flaky_tcp_write (%d, ..., %d): (-1) EINTR\n", fd, buflen);
		errno = EINTR;
		return -1;
	case 2:
		printf ("flaky_tcp_write (%d, ..., %d): (-1) EAGAIN\n", fd, buflen);
		errno = EAGAIN;
		return -1;
	case 3:
		printf ("flaky_tcp_write (%d, ..., %d): (-1) EWOULDBLOCK\n", fd, buflen);
		errno = EWOULDBLOCK;
		return -1;
	case 4:
	case 5:
	case 6:
		len = 1 + (gsize) (buflen * rand () / (RAND_MAX + 1.0));
		len = MIN (len, buflen);
		/* fall through... */
	default:
		printf ("flaky_tcp_write (%d, ..., %d): (%d) '%.*s'", fd, buflen, len, (gint) len, buffer);
		nwritten = write (fd, buffer, len);
		if (nwritten < 0)
			printf (" errno => %s\n", g_strerror (errno));
		else if (nwritten < len)
			printf (" only wrote %d bytes\n", nwritten);
		else
			printf ("\n");

		return nwritten;
	}
}

#define write(fd, buffer, buflen) flaky_tcp_write (fd, buffer, buflen)

static gssize
flaky_tcp_read (gint fd,
                gchar *buffer,
                gsize buflen)
{
	gsize len = buflen;
	gssize nread;
	gint val;

	if (buflen == 0)
		return 0;

	val = 1 + (gint) (10.0 * rand () / (RAND_MAX + 1.0));

	switch (val) {
	case 1:
		printf ("flaky_tcp_read (%d, ..., %d): (-1) EINTR\n", fd, buflen);
		errno = EINTR;
		return -1;
	case 2:
		printf ("flaky_tcp_read (%d, ..., %d): (-1) EAGAIN\n", fd, buflen);
		errno = EAGAIN;
		return -1;
	case 3:
		printf ("flaky_tcp_read (%d, ..., %d): (-1) EWOULDBLOCK\n", fd, buflen);
		errno = EWOULDBLOCK;
		return -1;
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
	case 10:
		len = 1 + (gsize) (10.0 * rand () / (RAND_MAX + 1.0));
		len = MIN (len, buflen);
		/* fall through... */
	default:
		printf ("flaky_tcp_read (%d, ..., %d): (%d)", fd, buflen, len);
		nread = read (fd, buffer, len);
		if (nread < 0)
			printf (" errno => %s\n", g_strerror (errno));
		else if (nread < len)
			printf (" only read %d bytes\n", nread);
		else
			printf ("\n");

		return nread;
	}
}

#define read(fd, buffer, buflen) flaky_tcp_read (fd, buffer, buflen)

#endif /* SIMULATE_FLAKY_NETWORK */

static void
tcp_stream_cancelled (GCancellable *cancellable,
                      PRThread *thread)
{
	PR_Interrupt (thread);
}

static void
tcp_stream_raw_finalize (GObject *object)
{
	CamelTcpStreamRaw *stream = CAMEL_TCP_STREAM_RAW (object);
	CamelTcpStreamRawPrivate *priv = stream->priv;

	if (priv->sockfd != NULL) {
		PR_Shutdown (priv->sockfd, PR_SHUTDOWN_BOTH);
		PR_Close (priv->sockfd);
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_tcp_stream_raw_parent_class)->finalize (object);
}

void
_set_errno_from_pr_error (gint pr_code)
{
	/* FIXME: this should handle more. */
	switch (pr_code) {
	case PR_INVALID_ARGUMENT_ERROR:
		errno = EINVAL;
		break;
	case PR_PENDING_INTERRUPT_ERROR:
		errno = EINTR;
		break;
	case PR_IO_PENDING_ERROR:
		errno = EAGAIN;
		break;
#ifdef EWOULDBLOCK
	case PR_WOULD_BLOCK_ERROR:
		errno = EWOULDBLOCK;
		break;
#endif
#ifdef EINPROGRESS
	case PR_IN_PROGRESS_ERROR:
		errno = EINPROGRESS;
		break;
#endif
#ifdef EALREADY
	case PR_ALREADY_INITIATED_ERROR:
		errno = EALREADY;
		break;
#endif
#ifdef EHOSTUNREACH
	case PR_NETWORK_UNREACHABLE_ERROR:
		errno = EHOSTUNREACH;
		break;
#endif
#ifdef ECONNREFUSED
	case PR_CONNECT_REFUSED_ERROR:
		errno = ECONNREFUSED;
		break;
#endif
#ifdef ETIMEDOUT
	case PR_CONNECT_TIMEOUT_ERROR:
	case PR_IO_TIMEOUT_ERROR:
		errno = ETIMEDOUT;
		break;
#endif
#ifdef ENOTCONN
	case PR_NOT_CONNECTED_ERROR:
		errno = ENOTCONN;
		break;
#endif
#ifdef ECONNRESET
	case PR_CONNECT_RESET_ERROR:
		errno = ECONNRESET;
		break;
#endif
	case PR_IO_ERROR:
	default:
		errno = EIO;
		break;
	}
}

void
_set_g_error_from_errno (GError **error,
                         gboolean eintr_means_cancelled)
{
	gint errn = errno;

	if (error)
		g_clear_error (error);

	/* This is stolen from camel_read() / camel_write() */
	if (eintr_means_cancelled && errn == EINTR)
		g_set_error (
			error, G_IO_ERROR,
			G_IO_ERROR_CANCELLED,
			_("Cancelled"));
	else
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errn),
			"%s", g_strerror (errn));
}

void
_set_error_from_pr_error (GError **error)
{
	gchar *error_message = NULL;
	PRInt32 length;

	length = PR_GetErrorTextLength ();
	if (length > 0) {
		error_message = g_malloc0 (length + 1);
		PR_GetErrorText (error_message);
	} else {
		const gchar *str = PR_ErrorToString (PR_GetError (), PR_LANGUAGE_I_DEFAULT);
		if (!str || !*str)
			str = PR_ErrorToName (PR_GetError ());

		if (str && *str)
			error_message = g_strdup (str);
		else
			g_warning (
				"NSPR error code %d has no text",
				PR_GetError ());
	}

	_set_errno_from_pr_error (PR_GetError ());

	if (error_message != NULL)
		g_set_error_literal (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			error_message);
	else
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("NSPR error code %d"),
			PR_GetError ());

	g_free (error_message);
}

static gssize
read_from_prfd (PRFileDesc *fd,
                gchar *buffer,
                gsize n,
                GCancellable *cancellable,
                GError **error)
{
	gssize bytes_read;
	gulong cancel_id = 0;

	if (G_IS_CANCELLABLE (cancellable))
		cancel_id = g_cancellable_connect (
			cancellable, G_CALLBACK (tcp_stream_cancelled),
			PR_GetCurrentThread (), (GDestroyNotify) NULL);

	do {
		bytes_read = PR_Recv (fd, buffer, n, 0, IO_TIMEOUT);
	} while (bytes_read == -1 && PR_GetError () == PR_IO_PENDING_ERROR);

	if (cancel_id > 0)
		g_cancellable_disconnect (cancellable, cancel_id);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return -1;

	if (bytes_read == -1) {
		_set_error_from_pr_error (error);
		return -1;
	}

	return bytes_read;
}

static gssize
tcp_stream_raw_read (CamelStream *stream,
                     gchar *buffer,
                     gsize n,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	return read_from_prfd (priv->sockfd, buffer, n, cancellable, error);
}

static gssize
write_to_prfd (PRFileDesc *fd,
               const gchar *buffer,
               gsize size,
               GCancellable *cancellable,
               GError **error)
{
	gssize bytes_written;
	gssize total_written = 0;
	gulong cancel_id = 0;

	if (G_IS_CANCELLABLE (cancellable))
		cancel_id = g_cancellable_connect (
			cancellable, G_CALLBACK (tcp_stream_cancelled),
			PR_GetCurrentThread (), (GDestroyNotify) NULL);

	do {
		do {
			bytes_written = PR_Send (
				fd, buffer + total_written,
				size - total_written, 0, IO_TIMEOUT);
		} while (bytes_written == -1 && PR_GetError () == PR_IO_PENDING_ERROR);

		if (bytes_written > 0)
			total_written += bytes_written;
	} while (bytes_written != -1 && total_written < size);

	if (cancel_id > 0)
		g_cancellable_disconnect (cancellable, cancel_id);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return -1;

	if (bytes_written == -1) {
		_set_error_from_pr_error (error);
		return -1;
	}

	return total_written;
}

static gssize
tcp_stream_raw_write (CamelStream *stream,
                      const gchar *buffer,
                      gsize n,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	return write_to_prfd (priv->sockfd, buffer, n, cancellable, error);
}

static gint
tcp_stream_raw_flush (CamelStream *stream,
                      GCancellable *cancellable,
                      GError **error)
{
#if 0
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	return PR_Sync (priv->sockfd);
#endif
	return 0;
}

static gint
tcp_stream_raw_close (CamelStream *stream,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	if (priv->sockfd == NULL)
		errno = EINVAL;
	else {
		gboolean err;

		PR_Shutdown (priv->sockfd, PR_SHUTDOWN_BOTH);

		err = (PR_Close (priv->sockfd) == PR_FAILURE);
		priv->sockfd = NULL;

		if (err)
			_set_errno_from_pr_error (PR_GetError ());
		else
			return 0;
	}

	_set_g_error_from_errno (error, FALSE);
	return -1;
}

static gint
sockaddr_to_praddr (struct sockaddr *s,
                    gint len,
                    PRNetAddr *addr)
{
	/* We assume the ip addresses are the same size - they have to be anyway.
	 * We could probably just use memcpy *shrug* */

	memset (addr, 0, sizeof (*addr));

	if (s->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *) s;

		if (len < sizeof (*sin))
			return -1;

		addr->inet.family = PR_AF_INET;
		addr->inet.port = sin->sin_port;
		memcpy (&addr->inet.ip, &sin->sin_addr, sizeof (addr->inet.ip));

		return 0;
	}
#ifdef ENABLE_IPv6
	else if (s->sa_family == PR_AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *) s;

		if (len < sizeof (*sin))
			return -1;

		addr->ipv6.family = PR_AF_INET6;
		addr->ipv6.port = sin->sin6_port;
		addr->ipv6.flowinfo = sin->sin6_flowinfo;
		memcpy (&addr->ipv6.ip, &sin->sin6_addr, sizeof (addr->ipv6.ip));
		addr->ipv6.scope_id = sin->sin6_scope_id;

		return 0;
	}
#endif

	return -1;
}

static PRFileDesc *
socket_connect (struct addrinfo *host,
                GCancellable *cancellable,
                GError **error)
{
	PRNetAddr netaddr;
	PRFileDesc *fd;
	PRStatus status;
	gulong cancel_id = 0;

	if (sockaddr_to_praddr (host->ai_addr, host->ai_addrlen, &netaddr) != 0) {
		errno = EINVAL;
		_set_g_error_from_errno (error, FALSE);
		return NULL;
	}

	fd = PR_OpenTCPSocket (netaddr.raw.family);
	if (fd == NULL) {
		_set_errno_from_pr_error (PR_GetError ());
		_set_g_error_from_errno (error, FALSE);
		return NULL;
	}

	if (G_IS_CANCELLABLE (cancellable))
		cancel_id = g_cancellable_connect (
			cancellable, G_CALLBACK (tcp_stream_cancelled),
			PR_GetCurrentThread (), (GDestroyNotify) NULL);

	status = PR_Connect (fd, &netaddr, CONNECT_TIMEOUT);

	if (cancel_id > 0)
		g_cancellable_disconnect (cancellable, cancel_id);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		goto fail;

	if (status == PR_FAILURE) {
		_set_error_from_pr_error (error);
		goto fail;
	}

	return fd;

fail:
	PR_Shutdown (fd, PR_SHUTDOWN_BOTH);
	PR_Close (fd);

	return NULL;
}

/* Just opens a TCP socket to a (presumed) SOCKS proxy.  Does not actually
 * negotiate anything with the proxy; this is just to create the socket and connect.
 */
static PRFileDesc *
connect_to_proxy (CamelTcpStreamRaw *raw,
                  const gchar *proxy_host,
                  gint proxy_port,
                  GCancellable *cancellable,
                  GError **error)
{
	struct addrinfo *addr, *ai, hints;
	gchar serv[16];
	PRFileDesc *fd;
	gint save_errno;

	g_assert (proxy_host != NULL);

	d (g_print ("TcpStreamRaw %p: connecting to proxy %s:%d {\n  resolving proxy host\n", raw, proxy_host, proxy_port));

	sprintf (serv, "%d", proxy_port);

	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;

	addr = camel_getaddrinfo (
		proxy_host, serv, &hints, cancellable, error);
	if (!addr)
		return NULL;

	d (g_print ("  creating socket and connecting\n"));

	ai = addr;
	while (ai) {
		fd = socket_connect (ai, cancellable, error);
		if (fd)
			goto out;

		ai = ai->ai_next;
	}

out:
	save_errno = errno;

	camel_freeaddrinfo (addr);

	if (!fd) {
		errno = save_errno;
		d (g_print ("  could not connect: errno %d\n", errno));
	}

	return fd;
}

/* Returns the FD of a socket, already connected to and validated by the SOCKS4
 * proxy that is configured in the stream.  Otherwise returns NULL.  Assumes that
 * a proxy *is* configured with camel_tcp_stream_set_socks_proxy().  Only tries the first
 * connect_addr; if you want to traverse all the addrinfos, call this function for each of them.
 */
static PRFileDesc *
connect_to_socks4_proxy (CamelTcpStreamRaw *raw,
                         const gchar *proxy_host,
                         gint proxy_port,
                         struct addrinfo *connect_addr,
                         GCancellable *cancellable,
                         GError **error)
{
	PRFileDesc *fd;
	gchar request[9];
	struct sockaddr_in *sin;
	gchar reply[8]; /* note that replies are 8 bytes, even if only the first 2 are used */
	gint save_errno;

	g_assert (connect_addr->ai_addr->sa_family == AF_INET);

	fd = connect_to_proxy (
		raw, proxy_host, proxy_port, cancellable, error);
	if (!fd)
		goto error;

	sin = (struct sockaddr_in *) connect_addr->ai_addr;

	request[0] = 0x04;				/* SOCKS4 */
	request[1] = 0x01;				/* CONNECT */
	memcpy (request + 2, &sin->sin_port, 2);	/* port in network byte order */
	memcpy (request + 4, &sin->sin_addr.s_addr, 4);	/* address in network byte order */
	request[8] = 0x00;				/* terminator */

	d (g_print ("  writing SOCKS4 request to connect to actual host\n"));
	if (write_to_prfd (fd, request, sizeof (request), cancellable, error) != sizeof (request)) {
		d (g_print ("  failed: %d\n", errno));
		goto error;
	}

	d (g_print ("  reading SOCKS4 reply\n"));
	if (read_from_prfd (fd, reply, sizeof (reply), cancellable, error) != sizeof (reply)) {
		d (g_print ("  failed: %d\n", errno));
		g_set_error (
			error, CAMEL_PROXY_ERROR,
			CAMEL_PROXY_ERROR_PROXY_NOT_SUPPORTED,
			_("The proxy host does not support SOCKS4"));
		goto error;
	}

	if (reply[0] != 0) { /* version of reply code is 0 */
#ifdef G_OS_WIN32
		errno = WSAECONNREFUSED;
#else
		errno = ECONNREFUSED;
#endif
		g_set_error (
			error, CAMEL_PROXY_ERROR,
			CAMEL_PROXY_ERROR_PROXY_NOT_SUPPORTED,
			_("The proxy host does not support SOCKS4"));
		goto error;
	}

	if (reply[1] != 90) {	/* 90 means "request granted" */
#ifdef G_OS_WIN32
		errno = WSAECONNREFUSED;
#else
		errno = ECONNREFUSED;
#endif
		g_set_error (
			error, CAMEL_PROXY_ERROR,
			CAMEL_PROXY_ERROR_CANT_AUTHENTICATE,
			_("The proxy host denied our request: code %d"),
			reply[1]);
		goto error;
	}

	/* We are now proxied; we are ready to send "normal" data through the socket */

	d (g_print ("  success\n"));

	goto out;

error:
	if (fd) {
		save_errno = errno;
		PR_Shutdown (fd, PR_SHUTDOWN_BOTH);
		PR_Close (fd);
		errno = save_errno;
		fd = NULL;
	}

	d (g_print ("  returning errno %d\n", errno));

out:

	return fd;
}

/* Resolves a port number using getaddrinfo().  Returns 0 if the port can't be resolved or if the operation is cancelled */
static gint
resolve_port (const gchar *service,
              gint fallback_port,
              GCancellable *cancellable,
              GError **error)
{
	struct addrinfo *ai;
	GError *my_error;
	gint port;

	port = 0;

	my_error = NULL;
	/* FIXME: camel_getaddrinfo() does not take NULL hostnames.  This is different
	 * from the standard getaddrinfo(), which lets you pass a NULL hostname
	 * if you just want to resolve a port number.
	 */
	ai = camel_getaddrinfo (
		"localhost", service, NULL, cancellable, &my_error);
	if (ai == NULL && fallback_port != 0 && !g_error_matches (my_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		port = fallback_port;
	else if (ai == NULL) {
		g_propagate_error (error, my_error);
	} else if (ai) {
		if (ai->ai_family == AF_INET) {
			port = ((struct sockaddr_in *) ai->ai_addr)->sin_port;
		}
#ifdef ENABLE_IPv6
		else if (ai->ai_family == AF_INET6) {
			port = ((struct sockaddr_in6 *) ai->ai_addr)->sin6_port;
		}
#endif
		else {
			g_assert_not_reached ();
		}

		camel_freeaddrinfo (ai);

		port = g_ntohs (port);
	}

	return port;
}

static gboolean
socks5_initiate_and_request_authentication (CamelTcpStreamRaw *raw,
                                            PRFileDesc *fd,
                                            GCancellable *cancellable,
                                            GError **error)
{
	gchar request[3];
	gchar reply[2];

	request[0] = 0x05;	/* SOCKS5 */
	request[1] = 1;		/* Number of authentication methods.  We just support "unauthenticated" for now. */
	request[2] = 0;		/* no authentication, please - extending this is left as an exercise for the reader */

	d (g_print ("  writing SOCKS5 request for authentication\n"));
	if (write_to_prfd (fd, request, sizeof (request), cancellable, error) != sizeof (request)) {
		d (g_print ("  failed: %d\n", errno));
		return FALSE;
	}

	d (g_print ("  reading SOCKS5 reply\n"));
	if (read_from_prfd (fd, reply, sizeof (reply), cancellable, error) != sizeof (reply)) {
		d (g_print ("  failed: %d\n", errno));
		g_clear_error (error);
		g_set_error (
			error, CAMEL_PROXY_ERROR,
			CAMEL_PROXY_ERROR_PROXY_NOT_SUPPORTED,
			_("The proxy host does not support SOCKS5"));
		return FALSE;
	}

	if (reply[0] != 5) {		/* server supports SOCKS5 */
		g_set_error (
			error, CAMEL_PROXY_ERROR,
			CAMEL_PROXY_ERROR_PROXY_NOT_SUPPORTED,
			_("The proxy host does not support SOCKS5"));
		return FALSE;
	}

	if (reply[1] != 0) {		/* and it grants us no authentication (see request[2]) */
		g_set_error (
			error, CAMEL_PROXY_ERROR,
			CAMEL_PROXY_ERROR_CANT_AUTHENTICATE,
			_("Could not find a suitable authentication type: code 0x%x"),
			reply[1]);
		return FALSE;
	}

	return TRUE;
}

static const gchar *
socks5_reply_error_to_string (gchar error_code)
{
	switch (error_code) {
	case 0x01: return _("General SOCKS server failure");
	case 0x02: return _("SOCKS server's rules do not allow connection");
	case 0x03: return _("Network is unreachable from SOCKS server");
	case 0x04: return _("Host is unreachable from SOCKS server");
	case 0x05: return _("Connection refused");
	case 0x06: return _("Time-to-live expired");
	case 0x07: return _("Command not supported by SOCKS server");
	case 0x08: return _("Address type not supported by SOCKS server");
	default: return _("Unknown error from SOCKS server");
	}
}

static gboolean
socks5_consume_reply_address (CamelTcpStreamRaw *raw,
                              PRFileDesc *fd,
                              GCancellable *cancellable,
                              GError **error)
{
	gchar address_type;
	gint bytes_to_consume;
	gchar *address_and_port;

	address_and_port = NULL;

	if (read_from_prfd (fd, &address_type, sizeof (address_type), cancellable, error) != sizeof (address_type))
		goto incomplete_reply;

	if (address_type == 0x01)
		bytes_to_consume = 4; /* IPv4 address */
	else if (address_type == 0x04)
		bytes_to_consume = 16; /* IPv6 address */
	else if (address_type == 0x03) {
		guchar address_len;

		/* we'll get an octet with the address length, and then the address itself */

		if (read_from_prfd (fd, (gchar *) &address_len, sizeof (address_len), cancellable, error) != sizeof (address_len))
			goto incomplete_reply;

		bytes_to_consume = address_len;
	} else {
		g_set_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_NOT_CONNECTED, _("Got unknown address type from SOCKS server"));
		return FALSE;
	}

	bytes_to_consume += 2; /* 2 octets for port number */
	address_and_port = g_new (gchar, bytes_to_consume);

	if (read_from_prfd (fd, address_and_port, bytes_to_consume, cancellable, error) != bytes_to_consume)
		goto incomplete_reply;

	g_free (address_and_port); /* Currently we don't do anything to these; maybe some authenticated method will need them later */

	return TRUE;

incomplete_reply:
	g_free (address_and_port);

	g_clear_error (error);
	g_set_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_NOT_CONNECTED, _("Incomplete reply from SOCKS server"));
	return FALSE;
}

static gboolean
socks5_request_connect (CamelTcpStreamRaw *raw,
                        PRFileDesc *fd,
                        const gchar *host,
                        gint port,
                        GCancellable *cancellable,
                        GError **error)
{
	gchar *request;
	gchar reply[3];
	gint host_len;
	gint request_len;
	gint num_written;

	host_len = strlen (host);
	if (host_len > 255) {
		g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, _("Hostname is too long (maximum is 255 characters)"));
		return FALSE;
	}

	request_len = 4 + 1 + host_len + 2; /* Request header + octect for host_len + host + 2 octets for port */
	request = g_new (gchar, request_len);

	request[0] = 0x05;	/* Version - SOCKS5 */
	request[1] = 0x01;	/* Command - CONNECT */
	request[2] = 0x00;	/* Reserved */
	request[3] = 0x03;	/* ATYP - address type - DOMAINNAME */
	request[4] = host_len;
	memcpy (request + 5, host, host_len);
	request[5 + host_len] = (port & 0xff00) >> 8; /* high byte of port */
	request[5 + host_len + 1] = port & 0xff;      /* low byte of port */

	d (g_print ("  writing SOCKS5 request for connection\n"));
	num_written = write_to_prfd (fd, request, request_len, cancellable, error);
	g_free (request);

	if (num_written != request_len) {
		d (g_print ("  failed: %d\n", errno));
		return FALSE;
	}

	d (g_print ("  reading SOCKS5 reply\n"));
	if (read_from_prfd (fd, reply, sizeof (reply), cancellable, error) != sizeof (reply)) {
		d (g_print ("  failed: %d\n", errno));
		return FALSE;
	}

	if (reply[0] != 0x05) {	/* SOCKS5 */
		g_set_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_NOT_CONNECTED, _("Invalid reply from proxy server"));
		return FALSE;
	}

	if (reply[1] != 0x00) {	/* error code */
		g_set_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_NOT_CONNECTED, "%s", socks5_reply_error_to_string (reply[1]));
		return FALSE;
	}

	if (reply[2] != 0x00) { /* reserved - must be 0 */
		g_set_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_NOT_CONNECTED, _("Invalid reply from proxy server"));
		return FALSE;
	}

	/* The rest of the reply is the address that the SOCKS server uses to
	 * identify to the final host.  This is of variable length, so we must
	 * consume it by hand.
	 */
	if (!socks5_consume_reply_address (raw, fd, cancellable, error))
		return FALSE;

	return TRUE;
}

/* RFC 1928 - SOCKS protocol version 5 */
static PRFileDesc *
connect_to_socks5_proxy (CamelTcpStreamRaw *raw,
                         const gchar *proxy_host,
                         gint proxy_port,
                         const gchar *host,
                         const gchar *service,
                         gint fallback_port,
                         GCancellable *cancellable,
                         GError **error)
{
	PRFileDesc *fd;
	gint port;

	fd = connect_to_proxy (
		raw, proxy_host, proxy_port, cancellable, error);
	if (!fd)
		goto error;

	port = resolve_port (service, fallback_port, cancellable, error);
	if (port == 0)
		goto error;

	if (!socks5_initiate_and_request_authentication (raw, fd, cancellable, error))
		goto error;

	if (!socks5_request_connect (raw, fd, host, port, cancellable, error))
		goto error;

	d (g_print ("  success\n"));

	goto out;

error:
	if (fd) {
		gint save_errno;

		save_errno = errno;
		PR_Shutdown (fd, PR_SHUTDOWN_BOTH);
		PR_Close (fd);
		errno = save_errno;
		fd = NULL;
	}

	d (g_print ("  returning errno %d\n", errno));

out:

	d (g_print ("}\n"));

	return fd;

}

static gint
tcp_stream_raw_connect (CamelTcpStream *stream,
                        const gchar *host,
                        const gchar *service,
                        gint fallback_port,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	struct addrinfo *addr, *ai;
	struct addrinfo hints;
	GError *my_error;
	gint retval;
	const gchar *proxy_host;
	gint proxy_port;

	camel_tcp_stream_peek_socks_proxy (stream, &proxy_host, &proxy_port);

	if (proxy_host) {
		/* First, try SOCKS5, which does name resolution itself */

		my_error = NULL;
		priv->sockfd = connect_to_socks5_proxy (
			raw, proxy_host, proxy_port, host, service,
			fallback_port, cancellable, &my_error);
		if (priv->sockfd)
			return 0;
		else if (g_error_matches (my_error, CAMEL_PROXY_ERROR, CAMEL_PROXY_ERROR_CANT_AUTHENTICATE)
			 || !g_error_matches (my_error, CAMEL_PROXY_ERROR, CAMEL_PROXY_ERROR_PROXY_NOT_SUPPORTED)) {
			g_propagate_error (error, my_error);
			return -1;
		}
	}

	/* Second, do name resolution ourselves and try SOCKS4 or a normal connection */

	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = PF_UNSPEC;

	my_error = NULL;
	addr = camel_getaddrinfo (
		host, service, &hints, cancellable, &my_error);
	if (addr == NULL && fallback_port != 0 && !g_error_matches (my_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		gchar str_port[16];

		g_clear_error (&my_error);
		sprintf (str_port, "%d", fallback_port);
		addr = camel_getaddrinfo (
			host, str_port, &hints, cancellable, &my_error);
	}

	if (addr == NULL) {
		g_propagate_error (error, my_error);
		return -1;
	}

	ai = addr;

	while (ai) {
		if (proxy_host) {
			/* SOCKS4 only does IPv4 */
			if (ai->ai_addr->sa_family == AF_INET)
				priv->sockfd = connect_to_socks4_proxy (
					raw, proxy_host, proxy_port,
					ai, cancellable, error);
		} else
			priv->sockfd = socket_connect (ai, cancellable, error);

		if (priv->sockfd) {
			retval = 0;
			goto out;
		}

		if (ai->ai_next != NULL)
			g_clear_error (error); /* Only preserve the error from the last try, in case no tries are successful */

		ai = ai->ai_next;
	}

	retval = -1;

out:

	camel_freeaddrinfo (addr);

	return retval;
}

static gint
tcp_stream_raw_getsockopt (CamelTcpStream *stream,
                           CamelSockOptData *data)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	PRSocketOptionData sodata;

	memset ((gpointer) &sodata, 0, sizeof (sodata));
	memcpy ((gpointer) &sodata, (gpointer) data, sizeof (CamelSockOptData));

	if (PR_GetSocketOption (priv->sockfd, &sodata) == PR_FAILURE)
		return -1;

	memcpy ((gpointer) data, (gpointer) &sodata, sizeof (CamelSockOptData));

	return 0;
}

static gint
tcp_stream_raw_setsockopt (CamelTcpStream *stream,
                           const CamelSockOptData *data)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	PRSocketOptionData sodata;

	memset ((gpointer) &sodata, 0, sizeof (sodata));
	memcpy ((gpointer) &sodata, (gpointer) data, sizeof (CamelSockOptData));

	if (PR_SetSocketOption (priv->sockfd, &sodata) == PR_FAILURE)
		return -1;

	return 0;
}

static struct sockaddr *
sockaddr_from_praddr (PRNetAddr *addr,
                      socklen_t *len)
{
	/* We assume the ip addresses are the same size - they have to be anyway */

	if (addr->raw.family == PR_AF_INET) {
		struct sockaddr_in *sin = g_malloc0 (sizeof (*sin));

		sin->sin_family = AF_INET;
		sin->sin_port = addr->inet.port;
		memcpy (&sin->sin_addr, &addr->inet.ip, sizeof (sin->sin_addr));
		*len = sizeof(*sin);

		return (struct sockaddr *) sin;
	}
#ifdef ENABLE_IPv6
	else if (addr->raw.family == PR_AF_INET6) {
		struct sockaddr_in6 *sin = g_malloc0 (sizeof (*sin));

		sin->sin6_family = AF_INET6;
		sin->sin6_port = addr->ipv6.port;
		sin->sin6_flowinfo = addr->ipv6.flowinfo;
		memcpy (&sin->sin6_addr, &addr->ipv6.ip, sizeof (sin->sin6_addr));
		sin->sin6_scope_id = addr->ipv6.scope_id;
		*len = sizeof(*sin);

		return (struct sockaddr *) sin;
	}
#endif

	return NULL;
}

static struct sockaddr *
tcp_stream_raw_get_local_address (CamelTcpStream *stream,
                                  socklen_t *len)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	PRNetAddr addr;

	if (PR_GetSockName (priv->sockfd, &addr) != PR_SUCCESS)
		return NULL;

	return sockaddr_from_praddr (&addr, len);
}

static struct sockaddr *
tcp_stream_raw_get_remote_address (CamelTcpStream *stream,
                                   socklen_t *len)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	PRNetAddr addr;

	if (PR_GetPeerName (priv->sockfd, &addr) != PR_SUCCESS)
		return NULL;

	return sockaddr_from_praddr (&addr, len);
}

static PRFileDesc *
tcp_stream_raw_get_file_desc (CamelTcpStream *stream)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	return priv->sockfd;
}

void
_camel_tcp_stream_raw_replace_file_desc (CamelTcpStreamRaw *raw,
                                         PRFileDesc *new_file_desc)
{
	CamelTcpStreamRawPrivate *priv = raw->priv;

	priv->sockfd = new_file_desc;
}

#define CAMEL_TCP_STREAM_RAW_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_TCP_STREAM_RAW, CamelTcpStreamRawPrivate))

static void
camel_tcp_stream_raw_class_init (CamelTcpStreamRawClass *class)
{
	GObjectClass *object_class;
	CamelStreamClass *stream_class;
	CamelTcpStreamClass *tcp_stream_class;

	g_type_class_add_private (class, sizeof (CamelTcpStreamRawPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = tcp_stream_raw_finalize;

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->read = tcp_stream_raw_read;
	stream_class->write = tcp_stream_raw_write;
	stream_class->flush = tcp_stream_raw_flush;
	stream_class->close = tcp_stream_raw_close;

	tcp_stream_class = CAMEL_TCP_STREAM_CLASS (class);
	tcp_stream_class->connect = tcp_stream_raw_connect;
	tcp_stream_class->getsockopt = tcp_stream_raw_getsockopt;
	tcp_stream_class->setsockopt = tcp_stream_raw_setsockopt;
	tcp_stream_class->get_local_address = tcp_stream_raw_get_local_address;
	tcp_stream_class->get_remote_address = tcp_stream_raw_get_remote_address;
	tcp_stream_class->get_file_desc = tcp_stream_raw_get_file_desc;
}

static void
camel_tcp_stream_raw_init (CamelTcpStreamRaw *stream)
{
	stream->priv = CAMEL_TCP_STREAM_RAW_GET_PRIVATE (stream);
}

GQuark
camel_proxy_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "camel-proxy-error-quark";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

/**
 * camel_tcp_stream_raw_new:
 *
 * Create a new #CamelTcpStreamRaw object.
 *
 * Returns: a new #CamelTcpStream object
 **/
CamelStream *
camel_tcp_stream_raw_new (void)
{
	return g_object_new (CAMEL_TYPE_TCP_STREAM_RAW, NULL);
}

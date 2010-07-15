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

#include "camel-file-utils.h"
#include "camel-net-utils.h"
#include "camel-operation.h"
#include "camel-tcp-stream-raw.h"

#define d(x)

#define IO_TIMEOUT (PR_TicksPerSecond() * 4 * 60)
#define CONNECT_TIMEOUT (PR_TicksPerSecond () * 4 * 60)

typedef struct _CamelTcpStreamRawPrivate {
	PRFileDesc *sockfd;
} CamelTcpStreamRawPrivate;

G_DEFINE_TYPE (CamelTcpStreamRaw, camel_tcp_stream_raw, CAMEL_TYPE_TCP_STREAM)

#ifdef SIMULATE_FLAKY_NETWORK
static gssize
flaky_tcp_write (gint fd, const gchar *buffer, gsize buflen)
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
flaky_tcp_read (gint fd, gchar *buffer, gsize buflen)
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

/* this is a 'cancellable' connect, cancellable from camel_operation_cancel etc */
/* returns -1 & errno == EINTR if the connection was cancelled */
static gint
socket_connect(struct addrinfo *h)
{
	struct timeval tv;
	socklen_t len;
	gint cancel_fd;
	gint errnosav;
	gint ret, fd;

	/* see if we're cancelled yet */
	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}

	if (h->ai_socktype != SOCK_STREAM) {
		errno = EINVAL;
		return -1;
	}

	if ((fd = socket (h->ai_family, SOCK_STREAM, 0)) == -1)
		return -1;

	cancel_fd = camel_operation_cancel_fd (NULL);
	if (cancel_fd == -1) {
		if (connect (fd, h->ai_addr, h->ai_addrlen) == -1) {
			/* Yeah, errno is meaningless on Win32 after a
			 * Winsock call fails, but I doubt the callers
			 * check errno anyway.
			 */
			errnosav = errno;
			SOCKET_CLOSE (fd);
			errno = errnosav;
			return -1;
		}

		return fd;
	} else {
#ifndef G_OS_WIN32
		gint flags;
#endif
		gint fdmax, status;
		fd_set rdset, wrset;

#ifndef G_OS_WIN32
		flags = fcntl (fd, F_GETFL);
		fcntl (fd, F_SETFL, flags | O_NONBLOCK);
#else
		{
			u_long yes = 1;
			ioctlsocket (fd, FIONBIO, &yes);
		}
#endif
		if (connect (fd, h->ai_addr, h->ai_addrlen) == 0) {
#ifndef G_OS_WIN32
			fcntl (fd, F_SETFL, flags);
#else
			{
				u_long no = 0;
				ioctlsocket (fd, FIONBIO, &no);
			}
#endif
			return fd;
		}

		if (!SOCKET_ERROR_IS_EINPROGRESS ()) {
			errnosav = errno;
			SOCKET_CLOSE (fd);
			errno = errnosav;
			return -1;
		}

		do {
			FD_ZERO (&rdset);
			FD_ZERO (&wrset);
			FD_SET (fd, &wrset);
			FD_SET (cancel_fd, &rdset);
			fdmax = MAX (fd, cancel_fd) + 1;
			tv.tv_sec = 60 * 4;
			tv.tv_usec = 0;

			status = select (fdmax, &rdset, &wrset, NULL, &tv);
		} while (status == -1 && SOCKET_ERROR_IS_EINTR ());

		if (status <= 0) {
			SOCKET_CLOSE (fd);
			errno = ETIMEDOUT;
			return -1;
		}

		if (cancel_fd != -1 && FD_ISSET (cancel_fd, &rdset)) {
			SOCKET_CLOSE (fd);
			errno = EINTR;
			return -1;
		} else {
			len = sizeof (gint);

			if (getsockopt (fd, SOL_SOCKET, SO_ERROR, (gchar *) &ret, &len) == -1) {
				errnosav = errno;
				SOCKET_CLOSE (fd);
				errno = errnosav;
				return -1;
			}

			if (ret != 0) {
				SOCKET_CLOSE (fd);
				errno = ret;
				return -1;
			}
		}
#ifndef G_OS_WIN32
		fcntl (fd, F_SETFL, flags);
#else
		{
			u_long no = 0;
			ioctlsocket (fd, FIONBIO, &no);
		}
#endif
	}

	return fd;
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

static void
set_g_error_from_errno (GError *error, gboolean eintr_means_cancelled)
{
	/* This is stolen from camel_read() / camel_write() */
	if (eintr_means_cancelled && errno == EINTR)
		g_set_error (
			error, G_IO_ERROR,
			G_IO_ERROR_CANCELLED,
			_("Cancelled"));
	else
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"%s", g_strerror (errno));
}

static gssize
read_from_prfd (PRFileDesc *fd, gchar *buffer, gsize n, GError **error)
{
	PRFileDesc *cancel_fd;
	gssize nread;

	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		set_g_error_from_errno (error, TRUE);
		return -1;
	}

	cancel_fd = camel_operation_cancel_prfd (NULL);
	if (cancel_fd == NULL) {
		do {
			nread = PR_Read (fd, buffer, n);
			if (nread == -1)
				_set_errno_from_pr_error (PR_GetError ());
		} while (nread == -1 && (PR_GetError () == PR_PENDING_INTERRUPT_ERROR ||
					 PR_GetError () == PR_IO_PENDING_ERROR ||
					 PR_GetError () == PR_WOULD_BLOCK_ERROR));
	} else {
		PRSocketOptionData sockopts;
		PRPollDesc pollfds[2];
		gboolean nonblock;
		gint error;

		/* get O_NONBLOCK options */
		sockopts.option = PR_SockOpt_Nonblocking;
		PR_GetSocketOption (fd, &sockopts);
		sockopts.option = PR_SockOpt_Nonblocking;
		nonblock = sockopts.value.non_blocking;
		sockopts.value.non_blocking = TRUE;
		PR_SetSocketOption (fd, &sockopts);

		pollfds[0].fd = fd;
		pollfds[0].in_flags = PR_POLL_READ;
		pollfds[1].fd = cancel_fd;
		pollfds[1].in_flags = PR_POLL_READ;

		do {
			PRInt32 res;

			pollfds[0].out_flags = 0;
			pollfds[1].out_flags = 0;
			nread = -1;

			res = PR_Poll(pollfds, 2, IO_TIMEOUT);
			if (res == -1)
				_set_errno_from_pr_error (PR_GetError());
			else if (res == 0) {
#ifdef ETIMEDOUT
				errno = ETIMEDOUT;
#else
				errno = EIO;
#endif
				goto failed;
			} else if (pollfds[1].out_flags == PR_POLL_READ) {
				errno = EINTR;
				goto failed;
			} else {
				do {
					nread = PR_Read (fd, buffer, n);
					if (nread == -1)
						_set_errno_from_pr_error (PR_GetError ());
				} while (nread == -1 && PR_GetError () == PR_PENDING_INTERRUPT_ERROR);
			}
		} while (nread == -1 && (PR_GetError () == PR_PENDING_INTERRUPT_ERROR ||
					 PR_GetError () == PR_IO_PENDING_ERROR ||
					 PR_GetError () == PR_WOULD_BLOCK_ERROR));

		/* restore O_NONBLOCK options */
	failed:
		error = errno;
		sockopts.option = PR_SockOpt_Nonblocking;
		sockopts.value.non_blocking = nonblock;
		PR_SetSocketOption (fd, &sockopts);
		errno = error;
	}

	if (nread == -1)
		set_g_error_from_errno (error, TRUE);

	return nread;
}

static gssize
tcp_stream_raw_read (CamelStream *stream,
                     gchar *buffer,
                     gsize n,
                     GError **error)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	return read_from_prfd (priv->sockfd, buffer, n, error);
}

static gssize
write_to_prfd (PRFileDesc *fd, const gchar *buffer, gsize n, GError **error)
{
	gssize w, written = 0;
	PRFileDesc *cancel_fd;

	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		set_g_error_from_errno (error, TRUE);
		return -1;
	}

	cancel_fd = camel_operation_cancel_prfd (NULL);
	if (cancel_fd == NULL) {
		do {
			do {
				w = PR_Write (fd, buffer + written, n - written);
				if (w == -1)
					_set_errno_from_pr_error (PR_GetError ());
			} while (w == -1 && (PR_GetError () == PR_PENDING_INTERRUPT_ERROR ||
					     PR_GetError () == PR_IO_PENDING_ERROR ||
					     PR_GetError () == PR_WOULD_BLOCK_ERROR));

			if (w > 0)
				written += w;
		} while (w != -1 && written < n);
	} else {
		PRSocketOptionData sockopts;
		PRPollDesc pollfds[2];
		gboolean nonblock;
		gint error;

		/* get O_NONBLOCK options */
		sockopts.option = PR_SockOpt_Nonblocking;
		PR_GetSocketOption (fd, &sockopts);
		sockopts.option = PR_SockOpt_Nonblocking;
		nonblock = sockopts.value.non_blocking;
		sockopts.value.non_blocking = TRUE;
		PR_SetSocketOption (fd, &sockopts);

		pollfds[0].fd = fd;
		pollfds[0].in_flags = PR_POLL_WRITE;
		pollfds[1].fd = cancel_fd;
		pollfds[1].in_flags = PR_POLL_READ;

		do {
			PRInt32 res;

			pollfds[0].out_flags = 0;
			pollfds[1].out_flags = 0;
			w = -1;

			res = PR_Poll (pollfds, 2, IO_TIMEOUT);
			if (res == -1) {
				_set_errno_from_pr_error (PR_GetError());
				if (PR_GetError () == PR_PENDING_INTERRUPT_ERROR)
					w = 0;
			} else if (res == 0) {
#ifdef ETIMEDOUT
				errno = ETIMEDOUT;
#else
				errno = EIO;
#endif
			} else if (pollfds[1].out_flags == PR_POLL_READ) {
				errno = EINTR;
			} else {
				do {
					w = PR_Write (fd, buffer + written, n - written);
					if (w == -1)
						_set_errno_from_pr_error (PR_GetError ());
				} while (w == -1 && PR_GetError () == PR_PENDING_INTERRUPT_ERROR);

				if (w == -1) {
					if (PR_GetError () == PR_IO_PENDING_ERROR ||
					    PR_GetError () == PR_WOULD_BLOCK_ERROR)
						w = 0;
				} else
					written += w;
			}
		} while (w != -1 && written < n);

		/* restore O_NONBLOCK options */
		error = errno;
		sockopts.option = PR_SockOpt_Nonblocking;
		sockopts.value.non_blocking = nonblock;
		PR_SetSocketOption (fd, &sockopts);
		errno = error;
	}

	if (w == -1)
		set_g_error_from_errno (error, TRUE);

	return written;
}

static gssize
tcp_stream_raw_write (CamelStream *stream,
                      const gchar *buffer,
                      gsize n,
                      GError **error)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	return write_to_prfd (priv->sockfd, buffer, n, error);
}

static gint
tcp_stream_raw_flush (CamelStream *stream,
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
			_set_errno_from_pr_error (PR_GetError());
		else
			return 0;
	}

	set_g_error_from_errno (error, FALSE);
	return -1;
}

static gint
sockaddr_to_praddr (struct sockaddr *s, gint len, PRNetAddr *addr)
{
	/* We assume the ip addresses are the same size - they have to be anyway.
	   We could probably just use memcpy *shrug* */

	memset(addr, 0, sizeof(*addr));

	if (s->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)s;

		if (len < sizeof(*sin))
			return -1;

		addr->inet.family = PR_AF_INET;
		addr->inet.port = sin->sin_port;
		memcpy(&addr->inet.ip, &sin->sin_addr, sizeof(addr->inet.ip));

		return 0;
	}
#ifdef ENABLE_IPv6
	else if (s->sa_family == PR_AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *)s;

		if (len < sizeof(*sin))
			return -1;

		addr->ipv6.family = PR_AF_INET6;
		addr->ipv6.port = sin->sin6_port;
		addr->ipv6.flowinfo = sin->sin6_flowinfo;
		memcpy(&addr->ipv6.ip, &sin->sin6_addr, sizeof(addr->ipv6.ip));
		addr->ipv6.scope_id = sin->sin6_scope_id;

		return 0;
	}
#endif

	return -1;
}

static PRFileDesc *
socket_connect (struct addrinfo *host, GError *error)
{
	PRNetAddr netaddr;
	PRFileDesc *fd, *cancel_fd;

	if (sockaddr_to_praddr(host->ai_addr, host->ai_addrlen, &netaddr) != 0) {
		errno = EINVAL;
		set_g_error_from_errno (error, FALSE);
		return NULL;
	}

	fd = PR_OpenTCPSocket(netaddr.raw.family);
	if (fd == NULL) {
		_set_errno_from_pr_error (PR_GetError ());
		set_g_error_from_errno (error, FALSE);
		return NULL;
	}

	cancel_fd = camel_operation_cancel_prfd(NULL);

	if (PR_Connect (fd, &netaddr, cancel_fd?0:CONNECT_TIMEOUT) == PR_FAILURE) {
		gint errnosave;

		_set_errno_from_pr_error (PR_GetError ());
		if (PR_GetError () == PR_IN_PROGRESS_ERROR ||
		    (cancel_fd && (PR_GetError () == PR_CONNECT_TIMEOUT_ERROR ||
				   PR_GetError () == PR_IO_TIMEOUT_ERROR))) {
			gboolean connected = FALSE;
			PRPollDesc poll[2];

			poll[0].fd = fd;
			poll[0].in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
			poll[1].fd = cancel_fd;
			poll[1].in_flags = PR_POLL_READ;

			do {
				poll[0].out_flags = 0;
				poll[1].out_flags = 0;

				if (PR_Poll (poll, cancel_fd?2:1, CONNECT_TIMEOUT) == PR_FAILURE) {
					_set_errno_from_pr_error (PR_GetError ());
					goto exception;
				}

				if (poll[1].out_flags == PR_POLL_READ) {
					errno = EINTR;
					goto exception;
				}

				if (PR_ConnectContinue(fd, poll[0].out_flags) == PR_FAILURE) {
					_set_errno_from_pr_error (PR_GetError ());
					if (PR_GetError () != PR_IN_PROGRESS_ERROR)
						goto exception;
				} else {
					connected = TRUE;
				}
			} while (!connected);
		} else {
		exception:
			errnosave = errno;
			PR_Shutdown (fd, PR_SHUTDOWN_BOTH);
			PR_Close (fd);
			errno = errnosave;
			fd = NULL;

			goto out;
		}

		errno = 0;
	}

out:

	if (!fd)
		set_g_error_from_errno (error, TRUE);

	return fd;
}

/* Returns the FD of a socket, already connected to and validated by the SOCKS4
 * proxy that is configured in the stream.  Otherwise returns NULL.  Assumes that
 * a proxy *is* configured with camel_tcp_stream_set_socks_proxy().
 */
static PRFileDesc *
connect_to_socks4_proxy (const gchar *proxy_host, gint proxy_port, struct addrinfo *connect_addr, GError **error)
{
	struct addrinfo *ai, hints;
	gchar serv[16];
	PRFileDesc *fd;
	gchar request[9];
	struct sockaddr_in *sin;
	gchar reply[8];
	gint save_errno;

	g_assert (proxy_host != NULL);

	d (g_print ("TcpStreamRaw %p: connecting to SOCKS4 proxy %s:%d {\n  resolving proxy host\n", ssl, proxy_host, proxy_port));

	sprintf (serv, "%d", proxy_port);

	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;

	ai = camel_getaddrinfo (proxy_host, serv, &hints, error);
	if (!ai)
		return NULL;

	d (g_print ("  creating socket and connecting\n"));

	fd = socket_connect (ai, error);
	save_errno = errno;

	camel_freeaddrinfo (ai);

	if (!fd) {
		errno = save_errno;
		goto error;
	}

	g_assert (connect_addr->ai_addr->sa_family == AF_INET); /* FMQ: check for AF_INET in the caller */
	sin = (struct sockaddr_in *) connect_addr->ai_addr;

	request[0] = 0x04;				/* SOCKS4 */
	request[1] = 0x01;				/* CONNECT */
	memcpy (request + 2, &sin->sin_port, 2);	/* port in network byte order */
	memcpy (request + 4, &sin->sin_addr.s_addr, 4);	/* address in network byte order */
	request[8] = 0x00;				/* terminator */

	d (g_print ("  writing SOCKS4 request to connect to actual host\n"));
	if (write_to_prfd (fd, request, sizeof (request), error) != sizeof (request)) {
		d (g_print ("  failed: %d\n", errno));
		goto error;
	}

	d (g_print ("  reading SOCKS4 reply\n"));
	if (read_from_prfd (fd, reply, sizeof (reply), error) != sizeof (reply)) {
		d (g_print ("  failed: %d\n", errno));
		goto error;
	}

	if (!(reply[0] == 0		/* first byte of reply is 0 */
	      && reply[1] == 90)) {	/* 90 means "request granted" */
#ifdef G_OS_WIN32
		errno = WSAECONNREFUSED;
#else
		errno = ECONNREFUSED;
#endif
		set_g_error_from_errno (error, FALSE);
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

static gint
tcp_stream_raw_connect (CamelTcpStream *stream,
			const char *host, const char *service, gint fallback_port,
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

	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = PF_UNSPEC;

	my_error = NULL;
	addr = camel_getaddrinfo (host, service, &hints, &my_error);
	if (addr == NULL && fallback_port != 0 && !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		char str_port[16];

		g_clear_error (&my_error);
		sprintf (str_port, "%d", fallback_port);
		addr = camel_getaddrinfo (host, str_port, &hints, &my_error);
	}

	if (addr == NULL) {
		g_propagate_error (error, my_error);
		return -1;
	}

	camel_tcp_stream_peek_socks_proxy (stream, &proxy_host, &proxy_port);

	ai = addr;

	while (ai) {
		if (proxy_host)
			priv->sockfd = connect_to_socks4_proxy (proxy_host, proxy_port, ai);
		else
			priv->sockfd = socket_connect (ai);

		if (priv->sockfd) {
			retval = 0;
			goto out;
		}

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
sockaddr_from_praddr(PRNetAddr *addr, socklen_t *len)
{
	/* We assume the ip addresses are the same size - they have to be anyway */

	if (addr->raw.family == PR_AF_INET) {
		struct sockaddr_in *sin = g_malloc0(sizeof(*sin));

		sin->sin_family = AF_INET;
		sin->sin_port = addr->inet.port;
		memcpy(&sin->sin_addr, &addr->inet.ip, sizeof(sin->sin_addr));
		*len = sizeof(*sin);

		return (struct sockaddr *)sin;
	}
#ifdef ENABLE_IPv6
	else if (addr->raw.family == PR_AF_INET6) {
		struct sockaddr_in6 *sin = g_malloc0(sizeof(*sin));

		sin->sin6_family = AF_INET6;
		sin->sin6_port = addr->ipv6.port;
		sin->sin6_flowinfo = addr->ipv6.flowinfo;
		memcpy(&sin->sin6_addr, &addr->ipv6.ip, sizeof(sin->sin6_addr));
		sin->sin6_scope_id = addr->ipv6.scope_id;
		*len = sizeof(*sin);

		return (struct sockaddr *)sin;
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

	if (PR_GetSockName(priv->sockfd, &addr) != PR_SUCCESS)
		return NULL;

	return sockaddr_from_praddr(&addr, len);
}

static struct sockaddr *
tcp_stream_raw_get_remote_address (CamelTcpStream *stream,
                                   socklen_t *len)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	PRNetAddr addr;

	if (PR_GetPeerName(priv->sockfd, &addr) != PR_SUCCESS)
		return NULL;

	return sockaddr_from_praddr(&addr, len);
}

static PRFileDesc *
tcp_stream_raw_get_file_desc (CamelTcpStream *stream)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	return priv->sockfd;
}

void
_camel_tcp_stream_raw_replace_file_desc (CamelTcpStreamRaw *raw, PRFileDesc *new_file_desc)
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
	tcp_stream_class->tcp_stream_raw_get_file_desc = tcp_stream_raw_get_file_desc;
}

static void
camel_tcp_stream_raw_init (CamelTcpStreamRaw *stream)
{
	CamelTcpStreamRawPrivate *priv;

	stream->priv = CAMEL_TCP_STREAM_RAW_GET_PRIVATE (stream);
	priv = stream->priv;

	priv->sockfd = NULL;
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

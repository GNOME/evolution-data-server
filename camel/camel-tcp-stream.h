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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_TCP_STREAM_H
#define CAMEL_TCP_STREAM_H

#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
typedef struct linger CamelLinger;
#else
typedef struct {
	gushort l_onoff;
	gushort l_linger;
} CamelLinger;
#define socklen_t int
struct addrinfo;
#endif
#include <unistd.h>

#include <prio.h>

#include <camel/camel-stream.h>

/* Standard GObject macros */
#define CAMEL_TYPE_TCP_STREAM \
	(camel_tcp_stream_get_type ())
#define CAMEL_TCP_STREAM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_TCP_STREAM, CamelTcpStream))
#define CAMEL_TCP_STREAM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_TCP_STREAM, CamelTcpStreamClass))
#define CAMEL_IS_TCP_STREAM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_TCP_STREAM))
#define CAMEL_IS_TCP_STREAM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_TCP_STREAM))
#define CAMEL_TCP_STREAM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_TCP_STREAM, CamelTcpStreamClass))

G_BEGIN_DECLS

typedef struct _CamelTcpStream CamelTcpStream;
typedef struct _CamelTcpStreamClass CamelTcpStreamClass;
typedef struct _CamelTcpStreamPrivate CamelTcpStreamPrivate;

typedef enum {
	CAMEL_SOCKOPT_NONBLOCKING,     /* nonblocking io */
	CAMEL_SOCKOPT_LINGER,          /* linger on close if data present */
	CAMEL_SOCKOPT_REUSEADDR,       /* allow local address reuse */
	CAMEL_SOCKOPT_KEEPALIVE,       /* keep connections alive */
	CAMEL_SOCKOPT_RECVBUFFERSIZE,  /* receive buffer size */
	CAMEL_SOCKOPT_SENDBUFFERSIZE,  /* send buffer size */

	CAMEL_SOCKOPT_IPTIMETOLIVE,    /* time to live */
	CAMEL_SOCKOPT_IPTYPEOFSERVICE, /* type of service and precedence */

	CAMEL_SOCKOPT_ADDMEMBER,       /* add an IP group membership */
	CAMEL_SOCKOPT_DROPMEMBER,      /* drop an IP group membership */
	CAMEL_SOCKOPT_MCASTINTERFACE,  /* multicast interface address */
	CAMEL_SOCKOPT_MCASTTIMETOLIVE, /* multicast timetolive */
	CAMEL_SOCKOPT_MCASTLOOPBACK,   /* multicast loopback */

	CAMEL_SOCKOPT_NODELAY,         /* don't delay send to coalesce packets */
	CAMEL_SOCKOPT_MAXSEGMENT,      /* maximum segment size */
	CAMEL_SOCKOPT_BROADCAST,       /* enable broadcast */
	CAMEL_SOCKOPT_LAST
} CamelSockOpt;

typedef struct _CamelSockOptData {
	CamelSockOpt option;
	union {
		guint       ip_ttl;              /* IP time to live */
		guint       mcast_ttl;           /* IP multicast time to live */
		guint       tos;                 /* IP type of service and precedence */
		gboolean    non_blocking;        /* Non-blocking (network) I/O */
		gboolean    reuse_addr;          /* Allow local address reuse */
		gboolean    keep_alive;          /* Keep connections alive */
		gboolean    mcast_loopback;      /* IP multicast loopback */
		gboolean    no_delay;            /* Don't delay send to coalesce packets */
		gboolean    broadcast;           /* Enable broadcast */
		gsize      max_segment;         /* Maximum segment size */
		gsize      recv_buffer_size;    /* Receive buffer size */
		gsize      send_buffer_size;    /* Send buffer size */
		CamelLinger linger;              /* Time to linger on close if data present */
	} value;
} CamelSockOptData;

struct _CamelTcpStream {
	CamelStream parent;

	CamelTcpStreamPrivate *priv;
};

struct _CamelTcpStreamClass {
	CamelStreamClass parent_class;

	gint		(*connect)		(CamelTcpStream *stream,
						 const gchar *host,
						 const gchar *service,
						 gint fallback_port,
						 GCancellable *cancellable,
						 GError **error);
	gint		(*getsockopt)		(CamelTcpStream *stream,
						 CamelSockOptData *data);
	gint		(*setsockopt)		(CamelTcpStream *stream,
						 const CamelSockOptData *data);
	struct sockaddr *
			(*get_local_address)	(CamelTcpStream *stream,
						 socklen_t *len);
	struct sockaddr *
			(*get_remote_address)	(CamelTcpStream *stream,
						 socklen_t *len);

	PRFileDesc *    (*get_file_desc)        (CamelTcpStream *stream);
};

GType		camel_tcp_stream_get_type	(void);
gint		camel_tcp_stream_connect	(CamelTcpStream *stream,
						 const gchar *host,
						 const gchar *service,
						 gint fallback_port,
						 GCancellable *cancellable,
						 GError **error);
gint		camel_tcp_stream_getsockopt	(CamelTcpStream *stream,
						 CamelSockOptData *data);
gint		camel_tcp_stream_setsockopt	(CamelTcpStream *stream,
						 const CamelSockOptData *data);
PRFileDesc  *   camel_tcp_stream_get_file_desc  (CamelTcpStream *stream);

/* Note about SOCKS proxies:
 *
 * As of 2010/Jun/02, Camel supports SOCKS4 proxies, but not SOCKS4a nor SOCKS5.
 * This comment leaves the implementation of those proxy types as an exercise
 * for the reader, with some hints:
 *
 * The way SOCKS proxies work right now is that clients of Camel call
 * camel_session_set_socks_proxy().  Later, users of TCP streams like
 * the POP/IMAP providers will look at the CamelSession's proxy, and
 * set it on the CamelTcpStream subclasses that they instantiate.
 *
 * Both SOCKS4a and SOCKS5 let you resolve hostnames on the proxy; while
 * SOCKS5 also has extra features like passing a username/password to the
 * proxy.  However, Camel's current API does not let us implement those
 * features.
 *
 * You use a CamelTCPStream by essentially doing this:
 *
 *   struct addrinfo *ai;
 *   CamelTcpStream *stream;
 *   gint result;
 *
 *   stream = camel_tcp_stream_{raw/ssl}_new (session, ...);
 *   camel_tcp_stream_set_socks_proxy (stream, ...);
 *
 *   ai = camel_getaddrinfo (host, port, ...);
 *   result = camel_tcp_stream_connect (stream, ai);
 *
 * Since you pass a struct addrinfo directly to camel_tcp_stream_connect(), this means
 * that the stream expects your hostname to be resolved already.  However, SOCKS4a/SOCKS5
 * proxies would rather resolve the hostname themselves.
 *
 * The solution would be to make camel_tcp_stream_connect() a higher-level API, more or
 * less like this:
 *
 *   gint camel_tcp_stream_connect (stream, host, port);
 *
 * Internally it would do camel_getaddrinfo() for the case without SOCKS proxies,
 * and otherwise have the proxy itself resolve the host.
 *
 * Fortunately, it seems that the only callers of CamelTcpStream are *inside* Camel;
 * Evolution doesn't use this API directly.  So all the changes required to
 * support SOCKS4a/SOCKS5 proxies should be well-contained within Camel,
 * with no extra changes required in Evolution.
 */
void		camel_tcp_stream_set_socks_proxy (CamelTcpStream *stream,
						 const gchar *socks_host,
						 gint socks_port);
void		camel_tcp_stream_peek_socks_proxy
						(CamelTcpStream *stream,
						 const gchar **socks_host_ret,
						 gint *socks_port_ret);

struct sockaddr *
		camel_tcp_stream_get_local_address
						(CamelTcpStream *stream,
						 socklen_t *len);
struct sockaddr *
		camel_tcp_stream_get_remote_address
						(CamelTcpStream *stream,
						 socklen_t *len);

G_END_DECLS

#ifdef G_OS_WIN32
#undef socklen_t
#endif

#endif /* CAMEL_TCP_STREAM_H */

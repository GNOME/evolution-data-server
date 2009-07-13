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

#ifndef CAMEL_TCP_STREAM_H
#define CAMEL_TCP_STREAM_H

#include <glib.h>

#ifndef G_OS_WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <unistd.h>

#include <camel/camel-stream.h>

#define CAMEL_TCP_STREAM_TYPE     (camel_tcp_stream_get_type ())
#define CAMEL_TCP_STREAM(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_TCP_STREAM_TYPE, CamelTcpStream))
#define CAMEL_TCP_STREAM_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_TCP_STREAM_TYPE, CamelTcpStreamClass))
#define CAMEL_IS_TCP_STREAM(o)    (CAMEL_CHECK_TYPE((o), CAMEL_TCP_STREAM_TYPE))

G_BEGIN_DECLS

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

typedef struct linger CamelLinger;

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
	CamelStream parent_object;

};

typedef struct {
	CamelStreamClass parent_class;

	/* Virtual methods */
	gint (*connect)    (CamelTcpStream *stream, struct addrinfo *host);
	gint (*getsockopt) (CamelTcpStream *stream, CamelSockOptData *data);
	gint (*setsockopt) (CamelTcpStream *stream, const CamelSockOptData *data);

	struct sockaddr * (*get_local_address)  (CamelTcpStream *stream, socklen_t *len);
	struct sockaddr * (*get_remote_address) (CamelTcpStream *stream, socklen_t *len);
} CamelTcpStreamClass;

/* Standard Camel function */
CamelType camel_tcp_stream_get_type (void);

/* public methods */
gint         camel_tcp_stream_connect    (CamelTcpStream *stream, struct addrinfo *host);
gint         camel_tcp_stream_getsockopt (CamelTcpStream *stream, CamelSockOptData *data);
gint         camel_tcp_stream_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data);

struct sockaddr *camel_tcp_stream_get_local_address  (CamelTcpStream *stream, socklen_t *len);
struct sockaddr *camel_tcp_stream_get_remote_address (CamelTcpStream *stream, socklen_t *len);

G_END_DECLS

#endif /* CAMEL_TCP_STREAM_H */

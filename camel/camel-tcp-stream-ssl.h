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

#ifndef CAMEL_TCP_STREAM_SSL_H
#define CAMEL_TCP_STREAM_SSL_H

#ifdef HAVE_SSL

#include <camel/camel-tcp-stream.h>
#include <prio.h>

/* Standard GObject macros */
#define CAMEL_TYPE_TCP_STREAM_SSL \
	(camel_tcp_stream_ssl_get_type ())
#define CAMEL_TCP_STREAM_SSL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_TCP_STREAM_SSL, CamelTcpStreamSSL))
#define CAMEL_TCP_STREAM_SSL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_TCP_STREAM_SSL, CamelTcpStreamSSLClass))
#define CAMEL_IS_TCP_STREAM_SSL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_TCP_STREAM_SSL))
#define CAMEL_IS_TCP_STREAM_SSL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_TCP_STREAM_SSL))
#define CAMEL_TCP_STREAM_SSL_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_TCP_STREAM_SSL, CamelTcpStreamSSLClass))

#define CAMEL_TCP_STREAM_SSL_ENABLE_SSL2   (1 << 0)
#define CAMEL_TCP_STREAM_SSL_ENABLE_SSL3   (1 << 1)
#define CAMEL_TCP_STREAM_SSL_ENABLE_TLS    (1 << 2)

G_BEGIN_DECLS

struct _CamelSession;

typedef struct _CamelTcpStreamSSL CamelTcpStreamSSL;
typedef struct _CamelTcpStreamSSLClass CamelTcpStreamSSLClass;
typedef struct _CamelTcpStreamSSLPrivate CamelTcpStreamSSLPrivate;

struct _CamelTcpStreamSSL {
	CamelTcpStream parent;
	CamelTcpStreamSSLPrivate *priv;
};

struct _CamelTcpStreamSSLClass {
	CamelTcpStreamClass parent_class;
};

GType camel_tcp_stream_ssl_get_type (void);

/* public methods */
CamelStream *camel_tcp_stream_ssl_new (struct _CamelSession *session, const gchar *expected_host, guint32 flags);

CamelStream *camel_tcp_stream_ssl_new_raw (struct _CamelSession *session, const gchar *expected_host, guint32 flags);

gint camel_tcp_stream_ssl_enable_ssl (CamelTcpStreamSSL *ssl);

PRFileDesc * camel_tcp_stream_ssl_sockfd (CamelTcpStreamSSL *stream);

G_END_DECLS

#endif /* HAVE_SSL */

#endif /* CAMEL_TCP_STREAM_SSL_H */

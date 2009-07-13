/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef __CAMEL_HTTP_STREAM_H__
#define __CAMEL_HTTP_STREAM_H__

#include <camel/camel-mime-parser.h>
#include <camel/camel-mime-utils.h>
#include <camel/camel-stream.h>
#include <camel/camel-url.h>

#define CAMEL_HTTP_STREAM_TYPE     (camel_http_stream_get_type ())
#define CAMEL_HTTP_STREAM(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_HTTP_STREAM_TYPE, CamelHttpStream))
#define CAMEL_HTTP_STREAM_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_HTTP_STREAM_TYPE, CamelHttpStreamClass))
#define CAMEL_IS_HTTP_STREAM(o)    (CAMEL_CHECK_TYPE((o), CAMEL_HTTP_STREAM_TYPE))

G_BEGIN_DECLS

typedef enum {
	/*CAMEL_HTTP_METHOD_OPTIONS,*/
	CAMEL_HTTP_METHOD_GET,
	CAMEL_HTTP_METHOD_HEAD
	/*CAMEL_HTTP_METHOD_POST,*/
	/*CAMEL_HTTP_METHOD_PUT,*/
	/*CAMEL_HTTP_METHOD_DELETE,*/
	/*CAMEL_HTTP_METHOD_TRACE,*/
	/*CAMEL_HTTP_METHOD_CONNECT*/
} CamelHttpMethod;

typedef struct _CamelHttpStreamClass CamelHttpStreamClass;

struct _CamelHttpStream {
	CamelStream parent_object;

	CamelMimeParser *parser;

	CamelContentType *content_type;
	struct _camel_header_raw *headers;

	CamelHttpMethod method;
	struct _CamelSession *session;
	CamelURL *url;

	gchar *user_agent;

	/* proxy info */
	CamelURL *proxy;
	gchar *authrealm;
	gchar *authpass;

	gint statuscode;

	CamelStream *raw;
	CamelStream *read;
};

struct _CamelHttpStreamClass {
	CamelStreamClass parent_class;

	/* Virtual methods */
};

/* Standard Camel function */
CamelType camel_http_stream_get_type (void);

/* public methods */
CamelStream *camel_http_stream_new (CamelHttpMethod method, struct _CamelSession *session, CamelURL *url);

void camel_http_stream_set_user_agent (CamelHttpStream *http_stream, const gchar *user_agent);

void camel_http_stream_set_proxy (CamelHttpStream *http_stream, const gchar *proxy_url);
void camel_http_stream_set_proxy_authrealm (CamelHttpStream *http_stream, const gchar *proxy_authrealm);
void camel_http_stream_set_proxy_authpass (CamelHttpStream *http_stream, const gchar *proxy_authpass);

CamelContentType *camel_http_stream_get_content_type (CamelHttpStream *http_stream);

G_END_DECLS

#endif /* __CAMEL_HTTP_STREAM_H__ */

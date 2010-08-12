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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camel-http-stream.h"
#include "camel-mime-utils.h"
#include "camel-net-utils.h"
#include "camel-service.h" /* for hostname stuff */
#include "camel-session.h"
#include "camel-stream-buffer.h"
#include "camel-tcp-stream-raw.h"

#ifdef CAMEL_HAVE_SSL
#include "camel-tcp-stream-ssl.h"
#endif

#define SSL_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 | CAMEL_TCP_STREAM_SSL_ENABLE_SSL3)

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define d(x)

G_DEFINE_TYPE (CamelHttpStream, camel_http_stream, CAMEL_TYPE_STREAM)

static CamelStream *
http_connect (CamelHttpStream *http,
              CamelURL *url,
              GError **error)
{
	CamelTcpStream *tcp_stream;
	CamelStream *stream = NULL;
	gint errsave;
	gchar *serv;

	d(printf("connecting to http stream @ '%s'\n", url->host));

	if (!g_ascii_strcasecmp (url->protocol, "https")) {
#ifdef CAMEL_HAVE_SSL
		stream = camel_tcp_stream_ssl_new (http->session, url->host, SSL_FLAGS);
#endif
	} else {
		stream = camel_tcp_stream_raw_new ();
	}

	if (stream == NULL) {
		errno = EINVAL;
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"%s", g_strerror (errno));
		return NULL;
	}

	if (url->port) {
		serv = g_alloca(16);
		sprintf(serv, "%d", url->port);
	} else {
		serv = url->protocol;
	}

	tcp_stream = CAMEL_TCP_STREAM (stream);

	if (camel_tcp_stream_connect (tcp_stream, url->host, serv, 0, error) == -1) {
		errsave = errno;
		g_object_unref (stream);
		errno = errsave;
		return NULL;
	}

	http->raw = stream;
	http->read = camel_stream_buffer_new (stream, CAMEL_STREAM_BUFFER_READ);

	return stream;
}

static void
http_disconnect (CamelHttpStream *http)
{
	if (http->raw) {
		g_object_unref (http->raw);
		http->raw = NULL;
	}

	if (http->read) {
		g_object_unref (http->read);
		http->read = NULL;
	}

	if (http->parser) {
		g_object_unref (http->parser);
		http->parser = NULL;
	}
}

static gint
http_method_invoke (CamelHttpStream *http,
                    GError **error)
{
	const gchar *method = NULL, *use_url;
	gchar *url;

	switch (http->method) {
	case CAMEL_HTTP_METHOD_GET:
		method = "GET";
		break;
	case CAMEL_HTTP_METHOD_HEAD:
		method = "HEAD";
		break;
	default:
		g_assert_not_reached ();
	}

	url = camel_url_to_string (http->url, 0);

	if (http->proxy) {
		use_url = url;
	} else if (http->url->host && *http->url->host) {
		use_url = strstr (url, http->url->host) + strlen (http->url->host);
	} else {
		use_url = http->url->path;
	}

	d(printf("HTTP Stream Sending: %s %s HTTP/1.0\r\nUser-Agent: %s\r\nHost: %s\r\n",
		 method,
		 use_url,
		 http->user_agent ? http->user_agent : "CamelHttpStream/1.0",
		 http->url->host));
	if (camel_stream_printf (
		http->raw,
		"%s %s HTTP/1.0\r\nUser-Agent: %s\r\nHost: %s\r\n",
		method, use_url, http->user_agent ? http->user_agent :
		"CamelHttpStream/1.0", http->url->host) == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"%s", g_strerror (errno));
		http_disconnect(http);
		g_free (url);
		return -1;
	}
	g_free (url);

	if (http->authrealm) {
		d(printf("HTTP Stream Sending: WWW-Authenticate: %s\n", http->authrealm));
	}

	if (http->authrealm && camel_stream_printf (
		http->raw, "WWW-Authenticate: %s\r\n",
		http->authrealm) == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"%s", g_strerror (errno));
		http_disconnect(http);
		return -1;
	}

	if (http->authpass && http->proxy) {
		d(printf("HTTP Stream Sending: Proxy-Aurhorization: Basic %s\n", http->authpass));
	}

	if (http->authpass && http->proxy && camel_stream_printf (
		http->raw, "Proxy-Authorization: Basic %s\r\n",
		http->authpass) == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"%s", g_strerror (errno));
		http_disconnect(http);
		return -1;
	}

	/* end the headers */
	if (camel_stream_write (http->raw, "\r\n", 2, error) == -1 ||
		camel_stream_flush (http->raw, error) == -1) {
		http_disconnect(http);
		return -1;
	}

	return 0;
}

static gint
http_get_headers (CamelHttpStream *http,
                  GError **error)
{
	struct _camel_header_raw *headers, *node, *tail;
	const gchar *type;
	gchar *buf;
	gsize len;
	gint err;

	if (http->parser)
		g_object_unref (http->parser);

	http->parser = camel_mime_parser_new ();
	camel_mime_parser_init_with_stream (http->parser, http->read, NULL);

	switch (camel_mime_parser_step (http->parser, &buf, &len)) {
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
	case CAMEL_MIME_PARSER_STATE_HEADER:
		headers = camel_mime_parser_headers_raw (http->parser);
		if (http->content_type)
			camel_content_type_unref (http->content_type);
		type = camel_header_raw_find (&headers, "Content-Type", NULL);
		if (type)
			http->content_type = camel_content_type_decode (type);
		else
			http->content_type = NULL;

		if (http->headers)
			camel_header_raw_clear (&http->headers);

		http->headers = NULL;
		tail = (struct _camel_header_raw *) &http->headers;

		d(printf("HTTP Headers:\n"));
		while (headers) {
			d(printf(" %s:%s\n", headers->name, headers->value));
			node = g_new (struct _camel_header_raw, 1);
			node->next = NULL;
			node->name = g_strdup (headers->name);
			node->value = g_strdup (headers->value);
			node->offset = headers->offset;
			tail->next = node;
			tail = node;
			headers = headers->next;
		}

		break;
	default:
		g_warning ("Invalid state encountered???: %u", camel_mime_parser_state (http->parser));
	}

	err = camel_mime_parser_errno (http->parser);

	if (err != 0) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (err),
			"%s", g_strerror (err));
		g_object_unref (http->parser);
		http->parser = NULL;
		goto exception;
	}

	camel_mime_parser_drop_step (http->parser);

	return 0;

 exception:
	http_disconnect(http);

	return -1;
}

static const gchar *
http_next_token (const guchar *in)
{
	const guchar *inptr = in;

	while (*inptr && !isspace ((gint) *inptr))
		inptr++;

	while (*inptr && isspace ((gint) *inptr))
		inptr++;

	return (const gchar *) inptr;
}

static gint
http_get_statuscode (CamelHttpStream *http,
                     GError **error)
{
	const gchar *token;
	gchar buffer[4096];

	if (camel_stream_buffer_gets (
		CAMEL_STREAM_BUFFER (http->read),
		buffer, sizeof (buffer), error) <= 0)
		return -1;

	d(printf("HTTP Status: %s\n", buffer));

	/* parse the HTTP status code */
	if (!g_ascii_strncasecmp (buffer, "HTTP/", 5)) {
		token = http_next_token ((const guchar *) buffer);
		http->statuscode = camel_header_decode_int (&token);
		return http->statuscode;
	}

	http_disconnect(http);

	return -1;
}

static void
http_stream_dispose (GObject *object)
{
	CamelHttpStream *http = CAMEL_HTTP_STREAM (object);

	if (http->parser != NULL) {
		g_object_unref (http->parser);
		http->parser = NULL;
	}

	if (http->content_type != NULL) {
		camel_content_type_unref (http->content_type);
		http->content_type = NULL;
	}

	if (http->session != NULL) {
		g_object_unref (http->session);
		http->session = NULL;
	}

	if (http->raw != NULL) {
		g_object_unref (http->raw);
		http->raw = NULL;
	}

	if (http->read != NULL) {
		g_object_unref (http->read);
		http->read = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_http_stream_parent_class)->dispose (object);
}

static void
http_stream_finalize (GObject *object)
{
	CamelHttpStream *http = CAMEL_HTTP_STREAM (object);

	camel_header_raw_clear (&http->headers);

	if (http->url != NULL)
		camel_url_free (http->url);

	if (http->proxy)
		camel_url_free (http->proxy);

	g_free (http->authrealm);
	g_free (http->authpass);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_http_stream_parent_class)->finalize (object);
}

static gssize
http_stream_read (CamelStream *stream,
                  gchar *buffer,
                  gsize n,
                  GError **error)
{
	CamelHttpStream *http = CAMEL_HTTP_STREAM (stream);
	const gchar *parser_buf;
	gssize nread;

	if (http->method != CAMEL_HTTP_METHOD_GET &&
		http->method != CAMEL_HTTP_METHOD_HEAD) {
		errno = EIO;
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"%s", g_strerror (errno));
		return -1;
	}

 redirect:

	if (!http->raw) {
		if (http_connect (
			http, http->proxy ? http->proxy :
			http->url, error) == NULL)
			return -1;

		if (http_method_invoke (http, error) == -1) {
			http_disconnect(http);
			return -1;
		}

		if (http_get_statuscode (http, error) == -1) {
			http_disconnect(http);
			return -1;
		}

		if (http_get_headers (http, error) == -1) {
			http_disconnect(http);
			return -1;
		}

		switch (http->statuscode) {
		case 200:
		case 206:
			/* we are OK to go... */
			break;
		case 301:
		case 302: {
			gchar *loc;
			CamelURL *url;

			camel_content_type_unref (http->content_type);
			http->content_type = NULL;
			http_disconnect(http);

			loc = g_strdup(camel_header_raw_find(&http->headers, "Location", NULL));
			if (loc == NULL) {
				camel_header_raw_clear(&http->headers);
				return -1;
			}

			/* redirect... */
			g_strstrip(loc);
			d(printf("HTTP redirect, location = %s\n", loc));
			url = camel_url_new_with_base(http->url, loc);
			camel_url_free (http->url);
			http->url = url;
			if (url == NULL)
				http->url = camel_url_new(loc, NULL);
			g_free(loc);
			if (http->url == NULL) {
				camel_header_raw_clear (&http->headers);
				return -1;
			}
			d(printf(" redirect url = %p\n", http->url));
			camel_header_raw_clear (&http->headers);

			goto redirect;
			break; }
		case 407:
			/* failed proxy authentication? */
		default:
			/* unknown error */
			http_disconnect(http);
			return -1;
		}
	}

	if (n == 0)
		return 0;

	nread = camel_mime_parser_read (http->parser, &parser_buf, n, error);

	if (nread > 0)
		memcpy (buffer, parser_buf, nread);
	else if (nread == 0)
		stream->eos = TRUE;

	return nread;
}

static gssize
http_stream_write (CamelStream *stream,
                   const gchar *buffer,
                   gsize n,
                   GError **error)
{
	return -1;
}

static gint
http_stream_flush (CamelStream *stream,
                   GError **error)
{
	CamelHttpStream *http = (CamelHttpStream *) stream;

	if (http->raw)
		return camel_stream_flush (http->raw, error);
	else
		return 0;
}

static gint
http_stream_close (CamelStream *stream,
                   GError **error)
{
	CamelHttpStream *http = (CamelHttpStream *) stream;

	if (http->raw) {
		if (camel_stream_close (http->raw, error) == -1)
			return -1;

		http_disconnect(http);
	}

	return 0;
}

static gint
http_stream_reset (CamelStream *stream,
                   GError **error)
{
	CamelHttpStream *http = CAMEL_HTTP_STREAM (stream);

	if (http->raw)
		http_disconnect(http);

	return 0;
}

static void
camel_http_stream_class_init (CamelHttpStreamClass *class)
{
	GObjectClass *object_class;
	CamelStreamClass *stream_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = http_stream_dispose;
	object_class->finalize = http_stream_finalize;

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->read = http_stream_read;
	stream_class->write = http_stream_write;
	stream_class->flush = http_stream_flush;
	stream_class->close = http_stream_close;
	stream_class->reset = http_stream_reset;
}

static void
camel_http_stream_init (CamelHttpStream *http)
{
}

/**
 * camel_http_stream_new:
 * @method: HTTP method
 * @session: active session
 * @url: URL to act upon
 *
 * Returns: a http stream
 **/
CamelStream *
camel_http_stream_new (CamelHttpMethod method, struct _CamelSession *session, CamelURL *url)
{
	CamelHttpStream *stream;
	gchar *str;

	g_return_val_if_fail(CAMEL_IS_SESSION(session), NULL);
	g_return_val_if_fail(url != NULL, NULL);

	stream = g_object_new (CAMEL_TYPE_HTTP_STREAM, NULL);

	stream->method = method;
	stream->session = g_object_ref (session);

	str = camel_url_to_string (url, 0);
	stream->url = camel_url_new (str, NULL);
	g_free (str);

	return (CamelStream *)stream;
}

CamelContentType *
camel_http_stream_get_content_type (CamelHttpStream *http_stream)
{
	g_return_val_if_fail (CAMEL_IS_HTTP_STREAM (http_stream), NULL);

	if (!http_stream->content_type && !http_stream->raw) {
		CamelStream *stream = CAMEL_STREAM (http_stream);

		if (http_stream_read (stream, NULL, 0, NULL) == -1)
			return NULL;
	}

	if (http_stream->content_type)
		camel_content_type_ref (http_stream->content_type);

	return http_stream->content_type;
}

void
camel_http_stream_set_user_agent (CamelHttpStream *http_stream, const gchar *user_agent)
{
	g_return_if_fail (CAMEL_IS_HTTP_STREAM (http_stream));

	g_free (http_stream->user_agent);
	http_stream->user_agent = g_strdup (user_agent);
}

void
camel_http_stream_set_proxy (CamelHttpStream *http_stream, const gchar *proxy_url)
{
	g_return_if_fail (CAMEL_IS_HTTP_STREAM (http_stream));

	if (http_stream->proxy)
		camel_url_free (http_stream->proxy);

	if (proxy_url == NULL || !*proxy_url)
		http_stream->proxy = NULL;
	else
		http_stream->proxy = camel_url_new (proxy_url, NULL);

	if (http_stream->proxy && ((http_stream->proxy->user && *http_stream->proxy->user) || (http_stream->proxy->passwd && *http_stream->proxy->passwd))) {
		gchar *basic, *basic64;

		basic = g_strdup_printf("%s:%s", http_stream->proxy->user?http_stream->proxy->user:"",
					http_stream->proxy->passwd?http_stream->proxy->passwd:"");
		basic64 = g_base64_encode((const guchar *) basic, strlen(basic));
		memset(basic, 0, strlen(basic));
		g_free(basic);
		camel_http_stream_set_proxy_authpass(http_stream, basic64);
		memset(basic64, 0, strlen(basic64));
		g_free(basic64);
	} else {
		camel_http_stream_set_proxy_authpass(http_stream, NULL);
	}
}

void
camel_http_stream_set_proxy_authrealm (CamelHttpStream *http_stream, const gchar *proxy_authrealm)
{
	g_return_if_fail (CAMEL_IS_HTTP_STREAM (http_stream));

	g_free (http_stream->authrealm);
	http_stream->authrealm = g_strdup (proxy_authrealm);
}

void
camel_http_stream_set_proxy_authpass (CamelHttpStream *http_stream, const gchar *proxy_authpass)
{
	g_return_if_fail (CAMEL_IS_HTTP_STREAM (http_stream));

	g_free (http_stream->authpass);
	http_stream->authpass = g_strdup (proxy_authpass);
}

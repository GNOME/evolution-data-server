/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 * Author:
 *  Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#include <glib/gi18n-lib.h>

#include <camel/camel-stream-mem.h>

#include "camel-imapx-utils.h"
#include "camel-imapx-stream.h"

#define CAMEL_IMAPX_STREAM_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_IMAPX_STREAM, CamelIMAPXStreamPrivate))

#define t(...) camel_imapx_debug(token, __VA_ARGS__)
#define io(...) camel_imapx_debug(io, __VA_ARGS__)

struct _CamelIMAPXStreamPrivate {
	CamelStream *source;

	guchar *buf, *ptr, *end;
	guint literal;

	guint unget;
	camel_imapx_token_t unget_tok;
	guchar *unget_token;
	guint unget_len;

	guchar *tokenbuf;
	guint bufsize;
};

enum {
	PROP_0,
	PROP_SOURCE
};

G_DEFINE_TYPE (CamelIMAPXStream, camel_imapx_stream, CAMEL_TYPE_STREAM)

static gint
imapx_stream_fill (CamelIMAPXStream *is,
                   GCancellable *cancellable,
                   GError **error)
{
	gint left = 0;

	if (is->priv->source != NULL) {
		left = is->priv->end - is->priv->ptr;
		memcpy (is->priv->buf, is->priv->ptr, left);
		is->priv->end = is->priv->buf + left;
		is->priv->ptr = is->priv->buf;
		left = camel_stream_read (
			is->priv->source,
			(gchar *) is->priv->end,
			is->priv->bufsize - (is->priv->end - is->priv->buf),
			cancellable, error);
		if (left > 0) {
			is->priv->end += left;
			io (is->tagprefix, "camel_imapx_read: buffer is '%.*s'\n", (gint)(is->priv->end - is->priv->ptr), is->priv->ptr);
			return is->priv->end - is->priv->ptr;
		} else {
			io (is->tagprefix, "camel_imapx_read: -1\n");
			/* If returning zero, camel_stream_read() doesn't consider
			 * that to be an error. But we do -- we should only be here
			 * if we *know* there are data to receive. So set the error
			 * accordingly */
			if (!left)
				g_set_error (
					error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
					_("Source stream returned no data"));
			return -1;
		}
	}

	io (is->tagprefix, "camel_imapx_read: -1\n");

	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Source stream unavailable"));

	return -1;
}

static void
imapx_stream_set_source (CamelIMAPXStream *stream,
                         CamelStream *source)
{
	g_return_if_fail (CAMEL_IS_STREAM (source));
	g_return_if_fail (stream->priv->source == NULL);

	stream->priv->source = g_object_ref (source);
}

static void
imapx_stream_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE:
			imapx_stream_set_source (
				CAMEL_IMAPX_STREAM (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_stream_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE:
			g_value_take_object (
				value,
				camel_imapx_stream_ref_source (
				CAMEL_IMAPX_STREAM (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_stream_dispose (GObject *object)
{
	CamelIMAPXStream *stream = CAMEL_IMAPX_STREAM (object);

	if (stream->priv->source != NULL) {
		g_object_unref (stream->priv->source);
		stream->priv->source = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_stream_parent_class)->dispose (object);
}

static void
imapx_stream_finalize (GObject *object)
{
	CamelIMAPXStream *stream = CAMEL_IMAPX_STREAM (object);

	g_free (stream->priv->buf);
	g_free (stream->priv->tokenbuf);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_stream_parent_class)->finalize (object);
}

static gssize
imapx_stream_read (CamelStream *stream,
                   gchar *buffer,
                   gsize n,
                   GCancellable *cancellable,
                   GError **error)
{
	CamelIMAPXStream *is = (CamelIMAPXStream *) stream;
	gssize max;

	if (is->priv->literal == 0 || n == 0)
		return 0;

	max = is->priv->end - is->priv->ptr;
	if (max > 0) {
		max = MIN (max, is->priv->literal);
		max = MIN (max, n);
		memcpy (buffer, is->priv->ptr, max);
		is->priv->ptr += max;
	} else {
		max = MIN (is->priv->literal, n);
		max = camel_stream_read (
			is->priv->source,
			buffer, max, cancellable, error);
		if (max <= 0)
			return max;
	}

	io (is->tagprefix, "camel_imapx_read(literal): '%.*s'\n", (gint) max, buffer);

	is->priv->literal -= max;

	return max;
}

static gssize
imapx_stream_write (CamelStream *stream,
                    const gchar *buffer,
                    gsize n,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelIMAPXStream *is = (CamelIMAPXStream *) stream;

	if (g_strstr_len (buffer, n, "LOGIN")) {
		io (is->tagprefix, "camel_imapx_write: 'LOGIN...'\n");
	} else {
		io (is->tagprefix, "camel_imapx_write: '%.*s'\n", (gint) n, buffer);
	}

	return camel_stream_write (
		is->priv->source,
		buffer, n, cancellable, error);
}

static gint
imapx_stream_close (CamelStream *stream,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelIMAPXStream *is = (CamelIMAPXStream *) stream;

	return camel_stream_close (is->priv->source, cancellable, error);
}

static gint
imapx_stream_flush (CamelStream *stream,
                    GCancellable *cancellable,
                    GError **error)
{
	/* nop? */
	return 0;
}

static gboolean
imapx_stream_eos (CamelStream *stream)
{
	CamelIMAPXStream *is = (CamelIMAPXStream *) stream;

	return is->priv->literal == 0;
}

static void
camel_imapx_stream_class_init (CamelIMAPXStreamClass *class)
{
	GObjectClass *object_class;
	CamelStreamClass *stream_class;

	g_type_class_add_private (class, sizeof (CamelIMAPXStreamPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = imapx_stream_set_property;
	object_class->get_property = imapx_stream_get_property;
	object_class->dispose = imapx_stream_dispose;
	object_class->finalize = imapx_stream_finalize;

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->read = imapx_stream_read;
	stream_class->write = imapx_stream_write;
	stream_class->close = imapx_stream_close;
	stream_class->flush = imapx_stream_flush;
	stream_class->eos = imapx_stream_eos;

	g_object_class_install_property (
		object_class,
		PROP_SOURCE,
		g_param_spec_object (
			"source",
			"Source",
			"Source stream",
			CAMEL_TYPE_STREAM,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_imapx_stream_init (CamelIMAPXStream *is)
{
	is->priv = CAMEL_IMAPX_STREAM_GET_PRIVATE (is);

	/* +1 is room for appending a 0 if we need to for a token */
	is->priv->bufsize = 4096;
	is->priv->buf = g_malloc (is->priv->bufsize + 1);
	is->priv->ptr = is->priv->end = is->priv->buf;
	is->priv->tokenbuf = g_malloc (is->priv->bufsize + 1);
}

static void
camel_imapx_stream_grow (CamelIMAPXStream *is,
                         guint len,
                         guchar **bufptr,
                         guchar **tokptr)
{
	guchar *oldtok = is->priv->tokenbuf;
	guchar *oldbuf = is->priv->buf;

	do {
		is->priv->bufsize <<= 1;
	} while (is->priv->bufsize <= len);

	io (is->tagprefix, "Grow imapx buffers to %d bytes\n", is->priv->bufsize);

	is->priv->tokenbuf = g_realloc (
		is->priv->tokenbuf,
		is->priv->bufsize + 1);
	if (tokptr)
		*tokptr = is->priv->tokenbuf + (*tokptr - oldtok);
	if (is->priv->unget)
		is->priv->unget_token =
			is->priv->tokenbuf +
			(is->priv->unget_token - oldtok);

	is->priv->buf = g_realloc (is->priv->buf, is->priv->bufsize + 1);
	is->priv->ptr = is->priv->buf + (is->priv->ptr - oldbuf);
	is->priv->end = is->priv->buf + (is->priv->end - oldbuf);
	if (bufptr)
		*bufptr = is->priv->buf + (*bufptr - oldbuf);
}

GQuark
camel_imapx_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "camel-imapx-error-quark";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

/**
 * camel_imapx_stream_new:
 *
 * Returns a NULL stream.  A null stream is always at eof, and
 * always returns success for all reads and writes.
 *
 * Returns: the stream
 **/
CamelStream *
camel_imapx_stream_new (CamelStream *source)
{
	g_return_val_if_fail (CAMEL_IS_STREAM (source), NULL);

	return g_object_new (
		CAMEL_TYPE_IMAPX_STREAM,
		"source", source, NULL);
}

CamelStream *
camel_imapx_stream_ref_source (CamelIMAPXStream *is)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (is), NULL);

	return g_object_ref (is->priv->source);
}

/* Returns if there is any data buffered that is ready for processing */
gint
camel_imapx_stream_buffered (CamelIMAPXStream *is)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (is), 0);

	return is->priv->end - is->priv->ptr;
}

/* FIXME: these should probably handle it themselves,
 * and get rid of the token interface? */
gint
camel_imapx_stream_atom (CamelIMAPXStream *is,
                         guchar **data,
                         guint *lenp,
                         GCancellable *cancellable,
                         GError **error)
{
	guchar *p, c;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (is), IMAPX_TOK_ERROR);
	g_return_val_if_fail (data != NULL, IMAPX_TOK_ERROR);
	g_return_val_if_fail (lenp != NULL, IMAPX_TOK_ERROR);

	/* this is only 'approximate' atom */
	switch (camel_imapx_stream_token (is, data, lenp, cancellable, &local_error)) {
	case IMAPX_TOK_TOKEN:
		p = *data;
		while ((c = *p))
			*p++ = toupper(c);
	case IMAPX_TOK_INT:
		return 0;
	case IMAPX_TOK_ERROR:
		if (local_error != NULL)
			g_propagate_error (error, local_error);
		return IMAPX_TOK_ERROR;
	default:
		if (local_error == NULL)
			g_set_error (error, CAMEL_IMAPX_ERROR, 1, "expecting atom");
		else
			g_propagate_error (error, local_error);
		io (is->tagprefix, "expecting atom!\n");
		return IMAPX_TOK_PROTOCOL;
	}
}

/* gets an atom, a quoted_string, or a literal */
gint
camel_imapx_stream_astring (CamelIMAPXStream *is,
                            guchar **data,
                            GCancellable *cancellable,
                            GError **error)
{
	guchar *p, *start;
	guint len, inlen;
	gint ret;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IMAPX_STREAM (is), IMAPX_TOK_ERROR);
	g_return_val_if_fail (data != NULL, IMAPX_TOK_ERROR);

	switch (camel_imapx_stream_token (is, data, &len, cancellable, &local_error)) {
	case IMAPX_TOK_TOKEN:
	case IMAPX_TOK_INT:
	case IMAPX_TOK_STRING:
		return 0;
	case IMAPX_TOK_LITERAL:
		if (len >= is->priv->bufsize)
			camel_imapx_stream_grow (is, len, NULL, NULL);
		p = is->priv->tokenbuf;
		camel_imapx_stream_set_literal (is, len);
		do {
			ret = camel_imapx_stream_getl (is, &start, &inlen, cancellable, error);
			if (ret < 0)
				return ret;
			memcpy (p, start, inlen);
			p += inlen;
		} while (ret > 0);
		*p = 0;
		*data = is->priv->tokenbuf;
		return 0;
	case IMAPX_TOK_ERROR:
		/* wont get unless no exception hanlder*/
		if (local_error != NULL)
			g_propagate_error (error, local_error);
		return IMAPX_TOK_ERROR;
	default:
		if (local_error == NULL)
			g_set_error (error, CAMEL_IMAPX_ERROR, 1, "expecting astring");
		else
			g_propagate_error (error, local_error);
		io (is->tagprefix, "expecting astring!\n");
		return IMAPX_TOK_PROTOCOL;
	}
}

/* check for NIL or (small) quoted_string or literal */
gint
camel_imapx_stream_nstring (CamelIMAPXStream *is,
                            guchar **data,
                            GCancellable *cancellable,
                            GError **error)
{
	guchar *p, *start;
	guint len, inlen;
	gint ret;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (is), IMAPX_TOK_ERROR);
	g_return_val_if_fail (data != NULL, IMAPX_TOK_ERROR);

	switch (camel_imapx_stream_token (is, data, &len, cancellable, &local_error)) {
	case IMAPX_TOK_STRING:
		return 0;
	case IMAPX_TOK_LITERAL:
		if (len >= is->priv->bufsize)
			camel_imapx_stream_grow (is, len, NULL, NULL);
		p = is->priv->tokenbuf;
		camel_imapx_stream_set_literal (is, len);
		do {
			ret = camel_imapx_stream_getl (is, &start, &inlen, cancellable, error);
			if (ret < 0)
				return ret;
			memcpy (p, start, inlen);
			p += inlen;
		} while (ret > 0);
		*p = 0;
		*data = is->priv->tokenbuf;
		return 0;
	case IMAPX_TOK_TOKEN:
		p = *data;
		if (toupper (p[0]) == 'N' && toupper (p[1]) == 'I' && toupper (p[2]) == 'L' && p[3] == 0) {
			*data = NULL;
			return 0;
		}
	default:
		if (local_error == NULL)
			g_set_error (error, CAMEL_IMAPX_ERROR, 1, "expecting nstring");
		else
			g_propagate_error (error, local_error);
		return IMAPX_TOK_PROTOCOL;
	case IMAPX_TOK_ERROR:
		/* we'll never get this unless there are no exception  handlers anyway */
		if (local_error != NULL)
			g_propagate_error (error, local_error);
		return IMAPX_TOK_ERROR;

	}
}

/* parse an nstring as a stream */
gint
camel_imapx_stream_nstring_stream (CamelIMAPXStream *is,
                                   CamelStream **stream,
                                   GCancellable *cancellable,
                                   GError **error)
{
	guchar *token;
	guint len;
	gint ret = 0;
	CamelStream * mem = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (is), -1);
	g_return_val_if_fail (stream != NULL, -1);

	*stream = NULL;

	switch (camel_imapx_stream_token (is, &token, &len, cancellable, &local_error)) {
		case IMAPX_TOK_STRING:
			mem = camel_stream_mem_new_with_buffer ((gchar *) token, len);
			*stream = mem;
			break;
		case IMAPX_TOK_LITERAL:
			/* if len is big, we could automatically use a file backing */
			camel_imapx_stream_set_literal (is, len);
			mem = camel_stream_mem_new ();
			if (camel_stream_write_to_stream ((CamelStream *) is, mem, cancellable, error) == -1) {
				g_object_unref (mem);
				ret = -1;
				break;
			}

			g_seekable_seek (
				G_SEEKABLE (mem), 0,
				G_SEEK_SET, NULL, NULL);

			*stream = mem;
			break;
		case IMAPX_TOK_TOKEN:
			if (toupper (token[0]) == 'N' && toupper (token[1]) == 'I' && toupper (token[2]) == 'L' && token[3] == 0) {
				*stream = NULL;
				break;
			}
		default:
			ret = -1;
			if (local_error == NULL)
				g_set_error (error, CAMEL_IMAPX_ERROR, 1, "nstring: token not string");
			else
				g_propagate_error (error, local_error);
	}

	return ret;
}

guint64
camel_imapx_stream_number (CamelIMAPXStream *is,
                           GCancellable *cancellable,
                           GError **error)
{
	guchar *token;
	guint len;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (is), 0);

	if (camel_imapx_stream_token (is, &token, &len, cancellable, &local_error) != IMAPX_TOK_INT) {
		if (local_error == NULL)
			g_set_error (error, CAMEL_IMAPX_ERROR, 1, "expecting number");
		else
			g_propagate_error (error, local_error);
		return 0;
	}

	return strtoull ((gchar *) token, 0, 10);
}

gint
camel_imapx_stream_text (CamelIMAPXStream *is,
                         guchar **text,
                         GCancellable *cancellable,
                         GError **error)
{
	GByteArray *build = g_byte_array_new ();
	guchar *token;
	guint len;
	gint tok;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (is), -1);
	g_return_val_if_fail (text != NULL, -1);

	while (is->priv->unget > 0) {
		switch (is->priv->unget_tok) {
			case IMAPX_TOK_TOKEN:
			case IMAPX_TOK_STRING:
			case IMAPX_TOK_INT:
				g_byte_array_append (
					build, (guint8 *)
					is->priv->unget_token,
					is->priv->unget_len);
				g_byte_array_append (
					build, (guint8 *) " ", 1);
			default: /* invalid, but we'll ignore */
				break;
		}
		is->priv->unget--;
	}

	do {
		tok = camel_imapx_stream_gets (is, &token, &len, cancellable, error);
		if (tok < 0) {
			*text = NULL;
			g_byte_array_free (build, TRUE);
			return -1;
		}
		if (len)
			g_byte_array_append (build, token, len);
	} while (tok > 0);

	g_byte_array_append (build, (guint8 *) "", 1);
	*text = build->data;
	g_byte_array_free (build, FALSE);

	return 0;
}

/* Get one token from the imap stream */
camel_imapx_token_t
camel_imapx_stream_token (CamelIMAPXStream *is,
                          guchar **data,
                          guint *len,
                          GCancellable *cancellable,
                          GError **error)
{
	register guchar c, *oe;
	guchar *o, *p, *e;
	guint literal;
	gint digits;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (is), IMAPX_TOK_ERROR);
	g_return_val_if_fail (data != NULL, IMAPX_TOK_ERROR);
	g_return_val_if_fail (len != NULL, IMAPX_TOK_ERROR);

	if (is->priv->unget > 0) {
		is->priv->unget--;
		*data = is->priv->unget_token;
		*len = is->priv->unget_len;
		return is->priv->unget_tok;
	}

	if (is->priv->literal > 0)
		g_warning (
			"stream_token called with literal %d",
			is->priv->literal);

	p = is->priv->ptr;
	e = is->priv->end;

	/* skip whitespace/prefill buffer */
	do {
		while (p >= e ) {
			is->priv->ptr = p;
			if (imapx_stream_fill (is, cancellable, error) == IMAPX_TOK_ERROR)
				return IMAPX_TOK_ERROR;
			p = is->priv->ptr;
			e = is->priv->end;
		}
		c = *p++;
	} while (c == ' ' || c == '\r');

	/*strchr("\n*()[]+", c)*/
	if (imapx_is_token_char (c)) {
		is->priv->ptr = p;
		t (is->tagprefix, "token '%c'\n", c);
		return c;
	} else if (c == '{') {
		literal = 0;
		*data = p;
		while (1) {
			while (p < e) {
				c = *p++;
				if (isdigit (c) && literal < (UINT_MAX / 10)) {
					literal = literal * 10 + (c - '0');
				} else if (c == '}') {
					while (1) {
						while (p < e) {
							c = *p++;
							if (c == '\n') {
								*len = literal;
								is->priv->ptr = p;
								is->priv->literal = literal;
								t (is->tagprefix, "token LITERAL %d\n", literal);
								return IMAPX_TOK_LITERAL;
							}
						}
						is->priv->ptr = p;
						if (imapx_stream_fill (is, cancellable, error) == IMAPX_TOK_ERROR)
							return IMAPX_TOK_ERROR;
						p = is->priv->ptr;
						e = is->priv->end;
					}
				} else {
					if (isdigit (c)) {
						io (is->tagprefix, "Protocol error: literal too big\n");
					} else {
						io (is->tagprefix, "Protocol error: literal contains invalid gchar %02x '%c'\n", c, isprint (c) ? c : c);
					}
					goto protocol_error;
				}
			}
			is->priv->ptr = p;
			if (imapx_stream_fill (is, cancellable, error) == IMAPX_TOK_ERROR)
				return IMAPX_TOK_ERROR;
			p = is->priv->ptr;
			e = is->priv->end;
		}
	} else if (c == '"') {
		o = is->priv->tokenbuf;
		oe = is->priv->tokenbuf + is->priv->bufsize - 1;
		while (1) {
			while (p < e) {
				c = *p++;
				if (c == '\\') {
					while (p >= e) {
						is->priv->ptr = p;
						if (imapx_stream_fill (is, cancellable, error) == IMAPX_TOK_ERROR)
							return IMAPX_TOK_ERROR;
						p = is->priv->ptr;
						e = is->priv->end;
					}
					c = *p++;
				} else if (c == '\"') {
					is->priv->ptr = p;
					*o = 0;
					*data = is->priv->tokenbuf;
					*len = o - is->priv->tokenbuf;
					t (is->tagprefix, "token STRING '%s'\n", is->priv->tokenbuf);
					return IMAPX_TOK_STRING;
				}
				if (c == '\n' || c == '\r') {
					io (is->tagprefix, "Protocol error: truncated string\n");
					goto protocol_error;
				}
				if (o >= oe) {
					camel_imapx_stream_grow (is, 0, &p, &o);
					oe = is->priv->tokenbuf + is->priv->bufsize - 1;
					e = is->priv->end;
				}
				*o++ = c;
			}
			is->priv->ptr = p;
			if (imapx_stream_fill (is, cancellable, error) == IMAPX_TOK_ERROR)
				return IMAPX_TOK_ERROR;
			p = is->priv->ptr;
			e = is->priv->end;
		}
	} else {
		o = is->priv->tokenbuf;
		oe = is->priv->tokenbuf + is->priv->bufsize - 1;
		digits = isdigit (c);
		*o++ = c;
		while (1) {
			while (p < e) {
				c = *p++;
				/*if (strchr(" \r\n*()[]+", c) != NULL) {*/
				if (imapx_is_notid_char (c)) {
					if (c == ' ' || c == '\r')
						is->priv->ptr = p;
					else
						is->priv->ptr = p - 1;
					*o = 0;
					*data = is->priv->tokenbuf;
					*len = o - is->priv->tokenbuf;
					t (is->tagprefix, "token TOKEN '%s'\n", is->priv->tokenbuf);
					return digits ? IMAPX_TOK_INT : IMAPX_TOK_TOKEN;
				}

				if (o >= oe) {
					camel_imapx_stream_grow (is, 0, &p, &o);
					oe = is->priv->tokenbuf + is->priv->bufsize - 1;
					e = is->priv->end;
				}
				digits &= isdigit (c);
				*o++ = c;
			}
			is->priv->ptr = p;
			if (imapx_stream_fill (is, cancellable, error) == IMAPX_TOK_ERROR)
				return IMAPX_TOK_ERROR;
			p = is->priv->ptr;
			e = is->priv->end;
		}
	}

	/* Protocol error, skip until next lf? */
protocol_error:
	io (is->tagprefix, "Got protocol error\n");

	if (c == '\n')
		is->priv->ptr = p - 1;
	else
		is->priv->ptr = p;

	g_set_error (error, CAMEL_IMAPX_ERROR, 1, "protocol error");
	return IMAPX_TOK_PROTOCOL;
}

void
camel_imapx_stream_ungettoken (CamelIMAPXStream *is,
                               camel_imapx_token_t tok,
                               guchar *token,
                               guint len)
{
	g_return_if_fail (CAMEL_IS_IMAPX_STREAM (is));

	is->priv->unget_tok = tok;
	is->priv->unget_token = token;
	is->priv->unget_len = len;
	is->priv->unget++;
}

/* returns -1 on error, 0 if last lot of data, >0 if more remaining */
gint
camel_imapx_stream_gets (CamelIMAPXStream *is,
                         guchar **start,
                         guint *len,
                         GCancellable *cancellable,
                         GError **error)
{
	gint max;
	guchar *end;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (is), -1);
	g_return_val_if_fail (start != NULL, -1);
	g_return_val_if_fail (len != NULL, -1);

	*len = 0;

	max = is->priv->end - is->priv->ptr;
	if (max == 0) {
		max = imapx_stream_fill (is, cancellable, error);
		if (max <= 0)
			return max;
	}

	*start = is->priv->ptr;
	end = memchr (is->priv->ptr, '\n', max);
	if (end)
		max = (end - is->priv->ptr) + 1;
	*start = is->priv->ptr;
	*len = max;
	is->priv->ptr += max;

	return end == NULL ? 1 : 0;
}

void
camel_imapx_stream_set_literal (CamelIMAPXStream *is,
                                guint literal)
{
	g_return_if_fail (CAMEL_IS_IMAPX_STREAM (is));

	is->priv->literal = literal;
}

/* returns -1 on erorr, 0 if last data, >0 if more data left */
gint
camel_imapx_stream_getl (CamelIMAPXStream *is,
                         guchar **start,
                         guint *len,
                         GCancellable *cancellable,
                         GError **error)
{
	gint max;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (is), -1);
	g_return_val_if_fail (start != NULL, -1);
	g_return_val_if_fail (len != NULL, -1);

	*len = 0;

	if (is->priv->literal > 0) {
		max = is->priv->end - is->priv->ptr;
		if (max == 0) {
			max = imapx_stream_fill (is, cancellable, error);
			if (max <= 0)
				return max;
		}

		max = MIN (max, is->priv->literal);
		*start = is->priv->ptr;
		*len = max;
		is->priv->ptr += max;
		is->priv->literal -= max;
	}

	if (is->priv->literal > 0)
		return 1;

	return 0;
}

/* skip the rest of the line of tokens */
gint
camel_imapx_stream_skip (CamelIMAPXStream *is,
                         GCancellable *cancellable,
                         GError **error)
{
	gint tok;
	guchar *token;
	guint len;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (is), -1);

	do {
		tok = camel_imapx_stream_token (is, &token, &len, cancellable, error);
		if (tok == IMAPX_TOK_LITERAL) {
			camel_imapx_stream_set_literal (is, len);
			while ((tok = camel_imapx_stream_getl (is, &token, &len, cancellable, error)) > 0) {
				io (is->tagprefix, "Skip literal data '%.*s'\n", (gint) len, token);
			}
		}
	} while (tok != '\n' && tok >= 0);

	if (tok < 0)
		return -1;

	return 0;
}


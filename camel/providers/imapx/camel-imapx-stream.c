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

#include "camel-imapx-utils.h"
#include "camel-imapx-stream.h"

#define t(x) camel_imapx_debug(token, x)
#define io(x) camel_imapx_debug(io, x)

G_DEFINE_TYPE (CamelIMAPXStream, camel_imapx_stream, CAMEL_TYPE_STREAM)

static gint
imapx_stream_fill (CamelIMAPXStream *is,
                   GError **error)
{
	gint left = 0;

	if (is->source) {
		left = is->end - is->ptr;
		memcpy(is->buf, is->ptr, left);
		is->end = is->buf + left;
		is->ptr = is->buf;
		left = camel_stream_read (
			is->source, (gchar *) is->end,
			is->bufsize - (is->end - is->buf), error);
		if (left > 0) {
			is->end += left;
			io(printf("camel_imapx_read: buffer is '%.*s'\n", (gint)(is->end - is->ptr), is->ptr));
			return is->end - is->ptr;
		} else {
			io(printf("camel_imapx_read: -1\n"));
			/* If returning zero, camel_stream_read() doesn't consider
			   that to be an error. But we do -- we should only be here
			   if we *know* there are data to receive. So set the error
			   accordingly */
			if (!left)
				g_set_error(error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
					    _("Source stream returned no data"));
			return -1;
		}
	}

	io(printf("camel_imapx_read: -1\n"));

	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Source stream unavailable"));

	return -1;
}

static void
imapx_stream_dispose (GObject *object)
{
	CamelIMAPXStream *stream = CAMEL_IMAPX_STREAM (object);

	if (stream->source != NULL) {
		g_object_unref (stream->source);
		stream->source = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_stream_parent_class)->dispose (object);
}

static void
imapx_stream_finalize (GObject *object)
{
	CamelIMAPXStream *stream = CAMEL_IMAPX_STREAM (object);

	g_free (stream->buf);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_stream_parent_class)->finalize (object);
}

static gssize
imapx_stream_read (CamelStream *stream,
                   gchar *buffer,
                   gsize n,
                   GError **error)
{
	CamelIMAPXStream *is = (CamelIMAPXStream *)stream;
	gssize max;

	if (is->literal == 0 || n == 0)
		return 0;

	max = is->end - is->ptr;
	if (max > 0) {
		max = MIN(max, is->literal);
		max = MIN(max, n);
		memcpy(buffer, is->ptr, max);
		is->ptr += max;
	} else {
		max = MIN(is->literal, n);
		max = camel_stream_read (is->source, buffer, max, error);
		if (max <= 0)
			return max;
	}

	io(printf("camel_imapx_read(literal): '%.*s'\n", (gint)max, buffer));

	is->literal -= max;

	return max;
}

static gssize
imapx_stream_write (CamelStream *stream,
                    const gchar *buffer,
                    gsize n,
                    GError **error)
{
	CamelIMAPXStream *is = (CamelIMAPXStream *)stream;

	io(printf("camel_imapx_write: '%.*s'\n", (gint)n, buffer));

	return camel_stream_write (is->source, buffer, n, error);
}

static gint
imapx_stream_close (CamelStream *stream,
                    GError **error)
{
	/* nop? */
	return 0;
}

static gint
imapx_stream_flush (CamelStream *stream,
                    GError **error)
{
	/* nop? */
	return 0;
}

static gboolean
imapx_stream_eos (CamelStream *stream)
{
	CamelIMAPXStream *is = (CamelIMAPXStream *)stream;

	return is->literal == 0;
}

static gint
imapx_stream_reset (CamelStream *stream,
                    GError **error)
{
	/* nop?  reset literal mode? */
	return 0;
}

static void
camel_imapx_stream_class_init (CamelIMAPXStreamClass *class)
{
	GObjectClass *object_class;
	CamelStreamClass *stream_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = imapx_stream_dispose;
	object_class->finalize = imapx_stream_finalize;

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->read = imapx_stream_read;
	stream_class->write = imapx_stream_write;
	stream_class->close = imapx_stream_close;
	stream_class->flush = imapx_stream_flush;
	stream_class->eos = imapx_stream_eos;
	stream_class->reset = imapx_stream_reset;
}

static void
camel_imapx_stream_init (CamelIMAPXStream *is)
{
	/* +1 is room for appending a 0 if we need to for a token */
	is->bufsize = 4096;
	is->ptr = is->end = is->buf = g_malloc (is->bufsize + 1);
	is->tokenbuf = g_malloc (is->bufsize + 1);
}

static void camel_imapx_stream_grow (CamelIMAPXStream *is, guint len, guchar **bufptr, guchar **tokptr)
{
	guchar *oldtok = is->tokenbuf;
	guchar *oldbuf = is->buf;

	do {
		is->bufsize <<= 1;
	} while (is->bufsize <= len);

	io(printf("Grow imapx buffers to %d bytes\n", is->bufsize));

	is->tokenbuf = g_realloc (is->tokenbuf, is->bufsize + 1);
	if (tokptr)
		*tokptr = is->tokenbuf + (*tokptr - oldtok);
	if (is->unget)
		is->unget_token = is->tokenbuf + (is->unget_token - oldtok);

	//io(printf("buf was %p, ptr %p end %p\n", is->buf, is->ptr, is->end));
	is->buf = g_realloc (is->buf, is->bufsize + 1);
	is->ptr = is->buf + (is->ptr - oldbuf);
	is->end = is->buf + (is->end - oldbuf);
	//io(printf("buf now %p, ptr %p end %p\n", is->buf, is->ptr, is->end));
	if (bufptr)
		*bufptr = is->buf + (*bufptr - oldbuf);
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
camel_imapx_stream_new(CamelStream *source)
{
	CamelIMAPXStream *is;

	is = g_object_new (CAMEL_TYPE_IMAPX_STREAM, NULL);
	is->source = g_object_ref (source);

	return (CamelStream *)is;
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

/* Returns if there is any data buffered that is ready for processing */
gint
camel_imapx_stream_buffered(CamelIMAPXStream *is)
{
	return is->end - is->ptr;
}

#if 0

static gint
skip_ws(CamelIMAPXStream *is, guchar *pp, guchar *pe)
{
	register guchar c, *p;
	guchar *e;

	p = is->ptr;
	e = is->end;

	do {
		while (p >= e ) {
			is->ptr = p;
			if (imapx_stream_fill(is, NULL) == IMAPX_TOK_ERROR)
				return IMAPX_TOK_ERROR;
			p = is->ptr;
			e = is->end;
		}
		c = *p++;
	} while (c == ' ' || c == '\r');

	is->ptr = p;
	is->end = e;

	return c;
}
#endif

/* FIXME: these should probably handle it themselves,
   and get rid of the token interface? */
gint
camel_imapx_stream_atom(CamelIMAPXStream *is, guchar **data, guint *lenp, GError **error)
{
	guchar *p, c;

	/* this is only 'approximate' atom */
	switch (camel_imapx_stream_token(is, data, lenp, NULL)) {
	case IMAPX_TOK_TOKEN:
		p = *data;
		while ((c = *p))
			*p++ = toupper(c);
	case IMAPX_TOK_INT:
		return 0;
	case IMAPX_TOK_ERROR:
		return IMAPX_TOK_ERROR;
	default:
		g_set_error (error, CAMEL_IMAPX_ERROR, 1, "expecting atom");
		io(printf("expecting atom!\n"));
		return IMAPX_TOK_PROTOCOL;
	}
}

/* gets an atom, a quoted_string, or a literal */
gint
camel_imapx_stream_astring(CamelIMAPXStream *is, guchar **data, GError **error)
{
	guchar *p, *start;
	guint len, inlen;
	gint ret;

	switch (camel_imapx_stream_token(is, data, &len, NULL)) {
	case IMAPX_TOK_TOKEN:
	case IMAPX_TOK_INT:
	case IMAPX_TOK_STRING:
		return 0;
	case IMAPX_TOK_LITERAL:
		if (len >= is->bufsize)
			camel_imapx_stream_grow(is, len, NULL, NULL);
		p = is->tokenbuf;
		camel_imapx_stream_set_literal(is, len);
		do {
			ret = camel_imapx_stream_getl(is, &start, &inlen);
			if (ret < 0)
				return ret;
			memcpy(p, start, inlen);
			p += inlen;
		} while (ret > 0);
		*p = 0;
		*data = is->tokenbuf;
		return 0;
	case IMAPX_TOK_ERROR:
		/* wont get unless no exception hanlder*/
		return IMAPX_TOK_ERROR;
	default:
		g_set_error (error, CAMEL_IMAPX_ERROR, 1, "expecting astring");
		io(printf("expecting astring!\n"));
		return IMAPX_TOK_PROTOCOL;
	}
}

/* check for NIL or (small) quoted_string or literal */
gint
camel_imapx_stream_nstring(CamelIMAPXStream *is, guchar **data, GError **error)
{
	guchar *p, *start;
	guint len, inlen;
	gint ret;

	switch (camel_imapx_stream_token(is, data, &len, NULL)) {
	case IMAPX_TOK_STRING:
		return 0;
	case IMAPX_TOK_LITERAL:
		if (len >= is->bufsize)
			camel_imapx_stream_grow(is, len, NULL, NULL);
		p = is->tokenbuf;
		camel_imapx_stream_set_literal(is, len);
		do {
			ret = camel_imapx_stream_getl(is, &start, &inlen);
			if (ret < 0)
				return ret;
			memcpy(p, start, inlen);
			p += inlen;
		} while (ret > 0);
		*p = 0;
		*data = is->tokenbuf;
		return 0;
	case IMAPX_TOK_TOKEN:
		p = *data;
		if (toupper(p[0]) == 'N' && toupper(p[1]) == 'I' && toupper(p[2]) == 'L' && p[3] == 0) {
			*data = NULL;
			return 0;
		}
	default:
		g_set_error (error, CAMEL_IMAPX_ERROR, 1, "expecting nstring");
		return IMAPX_TOK_PROTOCOL;
	case IMAPX_TOK_ERROR:
		/* we'll never get this unless there are no exception  handlers anyway */
		return IMAPX_TOK_ERROR;

	}
}

/* parse an nstring as a stream */
gint
camel_imapx_stream_nstring_stream(CamelIMAPXStream *is, CamelStream **stream, GError **error)
/* throws IO,PARSE exception */
{
	guchar *token;
	guint len;
	gint ret = 0;
	CamelStream * mem = NULL;

	*stream = NULL;

	switch (camel_imapx_stream_token(is, &token, &len, NULL)) {
		case IMAPX_TOK_STRING:
			mem = camel_stream_mem_new_with_buffer((gchar *)token, len);
			*stream = mem;
			break;
		case IMAPX_TOK_LITERAL:
			/* if len is big, we could automatically use a file backing */
			camel_imapx_stream_set_literal(is, len);
			mem = camel_stream_mem_new();
			if (camel_stream_write_to_stream((CamelStream *)is, mem, error) == -1) {
				g_object_unref (mem);
				ret = -1;
				break;
			}
			camel_stream_reset(mem, NULL);
			*stream = mem;
			break;
		case IMAPX_TOK_TOKEN:
			if (toupper(token[0]) == 'N' && toupper(token[1]) == 'I' && toupper(token[2]) == 'L' && token[3] == 0) {
				*stream = NULL;
				break;
			}
		default:
			ret = -1;
			g_set_error (error, CAMEL_IMAPX_ERROR, 1, "nstring: token not string");
	}

	return ret;
}

guint64
camel_imapx_stream_number(CamelIMAPXStream *is, GError **error)
{
	guchar *token;
	guint len;

	if (camel_imapx_stream_token(is, &token, &len, NULL) != IMAPX_TOK_INT) {
		g_set_error (error, CAMEL_IMAPX_ERROR, 1, "expecting number");
		return 0;
	}

	return strtoull((gchar *)token, 0, 10);
}

gint
camel_imapx_stream_text(CamelIMAPXStream *is, guchar **text, GError **error)
{
	GByteArray *build = g_byte_array_new();
	guchar *token;
	guint len;
	gint tok;

	while (is->unget > 0) {
		switch (is->unget_tok) {
			case IMAPX_TOK_TOKEN:
			case IMAPX_TOK_STRING:
			case IMAPX_TOK_INT:
				g_byte_array_append(build, (guint8 *) is->unget_token, is->unget_len);
				g_byte_array_append(build, (guint8 *) " ", 1);
			default: /* invalid, but we'll ignore */
				break;
		}
		is->unget--;
	}

	do {
		tok = camel_imapx_stream_gets(is, &token, &len);
		if (tok < 0) {
			g_set_error (error, CAMEL_IMAPX_ERROR, 1, "io error: %s", strerror(errno));
			*text = NULL;
			g_byte_array_free(build, TRUE);
			return -1;
		}
		if (len)
			g_byte_array_append(build, token, len);
	} while (tok > 0);

	g_byte_array_append(build, (guint8 *) "", 1);
	*text = build->data;
	g_byte_array_free(build, FALSE);

	return 0;
}

/* Get one token from the imap stream */
camel_imapx_token_t
/* throws IO,PARSE exception */
camel_imapx_stream_token(CamelIMAPXStream *is, guchar **data, guint *len, GError **error)
{
	register guchar c, *oe;
	guchar *o, *p, *e;
	guint literal;
	gint digits;

	if (is->unget > 0) {
		is->unget--;
		*data = is->unget_token;
		*len = is->unget_len;
		/*printf("token UNGET '%c' %s\n", is->unget_tok, is->unget_token);*/
		return is->unget_tok;
	}

	if (is->literal > 0)
		g_warning("stream_token called with literal %d", is->literal);

	p = is->ptr;
	e = is->end;

	/* skip whitespace/prefill buffer */
	do {
		while (p >= e ) {
			is->ptr = p;
			if (imapx_stream_fill (is, error) == IMAPX_TOK_ERROR)
				return IMAPX_TOK_ERROR;
			p = is->ptr;
			e = is->end;
		}
		c = *p++;
	} while (c == ' ' || c == '\r');

	/*strchr("\n*()[]+", c)*/
	if (imapx_is_token_char(c)) {
		is->ptr = p;
		t(printf("token '%c'\n", c));
		return c;
	} else if (c == '{') {
		literal = 0;
		*data = p;
		while (1) {
			while (p < e) {
				c = *p++;
				if (isdigit(c) && literal < (UINT_MAX/10)) {
					literal = literal * 10 + (c - '0');
				} else if (c == '}') {
					while (1) {
						while (p < e) {
							c = *p++;
							if (c == '\n') {
								*len = literal;
								is->ptr = p;
								is->literal = literal;
								t(printf("token LITERAL %d\n", literal));
								return IMAPX_TOK_LITERAL;
							}
						}
						is->ptr = p;
						if (imapx_stream_fill (is, error) == IMAPX_TOK_ERROR)
							return IMAPX_TOK_ERROR;
						p = is->ptr;
						e = is->end;
					}
				} else {
					if (isdigit(c)) {
						io(printf("Protocol error: literal too big\n"));
					} else {
						io(printf("Protocol error: literal contains invalid gchar %02x '%c'\n", c, isprint(c)?c:c));
					}
					goto protocol_error;
				}
			}
			is->ptr = p;
			if (imapx_stream_fill (is, error) == IMAPX_TOK_ERROR)
				return IMAPX_TOK_ERROR;
			p = is->ptr;
			e = is->end;
		}
	} else if (c == '"') {
		o = is->tokenbuf;
		oe = is->tokenbuf + is->bufsize - 1;
		while (1) {
			while (p < e) {
				c = *p++;
				if (c == '\\') {
					while (p >= e) {
						is->ptr = p;
						if (imapx_stream_fill (is, error) == IMAPX_TOK_ERROR)
							return IMAPX_TOK_ERROR;
						p = is->ptr;
						e = is->end;
					}
					c = *p++;
				} else if (c == '\"') {
					is->ptr = p;
					*o = 0;
					*data = is->tokenbuf;
					*len = o - is->tokenbuf;
					t(printf("token STRING '%s'\n", is->tokenbuf));
					return IMAPX_TOK_STRING;
				}
				if (c == '\n' || c == '\r') {
					io(printf("Protocol error: truncated string\n"));
					goto protocol_error;
				}
				if (o >= oe) {
					camel_imapx_stream_grow(is, 0, &p, &o);
					oe = is->tokenbuf + is->bufsize - 1;
					e = is->end;
				}
				*o++ = c;
			}
			is->ptr = p;
			if (imapx_stream_fill (is, error) == IMAPX_TOK_ERROR)
				return IMAPX_TOK_ERROR;
			p = is->ptr;
			e = is->end;
		}
	} else {
		o = is->tokenbuf;
		oe = is->tokenbuf + is->bufsize - 1;
		digits = isdigit(c);
		*o++ = c;
		while (1) {
			while (p < e) {
				c = *p++;
				/*if (strchr(" \r\n*()[]+", c) != NULL) {*/
				if (imapx_is_notid_char(c)) {
					if (c == ' ' || c == '\r')
						is->ptr = p;
					else
						is->ptr = p-1;
					*o = 0;
					*data = is->tokenbuf;
					*len = o - is->tokenbuf;
					t(printf("token TOKEN '%s'\n", is->tokenbuf));
					return digits?IMAPX_TOK_INT:IMAPX_TOK_TOKEN;
				}

				if (o >= oe) {
					camel_imapx_stream_grow(is, 0, &p, &o);
					oe = is->tokenbuf + is->bufsize - 1;
					e = is->end;
				}
				digits &= isdigit(c);
				*o++ = c;
			}
			is->ptr = p;
			if (imapx_stream_fill (is, error) == IMAPX_TOK_ERROR)
				return IMAPX_TOK_ERROR;
			p = is->ptr;
			e = is->end;
		}
	}

	/* Protocol error, skip until next lf? */
protocol_error:
	io(printf("Got protocol error\n"));

	if (c == '\n')
		is->ptr = p-1;
	else
		is->ptr = p;

	g_set_error (error, CAMEL_IMAPX_ERROR, 1, "protocol error");
	return IMAPX_TOK_PROTOCOL;
}

void
camel_imapx_stream_ungettoken(CamelIMAPXStream *is, camel_imapx_token_t tok, guchar *token, guint len)
{
	/*printf("ungettoken: '%c' '%s'\n", tok, token);*/
	is->unget_tok = tok;
	is->unget_token = token;
	is->unget_len = len;
	is->unget++;
}

/* returns -1 on error, 0 if last lot of data, >0 if more remaining */
gint camel_imapx_stream_gets(CamelIMAPXStream *is, guchar **start, guint *len)
{
	gint max;
	guchar *end;

	*len = 0;

	max = is->end - is->ptr;
	if (max == 0) {
		max = imapx_stream_fill(is, NULL);
		if (max <= 0)
			return max;
	}

	*start = is->ptr;
	end = memchr(is->ptr, '\n', max);
	if (end)
		max = (end - is->ptr) + 1;
	*start = is->ptr;
	*len = max;
	is->ptr += max;

	return end == NULL?1:0;
}

void camel_imapx_stream_set_literal(CamelIMAPXStream *is, guint literal)
{
	is->literal = literal;
}

/* returns -1 on erorr, 0 if last data, >0 if more data left */
gint camel_imapx_stream_getl(CamelIMAPXStream *is, guchar **start, guint *len)
{
	gint max;

	*len = 0;

	if (is->literal > 0) {
		max = is->end - is->ptr;
		if (max == 0) {
			max = imapx_stream_fill(is, NULL);
			if (max <= 0)
				return max;
		}

		max = MIN(max, is->literal);
		*start = is->ptr;
		*len = max;
		is->ptr += max;
		is->literal -= max;
	}

	if (is->literal > 0)
		return 1;

	return 0;
}

/* skip the rest of the line of tokens */
gint
camel_imapx_stream_skip(CamelIMAPXStream *is, GError **error)
{
	gint tok;
	guchar *token;
	guint len;

	do {
		tok = camel_imapx_stream_token(is, &token, &len, error);
		if (tok == IMAPX_TOK_LITERAL) {
			camel_imapx_stream_set_literal(is, len);
			while ((tok = camel_imapx_stream_getl(is, &token, &len)) > 0) {
				io(printf("Skip literal data '%.*s'\n", (gint)len, token));
			}
		}
	} while (tok != '\n' && tok >= 0);

	if (tok < 0)
		return -1;

	return 0;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 * Author:
 *  Michael Zucchi <notzed@ximian.com>
 *
 * Copyright 1999, 2000 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#include <glib.h>

#include <camel/camel-stream-mem.h>

#include "camel-imapx-utils.h"
#include "camel-imapx-stream.h"
#include "camel-imapx-exception.h"

#define t(x) 
#define io(x)

static CamelObjectClass *parent_class = NULL;

/* Returns the class for a CamelStream */
#define CS_CLASS(so) CAMEL_IMAPX_STREAM_CLASS(CAMEL_OBJECT_GET_CLASS(so))

#define CAMEL_IMAPX_STREAM_SIZE (4096)
#define CAMEL_IMAPX_STREAM_TOKEN (4096) /* maximum token size */

static gint
stream_fill(CamelIMAPXStream *is)
{
	gint left = 0;

	if (is->source) {
		left = is->end - is->ptr;
		memcpy(is->buf, is->ptr, left);
		is->end = is->buf + left;
		is->ptr = is->buf;
		left = camel_stream_read(is->source, (gchar *) is->end, CAMEL_IMAPX_STREAM_SIZE - (is->end - is->buf));
		if (left > 0) {
			is->end += left;
			io(printf("camel_imapx_read: buffer is '%.*s'\n", (gint)(is->end - is->ptr), is->ptr));
			return is->end - is->ptr;
		} else {
			io(printf("camel_imapx_read: -1\n"));
			return -1;
		}
	}

	printf("camel_imapx_read: -1\n");

	return -1;
}

static gssize
stream_read(CamelStream *stream, gchar *buffer, gsize n)
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
		max = camel_stream_read(is->source, buffer, max);
		if (max <= 0)
			return max;
	}

	io(printf("camel_imapx_read(literal): '%.*s'\n", (gint)max, buffer));

	is->literal -= max;

	return max;
}

static gssize
stream_write(CamelStream *stream, const gchar *buffer, gsize n)
{
	CamelIMAPXStream *is = (CamelIMAPXStream *)stream;

	io(printf("camel_imapx_write: '%.*s'\n", (gint)n, buffer));

	return camel_stream_write(is->source, buffer, n);
}

static gint
stream_close(CamelStream *stream)
{
	/* nop? */
	return 0;
}

static gint
stream_flush(CamelStream *stream)
{
	/* nop? */
	return 0;
}

static gboolean
stream_eos(CamelStream *stream)
{
	CamelIMAPXStream *is = (CamelIMAPXStream *)stream;

	return is->literal == 0;
}

static gint
stream_reset(CamelStream *stream)
{
	/* nop?  reset literal mode? */
	return 0;
}

static void
camel_imapx_stream_class_init (CamelStreamClass *camel_imapx_stream_class)
{
	CamelStreamClass *camel_stream_class = (CamelStreamClass *)camel_imapx_stream_class;

	parent_class = camel_type_get_global_classfuncs( CAMEL_OBJECT_TYPE );

	/* virtual method definition */
	camel_stream_class->read = stream_read;
	camel_stream_class->write = stream_write;
	camel_stream_class->close = stream_close;
	camel_stream_class->flush = stream_flush;
	camel_stream_class->eos = stream_eos;
	camel_stream_class->reset = stream_reset;
}

static void
camel_imapx_stream_init(CamelIMAPXStream *is, CamelIMAPXStreamClass *isclass)
{
	/* +1 is room for appending a 0 if we need to for a token */
	is->ptr = is->end = is->buf = g_malloc(CAMEL_IMAPX_STREAM_SIZE+1);
	is->tokenptr = is->tokenbuf = g_malloc(CAMEL_IMAPX_STREAM_SIZE+1);
	is->tokenend = is->tokenbuf + CAMEL_IMAPX_STREAM_SIZE;
}

static void
camel_imapx_stream_finalise(CamelIMAPXStream *is)
{
	g_free(is->buf);
	if (is->source)
		camel_object_unref((CamelObject *)is->source);
}

CamelType
camel_imapx_stream_get_type (void)
{
	static CamelType camel_imapx_stream_type = CAMEL_INVALID_TYPE;

	if (camel_imapx_stream_type == CAMEL_INVALID_TYPE) {
		camel_imapx_stream_type = camel_type_register( camel_stream_get_type(),
							    "CamelIMAPXStream",
							    sizeof( CamelIMAPXStream ),
							    sizeof( CamelIMAPXStreamClass ),
							    (CamelObjectClassInitFunc) camel_imapx_stream_class_init,
							    NULL,
							    (CamelObjectInitFunc) camel_imapx_stream_init,
							    (CamelObjectFinalizeFunc) camel_imapx_stream_finalise );
	}

	return camel_imapx_stream_type;
}

/**
 * camel_imapx_stream_new:
 *
 * Returns a NULL stream.  A null stream is always at eof, and
 * always returns success for all reads and writes.
 *
 * Return value: the stream
 **/
CamelStream *
camel_imapx_stream_new(CamelStream *source)
{
	CamelIMAPXStream *is;

	is = (CamelIMAPXStream *)camel_object_new(camel_imapx_stream_get_type ());
	camel_object_ref((CamelObject *)source);
	is->source = source;

	return (CamelStream *)is;
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
			if (stream_fill(is) == IMAPX_TOK_ERROR)
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
camel_imapx_stream_atom(CamelIMAPXStream *is, guchar **data, guint *lenp, CamelException *ex)
{
	guchar *p, c;

	/* this is only 'approximate' atom */
	switch (camel_imapx_stream_token(is, data, lenp, ex)) {
	case IMAPX_TOK_TOKEN:
		p = *data;
		while ((c = *p))
			*p++ = toupper(c);
	case IMAPX_TOK_INT:
		return 0;
	case IMAPX_TOK_ERROR:
		return IMAPX_TOK_ERROR;
	default:
		camel_exception_set (ex, 1, "expecting atom");
		printf("expecting atom!\n");
		return IMAPX_TOK_PROTOCOL;
	}
}

/* gets an atom, a quoted_string, or a literal */
gint
camel_imapx_stream_astring(CamelIMAPXStream *is, guchar **data, CamelException *ex)
{
	guchar *p, *start;
	guint len, inlen;
	gint ret;

	switch (camel_imapx_stream_token(is, data, &len, ex)) {
	case IMAPX_TOK_TOKEN:
	case IMAPX_TOK_INT:
	case IMAPX_TOK_STRING:
		return 0;
	case IMAPX_TOK_LITERAL:
		/* FIXME: just grow buffer */
		if (len >= CAMEL_IMAPX_STREAM_TOKEN) {
			camel_exception_set (ex, 1, "astring: literal too long");
			printf("astring too long\n");
			return IMAPX_TOK_PROTOCOL;
		}
		p = is->tokenptr;
		camel_imapx_stream_set_literal(is, len);
		do {
			ret = camel_imapx_stream_getl(is, &start, &inlen);
			if (ret < 0)
				return ret;
			memcpy(p, start, inlen);
			p += inlen;
		} while (ret > 0);
		*data = is->tokenptr;
		return 0;
	case IMAPX_TOK_ERROR:
		/* wont get unless no exception hanlder*/
		return IMAPX_TOK_ERROR;
	default:
		camel_exception_set (ex, 1, "expecting astring");
		printf("expecting astring!\n");
		return IMAPX_TOK_PROTOCOL;
	}
}

/* check for NIL or (small) quoted_string or literal */
gint
camel_imapx_stream_nstring(CamelIMAPXStream *is, guchar **data, CamelException *ex)
{
	guchar *p, *start;
	guint len, inlen;
	gint ret;

	switch (camel_imapx_stream_token(is, data, &len, ex)) {
	case IMAPX_TOK_STRING:
		return 0;
	case IMAPX_TOK_LITERAL:
		/* FIXME: just grow buffer */
		if (len >= CAMEL_IMAPX_STREAM_TOKEN) {
			camel_exception_set (ex, 1, "nstring: literal too long");
			return IMAPX_TOK_PROTOCOL;
		}
		p = is->tokenptr;
		camel_imapx_stream_set_literal(is, len);
		do {
			ret = camel_imapx_stream_getl(is, &start, &inlen);
			if (ret < 0)
				return ret;
			memcpy(p, start, inlen);
			p += inlen;
		} while (ret > 0);
		*data = is->tokenptr;
		return 0;
	case IMAPX_TOK_TOKEN:
		p = *data;
		if (toupper(p[0]) == 'N' && toupper(p[1]) == 'I' && toupper(p[2]) == 'L' && p[3] == 0) {
			*data = NULL;
			return 0;
		}
	default:
		camel_exception_set (ex, 1, "expecting nstring");
		return IMAPX_TOK_PROTOCOL;
	case IMAPX_TOK_ERROR:
		/* we'll never get this unless there are no exception  handlers anyway */
		return IMAPX_TOK_ERROR;

	}
}

/* parse an nstring as a stream */
gint
camel_imapx_stream_nstring_stream(CamelIMAPXStream *is, CamelStream **stream, CamelException *ex)
/* throws IO,PARSE exception */
{
	guchar *token;
	guint len;
	gint ret = 0;
	CamelStream * mem = NULL;

	*stream = NULL;

	switch (camel_imapx_stream_token(is, &token, &len, ex)) {
		case IMAPX_TOK_STRING:
			mem = camel_stream_mem_new_with_buffer((gchar *)token, len);
			*stream = mem;
			break;
		case IMAPX_TOK_LITERAL:
			/* if len is big, we could automatically use a file backing */
			camel_imapx_stream_set_literal(is, len);
			mem = camel_stream_mem_new();
			if (camel_stream_write_to_stream((CamelStream *)is, mem) == -1) {
				camel_exception_setv (ex, 1, "nstring: io error: %s", strerror(errno));
				camel_object_unref((CamelObject *)mem);
				ret = -1;
				break;
			}
			camel_stream_reset(mem);
			*stream = mem;
			break;
		case IMAPX_TOK_TOKEN:
			if (toupper(token[0]) == 'N' && toupper(token[1]) == 'I' && toupper(token[2]) == 'L' && token[3] == 0) {
				*stream = NULL;
				break;
			}
		default:
			ret = -1;
			camel_exception_set (ex, 1, "nstring: token not string");
	}

	return ret;
}

guint32
camel_imapx_stream_number(CamelIMAPXStream *is, CamelException *ex)
{
	guchar *token;
	guint len;

	if (camel_imapx_stream_token(is, &token, &len, ex) != IMAPX_TOK_INT) {
		camel_exception_set (ex, 1, "expecting number");
		return 0;
	}

	return strtoul((gchar *)token, 0, 10);
}

gint
camel_imapx_stream_text(CamelIMAPXStream *is, guchar **text, CamelException *ex)
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
			camel_exception_setv (ex, 1, "io error: %s", strerror(errno));
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
camel_imapx_stream_token(CamelIMAPXStream *is, guchar **data, guint *len, CamelException *ex)
{
	register guchar c, *p, *o, *oe;
	guchar *e;
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
			if (stream_fill(is) == IMAPX_TOK_ERROR)
				goto io_error;
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
						if (stream_fill(is) == IMAPX_TOK_ERROR)
							goto io_error;
						p = is->ptr;
						e = is->end;
					}
				} else {
					if (isdigit(c))
						printf("Protocol error: literal too big\n");
					else
						printf("Protocol error: literal contains invalid gchar %02x '%c'\n", c, isprint(c)?c:c);
					goto protocol_error;
				}
			}
			is->ptr = p;
			if (stream_fill(is) == IMAPX_TOK_ERROR)
				goto io_error;
			p = is->ptr;
			e = is->end;
		}
	} else if (c == '"') {
		o = is->tokenptr;
		oe = is->tokenptr + CAMEL_IMAPX_STREAM_TOKEN - 1;
		while (1) {
			while (p < e) {
				c = *p++;
				if (c == '\\') {
					while (p >= e) {
						is->ptr = p;
						if (stream_fill(is) == IMAPX_TOK_ERROR)
							goto io_error;
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

				if (c == '\n' || c == '\r' || o>=oe) {
					if (o >= oe)
						printf("Protocol error: string too long\n");
					else
						printf("Protocol error: truncated string\n");
					goto protocol_error;
				} else {
					*o++ = c;
				}
			}
			is->ptr = p;
			if (stream_fill(is) == IMAPX_TOK_ERROR)
				goto io_error;
			p = is->ptr;
			e = is->end;
		}
	} else {
		o = is->tokenptr;
		oe = is->tokenptr + CAMEL_IMAPX_STREAM_TOKEN - 1;
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
				} else if (o < oe) {
					digits &= isdigit(c);
					*o++ = c;
				} else {
					printf("Protocol error: token too long\n");
					goto protocol_error;
				}
			}
			is->ptr = p;
			if (stream_fill(is) == IMAPX_TOK_ERROR)
				goto io_error;
			p = is->ptr;
			e = is->end;
		}
	}

	/* Had an i/o erorr */
io_error:
	camel_exception_set (ex, 1, "io error");
	return IMAPX_TOK_ERROR;

	/* Protocol error, skip until next lf? */
protocol_error:
	printf("Got protocol error\n");

	if (c == '\n')
		is->ptr = p-1;
	else
		is->ptr = p;

	camel_exception_set (ex, 1, "protocol error");
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
		max = stream_fill(is);
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
			max = stream_fill(is);
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
camel_imapx_stream_skip(CamelIMAPXStream *is, CamelException *ex)
{
	gint tok;
	guchar *token;
	guint len;

	do {
		tok = camel_imapx_stream_token(is, &token, &len, ex);
		if (tok == IMAPX_TOK_LITERAL) {
			camel_imapx_stream_set_literal(is, len);
			while ((tok = camel_imapx_stream_getl(is, &token, &len)) > 0) {
				printf("Skip literal data '%.*s'\n", (gint)len, token);
			}
		}
	} while (tok != '\n' && tok >= 0);

	if (tok < 0)
		return -1;

	return 0;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Camel
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
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
 *  Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef CAMEL_IMAP4_STREAM_H
#define CAMEL_IMAP4_STREAM_H

#include <camel/camel-stream.h>

#define CAMEL_TYPE_IMAP4_STREAM     (camel_imap4_stream_get_type ())
#define CAMEL_IMAP4_STREAM(obj)     (CAMEL_CHECK_CAST ((obj), CAMEL_TYPE_IMAP4_STREAM, CamelIMAP4Stream))
#define CAMEL_IMAP4_STREAM_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_TYPE_IMAP4_STREAM, CamelIMAP4StreamClass))
#define CAMEL_IS_IMAP4_STREAM(o)    (CAMEL_CHECK_TYPE((o), CAMEL_TYPE_IMAP4_STREAM))

#define IMAP4_READ_PRELEN   128
#define IMAP4_READ_BUFLEN   4096

G_BEGIN_DECLS

typedef struct _CamelIMAP4Stream CamelIMAP4Stream;
typedef struct _CamelIMAP4StreamClass CamelIMAP4StreamClass;

enum {
	CAMEL_IMAP4_TOKEN_NO_DATA       = -8,
	CAMEL_IMAP4_TOKEN_ERROR         = -7,
	CAMEL_IMAP4_TOKEN_NIL           = -6,
	CAMEL_IMAP4_TOKEN_ATOM          = -5,
	CAMEL_IMAP4_TOKEN_FLAG          = -4,
	CAMEL_IMAP4_TOKEN_NUMBER        = -3,
	CAMEL_IMAP4_TOKEN_QSTRING       = -2,
	CAMEL_IMAP4_TOKEN_LITERAL       = -1,
	/* CAMEL_IMAP4_TOKEN_CHAR would just be the gchar we got */
	CAMEL_IMAP4_TOKEN_EOLN          = '\n',
	CAMEL_IMAP4_TOKEN_LPAREN        = '(',
	CAMEL_IMAP4_TOKEN_RPAREN        = ')',
	CAMEL_IMAP4_TOKEN_ASTERISK      = '*',
	CAMEL_IMAP4_TOKEN_PLUS          = '+',
	CAMEL_IMAP4_TOKEN_LBRACKET      = '[',
	CAMEL_IMAP4_TOKEN_RBRACKET      = ']',
};

typedef struct _camel_imap4_token_t {
	gint token;
	union {
		gchar *atom;
		gchar *flag;
		gchar *qstring;
		gsize literal;
		guint32 number;
	} v;
} camel_imap4_token_t;

enum {
	CAMEL_IMAP4_STREAM_MODE_TOKEN   = 0,
	CAMEL_IMAP4_STREAM_MODE_LITERAL = 1,
};

struct _CamelIMAP4Stream {
	CamelStream parent_object;

	CamelStream *stream;

	guint disconnected:1;  /* disconnected state */
	guint have_unget:1;    /* have an unget token */
	guint mode:1;          /* TOKEN vs LITERAL */
	guint eol:1;           /* end-of-literal */

	gsize literal;

	/* i/o buffers */
	guchar realbuf[IMAP4_READ_PRELEN + IMAP4_READ_BUFLEN + 1];
	guchar *inbuf;
	guchar *inptr;
	guchar *inend;

	/* token buffers */
	guchar *tokenbuf;
	guchar *tokenptr;
	guint tokenleft;

	camel_imap4_token_t unget;
};

struct _CamelIMAP4StreamClass {
	CamelStreamClass parent_class;

	/* Virtual methods */
};

/* Standard Camel function */
CamelType camel_imap4_stream_get_type (void);

CamelStream *camel_imap4_stream_new (CamelStream *stream);

gint camel_imap4_stream_next_token (CamelIMAP4Stream *stream, camel_imap4_token_t *token);
gint camel_imap4_stream_unget_token (CamelIMAP4Stream *stream, camel_imap4_token_t *token);

gint camel_imap4_stream_line (CamelIMAP4Stream *stream, guchar **line, gsize *len);
gint camel_imap4_stream_literal (CamelIMAP4Stream *stream, guchar **literal, gsize *len);

G_END_DECLS

#endif /* CAMEL_IMAP4_STREAM_H */

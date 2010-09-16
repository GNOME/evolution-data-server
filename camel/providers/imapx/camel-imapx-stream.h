/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#ifndef CAMEL_IMAPX_STREAM_H
#define CAMEL_IMAPX_STREAM_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAPX_STREAM \
	(camel_imapx_stream_get_type ())
#define CAMEL_IMAPX_STREAM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAPX_STREAM, CamelIMAPXStream))
#define CAMEL_IMAPX_STREAM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAPX_STREAM, CamelIMAPXStreamClass))
#define CAMEL_IS_IMAPX_STREAM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAPX_STREAM))
#define CAMEL_IS_IMAPX_STREAM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAPX_STREAM))
#define CAMEL_IMAPX_STREAM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAPX_STREAM, CamelIMAPXStreamClass))

#define CAMEL_IMAPX_ERROR \
	(camel_imapx_error_quark ())

G_BEGIN_DECLS

typedef struct _CamelIMAPXStream CamelIMAPXStream;
typedef struct _CamelIMAPXStreamClass CamelIMAPXStreamClass;

typedef enum {
	IMAPX_TOK_PROTOCOL = -2,
	IMAPX_TOK_ERROR = -1,
	IMAPX_TOK_TOKEN = 256,
	IMAPX_TOK_STRING,
	IMAPX_TOK_INT,
	IMAPX_TOK_LITERAL,
} camel_imapx_token_t;

struct _CamelIMAPXStream {
	CamelStream parent;

	CamelStream *source;

	/*int state;*/
	guchar *buf, *ptr, *end;
	guint literal;

	guint unget;
	camel_imapx_token_t unget_tok;
	guchar *unget_token;
	guint unget_len;

	guchar *tokenbuf;
	guint bufsize;
};

struct _CamelIMAPXStreamClass {
	CamelStreamClass parent_class;
};

GType		camel_imapx_stream_get_type	(void);
GQuark		camel_imapx_error_quark		(void) G_GNUC_CONST;
CamelStream *	camel_imapx_stream_new		(CamelStream *source);
gint		camel_imapx_stream_buffered	(CamelIMAPXStream *is);

/* throws IO,PARSE exception */
camel_imapx_token_t
		camel_imapx_stream_token	(CamelIMAPXStream *is,
						 guchar **start,
						 guint *len,
						 GError **error);

void		camel_imapx_stream_ungettoken	(CamelIMAPXStream *is,
						 camel_imapx_token_t tok,
						 guchar *token,
						 guint len);
void		camel_imapx_stream_set_literal	(CamelIMAPXStream *is,
						 guint literal);
gint		camel_imapx_stream_gets		(CamelIMAPXStream *is,
						 guchar **start,
						 guint *len);
gint		 camel_imapx_stream_getl	(CamelIMAPXStream *is,
						 guchar **start,
						 guint *len);

/* all throw IO,PARSE exceptions */

/* gets an atom, upper-cases */
gint		camel_imapx_stream_atom		(CamelIMAPXStream *is,
						 guchar **start,
						 guint *len,
						 GError **error);
/* gets an atom or string */
gint		camel_imapx_stream_astring	(CamelIMAPXStream *is,
						 guchar **start,
						 GError **error);
/* gets a NIL or a string, start==NULL if NIL */
gint		camel_imapx_stream_nstring	(CamelIMAPXStream *is,
						 guchar **start,
						 GError **error);
/* gets a NIL or string into a stream, stream==NULL if NIL */
gint		camel_imapx_stream_nstring_stream
						(CamelIMAPXStream *is,
						 CamelStream **stream,
						 GError **error);
/* gets 'text' */
gint		camel_imapx_stream_text		(CamelIMAPXStream *is,
						 guchar **text,
						 GError **error);

/* gets a 'number' */
guint64		 camel_imapx_stream_number	(CamelIMAPXStream *is,
						 GError **error);

/* skips the rest of a line, including literals, etc */
gint		camel_imapx_stream_skip		(CamelIMAPXStream *is,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_IMAPX_STREAM_H */

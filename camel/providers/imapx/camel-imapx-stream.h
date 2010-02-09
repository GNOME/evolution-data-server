/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _CAMEL_IMAPX_STREAM_H
#define _CAMEL_IMAPX_STREAM_H

#include <camel/camel-stream.h>

#define CAMEL_IMAPX_STREAM(obj)         CAMEL_CHECK_CAST (obj, camel_imapx_stream_get_type (), CamelIMAPXStream)
#define CAMEL_IMAPX_STREAM_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_imapx_stream_get_type (), CamelIMAPXStreamClass)
#define CAMEL_IS_IMAPX_STREAM(obj)      CAMEL_CHECK_TYPE (obj, camel_imapx_stream_get_type ())

typedef struct _CamelIMAPXStreamClass CamelIMAPXStreamClass;
typedef struct _CamelIMAPXStream CamelIMAPXStream;

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

	guchar *tokenbuf, *tokenptr, *tokenend;
};

struct _CamelIMAPXStreamClass {
	CamelStreamClass parent_class;
};

CamelType	 camel_imapx_stream_get_type	(void);

CamelStream     *camel_imapx_stream_new		(CamelStream *source);

gint		 camel_imapx_stream_buffered	(CamelIMAPXStream *is);

camel_imapx_token_t camel_imapx_stream_token	(CamelIMAPXStream *is, guchar **start, guint *len, CamelException *ex); /* throws IO,PARSE exception */
void		 camel_imapx_stream_ungettoken	(CamelIMAPXStream *is, camel_imapx_token_t tok, guchar *token, guint len);

void		 camel_imapx_stream_set_literal	(CamelIMAPXStream *is, guint literal);
gint		 camel_imapx_stream_gets		(CamelIMAPXStream *is, guchar **start, guint *len);
gint		 camel_imapx_stream_getl		(CamelIMAPXStream *is, guchar **start, guint *len);

/* all throw IO,PARSE exceptions */

/* gets an atom, upper-cases */
gint		 camel_imapx_stream_atom		(CamelIMAPXStream *is, guchar **start, guint *len, CamelException *ex);
/* gets an atom or string */
gint		 camel_imapx_stream_astring	(CamelIMAPXStream *is, guchar **start, CamelException *ex);
/* gets a NIL or a string, start==NULL if NIL */
gint		 camel_imapx_stream_nstring	(CamelIMAPXStream *is, guchar **start, CamelException *ex);
/* gets a NIL or string into a stream, stream==NULL if NIL */
gint		 camel_imapx_stream_nstring_stream(CamelIMAPXStream *is, CamelStream **stream, CamelException *ex);
/* gets 'text' */
gint		 camel_imapx_stream_text		(CamelIMAPXStream *is, guchar **text, CamelException *ex);

/* gets a 'number' */
guint32		 camel_imapx_stream_number(CamelIMAPXStream *is, CamelException *ex);

/* skips the rest of a line, including literals, etc */
gint camel_imapx_stream_skip(CamelIMAPXStream *is, CamelException *ex);

#endif /* _CAMEL_IMAPX_STREAM_H */

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
	IMAP_TOK_PROTOCOL = -2,
	IMAP_TOK_ERROR = -1,
	IMAP_TOK_TOKEN = 256,
	IMAP_TOK_STRING,
	IMAP_TOK_INT,
	IMAP_TOK_LITERAL,
} camel_imapx_token_t;

struct _CamelIMAPXStream {
	CamelStream parent;

	CamelStream *source;

	/*int state;*/
	unsigned char *buf, *ptr, *end;
	unsigned int literal;

	unsigned int unget;
	camel_imapx_token_t unget_tok;
	unsigned char *unget_token;
	unsigned int unget_len;

	unsigned char *tokenbuf, *tokenptr, *tokenend;
};

struct _CamelIMAPXStreamClass {
	CamelStreamClass parent_class;
};

CamelType	 camel_imapx_stream_get_type	(void);

CamelStream     *camel_imapx_stream_new		(CamelStream *source);

int 		 camel_imapx_stream_buffered	(CamelIMAPXStream *is);

camel_imapx_token_t camel_imapx_stream_token	(CamelIMAPXStream *is, unsigned char **start, unsigned int *len); /* throws IO,PARSE exception */
void		 camel_imapx_stream_ungettoken	(CamelIMAPXStream *is, camel_imapx_token_t tok, unsigned char *token, unsigned int len);

void		 camel_imapx_stream_set_literal	(CamelIMAPXStream *is, unsigned int literal);
int 		 camel_imapx_stream_gets		(CamelIMAPXStream *is, unsigned char **start, unsigned int *len);
int 		 camel_imapx_stream_getl		(CamelIMAPXStream *is, unsigned char **start, unsigned int *len);

/* all throw IO,PARSE exceptions */

/* gets an atom, upper-cases */
int		 camel_imapx_stream_atom		(CamelIMAPXStream *is, unsigned char **start, unsigned int *len);
/* gets an atom or string */
int		 camel_imapx_stream_astring	(CamelIMAPXStream *is, unsigned char **start);
/* gets a NIL or a string, start==NULL if NIL */
int		 camel_imapx_stream_nstring	(CamelIMAPXStream *is, unsigned char **start);
/* gets a NIL or string into a stream, stream==NULL if NIL */
int		 camel_imapx_stream_nstring_stream(CamelIMAPXStream *is, CamelStream **stream);
/* gets 'text' */
int		 camel_imapx_stream_text		(CamelIMAPXStream *is, unsigned char **text);

/* gets a 'number' */
guint32		 camel_imapx_stream_number(CamelIMAPXStream *is);

/* skips the rest of a line, including literals, etc */
int camel_imapx_stream_skip(CamelIMAPXStream *is);

#endif /* ! _CAMEL_IMAPX_STREAM_H */

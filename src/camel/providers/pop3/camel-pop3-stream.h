/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

/* This is *identical* to the camel-nntp-stream, so should probably
 * work out a way to merge them */

#ifndef CAMEL_POP3_STREAM_H
#define CAMEL_POP3_STREAM_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_POP3_STREAM \
	(camel_pop3_stream_get_type ())
#define CAMEL_POP3_STREAM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_POP3_STREAM, CamelPOP3Stream))
#define CAMEL_POP3_STREAM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_POP3_STREAM, CamelPOP3StreamClass))
#define CAMEL_IS_POP3_STREAM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_POP3_STREAM))
#define CAMEL_IS_POP3_STREAM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_POP3_STREAM))
#define CAMEL_POP3_STREAM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_POP3_STREAM, CamelPOP3StreamClass))

G_BEGIN_DECLS

typedef struct _CamelPOP3Stream CamelPOP3Stream;
typedef struct _CamelPOP3StreamClass CamelPOP3StreamClass;

typedef enum {
	CAMEL_POP3_STREAM_LINE,
	CAMEL_POP3_STREAM_DATA,
	CAMEL_POP3_STREAM_EOD	/* end of data, acts as if end of stream */
} camel_pop3_stream_mode_t;

struct _CamelPOP3Stream {
	CamelStream parent;

	CamelStream *source;

	camel_pop3_stream_mode_t mode;
	gint state;

	guchar *buf, *ptr, *end;
	guchar *linebuf, *lineptr, *lineend;
};

struct _CamelPOP3StreamClass {
	CamelStreamClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_pop3_stream_get_type	(void);
CamelStream *	camel_pop3_stream_new		(CamelStream *source);
void		camel_pop3_stream_set_mode	(CamelPOP3Stream *is,
						 camel_pop3_stream_mode_t mode);
gint		camel_pop3_stream_line		(CamelPOP3Stream *is,
						 guchar **data,
						 guint *len,
						 GCancellable *cancellable,
						 GError **error);
gint		camel_pop3_stream_getd		(CamelPOP3Stream *is,
						 guchar **start,
						 guint *len,
						 GCancellable *cancellable,
						 GError **error);
void		camel_pop3_stream_discard_cache	(CamelPOP3Stream *is);

G_END_DECLS

#endif /* CAMEL_POP3_STREAM_H */

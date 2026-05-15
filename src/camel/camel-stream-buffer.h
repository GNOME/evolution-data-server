/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_STREAM_BUFFER_H
#define CAMEL_STREAM_BUFFER_H

#include <stdio.h>
#include <camel/camel-stream.h>

/* Standard GObject macros */
#define CAMEL_TYPE_STREAM_BUFFER \
	(camel_stream_buffer_get_type ())
#define CAMEL_STREAM_BUFFER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_STREAM_BUFFER, CamelStreamBuffer))
#define CAMEL_STREAM_BUFFER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_STREAM_BUFFER, CamelStreamBufferClass))
#define CAMEL_IS_STREAM_BUFFER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_STREAM_BUFFER))
#define CAMEL_IS_STREAM_BUFFER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_STREAM_BUFFER))
#define CAMEL_STREAM_BUFFER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_STREAM_BUFFER, CamelStreamBufferClass))

G_BEGIN_DECLS

typedef struct _CamelStreamBuffer CamelStreamBuffer;
typedef struct _CamelStreamBufferClass CamelStreamBufferClass;
typedef struct _CamelStreamBufferPrivate CamelStreamBufferPrivate;

typedef enum {
	CAMEL_STREAM_BUFFER_BUFFER = 0,
	CAMEL_STREAM_BUFFER_NONE,
	CAMEL_STREAM_BUFFER_READ = 0x00,
	CAMEL_STREAM_BUFFER_WRITE = 0x80,
	CAMEL_STREAM_BUFFER_MODE = 0x80
} CamelStreamBufferMode;

struct _CamelStreamBuffer {
	CamelStream parent;
	CamelStreamBufferPrivate *priv;
};

struct _CamelStreamBufferClass {
	CamelStreamClass parent_class;

	void		(*init)		(CamelStreamBuffer *stream_buffer,
					 CamelStream *stream,
					 CamelStreamBufferMode mode);
	void		(*init_vbuf)	(CamelStreamBuffer *stream_buffer,
					 CamelStream *stream,
					 CamelStreamBufferMode mode,
					 gchar *buf,
					 guint32 size);

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_stream_buffer_get_type	(void);
CamelStream *	camel_stream_buffer_new		(CamelStream *stream,
						 CamelStreamBufferMode mode);
gint		camel_stream_buffer_gets	(CamelStreamBuffer *sbf,
						 gchar *buf,
						 guint max,
						 GCancellable *cancellable,
						 GError **error);
gchar *		camel_stream_buffer_read_line	(CamelStreamBuffer *sbf,
						 GCancellable *cancellable,
						 GError **error);
void		camel_stream_buffer_discard_cache
						(CamelStreamBuffer *sbf);

G_END_DECLS

#endif /* CAMEL_STREAM_BUFFER_H */

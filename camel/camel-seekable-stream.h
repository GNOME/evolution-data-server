/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream-fs.h :stream based on unix filesystem */

/*
 * Author:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SEEKABLE_STREAM_H
#define CAMEL_SEEKABLE_STREAM_H

#include <sys/types.h>
#include <unistd.h>
#include <camel/camel-stream.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SEEKABLE_STREAM \
	(camel_seekable_stream_get_type ())
#define CAMEL_SEEKABLE_STREAM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SEEKABLE_STREAM, CamelSeekableStream))
#define CAMEL_SEEKABLE_STREAM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SEEKABLE_STREAM, CamelSeekableStreamClass))
#define CAMEL_IS_SEEKABLE_STREAM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SEEKABLE_STREAM))
#define CAMEL_IS_SEEKABLE_STREAM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SEEKABLE_STREAM))
#define CAMEL_SEEKABLE_STREAM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SEEKABLE_STREAM, CamelSeekableStreamClass))

G_BEGIN_DECLS

typedef struct _CamelSeekableStream CamelSeekableStream;
typedef struct _CamelSeekableStreamClass CamelSeekableStreamClass;

typedef enum {
	CAMEL_STREAM_SET = SEEK_SET,
	CAMEL_STREAM_CUR = SEEK_CUR,
	CAMEL_STREAM_END = SEEK_END
} CamelStreamSeekPolicy;

#define CAMEL_STREAM_UNBOUND (~0)

struct _CamelSeekableStream {
	CamelStream parent;

	off_t position;		/* current postion in the stream */
	off_t bound_start;	/* first valid position */
	off_t bound_end;	/* first invalid position */
};

struct _CamelSeekableStreamClass {
	CamelStreamClass parent_class;

	off_t		(*seek)			(CamelSeekableStream *stream,
						 off_t offset,
						 CamelStreamSeekPolicy policy,
						 GError **error);
	off_t		(*tell)			(CamelSeekableStream *stream);
	gint		(*set_bounds)		(CamelSeekableStream *stream,
						 off_t start,
						 off_t end,
						 GError **error);
};

GType		camel_seekable_stream_get_type	(void);
off_t		camel_seekable_stream_seek	(CamelSeekableStream *stream,
						 off_t offset,
						 CamelStreamSeekPolicy policy,
						 GError **error);
off_t		camel_seekable_stream_tell	(CamelSeekableStream *stream);
gint		camel_seekable_stream_set_bounds(CamelSeekableStream *stream,
						 off_t start,
						 off_t end,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_SEEKABLE_STREAM_H */

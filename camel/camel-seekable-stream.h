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

#ifndef CAMEL_SEEKABLE_STREAM_H
#define CAMEL_SEEKABLE_STREAM_H 1

#include <sys/types.h>
#include <unistd.h>
#include <camel/camel-stream.h>

#define CAMEL_SEEKABLE_STREAM_TYPE     (camel_seekable_stream_get_type ())
#define CAMEL_SEEKABLE_STREAM(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_SEEKABLE_STREAM_TYPE, CamelSeekableStream))
#define CAMEL_SEEKABLE_STREAM_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_SEEKABLE_STREAM_TYPE, CamelSeekableStreamClass))
#define CAMEL_IS_SEEKABLE_STREAM(o)    (CAMEL_CHECK_TYPE((o), CAMEL_SEEKABLE_STREAM_TYPE))

G_BEGIN_DECLS

typedef enum {
	CAMEL_STREAM_SET = SEEK_SET,
	CAMEL_STREAM_CUR = SEEK_CUR,
	CAMEL_STREAM_END = SEEK_END
} CamelStreamSeekPolicy;

#define CAMEL_STREAM_UNBOUND (~0)

struct _CamelSeekableStream {
	CamelStream parent_object;

	off_t position;		/* current postion in the stream */
	off_t bound_start;	/* first valid position */
	off_t bound_end;	/* first invalid position */
};

typedef struct {
	CamelStreamClass parent_class;

	/* Virtual methods */
	off_t (*seek)       (CamelSeekableStream *stream, off_t offset,
			     CamelStreamSeekPolicy policy);
	off_t (*tell)	    (CamelSeekableStream *stream);
	gint  (*set_bounds)  (CamelSeekableStream *stream,
			     off_t start, off_t end);
} CamelSeekableStreamClass;

/* Standard Camel function */
CamelType camel_seekable_stream_get_type (void);

/* public methods */
off_t    camel_seekable_stream_seek            (CamelSeekableStream *stream, off_t offset,
						CamelStreamSeekPolicy policy);
off_t	 camel_seekable_stream_tell	       (CamelSeekableStream *stream);
gint	 camel_seekable_stream_set_bounds      (CamelSeekableStream *stream, off_t start, off_t end);

G_END_DECLS

#endif /* CAMEL_SEEKABLE_STREAM_H */

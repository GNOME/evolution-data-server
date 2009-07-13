/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream-mem.h: stream based on memory buffer */

/*
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *	    Michael Zucchi <notzed@ximian.com>
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

#ifndef CAMEL_STREAM_MEM_H
#define CAMEL_STREAM_MEM_H 1

#include <sys/types.h>
#include <camel/camel-seekable-stream.h>

#define CAMEL_STREAM_MEM_TYPE     (camel_stream_mem_get_type ())
#define CAMEL_STREAM_MEM(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_STREAM_MEM_TYPE, CamelStreamMem))
#define CAMEL_STREAM_MEM_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_STREAM_MEM_TYPE, CamelStreamMemClass))
#define CAMEL_IS_STREAM_MEM(o)    (CAMEL_CHECK_TYPE((o), CAMEL_STREAM_MEM_TYPE))

G_BEGIN_DECLS

typedef struct _CamelStreamMemClass CamelStreamMemClass;

struct _CamelStreamMem {
	CamelSeekableStream parent_object;

	guint owner:1;	/* do we own the buffer? */
	guint secure:1;	/* do we clear the buffer on finalise (if we own it) */
	GByteArray *buffer;
};

struct _CamelStreamMemClass {
	CamelSeekableStreamClass parent_class;

	/* Virtual methods */
};

/* Standard Camel function */
CamelType camel_stream_mem_get_type (void);

/* public methods */
CamelStream *camel_stream_mem_new(void);
CamelStream *camel_stream_mem_new_with_byte_array(GByteArray *buffer);
CamelStream *camel_stream_mem_new_with_buffer(const gchar *buffer, gsize len);

/* 'secure' data, currently just clears memory on finalise */
void camel_stream_mem_set_secure(CamelStreamMem *mem);

/* these are really only here for implementing classes */
void camel_stream_mem_set_byte_array(CamelStreamMem *mem, GByteArray *buffer);
void camel_stream_mem_set_buffer(CamelStreamMem *mem, const gchar *buffer, gsize len);

G_END_DECLS

#endif /* CAMEL_STREAM_MEM_H */

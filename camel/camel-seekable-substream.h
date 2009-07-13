/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-seekable-substream.h: stream that piggybacks on another stream */

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

#ifndef CAMEL_SEEKABLE_SUBSTREAM_H
#define CAMEL_SEEKABLE_SUBSTREAM_H 1

#include <camel/camel-seekable-stream.h>

#define CAMEL_SEEKABLE_SUBSTREAM_TYPE       (camel_seekable_substream_get_type ())
#define CAMEL_SEEKABLE_SUBSTREAM(obj)       (CAMEL_CHECK_CAST((obj), CAMEL_SEEKABLE_SUBSTREAM_TYPE, CamelSeekableSubstream))
#define CAMEL_SEEKABLE_SUBSTREAM_CLASS(k)   (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_SEEKABLE_SUBSTREAM_TYPE, CamelSeekableSubstreamClass))
#define CAMEL_IS_SEEKABLE_SUBSTREAM(o)      (CAMEL_CHECK_TYPE((o), CAMEL_SEEKABLE_SUBSTREAM_TYPE))

G_BEGIN_DECLS

struct _CamelSeekableSubstream {
	CamelSeekableStream parent_object;

	/*  --**-- Private fields --**--  */
	CamelSeekableStream *parent_stream;
};

typedef struct {
	CamelSeekableStreamClass parent_class;

} CamelSeekableSubstreamClass;

/* Standard Camel function */
CamelType camel_seekable_substream_get_type (void);

/* public methods */

/* obtain a new seekable substream */
CamelStream *camel_seekable_substream_new(CamelSeekableStream *parent_stream, off_t start, off_t end);

G_END_DECLS

#endif /* CAMEL_SEEKABLE_SUBSTREAM_H */

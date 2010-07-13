/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-debug.h"
#include "camel-seekable-stream.h"

G_DEFINE_TYPE (CamelSeekableStream, camel_seekable_stream, CAMEL_TYPE_STREAM)

static gint
seekable_stream_reset (CamelStream *stream,
                       GError **error)
{
	CamelSeekableStream *seekable_stream;

	seekable_stream = CAMEL_SEEKABLE_STREAM (stream);

	return camel_seekable_stream_seek (
		seekable_stream, seekable_stream->bound_start,
		CAMEL_STREAM_SET, error);
}

static goffset
seekable_stream_tell (CamelSeekableStream *stream)
{
	return stream->position;
}

static gint
seekable_stream_set_bounds (CamelSeekableStream *stream,
                            goffset start,
                            goffset end,
                            GError **error)
{
	/* store the bounds */
	stream->bound_start = start;
	stream->bound_end = end;

	if (start > stream->position)
		return camel_seekable_stream_seek (
			stream, start, CAMEL_STREAM_SET, error);

	return 0;
}

static void
camel_seekable_stream_class_init (CamelSeekableStreamClass *class)
{
	CamelStreamClass *stream_class;

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->reset = seekable_stream_reset;

	class->tell = seekable_stream_tell;
	class->set_bounds = seekable_stream_set_bounds;
}

static void
camel_seekable_stream_init (CamelSeekableStream *stream)
{
	stream->bound_start = 0;
	stream->bound_end = CAMEL_STREAM_UNBOUND;
}

/**
 * camel_seekable_stream_seek:
 * @stream: a #CamelStream object
 * @offset: offset value
 * @policy: what to do with the offset
 * @error: return location for a #GError, or %NULL
 *
 * Seek to the specified position in @stream.
 *
 * If @policy is #CAMEL_STREAM_SET, seeks to @offset.
 *
 * If @policy is #CAMEL_STREAM_CUR, seeks to the current position plus
 * @offset.
 *
 * If @policy is #CAMEL_STREAM_END, seeks to the end of the stream plus
 * @offset.
 *
 * Regardless of @policy, the stream's final position will be clamped
 * to the range specified by its lower and upper bounds, and the
 * stream's eos state will be updated.
 *
 * Returns: new position, %-1 if operation failed.
 **/
goffset
camel_seekable_stream_seek (CamelSeekableStream *stream,
                            goffset offset,
                            CamelStreamSeekPolicy policy,
                            GError **error)
{
	CamelSeekableStreamClass *class;
	goffset new_offset;

	g_return_val_if_fail (CAMEL_IS_SEEKABLE_STREAM (stream), -1);

	class = CAMEL_SEEKABLE_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->seek != NULL, -1);

	new_offset = class->seek (stream, offset, policy, error);
	CAMEL_CHECK_GERROR (stream, seek, new_offset >= 0, error);

	return new_offset;
}

/**
 * camel_seekable_stream_tell:
 * @stream: a #CamelSeekableStream object
 *
 * Get the current position of a seekable stream.
 *
 * Returns: the current position of the stream.
 **/
goffset
camel_seekable_stream_tell (CamelSeekableStream *stream)
{
	CamelSeekableStreamClass *class;

	g_return_val_if_fail (CAMEL_IS_SEEKABLE_STREAM (stream), -1);

	class = CAMEL_SEEKABLE_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->tell != NULL, -1);

	return class->tell (stream);
}

/**
 * camel_seekable_stream_set_bounds:
 * @stream: a #CamelSeekableStream object
 * @start: the first valid position
 * @end: the first invalid position, or #CAMEL_STREAM_UNBOUND
 * @error: return location for a #GError, or %NULL
 *
 * Set the range of valid data this stream is allowed to cover.  If
 * there is to be no @end value, then @end should be set to
 * #CAMEL_STREAM_UNBOUND.
 *
 * Returns: %-1 on error.
 **/
gint
camel_seekable_stream_set_bounds (CamelSeekableStream *stream,
                                  goffset start,
                                  goffset end,
                                  GError **error)
{
	CamelSeekableStreamClass *class;
	gint retval;

	g_return_val_if_fail (CAMEL_IS_SEEKABLE_STREAM (stream), -1);
	g_return_val_if_fail (end == CAMEL_STREAM_UNBOUND || end >= start, -1);

	class = CAMEL_SEEKABLE_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->set_bounds != NULL, -1);

	retval = class->set_bounds (stream, start, end, error);
	CAMEL_CHECK_GERROR (stream, set_bounds, retval == 0, error);

	return retval;
}

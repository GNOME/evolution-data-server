/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream-fs.c : file system based stream
 *
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-seekable-substream.h"

G_DEFINE_TYPE (CamelSeekableSubstream, camel_seekable_substream, CAMEL_TYPE_SEEKABLE_STREAM)

static gboolean
seekable_substream_parent_reset (CamelSeekableSubstream *seekable_substream,
                                 CamelSeekableStream *parent)
{
	CamelSeekableStream *seekable_stream = CAMEL_SEEKABLE_STREAM (seekable_substream);

	if (camel_seekable_stream_tell (parent) == seekable_stream->position)
		return TRUE;

	return camel_seekable_stream_seek (
		parent, seekable_stream->position,
		CAMEL_STREAM_SET, NULL) == seekable_stream->position;
}

static void
seekable_substream_dispose (GObject *object)
{
	CamelSeekableSubstream *seekable_substream;

	seekable_substream = CAMEL_SEEKABLE_SUBSTREAM (object);

	if (seekable_substream->parent_stream != NULL) {
		g_object_unref (seekable_substream->parent_stream);
		seekable_substream = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_seekable_substream_parent_class)->dispose (object);
}

static gssize
seekable_substream_read (CamelStream *stream,
                         gchar *buffer,
                         gsize n,
                         GError **error)
{
	CamelSeekableStream *parent;
	CamelSeekableStream *seekable_stream = CAMEL_SEEKABLE_STREAM (stream);
	CamelSeekableSubstream *seekable_substream = CAMEL_SEEKABLE_SUBSTREAM (stream);
	gssize v;

	if (n == 0)
		return 0;

	parent = seekable_substream->parent_stream;

	/* Go to our position in the parent stream. */
	if (!seekable_substream_parent_reset (seekable_substream, parent)) {
		stream->eos = TRUE;
		return 0;
	}

	/* Compute how many bytes should be read. */
	if (seekable_stream->bound_end != CAMEL_STREAM_UNBOUND)
		n = MIN (seekable_stream->bound_end -  seekable_stream->position, n);

	if (n == 0) {
		stream->eos = TRUE;
		return 0;
	}

	v = camel_stream_read (CAMEL_STREAM (parent), buffer, n, error);

	/* ignore <0 - it's an error, let the caller deal */
	if (v > 0)
		seekable_stream->position += v;

	return v;
}

static gssize
seekable_substream_write (CamelStream *stream,
                          const gchar *buffer,
                          gsize n,
                          GError **error)
{
	CamelSeekableStream *parent;
	CamelSeekableStream *seekable_stream = CAMEL_SEEKABLE_STREAM(stream);
	CamelSeekableSubstream *seekable_substream = CAMEL_SEEKABLE_SUBSTREAM(stream);
	gssize v;

	if (n == 0)
		return 0;

	parent = seekable_substream->parent_stream;

	/* Go to our position in the parent stream. */
	if (!seekable_substream_parent_reset (seekable_substream, parent)) {
		stream->eos = TRUE;
		return 0;
	}

	/* Compute how many bytes should be written. */
	if (seekable_stream->bound_end != CAMEL_STREAM_UNBOUND)
		n = MIN (seekable_stream->bound_end -  seekable_stream->position, n);

	if (n == 0) {
		stream->eos = TRUE;
		return 0;
	}

	v = camel_stream_write (CAMEL_STREAM (parent), buffer, n, error);

	/* ignore <0 - it's an error, let the caller deal */
	if (v > 0)
		seekable_stream->position += v;

	return v;

}

static gint
seekable_substream_flush (CamelStream *stream,
                          GError **error)
{
	CamelSeekableSubstream *sus = (CamelSeekableSubstream *)stream;

	return camel_stream_flush (CAMEL_STREAM (sus->parent_stream), error);
}

static gint
seekable_substream_close (CamelStream *stream,
                          GError **error)
{
	/* we dont really want to close the substream ... */
	return 0;
}

static gboolean
seekable_substream_eos (CamelStream *stream)
{
	CamelSeekableSubstream *seekable_substream = CAMEL_SEEKABLE_SUBSTREAM(stream);
	CamelSeekableStream *seekable_stream = CAMEL_SEEKABLE_STREAM(stream);
	CamelSeekableStream *parent;
	gboolean eos;

	if (stream->eos)
		eos = TRUE;
	else {
		parent = seekable_substream->parent_stream;
		if (!seekable_substream_parent_reset (seekable_substream, parent))
			return TRUE;

		eos = camel_stream_eos (CAMEL_STREAM (parent));
		if (!eos && (seekable_stream->bound_end != CAMEL_STREAM_UNBOUND)) {
			eos = seekable_stream->position >= seekable_stream->bound_end;
		}
	}

	return eos;
}

static goffset
seekable_substream_seek (CamelSeekableStream *seekable_stream,
                         goffset offset,
                         CamelStreamSeekPolicy policy,
                         GError **error)
{
	CamelStream *stream;
	CamelSeekableSubstream *seekable_substream;
	goffset real_offset = 0;

	stream = CAMEL_STREAM (seekable_stream);
	seekable_substream = CAMEL_SEEKABLE_SUBSTREAM (seekable_stream);

	stream->eos = FALSE;

	switch (policy) {
	case CAMEL_STREAM_SET:
		real_offset = offset;
		break;

	case CAMEL_STREAM_CUR:
		real_offset = seekable_stream->position + offset;
		break;

	case CAMEL_STREAM_END:
		if (seekable_stream->bound_end == CAMEL_STREAM_UNBOUND) {
			real_offset = camel_seekable_stream_seek (
				seekable_substream->parent_stream,
				offset, CAMEL_STREAM_END, error);
			if (real_offset != -1) {
				if (real_offset<seekable_stream->bound_start)
					real_offset = seekable_stream->bound_start;
				seekable_stream->position = real_offset;
			}
			return real_offset;
		}
		real_offset = seekable_stream->bound_end + offset;
		break;
	}

	if (seekable_stream->bound_end != CAMEL_STREAM_UNBOUND)
		real_offset = MIN (real_offset, seekable_stream->bound_end);

	if (real_offset<seekable_stream->bound_start)
		real_offset = seekable_stream->bound_start;

	seekable_stream->position = real_offset;

	return real_offset;
}

static void
camel_seekable_substream_class_init (CamelSeekableSubstreamClass *class)
{
	GObjectClass *object_class;
	CamelStreamClass *stream_class;
	CamelSeekableStreamClass *seekable_stream_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = seekable_substream_dispose;

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->read = seekable_substream_read;
	stream_class->write = seekable_substream_write;
	stream_class->flush = seekable_substream_flush;
	stream_class->close = seekable_substream_close;
	stream_class->eos = seekable_substream_eos;

	seekable_stream_class = CAMEL_SEEKABLE_STREAM_CLASS (class);
	seekable_stream_class->seek = seekable_substream_seek;
}

static void
camel_seekable_substream_init (CamelSeekableSubstream *seekable_substream)
{
}

/**
 * camel_seekable_substream_new:
 * @parent_stream: a #CamelSeekableStream object
 * @start: a lower bound
 * @end: an upper bound
 *
 * Creates a new CamelSeekableSubstream that references the portion
 * of @parent_stream from @inf_bound to @sup_bound. (If @sup_bound is
 * #CAMEL_STREAM_UNBOUND, it references to the end of stream, even if
 * the stream grows.)
 *
 * While the substream is open, the caller cannot assume anything about
 * the current position of @parent_stream. After the substream has been
 * closed, @parent_stream will stabilize again.
 *
 * Returns: the substream
 **/
CamelStream *
camel_seekable_substream_new(CamelSeekableStream *parent_stream, goffset start, goffset end)
{
	CamelSeekableSubstream *seekable_substream;

	g_return_val_if_fail (CAMEL_IS_SEEKABLE_STREAM (parent_stream), NULL);

	/* Create the seekable substream. */
	seekable_substream = g_object_new (CAMEL_TYPE_SEEKABLE_SUBSTREAM, NULL);

	/* Initialize it. */
	seekable_substream->parent_stream = g_object_ref (parent_stream);

	/* Set the bound of the substream. We can ignore any possible error
	 * here, because if we fail to seek now, it will try again later. */
	camel_seekable_stream_set_bounds (
		CAMEL_SEEKABLE_STREAM (seekable_substream),
		start, end, NULL);

	return CAMEL_STREAM (seekable_substream);
}

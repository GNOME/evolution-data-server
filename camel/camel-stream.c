/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream.c : abstract class for a stream */

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

#include <errno.h>
#include <string.h>

#include "camel-debug.h"
#include "camel-stream.h"

G_DEFINE_ABSTRACT_TYPE (CamelStream, camel_stream, CAMEL_TYPE_OBJECT)

static gssize
stream_read (CamelStream *stream,
             gchar *buffer,
             gsize n,
             GCancellable *cancellable,
             GError **error)
{
	return 0;
}

static gssize
stream_write (CamelStream *stream,
              const gchar *buffer,
              gsize n,
              GCancellable *cancellable,
              GError **error)
{
	return n;
}

static gint
stream_close (CamelStream *stream,
              GCancellable *cancellable,
              GError **error)
{
	return 0;
}

static gint
stream_flush (CamelStream *stream,
              GCancellable *cancellable,
              GError **error)
{
	return 0;
}

static gboolean
stream_eos (CamelStream *stream)
{
	return stream->eos;
}

static void
camel_stream_class_init (CamelStreamClass *class)
{
	class->read = stream_read;
	class->write = stream_write;
	class->close = stream_close;
	class->flush = stream_flush;
	class->eos = stream_eos;
}

static void
camel_stream_init (CamelStream *stream)
{
}

/**
 * camel_stream_read:
 * @stream: a #CamelStream object.
 * @buffer: output buffer
 * @n: max number of bytes to read.
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Attempts to read up to @len bytes from @stream into @buf.
 *
 * Returns: the number of bytes actually read, or %-1 on error and set
 * errno.
 **/
gssize
camel_stream_read (CamelStream *stream,
                   gchar *buffer,
                   gsize n,
                   GCancellable *cancellable,
                   GError **error)
{
	CamelStreamClass *class;
	gssize n_bytes;

	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);
	g_return_val_if_fail (n == 0 || buffer, -1);

	class = CAMEL_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->read != NULL, -1);

	n_bytes = class->read (stream, buffer, n, cancellable, error);
	CAMEL_CHECK_GERROR (stream, read, n_bytes >= 0, error);

	return n_bytes;
}

/**
 * camel_stream_write:
 * @stream: a #CamelStream object
 * @buffer: buffer to write.
 * @n: number of bytes to write
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Attempts to write up to @n bytes of @buffer into @stream.
 *
 * Returns: the number of bytes written to the stream, or %-1 on error
 * along with setting errno.
 **/
gssize
camel_stream_write (CamelStream *stream,
                    const gchar *buffer,
                    gsize n,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelStreamClass *class;
	gssize n_bytes;

	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);
	g_return_val_if_fail (n == 0 || buffer, -1);

	class = CAMEL_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->write != NULL, -1);

	n_bytes = class->write (stream, buffer, n, cancellable, error);
	CAMEL_CHECK_GERROR (stream, write, n_bytes >= 0, error);

	return n_bytes;
}

/**
 * camel_stream_flush:
 * @stream: a #CamelStream object
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Flushes any buffered data to the stream's backing store.  Only
 * meaningful for writable streams.
 *
 * Returns: %0 on success or %-1 on fail along with setting @error
 **/
gint
camel_stream_flush (CamelStream *stream,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelStreamClass *class;
	gint retval;

	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);

	class = CAMEL_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->flush != NULL, -1);

	retval = class->flush (stream, cancellable, error);
	CAMEL_CHECK_GERROR (stream, flush, retval == 0, error);

	return retval;
}

/**
 * camel_stream_close:
 * @stream: a #CamelStream object
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Closes the stream.
 *
 * Returns: %0 on success or %-1 on error.
 **/
gint
camel_stream_close (CamelStream *stream,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelStreamClass *class;
	gint retval;

	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);

	class = CAMEL_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->close != NULL, -1);

	retval = class->close (stream, cancellable, error);
	CAMEL_CHECK_GERROR (stream, close, retval == 0, error);

	return retval;
}

/**
 * camel_stream_eos:
 * @stream: a #CamelStream object
 *
 * Tests if there are bytes left to read on the @stream object.
 *
 * Returns: %TRUE on EOS or %FALSE otherwise.
 **/
gboolean
camel_stream_eos (CamelStream *stream)
{
	CamelStreamClass *class;

	g_return_val_if_fail (CAMEL_IS_STREAM (stream), TRUE);

	class = CAMEL_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->eos != NULL, TRUE);

	return class->eos (stream);
}

/***************** Utility functions ********************/

/**
 * camel_stream_write_string:
 * @stream: a #CamelStream object
 * @string: a string
 * @error: return location for a #GError, or %NULL
 *
 * Writes the string to the stream.
 *
 * Returns: the number of characters written or %-1 on error.
 **/
gssize
camel_stream_write_string (CamelStream *stream,
                           const gchar *string,
                           GCancellable *cancellable,
                           GError **error)
{
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);
	g_return_val_if_fail (string != NULL, -1);

	return camel_stream_write (
		stream, string, strlen (string), cancellable, error);
}

/**
 * camel_stream_write_to_stream:
 * @stream: source #CamelStream object
 * @output_stream: destination #CamelStream object
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Write all of a stream (until eos) into another stream, in a
 * blocking fashion.
 *
 * Returns: %-1 on error, or the number of bytes succesfully
 * copied across streams.
 **/
gssize
camel_stream_write_to_stream (CamelStream *stream,
                              CamelStream *output_stream,
                              GCancellable *cancellable,
                              GError **error)
{
	gchar tmp_buf[4096];
	gssize total = 0;
	gssize nb_read;
	gssize nb_written;

	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);
	g_return_val_if_fail (CAMEL_IS_STREAM (output_stream), -1);

	while (!camel_stream_eos (stream)) {
		nb_read = camel_stream_read (
			stream, tmp_buf, sizeof (tmp_buf),
			cancellable, error);
		if (nb_read < 0)
			return -1;
		else if (nb_read > 0) {
			nb_written = 0;

			while (nb_written < nb_read) {
				gssize len = camel_stream_write (
					output_stream,
					tmp_buf + nb_written,
					nb_read - nb_written,
					cancellable, error);
				if (len < 0)
					return -1;
				nb_written += len;
			}
			total += nb_written;
		}
	}
	return total;
}

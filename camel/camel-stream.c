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

#define CAMEL_STREAM_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_STREAM, CamelStreamPrivate))

struct _CamelStreamPrivate {
	GIOStream *base_stream;
};

enum {
	PROP_0,
	PROP_BASE_STREAM
};

G_DEFINE_TYPE (CamelStream, camel_stream, CAMEL_TYPE_OBJECT)

static void
stream_set_base_stream (CamelStream *stream,
                        GIOStream *base_stream)
{
	g_return_if_fail (stream->priv->base_stream == NULL);

	/* This will be NULL for CamelStream subclasses. */
	if (base_stream != NULL) {
		g_return_if_fail (G_IS_IO_STREAM (base_stream));
		g_object_ref (base_stream);
	}

	stream->priv->base_stream = base_stream;
}

static void
stream_set_property (GObject *object,
                     guint property_id,
                     const GValue *value,
                     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_BASE_STREAM:
			stream_set_base_stream (
				CAMEL_STREAM (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
stream_get_property (GObject *object,
                     guint property_id,
                     GValue *value,
                     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_BASE_STREAM:
			g_value_set_object (
				value,
				camel_stream_get_base_stream (
				CAMEL_STREAM (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
stream_dispose (GObject *object)
{
	CamelStreamPrivate *priv;

	priv = CAMEL_STREAM_GET_PRIVATE (object);

	g_clear_object (&priv->base_stream);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_stream_parent_class)->dispose (object);
}

static gssize
stream_read (CamelStream *stream,
             gchar *buffer,
             gsize n,
             GCancellable *cancellable,
             GError **error)
{
	GIOStream *base_stream;
	GInputStream *input_stream;

	base_stream = camel_stream_get_base_stream (stream);

	if (base_stream == NULL)
		return 0;

	input_stream = g_io_stream_get_input_stream (base_stream);

	return g_input_stream_read (
		input_stream, buffer, n, cancellable, error);
}

static gssize
stream_write (CamelStream *stream,
              const gchar *buffer,
              gsize n,
              GCancellable *cancellable,
              GError **error)
{
	GIOStream *base_stream;
	GOutputStream *output_stream;

	base_stream = camel_stream_get_base_stream (stream);

	if (base_stream == NULL)
		return n;

	output_stream = g_io_stream_get_output_stream (base_stream);

	return g_output_stream_write (
		output_stream, buffer, n, cancellable, error);
}

static gint
stream_close (CamelStream *stream,
              GCancellable *cancellable,
              GError **error)
{
	GIOStream *base_stream;
	gboolean success;

	base_stream = camel_stream_get_base_stream (stream);

	if (base_stream == NULL)
		return 0;

	success = g_io_stream_close (
		stream->priv->base_stream, cancellable, error);

	return success ? 0 : -1;
}

static gint
stream_flush (CamelStream *stream,
              GCancellable *cancellable,
              GError **error)
{
	GIOStream *base_stream;
	GOutputStream *output_stream;
	gboolean success;

	base_stream = camel_stream_get_base_stream (stream);

	if (base_stream == NULL)
		return 0;

	output_stream = g_io_stream_get_output_stream (base_stream);

	success = g_output_stream_flush (output_stream, cancellable, error);

	return success ? 0 : -1;
}

static gboolean
stream_eos (CamelStream *stream)
{
	return stream->eos;
}

static void
camel_stream_class_init (CamelStreamClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelStreamPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = stream_set_property;
	object_class->get_property = stream_get_property;
	object_class->dispose = stream_dispose;

	class->read = stream_read;
	class->write = stream_write;
	class->close = stream_close;
	class->flush = stream_flush;
	class->eos = stream_eos;

	g_object_class_install_property (
		object_class,
		PROP_BASE_STREAM,
		g_param_spec_object (
			"base-stream",
			"Base Stream",
			"The base GIOStream",
			G_TYPE_IO_STREAM,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_stream_init (CamelStream *stream)
{
	stream->priv = CAMEL_STREAM_GET_PRIVATE (stream);
}

/**
 * camel_stream_new:
 * @base_stream: a #GIOStream
 *
 * Creates a #CamelStream as a thin wrapper for @base_stream.
 *
 * Returns: a #CamelStream
 *
 * Since: 3.12
 **/
CamelStream *
camel_stream_new (GIOStream *base_stream)
{
	g_return_val_if_fail (G_IS_IO_STREAM (base_stream), NULL);

	return g_object_new (
		CAMEL_TYPE_STREAM, "base-stream", base_stream, NULL);
}

/**
 * camel_stream_get_base_stream:
 * @stream: a #CamelStream
 *
 * Returns the #GIOStream for @stream.  This is only valid if @stream was
 * created with camel_stream_new().  For all other #CamelStream subclasses
 * this function returns %NULL.
 *
 * Returns: a #GIOStream, or %NULL
 *
 * Since: 3.12
 **/
GIOStream *
camel_stream_get_base_stream (CamelStream *stream)
{
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), NULL);

	return stream->priv->base_stream;
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

/*
 * camel-null-output-stream.c
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 * SECTION: camel-null-output-stream
 * @short_description: Null output stream
 * @include: camel/camel.h
 * @see_also: #GOutputStream
 *
 * #CamelNullOutputStream is analogous to writing to /dev/null, except it
 * counts the total number of bytes written to it.  This is primarily useful
 * for determining the final size of some outgoing data, especially if using
 * filters on the output stream.
 **/

#include "camel-null-output-stream.h"

#define CAMEL_NULL_OUTPUT_STREAM_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_NULL_OUTPUT_STREAM, CamelNullOutputStreamPrivate))

struct _CamelNullOutputStreamPrivate {
	gsize bytes_written;
};

G_DEFINE_TYPE (
	CamelNullOutputStream,
	camel_null_output_stream,
	G_TYPE_OUTPUT_STREAM)

static gssize
null_output_stream_write (GOutputStream *stream,
                          gconstpointer buffer,
                          gsize count,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelNullOutputStreamPrivate *priv;

	priv = CAMEL_NULL_OUTPUT_STREAM_GET_PRIVATE (stream);

	priv->bytes_written += count;

	return count;
}

static void
camel_null_output_stream_class_init (CamelNullOutputStreamClass *class)
{
	GOutputStreamClass *stream_class;

	g_type_class_add_private (
		class, sizeof (CamelNullOutputStreamPrivate));

	stream_class = G_OUTPUT_STREAM_CLASS (class);
	stream_class->write_fn = null_output_stream_write;
}

static void
camel_null_output_stream_init (CamelNullOutputStream *null_stream)
{
	null_stream->priv = CAMEL_NULL_OUTPUT_STREAM_GET_PRIVATE (null_stream);
}

/**
 * camel_null_output_stream_new:
 *
 * Creates a new "null" output stream.
 *
 * Returns: a new #GOutputStream
 *
 * Since: 3.12
 **/
GOutputStream *
camel_null_output_stream_new (void)
{
	return g_object_new (CAMEL_TYPE_NULL_OUTPUT_STREAM, NULL);
}

/**
 * camel_null_output_stream_get_bytes_written:
 * @null_stream: a #CamelNullOutputStream
 *
 * Gets the total number of bytes written to @null_stream.
 *
 * Returns: total byte count
 *
 * Since: 3.12
 **/
gsize
camel_null_output_stream_get_bytes_written (CamelNullOutputStream *null_stream)
{
	g_return_val_if_fail (CAMEL_IS_NULL_OUTPUT_STREAM (null_stream), 0);

	return null_stream->priv->bytes_written;
}


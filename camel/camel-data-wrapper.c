/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; -*- */
/*
 *
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <glib/gi18n-lib.h>

#include "camel-data-wrapper.h"
#include "camel-debug.h"
#include "camel-mime-filter-basic.h"
#include "camel-mime-filter-crlf.h"
#include "camel-stream-filter.h"
#include "camel-stream-mem.h"

#define d(x)

#define CAMEL_DATA_WRAPPER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_DATA_WRAPPER, CamelDataWrapperPrivate))

typedef struct _AsyncContext AsyncContext;

struct _CamelDataWrapperPrivate {
	GMutex stream_lock;
	GByteArray *byte_array;
};

struct _AsyncContext {
	CamelStream *stream;
};

G_DEFINE_TYPE (CamelDataWrapper, camel_data_wrapper, G_TYPE_OBJECT)

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->stream != NULL)
		g_object_unref (async_context->stream);

	g_slice_free (AsyncContext, async_context);
}

static void
data_wrapper_dispose (GObject *object)
{
	CamelDataWrapper *data_wrapper = CAMEL_DATA_WRAPPER (object);

	if (data_wrapper->mime_type != NULL) {
		camel_content_type_unref (data_wrapper->mime_type);
		data_wrapper->mime_type = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_data_wrapper_parent_class)->dispose (object);
}

static void
data_wrapper_finalize (GObject *object)
{
	CamelDataWrapperPrivate *priv;

	priv = CAMEL_DATA_WRAPPER_GET_PRIVATE (object);

	g_mutex_clear (&priv->stream_lock);
	g_byte_array_free (priv->byte_array, TRUE);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_data_wrapper_parent_class)->finalize (object);
}

static void
data_wrapper_set_mime_type (CamelDataWrapper *data_wrapper,
                            const gchar *mime_type)
{
	if (data_wrapper->mime_type)
		camel_content_type_unref (data_wrapper->mime_type);
	data_wrapper->mime_type = camel_content_type_decode (mime_type);
}

static gchar *
data_wrapper_get_mime_type (CamelDataWrapper *data_wrapper)
{
	return camel_content_type_simple (data_wrapper->mime_type);
}

static CamelContentType *
data_wrapper_get_mime_type_field (CamelDataWrapper *data_wrapper)
{
	return data_wrapper->mime_type;
}

static void
data_wrapper_set_mime_type_field (CamelDataWrapper *data_wrapper,
                                  CamelContentType *mime_type)
{
	if (mime_type)
		camel_content_type_ref (mime_type);
	if (data_wrapper->mime_type)
		camel_content_type_unref (data_wrapper->mime_type);
	data_wrapper->mime_type = mime_type;
}

static gboolean
data_wrapper_is_offline (CamelDataWrapper *data_wrapper)
{
	return data_wrapper->offline;
}

static gssize
data_wrapper_write_to_stream_sync (CamelDataWrapper *data_wrapper,
                                   CamelStream *stream,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelStream *memory_stream;
	gssize ret;

	g_mutex_lock (&data_wrapper->priv->stream_lock);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		g_mutex_unlock (&data_wrapper->priv->stream_lock);
		return -1;
	}

	memory_stream = camel_stream_mem_new ();

	/* We retain ownership of the byte array. */
	camel_stream_mem_set_byte_array (
		CAMEL_STREAM_MEM (memory_stream),
		data_wrapper->priv->byte_array);

	ret = camel_stream_write_to_stream (
		memory_stream, stream, cancellable, error);

	g_object_unref (memory_stream);

	g_mutex_unlock (&data_wrapper->priv->stream_lock);

	return ret;
}

static gssize
data_wrapper_decode_to_stream_sync (CamelDataWrapper *data_wrapper,
                                    CamelStream *stream,
                                    GCancellable *cancellable,
                                    GError **error)
{
	CamelMimeFilter *filter;
	CamelStream *fstream;
	gssize ret;

	fstream = camel_stream_filter_new (stream);

	switch (data_wrapper->encoding) {
	case CAMEL_TRANSFER_ENCODING_BASE64:
		filter = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_BASE64_DEC);
		camel_stream_filter_add (CAMEL_STREAM_FILTER (fstream), filter);
		g_object_unref (filter);
		break;
	case CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE:
		filter = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_QP_DEC);
		camel_stream_filter_add (CAMEL_STREAM_FILTER (fstream), filter);
		g_object_unref (filter);
		break;
	case CAMEL_TRANSFER_ENCODING_UUENCODE:
		filter = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_UU_DEC);
		camel_stream_filter_add (CAMEL_STREAM_FILTER (fstream), filter);
		g_object_unref (filter);
		break;
	default:
		break;
	}

	if (!(camel_content_type_is (data_wrapper->mime_type, "text", "pdf")) && camel_content_type_is (data_wrapper->mime_type, "text", "*")) {
		filter = camel_mime_filter_crlf_new (
			CAMEL_MIME_FILTER_CRLF_DECODE,
			CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY);
		camel_stream_filter_add (CAMEL_STREAM_FILTER (fstream), filter);
		g_object_unref (filter);
	}

	ret = camel_data_wrapper_write_to_stream_sync (
		data_wrapper, fstream, cancellable, error);

	camel_stream_flush (fstream, NULL, NULL);
	g_object_unref (fstream);

	return ret;
}

static gboolean
data_wrapper_construct_from_stream_sync (CamelDataWrapper *data_wrapper,
                                         CamelStream *stream,
                                         GCancellable *cancellable,
                                         GError **error)
{
	CamelStream *memory_stream;
	gssize bytes_written;

	g_mutex_lock (&data_wrapper->priv->stream_lock);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		g_mutex_unlock (&data_wrapper->priv->stream_lock);
		return FALSE;
	}

	if (G_IS_SEEKABLE (stream)) {
		if (!g_seekable_seek (G_SEEKABLE (stream), 0, G_SEEK_SET, cancellable, error)) {
			g_mutex_unlock (&data_wrapper->priv->stream_lock);
			return FALSE;
		}
	}

	/* Wipe any previous contents from our byte array. */
	g_byte_array_set_size (data_wrapper->priv->byte_array, 0);

	memory_stream = camel_stream_mem_new ();

	/* We retain ownership of the byte array. */
	camel_stream_mem_set_byte_array (
		CAMEL_STREAM_MEM (memory_stream),
		data_wrapper->priv->byte_array);

	/* Transfer incoming contents to our byte array. */
	bytes_written = camel_stream_write_to_stream (
		stream, memory_stream, cancellable, error);

	g_object_unref (memory_stream);

	g_mutex_unlock (&data_wrapper->priv->stream_lock);

	return (bytes_written >= 0);
}

static void
camel_data_wrapper_class_init (CamelDataWrapperClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelDataWrapperPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = data_wrapper_dispose;
	object_class->finalize = data_wrapper_finalize;

	class->set_mime_type = data_wrapper_set_mime_type;
	class->get_mime_type = data_wrapper_get_mime_type;
	class->get_mime_type_field = data_wrapper_get_mime_type_field;
	class->set_mime_type_field = data_wrapper_set_mime_type_field;
	class->is_offline = data_wrapper_is_offline;

	class->write_to_stream_sync = data_wrapper_write_to_stream_sync;
	class->decode_to_stream_sync = data_wrapper_decode_to_stream_sync;
	class->construct_from_stream_sync = data_wrapper_construct_from_stream_sync;
}

static void
camel_data_wrapper_init (CamelDataWrapper *data_wrapper)
{
	data_wrapper->priv = CAMEL_DATA_WRAPPER_GET_PRIVATE (data_wrapper);

	g_mutex_init (&data_wrapper->priv->stream_lock);
	data_wrapper->priv->byte_array = g_byte_array_new ();

	data_wrapper->mime_type = camel_content_type_new (
		"application", "octet-stream");
	data_wrapper->encoding = CAMEL_TRANSFER_ENCODING_DEFAULT;
	data_wrapper->offline = FALSE;
}

/**
 * camel_data_wrapper_new:
 *
 * Create a new #CamelDataWrapper object.
 *
 * Returns: a new #CamelDataWrapper object
 **/
CamelDataWrapper *
camel_data_wrapper_new (void)
{
	return g_object_new (CAMEL_TYPE_DATA_WRAPPER, NULL);
}

/**
 * camel_data_wrapper_get_byte_array:
 * @data_wrapper: a #CamelDataWrapper
 *
 * Returns the #GByteArray being used to hold the contents of @data_wrapper.
 *
 * Note, it's up to the caller to use this in a thread-safe manner.
 *
 * Returns: the #GByteArray for @data_wrapper
 *
 * Since: 3.2
 **/
GByteArray *
camel_data_wrapper_get_byte_array (CamelDataWrapper *data_wrapper)
{
	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), NULL);

	return data_wrapper->priv->byte_array;
}

/**
 * camel_data_wrapper_set_mime_type:
 * @data_wrapper: a #CamelDataWrapper
 * @mime_type: a MIME type
 *
 * This sets the data wrapper's MIME type.
 *
 * It might fail, but you won't know. It will allow you to set
 * Content-Type parameters on the data wrapper, which are meaningless.
 * You should not be allowed to change the MIME type of a data wrapper
 * that contains data, or at least, if you do, it should invalidate the
 * data.
 **/
void
camel_data_wrapper_set_mime_type (CamelDataWrapper *data_wrapper,
                                  const gchar *mime_type)
{
	CamelDataWrapperClass *class;

	g_return_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper));
	g_return_if_fail (mime_type != NULL);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_if_fail (class->set_mime_type);

	class->set_mime_type (data_wrapper, mime_type);
}

/**
 * camel_data_wrapper_get_mime_type:
 * @data_wrapper: a #CamelDataWrapper
 *
 * Returns: the MIME type which must be freed by the caller
 **/
gchar *
camel_data_wrapper_get_mime_type (CamelDataWrapper *data_wrapper)
{
	CamelDataWrapperClass *class;

	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), NULL);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_val_if_fail (class->get_mime_type != NULL, NULL);

	return class->get_mime_type (data_wrapper);
}

/**
 * camel_data_wrapper_get_mime_type_field:
 * @data_wrapper: a #CamelDataWrapper
 *
 * Returns: the parsed form of the data wrapper's MIME type
 **/
CamelContentType *
camel_data_wrapper_get_mime_type_field (CamelDataWrapper *data_wrapper)
{
	CamelDataWrapperClass *class;

	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), NULL);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_val_if_fail (class->get_mime_type_field != NULL, NULL);

	return class->get_mime_type_field (data_wrapper);
}

/**
 * camel_data_wrapper_set_mime_type_field:
 * @data_wrapper: a #CamelDataWrapper
 * @mime_type: a #CamelContentType
 *
 * This sets the data wrapper's MIME type. It suffers from the same
 * flaws as camel_data_wrapper_set_mime_type().
 **/
void
camel_data_wrapper_set_mime_type_field (CamelDataWrapper *data_wrapper,
                                        CamelContentType *mime_type)
{
	CamelDataWrapperClass *class;

	g_return_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper));
	g_return_if_fail (mime_type != NULL);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_if_fail (class->set_mime_type_field != NULL);

	class->set_mime_type_field (data_wrapper, mime_type);
}

/**
 * camel_data_wrapper_is_offline:
 * @data_wrapper: a #CamelDataWrapper
 *
 * Returns: whether @data_wrapper is "offline" (data stored
 * remotely) or not. Some optional code paths may choose to not
 * operate on offline data.
 **/
gboolean
camel_data_wrapper_is_offline (CamelDataWrapper *data_wrapper)
{
	CamelDataWrapperClass *class;

	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), TRUE);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_val_if_fail (class->is_offline != NULL, TRUE);

	return class->is_offline (data_wrapper);
}

/**
 * camel_data_wrapper_write_to_stream_sync:
 * @data_wrapper: a #CamelDataWrapper
 * @stream: a #CamelStream for output
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Writes the content of @data_wrapper to @stream in a machine-independent
 * format appropriate for the data.  It should be possible to construct an
 * equivalent data wrapper object later by passing this stream to
 * camel_data_wrapper_construct_from_stream_sync().
 *
 * <note>
 *   <para>
 *     This function may block even if the given output stream does not.
 *     For example, the content may have to be fetched across a network
 *     before it can be written to @stream.
 *   </para>
 * </note>
 *
 * Returns: the number of bytes written, or %-1 on error
 *
 * Since: 3.0
 **/
gssize
camel_data_wrapper_write_to_stream_sync (CamelDataWrapper *data_wrapper,
                                         CamelStream *stream,
                                         GCancellable *cancellable,
                                         GError **error)
{
	CamelDataWrapperClass *class;
	gssize bytes_written;

	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), -1);
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_val_if_fail (class->write_to_stream_sync != NULL, -1);

	bytes_written = class->write_to_stream_sync (
		data_wrapper, stream, cancellable, error);
	CAMEL_CHECK_GERROR (
		data_wrapper, write_to_stream_sync,
		bytes_written >= 0, error);

	return bytes_written;
}

/* Helper for camel_data_wrapper_write_to_stream() */
static void
data_wrapper_write_to_stream_thread (GTask *task,
                                     gpointer source_object,
                                     gpointer task_data,
                                     GCancellable *cancellable)
{
	gssize bytes_written;
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = (AsyncContext *) task_data;

	bytes_written = camel_data_wrapper_write_to_stream_sync (
		CAMEL_DATA_WRAPPER (source_object),
		async_context->stream,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_int (task, bytes_written);
	}
}

/**
 * camel_data_wrapper_write_to_stream:
 * @data_wrapper: a #CamelDataWrapper
 * @stream: a #CamelStream for writed data to be written to
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously writes the content of @data_wrapper to @stream in a
 * machine-independent format appropriate for the data.  It should be
 * possible to construct an equivalent data wrapper object later by
 * passing this stream to camel_data_wrapper_construct_from_stream().
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_data_wrapper_write_to_stream_finish() to get the result of
 * the operation.
 *
 * Since: 3.0
 **/
void
camel_data_wrapper_write_to_stream (CamelDataWrapper *data_wrapper,
                                    CamelStream *stream,
                                    gint io_priority,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper));
	g_return_if_fail (CAMEL_IS_STREAM (stream));

	async_context = g_slice_new0 (AsyncContext);
	async_context->stream = g_object_ref (stream);

	task = g_task_new (data_wrapper, cancellable, callback, user_data);
	g_task_set_source_tag (task, camel_data_wrapper_write_to_stream);
	g_task_set_priority (task, io_priority);

	g_task_set_task_data (
		task, async_context,
		(GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, data_wrapper_write_to_stream_thread);

	g_object_unref (task);
}

/**
 * camel_data_wrapper_write_to_stream_finish:
 * @data_wrapper: a #CamelDataWrapper
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_data_wrapper_write_to_stream().
 *
 * Returns: the number of bytes written, or %-1 or error
 *
 * Since: 3.0
 **/
gssize
camel_data_wrapper_write_to_stream_finish (CamelDataWrapper *data_wrapper,
                                           GAsyncResult *result,
                                           GError **error)
{
	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), -1);
	g_return_val_if_fail (g_task_is_valid (result, data_wrapper), -1);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, camel_data_wrapper_write_to_stream), -1);

	return g_task_propagate_int (G_TASK (result), error);
}

/**
 * camel_data_wrapper_decode_to_stream_sync:
 * @data_wrapper: a #CamelDataWrapper
 * @stream: a #CamelStream for decoded data to be written to
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Writes the decoded data content to @stream.
 *
 * <note>
 *   <para>
 *     This function may block even if the given output stream does not.
 *     For example, the content may have to be fetched across a network
 *     before it can be written to @stream.
 *   </para>
 * </note>
 *
 * Returns: the number of bytes written, or %-1 on error
 *
 * Since: 3.0
 **/
gssize
camel_data_wrapper_decode_to_stream_sync (CamelDataWrapper *data_wrapper,
                                          CamelStream *stream,
                                          GCancellable *cancellable,
                                          GError **error)
{
	CamelDataWrapperClass *class;
	gssize bytes_written;

	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), -1);
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_val_if_fail (class->decode_to_stream_sync != NULL, -1);

	bytes_written = class->decode_to_stream_sync (
		data_wrapper, stream, cancellable, error);
	CAMEL_CHECK_GERROR (
		data_wrapper, decode_to_stream_sync,
		bytes_written >= 0, error);

	return bytes_written;
}

/* Helper for camel_data_wrapper_decode_to_stream() */
static void
data_wrapper_decode_to_stream_thread (GTask *task,
                                      gpointer source_object,
                                      gpointer task_data,
                                      GCancellable *cancellable)
{
	gssize bytes_written;
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = (AsyncContext *) task_data;

	bytes_written = camel_data_wrapper_decode_to_stream_sync (
		CAMEL_DATA_WRAPPER (source_object),
		async_context->stream,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_int (task, bytes_written);
	}
}

/**
 * camel_data_wrapper_decode_to_stream:
 * @data_wrapper: a #CamelDataWrapper
 * @stream: a #CamelStream for decoded data to be written to
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously writes the decoded data content to @stream.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_data_wrapper_decode_to_stream_finish() to get the result of
 * the operation.
 *
 * Since: 3.0
 **/
void
camel_data_wrapper_decode_to_stream (CamelDataWrapper *data_wrapper,
                                     CamelStream *stream,
                                     gint io_priority,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper));
	g_return_if_fail (CAMEL_IS_STREAM (stream));

	async_context = g_slice_new0 (AsyncContext);
	async_context->stream = g_object_ref (stream);

	task = g_task_new (data_wrapper, cancellable, callback, user_data);
	g_task_set_source_tag (task, camel_data_wrapper_decode_to_stream);
	g_task_set_priority (task, io_priority);

	g_task_set_task_data (
		task, async_context,
		(GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, data_wrapper_decode_to_stream_thread);

	g_object_unref (task);
}

/**
 * camel_data_wrapper_decode_to_stream_finish:
 * @data_wrapper: a #CamelDataWrapper
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_data_wrapper_decode_to_stream().
 *
 * Returns: the number of bytes written, or %-1 on error
 *
 * Since: 3.0
 **/
gssize
camel_data_wrapper_decode_to_stream_finish (CamelDataWrapper *data_wrapper,
                                            GAsyncResult *result,
                                            GError **error)
{
	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), -1);
	g_return_val_if_fail (g_task_is_valid (result, data_wrapper), -1);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, camel_data_wrapper_decode_to_stream), -1);

	return g_task_propagate_int (G_TASK (result), error);
}

/**
 * camel_data_wrapper_construct_from_stream_sync:
 * @data_wrapper: a #CamelDataWrapper
 * @stream: an input #CamelStream
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Constructs the content of @data_wrapper from the given @stream.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_data_wrapper_construct_from_stream_sync (CamelDataWrapper *data_wrapper,
                                               CamelStream *stream,
                                               GCancellable *cancellable,
                                               GError **error)
{
	CamelDataWrapperClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), FALSE);
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), FALSE);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_val_if_fail (class->construct_from_stream_sync != NULL, FALSE);

	success = class->construct_from_stream_sync (
		data_wrapper, stream, cancellable, error);
	CAMEL_CHECK_GERROR (
		data_wrapper, construct_from_stream_sync, success, error);

	return success;
}

/* Helper for camel_data_wrapper_construct_from_stream() */
static void
data_wrapper_construct_from_stream_thread (GTask *task,
                                           gpointer source_object,
                                           gpointer task_data,
                                           GCancellable *cancellable)
{
	gboolean success;
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = (AsyncContext *) task_data;

	success = camel_data_wrapper_construct_from_stream_sync (
		CAMEL_DATA_WRAPPER (source_object),
		async_context->stream,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * camel_data_wrapper_construct_from_stream:
 * @data_wrapper: a #CamelDataWrapper
 * @stream: an input #CamelStream
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously constructs the content of @data_wrapper from the given
 * @stream.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_data_wrapper_construct_from_stream_finish() to get the result
 * of the operation.
 *
 * Since: 3.0
 **/
void
camel_data_wrapper_construct_from_stream (CamelDataWrapper *data_wrapper,
                                          CamelStream *stream,
                                          gint io_priority,
                                          GCancellable *cancellable,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper));
	g_return_if_fail (CAMEL_IS_STREAM (stream));

	async_context = g_slice_new0 (AsyncContext);
	async_context->stream = g_object_ref (stream);

	task = g_task_new (data_wrapper, cancellable, callback, user_data);
	g_task_set_source_tag (task, camel_data_wrapper_construct_from_stream);
	g_task_set_priority (task, io_priority);

	g_task_set_task_data (
		task, async_context,
		(GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, data_wrapper_construct_from_stream_thread);

	g_object_unref (task);
}

/**
 * camel_data_wrapper_construct_from_stream_finish:
 * @data_wrapper: a #CamelDataWrapper
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with
 * camel_data_wrapper_construct_from_stream().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_data_wrapper_construct_from_stream_finish (CamelDataWrapper *data_wrapper,
                                                 GAsyncResult *result,
                                                 GError **error)
{
	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, data_wrapper), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, camel_data_wrapper_construct_from_stream), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

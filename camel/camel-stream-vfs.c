/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream-vfs.c : file system based stream */

/*
 * Authors: Srinivasa Ragavan <sragavan@novell.com>
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

#include <gio/gio.h>

#include "camel-file-utils.h"
#include "camel-operation.h"
#include "camel-stream-vfs.h"

G_DEFINE_TYPE (CamelStreamVFS, camel_stream_vfs, CAMEL_TYPE_STREAM)

static void
stream_vfs_dispose (GObject *object)
{
	CamelStreamVFS *stream = CAMEL_STREAM_VFS (object);

	if (stream->stream != NULL) {
		g_object_unref (stream->stream);
		stream->stream = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_stream_vfs_parent_class)->dispose (object);
}

static gssize
stream_vfs_read (CamelStream *stream,
                 gchar *buffer,
                 gsize n,
                 GCancellable *cancellable,
                 GError **error)
{
	CamelStreamVFS *stream_vfs = CAMEL_STREAM_VFS (stream);
	gssize nread;
	GError *local_error = NULL;

	nread = g_input_stream_read (
		G_INPUT_STREAM (stream_vfs->stream),
		buffer, n, cancellable, &local_error);

	if (nread == 0 || local_error != NULL)
		stream->eos = TRUE;

	if (local_error != NULL)
		g_propagate_error (error, local_error);

	return nread;
}

static gssize
stream_vfs_write (CamelStream *stream,
                  const gchar *buffer,
                  gsize n,
                  GCancellable *cancellable,
                  GError **error)
{
	CamelStreamVFS *stream_vfs = CAMEL_STREAM_VFS (stream);
	gboolean success;
	gsize bytes_written;

	success = g_output_stream_write_all (
		G_OUTPUT_STREAM (stream_vfs->stream),
		buffer, n, &bytes_written, cancellable, error);

	return success ? bytes_written : -1;
}

static gint
stream_vfs_flush (CamelStream *stream,
                  GCancellable *cancellable,
                  GError **error)
{
	CamelStreamVFS *stream_vfs = CAMEL_STREAM_VFS (stream);
	gboolean success;

	success = g_output_stream_flush (
		G_OUTPUT_STREAM (stream_vfs->stream), cancellable, error);

	return success ? 0 : -1;
}

static gint
stream_vfs_close (CamelStream *stream,
                  GCancellable *cancellable,
                  GError **error)
{
	CamelStreamVFS *stream_vfs = CAMEL_STREAM_VFS (stream);
	gboolean success;

	if (G_IS_OUTPUT_STREAM (stream_vfs->stream))
		success = g_output_stream_close (
			G_OUTPUT_STREAM (stream_vfs->stream),
			cancellable, error);
	else
		success = g_input_stream_close (
			G_INPUT_STREAM (stream_vfs->stream),
			cancellable, error);

	if (success) {
		g_object_unref (stream_vfs->stream);
		stream_vfs->stream = NULL;
	}

	return success ? 0 : -1;
}

static void
camel_stream_vfs_class_init (CamelStreamVFSClass *class)
{
	GObjectClass *object_class;
	CamelStreamClass *stream_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = stream_vfs_dispose;

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->read = stream_vfs_read;
	stream_class->write = stream_vfs_write;
	stream_class->flush = stream_vfs_flush;
	stream_class->close = stream_vfs_close;
}

static void
camel_stream_vfs_init (CamelStreamVFS *stream)
{
	stream->stream = NULL;
}

/**
 * camel_stream_vfs_new_with_stream:
 * @stream: a GInputStream or GOutputStream instance
 *
 * Creates a new fs stream using the given gio stream @stream as the
 * backing store. When the stream is destroyed, the file descriptor
 * will be closed. This will not increase reference counter on the stream.
 *
 * Returns: a new #CamelStreamVFS
 *
 * Since: 2.24
 **/
CamelStream *
camel_stream_vfs_new_with_stream (GObject *stream)
{
	CamelStreamVFS *stream_vfs;

	errno = EINVAL;

	if (!stream)
		return NULL;

	g_return_val_if_fail (G_IS_OUTPUT_STREAM (stream) || G_IS_INPUT_STREAM (stream), NULL);

	errno = 0;
	stream_vfs = g_object_new (CAMEL_TYPE_STREAM_VFS, NULL);
	stream_vfs->stream = stream;

	return CAMEL_STREAM (stream_vfs);
}

/**
 * camel_stream_vfs_new_with_uri:
 * @uri: a file uri
 * @mode: opening mode for the uri file
 *
 * Creates a new #CamelStreamVFS corresponding to the named file and mode.
 *
 * Returns: the new stream, or %NULL on error.
 **/
CamelStream *
camel_stream_vfs_new_with_uri (const gchar *uri,
                               CamelStreamVFSOpenMethod mode)
{
	GFile *file;
	GObject *stream;
	GError *error = NULL;

	file = g_file_new_for_uri (uri);

	switch (mode) {
		case CAMEL_STREAM_VFS_CREATE:
			stream = G_OBJECT (g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, &error));
			break;
		case CAMEL_STREAM_VFS_APPEND:
			stream = G_OBJECT (g_file_append_to (file, G_FILE_CREATE_NONE, NULL, &error));
			break;
		case CAMEL_STREAM_VFS_READ:
			stream = G_OBJECT (g_file_read (file, NULL, &error));
			break;
		default:
			errno = EINVAL;
			g_return_val_if_reached (NULL);
	}

	g_object_unref (file);

	if (error) {
		errno = error->code;
		g_warning ("%s", error->message);
		g_error_free (error);
		return NULL;
	}

	return camel_stream_vfs_new_with_stream (stream);
}

/**
 * camel_stream_vfs_is_writable:
 * @stream_vfs: a #CamelStreamVFS instance
 *
 * Returns: whether is the underlying stream writable or not.
 *
 * Since: 2.24
 **/
gboolean
camel_stream_vfs_is_writable (CamelStreamVFS *stream_vfs)
{
	g_return_val_if_fail (stream_vfs != NULL, FALSE);
	g_return_val_if_fail (stream_vfs->stream != NULL, FALSE);

	return G_IS_OUTPUT_STREAM (stream_vfs->stream);
}

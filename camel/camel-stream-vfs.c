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

#include <glib.h>
#include <gio/gio.h>

#include "camel-file-utils.h"
#include "camel-operation.h"
#include "camel-private.h"
#include "camel-stream-vfs.h"

static CamelStreamClass *parent_class = NULL;

/* Returns the class for a CamelStreamVFS */
#define CSVFS_CLASS(so) CAMEL_STREAM_VFS_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static ssize_t stream_read   (CamelStream *stream, char *buffer, size_t n);
static ssize_t stream_write  (CamelStream *stream, const char *buffer, size_t n);
static int stream_flush  (CamelStream *stream);
static int stream_close  (CamelStream *stream);

static void
camel_stream_vfs_class_init (CamelStreamVFSClass *camel_stream_vfs_class)
{
	CamelStreamClass *camel_stream_class =
		CAMEL_STREAM_CLASS (camel_stream_vfs_class);

	parent_class = CAMEL_STREAM_CLASS (camel_type_get_global_classfuncs (camel_stream_get_type ()));

	/* virtual method overload */
	camel_stream_class->read = stream_read;
	camel_stream_class->write = stream_write;
	camel_stream_class->flush = stream_flush;
	camel_stream_class->close = stream_close;
}

static void
camel_stream_vfs_init (gpointer object, gpointer klass)
{
	CamelStreamVFS *stream = CAMEL_STREAM_VFS (object);

	stream->stream = NULL;
}

static void
camel_stream_vfs_finalize (CamelObject *object)
{
	CamelStreamVFS *stream_vfs = CAMEL_STREAM_VFS (object);

	if (stream_vfs->stream)
		g_object_unref (stream_vfs->stream);
}


CamelType
camel_stream_vfs_get_type (void)
{
	static CamelType camel_stream_vfs_type = CAMEL_INVALID_TYPE;

	if (camel_stream_vfs_type == CAMEL_INVALID_TYPE) {
		camel_stream_vfs_type = camel_type_register (camel_stream_get_type (), "CamelStreamVFS",
							    sizeof (CamelStreamVFS),
							    sizeof (CamelStreamVFSClass),
							    (CamelObjectClassInitFunc) camel_stream_vfs_class_init,
							    NULL,
							    (CamelObjectInitFunc) camel_stream_vfs_init,
							    (CamelObjectFinalizeFunc) camel_stream_vfs_finalize);
	}

	return camel_stream_vfs_type;
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
	stream_vfs = CAMEL_STREAM_VFS (camel_object_new (camel_stream_vfs_get_type ()));
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
camel_stream_vfs_new_with_uri (const char *uri, CamelStreamVFSOpenMethod mode)
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
 **/
gboolean
camel_stream_vfs_is_writable (CamelStreamVFS *stream_vfs)
{
	g_return_val_if_fail (stream_vfs != NULL, FALSE);
	g_return_val_if_fail (stream_vfs->stream != NULL, FALSE);

	return G_IS_OUTPUT_STREAM (stream_vfs->stream);
}

static ssize_t
stream_read (CamelStream *stream, char *buffer, size_t n)
{
	gssize nread;
	GError *error = NULL;
	CamelStreamVFS *stream_vfs = CAMEL_STREAM_VFS (stream);

	g_return_val_if_fail (G_IS_INPUT_STREAM (stream_vfs->stream), 0);
	
	nread = g_input_stream_read (G_INPUT_STREAM (stream_vfs->stream), buffer, n, NULL, &error);

	if (nread == 0 || error)
		stream->eos = TRUE;

	if (error) {
		g_warning ("%s", error->message);
		g_error_free (error);
	}

	return nread;
}

static ssize_t
stream_write (CamelStream *stream, const char *buffer, size_t n)
{
	gboolean success;
	gsize bytes_written;
	GError *error = NULL;
	CamelStreamVFS *stream_vfs = CAMEL_STREAM_VFS (stream);

	g_return_val_if_fail (G_IS_OUTPUT_STREAM (stream_vfs->stream), 0);

	success = g_output_stream_write_all (G_OUTPUT_STREAM (stream_vfs->stream), buffer, n, &bytes_written, NULL, &error);

	if (error) {
		g_warning ("%s", error->message);
		g_error_free (error);
	}
	return success ? bytes_written : -1;
}

static int
stream_flush (CamelStream *stream)
{
	CamelStreamVFS *stream_vfs = CAMEL_STREAM_VFS (stream);
	GError *error = NULL;

	g_return_val_if_fail (CAMEL_IS_STREAM_VFS (stream) && stream_vfs != NULL, -1);
	g_return_val_if_fail (stream_vfs->stream != NULL, -1);
	g_return_val_if_fail (G_IS_OUTPUT_STREAM (stream_vfs->stream), -1);

	g_output_stream_flush (G_OUTPUT_STREAM (stream_vfs->stream), NULL, &error);

	if (error) {
		g_warning ("%s", error->message);
		g_error_free (error);
		return -1;
	}

	return 0;
}

static int
stream_close (CamelStream *stream)
{
	CamelStreamVFS *stream_vfs = CAMEL_STREAM_VFS (stream);
	GError *error = NULL;

	g_return_val_if_fail (CAMEL_IS_STREAM_VFS (stream) && stream_vfs != NULL, -1);
	g_return_val_if_fail (stream_vfs->stream != NULL, -1);
	g_return_val_if_fail (G_IS_OUTPUT_STREAM (stream_vfs->stream) || G_IS_INPUT_STREAM (stream_vfs->stream), -1);

	if (G_IS_OUTPUT_STREAM (stream_vfs->stream))
		g_output_stream_close (G_OUTPUT_STREAM (stream_vfs->stream), NULL, &error);
	else
		g_input_stream_close (G_INPUT_STREAM (stream_vfs->stream), NULL, &error);

	if (error) {
		g_warning ("%s", error->message);
		g_error_free (error);
		return -1;
	}

	g_object_unref (stream_vfs->stream);
	stream_vfs->stream = NULL;

	return 0;
}

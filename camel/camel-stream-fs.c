/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream-fs.c : file system based stream */

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gstdio.h>

#include "camel-file-utils.h"
#include "camel-operation.h"
#include "camel-stream-fs.h"
#include "camel-win32.h"

#define CAMEL_STREAM_FS_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_STREAM_FS, CamelStreamFsPrivate))

struct _CamelStreamFsPrivate {
	gint fd;	/* file descriptor on the underlying file */
};

G_DEFINE_TYPE (CamelStreamFs, camel_stream_fs, CAMEL_TYPE_SEEKABLE_STREAM)

static void
stream_fs_finalize (GObject *object)
{
	CamelStreamFsPrivate *priv;

	priv = CAMEL_STREAM_FS_GET_PRIVATE (object);

	if (priv->fd != -1)
		close (priv->fd);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_stream_fs_parent_class)->finalize (object);
}

static gssize
stream_fs_read (CamelStream *stream,
                gchar *buffer,
                gsize n,
                GError **error)
{
	CamelStreamFsPrivate *priv;
	CamelSeekableStream *seekable;
	gssize nread;

	priv = CAMEL_STREAM_FS_GET_PRIVATE (stream);
	seekable = CAMEL_SEEKABLE_STREAM (stream);

	if (seekable->bound_end != CAMEL_STREAM_UNBOUND)
		n = MIN (seekable->bound_end - seekable->position, n);

	if ((nread = camel_read (priv->fd, buffer, n, error)) > 0)
		seekable->position += nread;
	else if (nread == 0)
		stream->eos = TRUE;

	return nread;
}

static gssize
stream_fs_write (CamelStream *stream,
                 const gchar *buffer,
                 gsize n,
                 GError **error)
{
	CamelStreamFsPrivate *priv;
	CamelSeekableStream *seekable;
	gssize nwritten;

	priv = CAMEL_STREAM_FS_GET_PRIVATE (stream);
	seekable = CAMEL_SEEKABLE_STREAM (stream);

	if (seekable->bound_end != CAMEL_STREAM_UNBOUND)
		n = MIN (seekable->bound_end - seekable->position, n);

	if ((nwritten = camel_write (priv->fd, buffer, n, error)) > 0)
		seekable->position += nwritten;

	return nwritten;
}

static gint
stream_fs_flush (CamelStream *stream,
                 GError **error)
{
	CamelStreamFsPrivate *priv;

	priv = CAMEL_STREAM_FS_GET_PRIVATE (stream);

	if (fsync (priv->fd) == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"%s", g_strerror (errno));
		return -1;
	}

	return 0;
}

static gint
stream_fs_close (CamelStream *stream,
                 GError **error)
{
	CamelStreamFsPrivate *priv;

	priv = CAMEL_STREAM_FS_GET_PRIVATE (stream);

	if (close (priv->fd) == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"%s", g_strerror (errno));
		return -1;
	}

	priv->fd = -1;

	return 0;
}

static goffset
stream_fs_seek (CamelSeekableStream *stream,
                goffset offset,
                CamelStreamSeekPolicy policy,
                GError **error)
{
	CamelStreamFsPrivate *priv;
	goffset real = 0;

	priv = CAMEL_STREAM_FS_GET_PRIVATE (stream);

	switch (policy) {
	case CAMEL_STREAM_SET:
		real = offset;
		break;
	case CAMEL_STREAM_CUR:
		real = stream->position + offset;
		break;
	case CAMEL_STREAM_END:
		if (stream->bound_end == CAMEL_STREAM_UNBOUND) {
			real = lseek(priv->fd, offset, SEEK_END);
			if (real != -1) {
				if (real<stream->bound_start)
					real = stream->bound_start;
				stream->position = real;
			} else
				g_set_error (
					error, G_IO_ERROR,
					g_io_error_from_errno (errno),
					"%s", g_strerror (errno));
			return real;
		}
		real = stream->bound_end + offset;
		break;
	}

	if (stream->bound_end != CAMEL_STREAM_UNBOUND)
		real = MIN (real, stream->bound_end);
	real = MAX (real, stream->bound_start);

	real = lseek(priv->fd, real, SEEK_SET);
	if (real == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"%s", g_strerror (errno));
		return -1;
	}

	if (real != stream->position && ((CamelStream *)stream)->eos)
		((CamelStream *)stream)->eos = FALSE;

	stream->position = real;

	return real;
}

static void
camel_stream_fs_class_init (CamelStreamFsClass *class)
{
	GObjectClass *object_class;
	CamelStreamClass *stream_class;
	CamelSeekableStreamClass *seekable_stream_class;

	g_type_class_add_private (class, sizeof (CamelStreamFsPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = stream_fs_finalize;

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->read = stream_fs_read;
	stream_class->write = stream_fs_write;
	stream_class->flush = stream_fs_flush;
	stream_class->close = stream_fs_close;

	seekable_stream_class = CAMEL_SEEKABLE_STREAM_CLASS (class);
	seekable_stream_class->seek = stream_fs_seek;
}

static void
camel_stream_fs_init (CamelStreamFs *stream)
{
	stream->priv = CAMEL_STREAM_FS_GET_PRIVATE (stream);
	stream->priv->fd = -1;

	CAMEL_SEEKABLE_STREAM (stream)->bound_end = CAMEL_STREAM_UNBOUND;
}

/**
 * camel_stream_fs_new_with_fd:
 * @fd: a file descriptor
 *
 * Creates a new fs stream using the given file descriptor @fd as the
 * backing store. When the stream is destroyed, the file descriptor
 * will be closed.
 *
 * Returns: a new #CamelStreamFs
 **/
CamelStream *
camel_stream_fs_new_with_fd (gint fd)
{
	CamelStreamFsPrivate *priv;
	CamelStream *stream;
	goffset offset;

	if (fd == -1)
		return NULL;

	stream = g_object_new (CAMEL_TYPE_STREAM_FS, NULL);
	priv = CAMEL_STREAM_FS_GET_PRIVATE (stream);

	priv->fd = fd;
	offset = lseek (fd, 0, SEEK_CUR);
	if (offset == -1)
		offset = 0;
	CAMEL_SEEKABLE_STREAM (stream)->position = offset;

	return stream;
}

/**
 * camel_stream_fs_new_with_fd_and_bounds:
 * @fd: a file descriptor
 * @start: the first valid position in the file
 * @end: the first invalid position in the file, or #CAMEL_STREAM_UNBOUND
 *
 * Gets a stream associated with the given file descriptor and bounds.
 * When the stream is destroyed, the file descriptor will be closed.
 *
 * Returns: the bound stream
 **/
CamelStream *
camel_stream_fs_new_with_fd_and_bounds (gint fd,
                                        goffset start,
                                        goffset end,
                                        GError **error)
{
	CamelStream *stream;

	stream = camel_stream_fs_new_with_fd (fd);
	camel_seekable_stream_set_bounds (
		CAMEL_SEEKABLE_STREAM (stream), start, end, error);

	return stream;
}

/**
 * camel_stream_fs_new_with_name:
 * @name: a local filename
 * @flags: flags as in open(2)
 * @mode: a file mode
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #CamelStreamFs corresponding to the named file, flags,
 * and mode.
 *
 * Returns: the new stream, or %NULL on error.
 **/
CamelStream *
camel_stream_fs_new_with_name (const gchar *name,
                               gint flags,
                               mode_t mode,
                               GError **error)
{
	gint fd;

	fd = g_open (name, flags|O_BINARY, mode);
	if (fd == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"%s", g_strerror (errno));
		return NULL;
	}

	return camel_stream_fs_new_with_fd (fd);
}

/**
 * camel_stream_fs_new_with_name_and_bounds:
 * @name: a local filename
 * @flags: flags as in open(2)
 * @mode: a file mode
 * @start: the first valid position in the file
 * @end: the first invalid position in the file, or #CAMEL_STREAM_UNBOUND
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new CamelStream corresponding to the given arguments.
 *
 * Returns: the stream, or %NULL on error.
 **/
CamelStream *
camel_stream_fs_new_with_name_and_bounds (const gchar *name,
                                          gint flags,
                                          mode_t mode,
                                          goffset start,
                                          goffset end,
                                          GError **error)
{
	CamelStream *stream;
	gint retval;

	stream = camel_stream_fs_new_with_name (name, flags, mode, error);
	if (stream == NULL)
		return NULL;

	retval = camel_seekable_stream_set_bounds (
		CAMEL_SEEKABLE_STREAM (stream),
		start, end, error);

	if (retval == -1) {
		g_object_unref (stream);
		stream = NULL;
	}

	return stream;
}

/**
 * camel_stream_fs_get_fd:
 * @stream: a #CamelStream
 *
 * Since: 2.32
 **/
gint
camel_stream_fs_get_fd (CamelStreamFs *stream)
{
	g_return_val_if_fail (CAMEL_IS_STREAM_FS (stream), -1);

	return stream->priv->fd;
}

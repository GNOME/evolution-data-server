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

struct _CamelStreamFsPrivate {
	gint fd;	/* file descriptor on the underlying file */
};

static CamelSeekableStreamClass *parent_class = NULL;

static void
camel_stream_fs_finalize (CamelStreamFs *stream_fs)
{
	CamelStreamFsPrivate *priv = stream_fs->priv;

	if (priv->fd != -1)
		close (priv->fd);

	g_free (priv);
}

static gssize
stream_fs_read (CamelStream *stream,
                gchar *buffer,
                gsize n)
{
	CamelStreamFsPrivate *priv;
	CamelSeekableStream *seekable;
	gssize nread;

	priv = CAMEL_STREAM_FS (stream)->priv;
	seekable = CAMEL_SEEKABLE_STREAM (stream);

	if (seekable->bound_end != CAMEL_STREAM_UNBOUND)
		n = MIN (seekable->bound_end - seekable->position, n);

	if ((nread = camel_read (priv->fd, buffer, n)) > 0)
		seekable->position += nread;
	else if (nread == 0)
		stream->eos = TRUE;

	return nread;
}

static gssize
stream_fs_write (CamelStream *stream,
                 const gchar *buffer,
                 gsize n)
{
	CamelStreamFsPrivate *priv;
	CamelSeekableStream *seekable;
	gssize nwritten;

	priv = CAMEL_STREAM_FS (stream)->priv;
	seekable = CAMEL_SEEKABLE_STREAM (stream);

	if (seekable->bound_end != CAMEL_STREAM_UNBOUND)
		n = MIN (seekable->bound_end - seekable->position, n);

	if ((nwritten = camel_write (priv->fd, buffer, n)) > 0)
		seekable->position += nwritten;

	return nwritten;
}

static gint
stream_fs_flush (CamelStream *stream)
{
	CamelStreamFsPrivate *priv;

	priv = CAMEL_STREAM_FS (stream)->priv;

	return fsync (priv->fd);
}

static gint
stream_fs_close (CamelStream *stream)
{
	CamelStreamFsPrivate *priv;

	priv = CAMEL_STREAM_FS (stream)->priv;

	if (close (priv->fd) == -1)
		return -1;

	priv->fd = -1;

	return 0;
}

static off_t
stream_fs_seek (CamelSeekableStream *stream,
                off_t offset,
                CamelStreamSeekPolicy policy)
{
	CamelStreamFsPrivate *priv;
	off_t real = 0;

	priv = CAMEL_STREAM_FS (stream)->priv;

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
			}
			return real;
		}
		real = stream->bound_end + offset;
		break;
	}

	if (stream->bound_end != CAMEL_STREAM_UNBOUND)
		real = MIN (real, stream->bound_end);
	real = MAX (real, stream->bound_start);

	real = lseek(priv->fd, real, SEEK_SET);
	if (real == -1)
		return -1;

	if (real != stream->position && ((CamelStream *)stream)->eos)
		((CamelStream *)stream)->eos = FALSE;

	stream->position = real;

	return real;
}

static void
camel_stream_fs_class_init (CamelStreamFsClass *class)
{
	CamelStreamClass *stream_class;
	CamelSeekableStreamClass *seekable_stream_class;

	parent_class = CAMEL_SEEKABLE_STREAM_CLASS (camel_type_get_global_classfuncs (camel_seekable_stream_get_type ()));

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
	stream->priv = g_new0 (CamelStreamFsPrivate, 1);
	stream->priv->fd = -1;

	CAMEL_SEEKABLE_STREAM (stream)->bound_end = CAMEL_STREAM_UNBOUND;
}

CamelType
camel_stream_fs_get_type (void)
{
	static CamelType camel_stream_fs_type = CAMEL_INVALID_TYPE;

	if (camel_stream_fs_type == CAMEL_INVALID_TYPE) {
		camel_stream_fs_type = camel_type_register (camel_seekable_stream_get_type (), "CamelStreamFs",
							    sizeof (CamelStreamFs),
							    sizeof (CamelStreamFsClass),
							    (CamelObjectClassInitFunc) camel_stream_fs_class_init,
							    NULL,
							    (CamelObjectInitFunc) camel_stream_fs_init,
							    (CamelObjectFinalizeFunc) camel_stream_fs_finalize);
	}

	return camel_stream_fs_type;
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
	off_t offset;

	if (fd == -1)
		return NULL;

	stream = CAMEL_STREAM (camel_object_new (camel_stream_fs_get_type ()));
	priv = CAMEL_STREAM_FS (stream)->priv;

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
                                        off_t start,
                                        off_t end)
{
	CamelStream *stream;

	stream = camel_stream_fs_new_with_fd (fd);
	camel_seekable_stream_set_bounds (
		CAMEL_SEEKABLE_STREAM (stream), start, end);

	return stream;
}

/**
 * camel_stream_fs_new_with_name:
 * @name: a local filename
 * @flags: flags as in open(2)
 * @mode: a file mode
 *
 * Creates a new #CamelStreamFs corresponding to the named file, flags,
 * and mode.
 *
 * Returns: the new stream, or %NULL on error.
 **/
CamelStream *
camel_stream_fs_new_with_name (const gchar *name,
                               gint flags,
                               mode_t mode)
{
	gint fd;

	fd = g_open (name, flags|O_BINARY, mode);
	if (fd == -1)
		return NULL;

	return camel_stream_fs_new_with_fd (fd);
}

/**
 * camel_stream_fs_new_with_name_and_bounds:
 * @name: a local filename
 * @flags: flags as in open(2)
 * @mode: a file mode
 * @start: the first valid position in the file
 * @end: the first invalid position in the file, or #CAMEL_STREAM_UNBOUND
 *
 * Creates a new CamelStream corresponding to the given arguments.
 *
 * Returns: the stream, or %NULL on error.
 **/
CamelStream *
camel_stream_fs_new_with_name_and_bounds (const gchar *name,
                                          gint flags,
                                          mode_t mode,
                                          off_t start,
                                          off_t end)
{
	CamelStream *stream;

	stream = camel_stream_fs_new_with_name (name, flags, mode);
	if (stream == NULL)
		return NULL;

	camel_seekable_stream_set_bounds (
		CAMEL_SEEKABLE_STREAM (stream),
		start, end);

	return stream;
}

gint
camel_stream_fs_get_fd (CamelStreamFs *stream)
{
	g_return_val_if_fail (CAMEL_IS_STREAM_FS (stream), -1);

	return stream->priv->fd;
}

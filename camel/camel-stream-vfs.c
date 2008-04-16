/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream-vfs.c : file system based stream */

/*
 * Authors: Srinivasa Ragavan <sragavan@novell.com>
 *
 * Copyright 2006 Novell, Inc. (www.novell.com)
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

#include <glib.h>
#include <glib/gstdio.h>

#include "camel-file-utils.h"
#include "camel-operation.h"
#include "camel-private.h"
#include "camel-stream-vfs.h"

static CamelSeekableStreamClass *parent_class = NULL;

/* Returns the class for a CamelStreamVFS */
#define CSVFS_CLASS(so) CAMEL_STREAM_VFS_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static ssize_t stream_read   (CamelStream *stream, char *buffer, size_t n);
static ssize_t stream_write  (CamelStream *stream, const char *buffer, size_t n);
/* static int stream_flush  (CamelStream *stream); */
static int stream_close  (CamelStream *stream);
static off_t stream_seek (CamelSeekableStream *stream, off_t offset,
			  CamelStreamSeekPolicy policy);

static void
camel_stream_vfs_class_init (CamelStreamVFSClass *camel_stream_vfs_class)
{
	CamelSeekableStreamClass *camel_seekable_stream_class =
		CAMEL_SEEKABLE_STREAM_CLASS (camel_stream_vfs_class);
	CamelStreamClass *camel_stream_class =
		CAMEL_STREAM_CLASS (camel_stream_vfs_class);

	parent_class = CAMEL_SEEKABLE_STREAM_CLASS (camel_type_get_global_classfuncs (camel_seekable_stream_get_type ()));

	/* virtual method overload */
	camel_stream_class->read = stream_read;
	camel_stream_class->write = stream_write;
/* 	camel_stream_class->flush = stream_flush; */
	camel_stream_class->close = stream_close;

	camel_seekable_stream_class->seek = stream_seek;
}

static void
camel_stream_vfs_init (gpointer object, gpointer klass)
{
	CamelStreamVFS *stream = CAMEL_STREAM_VFS (object);

	stream->handle = GINT_TO_POINTER (-1);
	((CamelSeekableStream *)stream)->bound_end = CAMEL_STREAM_UNBOUND;
}

static void
camel_stream_vfs_finalize (CamelObject *object)
{
	CamelStreamVFS *stream_vfs = CAMEL_STREAM_VFS (object);

	if (stream_vfs->handle != GINT_TO_POINTER (-1))
		gnome_vfs_close (stream_vfs->handle);
}


CamelType
camel_stream_vfs_get_type (void)
{
	static CamelType camel_stream_vfs_type = CAMEL_INVALID_TYPE;

	if (camel_stream_vfs_type == CAMEL_INVALID_TYPE) {
		camel_stream_vfs_type = camel_type_register (camel_seekable_stream_get_type (), "CamelStreamVFS",
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
 * camel_stream_vfs_new_with_handle:
 * @handle: a GnomeVFS handle
 *
 * Creates a new fs stream using the given GnomeVFS handle @handle as the
 * backing store. When the stream is destroyed, the file descriptor
 * will be closed.
 *
 * Returns a new #CamelStreamVFS
 **/
CamelStream *
camel_stream_vfs_new_with_handle (GnomeVFSHandle *handle)
{
	CamelStreamVFS *stream_vfs;
	off_t offset;

	if (!handle)
		return NULL;

	stream_vfs = CAMEL_STREAM_VFS (camel_object_new (camel_stream_vfs_get_type ()));
	stream_vfs->handle = handle;
	gnome_vfs_seek (handle, GNOME_VFS_SEEK_CURRENT, 0);
	offset = 0;
	CAMEL_SEEKABLE_STREAM (stream_vfs)->position = offset;

	return CAMEL_STREAM (stream_vfs);
}

/**
 * camel_stream_vfs_new_with_uri:
 * @name: a file uri
 * @flags: flags as in open(2)
 * @mode: a file mode
 *
 * Creates a new #CamelStreamVFS corresponding to the named file, flags,
 * and mode.
 *
 * Returns the new stream, or %NULL on error.
 **/
CamelStream *
camel_stream_vfs_new_with_uri (const char *name, int flags, mode_t mode)
{
	GnomeVFSResult result;
	GnomeVFSHandle *handle;
	int vfs_flag = 0;

	if (flags & O_WRONLY)
		vfs_flag = vfs_flag | GNOME_VFS_OPEN_WRITE;
	if (flags & O_RDONLY)
		vfs_flag = vfs_flag | GNOME_VFS_OPEN_READ;
	if (flags & O_RDWR)
		vfs_flag = vfs_flag | GNOME_VFS_OPEN_READ |GNOME_VFS_OPEN_WRITE;

	if (flags & O_CREAT)
		result = gnome_vfs_create (&handle, name, vfs_flag, FALSE, mode);
	else
		result = gnome_vfs_open (&handle, name, vfs_flag);

	if (result != GNOME_VFS_OK) {
		return NULL;
	}

	return camel_stream_vfs_new_with_handle (handle);
}

static ssize_t
stream_read (CamelStream *stream, char *buffer, size_t n)
{
	CamelStreamVFS *stream_vfs = CAMEL_STREAM_VFS (stream);
	CamelSeekableStream *seekable = CAMEL_SEEKABLE_STREAM (stream);
	GnomeVFSFileSize nread = 0;
	GnomeVFSResult result;

	if (seekable->bound_end != CAMEL_STREAM_UNBOUND)
		n = MIN (seekable->bound_end - seekable->position, n);

	result = gnome_vfs_read (stream_vfs->handle, buffer, n, &nread);

	if (nread > 0 && result == GNOME_VFS_OK)
		seekable->position += nread;
	else if (nread == 0)
		stream->eos = TRUE;

	return nread;
}

static ssize_t
stream_write (CamelStream *stream, const char *buffer, size_t n)
{
	CamelStreamVFS *stream_vfs = CAMEL_STREAM_VFS (stream);
	CamelSeekableStream *seekable = CAMEL_SEEKABLE_STREAM (stream);
	GnomeVFSFileSize nwritten = 0;
	GnomeVFSResult result;

	if (seekable->bound_end != CAMEL_STREAM_UNBOUND)
		n = MIN (seekable->bound_end - seekable->position, n);

	result = gnome_vfs_write (stream_vfs->handle, buffer, n, &nwritten);

	if (nwritten > 0 && result == GNOME_VFS_OK)
		seekable->position += nwritten;

	return nwritten;
}

/* static int */
/* stream_flush (CamelStream *stream) */
/* { */
/* 	return fsync(((CamelStreamVFS *)stream)->handle); */
/* } */

static int
stream_close (CamelStream *stream)
{
	GnomeVFSResult result;

	result = gnome_vfs_close(((CamelStreamVFS *)stream)->handle);

	if (result != GNOME_VFS_OK)
		return -1;

	((CamelStreamVFS *)stream)->handle = NULL;
	return 0;
}

static off_t
stream_seek (CamelSeekableStream *stream, off_t offset, CamelStreamSeekPolicy policy)
{
	CamelStreamVFS *stream_vfs = CAMEL_STREAM_VFS (stream);
	GnomeVFSFileSize real = 0;
	GnomeVFSResult result;
	GnomeVFSHandle *handle = stream_vfs->handle;

	switch (policy) {
	case CAMEL_STREAM_SET:
		real = offset;
		break;
	case CAMEL_STREAM_CUR:
		real = stream->position + offset;
		break;
	case CAMEL_STREAM_END:
		if (stream->bound_end == CAMEL_STREAM_UNBOUND) {
			result = gnome_vfs_seek (handle, GNOME_VFS_SEEK_END, offset);
			if (result != GNOME_VFS_OK)
				return -1;
			gnome_vfs_tell (handle, &real);
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

	result = gnome_vfs_seek (handle, GNOME_VFS_SEEK_START, real);
	if (result != GNOME_VFS_OK)
		return -1;

	if (real != stream->position && ((CamelStream *)stream)->eos)
		((CamelStream *)stream)->eos = FALSE;

	stream->position = real;

	return real;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream-vfs.h :stream based on unix filesystem */

/*
 * Author:
 *  Srinivasa Ragavan <sragavan@novell.com>
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


#ifndef CAMEL_STREAM_VFS_H
#define CAMEL_STREAM_VFS_H 1

/* for open flags */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgnomevfs/gnome-vfs.h>

#include <camel/camel-seekable-stream.h>

#define CAMEL_STREAM_VFS_TYPE     (camel_stream_vfs_get_type ())
#define CAMEL_STREAM_VFS(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_STREAM_VFS_TYPE, CamelStreamVFS))
#define CAMEL_STREAM_VFS_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_STREAM_VFS_TYPE, CamelStreamVFSClass))
#define CAMEL_IS_STREAM_VFS(o)    (CAMEL_CHECK_TYPE((o), CAMEL_STREAM_VFS_TYPE))

G_BEGIN_DECLS

typedef struct _CamelStreamVFS CamelStreamVFS;

struct _CamelStreamVFS {
	CamelSeekableStream parent_object;

	GnomeVFSHandle *handle;
};

typedef struct {
	CamelSeekableStreamClass parent_class;

} CamelStreamVFSClass;

/* Standard Camel function */
CamelType camel_stream_vfs_get_type (void);

/* public methods */
CamelStream * camel_stream_vfs_new_with_uri            (const char *uri, int flags, mode_t mode);
CamelStream * camel_stream_vfs_new_with_handle         (GnomeVFSHandle *handle);

G_END_DECLS

#endif /* CAMEL_STREAM_VFS_H */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream-vfs.h :stream based on unix filesystem */

/*
 * Author:
 *  Srinivasa Ragavan <sragavan@novell.com>
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_STREAM_VFS_H
#define CAMEL_STREAM_VFS_H

#include <camel/camel-stream.h>

/* Standard GObject macros */
#define CAMEL_TYPE_STREAM_VFS \
	(camel_stream_vfs_get_type ())
#define CAMEL_STREAM_VFS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_STREAM_VFS, CamelStreamVFS))
#define CAMEL_STREAM_VFS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_STREAM_VFS, CamelStreamVFSClass))
#define CAMEL_IS_STREAM_VFS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_STREAM_VFS))
#define CAMEL_IS_STREAM_VFS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_STREAM_VFS))
#define CAMEL_STREAM_VFS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_STREAM_VFS, CamelStreamVFSClass))

G_BEGIN_DECLS

typedef struct _CamelStreamVFS CamelStreamVFS;
typedef struct _CamelStreamVFSClass CamelStreamVFSClass;

struct _CamelStreamVFS {
	CamelStream parent;

	GObject *stream;
};

struct _CamelStreamVFSClass {
	CamelStreamClass parent_class;
};

GType camel_stream_vfs_get_type (void);

/**
 * CamelStreamVFSOpenMethod:
 * CAMEL_STREAM_VFS_CREATE:
 * 	Writable, creates new file or replaces old file.
 * CAMEL_STREAM_VFS_APPEND:
 * 	Writable, creates new file or appends at the end of the old file.
 * CAMEL_STREAM_VFS_READ:
 * 	Readable, opens existing file for reading.
 *
 * Since: 2.24
 **/
typedef enum {
	CAMEL_STREAM_VFS_CREATE,
	CAMEL_STREAM_VFS_APPEND,
	CAMEL_STREAM_VFS_READ
} CamelStreamVFSOpenMethod;

/* public methods */
CamelStream * camel_stream_vfs_new_with_uri            (const gchar *uri, CamelStreamVFSOpenMethod mode);
CamelStream * camel_stream_vfs_new_with_stream         (GObject *stream);

gboolean      camel_stream_vfs_is_writable             (CamelStreamVFS *stream_vfs);

G_END_DECLS

#endif /* CAMEL_STREAM_VFS_H */

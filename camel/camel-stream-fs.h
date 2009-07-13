/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream-fs.h :stream based on unix filesystem */

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

#ifndef CAMEL_STREAM_FS_H
#define CAMEL_STREAM_FS_H 1

/* for open flags */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <camel/camel-seekable-stream.h>

#define CAMEL_STREAM_FS_TYPE     (camel_stream_fs_get_type ())
#define CAMEL_STREAM_FS(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_STREAM_FS_TYPE, CamelStreamFs))
#define CAMEL_STREAM_FS_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_STREAM_FS_TYPE, CamelStreamFsClass))
#define CAMEL_IS_STREAM_FS(o)    (CAMEL_CHECK_TYPE((o), CAMEL_STREAM_FS_TYPE))

G_BEGIN_DECLS

struct _CamelStreamFs {
	CamelSeekableStream parent_object;

	gint fd;             /* file descriptor on the underlying file */
};

typedef struct {
	CamelSeekableStreamClass parent_class;

} CamelStreamFsClass;

/* Standard Camel function */
CamelType camel_stream_fs_get_type (void);

/* public methods */
CamelStream * camel_stream_fs_new_with_name            (const gchar *name, gint flags, mode_t mode);
CamelStream * camel_stream_fs_new_with_name_and_bounds (const gchar *name, gint flags, mode_t mode,
							off_t start, off_t end);

CamelStream * camel_stream_fs_new_with_fd              (gint fd);
CamelStream * camel_stream_fs_new_with_fd_and_bounds   (gint fd, off_t start, off_t end);

G_END_DECLS

#endif /* CAMEL_STREAM_FS_H */

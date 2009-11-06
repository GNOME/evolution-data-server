/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * Authors:
 *   Michael Zucchi <notzed@ximian.com>
 *   Jeffrey Stedfast <fejj@ximian.com>
 *   Dan Winship <danw@ximian.com>
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

#ifndef CAMEL_FILE_UTILS_H
#define CAMEL_FILE_UTILS_H 1

#include <glib.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <fcntl.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

G_BEGIN_DECLS

gint camel_file_util_encode_fixed_int32 (FILE *out, gint32 value);
gint camel_file_util_decode_fixed_int32 (FILE *in, gint32 *dest);
gint camel_file_util_encode_uint32 (FILE *out, guint32 value);
gint camel_file_util_decode_uint32 (FILE *in, guint32 *dest);
gint camel_file_util_encode_time_t (FILE *out, time_t value);
gint camel_file_util_decode_time_t (FILE *in, time_t *dest);
gint camel_file_util_encode_off_t (FILE *out, off_t value);
gint camel_file_util_decode_off_t (FILE *in, off_t *dest);
gint camel_file_util_encode_gsize (FILE *out, gsize value);
gint camel_file_util_decode_gsize (FILE *in, gsize *dest);
gint camel_file_util_encode_string (FILE *out, const gchar *str);
gint camel_file_util_decode_string (FILE *in, gchar **str);
gint camel_file_util_encode_fixed_string (FILE *out, const gchar *str, gsize len);
gint camel_file_util_decode_fixed_string (FILE *in, gchar **str, gsize len);

gchar *camel_file_util_safe_filename (const gchar *name);

/* Code that intends to be portable to Win32 should use camel_read()
 * and camel_write() only on file descriptors returned from open(),
 * creat(), pipe() or fileno(). On Win32 camel_read() and
 * camel_write() calls will not be cancellable. For sockets, use
 * camel_read_socket() and camel_write_socket(). These are cancellable
 * also on Win32.
 */
gssize camel_read (gint fd, gchar *buf, gsize n);
gssize camel_write (gint fd, const gchar *buf, gsize n);

gssize camel_read_socket (gint fd, gchar *buf, gsize n);
gssize camel_write_socket (gint fd, const gchar *buf, gsize n);

gchar *camel_file_util_savename(const gchar *filename);

#ifndef CAMEL_DISABLE_DEPRECATED
gint camel_mkdir (const gchar *path, mode_t mode);
#endif

G_END_DECLS

#endif /* CAMEL_FILE_UTILS_H */

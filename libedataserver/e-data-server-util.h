/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
 */

#ifndef __E_DATA_SERVER_UTIL_H__
#define __E_DATA_SERVER_UTIL_H__

#include <pthread.h>
#include <sys/types.h>
#include <glib.h>

G_BEGIN_DECLS

struct tm;

#ifndef EDS_DISABLE_DEPRECATED
G_GNUC_DEPRECATED
gint          e_util_mkdir_hier (const gchar *path, mode_t mode);
#endif

gchar       *e_util_strstrcase (const gchar *haystack, const gchar *needle);
gchar       *e_util_unicode_get_utf8 (const gchar *text, gunichar *out);
const gchar *e_util_utf8_strstrcase (const gchar *haystack, const gchar *needle);
const gchar *e_util_utf8_strstrcasedecomp (const gchar *haystack, const gchar *needle);
gint	     e_util_utf8_strcasecmp (const gchar *s1, const gchar *s2);
gchar       *e_util_utf8_remove_accents (const gchar *str);
guint64      e_util_pthread_id (pthread_t t);
void         e_filename_make_safe (gchar *string);

gsize e_utf8_strftime(gchar *s, gsize max, const gchar *fmt, const struct tm *tm);
gsize e_strftime(gchar *s, gsize max, const gchar *fmt, const struct tm *tm);

#ifdef G_OS_WIN32
const gchar  *e_util_get_prefix (void) G_GNUC_CONST;
const gchar  *e_util_get_cp_prefix (void) G_GNUC_CONST;
const gchar  *e_util_get_localedir (void) G_GNUC_CONST;
gchar        *e_util_replace_prefix (const gchar *configure_time_prefix, const gchar *runtime_prefix, const gchar *configure_time_path);
#endif

G_END_DECLS

#endif

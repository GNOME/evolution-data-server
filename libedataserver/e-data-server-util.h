/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2003 Novell Inc.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
 */

#ifndef __E_DATA_SERVER_UTIL_H__
#define __E_DATA_SERVER_UTIL_H__

#include <pthread.h>
#include <sys/types.h>
#include <glib/gmacros.h>
#include <glib/gtypes.h>
#include <glib/gunicode.h>

G_BEGIN_DECLS

struct tm;

int          e_util_mkdir_hier (const char *path, mode_t mode);

gchar       *e_util_strstrcase (const gchar *haystack, const gchar *needle);
gchar       *e_util_unicode_get_utf8 (const gchar *text, gunichar *out);
const gchar *e_util_utf8_strstrcase (const gchar *haystack, const gchar *needle);
const gchar *e_util_utf8_strstrcasedecomp (const gchar *haystack, const gchar *needle);
int	     e_util_utf8_strcasecmp (const gchar *s1, const gchar *s2);
guint64      e_util_pthread_id (pthread_t t);
void         e_filename_make_safe (gchar *string);

size_t e_utf8_strftime(char *s, size_t max, const char *fmt, const struct tm *tm);
size_t e_strftime(char *s, size_t max, const char *fmt, const struct tm *tm);

#ifdef G_OS_WIN32
const char  *e_util_get_prefix (void) G_GNUC_CONST;
const char  *e_util_get_cp_prefix (void) G_GNUC_CONST;
const char  *e_util_get_localedir (void) G_GNUC_CONST;
char        *e_util_replace_prefix (const char *configure_time_prefix, const char *runtime_prefix, const char *configure_time_path);
#endif

G_END_DECLS

#endif

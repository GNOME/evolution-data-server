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

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_DATA_SERVER_UTIL_H
#define E_DATA_SERVER_UTIL_H

#include <sys/types.h>
#include <gio/gio.h>

G_BEGIN_DECLS

struct tm;

const gchar *	e_get_user_cache_dir		(void);
const gchar *	e_get_user_config_dir		(void);
const gchar *	e_get_user_data_dir		(void);

gchar *		e_util_strdup_strip		(const gchar *string);
gchar *		e_util_strstrcase		(const gchar *haystack,
						 const gchar *needle);
gchar *		e_util_unicode_get_utf8		(const gchar *text,
						 gunichar *out);
const gchar *	e_util_utf8_strstrcase		(const gchar *haystack,
						 const gchar *needle);
const gchar *	e_util_utf8_strstrcasedecomp	(const gchar *haystack,
						 const gchar *needle);
gint		e_util_utf8_strcasecmp		(const gchar *s1,
						 const gchar *s2);
gchar *		e_util_utf8_remove_accents	(const gchar *str);
gchar *		e_util_utf8_make_valid		(const gchar *str);
gchar *		e_util_utf8_data_make_valid	(const gchar *data,
						 gsize data_bytes);
const gchar *   e_util_ensure_gdbus_string	(const gchar *str,
						 gchar **gdbus_str);
guint64		e_util_gthread_id		(GThread *thread);
void		e_filename_make_safe		(gchar *string);
gchar *		e_filename_mkdir_encoded	(const gchar *basepath,
						 const gchar *fileprefix,
						 const gchar *filename,
						 gint fileindex);

gsize		e_utf8_strftime			(gchar *string,
						 gsize max,
						 const gchar *fmt,
						 const struct tm *tm);
gsize		e_strftime			(gchar *string,
						 gsize max,
						 const gchar *fmt,
						 const struct tm *tm);

gchar **	e_util_slist_to_strv		(const GSList *strings);
GSList *	e_util_strv_to_slist		(const gchar * const *strv);
GSList *	e_util_copy_string_slist	(GSList *copy_to, const GSList *strings);
GSList *	e_util_copy_object_slist	(GSList *copy_to, const GSList *objects);
void		e_util_free_string_slist	(GSList *strings);
void		e_util_free_object_slist	(GSList *objects);
void		e_util_free_nullable_object_slist	(GSList *objects);

gboolean	e_file_recursive_delete_sync	(GFile *file,
						 GCancellable *cancellable,
						 GError **error);
void		e_file_recursive_delete		(GFile *file,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_file_recursive_delete_finish	(GFile *file,
						 GAsyncResult *result,
						 GError **error);

/* Useful GBinding transform functions */
gboolean	e_binding_transform_enum_value_to_nick
						(GBinding *binding,
						 const GValue *source_value,
						 GValue *target_value,
						 gpointer not_used);
gboolean	e_binding_transform_enum_nick_to_value
						(GBinding *binding,
						 const GValue *source_value,
						 GValue *target_value,
						 gpointer not_used);

typedef struct _EAsyncClosure EAsyncClosure;

EAsyncClosure *	e_async_closure_new		(void);
GAsyncResult *	e_async_closure_wait		(EAsyncClosure *closure);
void		e_async_closure_free		(EAsyncClosure *closure);
void		e_async_closure_callback	(GObject *object,
						 GAsyncResult *result,
						 gpointer closure);

#ifdef G_OS_WIN32
const gchar *	e_util_get_prefix		(void) G_GNUC_CONST;
const gchar *	e_util_get_cp_prefix		(void) G_GNUC_CONST;
const gchar *	e_util_get_localedir		(void) G_GNUC_CONST;
gchar *		e_util_replace_prefix		(const gchar *configure_time_prefix,
						 const gchar *runtime_prefix,
						 const gchar *configure_time_path);
#endif

gint		e_data_server_util_get_dbus_call_timeout
						(void);
void		e_data_server_util_set_dbus_call_timeout
						(gint timeout_msec);

G_END_DECLS

#endif /* E_DATA_SERVER_UTIL_H */

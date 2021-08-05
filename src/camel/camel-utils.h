/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2016 Red Hat, Inc. (www.redhat.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_UTILS_H
#define CAMEL_UTILS_H

#include <glib-object.h>
#include <time.h>
#include <camel/camel-enums.h>
#include <camel/camel-message-info.h>
#include <camel/camel-name-value-array.h>

#define CAMEL_UTILS_MAX_USER_HEADERS 3

G_BEGIN_DECLS

gint64		camel_util_bdata_get_number	(/* const */ gchar **bdata_ptr,
						 gint64 default_value);
void		camel_util_bdata_put_number	(GString *bdata_str,
						 gint64 value);
gchar *		camel_util_bdata_get_string	(/* const */ gchar **bdata_ptr,
						 const gchar *default_value);
void		camel_util_bdata_put_string	(GString *bdata_str,
						 const gchar *value);

time_t		camel_time_value_apply		(time_t src_time,
						 CamelTimeUnit unit,
						 gint value);

GWeakRef *	camel_utils_weak_ref_new	(gpointer object);
void		camel_utils_weak_ref_free	(GWeakRef *weak_ref);

gboolean	camel_util_fill_message_info_user_headers
						(CamelMessageInfo *info,
						 const CamelNameValueArray *headers);
gchar *		camel_util_encode_user_header_setting
						(const gchar *display_name,
						 const gchar *header_name);
void		camel_util_decode_user_header_setting
						(const gchar *setting_value,
						 gchar **out_display_name,
						 const gchar **out_header_name);

G_END_DECLS

#endif /* CAMEL_UTILS_H */

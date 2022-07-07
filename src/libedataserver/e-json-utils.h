/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_JSON_UTILS_H
#define E_JSON_UTILS_H

#ifndef __GI_SCANNER__

#include <json-glib/json-glib.h>

G_BEGIN_DECLS

JsonArray *	e_json_get_array_member			(JsonObject *object,
							 const gchar *member_name);
void		e_json_begin_array_member		(JsonBuilder *builder,
							 const gchar *member_name);
void		e_json_end_array_member			(JsonBuilder *builder);
gboolean	e_json_get_boolean_member		(JsonObject *object,
							 const gchar *member_name,
							 gboolean default_value);
void		e_json_add_boolean_member		(JsonBuilder *builder,
							 const gchar *member_name,
							 gboolean value);
gdouble		e_json_get_double_member		(JsonObject *object,
							 const gchar *member_name,
							 gdouble default_value);
void		e_json_add_double_member		(JsonBuilder *builder,
							 const gchar *member_name,
							 gdouble value);
gint64		e_json_get_int_member			(JsonObject *object,
							 const gchar *member_name,
							 gint64 default_value);
void		e_json_add_int_member			(JsonBuilder *builder,
							 const gchar *member_name,
							 gint64 value);
gboolean	e_json_get_null_member			(JsonObject *object,
							 const gchar *member_name,
							 gboolean default_value);
void		e_json_add_null_member			(JsonBuilder *builder,
							 const gchar *member_name);
JsonObject *	e_json_get_object_member		(JsonObject *object,
							 const gchar *member_name);
void		e_json_begin_object_member		(JsonBuilder *builder,
							 const gchar *member_name);
void		e_json_end_object_member		(JsonBuilder *builder);
const gchar *	e_json_get_string_member		(JsonObject *object,
							 const gchar *member_name,
							 const gchar *default_value);
void		e_json_add_string_member		(JsonBuilder *builder,
							 const gchar *member_name,
							 const gchar *value);
void		e_json_add_nonempty_string_member	(JsonBuilder *builder,
							 const gchar *member_name,
							 const gchar *value);
void		e_json_add_nonempty_or_null_string_member
							(JsonBuilder *builder,
							 const gchar *member_name,
							 const gchar *value);
gint64		e_json_get_date_member			(JsonObject *object,
							 const gchar *member_name,
							 gint64 default_value);
void		e_json_add_date_member			(JsonBuilder *builder,
							 const gchar *member_name,
							 gint64 value);
gint64		e_json_get_iso8601_member		(JsonObject *object,
							 const gchar *member_name,
							 gint64 default_value);
void		e_json_add_iso8601_member		(JsonBuilder *builder,
							 const gchar *member_name,
							 gint64 value);

gint64		e_json_util_decode_date			(const gchar *str_date,
							 gint64 default_value);
gchar *		e_json_util_encode_date			(gint64 value);
gint64		e_json_util_decode_iso8601		(const gchar *str_datetime,
							 gint64 default_value);
gchar *		e_json_util_encode_iso8601		(gint64 value);

#endif /* __GI_SCANNER__ */

G_END_DECLS

#endif /* E_JSON_UTILS_H */

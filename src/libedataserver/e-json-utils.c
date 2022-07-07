/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * SECTION: e-json-utils
 * @include: libedataserver/libedataserver.h
 * @short_description: A set of JSON utility functions
 **/

#include "evolution-data-server-config.h"

#include <json-glib/json-glib.h>

#include "e-json-utils.h"

/**
 * e_json_get_array_member: (skip)
 * @object: a #JsonObject object
 * @member_name: name of the member
 *
 * Returns the @member_name of the @object as a #JsonArray,
 * checking the type is as expected. Asking for an array
 * when the member holds a different type is considered
 * a programmer error.
 *
 * Returns: (transfer none) (nullable): the @member_name of the @object
 *   as a #JsonArray, or %NULL when the member cannot be found or when
 *   it is a JSON-NULL object.
 *
 * Since: 3.46
 **/
JsonArray *
e_json_get_array_member (JsonObject *object,
			 const gchar *member_name)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, NULL);
	g_return_val_if_fail (member_name != NULL, NULL);

	node = json_object_get_member (object, member_name);

	if (!node || JSON_NODE_HOLDS_NULL (node))
		return NULL;

	g_return_val_if_fail (JSON_NODE_HOLDS_ARRAY (node), NULL);

	return json_node_get_array (node);
}

/**
 * e_json_begin_array_member: (skip)
 * @builder: a #JsonBuilder
 * @member_name: (nullable): name of the array
 *
 * Begins a new array object, optionally named @member_name.
 * The @member_name can be %NULL or an empty string,
 * in which case the new array has no name (unless the @builder
 * has already set the member name).
 *
 * End the array object with e_json_end_array_member().
 *
 * Since: 3.46
 **/
void
e_json_begin_array_member (JsonBuilder *builder,
			   const gchar *member_name)
{
	g_return_if_fail (builder != NULL);

	if (member_name && *member_name)
		json_builder_set_member_name (builder, member_name);

	json_builder_begin_array (builder);
}

/**
 * e_json_end_array_member: (skip)
 * @builder: a #JsonBuilder
 *
 * Ends the array object begun with e_json_begin_array_member().
 *
 * Since: 3.46
 **/
void
e_json_end_array_member (JsonBuilder *builder)
{
	g_return_if_fail (builder != NULL);

	json_builder_end_array (builder);
}

/**
 * e_json_get_boolean_member: (skip)
 * @object: a #JsonObject object
 * @member_name: name of the member
 * @default_value: the default value to return
 *
 * Returns the @member_name of the @object as a gboolean,
 * checking the type is as expected. Asking for a gboolean
 * when the member holds a different type is considered
 * a programmer error.
 *
 * Returns: the @member_name of the @object as a gboolean, or the @default_value
 *   when the member cannot be found or when it is a JSON-NULL object.
 *
 * Since: 3.46
 **/
gboolean
e_json_get_boolean_member (JsonObject *object,
			   const gchar *member_name,
			   gboolean default_value)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, default_value);
	g_return_val_if_fail (member_name != NULL, default_value);

	node = json_object_get_member (object, member_name);

	if (!node || JSON_NODE_HOLDS_NULL (node))
		return default_value;

	g_return_val_if_fail (JSON_NODE_HOLDS_VALUE (node), default_value);

	return json_node_get_boolean (node);
}

/**
 * e_json_add_boolean_member: (skip)
 * @builder: a #JsonBuilder
 * @member_name: (not nullable): member name
 * @value: value to set
 *
 * Adds a boolean member named @member_name, holding the @value.
 * The @member_name cannot be %NULL nor an empty string.
 *
 * Since: 3.46
 **/
void
e_json_add_boolean_member (JsonBuilder *builder,
			   const gchar *member_name,
			   gboolean value)
{
	g_return_if_fail (builder != NULL);
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_boolean_value (builder, value);
}

/**
 * e_json_get_double_member: (skip)
 * @object: a #JsonObject object
 * @member_name: name of the member
 * @default_value: the default value to return
 *
 * Returns the @member_name of the @object as a gdouble,
 * checking the type is as expected. Asking for a gdouble
 * when the member holds a different type is considered
 * a programmer error.
 *
 * Returns: the @member_name of the @object as a gdouble, or the @default_value
 *   when the member cannot be found or when it is a JSON-NULL object.
 *
 * Since: 3.46
 **/
gdouble
e_json_get_double_member (JsonObject *object,
			  const gchar *member_name,
			  gdouble default_value)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, default_value);
	g_return_val_if_fail (member_name != NULL, default_value);

	node = json_object_get_member (object, member_name);

	if (!node || JSON_NODE_HOLDS_NULL (node))
		return default_value;

	g_return_val_if_fail (JSON_NODE_HOLDS_VALUE (node), default_value);

	return json_node_get_double (node);
}

/**
 * e_json_add_double_member: (skip)
 * @builder: a #JsonBuilder
 * @member_name: (not nullable): member name
 * @value: value to set
 *
 * Adds a double member named @member_name, holding the @value.
 * The @member_name cannot be %NULL nor an empty string.
 *
 * Since: 3.46
 **/
void
e_json_add_double_member (JsonBuilder *builder,
			  const gchar *member_name,
			  gdouble value)
{
	g_return_if_fail (builder != NULL);
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_double_value (builder, value);
}

/**
 * e_json_get_int_member: (skip)
 * @object: a #JsonObject object
 * @member_name: name of the member
 * @default_value: the default value to return
 *
 * Returns the @member_name of the @object as a gint64,
 * checking the type is as expected. Asking for a gint64
 * when the member holds a different type is considered
 * a programmer error.
 *
 * Returns: the @member_name of the @object as a gint64, or the @default_value
 *   when the member cannot be found or when it is a JSON-NULL object.
 *
 * Since: 3.46
 **/
gint64
e_json_get_int_member (JsonObject *object,
		       const gchar *member_name,
		       gint64 default_value)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, default_value);
	g_return_val_if_fail (member_name != NULL, default_value);

	node = json_object_get_member (object, member_name);

	if (!node || JSON_NODE_HOLDS_NULL (node))
		return default_value;

	g_return_val_if_fail (JSON_NODE_HOLDS_VALUE (node), default_value);

	return json_node_get_int (node);
}

/**
 * e_json_add_int_member: (skip)
 * @builder: a #JsonBuilder
 * @member_name: (not nullable): member name
 * @value: value to set
 *
 * Adds an integer member named @member_name, holding the @value.
 * The @member_name cannot be %NULL nor an empty string.
 *
 * Since: 3.46
 **/
void
e_json_add_int_member (JsonBuilder *builder,
		       const gchar *member_name,
		       gint64 value)
{
	g_return_if_fail (builder != NULL);
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_int_value (builder, value);
}

/**
 * e_json_get_null_member: (skip)
 * @object: a #JsonObject object
 * @member_name: name of the member
 * @default_value: the default value to return
 *
 * Returns whether the @member_name of the @object is a JSON-NULL object,
 * or the @default_value, when the @member_name does not exist.
 *
 * Returns: whether the @member_name of the @object is a JSON-NULL object,
 *   or the @default_value when the member cannot be found.
 *
 * Since: 3.46
 **/
gboolean
e_json_get_null_member (JsonObject *object,
			const gchar *member_name,
			gboolean default_value)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, default_value);
	g_return_val_if_fail (member_name != NULL, default_value);

	node = json_object_get_member (object, member_name);

	if (!node)
		return default_value;

	g_return_val_if_fail (JSON_NODE_HOLDS_NULL (node), default_value);

	return json_node_is_null (node);
}

/**
 * e_json_add_null_member: (skip)
 * @builder: a #JsonBuilder
 * @member_name: (not nullable): member name
 *
 * Adds a JSON-NULL member named @member_name.
 * The @member_name cannot be %NULL nor an empty string.
 *
 * Since: 3.46
 **/
void
e_json_add_null_member (JsonBuilder *builder,
			const gchar *member_name)
{
	g_return_if_fail (builder != NULL);
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_null_value (builder);
}

/**
 * e_json_get_object_member: (skip)
 * @object: a #JsonObject object
 * @member_name: name of the member
 *
 * Returns the @member_name of the @object as a #JsonObject,
 * checking the type is as expected. Asking for an object
 * when the member holds a different type is considered
 * a programmer error.
 *
 * Returns: (transfer none) (nullable): the @member_name of the @object
 *   as a #JsonObject, or %NULL when the member cannot be found or when
 *   it is a JSON-NULL object.
 *
 * Since: 3.46
 **/
JsonObject *
e_json_get_object_member (JsonObject *object,
			  const gchar *member_name)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, NULL);
	g_return_val_if_fail (member_name != NULL, NULL);

	node = json_object_get_member (object, member_name);

	if (!node || JSON_NODE_HOLDS_NULL (node))
		return NULL;

	g_return_val_if_fail (JSON_NODE_HOLDS_OBJECT (node), NULL);

	return json_node_get_object (node);
}

/**
 * e_json_begin_object_member: (skip)
 * @builder: a #JsonBuilder
 * @member_name: (nullable): name of the object
 *
 * Begins a new object, optionally named @member_name.
 * The @member_name can be %NULL or an empty string,
 * in which case the new object has no name (unless the @builder
 * has already set the member name).
 *
 * End the object with e_json_end_object_member().
 *
 * Since: 3.46
 **/
void
e_json_begin_object_member (JsonBuilder *builder,
			    const gchar *member_name)
{
	g_return_if_fail (builder != NULL);

	if (member_name && *member_name)
		json_builder_set_member_name (builder, member_name);

	json_builder_begin_object (builder);
}

/**
 * e_json_end_object_member: (skip)
 * @builder: a #JsonBuilder
 *
 * Ends the object begun with e_json_begin_object_member().
 *
 * Since: 3.46
 **/
void
e_json_end_object_member (JsonBuilder *builder)
{
	g_return_if_fail (builder != NULL);

	json_builder_end_object (builder);
}

/**
 * e_json_get_string_member: (skip)
 * @object: a #JsonObject object
 * @member_name: name of the member
 * @default_value: (nullable): the default value to return
 *
 * Returns the @member_name of the @object as a string,
 * checking the type is as expected. Asking for a string
 * when the member holds a different type is considered
 * a programmer error.
 *
 * Returns: (transfer none) (nullable): the @member_name of the @object as string,
 *   or the @default_value when the member cannot be found or when it is a JSON-NULL object.
 *
 * Since: 3.46
 **/
const gchar *
e_json_get_string_member (JsonObject *object,
			  const gchar *member_name,
			  const gchar *default_value)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, default_value);
	g_return_val_if_fail (member_name != NULL, default_value);

	node = json_object_get_member (object, member_name);

	if (!node || JSON_NODE_HOLDS_NULL (node))
		return default_value;

	g_return_val_if_fail (JSON_NODE_HOLDS_VALUE (node), default_value);

	return json_node_get_string (node);
}

/**
 * e_json_add_string_member: (skip)
 * @builder: a #JsonBuilder
 * @member_name: (not nullable): member name
 * @value: (nullable): value to set
 *
 * Adds a string member named @member_name, holding the @value.
 * The @member_name cannot be %NULL nor an empty string.
 * When the @value is %NULL, an empty string is set instead.
 *
 * See e_json_add_nonempty_string_member(), e_json_add_nonempty_or_null_string_member().
 *
 * Since: 3.46
 **/
void
e_json_add_string_member (JsonBuilder *builder,
			  const gchar *member_name,
			  const gchar *value)
{
	g_return_if_fail (builder != NULL);
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_string_value (builder, value ? value : "");
}

/**
 * e_json_add_nonempty_string_member: (skip)
 * @builder: a #JsonBuilder
 * @member_name: (not nullable): member name
 * @value: (nullable): value to set
 *
 * Adds a string member named @member_name, holding the @value,
 * but only if the @value is a non-empty string.
 * The @member_name cannot be %NULL nor an empty string.
 *
 * See e_json_add_string_member(), e_json_add_nonempty_or_null_string_member().
 *
 * Since: 3.46
 **/
void
e_json_add_nonempty_string_member (JsonBuilder *builder,
				   const gchar *member_name,
				   const gchar *value)
{
	g_return_if_fail (builder != NULL);
	g_return_if_fail (member_name && *member_name);

	if (value && *value)
		e_json_add_string_member (builder, member_name, value);
}

/**
 * e_json_add_nonempty_or_null_string_member: (skip)
 * @builder: a #JsonBuilder
 * @member_name: (not nullable): member name
 * @value: (nullable): value to set
 *
 * Adds a string member named @member_name, holding the @value,
 * if the @value is a non-empty string, otherwise adds a JSON-NULL
 * object.
 *
 * The @member_name cannot be %NULL nor an empty string.
 *
 * See e_json_add_string_member(), e_json_add_nonempty_string_member().
 *
 * Since: 3.46
 **/
void
e_json_add_nonempty_or_null_string_member (JsonBuilder *builder,
					   const gchar *member_name,
					   const gchar *value)
{
	g_return_if_fail (builder != NULL);
	g_return_if_fail (member_name && *member_name);

	if (value && *value)
		e_json_add_string_member (builder, member_name, value);
	else
		e_json_add_null_member (builder, member_name);
}

/**
 * e_json_get_date_member: (skip)
 * @object: a #JsonObject object
 * @member_name: name of the member
 * @default_value: the default value to return
 *
 * Returns the @member_name date (encoded as YYYY-MM-DD) of the @object as
 * a gint64 Unix time. The member itself is expected to be of type string.
 *
 * Returns: the @member_name date (encoded as YYYY-MM-DD) of the @object as
 *   a gint64 Unix time, or the @default_value when the member cannot
 *   be found or when it is a JSON-NULL object.
 *
 * Since: 3.46
 **/
gint64
e_json_get_date_member (JsonObject *object,
			const gchar *member_name,
			gint64 default_value)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, default_value);
	g_return_val_if_fail (member_name != NULL, default_value);

	node = json_object_get_member (object, member_name);

	if (!node || JSON_NODE_HOLDS_NULL (node) || !JSON_NODE_HOLDS_VALUE (node))
		return default_value;

	return e_json_util_decode_date (json_node_get_string (node), default_value);
}

/**
 * e_json_add_date_member: (skip)
 * @builder: a #JsonBuilder
 * @member_name: (not nullable): member name
 * @value: value to set, as Unix time
 *
 * Adds a date (encoded as YYYY-MM-DD) member named @member_name, holding
 * the date as Unix time in the @value, stored as string.
 * The @member_name cannot be %NULL nor an empty string.
 *
 * Since: 3.46
 **/
void
e_json_add_date_member (JsonBuilder *builder,
			const gchar *member_name,
			gint64 value)
{
	gchar *str;

	g_return_if_fail (builder != NULL);
	g_return_if_fail (member_name && *member_name);

	str = e_json_util_encode_date (value);
	g_return_if_fail (str != NULL);

	e_json_add_string_member (builder, member_name, str);

	g_free (str);
}

/**
 * e_json_get_iso8601_member: (skip)
 * @object: a #JsonObject object
 * @member_name: name of the member
 * @default_value: the default value to return
 *
 * Returns the @member_name ISO 8601 date/time of the @object as
 * a gint64 Unix time. The member itself is expected to be of type string.
 *
 * Returns: the @member_name ISO 8601 date/time of the @object as
 *   a gint64 Unix time, or the @default_value when the member cannot
 *   be found or when it is a JSON-NULL object.
 *
 * Since: 3.46
 **/
gint64
e_json_get_iso8601_member (JsonObject *object,
			   const gchar *member_name,
			   gint64 default_value)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, default_value);
	g_return_val_if_fail (member_name != NULL, default_value);

	node = json_object_get_member (object, member_name);

	if (!node || JSON_NODE_HOLDS_NULL (node) || !JSON_NODE_HOLDS_VALUE (node))
		return default_value;

	return e_json_util_decode_iso8601 (json_node_get_string (node), default_value);
}

/**
 * e_json_add_iso8601_member: (skip)
 * @builder: a #JsonBuilder
 * @member_name: (not nullable): member name
 * @value: value to set, as Unix time
 *
 * Adds a date/time (encoded as ISO 8601) member named @member_name, holding
 * the Unix time in the @value, stored as string.
 * The @member_name cannot be %NULL nor an empty string.
 *
 * Since: 3.46
 **/
void
e_json_add_iso8601_member (JsonBuilder *builder,
			   const gchar *member_name,
			   gint64 value)
{
	gchar *str;

	g_return_if_fail (builder != NULL);
	g_return_if_fail (member_name && *member_name);

	str = e_json_util_encode_iso8601 (value);
	g_return_if_fail (str != NULL);

	e_json_add_string_member (builder, member_name, str);

	g_free (str);
}

/**
 * e_json_util_decode_date: (skip)
 * @str_date: (nullable): date as string
 * @default_value: the default value to return
 *
 * Returns the @str_date (encoded as YYYY-MM-DD) as a gint64 Unix time.
 *
 * Returns: the @str_date (encoded as YYYY-MM-DD) as a gint64 Unix time,
 *   or the @default_value when the @str_date cannot be decoded.
 *
 * Since: 3.46
 **/
gint64
e_json_util_decode_date (const gchar *str_date,
			 gint64 default_value)
{
	GDateTime *dt;
	gchar *tmp;
	gint64 res;

	if (!str_date || !*str_date)
		return default_value;

	tmp = g_strconcat (str_date, "T00:00:00Z", NULL);
	dt = g_date_time_new_from_iso8601 (tmp, NULL);
	g_free (tmp);

	if (dt) {
		res = g_date_time_to_unix (dt);
		g_date_time_unref (dt);
	} else {
		res = default_value;
	}

	return res;
}

/**
 * e_json_util_encode_date: (skip)
 * @value: date to encode, as Unix time
 *
 * Encodes (as YYYY-MM-DD) the date @value Unix time  to string.
 *
 * Returns: (transfer full): Unix time @value encoded as date
 *
 * Since: 3.46
 **/
gchar *
e_json_util_encode_date (gint64 value)
{
	GDateTime *dt;
	gchar *str;

	dt = g_date_time_new_from_unix_utc (value);

	g_return_val_if_fail (dt != NULL, NULL);

	str = g_strdup_printf ("%04d-%02d-%02d",
		g_date_time_get_year (dt),
		g_date_time_get_month (dt),
		g_date_time_get_day_of_month (dt));

	g_date_time_unref (dt);

	return str;
}

/**
 * e_json_decode_iso8601: (skip)
 * @str_datetime: (nullable): an ISO 8601 encoded date/time
 * @default_value: the default value to return
 *
 * Returns the @str_datetime ISO 8601 date/time as a gint64 Unix time.
 *
 * Returns: the @str_datetime ISO 8601 date/time as a gint64 Unix time,
 *   or the @default_value when the @str_datetime cannot be decoded.
 *
 * Since: 3.46
 **/
gint64
e_json_util_decode_iso8601 (const gchar *str_datetime,
			    gint64 default_value)
{
	GDateTime *dt;
	gint64 res;

	if (!str_datetime || !*str_datetime)
		return default_value;

	dt = g_date_time_new_from_iso8601 (str_datetime, NULL);

	if (dt) {
		res = g_date_time_to_unix (dt);
		g_date_time_unref (dt);
	} else {
		res = default_value;
	}

	return res;
}

/**
 * e_json_util_encode_iso8601: (skip)
 * @value: date/time to encode, as Unix time
 *
 * Encodes (as ISO 8601) the date/time @value Unix time  to string.
 *
 * Returns: (transfer full): Unix time @value encoded as date/time
 *
 * Since: 3.46
 **/
gchar *
e_json_util_encode_iso8601 (gint64 value)
{
	GDateTime *dt;
	gchar *str;

	dt = g_date_time_new_from_unix_utc (value);

	g_return_val_if_fail (dt != NULL, NULL);

	str = g_date_time_format_iso8601 (dt);

	g_date_time_unref (dt);

	return str;
}

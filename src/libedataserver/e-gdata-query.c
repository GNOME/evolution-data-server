/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2022 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * SECTION: e-gdata-query
 * @include: libedataserver/libedataserver.h
 * @short_description: A GData (Google Data) query parameters
 *
 * The #EGDataQuery is a structure to limit listing of GData objects.
 * Not every parameter can be used with every object list function.
 **/

#include "evolution-data-server-config.h"

#include <glib.h>

#include "e-json-utils.h"
#include "e-gdata-query.h"

G_DEFINE_BOXED_TYPE (EGDataQuery, e_gdata_query, e_gdata_query_ref, e_gdata_query_unref)

#define PARAM_MAX_RESULTS "maxResults"
#define PARAM_COMPLETED_MAX "completedMax"
#define PARAM_COMPLETED_MIN "completedMin"
#define PARAM_DUE_MAX "dueMax"
#define PARAM_DUE_MIN "dueMin"
#define PARAM_SHOW_COMPLETED "showCompleted"
#define PARAM_SHOW_DELETED "showDeleted"
#define PARAM_SHOW_HIDDEN "showHidden"
#define PARAM_UPDATED_MIN "updatedMin"

#define VALUE_TRUE "True"
#define VALUE_FALSE "False"

/**
 * e_gdata_query_new:
 *
 * Creates a new #EGDataQuery. Free it with e_gdata_query_unref(),
 * when no longer needed.
 *
 * Returns: (transfer full): a new #EGDataQuery
 *
 * Since: 3.46
 **/
EGDataQuery *
e_gdata_query_new (void)
{
	/* Static gchar *, param name ~> gchar *, the value */
	return (EGDataQuery *) g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
}

/**
 * e_gdata_query_ref:
 * @self: an #GEDataQuery
 *
 * Increases the reference count of the @self.
 * The added reference shuld be removed with e_gdata_query_unref().
 *
 * Returns: the @self
 *
 * Since: 3.46
 **/
EGDataQuery *
e_gdata_query_ref (EGDataQuery *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	g_hash_table_ref ((GHashTable *) self);

	return self;
}

/**
 * e_gdata_query_unref:
 * @self: an #EGDataQuery
 *
 * Decreases the reference count of the @self. When the reference count
 * reaches 0, the @self is freed.
 *
 * Since: 3.46
 **/
void
e_gdata_query_unref (EGDataQuery *self)
{
	g_return_if_fail (self != NULL);

	g_hash_table_unref ((GHashTable *) self);
}

/**
 * e_gdata_query_to_string:
 * @self: an #EGDataQuery
 *
 * Converts the @self into a string, which can be used as a URI query. The returned
 * string should be freed with g_free(), when no longer needed.
 *
 * Returns: (transfer full) (nullable): the @self converted into a string, or %NULL,
 *    when the @self doesn't have set any parameter.
 *
 * Since: 3.46
 **/
gchar *
e_gdata_query_to_string (EGDataQuery *self)
{
	GString *str = NULL;
	GHashTableIter iter;
	gpointer key, value;

	g_return_val_if_fail (self != NULL, NULL);

	g_hash_table_iter_init (&iter, (GHashTable *) self);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		gchar *escaped_value;

		if (!value)
			continue;

		escaped_value = g_uri_escape_string (value, NULL, FALSE);
		if (escaped_value) {
			if (!str)
				str = g_string_new (NULL);
			else
				g_string_append_c (str, '&');

			g_string_append_printf (str, "%s=%s", (const gchar *) key, escaped_value);
		}

		g_free (escaped_value);
	}

	if (str)
		return g_string_free (str, FALSE);

	return NULL;
}

static gboolean
e_gdata_query_get_boolean_property (EGDataQuery *self,
				    const gchar *prop_name,
				    gboolean *out_exists)
{
	gpointer value = NULL;

	g_return_val_if_fail (self != NULL, FALSE);

	if (!g_hash_table_lookup_extended ((GHashTable *) self, prop_name, NULL, &value) || !value) {
		if (out_exists)
			*out_exists = FALSE;
		return FALSE;
	}

	if (out_exists)
		*out_exists = TRUE;

	if (g_strcmp0 (value, VALUE_TRUE) == 0)
		return TRUE;

	g_warn_if_fail (g_strcmp0 (value, VALUE_FALSE) == 0);

	return FALSE;
}

static gint
e_gdata_query_get_int_property (EGDataQuery *self,
				const gchar *prop_name,
				gboolean *out_exists)
{
	gpointer value = NULL;

	g_return_val_if_fail (self != NULL, 0);

	if (!g_hash_table_lookup_extended ((GHashTable *) self, prop_name, NULL, &value) || !value) {
		if (out_exists)
			*out_exists = FALSE;
		return 0;
	}

	if (out_exists)
		*out_exists = TRUE;

	return (gint) g_ascii_strtoll (value, NULL, 10);
}

static gint64
e_gdata_query_get_datetime_property (EGDataQuery *self,
				     const gchar *prop_name,
				     gboolean *out_exists)
{
	gpointer value = NULL;

	if (!g_hash_table_lookup_extended ((GHashTable *) self, prop_name, NULL, &value) || !value) {
		if (out_exists)
			*out_exists = FALSE;
		return -1;
	}

	if (out_exists)
		*out_exists = TRUE;

	return e_json_util_decode_iso8601 (value, -1);
}

/**
 * e_gdata_query_set_max_results:
 * @self: an #EGDataQuery
 * @value: a value to set
 *
 * Sets max results to be returned in one call.
 *
 * This can be used for any object query.
 *
 * Since: 3.46
 **/
void
e_gdata_query_set_max_results (EGDataQuery *self,
			       gint value)
{
	g_return_if_fail (self != NULL);

	g_hash_table_insert ((GHashTable *) self, (gpointer) PARAM_MAX_RESULTS,
		g_strdup_printf ("%d", value));
}

/**
 * e_gdata_query_get_max_results:
 * @self: an #EDataQuery
 * @out_exists: (out) (optional): an out argument, where can
 *    be set whether the property exists, or %NULL
 *
 * Gets current value of the max results property.
 * When not set, returns 0. The optional @out_exists
 * can be used to see whether the property is set.
 *
 * Returns: the set value, or 0
 *
 * Since: 3.46
 **/
gint
e_gdata_query_get_max_results (EGDataQuery *self,
			       gboolean *out_exists)
{
	g_return_val_if_fail (self != NULL, 0);

	return e_gdata_query_get_int_property (self, PARAM_MAX_RESULTS, out_exists);
}

/**
 * e_gdata_query_set_completed_max:
 * @self: an #EGDataQuery
 * @value: a value to set, as a Unix date/time
 *
 * Sets upper bound for a task's completion date, as a Unix date/time, to filter by.
 * The default is not to filter by completion date.
 *
 * This can be used for Task object query only.
 *
 * Since: 3.46
 **/
void
e_gdata_query_set_completed_max (EGDataQuery *self,
				 gint64 value)
{
	g_return_if_fail (self != NULL);

	g_hash_table_insert ((GHashTable *) self, (gpointer) PARAM_COMPLETED_MAX,
		e_json_util_encode_iso8601 (value));
}

/**
 * e_gdata_query_get_completed_max:
 * @self: an #EDataQuery
 * @out_exists: (out) (optional): an out argument, where can
 *    be set whether the property exists, or %NULL
 *
 * Gets current value of the completed max property, as a Unix
 * date/time. When not set, returns -1. The optional @out_exists
 * can be used to see whether the property is set.
 *
 * Returns: the set value, or -1
 *
 * Since: 3.46
 **/
gint64
e_gdata_query_get_completed_max (EGDataQuery *self,
				 gboolean *out_exists)
{
	g_return_val_if_fail (self != NULL, -1);

	return e_gdata_query_get_datetime_property (self, PARAM_COMPLETED_MAX, out_exists);
}

/**
 * e_gdata_query_set_completed_min:
 * @self: an #EGDataQuery
 * @value: a value to set, as a Unix date/time
 *
 * Sets lower bound for a task's completion date, as a Unix date/time, to filter by.
 * The default is not to filter by completion date.
 *
 * This can be used for Task object query only.
 *
 * Since: 3.46
 **/
void
e_gdata_query_set_completed_min (EGDataQuery *self,
				 gint64 value)
{
	g_return_if_fail (self != NULL);

	g_hash_table_insert ((GHashTable *) self, (gpointer) PARAM_COMPLETED_MIN,
		e_json_util_encode_iso8601 (value));
}

/**
 * e_gdata_query_get_completed_min:
 * @self: an #EDataQuery
 * @out_exists: (out) (optional): an out argument, where can
 *    be set whether the property exists, or %NULL
 *
 * Gets current value of the completed min property, as a Unix
 * date/time. When not set, returns -1. The optional @out_exists
 * can be used to see whether the property is set.
 *
 * Returns: the set value, or -1
 *
 * Since: 3.46
 **/
gint64
e_gdata_query_get_completed_min (EGDataQuery *self,
				 gboolean *out_exists)
{
	g_return_val_if_fail (self != NULL, -1);

	return e_gdata_query_get_datetime_property (self, PARAM_COMPLETED_MIN, out_exists);
}

/**
 * e_gdata_query_set_due_max:
 * @self: an #EGDataQuery
 * @value: a value to set, as a Unix date/time
 *
 * Sets upper bound for a task's due date, as a Unix date/time, to filter by.
 * The default is not to filter by due date.
 *
 * This can be used for Task object query only.
 *
 * Since: 3.46
 **/
void
e_gdata_query_set_due_max (EGDataQuery *self,
			   gint64 value)
{
	g_return_if_fail (self != NULL);

	g_hash_table_insert ((GHashTable *) self, (gpointer) PARAM_DUE_MAX,
		e_json_util_encode_iso8601 (value));
}

/**
 * e_gdata_query_get_due_max:
 * @self: an #EDataQuery
 * @out_exists: (out) (optional): an out argument, where can
 *    be set whether the property exists, or %NULL
 *
 * Gets current value of the due max property, as a Unix
 * date/time. When not set, returns -1. The optional @out_exists
 * can be used to see whether the property is set.
 *
 * Returns: the set value, or -1
 *
 * Since: 3.46
 **/
gint64
e_gdata_query_get_due_max (EGDataQuery *self,
			   gboolean *out_exists)
{
	g_return_val_if_fail (self != NULL, -1);

	return e_gdata_query_get_datetime_property (self, PARAM_DUE_MAX, out_exists);
}

/**
 * e_gdata_query_set_due_min:
 * @self: an #EGDataQuery
 * @value: a value to set, as a Unix date/time
 *
 * Sets lower bound for a task's due date, as a Unix date/time, to filter by.
 * The default is not to filter by due date.
 *
 * This can be used for Task object query only.
 *
 * Since: 3.46
 **/
void
e_gdata_query_set_due_min (EGDataQuery *self,
			   gint64 value)
{
	g_return_if_fail (self != NULL);

	g_hash_table_insert ((GHashTable *) self, (gpointer) PARAM_DUE_MIN,
		e_json_util_encode_iso8601 (value));
}

/**
 * e_gdata_query_get_due_min:
 * @self: an #EDataQuery
 * @out_exists: (out) (optional): an out argument, where can
 *    be set whether the property exists, or %NULL
 *
 * Gets current value of the due min property, as a Unix
 * date/time. When not set, returns -1. The optional @out_exists
 * can be used to see whether the property is set.
 *
 * Returns: the set value, or -1
 *
 * Since: 3.46
 **/
gint64
e_gdata_query_get_due_min (EGDataQuery *self,
			   gboolean *out_exists)
{
	g_return_val_if_fail (self != NULL, -1);

	return e_gdata_query_get_datetime_property (self, PARAM_DUE_MIN, out_exists);
}

/**
 * e_gdata_query_set_show_completed:
 * @self: an #EGDataQuery
 * @value: a value to set
 *
 * Sets a flag indicating whether completed tasks are returned in the result.
 * The default is True. Note that show hidden should also be True to show
 * tasks completed in first party clients, such as the web UI and Google's
 * mobile apps.
 *
 * This can be used for Task object query only.
 *
 * Since: 3.46
 **/
void
e_gdata_query_set_show_completed (EGDataQuery *self,
				  gboolean value)
{
	g_return_if_fail (self != NULL);

	g_hash_table_insert ((GHashTable *) self, (gpointer) PARAM_SHOW_COMPLETED,
		g_strdup (value ? VALUE_TRUE : VALUE_FALSE));
}

/**
 * e_gdata_query_get_show_completed:
 * @self: an #EDataQuery
 * @out_exists: (out) (optional): an out argument, where can
 *    be set whether the property exists, or %NULL
 *
 * Gets current value of the show completed property. When not set,
 * returns %FALSE. The optional @out_exists can be used to see whether
 * the property is set.
 *
 * Returns: the set value, or %FALSE
 *
 * Since: 3.46
 **/
gboolean
e_gdata_query_get_show_completed (EGDataQuery *self,
				  gboolean *out_exists)
{
	g_return_val_if_fail (self != NULL, FALSE);

	return e_gdata_query_get_boolean_property (self, PARAM_SHOW_COMPLETED, out_exists);
}

/**
 * e_gdata_query_set_show_deleted:
 * @self: an #EGDataQuery
 * @value: a value to set
 *
 * Sets a flag indicating whether deleted tasks are returned in the result.
 * The default is False.
 *
 * This can be used for Task object query only.
 *
 * Since: 3.46
 **/
void
e_gdata_query_set_show_deleted (EGDataQuery *self,
				gboolean value)
{
	g_return_if_fail (self != NULL);

	g_hash_table_insert ((GHashTable *) self, (gpointer) PARAM_SHOW_DELETED,
		g_strdup (value ? VALUE_TRUE : VALUE_FALSE));
}

/**
 * e_gdata_query_get_show_deleted:
 * @self: an #EDataQuery
 * @out_exists: (out) (optional): an out argument, where can
 *    be set whether the property exists, or %NULL
 *
 * Gets current value of the show deleted property. When not set,
 * returns %FALSE. The optional @out_exists can be used to see whether
 * the property is set.
 *
 * Returns: the set value, or %FALSE
 *
 * Since: 3.46
 **/
gboolean
e_gdata_query_get_show_deleted (EGDataQuery *self,
				gboolean *out_exists)
{
	g_return_val_if_fail (self != NULL, FALSE);

	return e_gdata_query_get_boolean_property (self, PARAM_SHOW_DELETED, out_exists);
}

/**
 * e_gdata_query_set_show_hidden:
 * @self: an #EGDataQuery
 * @value: a value to set
 *
 * Sets a flag indicating whether hidden tasks are returned in the result.
 * The default is False.
 *
 * This can be used for Task object query only.
 *
 * Since: 3.46
 **/
void
e_gdata_query_set_show_hidden (EGDataQuery *self,
			       gboolean value)
{
	g_return_if_fail (self != NULL);

	g_hash_table_insert ((GHashTable *) self, (gpointer) PARAM_SHOW_HIDDEN,
		g_strdup (value ? VALUE_TRUE : VALUE_FALSE));
}

/**
 * e_gdata_query_get_show_hidden:
 * @self: an #EDataQuery
 * @out_exists: (out) (optional): an out argument, where can
 *    be set whether the property exists, or %NULL
 *
 * Gets current value of the show hidden property. When not set,
 * returns %FALSE. The optional @out_exists can be used to see whether
 * the property is set.
 *
 * Returns: the set value, or %FALSE
 *
 * Since: 3.46
 **/
gboolean
e_gdata_query_get_show_hidden (EGDataQuery *self,
			       gboolean *out_exists)
{
	g_return_val_if_fail (self != NULL, FALSE);

	return e_gdata_query_get_boolean_property (self, PARAM_SHOW_HIDDEN, out_exists);
}

/**
 * e_gdata_query_set_updated_min:
 * @self: an #EGDataQuery
 * @value: a value to set, as a Unix date/time
 *
 * Sets lower bound for a task's last modification time, as a Unix date/time,
 * to filter by. The default is not to filter by the last modification time.
 *
 * This can be used for Task object query only.
 *
 * Since: 3.46
 **/
void
e_gdata_query_set_updated_min (EGDataQuery *self,
			       gint64 value)
{
	g_return_if_fail (self != NULL);

	g_hash_table_insert ((GHashTable *) self, (gpointer) PARAM_UPDATED_MIN,
		e_json_util_encode_iso8601 (value));
}

/**
 * e_gdata_query_get_updated_min:
 * @self: an #EDataQuery
 * @out_exists: (out) (optional): an out argument, where can
 *    be set whether the property exists, or %NULL
 *
 * Gets current value of the updated min property, as a Unix
 * date/time. When not set, returns -1. The optional @out_exists
 * can be used to see whether the property is set.
 *
 * Returns: the set value, or -1
 *
 * Since: 3.46
 **/
gint64
e_gdata_query_get_updated_min (EGDataQuery *self,
			       gboolean *out_exists)
{
	g_return_val_if_fail (self != NULL, -1);

	return e_gdata_query_get_datetime_property (self, PARAM_UPDATED_MIN, out_exists);
}

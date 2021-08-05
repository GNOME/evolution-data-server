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

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <string.h>
#include <gio/gio.h>

#include "camel-mime-utils.h"
#include "camel-utils.h"

/**
 * camel_util_bdata_get_number:
 * @bdata_ptr: a backend specific data (bdata) pointer
 * @default_value: a value to return, when no data can be read
 *
 * Reads a numeric data from the @bdata_ptr and moves the @bdata_ptr
 * after that number. If the number cannot be read, then the @default_value
 * is returned instead and the @bdata_ptr is left unchanged. The number
 * might be previously stored with the camel_util_bdata_put_number().
 *
 * Returns: The read number, or the @default_value, if the @bdata_ptr doesn't
 *    point to a number.
 *
 * Since: 3.24
 **/
gint64
camel_util_bdata_get_number (/* const */ gchar **bdata_ptr,
			     gint64 default_value)
{
	gint64 result;
	gchar *endptr;

	g_return_val_if_fail (bdata_ptr != NULL, default_value);

	if (!bdata_ptr || !*bdata_ptr || !**bdata_ptr)
		return default_value;

	if (**bdata_ptr == ' ')
		*bdata_ptr += 1;

	if (!**bdata_ptr)
		return default_value;

	endptr = *bdata_ptr;

	result = g_ascii_strtoll (*bdata_ptr, &endptr, 10);

	if (endptr == *bdata_ptr)
		result = default_value;
	else
		*bdata_ptr = endptr;

	return result;
}

/**
 * camel_util_bdata_put_number:
 * @bdata_str: a #GString to store a backend specific data (bdata)
 * @value: a value to store
 *
 * Puts the number @value at the end of the @bdata_str. In case the @bdata_str
 * is not empty a space is added before the numeric @value. The stored value
 * can be read back with the camel_util_bdata_get_number().
 *
 * Since: 3.24
 **/
void
camel_util_bdata_put_number (GString *bdata_str,
			     gint64 value)
{
	g_return_if_fail (bdata_str != NULL);

	if (bdata_str->len && bdata_str->str[bdata_str->len - 1] != ' ')
		g_string_append_c (bdata_str, ' ');

	g_string_append_printf (bdata_str, "%" G_GINT64_FORMAT, value);
}

/**
 * camel_util_bdata_get_string:
 * @bdata_ptr: a backend specific data (bdata) pointer
 * @default_value: a value to return, when no data can be read
 *
 * Reads a string data from the @bdata_ptr and moves the @bdata_ptr
 * after that string. If the string cannot be read, then the @default_value
 * is returned instead and the @bdata_ptr is left unchanged. The string
 * might be previously stored with the camel_util_bdata_put_string().
 *
 * Returns: (transfer full): Newly allocated string, which was read, or
 *    dupped the @default_value, if the @bdata_ptr doesn't point to a string.
 *    Free returned pointer with g_free() when done with it.
 *
 * Since: 3.24
 **/
gchar *
camel_util_bdata_get_string (/* const */ gchar **bdata_ptr,
			     const gchar *default_value)
{
	gint64 length, has_length;
	gchar *orig_bdata_ptr;
	gchar *result;

	g_return_val_if_fail (bdata_ptr != NULL, NULL);

	orig_bdata_ptr = *bdata_ptr;

	length = camel_util_bdata_get_number (bdata_ptr, -1);

	/* might be a '-' sign */
	if (*bdata_ptr && **bdata_ptr == '-')
		*bdata_ptr += 1;
	else
		length = -1;

	if (length < 0 || !*bdata_ptr || !**bdata_ptr || *bdata_ptr == orig_bdata_ptr) {
		*bdata_ptr = orig_bdata_ptr;

		return g_strdup (default_value);
	}

	if (!length)
		return g_strdup ("");

	has_length = strlen (*bdata_ptr);
	if (has_length < length)
		length = has_length;

	result = g_strndup (*bdata_ptr, length);
	*bdata_ptr += length;

	return result;
}

/**
 * camel_util_bdata_put_string:
 * @bdata_str: a #GString to store a backend specific data (bdata)
 * @value: a value to store
 *
 * Puts the string @value at the end of the @bdata_str. In case the @bdata_str
 * is not empty a space is added before the string @value. The stored value
 * can be read back with the camel_util_bdata_get_string().
 *
 * The strings are encoded as "length-value", quotes for clarity only.
 *
 * Since: 3.24
 **/
void
camel_util_bdata_put_string (GString *bdata_str,
			     const gchar *value)
{
	g_return_if_fail (bdata_str != NULL);
	g_return_if_fail (value != NULL);

	camel_util_bdata_put_number (bdata_str, strlen (value));

	g_string_append_printf (bdata_str, "-%s", value);
}

/**
 * camel_time_value_apply:
 * @src_time: a time_t to apply the value to, or -1 to use the current time
 * @unit: a #CamelTimeUnit
 * @value: a value to apply
 *
 * Applies the given time @value in unit @unit to the @src_time.
 * Use negative value to subtract it. The time part is rounded
 * to the beginning of the day.
 *
 * Returns: @src_time modified by the given parameters as date, with
 *    the time part being beginning of the day.
 *
 * Since: 3.24
 **/
time_t
camel_time_value_apply (time_t src_time,
			CamelTimeUnit unit,
			gint value)
{
	GDate dt;
	struct tm tm;

	g_return_val_if_fail (unit >= CAMEL_TIME_UNIT_DAYS && unit <= CAMEL_TIME_UNIT_YEARS, src_time);

	if (src_time == (time_t) -1)
		src_time = time (NULL);

	if (!value)
		return src_time;

	g_date_clear (&dt, 1);

	g_date_set_time_t (&dt, src_time);

	switch (unit) {
	case CAMEL_TIME_UNIT_DAYS:
		if (value > 0)
			g_date_add_days (&dt, value);
		else
			g_date_subtract_days (&dt, (-1) * value);
		break;
	case CAMEL_TIME_UNIT_WEEKS:
		if (value > 0)
			g_date_add_days (&dt, value * 7);
		else
			g_date_subtract_days (&dt, (-1) * value * 7);
		break;
	case CAMEL_TIME_UNIT_MONTHS:
		if (value > 0)
			g_date_add_months (&dt, value);
		else
			g_date_subtract_months (&dt, (-1) * value);
		break;
	case CAMEL_TIME_UNIT_YEARS:
		if (value > 0)
			g_date_add_years (&dt, value);
		else
			g_date_subtract_years (&dt, (-1) * value);
		break;
	}

	g_date_to_struct_tm (&dt, &tm);

	tm.tm_sec = 0;
	tm.tm_min = 0;
	tm.tm_hour = 0;

	return mktime (&tm);
}

/**
 * camel_utils_weak_ref_new: (skip)
 * @object: (nullable): a #GObject or %NULL
 *
 * Allocates a new #GWeakRef and calls g_weak_ref_set() with @object.
 *
 * Free the returned #GWeakRef with camel_utils_weak_ref_free().
 *
 * Returns: (transfer full): a new #GWeakRef
 *
 * Since: 3.40
 **/
GWeakRef *
camel_utils_weak_ref_new (gpointer object)
{
	GWeakRef *weak_ref;

	/* Based on e_weak_ref_new(). */

	weak_ref = g_slice_new0 (GWeakRef);
	g_weak_ref_init (weak_ref, object);

	return weak_ref;
}

/**
 * camel_utils_weak_ref_free: (skip)
 * @weak_ref: a #GWeakRef
 *
 * Frees a #GWeakRef created by camel_utils_weak_ref_new().
 *
 * Since: 3.40
 **/
void
camel_utils_weak_ref_free (GWeakRef *weak_ref)
{
	g_return_if_fail (weak_ref != NULL);

	/* Based on e_weak_ref_free(). */

	g_weak_ref_clear (weak_ref);
	g_slice_free (GWeakRef, weak_ref);
}

G_LOCK_DEFINE_STATIC (mi_user_headers);
static GSettings *mi_user_headers_settings = NULL;
static gchar **mi_user_headers = NULL;

static void
mi_user_headers_settings_changed_cb (GSettings *settings,
				     const gchar *key,
				     gpointer user_data)
{
	G_LOCK (mi_user_headers);

	if (mi_user_headers_settings) {
		gboolean changed;
		gchar **strv;
		guint ii, jj = 0;

		strv = g_settings_get_strv (mi_user_headers_settings, "camel-message-info-user-headers");
		changed = (!mi_user_headers && strv && strv[0]) || (mi_user_headers && (!strv || !strv[0]));

		if (mi_user_headers && strv && !changed) {
			for (ii = 0, jj = 0; strv[ii] && mi_user_headers[jj] && jj < CAMEL_UTILS_MAX_USER_HEADERS; ii++) {
				const gchar *name = NULL;

				camel_util_decode_user_header_setting (strv[ii], NULL, &name);

				if (name && *name) {
					if (g_ascii_strcasecmp (mi_user_headers[jj], name) != 0) {
						changed = TRUE;
						break;
					}
					jj++;
				}
			}

			changed = changed || (strv[ii] && jj < CAMEL_UTILS_MAX_USER_HEADERS) || (!strv[ii] && jj < CAMEL_UTILS_MAX_USER_HEADERS && mi_user_headers[jj]);
		}

		if (changed) {
			GPtrArray *array;

			array = g_ptr_array_sized_new (jj + 2);

			for (ii = 0, jj = 0; strv && strv[ii] && jj < CAMEL_UTILS_MAX_USER_HEADERS; ii++) {
				const gchar *name = NULL;

				camel_util_decode_user_header_setting (strv[ii], NULL, &name);

				if (name && *name) {
					g_ptr_array_add (array, g_strdup (name));
					jj++;
				}
			}

			/* NULL-terminated */
			g_ptr_array_add (array, NULL);

			g_strfreev (mi_user_headers);
			mi_user_headers = (gchar **) g_ptr_array_free (array, FALSE);
		}

		g_strfreev (strv);
	}

	G_UNLOCK (mi_user_headers);
}

/* private functions */
void _camel_utils_initialize (void);
void _camel_utils_shutdown (void);

/* <private> */
void
_camel_utils_initialize (void)
{
	G_LOCK (mi_user_headers);
	mi_user_headers_settings = g_settings_new ("org.gnome.evolution-data-server");
	g_signal_connect (mi_user_headers_settings, "changed::camel-message-info-user-headers",
		G_CALLBACK (mi_user_headers_settings_changed_cb), NULL);
	G_UNLOCK (mi_user_headers);
	mi_user_headers_settings_changed_cb (NULL, NULL, NULL);
}

/* <private> */
void
_camel_utils_shutdown (void)
{
	G_LOCK (mi_user_headers);
	if (mi_user_headers_settings) {
		g_clear_object (&mi_user_headers_settings);
		g_strfreev (mi_user_headers);
		mi_user_headers = NULL;
	}
	G_UNLOCK (mi_user_headers);
}

/**
 * camel_util_fill_message_info_user_headers:
 * @info: a #CamelMessageInfo
 * @headers: a #CamelNameValueArray with the headers to read from
 *
 * Fill @info 's user-headers with the user-defined headers from
 * the @headers array.
 *
 * Returns: Whether the @info's user headers changed
 *
 * Since: 3.42
 **/
gboolean
camel_util_fill_message_info_user_headers (CamelMessageInfo *info,
					   const CamelNameValueArray *headers)
{
	gboolean changed = FALSE;

	g_return_val_if_fail (CAMEL_IS_MESSAGE_INFO (info), FALSE);
	g_return_val_if_fail (headers != NULL, FALSE);

	camel_message_info_freeze_notifications (info);

	G_LOCK (mi_user_headers);

	if (mi_user_headers) {
		CamelNameValueArray *array;
		guint ii;

		array = camel_name_value_array_new ();

		for (ii = 0; mi_user_headers[ii]; ii++) {
			const gchar *value;
			gchar *str;

			value = camel_name_value_array_get_named (headers, CAMEL_COMPARE_CASE_INSENSITIVE, mi_user_headers[ii]);
			if (!value)
				continue;

			while (*value && g_ascii_isspace (*value))
				value++;

			str = camel_header_unfold (value);

			if (str && *str)
				camel_name_value_array_set_named (array, CAMEL_COMPARE_CASE_INSENSITIVE, mi_user_headers[ii], str);
			else
				camel_name_value_array_remove_named (array, CAMEL_COMPARE_CASE_INSENSITIVE, mi_user_headers[ii], TRUE);

			g_free (str);
		}

		if (camel_name_value_array_get_length (array) == 0) {
			camel_name_value_array_free (array);
			array = NULL;
		}

		changed = camel_message_info_take_user_headers (info, array);
	}

	G_UNLOCK (mi_user_headers);

	camel_message_info_thaw_notifications (info);

	return changed;
}

/**
 * camel_util_encode_user_header_setting:
 * @display_name: (nullable): display name for the header name, or %NULL
 * @header_name: the header name
 *
 * Encode the optional @display_name and the @header_name to a value suitable
 * for GSettings schema org.gnome.evolution-data-server and key camel-message-info-user-headers.
 *
 * Free the returned string with g_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated string with encoded @display_name
 *    and @header_name
 *
 * Since: 3.42
 **/
gchar *
camel_util_encode_user_header_setting (const gchar *display_name,
				       const gchar *header_name)
{
	g_return_val_if_fail (header_name && *header_name, NULL);

	if (display_name && *display_name)
		return g_strconcat (display_name, "|", header_name, NULL);

	return g_strdup (header_name);
}

/**
 * camel_util_decode_user_header_setting:
 * @setting_value: the value to decode
 * @out_display_name: (out) (transfer full) (nullable): location for the decoded display name, or %NULL when not needed
 * @out_header_name: (out): the location for the decoded header name
 *
 * Decode the values previously encoded by camel_util_encode_user_header_setting().
 * The @out_header_name points to the @setting_value, thus it's valid as long
 * as the @setting_value is valid and unchanged.
 *
 * The @out_header_name can result in %NULL when the @setting_value
 * contains invalid data.
 *
 * The @out_display_name can result in %NULL when the @setting_value
 * does not contain the display name. In such case the header name can
 * be used as the display name.
 *
 * Since: 3.42
 **/
void
camel_util_decode_user_header_setting (const gchar *setting_value,
				       gchar **out_display_name,
				       const gchar **out_header_name)
{
	const gchar *ptr;

	g_return_if_fail (setting_value != NULL);
	g_return_if_fail (out_header_name != NULL);

	*out_header_name = NULL;

	if (out_display_name)
		*out_display_name = NULL;

	if (!*setting_value)
		return;

	ptr = strchr (setting_value, '|');

	/* Nothing after the pipe means no header name */
	if (ptr && !ptr[1])
		return;

	if (ptr) {
		if (out_display_name && ptr != setting_value)
			*out_display_name = g_strndup (setting_value, ptr - setting_value);

		*out_header_name = ptr + 1;
	} else {
		*out_header_name = setting_value;
	}
}

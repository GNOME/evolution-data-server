/*
 * camel-settings.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#include "camel-settings.h"

#include <stdlib.h>

G_DEFINE_TYPE (CamelSettings, camel_settings, G_TYPE_OBJECT)

static GParamSpec **
settings_list_settings (CamelSettingsClass *class,
                        guint *n_settings)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	return g_object_class_list_properties (object_class, n_settings);
}

static CamelSettings *
settings_clone (CamelSettings *settings)
{
	CamelSettingsClass *class;
	GParamSpec **properties;
	GParameter *parameters;
	CamelSettings *clone;
	guint ii, n_properties;

	class = CAMEL_SETTINGS_GET_CLASS (settings);
	properties = camel_settings_class_list_settings (class, &n_properties);

	parameters = g_new0 (GParameter, n_properties);

	for (ii = 0; ii < n_properties; ii++) {
		parameters[ii].name = properties[ii]->name;
		g_value_init (
			&parameters[ii].value,
			properties[ii]->value_type);
		g_object_get_property (
			G_OBJECT (settings),
			parameters[ii].name,
			&parameters[ii].value);
	}

	clone = g_object_newv (
		G_OBJECT_TYPE (settings),
		n_properties, parameters);

	for (ii = 0; ii < n_properties; ii++)
		g_value_unset (&parameters[ii].value);

	g_free (parameters);
	g_free (properties);

	return clone;
}

static gboolean
settings_equal (CamelSettings *settings_a,
                CamelSettings *settings_b)
{
	CamelSettingsClass *class;
	GParamSpec **properties;
	GValue *value_a;
	GValue *value_b;
	guint ii, n_properties;
	gboolean equal = TRUE;

	/* Make sure both instances are of the same type. */
	if (G_OBJECT_TYPE (settings_a) != G_OBJECT_TYPE (settings_b))
		return FALSE;

	value_a = g_slice_new0 (GValue);
	value_b = g_slice_new0 (GValue);

	class = CAMEL_SETTINGS_GET_CLASS (settings_a);
	properties = camel_settings_class_list_settings (class, &n_properties);

	for (ii = 0; equal && ii < n_properties; ii++) {
		GParamSpec *pspec = properties[ii];

		g_value_init (value_a, pspec->value_type);
		g_value_init (value_b, pspec->value_type);

		g_object_get_property (
			G_OBJECT (settings_a),
			pspec->name, value_a);

		g_object_get_property (
			G_OBJECT (settings_b),
			pspec->name, value_b);

		equal = (g_param_values_cmp (pspec, value_a, value_b) == 0);

		g_value_unset (value_a);
		g_value_unset (value_b);
	}

	g_free (properties);

	g_slice_free (GValue, value_a);
	g_slice_free (GValue, value_b);

	return equal;
}

static void
camel_settings_class_init (CamelSettingsClass *class)
{
	class->list_settings = settings_list_settings;
	class->clone = settings_clone;
	class->equal = settings_equal;
}

static void
camel_settings_init (CamelSettings *settings)
{
}

/**
 * camel_settings_class_list_settings:
 * @class: a #CamelSettingsClass
 * @n_settings: return location for the length of the returned array
 *
 * Returns an array of #GParamSpec for properties of @class which are
 * considered to be settings.  By default all properties are considered
 * to be settings, but subclasses may wish to exclude certain properties.
 * Free the returned array with g_free().
 *
 * Returns: an array of #GParamSpec which should be freed after use
 *
 * Since: 3.2
 **/
GParamSpec **
camel_settings_class_list_settings (CamelSettingsClass *class,
                                    guint *n_settings)
{
	g_return_val_if_fail (CAMEL_IS_SETTINGS_CLASS (class), NULL);
	g_return_val_if_fail (class->list_settings != NULL, NULL);

	return class->list_settings (class, n_settings);
}

/**
 * camel_settings_clone:
 * @settings: a #CamelSettings
 *
 * Creates an copy of @settings, such that passing @settings and the
 * copied instance to camel_settings_equal() would return %TRUE.
 *
 * By default, this creates a new settings instance with the same #GType
 * as @settings, and copies all #GObject property values from @settings
 * to the new instance.
 *
 * Returns: a newly-created copy of @settings
 *
 * Since: 3.2
 **/
CamelSettings *
camel_settings_clone (CamelSettings *settings)
{
	CamelSettingsClass *class;
	CamelSettings *clone;

	g_return_val_if_fail (CAMEL_IS_SETTINGS (settings), NULL);

	class = CAMEL_SETTINGS_GET_CLASS (settings);
	g_return_val_if_fail (class->clone != NULL, NULL);

	clone = class->clone (settings);

	/* Make sure the documented invariant is satisfied. */
	g_warn_if_fail (camel_settings_equal (settings, clone));

	return clone;
}

/**
 * camel_settings_equal:
 * @settings_a: a #CamelSettings
 * @settings_b: another #CamelSettings
 *
 * Returns %TRUE if @settings_a and @settings_b are equal.
 *
 * By default, equality requires both instances to have the same #GType
 * with the same set of #GObject properties, and each property value in
 * @settings_a is equal to the corresponding value in @settings_b.
 *
 * Returns: %TRUE if @settings_a and @settings_b are equal
 *
 * Since: 3.2
 **/
gboolean
camel_settings_equal (CamelSettings *settings_a,
                      CamelSettings *settings_b)
{
	CamelSettingsClass *class;

	g_return_val_if_fail (CAMEL_IS_SETTINGS (settings_a), FALSE);
	g_return_val_if_fail (CAMEL_IS_SETTINGS (settings_b), FALSE);

	class = CAMEL_SETTINGS_GET_CLASS (settings_a);
	g_return_val_if_fail (class->equal != NULL, FALSE);

	return class->equal (settings_a, settings_b);
}

/**
 * camel_settings_load_from_url:
 * @settings: a #CamelSettings
 * @url: a #CamelURL
 *
 * Populates @settings with parameters from @url.  The @url parameter value
 * is converted according to the #GParamSpec for the corresponding property
 * name in @settings.
 *
 * This function is highly Evolution-centric and is only temporary.
 * Expect this function to be removed as early as version 3.4.
 *
 * Since: 3.2
 **/
void
camel_settings_load_from_url (CamelSettings *settings,
                              CamelURL *url)
{
	CamelSettingsClass *class;
	GParamSpec **properties;
	GValue value = G_VALUE_INIT;
	guint ii, n_properties;

	g_return_if_fail (CAMEL_IS_SETTINGS (settings));
	g_return_if_fail (url != NULL);

	class = CAMEL_SETTINGS_GET_CLASS (settings);
	properties = camel_settings_class_list_settings (class, &n_properties);

	g_object_freeze_notify (G_OBJECT (settings));

	for (ii = 0; ii < n_properties; ii++) {
		GParamSpec *pspec = properties[ii];
		const gchar *string;
		gboolean value_is_set = FALSE;

		string = g_datalist_get_data (&url->params, pspec->name);

		/* If no corresponding URL parameter is found,
		 * leave the CamelSettings property unchanged. */
		if (string == NULL)
			continue;

		/* Handle more types as needed. */

		if (G_IS_PARAM_SPEC_CHAR (pspec) ||
		    G_IS_PARAM_SPEC_UCHAR (pspec) ||
		    G_IS_PARAM_SPEC_INT (pspec) ||
		    G_IS_PARAM_SPEC_UINT (pspec) ||
		    G_IS_PARAM_SPEC_LONG (pspec) ||
		    G_IS_PARAM_SPEC_ULONG (pspec)) {
			glong v_long;
			gchar *endptr;

			v_long = strtol (string, &endptr, 10);

			if (*string != '\0' && *endptr == '\0') {
				g_value_init (&value, G_TYPE_LONG);
				g_value_set_long (&value, v_long);
				value_is_set = TRUE;
			}

		} else if (G_IS_PARAM_SPEC_BOOLEAN (pspec)) {
			gboolean v_boolean = FALSE;

			value_is_set = TRUE;

			/* If the value is empty, then the mere
			 * presence of the parameter means TRUE. */
			if (*string == '\0')
				v_boolean = TRUE;

			else if (g_ascii_strcasecmp (string, "true") == 0)
				v_boolean = TRUE;

			else if (g_ascii_strcasecmp (string, "yes") == 0)
				v_boolean = TRUE;

			else if (g_ascii_strcasecmp (string, "1") == 0)
				v_boolean = TRUE;

			else if (g_ascii_strcasecmp (string, "false") == 0)
				v_boolean = FALSE;

			else if (g_ascii_strcasecmp (string, "no") == 0)
				v_boolean = FALSE;

			else if (g_ascii_strcasecmp (string, "0") == 0)
				v_boolean = FALSE;

			else
				value_is_set = FALSE;

			if (value_is_set) {
				g_value_init (&value, G_TYPE_BOOLEAN);
				g_value_set_boolean (&value, v_boolean);
			}

		} else if (G_IS_PARAM_SPEC_ENUM (pspec)) {
			GParamSpecEnum *enum_pspec;
			GEnumValue *enum_value;

			enum_pspec = G_PARAM_SPEC_ENUM (pspec);
			enum_value = g_enum_get_value_by_nick (
				enum_pspec->enum_class, string);
			if (enum_value != NULL) {
				g_value_init (&value, pspec->value_type);
				g_value_set_enum (&value, enum_value->value);
				value_is_set = TRUE;
			}

		} else if (G_IS_PARAM_SPEC_FLOAT (pspec) ||
			   G_IS_PARAM_SPEC_DOUBLE (pspec)) {
			gdouble v_double;
			gchar *endptr;

			v_double = g_ascii_strtod (string, &endptr);

			if (*string != '\0' && *endptr == '\0') {
				g_value_init (&value, G_TYPE_DOUBLE);
				g_value_set_double (&value, v_double);
				value_is_set = TRUE;
			}

		} else if (G_IS_PARAM_SPEC_STRING (pspec)) {
			g_value_init (&value, G_TYPE_STRING);
			g_value_set_string (&value, string);
			value_is_set = TRUE;

		} else if (g_type_is_a (pspec->value_type, G_TYPE_STRV)) {
			gchar **strv;

			strv = g_strsplit (string, ",", -1);
			g_value_init (&value, G_TYPE_STRV);
			g_value_take_boxed (&value, strv);
			value_is_set = TRUE;

		} else
			g_warning (
				"No handler to load property %s:%s (type %s)",
				G_OBJECT_TYPE_NAME (settings),
				pspec->name, g_type_name (pspec->value_type));

		if (value_is_set) {
			g_object_set_property (
				G_OBJECT (settings),
				pspec->name, &value);
			g_value_unset (&value);
		}
	}

	g_object_thaw_notify (G_OBJECT (settings));

	g_free (properties);
}

/**
 * camel_settings_save_to_url:
 * @settings: a #CamelSettings
 * @url: a #CamelURL
 *
 * Writes the values of all properties of @settings to @url as parameter
 * strings.  The parameter name in @url matches the corresponding property
 * in @settings.
 *
 * This function is highly Evolution-centric and is only temporary.
 * Expect this function to be removed as early as version 3.4.
 *
 * Since: 3.2
 **/
void
camel_settings_save_to_url (CamelSettings *settings,
                            CamelURL *url)
{
	CamelSettingsClass *class;
	GParamSpec **properties;
	GValue pvalue = G_VALUE_INIT;
	GValue svalue = G_VALUE_INIT;
	guint ii, n_properties;

	g_return_if_fail (CAMEL_IS_SETTINGS (settings));
	g_return_if_fail (url != NULL);

	g_value_init (&svalue, G_TYPE_STRING);

	class = CAMEL_SETTINGS_GET_CLASS (settings);
	properties = camel_settings_class_list_settings (class, &n_properties);

	for (ii = 0; ii < n_properties; ii++) {
		GParamSpec *pspec = properties[ii];
		gchar *string = NULL;

		g_value_init (&pvalue, pspec->value_type);

		g_object_get_property (
			G_OBJECT (settings),
			pspec->name, &pvalue);

		/* If the property value matches the default value,
		 * remove the corresponding URL parameter so we keep
		 * the URL string to a minimum. */
		if (g_param_value_defaults (pspec, &pvalue)) {
			g_datalist_remove_data (&url->params, pspec->name);
			g_value_unset (&pvalue);
			continue;
		}

		/* For the most part we can just transform any supported
		 * property type to a string, with a couple exceptions. */

		/* Transforming a boolean GValue to a string results
		 * in "TRUE" or "FALSE" (all uppercase).  Instead use
		 * all lowercase since GKeyFile will require it. */
		if (G_VALUE_HOLDS_BOOLEAN (&pvalue)) {
			gboolean v_boolean = g_value_get_boolean (&pvalue);
			string = g_strdup (v_boolean ? "true" : "false");

		/* Transforming an enum GValue to a string results in
		 * the GEnumValue name.  We want the shorter nickname. */
		} else if (G_VALUE_HOLDS_ENUM (&pvalue)) {
			GParamSpecEnum *enum_pspec;
			GEnumClass *enum_class;
			GEnumValue *enum_value;
			gint value;

			enum_pspec = G_PARAM_SPEC_ENUM (pspec);
			enum_class = enum_pspec->enum_class;

			value = g_value_get_enum (&pvalue);
			enum_value = g_enum_get_value (enum_class, value);

			if (enum_value != NULL)
				string = g_strdup (enum_value->value_nick);

		} else if (G_VALUE_HOLDS (&pvalue, G_TYPE_STRV)) {
			gchar **strv = g_value_get_boxed (&pvalue);

			if (strv != NULL)
				string = g_strjoinv (",", strv);

		} else if (g_value_transform (&pvalue, &svalue)) {
			string = g_value_dup_string (&svalue);

		} else
			g_warning (
				"No handler to save property %s:%s (type %s)",
				G_OBJECT_TYPE_NAME (settings),
				pspec->name, g_type_name (pspec->value_type));

		/* CamelURL takes ownership of the string. */
		if (string != NULL)
			g_datalist_set_data_full (
				&url->params, pspec->name, string,
				(GDestroyNotify) g_free);

		g_value_unset (&pvalue);
	}

	g_free (properties);

	g_value_unset (&svalue);
}

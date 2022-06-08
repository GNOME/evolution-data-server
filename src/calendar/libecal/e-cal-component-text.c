/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2019 Red Hat, Inc. (www.redhat.com)
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
 *
 */

#include "evolution-data-server-config.h"

/**
 * SECTION:e-cal-component-text
 * @short_description: An ECalComponentText structure
 * @include: libecal/libecal.h
 *
 * Contains functions to work with the #ECalComponentText structure.
 **/

#include "e-cal-component-text.h"

G_DEFINE_BOXED_TYPE (ECalComponentText, e_cal_component_text, e_cal_component_text_copy, e_cal_component_text_free)

struct _ECalComponentText {
	gchar *value;
	gchar *altrep;
	gchar *language;
};

/**
 * e_cal_component_text_new:
 * @value: (nullable): description text
 * @altrep: (nullable): alternate representation URI
 *
 * Creates a new #ECalComponentText describing text properties.
 * The returned structure should be freed with e_cal_component_text_free(),
 * when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentText
 *
 * Since: 3.34
 **/
ECalComponentText *
e_cal_component_text_new (const gchar *value,
			  const gchar *altrep)
{
	ECalComponentText *text;

	text = g_slice_new0 (ECalComponentText);
	text->value = g_strdup (value);
	text->altrep = g_strdup (altrep);

	return text;
}

/**
 * e_cal_component_text_new_from_property:
 * @property: an #ICalProperty
 *
 * Created a new #ECalComponentText filled with values from the @property.
 * The @property should hold a text value.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentText
 *
 * Since: 3.46
 **/
ECalComponentText *
e_cal_component_text_new_from_property (const ICalProperty *property)
{
	ECalComponentText *text;

	g_return_val_if_fail (I_CAL_IS_PROPERTY (property), NULL);

	text = e_cal_component_text_new (NULL, NULL);

	e_cal_component_text_set_from_property (text, property);

	return text;
}

/**
 * e_cal_component_text_copy:
 * @text: (not nullable): an #ECalComponentText to copy
 *
 * Returns: (transfer full): a newly allocated #ECalComponentText, copy of @text.
 *    The returned structure should be freed with e_cal_component_text_free(),
 *    when no longer needed.
 *
 * Since: 3.34
 **/
ECalComponentText *
e_cal_component_text_copy (const ECalComponentText *text)
{
	ECalComponentText *copy;

	g_return_val_if_fail (text != NULL, NULL);

	copy = e_cal_component_text_new (text->value, text->altrep);
	e_cal_component_text_set_language (copy, text->language);

	return copy;
}

/**
 * e_cal_component_text_free: (skip)
 * @text: (type ECalComponentText) (nullable): an #ECalComponentText to free
 *
 * Free the @text, previously allocated by e_cal_component_text_new() or
 * e_cal_component_text_copy().
 *
 * Since: 3.34
 **/
void
e_cal_component_text_free (gpointer text)
{
	ECalComponentText *te = text;

	if (te) {
		g_free (te->value);
		g_free (te->altrep);
		g_free (te->language);
		g_slice_free (ECalComponentText, te);
	}
}

/**
 * e_cal_component_text_set_from_property:
 * @text: an #ECalComponentText
 * @property: an #ICalProperty
 *
 * Fill the @text structure with the information from the @property.
 * The @property should hold a text value.
 *
 * Since: 3.46
 **/
void
e_cal_component_text_set_from_property (ECalComponentText *text,
					const ICalProperty *property)
{
	ICalValue *value;
	ICalParameter *param;

	g_return_if_fail (text != NULL);
	g_return_if_fail (I_CAL_IS_PROPERTY (property));

	value = i_cal_property_get_value (property);

	if (value && i_cal_value_isa (value) == I_CAL_TEXT_VALUE) {
		e_cal_component_text_set_value (text, i_cal_value_get_text (value));
	} else {
		e_cal_component_text_set_value (text, NULL);
	}
	g_clear_object (&value);

	param = i_cal_property_get_first_parameter ((ICalProperty *) property, I_CAL_ALTREP_PARAMETER);
	e_cal_component_text_set_altrep (text, param ? i_cal_parameter_get_altrep (param) : NULL);
	g_clear_object (&param);

	param = i_cal_property_get_first_parameter ((ICalProperty *) property, I_CAL_LANGUAGE_PARAMETER);
	e_cal_component_text_set_language (text, param ? i_cal_parameter_get_language (param) : NULL);
	g_clear_object (&param);
}

/**
 * e_cal_component_text_fill_property:
 * @text: an #ECalComponentText
 * @property: an #ICalProperty
 *
 * Fills the @property with the content of the @text.
 *
 * Since: 3.46
 **/
void
e_cal_component_text_fill_property (const ECalComponentText *text,
				    ICalProperty *property)
{
	ICalValue *value;
	ICalParameter *param;
	const gchar *str;

	g_return_if_fail (text != NULL);
	g_return_if_fail (I_CAL_IS_PROPERTY (property));

	str = e_cal_component_text_get_value (text) ? e_cal_component_text_get_value (text) : "";
	value = i_cal_property_get_value (property);

	if (value && i_cal_value_isa (value) == I_CAL_TEXT_VALUE) {
		i_cal_value_set_text (value, str);
		e_cal_component_text_get_value (text);
	} else {
		value = i_cal_value_new_text (str);
		i_cal_property_set_value (property, value);
	}

	g_clear_object (&value);

	str = e_cal_component_text_get_altrep (text);
	param = i_cal_property_get_first_parameter (property, I_CAL_ALTREP_PARAMETER);

	if (str && *str) {
		if (param) {
			i_cal_parameter_set_altrep (param, str);
		} else {
			param = i_cal_parameter_new_altrep (str);
			i_cal_property_take_parameter (property, param);
			param = NULL;
		}
	} else if (param) {
		i_cal_property_remove_parameter_by_kind (property, I_CAL_ALTREP_PARAMETER);
	}

	g_clear_object (&param);

	str = e_cal_component_text_get_language (text);
	param = i_cal_property_get_first_parameter (property, I_CAL_LANGUAGE_PARAMETER);

	if (str && *str) {
		if (param) {
			i_cal_parameter_set_language (param, str);
		} else {
			param = i_cal_parameter_new_language (str);
			i_cal_property_take_parameter (property, param);
			param = NULL;
		}
	} else if (param) {
		i_cal_property_remove_parameter_by_kind (property, I_CAL_LANGUAGE_PARAMETER);
	}

	g_clear_object (&param);
}

/**
 * e_cal_component_text_get_value:
 * @text: an #ECalComponentText
 *
 * Returns: the description string of the @text
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_text_get_value (const ECalComponentText *text)
{
	g_return_val_if_fail (text != NULL, NULL);

	return text->value;
}

/**
 * e_cal_component_text_set_value:
 * @text: an #ECalComponentText
 * @value: (nullable): description string to set
 *
 * Set the @value as the description string of the @text.
 *
 * Since: 3.34
 **/
void
e_cal_component_text_set_value (ECalComponentText *text,
				const gchar *value)
{
	g_return_if_fail (text != NULL);

	if (g_strcmp0 (text->value, value) != 0) {
		g_free (text->value);
		text->value = g_strdup (value);
	}
}

/**
 * e_cal_component_text_get_altrep:
 * @text: an #ECalComponentText
 *
 * Returns: the alternate representation URI of the @text
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_text_get_altrep (const ECalComponentText *text)
{
	g_return_val_if_fail (text != NULL, NULL);

	return text->altrep;
}

/**
 * e_cal_component_text_set_altrep:
 * @text: an #ECalComponentText
 * @altrep: (nullable): alternate representation URI to set
 *
 * Set the @altrep as the alternate representation URI of the @text.
 *
 * Since: 3.34
 **/
void
e_cal_component_text_set_altrep (ECalComponentText *text,
				 const gchar *altrep)
{
	g_return_if_fail (text != NULL);

	if (g_strcmp0 (text->altrep, altrep) != 0) {
		g_free (text->altrep);
		text->altrep = g_strdup (altrep);
	}
}

/**
 * e_cal_component_text_get_language:
 * @text: an #ECalComponentText
 *
 * Returns: the language of the @text
 *
 * Since: 3.46
 **/
const gchar *
e_cal_component_text_get_language (const ECalComponentText *text)
{
	g_return_val_if_fail (text != NULL, NULL);

	return text->language;
}

/**
 * e_cal_component_text_set_language:
 * @text: an #ECalComponentText
 * @language: (nullable): language of the @text
 *
 * Set the @language as the language of the @text. The language tag
 * is defined in RFC 5646. For example `en-US`, not `en_US`.
 *
 * Since: 3.46
 **/
void
e_cal_component_text_set_language (ECalComponentText *text,
				   const gchar *language)
{
	g_return_if_fail (text != NULL);

	if (language && !*language)
		language = NULL;

	if (g_strcmp0 (text->language, language) != 0) {
		g_free (text->language);
		text->language = g_strdup (language);
	}
}

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
 * SECTION:e-cal-component-organizer
 * @short_description: An ECalComponentOrganizer structure
 * @include: libecal/libecal.h
 *
 * Contains functions to work with the #ECalComponentOrganizer structure.
 **/

#include "e-cal-component-parameter-bag.h"

#include "e-cal-component-organizer.h"

G_DEFINE_BOXED_TYPE (ECalComponentOrganizer, e_cal_component_organizer, e_cal_component_organizer_copy, e_cal_component_organizer_free)

struct _ECalComponentOrganizer {
	gchar *value;
	gchar *sentby;
	gchar *cn;
	gchar *language;

	ECalComponentParameterBag *parameter_bag;
};

/**
 * e_cal_component_organizer_new:
 *
 * Creates a new empty #ECalComponentOrganizer structure. Free it
 * with e_cal_component_organizer_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentOrganizer
 *
 * Since: 3.34
 **/
ECalComponentOrganizer *
e_cal_component_organizer_new (void)
{
	ECalComponentOrganizer *organizer;

	organizer = g_new0 (ECalComponentOrganizer, 1);
	organizer->parameter_bag = e_cal_component_parameter_bag_new ();

	return organizer;
}

/**
 * e_cal_component_organizer_new_full:
 * @value: (nullable): usually a "mailto:email" of the organizer
 * @sentby: (nullable): sent by
 * @cn: (nullable): common name
 * @language: (nullable): language
 *
 * Creates a new #ECalComponentOrganizer structure, with all members filled
 * with given values from the parameters. The %NULL and empty strings are
 * treated as unset the value. Free the structure
 * with e_cal_component_organizer_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentOrganizer
 *
 * Since: 3.34
 **/
ECalComponentOrganizer *
e_cal_component_organizer_new_full (const gchar *value,
				    const gchar *sentby,
				    const gchar *cn,
				    const gchar *language)
{
	ECalComponentOrganizer *organizer;

	organizer = e_cal_component_organizer_new ();
	organizer->value = value && *value ? g_strdup (value) : NULL;
	organizer->sentby = sentby && *sentby ? g_strdup (sentby) : NULL;
	organizer->cn = cn && *cn ? g_strdup (cn) : NULL;
	organizer->language = language && *language ? g_strdup (language) : NULL;

	return organizer;
}

/**
 * e_cal_component_organizer_new_from_property:
 * @property: an #ICalProperty of kind %I_CAL_ORGANIZER_PROPERTY
 *
 * Creates a new #ECalComponentOrganizer, filled with values from @property,
 * which should be of kind %I_CAL_ORGANIZER_PROPERTY. The function returns
 * %NULL when it is not of the expected kind. Free the structure
 * with e_cal_component_organizer_free(), when no longer needed.
 *
 * Returns: (transfer full) (nullable): a newly allocated #ECalComponentOrganizer
 *
 * Since: 3.34
 **/
ECalComponentOrganizer *
e_cal_component_organizer_new_from_property (const ICalProperty *property)
{
	ECalComponentOrganizer *organizer;

	g_return_val_if_fail (I_CAL_IS_PROPERTY (property), NULL);

	if (i_cal_property_isa ((ICalProperty *) property) != I_CAL_ORGANIZER_PROPERTY)
		return NULL;

	organizer = e_cal_component_organizer_new ();

	e_cal_component_organizer_set_from_property (organizer, property);

	return organizer;
}

/**
 * e_cal_component_organizer_copy:
 * @organizer: (not nullable): an #ECalComponentOrganizer
 *
 * Returns a newly allocated copy of @organizer, which should be freed with
 * e_cal_component_organizer_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated copy of @organizer
 *
 * Since: 3.34
 **/
ECalComponentOrganizer *
e_cal_component_organizer_copy (const ECalComponentOrganizer *organizer)
{
	ECalComponentOrganizer *copy;

	g_return_val_if_fail (organizer != NULL, NULL);

	copy = e_cal_component_organizer_new_full (organizer->value,
		organizer->sentby,
		organizer->cn,
		organizer->language);

	e_cal_component_parameter_bag_assign (copy->parameter_bag, organizer->parameter_bag);

	return copy;
}

/**
 * e_cal_component_organizer_free: (skip)
 * @organizer: (type ECalComponentOrganizer) (nullable): an #ECalComponentOrganizer to free
 *
 * Free @organizer, previously created by e_cal_component_organizer_new(),
 * e_cal_component_organizer_new_full(), e_cal_component_organizer_new_from_property()
 * or e_cal_component_organizer_copy(). The function does nothing, if @organizer
 * is %NULL.
 *
 * Since: 3.34
 **/
void
e_cal_component_organizer_free (gpointer organizer)
{
	ECalComponentOrganizer *org = organizer;

	if (org) {
		e_cal_component_parameter_bag_free (org->parameter_bag);
		g_free (org->value);
		g_free (org->sentby);
		g_free (org->cn);
		g_free (org->language);
		g_free (org);
	}
}

static gboolean
e_cal_component_organizer_filter_params_cb (ICalParameter *param,
					    gpointer user_data)
{
	ICalParameterKind kind;

	kind = i_cal_parameter_isa (param);

	return kind != I_CAL_SENTBY_PARAMETER &&
	       kind != I_CAL_CN_PARAMETER &&
	       kind != I_CAL_LANGUAGE_PARAMETER;
}

/**
 * e_cal_component_organizer_set_from_property:
 * @organizer: an #ECalComponentOrganizer
 * @property: an #ICalProperty
 *
 * Fill the @organizer structure with the information from
 * the @property, which should be of %I_CAL_ORGANIZER_PROPERTY kind.
 *
 * Since: 3.34
 **/
void
e_cal_component_organizer_set_from_property (ECalComponentOrganizer *organizer,
					     const ICalProperty *property)
{
	ICalProperty *prop = (ICalProperty *) property;
	ICalParameter *param;

	g_return_if_fail (organizer != NULL);
	g_return_if_fail (I_CAL_IS_PROPERTY (property));
	g_return_if_fail (i_cal_property_isa (prop) == I_CAL_ORGANIZER_PROPERTY);

	e_cal_component_organizer_set_value (organizer, i_cal_property_get_organizer (prop));

	param = i_cal_property_get_first_parameter (prop, I_CAL_SENTBY_PARAMETER);
	e_cal_component_organizer_set_sentby (organizer, param ? i_cal_parameter_get_sentby (param) : NULL);
	g_clear_object (&param);

	param = i_cal_property_get_first_parameter (prop, I_CAL_CN_PARAMETER);
	e_cal_component_organizer_set_cn (organizer, param ? i_cal_parameter_get_cn (param) : NULL);
	g_clear_object (&param);

	param = i_cal_property_get_first_parameter (prop, I_CAL_LANGUAGE_PARAMETER);
	e_cal_component_organizer_set_language (organizer, param ? i_cal_parameter_get_language (param) : NULL);
	g_clear_object (&param);

	e_cal_component_parameter_bag_set_from_property (organizer->parameter_bag, prop, e_cal_component_organizer_filter_params_cb, NULL);
}

/**
 * e_cal_component_organizer_get_as_property:
 * @organizer: an #ECalComponentOrganizer
 *
 * Converts information stored in @organizer into an #ICalProperty
 * of %I_CAL_ORGANIZER_PROPERTY kind. The caller is responsible to free
 * the returned object with g_object_unref(), when no longer needed.
 *
 * Returns: (transfer full): a newly created #ICalProperty, containing
 *    information from the @organizer.
 *
 * Since: 3.34
 **/
ICalProperty *
e_cal_component_organizer_get_as_property (const ECalComponentOrganizer *organizer)
{
	ICalProperty *prop;

	g_return_val_if_fail (organizer != NULL, NULL);

	prop = i_cal_property_new (I_CAL_ORGANIZER_PROPERTY);
	g_return_val_if_fail (prop != NULL, NULL);

	e_cal_component_organizer_fill_property (organizer, prop);

	return prop;
}

/**
 * e_cal_component_organizer_fill_property:
 * @organizer: an #ECalComponentOrganizer
 * @property: (inout) (not nullable): an #ICalProperty
 *
 * Fill @property with information from @organizer. The @property
 * should be of kind %I_CAL_ORGANIZER_PROPERTY.
 *
 * Since: 3.34
 **/
void
e_cal_component_organizer_fill_property (const ECalComponentOrganizer *organizer,
					 ICalProperty *property)
{
	ICalParameter *param;

	g_return_if_fail (organizer != NULL);
	g_return_if_fail (I_CAL_IS_PROPERTY (property));
	g_return_if_fail (i_cal_property_isa (property) == I_CAL_ORGANIZER_PROPERTY);

	i_cal_property_set_organizer (property, organizer->value ? organizer->value : "mailto:");

	#define fill_param(_param, _val, _filled) \
		param = i_cal_property_get_first_parameter (property, _param); \
		if (_filled) { \
			if (!param) { \
				param = i_cal_parameter_new (_param); \
				i_cal_property_add_parameter (property, param); \
			} \
			i_cal_parameter_set_ ## _val (param, organizer-> _val); \
			g_clear_object (&param); \
		} else if (param) { \
			i_cal_property_remove_parameter_by_kind (property, _param); \
			g_clear_object (&param); \
		}

	fill_param (I_CAL_SENTBY_PARAMETER, sentby, organizer->sentby && *organizer->sentby);
	fill_param (I_CAL_CN_PARAMETER, cn, organizer->cn && *organizer->cn);
	fill_param (I_CAL_LANGUAGE_PARAMETER, language, organizer->language && *organizer->language);

	#undef fill_param

	e_cal_component_parameter_bag_fill_property (organizer->parameter_bag, property);
}

/**
 * e_cal_component_organizer_get_value:
 * @organizer: an #ECalComponentOrganizer
 *
 * Returns: (nullable): the @organizer URI, usually of "mailto:email" form
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_organizer_get_value (const ECalComponentOrganizer *organizer)
{
	g_return_val_if_fail (organizer != NULL, NULL);

	return organizer->value;
}

/**
 * e_cal_component_organizer_set_value:
 * @organizer: an #ECalComponentOrganizer
 * @value: (nullable): the value to set
 *
 * Set the @organizer URI, usually of "mailto:email" form. The %NULL
 * and empty strings are treated as unset the value.
 *
 * Since: 3.34
 **/
void
e_cal_component_organizer_set_value (ECalComponentOrganizer *organizer,
				     const gchar *value)
{
	g_return_if_fail (organizer != NULL);

	if (value && !*value)
		value = NULL;

	if (g_strcmp0 (organizer->value, value) != 0) {
		g_free (organizer->value);
		organizer->value = g_strdup (value);
	}
}

/**
 * e_cal_component_organizer_get_sentby:
 * @organizer: an #ECalComponentOrganizer
 *
 * Returns: (nullable): the @organizer sentby parameter
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_organizer_get_sentby (const ECalComponentOrganizer *organizer)
{
	g_return_val_if_fail (organizer != NULL, NULL);

	return organizer->sentby;
}

/**
 * e_cal_component_organizer_set_sentby:
 * @organizer: an #ECalComponentOrganizer
 * @sentby: (nullable): the value to set
 *
 * Set the @organizer sentby parameter. The %NULL
 * and empty strings are treated as unset the value.
 *
 * Since: 3.34
 **/
void
e_cal_component_organizer_set_sentby (ECalComponentOrganizer *organizer,
				      const gchar *sentby)
{
	g_return_if_fail (organizer != NULL);

	if (sentby && !*sentby)
		sentby = NULL;

	if (g_strcmp0 (organizer->sentby, sentby) != 0) {
		g_free (organizer->sentby);
		organizer->sentby = g_strdup (sentby);
	}
}

/**
 * e_cal_component_organizer_get_cn:
 * @organizer: an #ECalComponentOrganizer
 *
 * Returns: (nullable): the @organizer common name (cn) parameter
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_organizer_get_cn (const ECalComponentOrganizer *organizer)
{
	g_return_val_if_fail (organizer != NULL, NULL);

	return organizer->cn;
}

/**
 * e_cal_component_organizer_set_cn:
 * @organizer: an #ECalComponentOrganizer
 * @cn: (nullable): the value to set
 *
 * Set the @organizer common name (cn) parameter. The %NULL
 * and empty strings are treated as unset the value.
 *
 * Since: 3.34
 **/
void
e_cal_component_organizer_set_cn (ECalComponentOrganizer *organizer,
				  const gchar *cn)
{
	g_return_if_fail (organizer != NULL);

	if (cn && !*cn)
		cn = NULL;

	if (g_strcmp0 (organizer->cn, cn) != 0) {
		g_free (organizer->cn);
		organizer->cn = g_strdup (cn);
	}
}

/**
 * e_cal_component_organizer_get_language:
 * @organizer: an #ECalComponentOrganizer
 *
 * Returns: (nullable): the @organizer language parameter
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_organizer_get_language (const ECalComponentOrganizer *organizer)
{
	g_return_val_if_fail (organizer != NULL, NULL);

	return organizer->language;
}

/**
 * e_cal_component_organizer_set_language:
 * @organizer: an #ECalComponentOrganizer
 * @language: (nullable): the value to set
 *
 * Set the @organizer language parameter. The %NULL
 * and empty strings are treated as unset the value.
 *
 * Since: 3.34
 **/
void
e_cal_component_organizer_set_language (ECalComponentOrganizer *organizer,
					const gchar *language)
{
	g_return_if_fail (organizer != NULL);

	if (language && !*language)
		language = NULL;

	if (g_strcmp0 (organizer->language, language) != 0) {
		g_free (organizer->language);
		organizer->language = g_strdup (language);
	}
}

/**
 * e_cal_component_organizer_get_parameter_bag:
 * @organizer: an #ECalComponentOrganizer
 *
 * Returns: (transfer none): an #ECalComponentParameterBag with additional
 *    parameters stored with the organizer property, other than those accessible
 *    with the other functions of the @organizer.
 *
 * Since: 3.34
 **/
ECalComponentParameterBag *
e_cal_component_organizer_get_parameter_bag (const ECalComponentOrganizer *organizer)
{
	g_return_val_if_fail (organizer != NULL, NULL);

	return organizer->parameter_bag;
}

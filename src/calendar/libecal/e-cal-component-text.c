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

	text = g_new0 (ECalComponentText, 1);
	text->value = g_strdup (value);
	text->altrep = g_strdup (altrep);

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
	g_return_val_if_fail (text != NULL, NULL);

	return e_cal_component_text_new (text->value, text->altrep);
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
		g_free (te);
	}
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

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2012 Intel Corporation
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

#include <string.h>
#include <glib-object.h>

#include "e-data-server-util.h"

#include "e-named-parameters.h"

/**
 * SECTION: e-named-parameters
 * @include: libedataserver/libedataserver.h
 * @short_description: A structure to hold named parameters
 *
 * The #ENamedParameters is a structure, which holds a name~>value
 * pairs. It's usually used to pass credentials between callers.
 **/

static ENamedParameters *
e_named_parameters_ref (ENamedParameters *params)
{
	return (ENamedParameters *) g_ptr_array_ref ((GPtrArray *) params);
}

static void
e_named_parameters_unref (ENamedParameters *params)
{
	g_ptr_array_unref ((GPtrArray *) params);
}

G_DEFINE_BOXED_TYPE (ENamedParameters, e_named_parameters, e_named_parameters_ref, e_named_parameters_unref)

/**
 * e_named_parameters_new:
 *
 * Creates a new instance of an #ENamedParameters. This should be freed
 * with e_named_parameters_free(), when no longer needed. Names are
 * compared case insensitively.
 *
 * The structure is not thread safe, if the caller requires thread safety,
 * then it should provide it on its own.
 *
 * Returns: newly allocated #ENamedParameters
 *
 * Since: 3.8
 **/
ENamedParameters *
e_named_parameters_new (void)
{
	return (ENamedParameters *) g_ptr_array_new_with_free_func ((GDestroyNotify) e_util_safe_free_string);
}

/**
 * e_named_parameters_new_strv:
 * @strv: NULL-terminated string array to be used as a content of a newly
 *     created #ENamedParameters
 *
 * Creates a new instance of an #ENamedParameters, with initial content
 * being taken from @strv. This should be freed with e_named_parameters_free(),
 * when no longer needed. Names are compared case insensitively.
 *
 * The structure is not thread safe, if the caller requires thread safety,
 * then it should provide it on its own.
 *
 * Returns: newly allocated #ENamedParameters
 *
 * Since: 3.8
 **/
ENamedParameters *
e_named_parameters_new_strv (const gchar * const *strv)
{
	ENamedParameters *parameters;
	gint ii;

	g_return_val_if_fail (strv != NULL, NULL);

	parameters = e_named_parameters_new ();
	for (ii = 0; strv[ii]; ii++) {
		g_ptr_array_add ((GPtrArray *) parameters, g_strdup (strv[ii]));
	}

	return parameters;
}

/**
 * e_named_parameters_new_string:
 * @str: a string to be used as a content of a newly created #ENamedParameters
 *
 * Creates a new instance of an #ENamedParameters, with initial content being
 * taken from @str. This should be freed with e_named_parameters_free(),
 * when no longer needed. Names are compared case insensitively.
 *
 * The @str should be created with e_named_parameters_to_string(), to be
 * properly encoded.
 *
 * The structure is not thread safe, if the caller requires thread safety,
 * then it should provide it on its own.
 *
 * Returns: (transfer full): newly allocated #ENamedParameters
 *
 * Since: 3.18
 **/
ENamedParameters *
e_named_parameters_new_string (const gchar *str)
{
	ENamedParameters *parameters;
	gchar **split;
	gint ii;

	g_return_val_if_fail (str != NULL, NULL);

	split = g_strsplit (str, "\n", -1);

	parameters = e_named_parameters_new ();
	for (ii = 0; split && split[ii]; ii++) {
		g_ptr_array_add ((GPtrArray *) parameters, g_strcompress (split[ii]));
	}

	g_strfreev (split);

	return parameters;
}

/**
 * e_named_parameters_new_clone:
 * @parameters: an #ENamedParameters to be used as a content of a newly
 *    created #ENamedParameters
 *
 * Creates a new instance of an #ENamedParameters, with initial content
 * being taken from @parameters. This should be freed with e_named_parameters_free(),
 * when no longer needed. Names are compared case insensitively.
 *
 * The structure is not thread safe, if the caller requires thread safety,
 * then it should provide it on its own.
 *
 * Returns: newly allocated #ENamedParameters
 *
 * Since: 3.16
 **/
ENamedParameters *
e_named_parameters_new_clone (const ENamedParameters *parameters)
{
	ENamedParameters *clone;

	clone = e_named_parameters_new ();
	if (parameters)
		e_named_parameters_assign (clone, parameters);

	return clone;
}

/**
 * e_named_parameters_free:
 * @parameters: (nullable): an #ENamedParameters
 *
 * Frees an instance of #ENamedParameters, previously allocated
 * with e_named_parameters_new(). Function does nothing, if
 * @parameters is %NULL.
 *
 * Since: 3.8
 **/
void
e_named_parameters_free (ENamedParameters *parameters)
{
	if (!parameters)
		return;

	g_ptr_array_unref ((GPtrArray *) parameters);
}

/**
 * e_named_parameters_clear:
 * @parameters: an #ENamedParameters
 *
 * Removes all stored parameters from @parameters.
 *
 * Since: 3.8
 **/
void
e_named_parameters_clear (ENamedParameters *parameters)
{
	GPtrArray *array;
	g_return_if_fail (parameters != NULL);

	array = (GPtrArray *) parameters;

	if (array->len)
		g_ptr_array_remove_range (array, 0, array->len);
}

/**
 * e_named_parameters_assign:
 * @parameters: an #ENamedParameters to assign values to
 * @from: (nullable): an #ENamedParameters to get values from, or %NULL
 *
 * Makes content of the @parameters the same as @from.
 * Functions clears content of @parameters if @from is %NULL.
 *
 * Since: 3.8
 **/
void
e_named_parameters_assign (ENamedParameters *parameters,
                           const ENamedParameters *from)
{
	g_return_if_fail (parameters != NULL);

	e_named_parameters_clear (parameters);

	if (from) {
		gint ii;
		GPtrArray *from_array = (GPtrArray *) from;

		for (ii = 0; ii < from_array->len; ii++) {
			g_ptr_array_add (
				(GPtrArray *) parameters,
				g_strdup (from_array->pdata[ii]));
		}
	}
}

static gint
get_parameter_index (const ENamedParameters *parameters,
                     const gchar *name)
{
	GPtrArray *array;
	gint ii, name_len;

	g_return_val_if_fail (parameters != NULL, -1);
	g_return_val_if_fail (name != NULL, -1);

	name_len = strlen (name);

	array = (GPtrArray *) parameters;

	for (ii = 0; ii < array->len; ii++) {
		const gchar *name_and_value = g_ptr_array_index (array, ii);

		if (name_and_value == NULL || strlen (name_and_value) <= name_len)
			continue;

		if (name_and_value[name_len] != ':')
			continue;

		if (g_ascii_strncasecmp (name_and_value, name, name_len) == 0)
			return ii;
	}

	return -1;
}

/**
 * e_named_parameters_set:
 * @parameters: an #ENamedParameters
 * @name: name of a parameter to set
 * @value: (nullable): value to set, or %NULL to unset
 *
 * Sets parameter named @name to value @value. If @value is NULL,
 * then the parameter is removed. @value can be an empty string.
 *
 * Note: There is a restriction on parameter names, it cannot be empty or
 * contain a colon character (':'), otherwise it can be pretty much anything.
 *
 * Since: 3.8
 **/
void
e_named_parameters_set (ENamedParameters *parameters,
                        const gchar *name,
                        const gchar *value)
{
	GPtrArray *array;
	gint index;
	gchar *name_and_value;

	g_return_if_fail (parameters != NULL);
	g_return_if_fail (name != NULL);
	g_return_if_fail (strchr (name, ':') == NULL);
	g_return_if_fail (*name != '\0');

	array = (GPtrArray *) parameters;

	index = get_parameter_index (parameters, name);
	if (!value) {
		if (index != -1)
			g_ptr_array_remove_index (array, index);
		return;
	}

	name_and_value = g_strconcat (name, ":", value, NULL);
	if (index != -1) {
		g_free (array->pdata[index]);
		array->pdata[index] = name_and_value;
	} else {
		g_ptr_array_add (array, name_and_value);
	}
}

/**
 * e_named_parameters_get:
 * @parameters: an #ENamedParameters
 * @name: name of a parameter to get
 *
 * Returns current value of a parameter with name @name. If not such
 * exists, then returns %NULL.
 *
 * Returns: (nullable): value of a parameter named @name, or %NULL.
 *
 * Since: 3.8
 **/
const gchar *
e_named_parameters_get (const ENamedParameters *parameters,
                        const gchar *name)
{
	gint index;
	const gchar *name_and_value;

	g_return_val_if_fail (parameters != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	index = get_parameter_index (parameters, name);
	if (index == -1)
		return NULL;

	name_and_value = g_ptr_array_index ((GPtrArray *) parameters, index);

	return name_and_value + strlen (name) + 1;
}

/**
 * e_named_parameters_test:
 * @parameters: an #ENamedParameters
 * @name: name of a parameter to test
 * @value: value to test
 * @case_sensitively: whether to compare case sensitively
 *
 * Compares current value of parameter named @name with given @value
 * and returns whether they are equal, either case sensitively or
 * insensitively, based on @case_sensitively argument. Function
 * returns %FALSE, if no such parameter exists.
 *
 * Returns: Whether parameter of given name has stored value of given value.
 *
 * Since: 3.8
 **/
gboolean
e_named_parameters_test (const ENamedParameters *parameters,
                         const gchar *name,
                         const gchar *value,
                         gboolean case_sensitively)
{
	const gchar *stored_value;

	g_return_val_if_fail (parameters != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	stored_value = e_named_parameters_get (parameters, name);
	if (!stored_value)
		return FALSE;

	if (case_sensitively)
		return strcmp (stored_value, value) == 0;

	return g_ascii_strcasecmp (stored_value, value) == 0;
}

/**
 * e_named_parameters_to_strv:
 * @parameters: an #ENamedParameters
 *
 * Returns: (transfer full): Contents of @parameters as a null-terminated strv
 *
 * Since: 3.8
 */
gchar **
e_named_parameters_to_strv (const ENamedParameters *parameters)
{
	GPtrArray *array = (GPtrArray *) parameters;
	GPtrArray *ret = g_ptr_array_new ();

	if (array) {
		guint i;
		for (i = 0; i < array->len; i++) {
			g_ptr_array_add (ret, g_strdup (array->pdata[i]));
		}
	}

	g_ptr_array_add (ret, NULL);

	return (gchar **) g_ptr_array_free (ret, FALSE);
}

/**
 * e_named_parameters_to_string:
 * @parameters: an #ENamedParameters
 *
 * Returns: (transfer full) (nullable): Contents of @parameters as a string
 *
 * Since: 3.18
 */
gchar *
e_named_parameters_to_string (const ENamedParameters *parameters)
{
	gchar **strv, *str;
	gint ii;

	strv = e_named_parameters_to_strv (parameters);
	if (!strv)
		return NULL;

	for (ii = 0; strv[ii]; ii++) {
		gchar *name_and_value = strv[ii];

		strv[ii] = g_strescape (name_and_value, "");
		g_free (name_and_value);
	}

	str = g_strjoinv ("\n", strv);

	g_strfreev (strv);

	return str;
}

/**
 * e_named_parameters_exists:
 * @parameters: an #ENamedParameters
 * @name: name of the parameter whose existence to check
 *
 * Returns: Whether @parameters holds a parameter named @name
 *
 * Since: 3.18
 **/
gboolean
e_named_parameters_exists (const ENamedParameters *parameters,
			   const gchar *name)
{
	g_return_val_if_fail (parameters != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	return get_parameter_index (parameters, name) != -1;
}

/**
 * e_named_parameters_count:
 * @parameters: an #ENamedParameters
 *
 * Returns: The number of stored named parameters in @parameters
 *
 * Since: 3.18
 **/
guint
e_named_parameters_count (const ENamedParameters *parameters)
{
	g_return_val_if_fail (parameters != NULL, 0);

	return ((GPtrArray *) parameters)->len;
}

/**
 * e_named_parameters_get_name:
 * @parameters: an #ENamedParameters
 * @index: an index of the parameter whose name to retrieve
 *
 * Returns: (transfer full) (nullable): The name of the parameters at index @index,
 *    or %NULL, of the @index is out of bounds or other error. The returned
 *    string should be freed with g_free() when done with it.
 *
 * Since: 3.18
 **/
gchar *
e_named_parameters_get_name (const ENamedParameters *parameters,
			     gint index)
{
	const gchar *name_and_value, *colon;

	g_return_val_if_fail (parameters != NULL, NULL);
	g_return_val_if_fail (index >= 0 && index < e_named_parameters_count (parameters), NULL);

	name_and_value = g_ptr_array_index ((GPtrArray *) parameters, index);
	colon = name_and_value ? strchr (name_and_value, ':') : NULL;

	if (!colon || colon == name_and_value)
		return NULL;

	return g_strndup (name_and_value, colon - name_and_value);
}

/**
 * e_named_parameters_equal:
 * @parameters1: the first #ENamedParameters
 * @parameters2: the second #ENamedParameters
 *
 * Compares the two parameters objects and returns whether they equal.
 * Note a %NULL and empty parameters are also considered equal.
 *
 * Returns: whether the two parameters are equal
 *
 * Since: 3.46
 **/
gboolean
e_named_parameters_equal (const ENamedParameters *parameters1,
			  const ENamedParameters *parameters2)
{
	GPtrArray *arr1, *arr2;
	guint ii, jj;

	if (parameters1 == parameters2 ||
	    (!parameters1 && e_named_parameters_count (parameters2) == 0) ||
	    (!parameters2 && e_named_parameters_count (parameters1) == 0))
		return TRUE;

	if (!parameters1 || !parameters2 ||
	    e_named_parameters_count (parameters1) != e_named_parameters_count (parameters2))
		return FALSE;

	arr1 = (GPtrArray *) parameters1;
	arr2 = (GPtrArray *) parameters2;

	for (ii = 0; ii < arr1->len; ii++) {
		const gchar *name_and_value1 = g_ptr_array_index (arr1, ii);

		for (jj = 0; jj < arr2->len; jj++) {
			const gchar *name_and_value2 = g_ptr_array_index (arr2, jj);

			if (g_strcmp0 (name_and_value1, name_and_value2) == 0)
				break;
		}

		/* went through all the items, none matched */
		if (jj == arr2->len)
			return FALSE;
	}

	return TRUE;
}

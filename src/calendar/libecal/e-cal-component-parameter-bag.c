/*
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
 * SECTION:e-cal-component-parameter-bag
 * @short_description: An ECalComponentParameterBag structure
 * @include: libecal/libecal.h
 *
 * Contains functions to work with the #ECalComponentParameterBag structure.
 **/

#include "e-cal-component-parameter-bag.h"

G_DEFINE_BOXED_TYPE (ECalComponentParameterBag, e_cal_component_parameter_bag, e_cal_component_parameter_bag_copy, e_cal_component_parameter_bag_free)

struct _ECalComponentParameterBag {
	GPtrArray *parameters; /* ICalParameter * */
};

/**
 * e_cal_component_parameter_bag_new:
 *
 * Creates a new #ECalComponentParameterBag. Free the structure
 * with e_cal_component_parameter_bag_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentParameterBag
 *
 * Since: 3.34
 **/
ECalComponentParameterBag *
e_cal_component_parameter_bag_new (void)
{
	ECalComponentParameterBag *bag;

	bag = g_slice_new0 (ECalComponentParameterBag);
	bag->parameters = g_ptr_array_new_with_free_func (g_object_unref);

	return bag;
}

/**
 * e_cal_component_parameter_bag_new_from_property:
 * @property: an #ICalProperty containing the parameters to fill the bag with
 * @func: (nullable) (scope call) (closure user_data): an optional %ECalComponentParameterBagFilterFunc callback
 * @user_data: user data for the @func
 *
 * Creates a new #ECalComponentParameterBag, filled with parameters
 * from the @property, for which the @func returned %TRUE. When
 * the @func is %NULL, all the parameters are included.
 *
 * Free the structure with e_cal_component_parameter_bag_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentParameterBag
 *
 * Since: 3.34
 **/
ECalComponentParameterBag *
e_cal_component_parameter_bag_new_from_property (const ICalProperty *property,
						 ECalComponentParameterBagFilterFunc func,
						 gpointer user_data)
{
	ECalComponentParameterBag *bag;

	bag = e_cal_component_parameter_bag_new ();

	e_cal_component_parameter_bag_set_from_property (bag, property, func, user_data);

	return bag;
}

/**
 * e_cal_component_parameter_bag_copy:
 * @bag: (not nullable): an #ECalComponentParameterBag
 *
 * Returns a newly allocated copy of @bag, which should be freed with
 * e_cal_component_parameter_bag_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated copy of @bag
 *
 * Since: 3.34
 **/
ECalComponentParameterBag *
e_cal_component_parameter_bag_copy (const ECalComponentParameterBag *bag)
{
	ECalComponentParameterBag *copy;

	g_return_val_if_fail (bag != NULL, NULL);

	copy = e_cal_component_parameter_bag_new ();

	e_cal_component_parameter_bag_assign (copy, bag);

	return copy;
}

/**
 * e_cal_component_parameter_bag_free: (skip)
 * @bag: (type ECalComponentParameterBag) (nullable): an #ECalComponentParameterBag to free
 *
 * Free @bag, previously created by e_cal_component_parameter_bag_new(),
 * e_cal_component_parameter_bag_new_from_component() or
 * e_cal_component_parameter_bag_copy(). The function does nothing, if @bag
 * is %NULL.
 *
 * Since: 3.34
 **/
void
e_cal_component_parameter_bag_free (gpointer bag)
{
	ECalComponentParameterBag *bg = bag;

	if (bg) {
		g_ptr_array_unref (bg->parameters);
		g_slice_free (ECalComponentParameterBag, bg);
	}
}

/**
 * e_cal_component_parameter_bag_set_from_property:
 * @bag: an #ECalComponentParameterBag
 * @property: an #ICalProperty containing the parameters to fill the @bag with
 * @func: (nullable) (scope call) (closure user_data): an optional %ECalComponentParameterBagFilterFunc callback
 * @user_data: user data for the @func
 *
 * Fills the @bag with parameters from the @property, for which the @func
 * returned %TRUE. When the @func is %NULL, all the parameters are included.
 * The @bag content is cleared before any parameter is added.
 *
 * Since: 3.34
 **/
void
e_cal_component_parameter_bag_set_from_property (ECalComponentParameterBag *bag,
						 const ICalProperty *property,
						 ECalComponentParameterBagFilterFunc func,
						 gpointer user_data)
{
	ICalProperty *prop = (ICalProperty *) property;
	ICalParameter *param;

	g_return_if_fail (bag != NULL);
	g_return_if_fail (I_CAL_IS_PROPERTY ((ICalProperty *) property));

	e_cal_component_parameter_bag_clear (bag);

	for (param = i_cal_property_get_first_parameter (prop, I_CAL_ANY_PARAMETER);
	     param;
	     g_object_unref (param), param = i_cal_property_get_next_parameter (prop, I_CAL_ANY_PARAMETER)) {
		if (!func || func (param, user_data)) {
			e_cal_component_parameter_bag_add (bag, param);
		}
	}
}

/**
 * e_cal_component_parameter_bag_fill_property:
 * @bag: an #ECalComponentParameterBag
 * @property: an #ICalProperty
 *
 * Adds all the stored parameters in the @bag to the @property.
 * The function replaces any existing parameter with the new value,
 * if any such exists. Otherwise the parameter is added.
 *
 * Since: 3.34
 **/
void
e_cal_component_parameter_bag_fill_property (const ECalComponentParameterBag *bag,
					     ICalProperty *property)
{
	guint ii;

	g_return_if_fail (bag != NULL);
	g_return_if_fail (I_CAL_IS_PROPERTY (property));

	for (ii = 0; ii < bag->parameters->len; ii++) {
		ICalParameter *param = g_ptr_array_index (bag->parameters, ii);

		if (param)
			i_cal_property_take_parameter (property, i_cal_parameter_clone (param));
	}
}

/**
 * e_cal_component_parameter_bag_assign:
 * @bag: a destination #ECalComponentParameterBag
 * @src_bag: a source #ECalComponentParameterBag
 *
 * Assigns content of the @src_bag into the @bag.
 *
 * Since: 3.34
 **/
void
e_cal_component_parameter_bag_assign (ECalComponentParameterBag *bag,
				     const ECalComponentParameterBag *src_bag)
{
	guint count, ii;

	g_return_if_fail (bag != NULL);
	g_return_if_fail (src_bag != NULL);

	e_cal_component_parameter_bag_clear (bag);
	count = e_cal_component_parameter_bag_get_count (src_bag);

	if (count) {
		for (ii = 0; ii < count; ii++) {
			ICalParameter *param;

			param = e_cal_component_parameter_bag_get (src_bag, ii);

			e_cal_component_parameter_bag_add (bag, param);
		}
	}
}

/**
 * e_cal_component_parameter_bag_add:
 * @bag: an #ECalComponentParameterBag
 * @param: an #ICalParameter
 *
 * Adds a copy of the @param into the @bag.
 *
 * Since: 3.34
 **/
void
e_cal_component_parameter_bag_add (ECalComponentParameterBag *bag,
				  const ICalParameter *param)
{
	g_return_if_fail (bag != NULL);
	g_return_if_fail (I_CAL_IS_PARAMETER ((ICalParameter *) param));

	e_cal_component_parameter_bag_take (bag,
		i_cal_parameter_clone ((ICalParameter *) param));
}

/**
 * e_cal_component_parameter_bag_take:
 * @bag: an #ECalComponentParameterBag
 * @param: (transfer full): an #ICalParameter
 *
 * Adds the @param into the @bag and assumes ownership of the @param.
 *
 * Since: 3.34
 **/
void
e_cal_component_parameter_bag_take (ECalComponentParameterBag *bag,
				   ICalParameter *param)
{
	g_return_if_fail (bag != NULL);
	g_return_if_fail (I_CAL_IS_PARAMETER (param));

	g_ptr_array_add (bag->parameters, param);
}

/**
 * e_cal_component_parameter_bag_get_count:
 * @bag: an #ECalComponentParameterBag
 *
 * Returns: how many parameters are stored in the @bag
 *
 * Since: 3.34
 **/
guint
e_cal_component_parameter_bag_get_count (const ECalComponentParameterBag *bag)
{
	g_return_val_if_fail (bag != NULL, 0);
	g_return_val_if_fail (bag->parameters != NULL, 0);

	return bag->parameters->len;
}

/**
 * e_cal_component_parameter_bag_get:
 * @bag: an #ECalComponentParameterBag
 * @index: an index of the parameter to get
 *
 * Returns the #ICalParameter at the given @index. If the @index is
 * out of bounds (not lower than e_cal_component_parameter_bag_get_count()),
 * then %NULL is returned.
 *
 * The returned parameter is owned by the @bag and should not be freed
 * by the caller.
 *
 * Returns: (transfer none) (nullable): the #ICalParameter at the given @index,
 *    or %NULL on error
 *
 * Since: 3.34
 **/
ICalParameter *
e_cal_component_parameter_bag_get (const ECalComponentParameterBag *bag,
				  guint index)
{
	g_return_val_if_fail (bag != NULL, NULL);
	g_return_val_if_fail (bag->parameters != NULL, NULL);

	if (index >= bag->parameters->len)
		return NULL;

	return g_ptr_array_index (bag->parameters, index);
}

/**
 * e_cal_component_parameter_bag_get_first_by_kind:
 * @bag: an #ECalComponentParameterBag
 * @kind: an #ICalParameterKind to search for
 *
 * Returns: the index of the first parameter of the given @kind, or value
 *    out of bounds, if such parameter cannot be found
 *
 * Since: 3.34
 **/
guint
e_cal_component_parameter_bag_get_first_by_kind (const ECalComponentParameterBag *bag,
						 ICalParameterKind kind)
{
	guint index;

	g_return_val_if_fail (bag != NULL, ~0);
	g_return_val_if_fail (bag->parameters != NULL, ~0);

	for (index = 0; index < bag->parameters->len; index++) {
		ICalParameter *param;

		param = g_ptr_array_index (bag->parameters, index);
		if (param && i_cal_parameter_isa (param) == kind)
			return index;
	}

	return ~0;
}

/**
 * e_cal_component_parameter_bag_remove:
 * @bag: an #ECalComponentParameterBag
 * @index: an index of the parameter to remove
 *
 * Removes the #ICalParameter at the given @index. If the @index is
 * out of bounds (not lower than e_cal_component_parameter_bag_get_count()),
 * then the function does nothing.
 *
 * Since: 3.34
 **/
void
e_cal_component_parameter_bag_remove (ECalComponentParameterBag *bag,
				     guint index)
{
	g_return_if_fail (bag != NULL);
	g_return_if_fail (bag->parameters != NULL);

	if (index < bag->parameters->len)
		g_ptr_array_remove_index (bag->parameters, index);
}

/**
 * e_cal_component_parameter_bag_remove_by_kind:
 * @bag: an #ECalComponentParameterBag
 * @kind: an #ICalParameterKind to remove
 * @all: %TRUE to remove all parameters of the @kind, or %FALSE to only the first
 *
 * Removes the first or all (depending on the @all) parameters of the given @kind.
 *
 * Returns: how many parameters had been removed
 *
 * Since: 3.34
 **/
guint
e_cal_component_parameter_bag_remove_by_kind (ECalComponentParameterBag *bag,
					      ICalParameterKind kind,
					      gboolean all)
{
	guint index, count = 0;

	g_return_val_if_fail (bag != NULL, 0);
	g_return_val_if_fail (bag->parameters != NULL, 0);

	index = 0;
	while (index < bag->parameters->len) {
		ICalParameter *param;

		param = g_ptr_array_index (bag->parameters, index);
		if (param && i_cal_parameter_isa (param) == kind) {
			g_ptr_array_remove_index (bag->parameters, index);
			count++;

			if (!all)
				break;
		} else {
			index++;
		}
	}

	return count;
}

/**
 * e_cal_component_parameter_bag_clear:
 * @bag: an #ECalComponentParameterBag
 *
 * Removes all parameters from the @bag, thus it doesn't contain any
 * parameter after this function returns.
 *
 * Since: 3.34
 **/
void
e_cal_component_parameter_bag_clear (ECalComponentParameterBag *bag)
{
	g_return_if_fail (bag != NULL);
	g_return_if_fail (bag->parameters != NULL);

	g_ptr_array_set_size (bag->parameters, 0);
}

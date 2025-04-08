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
 * SECTION:e-cal-component-property-bag
 * @short_description: An ECalComponentPropertyBag structure
 * @include: libecal/libecal.h
 *
 * Contains functions to work with the #ECalComponentPropertyBag structure.
 **/

#include "e-cal-component-property-bag.h"

G_DEFINE_BOXED_TYPE (ECalComponentPropertyBag, e_cal_component_property_bag, e_cal_component_property_bag_copy, e_cal_component_property_bag_free)

struct _ECalComponentPropertyBag {
	GPtrArray *properties; /* ICalProperty * */
};

/**
 * e_cal_component_property_bag_new:
 *
 * Creates a new #ECalComponentPropertyBag. Free the structure
 * with e_cal_component_property_bag_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentPropertyBag
 *
 * Since: 3.34
 **/
ECalComponentPropertyBag *
e_cal_component_property_bag_new (void)
{
	ECalComponentPropertyBag *bag;

	bag = g_slice_new0 (ECalComponentPropertyBag);
	bag->properties = g_ptr_array_new_with_free_func (g_object_unref);

	return bag;
}

/**
 * e_cal_component_property_bag_new_from_component:
 * @component: an #ICalComponent containing the properties to fill the bag with
 * @func: (nullable) (scope call) (closure user_data): an optional %ECalComponentPropertyBagFilterFunc callback
 * @user_data: user data for the @func
 *
 * Creates a new #ECalComponentPropertyBag, filled with properties
 * from the @component, for which the @func returned %TRUE. When
 * the @func is %NULL, all the properties are included.
 *
 * Free the structure with e_cal_component_property_bag_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentPropertyBag
 *
 * Since: 3.34
 **/
ECalComponentPropertyBag *
e_cal_component_property_bag_new_from_component (const ICalComponent *component,
						 ECalComponentPropertyBagFilterFunc func,
						 gpointer user_data)
{
	ECalComponentPropertyBag *bag;

	bag = e_cal_component_property_bag_new ();

	e_cal_component_property_bag_set_from_component (bag, component, func, user_data);

	return bag;
}

/**
 * e_cal_component_property_bag_copy:
 * @bag: (not nullable): an #ECalComponentPropertyBag
 *
 * Returns a newly allocated copy of @bag, which should be freed with
 * e_cal_component_property_bag_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated copy of @bag
 *
 * Since: 3.34
 **/
ECalComponentPropertyBag *
e_cal_component_property_bag_copy (const ECalComponentPropertyBag *bag)
{
	ECalComponentPropertyBag *copy;

	g_return_val_if_fail (bag != NULL, NULL);

	copy = e_cal_component_property_bag_new ();

	e_cal_component_property_bag_assign (copy, bag);

	return copy;
}

/**
 * e_cal_component_property_bag_free: (skip)
 * @bag: (type ECalComponentPropertyBag) (nullable): an #ECalComponentPropertyBag to free
 *
 * Free @bag, previously created by e_cal_component_property_bag_new(),
 * e_cal_component_property_bag_new_from_component() or
 * e_cal_component_property_bag_copy(). The function does nothing, if @bag
 * is %NULL.
 *
 * Since: 3.34
 **/
void
e_cal_component_property_bag_free (gpointer bag)
{
	ECalComponentPropertyBag *bg = bag;

	if (bg) {
		g_ptr_array_unref (bg->properties);
		g_slice_free (ECalComponentPropertyBag, bg);
	}
}

/**
 * e_cal_component_property_bag_set_from_component:
 * @bag: an #ECalComponentPropertyBag
 * @component: an #ICalComponent containing the properties to fill the @bag with
 * @func: (nullable) (scope call) (closure user_data): an optional %ECalComponentPropertyBagFilterFunc callback
 * @user_data: user data for the @func
 *
 * Fills the @bag with properties from the @component, for which the @func
 * returned %TRUE. When the @func is %NULL, all the properties are included.
 * The @bag content is cleared before any property is added.
 *
 * Since: 3.34
 **/
void
e_cal_component_property_bag_set_from_component (ECalComponentPropertyBag *bag,
						 const ICalComponent *component,
						 ECalComponentPropertyBagFilterFunc func,
						 gpointer user_data)
{
	ICalComponent *comp = (ICalComponent *) component;
	ICalProperty *prop;

	g_return_if_fail (bag != NULL);
	g_return_if_fail (I_CAL_IS_COMPONENT ((ICalComponent *) component));

	e_cal_component_property_bag_clear (bag);

	for (prop = i_cal_component_get_first_property (comp, I_CAL_ANY_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (comp, I_CAL_ANY_PROPERTY)) {
		if (!func || func (prop, user_data)) {
			e_cal_component_property_bag_add (bag, prop);
		}
	}
}

/**
 * e_cal_component_property_bag_fill_component:
 * @bag: an #ECalComponentPropertyBag
 * @component: an #ICalComponent
 *
 * Adds all the stored properties in the @bag to the @component.
 * The function doesn't verify whether the @component contains
 * the same property already.
 *
 * Since: 3.34
 **/
void
e_cal_component_property_bag_fill_component (const ECalComponentPropertyBag *bag,
					     ICalComponent *component)
{
	guint ii;

	g_return_if_fail (bag != NULL);
	g_return_if_fail (I_CAL_IS_COMPONENT (component));

	for (ii = 0; ii < bag->properties->len; ii++) {
		ICalProperty *prop = g_ptr_array_index (bag->properties, ii);

		if (prop)
			i_cal_component_take_property (component, i_cal_property_clone (prop));
	}
}

/**
 * e_cal_component_property_bag_assign:
 * @bag: a destination #ECalComponentPropertyBag
 * @src_bag: a source #ECalComponentPropertyBag
 *
 * Assigns content of the @src_bag into the @bag.
 *
 * Since: 3.34
 **/
void
e_cal_component_property_bag_assign (ECalComponentPropertyBag *bag,
				     const ECalComponentPropertyBag *src_bag)
{
	guint count, ii;

	g_return_if_fail (bag != NULL);
	g_return_if_fail (src_bag != NULL);

	e_cal_component_property_bag_clear (bag);
	count = e_cal_component_property_bag_get_count (src_bag);

	if (count) {
		for (ii = 0; ii < count; ii++) {
			ICalProperty *prop;

			prop = e_cal_component_property_bag_get (src_bag, ii);

			e_cal_component_property_bag_add (bag, prop);
		}
	}
}

/**
 * e_cal_component_property_bag_add:
 * @bag: an #ECalComponentPropertyBag
 * @prop: an #ICalProperty
 *
 * Adds a copy of the @prop into the @bag.
 *
 * Since: 3.34
 **/
void
e_cal_component_property_bag_add (ECalComponentPropertyBag *bag,
				  const ICalProperty *prop)
{
	g_return_if_fail (bag != NULL);
	g_return_if_fail (I_CAL_IS_PROPERTY ((ICalProperty *) prop));

	e_cal_component_property_bag_take (bag,
		i_cal_property_clone ((ICalProperty *) prop));
}

/**
 * e_cal_component_property_bag_take:
 * @bag: an #ECalComponentPropertyBag
 * @prop: (transfer full): an #ICalProperty
 *
 * Adds the @prop into the @bag and assumes ownership of the @prop.
 *
 * Since: 3.34
 **/
void
e_cal_component_property_bag_take (ECalComponentPropertyBag *bag,
				   ICalProperty *prop)
{
	g_return_if_fail (bag != NULL);
	g_return_if_fail (I_CAL_IS_PROPERTY (prop));

	g_ptr_array_add (bag->properties, prop);
}

/**
 * e_cal_component_property_bag_get_count:
 * @bag: an #ECalComponentPropertyBag
 *
 * Returns: how many properties are stored in the @bag
 *
 * Since: 3.34
 **/
guint
e_cal_component_property_bag_get_count (const ECalComponentPropertyBag *bag)
{
	g_return_val_if_fail (bag != NULL, 0);
	g_return_val_if_fail (bag->properties != NULL, 0);

	return bag->properties->len;
}

/**
 * e_cal_component_property_bag_get:
 * @bag: an #ECalComponentPropertyBag
 * @index: an index of the property to get
 *
 * Returns the #ICalProperty at the given @index. If the @index is
 * out of bounds (not lower than e_cal_component_property_bag_get_count()),
 * then %NULL is returned.
 *
 * The returned property is owned by the @bag and should not be freed
 * by the caller.
 *
 * Returns: (transfer none) (nullable): the #ICalProperty at the given @index,
 *    or %NULL on error
 *
 * Since: 3.34
 **/
ICalProperty *
e_cal_component_property_bag_get (const ECalComponentPropertyBag *bag,
				  guint index)
{
	g_return_val_if_fail (bag != NULL, NULL);
	g_return_val_if_fail (bag->properties != NULL, NULL);

	if (index >= bag->properties->len)
		return NULL;

	return g_ptr_array_index (bag->properties, index);
}

/**
 * e_cal_component_property_bag_get_first_by_kind:
 * @bag: an #ECalComponentPropertyBag
 * @kind: an #ICalPropertyKind to search for
 *
 * Returns: the index of the first property of the given @kind, or value
 *    out of bounds, if such property cannot be found
 *
 * Since: 3.34
 **/
guint
e_cal_component_property_bag_get_first_by_kind (const ECalComponentPropertyBag *bag,
						ICalPropertyKind kind)
{
	guint index;

	g_return_val_if_fail (bag != NULL, ~0);
	g_return_val_if_fail (bag->properties != NULL, ~0);

	for (index = 0; index < bag->properties->len; index++) {
		ICalProperty *prop;

		prop = g_ptr_array_index (bag->properties, index);
		if (prop && i_cal_property_isa (prop) == kind)
			return index;
	}

	return ~0;
}

/**
 * e_cal_component_property_bag_remove:
 * @bag: an #ECalComponentPropertyBag
 * @index: an index of the property to remove
 *
 * Removes the #ICalProperty at the given @index. If the @index is
 * out of bounds (not lower than e_cal_component_property_bag_get_count()),
 * then the function does nothing.
 *
 * Since: 3.34
 **/
void
e_cal_component_property_bag_remove (ECalComponentPropertyBag *bag,
				     guint index)
{
	g_return_if_fail (bag != NULL);
	g_return_if_fail (bag->properties != NULL);

	if (index < bag->properties->len)
		g_ptr_array_remove_index (bag->properties, index);
}

/**
 * e_cal_component_property_bag_remove_by_kind:
 * @bag: an #ECalComponentPropertyBag
 * @kind: an #ICalPropertyKind to remove
 * @all: %TRUE to remove all properties of the @kind, or %FALSE to only the first
 *
 * Removes the first or all (depending on the @all) properties of the given @kind.
 *
 * Returns: how many properties had been removed
 *
 * Since: 3.34
 **/
guint
e_cal_component_property_bag_remove_by_kind (ECalComponentPropertyBag *bag,
					     ICalPropertyKind kind,
					     gboolean all)
{
	guint index, count = 0;

	g_return_val_if_fail (bag != NULL, 0);
	g_return_val_if_fail (bag->properties != NULL, 0);

	index = 0;
	while (index < bag->properties->len) {
		ICalProperty *prop;

		prop = g_ptr_array_index (bag->properties, index);
		if (prop && i_cal_property_isa (prop) == kind) {
			g_ptr_array_remove_index (bag->properties, index);
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
 * e_cal_component_property_bag_clear:
 * @bag: an #ECalComponentPropertyBag
 *
 * Removes all properties from the @bag, thus it doesn't contain any
 * property after this function returns.
 *
 * Since: 3.34
 **/
void
e_cal_component_property_bag_clear (ECalComponentPropertyBag *bag)
{
	g_return_if_fail (bag != NULL);
	g_return_if_fail (bag->properties != NULL);

	g_ptr_array_set_size (bag->properties, 0);
}

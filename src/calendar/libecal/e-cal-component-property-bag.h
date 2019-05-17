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

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_COMPONENT_PROPERTY_BAG_H
#define E_CAL_COMPONENT_PROPERTY_BAG_H

#include <glib-object.h>
#include <libical-glib/libical-glib.h>

G_BEGIN_DECLS

/**
 * ECalComponentPropertyBag:
 *
 * Opaque structure, which represents a bad (list) of #ICalProperty objects.
 * Use the functions below to work with it.
 **/
typedef struct _ECalComponentPropertyBag ECalComponentPropertyBag;

/**
 * ECalComponentPropertyBagFilterFunc:
 * @property: an #ICalProperty
 * @user_data: user data for the callback
 *
 * A function used to filter which properties should be added to the bag,
 * when filling it with e_cal_component_property_bag_new_from_component()
 * and e_cal_component_property_bag_set_from_component().
 *
 * Returns: %TRUE, to add the property to the bag; %FALSE, to not add it to the bag
 *
 * Since: 3.34
 **/
typedef gboolean (* ECalComponentPropertyBagFilterFunc)
						(ICalProperty *property,
						 gpointer user_data);

GType		e_cal_component_property_bag_get_type
						(void);
ECalComponentPropertyBag *
		e_cal_component_property_bag_new(void);
ECalComponentPropertyBag *
		e_cal_component_property_bag_new_from_component
						(const ICalComponent *component,
						 ECalComponentPropertyBagFilterFunc func,
						 gpointer user_data);
ECalComponentPropertyBag *
		e_cal_component_property_bag_copy
						(const ECalComponentPropertyBag *bag);
void		e_cal_component_property_bag_free
						(gpointer bag); /* ECalComponentPropertyBag * */
void		e_cal_component_property_bag_set_from_component
						(ECalComponentPropertyBag *bag,
						 const ICalComponent *component,
						 ECalComponentPropertyBagFilterFunc func,
						 gpointer user_data);
void		e_cal_component_property_bag_fill_component
						(const ECalComponentPropertyBag *bag,
						 ICalComponent *component);
void		e_cal_component_property_bag_assign
						(ECalComponentPropertyBag *bag,
						 const ECalComponentPropertyBag *src_bag);
void		e_cal_component_property_bag_add(ECalComponentPropertyBag *bag,
						 const ICalProperty *prop);
void		e_cal_component_property_bag_take
						(ECalComponentPropertyBag *bag,
						 ICalProperty *prop);
guint		e_cal_component_property_bag_get_count
						(const ECalComponentPropertyBag *bag);
ICalProperty *	e_cal_component_property_bag_get(const ECalComponentPropertyBag *bag,
						 guint index);
guint		e_cal_component_property_bag_get_first_by_kind
						(const ECalComponentPropertyBag *bag,
						 ICalPropertyKind kind);
void		e_cal_component_property_bag_remove
						(ECalComponentPropertyBag *bag,
						 guint index);
guint		e_cal_component_property_bag_remove_by_kind
						(ECalComponentPropertyBag *bag,
						 ICalPropertyKind kind,
						 gboolean all);
void		e_cal_component_property_bag_clear
						(ECalComponentPropertyBag *bag);

G_END_DECLS

#endif /* E_CAL_COMPONENT_PROPERTY_BAG_H */

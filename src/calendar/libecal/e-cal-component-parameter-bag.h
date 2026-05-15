/*
 * SPDX-FileCopyrightText: (C) 2019 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_COMPONENT_PARAMETER_BAG_H
#define E_CAL_COMPONENT_PARAMETER_BAG_H

#include <glib-object.h>
#include <libical-glib/libical-glib.h>

G_BEGIN_DECLS

/**
 * ECalComponentParameterBag:
 *
 * Opaque structure, which represents a bad (list) of #ICalParameter objects.
 * Use the functions below to work with it.
 **/
typedef struct _ECalComponentParameterBag ECalComponentParameterBag;

/**
 * ECalComponentParameterBagFilterFunc:
 * @parameter: an #ICalParameter
 * @user_data: user data for the callback
 *
 * A function used to filter which parameters should be added to the bag,
 * when filling it with e_cal_component_parameter_bag_new_from_property()
 * and e_cal_component_parameter_bag_set_from_property().
 *
 * Returns: %TRUE, to add the parameter to the bag; %FALSE, to not add it to the bag
 *
 * Since: 3.34
 **/
typedef gboolean (* ECalComponentParameterBagFilterFunc)
						(ICalParameter *parameter,
						 gpointer user_data);

GType		e_cal_component_parameter_bag_get_type
						(void);
ECalComponentParameterBag *
		e_cal_component_parameter_bag_new
						(void);
ECalComponentParameterBag *
		e_cal_component_parameter_bag_new_from_property
						(const ICalProperty *property,
						 ECalComponentParameterBagFilterFunc func,
						 gpointer user_data);
ECalComponentParameterBag *
		e_cal_component_parameter_bag_copy
						(const ECalComponentParameterBag *bag);
void		e_cal_component_parameter_bag_free
						(gpointer bag); /* ECalComponentParameterBag * */
void		e_cal_component_parameter_bag_set_from_property
						(ECalComponentParameterBag *bag,
						 const ICalProperty *property,
						 ECalComponentParameterBagFilterFunc func,
						 gpointer user_data);
void		e_cal_component_parameter_bag_fill_property
						(const ECalComponentParameterBag *bag,
						 ICalProperty *property);
void		e_cal_component_parameter_bag_assign
						(ECalComponentParameterBag *bag,
						 const ECalComponentParameterBag *src_bag);
void		e_cal_component_parameter_bag_add(ECalComponentParameterBag *bag,
						 const ICalParameter *param);
void		e_cal_component_parameter_bag_take
						(ECalComponentParameterBag *bag,
						 ICalParameter *param);
guint		e_cal_component_parameter_bag_get_count
						(const ECalComponentParameterBag *bag);
ICalParameter *	e_cal_component_parameter_bag_get(const ECalComponentParameterBag *bag,
						 guint index);
guint		e_cal_component_parameter_bag_get_first_by_kind
						(const ECalComponentParameterBag *bag,
						 ICalParameterKind kind);
void		e_cal_component_parameter_bag_remove
						(ECalComponentParameterBag *bag,
						 guint index);
guint		e_cal_component_parameter_bag_remove_by_kind
						(ECalComponentParameterBag *bag,
						 ICalParameterKind kind,
						 gboolean all);
void		e_cal_component_parameter_bag_clear
						(ECalComponentParameterBag *bag);

G_END_DECLS

#endif /* E_CAL_COMPONENT_PARAMETER_BAG_H */

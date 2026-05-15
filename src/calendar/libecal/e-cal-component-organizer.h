/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileCopyrightText: (C) 2019 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_COMPONENT_ORGANIZER_H
#define E_CAL_COMPONENT_ORGANIZER_H

#include <glib-object.h>
#include <libical-glib/libical-glib.h>

#include <libecal/e-cal-component-parameter-bag.h>

G_BEGIN_DECLS

/**
 * ECalComponentOrganizer:
 *
 * Describes an organizer. Use the functions below to work with it.
 **/
typedef struct _ECalComponentOrganizer ECalComponentOrganizer;

GType		e_cal_component_organizer_get_type
						(void);
ECalComponentOrganizer *
		e_cal_component_organizer_new	(void);
ECalComponentOrganizer *
		e_cal_component_organizer_new_full
						(const gchar *value,
						 const gchar *sentby,
						 const gchar *cn,
						 const gchar *language);
ECalComponentOrganizer *
		e_cal_component_organizer_new_from_property
						(const ICalProperty *property);
ECalComponentOrganizer *
		e_cal_component_organizer_copy	(const ECalComponentOrganizer *organizer);
void		e_cal_component_organizer_free	(gpointer organizer); /* ECalComponentOrganizer * */
void		e_cal_component_organizer_set_from_property
						(ECalComponentOrganizer *organizer,
						 const ICalProperty *property);
ICalProperty *	e_cal_component_organizer_get_as_property
						(const ECalComponentOrganizer *organizer);
void		e_cal_component_organizer_fill_property
						(const ECalComponentOrganizer *organizer,
						 ICalProperty *property);
const gchar *	e_cal_component_organizer_get_value
						(const ECalComponentOrganizer *organizer);
void		e_cal_component_organizer_set_value
						(ECalComponentOrganizer *organizer,
						 const gchar *value);
const gchar *	e_cal_component_organizer_get_sentby
						(const ECalComponentOrganizer *organizer);
void		e_cal_component_organizer_set_sentby
						(ECalComponentOrganizer *organizer,
						 const gchar *sentby);
const gchar *	e_cal_component_organizer_get_cn(const ECalComponentOrganizer *organizer);
void		e_cal_component_organizer_set_cn(ECalComponentOrganizer *organizer,
						 const gchar *cn);
const gchar *	e_cal_component_organizer_get_language
						(const ECalComponentOrganizer *organizer);
void		e_cal_component_organizer_set_language
						(ECalComponentOrganizer *organizer,
						 const gchar *language);
ECalComponentParameterBag *
		e_cal_component_organizer_get_parameter_bag
						(const ECalComponentOrganizer *organizer);

G_END_DECLS

#endif /* E_CAL_COMPONENT_ORGANIZER_H */

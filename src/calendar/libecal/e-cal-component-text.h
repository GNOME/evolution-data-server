/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileCopyrightText: (C) 2019 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_COMPONENT_TEXT_H
#define E_CAL_COMPONENT_TEXT_H

#include <glib-object.h>
#include <libical-glib/libical-glib.h>

G_BEGIN_DECLS

/**
 * ECalComponentText:
 *
 * Contains description string and an alternate representation URI
 * for text properties. Use the functions below to work with it.
 **/
typedef struct _ECalComponentText ECalComponentText;

GType		e_cal_component_text_get_type	(void);
ECalComponentText *
		e_cal_component_text_new	(const gchar *value,
						 const gchar *altrep);
ECalComponentText *
		e_cal_component_text_new_from_property
						(const ICalProperty *property);
ECalComponentText *
		e_cal_component_text_copy	(const ECalComponentText *text);
void		e_cal_component_text_free	(gpointer text); /* ECalComponentText * */
void		e_cal_component_text_set_from_property
						(ECalComponentText *text,
						 const ICalProperty *property);
void		e_cal_component_text_fill_property
						(const ECalComponentText *text,
						 ICalProperty *property);
const gchar *	e_cal_component_text_get_value	(const ECalComponentText *text);
void		e_cal_component_text_set_value	(ECalComponentText *text,
						 const gchar *value);
const gchar *	e_cal_component_text_get_altrep	(const ECalComponentText *text);
void		e_cal_component_text_set_altrep	(ECalComponentText *text,
						 const gchar *altrep);
const gchar *	e_cal_component_text_get_language
						(const ECalComponentText *text);
void		e_cal_component_text_set_language
						(ECalComponentText *text,
						 const gchar *language);

G_END_DECLS

#endif /* E_CAL_COMPONENT_TEXT_H */

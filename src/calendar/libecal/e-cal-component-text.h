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

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_COMPONENT_TEXT_H
#define E_CAL_COMPONENT_TEXT_H

#include <glib-object.h>

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
		e_cal_component_text_copy	(const ECalComponentText *text);
void		e_cal_component_text_free	(gpointer text); /* ECalComponentText * */
const gchar *	e_cal_component_text_get_value	(const ECalComponentText *text);
void		e_cal_component_text_set_value	(ECalComponentText *text,
						 const gchar *value);
const gchar *	e_cal_component_text_get_altrep	(const ECalComponentText *text);
void		e_cal_component_text_set_altrep	(ECalComponentText *text,
						 const gchar *altrep);

G_END_DECLS

#endif /* E_CAL_COMPONENT_TEXT_H */

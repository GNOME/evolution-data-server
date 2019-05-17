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

#ifndef E_CAL_COMPONENT_DATETIME_H
#define E_CAL_COMPONENT_DATETIME_H

#include <glib-object.h>
#include <libical-glib/libical-glib.h>

G_BEGIN_DECLS

/**
 * ECalComponentDateTime:
 *
 * An opaque structure containing an #ICalTime describing
 * the date/time value and also its TZID parameter. Use the functions
 * below to work with it.
 **/
typedef struct _ECalComponentDateTime ECalComponentDateTime;

GType		e_cal_component_datetime_get_type
						(void);
ECalComponentDateTime *
		e_cal_component_datetime_new	(const ICalTime *value,
						 const gchar *tzid);
ECalComponentDateTime *
		e_cal_component_datetime_new_take
						(ICalTime *value,
						 gchar *tzid);
ECalComponentDateTime *
		e_cal_component_datetime_copy	(const ECalComponentDateTime *dt);
void		e_cal_component_datetime_free	(gpointer dt); /* ECalComponentDateTime * */
void		e_cal_component_datetime_set	(ECalComponentDateTime *dt,
						 const ICalTime *value,
						 const gchar *tzid);
ICalTime *	e_cal_component_datetime_get_value
						(const ECalComponentDateTime *dt);
void		e_cal_component_datetime_set_value
						(ECalComponentDateTime *dt,
						 const ICalTime *value);
void		e_cal_component_datetime_take_value
						(ECalComponentDateTime *dt,
						 ICalTime *value);
const gchar *	e_cal_component_datetime_get_tzid
						(const ECalComponentDateTime *dt);
void		e_cal_component_datetime_set_tzid
						(ECalComponentDateTime *dt,
						 const gchar *tzid);
void		e_cal_component_datetime_take_tzid
						(ECalComponentDateTime *dt,
						 gchar *tzid);

G_END_DECLS

#endif /* E_CAL_COMPONENT_DATETIME_H */

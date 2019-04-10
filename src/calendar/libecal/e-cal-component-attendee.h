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

#ifndef E_CAL_COMPONENT_ATTENDEE_H
#define E_CAL_COMPONENT_ATTENDEE_H

#include <glib-object.h>
#include <libical-glib/libical-glib.h>

#include <libecal/e-cal-component-parameter-bag.h>

G_BEGIN_DECLS

/**
 * ECalComponentAttendee:
 *
 * Describes an attendee. Use the functions below to work with it.
 **/
typedef struct _ECalComponentAttendee ECalComponentAttendee;

GType		e_cal_component_attendee_get_type
						(void);
ECalComponentAttendee *
		e_cal_component_attendee_new	(void);
ECalComponentAttendee *
		e_cal_component_attendee_new_full
						(const gchar *value,
						 const gchar *member,
						 ICalParameterCutype cutype,
						 ICalParameterRole role,
						 ICalParameterPartstat partstat,
						 gboolean rsvp,
						 const gchar *delegatedfrom,
						 const gchar *delegatedto,
						 const gchar *sentby,
						 const gchar *cn,
						 const gchar *language);
ECalComponentAttendee *
		e_cal_component_attendee_new_from_property
						(const ICalProperty *property);
ECalComponentAttendee *
		e_cal_component_attendee_copy	(const ECalComponentAttendee *attendee);
void		e_cal_component_attendee_free	(gpointer attendee); /* ECalComponentAttendee * */
void		e_cal_component_attendee_set_from_property
						(ECalComponentAttendee *attendee,
						 const ICalProperty *property);
ICalProperty *	e_cal_component_attendee_get_as_property
						(const ECalComponentAttendee *attendee);
void		e_cal_component_attendee_fill_property
						(const ECalComponentAttendee *attendee,
						 ICalProperty *property);
const gchar *	e_cal_component_attendee_get_value
						(const ECalComponentAttendee *attendee);
void		e_cal_component_attendee_set_value
						(ECalComponentAttendee *attendee,
						 const gchar *value);
const gchar *	e_cal_component_attendee_get_member
						(const ECalComponentAttendee *attendee);
void		e_cal_component_attendee_set_member
						(ECalComponentAttendee *attendee,
						 const gchar *member);
ICalParameterCutype
		e_cal_component_attendee_get_cutype
						(const ECalComponentAttendee *attendee);
void		e_cal_component_attendee_set_cutype
						(ECalComponentAttendee *attendee,
						 ICalParameterCutype cutype);
ICalParameterRole
		e_cal_component_attendee_get_role
						(const ECalComponentAttendee *attendee);
void		e_cal_component_attendee_set_role
						(ECalComponentAttendee *attendee,
						 ICalParameterRole role);
ICalParameterPartstat
		e_cal_component_attendee_get_partstat
						(const ECalComponentAttendee *attendee);
void		e_cal_component_attendee_set_partstat
						(ECalComponentAttendee *attendee,
						 ICalParameterPartstat partstat);
gboolean	e_cal_component_attendee_get_rsvp
						(const ECalComponentAttendee *attendee);
void		e_cal_component_attendee_set_rsvp
						(ECalComponentAttendee *attendee,
						 gboolean rsvp);
const gchar *	e_cal_component_attendee_get_delegatedfrom
						(const ECalComponentAttendee *attendee);
void		e_cal_component_attendee_set_delegatedfrom
						(ECalComponentAttendee *attendee,
						 const gchar *delegatedfrom);
const gchar *	e_cal_component_attendee_get_delegatedto
						(const ECalComponentAttendee *attendee);
void		e_cal_component_attendee_set_delegatedto
						(ECalComponentAttendee *attendee,
						 const gchar *delegatedto);
const gchar *	e_cal_component_attendee_get_sentby
						(const ECalComponentAttendee *attendee);
void		e_cal_component_attendee_set_sentby
						(ECalComponentAttendee *attendee,
						 const gchar *sentby);
const gchar *	e_cal_component_attendee_get_cn	(const ECalComponentAttendee *attendee);
void		e_cal_component_attendee_set_cn	(ECalComponentAttendee *attendee,
						 const gchar *cn);
const gchar *	e_cal_component_attendee_get_language
						(const ECalComponentAttendee *attendee);
void		e_cal_component_attendee_set_language
						(ECalComponentAttendee *attendee,
						 const gchar *language);
ECalComponentParameterBag *
		e_cal_component_attendee_get_parameter_bag
						(const ECalComponentAttendee *attendee);

G_END_DECLS

#endif /* E_CAL_COMPONENT_ATTENDEE_H */

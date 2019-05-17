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

#ifndef E_CAL_COMPONENT_PERIOD_H
#define E_CAL_COMPONENT_PERIOD_H

#include <glib-object.h>
#include <libical-glib/libical-glib.h>
#include <libecal/e-cal-enums.h>

G_BEGIN_DECLS

/**
 * ECalComponentPeriod:
 *
 * Period of time, can have explicit start/end times or start/duration instead.
 * Use the functions below to work with it.
 **/
typedef struct _ECalComponentPeriod ECalComponentPeriod;

GType		e_cal_component_period_get_type	(void);
ECalComponentPeriod *
		e_cal_component_period_new_datetime
						(const ICalTime *start,
						 const ICalTime *end);
ECalComponentPeriod *
		e_cal_component_period_new_duration
						(const ICalTime *start,
						 const ICalDuration *duration);
ECalComponentPeriod *
		e_cal_component_period_copy	(const ECalComponentPeriod *period);
void		e_cal_component_period_free	(gpointer period); /* ECalComponentPeriod * */
ECalComponentPeriodKind
		e_cal_component_period_get_kind	(const ECalComponentPeriod *period);
void		e_cal_component_period_set_datetime_full
						(ECalComponentPeriod *period,
						 const ICalTime *start,
						 const ICalTime *end);
void		e_cal_component_period_set_duration_full
						(ECalComponentPeriod *period,
						 const ICalTime *start,
						 const ICalDuration *duration);
ICalTime *	e_cal_component_period_get_start(const ECalComponentPeriod *period);
void		e_cal_component_period_set_start(ECalComponentPeriod *period,
						 const ICalTime *start);
ICalTime *	e_cal_component_period_get_end	(const ECalComponentPeriod *period);
void		e_cal_component_period_set_end	(ECalComponentPeriod *period,
						 const ICalTime *end);
ICalDuration *	e_cal_component_period_get_duration
						(const ECalComponentPeriod *period);
void		e_cal_component_period_set_duration
						(ECalComponentPeriod *period,
						 const ICalDuration *duration);

G_END_DECLS

#endif /* E_CAL_COMPONENT_PERIOD_H */

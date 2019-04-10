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

#ifndef E_CAL_COMPONENT_ALARM_REPEAT_H
#define E_CAL_COMPONENT_ALARM_REPEAT_H

#include <glib-object.h>
#include <libical-glib/libical-glib.h>

G_BEGIN_DECLS

/**
 * ECalComponentAlarmRepeat:
 *
 * A structure holding whether and how an alarm repeats.
 * Use the functions below to work with it.
 **/
typedef struct _ECalComponentAlarmRepeat ECalComponentAlarmRepeat;

GType		e_cal_component_alarm_repeat_get_type
						(void);
ECalComponentAlarmRepeat *
		e_cal_component_alarm_repeat_new(gint repetitions,
						 const ICalDuration *interval);
ECalComponentAlarmRepeat *
		e_cal_component_alarm_repeat_new_seconds
						(gint repetitions,
						 gint interval_seconds);
ECalComponentAlarmRepeat *
		e_cal_component_alarm_repeat_copy
						(const ECalComponentAlarmRepeat *repeat);
void		e_cal_component_alarm_repeat_free
						(gpointer repeat); /* ECalComponentAlarmRepeat * */
gint		e_cal_component_alarm_repeat_get_repetitions
						(const ECalComponentAlarmRepeat *repeat);
void		e_cal_component_alarm_repeat_set_repetitions
						(ECalComponentAlarmRepeat *repeat,
						 gint repetitions);
ICalDuration *	e_cal_component_alarm_repeat_get_interval
						(const ECalComponentAlarmRepeat *repeat);
void		e_cal_component_alarm_repeat_set_interval
						(ECalComponentAlarmRepeat *repeat,
						 const ICalDuration *interval);
gint		e_cal_component_alarm_repeat_get_interval_seconds
						(const ECalComponentAlarmRepeat *repeat);
void		e_cal_component_alarm_repeat_set_interval_seconds
						(ECalComponentAlarmRepeat *repeat,
						 gint interval_seconds);

G_END_DECLS

#endif /* E_CAL_COMPONENT_ALARM_REPEAT_H */

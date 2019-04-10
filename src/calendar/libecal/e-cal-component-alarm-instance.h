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

#ifndef E_CAL_COMPONENT_ALARM_INSTANCE_H
#define E_CAL_COMPONENT_ALARM_INSTANCE_H

#include <glib-object.h>
#include <time.h>

G_BEGIN_DECLS

/**
 * ECalComponentAlarmInstance:
 *
 * Opaque structure, which represents an alarm occurrence, i.e. a instance instance.
 * Use the functions below to work with it.
 **/
typedef struct _ECalComponentAlarmInstance ECalComponentAlarmInstance;

GType		e_cal_component_alarm_instance_get_type
						(void);
ECalComponentAlarmInstance *
		e_cal_component_alarm_instance_new
						(const gchar *uid,
						 time_t instance_time,
						 time_t occur_start,
						 time_t occur_end);
ECalComponentAlarmInstance *
		e_cal_component_alarm_instance_copy
						(const ECalComponentAlarmInstance *instance);
void		e_cal_component_alarm_instance_free
						(gpointer instance); /* ECalComponentAlarmInstance * */
const gchar *	e_cal_component_alarm_instance_get_uid
						(const ECalComponentAlarmInstance *instance);
void		e_cal_component_alarm_instance_set_uid
						(ECalComponentAlarmInstance *instance,
						 const gchar *uid);
time_t		e_cal_component_alarm_instance_get_time
						(const ECalComponentAlarmInstance *instance);
void		e_cal_component_alarm_instance_set_time
						(ECalComponentAlarmInstance *instance,
						 time_t instance_time);
time_t		e_cal_component_alarm_instance_get_occur_start
						(const ECalComponentAlarmInstance *instance);
void		e_cal_component_alarm_instance_set_occur_start
						(ECalComponentAlarmInstance *instance,
						 time_t occur_start);
time_t		e_cal_component_alarm_instance_get_occur_end
						(const ECalComponentAlarmInstance *instance);
void		e_cal_component_alarm_instance_set_occur_end
						(ECalComponentAlarmInstance *instance,
						 time_t occur_end);

G_END_DECLS

#endif /* E_CAL_COMPONENT_ALARM_INSTANCE_H */

/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileCopyrightText: (C) 2019 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_COMPONENT_ALARM_INSTANCE_H
#define E_CAL_COMPONENT_ALARM_INSTANCE_H

#include <glib-object.h>
#include <time.h>

G_BEGIN_DECLS

/* Forward declaration */
struct _ECalComponent;

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
const gchar *	e_cal_component_alarm_instance_get_rid
						(const ECalComponentAlarmInstance *instance);
void		e_cal_component_alarm_instance_set_rid
						(ECalComponentAlarmInstance *instance,
						 const gchar *rid);
struct _ECalComponent *
		e_cal_component_alarm_instance_get_component
						(const ECalComponentAlarmInstance *instance);
void		e_cal_component_alarm_instance_set_component
						(ECalComponentAlarmInstance *instance,
						 struct _ECalComponent *component);
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

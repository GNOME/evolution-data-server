/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileCopyrightText: (C) 2019 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_COMPONENT_ALARM_TRIGGER_H
#define E_CAL_COMPONENT_ALARM_TRIGGER_H

#include <glib-object.h>
#include <libical-glib/libical-glib.h>

#include <libecal/e-cal-component-parameter-bag.h>
#include <libecal/e-cal-enums.h>

G_BEGIN_DECLS

/**
 * ECalComponentAlarmTrigger:
 *
 * Opaque structure, which represents when an alarm is supposed to be triggered.
 * Use the functions below to work with it.
 **/
typedef struct _ECalComponentAlarmTrigger ECalComponentAlarmTrigger;

GType		e_cal_component_alarm_trigger_get_type
						(void);
ECalComponentAlarmTrigger *
		e_cal_component_alarm_trigger_new_relative
						(ECalComponentAlarmTriggerKind kind,
						 const ICalDuration *duration);
ECalComponentAlarmTrigger *
		e_cal_component_alarm_trigger_new_absolute
						(const ICalTime *absolute_time);
ECalComponentAlarmTrigger *
		e_cal_component_alarm_trigger_new_from_property
						(const ICalProperty *property);
ECalComponentAlarmTrigger *
		e_cal_component_alarm_trigger_copy
						(const ECalComponentAlarmTrigger *trigger);
void		e_cal_component_alarm_trigger_free
						(gpointer trigger); /* ECalComponentAlarmTrigger * */
void		e_cal_component_alarm_trigger_set_from_property
						(ECalComponentAlarmTrigger *trigger,
						 const ICalProperty *property);
ICalProperty *	e_cal_component_alarm_trigger_get_as_property
						(const ECalComponentAlarmTrigger *trigger);
void		e_cal_component_alarm_trigger_fill_property
						(const ECalComponentAlarmTrigger *trigger,
						 ICalProperty *property);
void		e_cal_component_alarm_trigger_set_relative
						(ECalComponentAlarmTrigger *trigger,
						 ECalComponentAlarmTriggerKind kind,
						 const ICalDuration *duration);
void		e_cal_component_alarm_trigger_set_absolute
						(ECalComponentAlarmTrigger *trigger,
						 const ICalTime *absolute_time);
ECalComponentAlarmTriggerKind
		e_cal_component_alarm_trigger_get_kind
						(const ECalComponentAlarmTrigger *trigger);
void		e_cal_component_alarm_trigger_set_kind
						(ECalComponentAlarmTrigger *trigger,
						 ECalComponentAlarmTriggerKind kind);
ICalDuration *	e_cal_component_alarm_trigger_get_duration
						(const ECalComponentAlarmTrigger *trigger);
void		e_cal_component_alarm_trigger_set_duration
						(ECalComponentAlarmTrigger *trigger,
						 const ICalDuration *duration);
ICalTime *	e_cal_component_alarm_trigger_get_absolute_time
						(const ECalComponentAlarmTrigger *trigger);
void		e_cal_component_alarm_trigger_set_absolute_time
						(ECalComponentAlarmTrigger *trigger,
						 const ICalTime *absolute_time);
ECalComponentParameterBag *
		e_cal_component_alarm_trigger_get_parameter_bag
						(const ECalComponentAlarmTrigger *trigger);

G_END_DECLS

#endif /* E_CAL_COMPONENT_ALARM_TRIGGER_H */

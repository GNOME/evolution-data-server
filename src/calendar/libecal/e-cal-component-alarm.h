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

#ifndef E_CAL_COMPONENT_ALARM_H
#define E_CAL_COMPONENT_ALARM_H

#include <glib-object.h>
#include <libical-glib/libical-glib.h>

#include <libecal/e-cal-component-alarm-repeat.h>
#include <libecal/e-cal-component-alarm-trigger.h>
#include <libecal/e-cal-component-attendee.h>
#include <libecal/e-cal-component-property-bag.h>
#include <libecal/e-cal-component-text.h>
#include <libecal/e-cal-enums.h>

/**
 * E_CAL_EVOLUTION_ALARM_UID_PROPERTY:
 *
 * Extension property for alarm components so that we can reference them by UID.
 *
 * Since: 3.34
 **/
#define E_CAL_EVOLUTION_ALARM_UID_PROPERTY "X-EVOLUTION-ALARM-UID"

G_BEGIN_DECLS

/**
 * ECalComponentAlarm:
 *
 * Opaque structure, which represents alarm subcomponents.
 * Use the functions below to work with it.
 **/
typedef struct _ECalComponentAlarm ECalComponentAlarm;

GType		e_cal_component_alarm_get_type	(void);
ECalComponentAlarm *
		e_cal_component_alarm_new	(void);
ECalComponentAlarm *
		e_cal_component_alarm_new_from_component
						(const ICalComponent *component);
ECalComponentAlarm *
		e_cal_component_alarm_copy	(const ECalComponentAlarm *alarm);
void		e_cal_component_alarm_free	(gpointer alarm); /* ECalComponentAlarm * */
void		e_cal_component_alarm_set_from_component
						(ECalComponentAlarm *alarm,
						 const ICalComponent *component);
ICalComponent *	e_cal_component_alarm_get_as_component
						(ECalComponentAlarm *alarm);
void		e_cal_component_alarm_fill_component
						(ECalComponentAlarm *alarm,
						 ICalComponent *component);
const gchar *	e_cal_component_alarm_get_uid	(const ECalComponentAlarm *alarm);
void		e_cal_component_alarm_set_uid	(ECalComponentAlarm *alarm,
						 const gchar *uid);
ECalComponentAlarmAction
		e_cal_component_alarm_get_action(const ECalComponentAlarm *alarm);
void		e_cal_component_alarm_set_action(ECalComponentAlarm *alarm,
						 ECalComponentAlarmAction action);
ECalComponentText *
		e_cal_component_alarm_get_summary
						(const ECalComponentAlarm *alarm);
void		e_cal_component_alarm_set_summary
						(ECalComponentAlarm *alarm,
						 const ECalComponentText *summary);
void		e_cal_component_alarm_take_summary
						(ECalComponentAlarm *alarm,
						 ECalComponentText *summary);
ECalComponentText *
		e_cal_component_alarm_get_description
						(const ECalComponentAlarm *alarm);
void		e_cal_component_alarm_set_description
						(ECalComponentAlarm *alarm,
						 const ECalComponentText *description);
void		e_cal_component_alarm_take_description
						(ECalComponentAlarm *alarm,
						 ECalComponentText *description);
ECalComponentAlarmRepeat *
		e_cal_component_alarm_get_repeat(const ECalComponentAlarm *alarm);
void		e_cal_component_alarm_set_repeat(ECalComponentAlarm *alarm,
						 const ECalComponentAlarmRepeat *repeat);
void		e_cal_component_alarm_take_repeat
						(ECalComponentAlarm *alarm,
						 ECalComponentAlarmRepeat *repeat);
ECalComponentAlarmTrigger *
		e_cal_component_alarm_get_trigger
						(const ECalComponentAlarm *alarm);
void		e_cal_component_alarm_set_trigger
						(ECalComponentAlarm *alarm,
						 const ECalComponentAlarmTrigger *trigger);
void		e_cal_component_alarm_take_trigger
						(ECalComponentAlarm *alarm,
						 ECalComponentAlarmTrigger *trigger);
gboolean	e_cal_component_alarm_has_attendees
						(const ECalComponentAlarm *alarm);
GSList *	e_cal_component_alarm_get_attendees /* ECalComponentAttendee * */
						(const ECalComponentAlarm *alarm);
void		e_cal_component_alarm_set_attendees
						(ECalComponentAlarm *alarm,
						 const GSList *attendees); /* ECalComponentAttendee * */
void		e_cal_component_alarm_take_attendees
						(ECalComponentAlarm *alarm,
						 GSList *attendees); /* ECalComponentAttendee * */
gboolean	e_cal_component_alarm_has_attachments
						(const ECalComponentAlarm *alarm);
GSList *	e_cal_component_alarm_get_attachments /* ICalAttach * */
						(const ECalComponentAlarm *alarm);
void		e_cal_component_alarm_set_attachments
						(ECalComponentAlarm *alarm,
						 const GSList *attachments); /* ICalAttach * */
void		e_cal_component_alarm_take_attachments
						(ECalComponentAlarm *alarm,
						 GSList *attachments); /* ICalAttach * */
ECalComponentPropertyBag *
		e_cal_component_alarm_get_property_bag
						(const ECalComponentAlarm *alarm);

G_END_DECLS

#endif /* E_CAL_COMPONENT_ALARM_H */

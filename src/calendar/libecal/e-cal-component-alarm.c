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

#include "evolution-data-server-config.h"

/**
 * SECTION:e-cal-component-alarm
 * @short_description: An ECalComponentAlarm structure
 * @include: libecal/libecal.h
 *
 * Contains functions to work with the #ECalComponentAlarm structure.
 **/

#include <libedataserver/libedataserver.h>

#include "e-cal-component-alarm-repeat.h"
#include "e-cal-component-alarm-trigger.h"
#include "e-cal-component-attendee.h"
#include "e-cal-component-text.h"
#include "e-cal-enums.h"

#include "e-cal-component-alarm.h"

G_DEFINE_BOXED_TYPE (ECalComponentAlarm, e_cal_component_alarm, e_cal_component_alarm_copy, e_cal_component_alarm_free)

struct _ECalComponentAlarm {
	gchar *uid;
	ECalComponentAlarmAction action;
	ECalComponentText *description;
	ECalComponentAlarmRepeat *repeat;
	ECalComponentAlarmTrigger *trigger;
	GSList *attendees; /* ECalComponentAttendee * */
	GSList *attachments; /* ICalAttach * */
};

/**
 * e_cal_component_alarm_new:
 *
 * Creates a new empty #ECalComponentAlarm structure. Free it
 * with e_cal_component_alarm_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentAlarm
 *
 * Since: 3.36
 **/
ECalComponentAlarm *
e_cal_component_alarm_new (void)
{
	ECalComponentAlarm *alarm;

	alarm = g_new0 (ECalComponentAlarm, 1);
	alarm->uid = e_util_generate_uid ();
	alarm->action = E_CAL_COMPONENT_ALARM_UNKNOWN;

	return alarm;
}

/**
 * e_cal_component_alarm_new_from_component:
 * @component: an #ICalComponent of kind %I_CAL_VALARM_COMPONENT
 *
 * Creates a new #ECalComponentAlarm, filled with values from @component,
 * which should be of kind %I_CAL_VALARM_COMPONENT. The function returns
 * %NULL when it is not of the expected kind. Free the structure
 * with e_cal_component_alarm_free(), when no longer needed.
 *
 * Returns: (transfer full) (nullable): a newly allocated #ECalComponentAlarm
 *
 * Since: 3.36
 **/
ECalComponentAlarm *
e_cal_component_alarm_new_from_component (const ICalComponent *component)
{
	ECalComponentAlarm *alarm;

	g_return_val_if_fail (I_CAL_IS_COMPONENT (component), NULL);

	if (i_cal_component_isa ((ICalComponent *) component) != I_CAL_VALARM_COMPONENT)
		return NULL;

	alarm = e_cal_component_alarm_new ();

	e_cal_component_alarm_set_from_component (alarm, component);

	return alarm;
}

/**
 * e_cal_component_alarm_copy:
 * @alarm: (not nullable): an #ECalComponentAlarm
 *
 * Returns a newly allocated copy of @alarm, which should be freed with
 * e_cal_component_alarm_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated copy of @alarm
 *
 * Since: 3.36
 **/
ECalComponentAlarm *
e_cal_component_alarm_copy (const ECalComponentAlarm *alarm)
{
	ECalComponentAlarm *alrm;

	g_return_val_if_fail (alarm != NULL, NULL);

	alrm = e_cal_component_alarm_new ();

	g_free (alrm->uid);
	alrm->uid = g_strdup (alarm->uid);
	alrm->action = alarm->action;

	if (alarm->description)
		alrm->description = e_cal_component_text_copy (alarm->description);

	if (alarm->repeat)
		alrm->repeat = e_cal_component_alarm_repeat_copy (alarm->repeat);

	if (alarm->trigger)
		alrm->trigger = e_cal_component_alarm_trigger_copy (alarm->trigger);

	if (alarm->attendees) {
		GSList *link;

		for (link = alarm->attendees; link; link = g_slist_next (link)) {
			ECalComponentAttendee *attendee = link->data;

			if (!attendee)
				continue;

			alrm->attendees = g_slist_prepend (alrm->attendees, e_cal_component_attendee_copy (attendee));
		}

		alrm->attendees = g_slist_reverse (alrm->attendees);
	}

	if (alarm->attachments) {
		GSList *link;

		for (link = alarm->attachments; link; link = g_slist_next (link)) {
			ICalAttach *src_attach = link->data, *attach = NULL;

			if (!src_attach)
				continue;

			if (i_cal_attach_get_is_url (src_attach)) {
				const gchar *url;

				url = i_cal_attach_get_url (src_attach);
				if (url)
					attach = i_cal_attach_new_from_url (url);
			} else {
				const guchar *data;

				data = i_cal_attach_get_data (src_attach);
				if (data)
					attach = i_cal_attach_new_from_data (data, NULL, NULL);
			}

			if (attach)
				alrm->attachments = g_slist_prepend (alrm->attachments, attach);
		}

		alrm->attachments = g_slist_reverse (alrm->attachments);
	}

	return alrm;
}

/**
 * e_cal_component_alarm_free: (skip)
 * @alarm: (type ECalComponentAlarm) (nullable): an #ECalComponentAlarm to free
 *
 * Free @alarm, previously created by e_cal_component_alarm_new(),
 * e_cal_component_alarm_new_from_component()
 * or e_cal_component_alarm_copy(). The function does nothing, if @alarm
 * is %NULL.
 *
 * Since: 3.36
 **/
void
e_cal_component_alarm_free (gpointer alarm)
{
	ECalComponentAlarm *alrm = alarm;

	if (alrm) {
		g_free (alrm->uid);
		e_cal_component_text_free (alrm->description);
		e_cal_component_alarm_repeat_free (alrm->repeat);
		e_cal_component_alarm_trigger_free (alrm->trigger);
		g_slist_free_full (alrm->attendees, e_cal_component_attendee_free);
		g_slist_free_full (alrm->attachments, g_object_unref);
		g_free (alrm);
	}
}

/**
 * e_cal_component_alarm_set_from_component:
 * @alarm: an #ECalComponentAlarm
 * @component: an #ICalComponent
 *
 * Fill the @alarm structure with the information from
 * the @component, which should be of %I_CAL_VALARM_COMPONENT kind.
 *
 * Since: 3.36
 **/
void
e_cal_component_alarm_set_from_component (ECalComponentAlarm *alarm,
					  const ICalComponent *component)
{
	ICalComponent *comp = (ICalComponent *) component;
	ICalDurationType *duration = NULL;
	ICalProperty *prop, *repeat = NULL;

	g_return_if_fail (alarm != NULL);
	g_return_if_fail (I_CAL_IS_COMPONENT (component));
	g_return_if_fail (i_cal_component_isa ((ICalComponent *) component) == I_CAL_VALARM_COMPONENT);

	g_free (alarm->uid);
	e_cal_component_text_free (alarm->description);
	e_cal_component_alarm_repeat_free (alarm->repeat);
	e_cal_component_alarm_trigger_free (alarm->trigger);
	g_slist_free_full (alarm->attendees, e_cal_component_attendee_free);
	g_slist_free_full (alarm->attachments, g_object_unref);

	alarm->uid = NULL;
	alarm->action = E_CAL_COMPONENT_ALARM_NONE;
	alarm->description = NULL;
	alarm->repeat = NULL;
	alarm->trigger = NULL;
	alarm->attendees = NULL;
	alarm->attachments = NULL;

	for (prop = i_cal_component_get_first_property (comp, I_CAL_ANY_PROPERTY);
	     prop;
	     prop = i_cal_component_get_next_property (comp, I_CAL_ANY_PROPERTY)) {
		ECalComponentAttendee *attendee;
		ECalComponentText *text;
		ICalAttach *attach;
		const gchar *xname;

		switch (i_cal_property_isa (prop)) {
		case I_CAL_ACTION_PROPERTY:
			switch (i_cal_property_get_action (prop)) {
			case I_CAL_ACTION_AUDIO:
				alarm->action = E_CAL_COMPONENT_ALARM_AUDIO;
				break;

			case I_CAL_ACTION_DISPLAY:
				alarm->action = E_CAL_COMPONENT_ALARM_DISPLAY;
				break;

			case I_CAL_ACTION_EMAIL:
				alarm->action = E_CAL_COMPONENT_ALARM_EMAIL;
				break;

			case I_CAL_ACTION_PROCEDURE:
				alarm->action = E_CAL_COMPONENT_ALARM_PROCEDURE;
				break;

			case I_CAL_ACTION_NONE:
				alarm->action = E_CAL_COMPONENT_ALARM_NONE;
				break;

			default:
				alarm->action = E_CAL_COMPONENT_ALARM_UNKNOWN;
				break;
			}
			break;

		case I_CAL_ATTACH_PROPERTY:
			attach = i_cal_property_get_attach (prop);
			if (attach)
				alarm->attachments = g_slist_prepend (alarm->attachments, attach);
			break;

		case I_CAL_DESCRIPTION_PROPERTY:
			ICalParameter *param;

			if (i_cal_property_get_description (prop)) {
				param = i_cal_property_get_first_parameter (prop, I_CAL_ALTREP_PARAMETER);
				alarm->description = e_cal_component_text_new (i_cal_property_get_description (prop),
					param ? i_cal_property_get_altrep (param) : NULL);
			}
			break;

		case I_CAL_DURATION_PROPERTY:
			g_clear_object (&duration);
			duration = i_cal_property_get_duration (prop);
			break;

		case I_CAL_REPEAT_PROPERTY:
			g_clear_object (&repeat);
			repeat = g_object_ref (prop);
			break;

		case I_CAL_TRIGGER_PROPERTY:
			alarm->trigger = e_cal_component_alarm_trigger_new_from_property (prop);
			break;

		case I_CAL_ATTENDEE_PROPERTY:
			attendee = e_cal_component_attendee_new_from_property (prop);
			if (attendee)
				alarm->attendees = g_slist_prepend (alarm->attendees, attendee);
			break;

		case I_CAL_X_PROPERTY:
			xname = i_cal_property_get_x_name (prop);
			if (g_strcmp0 (xname, E_CAL_EVOLUTION_ALARM_UID_PROPERTY) == 0) {
				g_free (alarm->uid);
				alarm->uid = g_strdup (i_cal_property_get_x (prop));
			}
			break;

		default:
			break;
		}

		g_object_unref (prop);
	}

	alarm->attendees = g_slist_reverse (alarm->attendees);
	alarm->attachments = g_slist_reverse (alarm->attachments);

	if (duration && repeat) {
		alarm->repeat = e_cal_component_alarm_repeat_new (
			i_cal_property_get_repeat (repeat),
			duration);
	}

	g_clear_object (&duration);
	g_clear_object (&repeat);
}

ICalComponent *
e_cal_component_alarm_get_as_component (ECalComponentAlarm *alarm)
{
	xxxx
}

void
e_cal_component_alarm_fill_component (const ECalComponentAlarm *alarm,
				      ICalComponent *component)
{
}

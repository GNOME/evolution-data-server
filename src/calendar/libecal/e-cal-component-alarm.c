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
#include "e-cal-component-property-bag.h"
#include "e-cal-component-text.h"
#include "e-cal-enums.h"

#include "e-cal-component-alarm.h"

G_DEFINE_BOXED_TYPE (ECalComponentAlarm, e_cal_component_alarm, e_cal_component_alarm_copy, e_cal_component_alarm_free)

struct _ECalComponentAlarm {
	gchar *uid;
	ECalComponentAlarmAction action;
	ECalComponentText *summary;
	ECalComponentText *description;
	ECalComponentAlarmRepeat *repeat;
	ECalComponentAlarmTrigger *trigger;
	GSList *attendees; /* ECalComponentAttendee * */
	GSList *attachments; /* ICalAttach * */
	ECalComponentPropertyBag *property_bag;
	ICalTime *acknowledged;
};

/**
 * e_cal_component_alarm_new:
 *
 * Creates a new empty #ECalComponentAlarm structure. Free it
 * with e_cal_component_alarm_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentAlarm
 *
 * Since: 3.34
 **/
ECalComponentAlarm *
e_cal_component_alarm_new (void)
{
	ECalComponentAlarm *alarm;

	alarm = g_slice_new0 (ECalComponentAlarm);
	alarm->uid = e_util_generate_uid ();
	alarm->action = E_CAL_COMPONENT_ALARM_UNKNOWN;
	alarm->property_bag = e_cal_component_property_bag_new ();

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
 * Since: 3.34
 **/
ECalComponentAlarm *
e_cal_component_alarm_new_from_component (const ICalComponent *component)
{
	ECalComponentAlarm *alarm;

	g_return_val_if_fail (I_CAL_IS_COMPONENT ((ICalComponent *) component), NULL);

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
 * Since: 3.34
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

	if (alarm->summary)
		alrm->summary = e_cal_component_text_copy (alarm->summary);

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
				const gchar *data;

				data = i_cal_attach_get_data (src_attach);
				if (data)
					attach = i_cal_attach_new_from_data (data, NULL, NULL);
			}

			if (attach)
				alrm->attachments = g_slist_prepend (alrm->attachments, attach);
		}

		alrm->attachments = g_slist_reverse (alrm->attachments);
	}

	if (alarm->acknowledged)
		e_cal_component_alarm_set_acknowledged (alrm, alarm->acknowledged);

	e_cal_component_property_bag_assign (alrm->property_bag, alarm->property_bag);

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
 * Since: 3.34
 **/
void
e_cal_component_alarm_free (gpointer alarm)
{
	ECalComponentAlarm *alrm = alarm;

	if (alrm) {
		g_free (alrm->uid);
		e_cal_component_text_free (alrm->summary);
		e_cal_component_text_free (alrm->description);
		e_cal_component_alarm_repeat_free (alrm->repeat);
		e_cal_component_alarm_trigger_free (alrm->trigger);
		e_cal_component_property_bag_free (alrm->property_bag);
		g_slist_free_full (alrm->attendees, e_cal_component_attendee_free);
		g_slist_free_full (alrm->attachments, g_object_unref);
		g_clear_object (&alrm->acknowledged);
		g_slice_free (ECalComponentAlarm, alrm);
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
 * Since: 3.34
 **/
void
e_cal_component_alarm_set_from_component (ECalComponentAlarm *alarm,
					  const ICalComponent *component)
{
	ICalComponent *comp = (ICalComponent *) component;
	ICalDuration *duration = NULL;
	ICalProperty *prop, *repeat = NULL;

	g_return_if_fail (alarm != NULL);
	g_return_if_fail (I_CAL_IS_COMPONENT ((ICalComponent *) component));
	g_return_if_fail (i_cal_component_isa ((ICalComponent *) component) == I_CAL_VALARM_COMPONENT);

	g_free (alarm->uid);
	e_cal_component_text_free (alarm->summary);
	e_cal_component_text_free (alarm->description);
	e_cal_component_alarm_repeat_free (alarm->repeat);
	e_cal_component_alarm_trigger_free (alarm->trigger);
	g_slist_free_full (alarm->attendees, e_cal_component_attendee_free);
	g_slist_free_full (alarm->attachments, g_object_unref);
	g_clear_object (&alarm->acknowledged);

	alarm->uid = NULL;
	alarm->action = E_CAL_COMPONENT_ALARM_NONE;
	alarm->summary = NULL;
	alarm->description = NULL;
	alarm->repeat = NULL;
	alarm->trigger = NULL;
	alarm->attendees = NULL;
	alarm->attachments = NULL;

	e_cal_component_property_bag_clear (alarm->property_bag);

	for (prop = i_cal_component_get_first_property (comp, I_CAL_ANY_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (comp, I_CAL_ANY_PROPERTY)) {
		ECalComponentAttendee *attendee;
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

		case I_CAL_SUMMARY_PROPERTY:
			if (i_cal_property_get_summary (prop))
				e_cal_component_alarm_take_summary (alarm, e_cal_component_text_new_from_property (prop));
			break;

		case I_CAL_DESCRIPTION_PROPERTY:
			if (i_cal_property_get_description (prop))
				e_cal_component_alarm_take_description (alarm, e_cal_component_text_new_from_property (prop));
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

		case I_CAL_ACKNOWLEDGED_PROPERTY:
			g_clear_object (&alarm->acknowledged);
			alarm->acknowledged = i_cal_property_get_acknowledged (prop);
			break;

		case I_CAL_X_PROPERTY:
			xname = i_cal_property_get_x_name (prop);
			if (g_strcmp0 (xname, E_CAL_EVOLUTION_ALARM_UID_PROPERTY) == 0) {
				g_free (alarm->uid);
				alarm->uid = g_strdup (i_cal_property_get_x (prop));
			} else {
				e_cal_component_property_bag_add (alarm->property_bag, prop);
			}
			break;

		default:
			e_cal_component_property_bag_add (alarm->property_bag, prop);
			break;
		}
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

	/* Ensure mandatory property */
	if (!alarm->uid)
		alarm->uid = e_util_generate_uid ();
}

/**
 * e_cal_component_alarm_get_as_component:
 * @alarm: an #ECalComponentAlarm
 *
 * Creates a VALARM #ICalComponent filled with data from the @alarm.
 * In case the @alarm doesn't have set 'uid', a new is assigned.
 * Free the returned component with g_object_unref(), when no longer
 * needed.
 *
 * Returns: (transfer full): a newly created #ICalComponent
 *    of %I_CAL_VALARM_COMPONENT kind
 *
 * Since: 3.34
 **/
ICalComponent *
e_cal_component_alarm_get_as_component (ECalComponentAlarm *alarm)
{
	ICalComponent *valarm;

	g_return_val_if_fail (alarm != NULL, NULL);

	valarm = i_cal_component_new_valarm ();

	e_cal_component_alarm_fill_component (alarm, valarm);

	return valarm;
}

/**
 * e_cal_component_alarm_fill_component:
 * @alarm: an #ECalComponentAlarm
 * @component: an #ICalComponent of %I_CAL_VALARM_COMPONENT kind
 *
 * Fills @component with data from @alarm. The @component should
 * be of %I_CAL_VALARM_COMPONENT kind - the function does nothing,
 * if it's not. In case the @alarm doesn't have set 'uid', a new
 * is assigned.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_fill_component (ECalComponentAlarm *alarm,
				      ICalComponent *component)
{
	ICalPropertyKind remove_props[] = {
		I_CAL_ACTION_PROPERTY,
		I_CAL_ATTACH_PROPERTY,
		I_CAL_SUMMARY_PROPERTY,
		I_CAL_DESCRIPTION_PROPERTY,
		I_CAL_DURATION_PROPERTY,
		I_CAL_REPEAT_PROPERTY,
		I_CAL_TRIGGER_PROPERTY,
		I_CAL_ATTENDEE_PROPERTY };
	ICalProperty *prop;
	GSList *link;
	gint ii;

	g_return_if_fail (alarm != NULL);
	g_return_if_fail (I_CAL_IS_COMPONENT (component));

	if (i_cal_component_isa (component) != I_CAL_VALARM_COMPONENT)
		return;

	/* Remove used properties first */

	for (ii = 0; ii < G_N_ELEMENTS (remove_props); ii++) {
		if (remove_props[ii] == I_CAL_ACTION_PROPERTY &&
		    alarm->action == E_CAL_COMPONENT_ALARM_UNKNOWN)
			continue;

		while (prop = i_cal_component_get_first_property (component, remove_props[ii]), prop) {
			i_cal_component_remove_property (component, prop);
			g_object_unref (prop);
		}
	}

	if (!alarm->uid)
		alarm->uid = e_util_generate_uid ();

	for (prop = i_cal_component_get_first_property (component, I_CAL_X_PROPERTY);
	     prop;
	     g_object_unref (prop), i_cal_component_get_first_property (component, I_CAL_X_PROPERTY)) {
		const gchar *xname;

		xname = i_cal_property_get_x_name (prop);
		if (g_strcmp0 (xname, E_CAL_EVOLUTION_ALARM_UID_PROPERTY) == 0) {
			i_cal_property_set_x (prop, alarm->uid);
			/* Do not set to NULL, it's used below as a sentinel */
			g_object_unref (prop);
			break;
		}
	}

	/* Tried all existing and none was the E_CAL_EVOLUTION_ALARM_UID_PROPERTY, thus add it */
	if (!prop) {
		prop = i_cal_property_new_x (alarm->uid);
		i_cal_property_set_x_name (prop, E_CAL_EVOLUTION_ALARM_UID_PROPERTY);
		i_cal_component_take_property (component, prop);
	}

	prop = NULL;

	switch (alarm->action) {
	case E_CAL_COMPONENT_ALARM_AUDIO:
		prop = i_cal_property_new_action (I_CAL_ACTION_AUDIO);
		break;

	case E_CAL_COMPONENT_ALARM_DISPLAY:
		prop = i_cal_property_new_action (I_CAL_ACTION_DISPLAY);
		break;

	case E_CAL_COMPONENT_ALARM_EMAIL:
		prop = i_cal_property_new_action (I_CAL_ACTION_EMAIL);
		break;

	case E_CAL_COMPONENT_ALARM_PROCEDURE:
		prop = i_cal_property_new_action (I_CAL_ACTION_PROCEDURE);
		break;

	case E_CAL_COMPONENT_ALARM_NONE:
		prop = i_cal_property_new_action (I_CAL_ACTION_NONE);
		break;

	case E_CAL_COMPONENT_ALARM_UNKNOWN:
		break;
	}

	if (prop)
		i_cal_component_take_property (component, prop);

	if (alarm->summary && e_cal_component_text_get_value (alarm->summary)) {
		prop = i_cal_property_new_summary (e_cal_component_text_get_value (alarm->summary));

		if (prop) {
			e_cal_component_text_fill_property (alarm->summary, prop);
			i_cal_component_take_property (component, prop);
		}
	}

	if (alarm->description && e_cal_component_text_get_value (alarm->description)) {
		prop = i_cal_property_new_description (e_cal_component_text_get_value (alarm->description));

		if (prop) {
			e_cal_component_text_fill_property (alarm->description, prop);
			i_cal_component_take_property (component, prop);
		}
	}

	if (alarm->trigger) {
		prop = e_cal_component_alarm_trigger_get_as_property (alarm->trigger);
		if (prop)
			i_cal_component_take_property (component, prop);
	}

	if (alarm->repeat) {
		ICalDuration *interval;

		interval = e_cal_component_alarm_repeat_get_interval (alarm->repeat);
		if (interval) {
			prop = i_cal_property_new_repeat (e_cal_component_alarm_repeat_get_repetitions (alarm->repeat));
			i_cal_component_take_property (component, prop);

			prop = i_cal_property_new_duration (interval);
			i_cal_component_take_property (component, prop);
		}
	}

	for (link = alarm->attendees; link; link = g_slist_next (link)) {
		ECalComponentAttendee *attendee = link->data;

		if (!attendee)
			continue;

		prop = e_cal_component_attendee_get_as_property (attendee);
		if (prop)
			i_cal_component_take_property (component, prop);
	}

	for (link = alarm->attachments; link; link = g_slist_next (link)) {
		ICalAttach *attach = link->data;

		if (!attach)
			continue;

		prop = i_cal_property_new_attach (attach);
		if (prop)
			i_cal_component_take_property (component, prop);
	}

	if (alarm->acknowledged) {
		prop = i_cal_property_new_acknowledged (alarm->acknowledged);
		i_cal_component_take_property (component, prop);
	}

	e_cal_component_property_bag_fill_component (alarm->property_bag, component);
}

/**
 * e_cal_component_alarm_get_uid:
 * @alarm: an #ECalComponentAlarm
 *
 * Get the @alarm UID.
 *
 * Returns: (nullable): the @alarm UID, or %NULL, when none is set
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_alarm_get_uid (const ECalComponentAlarm *alarm)
{
	g_return_val_if_fail (alarm != NULL, NULL);

	return alarm->uid;
}

/**
 * e_cal_component_alarm_set_uid:
 * @alarm: an #ECalComponentAlarm
 * @uid: (nullable): a UID to set, or %NULL or empty string to generate new
 *
 * Set the @alarm UID, or generates a new UID, if @uid is %NULL or an empty string.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_set_uid (ECalComponentAlarm *alarm,
			       const gchar *uid)
{
	g_return_if_fail (alarm != NULL);

	if (!uid || !*uid) {
		g_free (alarm->uid);
		alarm->uid = e_util_generate_uid ();
	} else if (g_strcmp0 (alarm->uid, uid) != 0) {
		g_free (alarm->uid);
		alarm->uid = g_strdup (uid);
	}
}

/**
 * e_cal_component_alarm_get_action:
 * @alarm: an #ECalComponentAlarm
 *
 * Get the @alarm action, as an #ECalComponentAlarmAction.
 *
 * Returns: the @alarm action, or %E_CAL_COMPONENT_ALARM_NONE, when none is set
 *
 * Since: 3.34
 **/
ECalComponentAlarmAction
e_cal_component_alarm_get_action (const ECalComponentAlarm *alarm)
{
	g_return_val_if_fail (alarm != NULL, E_CAL_COMPONENT_ALARM_NONE);

	return alarm->action;
}

/**
 * e_cal_component_alarm_set_action:
 * @alarm: an #ECalComponentAlarm
 * @action: an #ECalComponentAlarmAction
 *
 * Set the @alarm action, as an #ECalComponentAlarmAction.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_set_action (ECalComponentAlarm *alarm,
				  ECalComponentAlarmAction action)
{
	g_return_if_fail (alarm != NULL);

	alarm->action = action;
}

/**
 * e_cal_component_alarm_get_summary:
 * @alarm: an #ECalComponentAlarm
 *
 * Get the @alarm summary, as an #ECalComponentText.
 *
 * Returns: (transfer none) (nullable): the @alarm summary, or %NULL, when none is set
 *
 * Since: 3.34
 **/
ECalComponentText *
e_cal_component_alarm_get_summary (const ECalComponentAlarm *alarm)
{
	g_return_val_if_fail (alarm != NULL, NULL);

	return alarm->summary;
}

/**
 * e_cal_component_alarm_set_summary:
 * @alarm: an #ECalComponentAlarm
 * @summary: (transfer none) (nullable): a summary to set, or %NULL to unset
 *
 * Set the @alarm summary, as an #ECalComponentText.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_set_summary (ECalComponentAlarm *alarm,
				   const ECalComponentText *summary)
{
	g_return_if_fail (alarm != NULL);

	if (summary != alarm->summary) {
		e_cal_component_text_free (alarm->summary);

		alarm->summary = summary ? e_cal_component_text_copy (summary) : NULL;
	}
}

/**
 * e_cal_component_alarm_take_summary: (skip)
 * @alarm: an #ECalComponentAlarm
 * @summary: (transfer full) (nullable): a summary to set, or %NULL to unset
 *
 * Set the @alarm summary, as an #ECalComponentText, and assumes
 * ownership of the @summary.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_take_summary (ECalComponentAlarm *alarm,
				    ECalComponentText *summary)
{
	g_return_if_fail (alarm != NULL);

	if (summary != alarm->summary) {
		e_cal_component_text_free (alarm->summary);
		alarm->summary = summary;
	}
}

/**
 * e_cal_component_alarm_get_description:
 * @alarm: an #ECalComponentAlarm
 *
 * Get the @alarm description, as an #ECalComponentText.
 *
 * Returns: (transfer none) (nullable): the @alarm description, or %NULL, when none is set
 *
 * Since: 3.34
 **/
ECalComponentText *
e_cal_component_alarm_get_description (const ECalComponentAlarm *alarm)
{
	g_return_val_if_fail (alarm != NULL, NULL);

	return alarm->description;
}

/**
 * e_cal_component_alarm_set_description:
 * @alarm: an #ECalComponentAlarm
 * @description: (transfer none) (nullable): a description to set, or %NULL to unset
 *
 * Set the @alarm description, as an #ECalComponentText.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_set_description (ECalComponentAlarm *alarm,
				       const ECalComponentText *description)
{
	g_return_if_fail (alarm != NULL);

	if (description != alarm->description) {
		e_cal_component_text_free (alarm->description);

		alarm->description = description ? e_cal_component_text_copy (description) : NULL;
	}
}

/**
 * e_cal_component_alarm_take_description: (skip)
 * @alarm: an #ECalComponentAlarm
 * @description: (transfer full) (nullable): a description to set, or %NULL to unset
 *
 * Set the @alarm description, as an #ECalComponentText, and assumes
 * ownership of the @description.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_take_description (ECalComponentAlarm *alarm,
					ECalComponentText *description)
{
	g_return_if_fail (alarm != NULL);

	if (description != alarm->description) {
		e_cal_component_text_free (alarm->description);
		alarm->description = description;
	}
}

/**
 * e_cal_component_alarm_get_repeat:
 * @alarm: an #ECalComponentAlarm
 *
 * Get the @alarm repeat information, as an ECalComponentAlarmRepeat.
 *
 * Returns: (transfer none) (nullable): the @alarm repeat information,
 *    or %NULL, when none is set
 *
 * Since: 3.34
 **/
ECalComponentAlarmRepeat *
e_cal_component_alarm_get_repeat (const ECalComponentAlarm *alarm)
{
	g_return_val_if_fail (alarm != NULL, NULL);

	return alarm->repeat;
}

/**
 * e_cal_component_alarm_set_repeat:
 * @alarm: an #ECalComponentAlarm
 * @repeat: (transfer none) (nullable): a repeat information to set, or %NULL to unset
 *
 * Set the @alarm repeat information, as an #ECalComponentAlarmRepeat.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_set_repeat (ECalComponentAlarm *alarm,
				  const ECalComponentAlarmRepeat *repeat)
{
	g_return_if_fail (alarm != NULL);

	if (repeat != alarm->repeat) {
		e_cal_component_alarm_repeat_free (alarm->repeat);

		alarm->repeat = repeat ? e_cal_component_alarm_repeat_copy (repeat) : NULL;
	}
}

/**
 * e_cal_component_alarm_take_repeat: (skip)
 * @alarm: an #ECalComponentAlarm
 * @repeat: (transfer none) (nullable): a repeat information to set, or %NULL to unset
 *
 * Set the @alarm repeat information, as an #ECalComponentAlarmRepeat and assumes
 * ownership of the @trigger.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_take_repeat (ECalComponentAlarm *alarm,
				   ECalComponentAlarmRepeat *repeat)
{
	g_return_if_fail (alarm != NULL);

	if (repeat != alarm->repeat) {
		e_cal_component_alarm_repeat_free (alarm->repeat);
		alarm->repeat = repeat;
	}
}

/**
 * e_cal_component_alarm_get_trigger:
 * @alarm: an #ECalComponentAlarm
 *
 * Get the @alarm trigger, as an #ECalComponentAlarmTrigger.
 *
 * Returns: (transfer none) (nullable): the @alarm trigger, or %NULL when, none is set
 *
 * Since: 3.34
 **/
ECalComponentAlarmTrigger *
e_cal_component_alarm_get_trigger (const ECalComponentAlarm *alarm)
{
	g_return_val_if_fail (alarm != NULL, NULL);

	return alarm->trigger;
}

/**
 * e_cal_component_alarm_set_trigger:
 * @alarm: an #ECalComponentAlarm
 * @trigger: (transfer none) (nullable): a trigger to set, or %NULL to unset
 *
 * Set the @alarm trigger, as an #ECalComponentAlarmTrigger.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_set_trigger (ECalComponentAlarm *alarm,
				   const ECalComponentAlarmTrigger *trigger)
{
	g_return_if_fail (alarm != NULL);

	if (trigger != alarm->trigger) {
		e_cal_component_alarm_trigger_free (alarm->trigger);

		alarm->trigger = trigger ? e_cal_component_alarm_trigger_copy (trigger) : NULL;
	}
}

/**
 * e_cal_component_alarm_take_trigger: (skip)
 * @alarm: an #ECalComponentAlarm
 * @trigger: (transfer full) (nullable): a trigger to set, or %NULL to unset
 *
 * Set the @alarm trigger, as an #ECalComponentAlarmTrigger and assumes
 * ownership of the @trigger.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_take_trigger (ECalComponentAlarm *alarm,
				    ECalComponentAlarmTrigger *trigger)
{
	g_return_if_fail (alarm != NULL);

	if (trigger != alarm->trigger) {
		e_cal_component_alarm_trigger_free (alarm->trigger);
		alarm->trigger = trigger;
	}
}

/**
 * e_cal_component_alarm_has_attendees:
 * @alarm: an #ECalComponentAlarm
 *
 * Returns: whether the @alarm has any attendees
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_alarm_has_attendees (const ECalComponentAlarm *alarm)
{
	g_return_val_if_fail (alarm != NULL, FALSE);

	return alarm->attendees != NULL;
}

/**
 * e_cal_component_alarm_get_attendees:
 * @alarm: an #ECalComponentAlarm
 *
 * Get the list of attendees, as #ECalComponentAttendee.
 * The returned #GSList is owned by @alarm and should not be modified,
 * neither its content.
 *
 * Returns: (transfer none) (nullable) (element-type ECalComponentAttendee): the @alarm attendees,
 *    as a #GSList of an #ECalComponentAttendee, or %NULL when, none are set
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_alarm_get_attendees (const ECalComponentAlarm *alarm)
{
	g_return_val_if_fail (alarm != NULL, NULL);

	return alarm->attendees;
}

/**
 * e_cal_component_alarm_set_attendees:
 * @alarm: an #ECalComponentAlarm
 * @attendees: (transfer none) (nullable) (element-type ECalComponentAttendee): a #GSList
 *    of an #ECalComponentAttendee objects to set as attendees, or %NULL to unset
 *
 * Set the list of attendees, as a #GSList of an #ECalComponentAttendee.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_set_attendees (ECalComponentAlarm *alarm,
				     const GSList *attendees)
{
	GSList *to_take = NULL, *link;

	g_return_if_fail (alarm != NULL);

	if (alarm->attendees == attendees)
		return;

	for (link = (GSList *) attendees; link; link = g_slist_next (link)) {
		ECalComponentAttendee *attendee = link->data;

		if (attendee)
			to_take = g_slist_prepend (to_take, e_cal_component_attendee_copy (attendee));
	}

	to_take = g_slist_reverse (to_take);

	e_cal_component_alarm_take_attendees (alarm, to_take);
}

/**
 * e_cal_component_alarm_take_attendees: (skip)
 * @alarm: an #ECalComponentAlarm
 * @attendees: (transfer full) (nullable) (element-type ECalComponentAttendee): a #GSList
 *    of an #ECalComponentAttendee objects to set as attendees, or %NULL to unset
 *
 * Sets the list of attendees, as a #GSList of an #ECalComponentAttendee and assumes
 * ownership of the @attendees and its content.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_take_attendees (ECalComponentAlarm *alarm,
				      GSList *attendees)
{
	g_return_if_fail (alarm != NULL);

	if (alarm->attendees != attendees) {
		g_slist_free_full (alarm->attendees, e_cal_component_attendee_free);
		alarm->attendees = attendees;
	}
}

/**
 * e_cal_component_alarm_has_attachments:
 * @alarm: an #ECalComponentAlarm
 *
 * Returns: whether the @alarm has any attachments
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_alarm_has_attachments (const ECalComponentAlarm *alarm)
{
	g_return_val_if_fail (alarm != NULL, FALSE);

	return alarm->attachments != NULL;
}

/**
 * e_cal_component_alarm_get_attachments:
 * @alarm: an #ECalComponentAlarm
 *
 * Get the list of attachments, as #ICalAttach.
 * The returned #GSList is owned by @alarm and should not be modified,
 * neither its content.
 *
 * Returns: (transfer none) (nullable) (element-type ICalAttach): the @alarm attachments,
 *    as a #GSList of an #ICalAttach, or %NULL, when none is set
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_alarm_get_attachments (const ECalComponentAlarm *alarm)
{
	g_return_val_if_fail (alarm != NULL, NULL);

	return alarm->attachments;
}

/**
 * e_cal_component_alarm_set_attachments:
 * @alarm: an #ECalComponentAlarm
 * @attachments: (transfer none) (nullable) (element-type ICalAttach): a #GSList
 *    of an #ICalAttach objects to set as attachments, or %NULL to unset
 *
 * Set the list of attachments, as a #GSList of an #ICalAttach.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_set_attachments (ECalComponentAlarm *alarm,
				       const GSList *attachments)
{
	GSList *to_take = NULL, *link;

	g_return_if_fail (alarm != NULL);

	if (alarm->attachments == attachments)
		return;

	for (link = (GSList *) attachments; link; link = g_slist_next (link)) {
		ICalAttach *attach = link->data;

		if (attach)
			to_take = g_slist_prepend (to_take, g_object_ref (attach));
	}

	to_take = g_slist_reverse (to_take);

	e_cal_component_alarm_take_attachments (alarm, to_take);
}

/**
 * e_cal_component_alarm_take_attachments: (skip)
 * @alarm: an #ECalComponentAlarm
 * @attachments: (transfer full) (nullable) (element-type ICalAttach): a #GSList
 *    of an #ICalAttach objects to set as attachments, or %NULL to unset
 *
 * Sets the list of attachments, as a #GSList of an #ICalAttach and assumes
 * ownership of the @attachments and its content.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_take_attachments (ECalComponentAlarm *alarm,
					GSList *attachments)
{
	g_return_if_fail (alarm != NULL);

	if (alarm->attachments != attachments) {
		g_slist_free_full (alarm->attachments, g_object_unref);
		alarm->attachments = attachments;
	}
}

/**
 * e_cal_component_alarm_get_property_bag:
 * @alarm: an #ECalComponentAlarm
 *
 * Returns: (transfer none): an #ECalComponentPropertyBag with additional
 *    properties stored with an alarm component, other than those accessible
 *    with the other functions of the @alarm.
 *
 * Since: 3.34
 **/
ECalComponentPropertyBag *
e_cal_component_alarm_get_property_bag (const ECalComponentAlarm *alarm)
{
	g_return_val_if_fail (alarm != NULL, NULL);

	return alarm->property_bag;
}

/**
 * e_cal_component_alarm_get_acknowledged:
 * @alarm: an #ECalComponentAlarm
 *
 * Get the last time the alarm had been acknowledged, that is, when its
 * reminder had been triggered.
 * The returned #ICalTime is owned by @alarm and should not be modified,
 * neither its content.
 *
 * Returns: (transfer none) (nullable): the @alarm acknowledged time,
 *    or %NULL, when none is set
 *
 * Since: 3.40
 **/
ICalTime *
e_cal_component_alarm_get_acknowledged (const ECalComponentAlarm *alarm)
{
	g_return_val_if_fail (alarm != NULL, NULL);

	return alarm->acknowledged;
}

/**
 * e_cal_component_alarm_set_acknowledged:
 * @alarm: an #ECalComponentAlarm
 * @when: (transfer none) (nullable): an #ICalTime when the @alarm
 *    had been acknowledged, or %NULL to unset
 *
 * Set the acknowledged time of the @alarm. Use %NULL to unset it.
 *
 * Since: 3.40
 **/
void
e_cal_component_alarm_set_acknowledged (ECalComponentAlarm *alarm,
					const ICalTime *when)
{
	g_return_if_fail (alarm != NULL);

	if (when != alarm->acknowledged)
		e_cal_component_alarm_take_acknowledged (alarm, when ? i_cal_time_clone (when) : NULL);
}

/**
 * e_cal_component_alarm_take_acknowledged:
 * @alarm: an #ECalComponentAlarm
 * @when: (transfer full) (nullable): an #ICalTime when the @alarm
 *    had been acknowledged, or %NULL to unset
 *
 * Set the acknowledged time of the @alarm. Use %NULL to unset it.
 * The function assumes ownership of the @when.
 *
 * Since: 3.40
 **/
void
e_cal_component_alarm_take_acknowledged (ECalComponentAlarm *alarm,
					 ICalTime *when)
{
	g_return_if_fail (alarm != NULL);

	if (when != alarm->acknowledged) {
		g_clear_object (&alarm->acknowledged);
		alarm->acknowledged = when;
	} else {
		g_clear_object (&when);
	}
}

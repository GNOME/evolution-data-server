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
 * SECTION:e-cal-component-alarm-trigger
 * @short_description: An ECalComponentAlarmTrigger structure
 * @include: libecal/libecal.h
 *
 * Contains functions to work with the #ECalComponentAlarmTrigger structure.
 **/

#include "e-cal-component-alarm-trigger.h"

G_DEFINE_BOXED_TYPE (ECalComponentAlarmTrigger, e_cal_component_alarm_trigger, e_cal_component_alarm_trigger_copy, e_cal_component_alarm_trigger_free)

struct _ECalComponentAlarmTrigger {
	ECalComponentAlarmTriggerKind kind;

	/* Only one of the below can be set, depending on the 'kind' */
	ICalDurationType *rel_duration;
	ICalTimetype *abs_time;
};

/**
 * e_cal_component_alarm_trigger_new_relative:
 * @kind: an #ECalComponentAlarmTriggerKind, any but the %E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE
 * @duration: (not nullable): the duration relative to @kind, as an #ICalDurationType
 *
 * Creates a new #ECalComponentAlarmTrigger structure, set with the given @kind
 * and @duration. The @kind can be any but the %E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE.
 * To create an absolute trigger use e_cal_component_alarm_trigger_new_absolute().
 * Free the trigger with e_cal_component_alarm_trigger_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentAlarmTrigger
 *
 * Since: 3.36
 **/
ECalComponentAlarmTrigger *
e_cal_component_alarm_trigger_new_relative (ECalComponentAlarmTriggerKind kind,
					    const ICalDurationType *duration)
{
	ECalComponentAlarmTrigger *trigger;

	g_return_val_if_fail (kind != E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE, NULL);
	g_return_val_if_fail (I_CAL_IS_DURATION_TYPE (duration), NULL);

	trigger = g_new0 (ECalComponentAlarmTrigger, 1);

	e_cal_component_alarm_trigger_set_relative (trigger, kind, duration);

	return trigger;
}

/**
 * e_cal_component_alarm_trigger_new_absolute:
 * @absolute_time: (not nullable): the absolute time when to trigger the alarm, as an #ICalTimetype
 *
 * Creates a new #ECalComponentAlarmTrigger structure, set with
 * the %E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE kind and the @absolute_time as
 * the time of the trigger.
 * To create a relative trigger use e_cal_component_alarm_trigger_new_relative().
 * Free the trigger with e_cal_component_alarm_trigger_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentAlarmTrigger
 *
 * Since: 3.36
 **/
ECalComponentAlarmTrigger *
e_cal_component_alarm_trigger_new_absolute (const ICalTimetype *absolute_time)
{
	ECalComponentAlarmTrigger *trigger;

	g_return_val_if_fail (I_CAL_IS_TIMETYPE (absolute_time), NULL);

	trigger = g_new0 (ECalComponentAlarmTrigger, 1);

	e_cal_component_alarm_trigger_set_absolute (trigger, absolute_time);

	return trigger;
}

/**
 * e_cal_component_alarm_trigger_new_from_property:
 * @property: an #ICalProperty of kind %I_CAL_TRIGGER_PROPERTY
 *
 * Creates a new #ECalComponentAlarmTrigger, filled with values from @property,
 * which should be of kind %I_CAL_TRIGGER_PROPERTY. The function returns
 * %NULL when it is not of the expected kind. Free the structure
 * with e_cal_component_alarm_trigger_free(), when no longer needed.
 *
 * Returns: (transfer full) (nullable): a newly allocated #ECalComponentAlarmTrigger
 *
 * Since: 3.36
 **/
ECalComponentAlarmTrigger *
e_cal_component_alarm_trigger_new_from_property (const ICalProperty *property)
{
	ECalComponentAlarmTrigger *trigger;

	g_return_val_if_fail (I_CAL_IS_PROPERTY (property), NULL);

	if (i_cal_property_isa ((ICalProperty *) property) != I_CAL_TRIGGER_PROPERTY)
		return NULL;

	trigger = e_cal_component_alarm_trigger_new ();

	e_cal_component_alarm_trigger_set_from_property (trigger, property);

	return trigger;
}

/**
 * e_cal_component_alarm_trigger_copy:
 * @trigger: (not nullable): an #ECalComponentAlarmTrigger
 *
 * Returns a newly allocated copy of @trigger, which should be freed with
 * e_cal_component_alarm_trigger_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated copy of @trigger
 *
 * Since: 3.36
 **/
ECalComponentAlarmTrigger *
e_cal_component_alarm_trigger_copy (const ECalComponentAlarmTrigger *trigger)
{
	g_return_val_if_fail (trigger != NULL, NULL);

	if (trigger->kind == E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE)
		return e_cal_component_alarm_trigger_new_absolute (trigger->abs_time);

	return e_cal_component_alarm_trigger_new_relative (trigger->kind, trigger->rel_duration);
}

/**
 * e_cal_component_alarm_trigger_free: (skip)
 * @trigger: (type ECalComponentAlarmTrigger) (nullable): an #ECalComponentAlarmTrigger to free
 *
 * Free @trigger, previously created by e_cal_component_alarm_trigger_new_relative(),
 * e_cal_component_alarm_trigger_new_absolute(), e_cal_component_alarm_trigger_new_from_property()
 * or e_cal_component_alarm_trigger_copy(). The function does nothing, if @trigger
 * is %NULL.
 *
 * Since: 3.36
 **/
void
e_cal_component_alarm_trigger_free (gpointer trigger)
{
	ECalComponentAlarmTrigger *trg = trigger;

	if (trg) {
		g_clear_object (&trg->rel_duration);
		g_clear_object (&trg->abs_time);
		g_free (trg);
	}
}

/**
 * e_cal_component_alarm_trigger_set_from_property:
 * @trigger: an #ECalComponentAlarmTrigger
 * @property: an #ICalProperty
 *
 * Fill the @trigger structure with the information from
 * the @property, which should be of %I_CAL_TRIGGER_PROPERTY kind.
 *
 * Since: 3.36
 **/
void
e_cal_component_alarm_trigger_set_from_property (ECalComponentAlarmTrigger *trigger,
						 const ICalProperty *property)
{
	ICalProperty *prop = (ICalProperty *) property;
	ICalTriggerType *trgtype;
	gboolean relative;

	g_return_if_fail (trigger != NULL);
	g_return_if_fail (I_CAL_IS_PROPERTY (property));
	g_return_if_fail (i_cal_property_isa (prop) == I_CAL_TRIGGER_PROPERTY);

	g_clear_object (&trigger->rel_duration);
	g_clear_object (&trigger->abs_time);

	param = i_cal_property_get_first_parameter (prop, I_CAL_VALUE_PARAMETER);
	if (param) {
		switch (i_cal_parameter_get_value (param)) {
		case I_CAL_VALUE_DURATION:
			relative = TRUE;
			break;

		case I_CAL_VALUE_DATETIME:
			relative = FALSE;
			break;

		default:
			relative = TRUE;
			break;
		}

		g_clear_object (&param);
	} else {
		relative = TRUE;
	}

	trgtype = i_cal_property_get_trigger (prop);

	if (relative) {
		ECalComponentAlarmTriggerKind kind;
		ICalDurationType *duration;
		ICalParameter *param;

		param = i_cal_property_get_first_parameter (prop, I_CAL_RELATED_PARAMETER);
		if (param) {
			switch (i_cal_parameter_get_related (param)) {
			case I_CAL_RELATED_START:
				kind = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;
				break;

			case ICAL_RELATED_END:
				kind = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END;
				break;

			default:
				kind = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_NONE;
				break;
			}
		} else {
			kind = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;
		}

		duration = i_cal_trigger_type_get_duration (trgtype);

		e_cal_component_alarm_trigger_set_relative (trigger, kind, duration);

		g_clear_object (&duration);
		g_clear_object (&param);
	} else {
		ICalTimetype *abs_time;

		abs_time = i_cal_trigger_type_get_time (trgtype);
		e_cal_component_alarm_trigger_set_absolute (trigger, abs_time);
		g_clear_object (&abs_time);
	}

	g_clear_object (&trgtype);
}

/**
 * e_cal_component_alarm_trigger_get_as_property:
 * @trigger: an #ECalComponentAlarmTrigger
 *
 * Converts information stored in @trigger into an #ICalProperty
 * of %I_CAL_TRIGGER_PROPERTY kind. The caller is responsible to free
 * the returned object with g_object_unref(), when no longer needed.
 *
 * Returns: (transfer full): a newly created #ICalProperty, containing
 *    information from the @trigger.
 *
 * Since: 3.36
 **/
ICalProperty *
e_cal_component_alarm_trigger_get_as_property (const ECalComponentAlarmTrigger *trigger)
{
	ICalProperty *prop;

	g_return_val_if_fail (trigger != NULL, NULL);

	prop = i_cal_property_new (I_CAL_TRIGGER_PROPERTY);
	g_return_val_if_fail (prop != NULL, NULL);

	e_cal_component_alarm_trigger_fill_property (trigger, prop);

	return prop;
}

/**
 * e_cal_component_alarm_trigger_fill_property:
 * @trigger: an #ECalComponentAlarmTrigger
 * @property: (inout) (not nullable): an #ICalProperty
 *
 * Fill @property with information from @trigger. The @property
 * should be of kind %I_CAL_TRIGGER_PROPERTY.
 *
 * Since: 3.36
 **/
void
e_cal_component_alarm_trigger_fill_property (const ECalComponentAlarmTrigger *trigger,
					     ICalProperty *property)
{
	ICalParameter *param;
	ICalParameterValue value_type;
	ICalParameterRelated related;
	ICalTriggerType *trgtype;

	g_return_if_fail (trigger != NULL);
	g_return_if_fail (I_CAL_IS_PROPERTY (property));
	g_return_if_fail (i_cal_property_isa (property) == I_CAL_TRIGGER_PROPERTY);

	g_return_if_fail (alarm != NULL);
	g_return_if_fail (trigger.type != E_CAL_COMPONENT_ALARM_TRIGGER_NONE);
	g_return_if_fail (alarm->icalcomp != NULL);

	related = I_CAL_RELATED_START;
	trgtype = i_cal_trigger_type_from_int (0);

	switch (trigger->kind) {
	case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START:
		i_cal_trigger_type_set_duration (trgtype, trigger->duration);
		value_type = I_CAL_VALUE_DURATION;
		related = I_CAL_RELATED_START;
		break;

	case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END:
		i_cal_trigger_type_set_duration (trgtype, trigger->duration);
		value_type = I_CAL_VALUE_DURATION;
		related = I_CAL_RELATED_END;
		break;

	case E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE:
		i_cal_trigger_type_set_time (trgtype, trigger->abs_time);
		value_type = I_CAL_VALUE_DATETIME;
		break;

	default:
		g_return_if_reached ();
	}

	i_cal_property_set_trigger (prop, trgtype);

	param = icalproperty_get_first_parameter (prop, I_CAL_VALUE_PARAMETER);
	if (param) {
		i_cal_parameter_set_value (param, value_type);
	} else {
		param = i_cal_parameter_new_value (value_type);
		i_cal_property_set_parameter (prop, param);
	}
	g_clear_object (&param);

	param = i_cal_property_get_first_parameter (prop, I_CAL_RELATED_PARAMETER);
	if (trigger->kind != E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE) {
		if (param) {
			i_cal_parameter_set_related (param, related);
		} else {
			param = i_cal_parameter_new_related (related);
			i_cal_property_add_parameter (prop, param);
		}
	} else if (param) {
		i_cal_property_remove_parameter (prop, param);
	}
	g_clear_object (&param);
}

/**
 * e_cal_component_alarm_trigger_set_relative:
 * @trigger: an #ECalComponentAlarmTrigger
 * @kind: an #ECalComponentAlarmTriggerKind, any but the %E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE
 * @duration: (not nullable): the duration relative to @kind, as an #ICalDurationType
 *
 * Set the @trigegr with the given @kind and @duration. The @kind can be any but
 * the %E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE.
 * To set an absolute trigger use e_cal_component_alarm_trigger_set_absolute().
 *
 * Since: 3.36
 **/
void
e_cal_component_alarm_trigger_set_relative (ECalComponentAlarmTrigger *trigger,
					    ECalComponentAlarmTriggerKind kind,
					    const ICalDurationType *duration)
{
	g_return_if_fail (trigger != NULL);
	g_return_if_fail (kind != E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE);
	g_return_if_fail (I_CAL_IS_DURATION_TYPE (duration));

	g_clear_object (&trigger->rel_duration);
	g_clear_object (&trigger->abs_time);

	trigger->kind = kind;
	trigger->rel_duration = i_cal_duration_type_from_int (
		i_cal_duration_type_as_int ((ICalDurationType *) duration));
}

/**
 * e_cal_component_alarm_trigger_set_absolute:
 * @trigger: an #ECalComponentAlarmTrigger
 * @absolute_time: (not nullable): the absolute time when to trigger the alarm, as an #ICalTimetype
 *
 * Set the @trigegr with the %E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE kind and
 * the @absolute_time as the time of the trigger.
 * To set a relative trigger use e_cal_component_alarm_trigger_set_relative().
 *
 * Since: 3.36
 **/
void
e_cal_component_alarm_trigger_set_absolute (ECalComponentAlarmTrigger *trigger,
					    const ICalTimetype *absolute_time)
{
	g_return_if_fail (trigger != NULL);
	g_return_if_fail (I_CAL_IS_TIMETYPE (absolute_time));

	g_clear_object (&trigger->rel_duration);
	g_clear_object (&trigger->abs_time);

	trigger->kind = E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE;
	trigger->absolute_time = i_cal_timetype_new_clone (absolute_time);
}

/**
 * e_cal_component_alarm_trigger_get_kind:
 * @trigger: an #ECalComponentAlarmTrigger
 *
 * Returns: the @trigger kind, one of #ECalComponentAlarmTriggerKind
 *
 * Since: 3.36
 **/
ECalComponentAlarmTriggerKind
e_cal_component_alarm_trigger_get_kind (ECalComponentAlarmTrigger *trigger)
{
	g_return_val_if_fail (trigger != NULL, E_CAL_COMPONENT_ALARM_TRIGGER_NONE);

	return trigger->kind;
}

/**
 * e_cal_component_alarm_trigger_set_kind:
 * @trigger: an #ECalComponentAlarmTrigger
 * @kind: the kind to set, one of #ECalComponentAlarmTriggerKind
 *
 * Set the @trigger kind to @kind. This works only for other than
 * the %E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE. To change the kind
 * from absolute to relative, or vice versa, use either
 * e_cal_component_alarm_trigger_set_relative() or
 * e_cal_component_alarm_trigger_set_absolute().
 *
 * Since: 3.36
 **/
void
e_cal_component_alarm_trigger_set_kind (ECalComponentAlarmTrigger *trigger,
					ECalComponentAlarmTriggerKind kind)
{
	g_return_if_fail (trigger != NULL);
	g_return_if_fail (trigger->kind != E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE);
	g_return_if_fail (kind != E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE);

	if (trigger->kind != kind) {
		trigger->kind = kind;
	}
}

/**
 * e_cal_component_alarm_trigger_get_duration:
 * @trigger: an #ECalComponentAlarmTrigger
 *
 * Returns the @trigger duration for a relative @trigger, or %NULL, when
 * the @trigger is an absolute trigger.
 *
 * Returns: (transfer none) (nullable): the @trigger duration, as an #ICalDurationType, or %NULL
 *
 * Since: 3.36
 **/
ICalDurationType *
e_cal_component_alarm_trigger_get_duration (const ECalComponentAlarmTrigger *trigger)
{
	g_return_val_if_fail (trigger != NULL, NULL);

	if (trigger->kind == E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE)
		return NULL;

	return trigger->rel_duration;
}

/**
 * e_cal_component_alarm_trigger_set_duration:
 * @trigger: an #ECalComponentAlarmTrigger
 * @duration: duration for a relative trigger, as an #ICalDurationType
 *
 * Sets the @trigger duration for a relative trigger. The function does nothing, when
 * the @trigger is an absolute trigger. The object is owned by @trigger and it's
 * valid until the @trigger is freed or its relative duration changed.
 *
 * Since: 3.36
 **/
void
e_cal_component_alarm_trigger_set_duration (ECalComponentAlarmTrigger *trigger,
					    const ICalDurationType *duration)
{
	g_return_if_fail (trigger != NULL);
	g_return_if_fail (I_CAL_IS_DURATION_TYPE (duration));

	if (trigger->kind == E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE)
		return;

	if (trigger->rel_duration != duration &&
	    i_cal_duration_type_as_int (trigger->rel_duration) != i_cal_duration_type_as_int ((ICalDurationType *) duration)) {
		g_clear_object (&trigger->rel_duration);
		trigger->rel_duration = i_cal_duration_type_from_int (
			i_cal_duration_type_as_int ((ICalDurationType *) duration));
	}
}

/**
 * e_cal_component_alarm_trigger_get_absolute_time:
 * @trigger: an #ECalComponentAlarmTrigger
 *
 * Returns the @trigger absolute time for an absolute trigger, or %NULL, when
 * the @trigger is a relative trigger. The object is owned by @trigger and it's
 * valid until the @trigger is freed or its absolute time changed.
 *
 * Returns: (transfer none) (nullable): the @trigger absolute time, as an #ICalTimetype, or %NULL
 *
 * Since: 3.36
 **/
ICalTimetype *
e_cal_component_alarm_trigger_get_absolute_time (const ECalComponentAlarmTrigger *trigger)
{
	g_return_val_if_fail (trigger != NULL, NULL);

	if (trigger->kind != E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE)
		return NULL;

	return trigger->abs_time;
}

/**
 * e_cal_component_alarm_trigger_set_absolute_time:
 * @trigger: an #ECalComponentAlarmTrigger
 * @absolute_time: absolute time for an absolute trigger, as an #ICalTimetype
 *
 * Sets the @trigger absolute time for an absolute trigger. The function does nothing, when
 * the @trigger is a relative trigger.
 *
 * Since: 3.36
 **/
void
e_cal_component_alarm_trigger_set_absolute_time (ECalComponentAlarmTrigger *trigger,
						 const ICalTimetype *absolute_time)
{
	g_return_if_fail (trigger != NULL);
	g_return_if_fail (I_CAL_IS_TIMETYPE (absolute_time));

	if (trigger->kind == E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE)
		return;

	if (trigger->abs_time != absolute_time) {
		g_clear_object (&trigger->abs_time);
		trigger->abs_time = i_cal_timetype_clone (absolute_time);
	}
}

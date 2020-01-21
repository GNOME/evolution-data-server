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

#include "e-cal-component-parameter-bag.h"

#include "e-cal-component-alarm-trigger.h"

G_DEFINE_BOXED_TYPE (ECalComponentAlarmTrigger, e_cal_component_alarm_trigger, e_cal_component_alarm_trigger_copy, e_cal_component_alarm_trigger_free)

struct _ECalComponentAlarmTrigger {
	ECalComponentAlarmTriggerKind kind;

	/* Only one of the below can be set, depending on the 'kind' */
	ICalDuration *rel_duration;
	ICalTime *abs_time;

	ECalComponentParameterBag *parameter_bag;
};

/**
 * e_cal_component_alarm_trigger_new_relative:
 * @kind: an #ECalComponentAlarmTriggerKind, any but the %E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE
 * @duration: (not nullable): the duration relative to @kind, as an #ICalDuration
 *
 * Creates a new #ECalComponentAlarmTrigger structure, set with the given @kind
 * and @duration. The @kind can be any but the %E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE.
 * To create an absolute trigger use e_cal_component_alarm_trigger_new_absolute().
 * Free the trigger with e_cal_component_alarm_trigger_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentAlarmTrigger
 *
 * Since: 3.34
 **/
ECalComponentAlarmTrigger *
e_cal_component_alarm_trigger_new_relative (ECalComponentAlarmTriggerKind kind,
					    const ICalDuration *duration)
{
	ECalComponentAlarmTrigger *trigger;

	g_return_val_if_fail (kind != E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE, NULL);
	g_return_val_if_fail (I_CAL_IS_DURATION (duration), NULL);

	trigger = g_slice_new0 (ECalComponentAlarmTrigger);
	trigger->parameter_bag = e_cal_component_parameter_bag_new ();

	e_cal_component_alarm_trigger_set_relative (trigger, kind, duration);

	return trigger;
}

/**
 * e_cal_component_alarm_trigger_new_absolute:
 * @absolute_time: (not nullable): the absolute time when to trigger the alarm, as an #ICalTime
 *
 * Creates a new #ECalComponentAlarmTrigger structure, set with
 * the %E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE kind and the @absolute_time as
 * the time of the trigger. The @absolute_time should be date/time (not date) in UTC.
 *
 * To create a relative trigger use e_cal_component_alarm_trigger_new_relative().
 * Free the trigger with e_cal_component_alarm_trigger_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentAlarmTrigger
 *
 * Since: 3.34
 **/
ECalComponentAlarmTrigger *
e_cal_component_alarm_trigger_new_absolute (const ICalTime *absolute_time)
{
	ECalComponentAlarmTrigger *trigger;

	g_return_val_if_fail (I_CAL_IS_TIME (absolute_time), NULL);

	trigger = g_slice_new0 (ECalComponentAlarmTrigger);
	trigger->parameter_bag = e_cal_component_parameter_bag_new ();

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
 * Since: 3.34
 **/
ECalComponentAlarmTrigger *
e_cal_component_alarm_trigger_new_from_property (const ICalProperty *property)
{
	ECalComponentAlarmTrigger *trigger;

	g_return_val_if_fail (I_CAL_IS_PROPERTY (property), NULL);

	if (i_cal_property_isa ((ICalProperty *) property) != I_CAL_TRIGGER_PROPERTY)
		return NULL;

	trigger = g_slice_new0 (ECalComponentAlarmTrigger);
	trigger->parameter_bag = e_cal_component_parameter_bag_new ();

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
 * Since: 3.34
 **/
ECalComponentAlarmTrigger *
e_cal_component_alarm_trigger_copy (const ECalComponentAlarmTrigger *trigger)
{
	ECalComponentAlarmTrigger *copy;

	g_return_val_if_fail (trigger != NULL, NULL);

	if (trigger->kind == E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE)
		copy = e_cal_component_alarm_trigger_new_absolute (trigger->abs_time);
	else
		copy = e_cal_component_alarm_trigger_new_relative (trigger->kind, trigger->rel_duration);

	e_cal_component_parameter_bag_assign (copy->parameter_bag, trigger->parameter_bag);

	return copy;
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
 * Since: 3.34
 **/
void
e_cal_component_alarm_trigger_free (gpointer trigger)
{
	ECalComponentAlarmTrigger *trg = trigger;

	if (trg) {
		e_cal_component_parameter_bag_free (trg->parameter_bag);
		g_clear_object (&trg->rel_duration);
		g_clear_object (&trg->abs_time);
		g_slice_free (ECalComponentAlarmTrigger, trg);
	}
}

static gboolean
e_cal_component_alarm_trigger_bag_filter_params_cb (ICalParameter *param,
						    gpointer user_data)
{
	ICalParameterKind kind;

	kind = i_cal_parameter_isa (param);

	return kind != I_CAL_VALUE_PARAMETER &&
	       kind != I_CAL_RELATED_PARAMETER;
}

/**
 * e_cal_component_alarm_trigger_set_from_property:
 * @trigger: an #ECalComponentAlarmTrigger
 * @property: an #ICalProperty
 *
 * Fill the @trigger structure with the information from
 * the @property, which should be of %I_CAL_TRIGGER_PROPERTY kind.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_trigger_set_from_property (ECalComponentAlarmTrigger *trigger,
						 const ICalProperty *property)
{
	ICalProperty *prop = (ICalProperty *) property;
	ICalParameter *param;
	ICalTrigger *trg;
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

	trg = i_cal_property_get_trigger (prop);

	if (relative) {
		ECalComponentAlarmTriggerKind kind;
		ICalDuration *duration;
		ICalParameter *param;

		param = i_cal_property_get_first_parameter (prop, I_CAL_RELATED_PARAMETER);
		if (param) {
			switch (i_cal_parameter_get_related (param)) {
			case I_CAL_RELATED_START:
				kind = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;
				break;

			case I_CAL_RELATED_END:
				kind = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END;
				break;

			default:
				kind = E_CAL_COMPONENT_ALARM_TRIGGER_NONE;
				break;
			}
		} else {
			kind = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;
		}

		duration = i_cal_trigger_get_duration (trg);

		e_cal_component_alarm_trigger_set_relative (trigger, kind, duration);

		g_clear_object (&duration);
		g_clear_object (&param);
	} else {
		ICalTime *abs_time;

		abs_time = i_cal_trigger_get_time (trg);
		e_cal_component_alarm_trigger_set_absolute (trigger, abs_time);
		g_clear_object (&abs_time);
	}

	g_clear_object (&trg);

	e_cal_component_parameter_bag_set_from_property (trigger->parameter_bag, prop, e_cal_component_alarm_trigger_bag_filter_params_cb, NULL);
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
 * Since: 3.34
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
 * Since: 3.34
 **/
void
e_cal_component_alarm_trigger_fill_property (const ECalComponentAlarmTrigger *trigger,
					     ICalProperty *property)
{
	ICalParameter *param;
	ICalParameterValue value_type = I_CAL_VALUE_DATETIME;
	ICalParameterRelated related;
	ICalTrigger *trg;

	g_return_if_fail (trigger != NULL);
	g_return_if_fail (I_CAL_IS_PROPERTY (property));
	g_return_if_fail (i_cal_property_isa (property) == I_CAL_TRIGGER_PROPERTY);

	related = I_CAL_RELATED_START;
	trg = i_cal_trigger_new_from_int (0);

	switch (trigger->kind) {
	case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START:
		i_cal_trigger_set_duration (trg, trigger->rel_duration);
		value_type = I_CAL_VALUE_DURATION;
		related = I_CAL_RELATED_START;
		break;

	case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END:
		i_cal_trigger_set_duration (trg, trigger->rel_duration);
		value_type = I_CAL_VALUE_DURATION;
		related = I_CAL_RELATED_END;
		break;

	case E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE:
		i_cal_trigger_set_time (trg, trigger->abs_time);
		value_type = I_CAL_VALUE_DATETIME;
		break;

	case E_CAL_COMPONENT_ALARM_TRIGGER_NONE:
		g_object_unref (trg);
		return;
	}

	i_cal_property_set_trigger (property, trg);

	g_object_unref (trg);

	param = i_cal_property_get_first_parameter (property, I_CAL_VALUE_PARAMETER);
	if (param) {
		i_cal_parameter_set_value (param, value_type);
	} else {
		param = i_cal_parameter_new_value (value_type);
		i_cal_property_set_parameter (property, param);
	}
	g_clear_object (&param);

	param = i_cal_property_get_first_parameter (property, I_CAL_RELATED_PARAMETER);
	if (trigger->kind != E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE) {
		if (param) {
			i_cal_parameter_set_related (param, related);
		} else {
			param = i_cal_parameter_new_related (related);
			i_cal_property_add_parameter (property, param);
		}
	} else if (param) {
		i_cal_property_remove_parameter_by_kind (property, I_CAL_RELATED_PARAMETER);
	}
	g_clear_object (&param);

	e_cal_component_parameter_bag_fill_property (trigger->parameter_bag, property);
}

/**
 * e_cal_component_alarm_trigger_set_relative:
 * @trigger: an #ECalComponentAlarmTrigger
 * @kind: an #ECalComponentAlarmTriggerKind, any but the %E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE
 * @duration: (not nullable): the duration relative to @kind, as an #ICalDuration
 *
 * Set the @trigegr with the given @kind and @duration. The @kind can be any but
 * the %E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE.
 * To set an absolute trigger use e_cal_component_alarm_trigger_set_absolute().
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_trigger_set_relative (ECalComponentAlarmTrigger *trigger,
					    ECalComponentAlarmTriggerKind kind,
					    const ICalDuration *duration)
{
	g_return_if_fail (trigger != NULL);
	g_return_if_fail (kind != E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE);
	g_return_if_fail (I_CAL_IS_DURATION (duration));

	g_clear_object (&trigger->rel_duration);
	g_clear_object (&trigger->abs_time);

	trigger->kind = kind;
	trigger->rel_duration = i_cal_duration_new_from_int (
		i_cal_duration_as_int ((ICalDuration *) duration));
}

/**
 * e_cal_component_alarm_trigger_set_absolute:
 * @trigger: an #ECalComponentAlarmTrigger
 * @absolute_time: (not nullable): the absolute time when to trigger the alarm, as an #ICalTime
 *
 * Set the @trigegr with the %E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE kind and
 * the @absolute_time as the time of the trigger. The @absolute_time
 * should be date/time (not date) in UTC.
 *
 * To set a relative trigger use e_cal_component_alarm_trigger_set_relative().
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_trigger_set_absolute (ECalComponentAlarmTrigger *trigger,
					    const ICalTime *absolute_time)
{
	g_return_if_fail (trigger != NULL);
	g_return_if_fail (I_CAL_IS_TIME (absolute_time));

	g_clear_object (&trigger->rel_duration);
	g_clear_object (&trigger->abs_time);

	trigger->kind = E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE;
	trigger->abs_time = i_cal_time_clone (absolute_time);
}

/**
 * e_cal_component_alarm_trigger_get_kind:
 * @trigger: an #ECalComponentAlarmTrigger
 *
 * Returns: the @trigger kind, one of #ECalComponentAlarmTriggerKind
 *
 * Since: 3.34
 **/
ECalComponentAlarmTriggerKind
e_cal_component_alarm_trigger_get_kind (const ECalComponentAlarmTrigger *trigger)
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
 * Since: 3.34
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
 * Returns: (transfer none) (nullable): the @trigger duration, as an #ICalDuration, or %NULL
 *
 * Since: 3.34
 **/
ICalDuration *
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
 * @duration: duration for a relative trigger, as an #ICalDuration
 *
 * Sets the @trigger duration for a relative trigger. The function does nothing, when
 * the @trigger is an absolute trigger. The object is owned by @trigger and it's
 * valid until the @trigger is freed or its relative duration changed.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_trigger_set_duration (ECalComponentAlarmTrigger *trigger,
					    const ICalDuration *duration)
{
	g_return_if_fail (trigger != NULL);
	g_return_if_fail (I_CAL_IS_DURATION (duration));

	if (trigger->kind == E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE)
		return;

	if (trigger->rel_duration != duration &&
	    i_cal_duration_as_int (trigger->rel_duration) != i_cal_duration_as_int ((ICalDuration *) duration)) {
		g_clear_object (&trigger->rel_duration);
		trigger->rel_duration = i_cal_duration_new_from_int (
			i_cal_duration_as_int ((ICalDuration *) duration));
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
 * Returns: (transfer none) (nullable): the @trigger absolute time, as an #ICalTime, or %NULL
 *
 * Since: 3.34
 **/
ICalTime *
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
 * @absolute_time: absolute time for an absolute trigger, as an #ICalTime
 *
 * Sets the @trigger absolute time for an absolute trigger. The @absolute_time
 * should be date/time (not date) in UTC.
 *
 * The function does nothing, when the @trigger is a relative trigger.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_trigger_set_absolute_time (ECalComponentAlarmTrigger *trigger,
						 const ICalTime *absolute_time)
{
	g_return_if_fail (trigger != NULL);
	g_return_if_fail (I_CAL_IS_TIME (absolute_time));

	if (trigger->kind != E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE)
		return;

	if (trigger->abs_time != absolute_time) {
		g_clear_object (&trigger->abs_time);
		trigger->abs_time = i_cal_time_clone ((ICalTime *) absolute_time);
	}
}

/**
 * e_cal_component_alarm_trigger_get_parameter_bag:
 * @trigger: an #ECalComponentAlarmTrigger
 *
 * Returns: (transfer none): an #ECalComponentParameterBag with additional
 *    parameters stored with the trigger property, other than those accessible
 *    with the other functions of the @trigger.
 *
 * Since: 3.34
 **/
ECalComponentParameterBag *
e_cal_component_alarm_trigger_get_parameter_bag (const ECalComponentAlarmTrigger *trigger)
{
	g_return_val_if_fail (trigger != NULL, NULL);

	return trigger->parameter_bag;
}

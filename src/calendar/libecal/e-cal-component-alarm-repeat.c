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
 * SECTION:e-cal-component-alarm-repeat
 * @short_description: An ECalComponentAlarmRepeat structure
 * @include: libecal/libecal.h
 *
 * Contains functions to work with the #ECalComponentAlarmRepeat structure.
 **/

#include "e-cal-component-alarm-repeat.h"

G_DEFINE_BOXED_TYPE (ECalComponentAlarmRepeat, e_cal_component_alarm_repeat, e_cal_component_alarm_repeat_copy, e_cal_component_alarm_repeat_free)

struct _ECalComponentAlarmRepeat {
	gint repetitions;
	ICalDuration *interval;
};

/**
 * e_cal_component_alarm_repeat_new:
 * @repetitions: number of extra repetitions, zero for none
 * @interval: (not nullable): interval between repetitions
 *
 * Creates a new #ECalComponentAlarmRepeat describing alarm repetitions.
 * The returned structure should be freed with e_cal_component_alarm_repeat_free(),
 * when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentAlarmRepeat
 *
 * Since: 3.34
 **/
ECalComponentAlarmRepeat *
e_cal_component_alarm_repeat_new (gint repetitions,
				  const ICalDuration *interval)
{
	g_return_val_if_fail (I_CAL_IS_DURATION ((ICalDuration *) interval), NULL);

	return e_cal_component_alarm_repeat_new_seconds (repetitions,
		i_cal_duration_as_int ((ICalDuration *) interval));
}

/**
 * e_cal_component_alarm_repeat_new_seconds:
 * @repetitions: number of extra repetitions, zero for none
 * @interval_seconds: interval between repetitions, in seconds
 *
 * Creates a new #ECalComponentAlarmRepeat describing alarm repetitions.
 * The returned structure should be freed with e_cal_component_alarm_repeat_free(),
 * when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentAlarmRepeat
 *
 * Since: 3.34
 **/
ECalComponentAlarmRepeat *
e_cal_component_alarm_repeat_new_seconds (gint repetitions,
					  gint interval_seconds)
{
	ECalComponentAlarmRepeat *repeat;

	repeat = g_slice_new0 (ECalComponentAlarmRepeat);
	repeat->repetitions = repetitions;
	repeat->interval = i_cal_duration_new_from_int (interval_seconds);

	return repeat;
}

/**
 * e_cal_component_alarm_repeat_copy:
 * @repeat: (not nullable): an #ECalComponentAlarmRepeat to copy
 *
 * Returns: (transfer full): a newly allocated #ECalComponentAlarmRepeat, copy of @repeat.
 *    The returned structure should be freed with e_cal_component_alarm_repeat_free(),
 *    when no longer needed.
 *
 * Since: 3.34
 **/
ECalComponentAlarmRepeat *
e_cal_component_alarm_repeat_copy (const ECalComponentAlarmRepeat *repeat)
{
	g_return_val_if_fail (repeat != NULL, NULL);

	return e_cal_component_alarm_repeat_new_seconds (repeat->repetitions,
		i_cal_duration_as_int (repeat->interval));
}

/**
 * e_cal_component_alarm_repeat_free: (skip)
 * @repeat: (type ECalComponentAlarmRepeat) (nullable): an #ECalComponentAlarmRepeat to free
 *
 * Free the @repeat, previously allocated by e_cal_component_alarm_repeat_new(),
 * e_cal_component_alarm_repeat_new_seconds() or e_cal_component_alarm_repeat_copy().
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_repeat_free (gpointer repeat)
{
	ECalComponentAlarmRepeat *rpt = repeat;

	if (rpt) {
		g_clear_object (&rpt->interval);
		g_slice_free (ECalComponentAlarmRepeat, rpt);
	}
}

/**
 * e_cal_component_alarm_repeat_get_repetitions:
 * @repeat: an #ECalComponentAlarmRepeat
 *
 * Returns: the repetitions count of the @repeat
 *
 * Since: 3.34
 **/
gint
e_cal_component_alarm_repeat_get_repetitions (const ECalComponentAlarmRepeat *repeat)
{
	g_return_val_if_fail (repeat != NULL, 0);

	return repeat->repetitions;
}

/**
 * e_cal_component_alarm_repeat_set_repetitions:
 * @repeat: an #ECalComponentAlarmRepeat
 * @repetitions: number of repetitions, zero for none
 *
 * Set the @repetitions count of the @repeat.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_repeat_set_repetitions (ECalComponentAlarmRepeat *repeat,
					      gint repetitions)
{
	g_return_if_fail (repeat != NULL);

	if (repeat->repetitions != repetitions) {
		repeat->repetitions = repetitions;
	}
}

/**
 * e_cal_component_alarm_repeat_get_interval:
 * @repeat: an #ECalComponentAlarmRepeat
 *
 * Returns the interval between repetitions of the @repeat, as an #ICalDuration
 * object. This object is owned by @repeat and should not be freed. It's valid until
 * the @repeat is not freed or its interval changed with either e_cal_component_alarm_repeat_set_interval()
 * or e_cal_component_alarm_repeat_set_interval_seconds().
 *
 * Returns: (transfer none): the interval between repetitions of the @repeat
 *
 * Since: 3.34
 **/
ICalDuration *
e_cal_component_alarm_repeat_get_interval (const ECalComponentAlarmRepeat *repeat)
{
	g_return_val_if_fail (repeat != NULL, NULL);

	return repeat->interval;
}

/**
 * e_cal_component_alarm_repeat_set_interval:
 * @repeat: an #ECalComponentAlarmRepeat
 * @interval: (not nullable): interval between repetitions, as an #ICalDuration
 *
 * Set the @interval between repetitions of the @repeat.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_repeat_set_interval (ECalComponentAlarmRepeat *repeat,
					   const ICalDuration *interval)
{
	g_return_if_fail (repeat != NULL);
	g_return_if_fail (interval != NULL);

	if (repeat->interval != interval) {
		e_cal_component_alarm_repeat_set_interval_seconds (repeat,
			i_cal_duration_as_int ((ICalDuration *) interval));
	}
}

/**
 * e_cal_component_alarm_repeat_get_interval_seconds:
 * @repeat: an #ECalComponentAlarmRepeat
 *
 * Returns the interval between repetitions of the @repeat in seconds.
 *
 * Returns: the interval between repetitions of the @repeat
 *
 * Since: 3.34
 **/
gint
e_cal_component_alarm_repeat_get_interval_seconds (const ECalComponentAlarmRepeat *repeat)
{
	g_return_val_if_fail (repeat != NULL, 0);

	return i_cal_duration_as_int (repeat->interval);
}

/**
 * e_cal_component_alarm_repeat_set_interval_seconds:
 * @repeat: an #ECalComponentAlarmRepeat
 * @interval_seconds: interval between repetitions, in seconds
 *
 * Set the @interval_seconds between repetitions of the @repeat.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_repeat_set_interval_seconds (ECalComponentAlarmRepeat *repeat,
						   gint interval_seconds)
{
	g_return_if_fail (repeat != NULL);

	if (i_cal_duration_as_int (repeat->interval) != interval_seconds) {
		g_clear_object (&repeat->interval);
		repeat->interval = i_cal_duration_new_from_int (interval_seconds);
	}
}

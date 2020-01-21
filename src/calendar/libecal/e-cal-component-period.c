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
 * SECTION:e-cal-component-period
 * @short_description: An ECalComponentPeriod structure
 * @include: libecal/libecal.h
 *
 * Contains functions to work with the #ECalComponentPeriod structure.
 **/

#include "e-cal-component-period.h"

G_DEFINE_BOXED_TYPE (ECalComponentPeriod, e_cal_component_period, e_cal_component_period_copy, e_cal_component_period_free)

struct _ECalComponentPeriod {
	ECalComponentPeriodKind kind;

	ICalTime *start;

	/* Only one of 'end' and 'duration' can be set, depending on the kind */
	ICalTime *end;
	ICalDuration *duration;
};

/**
 * e_cal_component_period_new_datetime:
 * @start: (not nullable): an #ICalTime, the start of the period
 * @end: (nullable): an #ICalTime, the end of the period
 *
 * Creates a new #ECalComponentPeriod of kind %E_CAL_COMPONENT_PERIOD_DATETIME.
 * The returned structure should be freed with e_cal_component_period_free(),
 * when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentPeriod
 *
 * Since: 3.34
 **/
ECalComponentPeriod *
e_cal_component_period_new_datetime (const ICalTime *start,
				     const ICalTime *end)
{
	ECalComponentPeriod *period;

	g_return_val_if_fail (I_CAL_IS_TIME (start), NULL);

	period = g_slice_new0 (ECalComponentPeriod);
	period->kind = E_CAL_COMPONENT_PERIOD_DATETIME;

	e_cal_component_period_set_datetime_full (period, start, end);

	return period;
}

/**
 * e_cal_component_period_new_duration:
 * @start: (not nullable): an #ICalTime, the start of the period
 * @duration: (not nullable): an #ICalDuration, the duration of the period
 *
 * Creates a new #ECalComponentPeriod of kind %E_CAL_COMPONENT_PERIOD_DURATION.
 * The returned structure should be freed with e_cal_component_period_free(),
 * when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentPeriod
 *
 * Since: 3.34
 **/
ECalComponentPeriod *
e_cal_component_period_new_duration (const ICalTime *start,
				     const ICalDuration *duration)
{
	ECalComponentPeriod *period;

	g_return_val_if_fail (I_CAL_IS_TIME (start), NULL);
	g_return_val_if_fail (I_CAL_IS_DURATION (duration), NULL);

	period = g_slice_new0 (ECalComponentPeriod);
	period->kind = E_CAL_COMPONENT_PERIOD_DURATION;

	e_cal_component_period_set_duration_full (period, start, duration);

	return period;
}

/**
 * e_cal_component_period_copy:
 * @period: (not nullable): an #ECalComponentPeriod to copy
 *
 * Returns: (transfer full): a newly allocated #ECalComponentPeriod, copy of @period.
 *    The returned structure should be freed with e_cal_component_period_free(),
 *    when no longer needed.
 *
 * Since: 3.34
 **/
ECalComponentPeriod *
e_cal_component_period_copy (const ECalComponentPeriod *period)
{
	ECalComponentPeriod *copy = NULL;

	g_return_val_if_fail (period != NULL, NULL);

	switch (e_cal_component_period_get_kind (period)) {
	case E_CAL_COMPONENT_PERIOD_DATETIME:
		copy = e_cal_component_period_new_datetime (
			e_cal_component_period_get_start (period),
			e_cal_component_period_get_end (period));
		break;
	case E_CAL_COMPONENT_PERIOD_DURATION:
		copy = e_cal_component_period_new_duration (
			e_cal_component_period_get_start (period),
			e_cal_component_period_get_duration (period));
		break;
	}

	return copy;
}

/**
 * e_cal_component_period_free: (skip)
 * @period: (type ECalComponentPeriod) (nullable): an #ECalComponentPeriod to free
 *
 * Free the @period, previously allocated by e_cal_component_period_new_datetime(),
 * e_cal_component_period_new_duration() or e_cal_component_period_copy().
 *
 * Since: 3.34
 **/
void
e_cal_component_period_free (gpointer period)
{
	ECalComponentPeriod *pd = period;

	if (pd) {
		g_clear_object (&pd->start);
		g_clear_object (&pd->end);
		g_clear_object (&pd->duration);
		g_slice_free (ECalComponentPeriod, pd);
	}
}

/**
 * e_cal_component_period_get_kind:
 * @period: an #ECalComponentPeriod
 *
 * Returns kind of the @period, one of #ECalComponentPeriodKind. Depending
 * on it either e_cal_component_period_get_end()/e_cal_component_period_set_end()
 * or e_cal_component_period_get_duration()/e_cal_component_period_set_duration()
 * can be used. The kind of an existing @period canbe changed with
 * e_cal_component_period_set_datetime_full() and e_cal_component_period_set_duration_full().
 *
 * Returns: kind of the period, one of #ECalComponentPeriodKind
 *
 * Since: 3.34
 **/
ECalComponentPeriodKind
e_cal_component_period_get_kind	(const ECalComponentPeriod *period)
{
	g_return_val_if_fail (period != NULL, E_CAL_COMPONENT_PERIOD_DATETIME);

	return period->kind;
}

/**
 * e_cal_component_period_set_datetime_full:
 * @period: an #ECalComponentPeriod
 * @start: (not nullable): an #ICalTime, the start of the @period
 * @end: (nullable): an #ICalTime, the end of the @period
 *
 * Set the kind of @period to be %E_CAL_COMPONENT_PERIOD_DATETIME
 * and fills the content with @start and @end.
 *
 * Since: 3.34
 **/
void
e_cal_component_period_set_datetime_full (ECalComponentPeriod *period,
					  const ICalTime *start,
					  const ICalTime *end)
{
	g_return_if_fail (period != NULL);
	g_return_if_fail (I_CAL_IS_TIME (start));

	g_clear_object (&period->duration);

	period->kind = E_CAL_COMPONENT_PERIOD_DATETIME;

	e_cal_component_period_set_start (period, start);
	e_cal_component_period_set_end (period, end);
}

/**
 * e_cal_component_period_set_duration_full:
 * @period: an #ECalComponentPeriod
 * @start: (not nullable): an #ICalTime, the start of the @period
 * @duration: (not nullable): an #ICalDuration, the duration of the @period
 *
 * Set the kind of @period to be %E_CAL_COMPONENT_PERIOD_DURATION
 * and fills the content with @start and @duration.
 *
 * Since: 3.34
 **/
void
e_cal_component_period_set_duration_full (ECalComponentPeriod *period,
					  const ICalTime *start,
					  const ICalDuration *duration)
{
	g_return_if_fail (period != NULL);
	g_return_if_fail (I_CAL_IS_TIME (start));
	g_return_if_fail (I_CAL_IS_DURATION (duration));

	g_clear_object (&period->end);

	period->kind = E_CAL_COMPONENT_PERIOD_DURATION;

	e_cal_component_period_set_start (period, start);
	e_cal_component_period_set_duration (period, duration);
}

/**
 * e_cal_component_period_get_start:
 * @period: an #ECalComponentPeriod
 *
 * Returns the start of the @period. The returned #ICalTime object
 * is owned by @period and should not be freed. It's valid until the @period
 * is freed or its start time changed.
 *
 * Returns: (transfer none): the start of the @period, as an #ICalTime
 *
 * Since: 3.34
 **/
ICalTime *
e_cal_component_period_get_start (const ECalComponentPeriod *period)
{
	g_return_val_if_fail (period != NULL, NULL);

	return period->start;
}

/**
 * e_cal_component_period_set_start:
 * @period: an #ECalComponentPeriod
 * @start: (not nullable): an #ICalTime, the start of the @period
 *
 * Set the @start of the @period. This can be called on any kind of the @period.
 *
 * Since: 3.34
 **/
void
e_cal_component_period_set_start (ECalComponentPeriod *period,
				  const ICalTime *start)
{
	g_return_if_fail (period != NULL);
	g_return_if_fail (I_CAL_IS_TIME (start));

	if (period->start != start) {
		g_clear_object (&period->start);
		period->start = i_cal_time_clone (start);
	}
}

/**
 * e_cal_component_period_get_end:
 * @period: an #ECalComponentPeriod
 *
 * Returns the end of the @period. This can be called only on @period
 * objects of kind %E_CAL_COMPONENT_PERIOD_DATETIME. The end time can
 * be a null-time, in which case the @period corresponds to a single
 * date/date-time value, not to a period.
 *
 * The returned #ICalTime object is owned by @period and should not
 * be freed. It's valid until the @period is freed or its end time changed.
 *
 * Returns: (transfer none) (nullable): the end of the period, as an #ICalTime
 *
 * Since: 3.34
 **/
ICalTime *
e_cal_component_period_get_end (const ECalComponentPeriod *period)
{
	g_return_val_if_fail (period != NULL, NULL);
	g_return_val_if_fail (period->kind == E_CAL_COMPONENT_PERIOD_DATETIME, NULL);

	return period->end;
}

/**
 * e_cal_component_period_set_end:
 * @period: an #ECalComponentPeriod
 * @end: (nullable): an #ICalTime, the end of the @period
 *
 * Set the end of the @period. This can be called only on @period
 * objects of kind %E_CAL_COMPONENT_PERIOD_DATETIME.
 *
 * Since: 3.34
 **/
void
e_cal_component_period_set_end (ECalComponentPeriod *period,
				const ICalTime *end)
{
	g_return_if_fail (period != NULL);
	g_return_if_fail (period->kind == E_CAL_COMPONENT_PERIOD_DATETIME);

	if (period->end != end) {
		g_clear_object (&period->end);
		if (end)
			period->end = i_cal_time_clone (end);
	}
}

/**
 * e_cal_component_period_get_duration:
 * @period: an #ECalComponentPeriod
 *
 * Returns the duration of the @period. This can be called only on @period
 * objects of kind %E_CAL_COMPONENT_PERIOD_DURATION.
 * The returned #ICalDuration object is owned by @period and should not
 * be freed. It's valid until the @period is freed or its duration changed.
 *
 * Returns: (transfer none): the duration of the period, as an #ICalDuration
 *
 * Since: 3.34
 **/
ICalDuration *
e_cal_component_period_get_duration (const ECalComponentPeriod *period)
{
	g_return_val_if_fail (period != NULL, NULL);
	g_return_val_if_fail (period->kind == E_CAL_COMPONENT_PERIOD_DURATION, NULL);

	return period->duration;
}

/**
 * e_cal_component_period_set_duration:
 * @period: an #ECalComponentPeriod
 * @duration: (not nullable): an #ICalDuration, the duration of the @period
 *
 * Set the duration of the @period. This can be called only on @period
 * objects of kind %E_CAL_COMPONENT_PERIOD_DURATION.
 *
 * Since: 3.34
 **/
void
e_cal_component_period_set_duration (ECalComponentPeriod *period,
				     const ICalDuration *duration)
{
	g_return_if_fail (period != NULL);
	g_return_if_fail (period->kind == E_CAL_COMPONENT_PERIOD_DURATION);
	g_return_if_fail (I_CAL_IS_DURATION (duration));

	if (period->duration != duration) {
		g_clear_object (&period->duration);
		period->duration = i_cal_duration_new_from_int (i_cal_duration_as_int ((ICalDuration *) duration));
	}
}

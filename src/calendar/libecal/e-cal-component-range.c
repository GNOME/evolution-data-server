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
 * SECTION:e-cal-component-range
 * @short_description: An ECalComponentRange structure
 * @include: libecal/libecal.h
 *
 * Contains functions to work with the #ECalComponentRange structure.
 **/

#include "e-cal-component-range.h"

G_DEFINE_BOXED_TYPE (ECalComponentRange, e_cal_component_range, e_cal_component_range_copy, e_cal_component_range_free)

struct _ECalComponentRange {
	ECalComponentRangeKind kind;
	ECalComponentDateTime *datetime;
};

/**
 * e_cal_component_range_new:
 * @kind: an #ECalComponentRangeKind
 * @datetime: (not nullable): an #ECalComponentDateTime
 *
 * Creates a new #ECalComponentRange describing a range.
 * The returned structure should be freed with e_cal_component_range_free(),
 * when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentRange
 *
 * Since: 3.34
 **/
ECalComponentRange *
e_cal_component_range_new (ECalComponentRangeKind kind,
			   const ECalComponentDateTime *datetime)
{
	ECalComponentDateTime *dt;

	g_return_val_if_fail (datetime != NULL, NULL);

	dt = e_cal_component_datetime_copy (datetime);
	g_return_val_if_fail (dt != NULL, NULL);

	return e_cal_component_range_new_take (kind, dt);
}

/**
 * e_cal_component_range_new_take: (skip)
 * @kind: an #ECalComponentRangeKind
 * @datetime: (not nullable) (transfer full): an #ECalComponentDateTime
 *
 * Creates a new #ECalComponentRange describing a range, similar to
 * e_cal_component_range_new() except is assumes ownership of @datetime.
 * The returned structure should be freed with e_cal_component_range_free(),
 * when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentRange
 *
 * Since: 3.34
 **/
ECalComponentRange *
e_cal_component_range_new_take (ECalComponentRangeKind kind,
				ECalComponentDateTime *datetime)
{
	ECalComponentRange *range;

	g_return_val_if_fail (datetime != NULL, NULL);

	range = g_new0 (ECalComponentRange, 1);
	range->kind = kind;
	range->datetime = datetime;

	return range;
}

/**
 * e_cal_component_range_copy:
 * @range: (not nullable): an #ECalComponentRange to copy
 *
 * Returns: (transfer full): a newly allocated #ECalComponentRange, copy of @range.
 *    The returned structure should be freed with e_cal_component_range_free(),
 *    when no longer needed.
 *
 * Since: 3.34
 **/
ECalComponentRange *
e_cal_component_range_copy (const ECalComponentRange *range)
{
	g_return_val_if_fail (range != NULL, NULL);

	return e_cal_component_range_new (
		e_cal_component_range_get_kind (range),
		e_cal_component_range_get_datetime (range));
}

/**
 * e_cal_component_range_free: (skip)
 * @range: (type ECalComponentRange) (nullable): an #ECalComponentRange to free
 *
 * Free the @range, previously allocated by e_cal_component_range_new(),
 * e_cal_component_range_new_take() or e_cal_component_range_copy().
 *
 * Since: 3.34
 **/
void
e_cal_component_range_free (gpointer range)
{
	ECalComponentRange *rng = range;

	if (rng) {
		e_cal_component_datetime_free (rng->datetime);
		g_free (rng);
	}
}

/**
 * e_cal_component_range_get_kind:
 * @range: an #ECalComponentRange
 *
 * Returns: the #ECalComponentRangeKind of the @range
 *
 * Since: 3.34
 **/
ECalComponentRangeKind
e_cal_component_range_get_kind (const ECalComponentRange *range)
{
	g_return_val_if_fail (range != NULL, E_CAL_COMPONENT_RANGE_SINGLE);

	return range->kind;
}

/**
 * e_cal_component_range_set_kind:
 * @range: an #ECalComponentRange
 * @kind: an #ECalComponentRangeKind
 *
 * Set the @kind of the @range.
 *
 * Since: 3.34
 **/
void
e_cal_component_range_set_kind (ECalComponentRange *range,
				ECalComponentRangeKind kind)
{
	g_return_if_fail (range != NULL);

	range->kind = kind;
}

/**
 * e_cal_component_range_get_datetime:
 * @range: an #ECalComponentRange
 *
 * Returns the date/time of the @range. The returned #ECalComponentDateTime
 * is owned by @range and should not be freed. It's valid until the @range
 * is freed or its date/time changed.
 *
 * Returns: (transfer none): the date/time of the @range, as an #ECalComponentDateTime
 *
 * Since: 3.34
 **/
ECalComponentDateTime *
e_cal_component_range_get_datetime (const ECalComponentRange *range)
{
	g_return_val_if_fail (range != NULL, NULL);

	return range->datetime;
}

/**
 * e_cal_component_range_set_datetime:
 * @range: an #ECalComponentRange
 * @datetime: (not nullable): an #ECalComponentDateTime
 *
 * Set the date/time part of the @range.
 *
 * Since: 3.34
 **/
void
e_cal_component_range_set_datetime (ECalComponentRange *range,
				    const ECalComponentDateTime *datetime)
{
	g_return_if_fail (range != NULL);
	g_return_if_fail (datetime != NULL);

	if (range->datetime != datetime) {
		ECalComponentDateTime *dt;

		dt = e_cal_component_datetime_copy (datetime);
		g_return_if_fail (dt != NULL);

		e_cal_component_range_take_datetime (range, dt);
	}
}

/**
 * e_cal_component_range_take_datetime: (skip)
 * @range: an #ECalComponentRange
 * @datetime: (not nullable) (transfer full): an #ECalComponentDateTime
 *
 * Set the date/time part of the @range, similar to e_cal_component_range_set_datetime(),
 * except it assumes ownership of the @datetime.
 *
 * Since: 3.34
 **/
void
e_cal_component_range_take_datetime (ECalComponentRange *range,
				     ECalComponentDateTime *datetime)
{
	g_return_if_fail (range != NULL);
	g_return_if_fail (datetime != NULL);

	if (range->datetime != datetime) {
		e_cal_component_datetime_free (range->datetime);
		range->datetime = datetime;
	}
}

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
 * SECTION:e-cal-component-datetime
 * @short_description: An ECalComponentDateTime structure
 * @include: libecal/libecal.h
 *
 * Contains functions to work with the #ECalComponentDateTime structure.
 **/

#include "e-cal-component-datetime.h"

G_DEFINE_BOXED_TYPE (ECalComponentDateTime, e_cal_component_datetime, e_cal_component_datetime_copy, e_cal_component_datetime_free)

struct _ECalComponentDateTime {
	/* Actual date/time value */
	ICalTime *value;

	/* Timezone ID */
	gchar *tzid;
};

/**
 * e_cal_component_datetime_new:
 * @value: (not nullable): an #ICalTime as a value
 * @tzid: (nullable): timezone ID for the @value, or %NULL
 *
 * Creates a new #ECalComponentDateTime instance, which holds
 * the @value and @tzid. The returned structure should be freed
 * with e_cal_component_datetime_free(), when no longer needed.
 *
 * Returns: (transfer full): a new #ECalComponentDateTime
 *
 * Since: 3.34
 **/
ECalComponentDateTime *
e_cal_component_datetime_new (const ICalTime *value,
			      const gchar *tzid)
{
	ECalComponentDateTime *dt;

	g_return_val_if_fail (I_CAL_IS_TIME (value), NULL);

	dt = g_slice_new0 (ECalComponentDateTime);
	e_cal_component_datetime_set (dt, value, tzid);

	return dt;
}

/**
 * e_cal_component_datetime_new_take:
 * @value: (transfer full) (not nullable): an #ICalTime as a value
 * @tzid: (transfer full) (nullable): timezone ID for the @value, or %NULL
 *
 * Creates a new #ECalComponentDateTime instance, which holds
 * the @value and @tzid. It is similar to e_cal_component_datetime_new(),
 * except this function assumes ownership of the @value and @tzid.
 * The returned structure should be freed with e_cal_component_datetime_free(),
 * when no longer needed.
 *
 * Returns: (transfer full): a new #ECalComponentDateTime
 *
 * Since: 3.34
 **/
ECalComponentDateTime *
e_cal_component_datetime_new_take (ICalTime *value,
				   gchar *tzid)
{
	ECalComponentDateTime *dt;

	g_return_val_if_fail (I_CAL_IS_TIME (value), NULL);

	dt = g_slice_new0 (ECalComponentDateTime);
	dt->value = value;
	dt->tzid = tzid;

	return dt;
}

/**
 * e_cal_component_datetime_copy:
 * @dt: (not nullable): an #ECalComponentDateTime
 *
 * Creates a new copy of @dt. The returned structure should be freed
 * with e_cal_component_datetime_free() when no longer needed.
 *
 * Returns: (transfer full): a new #ECalComponentDateTime, copy of @dt
 *
 * Since: 3.34
 **/
ECalComponentDateTime *
e_cal_component_datetime_copy (const ECalComponentDateTime *dt)
{
	g_return_val_if_fail (dt != NULL, NULL);

	return e_cal_component_datetime_new (
		e_cal_component_datetime_get_value (dt),
		e_cal_component_datetime_get_tzid (dt));
}

/**
 * e_cal_component_datetime_free: (skip)
 * @dt: (type ECalComponentDateTime) (nullable): an #ECalComponentDateTime to free
 *
 * Free @dt, previously created by e_cal_component_datetime_new(),
 * e_cal_component_datetime_new_take() or e_cal_component_datetime_copy().
 * The function does nothing, if @dt is %NULL.
 *
 * Since: 3.34
 **/
void
e_cal_component_datetime_free (gpointer dt)
{
	ECalComponentDateTime *pdt = dt;

	if (pdt) {
		g_clear_object (&pdt->value);
		g_free (pdt->tzid);
		g_slice_free (ECalComponentDateTime, pdt);
	}
}

/**
 * e_cal_component_datetime_set:
 * @dt: an #ECalComponentDateTime
 * @value: (not nullable): an #ICalTime as a value
 * @tzid: (nullable): timezone ID for the @value, or %NULL
 *
 * Sets both @value and @tzid in one call. Use e_cal_component_datetime_set_value()
 * and e_cal_component_datetime_set_tzid() to set them separately.
 *
 * Since: 3.34
 **/
void
e_cal_component_datetime_set (ECalComponentDateTime *dt,
			      const ICalTime *value,
			      const gchar *tzid)
{
	g_return_if_fail (dt != NULL);
	g_return_if_fail (I_CAL_IS_TIME (value));

	e_cal_component_datetime_set_value (dt, value);
	e_cal_component_datetime_set_tzid (dt, tzid);
}

/**
 * e_cal_component_datetime_get_value:
 * @dt: an #ECalComponentDateTime
 *
 * Returns the value stored with the @dt. The object is owned by @dt and
 * it's valid until the @dt is freed or its value overwritten.
 *
 * Returns: (transfer none): a value of @dt, as an #ICalTime
 *
 * Since: 3.34
 **/
ICalTime *
e_cal_component_datetime_get_value (const ECalComponentDateTime *dt)
{
	g_return_val_if_fail (dt != NULL, NULL);

	return dt->value;
}

/**
 * e_cal_component_datetime_set_value:
 * @dt: an #ECalComponentDateTime
 * @value: (not nullable): the value to set, as an #ICalTime
 *
 * Sets the @value of the @dt. Any previously set value is freed.
 *
 * Since: 3.34
 **/
void
e_cal_component_datetime_set_value (ECalComponentDateTime *dt,
				    const ICalTime *value)
{
	g_return_if_fail (dt != NULL);
	g_return_if_fail (I_CAL_IS_TIME (value));

	if (dt->value != value) {
		g_clear_object (&dt->value);
		dt->value = i_cal_time_clone (value);
	}
}

/**
 * e_cal_component_datetime_take_value:
 * @dt: an #ECalComponentDateTime
 * @value: (not nullable) (transfer full): the value to take, as an #ICalTime
 *
 * Sets the @value of the @dt and assumes ownership of the @value.
 * Any previously set value is freed.
 *
 * Since: 3.34
 **/
void
e_cal_component_datetime_take_value (ECalComponentDateTime *dt,
				     ICalTime *value)
{
	g_return_if_fail (dt != NULL);
	g_return_if_fail (I_CAL_IS_TIME (value));

	if (dt->value != value) {
		g_clear_object (&dt->value);
		dt->value = value;
	}
}

/**
 * e_cal_component_datetime_get_tzid:
 * @dt: an #ECalComponentDateTime
 *
 * Returns the TZID stored with the @dt. The string is owned by @dt and
 * it's valid until the @dt is freed or its TZID overwritten. It never
 * returns an empty string, it returns either set TZID parameter value
 * or %NULL, when none is set.
 *
 * Returns: (transfer none) (nullable): a TZID of @dt, or %NULL
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_datetime_get_tzid (const ECalComponentDateTime *dt)
{
	g_return_val_if_fail (dt != NULL, NULL);

	return dt->tzid;
}

/**
 * e_cal_component_datetime_set_tzid:
 * @dt: an #ECalComponentDateTime
 * @tzid: (nullable): the TZID to set, or %NULL
 *
 * Sets the @tzid of the @dt. Any previously set TZID is freed.
 * An empty string or a %NULL as @tzid is treated as none TZID.
 *
 * Since: 3.34
 **/
void
e_cal_component_datetime_set_tzid (ECalComponentDateTime *dt,
				   const gchar *tzid)
{
	g_return_if_fail (dt != NULL);

	if (tzid && !*tzid)
		tzid = NULL;

	if (tzid != dt->tzid) {
		g_free (dt->tzid);
		dt->tzid = g_strdup (tzid);
	}
}

/**
 * e_cal_component_datetime_take_tzid:
 * @dt: an #ECalComponentDateTime
 * @tzid: (nullable) (transfer full): the TZID to take, or %NULL
 *
 * Sets the @tzid of the @dt and assumes ownership of @tzid. Any previously
 * set TZID is freed. An empty string or a %NULL as @tzid is treated as none TZID.
 *
 * Since: 3.34
 **/
void
e_cal_component_datetime_take_tzid (ECalComponentDateTime *dt,
				    gchar *tzid)
{
	g_return_if_fail (dt != NULL);

	if (tzid && !*tzid) {
		g_free (tzid);
		tzid = NULL;
	}

	if (tzid != dt->tzid) {
		g_free (dt->tzid);
		dt->tzid = tzid;
	}
}

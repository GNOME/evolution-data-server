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
 * SECTION:e-cal-component-alarm-instance
 * @short_description: An ECalComponentAlarmInstance structure
 * @include: libecal/libecal.h
 *
 * Contains functions to work with the #ECalComponentAlarmInstance structure.
 **/

#include "e-cal-component-alarm-instance.h"

G_DEFINE_BOXED_TYPE (ECalComponentAlarmInstance, e_cal_component_alarm_instance, e_cal_component_alarm_instance_copy, e_cal_component_alarm_instance_free)

struct _ECalComponentAlarmInstance {
	/* UID of the alarm that instanceed */
	gchar *uid;

	/* recurrence ID of the component the alarm was generated from */
	gchar *rid;

	/* Instance time, i.e. "5 minutes before the appointment" */
	time_t instance_time;

	/* Actual event occurrence to which this instance corresponds */
	time_t occur_start;
	time_t occur_end;
};

/**
 * e_cal_component_alarm_instance_new:
 * @uid: (not nullable): UID of the alarm
 * @instance_time: instance time, i.e. "5 minutes before the appointment"
 * @occur_start: actual event occurrence start to which this instance corresponds
 * @occur_end: actual event occurrence end to which this instance corresponds
 *
 * Creates a new #ECalComponentAlarmInstance structure, filled with the given values.
 * Free the instance with e_cal_component_alarm_instance_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentAlarmInstance
 *
 * Since: 3.34
 **/
ECalComponentAlarmInstance *
e_cal_component_alarm_instance_new (const gchar *uid,
				    time_t instance_time,
				    time_t occur_start,
				    time_t occur_end)
{
	ECalComponentAlarmInstance *instance;

	g_return_val_if_fail (uid != NULL, NULL);

	instance = g_slice_new0 (ECalComponentAlarmInstance);
	instance->uid = g_strdup (uid);
	instance->rid = NULL;
	instance->instance_time = instance_time;
	instance->occur_start = occur_start;
	instance->occur_end = occur_end;

	return instance;
}

/**
 * e_cal_component_alarm_instance_copy:
 * @instance: (not nullable): an #ECalComponentAlarmInstance
 *
 * Returns a newly allocated copy of @instance, which should be freed with
 * e_cal_component_alarm_instance_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated copy of @instance
 *
 * Since: 3.34
 **/
ECalComponentAlarmInstance *
e_cal_component_alarm_instance_copy (const ECalComponentAlarmInstance *instance)
{
	ECalComponentAlarmInstance *instnc;

	g_return_val_if_fail (instance != NULL, NULL);

	instnc = e_cal_component_alarm_instance_new (instance->uid,
		instance->instance_time,
		instance->occur_start,
		instance->occur_end);

	e_cal_component_alarm_instance_set_rid (instnc,
		e_cal_component_alarm_instance_get_rid (instance));

	return instnc;
}

/**
 * e_cal_component_alarm_instance_free: (skip)
 * @instance: (type ECalComponentAlarmInstance) (nullable): an #ECalComponentAlarmInstance to free
 *
 * Free @instance, previously created by e_cal_component_alarm_instance_new()
 * or e_cal_component_alarm_instance_copy(). The function does nothing, if @instance
 * is %NULL.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_instance_free (gpointer instance)
{
	ECalComponentAlarmInstance *instnc = instance;

	if (instnc) {
		g_free (instnc->uid);
		g_free (instnc->rid);
		g_slice_free (ECalComponentAlarmInstance, instnc);
	}
}

/**
 * e_cal_component_alarm_instance_get_uid:
 * @instance: an #ECalComponentAlarmInstance
 *
 * Returns: alarm UID, to which this @instance corresponds
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_alarm_instance_get_uid (const ECalComponentAlarmInstance *instance)
{
	g_return_val_if_fail (instance != NULL, NULL);

	return instance->uid;
}

/**
 * e_cal_component_alarm_instance_set_uid:
 * @instance: an #ECalComponentAlarmInstance
 * @uid: (not nullable): alarm UID to set
 *
 * Set the alarm UID.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_instance_set_uid (ECalComponentAlarmInstance *instance,
					const gchar *uid)
{
	g_return_if_fail (instance != NULL);
	g_return_if_fail (uid != NULL);

	if (g_strcmp0 (instance->uid, uid) != 0) {
		g_free (instance->uid);
		instance->uid = g_strdup (uid);
	}
}

/**
 * e_cal_component_alarm_instance_get_rid:
 * @instance: an #ECalComponentAlarmInstance
 *
 * Returns: (nullable): the Recurrence ID of the component this @instance was generated for.
 *
 * Since: 3.40
 **/
const gchar *
e_cal_component_alarm_instance_get_rid (const ECalComponentAlarmInstance *instance)
{
	g_return_val_if_fail (instance != NULL, NULL);

	return instance->rid;
}

/**
 * e_cal_component_alarm_instance_set_rid:
 * @instance: an #ECalComponentAlarmInstance
 * @rid: (nullable): recurrence UID to set, or %NULL
 *
 * Set the Recurrence ID of the component this @instance was generated for.
 *
 * Since: 3.40
 **/
void
e_cal_component_alarm_instance_set_rid (ECalComponentAlarmInstance *instance,
					const gchar *rid)
{
	g_return_if_fail (instance != NULL);

	if (rid && !*rid)
		rid = NULL;

	if (g_strcmp0 (instance->rid, rid) != 0) {
		g_free (instance->rid);
		instance->rid = g_strdup (rid);
	}
}

/**
 * e_cal_component_alarm_instance_get_time:
 * @instance: an #ECalComponentAlarmInstance
 *
 * Returns: alarm instance time, i.e. "5 minutes before the appointment"
 *
 * Since: 3.34
 **/
time_t
e_cal_component_alarm_instance_get_time (const ECalComponentAlarmInstance *instance)
{
	g_return_val_if_fail (instance != NULL, (time_t) 0);

	return instance->instance_time;
}

/**
 * e_cal_component_alarm_instance_set_time:
 * @instance: an ECalComponentAlarmInstance
 * @instance_time: instance time to set
 *
 * Set the instance time, i.e. "5 minutes before the appointment".
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_instance_set_time (ECalComponentAlarmInstance *instance,
					 time_t instance_time)
{
	g_return_if_fail (instance != NULL);

	if (instance->instance_time != instance_time) {
		instance->instance_time = instance_time;
	}
}

/**
 * e_cal_component_alarm_instance_get_occur_start:
 * @instance: an #ECalComponentAlarmInstance
 *
 * Returns: actual event occurrence start to which this @instance corresponds
 *
 * Since: 3.34
 **/
time_t
e_cal_component_alarm_instance_get_occur_start (const ECalComponentAlarmInstance *instance)
{
	g_return_val_if_fail (instance != NULL, (time_t) 0);

	return instance->occur_start;
}

/**
 * e_cal_component_alarm_instance_set_occur_start:
 * @instance: an ECalComponentAlarmInstance
 * @occur_start: event occurence start to set
 *
 * Set the actual event occurrence start to which this @instance corresponds.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_instance_set_occur_start (ECalComponentAlarmInstance *instance,
						time_t occur_start)
{
	g_return_if_fail (instance != NULL);

	if (instance->occur_start != occur_start) {
		instance->occur_start = occur_start;
	}
}

/**
 * e_cal_component_alarm_instance_get_occur_end:
 * @instance: an #ECalComponentAlarmInstance
 *
 * Returns: actual event occurrence end to which this @instance corresponds
 *
 * Since: 3.34
 **/
time_t
e_cal_component_alarm_instance_get_occur_end (const ECalComponentAlarmInstance *instance)
{
	g_return_val_if_fail (instance != NULL, (time_t) 0);

	return instance->occur_end;
}

/**
 * e_cal_component_alarm_instance_set_occur_end:
 * @instance: an ECalComponentAlarmInstance
 * @occur_end: event occurence end to set
 *
 * Set the actual event occurrence end to which this @instance corresponds.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarm_instance_set_occur_end (ECalComponentAlarmInstance *instance,
					      time_t occur_end)
{
	g_return_if_fail (instance != NULL);

	if (instance->occur_end != occur_end) {
		instance->occur_end = occur_end;
	}
}

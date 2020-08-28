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
 * SECTION:e-cal-component-alarms
 * @short_description: An ECalComponentAlarms structure
 * @include: libecal/libecal.h
 *
 * Contains functions to work with the #ECalComponentAlarms structure.
 **/

#include "e-cal-component.h"

#include "e-cal-component-alarms.h"

G_DEFINE_BOXED_TYPE (ECalComponentAlarms, e_cal_component_alarms, e_cal_component_alarms_copy, e_cal_component_alarms_free)

struct _ECalComponentAlarms {
	/* The actual component */
	ECalComponent *comp;

	/* List of ECalComponentAlarmInstance structures */
	GSList *instances;
};

/**
 * e_cal_component_alarms_new:
 * @comp: (type ECalComponent) (not nullable): the actual alarm component, as #ECalComponent
 *
 * Creates a new #ECalComponentAlarms structure, associated with @comp.
 * Free the alarms with e_cal_component_alarms_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentAlarms
 *
 * Since: 3.34
 **/
ECalComponentAlarms *
e_cal_component_alarms_new (ECalComponent *comp)
{
	ECalComponentAlarms *alarms;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);

	alarms = g_slice_new0 (ECalComponentAlarms);
	alarms->comp = g_object_ref (comp);

	return alarms;
}

/**
 * e_cal_component_alarms_copy:
 * @alarms: (not nullable): an #ECalComponentAlarms
 *
 * Returns a newly allocated copy of @alarms, which should be freed with
 * e_cal_component_alarms_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated copy of @alarms
 *
 * Since: 3.34
 **/
ECalComponentAlarms *
e_cal_component_alarms_copy (const ECalComponentAlarms *alarms)
{
	ECalComponentAlarms *alrms;

	g_return_val_if_fail (alarms != NULL, NULL);

	alrms = e_cal_component_alarms_new (alarms->comp);
	alrms->instances = g_slist_copy_deep (alarms->instances, (GCopyFunc) e_cal_component_alarm_instance_copy, NULL);

	return alrms;
}

/**
 * e_cal_component_alarms_free: (skip)
 * @alarms: (type ECalComponentAlarms) (nullable): an #ECalComponentAlarms to free
 *
 * Free @alarms, previously created by e_cal_component_alarms_new()
 * or e_cal_component_alarms_copy(). The function does nothing, if @alarms
 * is %NULL.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarms_free (gpointer alarms)
{
	ECalComponentAlarms *alrms = alarms;

	if (alrms) {
		g_clear_object (&alrms->comp);
		g_slist_free_full (alrms->instances, e_cal_component_alarm_instance_free);
		g_slice_free (ECalComponentAlarms, alrms);
	}
}

/**
 * e_cal_component_alarms_get_component:
 * @alarms: an #ECalComponentAlarms
 *
 * The returned component is valid until the @alarms is freed.
 *
 * Returns: (type ECalComponent) (transfer none): an #ECalComponent associated with the @alarms structure
 *
 * Since: 3.34
 **/
ECalComponent *
e_cal_component_alarms_get_component (const ECalComponentAlarms *alarms)
{
	g_return_val_if_fail (alarms != NULL, NULL);

	return alarms->comp;
}

/**
 * e_cal_component_alarms_get_instances:
 * @alarms: an #ECalComponentAlarms
 *
 * The returned #GSList is owned by @alarms and should not be modified.
 * It's valid until the @alarms is freed or the list of instances is not
 * modified by other functions. The items are of type #ECalComponentAlarmInstance.
 *
 * Returns: (transfer none) (element-type ECalComponentAlarmInstance) (nullable): instances
 *    of the @alarms structure; can be %NULL, when none had been added yet
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_alarms_get_instances (const ECalComponentAlarms *alarms)
{
	g_return_val_if_fail (alarms != NULL, NULL);

	return alarms->instances;
}

/**
 * e_cal_component_alarms_set_instances:
 * @alarms: an #ECalComponentAlarms
 * @instances: (nullable) (element-type ECalComponentAlarmInstance): #ECalComponentAlarmInstance objects to set
 *
 * Modifies the list of instances to copy of the given @instances.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarms_set_instances (ECalComponentAlarms *alarms,
				      const GSList *instances)
{
	GSList *copy;

	g_return_if_fail (alarms != NULL);

	copy = g_slist_copy_deep ((GSList *) instances, (GCopyFunc) e_cal_component_alarm_instance_copy, NULL);
	g_slist_free_full (alarms->instances, e_cal_component_alarm_instance_free);
	alarms->instances = copy;
}

/**
 * e_cal_component_alarms_take_instances:
 * @alarms: an #ECalComponentAlarms
 * @instances: (nullable) (transfer full) (element-type ECalComponentAlarmInstance): #ECalComponentAlarmInstance objects to take
 *
 * Replaces the list of instances with the given @instances and
 * assumes ownership of it. Neither the #GSList, nor its items, should
 * contain the same structures.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarms_take_instances (ECalComponentAlarms *alarms,
				       GSList *instances)
{
	g_return_if_fail (alarms != NULL);

	g_slist_free_full (alarms->instances, e_cal_component_alarm_instance_free);
	alarms->instances = instances;
}

/**
 * e_cal_component_alarms_add_instance:
 * @alarms: an #ECalComponentAlarms
 * @instance: (not nullable): an #ECalComponentAlarmInstance
 *
 * Add a copy of @instance into the list of instances. It is added
 * in no particular order.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarms_add_instance (ECalComponentAlarms *alarms,
				     const ECalComponentAlarmInstance *instance)
{
	g_return_if_fail (alarms != NULL);
	g_return_if_fail (instance != NULL);

	alarms->instances = g_slist_prepend (alarms->instances,
		e_cal_component_alarm_instance_copy (instance));
}

/**
 * e_cal_component_alarms_take_instance:
 * @alarms: an #ECalComponentAlarms
 * @instance: (not nullable) (transfer full): an #ECalComponentAlarmInstance
 *
 * Add the @instance into the list of instances and assume ownership of it.
 * It is added in no particular order.
 *
 * Since: 3.34
 **/
void
e_cal_component_alarms_take_instance (ECalComponentAlarms *alarms,
				      ECalComponentAlarmInstance *instance)
{
	g_return_if_fail (alarms != NULL);
	g_return_if_fail (instance != NULL);

	alarms->instances = g_slist_prepend (alarms->instances, instance);
}

/**
 * e_cal_component_alarms_remove_instance:
 * @alarms: an #ECalComponentAlarms
 * @instance: (not nullable): an #ECalComponentAlarmInstance
 *
 * Remove the @instance from the list of instances. If found, the @instance
 * is also freed.
 *
 * Returns: whether the @instance had been found and freed
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_alarms_remove_instance (ECalComponentAlarms *alarms,
					const ECalComponentAlarmInstance *instance)
{
	GSList *found;

	g_return_val_if_fail (alarms != NULL, FALSE);
	g_return_val_if_fail (instance != NULL, FALSE);

	found = g_slist_find (alarms->instances, instance);
	if (found) {
		alarms->instances = g_slist_remove (alarms->instances, instance);
		e_cal_component_alarm_instance_free ((ECalComponentAlarmInstance *) instance);
	}

	return found != NULL;
}

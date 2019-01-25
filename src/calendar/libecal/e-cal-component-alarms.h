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

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_COMPONENT_ALARMS_H
#define E_CAL_COMPONENT_ALARMS_H

#include <glib-object.h>
#include <libecal/e-cal-component-alarm-instance.h>

G_BEGIN_DECLS

/* Forward declaration */
struct _ECalComponent;

/**
 * ECalComponentAlarms:
 *
 * Opaque structure, which represents alarm trigger instances for a particular component.
 * Use the functions below to work with it.
 **/
typedef struct _ECalComponentAlarms ECalComponentAlarms;

GType		e_cal_component_alarms_get_type	(void);
ECalComponentAlarms *
		e_cal_component_alarms_new	(struct _ECalComponent *comp);
ECalComponentAlarms *
		e_cal_component_alarms_copy	(const ECalComponentAlarms *alarms);
void		e_cal_component_alarms_free	(gpointer alarms); /* ECalComponentAlarms * */
struct _ECalComponent *
		e_cal_component_alarms_get_component
						(const ECalComponentAlarms *alarms);
GSList *	e_cal_component_alarms_get_instances /* ECalComponentAlarmInstance * */
						(const ECalComponentAlarms *alarms);
void		e_cal_component_alarms_set_instances
						(ECalComponentAlarms *alarms,
						 const GSList *instances); /* ECalComponentAlarmInstance * */
void		e_cal_component_alarms_take_instances
						(ECalComponentAlarms *alarms,
						 GSList *instances); /* ECalComponentAlarmInstance * */
void		e_cal_component_alarms_add_instance
						(ECalComponentAlarms *alarms,
						 const ECalComponentAlarmInstance *instance);
void		e_cal_component_alarms_take_instance
						(ECalComponentAlarms *alarms,
						 ECalComponentAlarmInstance *instance);
gboolean	e_cal_component_alarms_remove_instance
						(ECalComponentAlarms *alarms,
						 const ECalComponentAlarmInstance *instance);

G_END_DECLS

#endif /* E_CAL_COMPONENT_ALARMS_H */

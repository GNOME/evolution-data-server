/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileCopyrightText: (C) 2019 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
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

/*
 * SPDX-FileCopyrightText: (C) 2014 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Milan Crha <mcrha@redhat.com>
 */

#ifndef E_CAL_BACKEND_GTASKS_H
#define E_CAL_BACKEND_GTASKS_H

#include <libedata-cal/libedata-cal.h>

/* Standard GObject macros */
#define E_TYPE_CAL_BACKEND_GTASKS \
	(e_cal_backend_gtasks_get_type ())
#define E_CAL_BACKEND_GTASKS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_BACKEND_GTASKS, ECalBackendGTasks))
#define E_CAL_BACKEND_GTASKS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_BACKEND_GTASKS, ECalBackendGTasksClass))
#define E_IS_CAL_BACKEND_GTASKS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_BACKEND_GTASKS))
#define E_IS_CAL_BACKEND_GTASKS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_BACKEND_GTASKS))
#define E_CAL_BACKEND_GTASKS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CAL_BACKEND_GTASKS, ECalBackendGTasksClass))

G_BEGIN_DECLS

typedef struct _ECalBackendGTasks ECalBackendGTasks;
typedef struct _ECalBackendGTasksClass ECalBackendGTasksClass;
typedef struct _ECalBackendGTasksPrivate ECalBackendGTasksPrivate;

struct _ECalBackendGTasks {
	ECalMetaBackend parent;
	ECalBackendGTasksPrivate *priv;
};

struct _ECalBackendGTasksClass {
	ECalMetaBackendClass parent_class;
};

GType		e_cal_backend_gtasks_get_type	(void);

G_END_DECLS

#endif /* E_CAL_BACKEND_GTASKS_H */

/*
 * Copyright (C) 2014 Red Hat, Inc. (www.redhat.com)
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
 * Authors: Milan Crha <mcrha@redhat.com>
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
	ECalBackend parent;
	ECalBackendGTasksPrivate *priv;
};

struct _ECalBackendGTasksClass {
	ECalBackendClass parent_class;
};

GType		e_cal_backend_gtasks_get_type	(void);

G_END_DECLS

#endif /* E_CAL_BACKEND_GTASKS_H */

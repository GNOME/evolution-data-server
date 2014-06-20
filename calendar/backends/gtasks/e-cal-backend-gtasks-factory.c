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

#include <config.h>

#include "e-cal-backend-gtasks.h"

#define FACTORY_NAME "gtasks"

typedef ECalBackendFactory ECalBackendGTasksFactory;
typedef ECalBackendFactoryClass ECalBackendGTasksFactoryClass;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_cal_backend_gtasks_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendGTasksFactory,
	e_cal_backend_gtasks_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

static void
e_cal_backend_gtasks_factory_class_init (ECalBackendFactoryClass *class)
{
	class->factory_name = FACTORY_NAME;
	class->component_kind = ICAL_VTODO_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_GTASKS;
}

static void
e_cal_backend_gtasks_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_gtasks_factory_init (ECalBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_cal_backend_gtasks_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

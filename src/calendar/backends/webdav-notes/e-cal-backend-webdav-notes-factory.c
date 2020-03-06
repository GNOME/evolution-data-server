/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
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
 */

#include "evolution-data-server-config.h"

#include "e-cal-backend-webdav-notes.h"

typedef ECalBackendFactory ECalBackendWebDAVNotesFactory;
typedef ECalBackendFactoryClass ECalBackendWebDAVNotesFactoryClass;

static EModule *e_module;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_cal_backend_webdav_notes_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (ECalBackendWebDAVNotesFactory, e_cal_backend_webdav_notes_factory, E_TYPE_CAL_BACKEND_FACTORY)

static void
e_cal_backend_webdav_notes_factory_class_init (ECalBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = "webdav-notes";
	class->component_kind = I_CAL_VJOURNAL_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_WEBDAV_NOTES;
}

static void
e_cal_backend_webdav_notes_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_webdav_notes_factory_init (ECalBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_module = E_MODULE (type_module);

	e_cal_backend_webdav_notes_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
	e_module = NULL;
}

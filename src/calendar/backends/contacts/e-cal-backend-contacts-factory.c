/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Chris Toshok (toshok@ximian.com)
 */

#include "evolution-data-server-config.h"

#include "e-cal-backend-contacts.h"

#define FACTORY_NAME "contacts"

typedef ECalBackendFactory ECalBackendContactsEventsFactory;
typedef ECalBackendFactoryClass ECalBackendContactsEventsFactoryClass;

static EModule *e_module;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_cal_backend_contacts_events_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendContactsEventsFactory,
	e_cal_backend_contacts_events_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

static void
e_cal_backend_contacts_events_factory_class_init (ECalBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->component_kind = I_CAL_VEVENT_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_CONTACTS;

}

static void
e_cal_backend_contacts_events_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_contacts_events_factory_init (ECalBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_module = E_MODULE (type_module);

	e_cal_backend_contacts_events_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
	e_module = NULL;
}


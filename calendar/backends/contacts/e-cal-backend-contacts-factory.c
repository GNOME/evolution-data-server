/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#include <config.h>

#include "e-cal-backend-contacts.h"
#include "e-source-contacts.h"

#define FACTORY_NAME "contacts"

typedef ECalBackendFactory ECalBackendContactsEventsFactory;
typedef ECalBackendFactoryClass ECalBackendContactsEventsFactoryClass;

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
	class->factory_name = FACTORY_NAME;
	class->component_kind = ICAL_VEVENT_COMPONENT;
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
	e_source_contacts_type_register (type_module);
	e_cal_backend_contacts_events_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}


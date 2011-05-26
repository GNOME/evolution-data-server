/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e-cal-backend-contacts-factory.h"
#include "e-cal-backend-contacts.h"

typedef ECalBackendFactory ECalBackendContactsEventsFactory;
typedef ECalBackendFactoryClass ECalBackendContactsEventsFactoryClass;

/* Forward Declarations */
GType e_cal_backend_contacts_events_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendContactsEventsFactory,
	e_cal_backend_contacts_events_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

static const gchar *
_get_protocol (ECalBackendFactory *factory)
{
	return "contacts";
}

static ECalBackend *
_events_new_backend (ECalBackendFactory *factory,
                     ESource *source)
{
	return g_object_new (
		e_cal_backend_contacts_get_type (),
		"kind", ICAL_VEVENT_COMPONENT,
		"source", source, NULL);
}

static icalcomponent_kind
_events_get_kind (ECalBackendFactory *factory)
{
	return ICAL_VEVENT_COMPONENT;
}

static void
e_cal_backend_contacts_events_factory_class_init (ECalBackendFactoryClass *class)
{
	class->get_protocol = _get_protocol;
	class->get_kind     = _events_get_kind;
	class->new_backend  = _events_new_backend;
}

static void
e_cal_backend_contacts_events_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_contacts_events_factory_init (ECalBackendFactory *factory)
{
}

void
eds_module_initialize (GTypeModule *type_module)
{
	e_cal_backend_contacts_events_factory_register_type (type_module);
}

void
eds_module_shutdown (void)
{
}

void
eds_module_list_types (const GType **types, gint *num_types)
{
	static GType contacts_types[1];

	contacts_types[0] = e_cal_backend_contacts_events_factory_get_type ();

	*types = contacts_types;
	*num_types = G_N_ELEMENTS (contacts_types);
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Suman Manjunath <msuman@novell.com>
 *  Copyright (C) 2007 Novell, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */



#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-cal-backend-mapi-factory.h"
#include "e-cal-backend-mapi.h"
#include "e-cal-backend-mapi-tz-utils.h"
#define d(x) 

typedef struct {
	ECalBackendFactory            parent_object;
} ECalBackendMAPIFactory;

typedef struct {
	ECalBackendFactoryClass parent_class;
} ECalBackendMAPIFactoryClass;

static void
e_cal_backend_mapi_factory_instance_init (ECalBackendMAPIFactory *factory)
{
}

static const char *
_get_protocol (ECalBackendFactory *factory)
{
	return "mapi";
}

static ECalBackend*
_todos_new_backend (ECalBackendFactory *factory, ESource *source)
{
	return g_object_new (e_cal_backend_mapi_get_type (),
			     "source", source,
			     "kind", ICAL_VTODO_COMPONENT,
			     NULL);
}

static icalcomponent_kind
_todos_get_kind (ECalBackendFactory *factory)
{
	return ICAL_VTODO_COMPONENT;
}

static void
todos_backend_factory_class_init (ECalBackendMAPIFactoryClass *klass)
{
	E_CAL_BACKEND_FACTORY_CLASS (klass)->get_protocol = _get_protocol;
	E_CAL_BACKEND_FACTORY_CLASS (klass)->get_kind     = _todos_get_kind;
	E_CAL_BACKEND_FACTORY_CLASS (klass)->new_backend  = _todos_new_backend;
}

static GType
todos_backend_factory_get_type (GTypeModule *module)
{
	GType type;

	GTypeInfo info = {
		sizeof (ECalBackendMAPIFactoryClass),
		NULL, /* base_class_init */
		NULL, /* base_class_finalize */
		(GClassInitFunc)  todos_backend_factory_class_init,
		NULL, /* class_finalize */
		NULL, /* class_data */
		sizeof (ECalBackend),
		0,    /* n_preallocs */
		(GInstanceInitFunc) e_cal_backend_mapi_factory_instance_init
	};

	type = g_type_module_register_type (module,
					    E_TYPE_CAL_BACKEND_FACTORY,
					    "ECalBackendMAPITodosFactory",
					    &info, 0);

	return type;
}

static ECalBackend*
_events_new_backend (ECalBackendFactory *factory, ESource *source)
{
	return g_object_new (e_cal_backend_mapi_get_type (),
			     "source", source,
			     "kind", ICAL_VEVENT_COMPONENT,
			     NULL);
}

static icalcomponent_kind
_events_get_kind (ECalBackendFactory *factory)
{
	return ICAL_VEVENT_COMPONENT;
}

static void
events_backend_factory_class_init (ECalBackendMAPIFactoryClass *klass)
{
	E_CAL_BACKEND_FACTORY_CLASS (klass)->get_protocol = _get_protocol;
	E_CAL_BACKEND_FACTORY_CLASS (klass)->get_kind     = _events_get_kind;
	E_CAL_BACKEND_FACTORY_CLASS (klass)->new_backend  = _events_new_backend;
}

static GType
events_backend_factory_get_type (GTypeModule *module)
{
	GType type;

	GTypeInfo info = {
		sizeof (ECalBackendMAPIFactoryClass),
		NULL, /* base_class_init */
		NULL, /* base_class_finalize */
		(GClassInitFunc)  events_backend_factory_class_init,
		NULL, /* class_finalize */
		NULL, /* class_data */
		sizeof (ECalBackend),
		0,    /* n_preallocs */
		(GInstanceInitFunc) e_cal_backend_mapi_factory_instance_init
	};

	type = g_type_module_register_type (module,
					    E_TYPE_CAL_BACKEND_FACTORY,
					    "ECalBackendMAPIEventsFactory",
					    &info, 0);

	return type;
}

/* NOTE: Outlook "Notes" = Evolution "Memos" */
static ECalBackend*
_journal_new_backend (ECalBackendFactory *factory, ESource *source)
{
	return g_object_new (e_cal_backend_mapi_get_type (),
			     "source", source,
			     "kind", ICAL_VJOURNAL_COMPONENT,
			     NULL);
}

static icalcomponent_kind
_journal_get_kind (ECalBackendFactory *factory)
{
	return ICAL_VJOURNAL_COMPONENT;
}

static void
journal_backend_factory_class_init (ECalBackendMAPIFactoryClass *klass)
{
	E_CAL_BACKEND_FACTORY_CLASS (klass)->get_protocol = _get_protocol;
	E_CAL_BACKEND_FACTORY_CLASS (klass)->get_kind     = _journal_get_kind;
	E_CAL_BACKEND_FACTORY_CLASS (klass)->new_backend  = _journal_new_backend;
}

static GType
journal_backend_factory_get_type (GTypeModule *module)
{
	GType type;

	GTypeInfo info = {
		sizeof (ECalBackendMAPIFactoryClass),
		NULL, /* base_class_init */
		NULL, /* base_class_finalize */
		(GClassInitFunc)  journal_backend_factory_class_init,
		NULL, /* class_finalize */
		NULL, /* class_data */
		sizeof (ECalBackend),
		0,    /* n_preallocs */
		(GInstanceInitFunc) e_cal_backend_mapi_factory_instance_init
	};

	type = g_type_module_register_type (module,
					    E_TYPE_CAL_BACKEND_FACTORY,
					    "ECalBackendMAPIJournalFactory",
					    &info, 0);

	return type;
}

static GType mapi_types[3];

void
eds_module_initialize (GTypeModule *module)
{
	mapi_types[0] = todos_backend_factory_get_type (module);
	mapi_types[1] = events_backend_factory_get_type (module);
	mapi_types[2] = journal_backend_factory_get_type (module);

	e_cal_backend_mapi_tz_util_populate ();
	d(e_cal_backend_mapi_tz_util_dump ());
}

void
eds_module_shutdown   (void)
{
	e_cal_backend_mapi_tz_util_destroy ();
}

void
eds_module_list_types (const GType **types, int *num_types)
{
	*types = mapi_types;
	*num_types = 3;
}


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

#include "e-cal-backend-file-factory.h"
#include "e-cal-backend-file-events.h"
#include "e-cal-backend-file-journal.h"
#include "e-cal-backend-file-todos.h"

typedef struct {
	ECalBackendFactory            parent_object;
} ECalBackendFileFactory;

typedef struct {
	ECalBackendFactoryClass parent_class;
} ECalBackendFileFactoryClass;

static void
e_cal_backend_file_factory_instance_init (ECalBackendFileFactory *factory)
{
}

static const gchar *
_get_protocol (ECalBackendFactory *factory)
{
	return "local";
}

static ECalBackend*
_todos_new_backend (ECalBackendFactory *factory, ESource *source)
{
	return g_object_new (e_cal_backend_file_todos_get_type (),
			     "source", source,
			     "kind", ICAL_VTODO_COMPONENT,
			     NULL);
}

static icalcomponent_kind
_todos_get_kind (ECalBackendFactory *factory)
{
	return ICAL_VTODO_COMPONENT;
}

static ECalBackend*
_journal_new_backend (ECalBackendFactory *factory, ESource *source)
{
	return g_object_new (e_cal_backend_file_journal_get_type (),
			     "source", source,
			     "kind", ICAL_VJOURNAL_COMPONENT,
			     NULL);
}

static icalcomponent_kind
_journal_get_kind (ECalBackendFactory *factory)
{
	return ICAL_VJOURNAL_COMPONENT;
}

static ECalBackend*
_events_new_backend (ECalBackendFactory *factory, ESource *source)
{
	return g_object_new (e_cal_backend_file_events_get_type (),
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
todos_backend_factory_class_init (ECalBackendFileFactoryClass *klass)
{
	E_CAL_BACKEND_FACTORY_CLASS (klass)->get_protocol = _get_protocol;
	E_CAL_BACKEND_FACTORY_CLASS (klass)->get_kind     = _todos_get_kind;
	E_CAL_BACKEND_FACTORY_CLASS (klass)->new_backend  = _todos_new_backend;
}

static void
journal_backend_factory_class_init (ECalBackendFileFactoryClass *klass)
{
	E_CAL_BACKEND_FACTORY_CLASS (klass)->get_protocol = _get_protocol;
	E_CAL_BACKEND_FACTORY_CLASS (klass)->get_kind     = _journal_get_kind;
	E_CAL_BACKEND_FACTORY_CLASS (klass)->new_backend  = _journal_new_backend;
}

static void
events_backend_factory_class_init (ECalBackendFileFactoryClass *klass)
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
		sizeof (ECalBackendFileFactoryClass),
		NULL, /* base_class_init */
		NULL, /* base_class_finalize */
		(GClassInitFunc)  events_backend_factory_class_init,
		NULL, /* class_finalize */
		NULL, /* class_data */
		sizeof (ECalBackend),
		0,    /* n_preallocs */
		(GInstanceInitFunc) e_cal_backend_file_factory_instance_init
	};

	type = g_type_module_register_type (module,
					    E_TYPE_CAL_BACKEND_FACTORY,
					    "ECalBackendFileEventsFactory",
					    &info, 0);

	return type;
}

static GType
journal_backend_factory_get_type (GTypeModule *module)
{
	GType type;

	GTypeInfo info = {
		sizeof (ECalBackendFileFactoryClass),
		NULL, /* base_class_init */
		NULL, /* base_class_finalize */
		(GClassInitFunc)  journal_backend_factory_class_init,
		NULL, /* class_finalize */
		NULL, /* class_data */
		sizeof (ECalBackend),
		0,    /* n_preallocs */
		(GInstanceInitFunc) e_cal_backend_file_factory_instance_init
	};

	type = g_type_module_register_type (module,
					    E_TYPE_CAL_BACKEND_FACTORY,
					    "ECalBackendFileJournalFactory",
					    &info, 0);

	return type;
}

static GType
todos_backend_factory_get_type (GTypeModule *module)
{
	GType type;

	GTypeInfo info = {
		sizeof (ECalBackendFileFactoryClass),
		NULL, /* base_class_init */
		NULL, /* base_class_finalize */
		(GClassInitFunc)  todos_backend_factory_class_init,
		NULL, /* class_finalize */
		NULL, /* class_data */
		sizeof (ECalBackend),
		0,    /* n_preallocs */
		(GInstanceInitFunc) e_cal_backend_file_factory_instance_init
	};

	type = g_type_module_register_type (module,
					    E_TYPE_CAL_BACKEND_FACTORY,
					    "ECalBackendFileTodosFactory",
					    &info, 0);

	return type;
}



static GType file_types[3];

void
eds_module_initialize (GTypeModule *module)
{
	file_types[0] = todos_backend_factory_get_type (module);
	file_types[1] = events_backend_factory_get_type (module);
	file_types[2] = journal_backend_factory_get_type (module);
}

void
eds_module_shutdown   (void)
{
}

void
eds_module_list_types (const GType **types, gint *num_types)
{
	*types = file_types;
	*num_types = 3;
}

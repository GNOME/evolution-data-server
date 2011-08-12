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

typedef ECalBackendFactory ECalBackendFileEventsFactory;
typedef ECalBackendFactoryClass ECalBackendFileEventsFactoryClass;

typedef ECalBackendFactory ECalBackendFileJournalFactory;
typedef ECalBackendFactoryClass ECalBackendFileJournalFactoryClass;

typedef ECalBackendFactory ECalBackendFileTodosFactory;
typedef ECalBackendFactoryClass ECalBackendFileTodosFactoryClass;

/* Forward Declarations */
GType e_cal_backend_file_events_factory_get_type (void);
GType e_cal_backend_file_journal_factory_get_type (void);
GType e_cal_backend_file_todos_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendFileEventsFactory,
	e_cal_backend_file_events_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendFileJournalFactory,
	e_cal_backend_file_journal_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendFileTodosFactory,
	e_cal_backend_file_todos_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

static const gchar *
_get_protocol (ECalBackendFactory *factory)
{
	return "local";
}

static icalcomponent_kind
_events_get_kind (ECalBackendFactory *factory)
{
	return ICAL_VEVENT_COMPONENT;
}

static ECalBackend *
_events_new_backend (ECalBackendFactory *factory,
                     ESource *source)
{
	return g_object_new (
		e_cal_backend_file_events_get_type (),
		"kind", ICAL_VEVENT_COMPONENT,
		"source", source, NULL);
}

static icalcomponent_kind
_journal_get_kind (ECalBackendFactory *factory)
{
	return ICAL_VJOURNAL_COMPONENT;
}

static ECalBackend *
_journal_new_backend (ECalBackendFactory *factory,
                      ESource *source)
{
	return g_object_new (
		e_cal_backend_file_journal_get_type (),
		"kind", ICAL_VJOURNAL_COMPONENT,
		"source", source, NULL);
}

static icalcomponent_kind
_todos_get_kind (ECalBackendFactory *factory)
{
	return ICAL_VTODO_COMPONENT;
}

static ECalBackend *
_todos_new_backend (ECalBackendFactory *factory,
                    ESource *source)
{
	return g_object_new (
		e_cal_backend_file_todos_get_type (),
		"kind", ICAL_VTODO_COMPONENT,
		"source", source, NULL);
}

static void
e_cal_backend_file_events_factory_class_init (ECalBackendFactoryClass *class)
{
	class->get_protocol = _get_protocol;
	class->get_kind     = _events_get_kind;
	class->new_backend  = _events_new_backend;
}

static void
e_cal_backend_file_events_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_file_events_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_file_journal_factory_class_init (ECalBackendFactoryClass *class)
{
	class->get_protocol = _get_protocol;
	class->get_kind     = _journal_get_kind;
	class->new_backend  = _journal_new_backend;
}

static void
e_cal_backend_file_journal_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_file_journal_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_file_todos_factory_class_init (ECalBackendFactoryClass *class)
{
	class->get_protocol = _get_protocol;
	class->get_kind     = _todos_get_kind;
	class->new_backend  = _todos_new_backend;
}

static void
e_cal_backend_file_todos_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_file_todos_factory_init (ECalBackendFactory *factory)
{
}

void
eds_module_initialize (GTypeModule *type_module)
{
	e_cal_backend_file_events_factory_register_type (type_module);
	e_cal_backend_file_journal_factory_register_type (type_module);
	e_cal_backend_file_todos_factory_register_type (type_module);
}

void
eds_module_shutdown (void)
{
}

void
eds_module_list_types (const GType **types,
                       gint *num_types)
{
	static GType file_types[3];

	file_types[0] = e_cal_backend_file_events_factory_get_type ();
	file_types[1] = e_cal_backend_file_journal_factory_get_type ();
	file_types[2] = e_cal_backend_file_todos_factory_get_type ();

	*types = file_types;
	*num_types = G_N_ELEMENTS (file_types);
}

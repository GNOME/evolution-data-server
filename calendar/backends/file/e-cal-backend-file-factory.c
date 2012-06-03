/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#include <config.h>

#include "e-cal-backend-file-events.h"
#include "e-cal-backend-file-journal.h"
#include "e-cal-backend-file-todos.h"
#include "e-source-local.h"

#define FACTORY_NAME "local"

typedef ECalBackendFactory ECalBackendFileEventsFactory;
typedef ECalBackendFactoryClass ECalBackendFileEventsFactoryClass;

typedef ECalBackendFactory ECalBackendFileJournalFactory;
typedef ECalBackendFactoryClass ECalBackendFileJournalFactoryClass;

typedef ECalBackendFactory ECalBackendFileTodosFactory;
typedef ECalBackendFactoryClass ECalBackendFileTodosFactoryClass;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

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

static void
e_cal_backend_file_events_factory_class_init (ECalBackendFactoryClass *class)
{
	class->factory_name = FACTORY_NAME;
	class->component_kind = ICAL_VEVENT_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_FILE_EVENTS;
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
	class->factory_name = FACTORY_NAME;
	class->component_kind = ICAL_VJOURNAL_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_FILE_JOURNAL;
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
	class->factory_name = FACTORY_NAME;
	class->component_kind = ICAL_VTODO_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_FILE_TODOS;
}

static void
e_cal_backend_file_todos_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_file_todos_factory_init (ECalBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_source_local_type_register (type_module);
	e_cal_backend_file_events_factory_register_type (type_module);
	e_cal_backend_file_journal_factory_register_type (type_module);
	e_cal_backend_file_todos_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}


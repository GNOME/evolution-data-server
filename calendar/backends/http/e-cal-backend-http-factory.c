/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#include <config.h>

#include "e-cal-backend-http.h"

#define FACTORY_NAME "webcal"

typedef ECalBackendFactory ECalBackendHttpEventsFactory;
typedef ECalBackendFactoryClass ECalBackendHttpEventsFactoryClass;

typedef ECalBackendFactory ECalBackendHttpJournalFactory;
typedef ECalBackendFactoryClass ECalBackendHttpJournalFactoryClass;

typedef ECalBackendFactory ECalBackendHttpTodosFactory;
typedef ECalBackendFactoryClass ECalBackendHttpTodosFactoryClass;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_cal_backend_http_events_factory_get_type (void);
GType e_cal_backend_http_journal_factory_get_type (void);
GType e_cal_backend_http_todos_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendHttpEventsFactory,
	e_cal_backend_http_events_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendHttpJournalFactory,
	e_cal_backend_http_journal_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendHttpTodosFactory,
	e_cal_backend_http_todos_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

static void
e_cal_backend_http_events_factory_class_init (ECalBackendFactoryClass *class)
{
	class->factory_name = FACTORY_NAME;
	class->component_kind = ICAL_VEVENT_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_HTTP;
}

static void
e_cal_backend_http_events_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_http_events_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_http_journal_factory_class_init (ECalBackendFactoryClass *class)
{
	class->factory_name = FACTORY_NAME;
	class->component_kind = ICAL_VJOURNAL_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_HTTP;
}

static void
e_cal_backend_http_journal_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_http_journal_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_http_todos_factory_class_init (ECalBackendFactoryClass *class)
{
	class->factory_name = FACTORY_NAME;
	class->component_kind = ICAL_VTODO_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_HTTP;
}

static void
e_cal_backend_http_todos_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_http_todos_factory_init (ECalBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_cal_backend_http_events_factory_register_type (type_module);
	e_cal_backend_http_journal_factory_register_type (type_module);
	e_cal_backend_http_todos_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}


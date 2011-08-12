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

#include "e-cal-backend-http-factory.h"
#include "e-cal-backend-http.h"

typedef ECalBackendFactory ECalBackendHttpEventsFactory;
typedef ECalBackendFactoryClass ECalBackendHttpEventsFactoryClass;

typedef ECalBackendFactory ECalBackendHttpJournalFactory;
typedef ECalBackendFactoryClass ECalBackendHttpJournalFactoryClass;

typedef ECalBackendFactory ECalBackendHttpTodosFactory;
typedef ECalBackendFactoryClass ECalBackendHttpTodosFactoryClass;

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

static const gchar *
_get_protocol (ECalBackendFactory *factory)
{
	return "webcal";
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
		e_cal_backend_http_get_type (),
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
		e_cal_backend_http_get_type (),
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
		e_cal_backend_http_get_type (),
		"kind", ICAL_VTODO_COMPONENT,
		"source", source, NULL);
}

static void
e_cal_backend_http_events_factory_class_init (ECalBackendFactoryClass *class)
{
	class->get_protocol = _get_protocol;
	class->get_kind     = _events_get_kind;
	class->new_backend  = _events_new_backend;
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
	class->get_protocol = _get_protocol;
	class->get_kind     = _journal_get_kind;
	class->new_backend  = _journal_new_backend;
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
	class->get_protocol = _get_protocol;
	class->get_kind     = _todos_get_kind;
	class->new_backend  = _todos_new_backend;
}

static void
e_cal_backend_http_todos_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_http_todos_factory_init (ECalBackendFactory *factory)
{
}

void
eds_module_initialize (GTypeModule *type_module)
{
	e_cal_backend_http_events_factory_register_type (type_module);
	e_cal_backend_http_journal_factory_register_type (type_module);
	e_cal_backend_http_todos_factory_register_type (type_module);
}

void
eds_module_shutdown (void)
{
}

void
eds_module_list_types (const GType **types,
                       gint *num_types)
{
	static GType http_types[3];

	http_types[0] = e_cal_backend_http_events_factory_get_type ();
	http_types[1] = e_cal_backend_http_journal_factory_get_type ();
	http_types[2] = e_cal_backend_http_todos_factory_get_type ();

	*types = http_types;
	*num_types = G_N_ELEMENTS (http_types);
}

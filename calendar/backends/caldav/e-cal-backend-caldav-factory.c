/*
 * Evolution calendar - caldav backend factory
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Authors:
 *		Christian Kellner <gicmo@gnome.org>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include <config.h>

#include "e-cal-backend-caldav.h"

#define FACTORY_NAME "caldav"

typedef ECalBackendFactory ECalBackendCalDAVEventsFactory;
typedef ECalBackendFactoryClass ECalBackendCalDAVEventsFactoryClass;

typedef ECalBackendFactory ECalBackendCalDAVJournalFactory;
typedef ECalBackendFactoryClass ECalBackendCalDAVJournalFactoryClass;

typedef ECalBackendFactory ECalBackendCalDAVTodosFactory;
typedef ECalBackendFactoryClass ECalBackendCalDAVTodosFactoryClass;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_cal_backend_caldav_events_factory_get_type (void);
GType e_cal_backend_caldav_journal_factory_get_type (void);
GType e_cal_backend_caldav_todos_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendCalDAVEventsFactory,
	e_cal_backend_caldav_events_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendCalDAVJournalFactory,
	e_cal_backend_caldav_journal_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendCalDAVTodosFactory,
	e_cal_backend_caldav_todos_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

static void
e_cal_backend_caldav_events_factory_class_init (ECalBackendFactoryClass *class)
{
	class->factory_name = FACTORY_NAME;
	class->component_kind = ICAL_VEVENT_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_CALDAV;
}

static void
e_cal_backend_caldav_events_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_caldav_events_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_caldav_journal_factory_class_init (ECalBackendFactoryClass *class)
{
	class->factory_name = FACTORY_NAME;
	class->component_kind = ICAL_VJOURNAL_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_CALDAV;
}

static void
e_cal_backend_caldav_journal_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_caldav_journal_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_caldav_todos_factory_class_init (ECalBackendFactoryClass *class)
{
	class->factory_name = FACTORY_NAME;
	class->component_kind = ICAL_VTODO_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_CALDAV;
}

static void
e_cal_backend_caldav_todos_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_caldav_todos_factory_init (ECalBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_cal_backend_caldav_events_factory_register_type (type_module);
	e_cal_backend_caldav_journal_factory_register_type (type_module);
	e_cal_backend_caldav_todos_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}


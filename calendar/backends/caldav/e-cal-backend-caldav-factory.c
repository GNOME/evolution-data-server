/* Evolution calendar - caldav backend factory
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Author: Christian Kellner <gicmo@gnome.org>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e-cal-backend-caldav-factory.h"
#include "e-cal-backend-caldav.h"

typedef struct {
	   ECalBackendFactory parent_object;
} ECalBackendCalDAVFactory;

typedef struct {
	   ECalBackendFactoryClass parent_class;
} ECalBackendCalDAVFactoryClass;

static void
e_cal_backend_caldav_factory_instance_init (ECalBackendCalDAVFactory *factory)
{
}

static const char *
_get_protocol (ECalBackendFactory *factory)
{
	   return "caldav";
}

static ECalBackend*
_events_new_backend (ECalBackendFactory *factory, ESource *source)
{
	   return g_object_new (E_TYPE_CAL_BACKEND_CALDAV,
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
events_backend_factory_class_init (ECalBackendCalDAVFactoryClass *klass)
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
			 sizeof (ECalBackendCalDAVFactoryClass),
			 NULL, /* base_class_init */
			 NULL, /* base_class_finalize */
			 (GClassInitFunc)  events_backend_factory_class_init,
			 NULL, /* class_finalize */
			 NULL, /* class_data */
			 sizeof (ECalBackend),
			 0,    /* n_preallocs */
			 (GInstanceInitFunc) e_cal_backend_caldav_factory_instance_init
	   };

	   type = g_type_module_register_type (module,
								    E_TYPE_CAL_BACKEND_FACTORY,
								    "ECalBackendCalDAVEventsFactory",
								    &info, 0);

	   return type;
}


static GType caldav_types[1];

void
eds_module_initialize (GTypeModule *module)
{
	   caldav_types[0] = events_backend_factory_get_type (module);
}

void
eds_module_shutdown   (void)
{
}

void
eds_module_list_types (const GType **types, int *num_types)
{
	   *types = caldav_types;
	   *num_types = 1;
}

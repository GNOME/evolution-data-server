/* Evolution calendar - weather backend factory
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: David Trowbridge <trowbrds@cs.colorado.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e-cal-backend-weather-factory.h"
#include "e-cal-backend-weather.h"

typedef ECalBackendFactory ECalBackendWeatherEventsFactory;
typedef ECalBackendFactoryClass ECalBackendWeatherEventsFactoryClass;

/* Forward Declarations */
GType e_cal_backend_weather_events_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendWeatherEventsFactory,
	e_cal_backend_weather_events_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

static const gchar *
_get_protocol (ECalBackendFactory *factory)
{
	return "weather";
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
		e_cal_backend_weather_get_type (),
		"kind", ICAL_VEVENT_COMPONENT,
		"source", source, NULL);
}

static void
e_cal_backend_weather_events_factory_class_init (ECalBackendFactoryClass *class)
{
	class->get_protocol = _get_protocol;
	class->get_kind     = _events_get_kind;
	class->new_backend  = _events_new_backend;
}

static void
e_cal_backend_weather_events_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_weather_events_factory_init (ECalBackendFactory *factory)
{
}

void
eds_module_initialize (GTypeModule *type_module)
{
	e_cal_backend_weather_events_factory_register_type (type_module);
}

void
eds_module_shutdown (void)
{
}

void
eds_module_list_types (const GType **types, gint *num_types)
{
	static GType weather_types[1];

	weather_types[0] = e_cal_backend_weather_events_factory_get_type ();

	*types = weather_types;
	*num_types = G_N_ELEMENTS (weather_types);
}

/* Evolution calendar - weather backend factory
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: David Trowbridge <trowbrds@cs.colorado.edu>
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include "e-cal-backend-weather.h"
#include "e-source-weather.h"

#define FACTORY_NAME "weather"

typedef ECalBackendFactory ECalBackendWeatherEventsFactory;
typedef ECalBackendFactoryClass ECalBackendWeatherEventsFactoryClass;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_cal_backend_weather_events_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendWeatherEventsFactory,
	e_cal_backend_weather_events_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

static void
e_cal_backend_weather_events_factory_class_init (ECalBackendFactoryClass *class)
{
	class->factory_name = FACTORY_NAME;
	class->component_kind = ICAL_VEVENT_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_WEATHER;
}

static void
e_cal_backend_weather_events_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_weather_events_factory_init (ECalBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_source_weather_type_register (type_module);
	e_cal_backend_weather_events_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}


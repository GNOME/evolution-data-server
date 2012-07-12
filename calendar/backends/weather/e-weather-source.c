/* Evolution calendar - weather backend source class
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

#include "e-weather-source.h"

#include <string.h>

G_DEFINE_TYPE (EWeatherSource, e_weather_source, G_TYPE_OBJECT)

static void
parse_done (GWeatherInfo *info,
            gpointer data)
{
	EWeatherSource *source = (EWeatherSource *) data;

	if (!source)
		return;

	if (!info || !gweather_info_is_valid (info)) {
		source->done (NULL, source->finished_data);
		return;
	}

	source->done (info, source->finished_data);
}

void
e_weather_source_parse (EWeatherSource *source,
                        EWeatherSourceFinished done,
                        gpointer data)
{
	source->finished_data = data;
	source->done = done;

	if (!source->info) {
		source->info = gweather_info_new (source->location, GWEATHER_FORECAST_LIST);
		g_signal_connect (source->info, "updated", G_CALLBACK (parse_done), source);
	} else {
		gweather_info_update (source->info);
	}
}

static void
e_weather_source_finalize (GObject *object)
{
	EWeatherSource *self = (EWeatherSource *) object;

	if (self->location)
		gweather_location_unref (self->location);
	g_clear_object (&self->info);

	G_OBJECT_CLASS (e_weather_source_parent_class)->finalize (object);
}

static void
e_weather_source_class_init (EWeatherSourceClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = e_weather_source_finalize;
}

static void
e_weather_source_init (EWeatherSource *source)
{
}

EWeatherSource *
e_weather_source_new (const gchar *location)
{
	GWeatherLocation *world, *glocation;
	EWeatherSource *source;
	gchar **tokens;

	/* Old location is formatted as ccf/AAA[/BBB] - AAA is the 3-letter station
	 * code for identifying the providing station (subdirectory within the crh data
	 * repository). BBB is an optional additional station ID for the station within
	 * the CCF file. If not present, BBB is assumed to be the same station as AAA.
	 * But the new location is code/name, where code is 4-letter code.
	 * So if got the old format, then migrate to the new one, if possible.
	 */

	if (!location)
		return NULL;

	world = gweather_location_new_world (FALSE);

	if (strncmp (location, "ccf/", 4) == 0)
		location += 4;

	tokens = g_strsplit (location, "/", 2);

	glocation = gweather_location_find_by_station_code (world, tokens[0]);
	if (glocation)
		gweather_location_ref (glocation);

	gweather_location_unref (world);
	g_strfreev (tokens);

	if (!glocation)
		return NULL;

	source = E_WEATHER_SOURCE (g_object_new (e_weather_source_get_type (), NULL));
	source->location = glocation;
	source->info = NULL;

	return source;
}

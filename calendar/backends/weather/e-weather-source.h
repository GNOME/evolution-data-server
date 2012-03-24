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

#ifndef E_WEATHER_SOURCE_H
#define E_WEATHER_SOURCE_H

#include <glib-object.h>
#include <time.h>

#define GWEATHER_I_KNOW_THIS_IS_UNSTABLE
#include <libgweather/gweather-weather.h>
#undef GWEATHER_I_KNOW_THIS_IS_UNSTABLE

/* Standard GObject macros */
#define E_TYPE_WEATHER_SOURCE \
	(e_weather_source_get_type ())
#define E_WEATHER_SOURCE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_WEATHER_SOURCE, EWeatherSource))
#define E_WEATHER_SOURCE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_WEATHER_SOURCE, EWeatherSourceClass))
#define E_IS_WEATHER_SOURCE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_WEATHER_SOURCE))
#define E_IS_WEATHER_SOURCE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_WEATHER_SOURCE))
#define E_WEATHER_SOURCE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_WEATHER_SOURCE, EWeatherSourceClass))

G_BEGIN_DECLS

typedef void (*EWeatherSourceFinished)(GWeatherInfo *result, gpointer data);

typedef struct _EWeatherSource EWeatherSource;
typedef struct _EWeatherSourceClass EWeatherSourceClass;

/* This class is an abstract base-class for any weather data source.
 * All the URL fetching is handled outside of this, and all this has
 * to know how to do is parse the specific format. */
struct _EWeatherSource {
	GObject parent;

	GWeatherLocation *location;
	GWeatherInfo *info;

	EWeatherSourceFinished done;
	gpointer finished_data;
};

struct _EWeatherSourceClass {
	GObjectClass parent_class;
};

GType		e_weather_source_get_type	(void);
EWeatherSource *e_weather_source_new		(const gchar *location);
void		e_weather_source_parse		(EWeatherSource *source,
						 EWeatherSourceFinished done,
						 gpointer data);

G_END_DECLS

#endif /* E_WEATHER_SOURCE_H */

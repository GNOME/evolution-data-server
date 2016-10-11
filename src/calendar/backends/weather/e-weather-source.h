/* Evolution calendar - weather backend source class
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: David Trowbridge <trowbrds@cs.colorado.edu>
 */

#ifndef E_WEATHER_SOURCE_H
#define E_WEATHER_SOURCE_H

#include <glib-object.h>
#include <time.h>

#define GWEATHER_I_KNOW_THIS_IS_UNSTABLE
#include <libgweather/gweather.h>
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
typedef struct _EWeatherSourcePrivate EWeatherSourcePrivate;

struct _EWeatherSource {
	GObject parent;
	EWeatherSourcePrivate *priv;
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

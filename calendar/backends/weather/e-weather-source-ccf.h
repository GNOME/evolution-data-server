/* Evolution calendar - weather backend source class for parsing
 *	CCF (coded cities forecast) formatted NWS reports
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

#ifndef E_WEATHER_SOURCE_CCF_H
#define E_WEATHER_SOURCE_CCF_H

#include <libsoup/soup-session-async.h>
#include <libsoup/soup-uri.h>
#include "e-weather-source.h"

#define GWEATHER_I_KNOW_THIS_IS_UNSTABLE
#include <libgweather/weather.h>
#undef GWEATHER_I_KNOW_THIS_IS_UNSTABLE

G_BEGIN_DECLS

#define E_TYPE_WEATHER_SOURCE_CCF            (e_weather_source_ccf_get_type ())
#define E_WEATHER_SOURCE_CCF(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_WEATHER_SOURCE_CCF, EWeatherSourceCCF))
#define E_WEATHER_SOURCE_CCF_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_WEATHER_SOURCE_CCF, EWeatherSourceCCF))
#define E_IS_WEATHER_SOURCE_CCF(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_WEATHER_SOURCE_CCF))
#define E_IS_WEATHER_SOURCE_CCF_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_WEATHER_SOURCE_CCF))

typedef struct _EWeatherSourceCCF EWeatherSourceCCF;
typedef struct _EWeatherSourceCCFClass EWeatherSourceCCFClass;

struct _EWeatherSourceCCF {
	EWeatherSource parent;

	WeatherLocation *location;
	WeatherInfo *info;

	EWeatherSourceFinished done;
	gpointer finished_data;
};

struct _EWeatherSourceCCFClass {
	EWeatherSourceClass parent_class;
};

EWeatherSource*	e_weather_source_ccf_new (const gchar *uri);
GType		e_weather_source_ccf_get_type (void);

G_END_DECLS

#endif

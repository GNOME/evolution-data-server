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
#include "e-weather-source-ccf.h"

#include <string.h>

G_DEFINE_TYPE (EWeatherSource, e_weather_source, G_TYPE_OBJECT)

void
e_weather_source_parse (EWeatherSource *source, EWeatherSourceFinished done, gpointer data)
{
	EWeatherSourceClass *class;
	g_return_if_fail (source != NULL);
	class = (EWeatherSourceClass*) G_OBJECT_GET_CLASS (source);
	class->parse (source, done, data);
}

static void
e_weather_source_class_init (EWeatherSourceClass *class)
{
	/* nothing to do here */
}

static void
e_weather_source_init (EWeatherSource *source)
{
	/* nothing to do here */
}

EWeatherSource*	e_weather_source_new (const gchar *uri)
{
	const gchar *base = uri + 10; /* skip weather:// */

	return e_weather_source_ccf_new (base);
}

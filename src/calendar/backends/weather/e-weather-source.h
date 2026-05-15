/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: David Trowbridge <trowbrds@cs.colorado.edu>
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

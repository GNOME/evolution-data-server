/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: David Trowbridge <trowbrds@cs.colorado.edu>
 */

#ifndef E_CAL_BACKEND_WEATHER_H
#define E_CAL_BACKEND_WEATHER_H

#include <libedata-cal/libedata-cal.h>

/* Standard GObject macros */
#define E_TYPE_CAL_BACKEND_WEATHER \
	(e_cal_backend_weather_get_type ())
#define E_CAL_BACKEND_WEATHER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_BACKEND_WEATHER, ECalBackendWeather))
#define E_CAL_BACKEND_WEATHER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_BACKEND_WEATHER, ECalBackendWeatherClass))
#define E_IS_CAL_BACKEND_WEATHER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_BACKEND_WEATHER))
#define E_IS_CAL_BACKEND_WEATHER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_BACKEND_WEATHER))
#define E_CAL_BACKEND_WEATHER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CAL_BACKEND_WEATHER, ECalBackendWeatherClass))

G_BEGIN_DECLS

typedef struct _ECalBackendWeather ECalBackendWeather;
typedef struct _ECalBackendWeatherClass ECalBackendWeatherClass;
typedef struct _ECalBackendWeatherPrivate ECalBackendWeatherPrivate;

struct _ECalBackendWeather {
	ECalBackendSync backend;
	ECalBackendWeatherPrivate *priv;
};

struct _ECalBackendWeatherClass {
	ECalBackendSyncClass parent_class;
};

GType		e_cal_backend_weather_get_type	(void);

G_END_DECLS

#endif /* E_CAL_BACKEND_WEATHER_H */

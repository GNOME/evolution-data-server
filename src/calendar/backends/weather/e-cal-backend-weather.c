/* Evolution calendar - weather backend
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

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>
#include <string.h>

#include <libedataserver/libedataserver.h>

#include "e-cal-backend-weather.h"
#include "e-weather-source.h"

#define WEATHER_UID_EXT "-weather"

#define EC_ERROR(_code) e_client_error_create (_code, NULL)
#define EC_ERROR_EX(_code, _msg) e_client_error_create (_code, _msg)
#define ECC_ERROR(_code) e_cal_client_error_create (_code, NULL)

static void	maybe_start_reload_timeout	(ECalBackendWeather *cbw);
static void	finished_retrieval_cb		(GWeatherInfo *info,
						 ECalBackendWeather *cbw);
static gboolean	reload_cb			(gpointer user_data);
static ECalComponent *
		create_weather			(ECalBackendWeather *cbw,
						 GWeatherInfo *report,
						 GWeatherTemperatureUnit unit,
						 gboolean is_forecast,
						 GSList *same_day_forecasts);
static void	e_cal_backend_weather_add_timezone
						(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *tzobj,
						 GError **perror);

/* Private part of the ECalBackendWeather structure */
struct _ECalBackendWeatherPrivate {
	/* URI to get remote weather data from */
	gchar *uri;

	/* The file cache */
	ECalCache *cache;

	/* Reload */
	guint reload_timeout_id;
	guint is_loading : 1;

	/* Weather source */
	EWeatherSource *source;

	guint begin_retrival_id;

	gulong source_changed_id;

	GMutex last_used_mutex;
	ESourceWeatherUnits last_used_units;
	gchar *last_used_location;
};

G_DEFINE_TYPE_WITH_PRIVATE (ECalBackendWeather, e_cal_backend_weather, E_TYPE_CAL_BACKEND_SYNC)

static void
ecb_weather_begin_retrieval (ECalBackendWeather *cbw)
{
	ESource *source;

	maybe_start_reload_timeout (cbw);

	if (!e_backend_get_online (E_BACKEND (cbw)))
		return;

	source = e_backend_get_source (E_BACKEND (cbw));

	if (!cbw->priv->source) {
		ESourceWeather *extension;
		const gchar *extension_name;
		gchar *location;

		extension_name = E_SOURCE_EXTENSION_WEATHER_BACKEND;
		extension = e_source_get_extension (source, extension_name);

		location = e_source_weather_dup_location (extension);
		cbw->priv->source = e_weather_source_new (location);
		if (cbw->priv->source == NULL) {
			g_warning (
				"Invalid weather location '%s' "
				"for calendar '%s'",
				location,
				e_source_get_display_name (source));
		}
		g_free (location);
	}

	if (!cbw->priv->is_loading && cbw->priv->source != NULL) {
		cbw->priv->is_loading = TRUE;

		e_weather_source_parse (
			cbw->priv->source, (EWeatherSourceFinished)
			finished_retrieval_cb, cbw);
	}
}

static gboolean
reload_cb (gpointer user_data)
{
	ECalBackendWeather *cbw;

	cbw = E_CAL_BACKEND_WEATHER (user_data);

	if (cbw->priv->is_loading)
		return TRUE;

	cbw->priv->reload_timeout_id = 0;
	ecb_weather_begin_retrieval (cbw);

	return FALSE;
}

static void
maybe_start_reload_timeout (ECalBackendWeather *cbw)
{
	ECalBackendWeatherPrivate *priv;
	ESource *source;
	ESourceRefresh *extension;
	const gchar *extension_name;
	guint interval_in_minutes = 0;

	priv = cbw->priv;

	if (priv->reload_timeout_id)
		return;

	source = e_backend_get_source (E_BACKEND (cbw));

	extension_name = E_SOURCE_EXTENSION_REFRESH;
	extension = e_source_get_extension (source, extension_name);

	/* By default, reload every 4 hours. At least for CCF, the forecasts
	 * only come out twice a day, and chances are while the NWS and similar
	 * organizations have some serious bandwidth, they would appreciate it
	 * if we didn't hammer their servers. */
	if (e_source_refresh_get_enabled (extension)) {
		interval_in_minutes =
			e_source_refresh_get_interval_minutes (extension);
		if (interval_in_minutes == 0)
			interval_in_minutes = 240;
	}

	if (interval_in_minutes > 0) {
		priv->reload_timeout_id = e_named_timeout_add_seconds (
			interval_in_minutes * 60, reload_cb, cbw);
	}
}

static void
put_component_to_store (ECalBackendWeather *cb,
                        ECalComponent *comp)
{
	g_warn_if_fail (e_cal_cache_put_component (cb->priv->cache, comp, NULL, 0, E_CACHE_IS_ONLINE, NULL, NULL));
}

static gint
compare_weather_info_by_date (gconstpointer a,
			      gconstpointer b)
{
	GWeatherInfo *nfoa = (GWeatherInfo *) a, *nfob = (GWeatherInfo *) b;

	if (nfoa && nfob) {
		time_t tma, tmb;

		if (!gweather_info_get_value_update (nfoa, &tma))
			tma = 0;

		if (!gweather_info_get_value_update (nfob, &tmb))
			tmb = 0;

		return tma == tmb ? 0 : (tma < tmb ? -1 : 1);
	}

	return nfoa == nfob ? 0 : (nfoa ? 1 : -1);
}

static void
finished_retrieval_cb (GWeatherInfo *info,
                       ECalBackendWeather *cbw)
{
	ECalBackendWeatherPrivate *priv;
	ECalComponent *comp;
	GSList *ids, *link;
	GWeatherTemperatureUnit unit;
	ESource *source;
	ESourceWeather *weather_extension;

	priv = cbw->priv;

	if (info == NULL) {
		e_cal_backend_notify_error (E_CAL_BACKEND (cbw), _("Could not retrieve weather data"));
		return;
	}

	e_backend_ensure_source_status_connected (E_BACKEND (cbw));

	source = e_backend_get_source (E_BACKEND (cbw));
	weather_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEATHER_BACKEND);

	g_mutex_lock (&priv->last_used_mutex);

	priv->last_used_units = e_source_weather_get_units (weather_extension);
	g_free (priv->last_used_location);
	priv->last_used_location = e_source_weather_dup_location (weather_extension);

	if (priv->last_used_units == E_SOURCE_WEATHER_UNITS_CENTIGRADE)
		unit = GWEATHER_TEMP_UNIT_CENTIGRADE;
	else if (priv->last_used_units == E_SOURCE_WEATHER_UNITS_KELVIN)
		unit = GWEATHER_TEMP_UNIT_KELVIN;
	else /* E_SOURCE_WEATHER_UNITS_FAHRENHEIT */
		unit = GWEATHER_TEMP_UNIT_FAHRENHEIT;

	g_mutex_unlock (&priv->last_used_mutex);

	/* update cache */
	if (!e_cal_cache_search_ids (priv->cache, NULL, &ids, NULL, NULL))
		ids = NULL;

	for (link = ids; link; link = g_slist_next (link)) {
		ECalComponentId *id = link->data;

		e_cal_backend_notify_component_removed (E_CAL_BACKEND (cbw), id, NULL, NULL);
	}
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_id_free);
	e_cache_remove_all (E_CACHE (priv->cache), NULL, NULL);

	comp = create_weather (cbw, info, unit, FALSE, NULL);
	if (comp) {
		GSList *orig_forecasts;

		put_component_to_store (cbw, comp);
		e_cal_backend_notify_component_created (E_CAL_BACKEND (cbw), comp);
		g_object_unref (comp);

		orig_forecasts = gweather_info_get_forecast_list (info);
		if (orig_forecasts) {
			time_t info_day = 0;
			GSList *forecasts, *f;

			if (!gweather_info_get_value_update (info, &info_day))
				info_day = 0;

			info_day = info_day / (24 * 60 * 60);

			/* skip the first one, it's for today, which has been added above */
			forecasts = g_slist_copy (orig_forecasts->next);
			forecasts = g_slist_sort (forecasts, compare_weather_info_by_date);

			f = forecasts;
			while (f) {
				time_t current_day;
				GWeatherInfo *nfo = f->data;

				f = g_slist_next (f);

				if (nfo && gweather_info_get_value_update (nfo, &current_day)) {
					GSList *same_day_forecasts = NULL;
					gint current_hour;

					current_hour = current_day % (24 * 60 * 60);
					current_day = current_day / (24 * 60 * 60);

					if (current_day == info_day)
						continue;

					while (f) {
						GWeatherInfo *test_nfo = f->data;
						time_t test_day;

						if (test_nfo && gweather_info_get_value_update (test_nfo, &test_day)) {
							time_t test_hour;

							test_hour = test_day % (24 * 60 * 60);

							if (test_day / (24 * 60 * 60) != current_day)
								break;

							same_day_forecasts = g_slist_prepend (same_day_forecasts, test_nfo);

							/* Use the main GWeatherInfo the one closest to noon */
							if (ABS (test_hour - (12 * 60 * 60)) < ABS (current_hour - (12 * 60 * 60))) {
								nfo = test_nfo;
								current_hour = test_hour;
							}
						}

						f = g_slist_next (f);
					}

					same_day_forecasts = g_slist_reverse (same_day_forecasts);

					comp = create_weather (cbw, nfo, unit, TRUE, same_day_forecasts);
					if (comp) {
						put_component_to_store (cbw, comp);
						e_cal_backend_notify_component_created (E_CAL_BACKEND (cbw), comp);
						g_object_unref (comp);
					}

					g_slist_free (same_day_forecasts);
				}
			}

			g_slist_free (forecasts);
		}
	}

	priv->is_loading = FALSE;
}

static gboolean
begin_retrieval_cb (gpointer user_data)
{
	ECalBackendWeather *cbw = user_data;
	GSource *source;

	source = g_main_current_source ();
	if (g_source_is_destroyed (source))
		return FALSE;

	if (cbw->priv->begin_retrival_id == g_source_get_id (source))
		cbw->priv->begin_retrival_id = 0;

	ecb_weather_begin_retrieval (cbw);

	return FALSE;
}

static const gchar *
get_category (GWeatherInfo *report)
{
	struct {
		const gchar *description;
		const gchar *icon_name;
	} categories[] = {
		{ N_("Weather: Fog"),		"weather-fog" },
		{ N_("Weather: Cloudy Night"),	"weather-few-clouds-night" },
		{ N_("Weather: Cloudy"),	"weather-few-clouds" },
		{ N_("Weather: Overcast"),	"weather-overcast" },
		{ N_("Weather: Showers"),	"weather-showers" },
		{ N_("Weather: Snow"),		"weather-snow" },
		{ N_("Weather: Clear Night"),	"weather-clear-night" },
		{ N_("Weather: Sunny"),		"weather-clear" },
		{ N_("Weather: Thunderstorms"), "weather-storm" },
		{ NULL,				NULL }
	};

	gint i;
	const gchar *icon_name = gweather_info_get_icon_name (report);

	if (!icon_name)
		return NULL;

	for (i = 0; categories[i].description; i++) {
		if (!g_ascii_strncasecmp (categories[i].icon_name,
					      icon_name, strlen (icon_name)))
			return _(categories[i].description);
	}

	return NULL;
}

static gchar *
cal_backend_weather_get_temp (gdouble value,
                              GWeatherTemperatureUnit unit)
{
	switch (unit) {
	case GWEATHER_TEMP_UNIT_FAHRENHEIT:
		/* TRANSLATOR: This is the temperature in degrees Fahrenheit (\302\260 is U+00B0 DEGREE SIGN) */
		return g_strdup_printf (_("%.1f \302\260F"), value);
	case GWEATHER_TEMP_UNIT_CENTIGRADE:
		/* TRANSLATOR: This is the temperature in degrees Celsius (\302\260 is U+00B0 DEGREE SIGN) */
		return g_strdup_printf (_("%.1f \302\260C"), value);
	case GWEATHER_TEMP_UNIT_KELVIN:
		/* TRANSLATOR: This is the temperature in kelvin */
		return g_strdup_printf (_("%.1f K"), value);
	default:
		g_warn_if_reached ();
		break;
	}

	return g_strdup_printf (_("%.1f"), value);
}

static gchar *
describe_forecast (GWeatherInfo *nfo,
		   GWeatherTemperatureUnit unit)
{
	gchar *str, *date, *summary, *temp;
	gdouble tmin = 0.0, tmax = 0.0, temp1 = 0.0;

	date = gweather_info_get_update (nfo);
	summary = gweather_info_get_conditions (nfo);
	if (g_str_equal (summary, "-")) {
	    g_free (summary);
	    summary = gweather_info_get_sky (nfo);
	}

	if (gweather_info_get_value_temp_min (nfo, unit, &tmin) &&
	    gweather_info_get_value_temp_max (nfo, unit, &tmax) &&
	    tmin != tmax) {
		gchar *min, *max;

		min = cal_backend_weather_get_temp (tmin, unit);
		max = cal_backend_weather_get_temp (tmax, unit);

		temp = g_strdup_printf ("%s / %s", min, max);

		g_free (min);
		g_free (max);
	} else if (gweather_info_get_value_temp (nfo, unit, &temp1)) {
		temp = cal_backend_weather_get_temp (temp1, unit);
	} else {
		temp = gweather_info_get_temp (nfo);
	}

	str = g_strdup_printf (" * %s: %s, %s", date, summary, temp);

	g_free (date);
	g_free (summary);
	g_free (temp);

	return str;
}

static ECalComponent *
create_weather (ECalBackendWeather *cbw,
                GWeatherInfo *report,
                GWeatherTemperatureUnit unit,
                gboolean is_forecast,
		GSList *same_day_forecasts)
{
	ECalComponent             *cal_comp;
	ECalComponentText         *comp_summary;
	ICalComponent             *icomp;
	ICalTime              *itt;
	ECalComponentDateTime     *dt;
	gchar			  *uid;
	GSList                    *text_list, *link;
	ECalComponentText         *description;
	gchar                     *tmp, *city_name, *summary_text;
	time_t			   update_time;
	ICalTimezone		  *update_zone = NULL;
	const GWeatherLocation    *location;
	const GWeatherTimezone    *w_timezone;
	gdouble tmin = 0.0, tmax = 0.0, temp = 0.0;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEATHER (cbw), NULL);

	if (!gweather_info_get_value_update (report, &update_time))
		return NULL;

	/* create the component and event object */
	icomp = i_cal_component_new (I_CAL_VEVENT_COMPONENT);
	cal_comp = e_cal_component_new_from_icalcomponent (icomp);

	/* set uid */
	uid = e_util_generate_uid ();
	e_cal_component_set_uid (cal_comp, uid);
	g_free (uid);

	/* use timezone of the location to determine date for which this is set */
	location = gweather_info_get_location (report);
	if (location && (w_timezone = gweather_location_get_timezone ((GWeatherLocation *) location)))
		update_zone = i_cal_timezone_get_builtin_timezone (gweather_timezone_get_tzid ((GWeatherTimezone *) w_timezone));

	if (!update_zone)
		update_zone = i_cal_timezone_get_utc_timezone ();

	/* Set all-day event's date from forecast data - cannot set is_date,
	 * because in that case no timezone conversion is done */
	itt = i_cal_time_new_from_timet_with_zone (update_time, 0, update_zone);
	i_cal_time_set_hour (itt, 0);
	i_cal_time_set_minute (itt, 0);
	i_cal_time_set_second (itt, 0);
	i_cal_time_set_is_date (itt, TRUE);

	dt = e_cal_component_datetime_new (itt, update_zone ? i_cal_timezone_get_tzid (update_zone) : NULL);
	e_cal_component_set_dtstart (cal_comp, dt);
	e_cal_component_datetime_free (dt);

	/* We have to add 1 day to DTEND, as it is not inclusive. */
	i_cal_time_adjust (itt, 1, 0, 0, 0);

	dt = e_cal_component_datetime_new (itt, update_zone ? i_cal_timezone_get_tzid (update_zone) : NULL);
	e_cal_component_set_dtend (cal_comp, dt);
	e_cal_component_datetime_free (dt);
	g_object_unref (itt);

	city_name = gweather_info_get_location_name (report);
	if (gweather_info_get_value_temp_min (report, unit, &tmin) &&
	    gweather_info_get_value_temp_max (report, unit, &tmax) &&
	    tmin != tmax) {
		gchar *min, *max;

		min = cal_backend_weather_get_temp (tmin, unit);
		max = cal_backend_weather_get_temp (tmax, unit);
		summary_text = g_strdup_printf ("%s : %s / %s", city_name, min, max);

		g_free (min);
		g_free (max);
	} else if (gweather_info_get_value_temp (report, unit, &temp)) {
		tmp = cal_backend_weather_get_temp (temp, unit);
		summary_text = g_strdup_printf ("%s : %s", city_name, tmp);

		g_free (tmp);
	} else {
		tmp = gweather_info_get_temp (report);
		summary_text = g_strdup_printf ("%s : %s", city_name, tmp);
		g_free (tmp);
	}
	g_free (city_name);

	comp_summary = e_cal_component_text_new (summary_text, NULL);
	e_cal_component_set_summary (cal_comp, comp_summary);
	e_cal_component_text_free (comp_summary);
	g_free (summary_text);

	summary_text = gweather_info_get_weather_summary (report);

	description = e_cal_component_text_new ("", NULL);
	{
		GString *builder;
		gboolean has_forecast_word = FALSE;

		builder = g_string_new (NULL);

		if (!is_forecast) {
			g_string_append (builder, summary_text);
			g_string_append_c (builder, '\n');
		}

		tmp = NULL;

		for (link = gweather_info_get_forecast_list (report); link; link = g_slist_next (link)) {
			GWeatherInfo *nfo = link->data;

			if (nfo) {
				tmp = describe_forecast (nfo, unit);

				if (tmp && *tmp) {
					if (!has_forecast_word) {
						has_forecast_word = TRUE;

						g_string_append (builder, _("Forecast"));
						g_string_append_c (builder, ':');
						g_string_append_c (builder, '\n');
					}

					g_string_append (builder, tmp);
					g_string_append_c (builder, '\n');
				}

				g_free (tmp);
				tmp = NULL;
			}
		}

		if (same_day_forecasts) {
			g_free (tmp);
			tmp = NULL;

			for (link = same_day_forecasts; link; link = g_slist_next (link)) {
				GWeatherInfo *nfo = link->data;

				if (nfo) {
					tmp = describe_forecast (nfo, unit);

					if (tmp && *tmp) {
						if (!has_forecast_word) {
							has_forecast_word = TRUE;

							g_string_append (builder, _("Forecast"));
							g_string_append_c (builder, ':');
							g_string_append_c (builder, '\n');
						}

						g_string_append (builder, tmp);
						g_string_append_c (builder, '\n');
					}

					g_free (tmp);
					tmp = NULL;
				}
			}
		}

		e_cal_component_text_set_value (description, builder->str);
		g_string_free (builder, TRUE);
		g_free (tmp);
	}

	text_list = g_slist_prepend (NULL, description);
	e_cal_component_set_descriptions (cal_comp, text_list);
	g_slist_free_full (text_list, e_cal_component_text_free);
	g_free (summary_text);

	/* Set category and visibility */
	e_cal_component_set_categories (cal_comp, get_category (report));
	e_cal_component_set_classification (cal_comp, E_CAL_COMPONENT_CLASS_PUBLIC);

	/* Weather is shown as free time */
	e_cal_component_set_transparency (cal_comp, E_CAL_COMPONENT_TRANSP_TRANSPARENT);

	e_cal_component_commit_sequence (cal_comp);

	return cal_comp;
}

static gchar *
e_cal_backend_weather_get_backend_property (ECalBackend *backend,
                                            const gchar *prop_name)
{
	g_return_val_if_fail (prop_name != NULL, FALSE);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (
			","
			E_CAL_STATIC_CAPABILITY_NO_ALARM_REPEAT,
			E_CAL_STATIC_CAPABILITY_NO_AUDIO_ALARMS,
			E_CAL_STATIC_CAPABILITY_NO_DISPLAY_ALARMS,
			E_CAL_STATIC_CAPABILITY_NO_PROCEDURE_ALARMS,
			E_CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT,
			E_CAL_STATIC_CAPABILITY_NO_THISANDFUTURE,
			E_CAL_STATIC_CAPABILITY_NO_THISANDPRIOR,
			E_CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED,
			NULL);

	} else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS) ||
		   g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		/* Weather has no particular email addresses associated with it */
		return NULL;

	} else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_DEFAULT_OBJECT)) {
		return NULL;
	}

	/* Chain up to parent's method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_weather_parent_class)->impl_get_backend_property (backend, prop_name);
}

static void
e_cal_backend_weather_open (ECalBackendSync *backend,
                            EDataCal *cal,
                            GCancellable *cancellable,
                            GError **perror)
{
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;
	const gchar *cache_dir;

	cbw = E_CAL_BACKEND_WEATHER (backend);
	priv = cbw->priv;

	cache_dir = e_cal_backend_get_cache_dir (E_CAL_BACKEND (backend));

	e_cal_backend_set_writable (E_CAL_BACKEND (backend), FALSE);

	if (!priv->cache) {
		GError *local_error = NULL;
		gchar *filename;

		filename = g_build_filename (cache_dir, "cache.db", NULL);
		priv->cache = e_cal_cache_new (filename, NULL, &local_error);
		g_free (filename);

		if (!priv->cache) {
			if (!local_error)
				local_error = EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("Could not create cache file"));
			else
				g_prefix_error (&local_error, _("Could not create cache file: "));

			g_propagate_error (perror, local_error);

			return;
		}

		if (!e_backend_get_online (E_BACKEND (backend)))
			return;

		if (!priv->begin_retrival_id)
			priv->begin_retrival_id = g_idle_add (begin_retrieval_cb, cbw);
	}
}

static void
e_cal_backend_weather_refresh_content (ECalBackendWeather *cbw)
{
	g_return_if_fail (E_IS_CAL_BACKEND_WEATHER (cbw));

	if (!e_cal_backend_is_opened (E_CAL_BACKEND (cbw)) ||
	    cbw->priv->is_loading)
		return;

	if (cbw->priv->reload_timeout_id)
		g_source_remove (cbw->priv->reload_timeout_id);
	cbw->priv->reload_timeout_id = 0;

	/* wait a second, then start reloading */
	cbw->priv->reload_timeout_id = e_named_timeout_add_seconds (1, reload_cb, cbw);
}

static void
e_cal_backend_weather_refresh (ECalBackendSync *backend,
                               EDataCal *cal,
                               GCancellable *cancellable,
                               GError **perror)
{
	e_cal_backend_weather_refresh_content (E_CAL_BACKEND_WEATHER (backend));
}

static void
e_cal_backend_weather_receive_objects (ECalBackendSync *backend,
                                       EDataCal *cal,
                                       GCancellable *cancellable,
                                       const gchar *calobj,
				       ECalOperationFlags opflags,
                                       GError **perror)
{
	g_propagate_error (perror, EC_ERROR (E_CLIENT_ERROR_PERMISSION_DENIED));
}

static void
e_cal_backend_weather_get_object (ECalBackendSync *backend,
                                  EDataCal *cal,
                                  GCancellable *cancellable,
                                  const gchar *uid,
                                  const gchar *rid,
                                  gchar **object,
                                  GError **error)
{
	ECalBackendWeather *cbw = E_CAL_BACKEND_WEATHER (backend);
	ECalBackendWeatherPrivate *priv = cbw->priv;
	ECalComponent *comp = NULL;

	if (!e_cal_cache_get_component (priv->cache, uid, rid, &comp, NULL, NULL))
		comp = NULL;

	if (!comp) {
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND));
		return;
	}

	*object = e_cal_component_get_as_string (comp);
	g_object_unref (comp);
}

static void
e_cal_backend_weather_get_object_list (ECalBackendSync *backend,
                                       EDataCal *cal,
                                       GCancellable *cancellable,
                                       const gchar *sexp_string,
                                       GSList **objects,
                                       GError **perror)
{
	ECalBackendWeather *cbw = E_CAL_BACKEND_WEATHER (backend);
	ECalBackendWeatherPrivate *priv = cbw->priv;
	ECalBackendSExp *sexp;
	ETimezoneCache *timezone_cache;
	GSList *components = NULL, *l;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;

	sexp = e_cal_backend_sexp_new (sexp_string);
	if (sexp == NULL) {
		g_propagate_error (perror, EC_ERROR (E_CLIENT_ERROR_INVALID_QUERY));
		return;
	}

	timezone_cache = E_TIMEZONE_CACHE (backend);

	*objects = NULL;
	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (
		sexp,
		&occur_start,
		&occur_end);

	if (prunning_by_time) {
		if (!e_cal_cache_get_components_in_range (priv->cache, occur_start, occur_end, &components, NULL, NULL))
			components = NULL;
	} else {
		if (!e_cal_cache_search_components (priv->cache, NULL, &components, NULL, NULL))
			components = NULL;
	}

	for (l = components; l != NULL; l = g_slist_next (l)) {
		if (e_cal_backend_sexp_match_comp (sexp, E_CAL_COMPONENT (l->data), timezone_cache))
			*objects = g_slist_append (*objects, e_cal_component_get_as_string (l->data));
	}

	g_slist_foreach (components, (GFunc) g_object_unref, NULL);
	g_slist_free (components);
	g_object_unref (sexp);
}

static void
e_cal_backend_weather_add_timezone (ECalBackendSync *backend,
                                    EDataCal *cal,
                                    GCancellable *cancellable,
                                    const gchar *tzobj,
                                    GError **error)
{
	ICalComponent *tz_comp;
	ICalTimezone *zone;

	tz_comp = i_cal_parser_parse_string (tzobj);
	if (tz_comp == NULL) {
		g_set_error_literal (
			error, E_CAL_CLIENT_ERROR,
			E_CAL_CLIENT_ERROR_INVALID_OBJECT,
			e_cal_client_error_to_string (
			E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return;
	}

	if (i_cal_component_isa (tz_comp) != I_CAL_VTIMEZONE_COMPONENT) {
		g_object_unref (tz_comp);
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return;
	}

	zone = i_cal_timezone_new ();
	if (i_cal_timezone_set_component (zone, tz_comp))
		e_timezone_cache_add_timezone (E_TIMEZONE_CACHE (backend), zone);
	g_object_unref (zone);
	g_object_unref (tz_comp);
}

static void
e_cal_backend_weather_get_free_busy (ECalBackendSync *backend,
                                     EDataCal *cal,
                                     GCancellable *cancellable,
                                     const GSList *users,
                                     time_t start,
                                     time_t end,
                                     GSList **freebusy,
                                     GError **perror)
{
	/* Weather doesn't count as busy time */
	ICalComponent *vfb = i_cal_component_new_vfreebusy ();
	ICalTimezone *utc_zone = i_cal_timezone_get_utc_timezone ();
	ICalTime *itt;
	gchar *calobj;

	itt = i_cal_time_new_from_timet_with_zone (start, FALSE, utc_zone);
	i_cal_component_set_dtstart (vfb, itt);
	g_object_unref (itt);

	itt = i_cal_time_new_from_timet_with_zone (end, FALSE, utc_zone);
	i_cal_component_set_dtend (vfb, itt);
	g_object_unref (itt);

	calobj = i_cal_component_as_ical_string (vfb);
	*freebusy = g_slist_append (NULL, calobj);
	g_object_unref (vfb);
}

static void
e_cal_backend_weather_start_view (ECalBackend *backend,
                                  EDataCalView *query)
{
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;
	ECalBackendSExp *sexp;
	ETimezoneCache *timezone_cache;
	GSList *components = NULL, *l;
	GSList *objects;
	GError *error;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;

	cbw = E_CAL_BACKEND_WEATHER (backend);
	priv = cbw->priv;

	if (!priv->cache) {
		error = ECC_ERROR (E_CAL_CLIENT_ERROR_NO_SUCH_CALENDAR);
		e_data_cal_view_notify_complete (query, error);
		g_error_free (error);
		return;
	}

	sexp = e_data_cal_view_get_sexp (query);
	if (!sexp) {
		error = EC_ERROR (E_CLIENT_ERROR_INVALID_QUERY);
		e_data_cal_view_notify_complete (query, error);
		g_error_free (error);
		return;
	}

	timezone_cache = E_TIMEZONE_CACHE (backend);

	objects = NULL;
	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (sexp, &occur_start, &occur_end);
	if (prunning_by_time) {
		if (!e_cal_cache_get_components_in_range (priv->cache, occur_start, occur_end, &components, NULL, NULL))
			components = NULL;
	} else {
		if (!e_cal_cache_search_components (priv->cache, NULL, &components, NULL, NULL))
			components = NULL;
	}

	for (l = components; l != NULL; l = g_slist_next (l)) {
		if (e_cal_backend_sexp_match_comp (sexp, E_CAL_COMPONENT (l->data), timezone_cache))
			objects = g_slist_prepend (objects, l->data);
	}

	if (objects)
		e_data_cal_view_notify_components_added (query, objects);

	g_slist_free_full (components, g_object_unref);
	g_slist_free (objects);

	e_data_cal_view_notify_complete (query, NULL /* Success */);
}

static void
e_cal_backend_weather_notify_online_cb (ECalBackend *backend,
                                        GParamSpec *pspec)
{
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;
	gboolean loaded;

	cbw = E_CAL_BACKEND_WEATHER (backend);
	priv = cbw->priv;

	loaded = e_cal_backend_is_opened (backend);

	if (loaded && priv->reload_timeout_id) {
		g_source_remove (priv->reload_timeout_id);
		priv->reload_timeout_id = 0;
	}

	if (loaded)
		e_cal_backend_set_writable (backend, FALSE);
}

static void
e_cal_backend_weather_source_changed_cb (ESource *source,
					 ECalBackendWeather *cbw)
{
	ESourceWeather *weather_extension;
	gchar *location;

	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (E_IS_CAL_BACKEND_WEATHER (cbw));

	weather_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEATHER_BACKEND);
	location = e_source_weather_dup_location (weather_extension);

	g_mutex_lock (&cbw->priv->last_used_mutex);

	if (cbw->priv->last_used_units != e_source_weather_get_units (weather_extension) ||
	    g_strcmp0 (location, cbw->priv->last_used_location) != 0) {
		g_mutex_unlock (&cbw->priv->last_used_mutex);

		e_cal_backend_weather_refresh_content (cbw);
	} else {
		g_mutex_unlock (&cbw->priv->last_used_mutex);
	}

	g_free (location);
}

static void
e_cal_backend_weather_constructed (GObject *object)
{
	ECalBackendWeather *cbw;
	ESource *source;
	ESourceWeather *weather_extension;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_weather_parent_class)->constructed (object);

	cbw = E_CAL_BACKEND_WEATHER (object);
	source = e_backend_get_source (E_BACKEND (cbw));

	g_return_if_fail (source != NULL);

	weather_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEATHER_BACKEND);

	cbw->priv->last_used_units = e_source_weather_get_units (weather_extension);
	cbw->priv->source_changed_id = g_signal_connect (source, "changed", G_CALLBACK (e_cal_backend_weather_source_changed_cb), cbw);
}

static void
e_cal_backend_weather_dispose (GObject *object)
{
	ECalBackendWeatherPrivate *priv;

	priv = E_CAL_BACKEND_WEATHER (object)->priv;

	if (priv->reload_timeout_id) {
		g_source_remove (priv->reload_timeout_id);
		priv->reload_timeout_id = 0;
	}

	if (priv->begin_retrival_id) {
		g_source_remove (priv->begin_retrival_id);
		priv->begin_retrival_id = 0;
	}

	if (priv->source_changed_id) {
		ESource *source;

		source = e_backend_get_source (E_BACKEND (object));
		if (source) {
			g_signal_handler_disconnect (source, priv->source_changed_id);
		}

		priv->source_changed_id = 0;
	}

	g_clear_object (&priv->source);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_cal_backend_weather_parent_class)->dispose (object);
}

static void
e_cal_backend_weather_finalize (GObject *object)
{
	ECalBackendWeatherPrivate *priv;

	priv = E_CAL_BACKEND_WEATHER (object)->priv;

	g_clear_object (&priv->cache);
	g_free (priv->last_used_location);
	g_mutex_clear (&priv->last_used_mutex);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_backend_weather_parent_class)->finalize (object);
}

/* Object initialization function for the weather backend */
static void
e_cal_backend_weather_init (ECalBackendWeather *cbw)
{
	cbw->priv = e_cal_backend_weather_get_instance_private (cbw);

	g_mutex_init (&cbw->priv->last_used_mutex);

	g_signal_connect (
		cbw, "notify::online",
		G_CALLBACK (e_cal_backend_weather_notify_online_cb), NULL);
}

/* Class initialization function for the weather backend */
static void
e_cal_backend_weather_class_init (ECalBackendWeatherClass *class)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;
	ECalBackendSyncClass *sync_class;

	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;
	sync_class = (ECalBackendSyncClass *) class;

	object_class->constructed = e_cal_backend_weather_constructed;
	object_class->dispose = e_cal_backend_weather_dispose;
	object_class->finalize = e_cal_backend_weather_finalize;

	/* Execute one method at a time. */
	backend_class->use_serial_dispatch_queue = TRUE;

	backend_class->impl_get_backend_property = e_cal_backend_weather_get_backend_property;
	backend_class->impl_start_view = e_cal_backend_weather_start_view;

	sync_class->open_sync = e_cal_backend_weather_open;
	sync_class->refresh_sync = e_cal_backend_weather_refresh;
	sync_class->receive_objects_sync = e_cal_backend_weather_receive_objects;
	sync_class->get_object_sync = e_cal_backend_weather_get_object;
	sync_class->get_object_list_sync = e_cal_backend_weather_get_object_list;
	sync_class->add_timezone_sync = e_cal_backend_weather_add_timezone;
	sync_class->get_free_busy_sync = e_cal_backend_weather_get_free_busy;

	/* Register our ESource extension. */
	g_type_ensure (E_TYPE_SOURCE_WEATHER);
}

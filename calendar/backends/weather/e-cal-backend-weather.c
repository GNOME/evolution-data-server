/* Evolution calendar - weather backend
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

#include <config.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#include "e-cal-backend-weather.h"
#include "e-source-weather.h"
#include "e-weather-source.h"

#define GWEATHER_I_KNOW_THIS_IS_UNSTABLE
#include <libgweather/gweather-weather.h>
#undef GWEATHER_I_KNOW_THIS_IS_UNSTABLE

#define WEATHER_UID_EXT "-weather"

#define E_CAL_BACKEND_WEATHER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL_BACKEND_WEATHER, ECalBackendWeatherPrivate))

#define EDC_ERROR(_code) e_data_cal_create_error (_code, NULL)
#define EDC_ERROR_EX(_code, _msg) e_data_cal_create_error (_code, _msg)

G_DEFINE_TYPE (ECalBackendWeather, e_cal_backend_weather, E_TYPE_CAL_BACKEND_SYNC)

static gboolean reload_cb (ECalBackendWeather *cbw);
static gboolean begin_retrieval_cb (ECalBackendWeather *cbw);
static ECalComponent * create_weather (ECalBackendWeather *cbw, GWeatherInfo *report, gboolean is_forecast);
static void e_cal_backend_weather_add_timezone (ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *tzobj, GError **perror);

/* Private part of the ECalBackendWeather structure */
struct _ECalBackendWeatherPrivate {
	/* URI to get remote weather data from */
	gchar *uri;

	/* The file cache */
	ECalBackendStore *store;

	GHashTable *zones;

	/* Reload */
	guint reload_timeout_id;
	guint is_loading : 1;

	/* Flags */
	gboolean opened;

	/* City (for summary) */
	gchar *city;

	/* Weather source */
	EWeatherSource *source;

	guint begin_retrival_id;
};

static gboolean
reload_cb (ECalBackendWeather *cbw)
{
	ECalBackendWeatherPrivate *priv;

	priv = cbw->priv;

	if (priv->is_loading)
		return TRUE;

	priv->reload_timeout_id = 0;
	priv->opened = TRUE;
	begin_retrieval_cb (cbw);
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

	if (interval_in_minutes > 0)
		priv->reload_timeout_id = g_timeout_add_seconds (
			interval_in_minutes * 60,
			(GSourceFunc) reload_cb, cbw);
}

/* TODO Do not replicate this in every backend */
static icaltimezone *
resolve_tzid (const gchar *tzid,
              gpointer user_data)
{
	icaltimezone *zone;

	zone = (!strcmp (tzid, "UTC"))
		? icaltimezone_get_utc_timezone ()
		: icaltimezone_get_builtin_timezone_from_tzid (tzid);

	if (!zone)
		zone = e_cal_backend_internal_get_timezone (E_CAL_BACKEND (user_data), tzid);

	return zone;
}

static void
put_component_to_store (ECalBackendWeather *cb,
                        ECalComponent *comp)
{
	time_t time_start, time_end;
	ECalBackendWeatherPrivate *priv;

	priv = cb->priv;

	e_cal_util_get_component_occur_times (
		comp, &time_start, &time_end,
		resolve_tzid, cb, icaltimezone_get_utc_timezone (),
		e_cal_backend_get_kind (E_CAL_BACKEND (cb)));

	e_cal_backend_store_put_component_with_time_range (priv->store, comp, time_start, time_end);
}

static void
finished_retrieval_cb (GWeatherInfo *info,
                       ECalBackendWeather *cbw)
{
	ECalBackendWeatherPrivate *priv;
	ECalComponent *comp;
	GSList *comps, *l;

	priv = cbw->priv;

	if (info == NULL) {
		e_cal_backend_notify_error (E_CAL_BACKEND (cbw), _("Could not retrieve weather data"));
		return;
	}

	/* update cache */
	comps = e_cal_backend_store_get_components (priv->store);

	for (l = comps; l != NULL; l = g_slist_next (l)) {
		ECalComponentId *id;

		comp = E_CAL_COMPONENT (l->data);
		id = e_cal_component_get_id (comp);

		e_cal_backend_notify_component_removed (E_CAL_BACKEND (cbw), id, comp, NULL);

		e_cal_component_free_id (id);
		g_object_unref (comp);
	}
	g_slist_free (comps);
	e_cal_backend_store_clean (priv->store);

	comp = create_weather (cbw, info, FALSE);
	if (comp) {
		GSList *forecasts;

		put_component_to_store (cbw, comp);
		e_cal_backend_notify_component_created (E_CAL_BACKEND (cbw), comp);
		g_object_unref (comp);

		forecasts = gweather_info_get_forecast_list (info);
		if (forecasts) {
			GSList *f;

			/* skip the first one, it's for today, which has been added above */
			for (f = forecasts->next; f; f = f->next) {
				GWeatherInfo *nfo = f->data;

				if (nfo) {
					comp = create_weather (cbw, nfo, TRUE);
					if (comp) {
						put_component_to_store (cbw, comp);
						e_cal_backend_notify_component_created (E_CAL_BACKEND (cbw), comp);
						g_object_unref (comp);
					}
				}
			}
		}
	}

	priv->is_loading = FALSE;
}

static gboolean
begin_retrieval_cb (ECalBackendWeather *cbw)
{
	ECalBackendWeatherPrivate *priv = cbw->priv;
	ESource *e_source;
	GSource *source;

	/* XXX Too much overloading of the word 'source' here! */

	if (!e_backend_get_online (E_BACKEND (cbw)))
		return TRUE;

	maybe_start_reload_timeout (cbw);

	e_source = e_backend_get_source (E_BACKEND (cbw));

	if (priv->source == NULL) {
		ESourceWeather *extension;
		const gchar *extension_name;
		gchar *location;

		extension_name = E_SOURCE_EXTENSION_WEATHER_BACKEND;
		extension = e_source_get_extension (e_source, extension_name);

		location = e_source_weather_dup_location (extension);
		priv->source = e_weather_source_new (location);
		g_free (location);
	}

	source = g_main_current_source ();

	if (priv->begin_retrival_id == g_source_get_id (source))
		priv->begin_retrival_id = 0;

	if (priv->is_loading)
		return FALSE;

	priv->is_loading = TRUE;

	e_weather_source_parse (
		priv->source, (EWeatherSourceFinished)
		finished_retrieval_cb, cbw);

	return FALSE;
}

static const gchar *
getCategory (GWeatherInfo *report)
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

static ECalComponent *
create_weather (ECalBackendWeather *cbw,
                GWeatherInfo *report,
                gboolean is_forecast)
{
	ECalBackendWeatherPrivate *priv;
	ECalComponent             *cal_comp;
	ECalComponentText          comp_summary;
	icalcomponent             *ical_comp;
	struct icaltimetype        itt;
	ECalComponentDateTime      dt;
	gchar			  *uid;
	GSList                    *text_list = NULL;
	ECalComponentText         *description;
	gchar                     *tmp;
	time_t			   update_time;
	icaltimezone		  *update_zone = NULL;
	const GWeatherLocation    *location;
	const GWeatherTimezone    *w_timezone;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEATHER (cbw), NULL);

	if (!gweather_info_get_value_update (report, &update_time))
		return NULL;

	priv = cbw->priv;

	/* create the component and event object */
	ical_comp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	cal_comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (cal_comp, ical_comp);

	/* set uid */
	uid = e_cal_component_gen_uid ();
	e_cal_component_set_uid (cal_comp, uid);
	g_free (uid);

	/* use timezone of the location to determine date for which this is set */
	location = gweather_info_get_location (report);
	if (location && (w_timezone = gweather_location_get_timezone ((GWeatherLocation *) location)))
		update_zone = icaltimezone_get_builtin_timezone (gweather_timezone_get_tzid ((GWeatherTimezone *) w_timezone));

	if (!update_zone)
		update_zone = icaltimezone_get_utc_timezone ();

	/* Set all-day event's date from forecast data - cannot set is_date,
	 * because in that case no timezone conversion is done */
	itt = icaltime_from_timet_with_zone (update_time, 0, update_zone);
	itt.hour = 0;
	itt.minute = 0;
	itt.second = 0;
	itt.is_date = 1;

	dt.value = &itt;
	if (update_zone)
		dt.tzid = icaltimezone_get_tzid (update_zone);
	else
		dt.tzid = NULL;

	e_cal_component_set_dtstart (cal_comp, &dt);

	icaltime_adjust (&itt, 1, 0, 0, 0);
	/* We have to add 1 day to DTEND, as it is not inclusive. */
	e_cal_component_set_dtend (cal_comp, &dt);

	{
		gdouble tmin = 0.0, tmax = 0.0;

		if (gweather_info_get_value_temp_min (report, GWEATHER_TEMP_UNIT_DEFAULT, &tmin) &&
		    gweather_info_get_value_temp_max (report, GWEATHER_TEMP_UNIT_DEFAULT, &tmax) &&
		    tmin != tmax) {
			gchar *min, *max;

			min = gweather_info_get_temp_min (report);
			max = gweather_info_get_temp_max (report);
			comp_summary.value = g_strdup_printf ("%s : %s / %s", priv->city, min, max);

			g_free (min); g_free (max);
		} else {
			gchar *temp;

			temp = gweather_info_get_temp (report);
			comp_summary.value = g_strdup_printf ("%s : %s", priv->city, temp);

			g_free (temp);
		}
	}
	comp_summary.altrep = NULL;
	e_cal_component_set_summary (cal_comp, &comp_summary);
	g_free ((gchar *) comp_summary.value);

	tmp = gweather_info_get_forecast (report);
	comp_summary.value = gweather_info_get_weather_summary (report);

	description = g_new0 (ECalComponentText, 1);
	{
		GString *builder;

		builder = g_string_new (NULL);

		if (!is_forecast) {
			g_string_append (builder, comp_summary.value);
			g_string_append_c (builder, '\n');
		}
		if (tmp) {
			g_string_append (builder, _("Forecast"));
			g_string_append_c (builder, ':');
			if (!is_forecast)
				g_string_append_c (builder, '\n');
			g_string_append (builder, tmp);
		}

		description->value = g_string_free (builder, FALSE);
		g_free (tmp);
	}
	description->altrep = "";
	text_list = g_slist_append (text_list, description);
	e_cal_component_set_description_list (cal_comp, text_list);
	g_free ((gchar *) comp_summary.value);

	/* Set category and visibility */
	e_cal_component_set_categories (cal_comp, getCategory (report));
	e_cal_component_set_classification (cal_comp, E_CAL_COMPONENT_CLASS_PUBLIC);

	/* Weather is shown as free time */
	e_cal_component_set_transparency (cal_comp, E_CAL_COMPONENT_TRANSP_TRANSPARENT);

	e_cal_component_commit_sequence (cal_comp);

	return cal_comp;
}

static gboolean
e_cal_backend_weather_get_backend_property (ECalBackendSync *backend,
                                            EDataCal *cal,
                                            GCancellable *cancellable,
                                            const gchar *prop_name,
                                            gchar **prop_value,
                                            GError **perror)
{
	gboolean processed = TRUE;

	g_return_val_if_fail (prop_name != NULL, FALSE);
	g_return_val_if_fail (prop_value != NULL, FALSE);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		*prop_value = g_strdup (CAL_STATIC_CAPABILITY_NO_ALARM_REPEAT ","
					CAL_STATIC_CAPABILITY_NO_AUDIO_ALARMS ","
					CAL_STATIC_CAPABILITY_NO_DISPLAY_ALARMS ","
					CAL_STATIC_CAPABILITY_NO_PROCEDURE_ALARMS ","
					CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT ","
					CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
					CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
					CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED);
	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS) ||
		   g_str_equal (prop_name, CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		/* Weather has no particular email addresses associated with it */
		*prop_value = NULL;
	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_DEFAULT_OBJECT)) {
		g_propagate_error (perror, EDC_ERROR (UnsupportedMethod));
	} else {
		processed = FALSE;
	}

	return processed;
}

static void
e_cal_backend_weather_open (ECalBackendSync *backend,
                            EDataCal *cal,
                            GCancellable *cancellable,
                            gboolean only_if_exists,
                            GError **perror)
{
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;
	ESource *source;
	ESourceWeather *extension;
	const gchar *extension_name;
	const gchar *cache_dir;
	gchar *location;
	gboolean online;

	cbw = E_CAL_BACKEND_WEATHER (backend);
	priv = cbw->priv;

	source = e_backend_get_source (E_BACKEND (backend));
	cache_dir = e_cal_backend_get_cache_dir (E_CAL_BACKEND (backend));

	extension_name = E_SOURCE_EXTENSION_WEATHER_BACKEND;
	extension = e_source_get_extension (source, extension_name);

	g_free (priv->city);

	location = e_source_weather_dup_location (extension);
	priv->city = g_strdup (strrchr (location, '/') + 1);
	g_free (location);

	e_cal_backend_notify_readonly (E_CAL_BACKEND (backend), TRUE);

	online = e_backend_get_online (E_BACKEND (backend));
	e_cal_backend_notify_online (E_CAL_BACKEND (backend), online);

	if (!priv->store) {
		e_cal_backend_cache_remove (cache_dir, "cache.xml");
		priv->store = e_cal_backend_file_store_new (cache_dir);

		if (!priv->store) {
			g_propagate_error (perror, EDC_ERROR_EX (OtherError, _("Could not create cache file")));
			return;
		}

		/* do we require to load this new store */
		e_cal_backend_store_load (priv->store);
		e_cal_backend_notify_opened (E_CAL_BACKEND (backend), NULL);

		if (!e_backend_get_online (E_BACKEND (backend)))
			return;

		if (!priv->begin_retrival_id)
			priv->begin_retrival_id = g_idle_add ((GSourceFunc) begin_retrieval_cb, cbw);
	}

	e_cal_backend_notify_opened (E_CAL_BACKEND (backend), NULL);
}

static void
e_cal_backend_weather_refresh (ECalBackendSync *backend,
                               EDataCal *cal,
                               GCancellable *cancellable,
                               GError **perror)
{
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;

	cbw = E_CAL_BACKEND_WEATHER (backend);
	priv = cbw->priv;

	if (!priv->opened ||
	    priv->is_loading)
		return;

	if (priv->reload_timeout_id)
		g_source_remove (priv->reload_timeout_id);
	priv->reload_timeout_id = 0;

	/* wait a second, then start reloading */
	priv->reload_timeout_id = g_timeout_add (1000, (GSourceFunc) reload_cb, cbw);
}

static void
e_cal_backend_weather_receive_objects (ECalBackendSync *backend,
                                       EDataCal *cal,
                                       GCancellable *cancellable,
                                       const gchar *calobj,
                                       GError **perror)
{
	g_propagate_error (perror, EDC_ERROR (PermissionDenied));
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
	ECalComponent *comp;

	e_return_data_cal_error_if_fail (uid != NULL, InvalidArg);
	e_return_data_cal_error_if_fail (priv->store != NULL, InvalidArg);

	comp = e_cal_backend_store_get_component (priv->store, uid, rid);
	if (!comp) {
		g_propagate_error (error, EDC_ERROR (ObjectNotFound));
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
	ECalBackendSExp *sexp = e_cal_backend_sexp_new (sexp_string);
	GSList *components, *l;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;

	if (!sexp) {
		g_propagate_error (perror, EDC_ERROR (InvalidQuery));
		return;
	}

	*objects = NULL;
	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (
		sexp,
		&occur_start,
		&occur_end);

	components = prunning_by_time ?
		e_cal_backend_store_get_components_occuring_in_range (priv->store, occur_start, occur_end)
		: e_cal_backend_store_get_components (priv->store);

	for (l = components; l != NULL; l = g_slist_next (l)) {
		if (e_cal_backend_sexp_match_comp (sexp, E_CAL_COMPONENT (l->data), E_CAL_BACKEND (backend)))
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
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;
	icalcomponent *tz_comp;
	icaltimezone *zone;
	const gchar *tzid;

	cbw = (ECalBackendWeather *) backend;

	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_WEATHER (cbw), InvalidArg);
	e_return_data_cal_error_if_fail (tzobj != NULL, InvalidArg);

	priv = cbw->priv;

	tz_comp = icalparser_parse_string (tzobj);
	e_return_data_cal_error_if_fail (tz_comp != NULL, InvalidObject);

	if (icalcomponent_isa (tz_comp) != ICAL_VTIMEZONE_COMPONENT) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);
	tzid = icaltimezone_get_tzid (zone);

	if (g_hash_table_lookup (priv->zones, tzid)) {
		icaltimezone_free (zone, TRUE);
	} else {
		g_hash_table_insert (priv->zones, g_strdup (tzid), zone);
	}
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
	icalcomponent *vfb = icalcomponent_new_vfreebusy ();
	icaltimezone *utc_zone = icaltimezone_get_utc_timezone ();
	gchar *calobj;

	icalcomponent_set_dtstart (vfb, icaltime_from_timet_with_zone (start, FALSE, utc_zone));
	icalcomponent_set_dtend (vfb, icaltime_from_timet_with_zone (end, FALSE, utc_zone));

	calobj = icalcomponent_as_ical_string_r (vfb);
	*freebusy = g_slist_append (NULL, calobj);
	icalcomponent_free (vfb);
}

static void
e_cal_backend_weather_start_view (ECalBackend *backend,
                                  EDataCalView *query)
{
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;
	ECalBackendSExp *sexp;
	GSList *components, *l;
	GSList *objects;
	GError *error;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;

	cbw = E_CAL_BACKEND_WEATHER (backend);
	priv = cbw->priv;

	if (!priv->store) {
		error = EDC_ERROR (NoSuchCal);
		e_data_cal_view_notify_complete (query, error);
		g_error_free (error);
		return;
	}

	sexp = e_data_cal_view_get_object_sexp (query);
	if (!sexp) {
		error = EDC_ERROR (InvalidQuery);
		e_data_cal_view_notify_complete (query, error);
		g_error_free (error);
		return;
	}

	objects = NULL;
	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (sexp, &occur_start, &occur_end);
	components = prunning_by_time ?
		e_cal_backend_store_get_components_occuring_in_range (priv->store, occur_start, occur_end)
		: e_cal_backend_store_get_components (priv->store);

	for (l = components; l != NULL; l = g_slist_next (l)) {
		if (e_cal_backend_sexp_match_comp (sexp, E_CAL_COMPONENT (l->data), backend))
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
	gboolean online;

	cbw = E_CAL_BACKEND_WEATHER (backend);
	priv = cbw->priv;

	online = e_backend_get_online (E_BACKEND (backend));
	loaded = e_cal_backend_is_opened (backend);

	if (loaded && priv->reload_timeout_id) {
		g_source_remove (priv->reload_timeout_id);
		priv->reload_timeout_id = 0;
	}

	if (loaded) {
		e_cal_backend_notify_online (backend, online);
		e_cal_backend_notify_readonly (backend, TRUE);
	}
}

static icaltimezone *
e_cal_backend_weather_internal_get_timezone (ECalBackend *backend,
                                             const gchar *tzid)
{
	icaltimezone *zone;

	g_return_val_if_fail (tzid != NULL, NULL);

	if (!strcmp (tzid, "UTC")) {
		zone = icaltimezone_get_utc_timezone ();
	} else {
		ECalBackendWeather *cbw = E_CAL_BACKEND_WEATHER (backend);

		g_return_val_if_fail (E_IS_CAL_BACKEND_WEATHER (cbw), NULL);

		zone = g_hash_table_lookup (cbw->priv->zones, tzid);

		if (!zone && E_CAL_BACKEND_CLASS (e_cal_backend_weather_parent_class)->internal_get_timezone)
			zone = E_CAL_BACKEND_CLASS (e_cal_backend_weather_parent_class)->internal_get_timezone (backend, tzid);
	}

	return zone;
}

static void
free_zone (gpointer data)
{
	icaltimezone_free (data, TRUE);
}

/* Finalize handler for the weather backend */
static void
e_cal_backend_weather_finalize (GObject *object)
{
	ECalBackendWeatherPrivate *priv;

	priv = E_CAL_BACKEND_WEATHER_GET_PRIVATE (object);

	if (priv->reload_timeout_id)
		g_source_remove (priv->reload_timeout_id);

	if (priv->begin_retrival_id)
		g_source_remove (priv->begin_retrival_id);

	if (priv->store) {
		g_object_unref (priv->store);
		priv->store = NULL;
	}

	g_hash_table_destroy (priv->zones);

	g_free (priv->city);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_backend_weather_parent_class)->finalize (object);
}

/* Object initialization function for the weather backend */
static void
e_cal_backend_weather_init (ECalBackendWeather *cbw)
{
	cbw->priv = E_CAL_BACKEND_WEATHER_GET_PRIVATE (cbw);

	cbw->priv->zones = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) free_zone);

	e_cal_backend_sync_set_lock (E_CAL_BACKEND_SYNC (cbw), TRUE);

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

	g_type_class_add_private (class, sizeof (ECalBackendWeatherPrivate));

	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;
	sync_class = (ECalBackendSyncClass *) class;

	object_class->finalize = e_cal_backend_weather_finalize;

	sync_class->get_backend_property_sync	= e_cal_backend_weather_get_backend_property;
	sync_class->open_sync			= e_cal_backend_weather_open;
	sync_class->refresh_sync		= e_cal_backend_weather_refresh;
	sync_class->receive_objects_sync	= e_cal_backend_weather_receive_objects;
	sync_class->get_object_sync		= e_cal_backend_weather_get_object;
	sync_class->get_object_list_sync	= e_cal_backend_weather_get_object_list;
	sync_class->add_timezone_sync		= e_cal_backend_weather_add_timezone;
	sync_class->get_free_busy_sync		= e_cal_backend_weather_get_free_busy;

	backend_class->start_view		= e_cal_backend_weather_start_view;
	backend_class->internal_get_timezone	= e_cal_backend_weather_internal_get_timezone;

	/* Register our ESource extension. */
	E_TYPE_SOURCE_WEATHER;
}

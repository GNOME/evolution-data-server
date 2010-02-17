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
#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-file-store.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libedata-cal/e-cal-backend-sexp.h>
#include <glib/gi18n-lib.h>
#include <string.h>
#include "e-cal-backend-weather.h"
#include "e-weather-source.h"

#define GWEATHER_I_KNOW_THIS_IS_UNSTABLE
#include <libgweather/weather.h>
#undef GWEATHER_I_KNOW_THIS_IS_UNSTABLE

#define WEATHER_UID_EXT "-weather"

G_DEFINE_TYPE (ECalBackendWeather, e_cal_backend_weather, E_TYPE_CAL_BACKEND_SYNC)

static gboolean reload_cb (ECalBackendWeather *cbw);
static gboolean begin_retrieval_cb (ECalBackendWeather *cbw);
static ECalComponent* create_weather (ECalBackendWeather *cbw, WeatherInfo *report, gboolean is_forecast);
static ECalBackendSyncStatus
e_cal_backend_weather_add_timezone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj);

/* Private part of the ECalBackendWeather structure */
struct _ECalBackendWeatherPrivate {
	/* URI to get remote weather data from */
	gchar *uri;

	/* Local/remote mode */
	CalMode mode;

	/* The file cache */
	ECalBackendStore *store;

	/* The calendar's default timezone, used for resolving DATE and
	   floating DATE-TIME values. */
	icaltimezone *default_zone;
	GHashTable *zones;

	/* Reload */
	guint reload_timeout_id;
	guint source_changed_id;
	guint is_loading : 1;

	/* Flags */
	gboolean opened;

	/* City (for summary) */
	gchar *city;

	/* Weather source */
	EWeatherSource *source;

	guint begin_retrival_id;
};

static ECalBackendSyncClass *parent_class;

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
source_changed (ESource *source, ECalBackendWeather *cbw)
{
	/* FIXME
	 * We should force a reload of the data when this gets called. Unfortunately,
	 * this signal isn't getting through from evolution to the backend
	 */
}

static void
maybe_start_reload_timeout (ECalBackendWeather *cbw)
{
	ECalBackendWeatherPrivate *priv;
	ESource *source;
	const gchar *refresh_str;

	priv = cbw->priv;

	if (priv->reload_timeout_id)
		return;

	source = e_cal_backend_get_source (E_CAL_BACKEND (cbw));
	if (!source) {
		g_warning ("Could not get source for ECalBackendWeather reload.");
		return;
	}

	if (priv->source_changed_id == 0)
		priv->source_changed_id = g_signal_connect (G_OBJECT (source),
							    "changed",
							    G_CALLBACK (source_changed),
							    cbw);

	refresh_str = e_source_get_property (source, "refresh");

	/* By default, reload every 4 hours. At least for CCF, the forecasts only come out
	 * twice a day, and chances are while the NWS and similar organizations have some
	 * serious bandwidth, they would appreciate it if we didn't hammer their servers
	 */
	priv->reload_timeout_id = g_timeout_add ((refresh_str ? atoi (refresh_str) : 240) * 60000,
						 (GSourceFunc) reload_cb, cbw);

}

static void
finished_retrieval_cb (WeatherInfo *info, ECalBackendWeather *cbw)
{
	ECalBackendWeatherPrivate *priv;
	ECalComponent *comp;
	icalcomponent *icomp;
	GSList *l;
	gchar *obj;

	priv = cbw->priv;

	if (info == NULL) {
		e_cal_backend_notify_error (E_CAL_BACKEND (cbw), _("Could not retrieve weather data"));
		return;
	}

	/* update cache */
	l = e_cal_backend_store_get_components (priv->store);
	for (; l != NULL; l = g_slist_next (l)) {
		ECalComponentId *id;
		gchar *obj;

		icomp = e_cal_component_get_icalcomponent (E_CAL_COMPONENT (l->data));
		id = e_cal_component_get_id (E_CAL_COMPONENT (l->data));

		obj = icalcomponent_as_ical_string_r (icomp);
		e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbw),
			id,
			obj,
			NULL);

		e_cal_component_free_id (id);
		g_free (obj);
		g_object_unref (G_OBJECT (l->data));
	}
	g_slist_free (l);
	e_cal_backend_store_clean (priv->store);

	comp = create_weather (cbw, info, FALSE);
	if (comp) {
		GSList *forecasts;

		e_cal_backend_store_put_component (priv->store, comp);
		icomp = e_cal_component_get_icalcomponent (comp);
		obj = icalcomponent_as_ical_string_r (icomp);
		e_cal_backend_notify_object_created (E_CAL_BACKEND (cbw), obj);
		g_free (obj);
		g_object_unref (comp);

		forecasts = weather_info_get_forecast_list (info);
		if (forecasts) {
			GSList *f;

			/* skip the first one, it's for today, which has been added above */
			for (f = forecasts->next; f; f = f->next) {
				WeatherInfo *nfo = f->data;

				if (nfo) {
					comp = create_weather (cbw, nfo, TRUE);
					if (comp) {
						e_cal_backend_store_put_component (priv->store, comp);
						icomp = e_cal_component_get_icalcomponent (comp);
						obj = icalcomponent_as_ical_string_r (icomp);
						e_cal_backend_notify_object_created (E_CAL_BACKEND (cbw), obj);
						g_free (obj);
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
	GSource *source;

	if (priv->mode != CAL_MODE_REMOTE)
		return TRUE;

	maybe_start_reload_timeout (cbw);

	if (priv->source == NULL)
		priv->source = e_weather_source_new (e_cal_backend_get_uri (E_CAL_BACKEND (cbw)));

	source = g_main_current_source ();

	if (priv->begin_retrival_id == g_source_get_id (source))
		priv->begin_retrival_id = 0;

	if (priv->is_loading)
		return FALSE;

	priv->is_loading = TRUE;

	e_weather_source_parse (priv->source, (EWeatherSourceFinished) finished_retrieval_cb, cbw);

	return FALSE;
}

static const gchar *
getCategory (WeatherInfo *report)
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
	const gchar *icon_name = weather_info_get_icon_name (report);

	if (!icon_name)
		return NULL;

	for (i = 0; categories [i].description; i++) {
		if (!g_ascii_strncasecmp (categories [i].icon_name,
					      icon_name, strlen(icon_name)))
			return _(categories [i].description);
	}

	return NULL;
}

static ECalComponent*
create_weather (ECalBackendWeather *cbw, WeatherInfo *report, gboolean is_forecast)
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
	ESource                   *source;
	gboolean                   metric;
	const gchar                *tmp;
	time_t			   update_time;
	icaltimezone		  *update_zone = NULL;
	const WeatherLocation     *location;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEATHER (cbw), NULL);

	if (!weather_info_get_value_update (report, &update_time))
		return NULL;

	priv = cbw->priv;

	source = e_cal_backend_get_source (E_CAL_BACKEND (cbw));
	tmp = e_source_get_property (source, "units");
	if (tmp == NULL) {
		tmp = e_source_get_property (source, "temperature");
		if (tmp == NULL)
			metric = FALSE;
		else
			metric = (strcmp (tmp, "fahrenheit") != 0);
	} else {
		metric = (strcmp (tmp, "metric") == 0);
	}

	if (metric)
		weather_info_to_metric (report);
	else
		weather_info_to_imperial (report);

	/* create the component and event object */
	ical_comp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	cal_comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (cal_comp, ical_comp);

	/* set uid */
	uid = e_cal_component_gen_uid ();
	e_cal_component_set_uid (cal_comp, uid);
	g_free (uid);

	/* use timezone of the location to determine date for which this is set */
	location = weather_info_get_location (report);
	if (location && location->tz_hint && *location->tz_hint)
		update_zone = icaltimezone_get_builtin_timezone (location->tz_hint);

	if (!update_zone)
		update_zone = priv->default_zone;

	/* Set all-day event's date from forecast data - cannot set is_date,
	   because in that case no timezone conversion is done */
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

	if (is_forecast) {
		gdouble tmin = 0.0, tmax = 0.0;

		if (weather_info_get_value_temp_min (report, TEMP_UNIT_DEFAULT, &tmin) &&
		    weather_info_get_value_temp_max (report, TEMP_UNIT_DEFAULT, &tmax) &&
		    tmin != tmax) {
			/* because weather_info_get_temp* uses one internal buffer, thus finally
			   the last value is shown for both, which is obviously wrong */
			GString *str = g_string_new (priv->city);

			g_string_append (str, " : ");
			g_string_append (str, weather_info_get_temp_min (report));
			g_string_append (str, "/");
			g_string_append (str, weather_info_get_temp_max (report));

			comp_summary.value = g_string_free (str, FALSE);
		} else {
			comp_summary.value = g_strdup_printf ("%s : %s", priv->city, weather_info_get_temp (report));
		}
	} else {
		gdouble tmin = 0.0, tmax = 0.0;
		/* because weather_info_get_temp* uses one internal buffer, thus finally
		   the last value is shown for both, which is obviously wrong */
		GString *str = g_string_new (priv->city);

		g_string_append (str, " : ");
		if (weather_info_get_value_temp_min (report, TEMP_UNIT_DEFAULT, &tmin) &&
		    weather_info_get_value_temp_max (report, TEMP_UNIT_DEFAULT, &tmax) &&
		    tmin != tmax) {
			g_string_append (str, weather_info_get_temp_min (report));
			g_string_append (str, "/");
			g_string_append (str, weather_info_get_temp_max (report));
		} else {
			g_string_append (str, weather_info_get_temp (report));
		}

		comp_summary.value = g_string_free (str, FALSE);
	}
	comp_summary.altrep = NULL;
	e_cal_component_set_summary (cal_comp, &comp_summary);
	g_free ((gchar *)comp_summary.value);

	tmp = weather_info_get_forecast (report);
	comp_summary.value = weather_info_get_weather_summary (report);

	description = g_new0 (ECalComponentText, 1);
	description->value = g_strconcat (is_forecast ? "" : comp_summary.value, is_forecast ? "" : "\n", tmp ? _("Forecast") : "", tmp ? ":" : "", tmp && !is_forecast ? "\n" : "", tmp ? tmp : "", NULL);
	description->altrep = "";
	text_list = g_slist_append (text_list, description);
	e_cal_component_set_description_list (cal_comp, text_list);
	g_free ((gchar *)comp_summary.value);

	/* Set category and visibility */
	e_cal_component_set_categories (cal_comp, getCategory (report));
	e_cal_component_set_classification (cal_comp, E_CAL_COMPONENT_CLASS_PUBLIC);

	/* Weather is shown as free time */
	e_cal_component_set_transparency (cal_comp, E_CAL_COMPONENT_TRANSP_TRANSPARENT);

	e_cal_component_commit_sequence (cal_comp);

	return cal_comp;
}

static ECalBackendSyncStatus
e_cal_backend_weather_is_read_only (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{
	*read_only = TRUE;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_weather_get_cal_address (ECalBackendSync *backend, EDataCal *cal, gchar **address)
{
	/* Weather has no particular email addresses associated with it */
	*address = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_weather_get_alarm_email_address (ECalBackendSync *backend, EDataCal *cal, gchar **address)
{
	/* Weather has no particular email addresses associated with it */
	*address = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_weather_get_ldap_attribute (ECalBackendSync *backend, EDataCal *cal, gchar **attribute)
{
	*attribute = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_weather_get_static_capabilities (ECalBackendSync *backend, EDataCal *cal, gchar **capabilities)
{
	*capabilities = g_strdup (CAL_STATIC_CAPABILITY_NO_ALARM_REPEAT ","
				  CAL_STATIC_CAPABILITY_NO_AUDIO_ALARMS  ","
				  CAL_STATIC_CAPABILITY_NO_DISPLAY_ALARMS  ","
				  CAL_STATIC_CAPABILITY_NO_PROCEDURE_ALARMS  ","
				  CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT  ","
				  CAL_STATIC_CAPABILITY_NO_THISANDFUTURE  ","
				  CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
				  CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_weather_open (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists, const gchar *username, const gchar *password)
{
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;
	const gchar *uri;

	cbw = E_CAL_BACKEND_WEATHER (backend);
	priv = cbw->priv;

	uri = e_cal_backend_get_uri (E_CAL_BACKEND (backend));
	if (priv->city)
		g_free (priv->city);
	priv->city = g_strdup (strrchr (uri, '/') + 1);

	if (!priv->store) {
		e_cal_backend_cache_remove (uri, E_CAL_SOURCE_TYPE_EVENT);
		priv->store = (ECalBackendStore *) e_cal_backend_file_store_new (uri, E_CAL_SOURCE_TYPE_EVENT);

		if (!priv->store) {
			e_cal_backend_notify_error (E_CAL_BACKEND (cbw), _("Could not create cache file"));
			return GNOME_Evolution_Calendar_OtherError;
		}
		e_cal_backend_store_load (priv->store);

		if (priv->default_zone) {
			icalcomponent *icalcomp = icaltimezone_get_component (priv->default_zone);
			icaltimezone *zone = icaltimezone_new ();

			icaltimezone_set_component (zone, icalcomponent_new_clone (icalcomp));

			g_hash_table_insert (priv->zones, g_strdup (icaltimezone_get_tzid (zone)), zone);
		}

		if (priv->mode == CAL_MODE_LOCAL)
			return GNOME_Evolution_Calendar_Success;

		if (!priv->begin_retrival_id)
			priv->begin_retrival_id = g_idle_add ((GSourceFunc) begin_retrieval_cb, cbw);
	}

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_weather_refresh (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;

	cbw = E_CAL_BACKEND_WEATHER (backend);
	priv = cbw->priv;

	if (!priv->opened ||
	    priv->is_loading)
		return GNOME_Evolution_Calendar_Success;

	if (priv->reload_timeout_id)
		g_source_remove (priv->reload_timeout_id);
	priv->reload_timeout_id = 0;

	/* wait a second, then start reloading */
	priv->reload_timeout_id = g_timeout_add (1000, (GSourceFunc) reload_cb, cbw);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_weather_remove (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;

	cbw = E_CAL_BACKEND_WEATHER (backend);
	priv = cbw->priv;

	if (!priv->store) {
		/* lie here a bit, but otherwise the calendar will not be removed, even it should */
		g_print (G_STRLOC ": Doesn't have a cache?!?");
		return GNOME_Evolution_Calendar_Success;
	}

	e_cal_backend_store_remove (priv->store);
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_weather_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *auid)
{
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_weather_receive_objects (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj)
{
	return GNOME_Evolution_Calendar_PermissionDenied;
}

static ECalBackendSyncStatus
e_cal_backend_weather_get_default_object (ECalBackendSync *backend, EDataCal *cal, gchar **object)
{
	return GNOME_Evolution_Calendar_UnsupportedMethod;
}

static ECalBackendSyncStatus
e_cal_backend_weather_get_object (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid, gchar **object)
{
	ECalBackendWeather *cbw = E_CAL_BACKEND_WEATHER (backend);
	ECalBackendWeatherPrivate *priv = cbw->priv;
	ECalComponent *comp;

	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);
	g_return_val_if_fail (priv->store != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	comp = e_cal_backend_store_get_component (priv->store, uid, rid);
	g_return_val_if_fail (comp != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	*object = e_cal_component_get_as_string (comp);
	g_object_unref (comp);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_weather_get_object_list (ECalBackendSync *backend, EDataCal *cal, const gchar *sexp_string, GList **objects)
{
	ECalBackendWeather *cbw = E_CAL_BACKEND_WEATHER (backend);
	ECalBackendWeatherPrivate *priv = cbw->priv;
	ECalBackendSExp *sexp = e_cal_backend_sexp_new (sexp_string);
	GSList *components, *l;

	if (!sexp)
		return GNOME_Evolution_Calendar_InvalidQuery;

	*objects = NULL;
	components = e_cal_backend_store_get_components (priv->store);
	for (l = components; l != NULL; l = g_slist_next (l)) {
		if (e_cal_backend_sexp_match_comp (sexp, E_CAL_COMPONENT (l->data), E_CAL_BACKEND (backend)))
			*objects = g_list_append (*objects, e_cal_component_get_as_string (l->data));
	}

	g_slist_foreach (components, (GFunc) g_object_unref, NULL);
	g_slist_free (components);
	g_object_unref (sexp);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_weather_add_timezone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj)
{
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;
	icalcomponent *tz_comp;
	icaltimezone *zone;
	const gchar *tzid;

	cbw = (ECalBackendWeather*) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEATHER (cbw), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbw->priv;

	tz_comp = icalparser_parse_string (tzobj);
	g_return_val_if_fail (tz_comp != NULL, GNOME_Evolution_Calendar_InvalidObject);

	if (icalcomponent_isa (tz_comp) != ICAL_VTIMEZONE_COMPONENT)
		return GNOME_Evolution_Calendar_InvalidObject;

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);
	tzid = icaltimezone_get_tzid (zone);

	if (g_hash_table_lookup (priv->zones, tzid)) {
		icaltimezone_free (zone, TRUE);
		return GNOME_Evolution_Calendar_Success;
	}

	g_hash_table_insert (priv->zones, g_strdup (tzid), zone);
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_weather_set_default_zone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj)
{
	icalcomponent *tz_comp;
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;
	icaltimezone *zone;

	cbw = (ECalBackendWeather *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEATHER (cbw), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbw->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);

	if (priv->default_zone)
		icaltimezone_free (priv->default_zone, 1);

	/* Set the default timezone to it. */
	priv->default_zone = zone;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_weather_get_free_busy (ECalBackendSync *backend, EDataCal *cal, GList *users, time_t start, time_t end, GList **freebusy)
{
	/* Weather doesn't count as busy time */
	icalcomponent *vfb = icalcomponent_new_vfreebusy ();
	icaltimezone *utc_zone = icaltimezone_get_utc_timezone ();
	gchar *calobj;

	icalcomponent_set_dtstart (vfb, icaltime_from_timet_with_zone (start, FALSE, utc_zone));
	icalcomponent_set_dtend (vfb, icaltime_from_timet_with_zone (end, FALSE, utc_zone));

	calobj = icalcomponent_as_ical_string_r (vfb);
	*freebusy = g_list_append (NULL, calobj);
	icalcomponent_free (vfb);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_weather_get_changes (ECalBackendSync *backend, EDataCal *cal, const gchar *change_id, GList **adds, GList **modifies, GList **deletes)
{
	return GNOME_Evolution_Calendar_Success;
}

static gboolean
e_cal_backend_weather_is_loaded (ECalBackend *backend)
{
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;

	cbw = E_CAL_BACKEND_WEATHER (backend);
	priv = cbw->priv;

	if (!priv->store)
		return FALSE;

	return TRUE;
}

static void e_cal_backend_weather_start_query (ECalBackend *backend, EDataCalView *query)
{
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;
	ECalBackendSExp *sexp;
	GSList *components, *l;
	GList *objects;

	cbw = E_CAL_BACKEND_WEATHER (backend);
	priv = cbw->priv;

	if (!priv->store) {
		e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_NoSuchCal);
		return;
	}

	sexp = e_data_cal_view_get_object_sexp (query);
	if (!sexp) {
		e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_InvalidQuery);
		return;
	}

	objects = NULL;
	components = e_cal_backend_store_get_components (priv->store);
	for (l = components; l != NULL; l = g_slist_next (l)) {
		if (e_cal_backend_sexp_match_comp (sexp, E_CAL_COMPONENT (l->data), backend))
			objects = g_list_append (objects, e_cal_component_get_as_string (l->data));
	}

	if (objects)
		e_data_cal_view_notify_objects_added (query, (const GList *) objects);

	g_slist_foreach (components, (GFunc) g_object_unref, NULL);
	g_slist_free (components);
	g_list_foreach (objects, (GFunc) g_free, NULL);
	g_list_free (objects);
	g_object_unref (sexp);

	e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_Success);
}

static CalMode
e_cal_backend_weather_get_mode (ECalBackend *backend)
{
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;

	cbw = E_CAL_BACKEND_WEATHER (backend);
	priv = cbw->priv;

	return priv->mode;
}

static void
e_cal_backend_weather_set_mode (ECalBackend *backend, CalMode mode)
{
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;
	GNOME_Evolution_Calendar_CalMode set_mode;
	gboolean loaded;

	cbw = E_CAL_BACKEND_WEATHER (backend);
	priv = cbw->priv;

	loaded = e_cal_backend_weather_is_loaded (backend);

	if (priv->mode != mode) {
		switch (mode) {
			case CAL_MODE_LOCAL:
			case CAL_MODE_REMOTE:
				priv->mode = mode;
				set_mode = cal_mode_to_corba (mode);
				if (loaded && priv->reload_timeout_id) {
					g_source_remove (priv->reload_timeout_id);
					priv->reload_timeout_id = 0;
				}
				break;
			case CAL_MODE_ANY:
				priv->mode = mode;
				set_mode = cal_mode_to_corba (mode);
				if (loaded && !priv->begin_retrival_id)
					priv->begin_retrival_id = g_idle_add ((GSourceFunc) begin_retrieval_cb, backend);
				break;
			default:
				set_mode = GNOME_Evolution_Calendar_MODE_ANY;
				break;
		}
	} else {
		set_mode = cal_mode_to_corba (priv->mode);
	}

	if (loaded) {
		if (set_mode == GNOME_Evolution_Calendar_MODE_ANY)
			e_cal_backend_notify_mode (backend,
						   GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED,
						   cal_mode_to_corba (priv->mode));
		else
			e_cal_backend_notify_mode (backend,
						   GNOME_Evolution_Calendar_CalListener_MODE_SET,
						   set_mode);
	}
}

static icaltimezone *
e_cal_backend_weather_internal_get_default_timezone (ECalBackend *backend)
{
	ECalBackendWeather *cbw = E_CAL_BACKEND_WEATHER (backend);

	return cbw->priv->default_zone;
}

static icaltimezone *
e_cal_backend_weather_internal_get_timezone (ECalBackend *backend, const gchar *tzid)
{
	icaltimezone *zone;

	g_return_val_if_fail (tzid != NULL, NULL);

	if (!strcmp (tzid, "UTC")) {
		zone = icaltimezone_get_utc_timezone ();
	} else {
		ECalBackendWeather *cbw = E_CAL_BACKEND_WEATHER (backend);

		g_return_val_if_fail (E_IS_CAL_BACKEND_WEATHER (cbw), NULL);
		g_return_val_if_fail (cbw->priv != NULL, NULL);

		zone = g_hash_table_lookup (cbw->priv->zones, tzid);

		if (!zone && E_CAL_BACKEND_CLASS (parent_class)->internal_get_timezone)
			zone = E_CAL_BACKEND_CLASS (parent_class)->internal_get_timezone (backend, tzid);
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
	ECalBackendWeather *cbw;
	ECalBackendWeatherPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_WEATHER (object));

	cbw = (ECalBackendWeather *) object;
	priv = cbw->priv;

	if (priv->reload_timeout_id) {
		g_source_remove (priv->reload_timeout_id);
		priv->reload_timeout_id = 0;
	}

	if (priv->begin_retrival_id) {
		g_source_remove (priv->begin_retrival_id);
		priv->begin_retrival_id = 0;
	}

	if (priv->store) {
		g_object_unref (priv->store);
		priv->store = NULL;
	}

	g_hash_table_destroy (priv->zones);

	if (priv->city) {
		g_free (priv->city);
		priv->city = NULL;
	}

	if (priv->default_zone) {
		icaltimezone_free (priv->default_zone, 1);
		priv->default_zone = NULL;
	}

	g_free (priv);
	cbw->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Object initialization function for the weather backend */
static void
e_cal_backend_weather_init (ECalBackendWeather *cbw)
{
	ECalBackendWeatherPrivate *priv;

	priv = g_new0 (ECalBackendWeatherPrivate, 1);

	cbw->priv = priv;

	priv->reload_timeout_id = 0;
	priv->source_changed_id = 0;
	priv->begin_retrival_id = 0;
	priv->opened = FALSE;
	priv->source = NULL;
	priv->store = NULL;
	priv->city = NULL;

	priv->zones = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, free_zone);

	e_cal_backend_sync_set_lock (E_CAL_BACKEND_SYNC (cbw), TRUE);
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

	parent_class = (ECalBackendSyncClass *) g_type_class_peek_parent (class);

	object_class->finalize = e_cal_backend_weather_finalize;

	sync_class->is_read_only_sync = e_cal_backend_weather_is_read_only;
	sync_class->get_cal_address_sync = e_cal_backend_weather_get_cal_address;
	sync_class->get_alarm_email_address_sync = e_cal_backend_weather_get_alarm_email_address;
	sync_class->get_ldap_attribute_sync = e_cal_backend_weather_get_ldap_attribute;
	sync_class->get_static_capabilities_sync = e_cal_backend_weather_get_static_capabilities;
	sync_class->open_sync = e_cal_backend_weather_open;
	sync_class->refresh_sync = e_cal_backend_weather_refresh;
	sync_class->remove_sync = e_cal_backend_weather_remove;
	sync_class->discard_alarm_sync = e_cal_backend_weather_discard_alarm;
	sync_class->receive_objects_sync = e_cal_backend_weather_receive_objects;
	sync_class->get_default_object_sync = e_cal_backend_weather_get_default_object;
	sync_class->get_object_sync = e_cal_backend_weather_get_object;
	sync_class->get_object_list_sync = e_cal_backend_weather_get_object_list;
	sync_class->add_timezone_sync = e_cal_backend_weather_add_timezone;
	sync_class->set_default_zone_sync = e_cal_backend_weather_set_default_zone;
	sync_class->get_freebusy_sync = e_cal_backend_weather_get_free_busy;
	sync_class->get_changes_sync = e_cal_backend_weather_get_changes;
	backend_class->is_loaded = e_cal_backend_weather_is_loaded;
	backend_class->start_query = e_cal_backend_weather_start_query;
	backend_class->get_mode = e_cal_backend_weather_get_mode;
	backend_class->set_mode = e_cal_backend_weather_set_mode;

	backend_class->internal_get_default_timezone = e_cal_backend_weather_internal_get_default_timezone;
	backend_class->internal_get_timezone = e_cal_backend_weather_internal_get_timezone;
}

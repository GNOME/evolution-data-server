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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>

#include <glib/gi18n-lib.h>

#include "e-weather-source-ccf.h"

#define GWEATHER_I_KNOW_THIS_IS_UNSTABLE
#include <libgweather/weather.h>
#include <libgweather/gweather-xml.h>
#undef GWEATHER_I_KNOW_THIS_IS_UNSTABLE

#ifdef G_OS_WIN32

#include "libedataserver/e-data-server-util.h"

/* The localtime_r() in <pthread.h> doesn't guard against localtime()
 * returning NULL
 */
#undef localtime_r

/* The localtime() in Microsoft's C library is MT-safe */
#define localtime_r(tp,tmp) (localtime(tp)?(*(tmp)=*localtime(tp),(tmp)):0)

/* strtok() is also MT-safe (but not stateless, still uses only one
 * buffer pointer per thread, but for the use of strtok_r() here
 * that's enough).
 */
#define strtok_r(s,sep,lasts) (*(lasts)=strtok((s),(sep)))
#endif

G_DEFINE_TYPE (EWeatherSourceCCF, e_weather_source_ccf, E_TYPE_WEATHER_SOURCE)

struct search_struct
{
	const gchar *code;
	const gchar *name;
	gboolean is_old;
	WeatherLocation *location;
};

static gboolean
find_location_func (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *node, gpointer data)
{
	WeatherLocation *wl = NULL;
	struct search_struct *search = (struct search_struct *)data;

	gtk_tree_model_get (model, node, GWEATHER_XML_COL_POINTER, &wl, -1);
	if (!wl || !wl->name || !wl->code || !search || search->location)
		return FALSE;

	if (((!strcmp (wl->code, search->code)) || (search->is_old && !strcmp (wl->code + 1, search->code))) &&
	     (!strcmp (wl->name, search->name))) {
		search->location = weather_location_clone (wl);
		return TRUE;
	}

	return FALSE;
}

static WeatherLocation *
find_location (const gchar *code_name, gboolean is_old)
{
	GtkTreeModel *model;
	gchar **ids;
	struct search_struct search;

	search.location = NULL;

	ids = g_strsplit (code_name, "/", 2);

	if (!ids || !ids [0] || !ids [1])
		goto done;

	model = gweather_xml_load_locations ();
	if (!model)
		goto done;

	search.code = ids [0];
	search.name = ids [1];
	search.is_old = is_old;
	search.location = NULL;

	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) find_location_func, &search);

	gweather_xml_free_locations (model);
	g_strfreev (ids);

done:
	return search.location;
}

EWeatherSource*
e_weather_source_ccf_new (const gchar *uri)
{
	/* Old URI is formatted as weather://ccf/AAA[/BBB] - AAA is the 3-letter station
	 * code for identifying the providing station (subdirectory within the crh data
	 * repository). BBB is an optional additional station ID for the station within
	 * the CCF file. If not present, BBB is assumed to be the same station as AAA.
	 * But the new URI is as weather://code/name, where code is 4-letter code.
	 * So if got the old URI, then migrate to the new one, if possible.
	 */

	WeatherLocation *wl;
	EWeatherSourceCCF *source;

	if (!uri)
		return NULL;

	if (strncmp (uri, "ccf/", 4) == 0)
		wl = find_location (uri + 4, TRUE);
	else
		wl = find_location (uri, FALSE);

	if (!wl)
		return NULL;

	source = E_WEATHER_SOURCE_CCF (g_object_new (e_weather_source_ccf_get_type (), NULL));
	source->location = wl;
	source->info = NULL;

	return E_WEATHER_SOURCE (source);
}

#if 0
static GSList*
tokenize (gchar *buffer)
{
	gchar *token;
	gchar *tokbuf;
	GSList *ret;

	token = strtok_r (buffer, " \n", &tokbuf);
	ret = g_slist_append (NULL, g_strdup (token));
	while ((token = strtok_r (NULL, " \n/", &tokbuf)))
		ret = g_slist_append (ret, g_strdup (token));
	return ret;
}

static void
date2tm (gchar *date, struct tm *times)
{
	gchar tmp[3];
	time_t curtime = time(NULL);
	tmp[2] = '\0';

	localtime_r (&curtime, times);

	tmp[0] = date[0]; tmp[1] = date[1];
	times->tm_mday = atoi(tmp);
	tmp[0] = date[2]; tmp[1] = date[3];
	times->tm_hour = atoi(tmp);
	tmp[0] = date[4]; tmp[1] = date[5];
	times->tm_min = atoi(tmp);
}

static WeatherConditions
decodeConditions (gchar code)
{
	switch (code) {
		case 'A': return WEATHER_FAIR;
		case 'B': return WEATHER_PARTLY_CLOUDY;
		case 'C': return WEATHER_CLOUDY;
		case 'D': return WEATHER_DUST;
		case 'E': return WEATHER_MOSTLY_CLOUDY;
		case 'F': return WEATHER_FOGGY;
		case 'G': return WEATHER_VERY_HOT_OR_HOT_HUMID;
		case 'H': return WEATHER_HAZE;
		case 'I': return WEATHER_VERY_COLD_WIND_CHILL;
		case 'J': return WEATHER_SNOW_SHOWERS;
		case 'K': return WEATHER_SMOKE;
		case 'L': return WEATHER_DRIZZLE;
		case 'M': return WEATHER_SNOW_SHOWERS;
		case 'N': return WEATHER_WINDY;
		case 'O': return WEATHER_RAIN_OR_SNOW_MIXED;
		case 'P': return WEATHER_BLIZZARD;
		case 'Q': return WEATHER_BLOWING_SNOW;
		case 'R': return WEATHER_RAIN;
		case 'S': return WEATHER_SNOW;
		case 'T': return WEATHER_THUNDERSTORMS;
		case 'U': return WEATHER_SUNNY;
		case 'V': return WEATHER_CLEAR;
		case 'W': return WEATHER_RAIN_SHOWERS;
		case 'X': return WEATHER_SLEET;
		case 'Y': return WEATHER_FREEZING_RAIN;
		case 'Z': return WEATHER_FREEZING_DRIZZLE;
		/* hmm, this should never happen. */
		default: return WEATHER_SUNNY;
	}
}

static gint
decodePOP (gchar data)
{
	gint ret;

	switch (data) {
		case '-':
			ret = 5;
			break;
		case '+':
			ret = 95;
			break;
		case '/':
			ret = -1;	/* missing data */
			break;
		default:
			ret = (data - '0') * 10;
	}
	return ret;
}

static void
decodeSnowfall (gchar *data, gfloat *low, gfloat *high)
{
	gchar num[3];
	num[2] = '\0';

	num[0] = data[0]; num[1] = data[1];
	*low = atof (num) * 2.54f;
	num[0] = data[2]; num[1] = data[3];
	*high = atof (num) * 2.54f;
}

static float
ftoc (gchar *data)
{
	gint fahrenheit = atoi(data);
	if (fahrenheit >= 900)
		fahrenheit = (fahrenheit - 900) * -1;
	return ((gfloat)(fahrenheit-32)) * 5.0f / 9.0f;
}

static void
e_weather_source_ccf_do_parse (EWeatherSourceCCF *source, gchar *buffer)
{
	/* CCF gives us either 2 or 7 days of forecast data. IFPS WFO's
	 * will produce 7 day forecasts, whereas pre-IFPS WFO's are only
	 * mandated 2 (but may do 7). The morning forecast will give us either 2
	 * or 7 days worth of data. The evening forecast will give us the evening's
	 * low temperature plus 2 or 7 days forecast.
	 *
	 * The CCF format is described in NWS directive 10-503, but it's usually
	 * easier to look at a summary put up by one of the stations:
	 * http://www.crh.noaa.gov/lmk/product_guide/products/forecast/ccf.htm
	 */
	WeatherForecast *forecasts = g_new0 (WeatherForecast, 7);
	GSList *tokens = tokenize (buffer);
	GSList *date;
	GSList *current = tokens;
	GList *fc = NULL;
	struct tm tms;
	gint i;
	time_t base;
	gint n;

	date = g_slist_nth (tokens, 3);
	date2tm (date->data, &tms);

	/* fast-forward to the particular station we're interested in */
	current = g_slist_nth (tokens, 5);
	while (strcmp(current->data, source->substation))
		current = g_slist_next (current);
	current = g_slist_next (current);
	/* pick up the first two conditions reports */
	forecasts[0].conditions = decodeConditions (((gchar *)(current->data))[0]);
	forecasts[1].conditions = decodeConditions (((gchar *)(current->data))[1]);

	current = g_slist_next (current);
	if (tms.tm_hour < 12) {
		for (i = 0; i < 2; i++) {
			forecasts[i].high = ftoc (current->data);
			current = g_slist_next (current);
			forecasts[i].low  = ftoc (current->data);
			current = g_slist_next (current);
		}
		forecasts[2].high = ftoc (current->data);
		current = g_slist_next (current);
		forecasts[0].pop = decodePOP (((gchar *)(current->data))[2]);
		forecasts[1].pop = decodePOP (((gchar *)(current->data))[4]);
	} else {
		for (i = 0; i < 2; i++) {
			current = g_slist_next (current);
			forecasts[i].high = ftoc (current->data);
			current = g_slist_next (current);
			forecasts[i].low  = ftoc (current->data);
		}
		current = g_slist_next (current);
		forecasts[0].pop = decodePOP (((gchar *)(current->data))[1]);
		forecasts[1].pop = decodePOP (((gchar *)(current->data))[3]);
	}

	current = g_slist_next (current);
	if (strlen (current->data) == 4) {
		/* we've got the optional snowfall field */
		if (tms.tm_hour < 12) {
			decodeSnowfall (current->data, &forecasts[0].low, &forecasts[0].high);
			current = g_slist_next (g_slist_next (current));
			decodeSnowfall (current->data, &forecasts[1].low, &forecasts[1].high);
		} else {
			current = g_slist_next (current);
			decodeSnowfall (current->data, &forecasts[0].low, &forecasts[0].high);
		}
		current = g_slist_next (current);
	}

	/* set dates */
	base = mktime (&tms);
	if (tms.tm_hour >= 12)
		base += 43200;
	for (i = 0; i < 7; i++)
		forecasts[i].date = base + 86400*i;

	if (current == NULL || strlen (current->data) == 3) {
		/* We've got a pre-IFPS station. Realloc and return */
		WeatherForecast *f = g_new0(WeatherForecast, 2);
		memcpy (f, forecasts, sizeof (WeatherForecast) * 2);
		fc = g_list_append (fc, &f[0]);
		fc = g_list_append (fc, &f[1]);
		source->done (fc, source->finished_data);
	}

	/* Grab the conditions for the next 5 days */
	forecasts[2].conditions = decodeConditions (((gchar *)(current->data))[0]);
	forecasts[3].conditions = decodeConditions (((gchar *)(current->data))[1]);
	forecasts[4].conditions = decodeConditions (((gchar *)(current->data))[2]);
	forecasts[5].conditions = decodeConditions (((gchar *)(current->data))[3]);
	forecasts[6].conditions = decodeConditions (((gchar *)(current->data))[4]);

	/* Temperature forecasts */
	current = g_slist_next (current);
	if (tms.tm_hour < 12) {
		forecasts[2].low  = ftoc (current->data);
		for  (i = 3; i < 6; i++) {
			current = g_slist_next (current);
			forecasts[i].high = ftoc (current->data);
			current = g_slist_next (current);
			forecasts[i].low  = ftoc (current->data);
		}
		current = g_slist_next (current);
		forecasts[6].high = ftoc (current->data);
		forecasts[6].low  = forecasts[6].high;
		current = g_slist_next (current);
		forecasts[2].pop = decodePOP (((gchar *)(current->data))[1]);
		forecasts[3].pop = decodePOP (((gchar *)(current->data))[3]);
		forecasts[4].pop = decodePOP (((gchar *)(current->data))[5]);
		forecasts[5].pop = decodePOP (((gchar *)(current->data))[7]);
		forecasts[6].pop = decodePOP (((gchar *)(current->data))[9]);
		n = 7;
	} else {
		for (i = 2; i < 6; i++) {
			forecasts[i].high = ftoc (current->data);
			current = g_slist_next (current);
			forecasts[i].low  = ftoc (current->data);
			current = g_slist_next (current);
		}
		n = 6;
		/* hack for people who put out bad data, like Pueblo, CO. Yes, PUB, that means you */
		if (strlen (current->data) == 3)
			current = g_slist_next (current);
		forecasts[1].pop = decodePOP (((gchar *)(current->data))[0]);
		forecasts[2].pop = decodePOP (((gchar *)(current->data))[2]);
		forecasts[3].pop = decodePOP (((gchar *)(current->data))[4]);
		forecasts[4].pop = decodePOP (((gchar *)(current->data))[6]);
		forecasts[5].pop = decodePOP (((gchar *)(current->data))[8]);
	}

	for (i = 0; i < n; i++) {
		fc = g_list_append (fc, &forecasts[i]);
	}
	source->done (fc, source->finished_data);

	g_free (forecasts);
	g_list_free (fc);
}
#endif

static void
parse_done (WeatherInfo *info, gpointer data)
{
	EWeatherSourceCCF *ccfsource = (EWeatherSourceCCF *) data;

	if (!ccfsource)
		return;

	if (!info || !weather_info_is_valid (info)) {
		ccfsource->done (NULL, ccfsource->finished_data);
		return;
	}

	ccfsource->done (info, ccfsource->finished_data);
}

static void
e_weather_source_ccf_parse (EWeatherSource *source, EWeatherSourceFinished done, gpointer data)
{
	EWeatherSourceCCF *ccfsource = (EWeatherSourceCCF*) source;
	WeatherPrefs prefs;

	ccfsource->finished_data = data;
	ccfsource->done = done;

	prefs.type = FORECAST_LIST;
	prefs.radar = FALSE;
	prefs.radar_custom_url = NULL;
	prefs.temperature_unit = TEMP_UNIT_CENTIGRADE;
	prefs.speed_unit = SPEED_UNIT_MS;
	prefs.pressure_unit = PRESSURE_UNIT_HPA;
	prefs.distance_unit = DISTANCE_UNIT_METERS;

	if (ccfsource->location && !ccfsource->info) {
		ccfsource->info = weather_info_new (ccfsource->location, &prefs, parse_done, source);
		weather_location_free (ccfsource->location);
		ccfsource->location = NULL;
	} else {
		ccfsource->info = weather_info_update (ccfsource->info, &prefs, parse_done, source);
	}
}

static void
e_weather_source_ccf_class_init (EWeatherSourceCCFClass *class)
{
	EWeatherSourceClass *source_class;

	source_class = (EWeatherSourceClass *) class;

	source_class->parse = e_weather_source_ccf_parse;
}

static void
e_weather_source_ccf_init (EWeatherSourceCCF *source)
{
	source->location = NULL;
	source->info = NULL;
}

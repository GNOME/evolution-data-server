/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Ebby Wiselyn <ebbyw@gnome.org>
 *  Philip Withnall <philip@tecnocode.co.uk>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include <glib/gprintf.h>

#include <libedataserver/e-data-server-util.h>
#include <libedataserver/e-xml-hash-utils.h>

#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libedata-cal/e-cal-backend-sexp.h>

#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-time-util.h>
#include <libecal/e-cal-util.h>
#include <libecal/e-cal-component.h>
#include <libecal/e-cal-time-util.h>

#include <libical/ical.h>
#include <libsoup/soup-misc.h>

#include "e-cal-backend-google.h"
#include "e-cal-backend-google-utils.h"

#define CACHE_REFRESH_INTERVAL 10000

/****************************************************** Google Connection Helper Functions ***********************************************/

static gboolean gd_timeval_to_ical (GTimeVal *timeval, struct icaltimetype *itt, ECalComponentDateTime *dt, icaltimezone *default_zone);
static void get_timeval (ECalComponentDateTime dt, GTimeVal *timeval);
static gint utils_compare_ids (gconstpointer cache_id, gconstpointer modified_cache_id);
static gchar * utils_form_query (const gchar *query);
static gboolean get_deltas_timeout (gpointer cbgo);
static void utils_update_insertion (ECalBackendGoogle *cbgo, ECalBackendCache *cache, GDataFeed *feed, GSList *cache_keys);
static void utils_update_deletion (ECalBackendGoogle *cbgo, ECalBackendCache *cache, GSList *cache_keys);

/**
 *
 * e_cal_backend_google_utils_populate_cache:
 * @cbgo ECalBackendGoogle Object
 * Populates the cache with initial values
 *
 **/
static void
e_cal_backend_google_utils_populate_cache (ECalBackendGoogle *cbgo)
{
	ECalComponent *comp=NULL;
	ECalBackendCache *cache;
	GDataFeed *feed;
	ECalBackendGooglePrivate *priv;
	icalcomponent_kind kind;
	GList *entries = NULL, *list = NULL;

	cache = e_cal_backend_google_get_cache (cbgo);
	kind = e_cal_backend_get_kind (E_CAL_BACKEND(cbgo));

	feed = e_cal_backend_google_get_feed (cbgo);
	entries = gdata_feed_get_entries (feed);
	priv = cbgo->priv;

	for (list = entries; list != NULL; list = list->next) {
		comp = e_gdata_event_to_cal_component (GDATA_CALENDAR_EVENT (list->data), cbgo);
		if (comp && E_IS_CAL_COMPONENT(comp)) {
			gchar *comp_str;
			e_cal_component_commit_sequence (comp);
			comp_str = e_cal_component_get_as_string (comp);

			e_cal_backend_notify_object_created (E_CAL_BACKEND(cbgo), (const gchar *)comp_str);
			e_cal_backend_cache_put_component (cache, comp);
			g_object_unref (comp);
			g_free (comp_str);
		}
	}

	e_cal_backend_notify_view_done (E_CAL_BACKEND(cbgo), GNOME_Evolution_Calendar_Success);
}

/**
 *
 * e_cal_backend_google_utils_create_cache:
 * @cbgo: ECalBackendGoogle
 * Creates / Updates Cache
 *
 **/
static gpointer
e_cal_backend_google_utils_create_cache (ECalBackendGoogle *cbgo)
{
	ESource *source;
	gint x;
	const gchar *refresh_interval = NULL;
	ECalBackendCache *cache;

	source = e_cal_backend_get_source (E_CAL_BACKEND (cbgo));
	refresh_interval = e_source_get_property (source, "refresh");

	cache = e_cal_backend_google_get_cache (cbgo);
	if (e_cal_backend_cache_get_marker (cache)) {
		e_cal_backend_google_utils_populate_cache (cbgo);
		e_cal_backend_cache_set_marker (cache);
	} else
		get_deltas_timeout (cbgo);

	if (refresh_interval)
		x = atoi (refresh_interval);
	else
		x = 30;

	if (!e_cal_backend_google_get_timeout_id (cbgo)) {
		guint timeout_id;

		timeout_id = g_timeout_add (x * 60000,
					  (GSourceFunc) get_deltas_timeout,
					  (gpointer)cbgo);
		e_cal_backend_google_set_timeout_id (cbgo, timeout_id);
	}

	return GINT_TO_POINTER (GNOME_Evolution_Calendar_Success);
}

/**
 * e_cal_backend_google_utils_update:
 *
 * @handle:
 * Call this to update changes made to the calendar.
 *
 * Returns: %TRUE if update is successful, %FALSE otherwise
 **/
gpointer
e_cal_backend_google_utils_update (gpointer handle)
{
	static gint max_results = -1;
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;
	GDataFeed *feed;
	GError *error = NULL;

	ECalBackendCache *cache;

	GDataService *service;
	static GStaticMutex updating = G_STATIC_MUTEX_INIT;
	icalcomponent_kind kind;

	GList *entries_list = NULL, *iter_list = NULL, *ids_list = NULL;
	GSList *uid_list = NULL, *cache_keys = NULL;
	gboolean needs_to_insert = FALSE;
	gchar *uri, *full_uri;

	if (!handle || !E_IS_CAL_BACKEND_GOOGLE (handle)) {
		g_critical ("\n Invalid handle %s", G_STRLOC);
		return NULL;
	}

	g_static_mutex_lock (&updating);

	cbgo = (ECalBackendGoogle *)handle;
	priv = cbgo->priv;

	cache = e_cal_backend_google_get_cache (cbgo);
	service = GDATA_SERVICE (e_cal_backend_google_get_service (cbgo));
	uri = e_cal_backend_google_get_uri (cbgo);

	if (max_results <= 0) {
		const gchar *env = getenv ("EVO_GOOGLE_MAX_RESULTS");

		if (env)
			max_results = atoi (env);

		if (max_results <= 0)
			max_results = 1024;
	}

	full_uri = g_strdup_printf ("%s?max-results=%d", uri, max_results);
	feed = gdata_service_query (GDATA_SERVICE(service), full_uri, NULL, GDATA_TYPE_CALENDAR_EVENT, NULL, NULL, NULL, &error);
	g_free (full_uri);

	if (feed == NULL) {
		g_warning ("Error querying Google Calendar %s: %s", uri, error->message);
		g_error_free (error);
		g_static_mutex_unlock (&updating);
		return NULL;
	}

	e_cal_backend_google_set_feed (cbgo, feed);
	g_object_unref (feed);
	entries_list = gdata_feed_get_entries (feed);
	cache_keys = e_cal_backend_cache_get_keys (cache);
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgo));

	for (iter_list = entries_list; iter_list != NULL; iter_list = iter_list->next) {
		const gchar *id;
		id = gdata_entry_get_id (GDATA_ENTRY (iter_list->data));
		ids_list = g_list_prepend (ids_list, (gchar *) id);
	}

	/* Find the Removed Item */
	iter_list = NULL;
	for (iter_list = ids_list; iter_list != NULL; iter_list = iter_list->next) {
		GSList *remove_list = g_slist_find_custom (cache_keys, iter_list->data, (GCompareFunc) utils_compare_ids);

		if (!remove_list) {
			uid_list = g_slist_prepend (uid_list, g_strdup ((gchar *)iter_list->data));
			needs_to_insert = TRUE;
		} else {
			cache_keys = g_slist_remove_link (cache_keys, remove_list);
			g_slist_free (remove_list);
		}
	}

	/* Update the deleted entries */
	utils_update_deletion (cbgo, cache, cache_keys);

	/* Update the inserted entries */
	if (needs_to_insert) {
		utils_update_insertion (cbgo, cache, feed, uid_list);
		needs_to_insert = FALSE;
	}

	g_list_free (ids_list);
	g_slist_free (uid_list);

	g_static_mutex_unlock (&updating);

	return NULL;
}

ECalBackendSyncStatus
e_cal_backend_google_utils_connect (ECalBackendGoogle *cbgo)
{
	ECalBackendCache *cache;
	ESource *source;
	GDataFeed *feed;
	GDataCalendarService *service;

	ECalSourceType source_type;
	icalcomponent_kind kind;
	icaltimezone *default_zone;
	GError *error = NULL;
	GThread *thread;
	gchar *username, *password;
	guint timeout_id;
	gboolean mode_changed;
	gchar *uri, *suri;

	source = e_cal_backend_get_source (E_CAL_BACKEND(cbgo));

	service = gdata_calendar_service_new ("evolution-client-0.0.1");
	e_cal_backend_google_set_service (cbgo, service);

	/* Get the query URI */
	/* TODO: Would be better as a GDataCalendarQuery */
	suri = e_source_get_uri (source);
	uri = utils_form_query (suri);
	e_cal_backend_google_set_uri (cbgo, uri);
	g_free (suri);

	/* Authenticate with the service */
	username = e_cal_backend_google_get_username (cbgo);
	password = e_cal_backend_google_get_password (cbgo);
	if (!gdata_service_authenticate (GDATA_SERVICE(service), username, password, NULL, NULL)) {
		g_critical ("%s, Authentication Failed \n ", G_STRLOC);
		if (username || password)
			return GNOME_Evolution_Calendar_AuthenticationFailed;
		return GNOME_Evolution_Calendar_AuthenticationRequired;
	}

	/* Query for calendar events */
	feed = gdata_service_query (GDATA_SERVICE(service), uri, NULL, GDATA_TYPE_CALENDAR_EVENT, NULL, NULL, NULL, NULL);

	cache = e_cal_backend_google_get_cache (cbgo);
	service = e_cal_backend_google_get_service (cbgo);

	e_cal_backend_google_set_feed (cbgo, feed);
	g_object_unref (feed);

	/* For event sync */
	if (cache && service) {

		/* FIXME Get the current mode */
		mode_changed = FALSE;
		timeout_id = e_cal_backend_google_get_timeout_id (cbgo);

		if (!mode_changed && !timeout_id) {
			GThread *t1;

			/*FIXME Set the mode to be changed */
			t1 = g_thread_create ((GThreadFunc)e_cal_backend_google_utils_update, cbgo, FALSE, NULL);
			if (!t1) {
				e_cal_backend_notify_error (E_CAL_BACKEND (cbgo), _("Could not create thread for getting deltas"));
				return GNOME_Evolution_Calendar_OtherError;
			}

			timeout_id = g_timeout_add (CACHE_REFRESH_INTERVAL, (GSourceFunc) get_deltas_timeout, (gpointer)cbgo);
			e_cal_backend_google_set_timeout_id (cbgo, timeout_id);
		}

		return GNOME_Evolution_Calendar_Success;
	}
	/* FIXME Set the mode to be changed */
	kind = e_cal_backend_get_kind (E_CAL_BACKEND(cbgo));
	switch (kind) {
		case ICAL_VEVENT_COMPONENT:
			source_type = E_CAL_SOURCE_TYPE_EVENT;
			break;
		case ICAL_VTODO_COMPONENT:
			source_type = E_CAL_SOURCE_TYPE_TODO;
			break;
		case ICAL_VJOURNAL_COMPONENT:
			source_type = E_CAL_SOURCE_TYPE_JOURNAL;
			break;
		default:
			source_type = E_CAL_SOURCE_TYPE_EVENT;
	}

	/* Creating cache when in remote  */
	if (GDATA_IS_CALENDAR_SERVICE (service)) {
		cache = e_cal_backend_cache_new (e_cal_backend_get_uri (E_CAL_BACKEND (cbgo)),source_type);
		e_cal_backend_google_set_cache (cbgo, cache);
	}
	if (!cache) {
		e_cal_backend_notify_error (E_CAL_BACKEND(cbgo), _("Could not create cache file"));
		return GNOME_Evolution_Calendar_OtherError;
	}

	default_zone = e_cal_backend_google_get_default_zone (cbgo);
	e_cal_backend_cache_put_default_timezone (cache, default_zone);
	e_cal_backend_google_utils_create_cache (cbgo);
	thread = g_thread_create ((GThreadFunc)e_cal_backend_google_utils_create_cache, (gpointer) cbgo, FALSE, &error);

	if (!thread) {
		g_warning (G_STRLOC ": %s", error->message);
		g_error_free (error);

		e_cal_backend_notify_error (E_CAL_BACKEND (cbgo), _("Could not create thread for populating cache"));
		return GNOME_Evolution_Calendar_OtherError;
	}

	return GNOME_Evolution_Calendar_Success;

}

/*************************************************** GDataCalendarEvent Functions*********************************************/

/**
 * e_gdata_event_to_cal_component:
 * @event: a #GDataCalendarEvent
 * @cbgo: an #ECalBackendGoogle
 *
 * Creates an #ECalComponent from a #GDataCalendarEvent
 **/

ECalComponent *
e_gdata_event_to_cal_component (GDataCalendarEvent *event, ECalBackendGoogle *cbgo)
{
	ECalComponent *comp;
	ECalComponentText text;
	ECalComponentDateTime dt, dt_start;
	ECalComponentOrganizer *org = NULL;
	icaltimezone *default_zone;
	const gchar *description, *temp, *location = NULL;
	GTimeVal timeval, timeval2;
	struct icaltimetype itt;
	GList *category_ids;
	GList *go_attendee_list = NULL, *go_location_list = NULL, *l = NULL;
	GSList *attendee_list = NULL;

	comp = e_cal_component_new ();
	default_zone = e_cal_backend_google_get_default_zone (cbgo);

	if (!default_zone)
		g_message("Critical Default zone not set %s", G_STRLOC);

	e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);

	/* Description*/
	description = gdata_entry_get_content (GDATA_ENTRY (event));
	if (description) {
		GSList li;
		text.value = description;
		text.altrep = NULL;
		li.data = &text;
		li.next = NULL;
		e_cal_component_set_description_list (comp, &li);
	}

	/* Creation/Last update */
	gdata_entry_get_published (GDATA_ENTRY (event), &timeval);
	if (gd_timeval_to_ical (&timeval, &itt, &dt, default_zone))
		e_cal_component_set_created (comp, &itt);

	gdata_entry_get_updated (GDATA_ENTRY (event), &timeval);
	if (gd_timeval_to_ical (&timeval, &itt, &dt, default_zone))
		e_cal_component_set_dtstamp (comp, &itt);

	/* Start/End times */
	/* TODO: deal with multiple time periods */
	gdata_calendar_event_get_primary_time (event, &timeval, &timeval2, NULL);
	if (gd_timeval_to_ical (&timeval, &itt, &dt_start, default_zone))
		e_cal_component_set_dtstart (comp, &dt_start);
	if (gd_timeval_to_ical (&timeval2, &itt, &dt, default_zone))
		e_cal_component_set_dtend (comp, &dt);

	/* Summary of the event */
	text.value = gdata_entry_get_title (GDATA_ENTRY (event));
	text.altrep = NULL;
	if (text.value != NULL)
		e_cal_component_set_summary (comp, &text);

	/* Categories or Kinds */
	category_ids = NULL;
	category_ids = gdata_entry_get_categories (GDATA_ENTRY (event));

	/* Classification or Visibility */
	temp = NULL;
	temp = gdata_calendar_event_get_visibility (event);

	if (strcmp (temp, "public") == 0)
		e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PUBLIC);
	else
		e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_NONE);

	/* Specific properties */
	temp = NULL;

	/* Transparency */
	temp = gdata_calendar_event_get_transparency (event);
	if (strcmp (temp, "http://schemas.google.com/g/2005#event.opaque") == 0)
		e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_OPAQUE);
	else if (strcmp (temp, "http://schemas.google.com/g/2005#event.transparent") == 0)
		e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_TRANSPARENT);
	else
		e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_UNKNOWN);

	/* Attendees */
	go_attendee_list = gdata_calendar_event_get_people (event);

	for (l = go_attendee_list; l != NULL; l = l->next) {
		GDataGDWho *go_attendee;
		ECalComponentAttendee *attendee;

		go_attendee = (GDataGDWho *)l->data;

		attendee = g_new0 (ECalComponentAttendee, 1);
#if 0
		_print_attendee ((Attendee *)l->data);
#endif
		attendee->value = g_strconcat ("MAILTO:", gdata_gd_who_get_email_address (go_attendee), NULL);
		attendee->cn = g_strdup (gdata_gd_who_get_value_string (go_attendee));
		/* TODO: This could be made less static once the GData API's in place */
		attendee->role = ICAL_ROLE_OPTPARTICIPANT;
		attendee->status = ICAL_PARTSTAT_NEEDSACTION;
		attendee->cutype =  ICAL_CUTYPE_INDIVIDUAL;

		/* Check for Organizer */
		if (gdata_gd_who_get_relation_type (go_attendee)) {
			gchar *val;
			val = strstr (gdata_gd_who_get_relation_type (go_attendee), "organizer");
			if (val != NULL && !strcmp ("organizer", val)) {
				org = g_new0 (ECalComponentOrganizer, 1);

				if (gdata_gd_who_get_email_address (go_attendee))
					org->value = g_strconcat ("MAILTO:", gdata_gd_who_get_email_address (go_attendee), NULL);
				if (gdata_gd_who_get_value_string (go_attendee))
					org->cn = g_strdup (gdata_gd_who_get_value_string (go_attendee));
			}
		}

		attendee_list = g_slist_prepend (attendee_list, attendee);
	}
	e_cal_component_set_attendee_list (comp, attendee_list);

	/* Set the organizer if any */
	if (org)
		e_cal_component_set_organizer (comp, org);

	/* Location */
	go_location_list = gdata_calendar_event_get_places (event);

	for (l = go_location_list; l != NULL; l = l->next) {
		GDataGDWhere *go_location;

		go_location = (GDataGDWhere *)l->data;

		if (gdata_gd_where_get_relation_type (go_location) == NULL ||
		    strcmp (gdata_gd_where_get_relation_type (go_location), "http://schemas.google.com/g/2005#event") == 0)
			location = gdata_gd_where_get_value_string (go_location);
	}
	e_cal_component_set_location (comp, location);

	/* Recurrence */
	if (gdata_calendar_event_get_recurrence (event) != NULL) {
		/* We have to parse something like this (http://code.google.com/apis/gdata/elements.html#gdRecurrence):
			DTSTART;TZID=America/Los_Angeles:20060314T060000
			DURATION:PT3600S
			RRULE:FREQ=DAILY;UNTIL=20060321T220000Z
			BEGIN:VTIMEZONE
			...
			END:VTIMEZONE
		 * For the moment, we can ignore the vtimezone component. */

		gboolean in_timezone = FALSE;
		const gchar *recurrence = gdata_calendar_event_get_recurrence (event);
		const gchar *i = recurrence;
		GSList *rrule_list = NULL, *rdate_list = NULL, *exrule_list = NULL, *exdate_list = NULL;
		guint recurrence_length = strlen (recurrence);

		do {
			icalproperty *prop;
			const gchar *f = NULL;

			if (i == NULL || *i == '\0' || i - recurrence >= recurrence_length)
				break;

			f = strchr (i, '\n');

			/* Ignore the timezone component */
			if (strncmp (i, "BEGIN:VTIMEZONE", 15) == 0) {
				in_timezone = TRUE;
				goto next_property;
			} else if (strncmp (i, "END:VTIMEZONE", 13) == 0) {
				in_timezone = FALSE;
				goto next_property;
			}

			/* Skip any rules inside the timezone block */
			if (in_timezone == TRUE)
				goto next_property;

			/* Parse the recurrence properties */
			if (strncmp (i, "DTSTART", 7) == 0) {
				struct icaltimetype recur;
				gchar *recur_string;

				/* Parse the dtstart property; this takes priority over that retrieved from gdata_calendar_event_get_primary_time */
				recur_string = g_strndup (i, f - i);
				prop = icalproperty_new_from_string (recur_string);
				g_free (recur_string);

				if (icalproperty_isa (prop) != ICAL_DTSTART_PROPERTY) {
					icalproperty_free (prop);
					goto next_property;
				}

				recur = icalproperty_get_dtstart (prop);
				icalproperty_free (prop);
				itt = icaltime_convert_to_zone (recur, default_zone);
				dt.value = &itt;
				dt.tzid = NULL;
				e_cal_component_set_dtstart (comp, &dt);
			} else if (strncmp (i, "DTEND", 5) == 0) {
				struct icaltimetype recur;
				gchar *recur_string;

				/* Parse the dtend property; this takes priority over that retrieved from gdata_calendar_event_get_primary_time */
				recur_string = g_strndup (i, f - i);
				prop = icalproperty_new_from_string (recur_string);
				g_free (recur_string);

				if (icalproperty_isa (prop) != ICAL_DTEND_PROPERTY) {
					icalproperty_free (prop);
					goto next_property;
				}

				recur = icalproperty_get_dtstart (prop);
				icalproperty_free (prop);
				itt = icaltime_convert_to_zone (recur, default_zone);
				dt.value = &itt;
				dt.tzid = NULL;
				e_cal_component_set_dtend (comp, &dt);
			} else if (strncmp (i, "RRULE", 5) == 0) {
				struct icalrecurrencetype recur, *recur2;
				gchar *recur_string;

				/* Parse the rrule property */
				recur_string = g_strndup (i, f - i);
				prop = icalproperty_new_from_string (recur_string);
				g_free (recur_string);

				if (icalproperty_isa (prop) != ICAL_RRULE_PROPERTY) {
					icalproperty_free (prop);
					goto next_property;
				}

				recur = icalproperty_get_rrule (prop);
				icalproperty_free (prop);
				recur2 = g_memdup (&recur, sizeof (recur));
				rrule_list = g_slist_prepend (rrule_list, recur2);
			} else if (strncmp (i, "RDATE", 5) == 0) {
				struct icaldatetimeperiodtype recur;
				ECalComponentPeriod *period;
				icalvalue *value;
				gchar *recur_string;

				/* Parse the rdate property */
				recur_string = g_strndup (i, f - i);
				prop = icalproperty_new_from_string (recur_string);
				g_free (recur_string);

				if (icalproperty_isa (prop) != ICAL_RDATE_PROPERTY) {
					icalproperty_free (prop);
					goto next_property;
				}

				recur = icalproperty_get_rdate (prop);
				value = icalproperty_get_value (prop);

				period = g_new0 (ECalComponentPeriod, 1);
				period->start = recur.period.start;
				if (icalvalue_isa (value) == ICAL_VALUE_DATE || icalvalue_isa (value) == ICAL_VALUE_DATETIME) {
					period->type = E_CAL_COMPONENT_PERIOD_DATETIME;
					period->u.end = recur.period.end;
				} else if (icalvalue_isa (value) == ICAL_VALUE_DURATION) {
					period->type = E_CAL_COMPONENT_PERIOD_DURATION;
					period->u.duration = recur.period.duration;
				} else {
					g_assert_not_reached ();
				}
				icalproperty_free (prop);

				rdate_list = g_slist_prepend (rdate_list, period);
			} else if (strncmp (i, "EXRULE", 6) == 0) {
				struct icalrecurrencetype recur, *recur2;
				gchar *recur_string;

				/* Parse the exrule property */
				recur_string = g_strndup (i, f - i);
				prop = icalproperty_new_from_string (recur_string);
				g_free (recur_string);

				if (icalproperty_isa (prop) != ICAL_EXRULE_PROPERTY) {
					icalproperty_free (prop);
					goto next_property;
				}

				recur = icalproperty_get_rrule (prop);
				icalproperty_free (prop);
				recur2 = g_memdup (&recur, sizeof (recur));
				exrule_list = g_slist_prepend (exrule_list, recur2);
			} else if (strncmp (i, "EXDATE", 6) == 0) {
				struct icaltimetype recur;
				ECalComponentDateTime *date_time;
				gchar *recur_string;

				/* Parse the exdate property */
				recur_string = g_strndup (i, f - i);
				prop = icalproperty_new_from_string (recur_string);
				recur = icaltime_from_string (recur_string);
				g_free (recur_string);

				if (icalproperty_isa (prop) != ICAL_EXDATE_PROPERTY) {
					icalproperty_free (prop);
					goto next_property;
				}

				recur = icalproperty_get_exdate (prop);
				icalproperty_free (prop);
				itt = icaltime_convert_to_zone (recur, default_zone);
				date_time = g_new0 (ECalComponentDateTime, 1);
				date_time->value = g_memdup (&itt, sizeof (itt));
				exdate_list = g_slist_prepend (exdate_list, date_time);
			}

next_property:
			/* Advance to the next line, and hence the next property */
			i = f + 1;
		} while (TRUE);

		rrule_list = g_slist_reverse (rrule_list);
		e_cal_component_set_rrule_list (comp, rrule_list);
		g_slist_foreach (rrule_list, (GFunc) g_free, NULL);
		g_slist_free (rrule_list);

		rdate_list = g_slist_reverse (rdate_list);
		e_cal_component_set_rdate_list (comp, rdate_list);
		g_slist_foreach (rdate_list, (GFunc) g_free, NULL);
		g_slist_free (rdate_list);

		exrule_list = g_slist_reverse (exrule_list);
		e_cal_component_set_exrule_list (comp, exrule_list);
		g_slist_foreach (exrule_list, (GFunc) g_free, NULL);
		g_slist_free (exrule_list);

		exdate_list = g_slist_reverse (exdate_list);
		e_cal_component_set_exdate_list (comp, exdate_list);
		g_slist_foreach (exdate_list, (GFunc) e_cal_component_free_datetime, NULL);
		g_slist_foreach (exdate_list, (GFunc) g_free, NULL);
		g_slist_free (exdate_list);
	}

	/* Recurrence exceptions */
	if (gdata_calendar_event_is_exception (event)) {
		ECalComponentRange *recur_id;
		gchar *original_id = NULL;

		/* Provide the ID of the original event */
		gdata_calendar_event_get_original_event_details (event, &original_id, NULL);
		e_cal_component_set_uid (comp, original_id);
		g_free (original_id);

		/* Set the recurrence id and X-GW-RECORDID */
		recur_id = g_new0 (ECalComponentRange, 1);
		recur_id->type = E_CAL_COMPONENT_RANGE_SINGLE;
		recur_id->datetime = dt_start;
		e_cal_component_set_recurid (comp, recur_id);
		g_free (recur_id);
	} else {
		/* The event is not an exception to a recurring event */
		const gchar *uid = gdata_entry_get_id (GDATA_ENTRY (event));
		e_cal_component_set_uid (comp, uid);
	}

	e_cal_component_commit_sequence (comp);

	return comp;
}

/**
 *
 * e_gdata_event_from_cal_component:
 * @cbgo a ECalBackendGoogle
 * @comp a ECalComponent object
 * Creates a #GDataCalendarEvent from an #ECalComponent
 *
 **/
GDataCalendarEvent *
e_gdata_event_from_cal_component (ECalBackendGoogle *cbgo, ECalComponent *comp)
{
	const gchar *uid;
	GDataCalendarEvent *event;

	e_cal_component_get_uid (comp, &uid);
	event = gdata_calendar_event_new (uid);

	e_gdata_event_update_from_cal_component (cbgo, event, comp);

	return event;
}

static gchar *
comp_date_time_as_ical_string (ECalComponentDateTime *dt, icaltimezone *default_zone)
{
	icaltimetype itt;
	icalproperty *prop;
	gchar *buf;

	itt = icaltime_convert_to_zone (*(dt->value), default_zone);
	dt->value = &itt;
	prop = icalproperty_new_dtend (*(dt->value));
	buf = icalproperty_get_value_as_string_r (prop);
	icalproperty_free (prop);

	return buf;
}

void
e_gdata_event_update_from_cal_component (ECalBackendGoogle *cbgo, GDataCalendarEvent *event, ECalComponent *comp)
{
	ECalBackendGooglePrivate *priv;
	ECalComponentText text;
	ECalComponentDateTime dt;
	const gchar *term = NULL;
	icaltimezone *default_zone;
	icaltimetype itt;
	icalproperty *prop;
	GTimeVal timeval, timeval2;
	const gchar *location;
	GSList *list = NULL;
	GDataCategory *category;
	GDataGDWhen *when;
	ECalComponentText *t;
	GSList *attendee_list = NULL, *l = NULL;
	GString *recur_string;
	gchar *buf;

	priv = cbgo->priv;

	/* Summary */
	e_cal_component_get_summary (comp, &text);
	if (text.value != NULL)
		gdata_entry_set_title (GDATA_ENTRY (event), text.value);

	default_zone = e_cal_backend_google_get_default_zone (cbgo);

	/* Start/End times */
	e_cal_component_get_dtstart (comp, &dt);
	itt = icaltime_convert_to_zone (*dt.value, default_zone);
	dt.value = &itt;
	get_timeval (dt, &timeval);

	e_cal_component_get_dtend (comp, &dt);
	itt = icaltime_convert_to_zone (*dt.value, default_zone);
	dt.value = &itt;
	get_timeval (dt, &timeval2);

	/* TODO: deal with pure dates */
	when = gdata_gd_when_new (&timeval, &timeval2, FALSE);
	gdata_calendar_event_add_time (event, when);

	/* Content / Description */
	e_cal_component_get_description_list (comp, &list);
	if (list != NULL) {
		t = (ECalComponentText *)list->data;
		gdata_entry_set_content (GDATA_ENTRY (event), t->value);
	} else
		gdata_entry_set_content (GDATA_ENTRY (event), "");

	/* Location */
	e_cal_component_get_location (comp, &location);
	if (location) {
		GDataGDWhere *where;

		where = gdata_gd_where_new (NULL, location, NULL);
		gdata_calendar_event_add_place (event, where);
	}

	if (e_cal_backend_get_kind (E_CAL_BACKEND(cbgo)) == ICAL_VEVENT_COMPONENT)
		term = "http://schemas.google.com/g/2005#event";

	category = gdata_category_new (term, "http://schemas.google.com/g/2005#kind", "label");
	gdata_entry_add_category (GDATA_ENTRY (event), category);

	/* Attendee */
	e_cal_component_get_attendee_list (comp, &attendee_list);

	for (l = attendee_list; l != NULL; l = l->next) {
		ECalComponentAttendee *ecal_att;
		GDataGDWho *who;
		gchar *email;
		const gchar *rel;

		ecal_att = (ECalComponentAttendee *)l->data;

		/* Extract the attendee's e-mail address from att->value, which should be in the form:
		 * MAILTO:john@foobar.com
		 */
		email = strstr (ecal_att->value, "MAILTO:");
		if (!email)
			continue;
		email += 7; /* length of MAILTO: */

		rel = "http://schemas.google.com/g/2005#event.attendee";
		if (e_cal_component_has_organizer (comp)) {
			ECalComponentOrganizer org;

			e_cal_component_get_organizer (comp, &org);
			if (strcmp (org.value, ecal_att->value) == 0)
				rel = "http://schemas.google.com/g/2005#event.organizer";
		}

		who = gdata_gd_who_new (rel, ecal_att->cn, email);
		gdata_calendar_event_add_person (event, who);
	}

	/* Recurrence support */
	recur_string = g_string_new (NULL);

	e_cal_component_get_dtstart (comp, &dt);
	buf = comp_date_time_as_ical_string (&dt, default_zone);
	g_string_append_printf (recur_string, "DTSTART:%s\r\n", buf);
	icalmemory_free_buffer (buf);

	e_cal_component_get_dtend (comp, &dt);
	buf = comp_date_time_as_ical_string (&dt, default_zone);
	g_string_append_printf (recur_string, "DTEND:%s\r\n", buf);
	icalmemory_free_buffer (buf);

	e_cal_component_get_rrule_list (comp, &list);
	for (l = list; l != NULL; l = l->next) {
		/* Append each rrule to recur_string */
		buf = icalrecurrencetype_as_string_r ((struct icalrecurrencetype*) l->data);
		g_string_append_printf (recur_string, "RRULE:%s\r\n", buf);
		icalmemory_free_buffer (buf);
	}
	e_cal_component_free_recur_list (list);

	e_cal_component_get_rdate_list (comp, &list);
	for (l = list; l != NULL; l = l->next) {
		/* Append each rdate to recur_string */
		struct icaldatetimeperiodtype date_time_period = { { 0, }, };
		ECalComponentPeriod *period = l->data;

		/* Build a struct icaldatetimeperiodtype… */
		date_time_period.time = period->start;
		date_time_period.period.start = period->start;
		if (period->type == E_CAL_COMPONENT_PERIOD_DATETIME)
			date_time_period.period.end = period->u.end;
		else if (period->type == E_CAL_COMPONENT_PERIOD_DURATION)
			date_time_period.period.duration = period->u.duration;
		else
			g_assert_not_reached ();

		/* …allowing us to build an icalproperty and get it in string form */
		prop = icalproperty_new_rdate (date_time_period);

		buf = icalproperty_get_value_as_string_r (prop);
		g_string_append_printf (recur_string, "RDATE:%s\r\n", buf);
		icalmemory_free_buffer (buf);
		icalproperty_free (prop);
	}
	e_cal_component_free_period_list (list);

	e_cal_component_get_exrule_list (comp, &list);
	for (l = list; l != NULL; l = l->next) {
		/* Append each exrule to recur_string */
		buf = icalrecurrencetype_as_string_r ((struct icalrecurrencetype*) l->data);
		g_string_append_printf (recur_string, "EXRULE:%s\r\n", buf);
		icalmemory_free_buffer (buf);
	}
	e_cal_component_free_recur_list (list);

	e_cal_component_get_exdate_list (comp, &list);
	for (l = list; l != NULL; l = l->next) {
		/* Append each exdate to recur_string */
		ECalComponentDateTime *date_time = l->data;

		/* Build an icalproperty and get the exdate in string form */
		buf = comp_date_time_as_ical_string (date_time, default_zone);
		g_string_append_printf (recur_string, "EXDATE:%s\r\n", buf);
		icalmemory_free_buffer (buf);
	}
	e_cal_component_free_exdate_list (list);

	/* Set the recurrence data on the event */
	gdata_calendar_event_set_recurrence (event, g_string_free (recur_string, FALSE));

	/* FIXME For transparency and status */
}

/***************************************************************** Utility Functions *********************************************/

static gint
utils_compare_ids (gconstpointer cache_id, gconstpointer modified_cache_id)
{
	return strcmp ((gchar *)cache_id, (gchar *)modified_cache_id);
}

static gchar *
utils_form_query (const gchar *query)
{
	if (query!=NULL) {
		query = query + 9;
	}
	return g_strdup(query);
}

static void
utils_update_insertion (ECalBackendGoogle *cbgo, ECalBackendCache *cache, GDataFeed *feed, GSList *uid_list)
{
	ECalComponent *comp;
	GSList *list = NULL;
	GDataCalendarEvent *event;
	gchar *temp;

	comp = e_cal_component_new ();

	for (list = uid_list; list != NULL; list = list->next) {
		event = GDATA_CALENDAR_EVENT (gdata_feed_look_up_entry (feed, list->data));
		comp = e_gdata_event_to_cal_component (event, cbgo);

		if (comp) {
			e_cal_component_commit_sequence (comp);
			e_cal_backend_cache_put_component (cache, comp);

			temp = e_cal_component_get_as_string (comp);

			e_cal_backend_notify_object_created (E_CAL_BACKEND(cbgo), temp);

			g_free (temp);
			g_object_unref (comp);
		}
	}
}

static void
utils_update_deletion (ECalBackendGoogle *cbgo, ECalBackendCache *cache, GSList *cache_keys)
{
	ECalComponent *comp;
	GSList *list;

	comp = e_cal_component_new ();

	g_return_if_fail (E_IS_CAL_BACKEND_GOOGLE (cbgo));
	g_return_if_fail (cache != NULL && cbgo != NULL);
	g_return_if_fail (cache_keys != NULL);

	for (list = cache_keys; list; list = g_slist_next (list)) {
		ECalComponentId *id = NULL;
		gchar *comp_str = NULL;
		comp = e_cal_backend_cache_get_component (cache, (const gchar *)list->data, NULL);
		comp_str = e_cal_component_get_as_string (comp);
		id = e_cal_component_get_id (comp);

		e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbgo), id, comp_str, NULL);
		e_cal_backend_cache_remove_component (cache, (const gchar *) id->uid, id->rid);

		e_cal_component_free_id (id);
		g_object_unref (comp);
		g_free (comp_str);
	}
}

/**
 * get_timeval: Returns date in a GTimeVal
 * @dt a #ECalComponentDateTime value
 * @timeval a #GTimeVal
 **/
static void
get_timeval (ECalComponentDateTime dt, GTimeVal *timeval)
{
	time_t tt;

	/* GTimeVals are always in UTC */
	tt = icaltime_as_timet_with_zone (*(dt.value), icaltimezone_get_utc_timezone ());

	timeval->tv_sec = (glong) tt;
	timeval->tv_usec = 0;
}

static gboolean
get_deltas_timeout (gpointer cbgo)
{
	GThread *thread;

	if (!cbgo)
		return FALSE;

	e_cal_backend_google_utils_update (cbgo);
	thread = g_thread_create ((GThreadFunc) e_cal_backend_google_utils_update, cbgo, FALSE, NULL);
	if (!thread) {
		 /* FIXME */
	}

	return TRUE;
}

/**
 *
 * gd_timeval_to_ical:
 * Helper Function to convert a gdata format date to ical date
 * @event event from which the time comes. It's used to get to the event's timezone
 * @timeval date as a #GTimeVal
 * @iit Resulting icaltimetype.
 * @dt Resulting ECalComponentDateTime.
 * @default_zone Default time zone for the backend. If set, then the time will be converted to that timezone.
 *
 * @note Do not free itt or dt values, those come from buildin structures held by libical
 **/
static gboolean
gd_timeval_to_ical (GTimeVal *timeval, struct icaltimetype *itt, ECalComponentDateTime *dt, icaltimezone *default_zone)
{
	/* TODO: Event's timezone? */
	g_return_val_if_fail (itt != NULL, FALSE);
	g_return_val_if_fail (dt != NULL, FALSE);

	if (!timeval)
		return FALSE;

	*itt = icaltime_from_timet_with_zone (timeval->tv_sec, 0, icaltimezone_get_utc_timezone ());

	if (default_zone)
		*itt = icaltime_convert_to_zone (*itt, default_zone);

	dt->value = itt;
	dt->tzid = icaltimezone_get_tzid ((icaltimezone *) icaltime_get_timezone (*itt));

	return TRUE;
}

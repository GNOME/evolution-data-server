/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Ebby Wiselyn <ebbyw@gnome.org>
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

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define GDATA_SCHEMA "http://schemas.google.com/g/2005#"
#define CACHE_REFRESH_INTERVAL 10000

/****************************************************** Google Connection Helper Functions ***********************************************/

static gboolean gd_date_to_ical (EGoItem *item, const gchar *google_time_string, struct icaltimetype *itt, ECalComponentDateTime *dt, icaltimezone *default_zone);
static gchar * get_date (ECalComponentDateTime dt);
static gint utils_compare_ids (gconstpointer cache_id, gconstpointer modified_cache_id);
static gchar * utils_form_query (const gchar *query);
static gboolean get_deltas_timeout (gpointer cbgo);
static void utils_update_insertion (ECalBackendGoogle *cbgo, ECalBackendCache *cache, EGoItem *item, GSList *cache_keys);
static void utils_update_deletion (ECalBackendGoogle *cbgo, ECalBackendCache *cache, GSList *cache_keys);

/**
 *
 * e_cal_backend_google_utils_populate_cache:
 * @cbgo ECalBackendGoogle Object
 * Populates the cache with intial values
 *
 **/
static void
e_cal_backend_google_utils_populate_cache (ECalBackendGoogle *cbgo)
{
	ECalComponent *comp=NULL;
	ECalBackendCache *cache;
	EGoItem *item;
	ECalBackendGooglePrivate *priv;
	icalcomponent_kind kind;
	GSList *entries = NULL, *list = NULL;

	cache = e_cal_backend_google_get_cache (cbgo);
	kind = e_cal_backend_get_kind (E_CAL_BACKEND(cbgo));

	item = e_cal_backend_google_get_item (cbgo);
	entries = gdata_feed_get_entries (item->feed);
	priv = cbgo->priv;

	for (list = entries; list != NULL; list = list->next) {
		item->entry = (GDataEntry *)list->data;
		comp =	e_go_item_to_cal_component (item, cbgo);
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
 * Call this to Update changes, made to the calendar.
 *
 * Returns: TRUE if update is successful FALSE otherwise .
 **/
gpointer
e_cal_backend_google_utils_update (gpointer handle)
{
	static gint max_results = -1;
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;
	EGoItem *item;
	ECalBackendCache *cache;
	GDataGoogleService *service;
	static GStaticMutex updating = G_STATIC_MUTEX_INIT;
	icalcomponent_kind kind;
	GSList *ids_list = NULL, *cache_keys = NULL, *entries_list = NULL;
	GSList *uid_list = NULL, *iter_list = NULL, *remove = NULL;
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
	item =  e_cal_backend_google_get_item (cbgo);
	service = e_cal_backend_google_get_service (cbgo);
	uri = e_cal_backend_google_get_uri (cbgo);

	if (max_results <= 0) {
		const gchar *env = getenv ("EVO_GOOGLE_MAX_RESULTS");

		if (env)
			max_results = atoi (env);

		if (max_results <= 0)
			max_results = 1024;
	}

	full_uri = g_strdup_printf ("%s?max-results=%d", uri, max_results);
	item->feed = gdata_service_get_feed (GDATA_SERVICE(service), full_uri, NULL);
	g_free (full_uri);

	entries_list = gdata_feed_get_entries (item->feed);
	cache_keys = e_cal_backend_cache_get_keys (cache);
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgo));

	for (iter_list = entries_list; iter_list != NULL; iter_list = iter_list->next) {
		gchar *id;
		id = gdata_entry_get_id ((GDataEntry *)iter_list->data);
		ids_list = g_slist_prepend (ids_list, id);
	}

	/* Find the Removed Item */
	iter_list = NULL;
	for (iter_list = ids_list; iter_list != NULL; iter_list = iter_list->next) {
		GCompareFunc func = NULL;
		GSList *remove = NULL;

		func = (GCompareFunc)utils_compare_ids;

		if (!(remove = g_slist_find_custom (cache_keys, iter_list->data, func))) {
			uid_list = g_slist_prepend (uid_list, g_strdup ((gchar *)iter_list->data));
			needs_to_insert = TRUE;
		}else {
			cache_keys = g_slist_remove_link (cache_keys, remove);
		}

		if (remove)
			g_slist_free (remove);
	}

	/* Update the deleted entries */
	utils_update_deletion (cbgo, cache, cache_keys);

	/* Update the inserted entries */
	if (needs_to_insert) {
		utils_update_insertion (cbgo, cache, item,uid_list);
		needs_to_insert = FALSE;
	}

	if (ids_list) {
		ids_list = NULL;
		g_slist_free (ids_list);
	}

	if (uid_list) {
		/*FIXME could crash while freeing*/
		uid_list = NULL;
		g_slist_free (uid_list);
	}

	if (entries_list) {
		/* FIXME could crash while freeing */
		entries_list = NULL;
		g_slist_free (entries_list);
	}

	if (remove) {
		remove = NULL;
		g_slist_free (remove);
	}

	g_static_mutex_unlock (&updating);
	return NULL;
}

ECalBackendSyncStatus
e_cal_backend_google_utils_connect (ECalBackendGoogle *cbgo)
{
	ECalBackendCache *cache;
	EGoItem *item;
	ESource *source;
	GDataFeed *feed;
	GDataGoogleService *service;

	ECalSourceType source_type;
	icalcomponent_kind kind;
	icaltimezone *default_zone;
	GSList *entries;
	GError *error = NULL;
	GThread *thread;
	gchar *username, *password;
	guint timeout_id;
	gboolean mode_changed;
	gchar *uri, *suri;

	source = e_cal_backend_get_source (E_CAL_BACKEND(cbgo));

	service = gdata_google_service_new ("cl", "evolution-client-0.0.1");
	e_cal_backend_google_set_service (cbgo, service);

	suri = e_source_get_uri (source);
	uri = utils_form_query (suri);
	e_cal_backend_google_set_uri (cbgo, uri);

	g_free (suri);

	username = e_cal_backend_google_get_username (cbgo);
	password = e_cal_backend_google_get_password (cbgo);
	gdata_service_set_credentials (GDATA_SERVICE(service), username, password);
	feed = gdata_service_get_feed (GDATA_SERVICE(service), uri, NULL);

	if (!feed) {
		g_critical ("%s, Authentication Failed \n ", G_STRLOC);
		if (username || password)
			return GNOME_Evolution_Calendar_AuthenticationFailed;
		return GNOME_Evolution_Calendar_AuthenticationRequired;
	}

	entries = gdata_feed_get_entries (feed);

	item = g_new0 (EGoItem, 1);
	item->entry = e_cal_backend_google_get_entry (cbgo);
	item->feed = feed;

	cache = e_cal_backend_google_get_cache (cbgo);
	service = e_cal_backend_google_get_service (cbgo);

	e_cal_backend_google_set_item (cbgo, item);

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
	if (GDATA_IS_GOOGLE_SERVICE (service)) {
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

/*************************************************** EGoItem Functions*********************************************/

/**
 * e_go_item_to_cal_component:
 * @item a EGoItem object
 * @cbgo a ECalBackendGoogle object
 *
 * Creates a EGoItem from ECalComponent
 **/

ECalComponent *
e_go_item_to_cal_component (EGoItem *item, ECalBackendGoogle *cbgo)
{
	ECalComponent *comp;
	ECalComponentText text;
	ECalComponentDateTime dt;
	ECalComponentOrganizer *org = NULL;
	icaltimezone *default_zone;
	const gchar *description, *uid, *temp;
	struct icaltimetype itt;
	GSList *category_ids;
	GSList *go_attendee_list = NULL, *l = NULL, *attendee_list = NULL;

	comp = e_cal_component_new ();
	default_zone = e_cal_backend_google_get_default_zone (cbgo);

	if (!default_zone)
		g_message("Critical Default zone not set %s", G_STRLOC);

	e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);

	/* Description*/
	description = gdata_entry_get_content (item->entry);
	if (description) {
		GSList l;
		text.value = description;
		text.altrep = NULL;
		l.data = &text;
		l.next = NULL;
		e_cal_component_set_description_list (comp, &l);

	}

	/* Creation/Last update */
	if (gd_date_to_ical (item, gdata_entry_get_custom (item->entry, "published"), &itt, &dt, default_zone))
		e_cal_component_set_created (comp, &itt);

	if (gd_date_to_ical (item, gdata_entry_get_custom (item->entry, "updated"), &itt, &dt, default_zone))
		e_cal_component_set_dtstamp (comp, &itt);

	/* Start time */
	if (gd_date_to_ical (item, gdata_entry_get_start_time (item->entry), &itt, &dt, default_zone))
		e_cal_component_set_dtstart (comp, &dt);

	/* End time */
	if (gd_date_to_ical (item, gdata_entry_get_end_time (item->entry), &itt, &dt, default_zone))
		e_cal_component_set_dtend (comp, &dt);

	/* Summary of the Entry */
	text.value = gdata_entry_get_title (item->entry);
	text.altrep = NULL;
	if (text.value != NULL)
		e_cal_component_set_summary (comp, &text);

	/* Categories or Kinds */
	category_ids = NULL;
	category_ids = gdata_entry_get_categories (item->entry);

	/* Classification or Visibility */
	temp = NULL;
	temp = gdata_entry_get_visibility (item->entry);

	if (temp)
		e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PUBLIC);
	else
		e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_NONE);

	/* Specific properties */
	temp = NULL;

	/* Transparency */
	temp = gdata_entry_get_transparency (item->entry);
	e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_TRANSPARENT);

	/* Attendees */
	go_attendee_list = gdata_entry_get_attendee_list (item->entry);

	if (go_attendee_list != NULL) {

		for (l = go_attendee_list; l != NULL; l = l->next) {
			Attendee *go_attendee;
			ECalComponentAttendee *attendee;

			go_attendee = (Attendee *)l->data;

			attendee = g_new0 (ECalComponentAttendee, 1);
#if 0
			_print_attendee ((Attendee *)l->data);
#endif
			attendee->value = g_strconcat ("MAILTO:", go_attendee->attendee_email, NULL);
			attendee->cn = g_strdup (go_attendee->attendee_value);
			attendee->role = ICAL_ROLE_OPTPARTICIPANT;
			attendee->status = ICAL_PARTSTAT_NEEDSACTION;
			attendee->cutype =  ICAL_CUTYPE_INDIVIDUAL;

			/* Check for Organizer */
			if (go_attendee->attendee_rel) {
				gchar *val;
				val = strstr ((const gchar *)go_attendee->attendee_rel, (const gchar *)"organizer");
				if (val != NULL && !strcmp ("organizer", val)) {
					org = g_new0 (ECalComponentOrganizer, 1);

					if (go_attendee->attendee_email)
						org->value = g_strconcat("MAILTO:", go_attendee->attendee_email, NULL);
					if (go_attendee->attendee_value)
						org->cn =  g_strdup (go_attendee->attendee_value);
				}
			}

			attendee_list = g_slist_prepend (attendee_list, attendee);
		}
		e_cal_component_set_attendee_list (comp, attendee_list);
	}

	/* Set the organizer if any */
	if (org)
		e_cal_component_set_organizer (comp, org);

	/* Location */
	e_cal_component_set_location (comp, gdata_entry_get_location (item->entry));

#if 0
	/* temp hack to see how recurrence work */
	ECalComponentRange *recur_id;
	recur_id = g_new0 (ECalComponentRange, 1);
	recur_id->datetime = dt;
	recur_id->type = E_CAL_COMPONENT_RANGE_THISFUTURE;
	e_cal_component_set_recurid (comp, recur_id);
#endif
	e_cal_component_set_dtend (comp, &dt);

	uid = gdata_entry_get_id (item->entry);
	e_cal_component_set_uid (comp, (const gchar *)uid);
	e_cal_component_commit_sequence (comp);

	return comp;
}

/**
 *
 * e_go_item_from_cal_component:
 * @cbgo a ECalBackendGoogle
 * @comp a ECalComponent object
 * Creates a ECalComponent from EGoItem
 *
 **/
EGoItem *
e_go_item_from_cal_component (ECalBackendGoogle *cbgo, ECalComponent *comp)
{
	ECalBackendGooglePrivate *priv;
	EGoItem *item;
	ECalComponentText text;
	ECalComponentDateTime dt;
	gchar *temp, *term = NULL;
	icaltimezone *default_zone;
	icaltimetype itt;
	const gchar *uid;
	const gchar *location;
	GSList *list = NULL;
	GDataEntry *entry;
	ECalComponentText *t;
	GSList *attendee_list = NULL, *l = NULL;

	priv = cbgo->priv;

	item = g_new0 (EGoItem, 1);
	entry = gdata_entry_new ();

	/* Summary */
	e_cal_component_get_summary (comp, &text);
	if (text.value!=NULL)
		gdata_entry_set_title (entry, text.value);

	default_zone = e_cal_backend_google_get_default_zone (cbgo);

	/* Start time */
	e_cal_component_get_dtstart (comp, &dt);
	itt = icaltime_convert_to_zone (*dt.value, default_zone);
	dt.value = &itt;
	temp = get_date (dt);
	gdata_entry_set_start_time (entry, temp);

	/* End Time */
	e_cal_component_get_dtend (comp, &dt);
	itt = icaltime_convert_to_zone (*dt.value, default_zone);
	dt.value = &itt;
	temp = get_date (dt);
	gdata_entry_set_end_time (entry, temp);

	/* Content / Description */
	e_cal_component_get_description_list (comp, &list);
	if (list != NULL) {
		t = (ECalComponentText *)list->data;
		gdata_entry_set_content (entry, t->value);
	}
	else
		gdata_entry_set_content (entry, "");

	e_cal_component_get_uid (comp, &uid);
	gdata_entry_set_id (entry, g_strdup(uid));

	/* Location */
	e_cal_component_get_location (comp, &location);
	if (location)
		gdata_entry_set_location (entry , location);

	if (e_cal_backend_get_kind (E_CAL_BACKEND(cbgo)) == ICAL_VEVENT_COMPONENT)
		term = g_strconcat (GDATA_SCHEMA, "event", NULL);

	gdata_entry_create_categories (entry, g_strconcat (GDATA_SCHEMA, "kind", NULL),
			"label",
			term);
	/* Attendee */
	e_cal_component_get_attendee_list (comp, &attendee_list);

	for (l = attendee_list; l!=NULL; l=l->next) {
		ECalComponentAttendee *ecal_att;
		ecal_att = (ECalComponentAttendee *)l->data;
		/* TODO Convert ECalComponentAttendee to Attendee, and change during parsing to store the types */

	}

	/* FIXME For transparency and status */
	item->entry = entry;
	return item;
}

/**
 *
 * e_go_item_get_entry:
 * @item a EGoItem
 * Returns the GDataEntry object
 *
 **/

GDataEntry *
e_go_item_get_entry (EGoItem *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	return item->entry;
}

/**
 *
 * e_go_item_set_entry:
 * @item  a EGoItem
 * @entry a GDataEntry
 * Sets the GDataEntry of EGoItem to entry
 *
 **/
void
e_go_item_set_entry (EGoItem *item, GDataEntry *entry)
{
	g_return_if_fail (item != NULL);
	g_return_if_fail (entry != NULL);

	item->entry = entry;
}

/**
 *
 * gdata_entry_get_entry_by_id:
 * @entries List of entries
 * @id id to retreive
 * Gets the specified entry
 *
 **/
GDataEntry *
gdata_entry_get_entry_by_id (GSList *entries, const gchar *id)
{
	GSList *l = NULL;

	for (l = entries; l != NULL;l = l->next) {
		if (!strcmp (gdata_entry_get_id ((GDataEntry *)l->data), id)) {
			return l->data;
		}
	}

	return NULL;
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
utils_update_insertion (ECalBackendGoogle *cbgo, ECalBackendCache *cache, EGoItem *item, GSList *uid_list)
{
	EGoItem *item_t;
	ECalComponent *comp;
	GSList *list = NULL, *entries_list = NULL;
	GDataEntry *entry;
	gchar *temp;

	comp = e_cal_component_new ();
	item_t = g_new0 (EGoItem, 1);
	item_t->feed = item->feed;
	entries_list = gdata_feed_get_entries (item->feed);

	for (list = uid_list; list != NULL; list = list->next) {
		entry = gdata_entry_get_entry_by_id (entries_list, list->data);
		item_t->entry = entry;
		comp = e_go_item_to_cal_component (item_t, cbgo);

		if (comp) {
			e_cal_component_commit_sequence (comp);
			e_cal_backend_cache_put_component (cache, comp);

			temp = e_cal_component_get_as_string (comp);

			e_cal_backend_notify_object_created (E_CAL_BACKEND(cbgo), temp);

			g_free (temp);
			g_object_unref (comp);
		}
	}

	g_free (item_t);
	if (list)
		g_slist_free (list);
	if (entries_list)
		g_slist_free (entries_list);
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
 * get_date: Returns date in gdata format '2006-04-17T17:00:00.000Z'
 * @dt a #ECalComponentDateTime value
 **/
/* FIXME use proper functions to manipulate the dates */
static gchar *
get_date (ECalComponentDateTime dt)
{
	gchar *temp;
	struct icaltimetype itt;
	struct icaltimetype *itt_u;
	gchar *month;
	gchar *day, *minute, *hour, *second;

	itt_u = dt.value;
	itt.year = itt_u->year;
	itt.month = itt_u->month;
	itt.day = itt_u->day;
	itt.hour = itt_u->hour;
	itt.minute = itt_u->minute;
	itt.second = itt_u->second;
	itt.is_utc = itt_u->is_utc;
	itt.is_date = itt_u->is_date;
	itt.is_daylight = itt_u->is_daylight;
	itt.zone = itt_u->zone;

	month = (itt.month<10) ? g_strdup_printf("0%d", itt.month):g_strdup_printf ("%d", itt.month);
	day = (itt.day < 10) ? g_strdup_printf("0%d", itt.day):g_strdup_printf ("%d", itt.day);

	hour = (itt.hour<10) ? g_strdup_printf("0%d", itt.hour):g_strdup_printf ("%d", itt.hour);
	minute = (itt.minute<10) ? g_strdup_printf("0%d", itt.minute):g_strdup_printf ("%d", itt.minute);
	second = (itt.second<10) ? g_strdup_printf ("0%d", itt.second):g_strdup_printf ("%d", itt.second);

	/* FIXME not the best way to do this */
	temp =  g_strdup_printf ("%d-%s-%sT%s:%s:%s.000", itt.year, month, day, hour, minute, second);
	g_free (month);
	g_free (day);
	g_free (hour);
	g_free (minute);
	g_free (second);

	return temp;
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
 * gd_date_to_ical:
 * Helper Function to convert a gdata format date to ical date
 * @item item from which the time comes. It's used to get to the feed's timezone
 * @google_time_string date in gdata format eg: '2006-04-17T17:00:00.000Z' or '2006-04-17T17:00:00.000+07:00'
 * @iit Resulting icaltimetype.
 * @dt Resulting ECalComponentDateTime.
 * @default_zone Default time zone for the backend. If set, then the time will be converted to that timezone.
 *
 * @note Do not free itt or dt values, those come from buildin structures held by libical
 **/
static gboolean
gd_date_to_ical (EGoItem *item, const gchar *google_time_string, struct icaltimetype *itt, ECalComponentDateTime *dt, icaltimezone *default_zone)
{
	gchar *s, *string, *dup;
	gint count = 0;
	gboolean is_utc = TRUE;

	g_return_val_if_fail (itt != NULL, FALSE);
	g_return_val_if_fail (dt != NULL, FALSE);

	if (!google_time_string || !*google_time_string)
		return FALSE;

	dup = g_strdup (google_time_string);
	s = dup;
	string = dup;

	/* Strip of the string to the gdata format */
	while (s[0] != '\0') {
		if ((s[0] != '-') && (s[0] != '+') && (s[0] != ':') && (s[0] != '.')) {
			*string = *s;
			string = string + 1;
			s = s + 1;
			count = count + 1;
		}else
			s = s + 1;

		if (count == 15) {
			if (strlen (s) >= 5) {
				is_utc = s [4] == 'Z';
			}

			string[0] = '\0';
			break;
		}
		if (s[1] == '\0')
			string[0] = '\0';
	}

	*itt = icaltime_from_string (dup);

	if (!is_utc) {
		const gchar *zone_name = item->feed ? gdata_feed_get_timezone (item->feed) : NULL;

		if (zone_name) {
			icaltimezone *zone = icaltimezone_get_builtin_timezone (zone_name);

			if (zone)
				icaltime_set_timezone (itt, zone);
		}
	}

	if (!icaltime_get_timezone (*itt))
		icaltime_set_timezone (itt, icaltimezone_get_utc_timezone ());

	if (default_zone)
		*itt = icaltime_convert_to_zone (*itt, default_zone);

	dt->value = itt;
	dt->tzid = icaltimezone_get_tzid ((icaltimezone *) icaltime_get_timezone (*itt));

	g_free (dup);

	return TRUE;
}

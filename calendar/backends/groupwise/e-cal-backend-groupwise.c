/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *  Harish Krishnaswamy <kharish@novell.com>
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include <libedataserver/e-data-server-util.h>
#include <libedataserver/e-xml-hash-utils.h>
#include <libedataserver/e-url.h>
#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-file-store.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libecal/e-cal-component.h>
#include <libecal/e-cal-time-util.h>
#include "e-cal-backend-groupwise.h"
#include "e-cal-backend-groupwise-utils.h"
#include "e-gw-connection.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef G_OS_WIN32
#ifdef gmtime_r
#undef gmtime_r
#endif

/* The gmtime() in Microsoft's C library is MT-safe */
#define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)
#endif

#define SERVER_UTC_TIME "server_utc_time"
#define LOCAL_UTC_TIME "local_utc_time"
#define CACHE_MARKER "populated"

#define EDC_ERROR(_code) e_data_cal_create_error (_code, NULL)
#define EDC_ERROR_EX(_code, _msg) e_data_cal_create_error (_code, _msg)
#define EDC_ERROR_FAILED_STATUS(_code, _status) e_data_cal_create_error_fmt (_code, "Failed with status 0x%x", _status)

G_DEFINE_TYPE (ECalBackendGroupwise, e_cal_backend_groupwise, E_TYPE_CAL_BACKEND_SYNC)

typedef struct {
	GCond *cond;
	GMutex *mutex;
	gboolean exit;
} SyncDelta;

/* Private part of the CalBackendGroupwise structure */
struct _ECalBackendGroupwisePrivate {
	EGwConnection *cnc;
	ECalBackendStore *store;
	gboolean read_only;
	gchar *uri;
	gchar *username;
	gchar *password;
	gchar *container_id;
	CalMode mode;
	gboolean mode_changed;
	GHashTable *categories_by_id;
	GHashTable *categories_by_name;

	/* number of calendar items in the folder */
	guint32 total_count;

	/* timeout handler for syncing sendoptions */
	guint sendoptions_sync_timeout;

	/* fields for storing info while offline */
	gchar *user_email;

	gboolean first_delta_fetch;

	/* A mutex to control access to the private structure for the following */
	GStaticRecMutex rec_mutex;
	icaltimezone *default_zone;
	guint timeout_id;
	GThread *dthread;
	SyncDelta *dlock;
};

#define PRIV_LOCK(p)   (g_static_rec_mutex_lock (&(p)->rec_mutex))
#define PRIV_UNLOCK(p) (g_static_rec_mutex_unlock (&(p)->rec_mutex))

static void e_cal_backend_groupwise_dispose (GObject *object);
static void e_cal_backend_groupwise_finalize (GObject *object);
static void e_cal_backend_groupwise_add_timezone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj, GError **perror);
static const gchar * get_gw_item_id (icalcomponent *icalcomp);
static void get_retract_data (ECalComponent *comp, const gchar **retract_comment, gboolean *all_instances);
static const gchar * get_element_type (icalcomponent_kind kind);

#define PARENT_TYPE E_TYPE_CAL_BACKEND_SYNC
static ECalBackendClass *parent_class = NULL;

/* Time interval in milliseconds for obtaining changes from server and refresh the cache. */
#define CACHE_REFRESH_INTERVAL 600000
#define CURSOR_ITEM_LIMIT 100
#define CURSOR_ICALID_LIMIT 500

static guint get_cache_refresh_interval (ECalBackendGroupwise *cbgw);

EGwConnection *
e_cal_backend_groupwise_get_connection (ECalBackendGroupwise *cbgw) {

	g_return_val_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), NULL);

	return cbgw->priv->cnc;
}

GHashTable *
e_cal_backend_groupwise_get_categories_by_id (ECalBackendGroupwise *cbgw) {

	g_return_val_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), NULL);

	return cbgw->priv->categories_by_id;
}

GHashTable *
e_cal_backend_groupwise_get_categories_by_name (ECalBackendGroupwise *cbgw) {

	g_return_val_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), NULL);

	return cbgw->priv->categories_by_name;
}

icaltimezone *
e_cal_backend_groupwise_get_default_zone (ECalBackendGroupwise *cbgw) {

	g_return_val_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), NULL);

	return cbgw->priv->default_zone;
}

const gchar *
e_cal_backend_groupwise_get_container_id (ECalBackendGroupwise *cbgw)
{
	return cbgw->priv->container_id;
}

void
e_cal_backend_groupwise_priv_lock (ECalBackendGroupwise *cbgw)
{
       PRIV_LOCK (cbgw->priv);
}

void
e_cal_backend_groupwise_priv_unlock (ECalBackendGroupwise *cbgw)
{
       PRIV_UNLOCK (cbgw->priv);
}

static const gchar *
get_element_type (icalcomponent_kind kind)
{

	const gchar *type;

	if (kind == ICAL_VEVENT_COMPONENT)
		type = "Appointment";
	else if (kind == ICAL_VTODO_COMPONENT)
		type = "Task";
	else
		type = "Note";

	return type;

}

/* TODO Do not replicate this in every backend */
static icaltimezone *
resolve_tzid (const gchar *tzid, gpointer user_data)
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
put_component_to_store (ECalBackendGroupwise *cbgw,
			ECalComponent *comp)
{
	time_t time_start, time_end;
	ECalBackendGroupwisePrivate *priv;

	priv = cbgw->priv;

	e_cal_util_get_component_occur_times (comp, &time_start, &time_end,
				   resolve_tzid, cbgw, priv->default_zone,
				   e_cal_backend_get_kind (E_CAL_BACKEND (cbgw)));

	e_cal_backend_store_put_component_with_time_range (priv->store, comp, time_start, time_end);
}

/* Initialy populate the cache from the server */
static EGwConnectionStatus
populate_cache (ECalBackendGroupwise *cbgw)
{
	ECalBackendGroupwisePrivate *priv;
	EGwConnectionStatus status;
        ECalComponent *comp;
        GList *list = NULL, *l;
	gboolean done = FALSE,  forward = FALSE;
	gint cursor = 0;
	guint32	total, num = 0;
	gint percent = 0, i;
	const gchar *position = E_GW_CURSOR_POSITION_END;
	icalcomponent_kind kind;
	const gchar *type;
	EGwFilter* filter[3];
	gchar l_str[26];
	gchar h_str[26];
	icaltimetype temp;
	struct tm tm;
	time_t h_time, l_time;

	priv = cbgw->priv;
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));
	total = priv->total_count;

	type = get_element_type (kind);

	/* Fetch the data with a bias to present, near past/future */
	temp = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	i = g_ascii_strtod (g_getenv ("PRELOAD_WINDOW_DAYS")? g_getenv ("PRELOAD_WINDOW_DAYS"):"15", NULL);
	temp.day -= i;
	icaltime_normalize (temp);
	l_time = icaltime_as_timet_with_zone (temp, icaltimezone_get_utc_timezone ());
	gmtime_r (&l_time, &tm);
	strftime (l_str, 26, "%Y-%m-%dT%H:%M:%SZ", &tm);
	temp.day += (2*i);
	icaltime_normalize (temp);
	h_time = icaltime_as_timet_with_zone (temp, icaltimezone_get_utc_timezone ());
	gmtime_r (&h_time, &tm);
	strftime (h_str, 26, "%Y-%m-%dT%H:%M:%SZ", &tm);

	filter[0] = e_gw_filter_new ();
	e_gw_filter_add_filter_component (filter[0], E_GW_FILTER_OP_GREATERTHAN_OR_EQUAL, "startDate", l_str);
	e_gw_filter_add_filter_component (filter[0], E_GW_FILTER_OP_LESSTHAN_OR_EQUAL, "startDate", h_str);
	e_gw_filter_add_filter_component (filter[0], E_GW_FILTER_OP_EQUAL, "@type", type);
	e_gw_filter_group_conditions (filter[0], E_GW_FILTER_OP_AND, 3);
	filter[1] = e_gw_filter_new ();
	e_gw_filter_add_filter_component (filter[1], E_GW_FILTER_OP_GREATERTHAN, "startDate", h_str);
	e_gw_filter_add_filter_component (filter[1], E_GW_FILTER_OP_EQUAL, "@type", type);
	e_gw_filter_group_conditions (filter[1], E_GW_FILTER_OP_AND, 2);
	filter[2] = e_gw_filter_new ();
	e_gw_filter_add_filter_component (filter[2], E_GW_FILTER_OP_LESSTHAN, "startDate", l_str);
	e_gw_filter_add_filter_component (filter[2], E_GW_FILTER_OP_EQUAL, "@type", type);
	e_gw_filter_group_conditions (filter[2], E_GW_FILTER_OP_AND, 2);

	for (i = 0; i < 3; i++) {
		status = e_gw_connection_create_cursor (priv->cnc,
				priv->container_id,
				"recipients message recipientStatus attachments default peek", filter[i], &cursor);
		if (status != E_GW_CONNECTION_STATUS_OK) {
			e_cal_backend_groupwise_notify_error_code (cbgw, status);
			return status;
		}
		done = FALSE;
		if (i == 1) {
			position = E_GW_CURSOR_POSITION_START;
			forward = TRUE;

		} else {
			position = E_GW_CURSOR_POSITION_END;
			forward = FALSE;
		}

		e_cal_backend_notify_view_progress_start (E_CAL_BACKEND (cbgw));

		while (!done) {

			status = e_gw_connection_read_cursor (priv->cnc, priv->container_id, cursor, forward, CURSOR_ITEM_LIMIT, position, &list);
			if (status != E_GW_CONNECTION_STATUS_OK) {
				e_cal_backend_groupwise_notify_error_code (cbgw, status);
				return status;
			}
			for (l = list; l != NULL; l = g_list_next(l)) {
				EGwItem *item;
				gchar *progress_string = NULL;

				item = E_GW_ITEM (l->data);
				comp = e_gw_item_to_cal_component (item, cbgw);
				g_object_unref (item);

				/* Show the progress information */
				num++;
				percent = ((gfloat) num/total) * 100;

				/* FIXME The total obtained from the server is wrong. Sometimes the num can
				be greater than the total. The following makes sure that the percentage is not >= 100 */

				if (percent > 100)
					percent = 99;

				progress_string = g_strdup_printf (_("Loading %s items"), type);
				e_cal_backend_notify_view_progress (E_CAL_BACKEND (cbgw), progress_string, percent);

				if (E_IS_CAL_COMPONENT (comp)) {
					gchar *comp_str;

					e_cal_component_commit_sequence (comp);
					comp_str = e_cal_component_get_as_string (comp);
					e_cal_backend_notify_object_created (E_CAL_BACKEND (cbgw), (const gchar *) comp_str);
					g_free (comp_str);
					put_component_to_store (cbgw, comp);
					g_object_unref (comp);
				}
				g_free (progress_string);
			}

			if (!list  || g_list_length (list) == 0)
				done = TRUE;
			g_list_free (list);
			list = NULL;
			position = E_GW_CURSOR_POSITION_CURRENT;
		}
		e_gw_connection_destroy_cursor (priv->cnc, priv->container_id, cursor);
		g_object_unref (filter[i]);
	}
	e_cal_backend_notify_view_progress (E_CAL_BACKEND (cbgw), "", 100);

	return E_GW_CONNECTION_STATUS_OK;
}

typedef struct
{
	EGwItemCalId *calid;
	ECalBackendStore *store;
} CompareIdData;

static gint
compare_ids (gconstpointer a, gconstpointer b)
{
	ECalComponentId *cache_id = (ECalComponentId *) a;
	CompareIdData *data = (CompareIdData *) b;
	EGwItemCalId *calid = data->calid;
	ECalBackendStore *store = data->store;

	if (!calid->recur_key)
		return g_strcmp0 (cache_id->uid, calid->ical_id);
	else {
		ECalComponent *comp;
		gint ret = 1;
		const gchar *cache_item_id;

		if (strcmp (cache_id->uid, calid->recur_key))
			return 1;

		comp = e_cal_backend_store_get_component (store, cache_id->uid, cache_id->rid);
		cache_item_id = e_cal_component_get_gw_id (comp);
		if (!strcmp (cache_item_id, calid->item_id))
			ret = 0;

		g_object_unref (comp);
		return ret;
	}
}

#define ATTEMPTS_KEY "attempts"

static gboolean
get_deltas (gpointer handle)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
	EGwConnection *cnc;
	ECalBackendStore *store;
	EGwConnectionStatus status;
	icalcomponent_kind kind;
	GList *item_list = NULL, *total_list = NULL, *l;
	GSList *cache_ids = NULL, *ls;
	GPtrArray *uid_array = NULL;
	gchar t_str[26];
	const gchar *local_utc_time = NULL, *time_string = NULL, *serv_time, *position;
	gchar *attempts;
	icaltimetype current;
	EGwFilter *filter;
	gboolean done = FALSE;
	gint cursor = 0;
	struct tm tm;
	time_t current_time;
	gboolean needs_to_get = FALSE;

	if (!handle)
		return FALSE;

	cbgw = (ECalBackendGroupwise *) handle;
	priv = cbgw->priv;
	if (priv->mode == CAL_MODE_LOCAL)
		return FALSE;

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));
	cnc = priv->cnc;

	store = priv->store;
	item_list = NULL;

	attempts = g_strdup (e_cal_backend_store_get_key_value (store, ATTEMPTS_KEY));

	serv_time = e_cal_backend_store_get_key_value (store, SERVER_UTC_TIME);
	if (serv_time) {
		time_string = e_cal_backend_store_get_key_value (store, SERVER_UTC_TIME);
		if (!time_string || !*time_string)
			time_string = e_gw_connection_get_server_time (priv->cnc);
	} else
		time_string = e_gw_connection_get_server_time (priv->cnc);

	filter = e_gw_filter_new ();
	/* Items modified after the time-stamp */
	e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_GREATERTHAN, "modified", time_string);
	e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_EQUAL, "@type", get_element_type (kind));
	e_gw_filter_group_conditions (filter, E_GW_FILTER_OP_AND, 2);

	status = e_gw_connection_get_items (cnc, priv->container_id, "attachments recipients message recipientStatus default peek", filter, &item_list);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		status = e_gw_connection_get_items (cnc, priv->container_id, "attachments recipients message recipientStatus default peek", filter, &item_list);

	g_object_unref (filter);

	if (status != E_GW_CONNECTION_STATUS_OK) {

		const gchar *msg = NULL;
		gint failures;

		if (!attempts)
			failures = 2;
		else
			failures = g_ascii_strtod (attempts, NULL) + 1;
		g_free (attempts);
		attempts = g_strdup_printf ("%d", failures);
		e_cal_backend_store_put_key_value (store, ATTEMPTS_KEY, attempts);
		g_free (attempts);

		if (status == E_GW_CONNECTION_STATUS_NO_RESPONSE) {
			return TRUE;
		}

		msg = e_gw_connection_get_error_message (status);

		return TRUE;
	}

	e_cal_backend_store_freeze_changes (store);

	for (; item_list != NULL; item_list = g_list_next(item_list)) {
		EGwItem *item = NULL;
		ECalComponent *modified_comp = NULL, *cache_comp = NULL;
		gchar *cache_comp_str = NULL, *modif_comp_str, *rid = NULL;
		icaltimetype *tt = NULL, *c_tt = NULL;
		const gchar *uid;
		gint r_key;

		item = E_GW_ITEM(item_list->data);
		modified_comp = e_gw_item_to_cal_component (item, cbgw);
		if (!modified_comp) {
			continue;
		}
		if ((r_key = e_gw_item_get_recurrence_key (item)) != 0)
			rid = e_cal_component_get_recurid_as_string (modified_comp);

		e_cal_component_get_uid (modified_comp, &uid);
		cache_comp = e_cal_backend_store_get_component (store, uid, rid);
		g_free (rid);
		e_cal_component_commit_sequence (modified_comp);

		e_cal_component_get_last_modified (modified_comp, &tt);

		if (cache_comp) {
			e_cal_component_get_last_modified (cache_comp, &c_tt);
			e_cal_component_commit_sequence (cache_comp);
		}

		if (!c_tt || icaltime_compare (*tt, *c_tt) == 1)
		{
			modif_comp_str = e_cal_component_get_as_string (modified_comp);

			if (cache_comp) {
				cache_comp_str = e_cal_component_get_as_string (cache_comp);
				e_cal_backend_notify_object_modified (E_CAL_BACKEND (cbgw), cache_comp_str, modif_comp_str);
			} else {
				e_cal_backend_notify_object_created (E_CAL_BACKEND (cbgw), modif_comp_str);
			}

			g_free (modif_comp_str);
			g_free (cache_comp_str);
			cache_comp_str = NULL;
			put_component_to_store (cbgw, modified_comp);
		}

		e_cal_component_free_icaltimetype (tt);

		if (c_tt)
			e_cal_component_free_icaltimetype (c_tt);
		g_object_unref (item);
		g_object_unref (modified_comp);

		if (cache_comp)
			g_object_unref (cache_comp);
	}
	e_cal_backend_store_thaw_changes (store);

	/* Server utc time is the time when we lasted updated changes from server. local_utc_time is the system utc time. As
	   there is no way to get the server time on demand, we use the store system utc time to calculate server's time */
	local_utc_time = e_cal_backend_store_get_key_value (store, LOCAL_UTC_TIME);
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());

	if (priv->first_delta_fetch || !local_utc_time || !*local_utc_time) {
		e_cal_backend_store_put_key_value (store, SERVER_UTC_TIME, e_gw_connection_get_server_time (cnc));
	} else {
		icaltimetype old_local, server_utc;
		struct icaldurationtype dur;

		old_local = icaltime_from_string (local_utc_time);

		dur = icaltime_subtract (current, old_local);
		server_utc = icaltime_from_string (time_string);
		icaltime_adjust (&server_utc, dur.days, dur.hours, dur.minutes, dur.seconds);

		current_time = icaltime_as_timet_with_zone (server_utc, icaltimezone_get_utc_timezone ());
		gmtime_r (&current_time, &tm);

		strftime (t_str, 26, "%Y-%m-%dT%H:%M:%SZ", &tm);
		time_string = t_str;

		e_cal_backend_store_put_key_value (store, SERVER_UTC_TIME, time_string);
	}

	priv->first_delta_fetch = FALSE;

	current_time = icaltime_as_timet_with_zone (current, icaltimezone_get_utc_timezone ());
	gmtime_r (&current_time, &tm);
	strftime (t_str, 26, "%Y-%m-%dT%H:%M:%SZ", &tm);
	time_string = t_str;
	e_cal_backend_store_put_key_value (store, LOCAL_UTC_TIME, time_string);

	if (attempts) {
		e_cal_backend_store_put_key_value (store, ATTEMPTS_KEY, NULL);
		g_free (attempts);
	}

	if (item_list) {
		g_list_free (item_list);
		item_list = NULL;
	}

	/* handle deleted items here by going over the entire cache and
	 * checking for deleted items.*/
	position = E_GW_CURSOR_POSITION_END;
	cursor = 0;
	filter = e_gw_filter_new ();
	e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_EQUAL, "@type", get_element_type (kind));

	status = e_gw_connection_create_cursor (cnc, priv->container_id, "id iCalId recurrenceKey startDate", filter, &cursor);

	g_object_unref (filter);

	if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_NO_RESPONSE) {
			return TRUE;
		}

		e_cal_backend_groupwise_notify_error_code (cbgw, status);
		return TRUE;
	}

	cache_ids = e_cal_backend_store_get_component_ids (store);

	done = FALSE;
	while (!done) {
		status = e_gw_connection_read_cal_ids (cnc, priv->container_id, cursor, FALSE, CURSOR_ICALID_LIMIT, position, &item_list);
		if (status != E_GW_CONNECTION_STATUS_OK) {
			if (status == E_GW_CONNECTION_STATUS_NO_RESPONSE) {
				goto err_done;
			}
			e_cal_backend_groupwise_notify_error_code (cbgw, status);
			goto err_done;
		}

		if (!item_list  || g_list_length (item_list) == 0)
			done = TRUE;
		else {
			if (!total_list)
				total_list = item_list;
			else
				total_list = g_list_concat (total_list, item_list);
		}

		item_list = NULL;

		position = E_GW_CURSOR_POSITION_CURRENT;

	}
	e_gw_connection_destroy_cursor (cnc, priv->container_id, cursor);
	e_cal_backend_store_freeze_changes (store);

	uid_array = g_ptr_array_new ();
	for (l = total_list; l != NULL; l = g_list_next (l)) {
		EGwItemCalId *calid = (EGwItemCalId *)	l->data;
		GSList *remove = NULL;
		CompareIdData data;

		data.calid = calid;
		data.store = store;

		if (!(remove = g_slist_find_custom (cache_ids, &data,  (GCompareFunc) compare_ids))) {
			g_ptr_array_add (uid_array, g_strdup (calid->item_id));
			needs_to_get = TRUE;
		} else  {
			cache_ids = g_slist_remove_link (cache_ids, remove);
			e_cal_component_free_id (remove->data);
		}
	}

	for (ls = cache_ids; ls; ls = g_slist_next (ls)) {
		ECalComponent *comp = NULL;
		icalcomponent *icalcomp = NULL;
		ECalComponentId *id = ls->data;

		comp = e_cal_backend_store_get_component (store, id->uid, id->rid);

		if (!comp)
			continue;

		icalcomp = e_cal_component_get_icalcomponent (comp);
		if (kind == icalcomponent_isa (icalcomp)) {
			gchar *comp_str = NULL;
			ECalComponentId *id = e_cal_component_get_id (comp);

			comp_str = e_cal_component_get_as_string (comp);
			e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbgw),
					id, comp_str, NULL);
			e_cal_backend_store_remove_component (store, id->uid, id->rid);

			e_cal_component_free_id (id);
			g_free (comp_str);
		}
		g_object_unref (comp);
	}

	if (needs_to_get) {
		e_gw_connection_get_items_from_ids (
			cnc, priv->container_id,
			"attachments recipients message recipientStatus recurrenceKey default peek",
			uid_array, &item_list);

		for (l = item_list; l != NULL; l = l->next) {
			ECalComponent *comp = NULL;
			EGwItem *item = NULL;
			gchar *tmp = NULL;

			item = (EGwItem *) l->data;
			comp = e_gw_item_to_cal_component (item, cbgw);
			if (comp) {
				e_cal_component_commit_sequence (comp);
				put_component_to_store (cbgw, comp);
				if (kind == icalcomponent_isa (e_cal_component_get_icalcomponent (comp))) {
					tmp = e_cal_component_get_as_string (comp);
					e_cal_backend_notify_object_created (E_CAL_BACKEND (cbgw), tmp);
					g_free (tmp);
				}
				g_object_unref (comp);
			}

			g_object_unref (item);
		}
	}

	e_cal_backend_store_thaw_changes (store);

	g_ptr_array_foreach (uid_array, (GFunc) g_free, NULL);
	g_ptr_array_free (uid_array, TRUE);

 err_done:
	if (item_list) {
		g_list_free (item_list);
		item_list = NULL;
	}

	if (total_list) {
		g_list_foreach (total_list, (GFunc) e_gw_item_free_cal_id, NULL);
		g_list_free (total_list);
	}

	if (cache_ids) {
		g_slist_foreach (cache_ids, (GFunc) e_cal_component_free_id, NULL);
		g_slist_free (cache_ids);
	}

	return TRUE;
}

static guint
get_cache_refresh_interval (ECalBackendGroupwise *cbgw)
{
	guint time_interval;
	const gchar *time_interval_string = NULL;
	gchar *temp = NULL;
	ECalBackend *backend = E_CAL_BACKEND (cbgw);
	ESource *source;

	time_interval = CACHE_REFRESH_INTERVAL;
	source = e_cal_backend_get_source (backend);

	time_interval_string = g_getenv ("GETQM_TIME_INTERVAL");

	if (!time_interval_string)
		time_interval_string = temp = e_source_get_duped_property (source, "refresh");

	if (time_interval_string) {
		time_interval = g_ascii_strtod (time_interval_string, NULL);
		time_interval *= (60*1000);
	}

	g_free (temp);

	return time_interval;
}

static gpointer
delta_thread (gpointer data)
{
	ECalBackendGroupwise *cbgw = data;
	ECalBackendGroupwisePrivate *priv = cbgw->priv;
	GTimeVal timeout;

	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	while (TRUE)	{
		gboolean succeeded = get_deltas (cbgw);

		g_mutex_lock (priv->dlock->mutex);

		if (!succeeded || priv->dlock->exit)
			break;

		g_get_current_time (&timeout);
		g_time_val_add (&timeout, get_cache_refresh_interval (cbgw) * 1000);
		g_cond_timed_wait (priv->dlock->cond, priv->dlock->mutex, &timeout);

		if (priv->dlock->exit)
			break;

		g_mutex_unlock (priv->dlock->mutex);
	}

	g_mutex_unlock (priv->dlock->mutex);
	priv->dthread = NULL;
	return NULL;
}

static gboolean
fetch_deltas (ECalBackendGroupwise *cbgw)
{
	ECalBackendGroupwisePrivate *priv = cbgw->priv;
	GError *error = NULL;

	/* If the thread is already running just return back */
	if (priv->dthread)
		return FALSE;

	if (!priv->dlock) {
		priv->dlock = g_new0 (SyncDelta, 1);
		priv->dlock->mutex = g_mutex_new ();
		priv->dlock->cond = g_cond_new ();
	}

	priv->dlock->exit = FALSE;
	priv->dthread = g_thread_create ((GThreadFunc) delta_thread, cbgw, TRUE, &error);
	if (!priv->dthread) {
		g_warning (G_STRLOC ": %s", error->message);
		g_error_free (error);
	}

	return TRUE;
}

static gboolean
start_fetch_deltas (gpointer data)
{
	ECalBackendGroupwise *cbgw = data;

	fetch_deltas (cbgw);

	cbgw->priv->timeout_id = 0;
	return FALSE;
}

#if 0
/* TODO call it when a user presses SEND/RECEIVE or refresh*/
static void
e_cal_backend_groupwise_refresh_calendar (ECalBackendGroupwise *cbgw)
{
	ECalBackendGroupwisePrivate *priv = cbgw->priv;
	gboolean delta_started = FALSE;

	if (priv->mode == CAL_MODE_LOCAL)
		return;

	PRIV_LOCK (priv);
	delta_started = fetch_deltas (cbgw);
	PRIV_UNLOCK (priv);

	/* Emit the signal if the delta is already running */
	if (!delta_started)
		g_cond_signal (priv->dlock->cond);
}
#endif

static gchar *
form_uri (ESource *source)
{
	gchar *uri;
	const gchar *port;
	gchar *formed_uri;
	const gchar *use_ssl;

	EUri *parsed_uri;

	uri = e_source_get_uri (source);
	if (uri == NULL)
		return NULL;

	parsed_uri = e_uri_new (uri);
	if (parsed_uri == NULL)
		return NULL;

	port = e_source_get_property (source, "port");
	if (port == NULL)
		port = "7191";
	use_ssl = e_source_get_property (source, "use_ssl");

	if (use_ssl && !g_str_equal (use_ssl, "never"))
		formed_uri = g_strconcat ("https://", parsed_uri->host,":", port, "/soap", NULL );
	else
		formed_uri = g_strconcat ("http://", parsed_uri->host,":", port, "/soap", NULL );

	g_free (uri);
	e_uri_free (parsed_uri);
	return formed_uri;

}

static gpointer
cache_init (ECalBackendGroupwise *cbgw)
{
	ECalBackendGroupwisePrivate *priv = cbgw->priv;
	EGwConnectionStatus cnc_status;
	icalcomponent_kind kind;
	EGwSendOptions *opts;

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));

	cnc_status = e_gw_connection_get_settings (priv->cnc, &opts);
	if (cnc_status == E_GW_CONNECTION_STATUS_OK) {
		GwSettings *hold = g_new0 (GwSettings, 1);

		hold->cbgw = cbgw;
		hold->opts = opts;

		/* We now sync the sendoptions into e-source using the GLIB main loop. Doing this operation
		    in a thread causes crashes. */
		priv->sendoptions_sync_timeout = g_idle_add ((GSourceFunc ) e_cal_backend_groupwise_store_settings, hold);
	} else
		g_warning (G_STRLOC ": Could not get the settings from the server");

	/* get the list of category ids and corresponding names from the server */
	cnc_status = e_gw_connection_get_categories (priv->cnc, &priv->categories_by_id, &priv->categories_by_name);
	if (cnc_status != E_GW_CONNECTION_STATUS_OK) {
		g_warning (G_STRLOC ": Could not get the categories from the server");
        }

	priv->mode = CAL_MODE_REMOTE;

	/* We poke the cache for a default timezone. Its
	 * absence indicates that the cache file has not been
	 * populated before. */
	if (!e_cal_backend_store_get_key_value (priv->store, CACHE_MARKER)) {
		/* Populate the cache for the first time.*/
		/* start a timed polling thread set to 1 minute*/
		cnc_status = populate_cache (cbgw);
		if (cnc_status != E_GW_CONNECTION_STATUS_OK) {
			g_warning (G_STRLOC ": Could not populate the cache");
			/*FIXME  why dont we do a notify here */
			return NULL;
		} else {
			gint time_interval;
			gchar *utc_str;

			time_interval = get_cache_refresh_interval (cbgw);
			utc_str = (gchar *) e_gw_connection_get_server_time (priv->cnc);
			e_cal_backend_store_put_key_value (priv->store, CACHE_MARKER, "1");
			e_cal_backend_store_put_key_value (priv->store, SERVER_UTC_TIME, utc_str);

			priv->timeout_id = g_timeout_add (time_interval, start_fetch_deltas, cbgw);

			return NULL;
		}
	}

	PRIV_LOCK (priv);
	fetch_deltas (cbgw);
	PRIV_UNLOCK (priv);

	return NULL;
}

static gboolean
set_container_id_with_count (ECalBackendGroupwise *cbgw, GError **perror)
{
	ECalBackendGroupwisePrivate *priv;
	GList *container_list = NULL, *l;
	EGwConnectionStatus status;
	icalcomponent_kind kind;

	priv = cbgw->priv;

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));

	switch (kind) {
	case ICAL_VEVENT_COMPONENT:
	case ICAL_VTODO_COMPONENT:
	case ICAL_VJOURNAL_COMPONENT:
		e_source_set_name (e_cal_backend_get_source (E_CAL_BACKEND (cbgw)), _("Calendar"));
		break;
	default:
		priv->container_id = NULL;
		g_propagate_error (perror, EDC_ERROR (UnsupportedMethod));
		return FALSE;
	}

	status = e_gw_connection_get_container_list (priv->cnc, "folders", &container_list);

	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		status = e_gw_connection_get_container_list (priv->cnc, "folders", &container_list);

	if (status != E_GW_CONNECTION_STATUS_OK) {
		g_propagate_error (perror, EDC_ERROR_FAILED_STATUS (OtherError, status));
		return FALSE;
	}

	for (l = container_list; l != NULL; l = l->next) {
		EGwContainer *container = E_GW_CONTAINER (l->data);

		if (e_gw_container_get_is_system_folder (container) &&
				e_gw_container_get_container_type (container) == E_GW_CONTAINER_TYPE_CALENDAR) {

			priv->container_id = g_strdup (e_gw_container_get_id (container));
			priv->total_count = e_gw_container_get_total_count (container);
			break;
		}
	}

	if (l == NULL) {
		g_propagate_error (perror, EDC_ERROR (ObjectNotFound));
		return FALSE;
	}

	e_gw_connection_free_container_list (container_list);

	return TRUE;
}

static void
connect_to_server (ECalBackendGroupwise *cbgw, GError **perror)
{
	gchar *real_uri;
	ECalBackendGroupwisePrivate *priv;
	ESource *source;
	const gchar *use_ssl;
	gchar *http_uri;
	gint permissions;
	GThread *thread;
	GError *error = NULL;
	gchar *parent_user = NULL;
	icalcomponent_kind kind;
	EGwConnectionErrors errors;
	priv = cbgw->priv;

	source = e_cal_backend_get_source (E_CAL_BACKEND (cbgw));
	real_uri = NULL;
	if (source)
		real_uri = form_uri (source);
	use_ssl = e_source_get_property (source, "use_ssl");

	if (!real_uri) {
		g_propagate_error (perror, EDC_ERROR_EX (NoSuchCal, _("Invalid server URI")));
		return;
	}

	errors.status = E_GW_CONNECTION_STATUS_OK;
	errors.description = NULL;

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));

	parent_user = (gchar *) e_source_get_property (source, "parent_id_name");
	/* create connection to server */
	if (parent_user) {
		EGwConnection *cnc;
		/* create connection to server */
		cnc = e_gw_connection_new (real_uri, parent_user, priv->password);
		if (!E_IS_GW_CONNECTION(cnc) && use_ssl && g_str_equal (use_ssl, "when-possible")) {
			http_uri = g_strconcat ("http://", real_uri + 8, NULL);
			cnc = e_gw_connection_new (http_uri, parent_user, priv->password);
			g_free (http_uri);
		}

		if (!cnc) {
			g_propagate_error (perror, EDC_ERROR (AuthenticationFailed));
			return;
		}

		priv->cnc = e_gw_connection_get_proxy_connection (cnc, parent_user, priv->password, priv->username, &permissions);

		g_object_unref(cnc);

		if (!priv->cnc) {
			g_propagate_error (perror, EDC_ERROR (AuthenticationFailed));
			return;
		}

		cbgw->priv->read_only = TRUE;

		if (kind == ICAL_VEVENT_COMPONENT && (permissions & E_GW_PROXY_APPOINTMENT_WRITE) )
			cbgw->priv->read_only = FALSE;
		else if (kind == ICAL_VTODO_COMPONENT && (permissions & E_GW_PROXY_TASK_WRITE))
			cbgw->priv->read_only = FALSE;
		else if (kind == ICAL_VJOURNAL_COMPONENT && (permissions & E_GW_PROXY_NOTES_WRITE))
			cbgw->priv->read_only = FALSE;

	} else {

		priv->cnc = e_gw_connection_new_with_error_handler ( real_uri, priv->username, priv->password, &errors);

		if (!E_IS_GW_CONNECTION(priv->cnc) && use_ssl && g_str_equal (use_ssl, "when-possible")) {
			http_uri = g_strconcat ("http://", real_uri + 8, NULL);
			priv->cnc = e_gw_connection_new_with_error_handler (http_uri, priv->username, priv->password, &errors);
			g_free (http_uri);
		}
		cbgw->priv->read_only = FALSE;
	}
	g_free (real_uri);

	if (priv->cnc ) {
		if (priv->store && priv->container_id) {
			priv->mode = CAL_MODE_REMOTE;
			if (priv->mode_changed && !priv->dthread) {
				priv->mode_changed = FALSE;
				fetch_deltas (cbgw);
			}

			return;
		}
	} else {
		if (errors.status == E_GW_CONNECTION_STATUS_INVALID_PASSWORD) {
			g_propagate_error (perror, EDC_ERROR (AuthenticationFailed));
			return;
		} else if (errors.status == E_GW_CONNECTION_STATUS_UNKNOWN) {
			g_propagate_error (perror,EDC_ERROR (OtherError));
			return;
		}

		g_propagate_error (perror, EDC_ERROR_EX (OtherError, _(errors.description)));
		if (errors.description)
			g_free (errors.description);
		return;
	}
	priv->mode_changed = FALSE;

	if (E_IS_GW_CONNECTION (priv->cnc)) {
		ECalBackend *backend;
		const gchar *cache_dir;

		/* get the ID for the container */
		if (priv->container_id)
			g_free (priv->container_id);

		if (!set_container_id_with_count (cbgw, perror)) {
			return;
		}

		backend = E_CAL_BACKEND (cbgw);
		cache_dir = e_cal_backend_get_cache_dir (backend);

		e_cal_backend_cache_remove (cache_dir, "cache.xml");
		priv->store = e_cal_backend_file_store_new (cache_dir);
		if (!priv->store) {
			g_propagate_error (perror, EDC_ERROR_EX (OtherError, _("Could not create cache file")));
			return;
		}

		e_cal_backend_store_load (priv->store);

		/* spawn a new thread for opening the calendar */
		thread = g_thread_create ((GThreadFunc) cache_init, cbgw, FALSE, &error);
		if (!thread) {
			g_warning (G_STRLOC ": %s", error->message);
			g_error_free (error);

			g_propagate_error (perror, EDC_ERROR_EX (OtherError, _("Could not create thread for populating cache")));
			return;
		}

	} else {
		g_propagate_error (perror, EDC_ERROR (AuthenticationFailed));
		return;
	}

	if (!e_gw_connection_get_version (priv->cnc)) {
		g_propagate_error (perror, EDC_ERROR (InvalidServerVersion));
	}
}

/* Dispose handler for the file backend */
static void
e_cal_backend_groupwise_dispose (GObject *object)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;

	cbgw = E_CAL_BACKEND_GROUPWISE (object);
	priv = cbgw->priv;

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

/* Finalize handler for the file backend */
static void
e_cal_backend_groupwise_finalize (GObject *object)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_GROUPWISE (object));

	cbgw = E_CAL_BACKEND_GROUPWISE (object);
	priv = cbgw->priv;

	/* Clean up */

	if (priv->timeout_id) {
		g_source_remove (priv->timeout_id);
		priv->timeout_id = 0;
	}

	if (priv->dlock) {
		g_mutex_lock (priv->dlock->mutex);
		priv->dlock->exit = TRUE;
		g_mutex_unlock (priv->dlock->mutex);

		g_cond_signal (priv->dlock->cond);

		if (priv->dthread)
			g_thread_join (priv->dthread);

		g_mutex_free (priv->dlock->mutex);
		g_cond_free (priv->dlock->cond);
		g_free (priv->dlock);
		priv->dthread = NULL;
	}

	g_static_rec_mutex_free (&priv->rec_mutex);

	if (priv->cnc) {
		g_object_unref (priv->cnc);
		priv->cnc = NULL;
	}

	if (priv->store) {
		g_object_unref (priv->store);
		priv->store = NULL;
	}

	if (priv->username) {
		g_free (priv->username);
		priv->username = NULL;
	}

	if (priv->password) {
		g_free (priv->password);
		priv->password = NULL;
	}

	if (priv->container_id) {
		g_free (priv->container_id);
		priv->container_id = NULL;
	}

	if (priv->user_email) {
		g_free (priv->user_email);
		priv->user_email = NULL;
	}

	if (priv->sendoptions_sync_timeout) {
		g_source_remove (priv->sendoptions_sync_timeout);
		priv->sendoptions_sync_timeout = 0;
	}

	if (priv->default_zone) {
		icaltimezone_free (priv->default_zone, 1);
		priv->default_zone = NULL;
	}

	g_free (priv);
	cbgw->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Calendar backend methods */

/* Is_read_only handler for the file backend */
static void
e_cal_backend_groupwise_is_read_only (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only, GError **perror)
{
	ECalBackendGroupwise *cbgw;

	cbgw = E_CAL_BACKEND_GROUPWISE(backend);
	*read_only = cbgw->priv->read_only;
}

/* return email address of the person who opened the calendar */
static void
e_cal_backend_groupwise_get_cal_address (ECalBackendSync *backend, EDataCal *cal, gchar **address, GError **perror)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;

	cbgw = E_CAL_BACKEND_GROUPWISE(backend);
	priv = cbgw->priv;

	if (priv->mode == CAL_MODE_REMOTE) {
		if (priv->user_email)
			g_free (priv->user_email);

		priv->user_email = g_strdup (e_gw_connection_get_user_email (cbgw->priv->cnc));
	}

	*address = g_strdup (priv->user_email);
}

static void
e_cal_backend_groupwise_get_ldap_attribute (ECalBackendSync *backend, EDataCal *cal, gchar **attribute, GError **perror)
{
	/* ldap attribute is specific to Sun ONE connector to get free busy information*/
	/* retun NULL here as group wise backend know how to get free busy information */

	*attribute = NULL;
}

static void
e_cal_backend_groupwise_get_alarm_email_address (ECalBackendSync *backend, EDataCal *cal, gchar **address, GError **perror)
{
	/*group wise does not support email based alarms */

	*address = NULL;
}

static void
e_cal_backend_groupwise_get_static_capabilities (ECalBackendSync *backend, EDataCal *cal, gchar **capabilities, GError **perror)
{
	*capabilities = g_strdup (CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS ","
				  CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY ","
				  CAL_STATIC_CAPABILITY_REMOVE_ALARMS ","
				  CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
				  CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
				  CAL_STATIC_CAPABILITY_NO_CONV_TO_ASSIGN_TASK ","
				  CAL_STATIC_CAPABILITY_NO_CONV_TO_RECUR ","
				  CAL_STATIC_CAPABILITY_REQ_SEND_OPTIONS ","
				  CAL_STATIC_CAPABILITY_SAVE_SCHEDULES ","
				  CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ACCEPT ","
				  CAL_STATIC_CAPABILITY_DELEGATE_SUPPORTED ","
				  CAL_STATIC_CAPABILITY_DELEGATE_TO_MANY ","
				  CAL_STATIC_CAPABILITY_NO_ORGANIZER ","
				  CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER ","
				  CAL_STATIC_CAPABILITY_HAS_UNACCEPTED_MEETING ","
				  CAL_STATIC_CAPABILITY_SAVE_SCHEDULES);
}

static void
in_offline (ECalBackendGroupwise *cbgw) {
	ECalBackendGroupwisePrivate *priv;

	priv= cbgw->priv;
	priv->read_only = TRUE;

	if (priv->dlock) {
		g_mutex_lock (priv->dlock->mutex);
		priv->dlock->exit = TRUE;
		g_mutex_unlock (priv->dlock->mutex);

		g_cond_signal (priv->dlock->cond);
	}

	if (priv->timeout_id) {
		g_source_remove (priv->timeout_id);
		priv->timeout_id = 0;
	}

	if (priv->cnc) {
		g_object_unref (priv->cnc);
		priv->cnc = NULL;
	}

}

/* Open handler for the file backend */
static void
e_cal_backend_groupwise_open (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists,
			      const gchar *username, const gchar *password, GError **perror)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
	const gchar *cache_dir;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	cache_dir = e_cal_backend_get_cache_dir (E_CAL_BACKEND (backend));

	PRIV_LOCK (priv);

	cbgw->priv->read_only = FALSE;

	if (priv->mode == CAL_MODE_LOCAL) {
		ESource *esource;
		const gchar *display_contents = NULL;

		cbgw->priv->read_only = TRUE;
		esource = e_cal_backend_get_source (E_CAL_BACKEND (cbgw));
		display_contents = e_source_get_property (esource, "offline_sync");

		if (!display_contents || !g_str_equal (display_contents, "1")) {
			PRIV_UNLOCK (priv);
			g_propagate_error (perror, EDC_ERROR (RepositoryOffline));
			return;
		}

		if (!priv->store) {
			/* remove the old cache while migrating to ECalBackendStore */
			e_cal_backend_cache_remove (cache_dir, "cache.xml");
			priv->store = e_cal_backend_file_store_new (cache_dir);
			if (!priv->store) {
				PRIV_UNLOCK (priv);
				g_propagate_error (perror, EDC_ERROR_EX (OtherError, _("Could not create cache file")));
				return;
			}
		}

		e_cal_backend_store_load (priv->store);
		PRIV_UNLOCK (priv);
		return;
	}

	priv->username = g_strdup (username);
	priv->password = g_strdup (password);

	/* FIXME: no need to set it online here when we implement the online/offline stuff correctly */
	connect_to_server (cbgw, perror);

	PRIV_UNLOCK (priv);
}

static void
e_cal_backend_groupwise_remove (ECalBackendSync *backend, EDataCal *cal, GError **perror)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	PRIV_LOCK (priv);

	/* remove the cache */
	if (priv->store)
		e_cal_backend_store_remove (priv->store);

	PRIV_UNLOCK (priv);
}

/* is_loaded handler for the file backend */
static gboolean
e_cal_backend_groupwise_is_loaded (ECalBackend *backend)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	return priv->store ? TRUE : FALSE;
}

/* is_remote handler for the file backend */
static CalMode
e_cal_backend_groupwise_get_mode (ECalBackend *backend)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	return priv->mode;
}

/* Set_mode handler for the file backend */
static void
e_cal_backend_groupwise_set_mode (ECalBackend *backend, CalMode mode)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	if (priv->mode == mode) {
		e_cal_backend_notify_mode (backend, ModeSet,
					   cal_mode_to_corba (mode));
		return;
	}

	PRIV_LOCK (priv);

	priv->mode_changed = TRUE;
	switch (mode) {
	case CAL_MODE_REMOTE :/* go online */
		priv->mode = CAL_MODE_REMOTE;
		priv->read_only = FALSE;
		e_cal_backend_notify_mode (backend, ModeSet, Remote);
		e_cal_backend_notify_readonly (backend, priv->read_only);
		if (e_cal_backend_groupwise_is_loaded (backend))
			      e_cal_backend_notify_auth_required(backend);
		break;

	case CAL_MODE_LOCAL : /* go offline */
		/* FIXME: make sure we update the cache before closing the connection */
		priv->mode = CAL_MODE_LOCAL;
		in_offline (cbgw);
		e_cal_backend_notify_readonly (backend, priv->read_only);
		e_cal_backend_notify_mode (backend, ModeSet, Local);

		break;
	default :
		e_cal_backend_notify_mode (backend, ModeNotSupported,
					   cal_mode_to_corba (mode));
	}

	PRIV_UNLOCK (priv);
}

static void
e_cal_backend_groupwise_get_default_object (ECalBackendSync *backend, EDataCal *cal, gchar **object, GError **perror)
{

	ECalComponent *comp;

        comp = e_cal_component_new ();

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
	case ICAL_VEVENT_COMPONENT:
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
		break;
	case ICAL_VTODO_COMPONENT:
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
		break;
	default:
		g_object_unref (comp);
		g_propagate_error (perror, EDC_ERROR (ObjectNotFound));
		return;
	}

	*object = e_cal_component_get_as_string (comp);
	g_object_unref (comp);
}

/* Get_object_component handler for the groupwise backend */
static void
e_cal_backend_groupwise_get_object (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid, gchar **object, GError **error)
{
	ECalComponent *comp;
	ECalBackendGroupwisePrivate *priv;
	ECalBackendGroupwise *cbgw = (ECalBackendGroupwise *) backend;

	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), InvalidArg);

	priv = cbgw->priv;

	PRIV_LOCK (priv);

	/* search the object in the cache */
	comp = e_cal_backend_store_get_component (priv->store, uid, rid);
	if (comp) {
		PRIV_UNLOCK (priv);
		if (e_cal_backend_get_kind (E_CAL_BACKEND (backend)) ==
		    icalcomponent_isa (e_cal_component_get_icalcomponent (comp)))
			*object = e_cal_component_get_as_string (comp);
		else
			*object = NULL;

		g_object_unref (comp);

		if (!*object)
			g_propagate_error (error, EDC_ERROR (ObjectNotFound));
		return;
	}

	PRIV_UNLOCK (priv);

	/* callers will never have a uuid that is in server but not in cache */
	g_propagate_error (error, EDC_ERROR (ObjectNotFound));
}

/* Add_timezone handler for the groupwise backend */
static void
e_cal_backend_groupwise_add_timezone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj, GError **error)
{
	icalcomponent *tz_comp;
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;

	cbgw = (ECalBackendGroupwise *) backend;

	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), InvalidArg);
	e_return_data_cal_error_if_fail (tzobj != NULL, InvalidArg);

	priv = cbgw->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	if (icalcomponent_isa (tz_comp) == ICAL_VTIMEZONE_COMPONENT) {
		icaltimezone *zone;

		zone = icaltimezone_new ();
		icaltimezone_set_component (zone, tz_comp);
		if (e_cal_backend_store_put_timezone (priv->store, zone) == FALSE) {
			icaltimezone_free (zone, 1);
			g_propagate_error (error, EDC_ERROR_EX (OtherError, "Put timezone failed"));
			return;
		}
		icaltimezone_free (zone, 1);
	}
}

static void
e_cal_backend_groupwise_set_default_zone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj, GError **error)
{
	icalcomponent *tz_comp;
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
	icaltimezone *zone;

	cbgw = (ECalBackendGroupwise *) backend;

	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), InvalidArg);
	e_return_data_cal_error_if_fail (tzobj != NULL, InvalidArg);

	priv = cbgw->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);

	PRIV_LOCK (priv);

	if (priv->default_zone)
		icaltimezone_free (priv->default_zone, 1);

	/* Set the default timezone to it. */
	priv->default_zone = zone;

	PRIV_UNLOCK (priv);
}

/* Gets the list of attachments */
static void
e_cal_backend_groupwise_get_attachment_list (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid, GSList **list, GError **perror)
{
	/* TODO implement the function */
}

/* Get_objects_in_range handler for the groupwise backend */
static void
e_cal_backend_groupwise_get_object_list (ECalBackendSync *backend, EDataCal *cal, const gchar *sexp, GList **objects, GError **perror)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
        GSList *components, *l;
	ECalBackendSExp *cbsexp;
	gboolean search_needed = TRUE;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	if (!strcmp (sexp, "#t"))
		search_needed = FALSE;

	cbsexp = e_cal_backend_sexp_new (sexp);
	if (!cbsexp) {
		g_propagate_error (perror, EDC_ERROR (InvalidQuery));
		return;
	}

	*objects = NULL;

	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times(cbsexp,
									    &occur_start,
									    &occur_end);
	components = prunning_by_time ?
		e_cal_backend_store_get_components_occuring_in_range (priv->store, occur_start, occur_end)
		: e_cal_backend_store_get_components (priv->store);

	for (l = components; l != NULL; l = l->next) {
                ECalComponent *comp = E_CAL_COMPONENT (l->data);

		if (e_cal_backend_get_kind (E_CAL_BACKEND (backend)) ==
		    icalcomponent_isa (e_cal_component_get_icalcomponent (comp))) {
			if ((!search_needed) ||
			    (e_cal_backend_sexp_match_comp (cbsexp, comp, E_CAL_BACKEND (backend)))) {
				*objects = g_list_append (*objects, e_cal_component_get_as_string (comp));
			}
		}
        }

	g_object_unref (cbsexp);
	g_slist_foreach (components, (GFunc) g_object_unref, NULL);
	g_slist_free (components);
}

/* get_query handler for the groupwise backend */
static void
e_cal_backend_groupwise_start_query (ECalBackend *backend, EDataCalView *query)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
        GList *objects = NULL;
	GError *err = NULL;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

        e_cal_backend_groupwise_get_object_list (E_CAL_BACKEND_SYNC (backend), NULL,
							  e_data_cal_view_get_text (query), &objects, &err);
        if (err) {
		e_data_cal_view_notify_done (query, err);
		g_error_free (err);
                return;
	}

	/* notify listeners of all objects */
	if (objects) {
		e_data_cal_view_notify_objects_added (query, (const GList *) objects);

		/* free memory */
		g_list_foreach (objects, (GFunc) g_free, NULL);
		g_list_free (objects);
	}

	e_data_cal_view_notify_done (query, NULL);
}

/* Get_free_busy handler for the file backend */
static void
e_cal_backend_groupwise_get_free_busy (ECalBackendSync *backend, EDataCal *cal, GList *users,
				       time_t start, time_t end, GList **freebusy, GError **perror)
{
       EGwConnectionStatus status;
       ECalBackendGroupwise *cbgw;
       EGwConnection *cnc;

       cbgw = E_CAL_BACKEND_GROUPWISE (backend);
       cnc = cbgw->priv->cnc;

	if (cbgw->priv->mode == CAL_MODE_LOCAL) {
		in_offline (cbgw);
		g_propagate_error (perror, EDC_ERROR (RepositoryOffline));
		return;
	}

	status = e_gw_connection_get_freebusy_info (cbgw, users, start, end, freebusy);

	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		status = e_gw_connection_get_freebusy_info (cbgw, users, start, end, freebusy);

	if (status != E_GW_CONNECTION_STATUS_OK)
		g_propagate_error (perror, EDC_ERROR_FAILED_STATUS (OtherError, status));
}

typedef struct {
	ECalBackendGroupwise *backend;
	icalcomponent_kind kind;
	GList *deletes;
	EXmlHash *ehash;
} ECalBackendGroupwiseComputeChangesData;

static void
e_cal_backend_groupwise_compute_changes_foreach_key (const gchar *key, const gchar *value, gpointer data)
{
	ECalBackendGroupwiseComputeChangesData *be_data = data;

	if (!e_cal_backend_store_get_component (be_data->backend->priv->store, key, NULL)) {
		ECalComponent *comp;

		comp = e_cal_component_new ();
		if (be_data->kind == ICAL_VTODO_COMPONENT)
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
		else
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);

		e_cal_component_set_uid (comp, key);
		be_data->deletes = g_list_prepend (be_data->deletes, e_cal_component_get_as_string (comp));

		e_xmlhash_remove (be_data->ehash, key);
		g_object_unref (comp);
	}
}

static void
e_cal_backend_groupwise_compute_changes (ECalBackendGroupwise *cbgw, const gchar *change_id,
					 GList **adds, GList **modifies, GList **deletes, GError **perror)
{
	gchar    *filename;
	EXmlHash *ehash;
	ECalBackendGroupwiseComputeChangesData be_data;
	GList *i, *list = NULL;
	gchar *unescaped_uri;
	GError *err = NULL;

	/* FIXME Will this always work? */
	unescaped_uri = g_uri_unescape_string (cbgw->priv->uri, "");
	filename = g_strdup_printf ("%s-%s.db", unescaped_uri, change_id);
	ehash = e_xmlhash_new (filename);
	g_free (filename);
	g_free (unescaped_uri);

        e_cal_backend_groupwise_get_object_list (E_CAL_BACKEND_SYNC (cbgw), NULL, "#t", &list, &err);
        if (err) {
		g_propagate_error (perror, err);
                return;
	}

        /* Calculate adds and modifies */
	for (i = list; i != NULL; i = g_list_next (i)) {
		const gchar *uid;
		gchar *calobj;
		ECalComponent *comp;

		comp = e_cal_component_new_from_string (i->data);
		e_cal_component_get_uid (comp, &uid);
		calobj = i->data;

		g_assert (calobj != NULL);

		/* check what type of change has occurred, if any */
		switch (e_xmlhash_compare (ehash, uid, calobj)) {
		case E_XMLHASH_STATUS_SAME:
			break;
		case E_XMLHASH_STATUS_NOT_FOUND:
			*adds = g_list_prepend (*adds, g_strdup (calobj));
			e_xmlhash_add (ehash, uid, calobj);
			break;
		case E_XMLHASH_STATUS_DIFFERENT:
			*modifies = g_list_prepend (*modifies, g_strdup (calobj));
			e_xmlhash_add (ehash, uid, calobj);
			break;
		}

		g_free (calobj);
		g_object_unref (comp);
	}
	g_list_free (list);

	/* Calculate deletions */
	be_data.backend = cbgw;
	be_data.kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));
	be_data.deletes = NULL;
	be_data.ehash = ehash;
	e_xmlhash_foreach_key (ehash, (EXmlHashFunc)e_cal_backend_groupwise_compute_changes_foreach_key, &be_data);

	*deletes = be_data.deletes;

	e_xmlhash_write (ehash);
	e_xmlhash_destroy (ehash);
}

/* Get_changes handler for the groupwise backend */
static void
e_cal_backend_groupwise_get_changes (ECalBackendSync *backend, EDataCal *cal, const gchar *change_id,
				     GList **adds, GList **modifies, GList **deletes, GError **error)
{
        ECalBackendGroupwise *cbgw;
	cbgw = E_CAL_BACKEND_GROUPWISE (backend);

	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), InvalidArg);
	e_return_data_cal_error_if_fail (change_id != NULL, InvalidArg);

	e_cal_backend_groupwise_compute_changes (cbgw, change_id, adds, modifies, deletes, error);
}

/* Discard_alarm handler for the file backend */
static void
e_cal_backend_groupwise_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *auid, GError **perror)
{
	g_propagate_error (perror, EDC_ERROR (NotSupported));
}

static icaltimezone *
e_cal_backend_groupwise_internal_get_default_timezone (ECalBackend *backend)
{
	ECalBackendGroupwise *cbgw = E_CAL_BACKEND_GROUPWISE (backend);

	return cbgw->priv->default_zone;
}

static icaltimezone *
e_cal_backend_groupwise_internal_get_timezone (ECalBackend *backend, const gchar *tzid)
{
	icaltimezone *zone = NULL;
	ECalBackendGroupwise *cbgw;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	g_return_val_if_fail (cbgw != NULL, NULL);
	g_return_val_if_fail (cbgw->priv != NULL, NULL);

	if (cbgw->priv->store)
		zone = (icaltimezone *) e_cal_backend_store_get_timezone (cbgw->priv->store, tzid);

	if (!zone && E_CAL_BACKEND_CLASS (parent_class)->internal_get_timezone)
		zone = E_CAL_BACKEND_CLASS (parent_class)->internal_get_timezone (backend, tzid);

	return zone;
}

static EGwConnectionStatus
update_from_server (ECalBackendGroupwise *cbgw, GSList *uid_list, gchar **calobj, ECalComponent *comp)
{
	EGwConnectionStatus stat;
	ECalBackendGroupwisePrivate *priv;
	ECalBackendSync *backend;
	GList *list = NULL, *tmp;
	GSList *l;
	GPtrArray *uid_array = g_ptr_array_new ();
	gint i;

	priv = cbgw->priv;
	backend = E_CAL_BACKEND_SYNC (cbgw);

	for (l = uid_list; l; l = g_slist_next (l)) {
		g_ptr_array_add (uid_array, l->data);
	}

	/* convert uid_list to GPtrArray and get the items in a list */
	stat = e_gw_connection_get_items_from_ids (priv->cnc,
			priv->container_id,
			"attachments recipients message recipientStatus default peek",
			uid_array, &list);

	if (stat != E_GW_CONNECTION_STATUS_OK || (list == NULL) || (g_list_length (list) == 0)) {
		g_ptr_array_free (uid_array, TRUE);
		return stat;
	}

	comp = g_object_ref ( (ECalComponent *) list->data );
	/* convert items into components and add them to the cache */
	for (i=0, tmp = list; tmp; tmp = g_list_next (tmp), i++) {
		ECalComponent *e_cal_comp;
		EGwItem *item;

		item = (EGwItem *) tmp->data;
		e_cal_comp = e_gw_item_to_cal_component (item, cbgw);
		e_cal_component_commit_sequence (e_cal_comp);
		put_component_to_store (cbgw, e_cal_comp);

		if (i == 0) {
			*calobj = e_cal_component_get_as_string (e_cal_comp);
		}

		if (i != 0) {
			gchar *temp;
			temp = e_cal_component_get_as_string (e_cal_comp);
			e_cal_backend_notify_object_created (E_CAL_BACKEND (cbgw), temp);
			g_free (temp);
		}

		g_object_unref (e_cal_comp);
	}
	g_ptr_array_free (uid_array, TRUE);

	return E_GW_CONNECTION_STATUS_OK;
}

static void
e_cal_backend_groupwise_create_object (ECalBackendSync *backend, EDataCal *cal, gchar **calobj, gchar **uid, GError **error)
{
	ECalBackendGroupwise *cbgw;
        ECalBackendGroupwisePrivate *priv;
	icalcomponent *icalcomp;
	ECalComponent *comp;
	EGwConnectionStatus status;
	GSList *uid_list = NULL;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), InvalidArg);
	e_return_data_cal_error_if_fail (calobj != NULL && *calobj != NULL, InvalidArg);

	if (priv->mode == CAL_MODE_LOCAL) {
		in_offline(cbgw);
		g_propagate_error (error, EDC_ERROR (RepositoryOffline));
		return;
	}

	/* check the component for validity */
	icalcomp = icalparser_parse_string (*calobj);
	if (!icalcomp) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	if (e_cal_backend_get_kind (E_CAL_BACKEND (backend)) != icalcomponent_isa (icalcomp)) {
		icalcomponent_free (icalcomp);
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);

	/* check if the object exists */
	switch (priv->mode) {
	case CAL_MODE_ANY :
	case CAL_MODE_REMOTE :
		/* when online, send the item to the server */
		status = e_gw_connection_create_appointment (priv->cnc, priv->container_id, cbgw, comp, &uid_list);

		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			status = e_gw_connection_create_appointment (priv->cnc, priv->container_id, cbgw, comp, &uid_list);

		if (status != E_GW_CONNECTION_STATUS_OK) {
			g_object_unref (comp);

			if (status == E_GW_CONNECTION_STATUS_UNKNOWN_USER) {
				g_propagate_error (error, EDC_ERROR (UnknownUser));
				return;
			} else if (status == E_GW_CONNECTION_STATUS_OVER_QUOTA) {
				g_propagate_error (error, EDC_ERROR (PermissionDenied));
				return;
			} else {
				g_propagate_error (error, EDC_ERROR_FAILED_STATUS (OtherError, status));
				return;
			}
		}

		/* If delay deliver has been set, server will not send the uid */
		if (!uid_list || ((e_cal_component_get_vtype (comp) == E_CAL_COMPONENT_JOURNAL) && e_cal_component_has_organizer (comp))) {
			g_object_unref (comp);
			return;
		}

		/* Get the item back from server to update the last-modified time */
		status = update_from_server (cbgw, uid_list, calobj, comp);
		if (status != E_GW_CONNECTION_STATUS_OK) {
			g_propagate_error (error, EDC_ERROR_FAILED_STATUS (OtherError, status));
			return;
		}

		break;
	default :
		break;
	}

	g_object_unref (comp);
}

static void
get_retract_data (ECalComponent *comp, const gchar **retract_comment, gboolean *all_instances)
{
	icalcomponent *icalcomp = NULL;
	icalproperty *icalprop = NULL;
	gboolean is_instance = FALSE;
	const gchar *x_ret = NULL, *x_recur = NULL;

	is_instance = e_cal_component_is_instance (comp);
	icalcomp = e_cal_component_get_icalcomponent (comp);
	icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
	while (icalprop) {
		const gchar *x_name;

		x_name = icalproperty_get_x_name (icalprop);
		/* This property will be set only if the user is an organizer */
		if (!strcmp (x_name, "X-EVOLUTION-RETRACT-COMMENT")) {
			x_ret = icalproperty_get_x (icalprop);
			if (!strcmp (x_ret, "0")) {
				*retract_comment = NULL;
			} else
				*retract_comment = x_ret;
		}

		if (is_instance && !strcmp (x_name, "X-EVOLUTION-RECUR-MOD")) {
			x_recur = icalproperty_get_x (icalprop);
			if (!strcmp (x_recur, "All"))
				*all_instances = TRUE;
			else
				*all_instances = FALSE;
		}

		if (x_ret && (!is_instance || x_recur))
			break;
		icalprop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY);
	}
}

static void
e_cal_backend_groupwise_modify_object (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj,
				       CalObjModType mod, gchar **old_object, gchar **new_object, GError **error)
{
	ECalBackendGroupwise *cbgw;
        ECalBackendGroupwisePrivate *priv;
	icalcomponent *icalcomp;
	ECalComponent *comp, *cache_comp = NULL;
	EGwConnectionStatus status;
	EGwItem *item, *cache_item;
	const gchar *uid = NULL;
	gchar *rid = NULL;

	*old_object = NULL;
	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), InvalidArg);
	e_return_data_cal_error_if_fail (calobj != NULL, InvalidArg);

	if (priv->mode == CAL_MODE_LOCAL) {
		in_offline (cbgw);
		g_propagate_error (error, EDC_ERROR (RepositoryOffline));
		return;
	}

	/* check the component for validity */
	icalcomp = icalparser_parse_string (calobj);
	if (!icalcomp) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}
	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);
	e_cal_component_get_uid (comp, &uid);
	rid = e_cal_component_get_recurid_as_string (comp);

	/* check if the object exists */
	switch (priv->mode) {
	case CAL_MODE_ANY :
	case CAL_MODE_REMOTE :
		/* when online, send the item to the server */
		cache_comp = e_cal_backend_store_get_component (priv->store, uid, rid);
		if (!cache_comp) {
			g_critical ("Could not find the object in cache");
			g_free (rid);
			g_propagate_error (error, EDC_ERROR (ObjectNotFound));
			return;
		}

		if (e_cal_component_has_attendees (comp) &&
				e_cal_backend_groupwise_utils_check_delegate (comp, e_gw_connection_get_user_email (priv->cnc))) {
			const gchar *id = NULL, *recur_key = NULL;

			item = e_gw_item_new_for_delegate_from_cal (cbgw, comp);

			if (mod == CALOBJ_MOD_ALL && e_cal_component_is_instance (comp)) {
				recur_key = uid;
			}
			id = e_gw_item_get_id (item);

			status = e_gw_connection_delegate_request (priv->cnc, item, id, NULL, NULL, recur_key);

			if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
				status = e_gw_connection_delegate_request (priv->cnc, item, id, NULL, NULL, recur_key);
			if (status != E_GW_CONNECTION_STATUS_OK) {
				g_object_unref (comp);
				g_object_unref (cache_comp);
				g_free (rid);
				g_propagate_error (error, EDC_ERROR_FAILED_STATUS (OtherError, status));
				return;
			}

			put_component_to_store (cbgw, comp);
			*new_object = e_cal_component_get_as_string (comp);
			break;
		}

		item = e_gw_item_new_from_cal_component (priv->container_id, cbgw, comp);
		cache_item =  e_gw_item_new_from_cal_component (priv->container_id, cbgw, cache_comp);
		if ( e_gw_item_get_item_type (item) == E_GW_ITEM_TYPE_TASK) {
			gboolean completed, cache_completed;

			completed = e_gw_item_get_completed (item);
			cache_completed = e_gw_item_get_completed (cache_item);
			if (completed && !cache_completed) {
				/*FIXME  return values. */
				status = e_gw_connection_complete_request (priv->cnc, e_gw_item_get_id (item));

				if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
					status = e_gw_connection_complete_request (priv->cnc, e_gw_item_get_id (item));

				if (status != E_GW_CONNECTION_STATUS_OK) {
					g_object_unref (comp);
					g_object_unref (cache_comp);
					g_free (rid);

					if (status == E_GW_CONNECTION_STATUS_OVER_QUOTA) {
						g_propagate_error (error, EDC_ERROR (PermissionDenied));
						return;
					}

					g_propagate_error (error, EDC_ERROR_FAILED_STATUS (OtherError, status));
					return;
				}
				put_component_to_store (cbgw, comp);
				break;
			}
		}

		e_gw_item_set_changes (item, cache_item);

		/* the second argument is redundant */
		status = e_gw_connection_modify_item (priv->cnc, e_gw_item_get_id (item), item);

		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			status = e_gw_connection_modify_item (priv->cnc, e_gw_item_get_id (item), item);

		if (status != E_GW_CONNECTION_STATUS_OK) {
			g_object_unref (comp);
			g_object_unref (cache_comp);
			g_free (rid);
			g_propagate_error (error, EDC_ERROR_FAILED_STATUS (OtherError, status));
			return;
		}
		/* if successful, update the cache */

	case CAL_MODE_LOCAL :
		/* in offline mode, we just update the cache */
		put_component_to_store (cbgw, comp);
		break;
	default :
		break;
	}

	*old_object = e_cal_component_get_as_string (cache_comp);
	g_object_unref (cache_comp);
	g_object_unref (comp);
	g_free (rid);
}

static const gchar *
get_gw_item_id (icalcomponent *icalcomp)
{
	icalproperty *icalprop;

	/* search the component for the X-GWRECORDID property */
	icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
	while (icalprop) {
		const gchar *x_name, *x_val;

		x_name = icalproperty_get_x_name (icalprop);
		x_val = icalproperty_get_x (icalprop);
		if (!strcmp (x_name, "X-GWRECORDID")) {
			return x_val;
		}

		icalprop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY);
	}
	return NULL;
}

/* Remove_object handler for the file backend */
static void
e_cal_backend_groupwise_remove_object (ECalBackendSync *backend, EDataCal *cal,
				       const gchar *uid, const gchar *rid,
				       CalObjModType mod, gchar **old_object,
				       gchar **object, GError **perror)
{
	ECalBackendGroupwise *cbgw;
        ECalBackendGroupwisePrivate *priv;
	EGwConnectionStatus status;
	gchar *calobj = NULL;
	GError *err = NULL;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	*old_object = *object = NULL;

	/* if online, remove the item from the server */
	if (priv->mode == CAL_MODE_REMOTE) {
		const gchar *id_to_remove = NULL;
		icalcomponent *icalcomp;

		e_cal_backend_groupwise_get_object (backend, cal, uid, rid, &calobj, &err);
		if (err) {
			g_propagate_error (perror, err);
			return;
		}

		icalcomp = icalparser_parse_string (calobj);
		if (!icalcomp) {
			g_free (calobj);
			g_propagate_error (perror, EDC_ERROR (InvalidObject));
			return;
		}

		if (mod == CALOBJ_MOD_THIS) {
			id_to_remove = get_gw_item_id (icalcomp);
			if (!id_to_remove) {
				/* use the iCalId to remove the object */
				id_to_remove = uid;
			}

			/* remove the object */
			status = e_gw_connection_remove_item (priv->cnc, priv->container_id, id_to_remove);

			if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
				status = e_gw_connection_remove_item (priv->cnc, priv->container_id, id_to_remove);

			icalcomponent_free (icalcomp);
			if (status == E_GW_CONNECTION_STATUS_OK) {
				/* remove the component from the cache */
				if (!e_cal_backend_store_remove_component (priv->store, uid, rid)) {
					g_free (calobj);
					g_propagate_error (perror, EDC_ERROR (ObjectNotFound));
					return;
				}
				*object = NULL;
				*old_object = strdup (calobj);
				g_free (calobj);
				return;
			} else {
				g_free (calobj);
				g_propagate_error (perror, EDC_ERROR_FAILED_STATUS (OtherError, status));
				return;
			}
		} else if (mod == CALOBJ_MOD_ALL) {
			GSList *l, *comp_list = e_cal_backend_store_get_components_by_uid (priv->store, uid);

			if (e_cal_component_has_attendees (E_CAL_COMPONENT (comp_list->data))) {
				/* get recurrence key and send it to
				 * e_gw_connection_remove_recurrence_item */

				id_to_remove = get_gw_item_id (e_cal_component_get_icalcomponent (comp_list->data));
				status = e_gw_connection_decline_request (priv->cnc, id_to_remove, NULL, uid);
				if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
					status = e_gw_connection_decline_request (priv->cnc, id_to_remove, NULL, uid);
			} else {
				GList *item_ids = NULL;

				for (l = comp_list; l; l = l->next) {
					ECalComponent *comp = E_CAL_COMPONENT (l->data);

					id_to_remove = get_gw_item_id (e_cal_component_get_icalcomponent (comp));
					item_ids = g_list_append (item_ids, (gchar *) id_to_remove);
				}
				status = e_gw_connection_remove_items (priv->cnc, priv->container_id, item_ids);

				if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
					status = e_gw_connection_remove_items (priv->cnc, priv->container_id, item_ids);
			}

			if (status == E_GW_CONNECTION_STATUS_OK) {

				for (l = comp_list; l; l = l->next) {
					ECalComponent *comp = E_CAL_COMPONENT (l->data);
					ECalComponentId *id = e_cal_component_get_id (comp);

					e_cal_backend_store_remove_component (priv->store, id->uid,
							id->rid);
					if (!id->rid || !g_str_equal (id->rid, rid)) {
						gchar *comp_str = e_cal_component_get_as_string (comp);
						e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbgw), id, comp_str, NULL);
						g_free (comp_str);
					}
					e_cal_component_free_id (id);

					g_object_unref (comp);

				}
				/* Setting NULL would trigger another signal.
				 * We do not set the *object to NULL  */
				g_slist_free (comp_list);
				*old_object = strdup (calobj);
				*object = NULL;
				g_free (calobj);
				return;
			} else {
				g_free (calobj);
				g_propagate_error (perror, EDC_ERROR_FAILED_STATUS (OtherError, status));
				return;
			}
		} else {
			g_propagate_error (perror, EDC_ERROR (UnsupportedMethod));
			return;
		}
	} else if (priv->mode == CAL_MODE_LOCAL) {
		in_offline (cbgw);
		g_propagate_error (perror, EDC_ERROR (RepositoryOffline));
		return;
	} else {
		g_propagate_error (perror, EDC_ERROR_EX (OtherError, "Incorrect online mode set"));
		return;
	}
}

/* This function is largely duplicated in
 * ../file/e-cal-backend-file.c
 */
static void
fetch_attachments (ECalBackendGroupwise *cbgw, ECalComponent *comp)
{
	GSList *attach_list = NULL, *new_attach_list = NULL;
	GSList *l;
	gchar *dest_url, *dest_file;
	gint fd;
	const gchar *cache_dir;
	const gchar *uid;

	e_cal_component_get_attachment_list (comp, &attach_list);
	e_cal_component_get_uid (comp, &uid);
	/*FIXME  get the uri rather than computing the path */
	cache_dir = e_cal_backend_get_cache_dir (E_CAL_BACKEND (cbgw));

	for (l = attach_list; l; l = l->next) {
		gchar *sfname = (gchar *)l->data;
		gchar *filename, *new_filename;
		GMappedFile *mapped_file;
		GError *error = NULL;

		mapped_file = g_mapped_file_new (sfname, FALSE, &error);
		if (!mapped_file) {
			g_error_free (error);
			continue;
		}
		filename = g_path_get_basename (sfname);
		new_filename = g_strconcat (uid, "-", filename, NULL);
		g_free (filename);
		dest_file = g_build_filename (cache_dir, new_filename, NULL);
		g_free (new_filename);
		fd = g_open (dest_file, O_RDWR|O_CREAT|O_TRUNC|O_BINARY, 0600);
		if (fd == -1) {
			/* TODO handle error conditions */
		} else if (write (fd, g_mapped_file_get_contents (mapped_file),
				  g_mapped_file_get_length (mapped_file)) == -1) {
			/* TODO handle error condition */
		}

#if GLIB_CHECK_VERSION(2,21,3)
		g_mapped_file_unref (mapped_file);
#else
		g_mapped_file_free (mapped_file);
#endif
		if (fd != -1)
			close (fd);
		dest_url = g_filename_to_uri (dest_file, NULL, NULL);
		g_free (dest_file);
		new_attach_list = g_slist_append (new_attach_list, dest_url);
	}
	e_cal_component_set_attachment_list (comp, new_attach_list);

	for (l = new_attach_list; l != NULL; l = l->next)
		g_free (l->data);
	g_slist_free (new_attach_list);

}

static void
change_status (ECalComponent *comp, icalparameter_partstat status, const gchar *email)
{
	icalproperty *prop;
	icalparameter *param;
	gboolean found = FALSE;
	icalcomponent *icalcomp = e_cal_component_get_icalcomponent (comp);

	for (prop = icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY);
			prop;
			prop = icalcomponent_get_next_property (icalcomp, ICAL_ATTENDEE_PROPERTY)) {
		const gchar *attendee = icalproperty_get_attendee (prop);

		if (!g_ascii_strncasecmp (attendee, "mailto:", 7))
			attendee += 7;

		if (!g_ascii_strcasecmp (attendee, email)) {
			found = TRUE;
			param = icalparameter_new_partstat (status);
			icalproperty_set_parameter (prop, param);
			break;
		}
	}

	/* We couldn find the attendee in the component, so add a new attendee */
	if (!found) {
		gchar *temp = g_strdup_printf ("MAILTO:%s", email);

		prop = icalproperty_new_attendee ((const gchar *) temp);
		icalcomponent_add_property (icalcomp, prop);

		param = icalparameter_new_partstat (ICAL_PARTSTAT_DELEGATED);
		icalproperty_add_parameter (prop, param);

		param = icalparameter_new_role (ICAL_ROLE_NONPARTICIPANT);
		icalproperty_add_parameter (prop, param);

		param = icalparameter_new_cutype (ICAL_CUTYPE_INDIVIDUAL);
		icalproperty_add_parameter (prop, param);

		param = icalparameter_new_rsvp (ICAL_RSVP_TRUE);
		icalproperty_add_parameter (prop, param);

		g_free (temp);
	}
}

static void
receive_object (ECalBackendGroupwise *cbgw, EDataCal *cal, icalcomponent *icalcomp, GError **perror)
{
	ECalComponent *comp, *modif_comp = NULL;
	ECalBackendGroupwisePrivate *priv;
	icalproperty_method method;
	EGwConnectionStatus status;
	gboolean all_instances = FALSE;
	icalparameter_partstat pstatus;
	icalproperty *icalprop;

	priv = cbgw->priv;

	/* When the icalcomponent is obtained through the itip message rather
	 * than from the SOAP protocol, the container id has to be explicitly
	 * added to the xgwrecordid inorder to obtain the item id. */
	icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
	while (icalprop) {
		const gchar *x_name;

		x_name = icalproperty_get_x_name (icalprop);
		if (!strcmp (x_name, "X-GW-RECUR-INSTANCES-MOD-TYPE")) {
			if (!strcmp (icalproperty_get_x (icalprop), "All")) {
				all_instances = TRUE;
				icalcomponent_remove_property (icalcomp, icalprop);
				break;
			}
		}

		icalprop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY);
	}

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp));
	method = icalcomponent_get_method (icalcomp);

	/* handle attachments */
	if (e_cal_component_has_attachments (comp))
		fetch_attachments (cbgw, comp);

	status = e_gw_connection_send_appointment (cbgw, priv->container_id, comp, method, all_instances, &modif_comp, &pstatus);

	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		status = e_gw_connection_send_appointment (cbgw, priv->container_id, comp, method, all_instances, &modif_comp, &pstatus);

	if (!modif_comp)
		modif_comp = g_object_ref (comp);

	/* update the cache */
	if (status == E_GW_CONNECTION_STATUS_OK || status == E_GW_CONNECTION_STATUS_ITEM_ALREADY_ACCEPTED) {
		GSList *comps = NULL, *l;
		gboolean found = FALSE;

		if (all_instances) {
			const gchar *uid;

			e_cal_component_get_uid (modif_comp, (const gchar **) &uid);
			comps = e_cal_backend_store_get_components_by_uid (priv->store, uid);

			if (!comps)
				comps = g_slist_append (comps, g_object_ref (modif_comp));
			else
				found = TRUE;
		} else {
			ECalComponentId *id = e_cal_component_get_id (modif_comp);
			ECalComponent *component = NULL;

			component = e_cal_backend_store_get_component (priv->store, id->uid, id->rid);

			if (!component)
				comps = g_slist_append (comps, g_object_ref (modif_comp));
			else {
				comps = g_slist_append (comps, component);
				found = TRUE;
			}

			e_cal_component_free_id (id);
		}

		for (l = comps; l != NULL; l = l->next) {
			ECalComponent *component = E_CAL_COMPONENT (l->data);

			if (pstatus == ICAL_PARTSTAT_DECLINED) {
				ECalComponentId *id = e_cal_component_get_id (component);

				if (e_cal_backend_store_remove_component (priv->store, id->uid, id->rid)) {
					gchar *comp_str = e_cal_component_get_as_string (component);
					e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbgw), id, comp_str, NULL);
					g_free (comp_str);
				}

				e_cal_component_free_id (id);
			} else {
				gchar *comp_str = NULL;
				ECalComponentTransparency transp;

				change_status (component, pstatus, e_gw_connection_get_user_email (priv->cnc));
				e_cal_component_get_transparency (comp, &transp);
				e_cal_component_set_transparency (component, transp);
				put_component_to_store (cbgw, comp);
				comp_str = e_cal_component_get_as_string (component);

				if (found)
					e_cal_backend_notify_object_modified (E_CAL_BACKEND (cbgw), comp_str, comp_str);
				else
					e_cal_backend_notify_object_created (E_CAL_BACKEND (cbgw), comp_str);

				g_free (comp_str);
			}
		}

		g_slist_foreach (comps, (GFunc) g_object_unref, NULL);
		g_slist_free (comps);
		g_object_unref (comp);
		g_object_unref (modif_comp);
	}

	g_object_unref (comp);
	if (status == E_GW_CONNECTION_STATUS_OK)
		return;
	else if (status == E_GW_CONNECTION_STATUS_INVALID_OBJECT)
		g_propagate_error (perror, EDC_ERROR (InvalidObject));
	else if (status == E_GW_CONNECTION_STATUS_OVER_QUOTA)
		g_propagate_error (perror, EDC_ERROR (PermissionDenied));
	else
		g_propagate_error (perror, EDC_ERROR_FAILED_STATUS (OtherError, status));
}

/* Update_objects handler for the file backend. */
static void
e_cal_backend_groupwise_receive_objects (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj, GError **perror)
{
	ECalBackendGroupwise *cbgw;
        ECalBackendGroupwisePrivate *priv;
	icalcomponent *icalcomp, *subcomp;
	icalcomponent_kind kind;
	GError *err = NULL;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	if (priv->mode == CAL_MODE_LOCAL) {
		in_offline (cbgw);
		g_propagate_error (perror, EDC_ERROR (RepositoryOffline));
		return;
	}

	icalcomp = icalparser_parse_string (calobj);
	if (!icalcomp) {
		g_propagate_error (perror, EDC_ERROR (InvalidObject));
		return;
	}

	kind = icalcomponent_isa (icalcomp);
	if (kind == ICAL_VCALENDAR_COMPONENT) {
		subcomp = icalcomponent_get_first_component (icalcomp,
							     e_cal_backend_get_kind (E_CAL_BACKEND (backend)));
		while (subcomp) {
			icalcomponent_set_method (subcomp, icalcomponent_get_method (icalcomp));
			receive_object (cbgw, cal, subcomp, &err);
			if (err)
				break;
			subcomp = icalcomponent_get_next_component (icalcomp,
								    e_cal_backend_get_kind (E_CAL_BACKEND (backend)));
		}
	} else if (kind == e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
		receive_object (cbgw, cal, icalcomp, &err);
	} else
		err = EDC_ERROR (InvalidObject);

	icalcomponent_free (icalcomp);

	if (err)
		g_propagate_error (perror, err);
}

static void
send_object (ECalBackendGroupwise *cbgw, EDataCal *cal, icalcomponent *icalcomp, icalproperty_method method, GError **perror)
{
	ECalComponent *comp, *found_comp = NULL;
	ECalBackendGroupwisePrivate *priv;
	const gchar *uid = NULL;
	gchar *rid = NULL;

	priv = cbgw->priv;

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp));
	rid = e_cal_component_get_recurid_as_string (comp);

	e_cal_component_get_uid (comp, (const gchar **) &uid);
	found_comp = e_cal_backend_store_get_component (priv->store, uid, rid);
	g_free (rid);
	rid = NULL;

	if (!found_comp) {
		g_object_unref (comp);
		g_propagate_error (perror, EDC_ERROR (ObjectNotFound));
		return;
	}

	switch (priv->mode) {
	case CAL_MODE_ANY :
	case CAL_MODE_REMOTE :
		if (method == ICAL_METHOD_CANCEL) {
			const gchar *retract_comment = NULL;
			gboolean all_instances = FALSE;
			const gchar *id = NULL;
			EGwConnectionStatus status;

			get_retract_data (comp, &retract_comment, &all_instances);
			id = get_gw_item_id (icalcomp);
			status = e_gw_connection_retract_request (priv->cnc, id, retract_comment,
					all_instances, FALSE);

			if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
				status = e_gw_connection_retract_request (priv->cnc, id, retract_comment,
						all_instances, FALSE);

			if (status != E_GW_CONNECTION_STATUS_OK)
				g_propagate_error (perror, EDC_ERROR_FAILED_STATUS (OtherError, status));
		}
		break;
	case CAL_MODE_LOCAL :
		/* in offline mode, we just update the cache */
		g_propagate_error (perror, EDC_ERROR (RepositoryOffline));
		break;
	default:
		g_propagate_error (perror, EDC_ERROR (OtherError));
		break;
	}

	g_object_unref (comp);
	g_object_unref (found_comp);
}

static void
e_cal_backend_groupwise_send_objects (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj, GList **users,
				      gchar **modified_calobj, GError **perror)
{
	icalcomponent *icalcomp, *subcomp;
	icalcomponent_kind kind;
	icalproperty_method method;
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
	GError *err = NULL;

	*users = NULL;
	*modified_calobj = NULL;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	if (priv->mode == CAL_MODE_LOCAL) {
		in_offline (cbgw);
		g_propagate_error (perror, EDC_ERROR (RepositoryOffline));
		return;
	}

	icalcomp = icalparser_parse_string (calobj);
	if (!icalcomp) {
		g_propagate_error (perror, EDC_ERROR (InvalidObject));
		return;
	}

	method = icalcomponent_get_method (icalcomp);
	kind = icalcomponent_isa (icalcomp);
	if (kind == ICAL_VCALENDAR_COMPONENT) {
		subcomp = icalcomponent_get_first_component (icalcomp,
							     e_cal_backend_get_kind (E_CAL_BACKEND (backend)));
		while (subcomp) {

			send_object (cbgw, cal, subcomp, method, &err);
			if (err)
				break;
			subcomp = icalcomponent_get_next_component (icalcomp,
								    e_cal_backend_get_kind (E_CAL_BACKEND (backend)));
		}
	} else if (kind == e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
		send_object (cbgw, cal, icalcomp, method, &err);
	} else {
		err = EDC_ERROR (InvalidObject);
	}

	if (!err) {
		ECalComponent *comp;

		comp = e_cal_component_new ();

		if (e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp))) {
			GSList *attendee_list = NULL, *tmp;
			e_cal_component_get_attendee_list (comp, &attendee_list);
			/* convert this into GList */
			for (tmp = attendee_list; tmp; tmp = g_slist_next (tmp)) {
				const gchar *attendee = tmp->data;

				if (attendee)
					*users = g_list_append (*users, g_strdup (attendee));
			}

			g_object_unref (comp);
		}
		*modified_calobj = g_strdup (calobj);
	}
	icalcomponent_free (icalcomp);

	if (err)
		g_propagate_error (perror, err);
}

/* Object initialization function for the file backend */
static void
e_cal_backend_groupwise_init (ECalBackendGroupwise *cbgw)
{
	ECalBackendGroupwisePrivate *priv;

	priv = g_new0 (ECalBackendGroupwisePrivate, 1);

	priv->cnc = NULL;
	priv->sendoptions_sync_timeout = 0;
	priv->first_delta_fetch = TRUE;

	/* create the mutex for thread safety */
	g_static_rec_mutex_init (&priv->rec_mutex);

	cbgw->priv = priv;

	e_cal_backend_sync_set_lock (E_CAL_BACKEND_SYNC (cbgw), TRUE);
}

/* Class initialization function for the gw backend */
static void
e_cal_backend_groupwise_class_init (ECalBackendGroupwiseClass *class)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;
	ECalBackendSyncClass *sync_class;

	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;
	sync_class = (ECalBackendSyncClass *) class;

	parent_class = g_type_class_peek_parent (class);

	object_class->dispose = e_cal_backend_groupwise_dispose;
	object_class->finalize = e_cal_backend_groupwise_finalize;

	sync_class->is_read_only_sync = e_cal_backend_groupwise_is_read_only;
	sync_class->get_cal_address_sync = e_cal_backend_groupwise_get_cal_address;
	sync_class->get_alarm_email_address_sync = e_cal_backend_groupwise_get_alarm_email_address;
	sync_class->get_ldap_attribute_sync = e_cal_backend_groupwise_get_ldap_attribute;
	sync_class->get_static_capabilities_sync = e_cal_backend_groupwise_get_static_capabilities;
	sync_class->open_sync = e_cal_backend_groupwise_open;
	sync_class->remove_sync = e_cal_backend_groupwise_remove;
	sync_class->create_object_sync = e_cal_backend_groupwise_create_object;
	sync_class->modify_object_sync = e_cal_backend_groupwise_modify_object;
	sync_class->remove_object_sync = e_cal_backend_groupwise_remove_object;
	sync_class->discard_alarm_sync = e_cal_backend_groupwise_discard_alarm;
	sync_class->receive_objects_sync = e_cal_backend_groupwise_receive_objects;
	sync_class->send_objects_sync = e_cal_backend_groupwise_send_objects;
	sync_class->get_default_object_sync = e_cal_backend_groupwise_get_default_object;
	sync_class->get_object_sync = e_cal_backend_groupwise_get_object;
	sync_class->get_object_list_sync = e_cal_backend_groupwise_get_object_list;
	sync_class->get_attachment_list_sync = e_cal_backend_groupwise_get_attachment_list;
	sync_class->add_timezone_sync = e_cal_backend_groupwise_add_timezone;
	sync_class->set_default_zone_sync = e_cal_backend_groupwise_set_default_zone;
	sync_class->get_freebusy_sync = e_cal_backend_groupwise_get_free_busy;
	sync_class->get_changes_sync = e_cal_backend_groupwise_get_changes;

	backend_class->is_loaded = e_cal_backend_groupwise_is_loaded;
	backend_class->start_query = e_cal_backend_groupwise_start_query;
	backend_class->get_mode = e_cal_backend_groupwise_get_mode;
	backend_class->set_mode = e_cal_backend_groupwise_set_mode;
	backend_class->internal_get_default_timezone = e_cal_backend_groupwise_internal_get_default_timezone;
	backend_class->internal_get_timezone = e_cal_backend_groupwise_internal_get_timezone;
}

void
e_cal_backend_groupwise_notify_error_code (ECalBackendGroupwise *cbgw, EGwConnectionStatus status)
{
	const gchar *msg;

	g_return_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw));

	msg = e_gw_connection_get_error_message (status);
	if (msg)
		e_cal_backend_notify_error (E_CAL_BACKEND (cbgw), msg);
}

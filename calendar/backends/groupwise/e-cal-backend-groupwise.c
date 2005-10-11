/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Authors : 
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright 2003, Novell, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
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
#include <glib/gi18n-lib.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libedataserver/e-xml-hash-utils.h>
#include <libedataserver/e-url.h>
#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libecal/e-cal-component.h>
#include <libecal/e-cal-time-util.h>
#include "e-cal-backend-groupwise.h"
#include "e-cal-backend-groupwise-utils.h"
#include "e-gw-connection.h"

/* Private part of the CalBackendGroupwise structure */
struct _ECalBackendGroupwisePrivate {
	/* A mutex to control access to the private structure */
	GMutex *mutex;
	EGwConnection *cnc;
	ECalBackendCache *cache;
	gboolean read_only;
	char *uri;
	char *username;
	char *password;
	char *container_id;
	int timeout_id;
	CalMode mode;
	gboolean mode_changed;
	icaltimezone *default_zone;
	GHashTable *categories_by_id;
	GHashTable *categories_by_name;

	/* number of calendar items in the folder */
	guint32 total_count;
	
	/* fields for storing info while offline */
	char *user_email;
	char *local_attachments_store;
};

static void e_cal_backend_groupwise_dispose (GObject *object);
static void e_cal_backend_groupwise_finalize (GObject *object);
static void sanitize_component (ECalBackendSync *backend, ECalComponent *comp, char *server_uid);

#define PARENT_TYPE E_TYPE_CAL_BACKEND_SYNC
static ECalBackendClass *parent_class = NULL;

/* Time interval in milliseconds for obtaining changes from server and refresh the cache. */
#define CACHE_REFRESH_INTERVAL 600000
#define CURSOR_ITEM_LIMIT 100
#define CURSOR_ICALID_LIMIT 500

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

static GMutex *mutex = NULL;

/* Initialy populate the cache from the server */
static EGwConnectionStatus
populate_cache (ECalBackendGroupwise *cbgw)
{
	ECalBackendGroupwisePrivate *priv;
	EGwConnectionStatus status;
        ECalComponent *comp;
        GList *list = NULL, *l;
	gboolean done = FALSE;
	int cursor = 0;
	guint32	total, num = 0;
	int percent = 0;
	const char *position = E_GW_CURSOR_POSITION_END; 
	icalcomponent_kind kind;
	const char *type;

	priv = cbgw->priv;
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));
	total = priv->total_count;
	
	if (!mutex) {
		mutex = g_mutex_new ();
	}

	g_mutex_lock (mutex);

	if (kind == ICAL_VEVENT_COMPONENT)
		type = "Calendar";
	else
		type = "Task";
	
	status = e_gw_connection_create_cursor (priv->cnc,
			priv->container_id, 
			"recipients message recipientStatus attachments default peek", NULL, &cursor);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		e_cal_backend_groupwise_notify_error_code (cbgw, status);
		g_mutex_unlock (mutex);
                return status;
        }
	
	while (!done) {
		
		status = e_gw_connection_read_cursor (priv->cnc, priv->container_id, cursor, FALSE, CURSOR_ITEM_LIMIT, position, &list);
		if (status != E_GW_CONNECTION_STATUS_OK) {
			e_cal_backend_groupwise_notify_error_code (cbgw, status);
			g_mutex_unlock (mutex);
			return status;
		}
		for (l = list; l != NULL; l = g_list_next(l)) {
			EGwItem *item;
			char *progress_string = NULL;
			
			item = E_GW_ITEM (l->data);
			comp = e_gw_item_to_cal_component (item, cbgw);
			g_object_unref (item);
			
			/* Show the progress information */
			num++;
			percent = ((float) num/total) * 100;
		
			/* FIXME The total obtained from the server is wrong. Sometimes the num can 
			be greater than the total. The following makes sure that the percentage is not >= 100 */
 
			if (percent > 100)
				percent = 99; 

			progress_string = g_strdup_printf (_("Loading %s items"), type);
			e_cal_backend_notify_view_progress (E_CAL_BACKEND (cbgw), progress_string, percent);
			
			if (E_IS_CAL_COMPONENT (comp)) {
				char *comp_str;
				
				e_cal_component_commit_sequence (comp);
				if (kind == icalcomponent_isa (e_cal_component_get_icalcomponent (comp))) {
					comp_str = e_cal_component_get_as_string (comp);	
					e_cal_backend_notify_object_created (E_CAL_BACKEND (cbgw), (const char *) comp_str);
					g_free (comp_str);
				}
				e_cal_backend_cache_put_component (priv->cache, comp);
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
	e_cal_backend_notify_view_done (E_CAL_BACKEND (cbgw), GNOME_Evolution_Calendar_Success);

	g_mutex_unlock (mutex);

	return E_GW_CONNECTION_STATUS_OK;
}
static gboolean
compare_prefix (gconstpointer a, gconstpointer b)
{
	return !(g_str_has_prefix ((const char *)a, (const char *)b));
}

static gboolean
get_deltas (gpointer handle)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
	EGwConnection *cnc; 
	ECalBackendCache *cache; 
	EGwConnectionStatus status; 
	icalcomponent_kind kind;
	GList *item_list, *total_list = NULL, *l;
	GSList *cache_keys = NULL;
	GPtrArray *uid_array = g_ptr_array_new ();
	char *time_string = NULL;
	char t_str [100]; 
	const char *serv_time;
	static GStaticMutex connecting = G_STATIC_MUTEX_INIT;
	const char *time_interval_string;
	const char *key = "attempts";
	const char *attempts;
	const char *position ;
	

	EGwFilter *filter;
	int time_interval;
	icaltimetype temp;
	gboolean done = FALSE;
	int cursor = 0;
	struct tm *tm;
	time_t current_time;
	gboolean needs_to_get = FALSE;

	if (!handle)
		return FALSE;
	cbgw = (ECalBackendGroupwise *) handle;
	priv= cbgw->priv;
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));
	cnc = priv->cnc; 
	cache = priv->cache; 
	item_list = NULL;

	if (priv->mode == CAL_MODE_LOCAL)
		return FALSE;

	attempts = e_cal_backend_cache_get_key_value (cache, key);

	g_static_mutex_lock (&connecting);

	serv_time = e_cal_backend_cache_get_server_utc_time (cache);
	if (serv_time) {
		g_strlcpy (t_str, e_cal_backend_cache_get_server_utc_time (cache), 100);
		if (!*t_str || !strcmp (t_str, "")) {
			/* FIXME: When time-stamp is crashed, getting changes from current time */
			g_warning ("\n\a Could not get the correct time stamp. \n\a");
			temp = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
			current_time = icaltime_as_timet_with_zone (temp, icaltimezone_get_utc_timezone ());
			tm = gmtime (&current_time);
			strftime (t_str, 100, "%Y-%m-%dT%H:%M:%SZ", tm);
		}
	} else {
		/* FIXME: When time-stamp is crashed, getting changes from current time */
		g_warning ("\n\a Could not get the correct time stamp. \n\a");
		temp = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
		current_time = icaltime_as_timet_with_zone (temp, icaltimezone_get_utc_timezone ());
		tm = gmtime (&current_time);
		strftime (t_str, 100, "%Y-%m-%dT%H:%M:%SZ", tm);
	}
	time_string = g_strdup (t_str);

	filter = e_gw_filter_new ();
	/* Items modified after the time-stamp */
	e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_GREATERTHAN, "modified", time_string);

	status = e_gw_connection_get_items (cnc, cbgw->priv->container_id, "attachments recipients message recipientStatus default peek", filter, &item_list);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		status = e_gw_connection_get_items (cnc, cbgw->priv->container_id, "attachments recipients message recipientStatus default peek", filter, &item_list);
	g_object_unref (filter);

	if (status != E_GW_CONNECTION_STATUS_OK) {

		const char *msg = NULL;

		if (!attempts) {
			e_cal_backend_cache_put_key_value (cache, key, "2");
		} else {
			int failures;
			failures = g_ascii_strtod(attempts, NULL) + 1;
			e_cal_backend_cache_put_key_value (cache, key, GINT_TO_POINTER (failures));
		}

		if (status == E_GW_CONNECTION_STATUS_NO_RESPONSE) { 
			g_static_mutex_unlock (&connecting);
			return TRUE;
		}

		msg = e_gw_connection_get_error_message (status);

		g_static_mutex_unlock (&connecting);
		return TRUE;
	}

	e_file_cache_freeze_changes (E_FILE_CACHE (cache));

	for (; item_list != NULL; item_list = g_list_next(item_list)) {
		EGwItem *item = NULL;
		item = E_GW_ITEM(item_list->data);
		ECalComponent *modified_comp = NULL, *cache_comp = NULL;
		char *cache_comp_str = NULL;
		const char *uid, *rid = NULL;
		int r_key;

		modified_comp = e_gw_item_to_cal_component (item, cbgw);
		if (!modified_comp) {
			g_message ("Invalid component returned in update");
			continue;
		}
		if ((r_key = e_gw_item_get_recurrence_key (item)) != 0)
			rid = e_cal_component_get_recurid_as_string (modified_comp);
		
		e_cal_component_get_uid (modified_comp, &uid);		
		cache_comp = e_cal_backend_cache_get_component (cache, uid, rid);
		e_cal_component_commit_sequence (modified_comp);
		e_cal_component_commit_sequence (cache_comp);

		if (kind == icalcomponent_isa (e_cal_component_get_icalcomponent (modified_comp))) {
			cache_comp_str = e_cal_component_get_as_string (cache_comp);
			e_cal_backend_notify_object_modified (E_CAL_BACKEND (cbgw), cache_comp_str, e_cal_component_get_as_string (modified_comp));
			g_free (cache_comp_str);
			cache_comp_str = NULL;
		}
		e_cal_backend_cache_put_component (cache, modified_comp);

		g_object_unref (item);
		g_object_unref (modified_comp);
	}
	e_file_cache_thaw_changes (E_FILE_CACHE (cache));

	temp = icaltime_from_string (time_string);
	current_time = icaltime_as_timet_with_zone (temp, icaltimezone_get_utc_timezone ());
	tm = gmtime (&current_time);

	time_interval = (CACHE_REFRESH_INTERVAL / 60000);
	time_interval_string = g_getenv ("GETQM_TIME_INTERVAL");
	if (time_interval_string) {
		time_interval = g_ascii_strtod (time_interval_string, NULL);
	} 
	if (attempts) {
		tm->tm_min += (time_interval * g_ascii_strtod (attempts, NULL));
		e_cal_backend_cache_put_key_value (cache, key, NULL);
	} else {
		tm->tm_min += time_interval;
	}
	strftime (t_str, 100, "%Y-%m-%dT%H:%M:%SZ", tm);
	time_string = g_strdup (t_str);

	e_cal_backend_cache_put_server_utc_time (cache, time_string);

	g_free (time_string);
	time_string = NULL;

	if (item_list) {
		g_list_free (item_list);
		item_list = NULL;
	}
	
	/* TODO currently the read cursors response does not give us the recurrencKey, uncomment
	   this once the  response gives the recurrenceKey */
	/* handle deleted items here by going over the entire cache and
	 * checking for deleted items.*/
	position = E_GW_CURSOR_POSITION_END;
	cursor = 0;
	status = e_gw_connection_create_cursor (cnc, cbgw->priv->container_id, "id iCalId recurrenceKey", NULL, &cursor);

	if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_NO_RESPONSE) {
			g_static_mutex_unlock (&connecting);
			return TRUE;
		}

		e_cal_backend_groupwise_notify_error_code (cbgw, status);
		g_static_mutex_unlock (&connecting);
		return TRUE;
	}

	cache_keys = e_cal_backend_cache_get_keys (cache);
	
	done = FALSE;
	while (!done) {
		status = e_gw_connection_read_cal_ids (cnc, cbgw->priv->container_id, cursor, FALSE, CURSOR_ICALID_LIMIT, position, &item_list);
		if (status != E_GW_CONNECTION_STATUS_OK) {
			if (status == E_GW_CONNECTION_STATUS_NO_RESPONSE) {
				g_static_mutex_unlock (&connecting);
				return TRUE;
			}
			e_cal_backend_groupwise_notify_error_code (cbgw, status);
			g_static_mutex_unlock (&connecting);
			return TRUE;
		}
		
		/* FIXME handle deleted items here by going over the entire cache and
		 * checking for deleted items.*/
#if 0
		for (l1 = item_list; l1; l1 = g_list_next (l1)) {
			char *icalid;
			icalid = (char *)(l1->data);
			cache_keys = g_slist_delete_link (cache_keys, 
					g_slist_find_custom (cache_keys, icalid, (GCompareFunc) strcmp));
			if (l1->data)
				g_free (l1->data);
		}	
#endif
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
	e_gw_connection_destroy_cursor (cnc, cbgw->priv->container_id, cursor);
	e_file_cache_freeze_changes (E_FILE_CACHE (cache));

#if 0
	for (l = cache_keys; l ; l = g_slist_next (l)) {
		/* assumes rid is null - which works for now */
		ECalComponent *comp = NULL;
		GSList *comp_list = NULL;
		ECalComponentVType vtype;

		comp = e_cal_backend_cache_get_component (cache, (const char *) l->data, NULL);	

		if (!comp)
			continue;

		vtype = e_cal_component_get_vtype (comp);
		if ((vtype == E_CAL_COMPONENT_EVENT) ||
				(vtype == E_CAL_COMPONENT_TODO)) {
			comp_str = e_cal_component_get_as_string (comp);
			e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbgw), 
					(char *) l->data, comp_str, NULL);
			e_cal_backend_cache_remove_component (cache, (const char *) l->data, NULL);
			g_free (comp_str);
		}
		g_object_unref (comp);
	}
#endif

	for (l = total_list; l != NULL; l = l->next) {
		EGwItemCalId *calid = (EGwItemCalId *)	l->data;
		GCompareFunc func = NULL;
		GSList *remove = NULL;

		if (calid->ical_id) 
			func = (GCompareFunc) strcmp;
		else
			func = (GCompareFunc) compare_prefix;

		if (!(remove = g_slist_find_custom (cache_keys, calid->ical_id ? calid->ical_id :
						calid->recur_key,  func))) {
			g_ptr_array_add (uid_array, g_strdup (calid->item_id));
			needs_to_get = TRUE;
		} else  {
			cache_keys = g_slist_delete_link (cache_keys, remove);
			continue;
		}

	}

	if (needs_to_get) {
		e_gw_connection_get_items_from_ids (priv->cnc,
				priv->container_id, 
				"attachments recipients message recipientStatus default peek",
				uid_array, &item_list);

		for (l = item_list; l != NULL; l = l->next) {
			ECalComponent *comp = NULL;
			EGwItem *item = NULL;
			char *temp = NULL;

			item = (EGwItem *) l->data;
			comp = e_gw_item_to_cal_component (item, cbgw); 
			if (comp) {
				e_cal_component_commit_sequence (comp);
				sanitize_component (E_CAL_BACKEND_SYNC (cbgw), comp, (char *) e_gw_item_get_id (item));
				e_cal_backend_cache_put_component (priv->cache, comp);


				if (kind == icalcomponent_isa (e_cal_component_get_icalcomponent (comp))) {
					temp = e_cal_component_get_as_string (comp);
					e_cal_backend_notify_object_created (E_CAL_BACKEND (cbgw), temp);
					g_free (temp);
				}
				g_object_unref (comp);
			}

			g_object_unref (item);
		}
	}

	e_file_cache_thaw_changes (E_FILE_CACHE (cache));

	g_ptr_array_free (uid_array, TRUE);
	
	if (item_list) {
		g_list_free (item_list);
		item_list = NULL;
	}

	if (total_list) {
		g_list_foreach (total_list, (GFunc) e_gw_item_free_cal_id, NULL);
		g_list_free (total_list);
	}
	
	if (cache_keys) {
		g_slist_free (cache_keys);
	}
	
	g_static_mutex_unlock (&connecting);

	return TRUE;        
}

static gboolean
get_deltas_timeout (gpointer cbgw)
{
	GThread *thread;
	GError *error = NULL;

	if (!cbgw)
		return FALSE;

	thread = g_thread_create ((GThreadFunc) get_deltas, cbgw, FALSE, &error);
	if (!thread) {
		g_warning (G_STRLOC ": %s", error->message);
		g_error_free (error);
	}

	return TRUE;
}


static char* 
form_uri (ESource *source)
{
	char *uri;
	const char *port;
	char *formed_uri;
	const char *use_ssl;
	
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
				
static ECalBackendSyncStatus
cache_init (ECalBackendGroupwise *cbgw)
{
	ECalBackendGroupwisePrivate *priv = cbgw->priv;
	EGwConnectionStatus cnc_status;
	icalcomponent_kind kind;
	EGwSendOptions *opts;
	const char *time_interval_string;
	int time_interval;

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));
	
	time_interval = CACHE_REFRESH_INTERVAL;
	time_interval_string = g_getenv ("GETQM_TIME_INTERVAL");
	if (time_interval_string) {
		time_interval = g_ascii_strtod (time_interval_string, NULL);
		time_interval *= (60*1000); 
		
	}
	cnc_status = e_gw_connection_get_settings (priv->cnc, &opts);	
	if (cnc_status == E_GW_CONNECTION_STATUS_OK) {
		e_cal_backend_groupwise_store_settings (opts, cbgw);
		g_object_unref (opts);
	} else 
		g_warning (G_STRLOC ": Could not get the settings from the server");
	
	/* get the list of category ids and corresponding names from the server */
	cnc_status = e_gw_connection_get_categories (priv->cnc, &priv->categories_by_id, &priv->categories_by_name);
	if (cnc_status != E_GW_CONNECTION_STATUS_OK) {
		g_warning (G_STRLOC ": Could not get the categories from the server");
        }

	/* We poke the cache for a default timezone. Its
	 * absence indicates that the cache file has not been
	 * populated before. */
	if (!e_cal_backend_cache_get_marker (priv->cache)) {
		/* Populate the cache for the first time.*/
		/* start a timed polling thread set to 1 minute*/
		cnc_status = populate_cache (cbgw);
		if (cnc_status != E_GW_CONNECTION_STATUS_OK) {
			g_warning (G_STRLOC ": Could not populate the cache");
			/*FIXME  why dont we do a notify here */
			return GNOME_Evolution_Calendar_PermissionDenied;
		} else {
			char *utc_str;
			
			utc_str = (char *) e_gw_connection_get_server_time (priv->cnc);
			e_cal_backend_cache_set_marker (priv->cache);
			e_cal_backend_cache_put_server_utc_time (priv->cache, utc_str);

			/*  Set up deltas only if it is a Calendar backend */
			if (kind == ICAL_VEVENT_COMPONENT)
				priv->timeout_id = g_timeout_add (time_interval, (GSourceFunc) get_deltas_timeout, (gpointer) cbgw);
			priv->mode = CAL_MODE_REMOTE;
			return GNOME_Evolution_Calendar_Success;
		}

	} else {
		GList *cache_items = NULL, *l;
		/* notify the ecal about the objects already in cache */
		cache_items = e_cal_backend_cache_get_components (priv->cache);
		
		for (l = cache_items; l; l = g_list_next (l)) {
			ECalComponent *comp = E_CAL_COMPONENT (l->data);
			char *cal_string;

			if (kind == icalcomponent_isa (e_cal_component_get_icalcomponent (comp))) {
				cal_string = e_cal_component_get_as_string (comp);
				e_cal_backend_notify_object_created (E_CAL_BACKEND (cbgw), cal_string);
				g_free (cal_string);
			}
			g_object_unref (comp);
		}		
		if (cache_items)
			g_list_free (cache_items);
		
		/* get the deltas from the cache */
		if (get_deltas (cbgw)) {
			if (kind == ICAL_VEVENT_COMPONENT)
				priv->timeout_id = g_timeout_add (time_interval, (GSourceFunc) get_deltas_timeout, (gpointer) cbgw);
			priv->mode = CAL_MODE_REMOTE;
			return GNOME_Evolution_Calendar_Success;
		} else {
			g_warning (G_STRLOC ": Could not populate the cache");
			/*FIXME  why dont we do a notify here */
			return GNOME_Evolution_Calendar_PermissionDenied;	
		}
	}
	
}

static ECalBackendSyncStatus
set_container_id_with_count (ECalBackendGroupwise *cbgw) 
{
	ECalBackendGroupwisePrivate *priv;
	GList *container_list = NULL, *l;
	EGwConnectionStatus status;
	icalcomponent_kind kind;
	ECalBackendSyncStatus res;

	priv = cbgw->priv;

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));
	
	switch (kind) {
	case ICAL_VEVENT_COMPONENT:
	case ICAL_VTODO_COMPONENT:
		e_source_set_name (e_cal_backend_get_source (E_CAL_BACKEND (cbgw)), _("Calendar"));
		break;
	default:
		priv->container_id = NULL;
		return GNOME_Evolution_Calendar_UnsupportedMethod;
	}
	
	status = e_gw_connection_get_container_list (priv->cnc, "folders", &container_list);
	
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		status = e_gw_connection_get_container_list (priv->cnc, "folders", &container_list);
	
	if (status != E_GW_CONNECTION_STATUS_OK)
		return GNOME_Evolution_Calendar_OtherError;

	res =  GNOME_Evolution_Calendar_ObjectNotFound;
	for (l = container_list; l != NULL; l = l->next) {
		EGwContainer *container = E_GW_CONTAINER (l->data);
		const char *name = e_gw_container_get_name (container);
		
		if (name && strcmp (name, "Calendar") == 0) {
			priv->container_id = g_strdup (e_gw_container_get_id (container));
			priv->total_count = e_gw_container_get_total_count (container);
			res = GNOME_Evolution_Calendar_Success;
			break;
		}
	}

	e_gw_connection_free_container_list (container_list);

	return res;
}
	
static ECalBackendSyncStatus
connect_to_server (ECalBackendGroupwise *cbgw)
{
	char *real_uri;
	ECalBackendGroupwisePrivate *priv;
	ESource *source;
	const char *use_ssl;
	char *http_uri;
	int permissions;
	GThread *thread;
	GError *error = NULL;
	char *parent_user = NULL;
	icalcomponent_kind kind;
	
	priv = cbgw->priv;

	source = e_cal_backend_get_source (E_CAL_BACKEND (cbgw));
	real_uri = NULL;
	if (source)
		real_uri = form_uri (source);
	use_ssl = e_source_get_property (source, "use_ssl");

	if (!real_uri) {
		e_cal_backend_notify_error (E_CAL_BACKEND (cbgw), _("Invalid server URI"));
		return GNOME_Evolution_Calendar_NoSuchCal;
	} else {
		parent_user = (char *) e_source_get_property (source, "parent_id_name");
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
				e_cal_backend_notify_error (E_CAL_BACKEND (cbgw), _("Authentication failed"));
				return GNOME_Evolution_Calendar_AuthenticationFailed;
			}
				
			priv->cnc = e_gw_connection_get_proxy_connection (cnc, parent_user, priv->password, priv->username, &permissions);

			g_object_unref(cnc);
		
			if (!priv->cnc) {
				e_cal_backend_notify_error (E_CAL_BACKEND (cbgw), _("Authentication failed"));
				return GNOME_Evolution_Calendar_AuthenticationFailed;
			}

			kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));

			cbgw->priv->read_only = TRUE;
		
			if (kind == ICAL_VEVENT_COMPONENT && (permissions & E_GW_PROXY_APPOINTMENT_WRITE) )
				cbgw->priv->read_only = FALSE;
			else if (kind == ICAL_VTODO_COMPONENT && (permissions & E_GW_PROXY_TASK_WRITE))
				cbgw->priv->read_only = FALSE;

		} else {

			priv->cnc = e_gw_connection_new (
					real_uri,
					priv->username,
					priv->password);

			if (!E_IS_GW_CONNECTION(priv->cnc) && use_ssl && g_str_equal (use_ssl, "when-possible")) {
				http_uri = g_strconcat ("http://", real_uri + 8, NULL);
				priv->cnc = e_gw_connection_new (http_uri, priv->username, priv->password);
				g_free (http_uri);
			}
			cbgw->priv->read_only = FALSE;
		}
		g_free (real_uri);
			
		if (priv->cnc && priv->cache && priv->container_id) {
			char *utc_str;
			priv->mode = CAL_MODE_REMOTE;
			if (priv->mode_changed && !priv->timeout_id && (e_cal_backend_get_kind (E_CAL_BACKEND (cbgw)) == ICAL_VEVENT_COMPONENT)) {
				GThread *thread1;
				priv->mode_changed = FALSE;

				thread1 = g_thread_create ((GThreadFunc) get_deltas, cbgw, FALSE, &error);
				if (!thread1) {
					g_warning (G_STRLOC ": %s", error->message);
					g_error_free (error);

					e_cal_backend_notify_error (E_CAL_BACKEND (cbgw), _("Could not create thread for getting deltas"));
					return GNOME_Evolution_Calendar_OtherError;
				}
				priv->timeout_id = g_timeout_add (CACHE_REFRESH_INTERVAL, (GSourceFunc) get_deltas_timeout, (gpointer)cbgw);
			}
			utc_str = (char *) e_gw_connection_get_server_time (priv->cnc);
			e_cal_backend_cache_put_server_utc_time (priv->cache, utc_str);

			return GNOME_Evolution_Calendar_Success;
		}
		priv->mode_changed = FALSE;

		if (E_IS_GW_CONNECTION (priv->cnc)) {
			ECalBackendSyncStatus status;
			/* get the ID for the container */
			if (priv->container_id)
				g_free (priv->container_id);
			
			if ((status = set_container_id_with_count (cbgw)) != GNOME_Evolution_Calendar_Success) {
				return status;
			}
		
			priv->cache = e_cal_backend_cache_new (e_cal_backend_get_uri (E_CAL_BACKEND (cbgw)));
			if (!priv->cache) {
				g_mutex_unlock (priv->mutex);
				e_cal_backend_notify_error (E_CAL_BACKEND (cbgw), _("Could not create cache file"));
				return GNOME_Evolution_Calendar_OtherError;
			}

			/* spawn a new thread for opening the calendar */
			thread = g_thread_create ((GThreadFunc) cache_init, cbgw, FALSE, &error);
			if (!thread) {
				g_warning (G_STRLOC ": %s", error->message);
				g_error_free (error);

				e_cal_backend_notify_error (E_CAL_BACKEND (cbgw), _("Could not create thread for populating cache"));
				return GNOME_Evolution_Calendar_OtherError;
			}


		} else {
			e_cal_backend_notify_error (E_CAL_BACKEND (cbgw), _("Authentication failed"));
			return GNOME_Evolution_Calendar_AuthenticationFailed;
		}
	}

	if (!e_gw_connection_get_version (priv->cnc)) 
		return GNOME_Evolution_Calendar_InvalidServerVersion;

	return GNOME_Evolution_Calendar_Success;
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
	if (priv->mutex) {
		g_mutex_free (priv->mutex);
		priv->mutex = NULL;
	}

	if (priv->cnc) {
		g_object_unref (priv->cnc);
		priv->cnc = NULL;
	}

	if (priv->cache) {
		g_object_unref (priv->cache);
		priv->cache = NULL;
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

	if (priv->local_attachments_store) {
		g_free (priv->local_attachments_store);
		priv->local_attachments_store = NULL;
	}

	if (priv->timeout_id) {
		g_source_remove (priv->timeout_id);
		priv->timeout_id = 0;
	}

	g_free (priv);
	cbgw->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Calendar backend methods */

/* Is_read_only handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_is_read_only (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{
	ECalBackendGroupwise *cbgw;
	
	cbgw = E_CAL_BACKEND_GROUPWISE(backend);
	*read_only = cbgw->priv->read_only;
	
	return GNOME_Evolution_Calendar_Success;
}

/* return email address of the person who opened the calender */
static ECalBackendSyncStatus
e_cal_backend_groupwise_get_cal_address (ECalBackendSync *backend, EDataCal *cal, char **address)
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
	
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_get_ldap_attribute (ECalBackendSync *backend, EDataCal *cal, char **attribute)
{
	/* ldap attribute is specific to Sun ONE connector to get free busy information*/
	/* retun NULL here as group wise backend know how to get free busy information */
	
	*attribute = NULL;
	
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_get_alarm_email_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
	/*group wise does not support email based alarms */
	
	*address = NULL;
	
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_get_static_capabilities (ECalBackendSync *backend, EDataCal *cal, char **capabilities)
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

	return GNOME_Evolution_Calendar_Success;
}


static void
in_offline (ECalBackendGroupwise *cbgw) {
	ECalBackendGroupwisePrivate *priv;

	priv= cbgw->priv;
	priv->read_only = TRUE;

	if (priv->timeout_id) { 
		g_source_remove (priv->timeout_id);
		priv->timeout_id =0;
	}	
	
	if (priv->cnc) {
		g_object_unref (priv->cnc);
		priv->cnc = NULL;
	}


}

/* Open handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_open (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists,
			      const char *username, const char *password)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
	ECalBackendSyncStatus status;
	char *mangled_uri;
	int i;
	
	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	g_mutex_lock (priv->mutex);

	cbgw->priv->read_only = FALSE;

	if (priv->mode == CAL_MODE_LOCAL) {
		ESource *source;
		const char *display_contents = NULL;
			
		cbgw->priv->read_only = TRUE;				
		source = e_cal_backend_get_source (E_CAL_BACKEND (cbgw));
		display_contents = e_source_get_property (source, "offline_sync");
		
		if (!display_contents || !g_str_equal (display_contents, "1")) {
			g_mutex_unlock (priv->mutex);	
			return GNOME_Evolution_Calendar_RepositoryOffline;
		}

		if (!priv->cache) {
			priv->cache = e_cal_backend_cache_new (e_cal_backend_get_uri (E_CAL_BACKEND (cbgw)));
			if (!priv->cache) {
				g_mutex_unlock (priv->mutex);
				e_cal_backend_notify_error (E_CAL_BACKEND (cbgw), _("Could not create cache file"));
				
				return GNOME_Evolution_Calendar_OtherError;
			}
		}
		
		g_mutex_unlock (priv->mutex);	
		return GNOME_Evolution_Calendar_Success;
	}

	priv->username = g_strdup (username);
	priv->password = g_strdup (password);

	/* Set the local attachment store*/
	mangled_uri = g_strdup (e_cal_backend_get_uri (E_CAL_BACKEND (cbgw)));
	/* mangle the URI to not contain invalid characters */
	for (i = 0; i < strlen (mangled_uri); i++) {
		switch (mangled_uri[i]) {
		case ':' :
		case '/' :
			mangled_uri[i] = '_';
		}
	}

	priv->local_attachments_store = 
		g_strconcat ("file://", g_get_home_dir (), "/", ".evolution/cache/calendar",
			     "/", mangled_uri, NULL);
	g_free (mangled_uri);

	/* FIXME: no need to set it online here when we implement the online/offline stuff correctly */
	status = connect_to_server (cbgw);

	g_mutex_unlock (priv->mutex);
	return status;
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_remove (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
	
	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	g_mutex_lock (priv->mutex);

	/* remove the cache */
	if (priv->cache)
		e_file_cache_remove (E_FILE_CACHE (priv->cache));

	g_mutex_unlock (priv->mutex);

	return GNOME_Evolution_Calendar_Success;
}

/* is_loaded handler for the file backend */
static gboolean
e_cal_backend_groupwise_is_loaded (ECalBackend *backend)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	return priv->cache ? TRUE : FALSE;
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
		e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_SET,
					   cal_mode_to_corba (mode));
		return;
	}

	g_mutex_lock (priv->mutex);
	
	priv->mode_changed = TRUE;
	switch (mode) {
	case CAL_MODE_REMOTE :/* go online */
		priv->mode = CAL_MODE_REMOTE;
		priv->read_only = FALSE;
		e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_SET,
						    GNOME_Evolution_Calendar_MODE_REMOTE);
		e_cal_backend_notify_readonly (backend, priv->read_only);
		if(e_cal_backend_groupwise_is_loaded (backend))
		              e_cal_backend_notify_auth_required(backend);	
		break;

	case CAL_MODE_LOCAL : /* go offline */
		/* FIXME: make sure we update the cache before closing the connection */
		priv->mode = CAL_MODE_LOCAL;
		in_offline (cbgw);
		e_cal_backend_notify_readonly (backend, priv->read_only);
		e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_SET,
					   GNOME_Evolution_Calendar_MODE_LOCAL);

		break;
	default :
		e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED,
					   cal_mode_to_corba (mode));
	}

	g_mutex_unlock (priv->mutex);
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_get_default_object (ECalBackendSync *backend, EDataCal *cal, char **object)
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
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	*object = e_cal_component_get_as_string (comp);
	g_object_unref (comp);

	return GNOME_Evolution_Calendar_Success;
}

/* Get_object_component handler for the groupwise backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_get_object (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, char **object)
{
	ECalComponent *comp;
	ECalBackendGroupwisePrivate *priv;
	ECalBackendGroupwise *cbgw = (ECalBackendGroupwise *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), GNOME_Evolution_Calendar_OtherError);

	priv = cbgw->priv;

	g_mutex_lock (priv->mutex);

	/* search the object in the cache */
	comp = e_cal_backend_cache_get_component (priv->cache, uid, rid);
	if (comp) {
		g_mutex_unlock (priv->mutex);
		if (e_cal_backend_get_kind (E_CAL_BACKEND (backend)) ==
		    icalcomponent_isa (e_cal_component_get_icalcomponent (comp)))
			*object = e_cal_component_get_as_string (comp);
		else
			*object = NULL;

		g_object_unref (comp);

		return *object ? GNOME_Evolution_Calendar_Success : GNOME_Evolution_Calendar_ObjectNotFound;
	}

	g_mutex_unlock (priv->mutex);

	/* callers will never have a uuid that is in server but not in cache */
	return GNOME_Evolution_Calendar_ObjectNotFound;
}

/* Get_timezone_object handler for the groupwise backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_get_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid, char **object)
{
	ECalBackendGroupwise *cbgw;
        ECalBackendGroupwisePrivate *priv;
        icaltimezone *zone;
        icalcomponent *icalcomp;

        cbgw = E_CAL_BACKEND_GROUPWISE (backend);
        priv = cbgw->priv;

        g_return_val_if_fail (tzid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

        if (!strcmp (tzid, "UTC")) {
                zone = icaltimezone_get_utc_timezone ();
        } else {
		zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
		if (!zone)
			return GNOME_Evolution_Calendar_ObjectNotFound;
        }

        icalcomp = icaltimezone_get_component (zone);
        if (!icalcomp)
                return GNOME_Evolution_Calendar_InvalidObject;
                                                      
        *object = g_strdup (icalcomponent_as_ical_string (icalcomp));

        return GNOME_Evolution_Calendar_Success;
}

/* Add_timezone handler for the groupwise backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_add_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
	icalcomponent *tz_comp;
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;

	cbgw = (ECalBackendGroupwise *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbgw->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	if (icalcomponent_isa (tz_comp) == ICAL_VTIMEZONE_COMPONENT) {
		icaltimezone *zone;

		zone = icaltimezone_new ();
		icaltimezone_set_component (zone, tz_comp);
		if (e_cal_backend_cache_put_timezone (priv->cache, zone) == FALSE) {
			icaltimezone_free (zone, 1);
			return GNOME_Evolution_Calendar_OtherError;
		}
		icaltimezone_free (zone, 1);
	}
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_set_default_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid)
{
	ECalBackendGroupwise *cbgw;
        ECalBackendGroupwisePrivate *priv;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
        priv = cbgw->priv;
	
	/* Set the default timezone to it. */
	priv->default_zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);

	/* FIXME  write it into the cache*/
	e_cal_backend_cache_put_default_timezone (priv->cache, priv->default_zone);

	return GNOME_Evolution_Calendar_Success;
}

/* Gets the list of attachments */
static ECalBackendSyncStatus
e_cal_backend_groupwise_get_attachment_list (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, GSList **list)
{
	/* TODO implement the function */
	return GNOME_Evolution_Calendar_Success;
}

/* Get_objects_in_range handler for the groupwise backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_get_object_list (ECalBackendSync *backend, EDataCal *cal, const char *sexp, GList **objects)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
        GList *components, *l;
	ECalBackendSExp *cbsexp;
	gboolean search_needed = TRUE;
        
	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	g_mutex_lock (priv->mutex);

	g_message (G_STRLOC ": Getting object list (%s)", sexp);

	if (!strcmp (sexp, "#t"))
		search_needed = FALSE;

	cbsexp = e_cal_backend_sexp_new (sexp);
	if (!cbsexp) {
		g_mutex_unlock (priv->mutex);
		return GNOME_Evolution_Calendar_InvalidQuery;
	}

	*objects = NULL;
	components = e_cal_backend_cache_get_components (priv->cache);
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
	g_list_foreach (components, (GFunc) g_object_unref, NULL);
	g_list_free (components);

	g_mutex_unlock (priv->mutex);

	return GNOME_Evolution_Calendar_Success;
}

/* get_query handler for the groupwise backend */
static void
e_cal_backend_groupwise_start_query (ECalBackend *backend, EDataCalView *query)
{
        ECalBackendSyncStatus status;
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
        GList *objects = NULL;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	g_message (G_STRLOC ": Starting query (%s)", e_data_cal_view_get_text (query));

        status = e_cal_backend_groupwise_get_object_list (E_CAL_BACKEND_SYNC (backend), NULL,
							  e_data_cal_view_get_text (query), &objects);
        if (status != GNOME_Evolution_Calendar_Success) {
		e_data_cal_view_notify_done (query, status);
                return;
	}

       	/* notify listeners of all objects */
	if (objects) {
		e_data_cal_view_notify_objects_added (query, (const GList *) objects);

		/* free memory */
		g_list_foreach (objects, (GFunc) g_free, NULL);
		g_list_free (objects);
	}

	e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_Success);
}

/* Get_free_busy handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_get_free_busy (ECalBackendSync *backend, EDataCal *cal, GList *users,
				       time_t start, time_t end, GList **freebusy)
{
       EGwConnectionStatus status;
       ECalBackendGroupwise *cbgw;
       EGwConnection *cnc;

       cbgw = E_CAL_BACKEND_GROUPWISE (backend);
       cnc = cbgw->priv->cnc;

       if (cbgw->priv->mode == CAL_MODE_LOCAL) {
	       in_offline (cbgw);
	       return GNOME_Evolution_Calendar_RepositoryOffline;
       }

       status = e_gw_connection_get_freebusy_info (cnc, users, start, end, freebusy, cbgw->priv->default_zone);

       if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
	       status = e_gw_connection_get_freebusy_info (cnc, users, start, end, freebusy, cbgw->priv->default_zone);

       if (status != E_GW_CONNECTION_STATUS_OK)
               return GNOME_Evolution_Calendar_OtherError;
       return GNOME_Evolution_Calendar_Success; 
}

typedef struct {
	ECalBackendGroupwise *backend;
	icalcomponent_kind kind;
	GList *deletes;
	EXmlHash *ehash;
} ECalBackendGroupwiseComputeChangesData;

static void
e_cal_backend_groupwise_compute_changes_foreach_key (const char *key, const char *value, gpointer data)
{
	ECalBackendGroupwiseComputeChangesData *be_data = data;
                
	if (!e_cal_backend_cache_get_component (be_data->backend->priv->cache, key, NULL)) {
		ECalComponent *comp;

		comp = e_cal_component_new ();
		if (be_data->kind == ICAL_VTODO_COMPONENT)
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
		else
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);

		e_cal_component_set_uid (comp, key);
		be_data->deletes = g_list_prepend (be_data->deletes, e_cal_component_get_as_string (comp));

		e_xmlhash_remove (be_data->ehash, key);
 	}
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_compute_changes (ECalBackendGroupwise *cbgw, const char *change_id,
					 GList **adds, GList **modifies, GList **deletes)
{
        ECalBackendSyncStatus status;
	ECalBackendCache *cache;
	char    *filename;
	EXmlHash *ehash;
	ECalBackendGroupwiseComputeChangesData be_data;
	GList *i, *list = NULL;
	gchar *unescaped_uri;

	cache = cbgw->priv->cache;

	/* FIXME Will this always work? */
	unescaped_uri = gnome_vfs_unescape_string (cbgw->priv->uri, "");
	filename = g_strdup_printf ("%s-%s.db", unescaped_uri, change_id);
	ehash = e_xmlhash_new (filename);
	g_free (filename);
	g_free (unescaped_uri);

        status = e_cal_backend_groupwise_get_object_list (E_CAL_BACKEND_SYNC (cbgw), NULL, NULL, &list);
        if (status != GNOME_Evolution_Calendar_Success)
                return status;
        
        /* Calculate adds and modifies */
	for (i = list; i != NULL; i = g_list_next (i)) {
		const char *uid;
		char *calobj;

		e_cal_component_get_uid (i->data, &uid);
		calobj = e_cal_component_get_as_string (i->data);

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
	}

	/* Calculate deletions */
	be_data.backend = cbgw;
	be_data.kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));
	be_data.deletes = NULL;
	be_data.ehash = ehash;
   	e_xmlhash_foreach_key (ehash, (EXmlHashFunc)e_cal_backend_groupwise_compute_changes_foreach_key, &be_data);

	*deletes = be_data.deletes;

	e_xmlhash_write (ehash);
  	e_xmlhash_destroy (ehash);
	
	return GNOME_Evolution_Calendar_Success;
}

/* Get_changes handler for the groupwise backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_get_changes (ECalBackendSync *backend, EDataCal *cal, const char *change_id,
				     GList **adds, GList **modifies, GList **deletes)
{
        ECalBackendGroupwise *cbgw;
	cbgw = E_CAL_BACKEND_GROUPWISE (backend);

	g_return_val_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (change_id != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	return e_cal_backend_groupwise_compute_changes (cbgw, change_id, adds, modifies, deletes);

}

/* Discard_alarm handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *auid)
{
	return GNOME_Evolution_Calendar_OtherError;
}

static icaltimezone *
e_cal_backend_groupwise_internal_get_default_timezone (ECalBackend *backend)
{
	/* Groupwise server maintains data in UTC  */
	return icaltimezone_get_utc_timezone ();
}

static icaltimezone *
e_cal_backend_groupwise_internal_get_timezone (ECalBackend *backend, const char *tzid)
{
	icaltimezone *zone;

	zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);

	if (!zone)
		return icaltimezone_get_utc_timezone();

	return zone;
}

static void
sanitize_component (ECalBackendSync *backend, ECalComponent *comp, char *server_uid)
{
	ECalBackendGroupwise *cbgw;
	icalproperty *icalprop;
	int i;
	GString *str = g_string_new ("");;	
	
	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	if (server_uid) {

		/* the ID returned by sendItemResponse includes the container ID of the
		   inbox folder, so we need to replace that with our container ID */
		for (i = 0; i < strlen (server_uid); i++) {
			str = g_string_append_c (str, server_uid[i]);
			if (server_uid[i] == ':') {
				str = g_string_append (str, cbgw->priv->container_id);
				break;
			}
		}
		
		/* add the extra property to the component */
		icalprop = icalproperty_new_x (str->str);
		icalproperty_set_x_name (icalprop, "X-GWRECORDID");
		icalcomponent_add_property (e_cal_component_get_icalcomponent (comp), icalprop);

		g_string_free (str, TRUE);
	}
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_create_object (ECalBackendSync *backend, EDataCal *cal, char **calobj, char **uid)
{
	ECalBackendGroupwise *cbgw;
        ECalBackendGroupwisePrivate *priv;
	icalcomponent *icalcomp;
	ECalComponent *comp;
	EGwConnectionStatus status;
	char *server_uid = NULL;
	GSList *uid_list = NULL, *l;
	int i;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (calobj != NULL && *calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

	if (priv->mode == CAL_MODE_LOCAL) {
		in_offline(cbgw);
		return GNOME_Evolution_Calendar_RepositoryOffline;
	}

	/* check the component for validity */
	icalcomp = icalparser_parse_string (*calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	if (e_cal_backend_get_kind (E_CAL_BACKEND (backend)) != icalcomponent_isa (icalcomp)) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_InvalidObject;
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

			if (status == E_GW_CONNECTION_STATUS_UNKNOWN_USER)
				return GNOME_Evolution_Calendar_UnknownUser;
			else
				return GNOME_Evolution_Calendar_OtherError;
		}
	
		/* If delay deliver has been set, server will not send the uid */
		if (!uid_list)
			return GNOME_Evolution_Calendar_Success;
		
		if (g_slist_length (uid_list) == 1) {
			server_uid = (char *) uid_list->data;
			sanitize_component (backend, comp, server_uid);	
			g_free (server_uid);
			/* if successful, update the cache */
			e_cal_backend_cache_put_component (priv->cache, comp);
			*calobj = e_cal_component_get_as_string (comp);
		} else {
			EGwConnectionStatus status;	
			GList *list = NULL, *tmp;
			GPtrArray *uid_array = g_ptr_array_new ();
			for (l = uid_list; l; l = g_slist_next (l)) {
				g_ptr_array_add (uid_array, l->data);
			}
			
			/* convert uid_list to GPtrArray and get the items in a list */
			status = e_gw_connection_get_items_from_ids (priv->cnc,
					priv->container_id, 
					"attachments recipients message recipientStatus default peek",
					uid_array, &list);

			if (status != E_GW_CONNECTION_STATUS_OK || (list == NULL) || (g_list_length (list) == 0)) {
				g_ptr_array_free (uid_array, TRUE);
				return GNOME_Evolution_Calendar_OtherError;
			}
			
			comp = g_object_ref ( (ECalComponent *) list->data );
			/* convert items into components and add them to the cache */
			for (i=0, tmp = list; tmp ; tmp = g_list_next (tmp), i++) {
				ECalComponent *e_cal_comp;
				EGwItem *item;

				item = (EGwItem *) tmp->data;
				e_cal_comp = e_gw_item_to_cal_component (item, cbgw); 
				e_cal_component_commit_sequence (e_cal_comp);
				sanitize_component (backend, e_cal_comp, g_ptr_array_index (uid_array, i));
				e_cal_backend_cache_put_component (priv->cache, e_cal_comp);
				
				if (i == 0) {
					*calobj = e_cal_component_get_as_string (e_cal_comp);	
				}

				if (i != 0) {
					char *temp;
					temp = e_cal_component_get_as_string (e_cal_comp);
					e_cal_backend_notify_object_created (E_CAL_BACKEND (cbgw), temp);
					g_free (temp);
				}

				g_object_unref (e_cal_comp);
			}
			g_ptr_array_free (uid_array, TRUE);
		}
		break;
	default :
		break;
	}

	g_object_unref (comp);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_modify_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, 
				       CalObjModType mod, char **old_object, char **new_object)
{
	ECalBackendGroupwise *cbgw;
        ECalBackendGroupwisePrivate *priv;
	icalcomponent *icalcomp;
	ECalComponent *comp, *cache_comp = NULL;
	EGwConnectionStatus status;
	EGwItem *item, *cache_item;
	const char *uid = NULL;

	*old_object = NULL;
	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

	if (priv->mode == CAL_MODE_LOCAL) {
		in_offline (cbgw);
		return GNOME_Evolution_Calendar_RepositoryOffline;
	}

	/* check the component for validity */
	icalcomp = icalparser_parse_string (calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);
	e_cal_component_get_uid (comp, &uid);

	/* check if the object exists */
	switch (priv->mode) {
	case CAL_MODE_ANY :
	case CAL_MODE_REMOTE :
		/* when online, send the item to the server */
		cache_comp = e_cal_backend_cache_get_component (priv->cache, uid, NULL);
		if (!cache_comp) {
			g_message ("CRITICAL : Could not find the object in cache");
			return GNOME_Evolution_Calendar_ObjectNotFound;
		}

		if (e_cal_component_has_attendees (comp) && 
				e_cal_backend_groupwise_utils_check_delegate (comp, e_gw_connection_get_user_email (priv->cnc))) {
			const char *id = NULL, *recur_key = NULL;
			
			item = e_gw_item_new_for_delegate_from_cal (cbgw, comp);

			if (mod == CALOBJ_MOD_ALL && e_cal_component_is_instance (comp)) {
				recur_key = uid;
			} else {
				id = e_gw_item_get_id (item);
			}

			status = e_gw_connection_delegate_request (priv->cnc, item, id, NULL, NULL, recur_key); 
		
			if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
					status = e_gw_connection_delegate_request (priv->cnc, item, id, NULL, NULL, recur_key);
				
				if (status != E_GW_CONNECTION_STATUS_OK) {
					g_object_unref (comp);
					g_object_unref (cache_comp);
					return GNOME_Evolution_Calendar_OtherError;
				}
			
			
			e_cal_backend_cache_put_component (priv->cache, comp);
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
					return GNOME_Evolution_Calendar_OtherError;
				}
				e_cal_backend_cache_put_component (priv->cache, comp);
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
			return GNOME_Evolution_Calendar_OtherError;
		}
		/* if successful, update the cache */

	case CAL_MODE_LOCAL :
		/* in offline mode, we just update the cache */
		e_cal_backend_cache_put_component (priv->cache, comp);
		break;
	default :
		break;
	}


	*old_object = e_cal_component_get_as_string (cache_comp);
	g_object_unref (cache_comp);
	g_object_unref (comp);
	return GNOME_Evolution_Calendar_Success;
}

static const char *
get_gw_item_id (icalcomponent *icalcomp) 
{
	icalproperty *icalprop;	

	/* search the component for the X-GWRECORDID property */
	icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
	while (icalprop) {
		const char *x_name, *x_val;

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
static ECalBackendSyncStatus
e_cal_backend_groupwise_remove_object (ECalBackendSync *backend, EDataCal *cal,
				       const char *uid, const char *rid,
				       CalObjModType mod, char **old_object,
				       char **object)
{
	ECalBackendGroupwise *cbgw;
        ECalBackendGroupwisePrivate *priv;
	char *calobj = NULL;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	*old_object = *object = NULL;

	/* if online, remove the item from the server */
	if (priv->mode == CAL_MODE_REMOTE) {
		ECalBackendSyncStatus status;
		const char *id_to_remove = NULL;
		icalcomponent *icalcomp;
	
		status = e_cal_backend_groupwise_get_object (backend, cal, uid, rid, &calobj);
		if (status != GNOME_Evolution_Calendar_Success)
			return status;
		g_message ("object found \n");

		icalcomp = icalparser_parse_string (calobj);
		if (!icalcomp) {
			g_free (calobj);
			return GNOME_Evolution_Calendar_InvalidObject;
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
				if (!e_cal_backend_cache_remove_component (priv->cache, uid, rid)) {
					g_free (calobj);
					return GNOME_Evolution_Calendar_ObjectNotFound;
				}
				*object = NULL;
				*old_object = strdup (calobj);
				g_free (calobj);
				return GNOME_Evolution_Calendar_Success;
			} else {
				g_free (calobj);
				return GNOME_Evolution_Calendar_OtherError;
			}
		} else if (mod == CALOBJ_MOD_ALL) {
			GSList *l, *comp_list = e_cal_backend_cache_get_components_by_uid (priv->cache, uid);

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
					item_ids = g_list_append (item_ids, (char *) id_to_remove);
				}
				status = e_gw_connection_remove_items (priv->cnc, priv->container_id, item_ids);
				
				if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
					status = e_gw_connection_remove_items (priv->cnc, priv->container_id, item_ids);
			}

			if (status == E_GW_CONNECTION_STATUS_OK) {

				for (l = comp_list; l; l = l->next) {
					ECalComponent *comp = E_CAL_COMPONENT (l->data);
					ECalComponentId *id = e_cal_component_get_id (comp);

					e_cal_backend_cache_remove_component (priv->cache, id->uid, 
							id->rid);
					if (!g_str_equal (id->rid, rid))
						e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbgw), id, e_cal_component_get_as_string (comp), NULL);
					e_cal_component_free_id (id);
					
					g_object_unref (comp);
				
				}
				/* Setting NULL would trigger another signal.
				 * We do not set the *object to NULL  */
				g_slist_free (comp_list);
				*old_object = strdup (calobj);
				*object = NULL;
				g_free (calobj);
				return GNOME_Evolution_Calendar_Success;
			} else {
				g_free (calobj);
				return GNOME_Evolution_Calendar_OtherError;
			}
		} else
			return GNOME_Evolution_Calendar_UnsupportedMethod;
	} else if (priv->mode == CAL_MODE_LOCAL) {
		in_offline (cbgw);
		return GNOME_Evolution_Calendar_RepositoryOffline;
	} else
		return GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED;
	
}

static void
fetch_attachments (ECalBackendGroupwise *cbgw, ECalComponent *comp)
{
	GSList *attach_list = NULL, *new_attach_list = NULL;
	GSList *l;
	char  *attach_store, *filename, *file_contents;
	char *dest_url, *dest_file;
	int fd, len;
	int len_read = 0;
	char buf[1024];
	struct stat sb;
	const char *uid;


	e_cal_component_get_attachment_list (comp, &attach_list);
	e_cal_component_get_uid (comp, &uid);
	/*FIXME  get the uri rather than computing the path */
	attach_store = g_strconcat
			(e_cal_backend_groupwise_get_local_attachments_store (cbgw), NULL);
	
	for (l = attach_list; l ; l = l->next) {
		char *sfname = (char *)l->data;

		filename = g_strrstr (sfname, "/") + 1;	

		// open the file using the data
		fd = open (sfname, O_RDONLY); 
		if (fd == -1) {
			/* TODO handle error conditions */
			g_message ("DEBUG: could not open the file descriptor\n");
			continue;
		}
		if (fstat (fd, &sb) == -1) {
			/* TODO handle error conditions */
			g_message ("DEBUG: could not fstat the attachment file\n");
			continue;
		}
		len = sb.st_size;

		file_contents = g_malloc (len + 1);
	
		while (len_read < len) {
			int c = read (fd, buf, sizeof (buf));

			if (c == -1)
				break;

			memcpy (&file_contents[len_read], buf, c);
			len_read += c;
		}
		file_contents [len_read] = 0;

		/* write*/
		dest_file = g_strconcat (attach_store, "/", uid, "-",
				filename, NULL);
		fd = open (dest_file, O_RDWR|O_CREAT|O_TRUNC, 0600);
		if (fd == -1) {
			/* TODO handle error conditions */
			g_message ("DEBUG: could not serialize attachments\n");
		}

		if (write (fd, file_contents, len_read) == -1) {
			/* TODO handle error condition */
			g_message ("DEBUG: attachment write failed.\n");
		}

		dest_url = g_strconcat ("file:///", dest_file, NULL);
		new_attach_list = g_slist_append (new_attach_list, dest_url);
		g_free (dest_file);
	}
	g_free (attach_store);
	e_cal_component_set_attachment_list (comp, new_attach_list);
}

static void 
change_status (ECalComponent *comp, icalparameter_partstat status, const char *email)
{	
	icalproperty *prop;	
	icalparameter *param;
	gboolean found = FALSE;
	icalcomponent *icalcomp = e_cal_component_get_icalcomponent (comp);


	for (prop = icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY);
			prop;
			prop = icalcomponent_get_next_property (icalcomp, ICAL_ATTENDEE_PROPERTY)) {
		const char *attendee = icalproperty_get_attendee (prop);

		if (!g_strncasecmp (attendee, "mailto:", 7))
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
		char *temp = g_strdup_printf ("MAILTO:%s", email);

		prop = icalproperty_new_attendee ((const char *) temp);
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

static ECalBackendSyncStatus
receive_object (ECalBackendGroupwise *cbgw, EDataCal *cal, icalcomponent *icalcomp)
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
		const char *x_name;

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
			const char *uid;
 			
			e_cal_component_get_uid (modif_comp, (const char **) &uid);
			comps = e_cal_backend_cache_get_components_by_uid (priv->cache, uid);

			if (!comps)
				comps = g_slist_append (comps, g_object_ref (modif_comp));
			else
				found = TRUE;
		} else {
			ECalComponentId *id = e_cal_component_get_id (modif_comp);
			ECalComponent *comp = NULL;

			comp = e_cal_backend_cache_get_component (priv->cache, id->uid, id->rid);

			if (!comp)
				comps = g_slist_append (comps, g_object_ref (modif_comp));
			else {
				comps = g_slist_append (comps, comp);
				found = TRUE;
 			}
 
			e_cal_component_free_id (id);
		}
 
		for (l = comps; l != NULL; l = l->next) {
			ECalComponent *comp = E_CAL_COMPONENT (l->data);

			if (pstatus == ICAL_PARTSTAT_DECLINED) {
				ECalComponentId *id = e_cal_component_get_id (comp);

				if (e_cal_backend_cache_remove_component (priv->cache, id->uid, id->rid)) {

					e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbgw), id, e_cal_component_get_as_string (comp), NULL);
				e_cal_component_free_id (id);

				}

			} else {
				char *comp_str = NULL;

				change_status (comp, pstatus, e_gw_connection_get_user_email (priv->cnc));
				e_cal_backend_cache_put_component (priv->cache, comp);	
				comp_str = e_cal_component_get_as_string (comp);
				
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
 		return GNOME_Evolution_Calendar_Success;

	}

	if (status == E_GW_CONNECTION_STATUS_INVALID_OBJECT) {
		g_object_unref (comp);
		return  GNOME_Evolution_Calendar_InvalidObject;
	}
	return GNOME_Evolution_Calendar_OtherError;
}

/* Update_objects handler for the file backend. */
static ECalBackendSyncStatus
e_cal_backend_groupwise_receive_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
	ECalBackendGroupwise *cbgw;
        ECalBackendGroupwisePrivate *priv;
	icalcomponent *icalcomp, *subcomp;
	icalcomponent_kind kind;
	ECalBackendSyncStatus status = GNOME_Evolution_Calendar_Success;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	if (priv->mode == CAL_MODE_LOCAL) {
		in_offline (cbgw);
		return GNOME_Evolution_Calendar_RepositoryOffline;
	}

	icalcomp = icalparser_parse_string (calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	kind = icalcomponent_isa (icalcomp);
	if (kind == ICAL_VCALENDAR_COMPONENT) {
		subcomp = icalcomponent_get_first_component (icalcomp,
							     e_cal_backend_get_kind (E_CAL_BACKEND (backend)));
		while (subcomp) {
			icalcomponent_set_method (subcomp, icalcomponent_get_method (icalcomp));
			status = receive_object (cbgw, cal, subcomp);
			if (status != GNOME_Evolution_Calendar_Success)
				break;
			subcomp = icalcomponent_get_next_component (icalcomp,
								    e_cal_backend_get_kind (E_CAL_BACKEND (backend)));
		}
	} else if (kind == e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
		status = receive_object (cbgw, cal, icalcomp);
	} else
		status = GNOME_Evolution_Calendar_InvalidObject;

	icalcomponent_free (icalcomp);

	return status;
}

static ECalBackendSyncStatus
send_object (ECalBackendGroupwise *cbgw, EDataCal *cal, icalcomponent *icalcomp, icalproperty_method method)
{
	ECalComponent *comp, *found_comp;
	ECalBackendGroupwisePrivate *priv;
	ECalBackendSyncStatus status = GNOME_Evolution_Calendar_Success;
	char *uid, *comp_str;

	priv = cbgw->priv;

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp));

	e_cal_component_get_uid (comp, (const char **) &uid);
	found_comp = e_cal_backend_cache_get_component (priv->cache, uid, NULL);

	switch (priv->mode) {
	case CAL_MODE_ANY :
	case CAL_MODE_REMOTE :
		if (found_comp) {
			char *comp_str;
			status = e_cal_backend_groupwise_modify_object (E_CAL_BACKEND_SYNC (cbgw), cal, comp_str,
									CALOBJ_MOD_THIS, &comp_str, NULL);
			g_free (comp_str);
		} else
			status = e_cal_backend_groupwise_create_object (E_CAL_BACKEND_SYNC (cbgw), cal, &comp_str, NULL);

		break;
	case CAL_MODE_LOCAL :
		/* in offline mode, we just update the cache */
		e_cal_backend_cache_put_component (priv->cache, comp);
		break;
	default:
		break;	
	}

	g_object_unref (comp);

	return status;
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_send_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj, GList **users,
				      char **modified_calobj)
{
	ECalBackendSyncStatus status = GNOME_Evolution_Calendar_OtherError;
	icalcomponent *icalcomp, *subcomp;
	icalcomponent_kind kind;
	icalproperty_method method;
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;

	*users = NULL;
	*modified_calobj = NULL;

	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	if (priv->mode == CAL_MODE_LOCAL) {
		in_offline (cbgw);
		return GNOME_Evolution_Calendar_RepositoryOffline;
	}

	icalcomp = icalparser_parse_string (calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	method = icalcomponent_get_method (icalcomp);
	kind = icalcomponent_isa (icalcomp);
	if (kind == ICAL_VCALENDAR_COMPONENT) {
		subcomp = icalcomponent_get_first_component (icalcomp,
							     e_cal_backend_get_kind (E_CAL_BACKEND (backend)));
		while (subcomp) {

			status = send_object (cbgw, cal, subcomp, method);
			if (status != GNOME_Evolution_Calendar_Success)
				break;
			subcomp = icalcomponent_get_next_component (icalcomp,
								    e_cal_backend_get_kind (E_CAL_BACKEND (backend)));
		}
	} else if (kind == e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
		status = send_object (cbgw, cal, icalcomp, method);
	} else
		status = GNOME_Evolution_Calendar_InvalidObject;
	
	if (status == GNOME_Evolution_Calendar_Success) {
		ECalComponent *comp;

		comp = e_cal_component_new ();
		
		if (e_cal_component_set_icalcomponent (comp, icalcomp)) {
			GSList *attendee_list = NULL, *tmp;
			e_cal_component_get_attendee_list (comp, &attendee_list);
			/* convert this into GList */
			for (tmp = attendee_list; tmp; tmp = g_slist_next (tmp))
				*users = g_list_append (*users, tmp);
			
			g_object_unref (comp);	
		}
		*modified_calobj = g_strdup (calobj);
	}
	icalcomponent_free (icalcomp);

	return status;
}


/* Object initialization function for the file backend */
static void
e_cal_backend_groupwise_init (ECalBackendGroupwise *cbgw, ECalBackendGroupwiseClass *class)
{
	ECalBackendGroupwisePrivate *priv;

	priv = g_new0 (ECalBackendGroupwisePrivate, 1);

	priv->timeout_id = 0;
	priv->cnc = NULL;

	/* create the mutex for thread safety */
	priv->mutex = g_mutex_new ();

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
	sync_class->get_timezone_sync = e_cal_backend_groupwise_get_timezone;
	sync_class->add_timezone_sync = e_cal_backend_groupwise_add_timezone;
	sync_class->set_default_timezone_sync = e_cal_backend_groupwise_set_default_timezone;
	sync_class->get_freebusy_sync = e_cal_backend_groupwise_get_free_busy;
	sync_class->get_changes_sync = e_cal_backend_groupwise_get_changes;

	backend_class->is_loaded = e_cal_backend_groupwise_is_loaded;
	backend_class->start_query = e_cal_backend_groupwise_start_query;
	backend_class->get_mode = e_cal_backend_groupwise_get_mode;
	backend_class->set_mode = e_cal_backend_groupwise_set_mode;
	backend_class->internal_get_default_timezone = e_cal_backend_groupwise_internal_get_default_timezone;
	backend_class->internal_get_timezone = e_cal_backend_groupwise_internal_get_timezone;
}


/**
 * e_cal_backend_groupwise_get_type:
 * @void: 
 * 
 * Registers the #ECalBackendGroupwise class if necessary, and returns the type ID
 * associated to it.
 * 
 * Return value: The type ID of the #ECalBackendGroupwise class.
 **/
GType
e_cal_backend_groupwise_get_type (void)
{
	static GType e_cal_backend_groupwise_type = 0;

	if (!e_cal_backend_groupwise_type) {
		static GTypeInfo info = {
                        sizeof (ECalBackendGroupwiseClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_cal_backend_groupwise_class_init,
                        NULL, NULL,
                        sizeof (ECalBackendGroupwise),
                        0,
                        (GInstanceInitFunc) e_cal_backend_groupwise_init
                };
		e_cal_backend_groupwise_type = g_type_register_static (E_TYPE_CAL_BACKEND_SYNC,
								  "ECalBackendGroupwise", &info, 0);
	}

	return e_cal_backend_groupwise_type;
}

void
e_cal_backend_groupwise_notify_error_code (ECalBackendGroupwise *cbgw, EGwConnectionStatus status)
{
	const char *msg;

	g_return_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw));

	msg = e_gw_connection_get_error_message (status);
	if (msg)
		e_cal_backend_notify_error (E_CAL_BACKEND (cbgw), msg);
}

const char *
e_cal_backend_groupwise_get_local_attachments_store (ECalBackendGroupwise *cbgw)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), NULL);
	return cbgw->priv->local_attachments_store;
}

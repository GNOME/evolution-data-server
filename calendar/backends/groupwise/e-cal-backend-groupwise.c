/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Authors : 
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright 2003, Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs.h>
#include <bonobo/bonobo-i18n.h>
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
	icaltimezone *default_zone;
	GHashTable *categories_by_id;
	GHashTable *categories_by_name;

	/* fields for storing info while offline */
	char *user_email;
};

static void e_cal_backend_groupwise_dispose (GObject *object);
static void e_cal_backend_groupwise_finalize (GObject *object);

#define PARENT_TYPE E_TYPE_CAL_BACKEND_SYNC
static ECalBackendClass *parent_class = NULL;

/* Time interval in milliseconds for obtaining changes from server and refresh the cache. */
#define CACHE_REFRESH_INTERVAL 600000
#define CURSOR_ITEM_LIMIT 100

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
	icalcomponent_kind kind;
	
	priv = cbgw->priv;
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));
	
	if (!mutex) {
		mutex = g_mutex_new ();
	}

	g_mutex_lock (mutex);

	/* get the list of category ids and corresponding names from the server */
	status = e_gw_connection_get_categories (priv->cnc, priv->categories_by_id, priv->categories_by_name);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		e_cal_backend_groupwise_notify_error_code (cbgw, status);
		g_mutex_unlock (mutex);
                return status;
        }
	status = e_gw_connection_create_cursor (priv->cnc, priv->container_id, "recipients message recipientStatus default", NULL, &cursor);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		e_cal_backend_groupwise_notify_error_code (cbgw, status);
		g_mutex_unlock (mutex);
                return status;
        }
	
	status = e_gw_connection_position_cursor (priv->cnc, priv->container_id, cursor, "end", 1);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		e_cal_backend_groupwise_notify_error_code (cbgw, status);
		g_mutex_unlock (mutex);
                return status;
        }
	while (!done) {
		
		status = e_gw_connection_read_cursor (priv->cnc, priv->container_id, cursor, FALSE, CURSOR_ITEM_LIMIT, &list);
		if (status != E_GW_CONNECTION_STATUS_OK) {
			e_cal_backend_groupwise_notify_error_code (cbgw, status);
			g_mutex_unlock (mutex);
			return status;
		}
		for (l = list; l != NULL; l = g_list_next(l)) {
			EGwItem *item;
			
			item = E_GW_ITEM (l->data);
			comp = e_gw_item_to_cal_component (item, cbgw);
			g_object_unref (item);
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
		}
		
		if (!list  || g_list_length (list) == 0)
			done = TRUE;
		g_list_free (list);
		list = NULL;
        }
	e_gw_connection_destroy_cursor (priv->cnc, priv->container_id, cursor);

	g_mutex_unlock (mutex);

	return E_GW_CONNECTION_STATUS_OK;
}

static gboolean
get_deltas (gpointer handle)
{
 	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
	EGwConnection *cnc; 
 	ECalBackendCache *cache; 
        EGwConnectionStatus status; 
	GSList *item_list, *cache_keys, *l;
	const char *cache_file_name;
	char *comp_str;
	static time_t mod_time = 0;
	GTimeVal time_val;
	char time_string[100];
	const struct tm *tm;
	struct stat buf;
        
	if (!handle)
		return FALSE;
	
	cbgw = (ECalBackendGroupwise *) handle;
	priv= cbgw->priv;
 	cnc = priv->cnc; 
 	cache = priv->cache; 
	item_list = NULL;
	
	if (!mod_time) {
		cache_file_name = e_file_cache_get_filename (E_FILE_CACHE (priv->cache));
		printf ("%s %d\n", cache_file_name, stat (cache_file_name, &buf));
		mod_time = buf.st_mtime;
	}

	tm = gmtime (&mod_time);
	strftime (time_string, 100, "%Y-%m-%dT%H:%M:%SZ", tm);
	
	status = e_gw_connection_get_quick_messages (cnc, cbgw->priv->container_id, "recipients message recipientStatus default", time_string, "New", "CalendarItem", NULL,  -1,  &item_list);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		e_cal_backend_groupwise_notify_error_code (cbgw, status);
		return TRUE;
	}

	e_file_cache_freeze_changes (E_FILE_CACHE (cache));
	for (; item_list != NULL; item_list = g_slist_next(item_list)) {
		EGwItem *item = E_GW_ITEM(item_list->data);
		ECalComponent *comp = e_gw_item_to_cal_component (item, cbgw);
		
		e_cal_component_commit_sequence (comp);

		if (comp) {
			if (!e_cal_backend_cache_put_component (cache, comp)) 
				g_message ("Could not add the component");
			else  {
				comp_str = e_cal_component_get_as_string (comp);	
				e_cal_backend_notify_object_created (E_CAL_BACKEND (cbgw), comp_str);
				g_free (comp_str);
			}
		}
		else 
			g_message ("Invalid component returned");

		g_object_unref (comp);
		g_object_unref (item);
	}
	if (item_list) {
		g_slist_free (item_list);
		item_list = NULL;
	}
	e_file_cache_thaw_changes (E_FILE_CACHE (cache));

	status = e_gw_connection_get_quick_messages (cnc, cbgw->priv->container_id,"recipients message recipientStatus  default", time_string, "Modified", "CalendarItem", NULL,  -1,  &item_list);
	
	if (status != E_GW_CONNECTION_STATUS_OK) {
		e_cal_backend_groupwise_notify_error_code (cbgw, status);
		return TRUE;
	}

	e_file_cache_freeze_changes (E_FILE_CACHE (cache));
	for (; item_list != NULL; item_list = g_slist_next(item_list)) {
		EGwItem *item = E_GW_ITEM(item_list->data);
		ECalComponent *modified_comp, *cache_comp;
		char *cache_comp_str;
		
		modified_comp = e_gw_item_to_cal_component (item, cbgw);
		if (!modified_comp) {
			g_message ("Invalid component returned in update");
			continue;
		}
		cache_comp = e_cal_backend_cache_get_component (cache, e_gw_item_get_icalid (item), NULL);
		e_cal_component_commit_sequence (modified_comp);
		e_cal_component_commit_sequence (cache_comp);

		cache_comp_str = e_cal_component_get_as_string (cache_comp);
		e_cal_backend_notify_object_modified (E_CAL_BACKEND (cbgw), cache_comp_str, e_cal_component_get_as_string (modified_comp));
		e_cal_backend_cache_remove_component (cache, e_gw_item_get_icalid (item), NULL);
		e_cal_backend_cache_put_component (cache, modified_comp);
		g_free (cache_comp_str);
		g_object_unref (item);
		g_object_unref (modified_comp);
	}
	e_file_cache_thaw_changes (E_FILE_CACHE (cache));

	if (item_list) {
		g_slist_free (item_list);
		item_list = NULL;
	}

	status = e_gw_connection_get_quick_messages (cnc, cbgw->priv->container_id,"iCalId", NULL, "All", "CalendarItem", NULL,  -1,  &item_list);

	if (status != E_GW_CONNECTION_STATUS_OK) {
		e_cal_backend_groupwise_notify_error_code (cbgw, status);
		return TRUE;
	}

	/* handle deleted items here by going over the entire cache and
	 * checking for deleted items.*/
	
	cache_keys = e_cal_backend_cache_get_keys (cache);
	for (l = item_list; l; l = g_slist_next (l)) {
		/* this works assuming rid is null*/
		cache_keys = g_slist_delete_link (cache_keys, 
				g_slist_find_custom (cache_keys, l->data, (GCompareFunc) strcmp));
		g_free (l->data);
	}

	e_file_cache_freeze_changes (E_FILE_CACHE (cache));
	for (l = cache_keys; l ; l = g_slist_next (l)) {
		/* assumes rid is null - which works for now */
		ECalComponent *comp = NULL;
		ECalComponentVType vtype;

		comp = e_cal_backend_cache_get_component (cache, (const char *) l->data, NULL);	
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
	e_file_cache_thaw_changes (E_FILE_CACHE (cache));

	if (item_list) {
		g_slist_free (item_list);
		item_list = NULL;
	}
	if (cache_keys) {
		g_slist_free (cache_keys);
		item_list = NULL;
	}
		
	g_get_current_time (&time_val);
	mod_time = time_val.tv_sec;

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
		port = "7181";
	use_ssl = e_source_get_property (source, "use_ssl");

	if (use_ssl)
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
	const char *time_interval_string;
	int time_interval;

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));
	
	time_interval = CACHE_REFRESH_INTERVAL;
	time_interval_string = g_getenv ("GETQM_TIME_INTERVAL");
	if (time_interval_string) {
		time_interval = g_ascii_strtod (time_interval_string, NULL);
		time_interval *= (60*1000); 
		
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
			e_cal_backend_cache_set_marker (priv->cache);
			priv->timeout_id = g_timeout_add (time_interval, (GSourceFunc) get_deltas, (gpointer) cbgw);
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
			priv->timeout_id = g_timeout_add (time_interval, (GSourceFunc) get_deltas, (gpointer) cbgw);
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
connect_to_server (ECalBackendGroupwise *cbgw)
{
	char *real_uri;
	ECalBackendGroupwisePrivate *priv;
	ESource *source;
	const char *use_ssl;
	char *http_uri;
	GThread *thread;
	GError *error = NULL;
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
		/* create connection to server */
		priv->cnc = e_gw_connection_new (
			real_uri,
			priv->username,
			priv->password);

		if (!E_IS_GW_CONNECTION(priv->cnc) && use_ssl && g_str_equal (use_ssl, "when-possible")) {
			http_uri = g_strconcat ("http://", real_uri + 8, NULL);
			priv->cnc = e_gw_connection_new (http_uri, priv->username, priv->password);
			g_free (http_uri);
		}
		g_free (real_uri);
			
		/* As of now we are assuming that logged in user has write rights to calender */
		/* we need to read actual rights from server when we implement proxy user access */
		cbgw->priv->read_only = FALSE;

		if (priv->cnc && priv->cache) {
			priv->mode = CAL_MODE_REMOTE;
			return GNOME_Evolution_Calendar_Success;
		}

	
		if (E_IS_GW_CONNECTION (priv->cnc)) {
			icalcomponent_kind kind;

			/* get the ID for the container */
			if (priv->container_id)
				g_free (priv->container_id);

			kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgw));
			if (kind == ICAL_VEVENT_COMPONENT) {
				priv->container_id = g_strdup (e_gw_connection_get_container_id (priv->cnc, "Calendar"));
				e_source_set_name (e_cal_backend_get_source (E_CAL_BACKEND (cbgw)), _("Calendar"));
			} else if (kind == ICAL_VTODO_COMPONENT) {
				priv->container_id = g_strdup (e_gw_connection_get_container_id (priv->cnc, "Calendar"));
				e_source_set_name (e_cal_backend_get_source (E_CAL_BACKEND (cbgw)), _("Calendar"));
			} else
				priv->container_id = NULL;

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

	if (priv->categories_by_id) {
	        g_hash_table_destroy (priv->categories_by_id);
		priv->categories_by_id = NULL;
	}

	if (priv->categories_by_name) {
	        g_hash_table_destroy (priv->categories_by_name);
	        priv->categories_by_name = NULL;
	}

	if (priv->container_id) {
		g_free (priv->container_id);
		priv->container_id = NULL;
	}

	if (priv->user_email) {
		g_free (priv->user_email);
		priv->user_email = NULL;
	}

	if (priv->timeout_id) 
		g_source_remove (priv->timeout_id);
	
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
	*capabilities = g_strdup (CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS "," \
				  CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY "," \
				  CAL_STATIC_CAPABILITY_REMOVE_ALARMS ","   \
	                          CAL_STATIC_CAPABILITY_NO_THISANDPRIOR "," \
				  CAL_STATIC_CAPABILITY_NO_THISANDFUTURE "," \
				  CAL_STATIC_CAPABILITY_NO_CONV_TO_ASSIGN_TASK "," \
				  CAL_STATIC_CAPABILITY_NO_CONV_TO_RECUR "," \
				  CAL_STATIC_CAPABILITY_REQ_SEND_OPTIONS "," \
				  CAL_STATIC_CAPABILITY_SAVE_SCHEDULES);

	return GNOME_Evolution_Calendar_Success;
}



/* Open handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_open (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists,
			      const char *username, const char *password)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
	ECalBackendSyncStatus status;
	
	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	g_mutex_lock (priv->mutex);

	cbgw->priv->read_only = FALSE;
	priv->mode = CAL_MODE_LOCAL;
	priv->username = g_strdup (username);
	priv->password = g_strdup (password);
	
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
	ECalBackendSyncStatus status;
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

	switch (mode) {
	case CAL_MODE_REMOTE :/* go online */
		status = connect_to_server (cbgw);
		if (status == GNOME_Evolution_Calendar_Success)
			e_cal_backend_notify_mode (backend,
						   GNOME_Evolution_Calendar_CalListener_MODE_SET,
						   GNOME_Evolution_Calendar_MODE_REMOTE);
		else
			e_cal_backend_notify_mode (backend,
						   GNOME_Evolution_Calendar_CalListener_MODE_NOT_SET,
						   GNOME_Evolution_Calendar_MODE_REMOTE);
		break;
	case CAL_MODE_LOCAL : /* go offline */
		/* FIXME: make sure we update the cache before closing the connection */
		g_object_unref (priv->cnc);
		priv->cnc = NULL;
		priv->mode = CAL_MODE_LOCAL;

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
e_cal_backend_groupwise_compute_changes_foreach_key (const char *key, gpointer data)
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
		if (status != E_GW_CONNECTION_STATUS_OK) {
			g_object_unref (comp);

			if (status == E_GW_CONNECTION_STATUS_UNKNOWN_USER)
				return GNOME_Evolution_Calendar_UnknownUser;
			else
				return GNOME_Evolution_Calendar_OtherError;
		}
	
		if (uid_list && (g_slist_length (uid_list) == 1)) {
			server_uid = (char *) uid_list->data;
			sanitize_component (backend, comp, server_uid);	
			g_free (server_uid);
			/* if successful, update the cache */
			e_cal_backend_cache_put_component (priv->cache, comp);
			*calobj = e_cal_component_get_as_string (comp);
		} else {
			
			GList *list = NULL, *tmp;
			GPtrArray *uid_array = g_ptr_array_new ();
			for (l = uid_list; l; l = g_slist_next (l)) {
				g_ptr_array_add (uid_array, l->data);
			}
			
			/* convert uid_list to GPtrArray and get the items in a list */
			e_gw_connection_get_items_from_ids (priv->cnc, priv->container_id, "recipients message",
					uid_array, &list);
			/* FIXME  check if list is null and status may have
			 * failed. */
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

	*old_object = NULL;
	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

	/* check the component for validity */
	icalcomp = icalparser_parse_string (calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);
	item = e_gw_item_new_from_cal_component (priv->container_id, cbgw, comp);

	/* check if the object exists */
	switch (priv->mode) {
	case CAL_MODE_ANY :
	case CAL_MODE_REMOTE :
		/* when online, send the item to the server */
		cache_comp = e_cal_backend_cache_get_component (priv->cache, e_gw_item_get_icalid (item), NULL);
		if (!cache_comp) {
			g_message ("CRITICAL : Could not find the object in cache");
			return GNOME_Evolution_Calendar_ObjectNotFound;
		}

		cache_item =  e_gw_item_new_from_cal_component (priv->container_id, cbgw, cache_comp);
		if ( e_gw_item_get_item_type (item) == E_GW_ITEM_TYPE_TASK) {
			gboolean completed, cache_completed;
			
			completed = e_gw_item_get_completed (item);
			cache_completed = e_gw_item_get_completed (cache_item);
			if (completed && !cache_completed) {
				/*FIXME  return values. */
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
		icalproperty *icalprop;
		icalcomponent *icalcomp;

		status = e_cal_backend_groupwise_get_object (backend, cal, uid, rid, &calobj);
		if (status != GNOME_Evolution_Calendar_Success)
			return status;

		*old_object = calobj;

		icalcomp = icalparser_parse_string (calobj);
		if (!icalcomp) {
			g_free (calobj);
			return GNOME_Evolution_Calendar_InvalidObject;
		}

		/* search the component for the X-GWRECORDID property */
		icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
		while (icalprop) {
			const char *x_name, *x_val;

			x_name = icalproperty_get_x_name (icalprop);
			x_val = icalproperty_get_x (icalprop);
			if (!strcmp (x_name, "X-GWRECORDID")) {
				id_to_remove = x_val;
				break;
			}

			icalprop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY);
		}

		if (!id_to_remove) {
			/* use the iCalId to remove the object */
			id_to_remove = uid;
		}

		/* remove the object */
		status = e_gw_connection_remove_item (priv->cnc, priv->container_id, id_to_remove);

		icalcomponent_free (icalcomp);
		if (status == E_GW_CONNECTION_STATUS_OK) {
			/* remove the component from the cache */
			if (!e_cal_backend_cache_remove_component (priv->cache, uid, rid)) {
				g_free (calobj);
				return GNOME_Evolution_Calendar_ObjectNotFound;
			}
			*object = g_strdup (calobj);
			g_free (calobj);
			return GNOME_Evolution_Calendar_Success;
		} else {
			g_free (calobj);
			return GNOME_Evolution_Calendar_OtherError;
		}
	}

	/* remove the component from the cache */
	if (!e_cal_backend_cache_remove_component (priv->cache, uid, rid)) {
		g_free (calobj);
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	*object = g_strdup (calobj);
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
receive_object (ECalBackendGroupwise *cbgw, EDataCal *cal, icalcomponent *icalcomp)
{
	ECalComponent *comp, *modif_comp = NULL;
	ECalBackendGroupwisePrivate *priv;
	icalproperty_method method;
	EGwConnectionStatus status;
	gboolean remove = FALSE;

	priv = cbgw->priv;

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp));
	method = icalcomponent_get_method (icalcomp);
	
	status = e_gw_connection_send_appointment (cbgw, priv->container_id, comp, method, &remove, &modif_comp);

	if (status == E_GW_CONNECTION_STATUS_OK && !modif_comp) {
		g_object_unref (comp);
		return GNOME_Evolution_Calendar_Success;
	}

	/* update the cache */
	if (status == E_GW_CONNECTION_STATUS_OK) {
		if (remove) {
			const char *uid;
			
			e_cal_component_get_uid (comp, (const char **) &uid);
			e_cal_backend_cache_remove_component (priv->cache, uid, NULL);
			e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbgw), uid, e_cal_component_get_as_string (comp), NULL);
			g_free (comp);
		}
		else {
			char *cache_comp = NULL, *temp, *new_comp = NULL;
			ECalComponent *cache_component;
			
			e_cal_component_commit_sequence (modif_comp);
			e_cal_component_get_uid (modif_comp, (const char **) &temp);	
			cache_component = e_cal_backend_cache_get_component (priv->cache, temp, NULL);
			
			if (cache_component) {
				e_cal_component_commit_sequence (cache_component);
				cache_comp = e_cal_component_get_as_string (cache_component);
			}

			e_cal_backend_cache_put_component (priv->cache, modif_comp);	
			e_cal_component_commit_sequence (modif_comp);
			new_comp = e_cal_component_get_as_string (modif_comp);

			if (cache_comp)
				e_cal_backend_notify_object_modified (E_CAL_BACKEND (cbgw), cache_comp, new_comp);
			else
				e_cal_backend_notify_object_created (E_CAL_BACKEND (cbgw), new_comp);
				
			g_free (cache_comp);
			g_free (new_comp);
			g_free (temp);
		}
		g_object_unref (comp);	
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

	priv->categories_by_id = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->categories_by_name = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->timeout_id = 0;

	/* create the mutex for thread safety */
	priv->mutex = g_mutex_new ();

	cbgw->priv = priv;
}

/* Class initialization function for the file backend */
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

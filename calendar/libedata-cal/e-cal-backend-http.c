/* Evolution calendar - iCalendar http backend
 *
 * Copyright (C) 2003 Novell, Inc.
 *
 * Authors: Hans Petter Jansson <hpj@ximian.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 * Based in part on the file backend.
 */

#include <config.h>
#include <string.h>
#include <unistd.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-moniker-util.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libedataserver/e-xml-hash-utils.h>
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-util.h>
#include "e-cal-backend-file-events.h"
#include "e-cal-backend-http.h"
#include "e-cal-backend-util.h"
#include "e-cal-backend-sexp.h"



/* Private part of the ECalBackendHttp structure */
struct _ECalBackendHttpPrivate {
	/* URI to get remote calendar data from */
	char *uri;

	/* Local/remote mode */
	CalMode mode;

	/* Cached-file backend */
	ECalBackendSync *file_backend;

	/* The calendar's default timezone, used for resolving DATE and
	   floating DATE-TIME values. */
	icaltimezone *default_zone;

	/* The list of live queries */
	GList *queries;

	/* GnomeVFS handle for remote retrieval */
	GnomeVFSAsyncHandle *retrieval_handle;
};



static void e_cal_backend_http_dispose (GObject *object);
static void e_cal_backend_http_finalize (GObject *object);

static ECalBackendSyncClass *parent_class;



/* Dispose handler for the file backend */
static void
e_cal_backend_http_dispose (GObject *object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (object);
	priv = cbhttp->priv;

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

/* Finalize handler for the file backend */
static void
e_cal_backend_http_finalize (GObject *object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_HTTP (object));

	cbhttp = E_CAL_BACKEND_HTTP (object);
	priv = cbhttp->priv;

	/* Clean up */

	if (priv->retrieval_handle) {
		gnome_vfs_async_cancel (priv->retrieval_handle);
		priv->retrieval_handle = NULL;
	}

	if (priv->file_backend) {
		g_object_unref (priv->file_backend);
		e_cal_backend_set_notification_proxy (E_CAL_BACKEND (priv->file_backend), NULL);
	}

	if (priv->uri) {
	        g_free (priv->uri);
		priv->uri = NULL;
	}

	g_free (priv);
	cbhttp->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}



/* Calendar backend methods */

/* Is_read_only handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_is_read_only (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{
	*read_only = TRUE;
	
	return GNOME_Evolution_Calendar_Success;
}

/* Get_email_address handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_cal_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
	/* A file backend has no particular email address associated
	 * with it (although that would be a useful feature some day).
	 */
	*address = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_http_get_ldap_attribute (ECalBackendSync *backend, EDataCal *cal, char **attribute)
{
	*attribute = NULL;
	
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_http_get_alarm_email_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
 	/* A file backend has no particular email address associated
 	 * with it (although that would be a useful feature some day).
 	 */
	*address = NULL;
	
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_http_get_static_capabilities (ECalBackendSync *backend, EDataCal *cal, char **capabilities)
{
	*capabilities = CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS;
	
	return GNOME_Evolution_Calendar_Success;
}

static gchar *
webcal_to_http_method (const gchar *webcal_str)
{
	if (strncmp ("webcal://", webcal_str, sizeof ("webcal://") - 1))
		return NULL;

	return g_strconcat ("http://", webcal_str + sizeof ("webcal://") - 1, NULL);
}

static gchar *
uri_to_cache_dir (const gchar *uri_str)
{
	gchar *http_uri_str, *dir_str, *escaped_dir_str;
	GnomeVFSURI *uri;

	http_uri_str = webcal_to_http_method (uri_str);
	if (!http_uri_str)
		http_uri_str = g_strdup (uri_str);

	uri = gnome_vfs_uri_new (http_uri_str);
	g_free (http_uri_str);

	if (!uri) {
		g_warning ("No GnomeVFSURI.");
		return NULL;
	}

	dir_str = gnome_vfs_uri_to_string (uri,
					   GNOME_VFS_URI_HIDE_PASSWORD |
					   GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD);
	gnome_vfs_uri_unref (uri);

	if (!dir_str || !strlen (dir_str)) {
		g_warning ("No dir_str.");
		return NULL;
	}

	/* GnomeVFS unescapes paths. We need to escape twice. */
	escaped_dir_str = gnome_vfs_escape_slashes (dir_str);
	g_free (dir_str);
	dir_str = gnome_vfs_escape_slashes (escaped_dir_str);
	g_free (escaped_dir_str);
	escaped_dir_str = dir_str;

	if (!escaped_dir_str || !strlen (escaped_dir_str)) {
		g_warning ("No escaped_dir_str.");
		return NULL;
	}

	return g_build_filename (g_get_home_dir (),
				 "/.evolution/calendar/webcal/",
				 escaped_dir_str, NULL);
}

static gboolean
ensure_cache_dir (const gchar *cache_dir_str)
{
	GnomeVFSResult result;
	gchar *webcal_dir;

	/* Make sure we have the webcal base dir */
	webcal_dir = g_build_filename (g_get_home_dir (),
				       "/.evolution/calendar/webcal", NULL);
	gnome_vfs_make_directory (webcal_dir, GNOME_VFS_PERM_USER_ALL);
	g_free (webcal_dir);

	/* Create cache subdirectory */
	result = gnome_vfs_make_directory (cache_dir_str, GNOME_VFS_PERM_USER_ALL);
	if (result != GNOME_VFS_OK && result != GNOME_VFS_ERROR_FILE_EXISTS)
		return FALSE;

	return TRUE;
}

static void
retrieval_done (ECalBackendHttp *cbhttp)
{
	ECalBackendHttpPrivate *priv;
	icalcomponent *icalcomp;
	gchar *cache_dir;
	gchar *temp_file, *cal_file;
	gchar *temp_file_unescaped;

	priv = cbhttp->priv;

	priv->retrieval_handle = NULL;

	cache_dir = uri_to_cache_dir (e_cal_backend_get_uri (E_CAL_BACKEND (cbhttp)));
	if (!cache_dir)
		return;

	temp_file = g_build_filename (cache_dir, "/calendar.ics.tmp", NULL);
	cal_file = g_build_filename (cache_dir, "/calendar.ics", NULL);

	temp_file_unescaped = gnome_vfs_unescape_string (temp_file, "");
	icalcomp = e_cal_util_parse_ics_file (temp_file_unescaped);
	g_free (temp_file_unescaped);

	if (!icalcomp) {
		e_cal_backend_notify_error (E_CAL_BACKEND (cbhttp), _("Bad file format."));
		goto out;
	}

	if (icalcomponent_isa (icalcomp) != ICAL_VCALENDAR_COMPONENT) {
		e_cal_backend_notify_error (E_CAL_BACKEND (cbhttp), _("Not a calendar."));
		icalcomponent_free (icalcomp);
		goto out;
	}

	/* Update calendar file and tell file backend to reload */

	gnome_vfs_move (temp_file, cal_file, TRUE);
	e_cal_backend_file_reload (E_CAL_BACKEND_FILE (priv->file_backend));

out:
	g_free (temp_file);
	g_free (cal_file);
}

static gint
retrieval_progress_cb (GnomeVFSAsyncHandle *handle, GnomeVFSXferProgressInfo *info, gpointer data)
{
	ECalBackendHttp *cbhttp = E_CAL_BACKEND_HTTP (data);
	ECalBackendHttpPrivate *priv;

	priv = cbhttp->priv;

	/* TODO: Handle errors */
	/* TODO: Report progress */

	g_message ("GnomeVFS async progress: %s",
		   info->status == GNOME_VFS_XFER_PROGRESS_STATUS_OK ? "ok" :
		   info->status == GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR ? "error" : "other");

	g_message ("(%d) %s -> %s", info->phase, info->source_name, info->target_name);

	if (info->phase == GNOME_VFS_XFER_PHASE_COMPLETED)
		retrieval_done (cbhttp);

	return TRUE;
}

static gboolean
begin_retrieval_cb (ECalBackendHttp *cbhttp)
{
	GnomeVFSURI *source_uri, *dest_uri;
	gchar *source_uri_str, *cache_dir, *temp_file;
	GList *source_uri_list = NULL, *dest_uri_list = NULL;
	ECalBackendHttpPrivate *priv;

	priv = cbhttp->priv;

	if (priv->retrieval_handle != NULL)
		return FALSE;

	cache_dir = uri_to_cache_dir (e_cal_backend_get_uri (E_CAL_BACKEND (cbhttp)));
	if (!cache_dir)
		return FALSE;

	temp_file = g_build_filename (cache_dir, "/calendar.ics.tmp", NULL);

	source_uri_str = webcal_to_http_method (e_cal_backend_get_uri (E_CAL_BACKEND (cbhttp)));
	if (!source_uri_str)
		source_uri_str = g_strdup (e_cal_backend_get_uri (E_CAL_BACKEND (cbhttp)));

	source_uri = gnome_vfs_uri_new (source_uri_str);
	dest_uri = gnome_vfs_uri_new (temp_file);

	g_free (source_uri_str);

	source_uri_list = g_list_append (source_uri_list, source_uri);
	dest_uri_list   = g_list_append (dest_uri_list, dest_uri);

	gnome_vfs_async_xfer (&priv->retrieval_handle,
			      source_uri_list, dest_uri_list,
			      GNOME_VFS_XFER_DEFAULT,
			      GNOME_VFS_XFER_ERROR_MODE_QUERY,
			      GNOME_VFS_XFER_OVERWRITE_MODE_REPLACE,
			      GNOME_VFS_PRIORITY_DEFAULT,
			      retrieval_progress_cb, cbhttp,
			      NULL, NULL);

	g_list_free (source_uri_list);
	g_list_free (dest_uri_list);

	gnome_vfs_uri_unref (source_uri);
	gnome_vfs_uri_unref (dest_uri);
	g_free (temp_file);
	g_free (cache_dir);
	return FALSE;
}

/* Open handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_open (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	
	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->file_backend) {
		gchar *cache_dir;

		cache_dir = uri_to_cache_dir (e_cal_backend_get_uri (E_CAL_BACKEND (cbhttp)));
		if (!cache_dir)
			return GNOME_Evolution_Calendar_NoSuchCal;

		if (!ensure_cache_dir (cache_dir)) {
			g_free (cache_dir);
			return GNOME_Evolution_Calendar_NoSuchCal;
		}

		priv->file_backend = g_object_new (E_TYPE_CAL_BACKEND_FILE_EVENTS, "uri", cache_dir, NULL);
		e_cal_backend_set_notification_proxy (E_CAL_BACKEND (priv->file_backend),
						      E_CAL_BACKEND (backend));
		g_free (cache_dir);

		g_idle_add ((GSourceFunc) begin_retrieval_cb, cbhttp);
	}

	e_cal_backend_sync_open (priv->file_backend, cal, FALSE);
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_http_remove (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	
	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->file_backend)
		return GNOME_Evolution_Calendar_OtherError;

	return e_cal_backend_sync_remove (priv->file_backend, cal);
}

/* is_loaded handler for the file backend */
static gboolean
e_cal_backend_http_is_loaded (ECalBackend *backend)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->file_backend)
		return FALSE;

	return e_cal_backend_is_loaded (E_CAL_BACKEND (priv->file_backend));
}

/* is_remote handler for the file backend */
static CalMode
e_cal_backend_http_get_mode (ECalBackend *backend)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	return priv->mode;
}

#define cal_mode_to_corba(mode) \
        (mode == CAL_MODE_LOCAL   ? GNOME_Evolution_Calendar_MODE_LOCAL  : \
	 mode == CAL_MODE_REMOTE  ? GNOME_Evolution_Calendar_MODE_REMOTE : \
	                            GNOME_Evolution_Calendar_MODE_ANY)

/* Set_mode handler for the file backend */
static void
e_cal_backend_http_set_mode (ECalBackend *backend, CalMode mode)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	GNOME_Evolution_Calendar_CalMode set_mode;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	switch (mode) {
		case CAL_MODE_LOCAL:
		case CAL_MODE_REMOTE:
			priv->mode = mode;
			set_mode = cal_mode_to_corba (mode);
			break;
		case CAL_MODE_ANY:
			priv->mode = CAL_MODE_REMOTE;
			set_mode = GNOME_Evolution_Calendar_MODE_REMOTE;
			break;
		default:
			set_mode = GNOME_Evolution_Calendar_MODE_ANY;
			break;
	}

	if (set_mode == GNOME_Evolution_Calendar_MODE_ANY)
		e_cal_backend_notify_mode (backend,
					 GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED,
					 cal_mode_to_corba (priv->mode));
	else
		e_cal_backend_notify_mode (backend,
					 GNOME_Evolution_Calendar_CalListener_MODE_SET,
					 set_mode);
}

static ECalBackendSyncStatus
e_cal_backend_http_get_default_object (ECalBackendSync *backend, EDataCal *cal, char **object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->file_backend)
		return GNOME_Evolution_Calendar_ObjectNotFound;

	return e_cal_backend_sync_get_default_object (priv->file_backend, cal, object);
}

/* Get_object_component handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_object (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, char **object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	if (!priv->file_backend)
		return GNOME_Evolution_Calendar_ObjectNotFound;

	return e_cal_backend_sync_get_object (priv->file_backend, cal, uid, rid, object);
}

/* Get_timezone_object handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid, char **object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	g_return_val_if_fail (tzid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	if (!priv->file_backend) {
		icaltimezone *zone;
		icalcomponent *icalcomp;

		zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
		if (!zone)
			return GNOME_Evolution_Calendar_ObjectNotFound;

		icalcomp = icaltimezone_get_component (zone);
		if (!icalcomp)
			return GNOME_Evolution_Calendar_InvalidObject;

		*object = g_strdup (icalcomponent_as_ical_string (icalcomp));
		return GNOME_Evolution_Calendar_Success;
	}

	return e_cal_backend_sync_get_timezone (priv->file_backend, cal, tzid, object);
}

/* Add_timezone handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_add_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = (ECalBackendHttp *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_HTTP (cbhttp), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbhttp->priv;

	if (!priv->file_backend)
		return GNOME_Evolution_Calendar_InvalidObject;

	return e_cal_backend_sync_add_timezone (priv->file_backend, cal, tzobj);
}

static ECalBackendSyncStatus
e_cal_backend_http_set_default_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->file_backend)
		return GNOME_Evolution_Calendar_NoSuchCal;

	return e_cal_backend_sync_set_default_timezone (priv->file_backend, cal, tzid);
}

/* Get_objects_in_range handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_object_list (ECalBackendSync *backend, EDataCal *cal, const char *sexp, GList **objects)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->file_backend)
		return GNOME_Evolution_Calendar_NoSuchCal;

	return e_cal_backend_sync_get_object_list (priv->file_backend, cal, sexp, objects);
}

/* get_query handler for the file backend */
static void
e_cal_backend_http_start_query (ECalBackend *backend, EDataCalView *query)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->file_backend)
		return;

	e_cal_backend_start_query (E_CAL_BACKEND (priv->file_backend), query);
}

/* Get_free_busy handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_free_busy (ECalBackendSync *backend, EDataCal *cal, GList *users,
				time_t start, time_t end, GList **freebusy)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	g_return_val_if_fail (start != -1 && end != -1, GNOME_Evolution_Calendar_InvalidRange);
	g_return_val_if_fail (start <= end, GNOME_Evolution_Calendar_InvalidRange);

	if (!priv->file_backend)
		return GNOME_Evolution_Calendar_NoSuchCal;

	return e_cal_backend_sync_get_free_busy (priv->file_backend, cal, users, start, end, freebusy);
}

/* Get_changes handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_changes (ECalBackendSync *backend, EDataCal *cal, const char *change_id,
			      GList **adds, GList **modifies, GList **deletes)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	g_return_val_if_fail (change_id != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	if (!priv->file_backend)
		return GNOME_Evolution_Calendar_ObjectNotFound;

	return e_cal_backend_sync_get_changes (priv->file_backend, cal, change_id, adds, modifies, deletes);
}

/* Discard_alarm handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *auid)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->file_backend)
		return GNOME_Evolution_Calendar_Success;

	return e_cal_backend_sync_discard_alarm (priv->file_backend, cal, uid, auid);
}

static ECalBackendSyncStatus
e_cal_backend_http_create_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, char **uid)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	
	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	if (!priv->file_backend)
		return GNOME_Evolution_Calendar_NoSuchCal;

	return e_cal_backend_sync_create_object (priv->file_backend, cal, calobj, uid);
}

static ECalBackendSyncStatus
e_cal_backend_http_modify_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, 
				CalObjModType mod, char **old_object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;
		
	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	if (!priv->file_backend)
		return GNOME_Evolution_Calendar_NoSuchCal;

	return e_cal_backend_sync_modify_object (priv->file_backend, cal, calobj, mod, old_object);
}

/* Remove_object handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_remove_object (ECalBackendSync *backend, EDataCal *cal,
				const char *uid, const char *rid,
				CalObjModType mod, char **object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	if (!priv->file_backend)
		return GNOME_Evolution_Calendar_NoSuchCal;

	return e_cal_backend_sync_remove_object (priv->file_backend, cal, uid, rid, mod, object);
}

/* Update_objects handler for the file backend. */
static ECalBackendSyncStatus
e_cal_backend_http_receive_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

	if (!priv->file_backend)
		return GNOME_Evolution_Calendar_InvalidObject;

	return e_cal_backend_sync_receive_objects (priv->file_backend, cal, calobj);
}

static ECalBackendSyncStatus
e_cal_backend_http_send_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->file_backend)
		return GNOME_Evolution_Calendar_Success;

	return e_cal_backend_sync_send_objects (priv->file_backend, cal, calobj);
}

static icaltimezone *
e_cal_backend_http_internal_get_default_timezone (ECalBackend *backend)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->file_backend)
		return NULL;

	return e_cal_backend_internal_get_default_timezone (E_CAL_BACKEND (priv->file_backend));
}

static icaltimezone *
e_cal_backend_http_internal_get_timezone (ECalBackend *backend, const char *tzid)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	icaltimezone *zone;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (priv->file_backend)
		zone = e_cal_backend_internal_get_timezone (E_CAL_BACKEND (priv->file_backend), tzid);
	else if (!strcmp (tzid, "UTC"))
	        zone = icaltimezone_get_utc_timezone ();
	else {
		zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	}

	return zone;
}

/* Object initialization function for the file backend */
static void
e_cal_backend_http_init (ECalBackendHttp *cbhttp, ECalBackendHttpClass *class)
{
	ECalBackendHttpPrivate *priv;

	priv = g_new0 (ECalBackendHttpPrivate, 1);
	cbhttp->priv = priv;

	priv->uri = NULL;
}

/* Class initialization function for the file backend */
static void
e_cal_backend_http_class_init (ECalBackendHttpClass *class)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;
	ECalBackendSyncClass *sync_class;

	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;
	sync_class = (ECalBackendSyncClass *) class;

	parent_class = (ECalBackendSyncClass *) g_type_class_peek_parent (class);

	object_class->dispose = e_cal_backend_http_dispose;
	object_class->finalize = e_cal_backend_http_finalize;

	sync_class->is_read_only_sync = e_cal_backend_http_is_read_only;
	sync_class->get_cal_address_sync = e_cal_backend_http_get_cal_address;
 	sync_class->get_alarm_email_address_sync = e_cal_backend_http_get_alarm_email_address;
 	sync_class->get_ldap_attribute_sync = e_cal_backend_http_get_ldap_attribute;
 	sync_class->get_static_capabilities_sync = e_cal_backend_http_get_static_capabilities;
	sync_class->open_sync = e_cal_backend_http_open;
	sync_class->remove_sync = e_cal_backend_http_remove;
	sync_class->create_object_sync = e_cal_backend_http_create_object;
	sync_class->modify_object_sync = e_cal_backend_http_modify_object;
	sync_class->remove_object_sync = e_cal_backend_http_remove_object;
	sync_class->discard_alarm_sync = e_cal_backend_http_discard_alarm;
	sync_class->receive_objects_sync = e_cal_backend_http_receive_objects;
	sync_class->send_objects_sync = e_cal_backend_http_send_objects;
 	sync_class->get_default_object_sync = e_cal_backend_http_get_default_object;
	sync_class->get_object_sync = e_cal_backend_http_get_object;
	sync_class->get_object_list_sync = e_cal_backend_http_get_object_list;
	sync_class->get_timezone_sync = e_cal_backend_http_get_timezone;
	sync_class->add_timezone_sync = e_cal_backend_http_add_timezone;
	sync_class->set_default_timezone_sync = e_cal_backend_http_set_default_timezone;
	sync_class->get_freebusy_sync = e_cal_backend_http_get_free_busy;
	sync_class->get_changes_sync = e_cal_backend_http_get_changes;

	backend_class->is_loaded = e_cal_backend_http_is_loaded;
	backend_class->start_query = e_cal_backend_http_start_query;
	backend_class->get_mode = e_cal_backend_http_get_mode;
	backend_class->set_mode = e_cal_backend_http_set_mode;

	backend_class->internal_get_default_timezone = e_cal_backend_http_internal_get_default_timezone;
	backend_class->internal_get_timezone = e_cal_backend_http_internal_get_timezone;
}


/**
 * e_cal_backend_http_get_type:
 * @void: 
 * 
 * Registers the #ECalBackendHttp class if necessary, and returns the type ID
 * associated to it.
 * 
 * Return value: The type ID of the #ECalBackendHttp class.
 **/
GType
e_cal_backend_http_get_type (void)
{
	static GType e_cal_backend_http_type = 0;

	if (!e_cal_backend_http_type) {
		static GTypeInfo info = {
                        sizeof (ECalBackendHttpClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_cal_backend_http_class_init,
                        NULL, NULL,
                        sizeof (ECalBackendHttp),
                        0,
                        (GInstanceInitFunc) e_cal_backend_http_init
                };
		e_cal_backend_http_type = g_type_register_static (E_TYPE_CAL_BACKEND_SYNC,
								  "ECalBackendHttp", &info, 0);
	}

	return e_cal_backend_http_type;
}

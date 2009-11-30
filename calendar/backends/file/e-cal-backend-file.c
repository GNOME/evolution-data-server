/* Evolution calendar - iCalendar file backend
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Rodrigo Moya <rodrigo@ximian.com>
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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include "libedataserver/e-data-server-util.h"
#include "libedataserver/e-xml-hash-utils.h"
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-time-util.h>
#include <libecal/e-cal-util.h>
#include <libecal/e-cal-check-timezones.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libedata-cal/e-cal-backend-sexp.h>
#include "e-cal-backend-file-events.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

/* Placeholder for each component and its recurrences */
typedef struct {
	ECalComponent *full_object;
	GHashTable *recurrences;
	GList *recurrences_list;
} ECalBackendFileObject;

/* Private part of the ECalBackendFile structure */
struct _ECalBackendFilePrivate {
	/* path where the calendar data is stored */
	gchar *path;

	/* Filename in the dir */
	gchar *file_name;
	gboolean read_only;
	gboolean is_dirty;
	guint dirty_idle_id;

	/* locked in high-level functions to ensure data is consistent
	 * in idle and CORBA thread(s?); because high-level functions
	 * may call other high-level functions the mutex must allow
	 * recursive locking
	 */
	GStaticRecMutex idle_save_rmutex;

	/* Toplevel VCALENDAR component */
	icalcomponent *icalcomp;

	/* All the objects in the calendar, hashed by UID.  The
	 * hash key *is* the uid returned by cal_component_get_uid(); it is not
	 * copied, so don't free it when you remove an object from the hash
	 * table. Each item in the hash table is a ECalBackendFileObject.
	 */
	GHashTable *comp_uid_hash;

	GList *comp;

	/* The calendar's default timezone, used for resolving DATE and
	   floating DATE-TIME values. */
	icaltimezone *default_zone;

	/* a custom filename opened */
	gchar *custom_file;

	/* guards refresh members */
	GMutex *refresh_lock;
	/* set to TRUE to indicate thread should stop */
	gboolean refresh_thread_stop;
	/* condition for refreshing, not NULL when thread exists */
	GCond *refresh_cond;
	/* cond to know the refresh thread gone */
	GCond *refresh_gone_cond;
	/* increased when backend saves the file */
	guint refresh_skip;

	/* Monitor for a refresh type "1" */
	GFileMonitor *refresh_monitor;

	/* timeour id for refresh type "2" */
	guint refresh_timeout_id;
};



#define d(x)

static void e_cal_backend_file_dispose (GObject *object);
static void e_cal_backend_file_finalize (GObject *object);

static ECalBackendSyncClass *parent_class;

static ECalBackendSyncStatus
e_cal_backend_file_add_timezone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj);

static void free_refresh_data (ECalBackendFile *cbfile);

/* g_hash_table_foreach() callback to destroy a ECalBackendFileObject */
static void
free_object_data (gpointer data)
{
	ECalBackendFileObject *obj_data = data;

	if (obj_data->full_object)
		g_object_unref (obj_data->full_object);
	g_hash_table_destroy (obj_data->recurrences);
	g_list_free (obj_data->recurrences_list);

	g_free (obj_data);
}

/* Saves the calendar data */
static gboolean
save_file_when_idle (gpointer user_data)
{
	ECalBackendFilePrivate *priv;
	GError *e = NULL;
	GFile *file, *backup_file;
	GFileOutputStream *stream;
	gchar *tmp, *backup_uristr;
	gchar *buf;
	ECalBackendFile *cbfile = user_data;

	priv = cbfile->priv;
	g_assert (priv->path != NULL);
	g_assert (priv->icalcomp != NULL);

	g_static_rec_mutex_lock (&priv->idle_save_rmutex);
	if (!priv->is_dirty || priv->read_only) {
		priv->dirty_idle_id = 0;
		priv->is_dirty = FALSE;
		g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
		return FALSE;
	}

	file = g_file_new_for_path (priv->path);
	if (!file)
		goto error_malformed_uri;

	/* save calendar to backup file */
	tmp = g_file_get_uri (file);
	if (!tmp) {
		g_object_unref (file);
		goto error_malformed_uri;
	}

	backup_uristr = g_strconcat (tmp, "~", NULL);
	backup_file = g_file_new_for_uri (backup_uristr);

	g_free (tmp);
	g_free (backup_uristr);

	if (!backup_file) {
		g_object_unref (file);
		goto error_malformed_uri;
	}

	priv->refresh_skip++;
	stream = g_file_replace (backup_file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, &e);
	if (!stream || e) {
		if (stream)
			g_object_unref (stream);

		g_object_unref (file);
		g_object_unref (backup_file);
		priv->refresh_skip--;
		goto error;
	}

	buf = icalcomponent_as_ical_string_r (priv->icalcomp);
	g_output_stream_write_all (G_OUTPUT_STREAM (stream), buf, strlen (buf) * sizeof (gchar), NULL, NULL, &e);
	g_free (buf);

	if (e) {
		g_object_unref (stream);
		g_object_unref (file);
		g_object_unref (backup_file);
		goto error;
	}

	g_output_stream_close (G_OUTPUT_STREAM (stream), NULL, &e);
	g_object_unref (stream);

	if (e) {
		g_object_unref (file);
		g_object_unref (backup_file);
		goto error;
	}

	/* now copy the temporary file to the real file */
	g_file_move (backup_file, file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &e);

	g_object_unref (file);
	g_object_unref (backup_file);
	if (e)
		goto error;

	priv->is_dirty = FALSE;
	priv->dirty_idle_id = 0;

	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);

	return FALSE;

 error_malformed_uri:
	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
	e_cal_backend_notify_error (E_CAL_BACKEND (cbfile),
				  _("Cannot save calendar data: Malformed URI."));
	return FALSE;

 error:
	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);

	if (e) {
		gchar *msg = g_strdup_printf ("%s: %s", _("Cannot save calendar data"), e->message);

		e_cal_backend_notify_error (E_CAL_BACKEND (cbfile), msg);
		g_free (msg);
		g_error_free (e);
	} else
		e_cal_backend_notify_error (E_CAL_BACKEND (cbfile), _("Cannot save calendar data"));

	return FALSE;
}

static void
save (ECalBackendFile *cbfile)
{
	ECalBackendFilePrivate *priv;

	priv = cbfile->priv;

	g_static_rec_mutex_lock (&priv->idle_save_rmutex);
	priv->is_dirty = TRUE;

	if (!priv->dirty_idle_id)
		priv->dirty_idle_id = g_idle_add ((GSourceFunc) save_file_when_idle, cbfile);

	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
}

static void
free_calendar_components (GHashTable *comp_uid_hash, icalcomponent *top_icomp)
{
	if (comp_uid_hash)
		g_hash_table_destroy (comp_uid_hash);

	if (top_icomp)
		icalcomponent_free (top_icomp);
}

static void
free_calendar_data (ECalBackendFile *cbfile)
{
	ECalBackendFilePrivate *priv;

	priv = cbfile->priv;

	free_calendar_components (priv->comp_uid_hash, priv->icalcomp);
	priv->comp_uid_hash = NULL;
	priv->icalcomp = NULL;

	g_list_free (priv->comp);
	priv->comp = NULL;
}

/* Dispose handler for the file backend */
static void
e_cal_backend_file_dispose (GObject *object)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	ESource *source;

	cbfile = E_CAL_BACKEND_FILE (object);
	priv = cbfile->priv;

	/* Save if necessary */
	if (priv->is_dirty)
		save_file_when_idle (cbfile);

	free_calendar_data (cbfile);

	source = e_cal_backend_get_source (E_CAL_BACKEND (cbfile));
	if (source)
		g_signal_handlers_disconnect_matched (source, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, cbfile);

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

/* Finalize handler for the file backend */
static void
e_cal_backend_file_finalize (GObject *object)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_FILE (object));

	cbfile = E_CAL_BACKEND_FILE (object);
	priv = cbfile->priv;

	/* Clean up */

	if (priv->dirty_idle_id) {
		g_source_remove (priv->dirty_idle_id);
		priv->dirty_idle_id = 0;
	}

	free_refresh_data (cbfile);

	if (priv->refresh_lock)
		g_mutex_free (priv->refresh_lock);
	priv->refresh_lock = NULL;

	g_static_rec_mutex_free (&priv->idle_save_rmutex);

	if (priv->path) {
		g_free (priv->path);
		priv->path = NULL;
	}

	g_free (priv->custom_file);
	priv->custom_file = NULL;

	if (priv->default_zone && priv->default_zone != icaltimezone_get_utc_timezone ()) {
		icaltimezone_free (priv->default_zone, 1);
	}
	priv->default_zone = NULL;

	if (priv->file_name) {
		g_free (priv->file_name);
		priv->file_name = NULL;
	}
	g_free (priv);
	cbfile->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}



/* Looks up a component by its UID on the backend's component hash table */
static ECalComponent *
lookup_component (ECalBackendFile *cbfile, const gchar *uid)
{
	ECalBackendFilePrivate *priv;
	ECalBackendFileObject *obj_data;

	priv = cbfile->priv;

	obj_data = g_hash_table_lookup (priv->comp_uid_hash, uid);
	return obj_data ? obj_data->full_object : NULL;
}



/* Calendar backend methods */

/* Is_read_only handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_is_read_only (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{
	ECalBackendFile *cbfile = (ECalBackendFile *) backend;

	*read_only = cbfile->priv->read_only;

	return GNOME_Evolution_Calendar_Success;
}

/* Get_email_address handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_get_cal_address (ECalBackendSync *backend, EDataCal *cal, gchar **address)
{
	/* A file backend has no particular email address associated
	 * with it (although that would be a useful feature some day).
	 */
	*address = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_file_get_ldap_attribute (ECalBackendSync *backend, EDataCal *cal, gchar **attribute)
{
	*attribute = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_file_get_alarm_email_address (ECalBackendSync *backend, EDataCal *cal, gchar **address)
{
	/* A file backend has no particular email address associated
	 * with it (although that would be a useful feature some day).
	 */
	*address = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_file_get_static_capabilities (ECalBackendSync *backend, EDataCal *cal, gchar **capabilities)
{
	*capabilities = g_strdup (CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS ","
				  CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
				  CAL_STATIC_CAPABILITY_DELEGATE_SUPPORTED ","
				  CAL_STATIC_CAPABILITY_NO_THISANDPRIOR);

	return GNOME_Evolution_Calendar_Success;
}

/* function to resolve timezones */
static icaltimezone *
resolve_tzid (const gchar *tzid, gpointer user_data)
{
	icalcomponent *vcalendar_comp = user_data;

        if (!tzid || !tzid[0])
                return NULL;
        else if (!strcmp (tzid, "UTC"))
                return icaltimezone_get_utc_timezone ();

	return icalcomponent_get_timezone (vcalendar_comp, tzid);
}

/* Checks if the specified component has a duplicated UID and if so changes it */
static void
check_dup_uid (ECalBackendFile *cbfile, ECalComponent *comp)
{
	ECalBackendFilePrivate *priv;
	ECalBackendFileObject *obj_data;
	const gchar *uid = NULL;
	gchar *new_uid;

	priv = cbfile->priv;

	e_cal_component_get_uid (comp, &uid);

	if (!uid) {
		g_warning ("Checking for duplicate uid, the component does not have a valid UID skipping it\n");
		return;
	}

	obj_data = g_hash_table_lookup (priv->comp_uid_hash, uid);
	if (!obj_data)
		return; /* Everything is fine */

	d(g_message (G_STRLOC ": Got object with duplicated UID `%s', changing it...", uid));

	new_uid = e_cal_component_gen_uid ();
	e_cal_component_set_uid (comp, new_uid);
	g_free (new_uid);

	/* FIXME: I think we need to reset the SEQUENCE property and reset the
	 * CREATED/DTSTAMP/LAST-MODIFIED.
	 */

	save (cbfile);
}

static struct icaltimetype
get_rid_icaltime (ECalComponent *comp)
{
	ECalComponentRange range;
        struct icaltimetype tt;

        e_cal_component_get_recurid (comp, &range);
        if (!range.datetime.value)
                return icaltime_null_time ();
        tt = *range.datetime.value;
        e_cal_component_free_range (&range);

        return tt;
}

/* Tries to add an icalcomponent to the file backend.  We only store the objects
 * of the types we support; all others just remain in the toplevel component so
 * that we don't lose them.
 */
static void
add_component (ECalBackendFile *cbfile, ECalComponent *comp, gboolean add_to_toplevel)
{
	ECalBackendFilePrivate *priv;
	ECalBackendFileObject *obj_data;
	const gchar *uid = NULL;

	priv = cbfile->priv;

	e_cal_component_get_uid (comp, &uid);

	if (!uid) {
		g_warning ("The component does not have a valid UID skipping it\n");
		return;
	}

	obj_data = g_hash_table_lookup (priv->comp_uid_hash, uid);
	if (e_cal_component_is_instance (comp)) {
		gchar *rid;

		rid = e_cal_component_get_recurid_as_string (comp);
		if (obj_data) {
			if (g_hash_table_lookup (obj_data->recurrences, rid)) {
				g_warning (G_STRLOC ": Tried to add an already existing recurrence");
				g_free (rid);
				return;
			}
		} else {
			obj_data = g_new0 (ECalBackendFileObject, 1);
			obj_data->full_object = NULL;
			obj_data->recurrences = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
			g_hash_table_insert (priv->comp_uid_hash, g_strdup (uid), obj_data);
		}

		g_hash_table_insert (obj_data->recurrences, rid, comp);
		obj_data->recurrences_list = g_list_append (obj_data->recurrences_list, comp);
	} else {
		/* Ensure that the UID is unique; some broken implementations spit
		 * components with duplicated UIDs.
		 */
		check_dup_uid (cbfile, comp);

		if (obj_data) {
			if (obj_data->full_object) {
				g_warning (G_STRLOC ": Tried to add an already existing object");
				return;
			}

			obj_data->full_object = comp;
		} else {
			obj_data = g_new0 (ECalBackendFileObject, 1);
			obj_data->full_object = comp;
			obj_data->recurrences = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

			g_hash_table_insert (priv->comp_uid_hash, g_strdup (uid), obj_data);
		}
	}

	priv->comp = g_list_prepend (priv->comp, comp);

	/* Put the object in the toplevel component if required */

	if (add_to_toplevel) {
		icalcomponent *icalcomp;

		icalcomp = e_cal_component_get_icalcomponent (comp);
		g_assert (icalcomp != NULL);

		icalcomponent_add_component (priv->icalcomp, icalcomp);
	}
}

/* g_hash_table_foreach_remove() callback to remove recurrences from the calendar */
static gboolean
remove_recurrence_cb (gpointer key, gpointer value, gpointer data)
{
	GList *l;
	icalcomponent *icalcomp;
	ECalBackendFilePrivate *priv;
	ECalComponent *comp = value;
	ECalBackendFile *cbfile = data;

	priv = cbfile->priv;

	/* remove the recurrence from the top-level calendar */
	icalcomp = e_cal_component_get_icalcomponent (comp);
	g_assert (icalcomp != NULL);

	icalcomponent_remove_component (priv->icalcomp, icalcomp);

	/* remove it from our mapping */
	l = g_list_find (priv->comp, comp);
	priv->comp = g_list_delete_link (priv->comp, l);

	return TRUE;
}

/* Removes a component from the backend's hash and lists.  Does not perform
 * notification on the clients.  Also removes the component from the toplevel
 * icalcomponent.
 */
static void
remove_component (ECalBackendFile *cbfile, const gchar *uid, ECalBackendFileObject *obj_data)
{
	ECalBackendFilePrivate *priv;
	icalcomponent *icalcomp;
	GList *l;

	priv = cbfile->priv;

	/* Remove the icalcomp from the toplevel */
	if (obj_data->full_object) {
		icalcomp = e_cal_component_get_icalcomponent (obj_data->full_object);
		g_assert (icalcomp != NULL);

		icalcomponent_remove_component (priv->icalcomp, icalcomp);

		/* Remove it from our mapping */
		l = g_list_find (priv->comp, obj_data->full_object);
		g_assert (l != NULL);
		priv->comp = g_list_delete_link (priv->comp, l);
	}

	/* remove the recurrences also */
	g_hash_table_foreach_remove (obj_data->recurrences, (GHRFunc) remove_recurrence_cb, cbfile);

	g_hash_table_remove (priv->comp_uid_hash, uid);

	save (cbfile);
}

/* Scans the toplevel VCALENDAR component and stores the objects it finds */
static void
scan_vcalendar (ECalBackendFile *cbfile)
{
	ECalBackendFilePrivate *priv;
	icalcompiter iter;

	priv = cbfile->priv;
	g_assert (priv->icalcomp != NULL);
	g_assert (priv->comp_uid_hash != NULL);

	for (iter = icalcomponent_begin_component (priv->icalcomp, ICAL_ANY_COMPONENT);
	     icalcompiter_deref (&iter) != NULL;
	     icalcompiter_next (&iter)) {
		icalcomponent *icalcomp;
		icalcomponent_kind kind;
		ECalComponent *comp;

		icalcomp = icalcompiter_deref (&iter);

		kind = icalcomponent_isa (icalcomp);

		if (!(kind == ICAL_VEVENT_COMPONENT
		      || kind == ICAL_VTODO_COMPONENT
		      || kind == ICAL_VJOURNAL_COMPONENT))
			continue;

		comp = e_cal_component_new ();

		if (!e_cal_component_set_icalcomponent (comp, icalcomp))
			continue;

		add_component (cbfile, comp, FALSE);
	}
}

static gchar *
uri_to_path (ECalBackend *backend)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	ESource *source;
	const gchar *master_uri;
	gchar *full_uri, *str_uri;
	GFile *file = NULL;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	source = e_cal_backend_get_source (backend);
	if (source && e_source_get_property (source, "custom-file")) {
		/* customr-uri is with a filename already */
		master_uri = e_source_get_property (source, "custom-file");
		file = g_file_new_for_path (master_uri);
		if (!file)
			return NULL;
	}

	if (!file) {
		master_uri = e_cal_backend_get_uri (backend);

		/* FIXME Check the error conditions a little more elegantly here */
		if (g_strrstr ("tasks.ics", master_uri) || g_strrstr ("calendar.ics", master_uri)) {
			g_warning (G_STRLOC ": Existing file name %s", master_uri);

			return NULL;
		}

		full_uri = g_strdup_printf ("%s/%s", master_uri, priv->file_name);
		file = g_file_new_for_uri (full_uri);
		g_free (full_uri);
	}

	if (!file)
		return NULL;

	str_uri = g_file_get_path (file);

	g_object_unref (file);

	if (!str_uri || !*str_uri) {
		g_free (str_uri);

		return NULL;
	}

	return str_uri;
}

static gpointer
refresh_thread_func (gpointer data)
{
	ECalBackendFile *cbfile = data;
	ECalBackendFilePrivate *priv;
	GFile *file;
	GFileInfo *info;
	guint64 last_modified, modified;

	g_return_val_if_fail (cbfile != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND_FILE (cbfile), NULL);

	priv = cbfile->priv;
	g_return_val_if_fail (priv->custom_file != NULL, NULL);

	file = g_file_new_for_path (priv->custom_file);
	info = g_file_query_info (file, G_FILE_ATTRIBUTE_TIME_MODIFIED, G_FILE_QUERY_INFO_NONE, NULL, NULL);
	g_return_val_if_fail (info != NULL, NULL);

	last_modified = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
	g_object_unref (info);

	g_mutex_lock (priv->refresh_lock);
	while (!priv->refresh_thread_stop) {
		g_cond_wait (priv->refresh_cond, priv->refresh_lock);

		g_static_rec_mutex_lock (&priv->idle_save_rmutex);

		if (priv->refresh_skip > 0) {
			priv->refresh_skip--;
			g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
			continue;
		}

		if (priv->is_dirty) {
			/* save before reload, if dirty */
			if (priv->dirty_idle_id) {
				g_source_remove (priv->dirty_idle_id);
				priv->dirty_idle_id = 0;
			}
			save_file_when_idle (cbfile);
			priv->refresh_skip = 0;
		}

		g_static_rec_mutex_unlock (&priv->idle_save_rmutex);

		info = g_file_query_info (file, G_FILE_ATTRIBUTE_TIME_MODIFIED, G_FILE_QUERY_INFO_NONE, NULL, NULL);
		if (!info)
			break;

		modified = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
		g_object_unref (info);

		if (modified != last_modified) {
			last_modified = modified;
			e_cal_backend_file_reload (cbfile);
		}
	}

	g_object_unref (file);
	g_cond_signal (priv->refresh_gone_cond);
	g_mutex_unlock (priv->refresh_lock);

	return NULL;
}

static gboolean
check_refresh_calendar_timeout (ECalBackendFilePrivate *priv)
{
	g_return_val_if_fail (priv != NULL, FALSE);

	/* called in the main thread */
	if (priv->refresh_cond)
		g_cond_signal (priv->refresh_cond);

	/* call it next time again */
	return TRUE;
}

static void
custom_file_changed (GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, ECalBackendFilePrivate *priv)
{
	if (priv->refresh_cond)
		g_cond_signal (priv->refresh_cond);
}

static void
prepare_refresh_data (ECalBackendFile *cbfile)
{
	ECalBackendFilePrivate *priv;
	ESource *source;
	const gchar *value;

	g_return_if_fail (cbfile != NULL);

	priv = cbfile->priv;

	g_mutex_lock (priv->refresh_lock);

	priv->refresh_thread_stop = FALSE;
	priv->refresh_skip = 0;

	source = e_cal_backend_get_source (E_CAL_BACKEND (cbfile));
	value = e_source_get_property (source, "refresh-type");
	if (e_source_get_property (source, "custom-file") && value && *value && !value[1]) {
		GFile *file;
		GError *error = NULL;

		switch (*value) {
		case '1': /* on file change */
			file = g_file_new_for_path (priv->custom_file);
			priv->refresh_monitor = g_file_monitor_file (file, G_FILE_MONITOR_WATCH_MOUNTS, NULL, &error);
			if (file)
				g_object_unref (file);
			if (priv->refresh_monitor)
				g_signal_connect (G_OBJECT (priv->refresh_monitor), "changed", G_CALLBACK (custom_file_changed), priv);
			break;
		case '2': /* on refresh timeout */
			value = e_source_get_property (source, "refresh");
			if (value && atoi (value) > 0) {
				priv->refresh_timeout_id = g_timeout_add_seconds (60 * atoi (value), (GSourceFunc) check_refresh_calendar_timeout, priv);
			}
			break;
		default:
			break;
		}
	}

	if (priv->refresh_monitor || priv->refresh_timeout_id) {
		priv->refresh_cond = g_cond_new ();
		priv->refresh_gone_cond = g_cond_new ();

		g_thread_create (refresh_thread_func, cbfile, FALSE, NULL);
	}

	g_mutex_unlock (priv->refresh_lock);
}

static void
free_refresh_data (ECalBackendFile *cbfile)
{
	ECalBackendFilePrivate *priv;

	g_return_if_fail (cbfile != NULL);

	priv = cbfile->priv;
	g_return_if_fail (priv != NULL);

	g_mutex_lock (priv->refresh_lock);

	if (priv->refresh_monitor)
		g_object_unref (priv->refresh_monitor);
	priv->refresh_monitor = NULL;

	if (priv->refresh_timeout_id)
		g_source_remove (priv->refresh_timeout_id);
	priv->refresh_timeout_id = 0;

	if (priv->refresh_cond) {
		priv->refresh_thread_stop = TRUE;
		g_cond_signal (priv->refresh_cond);
		g_cond_wait (priv->refresh_gone_cond, priv->refresh_lock);

		g_cond_free (priv->refresh_cond);
		priv->refresh_cond = NULL;
		g_cond_free (priv->refresh_gone_cond);
		priv->refresh_gone_cond = NULL;
	}

	priv->refresh_skip = 0;

	g_mutex_unlock (priv->refresh_lock);
}

/* Parses an open iCalendar file and loads it into the backend */
static ECalBackendSyncStatus
open_cal (ECalBackendFile *cbfile, const gchar *uristr)
{
	ECalBackendFilePrivate *priv;
	icalcomponent *icalcomp;

	priv = cbfile->priv;

	free_refresh_data (cbfile);

	icalcomp = e_cal_util_parse_ics_file (uristr);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_OtherError;

	/* FIXME: should we try to demangle XROOT components and
	 * individual components as well?
	 */

	if (icalcomponent_isa (icalcomp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_free (icalcomp);

		return GNOME_Evolution_Calendar_OtherError;
	}

	priv->icalcomp = icalcomp;
	priv->path = uri_to_path (E_CAL_BACKEND (cbfile));
	g_free (priv->custom_file);
	priv->custom_file = g_strdup (uristr);

	priv->comp_uid_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, free_object_data);
	scan_vcalendar (cbfile);

	prepare_refresh_data (cbfile);

	return GNOME_Evolution_Calendar_Success;
}

typedef struct
{
	ECalBackend *backend;
	GHashTable *old_uid_hash;
	GHashTable *new_uid_hash;
}
BackendDeltaContext;

static void
notify_removals_cb (gpointer key, gpointer value, gpointer data)
{
	BackendDeltaContext *context = data;
	const gchar *uid = key;
	ECalBackendFileObject *old_obj_data = value;

	if (!g_hash_table_lookup (context->new_uid_hash, uid)) {
		icalcomponent *old_icomp;
		gchar *old_obj_str;
		ECalComponent *comp;
		ECalComponentId *id;

		/* Object was removed */

		old_icomp = e_cal_component_get_icalcomponent (old_obj_data->full_object);
		if (!old_icomp)
			return;

		old_obj_str = icalcomponent_as_ical_string_r (old_icomp);
		if (!old_obj_str)
			return;

		comp = e_cal_component_new_from_string (old_obj_str);
		id = e_cal_component_get_id (comp);

		e_cal_backend_notify_object_removed (context->backend, id, old_obj_str, NULL);

		e_cal_component_free_id (id);
		g_free (old_obj_str);
		g_object_unref (comp);
	}
}

static void
notify_adds_modifies_cb (gpointer key, gpointer value, gpointer data)
{
	BackendDeltaContext *context = data;
	const gchar *uid = key;
	ECalBackendFileObject *new_obj_data = value;
	ECalBackendFileObject *old_obj_data;
	icalcomponent *old_icomp, *new_icomp;
	gchar *old_obj_str, *new_obj_str;

	old_obj_data = g_hash_table_lookup (context->old_uid_hash, uid);

	if (!old_obj_data) {
		/* Object was added */

		new_icomp = e_cal_component_get_icalcomponent (new_obj_data->full_object);
		if (!new_icomp)
			return;

		new_obj_str = icalcomponent_as_ical_string_r (new_icomp);
		if (!new_obj_str)
			return;

		e_cal_backend_notify_object_created (context->backend, new_obj_str);
		g_free (new_obj_str);
	} else {
		old_icomp = e_cal_component_get_icalcomponent (old_obj_data->full_object);
		new_icomp = e_cal_component_get_icalcomponent (new_obj_data->full_object);
		if (!old_icomp || !new_icomp)
			return;

		old_obj_str = icalcomponent_as_ical_string_r (old_icomp);
		new_obj_str = icalcomponent_as_ical_string_r (new_icomp);
		if (!old_obj_str || !new_obj_str)
			return;

		if (strcmp (old_obj_str, new_obj_str)) {
			/* Object was modified */

			e_cal_backend_notify_object_modified (context->backend, old_obj_str, new_obj_str);
		}
		g_free (old_obj_str);
		g_free (new_obj_str);
	}
}

static void
notify_changes (ECalBackendFile *cbfile, GHashTable *old_uid_hash, GHashTable *new_uid_hash)
{
	BackendDeltaContext context;

	context.backend = E_CAL_BACKEND (cbfile);
	context.old_uid_hash = old_uid_hash;
	context.new_uid_hash = new_uid_hash;

	g_hash_table_foreach (old_uid_hash, (GHFunc) notify_removals_cb, &context);
	g_hash_table_foreach (new_uid_hash, (GHFunc) notify_adds_modifies_cb, &context);
}

static ECalBackendSyncStatus
reload_cal (ECalBackendFile *cbfile, const gchar *uristr)
{
	ECalBackendFilePrivate *priv;
	icalcomponent *icalcomp, *icalcomp_old;
	GHashTable *comp_uid_hash_old;

	priv = cbfile->priv;

	icalcomp = e_cal_util_parse_ics_file (uristr);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_OtherError;

	/* FIXME: should we try to demangle XROOT components and
	 * individual components as well?
	 */

	if (icalcomponent_isa (icalcomp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_free (icalcomp);

		return GNOME_Evolution_Calendar_OtherError;
	}

	/* Keep old data for comparison - free later */

	icalcomp_old = priv->icalcomp;
	priv->icalcomp = NULL;

	comp_uid_hash_old = priv->comp_uid_hash;
	priv->comp_uid_hash = NULL;

	/* Load new calendar */

	free_calendar_data (cbfile);

	priv->icalcomp = icalcomp;

	priv->comp_uid_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, free_object_data);
	scan_vcalendar (cbfile);

	priv->path = uri_to_path (E_CAL_BACKEND (cbfile));

	/* Compare old and new versions of calendar */

	notify_changes (cbfile, comp_uid_hash_old, priv->comp_uid_hash);

	/* Free old data */

	free_calendar_components (comp_uid_hash_old, icalcomp_old);
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
create_cal (ECalBackendFile *cbfile, const gchar *uristr)
{
	gchar *dirname;
	ECalBackendFilePrivate *priv;

	free_refresh_data (cbfile);

	priv = cbfile->priv;

	/* Create the directory to contain the file */
	dirname = g_path_get_dirname (uristr);
	if (g_mkdir_with_parents (dirname, 0700) != 0) {
		g_free (dirname);
		return GNOME_Evolution_Calendar_NoSuchCal;
	}

	g_free (dirname);

	/* Create the new calendar information */
	priv->icalcomp = e_cal_util_new_top_level ();

	/* Create our internal data */
	priv->comp_uid_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, free_object_data);

	priv->path = uri_to_path (E_CAL_BACKEND (cbfile));

	save (cbfile);

	g_free (priv->custom_file);
	priv->custom_file = g_strdup (uristr);
	prepare_refresh_data (cbfile);

	return GNOME_Evolution_Calendar_Success;
}

static gchar *
get_uri_string (ECalBackend *backend)
{
	gchar *str_uri, *full_uri;

	str_uri = uri_to_path (backend);
	full_uri = g_uri_unescape_string (str_uri, "");
	g_free (str_uri);

	return full_uri;
}

static gboolean
add_timezone (icalcomponent *icalcomp, icaltimezone *tzone)
{
	GSList *to_remove = NULL, *r;
	icalcomponent *subcomp;
	icaltimezone *zone;
	gboolean add = TRUE, have_same = FALSE;
	const gchar *tzid;
	gchar *cmp;

	g_return_val_if_fail (icalcomp != NULL, FALSE);

	/* it's fine to have passed in NULL tzcomp; for example UTC timezone does this */
	if (!tzone || !icaltimezone_get_component (tzone))
		return FALSE;

	tzid = icaltimezone_get_tzid (tzone);
	if (!tzid)
		return FALSE;

	cmp = icalcomponent_as_ical_string_r (icaltimezone_get_component (tzone));
	zone = icaltimezone_new ();

	for (subcomp = icalcomponent_get_first_component (icalcomp, ICAL_VTIMEZONE_COMPONENT);
	     subcomp;
	     subcomp = icalcomponent_get_next_component (icalcomp, ICAL_VTIMEZONE_COMPONENT)) {
		if (!icaltimezone_set_component (zone, icalcomponent_new_clone (subcomp))) {
			to_remove = g_slist_prepend (to_remove, subcomp);
		} else if (icaltimezone_get_tzid (zone) && g_str_equal (tzid, icaltimezone_get_tzid (zone))) {
			/* there is a timezone component with the same tzid already */
			if (have_same) {
				to_remove = g_slist_prepend (to_remove, subcomp);
			} else {
				gchar *str = icalcomponent_as_ical_string_r (subcomp);

				/* not the best way how to compare two components, but don't have better */
				if (str && g_str_equal (cmp, str)) {
					have_same = TRUE;
					add = FALSE;
				} else {
					to_remove = g_slist_prepend (to_remove, subcomp);
				}

				g_free (str);
			}
		}
	}

	g_free (cmp);

	for (r = to_remove; r; r = r->next) {
		icalcomponent_remove_component (icalcomp, r->data);
	}

	if (g_slist_length (to_remove) > 1) {
		/* there were more than once tzid as this,
		   thus check for duplicities for all of timezones there */
		GHashTable *known = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		GSList *rem2 = NULL;

		for (subcomp = icalcomponent_get_first_component (icalcomp, ICAL_VTIMEZONE_COMPONENT);
		     subcomp;
		     subcomp = icalcomponent_get_next_component (icalcomp, ICAL_VTIMEZONE_COMPONENT)) {
			if (!icaltimezone_set_component (zone, icalcomponent_new_clone (subcomp))) {
				rem2 = g_slist_prepend (rem2, subcomp);
			} else {
				const gchar *tzid2 = icaltimezone_get_tzid (zone);

				/* check all but not the one which was checked above */
				if (tzid2 && !g_str_equal (tzid, tzid2)) {
					if (g_hash_table_lookup (known, tzid2) == NULL) {
						/* the first component of this tzid, keep it */
						g_hash_table_insert (known, g_strdup (tzid2), GINT_TO_POINTER (1));
					} else {
						/* it's there already, remove it */
						rem2 = g_slist_prepend (rem2, subcomp);
					}
				}
			}
		}

		for (r = rem2; r; r = r->next) {
			icalcomponent_remove_component (icalcomp, r->data);
		}

		g_slist_free (rem2);
		g_hash_table_unref (known);
	}

	icaltimezone_free (zone, TRUE);
	g_slist_free (to_remove);

	if (add)
		icalcomponent_add_component (icalcomp, icalcomponent_new_clone (icaltimezone_get_component (tzone)));

	return add || to_remove != NULL;
}

static void
source_changed_cb (ESource *source, ECalBackend *backend)
{
	const gchar *value;

	g_return_if_fail (source != NULL);
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	value = e_source_get_property (source, "custom-file");
	if (value && *value) {
		ECalBackendFile *cbfile;
		gboolean forced_readonly;

		cbfile = E_CAL_BACKEND_FILE (backend);
		g_return_if_fail (cbfile != NULL);

		value = e_source_get_property (source, "custom-file-readonly");
		forced_readonly = value && g_str_equal (value, "1");

		if ((forced_readonly != FALSE) != (cbfile->priv->read_only != FALSE)) {
			cbfile->priv->read_only = forced_readonly;
			if (!forced_readonly) {
				gchar *str_uri = get_uri_string (backend);

				g_return_if_fail (str_uri != NULL);

				cbfile->priv->read_only = g_access (str_uri, W_OK) != 0;

				g_free (str_uri);
			}

			e_cal_backend_notify_readonly (backend, cbfile->priv->read_only);
		}
	}
}

/* Open handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_open (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists,
			 const gchar *username, const gchar *password)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	gchar *str_uri;
	ECalBackendSyncStatus status;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;
        g_static_rec_mutex_lock (&priv->idle_save_rmutex);

	/* Claim a succesful open if we are already open */
	if (priv->path && priv->comp_uid_hash) {
		status = GNOME_Evolution_Calendar_Success;
		goto done;
        }

	str_uri = get_uri_string (E_CAL_BACKEND (backend));
	if (!str_uri) {
		status = GNOME_Evolution_Calendar_OtherError;
		goto done;
        }

	priv->read_only = FALSE;
	if (g_access (str_uri, R_OK) == 0) {
		status = open_cal (cbfile, str_uri);
		if (g_access (str_uri, W_OK) != 0)
			priv->read_only = TRUE;
	} else {
		if (only_if_exists)
			status = GNOME_Evolution_Calendar_NoSuchCal;
		else
			status = create_cal (cbfile, str_uri);
	}

	if (status == GNOME_Evolution_Calendar_Success) {
		if (!priv->read_only) {
			ESource *source = e_cal_backend_get_source (E_CAL_BACKEND (backend));

			if (source) {
				g_signal_connect (source, "changed", G_CALLBACK (source_changed_cb), backend);

				if (e_source_get_property (source, "custom-file-readonly") && g_str_equal (e_source_get_property (source, "custom-file-readonly"), "1"))
					priv->read_only = TRUE;
			}
		}

		if (priv->default_zone && add_timezone (priv->icalcomp, priv->default_zone)) {
			save (cbfile);
		}
	}

	g_free (str_uri);

  done:
        g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
	return status;
}

static ECalBackendSyncStatus
e_cal_backend_file_remove (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	gchar *str_uri = NULL, *dirname = NULL;
        gchar *full_path = NULL;
	const gchar *fname;
	GDir *dir = NULL;
	GError *error = NULL;
        ECalBackendSyncStatus status = GNOME_Evolution_Calendar_Success;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;
        g_static_rec_mutex_lock (&priv->idle_save_rmutex);

	str_uri = get_uri_string (E_CAL_BACKEND (backend));
	if (!str_uri) {
		status = GNOME_Evolution_Calendar_OtherError;
                goto done;
        }

	if (g_access (str_uri, W_OK) != 0) {
		status = GNOME_Evolution_Calendar_PermissionDenied;
                goto done;
	}

	/* remove all files in the directory */
	dirname = g_path_get_dirname (str_uri);
	dir = g_dir_open (dirname, 0, &error);
	if (!dir) {
		status = GNOME_Evolution_Calendar_PermissionDenied;
                goto done;
	}

	while ((fname = g_dir_read_name (dir))) {
		full_path = g_build_filename (dirname, fname, NULL);
		if (g_unlink (full_path) != 0) {
			status = GNOME_Evolution_Calendar_OtherError;
                        goto done;
		}

		g_free (full_path);
                full_path = NULL;
	}

	/* remove the directory itself */
	if (g_rmdir (dirname) != 0) {
		status = GNOME_Evolution_Calendar_OtherError;
        }

  done:
        if (dir) {
            g_dir_close (dir);
        }
	g_free (str_uri);
	g_free (dirname);
        g_free (full_path);

        g_static_rec_mutex_unlock (&priv->idle_save_rmutex);

	/* lie here a bit, but otherwise the calendar will not be removed, even it should */
	if (status != GNOME_Evolution_Calendar_Success)
		g_print (G_STRLOC ": %s", e_cal_backend_status_to_string (status));

        return GNOME_Evolution_Calendar_Success;
}

/* is_loaded handler for the file backend */
static gboolean
e_cal_backend_file_is_loaded (ECalBackend *backend)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	return (priv->icalcomp != NULL);
}

/* is_remote handler for the file backend */
static CalMode
e_cal_backend_file_get_mode (ECalBackend *backend)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	return CAL_MODE_LOCAL;
}

/* Set_mode handler for the file backend */
static void
e_cal_backend_file_set_mode (ECalBackend *backend, CalMode mode)
{
	e_cal_backend_notify_mode (backend,
				   GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED,
				   GNOME_Evolution_Calendar_MODE_LOCAL);

}

static ECalBackendSyncStatus
e_cal_backend_file_get_default_object (ECalBackendSync *backend, EDataCal *cal, gchar **object)
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
	case ICAL_VJOURNAL_COMPONENT:
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_JOURNAL);
		break;
	default:
		g_object_unref (comp);
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	*object = e_cal_component_get_as_string (comp);
	g_object_unref (comp);

	return GNOME_Evolution_Calendar_Success;
}

static void
add_detached_recur_to_vcalendar (gpointer key, gpointer value, gpointer user_data)
{
	ECalComponent *recurrence = value;
	icalcomponent *vcalendar = user_data;

	icalcomponent_add_component (
		vcalendar,
		icalcomponent_new_clone (e_cal_component_get_icalcomponent (recurrence)));
}

/* Get_object_component handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_get_object (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid, gchar **object)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	ECalBackendFileObject *obj_data;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);
	g_assert (priv->comp_uid_hash != NULL);

	g_static_rec_mutex_lock (&priv->idle_save_rmutex);

	obj_data = g_hash_table_lookup (priv->comp_uid_hash, uid);
	if (!obj_data) {
		g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	if (rid && *rid) {
		ECalComponent *comp;

		comp = g_hash_table_lookup (obj_data->recurrences, rid);
		if (comp) {
			*object = e_cal_component_get_as_string (comp);
		} else {
			icalcomponent *icalcomp;
			struct icaltimetype itt;

			if (!obj_data->full_object) {
				g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
				return GNOME_Evolution_Calendar_ObjectNotFound;
			}

			itt = icaltime_from_string (rid);
			icalcomp = e_cal_util_construct_instance (
				e_cal_component_get_icalcomponent (obj_data->full_object),
				itt);
			if (!icalcomp) {
				g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
				return GNOME_Evolution_Calendar_ObjectNotFound;
                        }

			*object = icalcomponent_as_ical_string_r (icalcomp);

			icalcomponent_free (icalcomp);
		}
	} else {
		if (g_hash_table_size (obj_data->recurrences) > 0) {
			icalcomponent *icalcomp;

			/* if we have detached recurrences, return a VCALENDAR */
			icalcomp = e_cal_util_new_top_level ();

			/* detached recurrences don't have full_object */
			if (obj_data->full_object)
				icalcomponent_add_component (
					icalcomp,
					icalcomponent_new_clone (e_cal_component_get_icalcomponent (obj_data->full_object)));

			/* add all detached recurrences */
			g_hash_table_foreach (obj_data->recurrences, (GHFunc) add_detached_recur_to_vcalendar, icalcomp);

			*object = icalcomponent_as_ical_string_r (icalcomp);

			icalcomponent_free (icalcomp);
		} else if (obj_data->full_object)
			*object = e_cal_component_get_as_string (obj_data->full_object);
	}

	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
	return GNOME_Evolution_Calendar_Success;
}

/* Add_timezone handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_add_timezone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj)
{
	icalcomponent *tz_comp;
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;

	cbfile = (ECalBackendFile *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_FILE (cbfile), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbfile->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	if (icalcomponent_isa (tz_comp) == ICAL_VTIMEZONE_COMPONENT) {
		icaltimezone *zone;

		zone = icaltimezone_new ();
		icaltimezone_set_component (zone, tz_comp);

		g_static_rec_mutex_lock (&priv->idle_save_rmutex);
		if (!icalcomponent_get_timezone (priv->icalcomp,
						 icaltimezone_get_tzid (zone))) {
			icalcomponent_add_component (priv->icalcomp, tz_comp);
			save (cbfile);
		}
		g_static_rec_mutex_unlock (&priv->idle_save_rmutex);

		icaltimezone_free (zone, 1);
	}

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_file_set_default_zone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj)
{
	icalcomponent *tz_comp;
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	icaltimezone *zone;

	cbfile = (ECalBackendFile *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_FILE (cbfile), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbfile->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);

	g_static_rec_mutex_lock (&priv->idle_save_rmutex);
	if (priv->default_zone != icaltimezone_get_utc_timezone ())
		icaltimezone_free (priv->default_zone, 1);

	/* Set the default timezone to it. */
	priv->default_zone = zone;
	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);

	return GNOME_Evolution_Calendar_Success;
}

typedef struct {
	GList *obj_list;
	gboolean search_needed;
	const gchar *query;
	ECalBackendSExp *obj_sexp;
	ECalBackend *backend;
	icaltimezone *default_zone;
} MatchObjectData;

static void
match_recurrence_sexp (gpointer key, gpointer value, gpointer data)
{
	ECalComponent *comp = value;
	MatchObjectData *match_data = data;

	if ((!match_data->search_needed) ||
	    (e_cal_backend_sexp_match_comp (match_data->obj_sexp, comp, match_data->backend))) {
		match_data->obj_list = g_list_append (match_data->obj_list,
						      e_cal_component_get_as_string (comp));
	}
}

static void
match_object_sexp (gpointer key, gpointer value, gpointer data)
{
	ECalBackendFileObject *obj_data = value;
	MatchObjectData *match_data = data;

	if (obj_data->full_object) {
		if ((!match_data->search_needed) ||
		    (e_cal_backend_sexp_match_comp (match_data->obj_sexp, obj_data->full_object, match_data->backend))) {
			match_data->obj_list = g_list_append (match_data->obj_list,
							      e_cal_component_get_as_string (obj_data->full_object));
		}
	}

	/* match also recurrences */
	g_hash_table_foreach (obj_data->recurrences,
			      (GHFunc) match_recurrence_sexp,
			      match_data);
}

/* Get_objects_in_range handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_get_object_list (ECalBackendSync *backend, EDataCal *cal, const gchar *sexp, GList **objects)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	MatchObjectData match_data;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	d(g_message (G_STRLOC ": Getting object list (%s)", sexp));

	match_data.search_needed = TRUE;
	match_data.query = sexp;
	match_data.obj_list = NULL;
	match_data.backend = E_CAL_BACKEND (backend);
	match_data.default_zone = priv->default_zone;

	if (!strcmp (sexp, "#t"))
		match_data.search_needed = FALSE;

	match_data.obj_sexp = e_cal_backend_sexp_new (sexp);
	if (!match_data.obj_sexp)
		return GNOME_Evolution_Calendar_InvalidQuery;

	g_static_rec_mutex_lock (&priv->idle_save_rmutex);
	g_hash_table_foreach (priv->comp_uid_hash, (GHFunc) match_object_sexp, &match_data);
	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);

	*objects = match_data.obj_list;

	g_object_unref (match_data.obj_sexp);

	return GNOME_Evolution_Calendar_Success;
}

/* Gets the list of attachments */
static ECalBackendSyncStatus
e_cal_backend_file_get_attachment_list (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid, GSList **list)
{

	/* TODO implement the function */
	return GNOME_Evolution_Calendar_Success;
}

/* get_query handler for the file backend */
static void
e_cal_backend_file_start_query (ECalBackend *backend, EDataCalView *query)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	MatchObjectData match_data;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	d(g_message (G_STRLOC ": Starting query (%s)", e_data_cal_view_get_text (query)));

	/* try to match all currently existing objects */
	match_data.search_needed = TRUE;
	match_data.query = e_data_cal_view_get_text (query);
	match_data.obj_list = NULL;
	match_data.backend = backend;
	match_data.default_zone = priv->default_zone;

	if (!strcmp (match_data.query, "#t"))
		match_data.search_needed = FALSE;

	match_data.obj_sexp = e_data_cal_view_get_object_sexp (query);
	if (!match_data.obj_sexp) {
		e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_InvalidQuery);
		return;
	}

	g_static_rec_mutex_lock (&priv->idle_save_rmutex);
	g_hash_table_foreach (priv->comp_uid_hash, (GHFunc) match_object_sexp, &match_data);
	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);

	/* notify listeners of all objects */
	if (match_data.obj_list) {
		e_data_cal_view_notify_objects_added (query, (const GList *) match_data.obj_list);

		/* free memory */
		g_list_foreach (match_data.obj_list, (GFunc) g_free, NULL);
		g_list_free (match_data.obj_list);
	}
	g_object_unref (match_data.obj_sexp);

	e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_Success);
}

static gboolean
free_busy_instance (ECalComponent *comp,
		    time_t        instance_start,
		    time_t        instance_end,
		    gpointer      data)
{
	icalcomponent *vfb = data;
	icalproperty *prop;
	icalparameter *param;
	struct icalperiodtype ipt;
	icaltimezone *utc_zone;

	utc_zone = icaltimezone_get_utc_timezone ();

	ipt.start = icaltime_from_timet_with_zone (instance_start, FALSE, utc_zone);
	ipt.end = icaltime_from_timet_with_zone (instance_end, FALSE, utc_zone);
	ipt.duration = icaldurationtype_null_duration ();

        /* add busy information to the vfb component */
	prop = icalproperty_new (ICAL_FREEBUSY_PROPERTY);
	icalproperty_set_freebusy (prop, ipt);

	param = icalparameter_new_fbtype (ICAL_FBTYPE_BUSY);
	icalproperty_add_parameter (prop, param);

	icalcomponent_add_property (vfb, prop);

	return TRUE;
}

static icalcomponent *
create_user_free_busy (ECalBackendFile *cbfile, const gchar *address, const gchar *cn,
		       time_t start, time_t end)
{
	ECalBackendFilePrivate *priv;
	GList *l;
	icalcomponent *vfb;
	icaltimezone *utc_zone;
	ECalBackendSExp *obj_sexp;
	gchar *query, *iso_start, *iso_end;

	priv = cbfile->priv;

	/* create the (unique) VFREEBUSY object that we'll return */
	vfb = icalcomponent_new_vfreebusy ();
	if (address != NULL) {
		icalproperty *prop;
		icalparameter *param;

		prop = icalproperty_new_organizer (address);
		if (prop != NULL && cn != NULL) {
			param = icalparameter_new_cn (cn);
			icalproperty_add_parameter (prop, param);
		}
		if (prop != NULL)
			icalcomponent_add_property (vfb, prop);
	}
	utc_zone = icaltimezone_get_utc_timezone ();
	icalcomponent_set_dtstart (vfb, icaltime_from_timet_with_zone (start, FALSE, utc_zone));
	icalcomponent_set_dtend (vfb, icaltime_from_timet_with_zone (end, FALSE, utc_zone));

	/* add all objects in the given interval */
	iso_start = isodate_from_time_t (start);
	iso_end = isodate_from_time_t (end);
	query = g_strdup_printf ("occur-in-time-range? (make-time \"%s\") (make-time \"%s\")",
				 iso_start, iso_end);
	obj_sexp = e_cal_backend_sexp_new (query);
	g_free (query);
	g_free (iso_start);
	g_free (iso_end);

	if (!obj_sexp)
		return vfb;

	for (l = priv->comp; l; l = l->next) {
		ECalComponent *comp = l->data;
		icalcomponent *icalcomp, *vcalendar_comp;
		icalproperty *prop;

		icalcomp = e_cal_component_get_icalcomponent (comp);
		if (!icalcomp)
			continue;

		/* If the event is TRANSPARENT, skip it. */
		prop = icalcomponent_get_first_property (icalcomp,
							 ICAL_TRANSP_PROPERTY);
		if (prop) {
			icalproperty_transp transp_val = icalproperty_get_transp (prop);
			if (transp_val == ICAL_TRANSP_TRANSPARENT ||
			    transp_val == ICAL_TRANSP_TRANSPARENTNOCONFLICT)
				continue;
		}

		if (!e_cal_backend_sexp_match_comp (obj_sexp, l->data, E_CAL_BACKEND (cbfile)))
			continue;

		vcalendar_comp = icalcomponent_get_parent (icalcomp);
		e_cal_recur_generate_instances (comp, start, end,
						free_busy_instance,
						vfb,
						resolve_tzid,
						vcalendar_comp,
						priv->default_zone);
	}
	g_object_unref (obj_sexp);

	return vfb;
}

/* Get_free_busy handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_get_free_busy (ECalBackendSync *backend, EDataCal *cal, GList *users,
				time_t start, time_t end, GList **freebusy)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	gchar *address, *name;
	icalcomponent *vfb;
	gchar *calobj;
	GList *l;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_NoSuchCal);
	g_return_val_if_fail (start != -1 && end != -1, GNOME_Evolution_Calendar_InvalidRange);
	g_return_val_if_fail (start <= end, GNOME_Evolution_Calendar_InvalidRange);

	g_static_rec_mutex_lock (&priv->idle_save_rmutex);

	*freebusy = NULL;

	if (users == NULL) {
		if (e_cal_backend_mail_account_get_default (&address, &name)) {
			vfb = create_user_free_busy (cbfile, address, name, start, end);
			calobj = icalcomponent_as_ical_string_r (vfb);
			*freebusy = g_list_append (*freebusy, calobj);
			icalcomponent_free (vfb);
			g_free (address);
			g_free (name);
		}
	} else {
		for (l = users; l != NULL; l = l->next ) {
			address = l->data;
			if (e_cal_backend_mail_account_is_valid (address, &name)) {
				vfb = create_user_free_busy (cbfile, address, name, start, end);
				calobj = icalcomponent_as_ical_string_r (vfb);
				*freebusy = g_list_append (*freebusy, calobj);
				icalcomponent_free (vfb);
				g_free (name);
			}
		}
	}

	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);

	return GNOME_Evolution_Calendar_Success;
}

typedef struct
{
	ECalBackendFile *backend;
	icalcomponent_kind kind;
	GList *deletes;
	EXmlHash *ehash;
} ECalBackendFileComputeChangesData;

static gboolean
e_cal_backend_file_compute_changes_foreach_key (const gchar *key, gpointer value, gpointer data)
{
	ECalBackendFileComputeChangesData *be_data = data;

	if (!lookup_component (be_data->backend, key)) {
		ECalComponent *comp;

		comp = e_cal_component_new ();
		if (be_data->kind == ICAL_VTODO_COMPONENT)
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
		else
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);

		e_cal_component_set_uid (comp, key);
		be_data->deletes = g_list_prepend (be_data->deletes, e_cal_component_get_as_string (comp));

		g_object_unref (comp);
		return TRUE;
	}
	return FALSE;
}

static ECalBackendSyncStatus
e_cal_backend_file_compute_changes (ECalBackendFile *cbfile, const gchar *change_id,
				    GList **adds, GList **modifies, GList **deletes)
{
	ECalBackendFilePrivate *priv;
	gchar    *filename;
	EXmlHash *ehash;
	ECalBackendFileComputeChangesData be_data;
	GList *i;
	gchar *unescaped_uri;

	priv = cbfile->priv;

	/* FIXME Will this always work? */
	unescaped_uri = g_uri_unescape_string (priv->path, "");
	filename = g_strdup_printf ("%s-%s.db", unescaped_uri, change_id);
	g_free (unescaped_uri);
	if (!(ehash = e_xmlhash_new (filename))) {
		g_free (filename);
		return GNOME_Evolution_Calendar_OtherError;
	}

	g_free (filename);

	g_static_rec_mutex_lock (&priv->idle_save_rmutex);

	/* Calculate adds and modifies */
	for (i = priv->comp; i != NULL; i = i->next) {
		const gchar *uid;
		gchar *calobj;

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
	be_data.backend = cbfile;
	be_data.kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbfile));
	be_data.deletes = NULL;
	be_data.ehash = ehash;

	e_xmlhash_foreach_key_remove (ehash, (EXmlHashRemoveFunc)e_cal_backend_file_compute_changes_foreach_key, &be_data);

	*deletes = be_data.deletes;

	e_xmlhash_write (ehash);
	e_xmlhash_destroy (ehash);

	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
	return GNOME_Evolution_Calendar_Success;
}

/* Get_changes handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_get_changes (ECalBackendSync *backend, EDataCal *cal, const gchar *change_id,
			      GList **adds, GList **modifies, GList **deletes)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_NoSuchCal);
	g_return_val_if_fail (change_id != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	return e_cal_backend_file_compute_changes (cbfile, change_id, adds, modifies, deletes);
}

/* Discard_alarm handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *auid)
{
	/* we just do nothing with the alarm */
	return GNOME_Evolution_Calendar_Success;
}

static icaltimezone *
e_cal_backend_file_internal_get_default_timezone (ECalBackend *backend)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, NULL);

	return priv->default_zone;
}

static icaltimezone *
e_cal_backend_file_internal_get_timezone (ECalBackend *backend, const gchar *tzid)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	icaltimezone *zone;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, NULL);

	g_static_rec_mutex_lock (&priv->idle_save_rmutex);

	if (!strcmp (tzid, "UTC"))
		zone = icaltimezone_get_utc_timezone ();
	else {
		zone = icalcomponent_get_timezone (priv->icalcomp, tzid);

		if (!zone && E_CAL_BACKEND_CLASS (parent_class)->internal_get_timezone)
			zone = E_CAL_BACKEND_CLASS (parent_class)->internal_get_timezone (backend, tzid);
	}

	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
	return zone;
}

static void
sanitize_component (ECalBackendFile *cbfile, ECalComponent *comp)
{
	ECalComponentDateTime dt;
	icaltimezone *zone, *default_zone;

	/* Check dtstart, dtend and due's timezone, and convert it to local
	 * default timezone if the timezone is not in our builtin timezone
	 * list */
	e_cal_component_get_dtstart (comp, &dt);
	if (dt.value && dt.tzid) {
		zone = e_cal_backend_file_internal_get_timezone ((ECalBackend *)cbfile, dt.tzid);
		if (!zone) {
			default_zone = e_cal_backend_file_internal_get_default_timezone ((ECalBackend *)cbfile);
			g_free ((gchar *)dt.tzid);
			dt.tzid = g_strdup (icaltimezone_get_tzid (default_zone));
			e_cal_component_set_dtstart (comp, &dt);
		}
	}
	e_cal_component_free_datetime (&dt);

	e_cal_component_get_dtend (comp, &dt);
	if (dt.value && dt.tzid) {
		zone = e_cal_backend_file_internal_get_timezone ((ECalBackend *)cbfile, dt.tzid);
		if (!zone) {
			default_zone = e_cal_backend_file_internal_get_default_timezone ((ECalBackend *)cbfile);
			g_free ((gchar *)dt.tzid);
			dt.tzid = g_strdup (icaltimezone_get_tzid (default_zone));
			e_cal_component_set_dtend (comp, &dt);
		}
	}
	e_cal_component_free_datetime (&dt);

	e_cal_component_get_due (comp, &dt);
	if (dt.value && dt.tzid) {
		zone = e_cal_backend_file_internal_get_timezone ((ECalBackend *)cbfile, dt.tzid);
		if (!zone) {
			default_zone = e_cal_backend_file_internal_get_default_timezone ((ECalBackend *)cbfile);
			g_free ((gchar *)dt.tzid);
			dt.tzid = g_strdup (icaltimezone_get_tzid (default_zone));
			e_cal_component_set_due (comp, &dt);
		}
	}
	e_cal_component_free_datetime (&dt);
	e_cal_component_abort_sequence (comp);

}

static ECalBackendSyncStatus
e_cal_backend_file_create_object (ECalBackendSync *backend, EDataCal *cal, gchar **calobj, gchar **uid)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	icalcomponent *icalcomp;
	ECalComponent *comp;
	const gchar *comp_uid;
	struct icaltimetype current;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_NoSuchCal);
	g_return_val_if_fail (*calobj != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	/* Parse the icalendar text */
	icalcomp = icalparser_parse_string (*calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	/* Check kind with the parent */
	if (icalcomponent_isa (icalcomp) != e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	g_static_rec_mutex_lock (&priv->idle_save_rmutex);

	/* Get the UID */
	comp_uid = icalcomponent_get_uid (icalcomp);
	if (!comp_uid) {
		gchar *new_uid;

		new_uid = e_cal_component_gen_uid ();
		if (!new_uid) {
			icalcomponent_free (icalcomp);
			g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
			return GNOME_Evolution_Calendar_InvalidObject;
		}

		icalcomponent_set_uid (icalcomp, new_uid);
		comp_uid = icalcomponent_get_uid (icalcomp);

		g_free (new_uid);
	}

	/* check the object is not in our cache */
	if (lookup_component (cbfile, comp_uid)) {
		icalcomponent_free (icalcomp);
		g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
		return GNOME_Evolution_Calendar_ObjectIdAlreadyExists;
	}

	/* Create the cal component */
	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);

	/* Set the created and last modified times on the component */
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_created (comp, &current);
	e_cal_component_set_last_modified (comp, &current);

	/* sanitize the component*/
	sanitize_component (cbfile, comp);

	/* Add the object */
	add_component (cbfile, comp, TRUE);

	/* Save the file */
	save (cbfile);

	/* Return the UID and the modified component */
	if (uid)
		*uid = g_strdup (comp_uid);
	*calobj = e_cal_component_get_as_string (comp);

	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
	return GNOME_Evolution_Calendar_Success;
}

typedef struct {
	ECalBackendFile *cbfile;
	ECalBackendFileObject *obj_data;
	const gchar *rid;
	CalObjModType mod;
} RemoveRecurrenceData;

static gboolean
remove_object_instance_cb (gpointer key, gpointer value, gpointer user_data)
{
	time_t fromtt, instancett;
	ECalComponent *instance = value;
	RemoveRecurrenceData *rrdata = user_data;

	fromtt = icaltime_as_timet (icaltime_from_string (rrdata->rid));
	instancett = icaltime_as_timet (get_rid_icaltime (instance));

	if (fromtt > 0 && instancett > 0) {
		if ((rrdata->mod == CALOBJ_MOD_THISANDPRIOR && instancett <= fromtt) ||
		    (rrdata->mod == CALOBJ_MOD_THISANDFUTURE && instancett >= fromtt)) {
			/* remove the component from our data */
			icalcomponent_remove_component (rrdata->cbfile->priv->icalcomp,
							e_cal_component_get_icalcomponent (instance));
			rrdata->cbfile->priv->comp = g_list_remove (rrdata->cbfile->priv->comp, instance);

			rrdata->obj_data->recurrences_list = g_list_remove (rrdata->obj_data->recurrences_list, instance);

			return TRUE;
		}
	}

	return FALSE;
}

static ECalBackendSyncStatus
e_cal_backend_file_modify_object (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj,
				  CalObjModType mod, gchar **old_object, gchar **new_object)
{
	RemoveRecurrenceData rrdata;
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	icalcomponent *icalcomp;
	const gchar *comp_uid;
	gchar *rid = NULL;
	gchar *real_rid;
	ECalComponent *comp, *recurrence;
	ECalBackendFileObject *obj_data;
	struct icaltimetype current;
	GList *detached = NULL;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_NoSuchCal);
	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	/* Parse the icalendar text */
	icalcomp = icalparser_parse_string ((gchar *) calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	/* Check kind with the parent */
	if (icalcomponent_isa (icalcomp) != e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	g_static_rec_mutex_lock (&priv->idle_save_rmutex);

	/* Get the uid */
	comp_uid = icalcomponent_get_uid (icalcomp);

	/* Get the object from our cache */
	if (!(obj_data = g_hash_table_lookup (priv->comp_uid_hash, comp_uid))) {
		icalcomponent_free (icalcomp);
		g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	/* Create the cal component */
	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);

	/* Set the last modified time on the component */
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_last_modified (comp, &current);

	/* sanitize the component*/
	sanitize_component (cbfile, comp);
	rid = e_cal_component_get_recurid_as_string (comp);

	/* handle mod_type */
	switch (mod) {
	case CALOBJ_MOD_THIS :
		if (!rid || !*rid) {
			if (old_object && obj_data->full_object)
				*old_object = e_cal_component_get_as_string (obj_data->full_object);

			/* replace only the full object */
			if (obj_data->full_object) {
				icalcomponent_remove_component (priv->icalcomp,
							e_cal_component_get_icalcomponent (obj_data->full_object));
				priv->comp = g_list_remove (priv->comp, obj_data->full_object);

				g_object_unref (obj_data->full_object);
			}

			/* add the new object */
			obj_data->full_object = comp;

			icalcomponent_add_component (priv->icalcomp,
						     e_cal_component_get_icalcomponent (obj_data->full_object));
			priv->comp = g_list_prepend (priv->comp, obj_data->full_object);

			save (cbfile);

			if (new_object)
				*new_object = e_cal_component_get_as_string (comp);

			g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
			g_free (rid);
			return GNOME_Evolution_Calendar_Success;
		}

		if (g_hash_table_lookup_extended (obj_data->recurrences, rid, (gpointer *)&real_rid, (gpointer *)&recurrence)) {
			if (old_object)
				*old_object = e_cal_component_get_as_string (recurrence);

			/* remove the component from our data */
			icalcomponent_remove_component (priv->icalcomp,
							e_cal_component_get_icalcomponent (recurrence));
			priv->comp = g_list_remove (priv->comp, recurrence);
			obj_data->recurrences_list = g_list_remove (obj_data->recurrences_list, recurrence);
			g_hash_table_remove (obj_data->recurrences, rid);
		}

		/* add the detached instance */
		g_hash_table_insert (obj_data->recurrences,
				     rid,
				     comp);
		icalcomponent_add_component (priv->icalcomp,
					     e_cal_component_get_icalcomponent (comp));
		priv->comp = g_list_append (priv->comp, comp);
		obj_data->recurrences_list = g_list_append (obj_data->recurrences_list, comp);
		rid = NULL;
		break;
	case CALOBJ_MOD_THISANDPRIOR :
	case CALOBJ_MOD_THISANDFUTURE :
		if (!rid || !*rid) {
			if (old_object && obj_data->full_object)
				*old_object = e_cal_component_get_as_string (obj_data->full_object);

			remove_component (cbfile, comp_uid, obj_data);

			/* Add the new object */
			add_component (cbfile, comp, TRUE);
			g_free (rid);
			rid = NULL;
			break;
		}

		/* remove the component from our data, temporarily */
		if (obj_data->full_object) {
			icalcomponent_remove_component (priv->icalcomp,
						e_cal_component_get_icalcomponent (obj_data->full_object));
			priv->comp = g_list_remove (priv->comp, obj_data->full_object);
		}

		/* now deal with the detached recurrence */
		if (g_hash_table_lookup_extended (obj_data->recurrences, rid,
						  (gpointer *)&real_rid, (gpointer *)&recurrence)) {
			if (old_object)
				*old_object = e_cal_component_get_as_string (recurrence);

			/* remove the component from our data */
			icalcomponent_remove_component (priv->icalcomp,
							e_cal_component_get_icalcomponent (recurrence));
			priv->comp = g_list_remove (priv->comp, recurrence);
			obj_data->recurrences_list = g_list_remove (obj_data->recurrences_list, recurrence);
			g_hash_table_remove (obj_data->recurrences, rid);
		} else {
			if (old_object && obj_data->full_object)
				*old_object = e_cal_component_get_as_string (obj_data->full_object);
		}

		rrdata.cbfile = cbfile;
		rrdata.obj_data = obj_data;
		rrdata.rid = rid;
		rrdata.mod = mod;
		g_hash_table_foreach_remove (obj_data->recurrences, (GHRFunc) remove_object_instance_cb, &rrdata);

		/* add the modified object to the beginning of the list,
		   so that it's always before any detached instance we
		   might have */
		if (obj_data->full_object) {
			icalcomponent_add_component (priv->icalcomp,
					     e_cal_component_get_icalcomponent (obj_data->full_object));
			priv->comp = g_list_prepend (priv->comp, obj_data->full_object);
		}

		/* add the new detached recurrence */
		g_hash_table_insert (obj_data->recurrences,
				     rid,
				     comp);
		icalcomponent_add_component (priv->icalcomp,
					     e_cal_component_get_icalcomponent (comp));
		priv->comp = g_list_append (priv->comp, comp);
		obj_data->recurrences_list = g_list_append (obj_data->recurrences_list, comp);
		rid = NULL;
		break;
	case CALOBJ_MOD_ALL :
		/* Remove the old version */
		if (old_object && obj_data->full_object)
			*old_object = e_cal_component_get_as_string (obj_data->full_object);

		if (obj_data->recurrences_list) {
			/* has detached components, preserve them */
			GList *l;

			for (l = obj_data->recurrences_list; l; l = l->next) {
				detached = g_list_prepend (detached, g_object_ref (l->data));
			}
		}

		remove_component (cbfile, comp_uid, obj_data);

		/* Add the new object */
		add_component (cbfile, comp, TRUE);

		if (detached) {
			/* it had some detached components, place them back */
			comp_uid = icalcomponent_get_uid (e_cal_component_get_icalcomponent (comp));

			if ((obj_data = g_hash_table_lookup (priv->comp_uid_hash, comp_uid)) != NULL) {
				GList *l;

				for (l = detached; l; l = l->next) {
					ECalComponent *c = l->data;

					g_hash_table_insert (obj_data->recurrences, e_cal_component_get_recurid_as_string (c), c);
					icalcomponent_add_component (priv->icalcomp, e_cal_component_get_icalcomponent (c));
					priv->comp = g_list_append (priv->comp, c);
					obj_data->recurrences_list = g_list_append (obj_data->recurrences_list, c);
				}
			}

			g_list_free (detached);
		}
		break;
	}

	save (cbfile);
	g_free (rid);

	if (new_object)
		*new_object = e_cal_component_get_as_string (comp);

	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
	return GNOME_Evolution_Calendar_Success;
}

static void
remove_instance (ECalBackendFile *cbfile, ECalBackendFileObject *obj_data, const gchar *rid)
{
	gchar *hash_rid;
	ECalComponent *comp;
	struct icaltimetype current;

	if (!rid || !*rid)
		return;

	if (g_hash_table_lookup_extended (obj_data->recurrences, rid, (gpointer *)&hash_rid, (gpointer *)&comp)) {
		/* remove the component from our data */
		icalcomponent_remove_component (cbfile->priv->icalcomp,
						e_cal_component_get_icalcomponent (comp));
		cbfile->priv->comp = g_list_remove (cbfile->priv->comp, comp);
		obj_data->recurrences_list = g_list_remove (obj_data->recurrences_list, comp);
		g_hash_table_remove (obj_data->recurrences, rid);
	}

	if (!obj_data->full_object)
		return;

	/* remove the component from our data, temporarily */
	icalcomponent_remove_component (cbfile->priv->icalcomp,
					e_cal_component_get_icalcomponent (obj_data->full_object));
	cbfile->priv->comp = g_list_remove (cbfile->priv->comp, obj_data->full_object);

	e_cal_util_remove_instances (e_cal_component_get_icalcomponent (obj_data->full_object),
				     icaltime_from_string (rid), CALOBJ_MOD_THIS);

	/* Since we are only removing one instance of recurrence
	   event, update the last modified time on the component */
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_last_modified (obj_data->full_object, &current);

	/* add the modified object to the beginning of the list,
	   so that it's always before any detached instance we
	   might have */
	icalcomponent_add_component (cbfile->priv->icalcomp,
				     e_cal_component_get_icalcomponent (obj_data->full_object));
	cbfile->priv->comp = g_list_prepend (cbfile->priv->comp, obj_data->full_object);
}

static gchar *
get_object_string_from_fileobject (ECalBackendFileObject *obj_data, const gchar *rid)
{
	ECalComponent *comp = obj_data->full_object;
	gchar *real_rid;

	if (!comp)
		return NULL;

	if (!rid) {
		return e_cal_component_get_as_string (comp);
	} else {
		if (g_hash_table_lookup_extended (obj_data->recurrences, rid, (gpointer *)&real_rid, (gpointer *)&comp))
			return e_cal_component_get_as_string (comp);
		else {
			/* FIXME remove this once we delete an instance from master object through
			   modify request by setting exception */
			return e_cal_component_get_as_string (comp);
		}
	}

	return NULL;
}

/* Remove_object handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_remove_object (ECalBackendSync *backend, EDataCal *cal,
				  const gchar *uid, const gchar *rid,
				  CalObjModType mod, gchar **old_object,
				  gchar **object)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	ECalBackendFileObject *obj_data;
	ECalComponent *comp;
	RemoveRecurrenceData rrdata;
	const gchar *recur_id = NULL;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_NoSuchCal);
	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	*old_object = *object = NULL;

	g_static_rec_mutex_lock (&priv->idle_save_rmutex);

	obj_data = g_hash_table_lookup (priv->comp_uid_hash, uid);
	if (!obj_data) {
		g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	if (rid && *rid)
		recur_id = rid;

	comp = obj_data->full_object;

	switch (mod) {
	case CALOBJ_MOD_ALL :
		*old_object = get_object_string_from_fileobject (obj_data, recur_id);
		remove_component (cbfile, uid, obj_data);

		*object = NULL;
		break;
	case CALOBJ_MOD_THIS :
		if (!recur_id) {
			*old_object = get_object_string_from_fileobject (obj_data, recur_id);
			remove_component (cbfile, uid, obj_data);
			*object = NULL;
		} else {
			*old_object = get_object_string_from_fileobject (obj_data, recur_id);

			remove_instance (cbfile, obj_data, recur_id);
			if (comp)
				*object = e_cal_component_get_as_string (comp);
		}
		break;
	case CALOBJ_MOD_THISANDPRIOR :
	case CALOBJ_MOD_THISANDFUTURE :
		if (!recur_id || !*recur_id) {
			g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
			return GNOME_Evolution_Calendar_ObjectNotFound;
		}

		if (comp) {
			*old_object = e_cal_component_get_as_string (comp);

			/* remove the component from our data, temporarily */
			icalcomponent_remove_component (priv->icalcomp,
						e_cal_component_get_icalcomponent (comp));
			priv->comp = g_list_remove (priv->comp, comp);

			e_cal_util_remove_instances (e_cal_component_get_icalcomponent (comp),
					     icaltime_from_string (recur_id), mod);
		}

		/* now remove all detached instances */
		rrdata.cbfile = cbfile;
		rrdata.obj_data = obj_data;
		rrdata.rid = recur_id;
		rrdata.mod = mod;
		g_hash_table_foreach_remove (obj_data->recurrences, (GHRFunc) remove_object_instance_cb, &rrdata);

		/* add the modified object to the beginning of the list,
		   so that it's always before any detached instance we
		   might have */
		if (comp)
			priv->comp = g_list_prepend (priv->comp, comp);

		if (obj_data->full_object)
			*object = e_cal_component_get_as_string (obj_data->full_object);
		break;
	}

	save (cbfile);

	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
	return GNOME_Evolution_Calendar_Success;
}

static gboolean
cancel_received_object (ECalBackendFile *cbfile, icalcomponent *icalcomp, gchar **old_object, gchar **new_object)
{
	ECalBackendFileObject *obj_data;
	ECalBackendFilePrivate *priv;
	gchar *rid;
	ECalComponent *comp;

	priv = cbfile->priv;

	*old_object = NULL;
	*new_object = NULL;

	/* Find the old version of the component. */
	obj_data = g_hash_table_lookup (priv->comp_uid_hash, icalcomponent_get_uid (icalcomp));
	if (!obj_data)
		return FALSE;

	/* And remove it */
	comp = e_cal_component_new ();
	if (!e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp))) {
		g_object_unref (comp);
		return FALSE;
	}

	if (obj_data->full_object)
		*old_object = e_cal_component_get_as_string (obj_data->full_object);

	/* new_object is kept NULL if not removing the instance */
	rid = e_cal_component_get_recurid_as_string (comp);
	if (rid && *rid) {
		remove_instance (cbfile, obj_data, rid);
		if (obj_data->full_object)
			*new_object = e_cal_component_get_as_string (obj_data->full_object);
	} else
		remove_component (cbfile, icalcomponent_get_uid (icalcomp), obj_data);

	g_free (rid);

	return TRUE;
}

typedef struct {
	GHashTable *zones;

	gboolean found;
} ECalBackendFileTzidData;

static void
check_tzids (icalparameter *param, gpointer data)
{
	ECalBackendFileTzidData *tzdata = data;
	const gchar *tzid;

	tzid = icalparameter_get_tzid (param);
	if (!tzid || g_hash_table_lookup (tzdata->zones, tzid))
		tzdata->found = FALSE;
}

/* This function is largely duplicated in
 * ../groupwise/e-cal-backend-groupwise.c
 */
static void
fetch_attachments (ECalBackendSync *backend, ECalComponent *comp)
{
	GSList *attach_list = NULL, *new_attach_list = NULL;
	GSList *l;
	gchar  *attach_store;
	gchar *dest_url, *dest_file;
	gint fd;
	const gchar *uid;

	e_cal_component_get_attachment_list (comp, &attach_list);
	e_cal_component_get_uid (comp, &uid);
	/*FIXME  get the uri rather than computing the path */
	attach_store = g_build_filename (g_get_home_dir (),
			".evolution/calendar/local/system", NULL);

	for (l = attach_list; l; l = l->next) {
		gchar *sfname = (gchar *)l->data;
		gchar *filename, *new_filename;
		GMappedFile *mapped_file;
		GError *error = NULL;

		mapped_file = g_mapped_file_new (sfname, FALSE, &error);
		if (!mapped_file) {
			g_message ("DEBUG: could not map %s: %s\n",
				   sfname, error->message);
			g_error_free (error);
			continue;
		}
		filename = g_path_get_basename (sfname);
		new_filename = g_strconcat (uid, "-", filename, NULL);
		g_free (filename);
		dest_file = g_build_filename (attach_store, new_filename, NULL);
		g_free (new_filename);
		fd = g_open (dest_file, O_RDWR|O_CREAT|O_TRUNC|O_BINARY, 0600);
		if (fd == -1) {
			/* TODO handle error conditions */
			g_message ("DEBUG: could not open %s for writing\n",
				   dest_file);
		} else if (write (fd, g_mapped_file_get_contents (mapped_file),
				  g_mapped_file_get_length (mapped_file)) == -1) {
			/* TODO handle error condition */
			g_message ("DEBUG: attachment write failed.\n");
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
	g_free (attach_store);
	e_cal_component_set_attachment_list (comp, new_attach_list);
}

/* Update_objects handler for the file backend. */
static ECalBackendSyncStatus
e_cal_backend_file_receive_objects (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	icalcomponent *toplevel_comp, *icalcomp = NULL;
	icalcomponent_kind kind;
	icalproperty_method toplevel_method, method;
	icalcomponent *subcomp;
	GList *comps, *del_comps, *l;
	ECalComponent *comp;
	struct icaltimetype current;
	ECalBackendFileTzidData tzdata;
	ECalBackendSyncStatus status = GNOME_Evolution_Calendar_Success;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

	/* Pull the component from the string and ensure that it is sane */
	toplevel_comp = icalparser_parse_string ((gchar *) calobj);
	if (!toplevel_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	g_static_rec_mutex_lock (&priv->idle_save_rmutex);

	kind = icalcomponent_isa (toplevel_comp);
	if (kind != ICAL_VCALENDAR_COMPONENT) {
		/* If its not a VCALENDAR, make it one to simplify below */
		icalcomp = toplevel_comp;
		toplevel_comp = e_cal_util_new_top_level ();
		if (icalcomponent_get_method (icalcomp) == ICAL_METHOD_CANCEL)
			icalcomponent_set_method (toplevel_comp, ICAL_METHOD_CANCEL);
		else
			icalcomponent_set_method (toplevel_comp, ICAL_METHOD_PUBLISH);
		icalcomponent_add_component (toplevel_comp, icalcomp);
	} else {
		if (!icalcomponent_get_first_property (toplevel_comp, ICAL_METHOD_PROPERTY))
			icalcomponent_set_method (toplevel_comp, ICAL_METHOD_PUBLISH);
	}

	toplevel_method = icalcomponent_get_method (toplevel_comp);

	/* Build a list of timezones so we can make sure all the objects have valid info */
	tzdata.zones = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	subcomp = icalcomponent_get_first_component (toplevel_comp, ICAL_VTIMEZONE_COMPONENT);
	while (subcomp) {
		icaltimezone *zone;

		zone = icaltimezone_new ();
		if (icaltimezone_set_component (zone, subcomp))
			g_hash_table_insert (tzdata.zones, g_strdup (icaltimezone_get_tzid (zone)), NULL);

		subcomp = icalcomponent_get_next_component (toplevel_comp, ICAL_VTIMEZONE_COMPONENT);
	}

	/* First we make sure all the components are usuable */
	comps = del_comps = NULL;
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));

	subcomp = icalcomponent_get_first_component (toplevel_comp, ICAL_ANY_COMPONENT);
	while (subcomp) {
		icalcomponent_kind child_kind = icalcomponent_isa (subcomp);

		if (child_kind != kind) {
			/* remove the component from the toplevel VCALENDAR */
			if (child_kind != ICAL_VTIMEZONE_COMPONENT)
				del_comps = g_list_prepend (del_comps, subcomp);

			subcomp = icalcomponent_get_next_component (toplevel_comp, ICAL_ANY_COMPONENT);
			continue;
		}

		tzdata.found = TRUE;
		icalcomponent_foreach_tzid (subcomp, check_tzids, &tzdata);

		if (!tzdata.found) {
			status = GNOME_Evolution_Calendar_InvalidObject;
			goto error;
		}

		if (!icalcomponent_get_uid (subcomp)) {
			if (toplevel_method == ICAL_METHOD_PUBLISH) {

				gchar *new_uid = NULL;

				new_uid = e_cal_component_gen_uid ();
				icalcomponent_set_uid (subcomp, new_uid);
				g_free (new_uid);
			} else {
				status = GNOME_Evolution_Calendar_InvalidObject;
				goto error;
			}

		}

		comps = g_list_prepend (comps, subcomp);
		subcomp = icalcomponent_get_next_component (toplevel_comp, ICAL_ANY_COMPONENT);
	}

	/* Now we manipulate the components we care about */
	for (l = comps; l; l = l->next) {
		const gchar *uid;
		gchar *object, *old_object = NULL, *rid, *new_object;
		ECalBackendFileObject *obj_data;
		gboolean is_declined;

		subcomp = l->data;

		/* Create the cal component */
		comp = e_cal_component_new ();
		e_cal_component_set_icalcomponent (comp, subcomp);

		/* Set the created and last modified times on the component */
		current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
		e_cal_component_set_created (comp, &current);
		e_cal_component_set_last_modified (comp, &current);

		e_cal_component_get_uid (comp, &uid);
		rid = e_cal_component_get_recurid_as_string (comp);

		if (icalcomponent_get_first_property (subcomp, ICAL_METHOD_PROPERTY))
			method = icalcomponent_get_method (subcomp);
		else
			method = toplevel_method;

		switch (method) {
		case ICAL_METHOD_PUBLISH:
		case ICAL_METHOD_REQUEST:
		case ICAL_METHOD_REPLY:
			is_declined = e_cal_backend_user_declined (subcomp);

			/* handle attachments */
			if (!is_declined && e_cal_component_has_attachments (comp))
				fetch_attachments (backend, comp);
			obj_data = g_hash_table_lookup (priv->comp_uid_hash, uid);
			if (obj_data) {
				if (obj_data->full_object)
					old_object = e_cal_component_get_as_string (obj_data->full_object);
				if (rid)
					remove_instance (cbfile, obj_data, rid);
				else
					remove_component (cbfile, uid, obj_data);

				if (!is_declined)
					add_component (cbfile, comp, FALSE);

				object = e_cal_component_get_as_string (comp);
				if (!is_declined)
					e_cal_backend_notify_object_modified (E_CAL_BACKEND (backend), old_object, object);
				else {
					ECalComponentId *id = e_cal_component_get_id (comp);

					if (rid)
						e_cal_backend_notify_object_removed (E_CAL_BACKEND (backend), id, old_object, object);
					else
						e_cal_backend_notify_object_removed (E_CAL_BACKEND (backend), id, old_object, NULL);

					e_cal_component_free_id (id);
				}

				g_free (object);
				g_free (old_object);
			} else if (!is_declined) {
				add_component (cbfile, comp, FALSE);

				object = e_cal_component_get_as_string (comp);
				e_cal_backend_notify_object_created (E_CAL_BACKEND (backend), object);
				g_free (object);
			}
			g_free (rid);
			break;
		case ICAL_METHOD_ADD:
			/* FIXME This should be doable once all the recurid stuff is done */
			status = GNOME_Evolution_Calendar_UnsupportedMethod;
			g_free (rid);
			goto error;
			break;
		case ICAL_METHOD_COUNTER:
			status = GNOME_Evolution_Calendar_UnsupportedMethod;
			g_free (rid);
			goto error;
			break;
		case ICAL_METHOD_DECLINECOUNTER:
			status = GNOME_Evolution_Calendar_UnsupportedMethod;
			g_free (rid);
			goto error;
			break;
		case ICAL_METHOD_CANCEL:
			old_object = NULL;
			new_object = NULL;
			if (cancel_received_object (cbfile, subcomp, &old_object, &new_object)) {
				ECalComponentId *id;

				id = e_cal_component_get_id (comp);

				e_cal_backend_notify_object_removed (E_CAL_BACKEND (backend), id, old_object, new_object);

				/* remove the component from the toplevel VCALENDAR */
				icalcomponent_remove_component (toplevel_comp, subcomp);
				icalcomponent_free (subcomp);
				e_cal_component_free_id (id);

				g_free (new_object);
				g_free (old_object);
			}
			g_free (rid);
			break;
		default:
			status = GNOME_Evolution_Calendar_UnsupportedMethod;
			g_free (rid);
			goto error;
		}
	}

	g_list_free (comps);

	/* Now we remove the components we don't care about */
	for (l = del_comps; l; l = l->next) {
		subcomp = l->data;

		icalcomponent_remove_component (toplevel_comp, subcomp);
		icalcomponent_free (subcomp);
	}

	g_list_free (del_comps);

        /* check and patch timezones */
        {
            GError *error = NULL;
            if (!e_cal_check_timezones(toplevel_comp,
                                       NULL,
                                       e_cal_tzlookup_icomp,
                                       priv->icalcomp,
                                       &error)) {
                /*
                 * This makes assumptions about what kind of
                 * errors can occur inside e_cal_check_timezones().
                 * We control it, so that should be safe, but
                 * is the code really identical with the calendar
                 * status?
                 */
                status = error->code;
                g_clear_error(&error);
                goto error;
            }
        }

	/* Merge the iCalendar components with our existing VCALENDAR,
	   resolving any conflicting TZIDs. */
	icalcomponent_merge_component (priv->icalcomp, toplevel_comp);

	save (cbfile);

 error:
	g_hash_table_destroy (tzdata.zones);
	g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
	return status;
}

static ECalBackendSyncStatus
e_cal_backend_file_send_objects (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj, GList **users,
				 gchar **modified_calobj)
{
	*users = NULL;
	*modified_calobj = g_strdup (calobj);

	return GNOME_Evolution_Calendar_Success;
}

/* Object initialization function for the file backend */
static void
e_cal_backend_file_init (ECalBackendFile *cbfile)
{
	ECalBackendFilePrivate *priv;

	priv = g_new0 (ECalBackendFilePrivate, 1);
	cbfile->priv = priv;

	priv->path = NULL;
	priv->file_name = g_strdup ("calendar.ics");
	priv->read_only = FALSE;
	priv->is_dirty = FALSE;
	priv->dirty_idle_id = 0;
	g_static_rec_mutex_init (&priv->idle_save_rmutex);
	priv->icalcomp = NULL;
	priv->comp_uid_hash = NULL;
	priv->comp = NULL;
	priv->custom_file = NULL;
	priv->refresh_lock = g_mutex_new ();

	/* The timezone defaults to UTC. */
	priv->default_zone = icaltimezone_get_utc_timezone ();

        /*
         * data access is serialized via idle_save_rmutex, so locking at the
         * backend method level is not needed
         */
	e_cal_backend_sync_set_lock (E_CAL_BACKEND_SYNC (cbfile), FALSE);
}

/* Class initialization function for the file backend */
static void
e_cal_backend_file_class_init (ECalBackendFileClass *class)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;
	ECalBackendSyncClass *sync_class;

	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;
	sync_class = (ECalBackendSyncClass *) class;

	parent_class = (ECalBackendSyncClass *) g_type_class_peek_parent (class);

	object_class->dispose = e_cal_backend_file_dispose;
	object_class->finalize = e_cal_backend_file_finalize;

	sync_class->is_read_only_sync = e_cal_backend_file_is_read_only;
	sync_class->get_cal_address_sync = e_cal_backend_file_get_cal_address;
	sync_class->get_alarm_email_address_sync = e_cal_backend_file_get_alarm_email_address;
	sync_class->get_ldap_attribute_sync = e_cal_backend_file_get_ldap_attribute;
	sync_class->get_static_capabilities_sync = e_cal_backend_file_get_static_capabilities;
	sync_class->open_sync = e_cal_backend_file_open;
	sync_class->remove_sync = e_cal_backend_file_remove;
	sync_class->create_object_sync = e_cal_backend_file_create_object;
	sync_class->modify_object_sync = e_cal_backend_file_modify_object;
	sync_class->remove_object_sync = e_cal_backend_file_remove_object;
	sync_class->discard_alarm_sync = e_cal_backend_file_discard_alarm;
	sync_class->receive_objects_sync = e_cal_backend_file_receive_objects;
	sync_class->send_objects_sync = e_cal_backend_file_send_objects;
	sync_class->get_default_object_sync = e_cal_backend_file_get_default_object;
	sync_class->get_object_sync = e_cal_backend_file_get_object;
	sync_class->get_object_list_sync = e_cal_backend_file_get_object_list;
	sync_class->get_attachment_list_sync = e_cal_backend_file_get_attachment_list;
	sync_class->add_timezone_sync = e_cal_backend_file_add_timezone;
	sync_class->set_default_zone_sync = e_cal_backend_file_set_default_zone;
	sync_class->get_freebusy_sync = e_cal_backend_file_get_free_busy;
	sync_class->get_changes_sync = e_cal_backend_file_get_changes;

	backend_class->is_loaded = e_cal_backend_file_is_loaded;
	backend_class->start_query = e_cal_backend_file_start_query;
	backend_class->get_mode = e_cal_backend_file_get_mode;
	backend_class->set_mode = e_cal_backend_file_set_mode;

	backend_class->internal_get_default_timezone = e_cal_backend_file_internal_get_default_timezone;
	backend_class->internal_get_timezone = e_cal_backend_file_internal_get_timezone;
}

/**
 * e_cal_backend_file_get_type:
 * @void:
 *
 * Registers the #ECalBackendFile class if necessary, and returns the type ID
 * associated to it.
 *
 * Return value: The type ID of the #ECalBackendFile class.
 **/
GType
e_cal_backend_file_get_type (void)
{
	static GType e_cal_backend_file_type = 0;

	if (!e_cal_backend_file_type) {
		static GTypeInfo info = {
                        sizeof (ECalBackendFileClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_cal_backend_file_class_init,
                        NULL, NULL,
                        sizeof (ECalBackendFile),
                        0,
                        (GInstanceInitFunc) e_cal_backend_file_init
                };
		e_cal_backend_file_type = g_type_register_static (E_TYPE_CAL_BACKEND_SYNC,
								"ECalBackendFile", &info, 0);
	}

	return e_cal_backend_file_type;
}

void
e_cal_backend_file_set_file_name (ECalBackendFile *cbfile, const gchar *file_name)
{
	ECalBackendFilePrivate *priv;

	g_return_if_fail (cbfile != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_FILE (cbfile));
	g_return_if_fail (file_name != NULL);

	priv = cbfile->priv;
        g_static_rec_mutex_lock (&priv->idle_save_rmutex);

	if (priv->file_name)
		g_free (priv->file_name);

	priv->file_name = g_strdup (file_name);

        g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
}

const gchar *
e_cal_backend_file_get_file_name (ECalBackendFile *cbfile)
{
	ECalBackendFilePrivate *priv;

	g_return_val_if_fail (cbfile != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND_FILE (cbfile), NULL);

	priv = cbfile->priv;

	return priv->file_name;
}

ECalBackendSyncStatus
e_cal_backend_file_reload (ECalBackendFile *cbfile)
{
	ECalBackendFilePrivate *priv;
	gchar *str_uri;
	ECalBackendSyncStatus status;

	priv = cbfile->priv;
        g_static_rec_mutex_lock (&priv->idle_save_rmutex);

	str_uri = get_uri_string (E_CAL_BACKEND (cbfile));
	if (!str_uri) {
		status = GNOME_Evolution_Calendar_OtherError;
                goto done;
        }

	if (g_access (str_uri, R_OK) == 0) {
		status = reload_cal (cbfile, str_uri);
		if (g_access (str_uri, W_OK) != 0)
			priv->read_only = TRUE;
	} else {
		status = GNOME_Evolution_Calendar_NoSuchCal;
	}

	g_free (str_uri);

	if (status == GNOME_Evolution_Calendar_Success && !priv->read_only) {
		ESource *source = e_cal_backend_get_source (E_CAL_BACKEND (cbfile));

		if (source && e_source_get_property (source, "custom-file-readonly") && g_str_equal (e_source_get_property (source, "custom-file-readonly"), "1"))
			priv->read_only = TRUE;
	}
  done:
        g_static_rec_mutex_unlock (&priv->idle_save_rmutex);
	return status;
}

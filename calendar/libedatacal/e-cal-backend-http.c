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
#include "e-cal-backend-http.h"
#include "e-cal-backend-file.h"
#include "e-cal-backend-util.h"
#include "e-cal-backend-sexp.h"



/* Private part of the ECalBackendHttp structure */
struct _ECalBackendHttpPrivate {
	/* URI to get remote calendar data from */
	char *uri;

	/* Local/remote mode */
	CalMode mode;

	/* Cached-file backend */
	ECalBackendFile file_backend;

	/* The calendar's default timezone, used for resolving DATE and
	   floating DATE-TIME values. */
	icaltimezone *default_zone;

	/* The list of live queries */
	GList *queries;
};



static void e_cal_backend_http_dispose (GObject *object);
static void e_cal_backend_http_finalize (GObject *object);

static ECalBackendSyncClass *parent_class;



/* Dispose handler for the file backend */
static void
e_cal_backend_http_dispose (GObject *object)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;

	cbfile = E_CAL_BACKEND_HTTP (object);
	priv = cbfile->priv;

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

/* Finalize handler for the file backend */
static void
e_cal_backend_http_finalize (GObject *object)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_HTTP (object));

	cbfile = E_CAL_BACKEND_HTTP (object);
	priv = cbfile->priv;

	/* Clean up */

	if (priv->uri) {
	        g_free (priv->uri);
		priv->uri = NULL;
	}

	g_free (priv);
	cbfile->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}



/* Calendar backend methods */

/* Is_read_only handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_is_read_only (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{
	ECalBackendHttp *cbfile = backend;

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

/* Open handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_open (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;
	char *str_uri;
	ECalBackendSyncStatus status = GNOME_Evolution_Calendar_NoSuchCal;
	
	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

	g_message ("Open URI '%s'.", e_cal_backend_get_uri (E_CAL_BACKEND (cbfile)));

	return status;
}

static ECalBackendSyncStatus
e_cal_backend_http_remove (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;
	char *str_uri;
	
	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

	return GNOME_Evolution_Calendar_OtherError;
}

/* is_loaded handler for the file backend */
static gboolean
e_cal_backend_http_is_loaded (ECalBackend *backend)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

	return FALSE;
}

/* is_remote handler for the file backend */
static CalMode
e_cal_backend_http_get_mode (ECalBackend *backend)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

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
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;
	GNOME_Evolution_Calendar_CalMode set_mode;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

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
 	ECalComponent *comp;
 	
	return GNOME_Evolution_Calendar_Success;
}

/* Get_object_component handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_object (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, char **object)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;
	ECalComponent *comp = NULL;
	gboolean free_comp = FALSE;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	return GNOME_Evolution_Calendar_Success;
}

/* Get_timezone_object handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid, char **object)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;
	icaltimezone *zone;
	icalcomponent *icalcomp;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (tzid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	return GNOME_Evolution_Calendar_Success;
}

/* Add_timezone handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_add_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
	icalcomponent *tz_comp;
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;

	cbfile = (ECalBackendHttp *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_HTTP (cbfile), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbfile->priv;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_http_set_default_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;
	icaltimezone *zone;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

	return GNOME_Evolution_Calendar_Success;
}

/* Get_objects_in_range handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_object_list (ECalBackendSync *backend, EDataCal *cal, const char *sexp, GList **objects)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

	return GNOME_Evolution_Calendar_Success;	
}

/* get_query handler for the file backend */
static void
e_cal_backend_http_start_query (ECalBackend *backend, EDataCalView *query)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;
}

/* Get_free_busy handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_free_busy (ECalBackendSync *backend, EDataCal *cal, GList *users,
				time_t start, time_t end, GList **freebusy)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;
	gchar *address, *name;	
	icalcomponent *vfb;
	char *calobj;
	GList *l;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (start != -1 && end != -1, GNOME_Evolution_Calendar_InvalidRange);
	g_return_val_if_fail (start <= end, GNOME_Evolution_Calendar_InvalidRange);

	return GNOME_Evolution_Calendar_Success;
}

/* Get_changes handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_changes (ECalBackendSync *backend, EDataCal *cal, const char *change_id,
			      GList **adds, GList **modifies, GList **deletes)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (change_id != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	return GNOME_Evolution_Calendar_Success;
}

/* Discard_alarm handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *auid)
{
	/* we just do nothing with the alarm */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_http_create_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, char **uid)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;
	icalcomponent *icalcomp;
	icalcomponent_kind kind;
	ECalComponent *comp;
	const char *comp_uid;
	struct icaltimetype current;
	
	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_http_modify_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, 
				CalObjModType mod, char **old_object)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;
	icalcomponent *icalcomp;
	icalcomponent_kind kind;
	const char *comp_uid;
	ECalComponent *comp;
	struct icaltimetype current;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;
		
	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	return GNOME_Evolution_Calendar_Success;
}

/* Remove_object handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_remove_object (ECalBackendSync *backend, EDataCal *cal,
				const char *uid, const char *rid,
				CalObjModType mod, char **object)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;
	ECalComponent *comp;
	char *hash_rid;
	GSList *categories;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	return GNOME_Evolution_Calendar_Success;
}

/* Update_objects handler for the file backend. */
static ECalBackendSyncStatus
e_cal_backend_http_receive_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;
	icalcomponent *toplevel_comp, *icalcomp = NULL;
	icalcomponent_kind kind;
	icalproperty_method method;
	icalcomponent *subcomp;
	GList *comps, *l;
	ECalBackendSyncStatus status = GNOME_Evolution_Calendar_Success;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

	return status;
}

static ECalBackendSyncStatus
e_cal_backend_http_send_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
	/* FIXME Put in a util routine to send stuff via email */
	
	return GNOME_Evolution_Calendar_Success;
}

static icaltimezone *
e_cal_backend_http_internal_get_default_timezone (ECalBackend *backend)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;


	return priv->default_zone;
}

static icaltimezone *
e_cal_backend_http_internal_get_timezone (ECalBackend *backend, const char *tzid)
{
	ECalBackendHttp *cbfile;
	ECalBackendHttpPrivate *priv;
	icaltimezone *zone;

	cbfile = E_CAL_BACKEND_HTTP (backend);
	priv = cbfile->priv;

	if (!strcmp (tzid, "UTC"))
	        zone = icaltimezone_get_utc_timezone ();
	else {
		zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	}

	return zone;
}

/* Object initialization function for the file backend */
static void
e_cal_backend_http_init (ECalBackendHttp *cbfile, ECalBackendHttpClass *class)
{
	ECalBackendHttpPrivate *priv;

	g_message ("Webcal backend init.");

	priv = g_new0 (ECalBackendHttpPrivate, 1);
	cbfile->priv = priv;

	priv->uri = NULL;

#if 0
	priv->config_listener = e_config_listener_new ();
#endif
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

	g_message (G_STRLOC);

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

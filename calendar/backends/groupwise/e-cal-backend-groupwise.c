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
#include <libedata-cal/e-cal-backend-cache.h>
#include "e-cal-backend-groupwise.h"
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
	CalMode mode;
};

static void e_cal_backend_groupwise_dispose (GObject *object);
static void e_cal_backend_groupwise_finalize (GObject *object);

#define PARENT_TYPE E_TYPE_CAL_BACKEND_SYNC
static ECalBackendClass *parent_class = NULL;

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
	
	cbgw = E_CAL_BACKEND_GROUPWISE(backend);
	*address = g_strdup (e_gw_connection_get_user_email (cbgw->priv->cnc));
	
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
				  CAL_STATIC_CAPABILITY_REMOVE_ALARMS ","   \
	                          CAL_STATIC_CAPABILITY_NO_THISANDPRIOR "," \
				  CAL_STATIC_CAPABILITY_NO_THISANDFUTURE);

	return GNOME_Evolution_Calendar_Success;
}

static GnomeVFSURI *
convert_uri (const char *gw_uri)
{
	char *real_uri;
	GnomeVFSURI *vuri;

	if (strncmp ("groupwise://", gw_uri, sizeof ("groupwise://") - 1))
		return NULL;

	real_uri = g_strconcat ("http://", gw_uri + sizeof ("groupwise://") - 1, NULL);
	vuri = gnome_vfs_uri_new ((const char *) real_uri);

	g_free (real_uri);

	return vuri;
}

/* Initialy populate the cache from the server */
//static EGwConnectionStatus
static EGwConnectionStatus
populate_cache (ECalBackendGroupwisePrivate *priv)
{
	EGwConnectionStatus status;
	SoupSoapParameter *param;
	const char *uid;
	const char *rid;
	const char *calobj;
        GSList *list = NULL, *l;

        /* get all the objects from the server */
        status = e_gw_connection_get_items (priv->cnc, &list);
        if (status != E_GW_CONNECTION_STATUS_OK)
                return status;

        /* update the cache */
        g_mutex_lock (priv->mutex);

        for (l = list; l; l = l->next) {
		param = (SoupSoapParameter *) l->data;
		uid = soup_soap_parameter_get_first_child_by_name (param, "iCalId");
		rid = soup_soap_parameter_get_first_child_by_name (param, "recurrance");
		calobj = soup_soap_parameter_get_string_value (param);
		e_cal_backend_cache_put_component (priv->cache, uid, rid, calobj);
        }

	g_mutex_unlock (priv->mutex);

	return E_GW_CONNECTION_STATUS_OK;        
                                                                                                                    
}

static void
update_cache (gpointer *data)
{
	EGwConnection *cnc;

        cnc = E_GW_CONNECTION (data);
        /* FIXME: Get all changes from the server since last poll  */
        /* FIXME: parse the response and update the cache*/
        /*status =  e_gw_connection_get_items_with_Filter (cnc, &list);*/
}

/* Open handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_open (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists,
			      const char *username, const char *password)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
	
	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;

	g_mutex_lock (priv->mutex);

	/* create the local cache */
	if (priv->cache)
		g_object_unref (priv->cache);
	priv->cache = e_cal_backend_cache_new (e_cal_backend_get_uri (E_CAL_BACKEND (backend)));
	if (!priv->cache) {
		g_mutex_unlock (priv->mutex);
		g_warning (G_STRLOC ": Could not create cache file for %s",
			   e_cal_backend_get_uri (E_CAL_BACKEND (backend)));
		return GNOME_Evolution_Calendar_OtherError;
	}

	cbgw->priv->read_only = FALSE;
	priv->mode = CAL_MODE_LOCAL;
	priv->username = g_strdup (username);
	priv->password = g_strdup (password);

	g_mutex_unlock (priv->mutex);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_remove (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackendGroupwise *cbgw;
	ECalBackendGroupwisePrivate *priv;
	
	cbgw = E_CAL_BACKEND_GROUPWISE (backend);
	priv = cbgw->priv;
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
	GnomeVFSURI *vuri;
	char *real_uri;
	EGwConnectionStatus status;
	ECalBackendSyncStatus result;
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
		/* convert the URI */
		vuri = convert_uri (e_cal_backend_get_uri (E_CAL_BACKEND (backend)));
		if (!vuri) {
			e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_NOT_SET,
						   GNOME_Evolution_Calendar_MODE_REMOTE);
		} else {
			/* create connection to server */
			real_uri = gnome_vfs_uri_to_string ((const GnomeVFSURI *) vuri,
							    GNOME_VFS_URI_HIDE_USER_NAME |
							    GNOME_VFS_URI_HIDE_PASSWORD);
			priv->cnc = e_gw_connection_new (
				real_uri,
				priv->username ? priv->username : gnome_vfs_uri_get_user_name (vuri),
				priv->password ? priv->password : gnome_vfs_uri_get_password (vuri));

			gnome_vfs_uri_unref (vuri);
			g_free (real_uri);
			
			/* As of now we are assuming that logged in user has write rights to calender */
			/* we need to read actual rights from server when we implement proxy user access */
			cbgw->priv->read_only = FALSE;
	
			if (E_IS_GW_CONNECTION (priv->cnc)) {
				/* Populate the cache for the first time.*/
				/* start a timed polling thread set to 10 minutes*/
				status = populate_cache (priv);
				if (status != E_GW_CONNECTION_STATUS_OK) {
					g_object_unref (priv->cnc);
					priv->cnc = NULL;
					g_warning (G_STRLOC ": Could not populate the cache");

					e_cal_backend_notify_mode (backend,
								   GNOME_Evolution_Calendar_CalListener_MODE_NOT_SET,
								   GNOME_Evolution_Calendar_MODE_REMOTE);
				} else {
					/* FIXME : add a symbolic constant for the time-out */
					g_timeout_add (600000, (GSourceFunc) update_cache, (gpointer) cbgw);

					priv->mode = CAL_MODE_REMOTE;
					e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_SET,
								   GNOME_Evolution_Calendar_MODE_REMOTE);
				}
			} else
				e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_NOT_SET,
							   GNOME_Evolution_Calendar_MODE_REMOTE);
		}
		break;
	case CAL_MODE_LOCAL : /* go offline */
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

/* Get_object_component handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_get_object (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, char **object)
{
	ECalComponent *comp;
	ECalBackendGroupwisePrivate *priv;
	ECalBackendGroupwise *cbgw = (ECalBackendGroupwise *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GROUPWISE (cbgw), GNOME_Evolution_Calendar_OtherError);

	priv = cbgw->priv;

	/* search the object in the cache */
	comp = e_cal_backend_cache_get_component (priv->cache, uid, rid);
	if (comp) {
		*object = e_cal_component_get_as_string (comp);
		g_object_unref (comp);

		return GNOME_Evolution_Calendar_Success;
	}

	/* callers will never have a uuid that is in server but not in cache */
	return GNOME_Evolution_Calendar_ObjectNotFound;
}

/* Get_timezone_object handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_get_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid, char **object)
{
	ECalBackendGroupwise *cbfile;
        ECalBackendGroupwisePrivate *priv;
        icaltimezone *zone;
        icalcomponent *icalcomp;

        cbfile = E_CAL_BACKEND_GROUPWISE (backend);
        priv = cbfile->priv;

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

/* Add_timezone handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_add_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_set_default_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid)
{
}

/* Get_objects_in_range handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_get_object_list (ECalBackendSync *backend, EDataCal *cal, const char *sexp, GList **objects)
{
}

/* get_query handler for the file backend */
static void
e_cal_backend_groupwise_start_query (ECalBackend *backend, EDataCalView *query)
{
}

/* Get_free_busy handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_get_free_busy (ECalBackendSync *backend, EDataCal *cal, GList *users,
				time_t start, time_t end, GList **freebusy)
{
}

/* Get_changes handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_get_changes (ECalBackendSync *backend, EDataCal *cal, const char *change_id,
			      GList **adds, GList **modifies, GList **deletes)
{
}

/* Discard_alarm handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *auid)
{
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_create_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, char **uid)
{
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_modify_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, 
				CalObjModType mod, char **old_object)
{
}

/* Remove_object handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_groupwise_remove_object (ECalBackendSync *backend, EDataCal *cal,
				const char *uid, const char *rid,
				CalObjModType mod, char **object)
{
}

/* Update_objects handler for the file backend. */
static ECalBackendSyncStatus
e_cal_backend_groupwise_receive_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
}

static ECalBackendSyncStatus
e_cal_backend_groupwise_send_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
}

/* Object initialization function for the file backend */
static void
e_cal_backend_groupwise_init (ECalBackendGroupwise *cbgw, ECalBackendGroupwiseClass *class)
{
	ECalBackendGroupwisePrivate *priv;

	priv = g_new0 (ECalBackendGroupwisePrivate, 1);

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

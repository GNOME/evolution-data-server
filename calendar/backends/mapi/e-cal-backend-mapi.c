/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) Rémi L'Ecolier 2007.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


/* inspired by groupwise backend */

#include "e-cal-backend-mapi.h"

static ECalBackendClass *parent_class = NULL;


/* Private part of the CalBackendOpenchange structure */
struct _ECalBackendOpenchangePrivate {
	/* A mutex to control access to the private structure */
	GMutex			*mutex;
	ECalBackendCache	*cache;
	gboolean		read_only;
	char			*uri;
	char			*username;
	char			*password;
	char			*container_id;
	int			timeout_id;
	CalMode			mode;
	gboolean		mode_changed;
	icaltimezone		*default_zone;
	GHashTable		*categories_by_id;
	GHashTable		*categories_by_name;

	/* number of calendar items in the folder */
	guint32			total_count;

	/* timeout handler for syncing sendoptions */
	guint			sendoptions_sync_timeout;
	
	/* fields for storing info while offline */
	char			*user_email;
	char			*local_attachments_store;
};


/* object class */


static void	e_cal_backend_openchange_dispose(GObject *object)
{

	
	OC_DEBUG("e_cal_backend_openchange_dispose\n");
}

static void	e_cal_backend_openchange_finalize(GObject *object)
{

	OC_DEBUG("e_cal_backend_openchange_finalize\n");

}

/* sync class */


static ECalBackendSyncStatus	e_cal_backend_openchange_is_read_only(ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{

	OC_DEBUG("e_cal_backend_openchange_is_read_only\n");
	return GNOME_Evolution_Calendar_Success;
}



static ECalBackendSyncStatus	e_cal_backend_openchange_get_cal_address(ECalBackendSync *backend, EDataCal *cal, char **address)
{

	OC_DEBUG("e_cal_backend_openchange_get_cal_address\n");
	return GNOME_Evolution_Calendar_Success;
}


static ECalBackendSyncStatus	e_cal_backend_openchange_get_alarm_email_address(ECalBackendSync *backend, EDataCal *cal, char **address)
{

	OC_DEBUG("e_cal_backend_openchange_get_alarm_email_address\n");
	return GNOME_Evolution_Calendar_Success;	
}



static ECalBackendSyncStatus	e_cal_backend_openchange_get_ldap_attribute(ECalBackendSync *backend, EDataCal *cal, char **attribute)
{

	OC_DEBUG("e_cal_backend_openchange_get_ldap_attribute\n");
	return GNOME_Evolution_Calendar_Success;
}



static ECalBackendSyncStatus	e_cal_backend_openchange_get_static_capabilities(ECalBackendSync *backend, EDataCal *cal, char **capabilities)
{
	
	OC_DEBUG("e_cal_backend_openchange_get_static_capabilities\n");
	return GNOME_Evolution_Calendar_Success;

}


static ECalBackendSyncStatus	e_cal_backend_openchange_open(ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists, const char *username, const char *password)
{

	OC_DEBUG("e_cal_backend_openchange_open\n");
	return GNOME_Evolution_Calendar_Success;
}


static ECalBackendSyncStatus	e_cal_backend_openchange_remove(ECalBackendSync *backend, EDataCal *cal)
{

	OC_DEBUG("e_cal_backend_openchange_remove\n");
	return GNOME_Evolution_Calendar_Success;
}


static ECalBackendSyncStatus	e_cal_backend_openchange_create_object(ECalBackendSync *backend, EDataCal *cal, char **calobj, char **uid)
{

	OC_DEBUG("e_cal_backend_openchange_create_object\n");
	return GNOME_Evolution_Calendar_Success;
}


static ECalBackendSyncStatus	e_cal_backend_openchange_modify_object(ECalBackendSync *backend, EDataCal *cal, const char *calobj, 
				       CalObjModType mod, char **old_object, char **new_object)
{

	OC_DEBUG("e_cal_backend_openchange_modify_object\n");
	return GNOME_Evolution_Calendar_Success;
}


static ECalBackendSyncStatus	e_cal_backend_openchange_remove_object(ECalBackendSync *backend, EDataCal *cal,
				       const char *uid, const char *rid,
				       CalObjModType mod, char **old_object,
				       char **object)
{

	OC_DEBUG("e_cal_backend_openchange_remove_object\n");
	return GNOME_Evolution_Calendar_Success;

}

static ECalBackendSyncStatus	e_cal_backend_openchange_discard_alarm(ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *auid)
{

	OC_DEBUG("e_cal_backend_openchange_discard_alarm\n");
	return GNOME_Evolution_Calendar_Success;
	
}


static ECalBackendSyncStatus	e_cal_backend_openchange_receive_objects(ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{

	OC_DEBUG("e_cal_backend_openchange_receive_objects\n");
	return GNOME_Evolution_Calendar_Success;
}



static ECalBackendSyncStatus	e_cal_backend_openchange_send_objects(ECalBackendSync *backend, EDataCal *cal, const char *calobj, GList **users,
				      char **modified_calobj)
{

	OC_DEBUG("e_cal_backend_openchange_send_objects\n");
	return GNOME_Evolution_Calendar_Success;

}


static ECalBackendSyncStatus	e_cal_backend_openchange_get_default_object(ECalBackendSync *backend, EDataCal *cal, char **object)
{

	OC_DEBUG("e_cal_backend_openchange_get_default_object\n");
	return GNOME_Evolution_Calendar_Success;

}


static ECalBackendSyncStatus	e_cal_backend_openchange_get_object(ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, char **object)
{

	OC_DEBUG("e_cal_backend_openchange_get_object\n");
	return GNOME_Evolution_Calendar_Success;

}


static ECalBackendSyncStatus	e_cal_backend_openchange_get_object_list(ECalBackendSync *backend, EDataCal *cal, const char *sexp, GList **objects)
{

	OC_DEBUG("e_cal_backend_openchange_get_object_list\n");
	return GNOME_Evolution_Calendar_Success;
}


static ECalBackendSyncStatus	e_cal_backend_openchange_get_attachment_list(ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, GSList **list)
{


	OC_DEBUG("e_cal_backend_openchange_get_attachment_list\n");
	return GNOME_Evolution_Calendar_Success;
}


static ECalBackendSyncStatus	e_cal_backend_openchange_get_timezone(ECalBackendSync *backend, EDataCal *cal, const char *tzid, char **object)
{

	OC_DEBUG("e_cal_backend_openchange_get_timezone\n");
	return GNOME_Evolution_Calendar_Success;
}


static ECalBackendSyncStatus	e_cal_backend_openchange_add_timezone(ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{

	OC_DEBUG("e_cal_backend_openchange_add_timezone\n");
	return GNOME_Evolution_Calendar_Success;
	
}



static ECalBackendSyncStatus	e_cal_backend_openchange_set_default_timezone(ECalBackendSync *backend, EDataCal *cal, const char *tzid)
{

	OC_DEBUG("e_cal_backend_openchange_set_default_timezone\n");	
	return GNOME_Evolution_Calendar_Success;
}


static ECalBackendSyncStatus	e_cal_backend_openchange_get_free_busy(ECalBackendSync *backend, EDataCal *cal, GList *users,
				       time_t start, time_t end, GList **freebusy)
{

	OC_DEBUG("e_cal_backend_openchange_get_free_busy\n");
	return GNOME_Evolution_Calendar_Success;	

}



static ECalBackendSyncStatus	e_cal_backend_openchange_get_changes(ECalBackendSync *backend, EDataCal *cal, const char *change_id,
				     GList **adds, GList **modifies, GList **deletes)
{

	OC_DEBUG("e_cal_backend_openchange_get_changes\n");
	return GNOME_Evolution_Calendar_Success;

}



/* backend class */



static gboolean	e_cal_backend_openchange_is_loaded(ECalBackend *backend)
{

	OC_DEBUG("e_cal_backend_openchange_is_loaded\n");
	return FALSE;
}


static void	e_cal_backend_openchange_start_query (ECalBackend *backend, EDataCalView *query)
{

	OC_DEBUG("e_cal_backend_openchange_start_query\n");

}


static CalMode	e_cal_backend_openchange_get_mode(ECalBackend *backend)
{

	OC_DEBUG("e_cal_backend_openchange_get_mode\n");
	return FALSE;
}


static void	e_cal_backend_openchange_set_mode(ECalBackend *backend, CalMode mode)
{

	OC_DEBUG("e_cal_backend_openchange_set_mode\n");

}



static icaltimezone *e_cal_backend_openchange_internal_get_default_timezone(ECalBackend *backend)
{
	OC_DEBUG("e_cal_backend_openchange_internal_get_default_timezone\n");

	return NULL;
}


static icaltimezone *e_cal_backend_openchange_internal_get_timezone(ECalBackend *backend, const char *tzid)
{

	OC_DEBUG("e_cal_backend_openchange_internal_get_timezone\n");
	return NULL;
}



/*  init class */



static void	e_cal_backend_openchange_init(ECalBackendOpenchange *cboc, ECalBackendOpenchangeClass *class)
{
	
	OC_DEBUG("e_cal_backend_openchange_init\n");

	ECalBackendOpenchangePrivate *priv;
	
	priv = g_new0 (ECalBackendOpenchangePrivate, 1);

	priv->timeout_id = 0;

	priv->sendoptions_sync_timeout = 0;

	/* create the mutex for thread safety */
	priv->mutex = g_mutex_new ();

	cboc->priv = priv;

	e_cal_backend_sync_set_lock (E_CAL_BACKEND_SYNC (cboc), TRUE);

}



static void	e_cal_backend_openchange_class_init(ECalBackendOpenchangeClass *class)
{
	
	OC_DEBUG("e_cal_backend_openchange_class_init\n");


	GObjectClass *object_class;
	ECalBackendClass *backend_class;
	ECalBackendSyncClass *sync_class;
	
	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;
	sync_class = (ECalBackendSyncClass *) class;
	
	parent_class = g_type_class_peek_parent(class);

	object_class->dispose = e_cal_backend_openchange_dispose;
	object_class->finalize = e_cal_backend_openchange_finalize;

	sync_class->is_read_only_sync = e_cal_backend_openchange_is_read_only;
	sync_class->get_cal_address_sync = e_cal_backend_openchange_get_cal_address;
 	sync_class->get_alarm_email_address_sync = e_cal_backend_openchange_get_alarm_email_address;
 	sync_class->get_ldap_attribute_sync = e_cal_backend_openchange_get_ldap_attribute;
 	sync_class->get_static_capabilities_sync = e_cal_backend_openchange_get_static_capabilities;
	sync_class->open_sync = e_cal_backend_openchange_open;
	sync_class->remove_sync = e_cal_backend_openchange_remove;
	sync_class->create_object_sync = e_cal_backend_openchange_create_object;
	sync_class->modify_object_sync = e_cal_backend_openchange_modify_object;
	sync_class->remove_object_sync = e_cal_backend_openchange_remove_object;
	sync_class->discard_alarm_sync = e_cal_backend_openchange_discard_alarm;
	sync_class->receive_objects_sync = e_cal_backend_openchange_receive_objects;
	sync_class->send_objects_sync = e_cal_backend_openchange_send_objects;
 	sync_class->get_default_object_sync = e_cal_backend_openchange_get_default_object;
	sync_class->get_object_sync = e_cal_backend_openchange_get_object;
	sync_class->get_object_list_sync = e_cal_backend_openchange_get_object_list;
	sync_class->get_attachment_list_sync = e_cal_backend_openchange_get_attachment_list;
	sync_class->get_timezone_sync = e_cal_backend_openchange_get_timezone;
	sync_class->add_timezone_sync = e_cal_backend_openchange_add_timezone;
/* 	data server 1.6 whithout set_default_zone_sync */
	sync_class->set_default_timezone_sync = e_cal_backend_openchange_set_default_timezone;
	sync_class->get_freebusy_sync = e_cal_backend_openchange_get_free_busy;
	sync_class->get_changes_sync = e_cal_backend_openchange_get_changes;

	backend_class->is_loaded = e_cal_backend_openchange_is_loaded;
	backend_class->start_query = e_cal_backend_openchange_start_query;
	backend_class->get_mode = e_cal_backend_openchange_get_mode;
	backend_class->set_mode = e_cal_backend_openchange_set_mode;
	backend_class->internal_get_default_timezone = e_cal_backend_openchange_internal_get_default_timezone;
	backend_class->internal_get_timezone = e_cal_backend_openchange_internal_get_timezone;
}



GType	e_cal_backend_openchange_get_type(void)
{


	OC_DEBUG("e_cal_backend_openchange_get_type");


	static GType e_cal_backend_openchange_type = 0;

	if (!e_cal_backend_openchange_type) {
		static GTypeInfo info = {
                        sizeof (ECalBackendOpenchangeClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_cal_backend_openchange_class_init,
                        NULL, NULL,
                        sizeof (ECalBackendOpenchange),
                        0,
                        (GInstanceInitFunc)e_cal_backend_openchange_init
                };
		e_cal_backend_openchange_type = g_type_register_static (E_TYPE_CAL_BACKEND_SYNC,
								  "ECalBackendOpenchange", &info, 0);
	}

	return e_cal_backend_openchange_type;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 2003, Ximian, Inc.
 */

#ifdef CONFIG_H
#include <config.h>
#endif

#include "e-cal-backend-sync.h"

struct _ECalBackendSyncPrivate {
	int mumble;
	GMutex *sync_mutex;
};

static GObjectClass *parent_class;

ECalBackendSyncStatus
e_cal_backend_sync_is_read_only  (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (read_only, GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->is_read_only_sync);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->is_read_only_sync) (backend, cal, read_only);
}

ECalBackendSyncStatus
e_cal_backend_sync_get_cal_address  (ECalBackendSync *backend, EDataCal *cal, char **address)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (address, GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_cal_address_sync);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_cal_address_sync) (backend, cal, address);
}

ECalBackendSyncStatus
e_cal_backend_sync_get_alarm_email_address  (ECalBackendSync *backend, EDataCal *cal, char **address)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (address, GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_alarm_email_address_sync);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_alarm_email_address_sync) (backend, cal, address);
}

ECalBackendSyncStatus
e_cal_backend_sync_get_ldap_attribute  (ECalBackendSync *backend, EDataCal *cal, char **attribute)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (attribute, GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_ldap_attribute_sync);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_ldap_attribute_sync) (backend, cal, attribute);
}

ECalBackendSyncStatus
e_cal_backend_sync_get_static_capabilities  (ECalBackendSync *backend, EDataCal *cal, char **capabilities)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (capabilities, GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_static_capabilities_sync);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_static_capabilities_sync) (backend, cal, capabilities);
}

ECalBackendSyncStatus
e_cal_backend_sync_open  (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists,
			  const char *username, const char *password)
{
	ECalBackendSyncPrivate *priv;
	ECalBackendSyncStatus status;
	
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->open_sync);
	priv = backend->priv;

	g_mutex_lock (priv->sync_mutex);

	status = (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->open_sync) (backend, cal, only_if_exists, username, password);

	g_mutex_unlock (priv->sync_mutex);

	return status;
}

ECalBackendSyncStatus
e_cal_backend_sync_remove  (ECalBackendSync *backend, EDataCal *cal)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->remove_sync);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->remove_sync) (backend, cal);
}

ECalBackendSyncStatus
e_cal_backend_sync_create_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, char **uid)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->create_object_sync);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->create_object_sync) (backend, cal, calobj, uid);
}

ECalBackendSyncStatus
e_cal_backend_sync_modify_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, 
				CalObjModType mod, char **old_object)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->modify_object_sync);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->modify_object_sync) (backend, cal, 
									     calobj, mod, old_object);
}

ECalBackendSyncStatus
e_cal_backend_sync_remove_object (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid,
				CalObjModType mod, char **object)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->remove_object_sync);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->remove_object_sync) (backend, cal, uid, rid, mod, object);
}

ECalBackendSyncStatus
e_cal_backend_sync_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *auid)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->discard_alarm_sync);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->discard_alarm_sync) (backend, cal, uid, auid);
}

ECalBackendSyncStatus
e_cal_backend_sync_receive_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->receive_objects_sync);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->receive_objects_sync) (backend, cal, calobj);
}

ECalBackendSyncStatus
e_cal_backend_sync_send_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->send_objects_sync);
	
	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->send_objects_sync) (backend, cal, calobj);
}

ECalBackendSyncStatus
e_cal_backend_sync_get_default_object (ECalBackendSync *backend, EDataCal *cal, char **object)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (object, GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_default_object_sync);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_default_object_sync) (backend, cal, object);
}

ECalBackendSyncStatus
e_cal_backend_sync_get_object (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, char **object)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (object, GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_object_sync);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_object_sync) (backend, cal, uid, rid, object);
}

ECalBackendSyncStatus
e_cal_backend_sync_get_object_list (ECalBackendSync *backend, EDataCal *cal, const char *sexp, GList **objects)
{
	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (objects, GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_object_list_sync);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_object_list_sync) (backend, cal, sexp, objects);
}

ECalBackendSyncStatus
e_cal_backend_sync_get_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid, char **object)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_timezone_sync != NULL);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_timezone_sync) (backend, cal, tzid, object);
}

ECalBackendSyncStatus
e_cal_backend_sync_add_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->add_timezone_sync != NULL);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->add_timezone_sync) (backend, cal, tzobj);
}

ECalBackendSyncStatus
e_cal_backend_sync_set_default_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->set_default_timezone_sync != NULL);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->set_default_timezone_sync) (backend, cal, tzid);
}


ECalBackendSyncStatus
e_cal_backend_sync_get_changes (ECalBackendSync *backend, EDataCal *cal, const char *change_id,
			      GList **adds, GList **modifies, GList **deletes)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_changes_sync != NULL);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_changes_sync) (backend, cal, change_id, 
									   adds, modifies, deletes);
}

ECalBackendSyncStatus
e_cal_backend_sync_get_free_busy (ECalBackendSync *backend, EDataCal *cal, GList *users, 
				time_t start, time_t end, GList **freebusy)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_freebusy_sync != NULL);

	return (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_freebusy_sync) (backend, cal, users, 
									    start, end, freebusy);
}


static void
_e_cal_backend_is_read_only (ECalBackend *backend, EDataCal *cal)
{
	ECalBackendSyncStatus status;
	gboolean read_only = TRUE;

	status = e_cal_backend_sync_is_read_only (E_CAL_BACKEND_SYNC (backend), cal, &read_only);

	e_data_cal_notify_read_only (cal, status, read_only);
}

static void
_e_cal_backend_get_cal_address (ECalBackend *backend, EDataCal *cal)
{
	ECalBackendSyncStatus status;
	char *address = NULL;

	status = e_cal_backend_sync_get_cal_address (E_CAL_BACKEND_SYNC (backend), cal, &address);

	e_data_cal_notify_cal_address (cal, status, address);

	g_free (address);
}

static void
_e_cal_backend_get_alarm_email_address (ECalBackend *backend, EDataCal *cal)
{
	ECalBackendSyncStatus status;
	char *address = NULL;

	status = e_cal_backend_sync_get_alarm_email_address (E_CAL_BACKEND_SYNC (backend), cal, &address);

	e_data_cal_notify_alarm_email_address (cal, status, address);

	g_free (address);
}

static void
_e_cal_backend_get_ldap_attribute (ECalBackend *backend, EDataCal *cal)
{
	ECalBackendSyncStatus status;
	char *attribute = NULL;

	status = e_cal_backend_sync_get_ldap_attribute (E_CAL_BACKEND_SYNC (backend), cal, &attribute);

	e_data_cal_notify_ldap_attribute (cal, status, attribute);

	g_free (attribute);
}

static void
_e_cal_backend_get_static_capabilities (ECalBackend *backend, EDataCal *cal)
{
	ECalBackendSyncStatus status;
	char *capabilities = NULL;

	status = e_cal_backend_sync_get_static_capabilities (E_CAL_BACKEND_SYNC (backend), cal, &capabilities);

	e_data_cal_notify_static_capabilities (cal, status, capabilities);

	g_free (capabilities);
}

static void
_e_cal_backend_open (ECalBackend *backend, EDataCal *cal, gboolean only_if_exists,
		     const char *username, const char *password)
{
	ECalBackendSyncStatus status;

	status = e_cal_backend_sync_open (E_CAL_BACKEND_SYNC (backend), cal, only_if_exists, username, password);

	e_data_cal_notify_open (cal, status);
}

static void
_e_cal_backend_remove (ECalBackend *backend, EDataCal *cal)
{
	ECalBackendSyncStatus status;

	status = e_cal_backend_sync_remove (E_CAL_BACKEND_SYNC (backend), cal);

	e_data_cal_notify_remove (cal, status);
}

static void
_e_cal_backend_create_object (ECalBackend *backend, EDataCal *cal, const char *calobj)
{
	ECalBackendSyncStatus status;
	char *uid = NULL;
	
	status = e_cal_backend_sync_create_object (E_CAL_BACKEND_SYNC (backend), cal, calobj, &uid);

	e_data_cal_notify_object_created (cal, status, uid, calobj);

	if (uid)
		g_free (uid);
}

static void
_e_cal_backend_modify_object (ECalBackend *backend, EDataCal *cal, const char *calobj, CalObjModType mod)
{
	ECalBackendSyncStatus status;
	char *old_object = NULL;
	
	status = e_cal_backend_sync_modify_object (E_CAL_BACKEND_SYNC (backend), cal, 
						 calobj, mod, &old_object);

	e_data_cal_notify_object_modified (cal, status, old_object, calobj);
}

static void
_e_cal_backend_remove_object (ECalBackend *backend, EDataCal *cal, const char *uid, const char *rid, CalObjModType mod)
{
	ECalBackendSyncStatus status;
	char *object = NULL;
	
	status = e_cal_backend_sync_remove_object (E_CAL_BACKEND_SYNC (backend), cal, uid, rid, mod, &object);

	e_data_cal_notify_object_removed (cal, status, uid, object);
}

static void
_e_cal_backend_discard_alarm (ECalBackend *backend, EDataCal *cal, const char *uid, const char *auid)
{
	ECalBackendSyncStatus status;
	
	status = e_cal_backend_sync_discard_alarm (E_CAL_BACKEND_SYNC (backend), cal, uid, auid);

	e_data_cal_notify_alarm_discarded (cal, status);
}

static void
_e_cal_backend_receive_objects (ECalBackend *backend, EDataCal *cal, const char *calobj)
{
	ECalBackendSyncStatus status;
	
	status = e_cal_backend_sync_receive_objects (E_CAL_BACKEND_SYNC (backend), cal, calobj);

	e_data_cal_notify_objects_received (cal, status);
}

static void
_e_cal_backend_send_objects (ECalBackend *backend, EDataCal *cal, const char *calobj)
{
	ECalBackendSyncStatus status;

	status = e_cal_backend_sync_send_objects (E_CAL_BACKEND_SYNC (backend), cal, calobj);

	e_data_cal_notify_objects_sent (cal, status);
}

static void
_e_cal_backend_get_default_object (ECalBackend *backend, EDataCal *cal)
{
	ECalBackendSyncStatus status;
	char *object = NULL;

	status = e_cal_backend_sync_get_default_object (E_CAL_BACKEND_SYNC (backend), cal, &object);

	e_data_cal_notify_default_object (cal, status, object);

	g_free (object);
}

static void
_e_cal_backend_get_object (ECalBackend *backend, EDataCal *cal, const char *uid, const char *rid)
{
	ECalBackendSyncStatus status;
	char *object = NULL;

	status = e_cal_backend_sync_get_object (E_CAL_BACKEND_SYNC (backend), cal, uid, rid, &object);

	e_data_cal_notify_object (cal, status, object);
	
	g_free (object);
}

static void
_e_cal_backend_get_object_list (ECalBackend *backend, EDataCal *cal, const char *sexp)
{
	ECalBackendSyncStatus status;
	GList *objects = NULL, *l;

	status = e_cal_backend_sync_get_object_list (E_CAL_BACKEND_SYNC (backend), cal, sexp, &objects);

	e_data_cal_notify_object_list (cal, status, objects);

	for (l = objects; l; l = l->next)
		g_free (l->data);
	g_list_free (objects);
}

static void
_e_cal_backend_get_timezone (ECalBackend *backend, EDataCal *cal, const char *tzid)
{
	ECalBackendSyncStatus status;
	char *object = NULL;
	
	status = e_cal_backend_sync_get_timezone (E_CAL_BACKEND_SYNC (backend), cal, tzid, &object);

	e_data_cal_notify_timezone_requested (cal, status, object);
}

static void
_e_cal_backend_add_timezone (ECalBackend *backend, EDataCal *cal, const char *tzobj)
{
	ECalBackendSyncStatus status;

	status = e_cal_backend_sync_add_timezone (E_CAL_BACKEND_SYNC (backend), cal, tzobj);

	e_data_cal_notify_timezone_added (cal, status, tzobj);
}

static void
_e_cal_backend_set_default_timezone (ECalBackend *backend, EDataCal *cal, const char *tzid)
{
	ECalBackendSyncStatus status;

	status = e_cal_backend_sync_set_default_timezone (E_CAL_BACKEND_SYNC (backend), cal, tzid);

	e_data_cal_notify_default_timezone_set (cal, status);
}

static void
_e_cal_backend_get_changes (ECalBackend *backend, EDataCal *cal, const char *change_id)
{
	ECalBackendSyncStatus status;
	GList *adds = NULL, *modifies = NULL, *deletes = NULL, *l;
	
	status = e_cal_backend_sync_get_changes (E_CAL_BACKEND_SYNC (backend), cal, change_id, 
					       &adds, &modifies, &deletes);

	e_data_cal_notify_changes (cal, status, adds, modifies, deletes);

	for (l = adds; l; l = l->next)
		g_free (l->data);
	g_list_free (adds);

	for (l = modifies; l; l = l->next)
		g_free (l->data);
	g_list_free (modifies);

	for (l = deletes; l; l = l->next)
		g_free (l->data);
	g_list_free (deletes);
}

static void
_e_cal_backend_get_free_busy (ECalBackend *backend, EDataCal *cal, GList *users, time_t start, time_t end)
{
	ECalBackendSyncStatus status;
	GList *freebusy = NULL, *l;
	
	status = e_cal_backend_sync_get_free_busy (E_CAL_BACKEND_SYNC (backend), cal, users, start, end, &freebusy);

	e_data_cal_notify_free_busy (cal, status, freebusy);

	for (l = freebusy; l; l = l->next)
		g_free (l->data);
	g_list_free (freebusy);
}

static void
e_cal_backend_sync_init (ECalBackendSync *backend)
{
	ECalBackendSyncPrivate *priv;

	priv             = g_new0 (ECalBackendSyncPrivate, 1);
	priv->sync_mutex = g_mutex_new ();

	backend->priv = priv;
}

static void
e_cal_backend_sync_dispose (GObject *object)
{
	ECalBackendSync *backend;

	backend = E_CAL_BACKEND_SYNC (object);

	if (backend->priv) {
		g_mutex_free (backend->priv->sync_mutex);
		g_free (backend->priv);

		backend->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_cal_backend_sync_class_init (ECalBackendSyncClass *klass)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class = E_CAL_BACKEND_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class = (GObjectClass *) klass;

	backend_class->is_read_only = _e_cal_backend_is_read_only;
	backend_class->get_cal_address = _e_cal_backend_get_cal_address;
	backend_class->get_alarm_email_address = _e_cal_backend_get_alarm_email_address;
	backend_class->get_ldap_attribute = _e_cal_backend_get_ldap_attribute;
	backend_class->get_static_capabilities = _e_cal_backend_get_static_capabilities;
	backend_class->open = _e_cal_backend_open;
	backend_class->remove = _e_cal_backend_remove;
	backend_class->create_object = _e_cal_backend_create_object;
	backend_class->modify_object = _e_cal_backend_modify_object;
	backend_class->remove_object = _e_cal_backend_remove_object;
	backend_class->discard_alarm = _e_cal_backend_discard_alarm;
	backend_class->receive_objects = _e_cal_backend_receive_objects;
	backend_class->send_objects = _e_cal_backend_send_objects;
	backend_class->get_default_object = _e_cal_backend_get_default_object;
	backend_class->get_object = _e_cal_backend_get_object;
	backend_class->get_object_list = _e_cal_backend_get_object_list;
	backend_class->get_timezone = _e_cal_backend_get_timezone;
	backend_class->add_timezone = _e_cal_backend_add_timezone;
	backend_class->set_default_timezone = _e_cal_backend_set_default_timezone;
 	backend_class->get_changes = _e_cal_backend_get_changes;
 	backend_class->get_free_busy = _e_cal_backend_get_free_busy;

	object_class->dispose = e_cal_backend_sync_dispose;
}

/**
 * e_cal_backend_get_type:
 */
GType
e_cal_backend_sync_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (ECalBackendSyncClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_cal_backend_sync_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (ECalBackendSync),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_cal_backend_sync_init
		};

		type = g_type_register_static (E_TYPE_CAL_BACKEND, "ECalBackendSync", &info, 0);
	}

	return type;
}

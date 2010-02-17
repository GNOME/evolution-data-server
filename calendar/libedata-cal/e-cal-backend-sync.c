/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifdef CONFIG_H
#include <config.h>
#endif

#include "e-cal-backend-sync.h"
#include <libical/icaltz-util.h>

G_DEFINE_TYPE (ECalBackendSync, e_cal_backend_sync, E_TYPE_CAL_BACKEND)

struct _ECalBackendSyncPrivate {
	GMutex *sync_mutex;

	gboolean mutex_lock;
};

#define LOCK_WRAPPER(func, args) \
  g_assert (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->func); \
  if (backend->priv->mutex_lock) \
    g_mutex_lock (backend->priv->sync_mutex); \
  status = (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->func) args; \
  if (backend->priv->mutex_lock) \
    g_mutex_unlock (backend->priv->sync_mutex); \
  return status;

static GObjectClass *parent_class;

/**
 * e_cal_backend_sync_set_lock:
 * @backend: An ECalBackendSync object.
 * @lock: Lock mode.
 *
 * Sets the lock mode on the ECalBackendSync object. If TRUE, the backend
 * will create a locking mutex for every operation, so that only one can
 * happen at a time. If FALSE, no lock would be done and many operations
 * can happen at the same time.
 */
void
e_cal_backend_sync_set_lock (ECalBackendSync *backend, gboolean lock)
{
	g_return_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend));

	backend->priv->mutex_lock = lock;
}

/**
 * e_cal_backend_sync_is_read_only:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @read_only: Return value for read-only status.
 *
 * Calls the is_read_only method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_is_read_only  (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (read_only, GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (is_read_only_sync, (backend, cal, read_only));

	return status;
}

/**
 * e_cal_backend_sync_get_cal_address:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @address: Return value for the address.
 *
 * Calls the get_cal_address method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_get_cal_address  (ECalBackendSync *backend, EDataCal *cal, gchar **address)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (address, GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (get_cal_address_sync, (backend, cal, address));

	return status;
}

/**
 * e_cal_backend_sync_get_alarm_email_address:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @address: Return value for the address.
 *
 * Calls the get_alarm_email_address method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_get_alarm_email_address  (ECalBackendSync *backend, EDataCal *cal, gchar **address)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (address, GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (get_alarm_email_address_sync, (backend, cal, address));

	return status;
}

/**
 * e_cal_backend_sync_get_ldap_attribute:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @attribute: Return value for LDAP attribute.
 *
 * Calls the get_ldap_attribute method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_get_ldap_attribute  (ECalBackendSync *backend, EDataCal *cal, gchar **attribute)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (attribute, GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (get_ldap_attribute_sync, (backend, cal, attribute));

	return status;
}

/**
 * e_cal_backend_sync_get_static_capabilities:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @capabilities: Return value for capabilities.
 *
 * Calls the get_capabilities method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_get_static_capabilities  (ECalBackendSync *backend, EDataCal *cal, gchar **capabilities)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (capabilities, GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (get_static_capabilities_sync, (backend, cal, capabilities));

	return status;
}

/**
 * e_cal_backend_sync_open:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @only_if_exists: Whether to open the calendar if and only if it already exists
 * or just create it when it does not exist.
 * @username: User name to use for authentication.
 * @password: Password to use for authentication.
 *
 * Calls the open method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_open  (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists,
			  const gchar *username, const gchar *password)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (open_sync, (backend, cal, only_if_exists, username, password));

	return status;
}

/**
 * e_cal_backend_sync_remove:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 *
 * Calls the remove method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_remove  (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (remove_sync, (backend, cal));

	return status;
}

/**
 * e_cal_backend_sync_refresh:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 *
 * Calls the refresh method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_refresh  (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->refresh_sync != NULL,
			      GNOME_Evolution_Calendar_UnsupportedMethod);

	LOCK_WRAPPER (refresh_sync, (backend, cal));

	return status;
}

/**
 * e_cal_backend_sync_create_object:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @calobj: The object to be added.
 * @uid: Placeholder for server-generated UID.
 *
 * Calls the create_object method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_create_object (ECalBackendSync *backend, EDataCal *cal, gchar **calobj, gchar **uid)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->create_object_sync != NULL,
			      GNOME_Evolution_Calendar_UnsupportedMethod);

	LOCK_WRAPPER (create_object_sync, (backend, cal, calobj, uid));

	return status;
}

/**
 * e_cal_backend_sync_modify_object:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @calobj: Object to be modified.
 * @mod: Type of modification to be done.
 * @old_object: Placeholder for returning the old object as it was stored on the
 * backend.
 * @new_object: Placeholder for returning the new object as it has been stored
 * on the backend.
 *
 * Calls the modify_object method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_modify_object (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj,
				  CalObjModType mod, gchar **old_object, gchar **new_object)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->modify_object_sync != NULL,
			      GNOME_Evolution_Calendar_UnsupportedMethod);

	LOCK_WRAPPER (modify_object_sync, (backend, cal, calobj, mod, old_object, new_object));

	return status;
}

/**
 * e_cal_backend_sync_remove_object:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @uid: UID of the object to remove.
 * @rid: Recurrence ID of the instance to remove, or NULL if removing the
 * whole object.
 * @mod: Type of removal.
 * @old_object: Placeholder for returning the old object as it was stored on the
 * backend.
 * @object: Placeholder for returning the object after it has been modified (when
 * removing individual instances). If removing the whole object, this will be
 * NULL.
 *
 * Calls the remove_object method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_remove_object (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid,
				  CalObjModType mod, gchar **old_object, gchar **object)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->remove_object_sync != NULL,
			      GNOME_Evolution_Calendar_UnsupportedMethod);

	LOCK_WRAPPER (remove_object_sync, (backend, cal, uid, rid, mod, old_object, object));

	return status;
}

/**
 * e_cal_backend_sync_discard_alarm:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @uid: UID of the object to discard the alarm from.
 * @auid: UID of the alarm to be discarded.
 *
 * Calls the discard_alarm method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *auid)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->discard_alarm_sync != NULL,
			      GNOME_Evolution_Calendar_UnsupportedMethod);

	LOCK_WRAPPER (discard_alarm_sync, (backend, cal, uid, auid));

	return status;
}

/**
 * e_cal_backend_sync_receive_objects:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @calobj: iCalendar object to receive.
 *
 * Calls the receive_objects method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_receive_objects (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->receive_objects_sync != NULL,
			      GNOME_Evolution_Calendar_UnsupportedMethod);

	LOCK_WRAPPER (receive_objects_sync, (backend, cal, calobj));

	return status;
}

/**
 * e_cal_backend_sync_send_objects:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @calobj: The iCalendar object to send.
 * @users: List of users to send notifications to.
 * @modified_calobj: Placeholder for the iCalendar object after being modified.
 *
 * Calls the send_objects method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_send_objects (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj, GList **users,
				 gchar **modified_calobj)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->send_objects_sync != NULL,
			      GNOME_Evolution_Calendar_UnsupportedMethod);

	LOCK_WRAPPER (send_objects_sync, (backend, cal, calobj, users, modified_calobj));

	return status;
}

/**
 * e_cal_backend_sync_get_default_object:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @object: Placeholder for returned object.
 *
 * Calls the get_default_object method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_get_default_object (ECalBackendSync *backend, EDataCal *cal, gchar **object)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (object, GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (get_default_object_sync, (backend, cal, object));

	return status;
}

/**
 * e_cal_backend_sync_get_object:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @uid: UID of the object to get.
 * @rid: Recurrence ID of the specific instance to get, or NULL if getting the
 * master object.
 * @object: Placeholder for returned object.
 *
 * Calls the get_object method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_get_object (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid, gchar **object)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (object, GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (get_object_sync, (backend, cal, uid, rid, object));

	return status;
}

/**
 * e_cal_backend_sync_get_object_list:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @sexp: Search query.
 * @objects: Placeholder for list of returned objects.
 *
 * Calls the get_object_list method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_get_object_list (ECalBackendSync *backend, EDataCal *cal, const gchar *sexp, GList **objects)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (objects, GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (get_object_list_sync, (backend, cal, sexp, objects));

	return status;
}

/**
 * e_cal_backend_sync_get_attachment_list:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @uid: Unique id of the calendar object.
 * @rid: Recurrence id of the calendar object.
 * @attachments: Placeholder for list of returned attachment uris.
 *
 * Calls the get_attachment_list method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_get_attachment_list (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid, GSList **attachments)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (attachments, GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (get_attachment_list_sync, (backend, cal, uid, rid, attachments));

	return status;
}

/**
 * e_cal_backend_sync_get_timezone:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @tzid: ID of the timezone to retrieve.
 * @object: Placeholder for the returned timezone.
 *
 * Calls the get_timezone_sync method on the given backend.
 * This method is not mandatory on the backend, because here
 * is used internal_get_timezone call to fetch timezone from
 * it and that is transformed to a string. In other words,
 * any object deriving from ECalBackendSync can implement only
 * internal_get_timezone and can skip implementation of
 * get_timezone_sync completely.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_get_timezone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzid, gchar **object)
{
	ECalBackendSyncStatus status = GNOME_Evolution_Calendar_ObjectNotFound;

	g_return_val_if_fail (E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	if (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_timezone_sync) {
		LOCK_WRAPPER (get_timezone_sync, (backend, cal, tzid, object));
	}

	if (object && !*object) {
		icaltimezone *zone = NULL;

		if (backend->priv->mutex_lock)
			g_mutex_lock (backend->priv->sync_mutex);
		zone = e_cal_backend_internal_get_timezone (E_CAL_BACKEND (backend), tzid);
		if (backend->priv->mutex_lock)
			g_mutex_unlock (backend->priv->sync_mutex);

		if (!zone) {
			status = GNOME_Evolution_Calendar_ObjectNotFound;
		} else {
			icalcomponent *icalcomp;

			icalcomp = icaltimezone_get_component (zone);

			if (!icalcomp) {
				status = GNOME_Evolution_Calendar_InvalidObject;
			} else {
				*object = icalcomponent_as_ical_string_r (icalcomp);
				status = GNOME_Evolution_Calendar_Success;
			}
		}
	}

	return status;
}

/**
 * e_cal_backend_sync_add_timezone:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @tzobj: VTIMEZONE object to be added.
 *
 * Calls the add_timezone method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_add_timezone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (add_timezone_sync, (backend, cal, tzobj));

	return status;
}

/**
 * e_cal_backend_sync_set_default_zone:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @tz: Timezone object as string.
 *
 * Calls the set_default_timezone method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_set_default_zone (ECalBackendSync *backend, EDataCal *cal, const gchar *tz)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	/* Old backends might be using the set_default_timezone */
	if (!E_CAL_BACKEND_SYNC_GET_CLASS (backend)->set_default_zone_sync) {
		icalcomponent *icalcomp = icalparser_parse_string (tz);
		const gchar *tzid = NULL;
		icaltimezone *zone = icaltimezone_new ();

		if (icalcomp) {
			icaltimezone_set_component (zone, icalcomp);
			tzid = icaltimezone_get_tzid (zone);
		}

		LOCK_WRAPPER (set_default_timezone_sync, (backend, cal, tzid));

		icaltimezone_free (zone, 1);

		return status;
	}

	LOCK_WRAPPER (set_default_zone_sync, (backend, cal, tz));

	return status;
}

/**
 * @deprecated This virual function should not be used in the backends, use
 * e_cal_backend_sync_set_zone instead. This function restricts the default timezone
 * to be libical builtin timezone.
 *
 * e_cal_backend_sync_set_default_timezone:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @tzid: ID of the timezone to be set as default.
 *
 * Calls the set_default_timezone method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_set_default_timezone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzid)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (set_default_timezone_sync, (backend, cal, tzid));

	return status;
}

/**
 * e_cal_backend_sync_get_changes:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @change_id: ID of the change to use as base.
 * @adds: Placeholder for list of additions.
 * @modifies: Placeholder for list of modifications.
 * @deletes: Placeholder for list of deletions.
 *
 * Calls the get_changes method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_get_changes (ECalBackendSync *backend, EDataCal *cal, const gchar *change_id,
				GList **adds, GList **modifies, GList **deletes)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (get_changes_sync, (backend, cal, change_id, adds, modifies, deletes));

	return status;
}

/**
 * e_cal_backend_sync_get_free_busy:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @users: List of users to get F/B info from.
 * @start: Time range start.
 * @end: Time range end.
 * @freebusy: Placeholder for F/B information.
 *
 * Calls the get_free_busy method on the given backend.
 *
 * Return value: Status code.
 */
ECalBackendSyncStatus
e_cal_backend_sync_get_free_busy (ECalBackendSync *backend, EDataCal *cal, GList *users,
				  time_t start, time_t end, GList **freebusy)
{
	ECalBackendSyncStatus status;

	g_return_val_if_fail (E_IS_CAL_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	LOCK_WRAPPER (get_freebusy_sync, (backend, cal, users, start, end, freebusy));

	return status;
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
_e_cal_backend_get_cal_address (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	ECalBackendSyncStatus status;
	gchar *address = NULL;

	status = e_cal_backend_sync_get_cal_address (E_CAL_BACKEND_SYNC (backend), cal, &address);

	e_data_cal_notify_cal_address (cal, context, status, address);

	g_free (address);
}

static void
_e_cal_backend_get_alarm_email_address (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	ECalBackendSyncStatus status;
	gchar *address = NULL;

	status = e_cal_backend_sync_get_alarm_email_address (E_CAL_BACKEND_SYNC (backend), cal, &address);

	e_data_cal_notify_alarm_email_address (cal, context, status, address);

	g_free (address);
}

static void
_e_cal_backend_get_ldap_attribute (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	ECalBackendSyncStatus status;
	gchar *attribute = NULL;

	status = e_cal_backend_sync_get_ldap_attribute (E_CAL_BACKEND_SYNC (backend), cal, &attribute);

	e_data_cal_notify_ldap_attribute (cal, context, status, attribute);

	g_free (attribute);
}

static void
_e_cal_backend_get_static_capabilities (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	ECalBackendSyncStatus status;
	gchar *capabilities = NULL;

	status = e_cal_backend_sync_get_static_capabilities (E_CAL_BACKEND_SYNC (backend), cal, &capabilities);

	e_data_cal_notify_static_capabilities (cal, context, status, capabilities);

	g_free (capabilities);
}

static void
_e_cal_backend_open (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, gboolean only_if_exists,
		     const gchar *username, const gchar *password)
{
	ECalBackendSyncStatus status;

	status = e_cal_backend_sync_open (E_CAL_BACKEND_SYNC (backend), cal, only_if_exists, username, password);

	e_data_cal_notify_open (cal, context, status);
}

static void
_e_cal_backend_refresh (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	ECalBackendSyncStatus status;

	status = e_cal_backend_sync_refresh (E_CAL_BACKEND_SYNC (backend), cal);

	e_data_cal_notify_refresh (cal, context, status);
}

static void
_e_cal_backend_remove (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	ECalBackendSyncStatus status;

	status = e_cal_backend_sync_remove (E_CAL_BACKEND_SYNC (backend), cal);

	e_data_cal_notify_remove (cal, context, status);
}

static void
_e_cal_backend_create_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj)
{
	ECalBackendSyncStatus status;
	gchar *uid = NULL, *modified_calobj = (gchar *) calobj;

	status = e_cal_backend_sync_create_object (E_CAL_BACKEND_SYNC (backend), cal, &modified_calobj, &uid);

	e_data_cal_notify_object_created (cal, context, status, uid, modified_calobj);

	/* free memory */
	if (uid)
		g_free (uid);

	if (modified_calobj != calobj)
		g_free (modified_calobj);
}

static void
_e_cal_backend_modify_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj, CalObjModType mod)
{
	ECalBackendSyncStatus status;
	gchar *old_object = NULL;
	gchar *new_object = NULL;

	status = e_cal_backend_sync_modify_object (E_CAL_BACKEND_SYNC (backend), cal,
						   calobj, mod, &old_object, &new_object);

	if (new_object)
		e_data_cal_notify_object_modified (cal, context, status, old_object, new_object);
	else
		e_data_cal_notify_object_modified (cal, context, status, old_object, calobj);

	g_free (old_object);
	g_free (new_object);
}

static void
_e_cal_backend_remove_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid, CalObjModType mod)
{
	ECalBackendSyncStatus status;
	gchar *object = NULL, *old_object = NULL;

	status = e_cal_backend_sync_remove_object (E_CAL_BACKEND_SYNC (backend), cal, uid, rid, mod, &old_object, &object);

	if (status == GNOME_Evolution_Calendar_Success) {

		ECalComponentId *id = g_new0 (ECalComponentId, 1);
		id->uid = g_strdup (uid);

		if (mod == CALOBJ_MOD_THIS)
			id->rid = g_strdup (rid);

		if (!object)
			e_data_cal_notify_object_removed (cal, context, status, id, old_object, object);
		else
			e_data_cal_notify_object_modified (cal, context, status, old_object, object);

		e_cal_component_free_id (id);
	} else
		e_data_cal_notify_object_removed (cal, context, status, NULL, old_object, object);

	g_free (old_object);
	g_free (object);
}

static void
_e_cal_backend_discard_alarm (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *auid)
{
	ECalBackendSyncStatus status;

	status = e_cal_backend_sync_discard_alarm (E_CAL_BACKEND_SYNC (backend), cal, uid, auid);

	e_data_cal_notify_alarm_discarded (cal, context, status);
}

static void
_e_cal_backend_receive_objects (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj)
{
	ECalBackendSyncStatus status;

	status = e_cal_backend_sync_receive_objects (E_CAL_BACKEND_SYNC (backend), cal, calobj);

	e_data_cal_notify_objects_received (cal, context, status);
}

static void
_e_cal_backend_send_objects (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj)
{
	ECalBackendSyncStatus status;
	GList *users = NULL;
	gchar *modified_calobj = NULL;

	status = e_cal_backend_sync_send_objects (E_CAL_BACKEND_SYNC (backend), cal, calobj, &users, &modified_calobj);
	e_data_cal_notify_objects_sent (cal, context, status, users, modified_calobj);

	g_list_foreach (users, (GFunc) g_free, NULL);
	g_list_free (users);
	g_free (modified_calobj);
}

static void
_e_cal_backend_get_default_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	ECalBackendSyncStatus status;
	gchar *object = NULL;

	status = e_cal_backend_sync_get_default_object (E_CAL_BACKEND_SYNC (backend), cal, &object);

	e_data_cal_notify_default_object (cal, context, status, object);

	g_free (object);
}

static void
_e_cal_backend_get_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid)
{
	ECalBackendSyncStatus status;
	gchar *object = NULL;

	status = e_cal_backend_sync_get_object (E_CAL_BACKEND_SYNC (backend), cal, uid, rid, &object);

	e_data_cal_notify_object (cal, context, status, object);

	g_free (object);
}

static void
_e_cal_backend_get_attachment_list (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid)
{
	ECalBackendSyncStatus status;
	GSList *list = NULL;

	status = e_cal_backend_sync_get_attachment_list (E_CAL_BACKEND_SYNC (backend), cal, uid, rid, &list);

	e_data_cal_notify_attachment_list (cal, context, status, list);

	g_slist_foreach (list, (GFunc) g_free, NULL);
	g_free (list);
}

static void
_e_cal_backend_get_object_list (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *sexp)
{
	ECalBackendSyncStatus status;
	GList *objects = NULL, *l;

	status = e_cal_backend_sync_get_object_list (E_CAL_BACKEND_SYNC (backend), cal, sexp, &objects);

	e_data_cal_notify_object_list (cal, context, status, objects);

	for (l = objects; l; l = l->next)
		g_free (l->data);
	g_list_free (objects);
}

static void
_e_cal_backend_get_timezone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzid)
{
	ECalBackendSyncStatus status;
	gchar *object = NULL;

	status = e_cal_backend_sync_get_timezone (E_CAL_BACKEND_SYNC (backend), cal, tzid, &object);

	if (!object && tzid) {
		/* fallback if tzid contains only the location of timezone */
		gint i, slashes = 0;

		for (i = 0; tzid [i]; i++) {
			if (tzid [i] == '/')
				slashes++;
		}

		if (slashes == 1) {
			icalcomponent *icalcomp = NULL, *free_comp = NULL;

			icaltimezone *zone = icaltimezone_get_builtin_timezone (tzid);
			if (!zone) {
				/* Try fetching the timezone from zone directory. There are some timezones like MST, US/Pacific etc. which do not appear in
				zone.tab, so they will not be available in the libical builtin timezone */
				icalcomp = free_comp = icaltzutil_fetch_timezone (tzid);
			}

			if (zone)
				icalcomp = icaltimezone_get_component (zone);

			if (icalcomp) {
				icalcomponent *clone = icalcomponent_new_clone (icalcomp);
				icalproperty *prop;

				prop = icalcomponent_get_first_property (clone, ICAL_TZID_PROPERTY);
				if (prop) {
					/* change tzid to our, because the component has the buildin tzid */
					icalproperty_set_tzid (prop, tzid);

					object = icalcomponent_as_ical_string_r (clone);
					status = GNOME_Evolution_Calendar_Success;
				}
				icalcomponent_free (clone);
			}

			if (free_comp)
				icalcomponent_free (free_comp);
		}

		/* also cache this timezone to backend */
		if (object)
			e_cal_backend_sync_add_timezone (E_CAL_BACKEND_SYNC (backend), cal, object);
	}

	e_data_cal_notify_timezone_requested (cal, context, status, object);

	g_free (object);
}

static void
_e_cal_backend_add_timezone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzobj)
{
	ECalBackendSyncStatus status;

	status = e_cal_backend_sync_add_timezone (E_CAL_BACKEND_SYNC (backend), cal, tzobj);

	e_data_cal_notify_timezone_added (cal, context, status, tzobj);
}

/* The default implementation is looking for timezone in the ical's builtin timezones,
   and if that fails, then it tries to extract the location from the tzid and get the
   timezone based on it. If even that fails, then it's returning UTC timezone.
   That means, that any object deriving from ECalBackendSync is supposed to implement
   this function for checking for a timezone in its own timezone cache, and if that
   fails, then call parent's object internal_get_timezone, and that's all.
 */
static icaltimezone *
_e_cal_backend_internal_get_timezone (ECalBackend *backend, const gchar *tzid)
{
	icaltimezone *zone = NULL;

	if (!tzid || !*tzid)
		return NULL;

	zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);

	if (!zone) {
		const gchar *s, *slash1 = NULL, *slash2 = NULL;

		/* get builtin by a location, if any */
		for (s = tzid; *s; s++) {
			if (*s == '/') {
				slash1 = slash2;
				slash2 = s;
			}
		}

		if (slash1)
			zone = icaltimezone_get_builtin_timezone (slash1 + 1);
		else if (slash2)
			zone = icaltimezone_get_builtin_timezone (tzid);
	}

	if (!zone)
		zone = icaltimezone_get_utc_timezone ();

	return zone;
}

static void
_e_cal_backend_set_default_zone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tz)
{
	ECalBackendSyncStatus status;

	status = e_cal_backend_sync_set_default_zone (E_CAL_BACKEND_SYNC (backend), cal, tz);

	e_data_cal_notify_default_timezone_set (cal, context, status);
}

static void
_e_cal_backend_set_default_timezone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzid)
{
	ECalBackendSyncStatus status;

	status = e_cal_backend_sync_set_default_timezone (E_CAL_BACKEND_SYNC (backend), cal, tzid);

	e_data_cal_notify_default_timezone_set (cal, context, status);
}

static void
_e_cal_backend_get_changes (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *change_id)
{
	ECalBackendSyncStatus status;
	GList *adds = NULL, *modifies = NULL, *deletes = NULL, *l;

	status = e_cal_backend_sync_get_changes (E_CAL_BACKEND_SYNC (backend), cal, change_id,
					       &adds, &modifies, &deletes);

	e_data_cal_notify_changes (cal, context, status, adds, modifies, deletes);

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
_e_cal_backend_get_free_busy (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, GList *users, time_t start, time_t end)
{
	ECalBackendSyncStatus status;
	GList *freebusy = NULL, *l;

	status = e_cal_backend_sync_get_free_busy (E_CAL_BACKEND_SYNC (backend), cal, users, start, end, &freebusy);

	e_data_cal_notify_free_busy (cal, context, status, freebusy);

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
	backend_class->refresh = _e_cal_backend_refresh;
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
	backend_class->get_attachment_list = _e_cal_backend_get_attachment_list;
	backend_class->get_timezone = _e_cal_backend_get_timezone;
	backend_class->add_timezone = _e_cal_backend_add_timezone;
	backend_class->set_default_timezone = _e_cal_backend_set_default_timezone;
	backend_class->set_default_zone = _e_cal_backend_set_default_zone;
	backend_class->get_changes = _e_cal_backend_get_changes;
	backend_class->get_free_busy = _e_cal_backend_get_free_busy;
	backend_class->internal_get_timezone = _e_cal_backend_internal_get_timezone;

	object_class->dispose = e_cal_backend_sync_dispose;
}

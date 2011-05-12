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
  (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->func) args; \
  if (backend->priv->mutex_lock) \
    g_mutex_unlock (backend->priv->sync_mutex); \

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
 * @error: Out parameter for a #GError.
 *
 * Calls the is_read_only method on the given backend.
 */
void
e_cal_backend_sync_is_read_only  (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (read_only, InvalidArg);

	LOCK_WRAPPER (is_read_only_sync, (backend, cal, read_only, error));
}

/**
 * e_cal_backend_sync_get_cal_address:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @address: Return value for the address.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_cal_address method on the given backend.
 */
void
e_cal_backend_sync_get_cal_address  (ECalBackendSync *backend, EDataCal *cal, gchar **address, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (address, InvalidArg);

	LOCK_WRAPPER (get_cal_address_sync, (backend, cal, address, error));
}

/**
 * e_cal_backend_sync_get_alarm_email_address:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @address: Return value for the address.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_alarm_email_address method on the given backend.
 */
void
e_cal_backend_sync_get_alarm_email_address  (ECalBackendSync *backend, EDataCal *cal, gchar **address, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (address, InvalidArg);

	LOCK_WRAPPER (get_alarm_email_address_sync, (backend, cal, address, error));
}

/**
 * e_cal_backend_sync_get_ldap_attribute:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @attribute: Return value for LDAP attribute.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_ldap_attribute method on the given backend.
 */
void
e_cal_backend_sync_get_ldap_attribute  (ECalBackendSync *backend, EDataCal *cal, gchar **attribute, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (attribute, InvalidArg);

	LOCK_WRAPPER (get_ldap_attribute_sync, (backend, cal, attribute, error));
}

/**
 * e_cal_backend_sync_get_static_capabilities:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @capabilities: Return value for capabilities.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_capabilities method on the given backend.
 */
void
e_cal_backend_sync_get_static_capabilities  (ECalBackendSync *backend, EDataCal *cal, gchar **capabilities, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (capabilities, InvalidArg);

	LOCK_WRAPPER (get_static_capabilities_sync, (backend, cal, capabilities, error));
}

/**
 * e_cal_backend_sync_open:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @only_if_exists: Whether to open the calendar if and only if it already exists
 * or just create it when it does not exist.
 * @username: User name to use for authentication.
 * @password: Password to use for authentication.
 * @error: Out parameter for a #GError.
 *
 * Calls the open method on the given backend.
 */
void
e_cal_backend_sync_open  (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists,
			  const gchar *username, const gchar *password, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);

	LOCK_WRAPPER (open_sync, (backend, cal, only_if_exists, username, password, error));
}

/**
 * e_cal_backend_sync_remove:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @error: Out parameter for a #GError.
 *
 * Calls the remove method on the given backend.
 */
void
e_cal_backend_sync_remove  (ECalBackendSync *backend, EDataCal *cal, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);

	LOCK_WRAPPER (remove_sync, (backend, cal, error));
}

/**
 * e_cal_backend_sync_refresh:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @error: Out parameter for a #GError.
 *
 * Calls the refresh method on the given backend.
 *
 * Since: 2.30
 */
void
e_cal_backend_sync_refresh  (ECalBackendSync *backend, EDataCal *cal, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->refresh_sync != NULL, UnsupportedMethod);

	LOCK_WRAPPER (refresh_sync, (backend, cal, error));
}

/**
 * e_cal_backend_sync_create_object:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @calobj: The object to be added.
 * @uid: Placeholder for server-generated UID.
 * @error: Out parameter for a #GError.
 *
 * Calls the create_object method on the given backend.
 */
void
e_cal_backend_sync_create_object (ECalBackendSync *backend, EDataCal *cal, gchar **calobj, gchar **uid, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->create_object_sync != NULL, UnsupportedMethod);

	LOCK_WRAPPER (create_object_sync, (backend, cal, calobj, uid, error));
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
 * @error: Out parameter for a #GError.
 *
 * Calls the modify_object method on the given backend.
 */
void
e_cal_backend_sync_modify_object (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj,
				  CalObjModType mod, gchar **old_object, gchar **new_object, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->modify_object_sync != NULL, UnsupportedMethod);

	LOCK_WRAPPER (modify_object_sync, (backend, cal, calobj, mod, old_object, new_object, error));
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
 * @error: Out parameter for a #GError.
 *
 * Calls the remove_object method on the given backend.
 */
void
e_cal_backend_sync_remove_object (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid,
				  CalObjModType mod, gchar **old_object, gchar **object, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->remove_object_sync != NULL, UnsupportedMethod);

	LOCK_WRAPPER (remove_object_sync, (backend, cal, uid, rid, mod, old_object, object, error));
}

/**
 * e_cal_backend_sync_discard_alarm:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @uid: UID of the object to discard the alarm from.
 * @auid: UID of the alarm to be discarded.
 * @error: Out parameter for a #GError.
 *
 * Calls the discard_alarm method on the given backend.
 */
void
e_cal_backend_sync_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *auid, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->discard_alarm_sync != NULL, UnsupportedMethod);

	LOCK_WRAPPER (discard_alarm_sync, (backend, cal, uid, auid, error));
}

/**
 * e_cal_backend_sync_receive_objects:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @calobj: iCalendar object to receive.
 * @error: Out parameter for a #GError.
 *
 * Calls the receive_objects method on the given backend.
 */
void
e_cal_backend_sync_receive_objects (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->receive_objects_sync != NULL, UnsupportedMethod);

	LOCK_WRAPPER (receive_objects_sync, (backend, cal, calobj, error));
}

/**
 * e_cal_backend_sync_send_objects:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @calobj: The iCalendar object to send.
 * @users: List of users to send notifications to.
 * @modified_calobj: Placeholder for the iCalendar object after being modified.
 * @error: Out parameter for a #GError.
 *
 * Calls the send_objects method on the given backend.
 */
void
e_cal_backend_sync_send_objects (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj, GList **users,
				 gchar **modified_calobj, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->send_objects_sync != NULL, UnsupportedMethod);

	LOCK_WRAPPER (send_objects_sync, (backend, cal, calobj, users, modified_calobj, error));
}

/**
 * e_cal_backend_sync_get_default_object:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @object: Placeholder for returned object.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_default_object method on the given backend.
 */
void
e_cal_backend_sync_get_default_object (ECalBackendSync *backend, EDataCal *cal, gchar **object, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (object, InvalidArg);

	LOCK_WRAPPER (get_default_object_sync, (backend, cal, object, error));
}

/**
 * e_cal_backend_sync_get_object:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @uid: UID of the object to get.
 * @rid: Recurrence ID of the specific instance to get, or NULL if getting the
 * master object.
 * @object: Placeholder for returned object.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_object method on the given backend.
 */
void
e_cal_backend_sync_get_object (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid, gchar **object, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (object, InvalidArg);

	LOCK_WRAPPER (get_object_sync, (backend, cal, uid, rid, object, error));
}

/**
 * e_cal_backend_sync_get_object_list:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @sexp: Search query.
 * @objects: Placeholder for list of returned objects.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_object_list method on the given backend.
 */
void
e_cal_backend_sync_get_object_list (ECalBackendSync *backend, EDataCal *cal, const gchar *sexp, GList **objects, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (objects, InvalidArg);

	LOCK_WRAPPER (get_object_list_sync, (backend, cal, sexp, objects, error));
}

/**
 * e_cal_backend_sync_get_attachment_list:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @uid: Unique id of the calendar object.
 * @rid: Recurrence id of the calendar object.
 * @attachments: Placeholder for list of returned attachment uris.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_attachment_list method on the given backend.
 */
void
e_cal_backend_sync_get_attachment_list (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid, GSList **attachments, GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (attachments, InvalidArg);

	LOCK_WRAPPER (get_attachment_list_sync, (backend, cal, uid, rid, attachments, error));
}

/**
 * e_cal_backend_sync_get_timezone:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @tzid: ID of the timezone to retrieve.
 * @object: Placeholder for the returned timezone.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_timezone_sync method on the given backend.
 * This method is not mandatory on the backend, because here
 * is used internal_get_timezone call to fetch timezone from
 * it and that is transformed to a string. In other words,
 * any object deriving from ECalBackendSync can implement only
 * internal_get_timezone and can skip implementation of
 * get_timezone_sync completely.
 */
void
e_cal_backend_sync_get_timezone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzid, gchar **object, GError **error)
{
	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);

	if (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_timezone_sync) {
		LOCK_WRAPPER (get_timezone_sync, (backend, cal, tzid, object, error));
	}

	if (object && !*object) {
		icaltimezone *zone = NULL;

		if (backend->priv->mutex_lock)
			g_mutex_lock (backend->priv->sync_mutex);
		zone = e_cal_backend_internal_get_timezone (E_CAL_BACKEND (backend), tzid);
		if (backend->priv->mutex_lock)
			g_mutex_unlock (backend->priv->sync_mutex);

		if (!zone) {
			g_propagate_error (error, e_data_cal_create_error (ObjectNotFound, NULL));
		} else {
			icalcomponent *icalcomp;

			icalcomp = icaltimezone_get_component (zone);

			if (!icalcomp) {
				g_propagate_error (error, e_data_cal_create_error (InvalidObject, NULL));
			} else {
				*object = icalcomponent_as_ical_string_r (icalcomp);
			}
		}
	}
}

/**
 * e_cal_backend_sync_add_timezone:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @tzobj: VTIMEZONE object to be added.
 * @error: Out parameter for a #GError.
 *
 * Calls the add_timezone method on the given backend.
 */
void
e_cal_backend_sync_add_timezone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj, GError **error)
{
	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);

	LOCK_WRAPPER (add_timezone_sync, (backend, cal, tzobj, error));
}

/**
 * e_cal_backend_sync_set_default_zone:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @tz: Timezone object as string.
 * @error: Out parameter for a #GError.
 *
 * Calls the set_default_zone method on the given backend.
 */
void
e_cal_backend_sync_set_default_zone (ECalBackendSync *backend, EDataCal *cal, const gchar *tz, GError **error)
{
	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);

	LOCK_WRAPPER (set_default_zone_sync, (backend, cal, tz, error));
}

/**
 * e_cal_backend_sync_get_changes:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @change_id: ID of the change to use as base.
 * @adds: Placeholder for list of additions.
 * @modifies: Placeholder for list of modifications.
 * @deletes: Placeholder for list of deletions.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_changes method on the given backend.
 */
void
e_cal_backend_sync_get_changes (ECalBackendSync *backend, EDataCal *cal, const gchar *change_id,
				GList **adds, GList **modifies, GList **deletes, GError **error)
{
	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);

	LOCK_WRAPPER (get_changes_sync, (backend, cal, change_id, adds, modifies, deletes, error));
}

/**
 * e_cal_backend_sync_get_free_busy:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @users: List of users to get F/B info from.
 * @start: Time range start.
 * @end: Time range end.
 * @freebusy: Placeholder for F/B information.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_free_busy method on the given backend.
 */
void
e_cal_backend_sync_get_free_busy (ECalBackendSync *backend, EDataCal *cal, GList *users,
				  time_t start, time_t end, GList **freebusy, GError **error)
{
	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);

	LOCK_WRAPPER (get_freebusy_sync, (backend, cal, users, start, end, freebusy, error));
}

static void
_e_cal_backend_is_read_only (ECalBackend *backend, EDataCal *cal)
{
	GError *error = NULL;
	gboolean read_only = TRUE;

	e_cal_backend_sync_is_read_only (E_CAL_BACKEND_SYNC (backend), cal, &read_only, &error);

	e_data_cal_notify_read_only (cal, error, read_only);
}

static void
_e_cal_backend_get_cal_address (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	GError *error = NULL;
	gchar *address = NULL;

	e_cal_backend_sync_get_cal_address (E_CAL_BACKEND_SYNC (backend), cal, &address, &error);

	e_data_cal_notify_cal_address (cal, context, error, address);

	g_free (address);
}

static void
_e_cal_backend_get_alarm_email_address (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	GError *error = NULL;
	gchar *address = NULL;

	e_cal_backend_sync_get_alarm_email_address (E_CAL_BACKEND_SYNC (backend), cal, &address, &error);

	e_data_cal_notify_alarm_email_address (cal, context, error, address);

	g_free (address);
}

static void
_e_cal_backend_get_ldap_attribute (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	GError *error = NULL;
	gchar *attribute = NULL;

	e_cal_backend_sync_get_ldap_attribute (E_CAL_BACKEND_SYNC (backend), cal, &attribute, &error);

	e_data_cal_notify_ldap_attribute (cal, context, error, attribute);

	g_free (attribute);
}

static void
_e_cal_backend_get_static_capabilities (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	GError *error = NULL;
	gchar *capabilities = NULL;

	e_cal_backend_sync_get_static_capabilities (E_CAL_BACKEND_SYNC (backend), cal, &capabilities, &error);

	e_data_cal_notify_static_capabilities (cal, context, error, capabilities);

	g_free (capabilities);
}

static void
_e_cal_backend_open (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, gboolean only_if_exists,
		     const gchar *username, const gchar *password)
{
	GError *error = NULL;

	e_cal_backend_sync_open (E_CAL_BACKEND_SYNC (backend), cal, only_if_exists, username, password, &error);

	e_data_cal_notify_open (cal, context, error);
}

static void
_e_cal_backend_refresh (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	GError *error = NULL;

	e_cal_backend_sync_refresh (E_CAL_BACKEND_SYNC (backend), cal, &error);

	e_data_cal_notify_refresh (cal, context, error);
}

static void
_e_cal_backend_remove (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	GError *error = NULL;

	e_cal_backend_sync_remove (E_CAL_BACKEND_SYNC (backend), cal, &error);

	e_data_cal_notify_remove (cal, context, error);
}

static void
_e_cal_backend_create_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj)
{
	GError *error = NULL;
	gchar *uid = NULL, *modified_calobj = (gchar *) calobj;

	e_cal_backend_sync_create_object (E_CAL_BACKEND_SYNC (backend), cal, &modified_calobj, &uid, &error);

	e_data_cal_notify_object_created (cal, context, error, uid, modified_calobj);

	/* free memory */
	if (uid)
		g_free (uid);

	if (modified_calobj != calobj)
		g_free (modified_calobj);
}

static void
_e_cal_backend_modify_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj, CalObjModType mod)
{
	GError *error = NULL;
	gchar *old_object = NULL;
	gchar *new_object = NULL;

	e_cal_backend_sync_modify_object (E_CAL_BACKEND_SYNC (backend), cal,
						   calobj, mod, &old_object, &new_object, &error);

	if (new_object)
		e_data_cal_notify_object_modified (cal, context, error, old_object, new_object);
	else
		e_data_cal_notify_object_modified (cal, context, error, old_object, calobj);

	g_free (old_object);
	g_free (new_object);
}

static void
_e_cal_backend_remove_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid, CalObjModType mod)
{
	GError *error = NULL;
	gchar *object = NULL, *old_object = NULL;

	e_cal_backend_sync_remove_object (E_CAL_BACKEND_SYNC (backend), cal, uid, rid, mod, &old_object, &object, &error);

	if (!error) {
		ECalComponentId *id = g_new0 (ECalComponentId, 1);
		id->uid = g_strdup (uid);

		if (mod == CALOBJ_MOD_THIS || mod == CALOBJ_MOD_ONLY_THIS)
			id->rid = g_strdup (rid);

		if (!object)
			e_data_cal_notify_object_removed (cal, context, error, id, old_object, object);
		else
			e_data_cal_notify_object_modified (cal, context, error, old_object, object);

		e_cal_component_free_id (id);
	} else
		e_data_cal_notify_object_removed (cal, context, error, NULL, old_object, object);

	g_free (old_object);
	g_free (object);
}

static void
_e_cal_backend_discard_alarm (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *auid)
{
	GError *error = NULL;

	e_cal_backend_sync_discard_alarm (E_CAL_BACKEND_SYNC (backend), cal, uid, auid, &error);

	e_data_cal_notify_alarm_discarded (cal, context, error);
}

static void
_e_cal_backend_receive_objects (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj)
{
	GError *error = NULL;

	e_cal_backend_sync_receive_objects (E_CAL_BACKEND_SYNC (backend), cal, calobj, &error);

	e_data_cal_notify_objects_received (cal, context, error);
}

static void
_e_cal_backend_send_objects (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj)
{
	GError *error = NULL;
	GList *users = NULL;
	gchar *modified_calobj = NULL;

	e_cal_backend_sync_send_objects (E_CAL_BACKEND_SYNC (backend), cal, calobj, &users, &modified_calobj, &error);
	e_data_cal_notify_objects_sent (cal, context, error, users, modified_calobj);

	g_list_foreach (users, (GFunc) g_free, NULL);
	g_list_free (users);
	g_free (modified_calobj);
}

static void
_e_cal_backend_get_default_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	GError *error = NULL;
	gchar *object = NULL;

	e_cal_backend_sync_get_default_object (E_CAL_BACKEND_SYNC (backend), cal, &object, &error);

	e_data_cal_notify_default_object (cal, context, error, object);

	g_free (object);
}

static void
_e_cal_backend_get_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid)
{
	GError *error = NULL;
	gchar *object = NULL;

	e_cal_backend_sync_get_object (E_CAL_BACKEND_SYNC (backend), cal, uid, rid, &object, &error);

	e_data_cal_notify_object (cal, context, error, object);

	g_free (object);
}

static void
_e_cal_backend_get_attachment_list (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid)
{
	GError *error = NULL;
	GSList *list = NULL;

	e_cal_backend_sync_get_attachment_list (E_CAL_BACKEND_SYNC (backend), cal, uid, rid, &list, &error);

	e_data_cal_notify_attachment_list (cal, context, error, list);

	g_slist_foreach (list, (GFunc) g_free, NULL);
	g_free (list);
}

static void
_e_cal_backend_get_object_list (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *sexp)
{
	GError *error = NULL;
	GList *objects = NULL, *l;

	e_cal_backend_sync_get_object_list (E_CAL_BACKEND_SYNC (backend), cal, sexp, &objects, &error);

	e_data_cal_notify_object_list (cal, context, error, objects);

	for (l = objects; l; l = l->next)
		g_free (l->data);
	g_list_free (objects);
}

static void
_e_cal_backend_get_timezone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzid)
{
	GError *error = NULL;
	gchar *object = NULL;

	e_cal_backend_sync_get_timezone (E_CAL_BACKEND_SYNC (backend), cal, tzid, &object, &error);

	if (!object && tzid) {
		/* fallback if tzid contains only the location of timezone */
		gint i, slashes = 0;

		for (i = 0; tzid[i]; i++) {
			if (tzid[i] == '/')
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
					g_clear_error (&error);
				}
				icalcomponent_free (clone);
			}

			if (free_comp)
				icalcomponent_free (free_comp);
		}

		/* also cache this timezone to backend */
		if (object)
			e_cal_backend_sync_add_timezone (E_CAL_BACKEND_SYNC (backend), cal, object, NULL);
	}

	e_data_cal_notify_timezone_requested (cal, context, error, object);

	g_free (object);
}

static void
_e_cal_backend_add_timezone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzobj)
{
	GError *error = NULL;

	e_cal_backend_sync_add_timezone (E_CAL_BACKEND_SYNC (backend), cal, tzobj, &error);

	e_data_cal_notify_timezone_added (cal, context, error, tzobj);
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
	GError *error = NULL;

	e_cal_backend_sync_set_default_zone (E_CAL_BACKEND_SYNC (backend), cal, tz, &error);

	e_data_cal_notify_default_timezone_set (cal, context, error);
}

static void
_e_cal_backend_get_changes (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *change_id)
{
	GError *error = NULL;
	GList *adds = NULL, *modifies = NULL, *deletes = NULL, *l;

	e_cal_backend_sync_get_changes (E_CAL_BACKEND_SYNC (backend), cal, change_id,
					       &adds, &modifies, &deletes, &error);

	e_data_cal_notify_changes (cal, context, error, adds, modifies, deletes);

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
	GError *error = NULL;
	GList *freebusy = NULL, *l;

	e_cal_backend_sync_get_free_busy (E_CAL_BACKEND_SYNC (backend), cal, users, start, end, &freebusy, &error);

	e_data_cal_notify_free_busy (cal, context, error, freebusy);

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
	backend_class->set_default_zone = _e_cal_backend_set_default_zone;
	backend_class->get_changes = _e_cal_backend_get_changes;
	backend_class->get_free_busy = _e_cal_backend_get_free_busy;
	backend_class->internal_get_timezone = _e_cal_backend_internal_get_timezone;

	object_class->dispose = e_cal_backend_sync_dispose;
}

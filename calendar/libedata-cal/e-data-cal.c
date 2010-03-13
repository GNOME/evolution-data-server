/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar client interface object
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2009 Intel Corporation
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Rodrigo Moya <rodrigo@ximian.com>
 *          Ross Burton <ross@linux.intel.com>
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

#include <libical/ical.h>
#include <glib/gi18n-lib.h>
#include <unistd.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <glib-object.h>

#include "e-data-cal.h"
#include "e-data-cal-enumtypes.h"

DBusGConnection *connection;

/* DBus glue */
static void impl_Cal_get_uri (EDataCal *cal, DBusGMethodInvocation *context);
static void impl_Cal_open (EDataCal *cal, gboolean only_if_exists, gchar *username, gchar *password, DBusGMethodInvocation *context);
static gboolean impl_Cal_close (EDataCal *cal, GError **error);
static void impl_Cal_refresh (EDataCal *cal, DBusGMethodInvocation *context);
static void impl_Cal_remove (EDataCal *cal, DBusGMethodInvocation *context);
static void impl_Cal_isReadOnly (EDataCal *cal, DBusGMethodInvocation *context);
static void impl_Cal_getCalAddress (EDataCal *cal, DBusGMethodInvocation *context);
static void impl_Cal_getAlarmEmailAddress (EDataCal *cal, DBusGMethodInvocation *context);
static void impl_Cal_getLdapAttribute (EDataCal *cal, DBusGMethodInvocation *context);
static void impl_Cal_getStaticCapabilities (EDataCal *cal, DBusGMethodInvocation *context);
static void impl_Cal_setMode (EDataCal *cal, EDataCalMode mode, DBusGMethodInvocation *context);
static void impl_Cal_getDefaultObject (EDataCal *cal, DBusGMethodInvocation *context);
static void impl_Cal_getObject (EDataCal *cal, const gchar *uid, const gchar *rid, DBusGMethodInvocation *context);
static void impl_Cal_getObjectList (EDataCal *cal, const gchar *sexp, DBusGMethodInvocation *context);
static void impl_Cal_getChanges (EDataCal *cal, const gchar *change_id, DBusGMethodInvocation *context);
static void impl_Cal_getFreeBusy (EDataCal *cal, const gchar **user_list, const gulong start, const gulong end, DBusGMethodInvocation *context);
static void impl_Cal_discardAlarm (EDataCal *cal, const gchar *uid, const gchar *auid, DBusGMethodInvocation *context);
static void impl_Cal_createObject (EDataCal *cal, const gchar *calobj, DBusGMethodInvocation *context);
static void impl_Cal_modifyObject (EDataCal *cal, const gchar *calobj, const EDataCalObjModType mod, DBusGMethodInvocation *context);
static void impl_Cal_removeObject (EDataCal *cal, const gchar *uid, const gchar *rid, const EDataCalObjModType mod, DBusGMethodInvocation *context);
static void impl_Cal_receiveObjects (EDataCal *cal, const gchar *calobj, DBusGMethodInvocation *context);
static void impl_Cal_sendObjects (EDataCal *cal, const gchar *calobj, DBusGMethodInvocation *context);
static void impl_Cal_getAttachmentList (EDataCal *cal, gchar *uid, gchar *rid, DBusGMethodInvocation *context);
static void impl_Cal_getQuery (EDataCal *cal, const gchar *sexp, DBusGMethodInvocation *context);
static void impl_Cal_getTimezone (EDataCal *cal, const gchar *tzid, DBusGMethodInvocation *context);
static void impl_Cal_addTimezone (EDataCal *cal, const gchar *tz, DBusGMethodInvocation *context);
static void impl_Cal_setDefaultTimezone (EDataCal *cal, const gchar *tz, DBusGMethodInvocation *context);
#include "e-data-cal-glue.h"

enum
{
  AUTH_REQUIRED,
  BACKEND_ERROR,
  READ_ONLY,
  MODE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (EDataCal, e_data_cal, G_TYPE_OBJECT);

#define E_DATA_CAL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_DATA_CAL, EDataCalPrivate))

struct _EDataCalPrivate {
	ECalBackend *backend;
	ESource *source;
	GHashTable *live_queries;
};

/* Create the EDataCal error quark */
GQuark
e_data_cal_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("e_data_cal_error");
  return quark;
}

/* Class init */
static void
e_data_cal_class_init (EDataCalClass *e_data_cal_class)
{
	/* TODO: finalise dispose */

	g_type_class_add_private (e_data_cal_class, sizeof (EDataCalPrivate));

	signals[AUTH_REQUIRED] =
	  g_signal_new ("auth-required",
						G_OBJECT_CLASS_TYPE (e_data_cal_class),
						G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
						0,
						NULL, NULL,
						g_cclosure_marshal_VOID__VOID,
						G_TYPE_NONE, 0);
	signals[BACKEND_ERROR] =
	  g_signal_new ("backend-error",
						G_OBJECT_CLASS_TYPE (e_data_cal_class),
						G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
						0,
						NULL, NULL,
						g_cclosure_marshal_VOID__STRING,
						G_TYPE_NONE, 1, G_TYPE_STRING);
	signals[READ_ONLY] =
	  g_signal_new ("readonly",
						G_OBJECT_CLASS_TYPE (e_data_cal_class),
						G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
						0,
						NULL, NULL,
						g_cclosure_marshal_VOID__BOOLEAN,
						G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals[MODE] =
	  g_signal_new ("mode",
						G_OBJECT_CLASS_TYPE (e_data_cal_class),
						G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
						0,
						NULL, NULL,
						g_cclosure_marshal_VOID__INT,
						G_TYPE_NONE, 1, G_TYPE_INT);

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (e_data_cal_class), &dbus_glib_e_data_cal_object_info);

	dbus_g_error_domain_register (E_DATA_CAL_ERROR, NULL, E_TYPE_DATA_CAL_CALL_STATUS);
}

/* Instance init */
static void
e_data_cal_init (EDataCal *ecal)
{
	ecal->priv = E_DATA_CAL_GET_PRIVATE (ecal);
}

EDataCal *
e_data_cal_new (ECalBackend *backend, ESource *source)
{
	EDataCal *cal;
	cal = g_object_new (E_TYPE_DATA_CAL, NULL);
	cal->priv->backend = backend;
	cal->priv->source = source;
	return cal;
}

/**
 * e_data_cal_get_source:
 * @cal: an #EDataCal
 *
 * Returns the #ESource for @cal.
 *
 * Returns: the #ESource for @cal
 *
 * Since: 2.30
 **/
ESource*
e_data_cal_get_source (EDataCal *cal)
{
  return cal->priv->source;
}

ECalBackend*
e_data_cal_get_backend (EDataCal *cal)
{
  return cal->priv->backend;
}

/* EDataCal::getUri method */
static void
impl_Cal_get_uri (EDataCal *cal, DBusGMethodInvocation *context)
{
	dbus_g_method_return (context, g_strdup (e_cal_backend_get_uri (cal->priv->backend)));
}

/* EDataCal::open method */
static void
impl_Cal_open (EDataCal *cal,
	       gboolean only_if_exists,
	       gchar *username,
	       gchar *password, DBusGMethodInvocation *context)
{
	e_cal_backend_open (cal->priv->backend, cal, context, only_if_exists, username, password);
}

/* EDataCal::close method */
static gboolean
impl_Cal_close (EDataCal *cal, GError **error)
{
	e_cal_backend_remove_client (cal->priv->backend, cal);
	g_object_unref (cal);
	return TRUE;
}

/* EDataCal::refresh method */
static void
impl_Cal_refresh (EDataCal *cal, DBusGMethodInvocation *context)
{
	e_cal_backend_refresh (cal->priv->backend, cal, context);
}

/* EDataCal::remove method */
static void
impl_Cal_remove (EDataCal *cal, DBusGMethodInvocation *context)
{
	e_cal_backend_remove (cal->priv->backend, cal, context);
}

/* EDataCal::isReadOnly method */
static void
impl_Cal_isReadOnly (EDataCal *cal, DBusGMethodInvocation *context)
{
	e_cal_backend_is_read_only (cal->priv->backend, cal);
	dbus_g_method_return (context);
}

/* EDataCal::getCalAddress method */
static void
impl_Cal_getCalAddress (EDataCal *cal, DBusGMethodInvocation *context)
{
	e_cal_backend_get_cal_address (cal->priv->backend, cal, context);
}

/* EDataCal::getAlarmEmailAddress method */
static void
impl_Cal_getAlarmEmailAddress (EDataCal *cal, DBusGMethodInvocation *context)
{
	e_cal_backend_get_alarm_email_address (cal->priv->backend, cal, context);
}

/* EDataCal::getLdapAttribute method */
static void
impl_Cal_getLdapAttribute (EDataCal *cal, DBusGMethodInvocation *context)
{
	e_cal_backend_get_ldap_attribute (cal->priv->backend, cal, context);
}

/* EDataCal::getSchedulingInformation method */
static void
impl_Cal_getStaticCapabilities (EDataCal *cal, DBusGMethodInvocation *context)
{
	e_cal_backend_get_static_capabilities (cal->priv->backend, cal, context);
}

/* EDataCal::setMode method */
static void
impl_Cal_setMode (EDataCal *cal,
		  EDataCalMode mode,
		  DBusGMethodInvocation *context)
{
	e_cal_backend_set_mode (cal->priv->backend, mode);
	dbus_g_method_return (context);
}

/* EDataCal::getDefaultObject method */
static void
impl_Cal_getDefaultObject (EDataCal *cal, DBusGMethodInvocation *context)
{
	e_cal_backend_get_default_object (cal->priv->backend, cal, context);
}

/* EDataCal::getObject method */
static void
impl_Cal_getObject (EDataCal *cal,
		    const gchar *uid,
		    const gchar *rid,
		    DBusGMethodInvocation *context)
{
	e_cal_backend_get_object (cal->priv->backend, cal, context, uid, rid);
}

/* EDataCal::getObjectList method */
static void
impl_Cal_getObjectList (EDataCal *cal,
			const gchar *sexp,
			DBusGMethodInvocation *context)
{
		e_cal_backend_get_object_list (cal->priv->backend, cal, context, sexp);
}

/* EDataCal::getChanges method */
static void
impl_Cal_getChanges (EDataCal *cal,
		     const gchar *change_id,
		     DBusGMethodInvocation *context)
{
       e_cal_backend_get_changes (cal->priv->backend, cal, context, change_id);
}

/* EDataCal::getFreeBusy method */
static void
impl_Cal_getFreeBusy (EDataCal *cal,
		      const gchar **user_list,
		      const gulong start,
		      const gulong end,
		      DBusGMethodInvocation *context)
{
	GList *users = NULL;

	if (user_list) {
		gint i;

		for (i = 0; user_list[i]; i++)
			users = g_list_append (users, (gpointer)user_list[i]);
	}

	/* call the backend's get_free_busy method */
	e_cal_backend_get_free_busy (cal->priv->backend, cal, context, users, (time_t)start, (time_t)end);
}

/* EDataCal::discardAlarm method */
static void
impl_Cal_discardAlarm (EDataCal *cal,
		       const gchar *uid,
		       const gchar *auid,
		       DBusGMethodInvocation *context)
{
	e_cal_backend_discard_alarm (cal->priv->backend, cal, context, uid, auid);
}

/* EDataCal::createObject method */
static void
impl_Cal_createObject (EDataCal *cal,
		       const gchar *calobj,
		       DBusGMethodInvocation *context)
{
	e_cal_backend_create_object (cal->priv->backend, cal, context, calobj);
}

/* EDataCal::modifyObject method */
static void
impl_Cal_modifyObject (EDataCal *cal,
		       const gchar *calobj,
		       const EDataCalObjModType mod,
		       DBusGMethodInvocation *context)
{
	e_cal_backend_modify_object (cal->priv->backend, cal, context, calobj, mod);
}

/* EDataCal::removeObject method */
static void
impl_Cal_removeObject (EDataCal *cal,
		       const gchar *uid,
		       const gchar *rid,
		       const EDataCalObjModType mod,
		       DBusGMethodInvocation *context)
{
	if (rid[0] == '\0')
		rid = NULL;

	e_cal_backend_remove_object (cal->priv->backend, cal, context, uid, rid, mod);
}

/* EDataCal::receiveObjects method */
static void
impl_Cal_receiveObjects (EDataCal *cal,
			 const gchar *calobj,
			 DBusGMethodInvocation *context)
{
	e_cal_backend_receive_objects (cal->priv->backend, cal, context, calobj);
}

/* EDataCal::sendObjects method */
static void
impl_Cal_sendObjects (EDataCal *cal,
		      const gchar *calobj,
		      DBusGMethodInvocation *context)
{
	e_cal_backend_send_objects (cal->priv->backend, cal, context, calobj);
}

/* EDataCal::getAttachmentList method */
static void
impl_Cal_getAttachmentList (EDataCal *cal,
                   gchar *uid,
                   gchar *rid,
                   DBusGMethodInvocation *context)
{
	e_cal_backend_get_attachment_list (cal->priv->backend, cal, context, uid, rid);
}

/* Function to get a new EDataCalView path, used by getQuery below */
static gchar *
construct_calview_path (void)
{
  static guint counter = 1;
  return g_strdup_printf ("/org/gnome/evolution/dataserver/calendar/CalView/%d/%d", getpid(), counter++);
}

/* EDataCal::getQuery method */
static void
impl_Cal_getQuery (EDataCal *cal,
		   const gchar *sexp,
		   DBusGMethodInvocation *context)
{
	EDataCalView *query;
	ECalBackendSExp *obj_sexp;
	gchar *path;

	/* we handle this entirely here, since it doesn't require any
	   backend involvement now that we have e_cal_view_start to
	   actually kick off the search. */

	obj_sexp = e_cal_backend_sexp_new (sexp);
	if (!obj_sexp) {
		e_data_cal_notify_query (cal, context, InvalidQuery, NULL);
		return;
	}

	path = construct_calview_path ();
	query = e_data_cal_view_new (cal->priv->backend, path, obj_sexp);
	if (!query) {
		g_object_unref (obj_sexp);
		e_data_cal_notify_query (cal, context, OtherError, NULL);
		return;
	}

	e_cal_backend_add_query (cal->priv->backend, query);

	e_data_cal_notify_query (cal, context, Success, path);

        g_free (path);
}

/* EDataCal::getTimezone method */
static void
impl_Cal_getTimezone (EDataCal *cal,
		      const gchar *tzid,
		      DBusGMethodInvocation *context)
{
	e_cal_backend_get_timezone (cal->priv->backend, cal, context, tzid);
}

/* EDataCal::addTimezone method */
static void
impl_Cal_addTimezone (EDataCal *cal,
		      const gchar *tz,
		      DBusGMethodInvocation *context)
{
	e_cal_backend_add_timezone (cal->priv->backend, cal, context, tz);
}

/* EDataCal::setDefaultTimezone method */
static void
impl_Cal_setDefaultTimezone (EDataCal *cal,
			     const gchar *tz,
			     DBusGMethodInvocation *context)
{
	e_cal_backend_set_default_zone (cal->priv->backend, cal, context, tz);
}

/**
 * e_data_cal_notify_read_only:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @read_only: Read only value.
 *
 * Notifies listeners of the completion of the is_read_only method call.
 */
void
e_data_cal_notify_read_only (EDataCal *cal, EDataCalCallStatus status, gboolean read_only)
{
	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	g_signal_emit (cal, signals[READ_ONLY], 0, read_only);
}

/**
 * e_data_cal_notify_cal_address:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @address: Calendar address.
 *
 * Notifies listeners of the completion of the get_cal_address method call.
 */
void
e_data_cal_notify_cal_address (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status, const gchar *address)
{
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot retrieve calendar address")));
	else
		dbus_g_method_return (method, address ? address : "");
}

/**
 * e_data_cal_notify_alarm_email_address:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @address: Alarm email address.
 *
 * Notifies listeners of the completion of the get_alarm_email_address method call.
 */
void
e_data_cal_notify_alarm_email_address (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status, const gchar *address)
{
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot retrieve calendar alarm e-mail address")));
	else
		dbus_g_method_return (method, address ? address : "");
}

/**
 * e_data_cal_notify_ldap_attribute:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @attibute: LDAP attribute.
 *
 * Notifies listeners of the completion of the get_ldap_attribute method call.
 */
void
e_data_cal_notify_ldap_attribute (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status, const gchar *attribute)
{
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot retrieve calendar's ldap attribute")));
	else
		dbus_g_method_return (method, attribute ? attribute : "");
}

/**
 * e_data_cal_notify_static_capabilities:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @capabilities: Static capabilities from the backend.
 *
 * Notifies listeners of the completion of the get_static_capabilities method call.
 */
void
e_data_cal_notify_static_capabilities (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status, const gchar *capabilities)
{
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot retrieve calendar scheduling information")));
	else
		dbus_g_method_return (method, capabilities ? capabilities : "");
}

/**
 * e_data_cal_notify_open:
 * @cal: A calendar client interface.
 * @status: Status code.
 *
 * Notifies listeners of the completion of the open method call.
 */
void
e_data_cal_notify_open (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status)
{
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot open calendar")));
	else
		dbus_g_method_return (method);
}

/**
 * e_data_cal_notify_refresh:
 * @cal: A calendar client interface.
 * @status: Status code.
 *
 * Notifies listeners of the completion of the refresh method call.
 *
 * Since: 2.30
 */
void
e_data_cal_notify_refresh (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status)
{
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot refresh calendar")));
	else
		dbus_g_method_return (method);
}

/**
 * e_data_cal_notify_remove:
 * @cal: A calendar client interface.
 * @status: Status code.
 *
 * Notifies listeners of the completion of the remove method call.
 */
void
e_data_cal_notify_remove (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status)
{
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot remove calendar")));
	else
		dbus_g_method_return (method);
}

/**
 * e_data_cal_notify_object_created:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @uid: UID of the object created.
 * @object: The object created as an iCalendar string.
 *
 * Notifies listeners of the completion of the create_object method call.
 */void
e_data_cal_notify_object_created (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status,
				  const gchar *uid, const gchar *object)
{
	DBusGMethodInvocation *method = context;
	if (status != Success) {
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot create calendar object")));
	} else {
		e_cal_backend_notify_object_created (cal->priv->backend, object);
		dbus_g_method_return (method, uid ? uid : "");
	}
}

/**
 * e_data_cal_notify_object_modified:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @old_object: The old object as an iCalendar string.
 * @object: The modified object as an iCalendar string.
 *
 * Notifies listeners of the completion of the modify_object method call.
 */
void
e_data_cal_notify_object_modified (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status,
				   const gchar *old_object, const gchar *object)
{
	DBusGMethodInvocation *method = context;
	if (status != Success) {
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot modify calendar object")));
	} else {
		e_cal_backend_notify_object_modified (cal->priv->backend, old_object, object);
		dbus_g_method_return (method);
	}
}

/**
 * e_data_cal_notify_object_removed:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @uid: UID of the removed object.
 * @old_object: The old object as an iCalendar string.
 * @object: The new object as an iCalendar string. This will not be NULL only
 * when removing instances of a recurring appointment.
 *
 * Notifies listeners of the completion of the remove_object method call.
 */
void
e_data_cal_notify_object_removed (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status,
				  const ECalComponentId *id, const gchar *old_object, const gchar *object)
{
	DBusGMethodInvocation *method = context;
	if (status != Success) {
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot remove calendar object")));
	} else {
		e_cal_backend_notify_object_removed (cal->priv->backend, id, old_object, object);
		dbus_g_method_return (method);
	}
}

/**
 * e_data_cal_notify_objects_received:
 * @cal: A calendar client interface.
 * @status: Status code.
 *
 * Notifies listeners of the completion of the receive_objects method call.
 */
void
e_data_cal_notify_objects_received (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status)
{
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot receive calendar objects")));
	else
		dbus_g_method_return (method);
}

/**
 * e_data_cal_notify_alarm_discarded:
 * @cal: A calendar client interface.
 * @status: Status code.
 *
 * Notifies listeners of the completion of the discard_alarm method call.
 */
void
e_data_cal_notify_alarm_discarded (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status)
{
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot discard calendar alarm")));
	else
		dbus_g_method_return (method);
}

/**
 * e_data_cal_notify_objects_sent:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @users: List of users.
 * @calobj: An iCalendar string representing the object sent.
 *
 * Notifies listeners of the completion of the send_objects method call.
 */
void
e_data_cal_notify_objects_sent (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status, GList *users, const gchar *calobj)
{
	DBusGMethodInvocation *method = context;
	if (status != Success) {
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot send calendar objects")));
	} else {
		gchar **users_array = NULL;

		if (users) {
			GList *l;
			gchar **user;

			users_array = g_new0 (gchar *, g_list_length (users)+1);
			if (users_array)
				for (l = users, user = users_array; l != NULL; l = l->next, user++)
					*user = g_strdup (l->data);
		}
		else
			users_array = g_new0 (gchar *, 1);

		dbus_g_method_return (method, users_array, calobj ? calobj : "");
		if (users_array)
			g_strfreev (users_array);
	}
}

/**
 * e_data_cal_notify_default_object:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @object: The default object as an iCalendar string.
 *
 * Notifies listeners of the completion of the get_default_object method call.
 */
void
e_data_cal_notify_default_object (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status, const gchar *object)
{
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot retrieve default calendar object path")));
	else
		dbus_g_method_return (method, object ? object : "");
}

/**
 * e_data_cal_notify_object:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @object: The object retrieved as an iCalendar string.
 *
 * Notifies listeners of the completion of the get_object method call.
 */
void
e_data_cal_notify_object (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status, const gchar *object)
{
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot retrieve calendar object path")));
	else
		dbus_g_method_return (method, object ? object : "");
}

/**
 * e_data_cal_notify_object_list:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @objects: List of retrieved objects.
 *
 * Notifies listeners of the completion of the get_object_list method call.
 */
void
e_data_cal_notify_object_list (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status, GList *objects)
{
	DBusGMethodInvocation *method = context;
	if (status != Success) {
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot retrieve calendar object list")));
	} else {
		gchar **seq = NULL;
		GList *l;
		gint i;

		seq = g_new0 (gchar *, g_list_length (objects)+1);
		for (l = objects, i = 0; l; l = l->next, i++) {
			seq[i] = l->data;
		}

		dbus_g_method_return (method, seq);

		g_free (seq);
	}
}

/**
 * e_data_cal_notify_attachment_list:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @attachments: List of retrieved attachment uri's.
 *
 * Notifies listeners of the completion of the get_attachment_list method call.+ */
void
e_data_cal_notify_attachment_list (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status, GSList *attachments)
{
	DBusGMethodInvocation *method = context;
	gchar **seq;
	GSList *l;
	gint i;

	seq = g_new0 (gchar *, g_slist_length (attachments));
	for (l = attachments, i = 0; l; l = l->next, i++) {
		seq[i] = g_strdup (l->data);
	}

	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Could not retrieve attachment list")));
	else
		dbus_g_method_return (method, seq);
}

/**
 * e_data_cal_notify_query:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @query: The new live query.
 *
 * Notifies listeners of the completion of the get_query method call.
 */
void
e_data_cal_notify_query (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status, const gchar *query)
{
	/*
	 * Only have a seperate notify function to follow suit with the rest of this
	 * file - it'd be much easier to just do the return in the above function
	 */
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Could not complete calendar query")));
	else
		dbus_g_method_return (method, query);
}

/**
 * e_data_cal_notify_timezone_requested:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @object: The requested timezone as an iCalendar string.
 *
 * Notifies listeners of the completion of the get_timezone method call.
 */
void
e_data_cal_notify_timezone_requested (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status, const gchar *object)
{
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Could not retrieve calendar time zone")));
	else
		dbus_g_method_return (method, object ? object : "");
}

/**
 * e_data_cal_notify_timezone_added:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @tzid: ID of the added timezone.
 *
 * Notifies listeners of the completion of the add_timezone method call.
 */
void
e_data_cal_notify_timezone_added (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status, const gchar *tzid)
{
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Could not add calendar time zone")));
	else
		dbus_g_method_return (method, tzid ? tzid : "");
}

/**
 * e_data_cal_notify_default_timezone_set:
 * @cal: A calendar client interface.
 * @status: Status code.
 *
 * Notifies listeners of the completion of the set_default_timezone method call.
 */
void
e_data_cal_notify_default_timezone_set (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status)
{
	DBusGMethodInvocation *method = context;
	if (status != Success)
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Could not set default calendar time zone")));
	else
		dbus_g_method_return (method);
}

/**
 * e_data_cal_notify_changes:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @adds: List of additions.
 * @modifies: List of modifications.
 * @deletes: List of removals.
 *
 * Notifies listeners of the completion of the get_changes method call.
 */
void
e_data_cal_notify_changes (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status,
			   GList *adds, GList *modifies, GList *deletes)
{
	DBusGMethodInvocation *method = context;
	if (status != Success) {
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot retrieve calendar changes")));
	} else {
		gchar **additions, **modifications, **removals;
		GList *l;
		gint i;

		additions = NULL;
		if (adds) {
			additions = g_new0 (gchar *, g_list_length (adds) + 1);
			if (additions)
				for (i = 0, l = adds; l; i++, l = l->next)
					additions[i] = g_strdup (l->data);
		}

		modifications = NULL;
		if (modifies) {
			modifications = g_new0 (gchar *, g_list_length (modifies) + 1);
			if (modifications)
				for (i = 0, l = modifies; l; i++, l = l->next)
					modifications[i] = g_strdup (l->data);
		}

		removals = NULL;
		if (deletes) {
			removals = g_new0 (gchar *, g_list_length (deletes) + 1);
			if (removals)
				for (i = 0, l = deletes; l; i++, l = l->next)
					removals[i] = g_strdup (l->data);
		}

		dbus_g_method_return (method, additions, modifications, removals);
		if (additions) g_strfreev (additions);
		if (modifications) g_strfreev (modifications);
		if (removals) g_strfreev (removals);
	}
}

/**
 * e_data_cal_notify_free_busy:
 * @cal: A calendar client interface.
 * @status: Status code.
 * @freebusy: List of free/busy objects.
 *
 * Notifies listeners of the completion of the get_free_busy method call.
 */
void
e_data_cal_notify_free_busy (EDataCal *cal, EServerMethodContext context, EDataCalCallStatus status, GList *freebusy)
{
	DBusGMethodInvocation *method = context;
	if (status != Success) {
		dbus_g_method_return_error (method, g_error_new (E_DATA_CAL_ERROR, status, _("Cannot retrieve calendar free/busy list")));
	} else {
		gchar **seq;
		GList *l;
		gint i;

		seq = g_new0 (gchar *, g_list_length (freebusy) + 1);
		for (i = 0, l = freebusy; l; i++, l = l->next) {
			seq[i] = g_strdup (l->data);
		}

		dbus_g_method_return (method, seq);
		g_strfreev (seq);
	}
}

/**
 * e_data_cal_notify_mode:
 * @cal: A calendar client interface.
 * @status: Status of the mode set.
 * @mode: The current mode.
 *
 * Notifies the listener of the results of a set_mode call.
 **/
void
e_data_cal_notify_mode (EDataCal *cal,
			EDataCalViewListenerSetModeStatus status,
			EDataCalMode mode)
{
	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	g_signal_emit (cal, signals[MODE], 0, mode);
}

/**
 * e_data_cal_notify_auth_required:
 * @cal: A calendar client interface.
 *
 * Notifies listeners that authorization is required to open the calendar.
 */
void
e_data_cal_notify_auth_required (EDataCal *cal)
{
	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	g_signal_emit (cal, signals[AUTH_REQUIRED], 0);
}

/**
 * e_data_cal_notify_error
 * @cal: A calendar client interface.
 * @message: Error message.
 *
 * Notify a calendar client of an error occurred in the backend.
 */
void
e_data_cal_notify_error (EDataCal *cal, const gchar *message)
{
	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	g_signal_emit (cal, signals[BACKEND_ERROR], 0, message);
}

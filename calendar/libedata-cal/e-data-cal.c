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

#include <glib-object.h>
#include <libedataserver/e-debug-log.h>
#include "e-data-cal.h"
#include "e-data-cal-enumtypes.h"
#include "e-gdbus-egdbuscal.h"

G_DEFINE_TYPE (EDataCal, e_data_cal, G_TYPE_OBJECT);

#define E_DATA_CAL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_DATA_CAL, EDataCalPrivate))

#define EDC_ERROR(_code) e_data_cal_create_error (_code, NULL)
#define EDC_ERROR_EX(_code, _msg) e_data_cal_create_error (_code, _msg)

struct _EDataCalPrivate {
	EGdbusCal *gdbus_object;
	ECalBackend *backend;
	ESource *source;
	GHashTable *live_queries;
};

/* Create the EDataCal error quark */
GQuark
e_data_cal_error_quark (void)
{
	#define ERR_PREFIX "org.gnome.evolution.dataserver.calendar.Cal."

	static const GDBusErrorEntry entries[] = {
		{ Success,				ERR_PREFIX "Success" },
		{ RepositoryOffline,			ERR_PREFIX "RepositoryOffline" },
		{ PermissionDenied,			ERR_PREFIX "PermissionDenied" },
		{ InvalidRange,				ERR_PREFIX "InvalidRange" },
		{ ObjectNotFound,			ERR_PREFIX "ObjectNotFound" },
		{ InvalidObject,			ERR_PREFIX "InvalidObject" },
		{ ObjectIdAlreadyExists,		ERR_PREFIX "ObjectIdAlreadyExists" },
		{ AuthenticationFailed,			ERR_PREFIX "AuthenticationFailed" },
		{ AuthenticationRequired,		ERR_PREFIX "AuthenticationRequired" },
		{ UnsupportedField,			ERR_PREFIX "UnsupportedField" },
		{ UnsupportedMethod,			ERR_PREFIX "UnsupportedMethod" },
		{ UnsupportedAuthenticationMethod,	ERR_PREFIX "UnsupportedAuthenticationMethod" },
		{ TLSNotAvailable,			ERR_PREFIX "TLSNotAvailable" },
		{ NoSuchCal,				ERR_PREFIX "NoSuchCal" },
		{ UnknownUser,				ERR_PREFIX "UnknownUser" },
		{ OfflineUnavailable,			ERR_PREFIX "OfflineUnavailable" },
		{ SearchSizeLimitExceeded,		ERR_PREFIX "SearchSizeLimitExceeded" },
		{ SearchTimeLimitExceeded,		ERR_PREFIX "SearchTimeLimitExceeded" },
		{ InvalidQuery,				ERR_PREFIX "InvalidQuery" },
		{ QueryRefused,				ERR_PREFIX "QueryRefused" },
		{ CouldNotCancel,			ERR_PREFIX "CouldNotCancel" },
		{ OtherError,				ERR_PREFIX "OtherError" },
		{ InvalidServerVersion,			ERR_PREFIX "InvalidServerVersion" },
		{ InvalidArg,				ERR_PREFIX "InvalidArg" },
		{ NotSupported,				ERR_PREFIX "NotSupported" }
	};

	#undef ERR_PREFIX

	static volatile gsize quark_volatile = 0;

	g_dbus_error_register_error_domain ("e-data-cal-error", &quark_volatile, entries, G_N_ELEMENTS (entries));

	return (GQuark) quark_volatile;
}

/**
 * e_data_cal_status_to_string:
 *
 * Since: 2.32
 **/
const gchar *
e_data_cal_status_to_string (EDataCalCallStatus status)
{
	gint i;
	static struct _statuses {
		EDataCalCallStatus status;
		const gchar *msg;
	} statuses[] = {
		{ Success,				N_("Success") },
		{ RepositoryOffline,			N_("Repository offline") },
		{ PermissionDenied,			N_("Permission denied") },
		{ InvalidRange,				N_("Invalid range") },
		{ ObjectNotFound,			N_("Object not found") },
		{ InvalidObject,			N_("Invalid object") },
		{ ObjectIdAlreadyExists,		N_("Object ID already exists") },
		{ AuthenticationFailed,			N_("Authentication Failed") },
		{ AuthenticationRequired,		N_("Authentication Required") },
		{ UnsupportedField,			N_("Unsupported field") },
		{ UnsupportedMethod,			N_("Unsupported method") },
		{ UnsupportedAuthenticationMethod,	N_("Unsupported authentication method") },
		{ TLSNotAvailable,			N_("TLS not available") },
		{ NoSuchCal,				N_("Calendar does not exist") },
		{ UnknownUser,				N_("Unknown user") },
		{ OfflineUnavailable,			N_("Not available in offline mode") },
		{ SearchSizeLimitExceeded,		N_("Search size limit exceeded") },
		{ SearchTimeLimitExceeded,		N_("Search time limit exceeded") },
		{ InvalidQuery,				N_("Invalid query") },
		{ QueryRefused,				N_("Query refused") },
		{ CouldNotCancel,			N_("Could not cancel") },
		/* { OtherError,			N_("Other error") }, */
		{ InvalidServerVersion,			N_("Invalid server version") },
		{ InvalidArg,				N_("Invalid argument") },
		{ NotSupported,				N_("Not supported") }
	};

	for (i = 0; i < G_N_ELEMENTS (statuses); i++) {
		if (statuses[i].status == status)
			return _(statuses[i].msg);
	}

	return _("Other error");
}

/**
 * e_data_cal_create_error:
 *
 * Since: 2.32
 **/
GError *
e_data_cal_create_error (EDataCalCallStatus status, const gchar *custom_msg)
{
	if (status == Success)
		return NULL;

	return g_error_new_literal (E_DATA_CAL_ERROR, status, custom_msg ? custom_msg : e_data_cal_status_to_string (status));
}

/**
 * e_data_cal_create_error_fmt:
 *
 * Since: 2.32
 **/
GError *
e_data_cal_create_error_fmt (EDataCalCallStatus status, const gchar *custom_msg_fmt, ...)
{
	GError *error;
	gchar *custom_msg;
	va_list ap;

	if (!custom_msg_fmt)
		return e_data_cal_create_error (status, NULL);

	va_start (ap, custom_msg_fmt);
	custom_msg = g_strdup_vprintf (custom_msg_fmt, ap);
	va_end (ap);

	error = e_data_cal_create_error (status, custom_msg);

	g_free (custom_msg);

	return error;
}

static void
data_cal_return_error (GDBusMethodInvocation *invocation, const GError *perror, const gchar *error_fmt)
{
	GError *error;

	g_return_if_fail (perror != NULL);

	error = g_error_new (E_DATA_CAL_ERROR, perror->code, error_fmt, perror->message);

	g_dbus_method_invocation_return_gerror (invocation, error);

	g_error_free (error);
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
static gboolean
impl_Cal_getUri (EGdbusCal *object, GDBusMethodInvocation *invocation, EDataCal *cal)
{
	e_gdbus_cal_complete_get_uri (object, invocation, e_cal_backend_get_uri (cal->priv->backend));

	return TRUE;
}

/* EDataCal::getCacheDir method */
static gboolean
impl_Cal_getCacheDir (EGdbusCal *object, GDBusMethodInvocation *invocation, EDataCal *cal)
{
	e_gdbus_cal_complete_get_cache_dir (object, invocation, e_cal_backend_get_cache_dir (cal->priv->backend));

	return TRUE;
}

/* EDataCal::open method */
static gboolean
impl_Cal_open (EGdbusCal *object, GDBusMethodInvocation *invocation, gboolean only_if_exists, const gchar *username, const gchar *password, EDataCal *cal)
{
	e_cal_backend_open (cal->priv->backend, cal, invocation, only_if_exists, username, password);

	return TRUE;
}

/* EDataCal::refresh method */
static gboolean
impl_Cal_refresh (EGdbusCal *object, GDBusMethodInvocation *invocation, EDataCal *cal)
{
	e_cal_backend_refresh (cal->priv->backend, cal, invocation);

	return TRUE;
}

/* EDataCal::close method */
static gboolean
impl_Cal_close (EGdbusCal *object, GDBusMethodInvocation *invocation, EDataCal *cal)
{
	e_cal_backend_remove_client (cal->priv->backend, cal);
	e_gdbus_cal_complete_close (object, invocation);

	g_object_unref (cal);

	return TRUE;
}

/* EDataCal::remove method */
static gboolean
impl_Cal_remove (EGdbusCal *object, GDBusMethodInvocation *invocation, EDataCal *cal)
{
	e_cal_backend_remove (cal->priv->backend, cal, invocation);

	return TRUE;
}

/* EDataCal::isReadOnly method */
static gboolean
impl_Cal_isReadOnly (EGdbusCal *object, GDBusMethodInvocation *invocation, EDataCal *cal)
{
	e_cal_backend_is_read_only (cal->priv->backend, cal);
	e_gdbus_cal_complete_is_read_only (object, invocation);

	return TRUE;
}

/* EDataCal::getCalAddress method */
static gboolean
impl_Cal_getCalAddress (EGdbusCal *object, GDBusMethodInvocation *invocation, EDataCal *cal)
{
	e_cal_backend_get_cal_address (cal->priv->backend, cal, invocation);

	return TRUE;
}

/* EDataCal::getAlarmEmailAddress method */
static gboolean
impl_Cal_getAlarmEmailAddress (EGdbusCal *object, GDBusMethodInvocation *invocation, EDataCal *cal)
{
	e_cal_backend_get_alarm_email_address (cal->priv->backend, cal, invocation);

	return TRUE;
}

/* EDataCal::getLdapAttribute method */
static gboolean
impl_Cal_getLdapAttribute (EGdbusCal *object, GDBusMethodInvocation *invocation, EDataCal *cal)
{
	e_cal_backend_get_ldap_attribute (cal->priv->backend, cal, invocation);

	return TRUE;
}

/* EDataCal::getSchedulingInformation method */
static gboolean
impl_Cal_getSchedulingInformation (EGdbusCal *object, GDBusMethodInvocation *invocation, EDataCal *cal)
{
	e_cal_backend_get_static_capabilities (cal->priv->backend, cal, invocation);

	return TRUE;
}

/* EDataCal::setMode method */
static gboolean
impl_Cal_setMode (EGdbusCal *object, GDBusMethodInvocation *invocation, EDataCalMode mode, EDataCal *cal)
{
	e_cal_backend_set_mode (cal->priv->backend, mode);
	e_gdbus_cal_complete_set_mode (object, invocation);

	return TRUE;
}

/* EDataCal::getDefaultObject method */
static gboolean
impl_Cal_getDefaultObject (EGdbusCal *object, GDBusMethodInvocation *invocation, EDataCal *cal)
{
	e_cal_backend_get_default_object (cal->priv->backend, cal, invocation);

	return TRUE;
}

/* EDataCal::getObject method */
static gboolean
impl_Cal_getObject (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *uid, const gchar *rid, EDataCal *cal)
{
	e_cal_backend_get_object (cal->priv->backend, cal, invocation, uid, rid);

	return TRUE;
}

/* EDataCal::getObjectList method */
static gboolean
impl_Cal_getObjectList (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *sexp, EDataCal *cal)
{
	e_cal_backend_get_object_list (cal->priv->backend, cal, invocation, sexp);

	return TRUE;
}

/* EDataCal::getChanges method */
static gboolean
impl_Cal_getChanges (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *change_id, EDataCal *cal)
{
	e_cal_backend_get_changes (cal->priv->backend, cal, invocation, change_id);

	return TRUE;
}

/* EDataCal::getFreeBusy method */
static gboolean
impl_Cal_getFreeBusy (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar **user_list, guint start, guint end, EDataCal *cal)
{
	GList *users = NULL;

	if (user_list) {
		gint i;

		for (i = 0; user_list[i]; i++)
			users = g_list_append (users, (gpointer)user_list[i]);
	}

	/* call the backend's get_free_busy method */
	e_cal_backend_get_free_busy (cal->priv->backend, cal, invocation, users, (time_t)start, (time_t)end);

	return TRUE;
}

/* EDataCal::discardAlarm method */
static gboolean
impl_Cal_discardAlarm (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *uid, const gchar *auid, EDataCal *cal)
{
	e_cal_backend_discard_alarm (cal->priv->backend, cal, invocation, uid, auid);

	return TRUE;
}

/* EDataCal::createObject method */
static gboolean
impl_Cal_createObject (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *calobj, EDataCal *cal)
{
	e_cal_backend_create_object (cal->priv->backend, cal, invocation, calobj);

	return TRUE;
}

/* EDataCal::modifyObject method */
static gboolean
impl_Cal_modifyObject (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *calobj, EDataCalObjModType mod, EDataCal *cal)
{
	e_cal_backend_modify_object (cal->priv->backend, cal, invocation, calobj, mod);

	return TRUE;
}

/* EDataCal::removeObject method */
static gboolean
impl_Cal_removeObject (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *uid, const gchar *rid, EDataCalObjModType mod, EDataCal *cal)
{
	if (rid[0] == '\0')
		rid = NULL;

	e_cal_backend_remove_object (cal->priv->backend, cal, invocation, uid, rid, mod);

	return TRUE;
}

/* EDataCal::receiveObjects method */
static gboolean
impl_Cal_receiveObjects (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *calobj, EDataCal *cal)
{
	e_cal_backend_receive_objects (cal->priv->backend, cal, invocation, calobj);

	return TRUE;
}

/* EDataCal::sendObjects method */
static gboolean
impl_Cal_sendObjects (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *calobj, EDataCal *cal)
{
	e_cal_backend_send_objects (cal->priv->backend, cal, invocation, calobj);

	return TRUE;
}

/* EDataCal::getAttachmentList method */
static gboolean
impl_Cal_getAttachmentList (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *uid, const gchar *rid, EDataCal *cal)
{
	e_cal_backend_get_attachment_list (cal->priv->backend, cal, invocation, uid, rid);

	return TRUE;
}

/* Function to get a new EDataCalView path, used by getQuery below */
static gchar *
construct_calview_path (void)
{
	static guint counter = 1;
	return g_strdup_printf ("/org/gnome/evolution/dataserver/calendar/CalView/%d/%d", getpid(), counter++);
}

/* EDataCal::getQuery method */
static gboolean
impl_Cal_getQuery (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *sexp, EDataCal *cal)
{
	EDataCalView *query;
	ECalBackendSExp *obj_sexp;
	gchar *path;
	GError *error = NULL;

	/* we handle this entirely here, since it doesn't require any
	   backend involvement now that we have e_cal_view_start to
	   actually kick off the search. */

	obj_sexp = e_cal_backend_sexp_new (sexp);
	if (!obj_sexp) {
		e_data_cal_notify_query (cal, invocation, EDC_ERROR (InvalidQuery), NULL);
		return TRUE;
	}

	query = e_data_cal_view_new (cal->priv->backend, obj_sexp);
	e_debug_log (FALSE, E_DEBUG_LOG_DOMAIN_CAL_QUERIES, "%p;%p;NEW;%s;%s", cal, query, sexp, G_OBJECT_TYPE_NAME (cal->priv->backend));
	if (!query) {
		g_object_unref (obj_sexp);
		e_data_cal_notify_query (cal, invocation, EDC_ERROR (OtherError), NULL);
		return TRUE;
	}

	/* log query to evaluate cache performance */
	e_debug_log (FALSE, E_DEBUG_LOG_DOMAIN_CAL_QUERIES, "%p;%p;REUSED;%s;%s", cal, query, sexp, G_OBJECT_TYPE_NAME (cal->priv->backend));

	path = construct_calview_path ();
	e_data_cal_view_register_gdbus_object (query, g_dbus_method_invocation_get_connection (invocation), path, &error);

	if (error) {
		g_object_unref (query);
		e_data_cal_notify_query (cal, invocation, EDC_ERROR_EX (OtherError, error->message), NULL);
		g_error_free (error);
		g_free (path);

		return TRUE;
	}

	e_cal_backend_add_query (cal->priv->backend, query);

	e_data_cal_notify_query (cal, invocation, EDC_ERROR (Success), path);

        g_free (path);

	return TRUE;
}

/* EDataCal::getTimezone method */
static gboolean
impl_Cal_getTimezone (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *tzid, EDataCal *cal)
{
	e_cal_backend_get_timezone (cal->priv->backend, cal, invocation, tzid);

	return TRUE;
}

/* EDataCal::addTimezone method */
static gboolean
impl_Cal_addTimezone (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *tz, EDataCal *cal)
{
	e_cal_backend_add_timezone (cal->priv->backend, cal, invocation, tz);

	return TRUE;
}

/* EDataCal::setDefaultTimezone method */
static gboolean
impl_Cal_setDefaultTimezone (EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *tz, EDataCal *cal)
{
	e_cal_backend_set_default_zone (cal->priv->backend, cal, invocation, tz);

	return TRUE;
}

/* free returned pointer with g_free() */
static gchar **
create_str_array_from_glist (GList *lst)
{
	gchar **seq;
	GList *l;
	gint i;

	seq = g_new0 (gchar *, g_list_length (lst) + 1);
	for (l = lst, i = 0; l; l = l->next, i++) {
		seq[i] = l->data;
	}

	return seq;
}

/* free returned pointer with g_free() */
static gchar **
create_str_array_from_gslist (GSList *lst)
{
	gchar **seq;
	GSList *l;
	gint i;

	seq = g_new0 (gchar *, g_slist_length (lst) + 1);
	for (l = lst, i = 0; l; l = l->next, i++) {
		seq[i] = l->data;
	}

	return seq;
}

/**
 * e_data_cal_notify_read_only:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @read_only: Read only value.
 *
 * Notifies listeners of the completion of the is_read_only method call.
 */
void
e_data_cal_notify_read_only (EDataCal *cal, GError *error, gboolean read_only)
{
	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	if (error) {
		e_data_cal_notify_error (cal, error->message);
		g_error_free (error);
	} else {
		e_gdbus_cal_emit_readonly (cal->priv->gdbus_object, read_only);
	}
}

/**
 * e_data_cal_notify_cal_address:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @address: Calendar address.
 *
 * Notifies listeners of the completion of the get_cal_address method call.
 */
void
e_data_cal_notify_cal_address (EDataCal *cal, EServerMethodContext context, GError *error, const gchar *address)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot retrieve calendar address: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_get_cal_address (cal->priv->gdbus_object, invocation, address ? address : "");
}

/**
 * e_data_cal_notify_alarm_email_address:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @address: Alarm email address.
 *
 * Notifies listeners of the completion of the get_alarm_email_address method call.
 */
void
e_data_cal_notify_alarm_email_address (EDataCal *cal, EServerMethodContext context, GError *error, const gchar *address)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot retrieve calendar alarm e-mail address: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_get_alarm_email_address (cal->priv->gdbus_object, invocation, address ? address : "");
}

/**
 * e_data_cal_notify_ldap_attribute:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @attibute: LDAP attribute.
 *
 * Notifies listeners of the completion of the get_ldap_attribute method call.
 */
void
e_data_cal_notify_ldap_attribute (EDataCal *cal, EServerMethodContext context, GError *error, const gchar *attribute)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot retrieve calendar's LDAP attribute: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_get_ldap_attribute (cal->priv->gdbus_object, invocation, attribute ? attribute : "");
}

/**
 * e_data_cal_notify_static_capabilities:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @capabilities: Static capabilities from the backend.
 *
 * Notifies listeners of the completion of the get_static_capabilities method call.
 */
void
e_data_cal_notify_static_capabilities (EDataCal *cal, EServerMethodContext context, GError *error, const gchar *capabilities)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot retrieve calendar scheduling information: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_get_scheduling_information (cal->priv->gdbus_object, invocation, capabilities ? capabilities : "");
}

/**
 * e_data_cal_notify_open:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the open method call.
 */
void
e_data_cal_notify_open (EDataCal *cal, EServerMethodContext context, GError *error)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot open calendar: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_open (cal->priv->gdbus_object, invocation);
}

/**
 * e_data_cal_notify_refresh:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the refresh method call.
 *
 * Since: 2.30
 */
void
e_data_cal_notify_refresh (EDataCal *cal, EServerMethodContext context, GError *error)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot refresh calendar: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_refresh (cal->priv->gdbus_object, invocation);
}

/**
 * e_data_cal_notify_remove:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the remove method call.
 */
void
e_data_cal_notify_remove (EDataCal *cal, EServerMethodContext context, GError *error)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot remove calendar: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_remove (cal->priv->gdbus_object, invocation);
}

/**
 * e_data_cal_notify_object_created:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @uid: UID of the object created.
 * @object: The object created as an iCalendar string.
 *
 * Notifies listeners of the completion of the create_object method call.
 */void
e_data_cal_notify_object_created (EDataCal *cal, EServerMethodContext context, GError *error,
				  const gchar *uid, const gchar *object)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot create calendar object: %s"));
		g_error_free (error);
	} else {
		e_cal_backend_notify_object_created (cal->priv->backend, object);
		e_gdbus_cal_complete_create_object (cal->priv->gdbus_object, invocation, uid ? uid : "");
	}
}

/**
 * e_data_cal_notify_object_modified:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @old_object: The old object as an iCalendar string.
 * @object: The modified object as an iCalendar string.
 *
 * Notifies listeners of the completion of the modify_object method call.
 */
void
e_data_cal_notify_object_modified (EDataCal *cal, EServerMethodContext context, GError *error,
				   const gchar *old_object, const gchar *object)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot modify calendar object: %s"));
		g_error_free (error);
	} else {
		e_cal_backend_notify_object_modified (cal->priv->backend, old_object, object);
		e_gdbus_cal_complete_modify_object (cal->priv->gdbus_object, invocation);
	}
}

/**
 * e_data_cal_notify_object_removed:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @uid: UID of the removed object.
 * @old_object: The old object as an iCalendar string.
 * @object: The new object as an iCalendar string. This will not be NULL only
 * when removing instances of a recurring appointment.
 *
 * Notifies listeners of the completion of the remove_object method call.
 */
void
e_data_cal_notify_object_removed (EDataCal *cal, EServerMethodContext context, GError *error,
				  const ECalComponentId *id, const gchar *old_object, const gchar *object)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot remove calendar object: %s"));
		g_error_free (error);
	} else {
		e_cal_backend_notify_object_removed (cal->priv->backend, id, old_object, object);
		e_gdbus_cal_complete_remove_object (cal->priv->gdbus_object, invocation);
	}
}

/**
 * e_data_cal_notify_objects_received:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the receive_objects method call.
 */
void
e_data_cal_notify_objects_received (EDataCal *cal, EServerMethodContext context, GError *error)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot receive calendar objects: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_receive_objects (cal->priv->gdbus_object, invocation);
}

/**
 * e_data_cal_notify_alarm_discarded:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the discard_alarm method call.
 */
void
e_data_cal_notify_alarm_discarded (EDataCal *cal, EServerMethodContext context, GError *error)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot discard calendar alarm: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_discard_alarm (cal->priv->gdbus_object, invocation);
}

/**
 * e_data_cal_notify_objects_sent:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @users: List of users.
 * @calobj: An iCalendar string representing the object sent.
 *
 * Notifies listeners of the completion of the send_objects method call.
 */
void
e_data_cal_notify_objects_sent (EDataCal *cal, EServerMethodContext context, GError *error, GList *users, const gchar *calobj)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot send calendar objects: %s"));
		g_error_free (error);
	} else {
		gchar **users_array = create_str_array_from_glist (users);

		e_gdbus_cal_complete_send_objects (cal->priv->gdbus_object, invocation, (const gchar * const *) users_array, calobj ? calobj : "");

		g_free (users_array);
	}
}

/**
 * e_data_cal_notify_default_object:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @object: The default object as an iCalendar string.
 *
 * Notifies listeners of the completion of the get_default_object method call.
 */
void
e_data_cal_notify_default_object (EDataCal *cal, EServerMethodContext context, GError *error, const gchar *object)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot retrieve default calendar object path: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_get_default_object (cal->priv->gdbus_object, invocation, object ? object : "");
}

/**
 * e_data_cal_notify_object:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @object: The object retrieved as an iCalendar string.
 *
 * Notifies listeners of the completion of the get_object method call.
 */
void
e_data_cal_notify_object (EDataCal *cal, EServerMethodContext context, GError *error, const gchar *object)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot retrieve calendar object path: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_get_object (cal->priv->gdbus_object, invocation, object ? object : "");
}

/**
 * e_data_cal_notify_object_list:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @objects: List of retrieved objects.
 *
 * Notifies listeners of the completion of the get_object_list method call.
 */
void
e_data_cal_notify_object_list (EDataCal *cal, EServerMethodContext context, GError *error, GList *objects)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot retrieve calendar object list: %s"));
		g_error_free (error);
	} else {
		gchar **seq = create_str_array_from_glist (objects);

		e_gdbus_cal_complete_get_object_list (cal->priv->gdbus_object, invocation, (const gchar * const *) seq);

		g_free (seq);
	}
}

/**
 * e_data_cal_notify_attachment_list:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @attachments: List of retrieved attachment uri's.
 *
 * Notifies listeners of the completion of the get_attachment_list method call.+ */
void
e_data_cal_notify_attachment_list (EDataCal *cal, EServerMethodContext context, GError *error, GSList *attachments)
{
	GDBusMethodInvocation *invocation = context;
	gchar **seq;

	seq = create_str_array_from_gslist (attachments);

	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Could not retrieve attachment list: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_get_attachment_list (cal->priv->gdbus_object, invocation, (const gchar * const *) seq);

	g_free (seq);
}

/**
 * e_data_cal_notify_query:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @query: The new live query.
 *
 * Notifies listeners of the completion of the get_query method call.
 */
void
e_data_cal_notify_query (EDataCal *cal, EServerMethodContext context, GError *error, const gchar *query)
{
	/*
	 * Only have a seperate notify function to follow suit with the rest of this
	 * file - it'd be much easier to just do the return in the above function
	 */
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Could not complete calendar query: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_get_query (cal->priv->gdbus_object, invocation, query);
}

/**
 * e_data_cal_notify_timezone_requested:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @object: The requested timezone as an iCalendar string.
 *
 * Notifies listeners of the completion of the get_timezone method call.
 */
void
e_data_cal_notify_timezone_requested (EDataCal *cal, EServerMethodContext context, GError *error, const gchar *object)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Could not retrieve calendar time zone: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_get_timezone (cal->priv->gdbus_object, invocation, object ? object : "");
}

/**
 * e_data_cal_notify_timezone_added:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @tzid: ID of the added timezone.
 *
 * Notifies listeners of the completion of the add_timezone method call.
 */
void
e_data_cal_notify_timezone_added (EDataCal *cal, EServerMethodContext context, GError *error, const gchar *tzid)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Could not add calendar time zone: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_add_timezone (cal->priv->gdbus_object, invocation);
}

/**
 * e_data_cal_notify_default_timezone_set:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the set_default_timezone method call.
 */
void
e_data_cal_notify_default_timezone_set (EDataCal *cal, EServerMethodContext context, GError *error)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Could not set default calendar time zone: %s"));
		g_error_free (error);
	} else
		e_gdbus_cal_complete_set_default_timezone (cal->priv->gdbus_object, invocation);
}

/**
 * e_data_cal_notify_changes:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @adds: List of additions.
 * @modifies: List of modifications.
 * @deletes: List of removals.
 *
 * Notifies listeners of the completion of the get_changes method call.
 */
void
e_data_cal_notify_changes (EDataCal *cal, EServerMethodContext context, GError *error,
			   GList *adds, GList *modifies, GList *deletes)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot retrieve calendar changes: %s"));
		g_error_free (error);
	} else {
		gchar **additions, **modifications, **removals;

		additions = create_str_array_from_glist (adds);
		modifications = create_str_array_from_glist (modifies);
		removals = create_str_array_from_glist (deletes);

		e_gdbus_cal_complete_get_changes (cal->priv->gdbus_object, invocation, (const gchar * const *) additions, (const gchar * const *) modifications, (const gchar * const *) removals);

		g_free (additions);
		g_free (modifications);
		g_free (removals);
	}
}

/**
 * e_data_cal_notify_free_busy:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @freebusy: List of free/busy objects.
 *
 * Notifies listeners of the completion of the get_free_busy method call.
 */
void
e_data_cal_notify_free_busy (EDataCal *cal, EServerMethodContext context, GError *error, GList *freebusy)
{
	GDBusMethodInvocation *invocation = context;
	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_cal_return_error (invocation, error, _("Cannot retrieve calendar free/busy list: %s"));
		g_error_free (error);
	} else {
		gchar **seq;

		seq = create_str_array_from_glist (freebusy);

		e_gdbus_cal_complete_get_free_busy (cal->priv->gdbus_object, invocation, (const gchar * const *) seq);

		g_free (seq);
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

	e_gdbus_cal_emit_mode (cal->priv->gdbus_object, mode);
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

	e_gdbus_cal_emit_auth_required (cal->priv->gdbus_object);
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

	e_gdbus_cal_emit_backend_error (cal->priv->gdbus_object, message);
}

/* Instance init */
static void
e_data_cal_init (EDataCal *ecal)
{
	EGdbusCal *gdbus_object;

	ecal->priv = E_DATA_CAL_GET_PRIVATE (ecal);

	ecal->priv->gdbus_object = e_gdbus_cal_stub_new ();

	gdbus_object = ecal->priv->gdbus_object;
	g_signal_connect (gdbus_object, "handle-get-uri", G_CALLBACK (impl_Cal_getUri), ecal);
	g_signal_connect (gdbus_object, "handle-get-cache-dir", G_CALLBACK (impl_Cal_getCacheDir), ecal);
	g_signal_connect (gdbus_object, "handle-open", G_CALLBACK (impl_Cal_open), ecal);
	g_signal_connect (gdbus_object, "handle-refresh", G_CALLBACK (impl_Cal_refresh), ecal);
	g_signal_connect (gdbus_object, "handle-close", G_CALLBACK (impl_Cal_close), ecal);
	g_signal_connect (gdbus_object, "handle-remove", G_CALLBACK (impl_Cal_remove), ecal);
	g_signal_connect (gdbus_object, "handle-is-read-only", G_CALLBACK (impl_Cal_isReadOnly), ecal);
	g_signal_connect (gdbus_object, "handle-get-cal-address", G_CALLBACK (impl_Cal_getCalAddress), ecal);
	g_signal_connect (gdbus_object, "handle-get-alarm-email-address", G_CALLBACK (impl_Cal_getAlarmEmailAddress), ecal);
	g_signal_connect (gdbus_object, "handle-get-ldap-attribute", G_CALLBACK (impl_Cal_getLdapAttribute), ecal);
	g_signal_connect (gdbus_object, "handle-get-scheduling-information", G_CALLBACK (impl_Cal_getSchedulingInformation), ecal);
	g_signal_connect (gdbus_object, "handle-set-mode", G_CALLBACK (impl_Cal_setMode), ecal);
	g_signal_connect (gdbus_object, "handle-get-default-object", G_CALLBACK (impl_Cal_getDefaultObject), ecal);
	g_signal_connect (gdbus_object, "handle-get-object", G_CALLBACK (impl_Cal_getObject), ecal);
	g_signal_connect (gdbus_object, "handle-get-object-list", G_CALLBACK (impl_Cal_getObjectList), ecal);
	g_signal_connect (gdbus_object, "handle-get-changes", G_CALLBACK (impl_Cal_getChanges), ecal);
	g_signal_connect (gdbus_object, "handle-get-free-busy", G_CALLBACK (impl_Cal_getFreeBusy), ecal);
	g_signal_connect (gdbus_object, "handle-discard-alarm", G_CALLBACK (impl_Cal_discardAlarm), ecal);
	g_signal_connect (gdbus_object, "handle-create-object", G_CALLBACK (impl_Cal_createObject), ecal);
	g_signal_connect (gdbus_object, "handle-modify-object", G_CALLBACK (impl_Cal_modifyObject), ecal);
	g_signal_connect (gdbus_object, "handle-remove-object", G_CALLBACK (impl_Cal_removeObject), ecal);
	g_signal_connect (gdbus_object, "handle-receive-objects", G_CALLBACK (impl_Cal_receiveObjects), ecal);
	g_signal_connect (gdbus_object, "handle-send-objects", G_CALLBACK (impl_Cal_sendObjects), ecal);
	g_signal_connect (gdbus_object, "handle-get-attachment-list", G_CALLBACK (impl_Cal_getAttachmentList), ecal);
	g_signal_connect (gdbus_object, "handle-get-query", G_CALLBACK (impl_Cal_getQuery), ecal);
	g_signal_connect (gdbus_object, "handle-get-timezone", G_CALLBACK (impl_Cal_getTimezone), ecal);
	g_signal_connect (gdbus_object, "handle-add-timezone", G_CALLBACK (impl_Cal_addTimezone), ecal);
	g_signal_connect (gdbus_object, "handle-set-default-timezone", G_CALLBACK (impl_Cal_setDefaultTimezone), ecal);
}

static void
e_data_cal_finalize (GObject *object)
{
	EDataCal *cal = E_DATA_CAL (object);

	g_return_if_fail (cal != NULL);

	g_object_unref (cal->priv->gdbus_object);

	if (G_OBJECT_CLASS (e_data_cal_parent_class)->finalize)
		G_OBJECT_CLASS (e_data_cal_parent_class)->finalize (object);
}

/* Class init */
static void
e_data_cal_class_init (EDataCalClass *klass)
{
	GObjectClass *object_class;

	g_type_class_add_private (klass, sizeof (EDataCalPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = e_data_cal_finalize;
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
 * e_data_cal_register_gdbus_object:
 *
 * Registers GDBus object of this EDataCal.
 *
 * Since: 2.32
 **/
guint
e_data_cal_register_gdbus_object (EDataCal *cal, GDBusConnection *connection, const gchar *object_path, GError **error)
{
	g_return_val_if_fail (cal != NULL, 0);
	g_return_val_if_fail (E_IS_DATA_CAL (cal), 0);
	g_return_val_if_fail (connection != NULL, 0);
	g_return_val_if_fail (object_path != NULL, 0);

	return e_gdbus_cal_register_object (cal->priv->gdbus_object, connection, object_path, error);
}

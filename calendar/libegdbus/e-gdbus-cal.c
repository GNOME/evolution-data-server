/*
 * e-gdbus-cal.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Copyright (C) 2011 Red Hat, Inc. (www.redhat.com)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <gio/gio.h>

#include <libedataserver/e-data-server-util.h>
#include <libedataserver/e-gdbus-marshallers.h>

#include "e-gdbus-cal.h"

#define GDBUS_CAL_INTERFACE_NAME "org.gnome.evolution.dataserver.Calendar"

typedef EGdbusCalIface EGdbusCalInterface;
G_DEFINE_INTERFACE (EGdbusCal, e_gdbus_cal, G_TYPE_OBJECT);

enum
{
	_0_SIGNAL,
	__BACKEND_ERROR_SIGNAL,
	__READONLY_SIGNAL,
	__ONLINE_SIGNAL,
	__AUTH_REQUIRED_SIGNAL,
	__OPENED_SIGNAL,
	__FREE_BUSY_DATA_SIGNAL,
	__OPEN_METHOD,
	__OPEN_DONE_SIGNAL,
	__REMOVE_METHOD,
	__REMOVE_DONE_SIGNAL,
	__REFRESH_METHOD,
	__REFRESH_DONE_SIGNAL,
	__GET_BACKEND_PROPERTY_METHOD,
	__GET_BACKEND_PROPERTY_DONE_SIGNAL,
	__SET_BACKEND_PROPERTY_METHOD,
	__SET_BACKEND_PROPERTY_DONE_SIGNAL,
	__GET_OBJECT_METHOD,
	__GET_OBJECT_DONE_SIGNAL,
	__GET_OBJECT_LIST_METHOD,
	__GET_OBJECT_LIST_DONE_SIGNAL,
	__GET_FREE_BUSY_METHOD,
	__GET_FREE_BUSY_DONE_SIGNAL,
	__CREATE_OBJECT_METHOD,
	__CREATE_OBJECT_DONE_SIGNAL,
	__MODIFY_OBJECT_METHOD,
	__MODIFY_OBJECT_DONE_SIGNAL,
	__REMOVE_OBJECT_METHOD,
	__REMOVE_OBJECT_DONE_SIGNAL,
	__RECEIVE_OBJECTS_METHOD,
	__RECEIVE_OBJECTS_DONE_SIGNAL,
	__SEND_OBJECTS_METHOD,
	__SEND_OBJECTS_DONE_SIGNAL,
	__GET_ATTACHMENT_URIS_METHOD,
	__GET_ATTACHMENT_URIS_DONE_SIGNAL,
	__DISCARD_ALARM_METHOD,
	__DISCARD_ALARM_DONE_SIGNAL,
	__GET_VIEW_METHOD,
	__GET_VIEW_DONE_SIGNAL,
	__GET_TIMEZONE_METHOD,
	__GET_TIMEZONE_DONE_SIGNAL,
	__ADD_TIMEZONE_METHOD,
	__ADD_TIMEZONE_DONE_SIGNAL,
	__AUTHENTICATE_USER_METHOD,
	__CANCEL_OPERATION_METHOD,
	__CANCEL_ALL_METHOD,
	__CLOSE_METHOD,
	__LAST_SIGNAL
};

static guint signals[__LAST_SIGNAL] = {0};

struct _EGdbusCalProxyPrivate
{
	GHashTable *pending_ops;
};

/* ------------------------------------------------------------------------- */

/* Various lookup tables */

static GHashTable *_method_name_to_id = NULL;
static GHashTable *_method_name_to_type = NULL;
static GHashTable *_signal_name_to_id = NULL;
static GHashTable *_signal_name_to_type = NULL;

static guint
lookup_method_id_from_method_name (const gchar *method_name)
{
	return GPOINTER_TO_UINT (g_hash_table_lookup (_method_name_to_id, method_name));
}

static guint
lookup_method_type_from_method_name (const gchar *method_name)
{
	return GPOINTER_TO_UINT (g_hash_table_lookup (_method_name_to_type, method_name));
}

static guint
lookup_signal_id_from_signal_name (const gchar *signal_name)
{
	return GPOINTER_TO_UINT (g_hash_table_lookup (_signal_name_to_id, signal_name));
}

static guint
lookup_signal_type_from_signal_name (const gchar *signal_name)
{
	return GPOINTER_TO_UINT (g_hash_table_lookup (_signal_name_to_type, signal_name));
}

/* ------------------------------------------------------------------------- */

E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_STRING  (GDBUS_CAL_INTERFACE_NAME, backend_error)
E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_BOOLEAN (GDBUS_CAL_INTERFACE_NAME, readonly)
E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_BOOLEAN (GDBUS_CAL_INTERFACE_NAME, online)
E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_STRV    (GDBUS_CAL_INTERFACE_NAME, auth_required)
E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_STRV    (GDBUS_CAL_INTERFACE_NAME, opened)
E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_STRV    (GDBUS_CAL_INTERFACE_NAME, free_busy_data)

E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID	(GDBUS_CAL_INTERFACE_NAME, open)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID	(GDBUS_CAL_INTERFACE_NAME, remove)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID	(GDBUS_CAL_INTERFACE_NAME, refresh)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRING	(GDBUS_CAL_INTERFACE_NAME, get_backend_property)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID	(GDBUS_CAL_INTERFACE_NAME, set_backend_property)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRING	(GDBUS_CAL_INTERFACE_NAME, get_object)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRV	(GDBUS_CAL_INTERFACE_NAME, get_object_list)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID	(GDBUS_CAL_INTERFACE_NAME, get_free_busy)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRING	(GDBUS_CAL_INTERFACE_NAME, create_object)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID	(GDBUS_CAL_INTERFACE_NAME, modify_object)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID	(GDBUS_CAL_INTERFACE_NAME, remove_object)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID	(GDBUS_CAL_INTERFACE_NAME, receive_objects)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRV	(GDBUS_CAL_INTERFACE_NAME, send_objects)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRV	(GDBUS_CAL_INTERFACE_NAME, get_attachment_uris)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID	(GDBUS_CAL_INTERFACE_NAME, discard_alarm)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRING	(GDBUS_CAL_INTERFACE_NAME, get_view)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRING	(GDBUS_CAL_INTERFACE_NAME, get_timezone)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID	(GDBUS_CAL_INTERFACE_NAME, add_timezone)

static void
e_gdbus_cal_default_init (EGdbusCalIface *iface)
{
	/* Build lookup structures */
	_method_name_to_id = g_hash_table_new (g_str_hash, g_str_equal);
	_method_name_to_type = g_hash_table_new (g_str_hash, g_str_equal);
	_signal_name_to_id = g_hash_table_new (g_str_hash, g_str_equal);
	_signal_name_to_type = g_hash_table_new (g_str_hash, g_str_equal);

	/* GObject signals definitions for D-Bus signals: */
	E_INIT_GDBUS_SIGNAL_STRING		(EGdbusCalIface, "backend_error",	backend_error,	__BACKEND_ERROR_SIGNAL)
	E_INIT_GDBUS_SIGNAL_BOOLEAN		(EGdbusCalIface, "readonly",		readonly,	__READONLY_SIGNAL)
	E_INIT_GDBUS_SIGNAL_BOOLEAN		(EGdbusCalIface, "online",		online,		__ONLINE_SIGNAL)
	E_INIT_GDBUS_SIGNAL_STRV   		(EGdbusCalIface, "auth_required", 	auth_required,	__AUTH_REQUIRED_SIGNAL)
	E_INIT_GDBUS_SIGNAL_STRV   		(EGdbusCalIface, "opened", 		opened,		__OPENED_SIGNAL)
	E_INIT_GDBUS_SIGNAL_STRV   		(EGdbusCalIface, "free_busy_data", 	free_busy_data,	__FREE_BUSY_DATA_SIGNAL)

	/* GObject signals definitions for D-Bus methods: */
	E_INIT_GDBUS_METHOD_ASYNC_BOOLEAN__VOID	(EGdbusCalIface, "open",			open, __OPEN_METHOD, __OPEN_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_VOID__VOID	(EGdbusCalIface, "remove",			remove, __REMOVE_METHOD, __REMOVE_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_VOID__VOID	(EGdbusCalIface, "refresh",			refresh, __REFRESH_METHOD, __REFRESH_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRING__STRING(EGdbusCalIface, "getBackendProperty",		get_backend_property, __GET_BACKEND_PROPERTY_METHOD, __GET_BACKEND_PROPERTY_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRV__VOID	(EGdbusCalIface, "setBackendProperty",		set_backend_property, __SET_BACKEND_PROPERTY_METHOD, __SET_BACKEND_PROPERTY_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRV__STRING	(EGdbusCalIface, "getObject",			get_object, __GET_OBJECT_METHOD, __GET_OBJECT_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRING__STRV	(EGdbusCalIface, "getObjectList",		get_object_list, __GET_OBJECT_LIST_METHOD, __GET_OBJECT_LIST_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRV__VOID	(EGdbusCalIface, "getFreeBusy",			get_free_busy, __GET_FREE_BUSY_METHOD, __GET_FREE_BUSY_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRING__STRING(EGdbusCalIface, "createObject",		create_object, __CREATE_OBJECT_METHOD, __CREATE_OBJECT_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRV__VOID	(EGdbusCalIface, "modifyObject",		modify_object, __MODIFY_OBJECT_METHOD, __MODIFY_OBJECT_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRV__VOID	(EGdbusCalIface, "removeObject",		remove_object, __REMOVE_OBJECT_METHOD, __REMOVE_OBJECT_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRING__VOID	(EGdbusCalIface, "receiveObjects",		receive_objects, __RECEIVE_OBJECTS_METHOD, __RECEIVE_OBJECTS_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRING__STRV	(EGdbusCalIface, "sendObjects",			send_objects, __SEND_OBJECTS_METHOD, __SEND_OBJECTS_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRV__STRV	(EGdbusCalIface, "getAttachmentUris",		get_attachment_uris, __GET_ATTACHMENT_URIS_METHOD, __GET_ATTACHMENT_URIS_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRV__VOID	(EGdbusCalIface, "discardAlarm",		discard_alarm, __DISCARD_ALARM_METHOD, __DISCARD_ALARM_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRING__STRING(EGdbusCalIface, "getView",			get_view, __GET_VIEW_METHOD, __GET_VIEW_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRING__STRING(EGdbusCalIface, "getTimezone",			get_timezone, __GET_TIMEZONE_METHOD, __GET_TIMEZONE_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRING__VOID	(EGdbusCalIface, "addTimezone",			add_timezone, __ADD_TIMEZONE_METHOD, __ADD_TIMEZONE_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_STRV		(EGdbusCalIface, "authenticateUser",		authenticate_user, __AUTHENTICATE_USER_METHOD)
	E_INIT_GDBUS_METHOD_UINT		(EGdbusCalIface, "cancelOperation",		cancel_operation, __CANCEL_OPERATION_METHOD)
	E_INIT_GDBUS_METHOD_VOID		(EGdbusCalIface, "cancelAll",			cancel_all, __CANCEL_ALL_METHOD)
	E_INIT_GDBUS_METHOD_VOID		(EGdbusCalIface, "close",			close, __CLOSE_METHOD)
}

static gchar **
encode_string_string (const gchar *str1, const gchar *str2)
{
	gchar **strv;

	strv = g_new0 (gchar *, 3);
	strv[0] = e_util_utf8_make_valid (str1 ? str1 : "");
	strv[1] = e_util_utf8_make_valid (str2 ? str2 : "");
	strv[2] = NULL;

	return strv;
}

static gboolean
decode_string_string (const gchar * const *in_strv, gchar **out_str1, gchar **out_str2)
{
	g_return_val_if_fail (in_strv != NULL, FALSE);
	g_return_val_if_fail (in_strv[0] != NULL, FALSE);
	g_return_val_if_fail (in_strv[1] != NULL, FALSE);
	g_return_val_if_fail (in_strv[2] == NULL, FALSE);
	g_return_val_if_fail (out_str1 != NULL, FALSE);
	g_return_val_if_fail (out_str2 != NULL, FALSE);

	*out_str1 = g_strdup (in_strv[0]);
	*out_str2 = g_strdup (in_strv[1]);

	return TRUE;
}

void
e_gdbus_cal_call_open (GDBusProxy *proxy, gboolean in_only_if_exists, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_boolean ("open", e_gdbus_cal_call_open, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_only_if_exists, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_open_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_cal_call_open);
}

gboolean
e_gdbus_cal_call_open_sync (GDBusProxy *proxy, gboolean in_only_if_exists, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_boolean__void (proxy, in_only_if_exists, cancellable, error,
		e_gdbus_cal_call_open,
		e_gdbus_cal_call_open_finish);
}

void
e_gdbus_cal_call_remove (GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_void ("remove", e_gdbus_cal_call_remove, E_GDBUS_ASYNC_OP_KEEPER (proxy), cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_remove_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_cal_call_remove);
}

gboolean
e_gdbus_cal_call_remove_sync (GDBusProxy *proxy, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_void__void (proxy, cancellable, error,
		e_gdbus_cal_call_remove,
		e_gdbus_cal_call_remove_finish);
}

void
e_gdbus_cal_call_refresh (GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_void ("refresh", e_gdbus_cal_call_refresh, E_GDBUS_ASYNC_OP_KEEPER (proxy), cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_refresh_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_cal_call_refresh);
}

gboolean
e_gdbus_cal_call_refresh_sync (GDBusProxy *proxy, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_void__void (proxy, cancellable, error,
		e_gdbus_cal_call_refresh,
		e_gdbus_cal_call_refresh_finish);
}

void
e_gdbus_cal_call_get_backend_property (GDBusProxy *proxy, const gchar *in_prop_name, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_string ("getBackendProperty", e_gdbus_cal_call_get_backend_property, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_prop_name, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_get_backend_property_finish (GDBusProxy *proxy, GAsyncResult *result, gchar **out_prop_value, GError **error)
{
	return e_gdbus_proxy_finish_call_string (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, out_prop_value, error, e_gdbus_cal_call_get_backend_property);
}

gboolean
e_gdbus_cal_call_get_backend_property_sync (GDBusProxy *proxy, const gchar *in_prop_name, gchar **out_prop_value, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_string__string (proxy, in_prop_name, out_prop_value, cancellable, error,
		e_gdbus_cal_call_get_backend_property,
		e_gdbus_cal_call_get_backend_property_finish);
}

/* free returned pointer with g_strfreev() */
gchar **
e_gdbus_cal_encode_set_backend_property (const gchar *in_prop_name, const gchar *in_prop_value)
{
	return encode_string_string (in_prop_name, in_prop_value);
}

/* free out_prop_name and out_prop_value with g_free() */
gboolean
e_gdbus_cal_decode_set_backend_property (const gchar * const *in_strv, gchar **out_prop_name, gchar **out_prop_value)
{
	return decode_string_string (in_strv, out_prop_name, out_prop_value);
}

void
e_gdbus_cal_call_set_backend_property (GDBusProxy *proxy, const gchar * const *in_prop_name_value, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_strv ("setBackendProperty", e_gdbus_cal_call_set_backend_property, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_prop_name_value, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_set_backend_property_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_cal_call_set_backend_property);
}

gboolean
e_gdbus_cal_call_set_backend_property_sync (GDBusProxy *proxy, const gchar * const *in_prop_name_value, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_strv__void (proxy, in_prop_name_value, cancellable, error,
		e_gdbus_cal_call_set_backend_property,
		e_gdbus_cal_call_set_backend_property_finish);
}

/* free returned pointer with g_strfreev() */
gchar **
e_gdbus_cal_encode_get_object (const gchar *in_uid, const gchar *in_rid)
{
	return encode_string_string (in_uid, in_rid);
}

/* free out_uid and out_rid with g_free() */
gboolean
e_gdbus_cal_decode_get_object (const gchar * const *in_strv, gchar **out_uid, gchar **out_rid)
{
	return decode_string_string (in_strv, out_uid, out_rid);
}

void
e_gdbus_cal_call_get_object (GDBusProxy *proxy, const gchar * const *in_uid_rid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_strv ("getObject", e_gdbus_cal_call_get_object, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_uid_rid, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_get_object_finish (GDBusProxy *proxy, GAsyncResult *result, gchar **out_object, GError **error)
{
	return e_gdbus_proxy_finish_call_string (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, out_object, error, e_gdbus_cal_call_get_object);
}

gboolean
e_gdbus_cal_call_get_object_sync (GDBusProxy *proxy, const gchar * const *in_uid_rid, gchar **out_object, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_strv__string (proxy, in_uid_rid, out_object, cancellable, error,
		e_gdbus_cal_call_get_object,
		e_gdbus_cal_call_get_object_finish);
}

void
e_gdbus_cal_call_get_object_list (GDBusProxy *proxy, const gchar *in_sexp, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_string ("getObjectList", e_gdbus_cal_call_get_object_list, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_sexp, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_get_object_list_finish (GDBusProxy *proxy, GAsyncResult *result, gchar ***out_objects, GError **error)
{
	return e_gdbus_proxy_finish_call_strv (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, out_objects, error, e_gdbus_cal_call_get_object_list);
}

gboolean
e_gdbus_cal_call_get_object_list_sync (GDBusProxy *proxy, const gchar *in_sexp, gchar ***out_objects, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_string__strv (proxy, in_sexp, out_objects, cancellable, error,
		e_gdbus_cal_call_get_object_list,
		e_gdbus_cal_call_get_object_list_finish);
}

/* free returned pointer with g_strfreev() */
gchar **
e_gdbus_cal_encode_get_free_busy (guint in_start, guint in_end, const GSList *in_users)
{
	gchar **strv;
	gint ii;

	g_return_val_if_fail (in_users != NULL, NULL);

	strv = g_new0 (gchar *, g_slist_length ((GSList *) in_users) + 3);
	strv[0] = g_strdup_printf ("%u", in_start);
	strv[1] = g_strdup_printf ("%u", in_end);

	for (ii = 0; in_users; ii++, in_users = in_users->next) {
		strv[ii + 2] = e_util_utf8_make_valid (in_users->data);
	}

	strv[ii + 2] = NULL;

	return strv;
}

/* free out_users with g_slist_foreach (out_users, (GFunc) g_free, NULL), g_slist_free (out_users); */
gboolean
e_gdbus_cal_decode_get_free_busy (const gchar * const *in_strv, guint *out_start, guint *out_end, GSList **out_users)
{
	gint ii;

	g_return_val_if_fail (in_strv != NULL, FALSE);
	g_return_val_if_fail (in_strv[0] != NULL, FALSE);
	g_return_val_if_fail (in_strv[1] != NULL, FALSE);
	g_return_val_if_fail (out_start != NULL, FALSE);
	g_return_val_if_fail (out_end != NULL, FALSE);
	g_return_val_if_fail (out_users != NULL, FALSE);

	*out_users = NULL;

	for (ii = 0; in_strv[ii + 2]; ii++) {
		*out_users = g_slist_prepend (*out_users, g_strdup (in_strv[ii + 2]));
	}

	*out_start = atoi (in_strv[0]);
	*out_end = atoi (in_strv[1]);
	*out_users = g_slist_reverse (*out_users);

	return TRUE;
}

void
e_gdbus_cal_call_get_free_busy (GDBusProxy *proxy, const gchar * const *in_start_end_userlist, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_strv ("getFreeBusy", e_gdbus_cal_call_get_free_busy, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_start_end_userlist, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_get_free_busy_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_cal_call_get_free_busy);
}

gboolean
e_gdbus_cal_call_get_free_busy_sync (GDBusProxy *proxy, const gchar * const *in_start_end_userlist, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_strv__void (proxy, in_start_end_userlist, cancellable, error,
		e_gdbus_cal_call_get_free_busy,
		e_gdbus_cal_call_get_free_busy_finish);
}

void
e_gdbus_cal_call_create_object (GDBusProxy *proxy, const gchar *in_calobj, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_string ("createObject", e_gdbus_cal_call_create_object, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_calobj, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_create_object_finish (GDBusProxy *proxy, GAsyncResult *result, gchar **out_uid, GError **error)
{
	return e_gdbus_proxy_finish_call_string (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, out_uid, error, e_gdbus_cal_call_create_object);
}

gboolean
e_gdbus_cal_call_create_object_sync (GDBusProxy *proxy, const gchar *in_calobj, gchar **out_uid, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_string__string (proxy, in_calobj, out_uid, cancellable, error,
		e_gdbus_cal_call_create_object,
		e_gdbus_cal_call_create_object_finish);
}

/* free returned pointer with g_strfreev() */
gchar **
e_gdbus_cal_encode_modify_object (const gchar *in_calobj, guint in_mod)
{
	gchar **strv;

	g_return_val_if_fail (in_calobj != NULL, NULL);

	strv = g_new0 (gchar *, 3);
	strv[0] = e_util_utf8_make_valid (in_calobj);
	strv[1] = g_strdup_printf ("%u", (guint32) in_mod);
	strv[2] = NULL;

	return strv;
}

/* free out_calobj with g_free() */
gboolean
e_gdbus_cal_decode_modify_object (const gchar * const *in_strv, gchar **out_calobj, guint *out_mod)
{
	g_return_val_if_fail (in_strv != NULL, FALSE);
	g_return_val_if_fail (in_strv[0] != NULL, FALSE);
	g_return_val_if_fail (in_strv[1] != NULL, FALSE);
	g_return_val_if_fail (in_strv[2] == NULL, FALSE);
	g_return_val_if_fail (out_calobj != NULL, FALSE);
	g_return_val_if_fail (out_mod != NULL, FALSE);

	*out_calobj = g_strdup (in_strv[0]);
	*out_mod = atoi (in_strv[1]);

	return TRUE;
}

void
e_gdbus_cal_call_modify_object (GDBusProxy *proxy, const gchar * const *in_calobj_mod, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_strv ("modifyObject", e_gdbus_cal_call_modify_object, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_calobj_mod, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_modify_object_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_cal_call_modify_object);
}

gboolean
e_gdbus_cal_call_modify_object_sync (GDBusProxy *proxy, const gchar * const *in_calobj_mod, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_strv__void (proxy, in_calobj_mod, cancellable, error,
		e_gdbus_cal_call_modify_object,
		e_gdbus_cal_call_modify_object_finish);
}

/* free returned pointer with g_strfreev() */
gchar **
e_gdbus_cal_encode_remove_object (const gchar *in_uid, const gchar *in_rid, guint in_mod)
{
	gchar **strv;

	g_return_val_if_fail (in_uid != NULL, NULL);

	strv = g_new0 (gchar *, 4);
	strv[0] = e_util_utf8_make_valid (in_uid);
	strv[1] = e_util_utf8_make_valid (in_rid ? in_rid : "");
	strv[2] = g_strdup_printf ("%u", (guint32) in_mod);
	strv[3] = NULL;

	return strv;
}

/* free out_uid and out_rid with g_free() */
gboolean
e_gdbus_cal_decode_remove_object (const gchar * const *in_strv, gchar **out_uid, gchar **out_rid, guint *out_mod)
{
	g_return_val_if_fail (in_strv != NULL, FALSE);
	g_return_val_if_fail (in_strv[0] != NULL, FALSE);
	g_return_val_if_fail (in_strv[1] != NULL, FALSE);
	g_return_val_if_fail (in_strv[2] != NULL, FALSE);
	g_return_val_if_fail (in_strv[3] == NULL, FALSE);
	g_return_val_if_fail (out_uid != NULL, FALSE);
	g_return_val_if_fail (out_rid != NULL, FALSE);
	g_return_val_if_fail (out_mod != NULL, FALSE);

	*out_uid = g_strdup (in_strv[0]);
	*out_rid = g_strdup (in_strv[1]);
	*out_mod = atoi (in_strv[2]);

	return TRUE;
}

void
e_gdbus_cal_call_remove_object (GDBusProxy *proxy, const gchar * const *in_uid_rid_mod, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_strv ("removeObject", e_gdbus_cal_call_remove_object, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_uid_rid_mod, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_remove_object_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_cal_call_remove_object);
}

gboolean
e_gdbus_cal_call_remove_object_sync (GDBusProxy *proxy, const gchar * const *in_uid_rid_mod, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_strv__void (proxy, in_uid_rid_mod, cancellable, error,
		e_gdbus_cal_call_remove_object,
		e_gdbus_cal_call_remove_object_finish);
}

void
e_gdbus_cal_call_receive_objects (GDBusProxy *proxy, const gchar *in_calobj, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_string ("receiveObjects", e_gdbus_cal_call_receive_objects, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_calobj, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_receive_objects_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_cal_call_receive_objects);
}

gboolean
e_gdbus_cal_call_receive_objects_sync (GDBusProxy *proxy, const gchar *in_calobj, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_string__void (proxy, in_calobj, cancellable, error,
		e_gdbus_cal_call_receive_objects,
		e_gdbus_cal_call_receive_objects_finish);
}

/* free returned pointer with g_strfreev() */
gchar **
e_gdbus_cal_encode_send_objects (const gchar *in_calobj, const GSList *in_users)
{
	gint ii;
	gchar **strv;

	g_return_val_if_fail (in_calobj != NULL, NULL);

	strv = g_new0 (gchar *, g_slist_length ((GSList *) in_users) + 2);
	strv[0] = e_util_utf8_make_valid (in_calobj);
	for (ii = 0; in_users; ii++, in_users = in_users->next) {
		strv[ii + 1] = e_util_utf8_make_valid (in_users->data);
	}
	strv[ii + 1] = NULL;

	return strv;
}

/* free out_calobj with g_free() and out_users with g_strfreev() */
gboolean
e_gdbus_cal_decode_send_objects (const gchar * const *in_strv, gchar **out_calobj, GSList **out_users)
{
	gint ii;

	g_return_val_if_fail (in_strv != NULL, FALSE);
	g_return_val_if_fail (in_strv[0] != NULL, FALSE);
	g_return_val_if_fail (out_calobj != NULL, FALSE);
	g_return_val_if_fail (out_users != NULL, FALSE);

	*out_users = NULL;

	for (ii = 0; in_strv[ii + 1]; ii++) {
		*out_users = g_slist_prepend (*out_users, g_strdup (in_strv[ii + 1]));
	}

	*out_calobj = g_strdup (in_strv[0]);
	*out_users = g_slist_reverse (*out_users);

	return TRUE;
}

void
e_gdbus_cal_call_send_objects (GDBusProxy *proxy, const gchar *in_calobj, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_string ("sendObjects", e_gdbus_cal_call_send_objects, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_calobj, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_send_objects_finish (GDBusProxy *proxy, GAsyncResult *result, gchar ***out_calobj_users, GError **error)
{
	return e_gdbus_proxy_finish_call_strv (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, out_calobj_users, error, e_gdbus_cal_call_send_objects);
}

gboolean
e_gdbus_cal_call_send_objects_sync (GDBusProxy *proxy, const gchar *in_calobj, gchar ***out_calobj_users, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_string__strv (proxy, in_calobj, out_calobj_users, cancellable, error,
		e_gdbus_cal_call_send_objects,
		e_gdbus_cal_call_send_objects_finish);
}

/* free returned pointer with g_strfreev() */
gchar **
e_gdbus_cal_encode_get_attachment_uris (const gchar *in_uid, const gchar *in_rid)
{
	return encode_string_string (in_uid, in_rid);
}

/* free out_uid and out_rid with g_free() */
gboolean
e_gdbus_cal_decode_get_attachment_uris (const gchar * const *in_strv, gchar **out_uid, gchar **out_rid)
{
	return decode_string_string (in_strv, out_uid, out_rid);
}

void
e_gdbus_cal_call_get_attachment_uris (GDBusProxy *proxy, const gchar * const *in_uid_rid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_strv ("getAttachmentUris", e_gdbus_cal_call_get_attachment_uris, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_uid_rid, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_get_attachment_uris_finish (GDBusProxy *proxy, GAsyncResult *result, gchar ***out_attachments, GError **error)
{
	return e_gdbus_proxy_finish_call_strv (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, out_attachments, error, e_gdbus_cal_call_get_attachment_uris);
}

gboolean
e_gdbus_cal_call_get_attachment_uris_sync (GDBusProxy *proxy, const gchar * const *in_uid_rid, gchar ***out_attachments, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_strv__strv (proxy, in_uid_rid, out_attachments, cancellable, error,
		e_gdbus_cal_call_get_attachment_uris,
		e_gdbus_cal_call_get_attachment_uris_finish);
}

/* free returned pointer with g_strfreev() */
gchar **
e_gdbus_cal_encode_discard_alarm (const gchar *in_uid, const gchar *in_rid, const gchar *in_auid)
{
	gchar **strv;

	strv = g_new0 (gchar *, 4);
	strv[0] = e_util_utf8_make_valid (in_uid ? in_uid : "");
	strv[1] = e_util_utf8_make_valid (in_rid ? in_rid : "");
	strv[2] = e_util_utf8_make_valid (in_auid ? in_auid : "");
	strv[3] = NULL;

	return strv;
}

/* free out_uid, out_rid and out_auid with g_free() */
gboolean
e_gdbus_cal_decode_discard_alarm (const gchar * const *in_strv, gchar **out_uid, gchar **out_rid, gchar **out_auid)
{
	g_return_val_if_fail (in_strv != NULL, FALSE);
	g_return_val_if_fail (in_strv[0] != NULL, FALSE);
	g_return_val_if_fail (in_strv[1] != NULL, FALSE);
	g_return_val_if_fail (in_strv[2] != NULL, FALSE);
	g_return_val_if_fail (in_strv[3] == NULL, FALSE);
	g_return_val_if_fail (out_uid != NULL, FALSE);
	g_return_val_if_fail (out_rid != NULL, FALSE);
	g_return_val_if_fail (out_auid != NULL, FALSE);

	*out_uid = g_strdup (in_strv[0]);
	*out_rid = g_strdup (in_strv[1]);
	*out_auid = g_strdup (in_strv[2]);

	return TRUE;
}

void
e_gdbus_cal_call_discard_alarm (GDBusProxy *proxy, const gchar * const *in_uid_rid_auid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_strv ("discardAlarm", e_gdbus_cal_call_discard_alarm, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_uid_rid_auid, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_discard_alarm_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_cal_call_discard_alarm);
}

gboolean
e_gdbus_cal_call_discard_alarm_sync (GDBusProxy *proxy, const gchar * const *in_uid_rid_auid, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_strv__void (proxy, in_uid_rid_auid, cancellable, error,
		e_gdbus_cal_call_discard_alarm,
		e_gdbus_cal_call_discard_alarm_finish);
}

void
e_gdbus_cal_call_get_view (GDBusProxy *proxy, const gchar *in_sexp, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_string ("getView", e_gdbus_cal_call_get_view, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_sexp, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_get_view_finish (GDBusProxy *proxy, GAsyncResult *result, gchar **out_view_path, GError **error)
{
	return e_gdbus_proxy_finish_call_string (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, out_view_path, error, e_gdbus_cal_call_get_view);
}

gboolean
e_gdbus_cal_call_get_view_sync (GDBusProxy *proxy, const gchar *in_sexp, gchar **out_view_path, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_string__string (proxy, in_sexp, out_view_path, cancellable, error,
		e_gdbus_cal_call_get_view,
		e_gdbus_cal_call_get_view_finish);
}

void
e_gdbus_cal_call_get_timezone (GDBusProxy *proxy, const gchar *in_tzid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_string ("getTimezone", e_gdbus_cal_call_get_timezone, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_tzid, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_get_timezone_finish (GDBusProxy *proxy, GAsyncResult *result, gchar **out_tzobject, GError **error)
{
	return e_gdbus_proxy_finish_call_string (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, out_tzobject, error, e_gdbus_cal_call_get_timezone);
}

gboolean
e_gdbus_cal_call_get_timezone_sync (GDBusProxy *proxy, const gchar *in_tzid, gchar **out_tzobject, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_string__string (proxy, in_tzid, out_tzobject, cancellable, error,
		e_gdbus_cal_call_get_timezone,
		e_gdbus_cal_call_get_timezone_finish);
}

void
e_gdbus_cal_call_add_timezone (GDBusProxy *proxy, const gchar *in_tzobject, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_call_string ("addTimezone", e_gdbus_cal_call_add_timezone, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_tzobject, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_add_timezone_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_cal_call_add_timezone);
}

gboolean
e_gdbus_cal_call_add_timezone_sync (GDBusProxy *proxy, const gchar *in_tzobject, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_call_sync_string__void (proxy, in_tzobject, cancellable, error,
		e_gdbus_cal_call_add_timezone,
		e_gdbus_cal_call_add_timezone_finish);
}

void
e_gdbus_cal_call_authenticate_user (GDBusProxy *proxy, const gchar * const *in_credentials, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_method_call_strv ("authenticateUser", proxy, in_credentials, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_authenticate_user_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_method_call_finish_void (proxy, result, error);
}

gboolean
e_gdbus_cal_call_authenticate_user_sync (GDBusProxy *proxy, const gchar * const *in_credentials, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_method_call_sync_strv__void ("authenticateUser", proxy, in_credentials, cancellable, error);
}

void
e_gdbus_cal_call_cancel_operation (GDBusProxy *proxy, guint in_opid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_method_call_uint ("cancelOperation", proxy, in_opid, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_cancel_operation_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_method_call_finish_void (proxy, result, error);
}

gboolean
e_gdbus_cal_call_cancel_operation_sync (GDBusProxy *proxy, guint in_opid, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_method_call_sync_uint__void ("cancelOperation", proxy, in_opid, cancellable, error);
}

void
e_gdbus_cal_call_cancel_all (GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_method_call_void ("cancelAll", proxy, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_call_cancel_all_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_method_call_finish_void (proxy, result, error);
}

gboolean
e_gdbus_cal_call_cancel_all_sync (GDBusProxy *proxy, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_method_call_sync_void__void ("cancelAll", proxy, cancellable, error);
}

void
e_gdbus_cal_call_close (GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_method_call_void ("close", proxy, cancellable, callback, user_data);
}
	
gboolean
e_gdbus_cal_call_close_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_method_call_finish_void (proxy, result, error);
}

gboolean
e_gdbus_cal_call_close_sync (GDBusProxy *proxy, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_method_call_sync_void__void ("close", proxy, cancellable, error);
}

#define DECLARE_EMIT_DONE_SIGNAL_0(_mname, _sig_id)									\
void															\
e_gdbus_cal_emit_ ## _mname ## _done (EGdbusCal *object, guint arg_opid, const GError *arg_error)			\
{															\
	g_signal_emit (object, signals[_sig_id], 0, arg_opid, arg_error);						\
}

#define DECLARE_EMIT_DONE_SIGNAL_1(_mname, _sig_id, _par_type)								\
void															\
e_gdbus_cal_emit_ ## _mname ## _done (EGdbusCal *object, guint arg_opid, const GError *arg_error, _par_type out_par)	\
{															\
	g_signal_emit (object, signals[_sig_id], 0, arg_opid, arg_error, out_par);					\
}

DECLARE_EMIT_DONE_SIGNAL_0 (open,			__OPEN_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_0 (remove,			__REMOVE_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_0 (refresh,			__REFRESH_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_1 (get_backend_property,	__GET_BACKEND_PROPERTY_DONE_SIGNAL, const gchar *)
DECLARE_EMIT_DONE_SIGNAL_0 (set_backend_property,	__SET_BACKEND_PROPERTY_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_1 (get_object,			__GET_OBJECT_DONE_SIGNAL, const gchar *)
DECLARE_EMIT_DONE_SIGNAL_1 (get_object_list,		__GET_OBJECT_LIST_DONE_SIGNAL, const gchar * const *)
DECLARE_EMIT_DONE_SIGNAL_0 (get_free_busy,		__GET_FREE_BUSY_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_1 (create_object,		__CREATE_OBJECT_DONE_SIGNAL, const gchar *)
DECLARE_EMIT_DONE_SIGNAL_0 (modify_object,		__MODIFY_OBJECT_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_0 (remove_object,		__REMOVE_OBJECT_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_0 (receive_objects,		__RECEIVE_OBJECTS_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_1 (send_objects,		__SEND_OBJECTS_DONE_SIGNAL, const gchar * const *)
DECLARE_EMIT_DONE_SIGNAL_1 (get_attachment_uris,	__GET_ATTACHMENT_URIS_DONE_SIGNAL, const gchar * const *)
DECLARE_EMIT_DONE_SIGNAL_0 (discard_alarm,		__DISCARD_ALARM_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_1 (get_view,			__GET_VIEW_DONE_SIGNAL, const gchar *)
DECLARE_EMIT_DONE_SIGNAL_1 (get_timezone,		__GET_TIMEZONE_DONE_SIGNAL, const gchar *)
DECLARE_EMIT_DONE_SIGNAL_0 (add_timezone,		__ADD_TIMEZONE_DONE_SIGNAL)

void
e_gdbus_cal_emit_backend_error (EGdbusCal *object, const gchar *arg_message)
{
	g_return_if_fail (object != NULL);
	g_return_if_fail (arg_message != NULL);

	g_signal_emit (object, signals[__BACKEND_ERROR_SIGNAL], 0, arg_message);
}

void
e_gdbus_cal_emit_readonly (EGdbusCal *object, gboolean arg_is_readonly)
{
	g_signal_emit (object, signals[__READONLY_SIGNAL], 0, arg_is_readonly);
}

void
e_gdbus_cal_emit_online (EGdbusCal *object, gboolean arg_is_online)
{
	g_signal_emit (object, signals[__ONLINE_SIGNAL], 0, arg_is_online);
}

void
e_gdbus_cal_emit_auth_required (EGdbusCal *object, const gchar * const *arg_credentials)
{
	g_signal_emit (object, signals[__AUTH_REQUIRED_SIGNAL], 0, arg_credentials);
}

void
e_gdbus_cal_emit_opened (EGdbusCal *object, const gchar * const *arg_error)
{
	g_signal_emit (object, signals[__OPENED_SIGNAL], 0, arg_error);
}

void
e_gdbus_cal_emit_free_busy_data (EGdbusCal *object, const gchar * const *arg_free_busy)
{
	g_signal_emit (object, signals[__FREE_BUSY_DATA_SIGNAL], 0, arg_free_busy);
}

E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (cal, backend_error, message, "s")
E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (cal, readonly, is_readonly, "b")
E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (cal, online, is_online, "b")
E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (cal, auth_required, credentials, "as")
E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (cal, opened, error, "as")
E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (cal, free_busy_data, free_busy_data, "as")

E_DECLARE_GDBUS_ASYNC_METHOD_1			(cal, open, only_if_exists, "b")
E_DECLARE_GDBUS_ASYNC_METHOD_0			(cal, remove)
E_DECLARE_GDBUS_ASYNC_METHOD_0			(cal, refresh)
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN	(cal, getBackendProperty, propname, "s", propvalue, "s")
E_DECLARE_GDBUS_ASYNC_METHOD_1			(cal, setBackendProperty, propnamevalue, "as")
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN	(cal, getObject, uid_rid, "as", object, "s")
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN	(cal, getObjectList, sexp, "s", objects, "as")
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN	(cal, getFreeBusy, start_stop_users, "as", freebusy, "as")
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN	(cal, createObject, object, "s", uid, "s")
E_DECLARE_GDBUS_ASYNC_METHOD_1			(cal, modifyObject, object_mod, "as")
E_DECLARE_GDBUS_ASYNC_METHOD_1			(cal, removeObject, uid_rid_mod, "as")
E_DECLARE_GDBUS_ASYNC_METHOD_1			(cal, receiveObjects, object, "s")
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN	(cal, sendObjects, object, "s", object_users, "as")
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN	(cal, getAttachmentUris, uid_rid, "as", attachments, "as")
E_DECLARE_GDBUS_ASYNC_METHOD_1			(cal, discardAlarm, uid_rid_auid, "as")
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN	(cal, getView, sexp, "s", view_path, "s")
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN	(cal, getTimezone, tzid, "s", tzobject, "s")
E_DECLARE_GDBUS_ASYNC_METHOD_1			(cal, addTimezone, tzobject, "s")

E_DECLARE_GDBUS_SYNC_METHOD_1			(cal, authenticateUser, credentials, "as")
E_DECLARE_GDBUS_SYNC_METHOD_1			(cal, cancelOperation, opid, "u")
E_DECLARE_GDBUS_SYNC_METHOD_0			(cal, cancelAll)
E_DECLARE_GDBUS_SYNC_METHOD_0			(cal, close)

static const GDBusMethodInfo * const e_gdbus_cal_method_info_pointers[] =
{
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, open),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, remove),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, refresh),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, getBackendProperty),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, setBackendProperty),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, getObject),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, getObjectList),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, getFreeBusy),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, createObject),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, modifyObject),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, removeObject),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, receiveObjects),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, sendObjects),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, getAttachmentUris),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, discardAlarm),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, getView),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, getTimezone),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, addTimezone),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, authenticateUser),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, cancelOperation),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, cancelAll),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal, close),
	NULL
};

static const GDBusSignalInfo * const e_gdbus_cal_signal_info_pointers[] =
{
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, backend_error),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, readonly),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, online),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, auth_required),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, opened),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, free_busy_data),

	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, open_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, remove_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, refresh_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, getBackendProperty_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, setBackendProperty_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, getObject_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, getObjectList_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, getFreeBusy_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, createObject_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, modifyObject_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, removeObject_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, receiveObjects_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, sendObjects_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, getAttachmentUris_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, discardAlarm_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, getView_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, getTimezone_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (cal, addTimezone_done),
	NULL
};

static const GDBusInterfaceInfo _e_gdbus_cal_interface_info =
{
	-1,
	(gchar *) GDBUS_CAL_INTERFACE_NAME,
	(GDBusMethodInfo **) &e_gdbus_cal_method_info_pointers,
	(GDBusSignalInfo **) &e_gdbus_cal_signal_info_pointers,
	(GDBusPropertyInfo **) NULL
};

static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
	guint method_id, method_type;

	method_id = lookup_method_id_from_method_name (method_name);
	method_type = lookup_method_type_from_method_name (method_name);

	g_return_if_fail (method_id != 0);
	g_return_if_fail (method_type != 0);

	e_gdbus_stub_handle_method_call (user_data, invocation, parameters, method_name, signals[method_id], method_type);
}

static GVariant *
get_property (GDBusConnection *connection, const gchar *sender, const gchar *object_path, const gchar *interface_name, const gchar *property_name, GError **error, gpointer user_data)
{
	g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "This implementation does not support property `%s'", property_name);
	return NULL;
}

static gboolean
set_property (GDBusConnection *connection, const gchar *sender, const gchar *object_path, const gchar *interface_name, const gchar *property_name, GVariant *value, GError **error, gpointer user_data)
{
	g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "This implementation does not support property `%s'", property_name);
	return FALSE;
}

static const GDBusInterfaceVTable e_gdbus_cal_interface_vtable =
{
	handle_method_call,
	get_property,
	set_property
};

static gboolean
emit_notifications_in_idle (gpointer user_data)
{
	GObject *object = G_OBJECT (user_data);
	GDBusConnection *connection;
	const gchar *path;
	GHashTable *notification_queue;
	GHashTableIter iter;
	const gchar *property_name;
	GVariant *value;
	GVariantBuilder *builder;
	GVariantBuilder *invalidated_builder;
	GHashTable *pvc;
	gboolean has_changes;

	notification_queue = g_object_get_data (object, "gdbus-codegen-notification-queue");
	path = g_object_get_data (object, "gdbus-codegen-path");
	connection = g_object_get_data (object, "gdbus-codegen-connection");
	pvc = g_object_get_data (object, "gdbus-codegen-pvc");
	g_assert (notification_queue != NULL && path != NULL && connection != NULL && pvc != NULL);

	builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
	invalidated_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
	g_hash_table_iter_init (&iter, notification_queue);
	has_changes = FALSE;
	while (g_hash_table_iter_next (&iter, (gpointer) &property_name, (gpointer) &value)) {
		GVariant *cached_value;
		cached_value = g_hash_table_lookup (pvc, property_name);
		if (cached_value == NULL || !g_variant_equal (cached_value, value)) {
			g_hash_table_insert (pvc, (gpointer) property_name, (gpointer) g_variant_ref (value));
			g_variant_builder_add (builder, "{sv}", property_name, value);
			has_changes = TRUE;
		}
	}

	if (has_changes) {
		g_dbus_connection_emit_signal (connection,
					NULL,
					path,
					"org.freedesktop.DBus.Properties",
					"PropertiesChanged",
					g_variant_new ("(sa{sv}as)",
							GDBUS_CAL_INTERFACE_NAME,
							builder,
							invalidated_builder),
					NULL);
	} else {
		g_variant_builder_unref (builder);
		g_variant_builder_unref (invalidated_builder);
	}

	g_hash_table_remove_all (notification_queue);
	g_object_set_data (object, "gdbus-codegen-notification-idle-id", GUINT_TO_POINTER (0));
	return FALSE;
}

/**
 * e_gdbus_cal_drain_notify:
 * @object: A #EGdbusCal that is exported.
 *
 * If @object has queued notifications, empty the queue forcing
 * the <literal>PropertiesChanged</literal> signal to be emitted.
 * See <xref linkend="EGdbusCal.description"/> for more background information.
 */
void
e_gdbus_cal_drain_notify (EGdbusCal *object)
{
	gint idle_id;
	idle_id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (object), "gdbus-codegen-notification-idle-id"));
	if (idle_id > 0) {
		emit_notifications_in_idle (object);
		g_source_remove (idle_id);
	}
}

static void
on_object_unregistered (GObject *object)
{
	gint idle_id;
	idle_id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (object), "gdbus-codegen-notification-idle-id"));
	if (idle_id > 0) {
		g_source_remove (idle_id);
	}
	g_object_set_data (G_OBJECT (object), "gdbus-codegen-path", NULL);
	g_object_set_data (G_OBJECT (object), "gdbus-codegen-connection", NULL);
}

/**
 * e_gdbus_cal_register_object:
 * @object: An instance of a #GObject<!-- -->-derived type implementing the #EGdbusCal interface.
 * @connection: A #GDBusConnection.
 * @object_path: The object to register the object at.
 * @error: Return location for error or %NULL.
 *
 * Registers @object at @object_path on @connection.
 *
 * See <xref linkend="EGdbusCal.description"/>
 * for how properties, methods and signals are handled.
 *
 * Returns: 0 if @error is set, otherwise a registration id (never 0) that can be used with g_dbus_connection_unregister_object().
 */
guint
e_gdbus_cal_register_object (EGdbusCal *object, GDBusConnection *connection, const gchar *object_path, GError **error)
{
	GHashTable *pvc;

	pvc = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) g_variant_unref);

	g_object_set_data_full (G_OBJECT (object), "gdbus-codegen-path", (gpointer) g_strdup (object_path), g_free);
	g_object_set_data (G_OBJECT (object), "gdbus-codegen-connection", (gpointer) connection);
	g_object_set_data_full (G_OBJECT (object), "gdbus-codegen-pvc", (gpointer) pvc, (GDestroyNotify) g_hash_table_unref);

	return g_dbus_connection_register_object (connection,
			object_path,
			(GDBusInterfaceInfo *) &_e_gdbus_cal_interface_info,
			&e_gdbus_cal_interface_vtable,
			object,
			(GDestroyNotify) on_object_unregistered,
			error);
}

/**
 * e_gdbus_cal_interface_info:
 *
 * Gets interface description for the <literal>org.gnome.evolution.dataserver.Calendar</literal> D-Bus interface.
 *
 * Returns: A #GDBusInterfaceInfo. Do not free, the object is statically allocated.
 */
const GDBusInterfaceInfo *
e_gdbus_cal_interface_info (void)
{
	return &_e_gdbus_cal_interface_info;
}

/* ---------------------------------------------------------------------- */

static void proxy_iface_init (EGdbusCalIface *iface);
static void async_op_keeper_iface_init (EGdbusAsyncOpKeeperInterface *iface);

G_DEFINE_TYPE_WITH_CODE (EGdbusCalProxy, e_gdbus_cal_proxy, G_TYPE_DBUS_PROXY,
                         G_IMPLEMENT_INTERFACE (E_TYPE_GDBUS_CAL, proxy_iface_init)
			 G_IMPLEMENT_INTERFACE (E_TYPE_GDBUS_ASYNC_OP_KEEPER, async_op_keeper_iface_init));

static void
e_gdbus_cal_proxy_init (EGdbusCalProxy *proxy)
{
	g_dbus_proxy_set_interface_info (G_DBUS_PROXY (proxy), (GDBusInterfaceInfo *) &_e_gdbus_cal_interface_info);

	proxy->priv = G_TYPE_INSTANCE_GET_PRIVATE (proxy, E_TYPE_GDBUS_CAL_PROXY, EGdbusCalProxyPrivate);
	proxy->priv->pending_ops = e_gdbus_async_op_keeper_create_pending_ops (E_GDBUS_ASYNC_OP_KEEPER (proxy));

	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID   (open);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID   (remove);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID   (refresh);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRING (get_backend_property);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID   (set_backend_property);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRING (get_object);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRV   (get_object_list);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRV   (get_free_busy);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRING (create_object);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID   (modify_object);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID   (remove_object);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID   (receive_objects);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRV   (send_objects);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRV   (get_attachment_uris);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID   (discard_alarm);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRING (get_view);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRING (get_timezone);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID   (add_timezone);
}

static void
g_signal (GDBusProxy *proxy, const gchar *sender_name, const gchar *signal_name, GVariant *parameters)
{
	guint signal_id, signal_type;

	signal_id = lookup_signal_id_from_signal_name (signal_name);
	signal_type = lookup_signal_type_from_signal_name (signal_name);

	g_return_if_fail (signal_id != 0);
	g_return_if_fail (signal_type != 0);

	e_gdbus_proxy_emit_signal (proxy, parameters, signals[signal_id], signal_type);
}

static void
gdbus_cal_proxy_finalize (GObject *object)
{
	EGdbusCalProxy *proxy = E_GDBUS_CAL_PROXY (object);

	g_return_if_fail (proxy != NULL);
	g_return_if_fail (proxy->priv != NULL);

	if (g_hash_table_size (proxy->priv->pending_ops))
		g_debug ("%s: Kept %d items in pending_ops", G_STRFUNC, g_hash_table_size (proxy->priv->pending_ops));

	g_hash_table_destroy (proxy->priv->pending_ops);

	G_OBJECT_CLASS (e_gdbus_cal_proxy_parent_class)->finalize (object);
}

static void
e_gdbus_cal_proxy_class_init (EGdbusCalProxyClass *klass)
{
	GObjectClass *object_class;
	GDBusProxyClass *proxy_class;

	g_type_class_add_private (klass, sizeof (EGdbusCalProxyPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gdbus_cal_proxy_finalize;

	proxy_class = G_DBUS_PROXY_CLASS (klass);
	proxy_class->g_signal = g_signal;
}

static void
proxy_iface_init (EGdbusCalIface *iface)
{
}

static GHashTable *
gdbus_cal_get_pending_ops (EGdbusAsyncOpKeeper *object)
{
	EGdbusCalProxy *proxy;

	g_return_val_if_fail (object != NULL, NULL);
	g_return_val_if_fail (E_IS_GDBUS_CAL_PROXY (object), NULL);

	proxy = E_GDBUS_CAL_PROXY (object);
	g_return_val_if_fail (proxy != NULL, NULL);
	g_return_val_if_fail (proxy->priv != NULL, NULL);

	return proxy->priv->pending_ops;
}

static gboolean
gdbus_cal_call_cancel_operation_sync (EGdbusAsyncOpKeeper *object, guint in_opid, GCancellable *cancellable, GError **error)
{
	return e_gdbus_cal_call_cancel_operation_sync (G_DBUS_PROXY (object), in_opid, cancellable, error);
}

static void
async_op_keeper_iface_init (EGdbusAsyncOpKeeperInterface *iface)
{
	g_return_if_fail (iface != NULL);

	iface->get_pending_ops = gdbus_cal_get_pending_ops;
	iface->cancel_op_sync = gdbus_cal_call_cancel_operation_sync;
}

/**
 * e_gdbus_cal_proxy_new:
 * @connection: A #GDBusConnection.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique) or %NULL if @connection is not a message bus connection.
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Like g_dbus_proxy_new() but returns a #EGdbusCalProxy.
 *
 * This is a failable asynchronous constructor - when the proxy is ready, callback will be invoked and you can use e_gdbus_cal_proxy_new_finish() to get the result.
 */
void
e_gdbus_cal_proxy_new (GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	g_async_initable_new_async (E_TYPE_GDBUS_CAL_PROXY,
				G_PRIORITY_DEFAULT,
				cancellable,
				callback,
				user_data,
				"g-flags", flags,
				"g-name", name,
				"g-connection", connection,
				"g-object-path", object_path,
				"g-interface-name", GDBUS_CAL_INTERFACE_NAME,
				NULL);
}

/**
 * e_gdbus_cal_proxy_new_finish:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to e_gdbus_cal_proxy_new().
 * @error: Return location for error or %NULL.
 *
 * Finishes creating a #EGdbusCalProxy.
 *
 * Returns: A #EGdbusCalProxy or %NULL if @error is set. Free with g_object_unref().
 */
EGdbusCal *
e_gdbus_cal_proxy_new_finish (GAsyncResult  *result, GError **error)
{
	GObject *object;
	GObject *source_object;
	source_object = g_async_result_get_source_object (result);
	g_assert (source_object != NULL);
	object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), result, error);
	g_object_unref (source_object);
	if (object != NULL)
		return E_GDBUS_CAL (object);
	else
		return NULL;
}

/**
 * e_gdbus_cal_proxy_new_sync:
 * @connection: A #GDBusConnection.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique) or %NULL if @connection is not a message bus connection.
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Like g_dbus_proxy_new_sync() but returns a #EGdbusCalProxy.
 *
 * This is a synchronous failable constructor. See e_gdbus_cal_proxy_new() and e_gdbus_cal_proxy_new_finish() for the asynchronous version.
 *
 * Returns: A #EGdbusCalProxy or %NULL if error is set. Free with g_object_unref().
 */
EGdbusCal *
e_gdbus_cal_proxy_new_sync (GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error)
{
	GInitable *initable;
	initable = g_initable_new (E_TYPE_GDBUS_CAL_PROXY,
				cancellable,
				error,
				"g-flags", flags,
				"g-name", name,
				"g-connection", connection,
				"g-object-path", object_path,
				"g-interface-name", GDBUS_CAL_INTERFACE_NAME,
				NULL);
	if (initable != NULL)
		return E_GDBUS_CAL (initable);
	else
		return NULL;
}

/**
 * e_gdbus_cal_proxy_new_for_bus:
 * @bus_type: A #GBusType.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique).
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Like g_dbus_proxy_new_for_bus() but returns a #EGdbusCalProxy.
 *
 * This is a failable asynchronous constructor - when the proxy is ready, callback will be invoked and you can use e_gdbus_cal_proxy_new_for_bus_finish() to get the result.
 */
void
e_gdbus_cal_proxy_new_for_bus (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	g_async_initable_new_async (E_TYPE_GDBUS_CAL_PROXY,
				G_PRIORITY_DEFAULT,
				cancellable,
				callback,
				user_data,
				"g-flags", flags,
				"g-name", name,
				"g-bus-type", bus_type,
				"g-object-path", object_path,
				"g-interface-name", GDBUS_CAL_INTERFACE_NAME,
				NULL);
}

/**
 * e_gdbus_cal_proxy_new_for_bus_finish:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to e_gdbus_cal_proxy_new_for_bus().
 * @error: Return location for error or %NULL.
 *
 * Finishes creating a #EGdbusCalProxy.
 *
 * Returns: A #EGdbusCalProxy or %NULL if @error is set. Free with g_object_unref().
 */
EGdbusCal *
e_gdbus_cal_proxy_new_for_bus_finish (GAsyncResult *result, GError **error)
{
	GObject *object;
	GObject *source_object;
	source_object = g_async_result_get_source_object (result);
	g_assert (source_object != NULL);
	object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), result, error);
	g_object_unref (source_object);
	if (object != NULL)
		return E_GDBUS_CAL (object);
	else
		return NULL;
}

/**
 * e_gdbus_cal_proxy_new_for_bus_sync:
 * @bus_type: A #GBusType.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique).
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Like g_dbus_proxy_new_for_bus_sync() but returns a #EGdbusCalProxy.
 *
 * This is a synchronous failable constructor. See e_gdbus_cal_proxy_new_for_bus() and e_gdbus_cal_proxy_new_for_bus_finish() for the asynchronous version.
 *
 * Returns: A #EGdbusCalProxy or %NULL if error is set. Free with g_object_unref().
 */
EGdbusCal *
e_gdbus_cal_proxy_new_for_bus_sync (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error)
{
	GInitable *initable;
	initable = g_initable_new (E_TYPE_GDBUS_CAL_PROXY,
				cancellable,
				error,
				"g-flags", flags,
				"g-name", name,
				"g-bus-type", bus_type,
				"g-object-path", object_path,
				"g-interface-name", GDBUS_CAL_INTERFACE_NAME,
				NULL);
	if (initable != NULL)
		return E_GDBUS_CAL (initable);
	else
		return NULL;
}

/* ---------------------------------------------------------------------- */

struct _EGdbusCalStubPrivate
{
	gint foo;
};

static void stub_iface_init (EGdbusCalIface *iface);

G_DEFINE_TYPE_WITH_CODE (EGdbusCalStub, e_gdbus_cal_stub, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (E_TYPE_GDBUS_CAL, stub_iface_init));

static void
e_gdbus_cal_stub_init (EGdbusCalStub *stub)
{
	stub->priv = G_TYPE_INSTANCE_GET_PRIVATE (stub, E_TYPE_GDBUS_CAL_STUB, EGdbusCalStubPrivate);
}

static void
e_gdbus_cal_stub_class_init (EGdbusCalStubClass *klass)
{
	g_type_class_add_private (klass, sizeof (EGdbusCalStubPrivate));
}

static void
stub_iface_init (EGdbusCalIface *iface)
{
}

/**
 * e_gdbus_cal_stub_new:
 *
 * Creates a new stub object that can be exported via e_gdbus_cal_register_object().
 *
 * Returns: A #EGdbusCalStub instance. Free with g_object_unref().
 */
EGdbusCal *
e_gdbus_cal_stub_new (void)
{
	return E_GDBUS_CAL (g_object_new (E_TYPE_GDBUS_CAL_STUB, NULL));
}

/* Returns GDBus connection associated with the stub object */
GDBusConnection *
e_gdbus_cal_stub_get_connection (EGdbusCal *stub)
{
	g_return_val_if_fail (stub != NULL, NULL);
	g_return_val_if_fail (E_IS_GDBUS_CAL_STUB (stub), NULL);

	return g_object_get_data (G_OBJECT (stub), "gdbus-codegen-connection");
}

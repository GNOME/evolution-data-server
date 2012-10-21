/*
 * e-gdbus-book.c
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
#include <gio/gio.h>

#include "e-gdbus-book.h"

#define E_GDBUS_BOOK_PROXY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_GDBUS_BOOK_PROXY, EGdbusBookProxyPrivate))

#define GDBUS_BOOK_INTERFACE_NAME "org.gnome.evolution.dataserver.AddressBook"

typedef EGdbusBookIface EGdbusBookInterface;
G_DEFINE_INTERFACE (EGdbusBook, e_gdbus_book, G_TYPE_OBJECT);

enum
{
	_0_SIGNAL,
	__BACKEND_ERROR_SIGNAL,
	__READONLY_SIGNAL,
	__ONLINE_SIGNAL,
	__OPENED_SIGNAL,
	__BACKEND_PROPERTY_CHANGED_SIGNAL,
	__OPEN_METHOD,
	__OPEN_DONE_SIGNAL,
	__REMOVE_METHOD,
	__REMOVE_DONE_SIGNAL,
	__REFRESH_METHOD,
	__REFRESH_DONE_SIGNAL,
	__GET_CONTACT_METHOD,
	__GET_CONTACT_DONE_SIGNAL,
	__GET_CONTACT_LIST_METHOD,
	__GET_CONTACT_LIST_DONE_SIGNAL,
	__GET_CONTACT_LIST_UIDS_METHOD,
	__GET_CONTACT_LIST_UIDS_DONE_SIGNAL,
	__ADD_CONTACTS_METHOD,
	__ADD_CONTACTS_DONE_SIGNAL,
	__REMOVE_CONTACTS_METHOD,
	__REMOVE_CONTACTS_DONE_SIGNAL,
	__MODIFY_CONTACTS_METHOD,
	__MODIFY_CONTACTS_DONE_SIGNAL,
	__GET_BACKEND_PROPERTY_METHOD,
	__GET_BACKEND_PROPERTY_DONE_SIGNAL,
	__SET_BACKEND_PROPERTY_METHOD,
	__SET_BACKEND_PROPERTY_DONE_SIGNAL,
	__GET_VIEW_METHOD,
	__GET_VIEW_DONE_SIGNAL,
	__CANCEL_OPERATION_METHOD,
	__CANCEL_ALL_METHOD,
	__CLOSE_METHOD,
	__LAST_SIGNAL
};

static guint signals[__LAST_SIGNAL] = {0};

struct _EGdbusBookProxyPrivate
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

E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_STRING (GDBUS_BOOK_INTERFACE_NAME,
                                             backend_error)
E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_BOOLEAN (GDBUS_BOOK_INTERFACE_NAME,
                                              readonly)
E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_BOOLEAN (GDBUS_BOOK_INTERFACE_NAME,
                                              online)
E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_STRV (GDBUS_BOOK_INTERFACE_NAME,
                                           opened)
E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_STRV (GDBUS_BOOK_INTERFACE_NAME,
                                           backend_property_changed)

E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID (GDBUS_BOOK_INTERFACE_NAME,
                                                      open)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID (GDBUS_BOOK_INTERFACE_NAME,
                                                      remove)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID (GDBUS_BOOK_INTERFACE_NAME,
                                                      refresh)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRING (GDBUS_BOOK_INTERFACE_NAME,
                                                        get_contact)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRV (GDBUS_BOOK_INTERFACE_NAME,
                                                      get_contact_list)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRV (GDBUS_BOOK_INTERFACE_NAME,
                                                      get_contact_list_uids)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRV (GDBUS_BOOK_INTERFACE_NAME,
                                                        add_contacts)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID (GDBUS_BOOK_INTERFACE_NAME,
                                                      remove_contacts)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID (GDBUS_BOOK_INTERFACE_NAME,
                                                      modify_contacts)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRING (GDBUS_BOOK_INTERFACE_NAME,
                                                        get_backend_property)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID (GDBUS_BOOK_INTERFACE_NAME,
                                                      set_backend_property)
E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRING (GDBUS_BOOK_INTERFACE_NAME,
                                                        get_view)

static void
e_gdbus_book_default_init (EGdbusBookIface *iface)
{
	/* Build lookup structures */
	_method_name_to_id = g_hash_table_new (g_str_hash, g_str_equal);
	_method_name_to_type = g_hash_table_new (g_str_hash, g_str_equal);
	_signal_name_to_id = g_hash_table_new (g_str_hash, g_str_equal);
	_signal_name_to_type = g_hash_table_new (g_str_hash, g_str_equal);

	/* GObject signals definitions for D-Bus signals: */
	E_INIT_GDBUS_SIGNAL_STRING (
		EGdbusBookIface,
		"backend_error",
		backend_error,
		__BACKEND_ERROR_SIGNAL)
	E_INIT_GDBUS_SIGNAL_BOOLEAN (
		EGdbusBookIface,
		"readonly",
		readonly,
		__READONLY_SIGNAL)
	E_INIT_GDBUS_SIGNAL_BOOLEAN (
		EGdbusBookIface,
		"online",
		online,
		__ONLINE_SIGNAL)
	E_INIT_GDBUS_SIGNAL_STRV (
		EGdbusBookIface,
		"opened",
		opened,
		__OPENED_SIGNAL)
	E_INIT_GDBUS_SIGNAL_STRV (
		EGdbusBookIface,
		"backend_property_changed",
		backend_property_changed,
		__BACKEND_PROPERTY_CHANGED_SIGNAL)

	/* GObject signals definitions for D-Bus methods: */
	E_INIT_GDBUS_METHOD_ASYNC_BOOLEAN__VOID (
		EGdbusBookIface,
		"open",
		open,
		__OPEN_METHOD,
		__OPEN_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_VOID__VOID (
		EGdbusBookIface,
		"remove",
		remove,
		__REMOVE_METHOD,
		__REMOVE_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_VOID__VOID (
		EGdbusBookIface,
		"refresh",
		refresh,
		__REFRESH_METHOD,
		__REFRESH_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRING__STRING (
		EGdbusBookIface,
		"get_contact",
		get_contact,
		__GET_CONTACT_METHOD,
		__GET_CONTACT_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRING__STRV (
		EGdbusBookIface,
		"get_contact_list",
		get_contact_list,
		__GET_CONTACT_LIST_METHOD,
		__GET_CONTACT_LIST_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRING__STRV (
		EGdbusBookIface,
		"get_contact_list_uids",
		get_contact_list_uids,
		__GET_CONTACT_LIST_UIDS_METHOD,
		__GET_CONTACT_LIST_UIDS_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRV__STRV (
		EGdbusBookIface,
		"add_contacts",
		add_contacts,
		__ADD_CONTACTS_METHOD,
		__ADD_CONTACTS_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRV__VOID (
		EGdbusBookIface,
		"remove_contacts",
		remove_contacts,
		__REMOVE_CONTACTS_METHOD,
		__REMOVE_CONTACTS_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRV__VOID (
		EGdbusBookIface,
		"modify_contacts",
		modify_contacts,
		__MODIFY_CONTACTS_METHOD,
		__MODIFY_CONTACTS_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRING__STRING (
		EGdbusBookIface,
		"get_backend_property",
		get_backend_property,
		__GET_BACKEND_PROPERTY_METHOD,
		__GET_BACKEND_PROPERTY_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRV__VOID (
		EGdbusBookIface,
		"set_backend_property",
		set_backend_property,
		__SET_BACKEND_PROPERTY_METHOD,
		__SET_BACKEND_PROPERTY_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_ASYNC_STRING__STRING (
		EGdbusBookIface,
		"get_view",
		get_view,
		__GET_VIEW_METHOD,
		__GET_VIEW_DONE_SIGNAL)
	E_INIT_GDBUS_METHOD_UINT (
		EGdbusBookIface,
		"cancel_operation",
		cancel_operation,
		__CANCEL_OPERATION_METHOD)
	E_INIT_GDBUS_METHOD_VOID (
		EGdbusBookIface,
		"cancel_all",
		cancel_all,
		__CANCEL_ALL_METHOD)
	E_INIT_GDBUS_METHOD_VOID (
		EGdbusBookIface,
		"close",
		close,
		__CLOSE_METHOD)
}

void
e_gdbus_book_call_open (GDBusProxy *proxy,
                        gboolean in_only_if_exists,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	e_gdbus_proxy_call_boolean ("open", e_gdbus_book_call_open, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_only_if_exists, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_open_finish (GDBusProxy *proxy,
                               GAsyncResult *result,
                               GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_book_call_open);
}

gboolean
e_gdbus_book_call_open_sync (GDBusProxy *proxy,
                             gboolean in_only_if_exists,
                             GCancellable *cancellable,
                             GError **error)
{
	return e_gdbus_proxy_call_sync_boolean__void (
		proxy, in_only_if_exists, cancellable, error,
		e_gdbus_book_call_open,
		e_gdbus_book_call_open_finish);
}

void
e_gdbus_book_call_remove (GDBusProxy *proxy,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	e_gdbus_proxy_call_void ("remove", e_gdbus_book_call_remove, E_GDBUS_ASYNC_OP_KEEPER (proxy), cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_remove_finish (GDBusProxy *proxy,
                                 GAsyncResult *result,
                                 GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_book_call_remove);
}

gboolean
e_gdbus_book_call_remove_sync (GDBusProxy *proxy,
                               GCancellable *cancellable,
                               GError **error)
{
	return e_gdbus_proxy_call_sync_void__void (
		proxy, cancellable, error,
		e_gdbus_book_call_remove,
		e_gdbus_book_call_remove_finish);
}

void
e_gdbus_book_call_refresh (GDBusProxy *proxy,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	e_gdbus_proxy_call_void ("refresh", e_gdbus_book_call_refresh, E_GDBUS_ASYNC_OP_KEEPER (proxy), cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_refresh_finish (GDBusProxy *proxy,
                                  GAsyncResult *result,
                                  GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_book_call_refresh);
}

gboolean
e_gdbus_book_call_refresh_sync (GDBusProxy *proxy,
                                GCancellable *cancellable,
                                GError **error)
{
	return e_gdbus_proxy_call_sync_void__void (
		proxy, cancellable, error,
		e_gdbus_book_call_refresh,
		e_gdbus_book_call_refresh_finish);
}

void
e_gdbus_book_call_get_contact (GDBusProxy *proxy,
                               const gchar *in_uid,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	e_gdbus_proxy_call_string ("get_contact", e_gdbus_book_call_get_contact, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_uid, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_get_contact_finish (GDBusProxy *proxy,
                                      GAsyncResult *result,
                                      gchar **out_vcard,
                                      GError **error)
{
	return e_gdbus_proxy_finish_call_string (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, out_vcard, error, e_gdbus_book_call_get_contact);
}

gboolean
e_gdbus_book_call_get_contact_sync (GDBusProxy *proxy,
                                    const gchar *in_uid,
                                    gchar **out_vcard,
                                    GCancellable *cancellable,
                                    GError **error)
{
	return e_gdbus_proxy_call_sync_string__string (
		proxy, in_uid, out_vcard, cancellable, error,
		e_gdbus_book_call_get_contact,
		e_gdbus_book_call_get_contact_finish);
}

void
e_gdbus_book_call_get_contact_list (GDBusProxy *proxy,
                                    const gchar *in_query,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
	e_gdbus_proxy_call_string ("get_contact_list", e_gdbus_book_call_get_contact_list, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_query, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_get_contact_list_finish (GDBusProxy *proxy,
                                           GAsyncResult *result,
                                           gchar ***out_vcards,
                                           GError **error)
{
	return e_gdbus_proxy_finish_call_strv (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, out_vcards, error, e_gdbus_book_call_get_contact_list);
}

gboolean
e_gdbus_book_call_get_contact_list_sync (GDBusProxy *proxy,
                                         const gchar *in_query,
                                         gchar ***out_vcards,
                                         GCancellable *cancellable,
                                         GError **error)
{
	return e_gdbus_proxy_call_sync_string__strv (
		proxy, in_query, out_vcards, cancellable, error,
		e_gdbus_book_call_get_contact_list,
		e_gdbus_book_call_get_contact_list_finish);
}

void
e_gdbus_book_call_get_contact_list_uids (GDBusProxy *proxy,
                                         const gchar *in_query,
                                         GCancellable *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
	e_gdbus_proxy_call_string ("get_contact_list_uids", e_gdbus_book_call_get_contact_list_uids, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_query, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_get_contact_list_uids_finish (GDBusProxy *proxy,
                                                GAsyncResult *result,
                                                gchar ***out_uids,
                                                GError **error)
{
	return e_gdbus_proxy_finish_call_strv (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, out_uids, error, e_gdbus_book_call_get_contact_list_uids);
}

gboolean
e_gdbus_book_call_get_contact_list_uids_sync (GDBusProxy *proxy,
                                              const gchar *in_query,
                                              gchar ***out_uids,
                                              GCancellable *cancellable,
                                              GError **error)
{
	return e_gdbus_proxy_call_sync_string__strv (
		proxy, in_query, out_uids, cancellable, error,
		e_gdbus_book_call_get_contact_list_uids,
		e_gdbus_book_call_get_contact_list_uids_finish);
}

void
e_gdbus_book_call_add_contacts (GDBusProxy *proxy,
                               const gchar * const *in_vcards,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	e_gdbus_proxy_call_strv ("add_contacts", e_gdbus_book_call_add_contacts, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_vcards, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_add_contacts_finish (GDBusProxy *proxy,
                                      GAsyncResult *result,
                                      gchar ***out_uids,
                                      GError **error)
{
	return e_gdbus_proxy_finish_call_strv (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, out_uids, error, e_gdbus_book_call_add_contacts);
}

gboolean
e_gdbus_book_call_add_contacts_sync (GDBusProxy *proxy,
                                    const gchar * const *in_vcards,
                                    gchar ***out_uids,
                                    GCancellable *cancellable,
                                    GError **error)
{
	return e_gdbus_proxy_call_sync_strv__strv (
		proxy, in_vcards, out_uids, cancellable, error,
		e_gdbus_book_call_add_contacts,
		e_gdbus_book_call_add_contacts_finish);
}

void
e_gdbus_book_call_remove_contacts (GDBusProxy *proxy,
                                   const gchar * const *in_list,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	e_gdbus_proxy_call_strv ("remove_contacts", e_gdbus_book_call_remove_contacts, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_list, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_remove_contacts_finish (GDBusProxy *proxy,
                                          GAsyncResult *result,
                                          GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_book_call_remove_contacts);
}

gboolean
e_gdbus_book_call_remove_contacts_sync (GDBusProxy *proxy,
                                        const gchar * const *in_list,
                                        GCancellable *cancellable,
                                        GError **error)
{
	return e_gdbus_proxy_call_sync_strv__void (
		proxy, in_list, cancellable, error,
		e_gdbus_book_call_remove_contacts,
		e_gdbus_book_call_remove_contacts_finish);
}

void
e_gdbus_book_call_modify_contacts (GDBusProxy *proxy,
                                   const gchar * const *in_vcards,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	e_gdbus_proxy_call_strv ("modify_contacts", e_gdbus_book_call_modify_contacts, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_vcards, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_modify_contacts_finish (GDBusProxy *proxy,
                                         GAsyncResult *result,
                                         GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_book_call_modify_contacts);
}

gboolean
e_gdbus_book_call_modify_contacts_sync (GDBusProxy *proxy,
                                        const gchar * const *in_vcards,
                                        GCancellable *cancellable,
                                        GError **error)
{
	return e_gdbus_proxy_call_sync_strv__void (
		proxy, in_vcards, cancellable, error,
		e_gdbus_book_call_modify_contacts,
		e_gdbus_book_call_modify_contacts_finish);
}

void
e_gdbus_book_call_get_backend_property (GDBusProxy *proxy,
                                        const gchar *in_prop_name,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
	e_gdbus_proxy_call_string ("get_backend_property", e_gdbus_book_call_get_backend_property, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_prop_name, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_get_backend_property_finish (GDBusProxy *proxy,
                                               GAsyncResult *result,
                                               gchar **out_prop_value,
                                               GError **error)
{
	return e_gdbus_proxy_finish_call_string (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, out_prop_value, error, e_gdbus_book_call_get_backend_property);
}

gboolean
e_gdbus_book_call_get_backend_property_sync (GDBusProxy *proxy,
                                             const gchar *in_prop_name,
                                             gchar **out_prop_value,
                                             GCancellable *cancellable,
                                             GError **error)
{
	return e_gdbus_proxy_call_sync_string__string (
		proxy, in_prop_name, out_prop_value, cancellable, error,
		e_gdbus_book_call_get_backend_property,
		e_gdbus_book_call_get_backend_property_finish);
}

/* free returned pointer with g_strfreev() */
gchar **
e_gdbus_book_encode_set_backend_property (const gchar *in_prop_name,
                                          const gchar *in_prop_value)
{
	return e_gdbus_templates_encode_two_strings (in_prop_name, in_prop_value);
}

/* free out_prop_name and out_prop_value with g_free() */
gboolean
e_gdbus_book_decode_set_backend_property (const gchar * const *in_strv,
                                          gchar **out_prop_name,
                                          gchar **out_prop_value)
{
	return e_gdbus_templates_decode_two_strings (in_strv, out_prop_name, out_prop_value);
}

void
e_gdbus_book_call_set_backend_property (GDBusProxy *proxy,
                                        const gchar * const *in_prop_name_value,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
	e_gdbus_proxy_call_strv ("set_backend_property", e_gdbus_book_call_set_backend_property, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_prop_name_value, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_set_backend_property_finish (GDBusProxy *proxy,
                                               GAsyncResult *result,
                                               GError **error)
{
	return e_gdbus_proxy_finish_call_void (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, error, e_gdbus_book_call_set_backend_property);
}

gboolean
e_gdbus_book_call_set_backend_property_sync (GDBusProxy *proxy,
                                             const gchar * const *in_prop_name_value,
                                             GCancellable *cancellable,
                                             GError **error)
{
	return e_gdbus_proxy_call_sync_strv__void (
		proxy, in_prop_name_value, cancellable, error,
		e_gdbus_book_call_set_backend_property,
		e_gdbus_book_call_set_backend_property_finish);
}

void
e_gdbus_book_call_get_view (GDBusProxy *proxy,
                            const gchar *in_query,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	e_gdbus_proxy_call_string ("get_view", e_gdbus_book_call_get_view, E_GDBUS_ASYNC_OP_KEEPER (proxy), in_query, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_get_view_finish (GDBusProxy *proxy,
                                   GAsyncResult *result,
                                   gchar **out_view_path,
                                   GError **error)
{
	return e_gdbus_proxy_finish_call_string (E_GDBUS_ASYNC_OP_KEEPER (proxy), result, out_view_path, error, e_gdbus_book_call_get_view);
}

gboolean
e_gdbus_book_call_get_view_sync (GDBusProxy *proxy,
                                 const gchar *in_query,
                                 gchar **out_view_path,
                                 GCancellable *cancellable,
                                 GError **error)
{
	return e_gdbus_proxy_call_sync_string__string (
		proxy, in_query, out_view_path, cancellable, error,
		e_gdbus_book_call_get_view,
		e_gdbus_book_call_get_view_finish);
}

void
e_gdbus_book_call_cancel_operation (GDBusProxy *proxy,
                                    guint in_opid,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
	e_gdbus_proxy_method_call_uint ("cancel_operation", proxy, in_opid, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_cancel_operation_finish (GDBusProxy *proxy,
                                           GAsyncResult *result,
                                           GError **error)
{
	return e_gdbus_proxy_method_call_finish_void (proxy, result, error);
}

gboolean
e_gdbus_book_call_cancel_operation_sync (GDBusProxy *proxy,
                                         guint in_opid,
                                         GCancellable *cancellable,
                                         GError **error)
{
	return e_gdbus_proxy_method_call_sync_uint__void ("cancel_operation", proxy, in_opid, cancellable, error);
}

void
e_gdbus_book_call_cancel_all (GDBusProxy *proxy,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	e_gdbus_proxy_method_call_void ("cancel_all", proxy, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_cancel_all_finish (GDBusProxy *proxy,
                                     GAsyncResult *result,
                                     GError **error)
{
	return e_gdbus_proxy_method_call_finish_void (proxy, result, error);
}

gboolean
e_gdbus_book_call_cancel_all_sync (GDBusProxy *proxy,
                                   GCancellable *cancellable,
                                   GError **error)
{
	return e_gdbus_proxy_method_call_sync_void__void ("cancel_all", proxy, cancellable, error);
}

void
e_gdbus_book_call_close (GDBusProxy *proxy,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	e_gdbus_proxy_method_call_void ("close", proxy, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_call_close_finish (GDBusProxy *proxy,
                                GAsyncResult *result,
                                GError **error)
{
	return e_gdbus_proxy_method_call_finish_void (proxy, result, error);
}

gboolean
e_gdbus_book_call_close_sync (GDBusProxy *proxy,
                              GCancellable *cancellable,
                              GError **error)
{
	return e_gdbus_proxy_method_call_sync_void__void ("close", proxy, cancellable, error);
}

#define DECLARE_EMIT_DONE_SIGNAL_0(_mname, _sig_id)									\
void															\
e_gdbus_book_emit_ ## _mname ## _done (EGdbusBook *object, guint arg_opid, const GError *arg_error)			\
{															\
	g_signal_emit (object, signals[_sig_id], 0, arg_opid, arg_error);						\
}

#define DECLARE_EMIT_DONE_SIGNAL_1(_mname, _sig_id, _par_type)								\
void															\
e_gdbus_book_emit_ ## _mname ## _done (EGdbusBook *object, guint arg_opid, const GError *arg_error, _par_type out_par)	\
{															\
	g_signal_emit (object, signals[_sig_id], 0, arg_opid, arg_error, out_par);					\
}

DECLARE_EMIT_DONE_SIGNAL_0 (open,
                            __OPEN_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_0 (remove,
                            __REMOVE_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_0 (refresh,
                            __REFRESH_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_1 (get_contact,
                            __GET_CONTACT_DONE_SIGNAL,
                            const gchar *)
DECLARE_EMIT_DONE_SIGNAL_1 (get_contact_list,
                            __GET_CONTACT_LIST_DONE_SIGNAL,
                            const gchar * const *)
DECLARE_EMIT_DONE_SIGNAL_1 (get_contact_list_uids,
                            __GET_CONTACT_LIST_UIDS_DONE_SIGNAL,
                            const gchar * const *)
DECLARE_EMIT_DONE_SIGNAL_1 (add_contacts,
                            __ADD_CONTACTS_DONE_SIGNAL,
                            const gchar * const *)
DECLARE_EMIT_DONE_SIGNAL_0 (remove_contacts,
                            __REMOVE_CONTACTS_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_0 (modify_contacts,
                            __MODIFY_CONTACTS_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_1 (get_backend_property,
                            __GET_BACKEND_PROPERTY_DONE_SIGNAL,
                            const gchar *)
DECLARE_EMIT_DONE_SIGNAL_0 (set_backend_property,
                            __SET_BACKEND_PROPERTY_DONE_SIGNAL)
DECLARE_EMIT_DONE_SIGNAL_1 (get_view,
                            __GET_VIEW_DONE_SIGNAL,
                            const gchar *)

void
e_gdbus_book_emit_backend_error (EGdbusBook *object,
                                 const gchar *arg_message)
{
	g_return_if_fail (object != NULL);
	g_return_if_fail (arg_message != NULL);

	g_signal_emit (object, signals[__BACKEND_ERROR_SIGNAL], 0, arg_message);
}

void
e_gdbus_book_emit_readonly (EGdbusBook *object,
                            gboolean arg_is_readonly)
{
	g_signal_emit (object, signals[__READONLY_SIGNAL], 0, arg_is_readonly);
}

void
e_gdbus_book_emit_online (EGdbusBook *object,
                          gboolean arg_is_online)
{
	g_signal_emit (object, signals[__ONLINE_SIGNAL], 0, arg_is_online);
}

void
e_gdbus_book_emit_opened (EGdbusBook *object,
                          const gchar * const *arg_error)
{
	g_signal_emit (object, signals[__OPENED_SIGNAL], 0, arg_error);
}

void
e_gdbus_book_emit_backend_property_changed (EGdbusBook *object,
                                            const gchar * const *arg_name_value)
{
	g_signal_emit (object, signals[__BACKEND_PROPERTY_CHANGED_SIGNAL], 0, arg_name_value);
}

E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (book,
                                 backend_error,
                                 message,
                                 "s")
E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (book,
                                 readonly,
                                 is_readonly,
                                 "b")
E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (book,
                                 online,
                                 is_online,
                                 "b")
E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (book,
                                 opened,
                                 error,
                                 "as")
E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (book,
                                 backend_property_changed,
                                 name_value,
                                 "as")

E_DECLARE_GDBUS_ASYNC_METHOD_1 (book,
                                  open,
                                  only_if_exists,
                                  "b")
E_DECLARE_GDBUS_ASYNC_METHOD_0 (book,
                                remove)
E_DECLARE_GDBUS_ASYNC_METHOD_0 (book,
                                refresh)
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN (book,
                                            get_contact,
                                            uid,
                                            "s",
                                            vcard,
                                            "s")
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN (book,
                                            get_contact_list,
                                            query,
                                            "s",
                                            vcards,
                                            "as")
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN (book,
                                            get_contact_list_uids,
                                            query,
                                            "s",
                                            uids,
                                            "as")
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN (book,
                                            add_contacts,
                                            vcards,
                                            "as",
                                            uids,
                                            "as")
E_DECLARE_GDBUS_ASYNC_METHOD_1 (book,
                                  remove_contacts,
                                  list,
                                  "as")
E_DECLARE_GDBUS_ASYNC_METHOD_1 (book,
                                  modify_contacts,
                                  vcard,
                                  "as")
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN (book,
                                            get_backend_property,
                                            prop_name,
                                            "s",
                                            prop_value,
                                            "s")
E_DECLARE_GDBUS_ASYNC_METHOD_1 (book,
                                  set_backend_property,
                                  prop_name_value,
                                  "as")
E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN (book,
                                            get_view,
                                            query,
                                            "s",
                                            view,
                                            "s")

E_DECLARE_GDBUS_SYNC_METHOD_1 (book,
                                 cancel_operation,
                                 opid,
                                 "u")
E_DECLARE_GDBUS_SYNC_METHOD_0 (book,
                               cancel_all)
E_DECLARE_GDBUS_SYNC_METHOD_0 (book,
                               close)

static const GDBusMethodInfo * const e_gdbus_book_method_info_pointers[] =
{
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, open),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, remove),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, refresh),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, get_contact),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, get_contact_list),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, get_contact_list_uids),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, add_contacts),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, remove_contacts),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, modify_contacts),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, get_backend_property),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, set_backend_property),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, get_view),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, cancel_operation),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, cancel_all),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book, close),
	NULL
};

static const GDBusSignalInfo * const e_gdbus_book_signal_info_pointers[] =
{
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, backend_error),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, readonly),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, online),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, opened),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, backend_property_changed),

	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, open_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, remove_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, refresh_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, get_contact_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, get_contact_list_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, get_contact_list_uids_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, add_contacts_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, remove_contacts_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, modify_contacts_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, get_backend_property_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, set_backend_property_done),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book, get_view_done),
	NULL
};

static const GDBusInterfaceInfo _e_gdbus_book_interface_info =
{
	-1,
	(gchar *) GDBUS_BOOK_INTERFACE_NAME,
	(GDBusMethodInfo **) &e_gdbus_book_method_info_pointers,
	(GDBusSignalInfo **) &e_gdbus_book_signal_info_pointers,
	(GDBusPropertyInfo **) NULL
};

static void
handle_method_call (GDBusConnection *connection,
                    const gchar *sender,
                    const gchar *object_path,
                    const gchar *interface_name,
                    const gchar *method_name,
                    GVariant *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer user_data)
{
	guint method_id, method_type;

	method_id = lookup_method_id_from_method_name (method_name);
	method_type = lookup_method_type_from_method_name (method_name);

	g_return_if_fail (method_id != 0);
	g_return_if_fail (method_type != 0);

	e_gdbus_stub_handle_method_call (user_data, invocation, parameters, method_name, signals[method_id], method_type);
}

static GVariant *
get_property (GDBusConnection *connection,
              const gchar *sender,
              const gchar *object_path,
              const gchar *interface_name,
              const gchar *property_name,
              GError **error,
              gpointer user_data)
{
	g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "This implementation does not support property `%s'", property_name);
	return NULL;
}

static gboolean
set_property (GDBusConnection *connection,
              const gchar *sender,
              const gchar *object_path,
              const gchar *interface_name,
              const gchar *property_name,
              GVariant *value,
              GError **error,
              gpointer user_data)
{
	g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "This implementation does not support property `%s'", property_name);
	return FALSE;
}

static const GDBusInterfaceVTable e_gdbus_book_interface_vtable =
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
		g_dbus_connection_emit_signal (
			connection, NULL, path, "org.freedesktop.DBus.Properties", "PropertiesChanged",
			g_variant_new ("(sa{sv}as)", GDBUS_BOOK_INTERFACE_NAME, builder, invalidated_builder),
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
 * e_gdbus_book_drain_notify:
 * @object: A #EGdbusBook that is exported.
 *
 * If @object has queued notifications, empty the queue forcing
 * the <literal>PropertiesChanged</literal> signal to be emitted.
 * See <xref linkend="EGdbusBook.description"/> for more background information.
 */
void
e_gdbus_book_drain_notify (EGdbusBook *object)
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
 * e_gdbus_book_register_object:
 * @object: An instance of a #GObject<!-- -->-derived type implementing the #EGdbusBook interface.
 * @connection: A #GDBusConnection.
 * @object_path: The object to register the object at.
 * @error: Return location for error or %NULL.
 *
 * Registers @object at @object_path on @connection.
 *
 * See <xref linkend="EGdbusBook.description"/>
 * for how properties, methods and signals are handled.
 *
 * Returns: 0 if @error is set, otherwise a registration id (never 0) that can be used with g_dbus_connection_unregister_object().
 */
guint
e_gdbus_book_register_object (EGdbusBook *object,
                              GDBusConnection *connection,
                              const gchar *object_path,
                              GError **error)
{
	GHashTable *pvc;

	pvc = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) g_variant_unref);

	g_object_set_data_full (G_OBJECT (object), "gdbus-codegen-path", (gpointer) g_strdup (object_path), g_free);
	g_object_set_data (G_OBJECT (object), "gdbus-codegen-connection", (gpointer) connection);
	g_object_set_data_full (G_OBJECT (object), "gdbus-codegen-pvc", (gpointer) pvc, (GDestroyNotify) g_hash_table_unref);

	return g_dbus_connection_register_object (
		connection, object_path, (GDBusInterfaceInfo *) &_e_gdbus_book_interface_info,
			&e_gdbus_book_interface_vtable, object, (GDestroyNotify) on_object_unregistered, error);
}

/**
 * e_gdbus_book_interface_info:
 *
 * Gets interface description for the <literal>org.gnome.evolution.dataserver.AddressBook</literal> D-Bus interface.
 *
 * Returns: A #GDBusInterfaceInfo. Do not free, the object is statically allocated.
 */
const GDBusInterfaceInfo *
e_gdbus_book_interface_info (void)
{
	return &_e_gdbus_book_interface_info;
}

/* ---------------------------------------------------------------------- */

static void proxy_iface_init (EGdbusBookIface *iface);
static void async_op_keeper_iface_init (EGdbusAsyncOpKeeperInterface *iface);

G_DEFINE_TYPE_WITH_CODE (EGdbusBookProxy, e_gdbus_book_proxy, G_TYPE_DBUS_PROXY,
                         G_IMPLEMENT_INTERFACE (E_TYPE_GDBUS_BOOK, proxy_iface_init)
			 G_IMPLEMENT_INTERFACE (E_TYPE_GDBUS_ASYNC_OP_KEEPER, async_op_keeper_iface_init));

static void
e_gdbus_book_proxy_init (EGdbusBookProxy *proxy)
{
	g_dbus_proxy_set_interface_info (G_DBUS_PROXY (proxy), (GDBusInterfaceInfo *) &_e_gdbus_book_interface_info);

	proxy->priv = E_GDBUS_BOOK_PROXY_GET_PRIVATE (proxy);
	proxy->priv->pending_ops = e_gdbus_async_op_keeper_create_pending_ops (E_GDBUS_ASYNC_OP_KEEPER (proxy));

	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID   (open);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID   (remove);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID   (refresh);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRING (get_contact);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRV   (get_contact_list);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRV   (get_contact_list_uids);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRV   (add_contacts);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID   (remove_contacts);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID   (modify_contacts);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRING (get_backend_property);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID	  (set_backend_property);
	E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRING (get_view);
}

static void
g_signal (GDBusProxy *proxy,
          const gchar *sender_name,
          const gchar *signal_name,
          GVariant *parameters)
{
	guint signal_id, signal_type;

	signal_id = lookup_signal_id_from_signal_name (signal_name);
	signal_type = lookup_signal_type_from_signal_name (signal_name);

	g_return_if_fail (signal_id != 0);
	g_return_if_fail (signal_type != 0);

	e_gdbus_proxy_emit_signal (proxy, parameters, signals[signal_id], signal_type);
}

static void
gdbus_book_proxy_finalize (GObject *object)
{
	EGdbusBookProxy *proxy = E_GDBUS_BOOK_PROXY (object);

	if (g_hash_table_size (proxy->priv->pending_ops))
		g_debug ("%s: Kept %d items in pending_ops", G_STRFUNC, g_hash_table_size (proxy->priv->pending_ops));

	g_hash_table_destroy (proxy->priv->pending_ops);

	G_OBJECT_CLASS (e_gdbus_book_proxy_parent_class)->finalize (object);
}

static void
e_gdbus_book_proxy_class_init (EGdbusBookProxyClass *class)
{
	GObjectClass *object_class;
	GDBusProxyClass *proxy_class;

	g_type_class_add_private (class, sizeof (EGdbusBookProxyPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = gdbus_book_proxy_finalize;

	proxy_class = G_DBUS_PROXY_CLASS (class);
	proxy_class->g_signal = g_signal;
}

static void
proxy_iface_init (EGdbusBookIface *iface)
{
}

static GHashTable *
gdbus_book_get_pending_ops (EGdbusAsyncOpKeeper *object)
{
	EGdbusBookProxy *proxy;

	g_return_val_if_fail (E_IS_GDBUS_BOOK_PROXY (object), NULL);

	proxy = E_GDBUS_BOOK_PROXY (object);

	return proxy->priv->pending_ops;
}

static gboolean
gdbus_book_call_cancel_operation_sync (EGdbusAsyncOpKeeper *object,
                                       guint in_opid,
                                       GCancellable *cancellable,
                                       GError **error)
{
	return e_gdbus_book_call_cancel_operation_sync (G_DBUS_PROXY (object), in_opid, cancellable, error);
}

static void
async_op_keeper_iface_init (EGdbusAsyncOpKeeperInterface *iface)
{
	g_return_if_fail (iface != NULL);

	iface->get_pending_ops = gdbus_book_get_pending_ops;
	iface->cancel_op_sync = gdbus_book_call_cancel_operation_sync;
}

/**
 * e_gdbus_book_proxy_new:
 * @connection: A #GDBusConnection.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique) or %NULL if @connection is not a message bus connection.
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Like g_dbus_proxy_new() but returns a #EGdbusBookProxy.
 *
 * This is a failable asynchronous constructor - when the proxy is ready, callback will be invoked and you can use e_gdbus_book_proxy_new_finish() to get the result.
 */
void
e_gdbus_book_proxy_new (GDBusConnection *connection,
                        GDBusProxyFlags flags,
                        const gchar *name,
                        const gchar *object_path,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	g_async_initable_new_async (
		E_TYPE_GDBUS_BOOK_PROXY,
		G_PRIORITY_DEFAULT,
		cancellable,
		callback,
		user_data,
		"g-flags", flags,
		"g-name", name,
		"g-connection", connection,
		"g-object-path", object_path,
		"g-interface-name", GDBUS_BOOK_INTERFACE_NAME,
		NULL);
}

/**
 * e_gdbus_book_proxy_new_finish:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to e_gdbus_book_proxy_new().
 * @error: Return location for error or %NULL.
 *
 * Finishes creating a #EGdbusBookProxy.
 *
 * Returns: A #EGdbusBookProxy or %NULL if @error is set. Free with g_object_unref().
 */
EGdbusBook *
e_gdbus_book_proxy_new_finish (GAsyncResult *result,
                               GError **error)
{
	GObject *object;
	GObject *source_object;
	source_object = g_async_result_get_source_object (result);
	g_assert (source_object != NULL);
	object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), result, error);
	g_object_unref (source_object);

	if (object != NULL)
		return E_GDBUS_BOOK (object);
	else
		return NULL;
}

/**
 * e_gdbus_book_proxy_new_sync:
 * @connection: A #GDBusConnection.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique) or %NULL if @connection is not a message bus connection.
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Like g_dbus_proxy_new_sync() but returns a #EGdbusBookProxy.
 *
 * This is a synchronous failable constructor. See e_gdbus_book_proxy_new() and e_gdbus_book_proxy_new_finish() for the asynchronous version.
 *
 * Returns: A #EGdbusBookProxy or %NULL if error is set. Free with g_object_unref().
 */
EGdbusBook *
e_gdbus_book_proxy_new_sync (GDBusConnection *connection,
                             GDBusProxyFlags flags,
                             const gchar *name,
                             const gchar *object_path,
                             GCancellable *cancellable,
                             GError **error)
{
	GInitable *initable;
	initable = g_initable_new (
		E_TYPE_GDBUS_BOOK_PROXY,
		cancellable,
		error,
		"g-flags", flags,
		"g-name", name,
		"g-connection", connection,
		"g-object-path", object_path,
		"g-interface-name", GDBUS_BOOK_INTERFACE_NAME,
		NULL);
	if (initable != NULL)
		return E_GDBUS_BOOK (initable);
	else
		return NULL;
}

/**
 * e_gdbus_book_proxy_new_for_bus:
 * @bus_type: A #GBusType.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique).
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Like g_dbus_proxy_new_for_bus() but returns a #EGdbusBookProxy.
 *
 * This is a failable asynchronous constructor - when the proxy is ready, callback will be invoked and you can use e_gdbus_book_proxy_new_for_bus_finish() to get the result.
 */
void
e_gdbus_book_proxy_new_for_bus (GBusType bus_type,
                                GDBusProxyFlags flags,
                                const gchar *name,
                                const gchar *object_path,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	g_async_initable_new_async (
		E_TYPE_GDBUS_BOOK_PROXY,
		G_PRIORITY_DEFAULT,
		cancellable,
		callback,
		user_data,
		"g-flags", flags,
		"g-name", name,
		"g-bus-type", bus_type,
		"g-object-path", object_path,
		"g-interface-name", GDBUS_BOOK_INTERFACE_NAME,
		NULL);
}

/**
 * e_gdbus_book_proxy_new_for_bus_finish:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to e_gdbus_book_proxy_new_for_bus().
 * @error: Return location for error or %NULL.
 *
 * Finishes creating a #EGdbusBookProxy.
 *
 * Returns: A #EGdbusBookProxy or %NULL if @error is set. Free with g_object_unref().
 */
EGdbusBook *
e_gdbus_book_proxy_new_for_bus_finish (GAsyncResult *result,
                                       GError **error)
{
	GObject *object;
	GObject *source_object;
	source_object = g_async_result_get_source_object (result);
	g_assert (source_object != NULL);
	object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), result, error);
	g_object_unref (source_object);

	if (object != NULL)
		return E_GDBUS_BOOK (object);
	else
		return NULL;
}

/**
 * e_gdbus_book_proxy_new_for_bus_sync:
 * @bus_type: A #GBusType.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique).
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Like g_dbus_proxy_new_for_bus_sync() but returns a #EGdbusBookProxy.
 *
 * This is a synchronous failable constructor. See e_gdbus_book_proxy_new_for_bus() and e_gdbus_book_proxy_new_for_bus_finish() for the asynchronous version.
 *
 * Returns: A #EGdbusBookProxy or %NULL if error is set. Free with g_object_unref().
 */
EGdbusBook *
e_gdbus_book_proxy_new_for_bus_sync (GBusType bus_type,
                                     GDBusProxyFlags flags,
                                     const gchar *name,
                                     const gchar *object_path,
                                     GCancellable *cancellable,
                                     GError **error)
{
	GInitable *initable;

	initable = g_initable_new (
		E_TYPE_GDBUS_BOOK_PROXY,
		cancellable,
		error,
		"g-flags", flags,
		"g-name", name,
		"g-bus-type", bus_type,
		"g-object-path", object_path,
		"g-interface-name", GDBUS_BOOK_INTERFACE_NAME,
		NULL);
	if (initable != NULL)
		return E_GDBUS_BOOK (initable);
	else
		return NULL;
}

/* ---------------------------------------------------------------------- */

static void stub_iface_init (EGdbusBookIface *iface);

G_DEFINE_TYPE_WITH_CODE (EGdbusBookStub, e_gdbus_book_stub, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (E_TYPE_GDBUS_BOOK, stub_iface_init));

static void
e_gdbus_book_stub_init (EGdbusBookStub *stub)
{
}

static void
e_gdbus_book_stub_class_init (EGdbusBookStubClass *class)
{
}

static void
stub_iface_init (EGdbusBookIface *iface)
{
}

/**
 * e_gdbus_book_stub_new:
 *
 * Creates a new stub object that can be exported via e_gdbus_book_register_object().
 *
 * Returns: A #EGdbusBookStub instance. Free with g_object_unref().
 */
EGdbusBook *
e_gdbus_book_stub_new (void)
{
	return E_GDBUS_BOOK (g_object_new (E_TYPE_GDBUS_BOOK_STUB, NULL));
}

/* Returns GDBus connection associated with the stub object */
GDBusConnection *
e_gdbus_book_stub_get_connection (EGdbusBook *stub)
{
	g_return_val_if_fail (stub != NULL, NULL);
	g_return_val_if_fail (E_IS_GDBUS_BOOK_STUB (stub), NULL);

	return g_object_get_data (G_OBJECT (stub), "gdbus-codegen-connection");
}

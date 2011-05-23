/*
 * e-gdbus-book.h
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

#ifndef E_GDBUS_BOOK_H
#define E_GDBUS_BOOK_H

#include <gio/gio.h>

#include <libedataserver/e-gdbus-templates.h>

G_BEGIN_DECLS

#define E_TYPE_GDBUS_BOOK         (e_gdbus_book_get_type ())
#define E_GDBUS_BOOK(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_GDBUS_BOOK, EGdbusBook))
#define E_IS_GDBUS_BOOK(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_GDBUS_BOOK))
#define E_GDBUS_BOOK_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE((o), E_TYPE_GDBUS_BOOK, EGdbusBookIface))

/**
 * EGdbusBook:
 *
 * Opaque type representing a proxy or an exported object.
 */
typedef struct _EGdbusBook EGdbusBook; /* Dummy typedef */

typedef struct _EGdbusBookIface EGdbusBookIface;

GType e_gdbus_book_get_type (void) G_GNUC_CONST;

/* ---------------------------------------------------------------------- */

#define E_TYPE_GDBUS_BOOK_PROXY         (e_gdbus_book_proxy_get_type ())
#define E_GDBUS_BOOK_PROXY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_GDBUS_BOOK_PROXY, EGdbusBookProxy))
#define E_IS_GDBUS_BOOK_PROXY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_GDBUS_BOOK_PROXY))

typedef struct _EGdbusBookProxy EGdbusBookProxy;
typedef struct _EGdbusBookProxyClass EGdbusBookProxyClass;
typedef struct _EGdbusBookProxyPrivate EGdbusBookProxyPrivate;

struct _EGdbusBookProxy
{
	GDBusProxy parent_instance;
	EGdbusBookProxyPrivate *priv;
};

struct _EGdbusBookProxyClass
{
	GDBusProxyClass parent_class;
};

GType e_gdbus_book_proxy_get_type (void) G_GNUC_CONST;

void e_gdbus_book_proxy_new (GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
EGdbusBook *e_gdbus_book_proxy_new_finish (GAsyncResult *result, GError **error);
EGdbusBook *e_gdbus_book_proxy_new_sync (GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error);

void e_gdbus_book_proxy_new_for_bus (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
EGdbusBook *e_gdbus_book_proxy_new_for_bus_finish (GAsyncResult *result, GError **error);
EGdbusBook *e_gdbus_book_proxy_new_for_bus_sync (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error);

/* ---------------------------------------------------------------------- */

typedef struct _EGdbusBookStub EGdbusBookStub;
typedef struct _EGdbusBookStubClass EGdbusBookStubClass;
typedef struct _EGdbusBookStubPrivate EGdbusBookStubPrivate;

struct _EGdbusBookStub
{
	GObject parent_instance;
	EGdbusBookStubPrivate *priv;
};

struct _EGdbusBookStubClass
{
	GObjectClass parent_class;
};

#define E_TYPE_GDBUS_BOOK_STUB	(e_gdbus_book_stub_get_type ())
#define E_GDBUS_BOOK_STUB(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_GDBUS_BOOK_STUB, EGdbusBookStub))
#define E_IS_GDBUS_BOOK_STUB(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_GDBUS_BOOK_STUB))

GType e_gdbus_book_stub_get_type (void) G_GNUC_CONST;

EGdbusBook *e_gdbus_book_stub_new (void);
GDBusConnection *e_gdbus_book_stub_get_connection (EGdbusBook *stub);

guint e_gdbus_book_register_object (EGdbusBook *object, GDBusConnection *connection, const gchar *object_path, GError **error);

void e_gdbus_book_drain_notify (EGdbusBook *object);

const GDBusInterfaceInfo *e_gdbus_book_interface_info (void) G_GNUC_CONST;

struct _EGdbusBookIface
{
	GTypeInterface parent_iface;

	/* Signal handlers for receiving D-Bus signals: */
	void	(*backend_error)		(EGdbusBook *object, const gchar *arg_message);
	void	(*readonly)			(EGdbusBook *object, gboolean arg_is_readonly);
	void	(*online)			(EGdbusBook *object, gboolean arg_is_online);
	void	(*auth_required)		(EGdbusBook *object, const gchar * const *arg_credentials);
	void	(*opened)			(EGdbusBook *object, const gchar * const *arg_error);

	/* Signal handlers for handling D-Bus method calls: */
	gboolean (*handle_open)			(EGdbusBook *object, GDBusMethodInvocation *invocation, gboolean in_only_if_exists);
	void	 (*open_done)			(EGdbusBook *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_remove)		(EGdbusBook *object, GDBusMethodInvocation *invocation);
	void	 (*remove_done)			(EGdbusBook *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_refresh)		(EGdbusBook *object, GDBusMethodInvocation *invocation);
	void	 (*refresh_done)		(EGdbusBook *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_get_contact)		(EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar *in_uid);
	void	 (*get_contact_done)		(EGdbusBook *object, guint arg_opid, const GError *arg_error, gchar **out_vcard);

	gboolean (*handle_get_contact_list)	(EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar *in_query);
	void	 (*get_contact_list_done)	(EGdbusBook *object, guint arg_opid, const GError *arg_error, gchar ***out_vcards);

	gboolean (*handle_add_contact)		(EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar *in_vcard);
	void	 (*add_contact_done)		(EGdbusBook *object, guint arg_opid, const GError *arg_error, gchar **out_uid);

	gboolean (*handle_remove_contacts)	(EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar * const *in_list);
	void	 (*remove_contacts_done)	(EGdbusBook *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_modify_contact)	(EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar *in_vcard);
	void	 (*modify_contact_done)		(EGdbusBook *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_get_backend_property)	(EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar *in_prop_name);
	void	 (*get_backend_property_done)	(EGdbusBook *object, guint arg_opid, const GError *arg_error, gchar **out_prop_value);

	gboolean (*handle_set_backend_property)	(EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar *in_prop_name_value);
	void	 (*set_backend_property_done)	(EGdbusBook *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_get_view)		(EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar *in_query);
	void	 (*get_view_done)		(EGdbusBook *object, guint arg_opid, const GError *arg_error, gchar **out_view);

	gboolean (*handle_authenticate_user)	(EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar * const *in_credentials);
	gboolean (*handle_cancel_operation)	(EGdbusBook *object, GDBusMethodInvocation *invocation, guint in_opid);
	gboolean (*handle_cancel_all)		(EGdbusBook *object, GDBusMethodInvocation *invocation);
	gboolean (*handle_close)		(EGdbusBook *object, GDBusMethodInvocation *invocation);

};

/* C Bindings for properties */

/* D-Bus Methods */
void		e_gdbus_book_call_open (GDBusProxy *proxy, gboolean in_only_if_exists, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_open_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_book_call_open_sync (GDBusProxy *proxy, gboolean in_only_if_exists, GCancellable *cancellable, GError **error);

void		e_gdbus_book_call_remove (GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_remove_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_book_call_remove_sync (GDBusProxy *proxy, GCancellable *cancellable, GError **error);

void		e_gdbus_book_call_refresh (GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_refresh_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_book_call_refresh_sync (GDBusProxy *proxy, GCancellable *cancellable, GError **error);

void		e_gdbus_book_call_get_contact (GDBusProxy *proxy, const gchar *in_uid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_get_contact_finish (GDBusProxy *proxy, GAsyncResult *result, gchar **out_vcard, GError **error);
gboolean	e_gdbus_book_call_get_contact_sync (GDBusProxy *proxy, const gchar *in_uid, gchar **out_vcard, GCancellable *cancellable, GError **error);

void		e_gdbus_book_call_get_contact_list (GDBusProxy *proxy, const gchar *in_query, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_get_contact_list_finish (GDBusProxy *proxy, GAsyncResult *result, gchar ***out_vcards, GError **error);
gboolean	e_gdbus_book_call_get_contact_list_sync (GDBusProxy *proxy, const gchar *in_query, gchar ***out_vcards, GCancellable *cancellable, GError **error);

void		e_gdbus_book_call_add_contact (GDBusProxy *proxy, const gchar *in_vcard, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_add_contact_finish (GDBusProxy *proxy, GAsyncResult *result, gchar **out_uid, GError **error);
gboolean	e_gdbus_book_call_add_contact_sync (GDBusProxy *proxy, const gchar *in_vcard, gchar **out_uid, GCancellable *cancellable, GError **error);

void		e_gdbus_book_call_remove_contacts (GDBusProxy *proxy, const gchar * const *in_list, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_remove_contacts_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_book_call_remove_contacts_sync (GDBusProxy *proxy, const gchar * const *in_list, GCancellable *cancellable, GError **error);

void		e_gdbus_book_call_modify_contact (GDBusProxy *proxy, const gchar *in_vcard, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_modify_contact_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_book_call_modify_contact_sync (GDBusProxy *proxy, const gchar *in_vcard, GCancellable *cancellable, GError **error);

void		e_gdbus_book_call_get_backend_property (GDBusProxy *proxy, const gchar *in_prop_name, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_get_backend_property_finish (GDBusProxy *proxy, GAsyncResult *result, gchar **out_prop_value, GError **error);
gboolean	e_gdbus_book_call_get_backend_property_sync (GDBusProxy *proxy, const gchar *prop_name, gchar **out_prop_value, GCancellable *cancellable, GError **error);

gchar **	e_gdbus_book_encode_set_backend_property (const gchar *in_prop_name, const gchar *in_prop_value);
gboolean	e_gdbus_book_decode_set_backend_property (const gchar * const *in_strv, gchar **out_prop_name, gchar **out_prop_value);
void		e_gdbus_book_call_set_backend_property (GDBusProxy *proxy, const gchar * const *in_prop_name_value, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_set_backend_property_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_book_call_set_backend_property_sync (GDBusProxy *proxy, const gchar * const *prop_name_value, GCancellable *cancellable, GError **error);

void		e_gdbus_book_call_get_view (GDBusProxy *proxy, const gchar *in_query, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_get_view_finish (GDBusProxy *proxy, GAsyncResult *result, gchar **out_view_path, GError **error);
gboolean	e_gdbus_book_call_get_view_sync (GDBusProxy *proxy, const gchar *in_query, gchar **out_view_path, GCancellable *cancellable, GError **error);

void		e_gdbus_book_call_authenticate_user (GDBusProxy *proxy, const gchar * const *in_credentials, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_authenticate_user_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_book_call_authenticate_user_sync (GDBusProxy *proxy, const gchar * const *in_credentials, GCancellable *cancellable, GError **error);

void		e_gdbus_book_call_cancel_operation (GDBusProxy *proxy, guint in_opid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_cancel_operation_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_book_call_cancel_operation_sync (GDBusProxy *proxy, guint in_opid, GCancellable *cancellable, GError **error);

void		e_gdbus_book_call_cancel_all (GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_cancel_all_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_book_call_cancel_all_sync (GDBusProxy *proxy, GCancellable *cancellable, GError **error);

void		e_gdbus_book_call_close (GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_call_close_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_book_call_close_sync (GDBusProxy *proxy, GCancellable *cancellable, GError **error);

/* D-Bus Methods Completion Helpers */
#define e_gdbus_book_complete_open				e_gdbus_complete_async_method
#define e_gdbus_book_complete_remove				e_gdbus_complete_async_method
#define e_gdbus_book_complete_refresh				e_gdbus_complete_async_method
#define e_gdbus_book_complete_get_contact			e_gdbus_complete_async_method
#define e_gdbus_book_complete_get_contact_list			e_gdbus_complete_async_method
#define e_gdbus_book_complete_add_contact			e_gdbus_complete_async_method
#define e_gdbus_book_complete_remove_contacts			e_gdbus_complete_async_method
#define e_gdbus_book_complete_modify_contact			e_gdbus_complete_async_method
#define e_gdbus_book_complete_get_backend_property		e_gdbus_complete_async_method
#define e_gdbus_book_complete_set_backend_property		e_gdbus_complete_async_method
#define e_gdbus_book_complete_get_view				e_gdbus_complete_async_method
#define e_gdbus_book_complete_authenticate_user			e_gdbus_complete_sync_method_void
#define e_gdbus_book_complete_cancel_operation			e_gdbus_complete_sync_method_void
#define e_gdbus_book_complete_cancel_all			e_gdbus_complete_sync_method_void
#define e_gdbus_book_complete_close				e_gdbus_complete_sync_method_void

void e_gdbus_book_emit_open_done			(EGdbusBook *object, guint arg_opid, const GError *arg_error);
void e_gdbus_book_emit_remove_done			(EGdbusBook *object, guint arg_opid, const GError *arg_error);
void e_gdbus_book_emit_refresh_done			(EGdbusBook *object, guint arg_opid, const GError *arg_error);
void e_gdbus_book_emit_get_contact_done			(EGdbusBook *object, guint arg_opid, const GError *arg_error, const gchar *out_vcard);
void e_gdbus_book_emit_get_contact_list_done		(EGdbusBook *object, guint arg_opid, const GError *arg_error, const gchar * const *out_vcards);
void e_gdbus_book_emit_add_contact_done			(EGdbusBook *object, guint arg_opid, const GError *arg_error, const gchar *out_uid);
void e_gdbus_book_emit_remove_contacts_done		(EGdbusBook *object, guint arg_opid, const GError *arg_error);
void e_gdbus_book_emit_modify_contact_done		(EGdbusBook *object, guint arg_opid, const GError *arg_error);
void e_gdbus_book_emit_get_backend_property_done	(EGdbusBook *object, guint arg_opid, const GError *arg_error, const gchar *out_prop_value);
void e_gdbus_book_emit_set_backend_property_done	(EGdbusBook *object, guint arg_opid, const GError *arg_error);
void e_gdbus_book_emit_get_view_done			(EGdbusBook *object, guint arg_opid, const GError *arg_error, const gchar *out_view);

/* D-Bus Signal Emission Helpers */
void e_gdbus_book_emit_backend_error	(EGdbusBook *object, const gchar *arg_message);
void e_gdbus_book_emit_readonly		(EGdbusBook *object, gboolean arg_is_readonly);
void e_gdbus_book_emit_online		(EGdbusBook *object, gboolean arg_is_online);
void e_gdbus_book_emit_auth_required	(EGdbusBook *object, const gchar * const *arg_credentials);
void e_gdbus_book_emit_opened		(EGdbusBook *object, const gchar * const *arg_error);

G_END_DECLS

#endif /* E_GDBUS_BOOK_H */

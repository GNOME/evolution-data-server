/*
 * e-gdbus-cal.h
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

#ifndef E_GDBUS_CAL_H
#define E_GDBUS_CAL_H

#include <gio/gio.h>

#include <libedataserver/e-gdbus-templates.h>

G_BEGIN_DECLS

#define E_TYPE_GDBUS_CAL	(e_gdbus_cal_get_type ())
#define E_GDBUS_CAL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_GDBUS_CAL, EGdbusCal))
#define E_IS_GDBUS_CAL(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_GDBUS_CAL))
#define E_GDBUS_CAL_GET_IFACE(o)(G_TYPE_INSTANCE_GET_INTERFACE((o), E_TYPE_GDBUS_CAL, EGdbusCalIface))

typedef struct _EGdbusCal EGdbusCal; /* Dummy typedef */
typedef struct _EGdbusCalIface EGdbusCalIface;

GType e_gdbus_cal_get_type (void) G_GNUC_CONST;

/* ---------------------------------------------------------------------- */

typedef struct _EGdbusCalProxy EGdbusCalProxy;
typedef struct _EGdbusCalProxyClass EGdbusCalProxyClass;
typedef struct _EGdbusCalProxyPrivate EGdbusCalProxyPrivate;

struct _EGdbusCalProxy
{
	GDBusProxy parent_instance;
	EGdbusCalProxyPrivate *priv;
};

struct _EGdbusCalProxyClass
{
	GDBusProxyClass parent_class;
};

#define E_TYPE_GDBUS_CAL_PROXY	(e_gdbus_cal_proxy_get_type ())
#define E_GDBUS_CAL_PROXY(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_GDBUS_CAL_PROXY, EGdbusCalProxy))
#define E_IS_GDBUS_CAL_PROXY(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_GDBUS_CAL_PROXY))
GType e_gdbus_cal_proxy_get_type (void) G_GNUC_CONST;

void		e_gdbus_cal_proxy_new		(GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
EGdbusCal *	e_gdbus_cal_proxy_new_finish	(GAsyncResult  *result, GError **error);
EGdbusCal *	e_gdbus_cal_proxy_new_sync	(GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_proxy_new_for_bus		(GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
EGdbusCal *	e_gdbus_cal_proxy_new_for_bus_finish	(GAsyncResult *result, GError **error);
EGdbusCal *	e_gdbus_cal_proxy_new_for_bus_sync	(GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error);

/* ---------------------------------------------------------------------- */

typedef struct _EGdbusCalStub EGdbusCalStub;
typedef struct _EGdbusCalStubClass EGdbusCalStubClass;
typedef struct _EGdbusCalStubPrivate EGdbusCalStubPrivate;

struct _EGdbusCalStub
{
	GObject parent_instance;
	EGdbusCalStubPrivate *priv;
};

struct _EGdbusCalStubClass
{
	GObjectClass parent_class;
};

#define E_TYPE_GDBUS_CAL_STUB	(e_gdbus_cal_stub_get_type ())
#define E_GDBUS_CAL_STUB(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_GDBUS_CAL_STUB, EGdbusCalStub))
#define E_IS_GDBUS_CAL_STUB(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_GDBUS_CAL_STUB))
GType e_gdbus_cal_stub_get_type (void) G_GNUC_CONST;

EGdbusCal *e_gdbus_cal_stub_new (void);
GDBusConnection *e_gdbus_cal_stub_get_connection (EGdbusCal *stub);

guint e_gdbus_cal_register_object (EGdbusCal *object, GDBusConnection *connection, const gchar *object_path, GError **error);
void e_gdbus_cal_drain_notify (EGdbusCal *object);

const GDBusInterfaceInfo *e_gdbus_cal_interface_info (void) G_GNUC_CONST;

struct _EGdbusCalIface
{
	GTypeInterface parent_iface;

	/* Signal handlers for receiving D-Bus signals: */
	void	(*backend_error)			(EGdbusCal *object, const gchar *arg_message);
	void	(*readonly)				(EGdbusCal *object, gboolean arg_is_readonly);
	void	(*online)				(EGdbusCal *object, gboolean arg_is_online);
	void	(*auth_required)			(EGdbusCal *object, const gchar * const *arg_credentials);
	void	(*opened)				(EGdbusCal *object, const gchar * const *arg_error);
	void	(*free_busy_data)			(EGdbusCal *object, const gchar * const *arg_free_busy);

	/* Signal handlers for handling D-Bus method calls: */
	gboolean (*handle_open)				(EGdbusCal *object, GDBusMethodInvocation *invocation, gboolean in_only_if_exists);
	void	 (*open_done)				(EGdbusCal *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_remove)			(EGdbusCal *object, GDBusMethodInvocation *invocation);
	void	 (*remove_done)				(EGdbusCal *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_refresh)			(EGdbusCal *object, GDBusMethodInvocation *invocation);
	void	 (*refresh_done)			(EGdbusCal *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_get_backend_property)		(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *in_prop_name);
	void	 (*get_backend_property_done)		(EGdbusCal *object, guint arg_opid, const GError *arg_error, gchar **out_prop_value);

	gboolean (*handle_set_backend_property)		(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar * const *in_prop_name_value);
	void	 (*set_backend_property_done)		(EGdbusCal *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_get_object)			(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar * const *in_uid_rid);
	void	 (*get_object_done)			(EGdbusCal *object, guint arg_opid, const GError *arg_error, gchar **out_object);

	gboolean (*handle_get_object_list)		(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *in_sexp);
	void	 (*get_object_list_done)		(EGdbusCal *object, guint arg_opid, const GError *arg_error, gchar ***out_objects);

	gboolean (*handle_get_free_busy)		(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar * const *in_start_end_userlist);
	void	 (*get_free_busy_done)			(EGdbusCal *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_create_object)		(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *in_calobj);
	void	 (*create_object_done)			(EGdbusCal *object, guint arg_opid, const GError *arg_error, gchar **out_uid);

	gboolean (*handle_modify_object)		(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar * const *in_calobj_mod);
	void	 (*modify_object_done)			(EGdbusCal *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_remove_object)		(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar * const *in_uid_rid_mod);
	void	 (*remove_object_done)			(EGdbusCal *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_receive_objects)		(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *in_calobj);
	void	 (*receive_objects_done)		(EGdbusCal *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_send_objects)			(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *in_calobj);
	void	 (*send_objects_done)			(EGdbusCal *object, guint arg_opid, const GError *arg_error, gchar ***out_calobj_users);

	gboolean (*handle_get_attachment_uris)		(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar * const *in_uid_rid);
	void	 (*get_attachment_uris_done)		(EGdbusCal *object, guint arg_opid, const GError *arg_error, gchar ***out_attachments);

	gboolean (*handle_discard_alarm)		(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar * const *in_uid_rid_auid);
	void	 (*discard_alarm_done)			(EGdbusCal *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_get_view)			(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *in_sexp);
	void	 (*get_view_done)			(EGdbusCal *object, guint arg_opid, const GError *arg_error, gchar **out_view_path);

	gboolean (*handle_get_timezone)			(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *in_tzid);
	void	 (*get_timezone_done)			(EGdbusCal *object, guint arg_opid, const GError *arg_error, gchar **out_tzobject);

	gboolean (*handle_add_timezone)			(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar *in_tzobject);
	void	 (*add_timezone_done)			(EGdbusCal *object, guint arg_opid, const GError *arg_error);

	gboolean (*handle_authenticate_user)		(EGdbusCal *object, GDBusMethodInvocation *invocation, const gchar * const *in_credentials);
	gboolean (*handle_cancel_operation)		(EGdbusCal *object, GDBusMethodInvocation *invocation, guint in_opid);
	gboolean (*handle_cancel_all)			(EGdbusCal *object, GDBusMethodInvocation *invocation);
	gboolean (*handle_close)			(EGdbusCal *object, GDBusMethodInvocation *invocation);
};

/* D-Bus Methods */
void		e_gdbus_cal_call_open				(GDBusProxy *proxy, gboolean in_only_if_exists, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_open_finish			(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_call_open_sync			(GDBusProxy *proxy, gboolean in_only_if_exists, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_call_remove				(GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_remove_finish			(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_call_remove_sync			(GDBusProxy *proxy, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_call_refresh			(GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_refresh_finish			(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_call_refresh_sync			(GDBusProxy *proxy, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_call_get_backend_property		(GDBusProxy *proxy, const gchar *in_prop_name, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_get_backend_property_finish	(GDBusProxy *proxy, GAsyncResult *result, gchar **out_prop_value, GError **error);
gboolean	e_gdbus_cal_call_get_backend_property_sync	(GDBusProxy *proxy, const gchar *in_prop_name, gchar **out_prop_value, GCancellable *cancellable, GError **error);

gchar **	e_gdbus_cal_encode_set_backend_property		(const gchar *in_prop_name, const gchar *in_prop_value);
gboolean	e_gdbus_cal_decode_set_backend_property		(const gchar * const *in_strv, gchar **out_prop_name, gchar **out_prop_value);
void		e_gdbus_cal_call_set_backend_property		(GDBusProxy *proxy, const gchar * const *in_prop_name_value, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_set_backend_property_finish	(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_call_set_backend_property_sync	(GDBusProxy *proxy, const gchar * const *in_prop_name_value, GCancellable *cancellable, GError **error);

gchar **	e_gdbus_cal_encode_get_object			(const gchar *in_uid, const gchar *in_rid);
gboolean	e_gdbus_cal_decode_get_object			(const gchar * const *in_strv, gchar **out_uid, gchar **out_rid);
void		e_gdbus_cal_call_get_object			(GDBusProxy *proxy, const gchar * const *in_uid_rid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_get_object_finish		(GDBusProxy *proxy, GAsyncResult *result, gchar **out_object, GError **error);
gboolean	e_gdbus_cal_call_get_object_sync		(GDBusProxy *proxy, const gchar * const *in_uid_rid, gchar **out_object, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_call_get_object_list		(GDBusProxy *proxy, const gchar *in_sexp, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_get_object_list_finish		(GDBusProxy *proxy, GAsyncResult *result, gchar ***out_objects, GError **error);
gboolean	e_gdbus_cal_call_get_object_list_sync		(GDBusProxy *proxy, const gchar *in_sexp, gchar ***out_objects, GCancellable *cancellable, GError **error);

gchar **	e_gdbus_cal_encode_get_free_busy		(guint in_start, guint in_end, const GSList *in_users);
gboolean	e_gdbus_cal_decode_get_free_busy		(const gchar * const *in_strv, guint *out_start, guint *out_end, GSList **out_users);
void		e_gdbus_cal_call_get_free_busy			(GDBusProxy *proxy, const gchar * const *in_start_end_userlist, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_get_free_busy_finish		(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_call_get_free_busy_sync		(GDBusProxy *proxy, const gchar * const *in_start_end_userlist, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_call_create_object			(GDBusProxy *proxy, const gchar *in_calobj, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_create_object_finish		(GDBusProxy *proxy, GAsyncResult *result, gchar **out_uid, GError **error);
gboolean	e_gdbus_cal_call_create_object_sync		(GDBusProxy *proxy, const gchar *in_calobj, gchar **out_uid, GCancellable *cancellable, GError **error);

gchar **	e_gdbus_cal_encode_modify_object		(const gchar *in_calobj, guint in_mod);
gboolean	e_gdbus_cal_decode_modify_object		(const gchar * const *in_strv, gchar **out_calobj, guint *out_mod);
void		e_gdbus_cal_call_modify_object			(GDBusProxy *proxy, const gchar * const *in_calobj_mod, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_modify_object_finish		(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_call_modify_object_sync		(GDBusProxy *proxy, const gchar * const *in_calobj_mod, GCancellable *cancellable, GError **error);

gchar **	e_gdbus_cal_encode_remove_object		(const gchar *in_uid, const gchar *in_rid, guint in_mod);
gboolean	e_gdbus_cal_decode_remove_object		(const gchar * const *in_strv, gchar **out_uid, gchar **out_rid, guint *out_mod);
void		e_gdbus_cal_call_remove_object			(GDBusProxy *proxy, const gchar * const *in_uid_rid_mod, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_remove_object_finish		(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_call_remove_object_sync		(GDBusProxy *proxy, const gchar * const *in_uid_rid_mod, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_call_receive_objects		(GDBusProxy *proxy, const gchar *in_calobj, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_receive_objects_finish		(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_call_receive_objects_sync		(GDBusProxy *proxy, const gchar *in_calobj, GCancellable *cancellable, GError **error);

gchar **	e_gdbus_cal_encode_send_objects			(const gchar *in_calobj, const GSList *in_users);
gboolean	e_gdbus_cal_decode_send_objects			(const gchar * const *in_strv, gchar **out_calobj, GSList **out_users);
void		e_gdbus_cal_call_send_objects			(GDBusProxy *proxy, const gchar *in_calobj, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_send_objects_finish		(GDBusProxy *proxy, GAsyncResult *result, gchar ***out_calobj_users, GError **error);
gboolean	e_gdbus_cal_call_send_objects_sync		(GDBusProxy *proxy, const gchar *in_calobj, gchar ***out_calobj_users, GCancellable *cancellable, GError **error);

gchar **	e_gdbus_cal_encode_get_attachment_uris		(const gchar *in_uid, const gchar *in_rid);
gboolean	e_gdbus_cal_decode_get_attachment_uris		(const gchar * const *in_strv, gchar **out_uid, gchar **out_rid);
void		e_gdbus_cal_call_get_attachment_uris		(GDBusProxy *proxy, const gchar * const *in_uid_rid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_get_attachment_uris_finish	(GDBusProxy *proxy, GAsyncResult *result, gchar ***out_attachments, GError **error);
gboolean	e_gdbus_cal_call_get_attachment_uris_sync	(GDBusProxy *proxy, const gchar * const *in_uid_rid, gchar ***out_attachments, GCancellable *cancellable, GError **error);

gchar **	e_gdbus_cal_encode_discard_alarm		(const gchar *in_uid, const gchar *in_rid, const gchar *in_auid);
gboolean	e_gdbus_cal_decode_discard_alarm		(const gchar * const *in_strv, gchar **out_uid, gchar **out_rid, gchar **out_auid);
void		e_gdbus_cal_call_discard_alarm			(GDBusProxy *proxy, const gchar * const *in_uid_rid_auid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_discard_alarm_finish		(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_call_discard_alarm_sync		(GDBusProxy *proxy, const gchar * const *in_uid_rid_auid, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_call_get_view			(GDBusProxy *proxy, const gchar *in_sexp, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_get_view_finish		(GDBusProxy *proxy, GAsyncResult *result, gchar **out_view_path, GError **error);
gboolean	e_gdbus_cal_call_get_view_sync			(GDBusProxy *proxy, const gchar *in_sexp, gchar **out_view_path, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_call_get_timezone			(GDBusProxy *proxy, const gchar *in_tzid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_get_timezone_finish		(GDBusProxy *proxy, GAsyncResult *result, gchar **out_tzobject, GError **error);
gboolean	e_gdbus_cal_call_get_timezone_sync		(GDBusProxy *proxy, const gchar *in_tzid, gchar **out_tzobject, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_call_add_timezone			(GDBusProxy *proxy, const gchar *in_tzobject, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_add_timezone_finish		(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_call_add_timezone_sync		(GDBusProxy *proxy, const gchar *in_tzobject, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_call_authenticate_user		(GDBusProxy *proxy, const gchar * const *in_credentials, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_authenticate_user_finish	(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_call_authenticate_user_sync		(GDBusProxy *proxy, const gchar * const *in_credentials, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_call_cancel_operation		(GDBusProxy *proxy, guint in_opid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_cancel_operation_finish	(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_call_cancel_operation_sync		(GDBusProxy *proxy, guint in_opid, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_call_cancel_all			(GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_cancel_all_finish		(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_call_cancel_all_sync		(GDBusProxy *proxy, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_call_close				(GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_call_close_finish			(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_call_close_sync			(GDBusProxy *proxy, GCancellable *cancellable, GError **error);

/* D-Bus Methods Completion Helpers */
#define e_gdbus_cal_complete_open			e_gdbus_complete_async_method
#define e_gdbus_cal_complete_remove			e_gdbus_complete_async_method
#define e_gdbus_cal_complete_refresh			e_gdbus_complete_async_method
#define e_gdbus_cal_complete_get_backend_property	e_gdbus_complete_async_method
#define e_gdbus_cal_complete_set_backend_property	e_gdbus_complete_async_method
#define e_gdbus_cal_complete_get_object			e_gdbus_complete_async_method
#define e_gdbus_cal_complete_get_object_list		e_gdbus_complete_async_method
#define e_gdbus_cal_complete_get_free_busy		e_gdbus_complete_async_method
#define e_gdbus_cal_complete_create_object		e_gdbus_complete_async_method
#define e_gdbus_cal_complete_modify_object		e_gdbus_complete_async_method
#define e_gdbus_cal_complete_remove_object		e_gdbus_complete_async_method
#define e_gdbus_cal_complete_receive_objects		e_gdbus_complete_async_method
#define e_gdbus_cal_complete_send_objects		e_gdbus_complete_async_method
#define e_gdbus_cal_complete_get_attachment_uris	e_gdbus_complete_async_method
#define e_gdbus_cal_complete_discard_alarm		e_gdbus_complete_async_method
#define e_gdbus_cal_complete_get_view			e_gdbus_complete_async_method
#define e_gdbus_cal_complete_get_timezone		e_gdbus_complete_async_method
#define e_gdbus_cal_complete_add_timezone		e_gdbus_complete_async_method
#define e_gdbus_cal_complete_authenticate_user		e_gdbus_complete_sync_method_void
#define e_gdbus_cal_complete_cancel_operation		e_gdbus_complete_sync_method_void
#define e_gdbus_cal_complete_cancel_all			e_gdbus_complete_sync_method_void
#define e_gdbus_cal_complete_close			e_gdbus_complete_sync_method_void

void e_gdbus_cal_emit_open_done				(EGdbusCal *object, guint arg_opid, const GError *arg_error);
void e_gdbus_cal_emit_remove_done			(EGdbusCal *object, guint arg_opid, const GError *arg_error);
void e_gdbus_cal_emit_refresh_done			(EGdbusCal *object, guint arg_opid, const GError *arg_error);
void e_gdbus_cal_emit_get_backend_property_done		(EGdbusCal *object, guint arg_opid, const GError *arg_error, const gchar *out_prop_value);
void e_gdbus_cal_emit_set_backend_property_done		(EGdbusCal *object, guint arg_opid, const GError *arg_error);
void e_gdbus_cal_emit_get_object_done			(EGdbusCal *object, guint arg_opid, const GError *arg_error, const gchar *out_object);
void e_gdbus_cal_emit_get_object_list_done		(EGdbusCal *object, guint arg_opid, const GError *arg_error, const gchar * const *out_objects);
void e_gdbus_cal_emit_get_free_busy_done		(EGdbusCal *object, guint arg_opid, const GError *arg_error);
void e_gdbus_cal_emit_get_free_busy_data		(EGdbusCal *object, guint arg_opid, const GError *arg_error, const gchar * const *out_freebusy);
void e_gdbus_cal_emit_create_object_done		(EGdbusCal *object, guint arg_opid, const GError *arg_error, const gchar *out_uid);
void e_gdbus_cal_emit_modify_object_done		(EGdbusCal *object, guint arg_opid, const GError *arg_error);
void e_gdbus_cal_emit_remove_object_done		(EGdbusCal *object, guint arg_opid, const GError *arg_error);
void e_gdbus_cal_emit_receive_objects_done		(EGdbusCal *object, guint arg_opid, const GError *arg_error);
void e_gdbus_cal_emit_send_objects_done			(EGdbusCal *object, guint arg_opid, const GError *arg_error, const gchar * const *out_calobj_users);
void e_gdbus_cal_emit_get_attachment_uris_done		(EGdbusCal *object, guint arg_opid, const GError *arg_error, const gchar * const *out_attachments);
void e_gdbus_cal_emit_discard_alarm_done		(EGdbusCal *object, guint arg_opid, const GError *arg_error);
void e_gdbus_cal_emit_get_view_done			(EGdbusCal *object, guint arg_opid, const GError *arg_error, const gchar *out_view_path);
void e_gdbus_cal_emit_get_timezone_done			(EGdbusCal *object, guint arg_opid, const GError *arg_error, const gchar *out_object);
void e_gdbus_cal_emit_add_timezone_done			(EGdbusCal *object, guint arg_opid, const GError *arg_error);

/* D-Bus Signal Emission Helpers */
void e_gdbus_cal_emit_backend_error	(EGdbusCal *object, const gchar *arg_message);
void e_gdbus_cal_emit_readonly		(EGdbusCal *object, gboolean arg_is_readonly);
void e_gdbus_cal_emit_online		(EGdbusCal *object, gint arg_is_online);
void e_gdbus_cal_emit_auth_required	(EGdbusCal *object, const gchar * const *arg_credentials);
void e_gdbus_cal_emit_opened		(EGdbusCal *object, const gchar * const *arg_error);
void e_gdbus_cal_emit_free_busy_data	(EGdbusCal *object, const gchar * const *arg_free_busy);

G_END_DECLS

#endif /* E_GDBUS_CAL_H */

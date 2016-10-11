/*
 * e-gdbus-cal-view.h
 *
 * Copyright (C) 2011 Red Hat, Inc. (www.redhat.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef E_GDBUS_CAL_VIEW_H
#define E_GDBUS_CAL_VIEW_H

#include <libedataserver/libedataserver.h>

G_BEGIN_DECLS

#define E_TYPE_GDBUS_CAL_VIEW         (e_gdbus_cal_view_get_type ())
#define E_GDBUS_CAL_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_GDBUS_CAL_VIEW, EGdbusCalView))
#define E_IS_GDBUS_CAL_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_GDBUS_CAL_VIEW))
#define E_GDBUS_CAL_VIEW_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE((o), E_TYPE_GDBUS_CAL_VIEW, EGdbusCalViewIface))

typedef struct _EGdbusCalView EGdbusCalView; /* Dummy typedef */
typedef struct _EGdbusCalViewIface EGdbusCalViewIface;

GType e_gdbus_cal_view_get_type (void) G_GNUC_CONST;

/* ---------------------------------------------------------------------- */

typedef struct _EGdbusCalViewProxy EGdbusCalViewProxy;
typedef struct _EGdbusCalViewProxyClass EGdbusCalViewProxyClass;
typedef struct _EGdbusCalViewProxyPrivate EGdbusCalViewProxyPrivate;

struct _EGdbusCalViewProxy
{
	GDBusProxy parent_instance;
	EGdbusCalViewProxyPrivate *priv;
};

struct _EGdbusCalViewProxyClass
{
	GDBusProxyClass parent_class;
};

#define E_TYPE_GDBUS_CAL_VIEW_PROXY (e_gdbus_cal_view_proxy_get_type ())
GType e_gdbus_cal_view_proxy_get_type (void) G_GNUC_CONST;

void		e_gdbus_cal_view_proxy_new (GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
EGdbusCalView *	e_gdbus_cal_view_proxy_new_finish (GAsyncResult *result, GError **error);
EGdbusCalView *	e_gdbus_cal_view_proxy_new_sync (GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_view_proxy_new_for_bus (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
EGdbusCalView *	e_gdbus_cal_view_proxy_new_for_bus_finish (GAsyncResult *result, GError **error);
EGdbusCalView *	e_gdbus_cal_view_proxy_new_for_bus_sync (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error);

/* ---------------------------------------------------------------------- */

typedef struct _EGdbusCalViewStub EGdbusCalViewStub;
typedef struct _EGdbusCalViewStubClass EGdbusCalViewStubClass;
typedef struct _EGdbusCalViewStubPrivate EGdbusCalViewStubPrivate;

struct _EGdbusCalViewStub
{
	GObject parent_instance;
	EGdbusCalViewStubPrivate *priv;
};

struct _EGdbusCalViewStubClass
{
	GObjectClass parent_class;
};

#define E_TYPE_GDBUS_CAL_VIEW_STUB (e_gdbus_cal_view_stub_get_type ())
GType e_gdbus_cal_view_stub_get_type (void) G_GNUC_CONST;

EGdbusCalView *e_gdbus_cal_view_stub_new (void);
guint e_gdbus_cal_view_register_object (EGdbusCalView *object, GDBusConnection *connection, const gchar *object_path, GError **error);
void e_gdbus_cal_view_drain_notify (EGdbusCalView *object);

const GDBusInterfaceInfo *e_gdbus_cal_view_interface_info (void) G_GNUC_CONST;

struct _EGdbusCalViewIface
{
	GTypeInterface parent_iface;

	/* Signal handlers for receiving D-Bus signals: */
	void	(*objects_added)	(EGdbusCalView *object, const gchar * const *arg_objects);
	void	(*objects_modified)	(EGdbusCalView *object, const gchar * const *arg_objects);
	void	(*objects_removed)	(EGdbusCalView *object, const gchar * const *arg_uids);

	void	(*progress)		(EGdbusCalView *object, guint arg_percent, const gchar *arg_message);
	void	(*complete)		(EGdbusCalView *object, guint arg_status, const gchar *arg_message);

	/* Signal handlers for handling D-Bus method calls: */
	gboolean (*handle_start)		(EGdbusCalView *object, GDBusMethodInvocation *invocation);
	gboolean (*handle_stop)			(EGdbusCalView *object, GDBusMethodInvocation *invocation);
	gboolean (*handle_set_flags)		(EGdbusCalView *object, GDBusMethodInvocation *invocation, guint in_flags);
	gboolean (*handle_dispose)		(EGdbusCalView *object, GDBusMethodInvocation *invocation);
	gboolean (*handle_set_fields_of_interest)(EGdbusCalView *object, GDBusMethodInvocation *invocation, const gchar * const *in_only_fields);
};

/* D-Bus Methods */
void		e_gdbus_cal_view_call_start		(GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_view_call_start_finish	(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_view_call_start_sync	(GDBusProxy *proxy, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_view_call_stop		(GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_view_call_stop_finish	(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_view_call_stop_sync		(GDBusProxy *proxy, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_view_call_set_flags		(GDBusProxy *proxy, guint in_flags, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_view_call_set_flags_finish	(GDBusProxy *proxy, GAsyncResult *res, GError **error);
gboolean	e_gdbus_cal_view_call_set_flags_sync	(GDBusProxy *proxy, guint in_flags, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_view_call_dispose		(GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_view_call_dispose_finish	(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_view_call_dispose_sync	(GDBusProxy *proxy, GCancellable *cancellable, GError **error);

void		e_gdbus_cal_view_call_set_fields_of_interest		(GDBusProxy *proxy, const gchar * const *in_only_fileds, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_view_call_set_fields_of_interest_finish	(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_cal_view_call_set_fields_of_interest_sync	(GDBusProxy *proxy, const gchar * const *in_only_fileds, GCancellable *cancellable, GError **error);

/* D-Bus Methods Completion Helpers */
#define e_gdbus_cal_view_complete_start				e_gdbus_complete_sync_method_void
#define e_gdbus_cal_view_complete_stop				e_gdbus_complete_sync_method_void
#define e_gdbus_cal_view_complete_set_flags			e_gdbus_complete_sync_method_void
#define e_gdbus_cal_view_complete_dispose			e_gdbus_complete_sync_method_void
#define e_gdbus_cal_view_complete_set_fields_of_interest	e_gdbus_complete_sync_method_void

/* D-Bus Signal Emission Helpers */
void e_gdbus_cal_view_emit_objects_added	(EGdbusCalView *object, const gchar * const *arg_objects);
void e_gdbus_cal_view_emit_objects_modified	(EGdbusCalView *object, const gchar * const *arg_objects);
void e_gdbus_cal_view_emit_objects_removed	(EGdbusCalView *object, const gchar * const *arg_uids);

void e_gdbus_cal_view_emit_progress		(EGdbusCalView *object, guint arg_percent, const gchar *arg_message);
void e_gdbus_cal_view_emit_complete		(EGdbusCalView *object, const gchar * const *arg_error);

G_END_DECLS

#endif /* E_GDBUS_CAL_VIEW_H */

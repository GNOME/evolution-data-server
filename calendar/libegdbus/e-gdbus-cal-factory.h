/*
 * e-gdbus-cal-factory.h
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

#ifndef E_GDBUS_CAL_FACTORY_H
#define E_GDBUS_CAL_FACTORY_H

#include <gio/gio.h>

#include <libedataserver/e-gdbus-templates.h>

G_BEGIN_DECLS

#define E_TYPE_GDBUS_CAL_FACTORY         (e_gdbus_cal_factory_get_type ())
#define E_GDBUS_CAL_FACTORY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_GDBUS_CAL_FACTORY, EGdbusCalFactory))
#define E_IS_GDBUS_CAL_FACTORY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_GDBUS_CAL_FACTORY))
#define E_GDBUS_CAL_FACTORY_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE((o), E_TYPE_GDBUS_CAL_FACTORY, EGdbusCalFactoryIface))

typedef struct _EGdbusCalFactory EGdbusCalFactory; /* Dummy typedef */
typedef struct _EGdbusCalFactoryIface EGdbusCalFactoryIface;

GType e_gdbus_cal_factory_get_type (void) G_GNUC_CONST;

/* ---------------------------------------------------------------------- */

typedef struct _EGdbusCalFactoryProxy EGdbusCalFactoryProxy;
typedef struct _EGdbusCalFactoryProxyClass EGdbusCalFactoryProxyClass;
typedef struct _EGdbusCalFactoryProxyPrivate EGdbusCalFactoryProxyPrivate;

struct _EGdbusCalFactoryProxy
{
	GDBusProxy parent_instance;
	EGdbusCalFactoryProxyPrivate *priv;
};

struct _EGdbusCalFactoryProxyClass
{
	GDBusProxyClass parent_class;
};

#define E_TYPE_GDBUS_CAL_FACTORY_PROXY (e_gdbus_cal_factory_proxy_get_type ())
GType e_gdbus_cal_factory_proxy_get_type (void) G_GNUC_CONST;

void			e_gdbus_cal_factory_proxy_new		(GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
EGdbusCalFactory *	e_gdbus_cal_factory_proxy_new_finish	(GAsyncResult *result, GError **error);
EGdbusCalFactory *	e_gdbus_cal_factory_proxy_new_sync	(GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error);

void			e_gdbus_cal_factory_proxy_new_for_bus	(GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
EGdbusCalFactory *	e_gdbus_cal_factory_proxy_new_for_bus_finish (GAsyncResult  *result, GError **error);
EGdbusCalFactory *	e_gdbus_cal_factory_proxy_new_for_bus_sync (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error);

/* ---------------------------------------------------------------------- */

typedef struct _EGdbusCalFactoryStub EGdbusCalFactoryStub;
typedef struct _EGdbusCalFactoryStubClass EGdbusCalFactoryStubClass;
typedef struct _EGdbusCalFactoryStubPrivate EGdbusCalFactoryStubPrivate;

struct _EGdbusCalFactoryStub
{
	GObject parent_instance;
	EGdbusCalFactoryStubPrivate *priv;
};

struct _EGdbusCalFactoryStubClass
{
	GObjectClass parent_class;
};

#define E_TYPE_GDBUS_CAL_FACTORY_STUB (e_gdbus_cal_factory_stub_get_type ())
GType e_gdbus_cal_factory_stub_get_type (void) G_GNUC_CONST;

EGdbusCalFactory *e_gdbus_cal_factory_stub_new (void);

guint e_gdbus_cal_factory_register_object (EGdbusCalFactory *object, GDBusConnection *connection, const gchar *object_path, GError **error);
void e_gdbus_cal_factory_drain_notify (EGdbusCalFactory *object);
const GDBusInterfaceInfo *e_gdbus_cal_factory_interface_info (void) G_GNUC_CONST;

struct _EGdbusCalFactoryIface
{
	GTypeInterface parent_iface;

	/* Signal handlers for handling D-Bus method calls: */
	gboolean (*handle_get_cal) (EGdbusCalFactory *object, GDBusMethodInvocation *invocation, const gchar * const *in_source_type);
};

gchar **	e_gdbus_cal_factory_encode_get_cal (const gchar *in_source, guint in_type);
gboolean	e_gdbus_cal_factory_decode_get_cal (const gchar * const * in_strv, gchar **out_source, guint *out_type);

/* D-Bus Methods */
void		e_gdbus_cal_factory_call_get_cal	(GDBusProxy *proxy, const gchar * const *in_source_type, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_cal_factory_call_get_cal_finish	(GDBusProxy *proxy, GAsyncResult *result, gchar **out_path, GError **error);
gboolean	e_gdbus_cal_factory_call_get_cal_sync	(GDBusProxy *proxy, const gchar * const *in_source_type, gchar **out_path, GCancellable *cancellable, GError **error);

/* D-Bus Methods Completion Helpers */
void		e_gdbus_cal_factory_complete_get_cal	(EGdbusCalFactory *object, GDBusMethodInvocation *invocation, const gchar *out_path, const GError *error);

G_END_DECLS

#endif /* E_GDBUS_CAL_FACTORY_H */

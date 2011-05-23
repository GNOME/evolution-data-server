/*
 * e-gdbus-book-factory.h
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

#ifndef E_GDBUS_BOOK_FACTORY_H
#define E_GDBUS_BOOK_FACTORY_H

#include <gio/gio.h>

#include <libedataserver/e-gdbus-templates.h>

G_BEGIN_DECLS

#define E_TYPE_GDBUS_BOOK_FACTORY         (e_gdbus_book_factory_get_type ())
#define E_GDBUS_BOOK_FACTORY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_GDBUS_BOOK_FACTORY, EGdbusBookFactory))
#define E_IS_GDBUS_BOOK_FACTORY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_GDBUS_BOOK_FACTORY))
#define E_GDBUS_BOOK_FACTORY_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE((o), E_TYPE_GDBUS_BOOK_FACTORY, EGdbusBookFactoryIface))

/**
 * EGdbusBookFactory:
 *
 * Opaque type representing a proxy or an exported object.
 */
typedef struct _EGdbusBookFactory EGdbusBookFactory; /* Dummy typedef */
typedef struct _EGdbusBookFactoryIface EGdbusBookFactoryIface;

GType e_gdbus_book_factory_get_type (void) G_GNUC_CONST;

/* ---------------------------------------------------------------------- */

typedef struct _EGdbusBookFactoryProxy EGdbusBookFactoryProxy;
typedef struct _EGdbusBookFactoryProxyClass EGdbusBookFactoryProxyClass;
typedef struct _EGdbusBookFactoryProxyPrivate EGdbusBookFactoryProxyPrivate;

struct _EGdbusBookFactoryProxy
{
	GDBusProxy parent_instance;
	EGdbusBookFactoryProxyPrivate *priv;
};

struct _EGdbusBookFactoryProxyClass
{
	GDBusProxyClass parent_class;
};

#define E_TYPE_GDBUS_BOOK_FACTORY_PROXY (e_gdbus_book_factory_proxy_get_type ())
GType e_gdbus_book_factory_proxy_get_type (void) G_GNUC_CONST;

void			e_gdbus_book_factory_proxy_new (GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
EGdbusBookFactory *	e_gdbus_book_factory_proxy_new_finish (GAsyncResult *result, GError **error);
EGdbusBookFactory *	e_gdbus_book_factory_proxy_new_sync (GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error);

void			e_gdbus_book_factory_proxy_new_for_bus (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
EGdbusBookFactory *	e_gdbus_book_factory_proxy_new_for_bus_finish (GAsyncResult *result, GError **error);
EGdbusBookFactory *	e_gdbus_book_factory_proxy_new_for_bus_sync (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error);

/* ---------------------------------------------------------------------- */

typedef struct _EGdbusBookFactoryStub EGdbusBookFactoryStub;
typedef struct _EGdbusBookFactoryStubClass EGdbusBookFactoryStubClass;
typedef struct _EGdbusBookFactoryStubPrivate EGdbusBookFactoryStubPrivate;

struct _EGdbusBookFactoryStub
{
	GObject parent_instance;
	EGdbusBookFactoryStubPrivate *priv;
};

struct _EGdbusBookFactoryStubClass
{
	GObjectClass parent_class;
};

#define E_TYPE_GDBUS_BOOK_FACTORY_STUB (e_gdbus_book_factory_stub_get_type ())
GType e_gdbus_book_factory_stub_get_type (void) G_GNUC_CONST;

EGdbusBookFactory *e_gdbus_book_factory_stub_new (void);

guint e_gdbus_book_factory_register_object (EGdbusBookFactory *object, GDBusConnection *connection, const gchar *object_path, GError **error);

void e_gdbus_book_factory_drain_notify (EGdbusBookFactory *object);

const GDBusInterfaceInfo *e_gdbus_book_factory_interface_info (void) G_GNUC_CONST;

struct _EGdbusBookFactoryIface
{
	GTypeInterface parent_iface;

	/* Signal handlers for handling D-Bus method calls: */
	gboolean (*handle_get_book) (EGdbusBookFactory *object, GDBusMethodInvocation *invocation, const gchar *in_source);
};

/* D-Bus Methods */
void		e_gdbus_book_factory_call_get_book		(GDBusProxy *proxy, const gchar *in_source, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_factory_call_get_book_finish	(GDBusProxy *proxy, GAsyncResult *result, gchar **out_path, GError **error);
gboolean	e_gdbus_book_factory_call_get_book_sync		(GDBusProxy *proxy, const gchar *in_source, gchar **out_path, GCancellable *cancellable, GError **error);

/* D-Bus Methods Completion Helpers */
void e_gdbus_book_factory_complete_get_book (EGdbusBookFactory *object, GDBusMethodInvocation *invocation, const gchar *out_path, const GError *error);

G_END_DECLS

#endif /* E_GDBUS_BOOK_FACTORY_H */

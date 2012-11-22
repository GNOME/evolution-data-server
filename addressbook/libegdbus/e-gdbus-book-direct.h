/*
 * e-gdbus-book-direct.h
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
 * Copyright (C) 2012 Openismus GmbH (www.openismus.com)
 *
 */

#ifndef E_GDBUS_BOOK_DIRECT_H
#define E_GDBUS_BOOK_DIRECT_H

#include <gio/gio.h>

#include <libedataserver/libedataserver.h>

G_BEGIN_DECLS


/* ------------------------------------------------------------------------ */
/* Declarations for org.gnome.evolution.dataserver.AddressBookDirect */

#define E_GDBUS_TYPE_BOOK_DIRECT (e_gdbus_book_direct_get_type ())
#define E_GDBUS_BOOK_DIRECT(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), E_GDBUS_TYPE_BOOK_DIRECT, EGdbusBookDirect))
#define E_GDBUS_IS_BOOK_DIRECT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_GDBUS_TYPE_BOOK_DIRECT))
#define E_GDBUS_BOOK_DIRECT_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), E_GDBUS_TYPE_BOOK_DIRECT, EGdbusBookDirectIface))

struct _EGdbusBookDirect;
typedef struct _EGdbusBookDirect EGdbusBookDirect;
typedef struct _EGdbusBookDirectIface EGdbusBookDirectIface;

struct _EGdbusBookDirectIface
{
  GTypeInterface parent_iface;

  const gchar * (*get_backend_config) (EGdbusBookDirect *object);

  const gchar * (*get_backend_name) (EGdbusBookDirect *object);

  const gchar * (*get_backend_path) (EGdbusBookDirect *object);

};

GType e_gdbus_book_direct_get_type (void) G_GNUC_CONST;

GDBusInterfaceInfo *e_gdbus_book_direct_interface_info (void);
guint e_gdbus_book_direct_override_properties (GObjectClass *klass, guint property_id_begin);


/* D-Bus property accessors: */
const gchar *e_gdbus_book_direct_get_backend_path (EGdbusBookDirect *object);
gchar *e_gdbus_book_direct_dup_backend_path (EGdbusBookDirect *object);
void e_gdbus_book_direct_set_backend_path (EGdbusBookDirect *object, const gchar *value);

const gchar *e_gdbus_book_direct_get_backend_name (EGdbusBookDirect *object);
gchar *e_gdbus_book_direct_dup_backend_name (EGdbusBookDirect *object);
void e_gdbus_book_direct_set_backend_name (EGdbusBookDirect *object, const gchar *value);

const gchar *e_gdbus_book_direct_get_backend_config (EGdbusBookDirect *object);
gchar *e_gdbus_book_direct_dup_backend_config (EGdbusBookDirect *object);
void e_gdbus_book_direct_set_backend_config (EGdbusBookDirect *object, const gchar *value);


/* ---- */

#define E_GDBUS_TYPE_BOOK_DIRECT_PROXY (e_gdbus_book_direct_proxy_get_type ())
#define E_GDBUS_BOOK_DIRECT_PROXY(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), E_GDBUS_TYPE_BOOK_DIRECT_PROXY, EGdbusBookDirectProxy))
#define E_GDBUS_BOOK_DIRECT_PROXY_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), E_GDBUS_TYPE_BOOK_DIRECT_PROXY, EGdbusBookDirectProxyClass))
#define E_GDBUS_BOOK_DIRECT_PROXY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_GDBUS_TYPE_BOOK_DIRECT_PROXY, EGdbusBookDirectProxyClass))
#define E_GDBUS_IS_BOOK_DIRECT_PROXY(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_GDBUS_TYPE_BOOK_DIRECT_PROXY))
#define E_GDBUS_IS_BOOK_DIRECT_PROXY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_GDBUS_TYPE_BOOK_DIRECT_PROXY))

typedef struct _EGdbusBookDirectProxy EGdbusBookDirectProxy;
typedef struct _EGdbusBookDirectProxyClass EGdbusBookDirectProxyClass;
typedef struct _EGdbusBookDirectProxyPrivate EGdbusBookDirectProxyPrivate;

struct _EGdbusBookDirectProxy
{
  /*< private >*/
  GDBusProxy parent_instance;
  EGdbusBookDirectProxyPrivate *priv;
};

struct _EGdbusBookDirectProxyClass
{
  GDBusProxyClass parent_class;
};

GType e_gdbus_book_direct_proxy_get_type (void) G_GNUC_CONST;

void e_gdbus_book_direct_proxy_new (
    GDBusConnection     *connection,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GAsyncReadyCallback  callback,
    gpointer             user_data);
EGdbusBookDirect *e_gdbus_book_direct_proxy_new_finish (
    GAsyncResult        *res,
    GError             **error);
EGdbusBookDirect *e_gdbus_book_direct_proxy_new_sync (
    GDBusConnection     *connection,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GError             **error);

void e_gdbus_book_direct_proxy_new_for_bus (
    GBusType             bus_type,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GAsyncReadyCallback  callback,
    gpointer             user_data);
EGdbusBookDirect *e_gdbus_book_direct_proxy_new_for_bus_finish (
    GAsyncResult        *res,
    GError             **error);
EGdbusBookDirect *e_gdbus_book_direct_proxy_new_for_bus_sync (
    GBusType             bus_type,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GError             **error);


/* ---- */

#define E_GDBUS_TYPE_BOOK_DIRECT_SKELETON (e_gdbus_book_direct_skeleton_get_type ())
#define E_GDBUS_BOOK_DIRECT_SKELETON(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), E_GDBUS_TYPE_BOOK_DIRECT_SKELETON, EGdbusBookDirectSkeleton))
#define E_GDBUS_BOOK_DIRECT_SKELETON_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), E_GDBUS_TYPE_BOOK_DIRECT_SKELETON, EGdbusBookDirectSkeletonClass))
#define E_GDBUS_BOOK_DIRECT_SKELETON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_GDBUS_TYPE_BOOK_DIRECT_SKELETON, EGdbusBookDirectSkeletonClass))
#define E_GDBUS_IS_BOOK_DIRECT_SKELETON(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_GDBUS_TYPE_BOOK_DIRECT_SKELETON))
#define E_GDBUS_IS_BOOK_DIRECT_SKELETON_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_GDBUS_TYPE_BOOK_DIRECT_SKELETON))

typedef struct _EGdbusBookDirectSkeleton EGdbusBookDirectSkeleton;
typedef struct _EGdbusBookDirectSkeletonClass EGdbusBookDirectSkeletonClass;
typedef struct _EGdbusBookDirectSkeletonPrivate EGdbusBookDirectSkeletonPrivate;

struct _EGdbusBookDirectSkeleton
{
  /*< private >*/
  GDBusInterfaceSkeleton parent_instance;
  EGdbusBookDirectSkeletonPrivate *priv;
};

struct _EGdbusBookDirectSkeletonClass
{
  GDBusInterfaceSkeletonClass parent_class;
};

GType e_gdbus_book_direct_skeleton_get_type (void) G_GNUC_CONST;

EGdbusBookDirect *e_gdbus_book_direct_skeleton_new (void);


G_END_DECLS

#endif /* E_GDBUS_BOOK_DIRECT_H */

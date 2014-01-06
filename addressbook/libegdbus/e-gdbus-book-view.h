/*
 * e-gdbus-book-view.h
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Copyright (C) 2011 Red Hat, Inc. (www.redhat.com)
 *
 */

#ifndef E_GDBUS_BOOK_VIEW_H
#define E_GDBUS_BOOK_VIEW_H

#include <gio/gio.h>

#include <libedataserver/libedataserver.h>

G_BEGIN_DECLS

#define E_TYPE_GDBUS_BOOK_VIEW         (e_gdbus_book_view_get_type ())
#define E_GDBUS_BOOK_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_GDBUS_BOOK_VIEW, EGdbusBookView))
#define E_IS_GDBUS_BOOK_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_GDBUS_BOOK_VIEW))
#define E_GDBUS_BOOK_VIEW_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE((o), E_TYPE_GDBUS_BOOK_VIEW, EGdbusBookViewIface))

/**
 * EGdbusBookView:
 *
 * Opaque type representing a proxy or an exported object.
 */
typedef struct _EGdbusBookView EGdbusBookView; /* Dummy typedef */
typedef struct _EGdbusBookViewIface EGdbusBookViewIface;

GType e_gdbus_book_view_get_type (void) G_GNUC_CONST;

/* ---------------------------------------------------------------------- */

typedef struct _EGdbusBookViewProxy EGdbusBookViewProxy;
typedef struct _EGdbusBookViewProxyClass EGdbusBookViewProxyClass;
typedef struct _EGdbusBookViewProxyPrivate EGdbusBookViewProxyPrivate;

struct _EGdbusBookViewProxy
{
	GDBusProxy parent_instance;
	EGdbusBookViewProxyPrivate *priv;
};

struct _EGdbusBookViewProxyClass
{
	GDBusProxyClass parent_class;
};

#define E_TYPE_GDBUS_BOOK_VIEW_PROXY (e_gdbus_book_view_proxy_get_type ())
GType e_gdbus_book_view_proxy_get_type (void) G_GNUC_CONST;

void		e_gdbus_book_view_proxy_new (GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
EGdbusBookView *e_gdbus_book_view_proxy_new_finish (GAsyncResult  *result, GError **error);
EGdbusBookView *e_gdbus_book_view_proxy_new_sync (GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error);

void		e_gdbus_book_view_proxy_new_for_bus (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
EGdbusBookView *e_gdbus_book_view_proxy_new_for_bus_finish (GAsyncResult  *result, GError **error);
EGdbusBookView *e_gdbus_book_view_proxy_new_for_bus_sync (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error);

/* ---------------------------------------------------------------------- */

typedef struct _EGdbusBookViewStub EGdbusBookViewStub;
typedef struct _EGdbusBookViewStubClass EGdbusBookViewStubClass;
typedef struct _EGdbusBookViewStubPrivate EGdbusBookViewStubPrivate;

struct _EGdbusBookViewStub
{
	GObject parent_instance;
	EGdbusBookViewStubPrivate *priv;
};

struct _EGdbusBookViewStubClass
{
	GObjectClass parent_class;
};

#define E_TYPE_GDBUS_BOOK_VIEW_STUB (e_gdbus_book_view_stub_get_type ())
GType e_gdbus_book_view_stub_get_type (void) G_GNUC_CONST;

EGdbusBookView *e_gdbus_book_view_stub_new (void);

guint e_gdbus_book_view_register_object (EGdbusBookView *object, GDBusConnection *connection, const gchar *object_path, GError **error);

void e_gdbus_book_view_drain_notify (EGdbusBookView *object);

const GDBusInterfaceInfo *e_gdbus_book_view_interface_info (void) G_GNUC_CONST;

struct _EGdbusBookViewIface
{
	GTypeInterface parent_iface;

	/* Signal handlers for receiving D-Bus signals: */
	void (*objects_added)		(EGdbusBookView *object, const gchar * const *arg_objects);
	void (*objects_modified)	(EGdbusBookView *object, const gchar * const *arg_objects);
	void (*objects_removed)		(EGdbusBookView *object, const gchar * const *arg_uids);

	void (*progress)		(EGdbusBookView *object, guint arg_percent, const gchar *arg_message);
	void (*complete)		(EGdbusBookView *object, const gchar * const *arg_error);

	/* Signal handlers for handling D-Bus method calls: */
	gboolean (*handle_start)		(EGdbusBookView *object, GDBusMethodInvocation *invocation);
	gboolean (*handle_stop)			(EGdbusBookView *object, GDBusMethodInvocation *invocation);
	gboolean (*handle_set_flags)            (EGdbusBookView *object, GDBusMethodInvocation *invocation, guint in_flags);
	gboolean (*handle_dispose)		(EGdbusBookView *object, GDBusMethodInvocation *invocation);
	gboolean (*handle_set_fields_of_interest)(EGdbusBookView *object, GDBusMethodInvocation *invocation, const gchar * const *in_only_fields);
};

/* D-Bus Methods */
void		e_gdbus_book_view_call_start		(GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_view_call_start_finish	(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_book_view_call_start_sync	(GDBusProxy *proxy, GCancellable *cancellable, GError **error);

void		e_gdbus_book_view_call_stop		(GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_view_call_stop_finish	(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_book_view_call_stop_sync	(GDBusProxy *proxy, GCancellable *cancellable, GError **error);

void            e_gdbus_book_view_call_set_flags        (GDBusProxy         *proxy,
							 guint               in_flags,
							 GCancellable       *cancellable,
							 GAsyncReadyCallback callback,
							 gpointer            user_data);
gboolean        e_gdbus_book_view_call_set_flags_finish (GDBusProxy         *proxy,
							 GAsyncResult       *res,
							 GError            **error);
gboolean        e_gdbus_book_view_call_set_flags_sync   (GDBusProxy         *proxy,
							 guint               in_flags,
							 GCancellable       *cancellable,
							 GError            **error);

void		e_gdbus_book_view_call_dispose		(GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_view_call_dispose_finish	(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_book_view_call_dispose_sync	(GDBusProxy *proxy, GCancellable *cancellable, GError **error);

void		e_gdbus_book_view_call_set_fields_of_interest		(GDBusProxy *proxy, const gchar * const *in_only_fileds, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_gdbus_book_view_call_set_fields_of_interest_finish	(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean	e_gdbus_book_view_call_set_fields_of_interest_sync	(GDBusProxy *proxy, const gchar * const *in_only_fileds, GCancellable *cancellable, GError **error);

/* D-Bus Methods Completion Helpers */
#define e_gdbus_book_view_complete_start			e_gdbus_complete_sync_method_void
#define e_gdbus_book_view_complete_stop				e_gdbus_complete_sync_method_void
#define e_gdbus_book_view_complete_set_flags			e_gdbus_complete_sync_method_void
#define e_gdbus_book_view_complete_dispose			e_gdbus_complete_sync_method_void
#define e_gdbus_book_view_complete_set_fields_of_interest	e_gdbus_complete_sync_method_void

/* D-Bus Signal Emission Helpers */
void	e_gdbus_book_view_emit_objects_added	(EGdbusBookView *object, const gchar * const *arg_objects);
void	e_gdbus_book_view_emit_objects_modified	(EGdbusBookView *object, const gchar * const *arg_objects);
void	e_gdbus_book_view_emit_objects_removed	(EGdbusBookView *object, const gchar * const *arg_uids);

void	e_gdbus_book_view_emit_progress		(EGdbusBookView *object, guint arg_percent, const gchar *arg_message);
void	e_gdbus_book_view_emit_complete		(EGdbusBookView *object, const gchar * const *arg_error);

G_END_DECLS

#endif /* E_GDBUS_BOOK_VIEW_H */

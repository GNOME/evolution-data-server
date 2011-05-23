/*
 * e-gdbus-cal-factory.c
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

#include "e-gdbus-cal-factory.h"

#define GDBUS_CAL_FACTORY_INTERFACE_NAME "org.gnome.evolution.dataserver.CalendarFactory"

typedef EGdbusCalFactoryIface EGdbusCalFactoryInterface;
G_DEFINE_INTERFACE (EGdbusCalFactory, e_gdbus_cal_factory, G_TYPE_OBJECT);

enum
{
	_0_SIGNAL,
	__GET_CAL_METHOD,
	__LAST_SIGNAL
};

static guint signals[__LAST_SIGNAL] = {0};

/* ------------------------------------------------------------------------- */

/* Various lookup tables */

static GHashTable *_method_name_to_id = NULL;
static GHashTable *_method_name_to_type = NULL;

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

/* ------------------------------------------------------------------------- */

static void
e_gdbus_cal_factory_default_init (EGdbusCalFactoryIface *iface)
{
	/* Build lookup structures */
	_method_name_to_id = g_hash_table_new (g_str_hash, g_str_equal);
	_method_name_to_type = g_hash_table_new (g_str_hash, g_str_equal);

	E_INIT_GDBUS_METHOD_STRV (EGdbusCalFactoryIface, "getCal", get_cal, __GET_CAL_METHOD)
}

/* encodes source and source type into a strv usable for a wire transfer;
   Free returned pointer with g_strfreev() */
gchar **
e_gdbus_cal_factory_encode_get_cal (const gchar *in_source, guint in_type)
{
	gchar **strv;

	g_return_val_if_fail (in_source != NULL, NULL);

	strv = g_new0 (gchar *, 3);
	strv[0] = g_strdup (in_source);
	strv[1] = g_strdup_printf ("%u", (guint32) in_type);
	strv[2] = NULL;

	return strv;
}

/* decodes source and source type from a strv recevied from a wire transfer;
   free out_source with g_free(); returns TRUE is successful. */
gboolean
e_gdbus_cal_factory_decode_get_cal (const gchar * const * in_strv, gchar **out_source, guint *out_type)
{
	g_return_val_if_fail (in_strv != NULL, FALSE);
	g_return_val_if_fail (in_strv[0] != NULL, FALSE);
	g_return_val_if_fail (in_strv[1] != NULL, FALSE);
	g_return_val_if_fail (in_strv[2] == NULL, FALSE);
	g_return_val_if_fail (out_source != NULL, FALSE);
	g_return_val_if_fail (out_type != NULL, FALSE);

	*out_source = g_strdup (in_strv[0]);
	*out_type = atoi (in_strv[1]);

	return TRUE;
}

/* C Bindings for properties */

void
e_gdbus_cal_factory_call_get_cal (GDBusProxy *proxy, const gchar * const *in_source_type, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	/* in_source_type has only two items, the first is ESource, the second is source type */
	/* use e_gdbus_cal_factory_encode_get_cal() to encode them */

	g_return_if_fail (in_source_type != NULL);
	g_return_if_fail (in_source_type[0] != NULL);
	g_return_if_fail (in_source_type[1] != NULL);
	g_return_if_fail (in_source_type[2] == NULL);

	e_gdbus_proxy_method_call_strv ("getCal", proxy, in_source_type, cancellable, callback, user_data);
}

gboolean
e_gdbus_cal_factory_call_get_cal_finish (GDBusProxy *proxy, GAsyncResult *result, gchar **out_path, GError **error)
{
	return e_gdbus_proxy_method_call_finish_string (proxy, result, out_path, error);
}

gboolean
e_gdbus_cal_factory_call_get_cal_sync (GDBusProxy *proxy, const gchar * const *in_source_type, gchar **out_path, GCancellable *cancellable, GError **error)
{
	/* in_source_type has only two items, the first is ESource, the second is source type */
	/* use e_gdbus_cal_factory_encode_get_cal() to encode them */

	g_return_val_if_fail (in_source_type != NULL, FALSE);
	g_return_val_if_fail (in_source_type[0] != NULL, FALSE);
	g_return_val_if_fail (in_source_type[1] != NULL, FALSE);
	g_return_val_if_fail (in_source_type[2] == NULL, FALSE);

	return e_gdbus_proxy_method_call_sync_strv__string ("getCal", proxy, in_source_type, out_path, cancellable, error);
}

void
e_gdbus_cal_factory_complete_get_cal (EGdbusCalFactory *object, GDBusMethodInvocation *invocation, const gchar *out_path, const GError *error)
{
	e_gdbus_complete_sync_method_string (object, invocation, out_path, error);
}

E_DECLARE_GDBUS_SYNC_METHOD_1_WITH_RETURN (cal_factory, getCal, source_type, "as", path, "s")

static const GDBusMethodInfo * const e_gdbus_cal_factory_method_info_pointers[] =
{
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (cal_factory, getCal),
	NULL
};

static const GDBusInterfaceInfo _e_gdbus_cal_factory_interface_info =
{
	-1,
	(gchar *) GDBUS_CAL_FACTORY_INTERFACE_NAME,
	(GDBusMethodInfo **) &e_gdbus_cal_factory_method_info_pointers,
	(GDBusSignalInfo **) NULL,
	(GDBusPropertyInfo **) NULL,
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

static const GDBusInterfaceVTable e_gdbus_cal_factory_interface_vtable =
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
							GDBUS_CAL_FACTORY_INTERFACE_NAME,
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
 * e_gdbus_cal_factory_drain_notify:
 * @object: A #EGdbusCalFactory that is exported.
 *
 * If @object has queued notifications, empty the queue forcing
 * the <literal>PropertiesChanged</literal> signal to be emitted.
 * See <xref linkend="EGdbusCalFactory.description"/> for more background information.
 */
void
e_gdbus_cal_factory_drain_notify (EGdbusCalFactory *object)
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
 * e_gdbus_cal_factory_register_object:
 * @object: An instance of a #GObject<!-- -->-derived type implementing the #EGdbusCalFactory interface.
 * @connection: A #GDBusConnection.
 * @object_path: The object to register the object at.
 * @error: Return location for error or %NULL.
 *
 * Registers @object at @object_path on @connection.
 *
 * See <xref linkend="EGdbusCalFactory.description"/>
 * for how properties, methods and signals are handled.
 *
 * Returns: 0 if @error is set, otherwise a registration id (never 0) that can be used with g_dbus_connection_unregister_object().
 */
guint
e_gdbus_cal_factory_register_object (EGdbusCalFactory *object, GDBusConnection *connection, const gchar *object_path, GError **error)
{
	GHashTable *pvc;

	pvc = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) g_variant_unref);

	g_object_set_data_full (G_OBJECT (object), "gdbus-codegen-path", (gpointer) g_strdup (object_path), g_free);
	g_object_set_data (G_OBJECT (object), "gdbus-codegen-connection", (gpointer) connection);
	g_object_set_data_full (G_OBJECT (object), "gdbus-codegen-pvc", (gpointer) pvc, (GDestroyNotify) g_hash_table_unref);
	return g_dbus_connection_register_object (connection,
		object_path,
		(GDBusInterfaceInfo *) &_e_gdbus_cal_factory_interface_info,
		&e_gdbus_cal_factory_interface_vtable,
		object,
		(GDestroyNotify) on_object_unregistered,
		error);
}

/**
 * e_gdbus_cal_factory_interface_info:
 *
 * Gets interface description for the <literal>org.gnome.evolution.dataserver.CalendarFactory</literal> D-Bus interface.
 *
 * Returns: A #GDBusInterfaceInfo. Do not free, the object is statically allocated.
 */
const GDBusInterfaceInfo *
e_gdbus_cal_factory_interface_info (void)
{
	return &_e_gdbus_cal_factory_interface_info;
}

/* ---------------------------------------------------------------------- */

static void proxy_iface_init (EGdbusCalFactoryIface *iface);

G_DEFINE_TYPE_WITH_CODE (EGdbusCalFactoryProxy, e_gdbus_cal_factory_proxy, G_TYPE_DBUS_PROXY,
                         G_IMPLEMENT_INTERFACE (E_TYPE_GDBUS_CAL_FACTORY, proxy_iface_init));

static void
e_gdbus_cal_factory_proxy_init (EGdbusCalFactoryProxy *proxy)
{
	g_dbus_proxy_set_interface_info (G_DBUS_PROXY (proxy), (GDBusInterfaceInfo *) &_e_gdbus_cal_factory_interface_info);
}

static void
g_signal (GDBusProxy *proxy, const gchar *sender_name, const gchar *signal_name, GVariant *parameters)
{
	/*
	guint signal_id, signal_type;

	signal_id = lookup_signal_id_from_signal_name (signal_name);
	signal_type = lookup_signal_type_from_signal_name (signal_name);

	g_return_if_fail (signal_id != 0);
	g_return_if_fail (signal_type != 0);

	e_gdbus_proxy_emit_signal (proxy, parameters, signals[signal_id], signal_type);
	*/
}

static void
e_gdbus_cal_factory_proxy_class_init (EGdbusCalFactoryProxyClass *klass)
{
	GDBusProxyClass *proxy_class;

	proxy_class = G_DBUS_PROXY_CLASS (klass);
	proxy_class->g_signal = g_signal;
}

static void
proxy_iface_init (EGdbusCalFactoryIface *iface)
{
}

/**
 * e_gdbus_cal_factory_proxy_new:
 * @connection: A #GDBusConnection.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique) or %NULL if @connection is not a message bus connection.
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Like g_dbus_proxy_new() but returns a #EGdbusCalFactoryProxy.
 *
 * This is a failable asynchronous constructor - when the proxy is ready, callback will be invoked and you can use e_gdbus_cal_factory_proxy_new_finish() to get the result.
 */
void
e_gdbus_cal_factory_proxy_new (GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	g_async_initable_new_async (E_TYPE_GDBUS_CAL_FACTORY_PROXY,
				G_PRIORITY_DEFAULT,
				cancellable,
				callback,
				user_data,
				"g-flags", flags,
				"g-name", name,
				"g-connection", connection,
				"g-object-path", object_path,
				"g-interface-name", GDBUS_CAL_FACTORY_INTERFACE_NAME,
				NULL);
}

/**
 * e_gdbus_cal_factory_proxy_new_finish:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to e_gdbus_cal_factory_proxy_new().
 * @error: Return location for error or %NULL.
 *
 * Finishes creating a #EGdbusCalFactoryProxy.
 *
 * Returns: A #EGdbusCalFactoryProxy or %NULL if @error is set. Free with g_object_unref().
 */
EGdbusCalFactory *
e_gdbus_cal_factory_proxy_new_finish (GAsyncResult *result, GError **error)
{
	GObject *object;
	GObject *source_object;
	source_object = g_async_result_get_source_object (result);
	g_assert (source_object != NULL);
	object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), result, error);
	g_object_unref (source_object);
	if (object != NULL)
		return E_GDBUS_CAL_FACTORY (object);
	else
		return NULL;
}

/**
 * e_gdbus_cal_factory_proxy_new_sync:
 * @connection: A #GDBusConnection.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique) or %NULL if @connection is not a message bus connection.
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Like g_dbus_proxy_new_sync() but returns a #EGdbusCalFactoryProxy.
 *
 * This is a synchronous failable constructor. See e_gdbus_cal_factory_proxy_new() and e_gdbus_cal_factory_proxy_new_finish() for the asynchronous version.
 *
 * Returns: A #EGdbusCalFactoryProxy or %NULL if error is set. Free with g_object_unref().
 */
EGdbusCalFactory *
e_gdbus_cal_factory_proxy_new_sync (GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error)
{
	GInitable *initable;
	initable = g_initable_new (E_TYPE_GDBUS_CAL_FACTORY_PROXY,
				cancellable,
				error,
				"g-flags", flags,
				"g-name", name,
				"g-connection", connection,
				"g-object-path", object_path,
				"g-interface-name", GDBUS_CAL_FACTORY_INTERFACE_NAME,
				NULL);
	if (initable != NULL)
		return E_GDBUS_CAL_FACTORY (initable);
	else
		return NULL;
}

/**
 * e_gdbus_cal_factory_proxy_new_for_bus:
 * @bus_type: A #GBusType.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique).
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Like g_dbus_proxy_new_for_bus() but returns a #EGdbusCalFactoryProxy.
 *
 * This is a failable asynchronous constructor - when the proxy is ready, callback will be invoked and you can use e_gdbus_cal_factory_proxy_new_for_bus_finish() to get the result.
 */
void
e_gdbus_cal_factory_proxy_new_for_bus (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	g_async_initable_new_async (E_TYPE_GDBUS_CAL_FACTORY_PROXY,
				G_PRIORITY_DEFAULT,
				cancellable,
				callback,
				user_data,
				"g-flags", flags,
				"g-name", name,
				"g-bus-type", bus_type,
				"g-object-path", object_path,
				"g-interface-name", GDBUS_CAL_FACTORY_INTERFACE_NAME,
				NULL);
}

/**
 * e_gdbus_cal_factory_proxy_new_for_bus_finish:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to e_gdbus_cal_factory_proxy_new_for_bus().
 * @error: Return location for error or %NULL.
 *
 * Finishes creating a #EGdbusCalFactoryProxy.
 *
 * Returns: A #EGdbusCalFactoryProxy or %NULL if @error is set. Free with g_object_unref().
 */
EGdbusCalFactory *
e_gdbus_cal_factory_proxy_new_for_bus_finish (GAsyncResult *result, GError **error)
{
	GObject *object;
	GObject *source_object;
	source_object = g_async_result_get_source_object (result);
	g_assert (source_object != NULL);
	object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), result, error);
	g_object_unref (source_object);
	if (object != NULL)
		return E_GDBUS_CAL_FACTORY (object);
	else
		return NULL;
}

/**
 * e_gdbus_cal_factory_proxy_new_for_bus_sync:
 * @bus_type: A #GBusType.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique).
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Like g_dbus_proxy_new_for_bus_sync() but returns a #EGdbusCalFactoryProxy.
 *
 * This is a synchronous failable constructor. See e_gdbus_cal_factory_proxy_new_for_bus() and e_gdbus_cal_factory_proxy_new_for_bus_finish() for the asynchronous version.
 *
 * Returns: A #EGdbusCalFactoryProxy or %NULL if error is set. Free with g_object_unref().
 */
EGdbusCalFactory *
e_gdbus_cal_factory_proxy_new_for_bus_sync (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error)
{
	GInitable *initable;
	initable = g_initable_new (E_TYPE_GDBUS_CAL_FACTORY_PROXY,
				cancellable,
				error,
				"g-flags", flags,
				"g-name", name,
				"g-bus-type", bus_type,
				"g-object-path", object_path,
				"g-interface-name", GDBUS_CAL_FACTORY_INTERFACE_NAME,
				NULL);
	if (initable != NULL)
		return E_GDBUS_CAL_FACTORY (initable);
	else
		return NULL;
}

/* ---------------------------------------------------------------------- */

struct _EGdbusCalFactoryStubPrivate
{
	gint foo;
};

static void stub_iface_init (EGdbusCalFactoryIface *iface);

G_DEFINE_TYPE_WITH_CODE (EGdbusCalFactoryStub, e_gdbus_cal_factory_stub, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (E_TYPE_GDBUS_CAL_FACTORY, stub_iface_init));

static void
e_gdbus_cal_factory_stub_init (EGdbusCalFactoryStub *stub)
{
	stub->priv = G_TYPE_INSTANCE_GET_PRIVATE (stub, E_TYPE_GDBUS_CAL_FACTORY_STUB, EGdbusCalFactoryStubPrivate);
}

static void
e_gdbus_cal_factory_stub_class_init (EGdbusCalFactoryStubClass *klass)
{
	g_type_class_add_private (klass, sizeof (EGdbusCalFactoryStubPrivate));
}

static void
stub_iface_init (EGdbusCalFactoryIface *iface)
{
}

/**
 * e_gdbus_cal_factory_stub_new:
 *
 * Creates a new stub object that can be exported via e_gdbus_cal_factory_register_object().
 *
 * Returns: A #EGdbusCalFactoryStub instance. Free with g_object_unref().
 */
EGdbusCalFactory *
e_gdbus_cal_factory_stub_new (void)
{
	return E_GDBUS_CAL_FACTORY (g_object_new (E_TYPE_GDBUS_CAL_FACTORY_STUB, NULL));
}

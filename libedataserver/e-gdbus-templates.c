/*
 * e-gdbus-templates.c
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

#include <gio/gio.h>

#include <stdio.h>

#include "e-data-server-util.h"
#include "e-flag.h"
#include "e-gdbus-templates.h"

static GThread *main_thread = NULL;

void
e_gdbus_templates_init_main_thread (void)
{
	if (!main_thread) {
		main_thread = g_thread_self ();
	} else if (main_thread != g_thread_self ()) {
		g_warning ("%s: Called in different main thread, stored: %p would use: %p", G_STRFUNC, main_thread, g_thread_self ());
	}
}

gboolean
e_gdbus_signal_emission_hook_void (GSignalInvocationHint *ihint,
                                   guint n_param_values,
                                   const GValue *param_values,
                                   const gchar *signal_name,
                                   const gchar *iface_name)
{
	GObject *object;
	GDBusConnection *connection;
	const gchar *path;

	if (n_param_values < 1 || !G_VALUE_HOLDS (&param_values[0], G_TYPE_OBJECT))
		return FALSE;

	object = g_value_get_object (&param_values[0]);
	path = g_object_get_data (object, "gdbus-codegen-path");
	connection = g_object_get_data (object, "gdbus-codegen-connection");
	if (connection == NULL || path == NULL)
		return FALSE;

	g_dbus_connection_emit_signal (connection, NULL, path, iface_name, signal_name, NULL, NULL);

	return TRUE;
}

gboolean
e_gdbus_signal_emission_hook_boolean (GSignalInvocationHint *ihint,
                                      guint n_param_values,
                                      const GValue *param_values,
                                      const gchar *signal_name,
                                      const gchar *iface_name)
{
	GObject *object;
	GDBusConnection *connection;
	const gchar *path;
	GVariant *params;
	GVariant *item;
	GVariantBuilder *builder;

	if (n_param_values < 1 || !G_VALUE_HOLDS (&param_values[0], G_TYPE_OBJECT))
		return FALSE;

	object = g_value_get_object (&param_values[0]);
	path = g_object_get_data (object, "gdbus-codegen-path");
	connection = g_object_get_data (object, "gdbus-codegen-connection");
	if (connection == NULL || path == NULL)
		return FALSE;

	builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
	g_assert_cmpint (n_param_values - 1, ==, 1);
	param_values++;
	item = g_variant_new_boolean (g_value_get_boolean (param_values));
	g_variant_builder_add_value (builder, item);
	param_values++;
	params = g_variant_builder_end (builder);
	g_variant_builder_unref (builder);

	g_dbus_connection_emit_signal (connection, NULL, path, iface_name, signal_name, params, NULL);

	return TRUE;
}

gboolean
e_gdbus_signal_emission_hook_string (GSignalInvocationHint *ihint,
                                     guint n_param_values,
                                     const GValue *param_values,
                                     const gchar *signal_name,
                                     const gchar *iface_name)
{
	GObject *object;
	GDBusConnection *connection;
	const gchar *path;
	GVariant *params;
	GVariant *item;
	GVariantBuilder *builder;

	if (n_param_values < 1 || !G_VALUE_HOLDS (&param_values[0], G_TYPE_OBJECT))
		return FALSE;

	object = g_value_get_object (&param_values[0]);
	path = g_object_get_data (object, "gdbus-codegen-path");
	connection = g_object_get_data (object, "gdbus-codegen-connection");
	if (connection == NULL || path == NULL)
		return FALSE;

	builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
	g_assert_cmpint (n_param_values - 1, ==, 1);
	param_values++;
	item = g_variant_new_string (g_value_get_string (param_values));
	g_variant_builder_add_value (builder, item);
	param_values++;
	params = g_variant_builder_end (builder);
	g_variant_builder_unref (builder);

	g_dbus_connection_emit_signal (connection, NULL, path, iface_name, signal_name, params, NULL);

	return TRUE;
}

gboolean
e_gdbus_signal_emission_hook_strv (GSignalInvocationHint *ihint,
                                   guint n_param_values,
                                   const GValue *param_values,
                                   const gchar *signal_name,
                                   const gchar *iface_name)
{
	GObject *object;
	GDBusConnection *connection;
	const gchar *path;
	GVariant *params;
	GVariant *item;
	GVariantBuilder *builder;
	const gchar * const *arg_strv;

	if (n_param_values < 1 || !G_VALUE_HOLDS (&param_values[0], G_TYPE_OBJECT))
		return FALSE;

	object = g_value_get_object (&param_values[0]);
	path = g_object_get_data (object, "gdbus-codegen-path");
	connection = g_object_get_data (object, "gdbus-codegen-connection");
	if (connection == NULL || path == NULL)
		return FALSE;

	builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
	g_assert_cmpint (n_param_values - 1, ==, 1);
	param_values++;
	arg_strv = g_value_get_boxed (param_values);
	item = g_variant_new_strv (arg_strv, -1);
	g_variant_builder_add_value (builder, item);
	param_values++;
	params = g_variant_builder_end (builder);
	g_variant_builder_unref (builder);

	g_dbus_connection_emit_signal (connection, NULL, path, iface_name, signal_name, params, NULL);

	return TRUE;
}

gboolean
e_gdbus_signal_emission_hook_uint (GSignalInvocationHint *ihint,
                                   guint n_param_values,
                                   const GValue *param_values,
                                   const gchar *signal_name,
                                   const gchar *iface_name)
{
	GObject *object;
	GDBusConnection *connection;
	const gchar *path;
	GVariant *params;
	GVariant *item;
	GVariantBuilder *builder;

	if (n_param_values < 1 || !G_VALUE_HOLDS (&param_values[0], G_TYPE_OBJECT))
		return FALSE;

	object = g_value_get_object (&param_values[0]);
	path = g_object_get_data (object, "gdbus-codegen-path");
	connection = g_object_get_data (object, "gdbus-codegen-connection");
	if (connection == NULL || path == NULL)
		return FALSE;

	builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
	g_assert_cmpint (n_param_values - 1, ==, 1);
	param_values++;
	item = g_variant_new_uint32 (g_value_get_uint (param_values));
	g_variant_builder_add_value (builder, item);
	param_values++;
	params = g_variant_builder_end (builder);
	g_variant_builder_unref (builder);

	g_dbus_connection_emit_signal (connection, NULL, path, iface_name, signal_name, params, NULL);

	return TRUE;
}

gboolean
e_gdbus_signal_emission_hook_uint_string (GSignalInvocationHint *ihint,
                                          guint n_param_values,
                                          const GValue *param_values,
                                          const gchar *signal_name,
                                          const gchar *iface_name)
{
	GObject *object;
	GDBusConnection *connection;
	const gchar *path;
	GVariant *params;
	GVariant *item;
	GVariantBuilder *builder;

	if (n_param_values < 1 || !G_VALUE_HOLDS (&param_values[0], G_TYPE_OBJECT))
		return FALSE;

	object = g_value_get_object (&param_values[0]);
	path = g_object_get_data (object, "gdbus-codegen-path");
	connection = g_object_get_data (object, "gdbus-codegen-connection");
	if (connection == NULL || path == NULL)
		return FALSE;

	builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
	g_assert_cmpint (n_param_values - 1, ==, 2);
	param_values++;
	item = g_variant_new_uint32 (g_value_get_uint (param_values));
	g_variant_builder_add_value (builder, item);
	param_values++;
	item = g_variant_new_string (g_value_get_string (param_values));
	g_variant_builder_add_value (builder, item);
	param_values++;
	params = g_variant_builder_end (builder);
	g_variant_builder_unref (builder);

	g_dbus_connection_emit_signal (connection, NULL, path, iface_name, signal_name, params, NULL);

	return TRUE;
}

gboolean
e_gdbus_signal_emission_hook_async_void (GSignalInvocationHint *ihint,
                                         guint n_param_values,
                                         const GValue *param_values,
                                         const gchar *signal_name,
                                         const gchar *iface_name)
{
	GObject *object;
	GDBusConnection *connection;
	const gchar *path;
	GVariant *params;
	GVariant *item;
	GVariantBuilder *builder;
	GError *arg_error;

	if (n_param_values < 1 || !G_VALUE_HOLDS (&param_values[0], G_TYPE_OBJECT))
		return FALSE;

	object = g_value_get_object (&param_values[0]);
	path = g_object_get_data (object, "gdbus-codegen-path");
	connection = g_object_get_data (object, "gdbus-codegen-connection");
	if (connection == NULL || path == NULL)
		return FALSE;

	builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
	g_assert_cmpint (n_param_values - 1, ==, 2);
	param_values++;
	item = g_variant_new_uint32 (g_value_get_uint (param_values));
	g_variant_builder_add_value (builder, item);
	param_values++;
	arg_error = g_value_get_boxed (param_values);
	if (arg_error) {
		gchar *dbus_error_name = g_dbus_error_encode_gerror (arg_error);
		item = g_variant_new_string (dbus_error_name ? dbus_error_name : "");
		g_variant_builder_add_value (builder, item);
		item = g_variant_new_string (arg_error->message);
		g_variant_builder_add_value (builder, item);
		g_free (dbus_error_name);
	} else {
		item = g_variant_new_string ("");
		g_variant_builder_add_value (builder, item);
		item = g_variant_new_string ("");
		g_variant_builder_add_value (builder, item);

		param_values++;
	}
	params = g_variant_builder_end (builder);
	g_variant_builder_unref (builder);

	g_dbus_connection_emit_signal (connection, NULL, path, iface_name, signal_name, params, NULL);

	return TRUE;
}

gboolean
e_gdbus_signal_emission_hook_async_boolean (GSignalInvocationHint *ihint,
                                            guint n_param_values,
                                            const GValue *param_values,
                                            const gchar *signal_name,
                                            const gchar *iface_name)
{
	GObject *object;
	GDBusConnection *connection;
	const gchar *path;
	GVariant *params;
	GVariant *item;
	GVariantBuilder *builder;
	GError *arg_error;

	if (n_param_values < 1 || !G_VALUE_HOLDS (&param_values[0], G_TYPE_OBJECT))
		return FALSE;

	object = g_value_get_object (&param_values[0]);
	path = g_object_get_data (object, "gdbus-codegen-path");
	connection = g_object_get_data (object, "gdbus-codegen-connection");
	if (connection == NULL || path == NULL)
		return FALSE;

	builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
	g_assert_cmpint (n_param_values - 1, ==, 3);
	param_values++;
	item = g_variant_new_uint32 (g_value_get_uint (param_values));
	g_variant_builder_add_value (builder, item);
	param_values++;
	arg_error = g_value_get_boxed (param_values);
	if (arg_error) {
		gchar *dbus_error_name = g_dbus_error_encode_gerror (arg_error);
		item = g_variant_new_string (dbus_error_name ? dbus_error_name : "");
		g_variant_builder_add_value (builder, item);
		item = g_variant_new_string (arg_error->message);
		g_variant_builder_add_value (builder, item);
		g_free (dbus_error_name);

		/* fake value for easier processing in e_gdbus_proxy_emit_signal() */
		item = g_variant_new_boolean (FALSE);
		g_variant_builder_add_value (builder, item);
	} else {
		item = g_variant_new_string ("");
		g_variant_builder_add_value (builder, item);
		item = g_variant_new_string ("");
		g_variant_builder_add_value (builder, item);

		param_values++;
		item = g_variant_new_boolean (g_value_get_boolean (param_values));
		g_variant_builder_add_value (builder, item);
		param_values++;
	}
	params = g_variant_builder_end (builder);
	g_variant_builder_unref (builder);

	g_dbus_connection_emit_signal (connection, NULL, path, iface_name, signal_name, params, NULL);

	return TRUE;
}

gboolean
e_gdbus_signal_emission_hook_async_string (GSignalInvocationHint *ihint,
                                           guint n_param_values,
                                           const GValue *param_values,
                                           const gchar *signal_name,
                                           const gchar *iface_name)
{
	GObject *object;
	GDBusConnection *connection;
	const gchar *path;
	GVariant *params;
	GVariant *item;
	GVariantBuilder *builder;
	GError *arg_error;

	if (n_param_values < 1 || !G_VALUE_HOLDS (&param_values[0], G_TYPE_OBJECT))
		return FALSE;

	object = g_value_get_object (&param_values[0]);
	path = g_object_get_data (object, "gdbus-codegen-path");
	connection = g_object_get_data (object, "gdbus-codegen-connection");
	if (connection == NULL || path == NULL)
		return FALSE;

	builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
	g_assert_cmpint (n_param_values - 1, ==, 3);
	param_values++;
	item = g_variant_new_uint32 (g_value_get_uint (param_values));
	g_variant_builder_add_value (builder, item);
	param_values++;
	arg_error = g_value_get_boxed (param_values);
	if (arg_error) {
		gchar *dbus_error_name = g_dbus_error_encode_gerror (arg_error);
		item = g_variant_new_string (dbus_error_name ? dbus_error_name : "");
		g_variant_builder_add_value (builder, item);
		item = g_variant_new_string (arg_error->message);
		g_variant_builder_add_value (builder, item);
		g_free (dbus_error_name);

		/* fake value for easier processing in e_gdbus_proxy_emit_signal() */
		item = g_variant_new_string ("");
		g_variant_builder_add_value (builder, item);
	} else {
		item = g_variant_new_string ("");
		g_variant_builder_add_value (builder, item);
		item = g_variant_new_string ("");
		g_variant_builder_add_value (builder, item);

		param_values++;
		item = g_variant_new_string (g_value_get_string (param_values));
		g_variant_builder_add_value (builder, item);
		param_values++;
	}
	params = g_variant_builder_end (builder);
	g_variant_builder_unref (builder);

	g_dbus_connection_emit_signal (connection, NULL, path, iface_name, signal_name, params, NULL);

	return TRUE;
}

gboolean
e_gdbus_signal_emission_hook_async_strv (GSignalInvocationHint *ihint,
                                         guint n_param_values,
                                         const GValue *param_values,
                                         const gchar *signal_name,
                                         const gchar *iface_name)
{
	GObject *object;
	GDBusConnection *connection;
	const gchar *path;
	GVariant *params;
	GVariant *item;
	GVariantBuilder *builder;
	const GError *arg_error;
	const gchar * const *arg_strv;

	if (n_param_values < 1 || !G_VALUE_HOLDS (&param_values[0], G_TYPE_OBJECT))
		return FALSE;

	object = g_value_get_object (&param_values[0]);
	path = g_object_get_data (object, "gdbus-codegen-path");
	connection = g_object_get_data (object, "gdbus-codegen-connection");
	if (connection == NULL || path == NULL)
		return FALSE;

	builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
	g_assert_cmpint (n_param_values - 1, ==, 3);
	param_values++;
	item = g_variant_new_uint32 (g_value_get_uint (param_values));
	g_variant_builder_add_value (builder, item);
	param_values++;
	arg_error = g_value_get_boxed (param_values);
	if (arg_error) {
		const gchar *fake_strv;
		gchar *dbus_error_name = g_dbus_error_encode_gerror (arg_error);
		item = g_variant_new_string (dbus_error_name ? dbus_error_name : "");
		g_variant_builder_add_value (builder, item);
		item = g_variant_new_string (arg_error->message);
		g_variant_builder_add_value (builder, item);
		g_free (dbus_error_name);

		/* fake value for easier processing in e_gdbus_proxy_emit_signal() */
		fake_strv = NULL;
		item = g_variant_new_strv (&fake_strv, -1);
		g_variant_builder_add_value (builder, item);
	} else {
		item = g_variant_new_string ("");
		g_variant_builder_add_value (builder, item);
		item = g_variant_new_string ("");
		g_variant_builder_add_value (builder, item);

		param_values++;
		arg_strv = g_value_get_boxed (param_values);
		item = g_variant_new_strv (arg_strv, -1);
		g_variant_builder_add_value (builder, item);
		param_values++;
	}
	params = g_variant_builder_end (builder);
	g_variant_builder_unref (builder);

	g_dbus_connection_emit_signal (connection, NULL, path, iface_name, signal_name, params, NULL);

	return TRUE;
}

gboolean
e_gdbus_signal_emission_hook_async_uint (GSignalInvocationHint *ihint,
                                         guint n_param_values,
                                         const GValue *param_values,
                                         const gchar *signal_name,
                                         const gchar *iface_name)
{
	GObject *object;
	GDBusConnection *connection;
	const gchar *path;
	GVariant *params;
	GVariant *item;
	GVariantBuilder *builder;
	GError *arg_error;

	if (n_param_values < 1 || !G_VALUE_HOLDS (&param_values[0], G_TYPE_OBJECT))
		return FALSE;

	object = g_value_get_object (&param_values[0]);
	path = g_object_get_data (object, "gdbus-codegen-path");
	connection = g_object_get_data (object, "gdbus-codegen-connection");
	if (connection == NULL || path == NULL)
		return FALSE;

	builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
	g_assert_cmpint (n_param_values - 1, ==, 3);
	param_values++;
	item = g_variant_new_uint32 (g_value_get_uint (param_values));
	g_variant_builder_add_value (builder, item);
	param_values++;
	arg_error = g_value_get_boxed (param_values);
	if (arg_error) {
		gchar *dbus_error_name = g_dbus_error_encode_gerror (arg_error);
		item = g_variant_new_string (dbus_error_name ? dbus_error_name : "");
		g_variant_builder_add_value (builder, item);
		item = g_variant_new_string (arg_error->message);
		g_variant_builder_add_value (builder, item);
		g_free (dbus_error_name);

		/* fake value for easier processing in e_gdbus_proxy_emit_signal() */
		item = g_variant_new_uint32 (g_value_get_uint (0));
		g_variant_builder_add_value (builder, item);
	} else {
		item = g_variant_new_string ("");
		g_variant_builder_add_value (builder, item);
		item = g_variant_new_string ("");
		g_variant_builder_add_value (builder, item);

		param_values++;
		item = g_variant_new_uint32 (g_value_get_uint (param_values));
		g_variant_builder_add_value (builder, item);
		param_values++;
	}
	params = g_variant_builder_end (builder);
	g_variant_builder_unref (builder);

	g_dbus_connection_emit_signal (connection, NULL, path, iface_name, signal_name, params, NULL);

	return TRUE;
}

void
e_gdbus_proxy_emit_signal (GDBusProxy *proxy,
                           GVariant *parameters,
                           guint signal_id,
                           guint signal_type)
{
	gboolean arg_boolean = FALSE;
	const gchar *arg_const_string = NULL;
	const gchar **arg_const_strv = NULL;
	guint arg_uint = 0;

	g_return_if_fail (proxy != NULL);

	if ((signal_type & E_GDBUS_TYPE_IS_ASYNC) != 0) {
		/* the signal is a done signal, thus opid and error name with error message are first two parameters */
		guint arg_opid = 0;
		const gchar *dbus_error_name = NULL, *dbus_error_message = NULL;
		GError *arg_error = NULL;

		signal_type = signal_type & (~E_GDBUS_TYPE_IS_ASYNC);
		switch (signal_type) {
		case E_GDBUS_TYPE_VOID:
			g_variant_get (parameters, "(u&s&s)", &arg_opid, &dbus_error_name, &dbus_error_message);
			break;
		case E_GDBUS_TYPE_BOOLEAN:
			g_variant_get (parameters, "(u&s&sb)", &arg_opid, &dbus_error_name, &dbus_error_message, &arg_boolean);
			break;
		case E_GDBUS_TYPE_STRING:
			g_variant_get (parameters, "(u&s&s&s)", &arg_opid, &dbus_error_name, &dbus_error_message, &arg_const_string);
			break;
		case E_GDBUS_TYPE_STRV:
			/* array is newly allocated, but items are gvariant's */
			g_variant_get (parameters, "(u&s&s^a&s)", &arg_opid, &dbus_error_name, &dbus_error_message, &arg_const_strv);
			break;
		case E_GDBUS_TYPE_UINT:
			g_variant_get (parameters, "(u&s&su)", &arg_opid, &dbus_error_name, &dbus_error_message, &arg_uint);
			break;
		default:
			/* fix below too, if this is reached */
			g_warning ("%s: Unknown E_GDBUS_TYPE %x", G_STRFUNC, signal_type);
			return;
		}

		if (dbus_error_name && *dbus_error_name && dbus_error_message)
			arg_error = g_dbus_error_new_for_dbus_error (dbus_error_name, dbus_error_message);

		switch (signal_type) {
		case E_GDBUS_TYPE_VOID:
			g_signal_emit (proxy, signal_id, 0, arg_opid, arg_error);
			break;
		case E_GDBUS_TYPE_BOOLEAN:
			g_signal_emit (proxy, signal_id, 0, arg_opid, arg_error, arg_boolean);
			break;
		case E_GDBUS_TYPE_STRING:
			g_signal_emit (proxy, signal_id, 0, arg_opid, arg_error, arg_const_string);
			break;
		case E_GDBUS_TYPE_STRV:
			g_signal_emit (proxy, signal_id, 0, arg_opid, arg_error, arg_const_strv);
			g_free (arg_const_strv);
			break;
		case E_GDBUS_TYPE_UINT:
			g_signal_emit (proxy, signal_id, 0, arg_opid, arg_error, arg_uint);
			break;
		}

		if (arg_error)
			g_error_free (arg_error);
	} else {
		switch (signal_type) {
		case E_GDBUS_TYPE_VOID:
			g_signal_emit (proxy, signal_id, 0);
			break;
		case E_GDBUS_TYPE_BOOLEAN:
			g_variant_get (parameters, "(b)", &arg_boolean);
			g_signal_emit (proxy, signal_id, 0, arg_boolean);
			break;
		case E_GDBUS_TYPE_STRING:
			g_variant_get (parameters, "(&s)", &arg_const_string);
			g_signal_emit (proxy, signal_id, 0, arg_const_string);
			break;
		case E_GDBUS_TYPE_STRV:
			/* array is newly allocated, but items are gvariant's */
			g_variant_get (parameters, "(^a&s)", &arg_const_strv);
			g_signal_emit (proxy, signal_id, 0, arg_const_strv);
			g_free (arg_const_strv);
			break;
		case E_GDBUS_TYPE_UINT:
			g_variant_get (parameters, "(u)", &arg_uint);
			g_signal_emit (proxy, signal_id, 0, arg_uint);
			break;
		case E_GDBUS_TYPE_UINT | E_GDBUS_TYPE_STRING:
			g_variant_get (parameters, "(u&s)", &arg_uint, &arg_const_string);
			g_signal_emit (proxy, signal_id, 0, arg_uint, arg_const_string);
			break;
		default:
			g_warning ("%s: Unknown E_GDBUS_TYPE %x", G_STRFUNC, signal_type);
			break;
		}
	}
}

void
e_gdbus_stub_handle_method_call (GObject *stub_object,
                                 GDBusMethodInvocation *invocation,
                                 GVariant *parameters,
                                 const gchar *method_name,
                                 guint method_id,
                                 guint method_type)
{
	gboolean handled = FALSE;
	gboolean arg_boolean = FALSE;
	const gchar *arg_const_string = NULL;
	const gchar ** arg_const_strv = NULL;
	guint arg_uint = 0;

	g_return_if_fail (stub_object != NULL);
	g_return_if_fail (method_name != NULL);

	switch (method_type & (~E_GDBUS_TYPE_IS_ASYNC)) {
	case E_GDBUS_TYPE_VOID:
		g_signal_emit (stub_object, method_id, 0, invocation, &handled);
		break;
	case E_GDBUS_TYPE_BOOLEAN:
		g_variant_get (parameters, "(b)", &arg_boolean);
		g_signal_emit (stub_object, method_id, 0, invocation, arg_boolean, &handled);
		break;
	case E_GDBUS_TYPE_STRING:
		g_variant_get (parameters, "(&s)", &arg_const_string);
		g_signal_emit (stub_object, method_id, 0, invocation, arg_const_string, &handled);
		break;
	case E_GDBUS_TYPE_STRV:
		/* array is newly allocated, but items are gvariant's */
		g_variant_get (parameters, "(^a&s)", &arg_const_strv);
		g_signal_emit (stub_object, method_id, 0, invocation, arg_const_strv, &handled);
		g_free (arg_const_strv);
		break;
	case E_GDBUS_TYPE_UINT:
		g_variant_get (parameters, "(u)", &arg_uint);
		g_signal_emit (stub_object, method_id, 0, invocation, arg_uint, &handled);
		break;
	default:
		g_warning ("%s: Unknown E_GDBUS_TYPE %x", G_STRFUNC, method_type);
		break;
	}

	if (!handled)
	      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "Method `%s' is not implemented", method_name);
}

G_DEFINE_INTERFACE (EGdbusAsyncOpKeeper, e_gdbus_async_op_keeper, G_TYPE_OBJECT)

static void
e_gdbus_async_op_keeper_default_init (EGdbusAsyncOpKeeperInterface *iface)
{
}

/**
 * e_gdbus_async_op_keeper_create_pending_ops:
 * @object: a #EGdbusAsyncOpKeeper
 *
 * Create a hash table of pending async operations. This can be freed
 * with g_hash_table_unref() in dispose. The interface asks for this
 * pointer by calling e_gdbus_async_op_keeper_create_pending_ops().
 *
 * Returns: (transfer full) (element-type gpointer gpointer): hash table of
 * pending async operations; free with g_hash_table_unref()
 */
GHashTable *
e_gdbus_async_op_keeper_create_pending_ops (EGdbusAsyncOpKeeper *object)
{
	g_return_val_if_fail (object != NULL, NULL);
	g_return_val_if_fail (E_IS_GDBUS_ASYNC_OP_KEEPER (object), NULL);

	return g_hash_table_new (g_direct_hash, g_direct_equal);
}

/**
 * e_gdbus_async_op_keeper_get_pending_ops:
 * @object: a #EGdbusAsyncOpKeeper
 *
 * Get the hash table of pending async operations previously created
 * by e_gdbus_async_op_keeper_create_pending_ops().
 *
 * Returns: (transfer none): hash table of pending async operations
 */
GHashTable *
e_gdbus_async_op_keeper_get_pending_ops (EGdbusAsyncOpKeeper *object)
{
	EGdbusAsyncOpKeeperInterface *iface;

	g_return_val_if_fail (E_IS_GDBUS_ASYNC_OP_KEEPER (object), NULL);

	iface = E_GDBUS_ASYNC_OP_KEEPER_GET_IFACE (object);
	g_return_val_if_fail (iface->get_pending_ops != NULL, NULL);

	return iface->get_pending_ops (object);
}

/* synchronously cancels one operation - sends a request from client to the server */
gboolean
e_gdbus_async_op_keeper_cancel_op_sync (EGdbusAsyncOpKeeper *object,
                                        guint in_opid,
                                        GCancellable *cancellable,
                                        GError **error)
{
	EGdbusAsyncOpKeeperInterface *iface;

	g_return_val_if_fail (E_IS_GDBUS_ASYNC_OP_KEEPER (object), FALSE);

	iface = E_GDBUS_ASYNC_OP_KEEPER_GET_IFACE (object);
	g_return_val_if_fail (iface->cancel_op_sync != NULL, FALSE);

	return iface->cancel_op_sync (object, in_opid, cancellable, error);
}

/* Used to finish asynchronous GDBus call - this might be done in the callback
 * as soon as possible; method returns to a caller operation ID which was started */
void
e_gdbus_complete_async_method (gpointer object,
                               GDBusMethodInvocation *invocation,
                               guint opid)
{
	g_dbus_method_invocation_return_value (invocation, g_variant_new ("(u)", opid));
}

/* Used to finish synchronous GDBus call - this might be done in the callback
 * as soon as possible */
void
e_gdbus_complete_sync_method_void (gpointer object,
                                   GDBusMethodInvocation *invocation,
                                   const GError *error)
{
	if (error)
		g_dbus_method_invocation_return_gerror (invocation, error);
	else
		g_dbus_method_invocation_return_value (invocation, NULL);
}

void
e_gdbus_complete_sync_method_boolean (gpointer object,
                                      GDBusMethodInvocation *invocation,
                                      gboolean out_boolean,
                                      const GError *error)
{
	if (error)
		g_dbus_method_invocation_return_gerror (invocation, error);
	else
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", out_boolean));
}

void
e_gdbus_complete_sync_method_string (gpointer object,
                                     GDBusMethodInvocation *invocation,
                                     const gchar *out_string,
                                     const GError *error)
{
	if (error)
		g_dbus_method_invocation_return_gerror (invocation, error);
	else
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", out_string));
}

void
e_gdbus_complete_sync_method_strv (gpointer object,
                                   GDBusMethodInvocation *invocation,
                                   const gchar * const *out_strv,
                                   const GError *error)
{
	if (error)
		g_dbus_method_invocation_return_gerror (invocation, error);
	else
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(^as)", out_strv));
}

void
e_gdbus_complete_sync_method_uint (gpointer object,
                                   GDBusMethodInvocation *invocation,
                                   guint out_uint,
                                   const GError *error)
{
	if (error)
		g_dbus_method_invocation_return_gerror (invocation, error);
	else
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(u)", out_uint));
}

typedef struct _AsyncOpData
{
	gint ref_count;
	EGdbusAsyncOpKeeper *proxy;
	guint opid;

	GCancellable *cancellable;
	gulong cancel_id;
	guint cancel_idle_id;

	gpointer async_source_tag;
	GAsyncReadyCallback async_callback;
	gpointer async_user_data;

	guint result_type; /* any of E_GDBUS_TYPE_... except of E_GDBUS_TYPE_IS_ASYNC */
	union {
		gboolean out_boolean;
		gchar *out_string;
		gchar ** out_strv;
		guint out_uint;
	} result;
} AsyncOpData;

static void
async_op_data_free (AsyncOpData *op_data)
{
	GHashTable *pending_ops;

	g_return_if_fail (op_data != NULL);

	pending_ops = e_gdbus_async_op_keeper_get_pending_ops (op_data->proxy);

	if (op_data->cancel_idle_id) {
		GError *error = NULL;

		g_source_remove (op_data->cancel_idle_id);
		op_data->cancel_idle_id = 0;

		if (pending_ops)
			g_hash_table_remove (pending_ops, GUINT_TO_POINTER (op_data->opid));

		if (!e_gdbus_async_op_keeper_cancel_op_sync (op_data->proxy, op_data->opid, NULL, &error)) {
			g_debug ("%s: Failed to cancel operation: %s\n", G_STRFUNC, error ? error->message : "Unknown error");
			g_clear_error (&error);
		}
	} else if (pending_ops) {
		g_hash_table_remove (pending_ops, GUINT_TO_POINTER (op_data->opid));
	}

	if (op_data->cancellable) {
		if (op_data->cancel_id) {
			g_cancellable_disconnect (op_data->cancellable, op_data->cancel_id);
			op_data->cancel_id = 0;
		}
		g_object_unref (op_data->cancellable);
		op_data->cancellable = NULL;
	}

	if (!g_atomic_int_dec_and_test (&op_data->ref_count))
		return;

	g_object_unref (op_data->proxy);

	switch (op_data->result_type) {
	case E_GDBUS_TYPE_STRING:
		if (op_data->result.out_string)
			g_free (op_data->result.out_string);
		break;
	case E_GDBUS_TYPE_STRV:
		if (op_data->result.out_strv)
			g_strfreev (op_data->result.out_strv);
		break;
	}

	g_free (op_data);

	g_return_if_fail (pending_ops != NULL);
}

static void
async_op_complete (AsyncOpData *op_data,
                   const GError *error,
                   gboolean in_idle)
{
	GSimpleAsyncResult *simple;

	g_return_if_fail (op_data != NULL);

	g_atomic_int_inc (&op_data->ref_count);
	simple = g_simple_async_result_new (G_OBJECT (op_data->proxy), op_data->async_callback, op_data->async_user_data, op_data->async_source_tag);
	g_simple_async_result_set_op_res_gpointer (simple, op_data, (GDestroyNotify) async_op_data_free);
	if (error)
		g_simple_async_result_set_from_error (simple, error);

	if (in_idle)
		g_simple_async_result_complete_in_idle (simple);
	else
		g_simple_async_result_complete (simple);

	g_object_unref (simple);
}

typedef struct _CancelData
{
	EGdbusAsyncOpKeeper *proxy;
	guint opid;
	AsyncOpData *op_data;
} CancelData;

static void
cancel_data_free (gpointer ptr)
{
	CancelData *cd = ptr;

	if (!cd)
		return;

	g_object_unref (cd->proxy);
	g_free (cd);
}

static gboolean
e_gdbus_op_cancelled_idle_cb (gpointer user_data)
{
	CancelData *cd = user_data;
	AsyncOpData *op_data;
	GHashTable *pending_ops;
	GCancellable *cancellable;
	GError *error = NULL;

	g_return_val_if_fail (cd != NULL, FALSE);

	pending_ops = e_gdbus_async_op_keeper_get_pending_ops (cd->proxy);
	if (pending_ops && !g_hash_table_lookup (pending_ops, GUINT_TO_POINTER (cd->opid))) {
		/* got served already */
		return FALSE;
	}

	op_data = cd->op_data;
	g_return_val_if_fail (op_data != NULL, FALSE);

	cancellable = op_data->cancellable;
	op_data->cancel_idle_id = 0;

	if (!e_gdbus_async_op_keeper_cancel_op_sync (op_data->proxy, op_data->opid, NULL, &error)) {
		g_debug ("%s: Failed to cancel operation: %s\n", G_STRFUNC, error ? error->message : "Unknown error");
		g_clear_error (&error);
	}

	g_return_val_if_fail (g_cancellable_set_error_if_cancelled (cancellable, &error), FALSE);

	async_op_complete (op_data, error, TRUE);
	g_clear_error (&error);

	return FALSE;
}

static void
e_gdbus_op_cancelled_cb (GCancellable *cancellable,
                         AsyncOpData *op_data)
{
	CancelData *cd;

	g_return_if_fail (op_data != NULL);
	g_return_if_fail (op_data->cancellable == cancellable);

	cd = g_new0 (CancelData, 1);
	cd->proxy = g_object_ref (op_data->proxy);
	cd->opid = op_data->opid;
	cd->op_data = op_data;

	/* do this on idle, because this callback should be left
	 * as soon as possible, with no sync calls being done;
	 * also schedule with priority higher than gtk+ uses
	 * for animations (check docs for G_PRIORITY_HIGH_IDLE) */
	op_data->cancel_idle_id = g_idle_add_full (G_PRIORITY_DEFAULT, e_gdbus_op_cancelled_idle_cb, cd, cancel_data_free);
}

static void
e_gdbus_async_call_opid_ready_cb (GObject *source_proxy,
                                  GAsyncResult *result,
                                  gpointer user_data)
{
	GVariant *_result;
	GError *error = NULL;
	AsyncOpData *op_data = user_data;

	_result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_proxy), result, &error);

	if (_result != NULL && !error) {
		EGdbusAsyncOpKeeper *op_keeper = E_GDBUS_ASYNC_OP_KEEPER (source_proxy);
		GHashTable *pending_ops;
		gboolean add_pending = TRUE;

		g_return_if_fail (op_keeper != NULL);

		pending_ops = e_gdbus_async_op_keeper_get_pending_ops (op_keeper);
		g_return_if_fail (pending_ops != NULL);

		g_variant_get (_result, "(u)", &op_data->opid);
		g_variant_unref (_result);

		if (op_data->cancellable && !g_cancellable_set_error_if_cancelled (op_data->cancellable, &error))
			op_data->cancel_id = g_cancellable_connect (op_data->cancellable, G_CALLBACK (e_gdbus_op_cancelled_cb), op_data, NULL);
		else
			add_pending = op_data->cancellable == NULL;

		/* add to pending ops, waiting for associated 'done' signal */
		if (add_pending)
			g_hash_table_insert (pending_ops, GUINT_TO_POINTER (op_data->opid), op_data);
	} else if (_result) {
		g_variant_unref (_result);
	}

	if (error) {
		async_op_complete (op_data, error, FALSE);
		g_error_free (error);
	}
}

static gchar **
copy_strv (const gchar * const *strv)
{
	GPtrArray *array;
	gint ii;

	array = g_ptr_array_sized_new (g_strv_length ((gchar **) strv) + 1);

	for (ii = 0; strv[ii]; ii++) {
		g_ptr_array_add (array, g_strdup (strv[ii]));
	}

	/* NULL-terminated */
	g_ptr_array_add (array, NULL);

	return (gchar **) g_ptr_array_free (array, FALSE);
}

static void
e_gdbus_proxy_async_method_done (guint e_gdbus_type,
                                 gconstpointer out_value,
                                 EGdbusAsyncOpKeeper *object,
                                 guint arg_opid,
                                 const GError *error)
{
	AsyncOpData *op_data;
	GHashTable *pending_ops;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_GDBUS_ASYNC_OP_KEEPER (object));

	pending_ops = e_gdbus_async_op_keeper_get_pending_ops (object);
	g_return_if_fail (pending_ops != NULL);

	op_data = g_hash_table_lookup (pending_ops, GUINT_TO_POINTER (arg_opid));
	if (!op_data) {
		/* it happens for cancelled operations, thus rather than track cancelled ops disable the debug warning */
		/* g_debug ("%s: Operation %d gone before got done signal for it", G_STRFUNC, arg_opid); */
		return;
	}

	if (out_value) {
		op_data->result_type = e_gdbus_type;

		switch (e_gdbus_type) {
		case E_GDBUS_TYPE_VOID:
			break;
		case E_GDBUS_TYPE_BOOLEAN:
			op_data->result.out_boolean = * ((const gboolean *) out_value);
			break;
		case E_GDBUS_TYPE_STRING:
			op_data->result.out_string = g_strdup ((const gchar *) out_value);
			break;
		case E_GDBUS_TYPE_STRV:
			op_data->result.out_strv = copy_strv ((const gchar * const *) out_value);
			break;
		case E_GDBUS_TYPE_UINT:
			op_data->result.out_uint = * ((const guint *) out_value);
			break;
		default:
			g_warning ("%s: Unknown E_GDBUS_TYPE %x", G_STRFUNC, e_gdbus_type);
			break;
		}
	}

	async_op_complete (op_data, error, TRUE);
}

void
e_gdbus_proxy_async_method_done_void (EGdbusAsyncOpKeeper *proxy,
                                      guint arg_opid,
                                      const GError *error)
{
	e_gdbus_proxy_async_method_done (E_GDBUS_TYPE_VOID, NULL, proxy, arg_opid, error);
}

void
e_gdbus_proxy_async_method_done_boolean (EGdbusAsyncOpKeeper *proxy,
                                         guint arg_opid,
                                         const GError *error,
                                         gboolean out_boolean)
{
	e_gdbus_proxy_async_method_done (E_GDBUS_TYPE_BOOLEAN, &out_boolean, proxy, arg_opid, error);
}

/* takes ownership of the out parameter */
void
e_gdbus_proxy_async_method_done_string (EGdbusAsyncOpKeeper *proxy,
                                        guint arg_opid,
                                        const GError *error,
                                        const gchar *out_string)
{
	e_gdbus_proxy_async_method_done (E_GDBUS_TYPE_STRING, out_string, proxy, arg_opid, error);
}

/* takes ownership of the out parameter */
void
e_gdbus_proxy_async_method_done_strv (EGdbusAsyncOpKeeper *proxy,
                                      guint arg_opid,
                                      const GError *error,
                                      const gchar * const *out_strv)
{
	e_gdbus_proxy_async_method_done (E_GDBUS_TYPE_STRV, out_strv, proxy, arg_opid, error);
}

void
e_gdbus_proxy_async_method_done_uint (EGdbusAsyncOpKeeper *proxy,
                                      guint arg_opid,
                                      const GError *error,
                                      guint out_uint)
{
	e_gdbus_proxy_async_method_done (E_GDBUS_TYPE_UINT, &out_uint, proxy, arg_opid, error);
}

/* takes ownership of _params */
static void
e_gdbus_proxy_call_with_params (GVariant *_params,
                                const gchar *method_name,
                                gpointer source_tag,
                                EGdbusAsyncOpKeeper *proxy,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	AsyncOpData *op_data;

	op_data = g_new0 (AsyncOpData, 1);
	op_data->proxy = g_object_ref (proxy);
	op_data->opid = 0;
	op_data->async_source_tag = source_tag;
	op_data->async_callback = callback;
	op_data->async_user_data = user_data;
	op_data->cancellable = cancellable;
	if (op_data->cancellable)
		g_object_ref (op_data->cancellable);

	g_dbus_proxy_call (G_DBUS_PROXY (proxy), method_name, _params, G_DBUS_CALL_FLAGS_NONE, e_data_server_util_get_dbus_call_timeout (), cancellable, e_gdbus_async_call_opid_ready_cb, op_data);
}

void
e_gdbus_proxy_call_void (const gchar *method_name,
                         gpointer source_tag,
                         EGdbusAsyncOpKeeper *proxy,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	e_gdbus_proxy_call_with_params (NULL, method_name, source_tag, proxy, cancellable, callback, user_data);
}

void
e_gdbus_proxy_call_boolean (const gchar *method_name,
                            gpointer source_tag,
                            EGdbusAsyncOpKeeper *proxy,
                            gboolean in_boolean,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	GVariant *_params;

	_params = g_variant_new ("(b)", in_boolean);

	e_gdbus_proxy_call_with_params (_params, method_name, source_tag, proxy, cancellable, callback, user_data);
}

void
e_gdbus_proxy_call_string (const gchar *method_name,
                           gpointer source_tag,
                           EGdbusAsyncOpKeeper *proxy,
                           const gchar *in_string,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	GVariant *_params;

	_params = g_variant_new ("(s)", in_string);

	e_gdbus_proxy_call_with_params (_params, method_name, source_tag, proxy, cancellable, callback, user_data);
}

void
e_gdbus_proxy_call_strv (const gchar *method_name,
                         gpointer source_tag,
                         EGdbusAsyncOpKeeper *proxy,
                         const gchar * const *in_strv,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	GVariant *_params;

	_params = g_variant_new ("(^as)", in_strv);

	e_gdbus_proxy_call_with_params (_params, method_name, source_tag, proxy, cancellable, callback, user_data);
}

void
e_gdbus_proxy_call_uint (const gchar *method_name,
                         gpointer source_tag,
                         EGdbusAsyncOpKeeper *proxy,
                         guint in_uint,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	GVariant *_params;

	_params = g_variant_new ("(u)", in_uint);

	e_gdbus_proxy_call_with_params (_params, method_name, source_tag, proxy, cancellable, callback, user_data);
}

gboolean
e_gdbus_proxy_finish_call_void (EGdbusAsyncOpKeeper *proxy,
                                GAsyncResult *result,
                                GError **error,
                                gpointer source_tag)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (proxy), source_tag), FALSE);

	return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);
}

gboolean
e_gdbus_proxy_finish_call_boolean (EGdbusAsyncOpKeeper *proxy,
                                   GAsyncResult *result,
                                   gboolean *out_boolean,
                                   GError **error,
                                   gpointer source_tag)
{
	AsyncOpData *op_data;

	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (proxy), source_tag), FALSE);
	g_return_val_if_fail (out_boolean != NULL, FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	op_data = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
	g_return_val_if_fail (op_data != NULL, FALSE);
	g_return_val_if_fail (op_data->result_type == E_GDBUS_TYPE_BOOLEAN, FALSE);

	*out_boolean = op_data->result.out_boolean;

	return TRUE;
}

/* caller takes ownership and responsibility for freeing the out parameter */
gboolean
e_gdbus_proxy_finish_call_string (EGdbusAsyncOpKeeper *proxy,
                                  GAsyncResult *result,
                                  gchar **out_string,
                                  GError **error,
                                  gpointer source_tag)
{
	AsyncOpData *op_data;

	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (proxy), source_tag), FALSE);
	g_return_val_if_fail (out_string != NULL, FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	op_data = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
	g_return_val_if_fail (op_data != NULL, FALSE);
	g_return_val_if_fail (op_data->result_type == E_GDBUS_TYPE_STRING, FALSE);

	*out_string = op_data->result.out_string;
	op_data->result.out_string = NULL;

	return TRUE;
}

/* caller takes ownership and responsibility for freeing the out parameter */
gboolean
e_gdbus_proxy_finish_call_strv (EGdbusAsyncOpKeeper *proxy,
                                GAsyncResult *result,
                                gchar ***out_strv,
                                GError **error,
                                gpointer source_tag)
{
	AsyncOpData *op_data;

	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (proxy), source_tag), FALSE);
	g_return_val_if_fail (out_strv != NULL, FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	op_data = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
	g_return_val_if_fail (op_data != NULL, FALSE);
	g_return_val_if_fail (op_data->result_type == E_GDBUS_TYPE_STRV, FALSE);

	*out_strv = op_data->result.out_strv;
	op_data->result.out_strv = NULL;

	return TRUE;
}

gboolean
e_gdbus_proxy_finish_call_uint (EGdbusAsyncOpKeeper *proxy,
                                GAsyncResult *result,
                                guint *out_uint,
                                GError **error,
                                gpointer source_tag)
{
	AsyncOpData *op_data;

	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (proxy), source_tag), FALSE);
	g_return_val_if_fail (out_uint != NULL, FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	op_data = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
	g_return_val_if_fail (op_data != NULL, FALSE);
	g_return_val_if_fail (op_data->result_type == E_GDBUS_TYPE_UINT, FALSE);

	*out_uint = op_data->result.out_uint;

	return TRUE;
}

typedef struct _SyncOpData
{
	EFlag *flag;
	GError **error;

	guint out_type; /* one of E_GDBUS_TYPE_... except of E_GDBUS_TYPE_IS_ASYNC */
	union {
		gboolean *out_boolean;
		gchar **out_string;
		gchar ***out_strv;
		guint *out_uint;
	} out_arg;

	union {
		EGdbusCallFinishVoid finish_void;
		EGdbusCallFinishBoolean finish_boolean;
		EGdbusCallFinishString finish_string;
		EGdbusCallFinishStrv finish_strv;
		EGdbusCallFinishUint finish_uint;
	} finish_func;

	gboolean finish_result;
} SyncOpData;

#define SYNC_DATA_HASH_KEY "EGdbusTemplates-SyncOp-Hash"
static GMutex sync_data_hash_mutex;

static void
e_gdbus_proxy_sync_ready_cb (GObject *proxy,
                             GAsyncResult *result,
                             gpointer user_data)
{
	gint sync_opid = GPOINTER_TO_INT (user_data);
	SyncOpData *sync_data = NULL;
	GHashTable *sync_data_hash;

	g_mutex_lock (&sync_data_hash_mutex);
	sync_data_hash = g_object_get_data (proxy, SYNC_DATA_HASH_KEY);
	if (sync_data_hash)
		sync_data = g_hash_table_lookup (sync_data_hash, GINT_TO_POINTER (sync_opid));
	g_mutex_unlock (&sync_data_hash_mutex);

	if (!sync_data) {
		/* already finished operation; it can happen when the operation is cancelled,
		 * but the result is already waiting in an idle queue.
		*/
		return;
	}

	g_return_if_fail (sync_data->flag != NULL);

	switch (sync_data->out_type) {
	case E_GDBUS_TYPE_VOID:
		g_return_if_fail (sync_data->finish_func.finish_void != NULL);
		sync_data->finish_result = sync_data->finish_func.finish_void (G_DBUS_PROXY (proxy), result, sync_data->error);
		break;
	case E_GDBUS_TYPE_BOOLEAN:
		g_return_if_fail (sync_data->finish_func.finish_boolean != NULL);
		sync_data->finish_result = sync_data->finish_func.finish_boolean (G_DBUS_PROXY (proxy), result, sync_data->out_arg.out_boolean, sync_data->error);
		break;
	case E_GDBUS_TYPE_STRING:
		g_return_if_fail (sync_data->finish_func.finish_string != NULL);
		sync_data->finish_result = sync_data->finish_func.finish_string (G_DBUS_PROXY (proxy), result, sync_data->out_arg.out_string, sync_data->error);
		break;
	case E_GDBUS_TYPE_STRV:
		g_return_if_fail (sync_data->finish_func.finish_strv != NULL);
		sync_data->finish_result = sync_data->finish_func.finish_strv (G_DBUS_PROXY (proxy), result, sync_data->out_arg.out_strv, sync_data->error);
		break;
	case E_GDBUS_TYPE_UINT:
		g_return_if_fail (sync_data->finish_func.finish_uint != NULL);
		sync_data->finish_result = sync_data->finish_func.finish_uint (G_DBUS_PROXY (proxy), result, sync_data->out_arg.out_uint, sync_data->error);
		break;
	default:
		g_warning ("%s: Unknown 'out' E_GDBUS_TYPE %x", G_STRFUNC, sync_data->out_type);
		sync_data->finish_result = FALSE;
	}

	e_flag_set (sync_data->flag);
}

static gboolean
e_gdbus_proxy_call_sync (GDBusProxy *proxy,
                         GCancellable *cancellable,
                         GError **error,
                         gpointer start_func,
                         gpointer finish_func,
                         guint in_type,
                         gconstpointer in_value,
                         guint out_type,
                         gpointer out_value)
{
	static volatile gint sync_op_counter = 0;
	gint sync_opid;
	gpointer sync_opid_ident;
	SyncOpData sync_data = { 0 };
	GHashTable *sync_data_hash;

	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (start_func != NULL, FALSE);
	g_return_val_if_fail (finish_func != NULL, FALSE);

	g_object_ref (proxy);

	switch (out_type) {
	case E_GDBUS_TYPE_VOID:
		sync_data.finish_func.finish_void = finish_func;
		break;
	case E_GDBUS_TYPE_BOOLEAN:
		sync_data.out_arg.out_boolean = out_value;
		sync_data.finish_func.finish_boolean = finish_func;
		break;
	case E_GDBUS_TYPE_STRING:
		sync_data.out_arg.out_string = out_value;
		sync_data.finish_func.finish_string = finish_func;
		break;
	case E_GDBUS_TYPE_STRV:
		sync_data.out_arg.out_strv = out_value;
		sync_data.finish_func.finish_strv = finish_func;
		break;
	case E_GDBUS_TYPE_UINT:
		sync_data.out_arg.out_uint = out_value;
		sync_data.finish_func.finish_uint = finish_func;
		break;
	default:
		g_warning ("%s: Unknown 'out' E_GDBUS_TYPE %x", G_STRFUNC, out_type);
		g_object_unref (proxy);
		return FALSE;
	}

	sync_data.flag = e_flag_new ();
	sync_data.error = error;
	sync_data.out_type = out_type;

	sync_opid = g_atomic_int_add (&sync_op_counter, 1);

	g_mutex_lock (&sync_data_hash_mutex);
	sync_data_hash = g_object_get_data (G_OBJECT (proxy), SYNC_DATA_HASH_KEY);
	if (!sync_data_hash) {
		sync_data_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
		g_object_set_data_full (
			G_OBJECT (proxy), SYNC_DATA_HASH_KEY, sync_data_hash,
			(GDestroyNotify) g_hash_table_destroy);
	}
	sync_opid_ident = GINT_TO_POINTER (sync_opid);
	g_hash_table_insert (sync_data_hash, sync_opid_ident, &sync_data);
	g_mutex_unlock (&sync_data_hash_mutex);

	switch (in_type) {
	case E_GDBUS_TYPE_VOID: {
		EGdbusCallStartVoid start = start_func;
		start (proxy, cancellable, e_gdbus_proxy_sync_ready_cb, GINT_TO_POINTER (sync_opid));
	} break;
	case E_GDBUS_TYPE_BOOLEAN: {
		EGdbusCallStartBoolean start = start_func;
		start (proxy, * ((gboolean *) in_value), cancellable, e_gdbus_proxy_sync_ready_cb, GINT_TO_POINTER (sync_opid));
	} break;
	case E_GDBUS_TYPE_STRING: {
		EGdbusCallStartString start = start_func;
		start (proxy, (const gchar *) in_value, cancellable, e_gdbus_proxy_sync_ready_cb, GINT_TO_POINTER (sync_opid));
	} break;
	case E_GDBUS_TYPE_STRV: {
		EGdbusCallStartStrv start = start_func;
		start (proxy, (const gchar * const *) in_value, cancellable, e_gdbus_proxy_sync_ready_cb, GINT_TO_POINTER (sync_opid));
	} break;
	case E_GDBUS_TYPE_UINT: {
		EGdbusCallStartUint start = start_func;
		start (proxy, * ((guint *) in_value), cancellable, e_gdbus_proxy_sync_ready_cb, GINT_TO_POINTER (sync_opid));
	} break;
	default:
		g_warning ("%s: Unknown 'in' E_GDBUS_TYPE %x", G_STRFUNC, in_type);
		e_flag_free (sync_data.flag);
		g_mutex_lock (&sync_data_hash_mutex);
		g_hash_table_remove (sync_data_hash, sync_opid_ident);
		g_mutex_unlock (&sync_data_hash_mutex);
		g_object_unref (proxy);
		return FALSE;
	}

	/* check if called from the main thread */
	if ((main_thread && main_thread == g_thread_self ()) ||
	    (!main_thread && (g_main_context_is_owner (g_main_context_default ())
	    || g_main_context_default () == g_main_context_get_thread_default ()
	    || !g_main_context_get_thread_default ()))) {
		/* the call to e_gdbus_templates_init_main_thread() wasn't done, but no problem,
		 * check if the call was done in the main thread with main loop running,
		 * and if so, then remember it
		*/
		if (!main_thread && g_main_context_is_owner (g_main_context_default ()))
			e_gdbus_templates_init_main_thread ();

		/* Might not be the best thing here, but as the async operation
		 * is divided into two-step process, invoking the method and
		 * waiting for its "done" signal, then if the sync method is called
		 * from the main thread, then there is probably no other option.
		*/
		while (!e_flag_is_set (sync_data.flag)) {
			g_usleep (1000);
			g_main_context_iteration (NULL, FALSE);
		}
	} else {
		/* is called in a dedicated thread */
		e_flag_wait (sync_data.flag);
	}

	g_mutex_lock (&sync_data_hash_mutex);
	g_hash_table_remove (sync_data_hash, sync_opid_ident);
	g_mutex_unlock (&sync_data_hash_mutex);

	e_flag_free (sync_data.flag);

	g_object_unref (proxy);

	return sync_data.finish_result;
}

gboolean
e_gdbus_proxy_call_sync_void__void (GDBusProxy *proxy,
                                    GCancellable *cancellable,
                                    GError **error,
                                    EGdbusCallStartVoid start_func,
                                    EGdbusCallFinishVoid finish_func)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (start_func != NULL, FALSE);
	g_return_val_if_fail (finish_func != NULL, FALSE);

	return e_gdbus_proxy_call_sync (proxy, cancellable, error, start_func, finish_func, E_GDBUS_TYPE_VOID, NULL, E_GDBUS_TYPE_VOID, NULL);
}

/**
 * e_gdbus_proxy_call_sync_void__boolean:
 * @proxy:
 * @out_boolean:
 * @cancellable: (allow-none):
 * @error:
 * @start_func: (scope call):
 * @finish_func: (scope call):
 *
 * Returns:
 */
gboolean
e_gdbus_proxy_call_sync_void__boolean (GDBusProxy *proxy,
                                       gboolean *out_boolean,
                                       GCancellable *cancellable,
                                       GError **error,
                                       EGdbusCallStartVoid start_func,
                                       EGdbusCallFinishBoolean finish_func)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (start_func != NULL, FALSE);
	g_return_val_if_fail (finish_func != NULL, FALSE);
	g_return_val_if_fail (out_boolean != NULL, FALSE);

	return e_gdbus_proxy_call_sync (proxy, cancellable, error, start_func, finish_func, E_GDBUS_TYPE_VOID, NULL, E_GDBUS_TYPE_BOOLEAN, out_boolean);
}

/**
 * e_gdbus_proxy_call_sync_void__string:
 * @proxy:
 * @out_string:
 * @cancellable: (allow-none):
 * @error:
 * @start_func: (scope call):
 * @finish_func: (scope call):
 *
 * Returns:
 */
gboolean
e_gdbus_proxy_call_sync_void__string (GDBusProxy *proxy,
                                      gchar **out_string,
                                      GCancellable *cancellable,
                                      GError **error,
                                      EGdbusCallStartVoid start_func,
                                      EGdbusCallFinishString finish_func)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (start_func != NULL, FALSE);
	g_return_val_if_fail (finish_func != NULL, FALSE);
	g_return_val_if_fail (out_string != NULL, FALSE);

	return e_gdbus_proxy_call_sync (proxy, cancellable, error, start_func, finish_func, E_GDBUS_TYPE_VOID, NULL, E_GDBUS_TYPE_STRING, out_string);
}

/**
 * e_gdbus_proxy_call_sync_void__strv:
 * @proxy:
 * @out_strv:
 * @cancellable: (allow-none):
 * @error:
 * @start_func: (scope call):
 * @finish_func: (scope call):
 *
 * Returns:
 */
gboolean
e_gdbus_proxy_call_sync_void__strv (GDBusProxy *proxy,
                                    gchar ***out_strv,
                                    GCancellable *cancellable,
                                    GError **error,
                                    EGdbusCallStartVoid start_func,
                                    EGdbusCallFinishStrv finish_func)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (start_func != NULL, FALSE);
	g_return_val_if_fail (finish_func != NULL, FALSE);
	g_return_val_if_fail (out_strv != NULL, FALSE);

	return e_gdbus_proxy_call_sync (proxy, cancellable, error, start_func, finish_func, E_GDBUS_TYPE_VOID, NULL, E_GDBUS_TYPE_STRV, out_strv);
}

/**
 * e_gdbus_proxy_call_sync_void__uint:
 * @proxy:
 * @out_uint:
 * @cancellable: (allow-none):
 * @error:
 * @start_func: (scope call):
 * @finish_func: (scope call):
 *
 * Returns:
 */
gboolean
e_gdbus_proxy_call_sync_void__uint (GDBusProxy *proxy,
                                    guint *out_uint,
                                    GCancellable *cancellable,
                                    GError **error,
                                    EGdbusCallStartVoid start_func,
                                    EGdbusCallFinishUint finish_func)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (start_func != NULL, FALSE);
	g_return_val_if_fail (finish_func != NULL, FALSE);
	g_return_val_if_fail (out_uint != NULL, FALSE);

	return e_gdbus_proxy_call_sync (proxy, cancellable, error, start_func, finish_func, E_GDBUS_TYPE_VOID, NULL, E_GDBUS_TYPE_UINT, out_uint);
}

gboolean
e_gdbus_proxy_call_sync_boolean__void (GDBusProxy *proxy,
                                       gboolean in_boolean,
                                       GCancellable *cancellable,
                                       GError **error,
                                       EGdbusCallStartBoolean start_func,
                                       EGdbusCallFinishVoid finish_func)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (start_func != NULL, FALSE);
	g_return_val_if_fail (finish_func != NULL, FALSE);

	return e_gdbus_proxy_call_sync (proxy, cancellable, error, start_func, finish_func, E_GDBUS_TYPE_BOOLEAN, &in_boolean, E_GDBUS_TYPE_VOID, NULL);
}

/**
 * e_gdbus_proxy_call_sync_string__void:
 * @proxy:
 * @in_string:
 * @cancellable: (allow-none):
 * @error:
 * @start_func: (scope call):
 * @finish_func: (scope call):
 *
 * Returns:
 */
gboolean
e_gdbus_proxy_call_sync_string__void (GDBusProxy *proxy,
                                      const gchar *in_string,
                                      GCancellable *cancellable,
                                      GError **error,
                                      EGdbusCallStartString start_func,
                                      EGdbusCallFinishVoid finish_func)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (start_func != NULL, FALSE);
	g_return_val_if_fail (finish_func != NULL, FALSE);
	g_return_val_if_fail (in_string != NULL, FALSE);

	return e_gdbus_proxy_call_sync (proxy, cancellable, error, start_func, finish_func, E_GDBUS_TYPE_STRING, in_string, E_GDBUS_TYPE_VOID, NULL);
}

gboolean
e_gdbus_proxy_call_sync_strv__void (GDBusProxy *proxy,
                                    const gchar * const *in_strv,
                                    GCancellable *cancellable,
                                    GError **error,
                                    EGdbusCallStartStrv start_func,
                                    EGdbusCallFinishVoid finish_func)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (start_func != NULL, FALSE);
	g_return_val_if_fail (finish_func != NULL, FALSE);
	g_return_val_if_fail (in_strv != NULL, FALSE);

	return e_gdbus_proxy_call_sync (proxy, cancellable, error, start_func, finish_func, E_GDBUS_TYPE_STRV, in_strv, E_GDBUS_TYPE_VOID, NULL);
}

/**
 * e_gdbus_proxy_call_sync_uint__void:
 * @proxy:
 * @in_uint:
 * @cancellable: (allow-none):
 * @error:
 * @start_func: (scope call):
 * @finish_func: (scope call):
 *
 * Returns:
 */
gboolean
e_gdbus_proxy_call_sync_uint__void (GDBusProxy *proxy,
                                    guint in_uint,
                                    GCancellable *cancellable,
                                    GError **error,
                                    EGdbusCallStartUint start_func,
                                    EGdbusCallFinishVoid finish_func)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (start_func != NULL, FALSE);
	g_return_val_if_fail (finish_func != NULL, FALSE);

	return e_gdbus_proxy_call_sync (proxy, cancellable, error, start_func, finish_func, E_GDBUS_TYPE_UINT, &in_uint, E_GDBUS_TYPE_VOID, NULL);
}

gboolean
e_gdbus_proxy_call_sync_string__string (GDBusProxy *proxy,
                                        const gchar *in_string,
                                        gchar **out_string,
                                        GCancellable *cancellable,
                                        GError **error,
                                        EGdbusCallStartString start_func,
                                        EGdbusCallFinishString finish_func)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (start_func != NULL, FALSE);
	g_return_val_if_fail (finish_func != NULL, FALSE);
	g_return_val_if_fail (in_string != NULL, FALSE);
	g_return_val_if_fail (out_string != NULL, FALSE);

	return e_gdbus_proxy_call_sync (proxy, cancellable, error, start_func, finish_func, E_GDBUS_TYPE_STRING, in_string, E_GDBUS_TYPE_STRING, out_string);
}

/**
 * e_gdbus_proxy_call_sync_string__strv:
 * @proxy:
 * @cancellable: (allow-none):
 * @error:
 * @start_func: (scope call):
 * @finish_func: (scope call):
 *
 * Returns:
 */
gboolean
e_gdbus_proxy_call_sync_string__strv (GDBusProxy *proxy,
                                      const gchar *in_string,
                                      gchar ***out_strv,
                                      GCancellable *cancellable,
                                      GError **error,
                                      EGdbusCallStartString start_func,
                                      EGdbusCallFinishStrv finish_func)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (start_func != NULL, FALSE);
	g_return_val_if_fail (finish_func != NULL, FALSE);
	g_return_val_if_fail (in_string != NULL, FALSE);
	g_return_val_if_fail (out_strv != NULL, FALSE);

	return e_gdbus_proxy_call_sync (proxy, cancellable, error, start_func, finish_func, E_GDBUS_TYPE_STRING, in_string, E_GDBUS_TYPE_STRV, out_strv);
}

gboolean
e_gdbus_proxy_call_sync_strv__string (GDBusProxy *proxy,
                                      const gchar * const *in_strv,
                                      gchar **out_string,
                                      GCancellable *cancellable,
                                      GError **error,
                                      EGdbusCallStartStrv start_func,
                                      EGdbusCallFinishString finish_func)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (start_func != NULL, FALSE);
	g_return_val_if_fail (finish_func != NULL, FALSE);
	g_return_val_if_fail (in_strv != NULL, FALSE);
	g_return_val_if_fail (out_string != NULL, FALSE);

	return e_gdbus_proxy_call_sync (proxy, cancellable, error, start_func, finish_func, E_GDBUS_TYPE_STRV, in_strv, E_GDBUS_TYPE_STRING, out_string);
}

/**
 * e_gdbus_proxy_call_sync_strv__strv:
 * @proxy:
 * @in_strv:
 * @out_strv:
 * @cancellable: (allow-none):
 * @error:
 * @start_func: (scope call):
 * @finish_func: (scope call):
 *
 * Returns:
 */
gboolean
e_gdbus_proxy_call_sync_strv__strv (GDBusProxy *proxy,
                                    const gchar * const *in_strv,
                                    gchar ***out_strv,
                                    GCancellable *cancellable,
                                    GError **error,
                                    EGdbusCallStartStrv start_func,
                                    EGdbusCallFinishStrv finish_func)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (start_func != NULL, FALSE);
	g_return_val_if_fail (finish_func != NULL, FALSE);
	g_return_val_if_fail (in_strv != NULL, FALSE);
	g_return_val_if_fail (out_strv != NULL, FALSE);

	return e_gdbus_proxy_call_sync (proxy, cancellable, error, start_func, finish_func, E_GDBUS_TYPE_STRV, in_strv, E_GDBUS_TYPE_STRV, out_strv);
}

static void
proxy_method_call (const gchar *method_name,
                   guint param_type,
                   gconstpointer param_value,
                   GDBusProxy *proxy,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
	GVariant *params = NULL;
	GVariant *item;
	GVariantBuilder *builder = NULL;

	g_return_if_fail (method_name != NULL);
	g_return_if_fail (proxy != NULL);
	g_return_if_fail (G_IS_DBUS_PROXY (proxy));
	if (param_type != E_GDBUS_TYPE_VOID)
		g_return_if_fail (param_value != NULL);

	switch (param_type) {
	case E_GDBUS_TYPE_VOID:
		break;
	case E_GDBUS_TYPE_BOOLEAN:
		builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
		item = g_variant_new_boolean (* ((const gboolean *) param_value));
		g_variant_builder_add_value (builder, item);
		break;
	case E_GDBUS_TYPE_STRING:
		builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
		item = g_variant_new_string ((const gchar *) param_value);
		g_variant_builder_add_value (builder, item);
		break;
	case E_GDBUS_TYPE_STRV:
		builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
		item = g_variant_new_strv ((const gchar * const *) param_value, -1);
		g_variant_builder_add_value (builder, item);
		break;
	case E_GDBUS_TYPE_UINT:
		builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
		item = g_variant_new_uint32 (* ((const guint *) param_value));
		g_variant_builder_add_value (builder, item);
		break;
	default:
		g_warning ("%s: Unknown 'param' E_GDBUS_TYPE %x", G_STRFUNC, param_type);
		return;
	}

	if (builder != NULL) {
		params = g_variant_builder_end (builder);
		g_variant_builder_unref (builder);
	}

	g_dbus_proxy_call (G_DBUS_PROXY (proxy), method_name, params, G_DBUS_CALL_FLAGS_NONE, e_data_server_util_get_dbus_call_timeout (), cancellable, callback, user_data);
}

void
e_gdbus_proxy_method_call_void (const gchar *method_name,
                                GDBusProxy *proxy,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	proxy_method_call (method_name, E_GDBUS_TYPE_VOID, NULL, proxy, cancellable, callback, user_data);
}

void
e_gdbus_proxy_method_call_boolean (const gchar *method_name,
                                   GDBusProxy *proxy,
                                   gboolean in_boolean,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	proxy_method_call (method_name, E_GDBUS_TYPE_BOOLEAN, &in_boolean, proxy, cancellable, callback, user_data);
}

void
e_gdbus_proxy_method_call_string (const gchar *method_name,
                                  GDBusProxy *proxy,
                                  const gchar *in_string,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	proxy_method_call (method_name, E_GDBUS_TYPE_STRING, in_string, proxy, cancellable, callback, user_data);
}

void
e_gdbus_proxy_method_call_strv (const gchar *method_name,
                                GDBusProxy *proxy,
                                const gchar * const *in_strv,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	proxy_method_call (method_name, E_GDBUS_TYPE_STRV, in_strv, proxy, cancellable, callback, user_data);
}

void
e_gdbus_proxy_method_call_uint (const gchar *method_name,
                                GDBusProxy *proxy,
                                guint in_uint,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	proxy_method_call (method_name, E_GDBUS_TYPE_VOID, &in_uint, proxy, cancellable, callback, user_data);
}

static gboolean
process_result (const gchar *caller_func_name,
                guint out_type,
                gpointer out_value,
                GVariant *_result)
{
	if (out_type != E_GDBUS_TYPE_VOID)
		g_return_val_if_fail (out_value != NULL, FALSE);

	if (_result == NULL)
		return FALSE;

	switch (out_type) {
	case E_GDBUS_TYPE_VOID:
		break;
	case E_GDBUS_TYPE_BOOLEAN:
		g_variant_get (_result, "(b)", (gboolean *) out_value);
		break;
	case E_GDBUS_TYPE_STRING:
		g_variant_get (_result, "(s)", (gchar **) out_value);
		break;
	case E_GDBUS_TYPE_STRV:
		g_variant_get (_result, "(^as)", (gchar ***) out_value);
		break;
	case E_GDBUS_TYPE_UINT:
		g_variant_get (_result, "(u)", (guint *) out_value);
		break;
	default:
		g_warning ("%s: Unknown 'out' E_GDBUS_TYPE %x", caller_func_name ? caller_func_name : G_STRFUNC, out_type);
		break;
	}

	g_variant_unref (_result);

	return TRUE;
}

static gboolean
proxy_method_call_finish (guint out_type,
                          gpointer out_param,
                          GDBusProxy *proxy,
                          GAsyncResult *result,
                          GError **error)
{
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (G_IS_DBUS_PROXY (proxy), FALSE);
	if (out_type != E_GDBUS_TYPE_VOID)
		g_return_val_if_fail (out_param != NULL, FALSE);

	return process_result (G_STRFUNC, out_type, out_param, g_dbus_proxy_call_finish (proxy, result, error));
}

gboolean
e_gdbus_proxy_method_call_finish_void (GDBusProxy *proxy,
                                       GAsyncResult *result,
                                       GError **error)
{
	return proxy_method_call_finish (E_GDBUS_TYPE_VOID, NULL, proxy, result, error);
}

gboolean
e_gdbus_proxy_method_call_finish_boolean (GDBusProxy *proxy,
                                          GAsyncResult *result,
                                          gboolean *out_boolean,
                                          GError **error)
{
	return proxy_method_call_finish (E_GDBUS_TYPE_BOOLEAN, out_boolean, proxy, result, error);
}

gboolean
e_gdbus_proxy_method_call_finish_string (GDBusProxy *proxy,
                                         GAsyncResult *result,
                                         gchar **out_string,
                                         GError **error)
{
	return proxy_method_call_finish (E_GDBUS_TYPE_STRING, out_string, proxy, result, error);
}

gboolean
e_gdbus_proxy_method_call_finish_strv (GDBusProxy *proxy,
                                       GAsyncResult *result,
                                       gchar ***out_strv,
                                       GError **error)
{
	return proxy_method_call_finish (E_GDBUS_TYPE_STRV, out_strv, proxy, result, error);
}

gboolean
e_gdbus_proxy_method_call_finish_uint (GDBusProxy *proxy,
                                       GAsyncResult *result,
                                       guint *out_uint,
                                       GError **error)
{
	return proxy_method_call_finish (E_GDBUS_TYPE_UINT, out_uint, proxy, result, error);
}

static gboolean
proxy_method_call_sync (const gchar *method_name,
                        guint in_type,
                        gconstpointer in_value,
                        guint out_type,
                        gpointer out_value,
                        GDBusProxy *proxy,
                        GCancellable *cancellable,
                        GError **error)
{
	GVariant *params = NULL;
	GVariant *item;
	GVariantBuilder *builder = NULL;

	g_return_val_if_fail (method_name != NULL, FALSE);
	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (G_IS_DBUS_PROXY (proxy), FALSE);
	if (in_type != E_GDBUS_TYPE_VOID)
		g_return_val_if_fail (in_value != NULL, FALSE);
	if (out_type != E_GDBUS_TYPE_VOID)
		g_return_val_if_fail (out_value != NULL, FALSE);

	switch (in_type) {
	case E_GDBUS_TYPE_VOID:
		break;
	case E_GDBUS_TYPE_BOOLEAN:
		builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
		item = g_variant_new_boolean (* ((const gboolean *) in_value));
		g_variant_builder_add_value (builder, item);
		break;
	case E_GDBUS_TYPE_STRING:
		builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
		item = g_variant_new_string ((const gchar *) in_value);
		g_variant_builder_add_value (builder, item);
		break;
	case E_GDBUS_TYPE_STRV:
		builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
		item = g_variant_new_strv ((const gchar * const *) in_value, -1);
		g_variant_builder_add_value (builder, item);
		break;
	case E_GDBUS_TYPE_UINT:
		builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
		item = g_variant_new_uint32 (* ((const guint *) in_value));
		g_variant_builder_add_value (builder, item);
		break;
	default:
		g_warning ("%s: Unknown 'in' E_GDBUS_TYPE %x", G_STRFUNC, in_type);
		return FALSE;
	}

	if (builder != NULL) {
		params = g_variant_builder_end (builder);
		g_variant_builder_unref (builder);
	}

	return process_result (G_STRFUNC, out_type, out_value, g_dbus_proxy_call_sync (G_DBUS_PROXY (proxy), method_name, params, G_DBUS_CALL_FLAGS_NONE, e_data_server_util_get_dbus_call_timeout (), cancellable, error));
}

/**
 * e_gdbus_proxy_call_sync_void__void:
 * @proxy:
 * @cancellable: (allow-none):
 * @error:
 * @start_func: (scope call):
 * @finish_func: (scope call):
 *
 * Returns:
 */
gboolean
e_gdbus_proxy_method_call_sync_void__void (const gchar *method_name,
                                           GDBusProxy *proxy,
                                           GCancellable *cancellable,
                                           GError **error)
{
	return proxy_method_call_sync (method_name, E_GDBUS_TYPE_VOID, NULL, E_GDBUS_TYPE_VOID, NULL, proxy, cancellable, error);
}

/**
 * e_gdbus_proxy_call_sync_boolean__void:
 * @proxy:
 * @in_boolean:
 * @cancellable: (allow-none):
 * @error:
 * @start_func: (scope call):
 * @finish_func: (scope call):
 *
 * Returns:
 */
gboolean
e_gdbus_proxy_method_call_sync_boolean__void (const gchar *method_name,
                                              GDBusProxy *proxy,
                                              gboolean in_boolean,
                                              GCancellable *cancellable,
                                              GError **error)
{
	return proxy_method_call_sync (method_name, E_GDBUS_TYPE_BOOLEAN, &in_boolean, E_GDBUS_TYPE_VOID, NULL, proxy, cancellable, error);
}

gboolean
e_gdbus_proxy_method_call_sync_string__void (const gchar *method_name,
                                             GDBusProxy *proxy,
                                             const gchar *in_string,
                                             GCancellable *cancellable,
                                             GError **error)
{
	return proxy_method_call_sync (method_name, E_GDBUS_TYPE_STRING, in_string, E_GDBUS_TYPE_VOID, NULL, proxy, cancellable, error);
}

/**
 * e_gdbus_proxy_call_sync_strv__void:
 * @proxy:
 * @in_strv:
 * @cancellable: (allow-none):
 * @error:
 * @start_func: (scope call):
 * @finish_func: (scope call):
 *
 * Returns:
 */
gboolean
e_gdbus_proxy_method_call_sync_strv__void (const gchar *method_name,
                                           GDBusProxy *proxy,
                                           const gchar * const *in_strv,
                                           GCancellable *cancellable,
                                           GError **error)
{
	return proxy_method_call_sync (method_name, E_GDBUS_TYPE_STRV, in_strv, E_GDBUS_TYPE_VOID, NULL, proxy, cancellable, error);
}

gboolean
e_gdbus_proxy_method_call_sync_uint__void (const gchar *method_name,
                                           GDBusProxy *proxy,
                                           guint in_uint,
                                           GCancellable *cancellable,
                                           GError **error)
{
	return proxy_method_call_sync (method_name, E_GDBUS_TYPE_UINT, &in_uint, E_GDBUS_TYPE_VOID, NULL, proxy, cancellable, error);
}

/**
 * e_gdbus_proxy_call_sync_string__string:
 * @proxy:
 * @in_string:
 * @out_string:
 * @cancellable: (allow-none):
 * @error:
 * @start_func: (scope call):
 * @finish_func: (scope call):
 *
 * Returns:
 */
gboolean
e_gdbus_proxy_method_call_sync_string__string (const gchar *method_name,
                                               GDBusProxy *proxy,
                                               const gchar *in_string,
                                               gchar **out_string,
                                               GCancellable *cancellable,
                                               GError **error)
{
	return proxy_method_call_sync (method_name, E_GDBUS_TYPE_STRING, in_string, E_GDBUS_TYPE_STRING, out_string, proxy, cancellable, error);
}

/**
 * e_gdbus_proxy_call_sync_strv__string:
 * @proxy:
 * @in_strv:
 * @out_string:
 * @cancellable: (allow-none):
 * @error:
 * @start_func: (scope call):
 * @finish_func: (scope call):
 *
 * Returns:
 */
gboolean
e_gdbus_proxy_method_call_sync_strv__string (const gchar *method_name,
                                             GDBusProxy *proxy,
                                             const gchar * const *in_strv,
                                             gchar **out_string,
                                             GCancellable *cancellable,
                                             GError **error)
{
	return proxy_method_call_sync (method_name, E_GDBUS_TYPE_STRV, in_strv, E_GDBUS_TYPE_STRING, out_string, proxy, cancellable, error);
}

/**
 * e_gdbus_templates_encode_error:
 * @in_error: (allow-none):
 *
 * Returns: (transfer full): a %NULL-terminated array of strings; free with
 * g_strfreev()
 */
gchar **
e_gdbus_templates_encode_error (const GError *in_error)
{
	gchar **strv;

	strv = g_new0 (gchar *, 3);

	if (!in_error) {
		strv[0] = g_strdup ("");
		strv[1] = g_strdup ("");
	} else {
		gchar *dbus_error_name = g_dbus_error_encode_gerror (in_error);

		strv[0] = e_util_utf8_make_valid (dbus_error_name ? dbus_error_name : "");
		strv[1] = e_util_utf8_make_valid (in_error->message);

		g_free (dbus_error_name);
	}

	return strv;
}

/* free *out_error with g_error_free(), if not NULL */
gboolean
e_gdbus_templates_decode_error (const gchar * const *in_strv,
                                GError **out_error)
{
	const gchar *error_name, *error_message;

	g_return_val_if_fail (out_error != NULL, FALSE);

	*out_error = NULL;

	g_return_val_if_fail (in_strv != NULL, FALSE);
	g_return_val_if_fail (in_strv[0] != NULL, FALSE);
	g_return_val_if_fail (in_strv[1] != NULL, FALSE);
	g_return_val_if_fail (in_strv[2] == NULL, FALSE);

	error_name = in_strv[0];
	error_message = in_strv[1];

	if (error_name && *error_name && error_message)
		*out_error = g_dbus_error_new_for_dbus_error (error_name, error_message);

	return TRUE;
}

/**
 * e_gdbus_templates_encode_two_strings:
 * @in_str1: (allow-none):
 * @in_str2: (allow-none):
 *
 * Returns: (transfer full): a %NULL-terminated array of strings; free with
 * g_strfreev()
 */
gchar **
e_gdbus_templates_encode_two_strings (const gchar *in_str1,
                                      const gchar *in_str2)
{
	gchar **strv;

	strv = g_new0 (gchar *, 3);
	strv[0] = e_util_utf8_make_valid (in_str1 ? in_str1 : "");
	strv[1] = e_util_utf8_make_valid (in_str2 ? in_str2 : "");
	strv[2] = NULL;

	return strv;
}

/* free *out_str1 and *out_str2 with g_free() */
gboolean
e_gdbus_templates_decode_two_strings (const gchar * const *in_strv,
                                      gchar **out_str1,
                                      gchar **out_str2)
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

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2018 Red Hat, Inc. (www.redhat.com)
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-data-server-config.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>

#define DBUS_CALL_TIMEOUT -1 /* as set by D-Bus settings */

static void
object_manager_object_added_cb (GDBusObjectManager *manager,
				GDBusObject *object,
				gpointer user_data);
static void
object_manager_object_removed_cb (GDBusObjectManager *manager,
				  GDBusObject *object,
				  gpointer user_data);

typedef struct _ProxyData {
	gchar *iface_name;
	gchar *path;

	/* The original D-Bus session part */
	GDBusProxy *proxy;
	GDBusObjectManager *object_manager;
	GSList *sigids; /* GUINT_TO_POINTER(sigid) */

	/* The new D-Bus session part */
	GDBusConnection *regs_connection;
	guint ownid;
	GSList *regids; /* GUINT_TO_POINTER(regid) */
	GSList *object_manager_pds; /* ProxyData *, when object_manager is not NULL */
} ProxyData;

static void
proxy_data_free (gpointer ptr)
{
	ProxyData *pd = ptr;

	if (pd) {
		if (pd->object_manager)
			g_signal_handlers_disconnect_by_data (pd->object_manager, pd);

		g_slist_free_full (pd->object_manager_pds, proxy_data_free);

		if (pd->regs_connection && !g_dbus_connection_is_closed (pd->regs_connection)) {
			GSList *link;

			for (link = pd->regids; link; link = g_slist_next (link)) {
				guint regid = GPOINTER_TO_UINT (link->data);

				g_dbus_connection_unregister_object (pd->regs_connection, regid);
			}

			if (pd->object_manager) {
				GList *objects, *llink;

				objects = g_dbus_object_manager_get_objects (pd->object_manager);

				for (llink = objects; llink; llink = g_list_next (llink)) {
					GDBusObject *object = llink->data;

					object_manager_object_removed_cb (pd->object_manager, object, pd);
				}

				g_list_free_full (objects, g_object_unref);
			}
		}

		if (pd->ownid) {
			g_bus_unown_name (pd->ownid);
			pd->ownid = 0;
		}

		if (pd->proxy && pd->sigids) {
			GDBusConnection *connection;

			connection = g_dbus_proxy_get_connection (pd->proxy);

			if (connection && !g_dbus_connection_is_closed (connection)) {
				GSList *link;

				for (link = pd->sigids; link; link = g_slist_next (link)) {
					guint sigid = GPOINTER_TO_UINT (link->data);

					g_dbus_connection_signal_unsubscribe (connection, sigid);
				}
			}
		}

		g_clear_object (&pd->proxy);
		g_clear_object (&pd->object_manager);
		g_clear_object (&pd->regs_connection);
		g_slist_free (pd->regids);
		g_slist_free (pd->sigids);
		g_free (pd->iface_name);
		g_free (pd->path);
		g_free (pd);
	}
}

/* encoded_string :== iface_name + ":" + path */
static ProxyData *
proxy_data_new (const gchar *encoded_string)
{
	ProxyData *pd;
	gchar **strv;
	GError *error = NULL;

	strv = g_strsplit (encoded_string, ":", -1);
	g_return_val_if_fail (strv != NULL, NULL);
	if (!strv[0] || !strv[1] || strv[2]) {
		g_warning ("Unexpected proxy data string format: '%s'", encoded_string);
		g_strfreev (strv);
		return NULL;
	}

	pd = g_new0 (ProxyData, 1);
	pd->iface_name = g_strdup (strv[0]); /* like "org.freedesktop.portal.Desktop" */
	pd->path = g_strdup (strv[1]); /* like "/org/freedesktop/portal/desktop" */
	pd->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, NULL,
		pd->iface_name, pd->path, pd->iface_name, NULL, &error);

	if (!pd->proxy) {
		g_warning ("Failed to open proxy for interface '%s' at path '%s': %s", pd->iface_name, pd->path, error ? error->message : "Unknown error");
		proxy_data_free (pd);
		pd = NULL;
	}

	g_clear_error (&error);
	g_strfreev (strv);

	return pd;
}

static void
handle_method_call_cb (GDBusConnection *connection,
		       const gchar *sender,
		       const gchar *object_path,
		       const gchar *interface_name,
		       const gchar *method_name,
		       GVariant *parameters,
		       GDBusMethodInvocation *invocation,
		       gpointer user_data)
{
	ProxyData *pd = user_data;
	GVariant *result;
	GError *error = NULL;

	g_return_if_fail (pd != NULL);

	result = g_dbus_connection_call_sync (g_dbus_proxy_get_connection (pd->proxy),
		g_dbus_proxy_get_name (pd->proxy),
		g_dbus_proxy_get_object_path (pd->proxy),
		interface_name,
		method_name,
		parameters,
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		DBUS_CALL_TIMEOUT,
		NULL, &error);

	if (result) {
		g_dbus_method_invocation_return_value (invocation, result);
		g_variant_unref (result);
	} else {
		g_dbus_method_invocation_return_gerror (invocation, error);
	}

	g_clear_error (&error);
}

static GVariant *
handle_get_property_cb (GDBusConnection *connection,
			const gchar *sender,
			const gchar *object_path,
			const gchar *interface_name,
			const gchar *property_name,
			GError **error,
			gpointer user_data)
{
	ProxyData *pd = user_data;

	g_return_val_if_fail (pd != NULL, NULL);

	return g_dbus_connection_call_sync (g_dbus_proxy_get_connection (pd->proxy),
		g_dbus_proxy_get_name (pd->proxy),
		g_dbus_proxy_get_object_path (pd->proxy),
		"org.freedesktop.DBus.Properties",
		"Get",
		g_variant_new ("(ss)", interface_name, property_name),
		G_VARIANT_TYPE ("(s)"),
		G_DBUS_CALL_FLAGS_NONE,
		DBUS_CALL_TIMEOUT,
		NULL, error);
}

static gboolean
handle_set_property_cb (GDBusConnection *connection,
			const gchar *sender,
			const gchar *object_path,
			const gchar *interface_name,
			const gchar *property_name,
			GVariant *value,
			GError **error,
			gpointer user_data)
{
	ProxyData *pd = user_data;
	GVariant *result;
	GError *local_error = NULL;

	g_return_val_if_fail (pd != NULL, FALSE);

	result = g_dbus_connection_call_sync (g_dbus_proxy_get_connection (pd->proxy),
		g_dbus_proxy_get_name (pd->proxy),
		g_dbus_proxy_get_object_path (pd->proxy),
		"org.freedesktop.DBus.Properties",
		"Set",
		g_variant_new ("(ssv)", interface_name, property_name, value),
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		DBUS_CALL_TIMEOUT,
		NULL, &local_error);

	if (result)
		g_variant_unref (result);

	if (local_error)
		g_propagate_error (error, local_error);

	return local_error != NULL;
}

static void
handle_signal_cb (GDBusConnection *connection,
		  const gchar *sender_name,
		  const gchar *object_path,
		  const gchar *interface_name,
		  const gchar *signal_name,
		  GVariant *parameters,
		  gpointer user_data)
{
	ProxyData *pd = user_data;
	GError *error = NULL;

	g_return_if_fail (pd != NULL);

	if (!g_dbus_connection_emit_signal (pd->regs_connection, NULL, g_dbus_proxy_get_object_path (pd->proxy),
		interface_name, signal_name, parameters, &error)) {
		g_clear_error (&error);
	}
}

static void
proxy_data_reg_interfaces (ProxyData *pd,
			   GDBusConnection *connection)
{
	const GDBusInterfaceVTable interface_vtable = {
		handle_method_call_cb,
		handle_get_property_cb,
		handle_set_property_cb
	};
	GDBusNodeInfo *introspection_data;
	GVariant *result;
	const gchar *xml_data;
	guint regid;
	gint ii;
	GError *error = NULL;

	g_return_if_fail (pd != NULL);
	g_return_if_fail (pd->proxy != NULL);
	g_return_if_fail (pd->regs_connection == NULL);
	g_return_if_fail (connection != NULL);

	result = g_dbus_connection_call_sync (g_dbus_proxy_get_connection (pd->proxy),
		g_dbus_proxy_get_name (pd->proxy),
		g_dbus_proxy_get_object_path (pd->proxy),
		"org.freedesktop.DBus.Introspectable",
		"Introspect",
		NULL,
		G_VARIANT_TYPE ("(s)"),
		G_DBUS_CALL_FLAGS_NONE,
		DBUS_CALL_TIMEOUT,
		NULL, &error);

	if (!result) {
		g_warning ("Failed to get introspected data for '%s': %s", g_dbus_proxy_get_name (pd->proxy), error ? error->message : "Unknown error");
		g_clear_error (&error);
		return;
	}

	g_variant_get (result, "(&s)", &xml_data);
	introspection_data = g_dbus_node_info_new_for_xml (xml_data, NULL);
	g_variant_unref (result);

	if (!introspection_data) {
		g_warning ("Failed to parse introspected data for '%s': %s", g_dbus_proxy_get_name (pd->proxy), error ? error->message : "Unknown error");
		g_clear_error (&error);
		return;
	}

	pd->regs_connection = g_object_ref (connection);

	for (ii = 0; introspection_data->interfaces && introspection_data->interfaces[ii]; ii++) {
		GDBusInterfaceInfo *iface_info = introspection_data->interfaces[ii];

		if (!iface_info)
			continue;

		regid = g_dbus_connection_register_object (pd->regs_connection, g_dbus_proxy_get_object_path (pd->proxy),
			iface_info,
			&interface_vtable,
			pd,
			NULL,
			&error);

		if (regid) {
			pd->regids = g_slist_prepend (pd->regids, GUINT_TO_POINTER (regid));

			if (iface_info->signals) {
				gint jj;

				for (jj = 0; iface_info->signals[jj]; jj++) {
					guint sigid;

					sigid = g_dbus_connection_signal_subscribe (g_dbus_proxy_get_connection (pd->proxy), NULL,
						iface_info->name,
						iface_info->signals[jj]->name,
						g_dbus_proxy_get_object_path (pd->proxy),
						NULL, G_DBUS_SIGNAL_FLAGS_NONE,
						handle_signal_cb, pd, NULL);

					if (sigid)
						pd->sigids = g_slist_prepend (pd->sigids, GUINT_TO_POINTER (sigid));
				}
			}

			if (g_strcmp0 (iface_info->name, "org.freedesktop.DBus.ObjectManager") == 0) {
				pd->object_manager = g_dbus_object_manager_client_new_sync (g_dbus_proxy_get_connection (pd->proxy),
					G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
					g_dbus_proxy_get_name (pd->proxy),
					g_dbus_proxy_get_object_path (pd->proxy),
					NULL, NULL, NULL, NULL, &error);

				if (pd->object_manager) {
					GList *objects, *link;

					g_signal_connect (pd->object_manager, "object-added",
						G_CALLBACK (object_manager_object_added_cb), pd);
					g_signal_connect (pd->object_manager, "object-removed",
						G_CALLBACK (object_manager_object_removed_cb), pd);

					objects = g_dbus_object_manager_get_objects (pd->object_manager);
					for (link = objects; link; link = g_list_next (link)) {
						GDBusObject *object = link->data;

						object_manager_object_added_cb (pd->object_manager, object, pd);
					}

					g_list_free_full (objects, g_object_unref);
				} else {
					g_clear_error (&error);
				}
			}
		} else {
			g_clear_error (&error);
		}
	}

	g_dbus_node_info_unref (introspection_data);
}

static void
bus_name_acquired_cb (GDBusConnection *connection,
		      const gchar *name,
		      gpointer user_data)
{
	ProxyData *pd = user_data;

	g_return_if_fail (pd != NULL);

	proxy_data_reg_interfaces (pd, connection);
}

static void
bus_name_lost_cb (GDBusConnection *connection,
		  const gchar *name,
		  gpointer user_data)
{
	ProxyData *pd = user_data;

	g_return_if_fail (pd != NULL);
}

static void
object_manager_object_added_cb (GDBusObjectManager *manager,
				GDBusObject *object,
				gpointer user_data)
{
	ProxyData *parent_pd = user_data, *child_pd;
	gchar *encoded_string;

	encoded_string = g_strconcat (parent_pd->iface_name, ":", g_dbus_object_get_object_path (object), NULL);
	child_pd = proxy_data_new (encoded_string);
	g_free (encoded_string);

	if (child_pd) {
		proxy_data_reg_interfaces (child_pd, parent_pd->regs_connection);
		parent_pd->object_manager_pds = g_slist_prepend (parent_pd->object_manager_pds, child_pd);
	}
}

static void
object_manager_object_removed_cb (GDBusObjectManager *manager,
				  GDBusObject *object,
				  gpointer user_data)
{
	ProxyData *parent_pd = user_data;
	GSList *link;
	const gchar *obj_path;

	obj_path = g_dbus_object_get_object_path (object);
	for (link = parent_pd->object_manager_pds; link; link = g_slist_next (link)) {
		ProxyData *child_pd = link->data;

		if (child_pd && g_strcmp0 (child_pd->path, obj_path) == 0) {
			proxy_data_free (child_pd);
			parent_pd->object_manager_pds = g_slist_remove (parent_pd->object_manager_pds, child_pd);
			break;
		}
	}
}

static gpointer
main_loop_thread (gpointer user_data)
{
	GMainLoop *main_loop = user_data;

	g_main_loop_run (main_loop);

	return NULL;
}

/* Adapted from glib's gtestdbus.c */
static gchar *
write_config_file (const GSList *service_dirs)
{
	GString *contents;
	gint fd;
	GSList *link;
	gchar *path = NULL;
	GError *error = NULL;

	fd = g_file_open_tmp ("evolution-bus-tunnel-XXXXXX", &path, &error);
	if (fd <= 0) {
		g_warning ("Failed to create tmp file for D-Bus configuration: %s", error ? error->message : "Unknown error");
		g_clear_error (&error);
		return NULL;
	}

	contents = g_string_new (NULL);
	g_string_append (contents,
		"<busconfig>\n"
		"  <type>session</type>\n"
		"  <listen>unix:tmpdir=/tmp</listen>\n");

	for (link = (GSList *) service_dirs; link; link = g_slist_next (link)) {
		const gchar *dir_path = link->data;

		g_string_append_printf (contents,"  <servicedir>%s</servicedir>\n", dir_path);
	}

	g_string_append (contents,
		"  <policy context=\"default\">\n"
		"    <!-- Allow everything to be sent -->\n"
		"    <allow send_destination=\"*\" eavesdrop=\"true\"/>\n"
		"    <!-- Allow everything to be received -->\n"
		"    <allow eavesdrop=\"true\"/>\n"
		"    <!-- Allow anyone to own anything -->\n"
		"    <allow own=\"*\"/>\n"
		"  </policy>\n"
		"</busconfig>\n");

	close (fd);
	if (!g_file_set_contents (path, contents->str, contents->len, &error)) {
		g_warning ("Failed to write file '%s': %s", path, error ? error->message : "Unknown error");
		g_clear_error (&error);
		g_free (path);
		return NULL;
	}

	g_string_free (contents, TRUE);

	return path;
}

/* Adapted from glib's gtestdbus.c */
static gboolean
start_daemon (const GSList *service_dirs,
	      GPid *pbus_pid,
	      gint *pbus_stdout_fd,
	      gchar **pbus_address)
{
	const gchar *argv[] = {"dbus-daemon", "--print-address", "--config-file=foo", NULL};
	gchar *config_path;
	gchar *config_arg;
	GIOChannel *channel;
	gint stdout_fd2;
	gsize termpos;
	GError *error = NULL;

	/* Write config file and set its path in argv */
	config_path = write_config_file (service_dirs);
	config_arg = g_strdup_printf ("--config-file=%s", config_path);
	argv[2] = config_arg;

	/* Spawn dbus-daemon */
	g_spawn_async_with_pipes (NULL, (gchar **) argv, NULL,
		G_SPAWN_SEARCH_PATH, NULL, NULL, pbus_pid,
		NULL, pbus_stdout_fd, NULL, &error);
	if (error) {
		g_warning ("Failed to start D-Bus daemon: %s", error->message);
		g_clear_error (&error);
		g_free (config_path);
		g_free (config_arg);
		return FALSE;
	}

	/* Read bus address from daemon' stdout. We have to be careful to avoid
	 * closing the FD, as it is passed to any D-Bus service activated processes,
	 * and if we close it, they will get a SIGPIPE and die when they try to write
	 * to their stdout. */
	stdout_fd2 = dup (*pbus_stdout_fd);
	g_assert_cmpint (stdout_fd2, >=, 0);
	channel = g_io_channel_unix_new (stdout_fd2);

	g_io_channel_read_line (channel, pbus_address, NULL, &termpos, &error);
	if (error) {
		g_warning ("Failed to read new D-Bus daemon address: %s", error->message);
		g_io_channel_shutdown (channel, FALSE, NULL);
		g_io_channel_unref (channel);
		g_clear_error (&error);
		g_free (config_path);
		g_free (config_arg);
		return FALSE;
	}

	(*pbus_address)[termpos] = '\0';

	/* Cleanup */
	g_io_channel_shutdown (channel, FALSE, &error);
	if (error) {
		g_debug ("Failed to shutdown io channel: %s", error->message);
		g_clear_error (&error);
	}
	g_io_channel_unref (channel);

	/* Don't use g_file_delete since it calls into gvfs */
	if (g_unlink (config_path) != 0)
		g_warning ("Failed to remove config file '%s'", config_path);

	g_free (config_path);
	g_free (config_arg);

	g_unsetenv ("DBUS_STARTER_ADDRESS");
	g_unsetenv ("DBUS_STARTER_BUS_TYPE");
	g_setenv ("DBUS_SESSION_BUS_ADDRESS", *pbus_address, TRUE);

	return TRUE;
}

/* Adapted from glib's gtestdbus.c */
static void
stop_daemon (GPid *pbus_pid,
	     gint *pbus_stdout_fd,
	     gchar **pbus_address)
{
	kill (*pbus_pid, SIGTERM);
	g_spawn_close_pid (*pbus_pid);
	*pbus_pid = 0;

	close (*pbus_stdout_fd);
	*pbus_stdout_fd = -1;

	g_free (*pbus_address);
	*pbus_address = NULL;
}

static void
print_help (void)
{
	printf ("Usage: evolution-dbus-session --exec exec [ --service-dir dir ] [ --iface name:path ]\n");
	printf ("   --help ... shows this help and exits\n");
	printf ("   --exec exec ... a program to run in a dedicated D-Bus session\n");
	printf ("   --service-dir dir ... adds a dir as a service directory for the new D-Bus session\n");
	printf ("   --iface name:path ... adds an interface name and its D-Bus path to proxy into the new D-Bus session\n");
	printf ("\n");
	printf ("   Both --service-dir and --iface arguments are optional and can be specified multiple times.\n");
}

gint
main (gint argc,
      gchar *argv[])
{
	GPid bus_pid = 0;
	gint bus_stdout_fd = -1, ii, res = 0;
	gchar *bus_address = NULL;
	GSList *service_dirs = NULL;
	GSList *pds = NULL; /* ProxyData * */
	const gchar *to_exec = NULL;

	if (argc == 1) {
		print_help ();
		return 0;
	}

	for (ii = 1; ii < argc; ii++) {
		if (g_str_equal (argv[ii], "--help")) {
			print_help ();
			g_slist_free_full (pds, proxy_data_free);
			g_slist_free (service_dirs);
			return 0;
		} else if (g_str_equal (argv[ii], "--exec")) {
			if (ii + 1 >= argc) {
				g_printerr ("Missing value for %s\n", argv[ii]);
				return 1;
			}

			ii++;
			to_exec = argv[ii];
		} else if (g_str_equal (argv[ii], "--service-dir")) {
			if (ii + 1 >= argc) {
				g_printerr ("Missing value for %s\n", argv[ii]);
				return 2;
			}

			ii++;
			service_dirs = g_slist_prepend (service_dirs, argv[ii]);
		} else if (g_str_equal (argv[ii], "--iface")) {
			ProxyData *pd;

			if (ii + 1 >= argc) {
				g_printerr ("Missing value for %s\n", argv[ii]);
				return 3;
			}

			ii++;
			pd = proxy_data_new (argv[ii]);
			if (pd) {
				pds = g_slist_prepend (pds, pd);
			}
		} else {
			g_printerr ("Unknown argument '%s'\n", argv[ii]);
			return 4;
		}
	}

	if (!pds) {
		g_printerr ("No --iface defined or cannot connect to any, exiting\n");
		g_slist_free_full (pds, proxy_data_free);
		g_slist_free (service_dirs);
		return 5;
	}

	if (!to_exec) {
		g_printerr ("No --exec defined, exiting\n");
		g_slist_free_full (pds, proxy_data_free);
		g_slist_free (service_dirs);
		return 6;
	}

	/* Preserve order as specified on the command line */
	pds = g_slist_reverse (pds);
	service_dirs = g_slist_reverse (service_dirs);

	if (start_daemon (service_dirs, &bus_pid, &bus_stdout_fd, &bus_address)) {
		GThread *thread;
		GMainLoop *main_loop;
		GDBusConnection *connection;
		GError *error = NULL;

		connection = g_dbus_connection_new_for_address_sync (bus_address,
			G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION, NULL, NULL, &error);
		if (connection) {
			GSList *link;

			for (link = pds; link; link = g_slist_next (link)) {
				ProxyData *pd = link->data;

				if (!pd)
					continue;

				pd->ownid = g_bus_own_name_on_connection (connection,
					pd->iface_name,
					G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
					bus_name_acquired_cb,
					bus_name_lost_cb,
					pd, NULL);
			}

			main_loop = g_main_loop_new (NULL, FALSE);

			thread = g_thread_new (NULL, main_loop_thread, main_loop);

			if (system (to_exec) == -1)
				g_warning ("Failed to execute '%s'", to_exec);

			if (!g_dbus_connection_is_closed (connection) &&
			    !g_dbus_connection_close_sync (connection, NULL, &error)) {
				g_warning ("Failed to close connection: %s", error ? error->message : "Unknown error");
				g_clear_error (&error);
			}

			g_slist_free_full (pds, proxy_data_free);
			pds = NULL;

			stop_daemon (&bus_pid, &bus_stdout_fd, &bus_address);
			g_main_loop_quit (main_loop);
			g_thread_join (thread);

			g_main_loop_unref (main_loop);
			g_clear_object (&connection);
		} else {
			g_warning ("Failed to create connection for dedicated server address '%s': %s",
				bus_address, error ? error->message : "Unknown error");
			g_clear_error (&error);
		}
	} else {
		res = 7;
	}

	g_slist_free_full (pds, proxy_data_free);
	g_slist_free (service_dirs);

	return res;
}

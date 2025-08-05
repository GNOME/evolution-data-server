/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <glib.h>
#include <gio/gio.h>

#include "e-dbus-oauth2-response.h"

static const gchar *glob_uri = NULL;
static GDBusConnection *glob_connection = NULL;

static void
called_reponse_uri_cb (GObject *source_object,
		       GAsyncResult *result,
		       gpointer user_data)
{
	GMainLoop *loop = user_data;
	GError *local_error = NULL;

	if (!e_dbus_oauth2_response_call_response_uri_finish (E_DBUS_OAUTH2_RESPONSE (source_object), result, &local_error)) {
		g_warning ("Failed to call ResponseURI: %s", local_error ? local_error->message : "Unknown error");
		g_clear_error (&local_error);
	}

	g_main_loop_quit (loop);
}

static void
proxy_created_cb (GObject *source_object,
		  GAsyncResult *result,
		  gpointer user_data)
{
	GMainLoop *loop = user_data;
	EDBusOAuth2Response *proxy;
	GError *local_error = NULL;

	proxy = e_dbus_oauth2_response_proxy_new_finish (result, &local_error);
	if (!proxy) {
		g_warning ("Failed to get OAuth2Response proxy: %s", local_error ? local_error->message : "Unknown error");
		g_clear_error (&local_error);
		g_main_loop_quit (loop);
		return;
	}

	e_dbus_oauth2_response_call_response_uri (proxy, glob_uri, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
		called_reponse_uri_cb, loop);

	g_clear_object (&proxy);
}

static void
got_bus_cb (GObject *source_object,
	    GAsyncResult *result,
	    gpointer user_data)
{
	GMainLoop *loop = user_data;
	GError *local_error = NULL;

	glob_connection = g_bus_get_finish (result, &local_error);
	if (!glob_connection) {
		g_warning ("Failed to get D-Bus session: %s", local_error ? local_error->message : "Unknown error");
		g_clear_error (&local_error);
		g_main_loop_quit (loop);
		return;
	}

	e_dbus_oauth2_response_proxy_new (glob_connection,
		G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
		G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
		G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
		OAUTH2_RESPONSE_DBUS_SERVICE_NAME,
		"/org/gnome/evolution/dataserver/OAuth2Response",
		NULL,
		proxy_created_cb,
		loop);
}

gint
main (gint argc,
      const gchar *argv[])
{
	GMainLoop *loop;

	g_set_prgname ("evolution-oauth2-handler");

	if (argc != 2) {
		g_warning ("Expects one argument, a URI to pass to the OAuth2Response D-Bus proxy");
		return 1;
	}

	glob_uri = argv[1];

	loop = g_main_loop_new (NULL, FALSE);

	g_bus_get (G_BUS_TYPE_SESSION, NULL, got_bus_cb, loop);

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	g_clear_object (&glob_connection);

	return 0;
}

/*
 * e-dbus-server.c
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
 */

/**
 * SECTION: e-dbus-server
 * @short_description: an abstract base class for a D-Bus server
 * @include: libebackend/e-dbus-server
 **/

#include "e-dbus-server.h"

#include <config.h>

#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

#include <libebackend/e-module.h>
#include <libebackend/e-extensible.h>

#define E_DBUS_SERVER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DBUS_SERVER, EDBusServerPrivate))

#define INACTIVITY_TIMEOUT 10  /* seconds */

struct _EDBusServerPrivate {
	GMainLoop *main_loop;
	guint bus_owner_id;
	guint terminate_id;

	guint inactivity_timeout_id;
	guint use_count;
	gboolean wait_for_client;
};

enum {
	BUS_ACQUIRED,
	BUS_NAME_ACQUIRED,
	BUS_NAME_LOST,
	RUN_SERVER,
	QUIT_SERVER,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (
	EDBusServer, e_dbus_server, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (E_TYPE_EXTENSIBLE, NULL))

static void
dbus_server_bus_acquired_cb (GDBusConnection *connection,
                             const gchar *bus_name,
                             EDBusServer *server)
{
	g_signal_emit (server, signals[BUS_ACQUIRED], 0, connection);
}

static void
dbus_server_name_acquired_cb (GDBusConnection *connection,
                              const gchar *bus_name,
                              EDBusServer *server)
{
	g_signal_emit (server, signals[BUS_NAME_ACQUIRED], 0, connection);
}

static void
dbus_server_name_lost_cb (GDBusConnection *connection,
                          const gchar *bus_name,
                          EDBusServer *server)
{
	g_signal_emit (server, signals[BUS_NAME_LOST], 0, connection);
}

static gboolean
dbus_server_inactivity_timeout_cb (EDBusServer *server)
{
	e_dbus_server_quit (server);

	return FALSE;
}

#ifdef G_OS_UNIX
static gboolean
dbus_server_terminate_cb (EDBusServer *server)
{
	g_print ("Received terminate signal.\n");
	e_dbus_server_quit (server);

	return FALSE;
}
#endif

static void
dbus_server_finalize (GObject *object)
{
	EDBusServerPrivate *priv;

	priv = E_DBUS_SERVER_GET_PRIVATE (object);

	g_main_loop_unref (priv->main_loop);
	g_bus_unown_name (priv->bus_owner_id);

	if (priv->terminate_id > 0)
		g_source_remove (priv->terminate_id);

	if (priv->inactivity_timeout_id > 0)
		g_source_remove (priv->inactivity_timeout_id);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_dbus_server_parent_class)->finalize (object);
}

static void
dbus_server_bus_acquired (EDBusServer *server,
                          GDBusConnection *connection)
{
	if (server->priv->use_count == 0 && !server->priv->wait_for_client)
		server->priv->inactivity_timeout_id =
			g_timeout_add_seconds (
				INACTIVITY_TIMEOUT, (GSourceFunc)
				dbus_server_inactivity_timeout_cb,
				server);
}

static void
dbus_server_bus_name_acquired (EDBusServer *server,
                               GDBusConnection *connection)
{
	EDBusServerClass *class;

	class = E_DBUS_SERVER_GET_CLASS (server);
	g_return_if_fail (class->bus_name != NULL);

	g_print ("Bus name '%s' acquired.\n", class->bus_name);
}

static void
dbus_server_bus_name_lost (EDBusServer *server,
                           GDBusConnection *connection)
{
	EDBusServerClass *class;

	class = E_DBUS_SERVER_GET_CLASS (server);
	g_return_if_fail (class->bus_name != NULL);

	g_print ("Bus name '%s' lost.\n", class->bus_name);

	e_dbus_server_quit (server);
}

static void
dbus_server_run_server (EDBusServer *server)
{
	EDBusServerClass *class;

	/* Try to acquire the well-known bus name. */

	class = E_DBUS_SERVER_GET_CLASS (server);
	g_return_if_fail (class->bus_name != NULL);

	server->priv->bus_owner_id = g_bus_own_name (
		G_BUS_TYPE_SESSION,
		class->bus_name,
		G_BUS_NAME_OWNER_FLAGS_REPLACE |
		G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT,
		(GBusAcquiredCallback) dbus_server_bus_acquired_cb,
		(GBusNameAcquiredCallback) dbus_server_name_acquired_cb,
		(GBusNameLostCallback) dbus_server_name_lost_cb,
		g_object_ref (server),
		(GDestroyNotify) g_object_unref);

	g_main_loop_run (server->priv->main_loop);
}

static void
dbus_server_quit_server (EDBusServer *server)
{
	g_main_loop_quit (server->priv->main_loop);
}

static void
e_dbus_server_class_init (EDBusServerClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EDBusServerPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = dbus_server_finalize;

	class->bus_acquired = dbus_server_bus_acquired;
	class->bus_name_acquired = dbus_server_bus_name_acquired;
	class->bus_name_lost = dbus_server_bus_name_lost;
	class->run_server = dbus_server_run_server;
	class->quit_server = dbus_server_quit_server;

	/**
	 * EDBusServer::bus-acquired:
	 * @server: the #EDBusServer which emitted the signal
	 * @connection: the #GDBusConnection to the session bus
	 *
	 * Emitted when @server acquires a connection to the session bus.
	 **/
	signals[BUS_ACQUIRED] = g_signal_new (
		"bus-acquired",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EDBusServerClass, bus_acquired),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		G_TYPE_DBUS_CONNECTION);

	/**
	 * EDBusServer::bus-name-acquired:
	 * @server: the #EDBusServer which emitted the signal
	 * @connection: the #GDBusConnection to the session bus
	 *
	 * Emitted when @server acquires its well-known session bus name.
	 **/
	signals[BUS_NAME_ACQUIRED] = g_signal_new (
		"bus-name-acquired",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EDBusServerClass, bus_name_acquired),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		G_TYPE_DBUS_CONNECTION);

	/**
	 * EDBusServer::bus-name-lost:
	 * @server: the #EDBusServer which emitted the signal
	 * @connection: the #GDBusconnection to the session bus,
	 *              or %NULL if the connection has been closed
	 *
	 * Emitted when @server loses its well-known session bus name
	 * or the session bus connection has been closed.
	 **/
	signals[BUS_NAME_LOST] = g_signal_new (
		"bus-name-lost",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EDBusServerClass, bus_name_lost),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		G_TYPE_DBUS_CONNECTION);

	/**
	 * EDBusServer::run-server:
	 * @server: the #EDBusServer which emitted the signal
	 *
	 * Emitted to request that @server start its main loop and
	 * attempt to acquire its well-known session bus name.
	 **/
	signals[RUN_SERVER] = g_signal_new (
		"run-server",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EDBusServerClass, run_server),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);

	/**
	 * EDBusServer::quit-server:
	 * @server: the #EDBusServer which emitted the signal
	 *
	 * Emitted to request that @server quit its main loop.
	 **/
	signals[QUIT_SERVER] = g_signal_new (
		"quit-server",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EDBusServerClass, quit_server),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

static void
e_dbus_server_init (EDBusServer *server)
{
	server->priv = E_DBUS_SERVER_GET_PRIVATE (server);
	server->priv->main_loop = g_main_loop_new (NULL, FALSE);
	server->priv->wait_for_client = FALSE;

#ifdef G_OS_UNIX
	server->priv->terminate_id = g_unix_signal_add (
		SIGTERM, (GSourceFunc) dbus_server_terminate_cb, server);
#endif
}

/**
 * e_dbus_server_run:
 * @server: an #EDBusServer
 * @wait_for_client: continue running until a client connects
 *
 * Emits the #EDBusServer::run signal.
 *
 * By default the @server will start its main loop and attempt to acquire
 * its well-known session bus name.  If the @server's main loop is already
 * running this function does nothing.
 *
 * If @wait_for_client is %TRUE, the @server will continue running until
 * the first client connection is made instead of terminating on its own
 * if no client connection is made within the first few seconds.
 *
 * Since: 3.4
 **/
void
e_dbus_server_run (EDBusServer *server,
                   gboolean wait_for_client)
{
	g_return_if_fail (E_IS_DBUS_SERVER (server));

	server->priv->wait_for_client = wait_for_client;

	if (g_main_loop_is_running (server->priv->main_loop))
		return;

	g_signal_emit (server, signals[RUN_SERVER], 0);
}

/**
 * e_dbus_server_quit:
 * @server: an #EDBusServer
 *
 * Emits the #EDBusServer::quit signal.
 *
 * By default the @server will quit its main loop.
 *
 * Since: 3.4
 **/
void
e_dbus_server_quit (EDBusServer *server)
{
	g_return_if_fail (E_IS_DBUS_SERVER (server));

	g_signal_emit (server, signals[QUIT_SERVER], 0);
}

/**
 * e_dbus_server_hold:
 * @server: an #EDBusServer
 *
 * Increases the use count of @server.
 *
 * Use this function to indicate that the server has a reason to continue
 * to run.  To cancel the hold, call e_dbus_server_release().
 *
 * Since: 3.4
 **/
void
e_dbus_server_hold (EDBusServer *server)
{
	g_return_if_fail (E_IS_DBUS_SERVER (server));

	if (server->priv->inactivity_timeout_id > 0) {
		g_source_remove (server->priv->inactivity_timeout_id);
		server->priv->inactivity_timeout_id = 0;
	}

	server->priv->use_count++;
}

/**
 * e_dbus_server_release:
 * @server: an #EDBusServer
 *
 * Decreates the use count of @server.
 *
 * When the use count reaches zero, the server will stop running.
 *
 * Never call this function except to cancel the effect of a previous call
 * to e_dbus_server_hold().
 *
 * Since: 3.4
 **/
void
e_dbus_server_release (EDBusServer *server)
{
	g_return_if_fail (E_IS_DBUS_SERVER (server));
	g_return_if_fail (server->priv->use_count > 0);

	server->priv->use_count--;

	if (server->priv->use_count == 0)
		server->priv->inactivity_timeout_id =
			g_timeout_add_seconds (
				INACTIVITY_TIMEOUT, (GSourceFunc)
				dbus_server_inactivity_timeout_cb,
				server);
}

/**
 * e_dbus_server_load_modules:
 * @server: an #EDBusServer
 *
 * This function should be called once during @server initialization to
 * load all available library modules to extend the @server's functionality.
 *
 * Since: 3.4
 **/
void
e_dbus_server_load_modules (EDBusServer *server)
{
	EDBusServerClass *class;
	GList *list;

	g_return_if_fail (E_IS_DBUS_SERVER (server));

	class = E_DBUS_SERVER_GET_CLASS (server);
	g_return_if_fail (class->module_directory != NULL);

	list = e_module_load_all_in_directory (class->module_directory);
	g_list_free_full (list, (GDestroyNotify) g_type_module_unuse);
}

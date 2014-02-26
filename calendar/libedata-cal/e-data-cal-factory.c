/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar factory
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2009 Intel Corporation
 *
 * Authors:
 *   Federico Mena-Quintero <federico@ximian.com>
 *   JP Rosevear <jpr@ximian.com>
 *   Ross Burton <ross@linux.intel.com>
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION: e-data-cal-factory
 * @include: libedata-cal/libedata-cal.h
 * @short_description: The main calendar server object
 *
 * This class handles incomming D-Bus connections and creates
 * the #EDataCal layer for server side calendars to communicate
 * with client side #ECalClient objects.
 **/

#include <config.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gi18n.h>

/* Private D-Bus classes. */
#include <e-dbus-calendar-factory.h>

#include "e-cal-backend.h"
#include "e-cal-backend-factory.h"
#include "e-data-cal.h"
#include "e-data-cal-factory.h"

#include <libical/ical.h>

#define d(x)

#define E_DATA_CAL_FACTORY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_CAL_FACTORY, EDataCalFactoryPrivate))

struct _EDataCalFactoryPrivate {
	ESourceRegistry *registry;
	EDBusCalendarFactory *dbus_factory;

	/* This is a hash table of client bus names to an array of
	 * ECalBackend references; one for every connection opened. */
	GHashTable *connections;
	GMutex connections_lock;

	/* This is a hash table of client bus names being watched.
	 * The value is the watcher ID for g_bus_unwatch_name(). */
	GHashTable *watched_names;
	GMutex watched_names_lock;
};

enum {
	PROP_0,
	PROP_REGISTRY
};

/* Forward Declarations */
static void	e_data_cal_factory_initable_init
						(GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (
	EDataCalFactory,
	e_data_cal_factory,
	E_TYPE_DATA_FACTORY,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_data_cal_factory_initable_init))

static void
watched_names_value_free (gpointer value)
{
	g_bus_unwatch_name (GPOINTER_TO_UINT (value));
}

static void
data_cal_factory_toggle_notify_cb (gpointer data,
                                   GObject *backend,
                                   gboolean is_last_ref)
{
	if (is_last_ref) {
		/* Take a strong reference before removing the
		 * toggle reference, to keep the backend alive. */
		g_object_ref (backend);

		g_object_remove_toggle_ref (
			backend, data_cal_factory_toggle_notify_cb, data);

		g_signal_emit_by_name (backend, "shutdown");

		g_object_unref (backend);
	}
}

static void
data_cal_factory_connections_add (EDataCalFactory *factory,
                                  const gchar *name,
                                  ECalBackend *backend)
{
	GHashTable *connections;
	GPtrArray *array;

	g_return_if_fail (name != NULL);
	g_return_if_fail (backend != NULL);

	g_mutex_lock (&factory->priv->connections_lock);

	connections = factory->priv->connections;

	if (g_hash_table_size (connections) == 0)
		e_dbus_server_hold (E_DBUS_SERVER (factory));

	array = g_hash_table_lookup (connections, name);

	if (array == NULL) {
		array = g_ptr_array_new_with_free_func (
			(GDestroyNotify) g_object_unref);
		g_hash_table_insert (
			connections, g_strdup (name), array);
	}

	g_ptr_array_add (array, g_object_ref (backend));

	g_mutex_unlock (&factory->priv->connections_lock);
}

static gboolean
data_cal_factory_connections_remove (EDataCalFactory *factory,
                                     const gchar *name,
                                     ECalBackend *backend)
{
	GHashTable *connections;
	GPtrArray *array;
	gboolean removed = FALSE;

	/* If backend is NULL, we remove all backends for name. */
	g_return_val_if_fail (name != NULL, FALSE);

	g_mutex_lock (&factory->priv->connections_lock);

	connections = factory->priv->connections;
	array = g_hash_table_lookup (connections, name);

	if (array != NULL) {
		if (backend != NULL) {
			removed = g_ptr_array_remove_fast (array, backend);
		} else if (array->len > 0) {
			g_ptr_array_set_size (array, 0);
			removed = TRUE;
		}

		if (array->len == 0)
			g_hash_table_remove (connections, name);

		if (g_hash_table_size (connections) == 0)
			e_dbus_server_release (E_DBUS_SERVER (factory));
	}

	g_mutex_unlock (&factory->priv->connections_lock);

	return removed;
}

static void
data_cal_factory_connections_remove_all (EDataCalFactory *factory)
{
	GHashTable *connections;

	g_mutex_lock (&factory->priv->connections_lock);

	connections = factory->priv->connections;

	if (g_hash_table_size (connections) > 0) {
		g_hash_table_remove_all (connections);
		e_dbus_server_release (E_DBUS_SERVER (factory));
	}

	g_mutex_unlock (&factory->priv->connections_lock);
}

static void
data_cal_factory_name_vanished_cb (GDBusConnection *connection,
                                   const gchar *name,
                                   gpointer user_data)
{
	GWeakRef *weak_ref = user_data;
	EDataCalFactory *factory;

	factory = g_weak_ref_get (weak_ref);

	if (factory != NULL) {
		data_cal_factory_connections_remove (factory, name, NULL);

		/* Unwatching the bus name from here will corrupt the
		 * 'name' argument, and possibly also the 'user_data'.
		 *
		 * This is a GDBus bug.  Work around it by unwatching
		 * the bus name last.
		 *
		 * See: https://bugzilla.gnome.org/706088
		 */
		g_mutex_lock (&factory->priv->watched_names_lock);
		g_hash_table_remove (factory->priv->watched_names, name);
		g_mutex_unlock (&factory->priv->watched_names_lock);

		g_object_unref (factory);
	}
}

static void
data_cal_factory_watched_names_add (EDataCalFactory *factory,
                                    GDBusConnection *connection,
                                    const gchar *name)
{
	GHashTable *watched_names;

	g_return_if_fail (name != NULL);

	g_mutex_lock (&factory->priv->watched_names_lock);

	watched_names = factory->priv->watched_names;

	if (!g_hash_table_contains (watched_names, name)) {
		guint watcher_id;

		/* The g_bus_watch_name() documentation says one of the two
		 * callbacks are guaranteed to be invoked after calling the
		 * function.  But which one is determined asynchronously so
		 * there should be no chance of the name vanished callback
		 * deadlocking with us when it tries to acquire the lock. */
		watcher_id = g_bus_watch_name_on_connection (
			connection, name,
			G_BUS_NAME_WATCHER_FLAGS_NONE,
			(GBusNameAppearedCallback) NULL,
			data_cal_factory_name_vanished_cb,
			e_weak_ref_new (factory),
			(GDestroyNotify) e_weak_ref_free);

		g_hash_table_insert (
			watched_names, g_strdup (name),
			GUINT_TO_POINTER (watcher_id));
	}

	g_mutex_unlock (&factory->priv->watched_names_lock);
}

static EBackend *
data_cal_factory_ref_backend (EDataFactory *factory,
                              ESource *source,
                              const gchar *extension_name,
                              const gchar *type_string,
                              GError **error)
{
	EBackend *backend;
	ESourceBackend *extension;
	gchar *backend_name;
	gchar *hash_key;

	extension = e_source_get_extension (source, extension_name);
	backend_name = e_source_backend_dup_backend_name (extension);

	if (backend_name == NULL || *backend_name == '\0') {
		g_set_error (
			error, E_DATA_CAL_ERROR, NoSuchCal,
			_("No backend name in source '%s'"),
			e_source_get_display_name (source));
		g_free (backend_name);
		return NULL;
	}

	hash_key = g_strdup_printf ("%s:%s", backend_name, type_string);
	backend = e_data_factory_ref_initable_backend (
		factory, hash_key, source, NULL, error);
	g_free (hash_key);

	g_free (backend_name);

	return backend;
}

static gchar *
construct_cal_factory_path (void)
{
	static volatile gint counter = 1;

	g_atomic_int_inc (&counter);

	return g_strdup_printf (
		"/org/gnome/evolution/dataserver/Calendar/%d/%u",
		getpid (), counter);
}

static void
data_cal_factory_closed_cb (ECalBackend *backend,
                            const gchar *sender,
                            EDataCalFactory *factory)
{
	data_cal_factory_connections_remove (factory, sender, backend);
}

static gchar *
data_cal_factory_open (EDataCalFactory *factory,
                       GDBusConnection *connection,
                       const gchar *sender,
                       const gchar *uid,
                       const gchar *extension_name,
                       const gchar *type_string,
                       GError **error)
{
	EDataCal *data_cal;
	EBackend *backend;
	ESourceRegistry *registry;
	ESource *source;
	gchar *object_path;

	if (uid == NULL || *uid == '\0') {
		g_set_error (
			error, E_DATA_CAL_ERROR, NoSuchCal,
			_("Missing source UID"));
		return NULL;
	}

	registry = e_data_cal_factory_get_registry (factory);
	source = e_source_registry_ref_source (registry, uid);

	if (source == NULL) {
		g_set_error (
			error, E_DATA_CAL_ERROR, NoSuchCal,
			_("No such source for UID '%s'"), uid);
		return NULL;
	}

	backend = data_cal_factory_ref_backend (
		E_DATA_FACTORY (factory), source,
		extension_name, type_string, error);

	g_object_unref (source);

	if (backend == NULL)
		return NULL;

	/* If the backend already has an EDataCal installed, return its
	 * object path.  Otherwise we need to install a new EDataCal. */

	data_cal = e_cal_backend_ref_data_cal (E_CAL_BACKEND (backend));

	if (data_cal != NULL) {
		object_path = g_strdup (
			e_data_cal_get_object_path (data_cal));
	} else {
		object_path = construct_cal_factory_path ();

		/* The EDataCal will attach itself to ECalBackend,
		 * so no need to call e_cal_backend_set_data_cal(). */
		data_cal = e_data_cal_new (
			E_CAL_BACKEND (backend),
			connection, object_path, error);

		if (data_cal != NULL) {
			/* Install a toggle reference on the backend
			 * so we can signal it to shut down once all
			 * client connections are closed. */
			g_object_add_toggle_ref (
				G_OBJECT (backend),
				data_cal_factory_toggle_notify_cb,
				NULL);

			g_signal_connect_object (
				backend, "closed",
				G_CALLBACK (data_cal_factory_closed_cb),
				factory, 0);

		} else {
			g_free (object_path);
			object_path = NULL;
		}
	}

	if (data_cal != NULL) {
		/* Watch the sender's bus name so we can clean
		 * up its connections if the bus name vanishes. */
		data_cal_factory_watched_names_add (
			factory, connection, sender);

		/* A client may create multiple EClient instances for the
		 * same ESource, each of which calls close() individually.
		 * So we must track each and every connection made. */
		data_cal_factory_connections_add (
			factory, sender, E_CAL_BACKEND (backend));

		g_object_unref (data_cal);
	}

	g_object_unref (backend);

	return object_path;
}

static gboolean
data_cal_factory_handle_open_calendar_cb (EDBusCalendarFactory *dbus_interface,
                                          GDBusMethodInvocation *invocation,
                                          const gchar *uid,
                                          EDataCalFactory *factory)
{
	GDBusConnection *connection;
	const gchar *sender;
	gchar *object_path;
	GError *error = NULL;

	connection = g_dbus_method_invocation_get_connection (invocation);
	sender = g_dbus_method_invocation_get_sender (invocation);

	object_path = data_cal_factory_open (
		factory, connection, sender, uid,
		E_SOURCE_EXTENSION_CALENDAR, "VEVENT", &error);

	if (object_path != NULL) {
		e_dbus_calendar_factory_complete_open_calendar (
			dbus_interface, invocation, object_path);
		g_free (object_path);
	} else {
		g_return_val_if_fail (error != NULL, FALSE);
		g_dbus_method_invocation_take_error (invocation, error);
	}

	return TRUE;
}

static gboolean
data_cal_factory_handle_open_task_list_cb (EDBusCalendarFactory *dbus_interface,
                                           GDBusMethodInvocation *invocation,
                                           const gchar *uid,
                                           EDataCalFactory *factory)
{
	GDBusConnection *connection;
	const gchar *sender;
	gchar *object_path;
	GError *error = NULL;

	connection = g_dbus_method_invocation_get_connection (invocation);
	sender = g_dbus_method_invocation_get_sender (invocation);

	object_path = data_cal_factory_open (
		factory, connection, sender, uid,
		E_SOURCE_EXTENSION_TASK_LIST, "VTODO", &error);

	if (object_path != NULL) {
		e_dbus_calendar_factory_complete_open_task_list (
			dbus_interface, invocation, object_path);
		g_free (object_path);
	} else {
		g_return_val_if_fail (error != NULL, FALSE);
		g_dbus_method_invocation_take_error (invocation, error);
	}

	return TRUE;
}

static gboolean
data_cal_factory_handle_open_memo_list_cb (EDBusCalendarFactory *dbus_interface,
                                           GDBusMethodInvocation *invocation,
                                           const gchar *uid,
                                           EDataCalFactory *factory)
{
	GDBusConnection *connection;
	const gchar *sender;
	gchar *object_path;
	GError *error = NULL;

	connection = g_dbus_method_invocation_get_connection (invocation);
	sender = g_dbus_method_invocation_get_sender (invocation);

	object_path = data_cal_factory_open (
		factory, connection, sender, uid,
		E_SOURCE_EXTENSION_MEMO_LIST, "VJOURNAL", &error);

	if (object_path != NULL) {
		e_dbus_calendar_factory_complete_open_memo_list (
			dbus_interface, invocation, object_path);
		g_free (object_path);
	} else {
		g_return_val_if_fail (error != NULL, FALSE);
		g_dbus_method_invocation_take_error (invocation, error);
	}

	return TRUE;
}

static void
data_cal_factory_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_REGISTRY:
			g_value_set_object (
				value,
				e_data_cal_factory_get_registry (
				E_DATA_CAL_FACTORY (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_cal_factory_dispose (GObject *object)
{
	EDataCalFactoryPrivate *priv;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (object);

	if (priv->registry != NULL) {
		g_object_unref (priv->registry);
		priv->registry = NULL;
	}

	if (priv->dbus_factory != NULL) {
		g_object_unref (priv->dbus_factory);
		priv->dbus_factory = NULL;
	}

	g_hash_table_remove_all (priv->connections);
	g_hash_table_remove_all (priv->watched_names);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_data_cal_factory_parent_class)->dispose (object);
}

static void
data_cal_factory_finalize (GObject *object)
{
	EDataCalFactoryPrivate *priv;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (object);

	g_hash_table_destroy (priv->connections);
	g_mutex_clear (&priv->connections_lock);

	g_hash_table_destroy (priv->watched_names);
	g_mutex_clear (&priv->watched_names_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_cal_factory_parent_class)->finalize (object);
}

static void
data_cal_factory_bus_acquired (EDBusServer *server,
                               GDBusConnection *connection)
{
	EDataCalFactoryPrivate *priv;
	GError *error = NULL;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (server);

	g_dbus_interface_skeleton_export (
		G_DBUS_INTERFACE_SKELETON (priv->dbus_factory),
		connection,
		"/org/gnome/evolution/dataserver/CalendarFactory",
		&error);

	if (error != NULL) {
		g_error (
			"Failed to export CalendarFactory interface: %s",
			error->message);
		g_assert_not_reached ();
	}

	/* Chain up to parent's bus_acquired() method. */
	E_DBUS_SERVER_CLASS (e_data_cal_factory_parent_class)->
		bus_acquired (server, connection);
}

static void
data_cal_factory_bus_name_lost (EDBusServer *server,
                                GDBusConnection *connection)
{
	EDataCalFactory *factory;

	factory = E_DATA_CAL_FACTORY (server);

	data_cal_factory_connections_remove_all (factory);

	/* Chain up to parent's bus_name_lost() method. */
	E_DBUS_SERVER_CLASS (e_data_cal_factory_parent_class)->
		bus_name_lost (server, connection);
}

static void
data_cal_factory_quit_server (EDBusServer *server,
                              EDBusServerExitCode exit_code)
{
	/* This factory does not support reloading, so stop the signal
	 * emission and return without chaining up to prevent quitting. */
	if (exit_code == E_DBUS_SERVER_EXIT_RELOAD) {
		g_signal_stop_emission_by_name (server, "quit-server");
		return;
	}

	/* Chain up to parent's quit_server() method. */
	E_DBUS_SERVER_CLASS (e_data_cal_factory_parent_class)->
		quit_server (server, exit_code);
}

static gboolean
data_cal_factory_initable_init (GInitable *initable,
                                GCancellable *cancellable,
                                GError **error)
{
	EDataCalFactoryPrivate *priv;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (initable);

	priv->registry = e_source_registry_new_sync (cancellable, error);

	return (priv->registry != NULL);
}

static void
e_data_cal_factory_class_init (EDataCalFactoryClass *class)
{
	GObjectClass *object_class;
	EDBusServerClass *dbus_server_class;
	EDataFactoryClass *data_factory_class;
	const gchar *modules_directory = BACKENDDIR;
	const gchar *modules_directory_env;

	modules_directory_env = g_getenv (EDS_CALENDAR_MODULES);
	if (modules_directory_env &&
	    g_file_test (modules_directory_env, G_FILE_TEST_IS_DIR))
		modules_directory = g_strdup (modules_directory_env);

	g_type_class_add_private (class, sizeof (EDataCalFactoryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->get_property = data_cal_factory_get_property;
	object_class->dispose = data_cal_factory_dispose;
	object_class->finalize = data_cal_factory_finalize;

	dbus_server_class = E_DBUS_SERVER_CLASS (class);
	dbus_server_class->bus_name = CALENDAR_DBUS_SERVICE_NAME;
	dbus_server_class->module_directory = modules_directory;
	dbus_server_class->bus_acquired = data_cal_factory_bus_acquired;
	dbus_server_class->bus_name_lost = data_cal_factory_bus_name_lost;
	dbus_server_class->quit_server = data_cal_factory_quit_server;

	data_factory_class = E_DATA_FACTORY_CLASS (class);
	data_factory_class->backend_factory_type = E_TYPE_CAL_BACKEND_FACTORY;

	g_object_class_install_property (
		object_class,
		PROP_REGISTRY,
		g_param_spec_object (
			"registry",
			"Registry",
			"Data source registry",
			E_TYPE_SOURCE_REGISTRY,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));
}

static void
e_data_cal_factory_initable_init (GInitableIface *iface)
{
	iface->init = data_cal_factory_initable_init;
}

static void
e_data_cal_factory_init (EDataCalFactory *factory)
{
	factory->priv = E_DATA_CAL_FACTORY_GET_PRIVATE (factory);

	factory->priv->dbus_factory =
		e_dbus_calendar_factory_skeleton_new ();

	g_signal_connect (
		factory->priv->dbus_factory, "handle-open-calendar",
		G_CALLBACK (data_cal_factory_handle_open_calendar_cb),
		factory);

	g_signal_connect (
		factory->priv->dbus_factory, "handle-open-task-list",
		G_CALLBACK (data_cal_factory_handle_open_task_list_cb),
		factory);

	g_signal_connect (
		factory->priv->dbus_factory, "handle-open-memo-list",
		G_CALLBACK (data_cal_factory_handle_open_memo_list_cb),
		factory);

	factory->priv->connections = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_ptr_array_unref);

	g_mutex_init (&factory->priv->connections_lock);
	g_mutex_init (&factory->priv->watched_names_lock);

	factory->priv->watched_names = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) watched_names_value_free);
}

EDBusServer *
e_data_cal_factory_new (GCancellable *cancellable,
                        GError **error)
{
	icalarray *builtin_timezones;
	gint ii;

#ifdef HAVE_ICAL_UNKNOWN_TOKEN_HANDLING
	ical_set_unknown_token_handling_setting (ICAL_DISCARD_TOKEN);
#endif

	/* XXX Pre-load all built-in timezones in libical.
	 *
	 *     Built-in time zones in libical 0.43 are loaded on demand,
	 *     but not in a thread-safe manner, resulting in a race when
	 *     multiple threads call icaltimezone_load_builtin_timezone()
	 *     on the same time zone.  Until built-in time zone loading
	 *     in libical is made thread-safe, work around the issue by
	 *     loading all built-in time zones now, so libical's internal
	 *     time zone array will be fully populated before any threads
	 *     are spawned.
	 */
	builtin_timezones = icaltimezone_get_builtin_timezones ();
	for (ii = 0; ii < builtin_timezones->num_elements; ii++) {
		icaltimezone *zone;

		zone = icalarray_element_at (builtin_timezones, ii);

		/* We don't care about the component right now,
		 * we just need some function that will trigger
		 * icaltimezone_load_builtin_timezone(). */
		icaltimezone_get_component (zone);
	}

	return g_initable_new (
		E_TYPE_DATA_CAL_FACTORY,
		cancellable, error, NULL);
}

/**
 * e_data_cal_factory_get_registry:
 * @factory: an #EDataCalFactory
 *
 * Returns the #ESourceRegistry owned by @factory.
 *
 * Returns: the #ESourceRegistry
 *
 * Since: 3.6
 **/
ESourceRegistry *
e_data_cal_factory_get_registry (EDataCalFactory *factory)
{
	g_return_val_if_fail (E_IS_DATA_CAL_FACTORY (factory), NULL);

	return factory->priv->registry;
}

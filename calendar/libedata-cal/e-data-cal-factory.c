/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar factory
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          JP Rosevear <jpr@ximian.com>
 *          Ross Burton <ross@linux.intel.com>
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
	EDBusCalendarFactory *dbus_factory;
};

static GInitableIface *initable_parent_interface;

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

static GDBusInterfaceSkeleton *
data_cal_factory_get_dbus_interface_skeleton (EDBusServer *server)
{
	EDataCalFactory *factory;

	factory = E_DATA_CAL_FACTORY (server);

	return G_DBUS_INTERFACE_SKELETON (factory->priv->dbus_factory);
}

static const gchar *
data_cal_get_factory_name (EBackendFactory *backend_factory)
{
	ECalBackendFactoryClass *class;

	class = E_CAL_BACKEND_FACTORY_GET_CLASS (E_CAL_BACKEND_FACTORY (backend_factory));

	return class->factory_name;
}

static void
data_cal_complete_calendar_open (EDataFactory *data_factory,
				 GDBusMethodInvocation *invocation,
				 const gchar *object_path,
				 const gchar *bus_name)
{
	EDataCalFactory *data_cal_factory = E_DATA_CAL_FACTORY (data_factory);

	e_dbus_calendar_factory_complete_open_calendar (
		data_cal_factory->priv->dbus_factory, invocation, object_path, bus_name);
}

static void
data_cal_complete_task_list_open (EDataFactory *data_factory,
				  GDBusMethodInvocation *invocation,
				  const gchar *object_path,
				  const gchar *bus_name)
{
	EDataCalFactory *data_cal_factory = E_DATA_CAL_FACTORY (data_factory);

	e_dbus_calendar_factory_complete_open_task_list (
		data_cal_factory->priv->dbus_factory, invocation, object_path, bus_name);
}

static void
data_cal_complete_memo_list_open (EDataFactory *data_factory,
				  GDBusMethodInvocation *invocation,
				  const gchar *object_path,
				  const gchar *bus_name)
{
	EDataCalFactory *data_cal_factory = E_DATA_CAL_FACTORY (data_factory);

	e_dbus_calendar_factory_complete_open_memo_list (
		data_cal_factory->priv->dbus_factory, invocation, object_path, bus_name);
}

static void
data_cal_complete_open (EDataFactory *data_factory,
			GDBusMethodInvocation *invocation,
			const gchar *object_path,
			const gchar *bus_name,
			const gchar *extension_name)
{
	if (g_strcmp0 (extension_name, E_SOURCE_EXTENSION_CALENDAR) == 0)
		data_cal_complete_calendar_open (data_factory, invocation, object_path, bus_name);
	else if (g_strcmp0 (extension_name, E_SOURCE_EXTENSION_TASK_LIST) == 0)
		data_cal_complete_task_list_open (data_factory, invocation, object_path, bus_name);
	else if (g_strcmp0 (extension_name, E_SOURCE_EXTENSION_MEMO_LIST) == 0)
		data_cal_complete_memo_list_open (data_factory, invocation, object_path, bus_name);
}

static gboolean
data_cal_factory_initable_init (GInitable *initable,
			        GCancellable *cancellable,
				GError **error)
{
	/* Chain up to parent interface's init() method. */
	return initable_parent_interface->init (initable, cancellable, error);
}

static void
e_data_cal_factory_initable_init (GInitableIface *iface)
{
	initable_parent_interface = g_type_interface_peek_parent (iface);

	iface->init = data_cal_factory_initable_init;
}

static gchar *overwrite_subprocess_cal_path = NULL;

static void
e_data_cal_factory_class_init (EDataCalFactoryClass *class)
{
	EDBusServerClass *dbus_server_class;
	EDataFactoryClass *data_factory_class;
	const gchar *modules_directory = BACKENDDIR;
	const gchar *modules_directory_env;
	const gchar *subprocess_cal_path_env;

	modules_directory_env = g_getenv (EDS_CALENDAR_MODULES);
	if (modules_directory_env &&
	    g_file_test (modules_directory_env, G_FILE_TEST_IS_DIR))
		modules_directory = g_strdup (modules_directory_env);

	subprocess_cal_path_env = g_getenv (EDS_SUBPROCESS_CAL_PATH);
	if (subprocess_cal_path_env &&
	    g_file_test (subprocess_cal_path_env, G_FILE_TEST_IS_EXECUTABLE))
		overwrite_subprocess_cal_path = g_strdup (subprocess_cal_path_env);

	g_type_class_add_private (class, sizeof (EDataCalFactoryPrivate));

	dbus_server_class = E_DBUS_SERVER_CLASS (class);
	dbus_server_class->bus_name = CALENDAR_DBUS_SERVICE_NAME;
	dbus_server_class->module_directory = modules_directory;

	data_factory_class = E_DATA_FACTORY_CLASS (class);
	data_factory_class->backend_factory_type = E_TYPE_CAL_BACKEND_FACTORY;
	data_factory_class->factory_object_path = "/org/gnome/evolution/dataserver/CalendarFactory";
	data_factory_class->subprocess_object_path_prefix = "/org/gnome/evolution/dataserver/Subprocess/Backend/Calendar";
	data_factory_class->subprocess_bus_name_prefix = "org.gnome.evolution.dataserver.Subprocess.Backend.Calendar";
	data_factory_class->get_dbus_interface_skeleton = data_cal_factory_get_dbus_interface_skeleton;
	data_factory_class->get_factory_name = data_cal_get_factory_name;
	data_factory_class->complete_open = data_cal_complete_open;
}

static gboolean
data_cal_factory_handle_open_calendar_cb (EDBusCalendarFactory *dbus_interface,
					  GDBusMethodInvocation *invocation,
					  const gchar *uid,
					  EDataCalFactory *factory)
{
	EDataFactory *data_factory = E_DATA_FACTORY (factory);

	e_data_factory_spawn_subprocess_backend (
		data_factory, invocation, uid, E_SOURCE_EXTENSION_CALENDAR,
		overwrite_subprocess_cal_path ? overwrite_subprocess_cal_path : SUBPROCESS_CAL_BACKEND_PATH);

	return TRUE;
}

static gboolean
data_cal_factory_handle_open_task_list_cb (EDBusCalendarFactory *dbus_interface,
					   GDBusMethodInvocation *invocation,
					   const gchar *uid,
					   EDataCalFactory *factory)
{
	EDataFactory *data_factory = E_DATA_FACTORY (factory);

	e_data_factory_spawn_subprocess_backend (
		data_factory, invocation, uid, E_SOURCE_EXTENSION_TASK_LIST,
		overwrite_subprocess_cal_path ? overwrite_subprocess_cal_path : SUBPROCESS_CAL_BACKEND_PATH);

	return TRUE;
}

static gboolean
data_cal_factory_handle_open_memo_list_cb (EDBusCalendarFactory *dbus_interface,
					   GDBusMethodInvocation *invocation,
					   const gchar *uid,
					   EDataCalFactory *factory)
{
	EDataFactory *data_factory = E_DATA_FACTORY (factory);

	e_data_factory_spawn_subprocess_backend (
		data_factory, invocation, uid, E_SOURCE_EXTENSION_MEMO_LIST,
		overwrite_subprocess_cal_path ? overwrite_subprocess_cal_path : SUBPROCESS_CAL_BACKEND_PATH);

	return TRUE;
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

#ifdef HAVE_ICALTZUTIL_SET_EXACT_VTIMEZONES_SUPPORT
	icaltzutil_set_exact_vtimezones_support (0);
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
		cancellable, error,
		"reload-supported", TRUE, NULL);
}

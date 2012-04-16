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
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <config.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gi18n.h>

#include "e-cal-backend.h"
#include "e-cal-backend-factory.h"
#include "e-data-cal.h"
#include "e-data-cal-factory.h"

#include "e-gdbus-cal-factory.h"

#include <libical/ical.h>

#define d(x)

#define E_DATA_CAL_FACTORY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_CAL_FACTORY, EDataCalFactoryPrivate))

struct _EDataCalFactoryPrivate {
	EGdbusCalFactory *gdbus_object;

	GMutex *calendars_lock;
	/* A hash of object paths for calendar URIs to EDataCals */
	GHashTable *calendars;

	GMutex *connections_lock;
	/* This is a hash of client addresses to GList* of EDataCals */
	GHashTable *connections;
};

/* Forward Declarations */
static void	e_data_cal_factory_initable_init
						(GInitableIface *interface);

G_DEFINE_TYPE_WITH_CODE (
	EDataCalFactory,
	e_data_cal_factory,
	E_TYPE_DATA_FACTORY,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_data_cal_factory_initable_init))

static const gchar *
calobjtype_to_string (const EDataCalObjType type)
{
	switch (type) {
	case Event:
		return "VEVENT";
	case Todo:
		return "VTODO";
	case Journal:
		return "VJOURNAL";
	case AnyType:
		break;
	}

	g_return_val_if_reached (NULL);
}

static gchar *
e_data_cal_factory_extract_proto_from_uri (const gchar *uri)
{
	gchar *proto, *cp;

	cp = strchr (uri, ':');
	if (cp == NULL)
		return NULL;

	proto = g_malloc0 (cp - uri + 1);
	strncpy (proto, uri, cp - uri);

	return proto;
}

static EBackend *
data_cal_factory_ref_backend (EDataCalFactory *factory,
                              ESource *source,
                              const gchar *uri,
                              EDataCalObjType type)
{
	EBackend *backend;
	gchar *protocol;
	gchar *hash_key;

	protocol = e_data_cal_factory_extract_proto_from_uri (uri);
	if (protocol == NULL) {
		g_warning ("Cannot extract protocol from URI %s", uri);
		return NULL;
	}

	hash_key = g_strdup_printf (
		"%s:%s", protocol, calobjtype_to_string (type));

	backend = e_data_factory_ref_backend (
		E_DATA_FACTORY (factory), hash_key, source);

	g_free (hash_key);
	g_free (protocol);

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

static gboolean
remove_dead_calendar_cb (gpointer path,
                         gpointer calendar,
                         gpointer dead_calendar)
{
	return calendar == dead_calendar;
}

static void
calendar_freed_cb (EDataCalFactory *factory,
                   GObject *dead)
{
	EDataCalFactoryPrivate *priv = factory->priv;
	GHashTableIter iter;
	gpointer hkey, hvalue;

	d (g_debug ("in factory %p (%p) is dead", factory, dead));

	g_hash_table_foreach_remove (
		priv->calendars, remove_dead_calendar_cb, dead);

	g_hash_table_iter_init (&iter, priv->connections);
	while (g_hash_table_iter_next (&iter, &hkey, &hvalue)) {
		GList *calendars = hvalue;

		if (g_list_find (calendars, dead)) {
			calendars = g_list_remove (calendars, dead);
			if (calendars != NULL)
				g_hash_table_insert (
					priv->connections,
					g_strdup (hkey), calendars);
			else
				g_hash_table_remove (priv->connections, hkey);

			break;
		}
	}

	e_dbus_server_release (E_DBUS_SERVER (factory));
}

static gboolean
impl_CalFactory_get_cal (EGdbusCalFactory *object,
                         GDBusMethodInvocation *invocation,
                         const gchar * const *in_source_type,
                         EDataCalFactory *factory)
{
	EDataCal *calendar;
	EBackend *backend;
	EDataCalFactoryPrivate *priv = factory->priv;
	GDBusConnection *connection;
	ESource *source;
	gchar *uri;
	gchar *path = NULL;
	const gchar *sender;
	GList *list;
	GError *error = NULL;
	gchar *source_xml = NULL;
	guint type = 0;

	sender = g_dbus_method_invocation_get_sender (invocation);
	connection = g_dbus_method_invocation_get_connection (invocation);

	if (!e_gdbus_cal_factory_decode_get_cal (in_source_type, &source_xml, &type)) {
		error = g_error_new (
			E_DATA_CAL_ERROR, NoSuchCal, _("Invalid call"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	source = e_source_new_from_standalone_xml (source_xml);
	g_free (source_xml);

	if (!source) {
		error = g_error_new (
			E_DATA_CAL_ERROR,
			NoSuchCal,
			_("Invalid source"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	uri = e_source_get_uri (source);

	if (uri == NULL || *uri == '\0') {
		g_object_unref (source);
		g_free (uri);

		error = g_error_new (
			E_DATA_CAL_ERROR,
			NoSuchCal,
			_("Empty URI"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	backend = data_cal_factory_ref_backend (factory, source, uri, type);

	if (backend == NULL) {
		error = g_error_new (
			E_DATA_CAL_ERROR,
			NoSuchCal,
			_("Invalid source"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	g_mutex_lock (priv->calendars_lock);

	e_dbus_server_hold (E_DBUS_SERVER (factory));

	path = construct_cal_factory_path ();
	calendar = e_data_cal_new (E_CAL_BACKEND (backend));
	g_hash_table_insert (priv->calendars, g_strdup (path), calendar);
	e_cal_backend_add_client (E_CAL_BACKEND (backend), calendar);
	e_data_cal_register_gdbus_object (calendar, connection, path, &error);
	g_object_weak_ref (
		G_OBJECT (calendar), (GWeakNotify)
		calendar_freed_cb, factory);

	g_object_unref (backend);

	/* Update the hash of open connections. */
	g_mutex_lock (priv->connections_lock);
	list = g_hash_table_lookup (priv->connections, sender);
	list = g_list_prepend (list, calendar);
	g_hash_table_insert (priv->connections, g_strdup (sender), list);
	g_mutex_unlock (priv->connections_lock);

	g_mutex_unlock (priv->calendars_lock);

	g_object_unref (source);
	g_free (uri);

	e_gdbus_cal_factory_complete_get_cal (
		object, invocation, path, error);

	if (error)
		g_error_free (error);

	g_free (path);

	return TRUE;
}

static void
remove_data_cal_cb (EDataCal *data_cal)
{
	ECalBackend *backend;

	g_return_if_fail (data_cal != NULL);

	backend = e_data_cal_get_backend (data_cal);
	e_cal_backend_remove_client (backend, data_cal);

	g_object_unref (data_cal);
}

static void
data_cal_factory_dispose (GObject *object)
{
	EDataCalFactoryPrivate *priv;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (object);

	if (priv->gdbus_object != NULL) {
		g_object_unref (priv->gdbus_object);
		priv->gdbus_object = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_data_cal_factory_parent_class)->dispose (object);
}

static void
data_cal_factory_finalize (GObject *object)
{
	EDataCalFactoryPrivate *priv;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (object);

	g_hash_table_destroy (priv->calendars);
	g_hash_table_destroy (priv->connections);

	g_mutex_free (priv->calendars_lock);
	g_mutex_free (priv->connections_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_cal_factory_parent_class)->finalize (object);
}

static void
data_cal_factory_bus_acquired (EDBusServer *server,
                               GDBusConnection *connection)
{
	EDataCalFactoryPrivate *priv;
	guint registration_id;
	GError *error = NULL;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (server);

	registration_id = e_gdbus_cal_factory_register_object (
		priv->gdbus_object,
		connection,
		"/org/gnome/evolution/dataserver/CalendarFactory",
		&error);

	if (error != NULL) {
		g_error (
			"Failed to register a CalendarFactory object: %s",
			error->message);
		g_assert_not_reached ();
	}

	g_assert (registration_id > 0);

	/* Chain up to parent's bus_acquired() method. */
	E_DBUS_SERVER_CLASS (e_data_cal_factory_parent_class)->
		bus_acquired (server, connection);
}

static void
data_cal_factory_bus_name_lost (EDBusServer *server,
                                GDBusConnection *connection)
{
	EDataCalFactoryPrivate *priv;
	GList *list = NULL;
	gchar *key;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (server);

	g_mutex_lock (priv->connections_lock);

	while (g_hash_table_lookup_extended (
		priv->connections,
		CALENDAR_DBUS_SERVICE_NAME,
		(gpointer) &key, (gpointer) &list)) {
		GList *copy;

		/* this should trigger the calendar's weak ref notify
		 * function, which will remove it from the list before
		 * it's freed, and will remove the connection from
		 * priv->connections once they're all gone */
		copy = g_list_copy (list);
		g_list_foreach (copy, (GFunc) remove_data_cal_cb, NULL);
		g_list_free (copy);
	}

	g_mutex_unlock (priv->connections_lock);

	/* Chain up to parent's bus_name_lost() method. */
	E_DBUS_SERVER_CLASS (e_data_cal_factory_parent_class)->
		bus_name_lost (server, connection);
}

static gboolean
data_cal_factory_initable_init (GInitable *initable,
                                GCancellable *cancellable,
                                GError **error)
{
	/* XXX Nothing to do here just yet.  More to come soon. */

	return TRUE;
}

static void
e_data_cal_factory_class_init (EDataCalFactoryClass *class)
{
	GObjectClass *object_class;
	EDBusServerClass *dbus_server_class;
	EDataFactoryClass *data_factory_class;

	g_type_class_add_private (class, sizeof (EDataCalFactoryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = data_cal_factory_dispose;
	object_class->finalize = data_cal_factory_finalize;

	dbus_server_class = E_DBUS_SERVER_CLASS (class);
	dbus_server_class->bus_name = CALENDAR_DBUS_SERVICE_NAME;
	dbus_server_class->module_directory = BACKENDDIR;
	dbus_server_class->bus_acquired = data_cal_factory_bus_acquired;
	dbus_server_class->bus_name_lost = data_cal_factory_bus_name_lost;

	data_factory_class = E_DATA_FACTORY_CLASS (class);
	data_factory_class->backend_factory_type = E_TYPE_CAL_BACKEND_FACTORY;
}

static void
e_data_cal_factory_initable_init (GInitableIface *interface)
{
	interface->init = data_cal_factory_initable_init;
}

static void
e_data_cal_factory_init (EDataCalFactory *factory)
{
	factory->priv = E_DATA_CAL_FACTORY_GET_PRIVATE (factory);

	factory->priv->gdbus_object = e_gdbus_cal_factory_stub_new ();
	g_signal_connect (
		factory->priv->gdbus_object, "handle-get-cal",
		G_CALLBACK (impl_CalFactory_get_cal), factory);

	factory->priv->calendars_lock = g_mutex_new ();
	factory->priv->calendars = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	factory->priv->connections_lock = g_mutex_new ();
	factory->priv->connections = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);
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

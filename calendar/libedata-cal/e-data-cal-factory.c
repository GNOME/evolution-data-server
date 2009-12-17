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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus-glib-bindings.h>

#include "libedataserver/e-url.h"
#include "libedataserver/e-source.h"
#include "libedataserver/e-source-list.h"
#include "libebackend/e-data-server-module.h"
#include <libebackend/e-offline-listener.h>
#include "libecal/e-cal.h"
#include "e-cal-backend.h"
#include "e-cal-backend-factory.h"
#include "e-data-cal.h"
#include "e-data-cal-factory.h"
#include "e-cal-backend-loader-factory.h"

#define d(x)

static void impl_CalFactory_getCal (EDataCalFactory *factory, const gchar *IN_uri, EDataCalObjType type, DBusGMethodInvocation *context);
#include "e-data-cal-factory-glue.h"

static GMainLoop *loop;
static EDataCalFactory *factory;
extern DBusGConnection *connection;

/* Convenience macro to test and set a GError/return on failure */
#define g_set_error_val_if_fail(test, returnval, error, domain, code) G_STMT_START{ \
		if G_LIKELY (test) {} else {				\
			g_set_error (error, domain, code, #test);	\
			g_warning(#test " failed");			\
			return (returnval);				\
		}							\
	} G_STMT_END

G_DEFINE_TYPE(EDataCalFactory, e_data_cal_factory, G_TYPE_OBJECT);

#define E_DATA_CAL_FACTORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_DATA_CAL_FACTORY, EDataCalFactoryPrivate))

struct _EDataCalFactoryPrivate {
	/* Hash table from URI method strings to GType * for backend class types */
	GHashTable *methods;

	GHashTable *backends;
	/* mutex to access backends hash table */
	GMutex *backends_mutex;

	GHashTable *calendars;

	GHashTable *connections;

	gint mode;

	/* this is for notifications of source changes */
	ESourceList *lists[E_CAL_SOURCE_TYPE_LAST];

	/* backends divided by their type */
	GSList *backends_by_type[E_CAL_SOURCE_TYPE_LAST];

	guint exit_timeout;
};

/* Create the EDataCalFactory error quark */
GQuark
e_data_cal_factory_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("e_data_cal_factory_error");
	return quark;
}

static icalcomponent_kind
calobjtype_to_icalkind (const EDataCalObjType type)
{
	switch (type) {
	case Event:
		return ICAL_VEVENT_COMPONENT;
	case Todo:
		return ICAL_VTODO_COMPONENT;
	case Journal:
		return ICAL_VJOURNAL_COMPONENT;
	case AnyType:
		return ICAL_NO_COMPONENT;
	}

	return ICAL_NO_COMPONENT;
}

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

	return "UNKNOWN COMPONENT";
}

static ECalSourceType
icalkind_to_ecalsourcetype (const icalcomponent_kind kind)
{
	switch (kind) {
	case ICAL_VEVENT_COMPONENT:
		return E_CAL_SOURCE_TYPE_EVENT;
	case ICAL_VTODO_COMPONENT:
		return E_CAL_SOURCE_TYPE_TODO;
	case ICAL_VJOURNAL_COMPONENT:
		return E_CAL_SOURCE_TYPE_JOURNAL;
	default:
		break;
	}

	return E_CAL_SOURCE_TYPE_LAST;
}

static void
update_source_in_backend (ECalBackend *backend, ESource *updated_source)
{
	xmlNodePtr xml;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (updated_source != NULL);

	xml = xmlNewNode (NULL, (const xmlChar *)"dummy");
	e_source_dump_to_xml_node (updated_source, xml);
	e_source_update_from_xml_node (e_cal_backend_get_source (backend), xml->children, NULL);
	xmlFreeNode (xml);
}

static void
source_list_changed_cb (ESourceList *list, EDataCalFactory *factory)
{
	EDataCalFactoryPrivate *priv;
	gint i;

	g_return_if_fail (list != NULL);
	g_return_if_fail (factory != NULL);
	g_return_if_fail (E_IS_DATA_CAL_FACTORY (factory));

	priv = factory->priv;

	g_mutex_lock (priv->backends_mutex);

	for (i = 0; i < E_CAL_SOURCE_TYPE_LAST; i++) {
		if (list == priv->lists[i]) {
			GSList *l;

			for (l = priv->backends_by_type [i]; l; l = l->next) {
				ECalBackend *backend = l->data;
				ESource *source, *list_source;

				source = e_cal_backend_get_source (backend);
				list_source = e_source_list_peek_source_by_uid (priv->lists[i], e_source_peek_uid (source));

				if (list_source) {
					update_source_in_backend (backend, list_source);
				}
			}

			break;
		}
	}

	g_mutex_unlock (priv->backends_mutex);
}

static ECalBackendFactory *
get_backend_factory (GHashTable *methods, const gchar *method, icalcomponent_kind kind)
{
	GHashTable *kinds;
	ECalBackendFactory *factory;

	kinds = g_hash_table_lookup (methods, method);
	if (!kinds) {
		return NULL;
	}

	factory = g_hash_table_lookup (kinds, GINT_TO_POINTER (kind));

	return factory;
}

static gchar *
construct_cal_factory_path (void)
{
	static volatile gint counter = 1;

	return g_strdup_printf (
		"/org/gnome/evolution/dataserver/calendar/%d/%u",
		getpid (),
		g_atomic_int_exchange_and_add (&counter, 1));
}

static void
my_remove (gchar *key, GObject *dead)
{
	EDataCalFactoryPrivate *priv = factory->priv;
	GHashTableIter iter;
	gpointer hkey, hvalue;

	d (g_debug ("%s (%p) is dead", key, dead));

	g_hash_table_remove (priv->calendars, key);

	g_hash_table_iter_init (&iter, priv->connections);
	while (g_hash_table_iter_next (&iter, &hkey, &hvalue)) {
		GList *calendars = hvalue;

		if (g_list_find (calendars, dead)) {
			calendars = g_list_remove (calendars, dead);
			if (calendars) {
				g_hash_table_insert (priv->connections, g_strdup (hkey), calendars);
			} else {
				g_hash_table_remove (priv->connections, hkey);
			}

			break;
		}
	}

	g_free (key);

	/* If there are no open calendars, start a timer to quit */
	if (priv->exit_timeout == 0 && g_hash_table_size (priv->calendars) == 0) {
		priv->exit_timeout = g_timeout_add (10000, (GSourceFunc)g_main_loop_quit, loop);
	}
}

struct find_backend_data
{
	const gchar *str_uri;
	ECalBackend *backend;
	icalcomponent_kind kind;
};

static void
find_backend_cb (gpointer key, gpointer value, gpointer data)
{
	struct find_backend_data *fbd = data;

	if (fbd && fbd->str_uri && !fbd->backend) {
		ECalBackend *backend = value;
		gchar *str_uri;

		str_uri = e_source_get_uri (e_cal_backend_get_source (backend));

		if (str_uri && g_str_equal (str_uri, fbd->str_uri)) {
			const gchar *uid_kind = key, *pos;

			pos = strrchr (uid_kind, ':');
			if (pos && atoi (pos + 1) == fbd->kind)
				fbd->backend = backend;
		}

		g_free (str_uri);
	}
}

static void
impl_CalFactory_getCal (EDataCalFactory		*factory,
                        const gchar		*source_xml,
                        EDataCalObjType		 type,
                        DBusGMethodInvocation	*context)
{
	EDataCal *calendar;
	EDataCalFactoryPrivate *priv = factory->priv;
	ECalBackendFactory *backend_factory;
	ECalBackend *backend;
	ESource *source;
	gchar *str_uri;
	EUri *uri;
	gchar *uid_type_string;
	gchar *path = NULL, *sender;
	GList *list;
	GError *error = NULL;

	/* Remove a pending exit */
	if (priv->exit_timeout) {
		g_source_remove (priv->exit_timeout);
		priv->exit_timeout = 0;
	}

	source = e_source_new_from_standalone_xml (source_xml);
	if (!source) {
		dbus_g_method_return_error (context, g_error_new (E_DATA_CAL_ERROR, NoSuchCal, _("Invalid source")));
		return;
	}

	/* Get the URI so we can extract the protocol */
	str_uri = e_source_get_uri (source);
	if (!str_uri) {
		g_object_unref (source);

		dbus_g_method_return_error (context, g_error_new (E_DATA_CAL_ERROR, NoSuchCal, _("Invalid source")));
		return;
	}

	/* Parse the uri */
	uri = e_uri_new (str_uri);
	if (!uri) {
		dbus_g_method_return_error (context, g_error_new (E_DATA_CAL_ERROR, NoSuchCal, _("Invalid URI")));
		return;
	}

	uid_type_string = g_strdup_printf ("%s:%d", e_source_peek_uid (source), (gint)calobjtype_to_icalkind (type));

	/* Find the associated backend factory (if any) */
	backend_factory = get_backend_factory (priv->methods, uri->protocol, calobjtype_to_icalkind (type));
	if (!backend_factory) {
		error = g_error_new (
			E_DATA_CAL_ERROR, NoSuchCal,
			_("No backend factory for '%s' of '%s'"),
			uri->protocol, calobjtype_to_string (type));

		goto cleanup2;
	}

	g_mutex_lock (priv->backends_mutex);

	/* Look for an existing backend */
	backend = g_hash_table_lookup (factory->priv->backends, uid_type_string);

	if (!backend) {
		/* find backend by URL, if opened, thus functions like e_cal_system_new_* will not
		   create new backends for the same url */
		struct find_backend_data fbd;

		fbd.str_uri = str_uri;
		fbd.kind = calobjtype_to_icalkind (type);
		fbd.backend = NULL;

		g_hash_table_foreach (priv->backends, find_backend_cb, &fbd);

		if (fbd.backend) {
			backend = fbd.backend;
			g_object_unref (source);
			source = g_object_ref (e_cal_backend_get_source (backend));
		}
	}

	if (!backend) {
		ECalSourceType st;

		/* There was no existing backend, create a new one */
		if (E_IS_CAL_BACKEND_LOADER_FACTORY (backend_factory)) {
			backend = E_CAL_BACKEND_LOADER_FACTORY_GET_CLASS (backend_factory)->new_backend_with_protocol ((ECalBackendLoaderFactory *)backend_factory,
														       source, uri->protocol);
		} else
			backend = e_cal_backend_factory_new_backend (backend_factory, source);

		if (!backend) {
			error = g_error_new (E_DATA_CAL_ERROR, NoSuchCal, _("Could not instantiate backend"));
			goto cleanup;
		}

		st = icalkind_to_ecalsourcetype (e_cal_backend_get_kind (backend));
		if (st != E_CAL_SOURCE_TYPE_LAST) {
			if (!priv->lists[st] && e_cal_get_sources (&(priv->lists[st]), st, NULL)) {
				g_signal_connect (priv->lists[st], "changed", G_CALLBACK (source_list_changed_cb), factory);
			}

			if (priv->lists[st])
				priv->backends_by_type[st] = g_slist_prepend (priv->backends_by_type[st], backend);
		}

		/* Track the backend */
		g_hash_table_insert (priv->backends, g_strdup (uid_type_string), backend);

		e_cal_backend_set_mode (backend, priv->mode);
	} else if (!e_source_equal (source, e_cal_backend_get_source (backend))) {
		/* source changed, update it in a backend */
		update_source_in_backend (backend, source);
	}

	calendar = e_data_cal_new (backend, source);
	e_cal_backend_add_client (backend, calendar);

	path = construct_cal_factory_path ();
	dbus_g_connection_register_g_object (connection, path, G_OBJECT (calendar));
	g_object_weak_ref (G_OBJECT (calendar), (GWeakNotify)my_remove, path);

	g_hash_table_insert (priv->calendars, g_strdup (path), calendar);

	sender = dbus_g_method_get_sender (context);
	list = g_hash_table_lookup (priv->connections, sender);
	list = g_list_prepend (list, calendar);
	g_hash_table_insert (priv->connections, sender, list);

 cleanup:
	/* The reason why the lock is held for such a long time is that there is
	   a subtle race where e_cal_backend_add_client() can be called just
	   before e_cal_backend_finalize() is called from the
	   backend_last_client_gone_cb(), for details see bug 506457. */
	g_mutex_unlock (priv->backends_mutex);
 cleanup2:
	g_free (str_uri);
	e_uri_free (uri);
	g_free (uid_type_string);
	g_object_unref (source);

	if (error)
		dbus_g_method_return_error (context, error);
	else
		dbus_g_method_return (context, path);
}

static void
name_owner_changed (DBusGProxy      *proxy,
                    const gchar      *name,
                    const gchar      *prev_owner,
                    const gchar      *new_owner,
                    EDataCalFactory *factory)
{
	if (strcmp (new_owner, "") == 0 && strcmp (name, prev_owner) == 0) {
		gchar *key;
		GList *list = NULL;
                while (g_hash_table_lookup_extended (factory->priv->connections, prev_owner, (gpointer)&key, (gpointer)&list)) {
                        /* this should trigger the book's weak ref notify
                         * function, which will remove it from the list before
                         * it's freed, and will remove the connection from
                         * priv->connections once they're all gone */
                        g_object_unref (list->data);
                }
	}
}

/* Class initialization function for the calendar factory */
static void
e_data_cal_factory_class_init (EDataCalFactoryClass *klass)
{
	g_type_class_add_private (klass, sizeof (EDataCalFactoryPrivate));
	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (klass), &dbus_glib_e_data_cal_factory_object_info);
}

/* Instance init */
static void
e_data_cal_factory_init (EDataCalFactory *factory)
{
	factory->priv = E_DATA_CAL_FACTORY_GET_PRIVATE (factory);

	factory->priv->methods = g_hash_table_new_full (g_str_hash, g_str_equal,
							(GDestroyNotify) g_free, (GDestroyNotify) g_hash_table_destroy);

	factory->priv->backends_mutex = g_mutex_new ();
	factory->priv->backends = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	factory->priv->calendars = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	factory->priv->connections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	e_data_server_module_init ();
	e_data_cal_factory_register_backends (factory);
}

static void
set_backend_online_status (gpointer key, gpointer value, gpointer data)
{
	ECalBackend *backend = E_CAL_BACKEND (value);

	e_cal_backend_set_mode (backend,  GPOINTER_TO_INT (data));
}

/**
 * e_data_cal_factory_set_backend_mode:
 * @factory: A calendar factory.
 * @mode: Online mode to set.
 *
 * Sets the online mode for all backends created by the given factory.
 */
void
e_data_cal_factory_set_backend_mode (EDataCalFactory *factory, gint mode)
{
	EDataCalFactoryPrivate *priv = factory->priv;

	priv->mode = mode;
	g_mutex_lock (priv->backends_mutex);
	g_hash_table_foreach (priv->backends, set_backend_online_status, GINT_TO_POINTER (priv->mode));
	g_mutex_unlock (priv->backends_mutex);
}

/**
 * e_data_cal_factory_register_backend:
 * @factory: A calendar factory.
 * @backend_factory: The object responsible for creating backends.
 *
 * Registers an #ECalBackend subclass that will be used to handle URIs
 * with a particular method.  When the factory is asked to open a
 * particular URI, it will look in its list of registered methods and
 * create a backend of the appropriate type.
 **/
void
e_data_cal_factory_register_backend (EDataCalFactory *factory, ECalBackendFactory *backend_factory)
{
	EDataCalFactoryPrivate *priv;
	const gchar *method;
	GHashTable *kinds;
	GType type;
	icalcomponent_kind kind;
	GSList *methods = NULL, *l;

	g_return_if_fail (factory && E_IS_DATA_CAL_FACTORY (factory));
	g_return_if_fail (backend_factory && E_IS_CAL_BACKEND_FACTORY (backend_factory));

	priv = factory->priv;

	if (E_IS_CAL_BACKEND_LOADER_FACTORY (backend_factory)) {
		GSList *list = E_CAL_BACKEND_LOADER_FACTORY_GET_CLASS (backend_factory)->get_protocol_list ((ECalBackendLoaderFactory *) backend_factory);
		methods = g_slist_copy (list);
	} else if (E_CAL_BACKEND_FACTORY_GET_CLASS (backend_factory)->get_protocol) {
		method = E_CAL_BACKEND_FACTORY_GET_CLASS (backend_factory)->get_protocol (backend_factory);
		methods = g_slist_append (methods, (gpointer) method);
	} else {
		g_assert_not_reached ();
		return;
	}

	kind = E_CAL_BACKEND_FACTORY_GET_CLASS (backend_factory)->get_kind (backend_factory);

	for (l= methods; l != NULL; l = g_slist_next (l)) {
		gchar *method_str;

		method = l->data;

		method_str = g_ascii_strdown (method, -1);

		kinds = g_hash_table_lookup (priv->methods, method_str);
		if (kinds) {
			type = GPOINTER_TO_INT (g_hash_table_lookup (kinds, GINT_TO_POINTER (kind)));
			if (type) {
				g_warning (G_STRLOC ": method `%s' already registered", method_str);
				g_free (method_str);
				g_slist_free (methods);
				return;
			}

			g_free (method_str);
		} else {
			kinds = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, NULL);
			g_hash_table_insert (priv->methods, method_str, kinds);
		}

		g_hash_table_insert (kinds, GINT_TO_POINTER (kind), backend_factory);
	}
	g_slist_free (methods);
}

/**
 * e_data_cal_factory_register_backends:
 * @cal_factory: A calendar factory.
 *
 * Register all backends for the given factory.
 */
void
e_data_cal_factory_register_backends (EDataCalFactory *cal_factory)
{
	GList *factories, *f;

	factories = e_data_server_get_extensions_for_type (E_TYPE_CAL_BACKEND_FACTORY);

	for (f = factories; f; f = f->next) {
		ECalBackendFactory *backend_factory = f->data;

		e_data_cal_factory_register_backend (cal_factory, g_object_ref (backend_factory));
	}

	e_data_server_extension_list_free (factories);
	e_data_server_module_remove_unused ();
}

/**
 * e_data_cal_factory_get_n_backends
 * @factory: A calendar factory.
 *
 * Get the number of backends currently active in the given factory.
 *
 * Returns: the number of backends.
 */
gint
e_data_cal_factory_get_n_backends (EDataCalFactory *factory)
{
	EDataCalFactoryPrivate *priv;
	gint sz;

	g_return_val_if_fail (E_IS_DATA_CAL_FACTORY (factory), 0);

	priv = factory->priv;
	g_mutex_lock (priv->backends_mutex);
	sz = g_hash_table_size (priv->backends);
	g_mutex_unlock (priv->backends_mutex);

	return sz;
}

/* Frees a uri/backend pair from the backends hash table */
static void
dump_backend (gpointer key, gpointer value, gpointer data)
{
	gchar *uri;
	ECalBackend *backend;

	uri = key;
	backend = value;

	g_message ("  %s: %p", uri, (gpointer) backend);
}

/**
 * e_data_cal_factory_dump_active_backends:
 * @factory: A calendar factory.
 *
 * Dumps to standard output a list of all active backends for the given
 * factory.
 */
void
e_data_cal_factory_dump_active_backends (EDataCalFactory *factory)
{
	EDataCalFactoryPrivate *priv;

	g_message ("Active PCS backends");

	priv = factory->priv;
	g_mutex_lock (priv->backends_mutex);
	g_hash_table_foreach (priv->backends, dump_backend, NULL);
	g_mutex_unlock (priv->backends_mutex);
}

/* Convenience function to print an error and exit */
G_GNUC_NORETURN static void
die (const gchar *prefix, GError *error)
{
	g_error("%s: %s", prefix, error->message);
	g_error_free (error);
	exit(1);
}

static void
offline_state_changed_cb (EOfflineListener *eol, EDataCalFactory *factory)
{
	EOfflineListenerState state = e_offline_listener_get_state (eol);

	g_return_if_fail (state == EOL_STATE_ONLINE || state == EOL_STATE_OFFLINE);

	e_data_cal_factory_set_backend_mode (factory, state == EOL_STATE_ONLINE ? GNOME_Evolution_Calendar_MODE_REMOTE : GNOME_Evolution_Calendar_MODE_LOCAL);
}

#define E_DATA_CAL_FACTORY_SERVICE_NAME "org.gnome.evolution.dataserver.Calendar"

gint
main (gint argc, gchar **argv)
{
	GError *error = NULL;
	DBusGProxy *bus_proxy;
	guint32 request_name_ret;
	EOfflineListener *eol;

	g_type_init ();
	g_set_prgname (E_PRGNAME);
	if (!g_thread_supported ()) g_thread_init (NULL);
	dbus_g_thread_init ();

	loop = g_main_loop_new (NULL, FALSE);

	/* Obtain a connection to the session bus */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (connection == NULL)
		die ("Failed to open connection to bus", error);

	bus_proxy = dbus_g_proxy_new_for_name (connection,
					       DBUS_SERVICE_DBUS,
					       DBUS_PATH_DBUS,
					       DBUS_INTERFACE_DBUS);

	factory = g_object_new (E_TYPE_DATA_CAL_FACTORY, NULL);
	dbus_g_connection_register_g_object (connection,
					     "/org/gnome/evolution/dataserver/calendar/CalFactory",
					     G_OBJECT (factory));

	dbus_g_proxy_add_signal (bus_proxy, "NameOwnerChanged",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (bus_proxy, "NameOwnerChanged", G_CALLBACK (name_owner_changed), factory, NULL);

	if (!org_freedesktop_DBus_request_name (bus_proxy, E_DATA_CAL_FACTORY_SERVICE_NAME,
						0, &request_name_ret, &error))
		die ("Failed to get name", error);

	if (request_name_ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		g_error ("Got result code %u from requesting name", request_name_ret);
		exit (1);
	}

	eol = e_offline_listener_new ();
	offline_state_changed_cb (eol, factory);
	g_signal_connect (eol, "changed", G_CALLBACK (offline_state_changed_cb), factory);

	printf ("Server is up and running...\n");

	g_main_loop_run (loop);

	dbus_g_connection_unref (connection);

	g_object_unref (eol);

	printf ("Bye.\n");

	return 0;
}

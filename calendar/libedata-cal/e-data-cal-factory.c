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

#ifdef ENABLE_MAINTAINER_MODE
#include <gtk/gtk.h>
#endif

#ifdef G_OS_UNIX
#if GLIB_CHECK_VERSION(2,29,5)
#include <glib-unix.h>
#endif
#endif

#include <libedataserver/e-url.h>
#include <libedataserver/e-source-list.h>
#include <libebackend/e-data-server-module.h>
#include <libebackend/e-offline-listener.h>
#include <libecal/e-cal-client.h>
#include "e-cal-backend.h"
#include "e-cal-backend-factory.h"
#include "e-data-cal.h"
#include "e-data-cal-factory.h"

#include "e-gdbus-cal-factory.h"

#include <libical/ical.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <conio.h>
#ifndef PROCESS_DEP_ENABLE
#define PROCESS_DEP_ENABLE 0x00000001
#endif
#ifndef PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION
#define PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION 0x00000002
#endif
#endif

#define d(x)

#define E_DATA_CAL_FACTORY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_CAL_FACTORY, EDataCalFactoryPrivate))

static GMainLoop *loop;

/* Keeps running after the last client is closed. */
static gboolean opt_keep_running = FALSE;

/* Convenience macro to test and set a GError/return on failure */
#define g_set_error_val_if_fail(test, returnval, error, domain, code) \
	G_STMT_START {							\
	if G_LIKELY (test) {} else {					\
		g_set_error_literal (error, domain, code, #test);	\
		g_warning(#test " failed");				\
		return (returnval);					\
	}								\
	} G_STMT_END

G_DEFINE_TYPE (EDataCalFactory, e_data_cal_factory, G_TYPE_OBJECT);

struct _EDataCalFactoryPrivate {
	EGdbusCalFactory *gdbus_object;

	/* Hash table from URI method strings to GType * for backend class types */
	GHashTable *methods;

	GHashTable *backends;
	/* mutex to access backends hash table */
	GMutex *backends_mutex;

	GHashTable *calendars;

	GHashTable *connections;

	gboolean is_online;

	/* this is for notifications of source changes */
	ESourceList *lists[E_CAL_CLIENT_SOURCE_TYPE_LAST];

	/* backends divided by their type */
	GList *backends_by_type[E_CAL_CLIENT_SOURCE_TYPE_LAST];

	guint exit_timeout;
};

/* Forward Declarations */
void e_data_cal_migrate_basedir (void);

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

static ECalClientSourceType
icalkind_to_ecalclientsourcetype (const icalcomponent_kind kind)
{
	switch (kind) {
	case ICAL_VEVENT_COMPONENT:
		return E_CAL_CLIENT_SOURCE_TYPE_EVENTS;
	case ICAL_VTODO_COMPONENT:
		return E_CAL_CLIENT_SOURCE_TYPE_TASKS;
	case ICAL_VJOURNAL_COMPONENT:
		return E_CAL_CLIENT_SOURCE_TYPE_MEMOS;
	default:
		break;
	}

	return E_CAL_CLIENT_SOURCE_TYPE_LAST;
}

static void
update_source_in_backend (ECalBackend *backend,
                          ESource *updated_source)
{
	ESource *backend_source;
	xmlNodePtr xml;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (updated_source != NULL);

	backend_source = e_cal_backend_get_source (backend);

	xml = xmlNewNode (NULL, (const xmlChar *) "dummy");
	e_source_dump_to_xml_node (updated_source, xml);
	e_source_update_from_xml_node (backend_source, xml->children, NULL);
	xmlFreeNode (xml);
}

static void
source_list_changed_cb (ESourceList *list,
                        EDataCalFactory *factory)
{
	EDataCalFactoryPrivate *priv;
	gint i;

	g_return_if_fail (list != NULL);
	g_return_if_fail (E_IS_DATA_CAL_FACTORY (factory));

	priv = factory->priv;

	g_mutex_lock (priv->backends_mutex);

	for (i = 0; i < E_CAL_CLIENT_SOURCE_TYPE_LAST; i++) {
		GList *iter;

		if (list != priv->lists[i])
			continue;

		for (iter = priv->backends_by_type[i]; iter; iter = iter->next) {
			ECalBackend *backend = iter->data;
			ESource *source, *list_source;
			const gchar *uid;

			source = e_cal_backend_get_source (backend);
			uid = e_source_peek_uid (source);
			list_source = e_source_list_peek_source_by_uid (
				priv->lists[i], uid);

			if (list_source != NULL)
				update_source_in_backend (backend, list_source);
		}

		break;
	}

	g_mutex_unlock (priv->backends_mutex);
}

static ECalBackendFactory *
get_backend_factory (GHashTable *methods,
                     const gchar *method,
                     icalcomponent_kind kind)
{
	GHashTable *kinds;

	kinds = g_hash_table_lookup (methods, method);
	if (kinds == NULL)
		return NULL;

	return g_hash_table_lookup (kinds, GINT_TO_POINTER (kind));
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

	if (g_hash_table_size (priv->calendars) > 0)
		return;

	/* If there are no open calendars, start a timer to quit. */
	if (!opt_keep_running && priv->exit_timeout == 0)
		priv->exit_timeout = g_timeout_add_seconds (
			10, (GSourceFunc) g_main_loop_quit, loop);
}

static void
last_client_gone_cb (ECalBackend *backend,
                     EDataCalFactory *factory)
{
	EDataCalFactoryPrivate *priv = factory->priv;

	if (e_cal_backend_is_removed (backend)) {
		g_mutex_lock (priv->backends_mutex);
		g_hash_table_foreach_remove (
			priv->backends, remove_dead_calendar_cb, backend);
		g_mutex_unlock (priv->backends_mutex);
	}
}

struct find_backend_data {
	const gchar *str_uri;
	ECalBackend *backend;
	icalcomponent_kind kind;
};

static void
find_backend_cb (gpointer key,
                 gpointer value,
                 gpointer data)
{
	struct find_backend_data *fbd = data;

	if (fbd && fbd->str_uri && !fbd->backend) {
		ECalBackend *backend = value;
		ESource *backend_source;
		gchar *str_uri;

		backend_source = e_cal_backend_get_source (backend);
		str_uri = e_source_get_uri (backend_source);

		if (str_uri && g_str_equal (str_uri, fbd->str_uri)) {
			const gchar *uid_kind = key, *pos;

			pos = strrchr (uid_kind, ':');
			if (pos && atoi (pos + 1) == fbd->kind)
				fbd->backend = backend;
		}

		g_free (str_uri);
	}
}

static gboolean
impl_CalFactory_get_cal (EGdbusCalFactory *object,
                         GDBusMethodInvocation *invocation,
                         const gchar * const *in_source_type,
                         EDataCalFactory *factory)
{
	EDataCal *calendar;
	ECalBackend *backend;
	EDataCalFactoryPrivate *priv = factory->priv;
	ECalBackendFactory *backend_factory;
	GDBusConnection *connection;
	ESource *source;
	gchar *str_uri;
	EUri *uri;
	gchar *uid_type_string;
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
	if (!source) {
		g_free (source_xml);

		error = g_error_new (
			E_DATA_CAL_ERROR, NoSuchCal, _("Invalid source"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	g_free (source_xml);

	/* Get the URI so we can extract the protocol */
	str_uri = e_source_get_uri (source);
	if (!str_uri) {
		g_object_unref (source);

		error = g_error_new (
			E_DATA_CAL_ERROR, NoSuchCal, _("Empty URI"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	/* Parse the uri */
	uri = e_uri_new (str_uri);
	if (!uri) {
		g_object_unref (source);
		g_free (str_uri);

		error = g_error_new (
			E_DATA_CAL_ERROR, NoSuchCal, _("Invalid URI"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	uid_type_string = g_strdup_printf (
		"%s:%d", e_source_peek_uid (source),
		(gint) calobjtype_to_icalkind (type));

	/* Find the associated backend factory (if any) */
	backend_factory = get_backend_factory (
		priv->methods, uri->protocol,
		calobjtype_to_icalkind (type));
	if (!backend_factory) {
		error = g_error_new (
			E_DATA_CAL_ERROR, NoSuchCal,
			_("No backend factory for '%s' of '%s'"),
			uri->protocol, calobjtype_to_string (type));

		goto cleanup2;
	}

	g_mutex_lock (priv->backends_mutex);

	/* Look for an existing backend */
	backend = g_hash_table_lookup (
		factory->priv->backends, uid_type_string);

	if (!backend) {
		/* Find backend by URL, if opened, thus functions like
		 * e_cal_system_new_* will not create new backends for
		 * the same URL. */
		struct find_backend_data fbd;

		fbd.str_uri = str_uri;
		fbd.kind = calobjtype_to_icalkind (type);
		fbd.backend = NULL;

		g_hash_table_foreach (priv->backends, find_backend_cb, &fbd);

		if (fbd.backend) {
			backend = fbd.backend;
			g_object_unref (source);
			source = e_cal_backend_get_source (backend);
			g_object_ref (source);
		}
	}

	if (!backend) {
		ECalClientSourceType st;
		icalcomponent_kind kind;

		/* There was no existing backend, create a new one */
		backend = e_cal_backend_factory_new_backend (
			backend_factory, source);

		if (!backend) {
			error = g_error_new (
				E_DATA_CAL_ERROR, NoSuchCal,
				_("Could not instantiate backend"));
			goto cleanup;
		}

		kind = e_cal_backend_get_kind (backend);
		st = icalkind_to_ecalclientsourcetype (kind);
		if (st != E_CAL_CLIENT_SOURCE_TYPE_LAST) {
			if (!priv->lists[st] &&
			    e_cal_client_get_sources (
			    &(priv->lists[st]), st, NULL)) {
				g_signal_connect (
					priv->lists[st], "changed",
					G_CALLBACK (source_list_changed_cb),
					factory);
			}

			if (priv->lists[st])
				priv->backends_by_type[st] = g_list_prepend (
					priv->backends_by_type[st], backend);
		}

		/* Track the backend */
		g_hash_table_insert (
			priv->backends, g_strdup (uid_type_string), backend);

		g_signal_connect (
			backend, "last-client-gone",
			G_CALLBACK (last_client_gone_cb), factory);
		e_cal_backend_set_online (backend, priv->is_online);
	} else if (!e_source_equal (source, e_cal_backend_get_source (backend))) {
		/* source changed, update it in a backend */
		update_source_in_backend (backend, source);
	}

	/* Remove a pending exit */
	if (priv->exit_timeout) {
		g_source_remove (priv->exit_timeout);
		priv->exit_timeout = 0;
	}

	calendar = e_data_cal_new (backend, source);
	e_cal_backend_add_client (backend, calendar);

	path = construct_cal_factory_path ();
	e_data_cal_register_gdbus_object (calendar, connection, path, &error);
	g_object_weak_ref (
		G_OBJECT (calendar), (GWeakNotify)
		calendar_freed_cb, factory);

	g_hash_table_insert (priv->calendars, g_strdup (path), calendar);

	list = g_hash_table_lookup (priv->connections, sender);
	list = g_list_prepend (list, calendar);
	g_hash_table_insert (priv->connections, g_strdup (sender), list);

cleanup:
	/* The reason why the lock is held for such a long time is that there is
	 * a subtle race where e_cal_backend_add_client () can be called just
	 * before e_cal_backend_finalize () is called from the
	 * backend_last_client_gone_cb (), for details see bug 506457. */
	g_mutex_unlock (priv->backends_mutex);

cleanup2:
	g_free (str_uri);
	e_uri_free (uri);
	g_free (uid_type_string);
	g_object_unref (source);

	e_gdbus_cal_factory_complete_get_cal (object, invocation, path, error);

	if (error)
		g_error_free (error);

	g_free (path);

	return TRUE;
}

static void
remove_data_cal_cb (gpointer data_cl,
                    gpointer user_data)
{
	ECalBackend *backend;
	EDataCal *data_cal;

	data_cal = E_DATA_CAL (data_cl);
	g_return_if_fail (data_cal != NULL);

	backend = e_data_cal_get_backend (data_cal);
	e_cal_backend_remove_client (backend, data_cal);

	g_object_unref (data_cal);
}

static void
unref_backend_cb (gpointer key,
                  gpointer value,
                  gpointer user_data)
{
	ECalBackend *backend = value;

	if (backend)
		g_object_unref (backend);
}

static void
e_data_cal_factory_dispose (GObject *object)
{
	EDataCalFactoryPrivate *priv;
	gint ii;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (object);

	if (priv->gdbus_object != NULL) {
		g_object_unref (priv->gdbus_object);
		priv->gdbus_object = NULL;
	}

	for (ii = 0; ii < E_CAL_CLIENT_SOURCE_TYPE_LAST; ii++) {
		if (priv->lists[ii] != NULL) {
			g_object_unref (priv->lists[ii]);
			priv->lists[ii] = NULL;
		}

		g_list_free (priv->backends_by_type[ii]);
		priv->backends_by_type[ii] = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_data_cal_factory_parent_class)->dispose (object);
}

static void
e_data_cal_factory_finalize (GObject *object)
{
	EDataCalFactoryPrivate *priv;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (object);

	g_hash_table_foreach (priv->backends, unref_backend_cb, NULL);

	g_hash_table_destroy (priv->methods);
	g_hash_table_destroy (priv->backends);
	g_hash_table_destroy (priv->calendars);
	g_hash_table_destroy (priv->connections);

	g_mutex_free (priv->backends_mutex);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_cal_factory_parent_class)->finalize (object);
}

/* Class initialization function for the calendar factory */
static void
e_data_cal_factory_class_init (EDataCalFactoryClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EDataCalFactoryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = e_data_cal_factory_dispose;
	object_class->finalize = e_data_cal_factory_finalize;
}

static void
e_data_cal_factory_init (EDataCalFactory *factory)
{
	GError *error = NULL;

	factory->priv = E_DATA_CAL_FACTORY_GET_PRIVATE (factory);

	factory->priv->gdbus_object = e_gdbus_cal_factory_stub_new ();
	g_signal_connect (
		factory->priv->gdbus_object, "handle-get-cal",
		G_CALLBACK (impl_CalFactory_get_cal), factory);

	factory->priv->methods = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_hash_table_destroy);

	factory->priv->backends_mutex = g_mutex_new ();

	factory->priv->backends = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	factory->priv->calendars = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	factory->priv->connections = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	if (!e_data_server_module_init (BACKENDDIR, &error))
		g_error ("%s", error->message);

	e_data_cal_factory_register_backends (factory);
}

static guint
e_data_cal_factory_register_gdbus_object (EDataCalFactory *factory,
                                          GDBusConnection *connection,
                                          const gchar *object_path,
                                          GError **error)
{
	g_return_val_if_fail (E_IS_DATA_CAL_FACTORY (factory), 0);
	g_return_val_if_fail (connection != NULL, 0);
	g_return_val_if_fail (object_path != NULL, 0);

	return e_gdbus_cal_factory_register_object (
		factory->priv->gdbus_object, connection, object_path, error);
}

/**
 * e_data_cal_factory_set_backend_online:
 * @factory: A calendar factory.
 * @is_online: Online mode to set.
 *
 * Sets the online mode for all backends created by the given factory.
 *
 * Since: 3.2
 */
void
e_data_cal_factory_set_backend_online (EDataCalFactory *factory,
                                       gboolean is_online)
{
	GHashTableIter iter;
	gpointer value;

	g_return_if_fail (E_IS_DATA_CAL_FACTORY (factory));

	factory->priv->is_online = is_online;
	g_mutex_lock (factory->priv->backends_mutex);

	g_hash_table_iter_init (&iter, factory->priv->backends);
	while (g_hash_table_iter_next (&iter, NULL, &value))
		e_cal_backend_set_online (
			E_CAL_BACKEND (value),
			factory->priv->is_online);

	g_mutex_unlock (factory->priv->backends_mutex);
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
e_data_cal_factory_register_backend (EDataCalFactory *factory,
                                     ECalBackendFactory *backend_factory)
{
	ECalBackendFactoryClass *class;
	EDataCalFactoryPrivate *priv;
	const gchar *method;
	GHashTable *kinds;
	GType type;
	icalcomponent_kind kind;
	GList *methods = NULL, *l;

	g_return_if_fail (E_IS_DATA_CAL_FACTORY (factory));
	g_return_if_fail (E_IS_CAL_BACKEND_FACTORY (backend_factory));

	priv = factory->priv;

	class = E_CAL_BACKEND_FACTORY_GET_CLASS (backend_factory);
	g_return_if_fail (class->get_protocol != NULL);

	method = class->get_protocol (backend_factory);
	methods = g_list_append (methods, (gpointer) method);

	kind = class->get_kind (backend_factory);

	for (l = methods; l != NULL; l = g_list_next (l)) {
		gchar *method_str;

		method = l->data;

		method_str = g_ascii_strdown (method, -1);

		kinds = g_hash_table_lookup (priv->methods, method_str);
		if (kinds) {
			gpointer data;

			data = GINT_TO_POINTER (kind);
			data = g_hash_table_lookup (kinds, data);
			type = GPOINTER_TO_INT (data);

			if (type) {
				g_warning (
					G_STRLOC ": method '%s' "
					"already registered", method_str);
				g_free (method_str);
				g_list_free (methods);
				return;
			}

			g_free (method_str);
		} else {
			kinds = g_hash_table_new_full (
				g_direct_hash, g_direct_equal, NULL, NULL);
			g_hash_table_insert (priv->methods, method_str, kinds);
		}

		g_hash_table_insert (
			kinds, GINT_TO_POINTER (kind), backend_factory);
	}
	g_list_free (methods);
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
	GType type;
	GList *factories;
	GList *iter;

	g_return_if_fail (E_IS_DATA_CAL_FACTORY (cal_factory));

	type = E_TYPE_CAL_BACKEND_FACTORY;
	factories = e_data_server_get_extensions_for_type (type);

	for (iter = factories; iter != NULL; iter = g_list_next (iter)) {
		ECalBackendFactory *backend_factory = iter->data;

		e_data_cal_factory_register_backend (
			cal_factory, g_object_ref (backend_factory));
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
guint
e_data_cal_factory_get_n_backends (EDataCalFactory *factory)
{
	EDataCalFactoryPrivate *priv;
	guint n_backends;

	g_return_val_if_fail (E_IS_DATA_CAL_FACTORY (factory), 0);

	priv = factory->priv;
	g_mutex_lock (priv->backends_mutex);
	n_backends = g_hash_table_size (priv->backends);
	g_mutex_unlock (priv->backends_mutex);

	return n_backends;
}

/* Frees a uri/backend pair from the backends hash table */
static void
dump_backend (gpointer key,
              gpointer value,
              gpointer data)
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
die (const gchar *prefix,
     GError *error)
{
	g_error ("%s: %s", prefix, error->message);
	g_error_free (error);
	exit (1);
}

static void
offline_state_changed_cb (EOfflineListener *eol,
                          EDataCalFactory *factory)
{
	switch (e_offline_listener_get_state (eol)) {
		case EOL_STATE_OFFLINE:
			e_data_cal_factory_set_backend_online (factory, FALSE);
			break;
		case EOL_STATE_ONLINE:
			e_data_cal_factory_set_backend_online (factory, TRUE);
			break;
		default:
			g_warn_if_reached ();
	}
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar *name,
                 gpointer user_data)
{
	EDataCalFactory *factory = user_data;
	guint registration_id;
	GError *error = NULL;

	registration_id = e_data_cal_factory_register_gdbus_object (
		factory,
		connection,
		"/org/gnome/evolution/dataserver/CalendarFactory",
		&error);

	if (error)
		die ("Failed to register a CalendarFactory object", error);

	g_assert (registration_id > 0);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar *name,
                  gpointer user_data)
{
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar *name,
              gpointer user_data)
{
	GList *list = NULL;
	gchar *key;
	EDataCalFactory *factory = user_data;

	while (g_hash_table_lookup_extended (
		factory->priv->connections, name,
		(gpointer) &key, (gpointer) &list)) {

		GList *copy = g_list_copy (list);

		/* this should trigger the book's weak ref notify
		 * function, which will remove it from the list before
		 * it's freed, and will remove the connection from
		 * priv->connections once they're all gone */
		g_list_foreach (copy, remove_data_cal_cb, NULL);
		g_list_free (copy);
	}
	g_print ("Superseded...\n");
	g_main_loop_quit (loop);
}

#ifdef G_OS_UNIX
#if GLIB_CHECK_VERSION(2,29,5)
static gboolean
handle_term_signal (gpointer data)
{
	g_print ("Received terminate signal...\n");
	g_main_loop_quit (loop);

	return FALSE;
}
#endif
#endif

static GOptionEntry entries[] = {

	/* FIXME Have the description translated for 3.2, but this
	 *       option is to aid in testing and development so it
	 *       doesn't really matter. */
	{ "keep-running", 'r', 0, G_OPTION_ARG_NONE, &opt_keep_running,
	  "Keep running after the last client is closed", NULL },
	{ NULL }
};

gint
main (gint argc,
      gchar **argv)
{
	EOfflineListener *eol;
	GOptionContext *context;
	EDataCalFactory *factory;
	guint owner_id;
	GError *error = NULL;
	icalarray *builtin_timezones;
	gint ii;

#ifdef G_OS_WIN32
	/* Reduce risks */
	{
		typedef BOOL (WINAPI *t_SetDllDirectoryA) (LPCSTR lpPathName);
		t_SetDllDirectoryA p_SetDllDirectoryA;

		p_SetDllDirectoryA = GetProcAddress (GetModuleHandle ("kernel32.dll"), "SetDllDirectoryA");
		if (p_SetDllDirectoryA)
			(*p_SetDllDirectoryA) ("");
	}
#ifndef _WIN64
	{
		typedef BOOL (WINAPI *t_SetProcessDEPPolicy) (DWORD dwFlags);
		t_SetProcessDEPPolicy p_SetProcessDEPPolicy;

		p_SetProcessDEPPolicy = GetProcAddress (GetModuleHandle ("kernel32.dll"), "SetProcessDEPPolicy");
		if (p_SetProcessDEPPolicy)
			(*p_SetProcessDEPPolicy) (PROCESS_DEP_ENABLE | PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION);
	}
#endif
#endif

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	g_type_init ();
	g_set_prgname (E_PRGNAME);
	if (!g_thread_supported ()) g_thread_init (NULL);

	#ifdef ENABLE_MAINTAINER_MODE
	/* only to load gtk-modules, like bug-buddy's gnomesegvhandler, if possible */
	gtk_init_check (&argc, &argv);
	#endif

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
	g_option_context_parse (context, &argc, &argv, &error);
	g_option_context_free (context);

	if (error != NULL) {
		g_printerr ("%s\n", error->message);
		exit (EXIT_FAILURE);
	}

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

	factory = g_object_new (E_TYPE_DATA_CAL_FACTORY, NULL);

	loop = g_main_loop_new (NULL, FALSE);

	eol = e_offline_listener_new ();
	offline_state_changed_cb (eol, factory);
	g_signal_connect (
		eol, "changed",
		G_CALLBACK (offline_state_changed_cb), factory);

	owner_id = g_bus_own_name (
		G_BUS_TYPE_SESSION,
		CALENDAR_DBUS_SERVICE_NAME,
		G_BUS_NAME_OWNER_FLAGS_REPLACE | G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT,
		on_bus_acquired,
		on_name_acquired,
		on_name_lost,
		factory,
		NULL);

	/* Migrate user data from ~/.evolution to XDG base directories. */
	e_data_cal_migrate_basedir ();

#ifdef G_OS_UNIX
#if GLIB_CHECK_VERSION(2,29,19)
	g_unix_signal_add_full (
		G_PRIORITY_DEFAULT, SIGTERM,
		handle_term_signal, NULL, NULL);
#elif GLIB_CHECK_VERSION(2,29,5)
	g_unix_signal_add_watch_full (
		SIGTERM, G_PRIORITY_DEFAULT,
		handle_term_signal, NULL, NULL);
#endif
#endif

	g_print ("Server is up and running...\n");

	g_main_loop_run (loop);

	g_bus_unown_name (owner_id);
	g_object_unref (eol);
	g_object_unref (factory);

	g_print ("Bye.\n");

	return 0;
}

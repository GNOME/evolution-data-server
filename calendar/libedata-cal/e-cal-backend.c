/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - generic backend class
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          JP Rosevear <jpr@ximian.com>
 *          Rodrigo Moya <rodrigo@ximian.com>
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
#include <libxml/parser.h>
#include <libxml/parserInternals.h>
#include <libxml/xmlmemory.h>

#include <libedataserver/e-data-server-util.h>

#include "e-cal-backend.h"
#include "e-cal-backend-cache.h"

#define E_CAL_BACKEND_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL_BACKEND, ECalBackendPrivate))

/* For convenience */
#define CLASS(backend) (E_CAL_BACKEND_GET_CLASS(backend))

#define EDC_ERROR(_code) e_data_cal_create_error (_code, NULL)

/* Private part of the CalBackend structure */
struct _ECalBackendPrivate {
	/* The source for this backend */
	ESource *source;
	/* signal handler ID for source's 'changed' signal */
	gulong source_changed_id;

	/* URI, from source. This is cached, since we return const. */
	gchar *uri;

	gchar *cache_dir;

	/* The kind of components for this backend */
	icalcomponent_kind kind;

	/* List of Cal objects */
	GMutex *clients_mutex;
	GList *clients;

	GMutex *queries_mutex;
	EList *queries;

	/* ECalBackend to pass notifications on to */
	ECalBackend *notification_proxy;

	/* used when notifying clients about progress of some operation,
	 * we do not send multiple notifications with the same percent
	 * value */
	gint last_percent_notified;
};

/* Property IDs */
enum {
	PROP_0,
	PROP_CACHE_DIR,
	PROP_KIND,
	PROP_SOURCE,
	PROP_URI
};

/* Signal IDs */
enum {
	LAST_CLIENT_GONE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void e_cal_backend_remove_client_private (ECalBackend *backend, EDataCal *cal, gboolean weak_unref);

G_DEFINE_TYPE (ECalBackend, e_cal_backend, G_TYPE_OBJECT);

static void
source_changed_cb (ESource *source, ECalBackend *backend)
{
	ECalBackendPrivate *priv;
	gchar *suri;

	g_return_if_fail (source != NULL);
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	priv = backend->priv;
	g_return_if_fail (priv != NULL);
	g_return_if_fail (priv->source == source);

	suri = e_source_get_uri (priv->source);
	if (!priv->uri || (suri && !g_str_equal (priv->uri, suri))) {
		g_free (priv->uri);
		priv->uri = suri;
	} else {
		g_free (suri);
	}
}

static void
cal_backend_set_default_cache_dir (ECalBackend *backend)
{
	ESource *source;
	icalcomponent_kind kind;
	const gchar *component_type;
	const gchar *user_cache_dir;
	gchar *mangled_uri;
	gchar *filename;

	user_cache_dir = e_get_user_cache_dir ();

	kind = e_cal_backend_get_kind (backend);
	source = e_cal_backend_get_source (backend);
	g_return_if_fail (source != NULL);

	switch (kind) {
		case ICAL_VEVENT_COMPONENT:
			component_type = "calendar";
			break;
		case ICAL_VTODO_COMPONENT:
			component_type = "tasks";
			break;
		case ICAL_VJOURNAL_COMPONENT:
			component_type = "memos";
			break;
		default:
			g_return_if_reached ();
	}

	/* Mangle the URI to not contain invalid characters. */
	mangled_uri = g_strdelimit (e_source_get_uri (source), ":/", '_');

	filename = g_build_filename (
		user_cache_dir, component_type, mangled_uri, NULL);
	e_cal_backend_set_cache_dir (backend, filename);
	g_free (filename);

	g_free (mangled_uri);
}

static void
cal_backend_set_source (ECalBackend *backend,
                        ESource *source)
{
	if (backend->priv->source != NULL) {
		if (backend->priv->source_changed_id > 0) {
			g_signal_handler_disconnect (
				backend->priv->source,
				backend->priv->source_changed_id);
			backend->priv->source_changed_id = 0;
		}
		g_object_unref (backend->priv->source);
	}

	if (source != NULL)
		backend->priv->source_changed_id = g_signal_connect (
			g_object_ref (source), "changed",
			G_CALLBACK (source_changed_cb), backend);

	backend->priv->source = source;

	/* Cache the URI */
	if (source != NULL) {
		g_free (backend->priv->uri);
		backend->priv->uri = e_source_get_uri (source);
	}
}

static void
cal_backend_set_uri (ECalBackend *backend,
                     const gchar *uri)
{
	/* ESource's URI gets priority. */
	if (backend->priv->source == NULL) {
		g_free (backend->priv->uri);
		backend->priv->uri = g_strdup (uri);
	}
}

static void
cal_backend_set_kind (ECalBackend *backend,
                      icalcomponent_kind kind)
{
	backend->priv->kind = kind;
}

static void
cal_backend_set_property (GObject *object,
                          guint property_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CACHE_DIR:
			e_cal_backend_set_cache_dir (
				E_CAL_BACKEND (object),
				g_value_get_string (value));
			return;
		case PROP_KIND:
			cal_backend_set_kind (
				E_CAL_BACKEND (object),
				g_value_get_ulong (value));
			return;
		case PROP_SOURCE:
			cal_backend_set_source (
				E_CAL_BACKEND (object),
				g_value_get_object (value));
			return;
		case PROP_URI:
			cal_backend_set_uri (
				E_CAL_BACKEND (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cal_backend_get_property (GObject *object,
                          guint property_id,
                          GValue *value,
                          GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CACHE_DIR:
			g_value_set_string (
				value, e_cal_backend_get_cache_dir (
				E_CAL_BACKEND (object)));
			return;
		case PROP_KIND:
			g_value_set_ulong (
				value, e_cal_backend_get_kind (
				E_CAL_BACKEND (object)));
			return;
		case PROP_SOURCE:
			g_value_set_object (
				value, e_cal_backend_get_source (
				E_CAL_BACKEND (object)));
			return;
		case PROP_URI:
			g_value_set_string (
				value, e_cal_backend_get_uri (
				E_CAL_BACKEND (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cal_backend_finalize (GObject *object)
{
	ECalBackendPrivate *priv;

	priv = E_CAL_BACKEND_GET_PRIVATE (object);

	g_assert (priv->clients == NULL);

	g_object_unref (priv->queries);

	g_mutex_free (priv->clients_mutex);
	g_mutex_free (priv->queries_mutex);

	g_free (priv->uri);
	g_free (priv->cache_dir);

	if (priv->source_changed_id && priv->source) {
		g_signal_handler_disconnect (priv->source, priv->source_changed_id);
		priv->source_changed_id = 0;
	}
	g_object_unref (priv->source);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_backend_parent_class)->finalize (object);
}

static void
cal_backend_constructed (GObject *object)
{
	cal_backend_set_default_cache_dir (E_CAL_BACKEND (object));
}

static void
e_cal_backend_class_init (ECalBackendClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (ECalBackendPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = cal_backend_set_property;
	object_class->get_property = cal_backend_get_property;
	object_class->finalize = cal_backend_finalize;
	object_class->constructed = cal_backend_constructed;

	g_object_class_install_property (
		object_class,
		PROP_CACHE_DIR,
		g_param_spec_string (
			"cache-dir",
			NULL,
			NULL,
			NULL,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_KIND,
		g_param_spec_ulong (
			"kind",
			NULL,
			NULL,
			ICAL_NO_COMPONENT,
			ICAL_XLICMIMEPART_COMPONENT,
			ICAL_NO_COMPONENT,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_SOURCE,
		g_param_spec_object (
			"source",
			NULL,
			NULL,
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_URI,
		g_param_spec_string (
			"uri",
			NULL,
			NULL,
			"",
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	signals[LAST_CLIENT_GONE] = g_signal_new (
		"last_client_gone",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ECalBackendClass, last_client_gone),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

static void
e_cal_backend_init (ECalBackend *backend)
{
	backend->priv = E_CAL_BACKEND_GET_PRIVATE (backend);

	backend->priv->clients = NULL;
	backend->priv->clients_mutex = g_mutex_new ();

	backend->priv->queries = e_list_new (
		(EListCopyFunc) g_object_ref,
		(EListFreeFunc) g_object_unref, NULL);
	backend->priv->queries_mutex = g_mutex_new ();
}

/**
 * e_cal_backend_get_cache_dir:
 * @backend: an #ECalBackend
 *
 * Returns the cache directory for the given backend.
 *
 * Returns: the cache directory for the backend
 *
 * Since: 2.32
 **/
const gchar *
e_cal_backend_get_cache_dir (ECalBackend *backend)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);

	return backend->priv->cache_dir;
}

/**
 * e_cal_backend_set_cache_dir:
 * @backend: an #ECalBackend
 * @cache_dir: a local cache directory
 *
 * Sets the cache directory for the given backend.
 *
 * Note that #ECalBackend is initialized with a usable default based on
 * #ECalBackend:source and #ECalBackend:kind properties.  Backends should
 * not override the default without good reason.
 *
 * Since: 2.32
 **/
void
e_cal_backend_set_cache_dir (ECalBackend *backend,
                             const gchar *cache_dir)
{
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (cache_dir != NULL);

	g_free (backend->priv->cache_dir);
	backend->priv->cache_dir = g_strdup (cache_dir);

	g_object_notify (G_OBJECT (backend), "cache-dir");
}

/**
 * e_cal_backend_get_kind:
 * @backend: an #ECalBackend
 *
 * Gets the kind of components the given backend stores.
 *
 * Returns: The kind of components for this backend.
 */
icalcomponent_kind
e_cal_backend_get_kind (ECalBackend *backend)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), ICAL_NO_COMPONENT);

	return backend->priv->kind;
}

/**
 * e_cal_backend_get_source:
 * @backend: an #ECalBackend
 *
 * Gets the #ESource associated with the given backend.
 *
 * Returns: The #ESource for the backend.
 */
ESource *
e_cal_backend_get_source (ECalBackend *backend)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);

	return backend->priv->source;
}

/**
 * e_cal_backend_get_uri:
 * @backend: an #ECalBackend
 *
 * Queries the URI of a calendar backend, which must already have an open
 * calendar.
 *
 * Returns: The URI where the calendar is stored.
 **/
const gchar *
e_cal_backend_get_uri (ECalBackend *backend)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);

	return backend->priv->uri;
}

static void
cal_destroy_cb (gpointer data, GObject *where_cal_was)
{
	e_cal_backend_remove_client_private (E_CAL_BACKEND (data),
					     (EDataCal *) where_cal_was, FALSE);
}

/**
 * e_cal_backend_add_client:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 *
 * Adds a new client to the given backend. For any event, the backend will
 * notify all clients added via this function.
 */
void
e_cal_backend_add_client (ECalBackend *backend, EDataCal *cal)
{
	ECalBackendPrivate *priv;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = backend->priv;

	g_object_weak_ref (G_OBJECT (cal), cal_destroy_cb, backend);

	g_mutex_lock (priv->clients_mutex);
	priv->clients = g_list_append (priv->clients, cal);
	g_mutex_unlock (priv->clients_mutex);
}

static void
e_cal_backend_remove_client_private (ECalBackend *backend, EDataCal *cal, gboolean weak_unref)
{
	ECalBackendPrivate *priv;

	/* XXX this needs a bit more thinking wrt the mutex - we
	   should be holding it when we check to see if clients is
	   NULL */
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = backend->priv;

	if (weak_unref)
		g_object_weak_unref (G_OBJECT (cal), cal_destroy_cb, backend);

	/* Disconnect */
	g_mutex_lock (priv->clients_mutex);
	priv->clients = g_list_remove (priv->clients, cal);
	g_mutex_unlock (priv->clients_mutex);

	/* When all clients go away, notify the parent factory about it so that
	 * it may decide whether to kill the backend or not.
	 */
	if (!priv->clients)
		g_signal_emit (backend, signals[LAST_CLIENT_GONE], 0);
}

/**
 * e_cal_backend_remove_client:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 *
 * Removes a client from the list of connected clients to the given backend.
 */
void
e_cal_backend_remove_client (ECalBackend *backend, EDataCal *cal)
{
	e_cal_backend_remove_client_private (backend, cal, TRUE);
}

/**
 * e_cal_backend_add_query:
 * @backend: an #ECalBackend
 * @query: An #EDataCalView object.
 *
 * Adds a query to the list of live queries being run by the given backend.
 * Doing so means that any listener on the query will get notified of any
 * change that affect the live query.
 */
void
e_cal_backend_add_query (ECalBackend *backend, EDataCalView *query)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_mutex_lock (backend->priv->queries_mutex);

	e_list_append (backend->priv->queries, query);

	g_mutex_unlock (backend->priv->queries_mutex);
}

/**
 * e_cal_backend_get_queries:
 * @backend: an #ECalBackend
 *
 * Gets the list of live queries being run on the given backend.
 *
 * Returns: The list of live queries.
 */
EList *
e_cal_backend_get_queries (ECalBackend *backend)
{
	g_return_val_if_fail (backend != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);

	return backend->priv->queries;
}

/**
 * e_cal_backend_remove_query
 * @backend: an #ECalBackend
 * @query: An #EDataCalView object, previously added with @ref e_cal_backend_add_query.
 *
 * Removes query from the list of live queries for the backend.
 *
 * Since: 2.24
 **/
void
e_cal_backend_remove_query (ECalBackend *backend, EDataCalView *query)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_mutex_lock (backend->priv->queries_mutex);

	e_list_remove (backend->priv->queries, query);

	g_mutex_unlock (backend->priv->queries_mutex);
}

/**
 * e_cal_backend_get_cal_address:
 * @backend: an #ECalBackend
 *
 * Queries the cal address associated with a calendar backend, which
 * must already have an open calendar.
 **/
void
e_cal_backend_get_cal_address (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->get_cal_address != NULL);
	(* CLASS (backend)->get_cal_address) (backend, cal, context);
}

void
e_cal_backend_notify_readonly (ECalBackend *backend, gboolean read_only)
{
	ECalBackendPrivate *priv;
	GList *l;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_readonly (priv->notification_proxy, read_only);
		return;
	}
	for (l = priv->clients; l; l = l->next)
		e_data_cal_notify_read_only (l->data, NULL /* Success */, read_only);
}

void
e_cal_backend_notify_cal_address (ECalBackend *backend, EServerMethodContext context, gchar *address)
{
	ECalBackendPrivate *priv;
	GList *l;

	priv = backend->priv;

	for (l = priv->clients; l; l = l->next)
		e_data_cal_notify_cal_address (l->data, context, NULL /* Success */, address);
}

/**
 * e_cal_backend_get_alarm_email_address:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 *
 * Calls the get_alarm_email_address method on the given backend.
 */
void
e_cal_backend_get_alarm_email_address (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->get_alarm_email_address != NULL);
	(* CLASS (backend)->get_alarm_email_address) (backend, cal, context);
}

/**
 *e_cal_backend_get_alarm_email_address:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 *
 * Calls the get_ldap_attribute method of the given backend.
 */
void
e_cal_backend_get_ldap_attribute (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->get_ldap_attribute != NULL);
	(* CLASS (backend)->get_ldap_attribute) (backend, cal, context);
}

/**
 * e_cal_backend_get_alarm_email_address:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 *
 * Calls the get_static_capabilities method on the given backend.
 */
void
e_cal_backend_get_static_capabilities (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->get_static_capabilities != NULL);
	(* CLASS (backend)->get_static_capabilities) (backend, cal, context);
}

/**
 * e_cal_backend_open:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @only_if_exists: Whether the calendar should be opened only if it already
 * exists.  If FALSE, a new calendar will be created when the specified @uri
 * does not exist.
 * @username: User name to use for authentication (if needed).
 * @password: Password for @username.
 *
 * Opens a calendar backend with data from a calendar stored at the specified
 * URI.
 */
void
e_cal_backend_open (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, gboolean only_if_exists,
		    const gchar *username, const gchar *password)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->open != NULL);
	(* CLASS (backend)->open) (backend, cal, context, only_if_exists, username, password);
}

/**
 * e_cal_backend_refresh:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 *
 * Refreshes the calendar being accessed by the given backend.
 *
 * Since: 2.30
 */
void
e_cal_backend_refresh (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->refresh != NULL);
	(* CLASS (backend)->refresh) (backend, cal, context);
}

/**
 * e_cal_backend_remove:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 *
 * Removes the calendar being accessed by the given backend.
 */
void
e_cal_backend_remove (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->remove != NULL);
	(* CLASS (backend)->remove) (backend, cal, context);
}

/**
 * e_cal_backend_is_loaded:
 * @backend: an #ECalBackend
 *
 * Queries whether a calendar backend has been loaded yet.
 *
 * Returns: TRUE if the backend has been loaded with data, FALSE
 * otherwise.
 */
gboolean
e_cal_backend_is_loaded (ECalBackend *backend)
{
	gboolean result;

	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), FALSE);

	g_assert (CLASS (backend)->is_loaded != NULL);
	result = (* CLASS (backend)->is_loaded) (backend);

	return result;
}

/**
 * e_cal_backend_is_read_only
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 *
 * Queries whether a calendar backend is read only or not.
 *
 */
void
e_cal_backend_is_read_only (ECalBackend *backend, EDataCal *cal)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->is_read_only != NULL);
	(* CLASS (backend)->is_read_only) (backend, cal);
}

/**
 * e_cal_backend_start_query:
 * @backend: an #ECalBackend
 * @query: The query to be started.
 *
 * Starts a new live query on the given backend.
 */
void
e_cal_backend_start_query (ECalBackend *backend, EDataCalView *query)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->start_query != NULL);
	(* CLASS (backend)->start_query) (backend, query);
}

/**
 * e_cal_backend_get_mode:
 * @backend: an #ECalBackend
 *
 * Queries whether a calendar backend is connected remotely.
 *
 * Returns: The current mode the calendar is in
 **/
CalMode
e_cal_backend_get_mode (ECalBackend *backend)
{
	CalMode result;

	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), FALSE);

	g_assert (CLASS (backend)->get_mode != NULL);
	result = (* CLASS (backend)->get_mode) (backend);

	return result;
}

/**
 * e_cal_backend_set_mode:
 * @backend: A calendar backend
 * @mode: Mode to change to
 *
 * Sets the mode of the calendar
 */
void
e_cal_backend_set_mode (ECalBackend *backend, CalMode mode)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->set_mode != NULL);
	(* CLASS (backend)->set_mode) (backend, mode);
}

/**
 * e_cal_backend_get_default_object:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 *
 * Calls the get_default_object method on the given backend.
 */
void
e_cal_backend_get_default_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->get_default_object != NULL);
	(* CLASS (backend)->get_default_object) (backend, cal, context);
}

/**
 * e_cal_backend_get_object:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @uid: Unique identifier for a calendar object.
 * @rid: ID for the object's recurrence to get.
 *
 * Queries a calendar backend for a calendar object based on its unique
 * identifier and its recurrence ID (if a recurrent appointment).
 */
void
e_cal_backend_get_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (uid != NULL);

	g_assert (CLASS (backend)->get_object != NULL);
	(* CLASS (backend)->get_object) (backend, cal, context, uid, rid);
}

/**
 * e_cal_backend_get_object_list:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @sexp: Expression to search for.
 *
 * Calls the get_object_list method on the given backend.
 */
void
e_cal_backend_get_object_list (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *sexp)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->get_object_list != NULL);
	(* CLASS (backend)->get_object_list) (backend, cal, context, sexp);
}

/**
 * e_cal_backend_get_attachment_list:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @uid: Unique identifier for a calendar object.
 * @rid: ID for the object's recurrence to get.
 *
 * Queries a calendar backend for attachments present in a calendar object based
 * on its unique identifier and its recurrence ID (if a recurrent appointment).
 */
void
e_cal_backend_get_attachment_list (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (uid != NULL);

	g_assert (CLASS (backend)->get_object != NULL);
	(* CLASS (backend)->get_attachment_list) (backend, cal, context, uid, rid);
}

/**
 * e_cal_backend_get_free_busy:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @users: List of users to get free/busy information for.
 * @start: Start time for query.
 * @end: End time for query.
 *
 * Gets a free/busy object for the given time interval
 */
void
e_cal_backend_get_free_busy (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, GList *users, time_t start, time_t end)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (start != -1 && end != -1);
	g_return_if_fail (start <= end);

	g_assert (CLASS (backend)->get_free_busy != NULL);
	(* CLASS (backend)->get_free_busy) (backend, cal, context, users, start, end);
}

/**
 * e_cal_backend_get_changes:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @change_id: A unique uid for the callers change list
 *
 * Builds a sequence of objects and the type of change that occurred on them since
 * the last time the give change_id was seen
 */
void
e_cal_backend_get_changes (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *change_id)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (change_id != NULL);

	g_assert (CLASS (backend)->get_changes != NULL);
	(* CLASS (backend)->get_changes) (backend, cal, context, change_id);
}

/**
 * e_cal_backend_discard_alarm
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @uid: UID of the component to discard the alarm from.
 * @auid: Alarm ID.
 *
 * Discards an alarm from the given component. This allows the specific backend
 * to do whatever is needed to really discard the alarm.
 */
void
e_cal_backend_discard_alarm (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *auid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (auid != NULL);

	g_assert (CLASS (backend)->discard_alarm != NULL);
	(* CLASS (backend)->discard_alarm) (backend, cal, context, uid, auid);
}

/**
 * e_cal_backend_create_object:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @calobj: The object to create.
 *
 * Calls the create_object method on the given backend.
 */
void
e_cal_backend_create_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobj != NULL);

	if (CLASS (backend)->create_object)
		(* CLASS (backend)->create_object) (backend, cal, context, calobj);
	else
		e_data_cal_notify_object_created (cal, context, EDC_ERROR (UnsupportedMethod), NULL, NULL);
}

/**
 * e_cal_backend_modify_object:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @calobj: Object to be modified.
 * @mod: Type of modification.
 *
 * Calls the modify_object method on the given backend.
 */
void
e_cal_backend_modify_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj, CalObjModType mod)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobj != NULL);

	if (CLASS (backend)->modify_object)
		(* CLASS (backend)->modify_object) (backend, cal, context, calobj, mod);
	else
		e_data_cal_notify_object_removed (cal, context, EDC_ERROR (UnsupportedMethod), NULL, NULL, NULL);
}

/**
 * e_cal_backend_remove_object:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @uid: Unique identifier of the object to remove.
 * @rid: A recurrence ID.
 * @mod: Type of removal.
 *
 * Removes an object in a calendar backend.  The backend will notify all of its
 * clients about the change.
 */
void
e_cal_backend_remove_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid, CalObjModType mod)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (uid != NULL);

	g_assert (CLASS (backend)->remove_object != NULL);
	(* CLASS (backend)->remove_object) (backend, cal, context, uid, rid, mod);
}

/**
 * e_cal_backend_receive_objects:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @calobj: iCalendar object.
 *
 * Calls the receive_objects method on the given backend.
 */
void
e_cal_backend_receive_objects (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobj != NULL);

	g_assert (CLASS (backend)->receive_objects != NULL);
	(* CLASS (backend)->receive_objects) (backend, cal, context, calobj);
}

/**
 * e_cal_backend_send_objects:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @calobj: iCalendar object to be sent.
 *
 * Calls the send_objects method on the given backend.
 */
void
e_cal_backend_send_objects (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobj != NULL);

	g_assert (CLASS (backend)->send_objects != NULL);
	(* CLASS (backend)->send_objects) (backend, cal, context, calobj);
}

/**
 * e_cal_backend_get_timezone:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @tzid: Unique identifier of a VTIMEZONE object. Note that this must not be
 * NULL.
 *
 * Returns the icaltimezone* corresponding to the TZID, or NULL if the TZID
 * can't be found.
 */
void
e_cal_backend_get_timezone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (tzid != NULL);

	g_assert (CLASS (backend)->get_timezone != NULL);
	(* CLASS (backend)->get_timezone) (backend, cal, context, tzid);
}

/**
 * e_cal_backend_set_default_zone:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @tzobj: The timezone object, in a string.
 *
 * Sets the default timezone for the calendar, which is used to resolve
 * DATE and floating DATE-TIME values.
 */
void
e_cal_backend_set_default_zone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzobj)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (tzobj != NULL);

	(* CLASS (backend)->set_default_zone) (backend, cal, context, tzobj);
}

/**
 * e_cal_backend_add_timezone
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @tzobj: The timezone object, in a string.
 *
 * Add a timezone object to the given backend.
 */
void
e_cal_backend_add_timezone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzobj)
{
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (tzobj != NULL);
	g_return_if_fail (CLASS (backend)->add_timezone != NULL);

	(* CLASS (backend)->add_timezone) (backend, cal, context, tzobj);
}

/**
 * e_cal_backend_internal_get_default_timezone:
 * @backend: an #ECalBackend
 *
 * Calls the internal_get_default_timezone method on the given backend.
 */
icaltimezone *
e_cal_backend_internal_get_default_timezone (ECalBackend *backend)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);
	g_return_val_if_fail (CLASS (backend)->internal_get_default_timezone != NULL, NULL);

	return (* CLASS (backend)->internal_get_default_timezone) (backend);
}

/**
 * e_cal_backend_internal_get_timezone:
 * @backend: an #ECalBackend
 * @tzid: ID of the timezone to get.
 *
 * Calls the internal_get_timezone method on the given backend.
 */
icaltimezone *
e_cal_backend_internal_get_timezone (ECalBackend *backend, const gchar *tzid)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);
	g_return_val_if_fail (tzid != NULL, NULL);
	g_return_val_if_fail (CLASS (backend)->internal_get_timezone != NULL, NULL);

	return (* CLASS (backend)->internal_get_timezone) (backend, tzid);
}

/**
 * e_cal_backend_set_notification_proxy:
 * @backend: an #ECalBackend
 * @proxy: The calendar backend to act as notification proxy.
 *
 * Sets the backend that will act as notification proxy for the given backend.
 */
void
e_cal_backend_set_notification_proxy (ECalBackend *backend, ECalBackend *proxy)
{
	ECalBackendPrivate *priv;

	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	priv = backend->priv;

	priv->notification_proxy = proxy;
}

/**
 * e_cal_backend_notify_object_created:
 * @backend: an #ECalBackend
 * @calobj: iCalendar representation of new object
 *
 * Notifies each of the backend's listeners about a new object.
 *
 * #e_data_cal_notify_object_created() calls this for you. You only need to
 * call e_cal_backend_notify_object_created() yourself to report objects
 * created by non-EDS clients.
 **/
void
e_cal_backend_notify_object_created (ECalBackend *backend, const gchar *calobj)
{
	ECalBackendPrivate *priv;
	EList *queries;
	EIterator *iter;
	EDataCalView *query;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_object_created (priv->notification_proxy, calobj);
		return;
	}

	queries = e_cal_backend_get_queries (backend);
	iter = e_list_get_iterator (queries);

	while (e_iterator_is_valid (iter)) {
		query = QUERY (e_iterator_get (iter));

		g_object_ref (query);
		if (e_data_cal_view_object_matches (query, calobj))
			e_data_cal_view_notify_objects_added_1 (query, calobj);
		g_object_unref (query);

		e_iterator_next (iter);
	}
	g_object_unref (iter);
}

static void
match_query_and_notify (EDataCalView *query, const gchar *old_object, const gchar *object)
{
	gboolean old_match = FALSE, new_match = FALSE;

	if (old_object)
		old_match = e_data_cal_view_object_matches (query, old_object);

	new_match = e_data_cal_view_object_matches (query, object);
	if (old_match && new_match)
		e_data_cal_view_notify_objects_modified_1 (query, object);
	else if (new_match)
		e_data_cal_view_notify_objects_added_1 (query, object);
	else if (old_match) {
		ECalComponent *comp = NULL;

		comp = e_cal_component_new_from_string (old_object);
		if (comp) {
			ECalComponentId *id = e_cal_component_get_id (comp);

			e_data_cal_view_notify_objects_removed_1 (query, id);

			e_cal_component_free_id (id);
			g_object_unref (comp);
		}
	}
}

/**
 * e_cal_backend_notify_view_progress_start
 * @backend: an #ECalBackend
 *
 * This methods has to be used before e_cal_backend_notify_view_progress.
 * Sets last notified percent value to 0.
 *
 * Since: 2.24
 **/
void
e_cal_backend_notify_view_progress_start (ECalBackend *backend)
{
	ECalBackendPrivate *priv;

	priv = backend->priv;

	priv->last_percent_notified = 0;
}

/**
 * e_cal_backend_notify_view_progress:
 * @backend: an #ECalBackend
 * @message: the UID of the removed object
 * @percent: percentage of the objects loaded in the view
 *
 * Notifies each of the backend's listeners about the view_progress in downloading the items.
 **/
void
e_cal_backend_notify_view_progress (ECalBackend *backend, const gchar *message, gint percent)
{
	ECalBackendPrivate *priv;
	EList *queries;
	EIterator *iter;
	EDataCalView *query;

	priv = backend->priv;

	if (percent <= priv->last_percent_notified)
		return;

	priv->last_percent_notified = percent;

	if (priv->notification_proxy) {
		e_cal_backend_notify_view_progress (priv->notification_proxy, message, percent);
		return;
	}

	queries = e_cal_backend_get_queries (backend);
	iter = e_list_get_iterator (queries);

	while (e_iterator_is_valid (iter)) {
		query = QUERY (e_iterator_get (iter));

		g_object_ref (query);

		e_data_cal_view_notify_progress (query, message, percent);

		g_object_unref (query);

		e_iterator_next (iter);
	}
	g_object_unref (iter);
}

/**
 * e_cal_backend_notify_view_done:
 * @backend: an #ECalBackend
 * @error: returns the error, if any, once the view is fully populated.
 *
 * Notifies each of the backend's listeners about the view_done in downloading the items.
 **/
void
e_cal_backend_notify_view_done (ECalBackend *backend, const GError *error)
{
	ECalBackendPrivate *priv;
	EList *queries;
	EIterator *iter;
	EDataCalView *query;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_view_done (priv->notification_proxy, error);
		return;
	}

	queries = e_cal_backend_get_queries (backend);
	iter = e_list_get_iterator (queries);

	while (e_iterator_is_valid (iter)) {
		query = QUERY (e_iterator_get (iter));

		g_object_ref (query);

		e_data_cal_view_notify_done (query, error);

		g_object_unref (query);

		e_iterator_next (iter);
	}
	g_object_unref (iter);
}

/**
 * e_cal_backend_notify_object_modified:
 * @backend: an #ECalBackend
 * @old_object: iCalendar representation of the original form of the object
 * @object: iCalendar representation of the new form of the object
 *
 * Notifies each of the backend's listeners about a modified object.
 *
 * #e_data_cal_notify_object_modified() calls this for you. You only need to
 * call e_cal_backend_notify_object_modified() yourself to report objects
 * modified by non-EDS clients.
 **/
void
e_cal_backend_notify_object_modified (ECalBackend *backend,
				      const gchar *old_object, const gchar *object)
{
	ECalBackendPrivate *priv;
	EList *queries;
	EIterator *iter;
	EDataCalView *query;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_object_modified (priv->notification_proxy, old_object, object);
		return;
	}

	queries = e_cal_backend_get_queries (backend);
	iter = e_list_get_iterator (queries);

	while (e_iterator_is_valid (iter)) {
		query = QUERY (e_iterator_get (iter));

		g_object_ref (query);
		match_query_and_notify (query, old_object, object);
		g_object_unref (query);

		e_iterator_next (iter);
	}
	g_object_unref (iter);
}

/**
 * e_cal_backend_notify_object_removed:
 * @backend: an #ECalBackend
 * @id: the Id of the removed object
 * @old_object: iCalendar representation of the removed object
 * @new_object: iCalendar representation of the object after the removal. This
 * only applies to recurrent appointments that had an instance removed. In that
 * case, this function notifies a modification instead of a removal.
 *
 * Notifies each of the backend's listeners about a removed object.
 *
 * e_data_cal_notify_object_removed() calls this for you. You only need to
 * call e_cal_backend_notify_object_removed() yourself to report objects
 * removed by non-EDS clients.
 **/
void
e_cal_backend_notify_object_removed (ECalBackend *backend, const ECalComponentId *id,
				     const gchar *old_object, const gchar *object)
{
	ECalBackendPrivate *priv;
	EList *queries;
	EIterator *iter;
	EDataCalView *query;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_object_removed (priv->notification_proxy, id, old_object, object);
		return;
	}

	queries = e_cal_backend_get_queries (backend);
	iter = e_list_get_iterator (queries);

	while (e_iterator_is_valid (iter)) {
		query = QUERY (e_iterator_get (iter));

		g_object_ref (query);

		if (object == NULL) {
			/* if object == NULL, it means the object has been completely
			   removed from the backend */
			if (!old_object || e_data_cal_view_object_matches (query, old_object))
				e_data_cal_view_notify_objects_removed_1 (query, id);
		} else
			match_query_and_notify (query, old_object, object);

		g_object_unref (query);

		e_iterator_next (iter);
	}
	g_object_unref (iter);
}

/**
 * e_cal_backend_notify_objects_added:
 *
 * Since: 2.24
 **/
void
e_cal_backend_notify_objects_added (ECalBackend *backend, EDataCalView *query, const GList *objects)
{
	e_data_cal_view_notify_objects_added (query, objects);
}

/**
 * e_cal_backend_notify_objects_removed:
 *
 * Since: 2.24
 **/
void
e_cal_backend_notify_objects_removed (ECalBackend *backend, EDataCalView *query, const GList *ids)
{
	e_data_cal_view_notify_objects_removed (query, ids);
}

/**
 * e_cal_backend_notify_objects_modified:
 *
 * Since: 2.24
 **/
void
e_cal_backend_notify_objects_modified (ECalBackend *backend, EDataCalView *query, const GList *objects)
{
	e_data_cal_view_notify_objects_modified (query, objects);
}

/**
 * e_cal_backend_notify_mode:
 * @backend: an #ECalBackend
 * @status: Status of the mode set
 * @mode: the current mode
 *
 * Notifies each of the backend's listeners about the results of a
 * setMode call.
 **/
void
e_cal_backend_notify_mode (ECalBackend *backend,
			   EDataCalViewListenerSetModeStatus status,
			   EDataCalMode mode)
{
	ECalBackendPrivate *priv = backend->priv;
	GList *l;

	if (priv->notification_proxy) {
		e_cal_backend_notify_mode (priv->notification_proxy, status, mode);
		return;
	}

	for (l = priv->clients; l; l = l->next)
		e_data_cal_notify_mode (l->data, status, mode);
}

/**
 * e_cal_backend_notify_auth_required:
 * @backend: an #ECalBackend
 *
 * Notifies each of the backend's listeners that authentication is required to
 * open the calendar.
 */
void
e_cal_backend_notify_auth_required (ECalBackend *backend)
{
        ECalBackendPrivate *priv = backend->priv;
        GList *l;

        for (l = priv->clients; l; l = l->next)
                e_data_cal_notify_auth_required (l->data);
}

/**
 * e_cal_backend_notify_error:
 * @backend: an #ECalBackend
 * @message: Error message
 *
 * Notifies each of the backend's listeners about an error
 **/
void
e_cal_backend_notify_error (ECalBackend *backend, const gchar *message)
{
	ECalBackendPrivate *priv = backend->priv;
	GList *l;

	if (priv->notification_proxy) {
		e_cal_backend_notify_error (priv->notification_proxy, message);
		return;
	}

	for (l = priv->clients; l; l = l->next)
		e_data_cal_notify_error (l->data, message);
}

/**
 * e_cal_backend_empty_cache:
 * @backend: an #ECalBackend
 * @cache: Backend's cache to empty.
 *
 * Empties backend's cache with all notifications and so on, thus all listening
 * will know there is nothing in this backend.
 *
 * Since: 2.28
 **/
void
e_cal_backend_empty_cache (ECalBackend *backend, ECalBackendCache *cache)
{
	GList *comps_in_cache;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	if (!cache)
		return;

	g_return_if_fail (E_IS_CAL_BACKEND_CACHE (cache));

	e_file_cache_freeze_changes (E_FILE_CACHE (cache));

	for (comps_in_cache = e_cal_backend_cache_get_components (cache);
	     comps_in_cache;
	     comps_in_cache = comps_in_cache->next) {
		gchar *comp_str;
		ECalComponentId *id;
		ECalComponent *comp = comps_in_cache->data;

		id = e_cal_component_get_id (comp);
		comp_str = e_cal_component_get_as_string (comp);

		e_cal_backend_cache_remove_component (cache, id->uid, id->rid);
		e_cal_backend_notify_object_removed (backend, id, comp_str, NULL);

		g_free (comp_str);
		e_cal_component_free_id (id);
		g_object_unref (comp);
	}

	g_list_free (comps_in_cache);

	e_file_cache_thaw_changes (E_FILE_CACHE (cache));
}

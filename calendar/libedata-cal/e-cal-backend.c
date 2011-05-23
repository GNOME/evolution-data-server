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

#include <glib/gi18n-lib.h>

#include <libedataserver/e-data-server-util.h>

#include "e-cal-backend.h"
#include "e-cal-backend-cache.h"

#define EDC_ERROR(_code) e_data_cal_create_error (_code, NULL)
#define EDC_OPENING_ERROR e_data_cal_create_error (Busy, _("Cannot process, calendar backend is opening"))

/* Private part of the CalBackend structure */
struct _ECalBackendPrivate {
	/* The source for this backend */
	ESource *source;
	/* The kind of components for this backend */
	icalcomponent_kind kind;

	gboolean opening, opened, readonly, removed, online;

	/* URI, from source. This is cached, since we return const. */
	gchar *uri;

	gchar *cache_dir;

	/* List of Cal objects */
	GMutex *clients_mutex;
	GSList *clients;

	GMutex *views_mutex;
	GSList *views;

	/* ECalBackend to pass notifications on to */
	ECalBackend *notification_proxy;

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
		g_signal_handlers_disconnect_matched (backend->priv->source, G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA, 0, 0, NULL, source_changed_cb, backend);
		g_object_unref (backend->priv->source);
	}

	if (source != NULL)
		g_signal_connect (
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
cal_backend_get_backend_property (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *prop_name)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (cal != NULL);
	g_return_if_fail (prop_name != NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_OPENED)) {
		e_data_cal_respond_get_backend_property (cal, opid, NULL, e_cal_backend_is_opened (backend) ? "TRUE" : "FALSE");
	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_OPENING)) {
		e_data_cal_respond_get_backend_property (cal, opid, NULL, e_cal_backend_is_opening (backend) ? "TRUE" : "FALSE");
	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_ONLINE)) {
		e_data_cal_respond_get_backend_property (cal, opid, NULL, e_cal_backend_is_online (backend) ? "TRUE" : "FALSE");
	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_READONLY)) {
		e_data_cal_respond_get_backend_property (cal, opid, NULL, e_cal_backend_is_readonly (backend) ? "TRUE" : "FALSE");
	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CACHE_DIR)) {
		e_data_cal_respond_get_backend_property (cal, opid, NULL, e_cal_backend_get_cache_dir (backend));
	} else {
		e_data_cal_respond_get_backend_property (cal, opid, e_data_cal_create_error_fmt (NotSupported, _("Unknown calendar property '%s'"), prop_name), NULL);
	}
}

static void
cal_backend_set_backend_property (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *prop_name, const gchar *prop_value)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (cal != NULL);
	g_return_if_fail (prop_name != NULL);

	e_data_cal_respond_set_backend_property (cal, opid, e_data_cal_create_error_fmt (NotSupported, _("Cannot change value of calendar property '%s'"), prop_name));
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

	priv = E_CAL_BACKEND (object)->priv;

	g_assert (priv->clients == NULL);

	g_slist_free (priv->views);
	/* should be NULL, anyway */
	g_slist_free (priv->clients);

	g_mutex_free (priv->clients_mutex);
	g_mutex_free (priv->views_mutex);

	g_free (priv->uri);
	g_free (priv->cache_dir);

	if (priv->source) {
		g_signal_handlers_disconnect_matched (priv->source, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, object);
		g_object_unref (priv->source);
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_backend_parent_class)->finalize (object);
}

static void
cal_backend_constructed (GObject *object)
{
	cal_backend_set_default_cache_dir (E_CAL_BACKEND (object));

	G_OBJECT_CLASS (e_cal_backend_parent_class)->constructed (object);
}

static void
e_cal_backend_class_init (ECalBackendClass *klass)
{
	GObjectClass *object_class;

	g_type_class_add_private (klass, sizeof (ECalBackendPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->set_property = cal_backend_set_property;
	object_class->get_property = cal_backend_get_property;
	object_class->finalize = cal_backend_finalize;
	object_class->constructed = cal_backend_constructed;

	klass->get_backend_property = cal_backend_get_backend_property;
	klass->set_backend_property = cal_backend_set_backend_property;

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
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ECalBackendClass, last_client_gone),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

static void
e_cal_backend_init (ECalBackend *backend)
{
	backend->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		backend, E_TYPE_CAL_BACKEND, ECalBackendPrivate);

	backend->priv->clients = NULL;
	backend->priv->clients_mutex = g_mutex_new ();

	backend->priv->views = NULL;
	backend->priv->views_mutex = g_mutex_new ();

	backend->priv->readonly = TRUE;
	backend->priv->online = FALSE;
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
 * e_cal_backend_is_online:
 * @backend: an #ECalBackend
 *
 * Returns: Whether is backend online.
 **/
gboolean
e_cal_backend_is_online (ECalBackend *backend)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), FALSE);

	return backend->priv->online;
}

/**
 * e_cal_backend_is_opened:
 * @backend: an #ECalBackend
 *
 * Checks if @backend's storage has been opened (and
 * authenticated, if necessary) and the backend itself
 * is ready for accessing. This property is changed automatically
 * within call of e_cal_backend_notify_opened().
 *
 * Returns: %TRUE if fully opened, %FALSE otherwise.
 **/
gboolean
e_cal_backend_is_opened (ECalBackend *backend)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), FALSE);

	return backend->priv->opened;
}

/**
 * e_cal_backend_is_opening::
 * @backend: an #ECalBackend
 *
 * Checks if @backend is processing its opening phase, which
 * includes everything since the e_cal_backend_open() call,
 * through authentication, up to e_cal_backend_notify_opened().
 * This property is managed automatically and the backend deny
 * every operation except of cancel and authenticate_user while
 * it is being opening.
 *
 * Returns: %TRUE if opening phase is in the effect, %FALSE otherwise.
 **/
gboolean
e_cal_backend_is_opening (ECalBackend *backend)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), FALSE);

	return backend->priv->opening;
}

/**
 * e_cal_backend_is_readonly:
 * @backend: an #ECalBackend
 *
 * Returns: Whether is backend read-only. This value is the last used
 * in a call of e_cal_backend_notify_readonly().
 **/
gboolean
e_cal_backend_is_readonly (ECalBackend *backend)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), FALSE);

	return backend->priv->readonly;
}

/**
 * e_cal_backend_is_removed:
 * @backend: an #ECalBackend
 *
 * Checks if @backend has been removed from its physical storage.
 *
 * Returns: %TRUE if @backend has been removed, %FALSE otherwise.
 **/
gboolean
e_cal_backend_is_removed (ECalBackend *backend)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), FALSE);

	return backend->priv->removed;
}

/**
 * e_cal_backend_set_is_removed:
 * @backend: an #ECalBackend
 * @is_removed: A flag indicating whether the backend's storage was removed
 *
 * Sets the flag indicating whether @backend was removed to @is_removed.
 * Meant to be used by backend implementations.
 **/
void
e_cal_backend_set_is_removed (ECalBackend *backend, gboolean is_removed)
{
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	backend->priv->removed = is_removed;
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
 * e_cal_backend_get_backend_property:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @prop_name: property name to get value of; cannot be NULL
 *
 * Calls the get_backend_property method on the given backend.
 * This might be finished with e_data_cal_respond_get_backend_property().
 * Default implementation takes care of common properties and returns
 * an 'unsupported' error for any unknown properties. The subclass may
 * always call this default implementation for properties which fetching
 * it doesn't overwrite.
 *
 * Since: 3.2
 **/
void
e_cal_backend_get_backend_property (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *prop_name)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->get_backend_property != NULL);

	(* E_CAL_BACKEND_GET_CLASS (backend)->get_backend_property) (backend, cal, opid, cancellable, prop_name);
}

/**
 * e_cal_backend_set_backend_property:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @prop_name: property name to change; cannot be NULL
 * @prop_value: value to set to @prop_name; cannot be NULL
 *
 * Calls the set_backend_property method on the given backend.
 * This might be finished with e_data_cal_respond_set_backend_property().
 * Default implementation simply returns an 'unsupported' error.
 * The subclass may always call this default implementation for properties
 * which fetching it doesn't overwrite.
 *
 * Since: 3.2
 **/
void
e_cal_backend_set_backend_property (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *prop_name, const gchar *prop_value)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (prop_value != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->set_backend_property != NULL);

	(* E_CAL_BACKEND_GET_CLASS (backend)->set_backend_property) (backend, cal, opid, cancellable, prop_name, prop_value);
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
	priv->clients = g_slist_append (priv->clients, cal);
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
	priv->clients = g_slist_remove (priv->clients, cal);
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
 * e_cal_backend_add_view:
 * @backend: an #ECalBackend
 * @view: An #EDataCalView object.
 *
 * Adds a view to the list of live views being run by the given backend.
 * Doing so means that any listener on the view will get notified of any
 * change that affect the live view.
 */
void
e_cal_backend_add_view (ECalBackend *backend, EDataCalView *view)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_mutex_lock (backend->priv->views_mutex);

	backend->priv->views = g_slist_append (backend->priv->views, view);

	g_mutex_unlock (backend->priv->views_mutex);
}

/**
 * e_cal_backend_remove_view
 * @backend: an #ECalBackend
 * @view: An #EDataCalView object, previously added with @ref e_cal_backend_add_view.
 *
 * Removes view from the list of live views for the backend.
 *
 * Since: 2.24
 **/
void
e_cal_backend_remove_view (ECalBackend *backend, EDataCalView *view)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_mutex_lock (backend->priv->views_mutex);

	backend->priv->views = g_slist_remove (backend->priv->views, view);

	g_mutex_unlock (backend->priv->views_mutex);
}

/**
 * e_cal_backend_foreach_view:
 * @backend: an #ECalBackend
 * @callback: callback to call
 * @user_data: user_data passed into the @callback
 *
 * Calls @callback for each known calendar view of this @backend.
 * @callback returns %FALSE to stop further processing.
 **/
void
e_cal_backend_foreach_view (ECalBackend *backend, gboolean (* callback) (EDataCalView *view, gpointer user_data), gpointer user_data)
{
	const GSList *views;
	EDataCalView *view;
	gboolean stop = FALSE;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (callback != NULL);

	g_mutex_lock (backend->priv->views_mutex);

	for (views = backend->priv->views; views && !stop; views = views->next) {
		view = E_DATA_CAL_VIEW (views->data);

		g_object_ref (view);
		stop = !callback (view, user_data);
		g_object_unref (view);
	}

	g_mutex_unlock (backend->priv->views_mutex);
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
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	backend->priv->notification_proxy = proxy;
}

/**
 * e_cal_backend_set_online:
 * @backend: A calendar backend
 * @is_online: Whether is online
 *
 * Sets the online mode of the calendar
 */
void
e_cal_backend_set_online (ECalBackend *backend, gboolean is_online)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->set_online != NULL);

	(* E_CAL_BACKEND_GET_CLASS (backend)->set_online) (backend, is_online);
}

/**
 * e_cal_backend_open:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @only_if_exists: Whether the calendar should be opened only if it already
 * exists.  If FALSE, a new calendar will be created when the specified @uri
 * does not exist.
 *
 * Opens a calendar backend with data from a calendar stored at the specified URI.
 * This might be finished with e_data_cal_respond_open() or e_cal_backend_respond_opened(),
 * though the overall opening phase finishes only after call
 * of e_cal_backend_notify_opened() after which call the backend
 * is either fully opened (including authentication against (remote)
 * server/storage) or an error was encountered during this opening phase.
 * 'opened' and 'opening' properties are updated automatically.
 * The backend refuses all other operations until the opening phase is finished.
 *
 * The e_cal_backend_notify_opened() is called either from this function
 * or from e_cal_backend_authenticate_user(), or after necessary steps
 * initiated by these two functions.
 *
 * The opening phase usually works like this:
 * 1) client requests open for the backend
 * 2) server receives this request and calls e_cal_backend_open() - the opening phase begun
 * 3) either the backend is opened during this call, and notifies client
 *    with e_cal_backend_notify_opened() about that. This is usually
 *    for local backends; their opening phase is finished
 * 4) or the backend requires authentication, thus it notifies client
 *    about that with e_cal_backend_notify_auth_required() and is
 *    waiting for credentials, which will be received from client
 *    by e_cal_backend_authenticate_user() call. Backend's opening
 *    phase is still running in this case, thus it doesn't call
 *    e_cal_backend_notify_opened() within e_cal_backend_open() call.
 * 5) when backend receives credentials in e_cal_backend_authenticate_user()
 *    then it tries to authenticate against a server/storage with them
 *    and only after it knows result of the authentication, whether user
 *    was or wasn't authenticated, it notifies client with the result
 *    by e_cal_backend_notify_opened() and it's opening phase is
 *    finished now. If there was no error returned then the backend is
 *    considered opened, otherwise it's considered closed. Use AuthenticationFailed
 *    error when the given credentials were rejected by the server/store, which
 *    will result in a re-prompt on the client side, otherwise use AuthenticationRequired
 *    if there was anything wrong with the given credentials. Set error's
 *    message to a reason for a re-prompt, it'll be shown to a user.
 * 6) client checks error returned from e_cal_backend_notify_opened() and
 *    reprompts for a password if it was AuthenticationFailed. Otherwise
 *    considers backend opened based on the error presence (no error means success).
 *
 * In any case, the call of e_cal_backend_open() should be always finished
 * with e_data_cal_respond_open(), which has no influence on the opening phase,
 * or alternatively with e_cal_backend_respond_opened(). Never use authentication
 * errors in e_data_cal_respond_open() to notify the client the authentication is
 * required, there is e_cal_backend_notify_auth_required() for this.
 **/
void
e_cal_backend_open (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, gboolean only_if_exists)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->open != NULL);

	g_mutex_lock (backend->priv->clients_mutex);

	if (e_cal_backend_is_opened (backend)) {
		g_mutex_unlock (backend->priv->clients_mutex);

		e_data_cal_report_readonly (cal, backend->priv->readonly);
		e_data_cal_report_online (cal, backend->priv->online);

		e_cal_backend_respond_opened (backend, cal, opid, NULL);
	} else if (e_cal_backend_is_opening (backend)) {
		g_mutex_unlock (backend->priv->clients_mutex);

		e_data_cal_respond_open (cal, opid, EDC_OPENING_ERROR);
	} else {
		backend->priv->opening = TRUE;
		g_mutex_unlock (backend->priv->clients_mutex);

		(* E_CAL_BACKEND_GET_CLASS (backend)->open) (backend, cal, opid, cancellable, only_if_exists);
	}
}

/**
 * e_cal_backend_authenticate_user:
 * @backend: an #ECalBackend
 * @cancellable: a #GCancellable for the operation
 * @credentials: #ECredentials to use for authentication
 *
 * Notifies @backend about @credentials provided by user to use
 * for authentication. This notification is usually called during
 * opening phase as a response to e_cal_backend_notify_auth_required()
 * on the client side and it results in setting property 'opening' to %TRUE
 * unless the backend is already opened. This function finishes opening
 * phase, thus it should be finished with e_cal_backend_notify_opened().
 *
 * See information at e_cal_backend_open() for more details
 * how the opening phase works.
 **/
void
e_cal_backend_authenticate_user (ECalBackend  *backend,
				 GCancellable *cancellable,
				 ECredentials *credentials)
{
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (credentials != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->authenticate_user);

	if (!e_cal_backend_is_opened (backend))
		backend->priv->opening = TRUE;

	(* E_CAL_BACKEND_GET_CLASS (backend)->authenticate_user) (backend, cancellable, credentials);
}

/**
 * e_cal_backend_remove:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 *
 * Removes the calendar being accessed by the given backend.
 * This might be finished with e_data_cal_respond_remove().
 **/
void
e_cal_backend_remove (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->remove != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_remove (cal, opid, EDC_OPENING_ERROR);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->remove) (backend, cal, opid, cancellable);
}

/**
 * e_cal_backend_refresh:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 *
 * Refreshes the calendar being accessed by the given backend.
 * This might be finished with e_data_cal_respond_refresh(),
 * and it might be called as soon as possible; it doesn't mean
 * that the refreshing is done after calling that, the backend
 * is only notifying client whether it started the refresh process
 * or not.
 *
 * Since: 2.30
 **/
void
e_cal_backend_refresh (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_refresh (cal, opid, EDC_OPENING_ERROR);
	else if (!E_CAL_BACKEND_GET_CLASS (backend)->refresh)
		e_data_cal_respond_refresh (cal, opid, EDC_ERROR (UnsupportedMethod));
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->refresh) (backend, cal, opid, cancellable);
}

/**
 * e_cal_backend_get_object:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @uid: Unique identifier for a calendar object.
 * @rid: ID for the object's recurrence to get.
 *
 * Queries a calendar backend for a calendar object based on its unique
 * identifier and its recurrence ID (if a recurrent appointment).
 * This might be finished with e_data_cal_respond_get_object().
 **/
void
e_cal_backend_get_object (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *uid, const gchar *rid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->get_object != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_get_object (cal, opid, EDC_OPENING_ERROR, NULL);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->get_object) (backend, cal, opid, cancellable, uid, rid);
}

/**
 * e_cal_backend_get_object_list:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @sexp: Expression to search for.
 *
 * Calls the get_object_list method on the given backend.
 * This might be finished with e_data_cal_respond_get_object_list().
 **/
void
e_cal_backend_get_object_list (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *sexp)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->get_object_list != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_get_object_list (cal, opid, EDC_OPENING_ERROR, NULL);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->get_object_list) (backend, cal, opid, cancellable, sexp);
}

/**
 * e_cal_backend_get_free_busy:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @users: List of users to get free/busy information for.
 * @start: Start time for query.
 * @end: End time for query.
 *
 * Gets a free/busy object for the given time interval. Client side is
 * notified about free/busy objects throug e_data_cal_report_free_busy_data().
 * This might be finished with e_data_cal_respond_get_free_busy().
 **/
void
e_cal_backend_get_free_busy (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const GSList *users, time_t start, time_t end)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (start != -1 && end != -1);
	g_return_if_fail (start <= end);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->get_free_busy != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_get_free_busy (cal, opid, EDC_OPENING_ERROR);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->get_free_busy) (backend, cal, opid, cancellable, users, start, end);
}

/**
 * e_cal_backend_create_object:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @calobj: The object to create.
 *
 * Calls the create_object method on the given backend.
 * This might be finished with e_data_cal_respond_create_object().
 **/
void
e_cal_backend_create_object (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *calobj)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobj != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_create_object (cal, opid, EDC_OPENING_ERROR, NULL, NULL);
	else if (E_CAL_BACKEND_GET_CLASS (backend)->create_object)
		(* E_CAL_BACKEND_GET_CLASS (backend)->create_object) (backend, cal, opid, cancellable, calobj);
	else
		e_data_cal_respond_create_object (cal, opid, EDC_ERROR (UnsupportedMethod), NULL, NULL);
}

/**
 * e_cal_backend_modify_object:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @calobj: Object to be modified.
 * @mod: Type of modification.
 *
 * Calls the modify_object method on the given backend.
 * This might be finished with e_data_cal_respond_modify_object().
 **/
void
e_cal_backend_modify_object (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *calobj, CalObjModType mod)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobj != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_modify_object (cal, opid, EDC_OPENING_ERROR, NULL, NULL);
	else if (E_CAL_BACKEND_GET_CLASS (backend)->modify_object)
		(* E_CAL_BACKEND_GET_CLASS (backend)->modify_object) (backend, cal, opid, cancellable, calobj, mod);
	else
		e_data_cal_respond_modify_object (cal, opid, EDC_ERROR (UnsupportedMethod), NULL, NULL);
}

/**
 * e_cal_backend_remove_object:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @uid: Unique identifier of the object to remove.
 * @rid: A recurrence ID.
 * @mod: Type of removal.
 *
 * Removes an object in a calendar backend.  The backend will notify all of its
 * clients about the change.
 * This might be finished with e_data_cal_respond_remove_object().
 **/
void
e_cal_backend_remove_object (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *uid, const gchar *rid, CalObjModType mod)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->remove_object != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_remove_object (cal, opid, EDC_OPENING_ERROR, NULL, NULL, NULL);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->remove_object) (backend, cal, opid, cancellable, uid, rid, mod);
}

/**
 * e_cal_backend_receive_objects:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @calobj: iCalendar object.
 *
 * Calls the receive_objects method on the given backend.
 * This might be finished with e_data_cal_respond_receive_objects().
 **/
void
e_cal_backend_receive_objects (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *calobj)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobj != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->receive_objects != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_receive_objects (cal, opid, EDC_OPENING_ERROR);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->receive_objects) (backend, cal, opid, cancellable, calobj);
}

/**
 * e_cal_backend_send_objects:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @calobj: iCalendar object to be sent.
 *
 * Calls the send_objects method on the given backend.
 * This might be finished with e_data_cal_respond_send_objects().
 **/
void
e_cal_backend_send_objects (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *calobj)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobj != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->send_objects != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_send_objects (cal, opid, EDC_OPENING_ERROR, NULL, NULL);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->send_objects) (backend, cal, opid, cancellable, calobj);
}

/**
 * e_cal_backend_get_attachment_uris:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @uid: Unique identifier for a calendar object.
 * @rid: ID for the object's recurrence to get.
 *
 * Queries a calendar backend for attachments present in a calendar object based
 * on its unique identifier and its recurrence ID (if a recurrent appointment).
 * This might be finished with e_data_cal_respond_get_attachment_uris().
 **/
void
e_cal_backend_get_attachment_uris (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *uid, const gchar *rid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->get_attachment_uris != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_get_attachment_uris (cal, opid, EDC_OPENING_ERROR, NULL);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->get_attachment_uris) (backend, cal, opid, cancellable, uid, rid);
}

/**
 * e_cal_backend_discard_alarm:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @uid: Unique identifier for a calendar object.
 * @rid: ID for the object's recurrence to discard alarm in.
 * @auid: Unique identifier of the alarm itself.
 *
 * Discards alarm @auid from the object identified by @uid and @rid.
 * This might be finished with e_data_cal_respond_discard_alarm().
 * Default implementation of this method returns Not Supported error.
 **/
void
e_cal_backend_discard_alarm (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *uid, const gchar *rid, const gchar *auid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (auid != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_discard_alarm (cal, opid, EDC_OPENING_ERROR);
	else if (E_CAL_BACKEND_GET_CLASS (backend)->discard_alarm)
		(* E_CAL_BACKEND_GET_CLASS (backend)->discard_alarm) (backend, cal, opid, cancellable, uid, rid, auid);
	else
		e_data_cal_respond_discard_alarm (cal, opid, e_data_cal_create_error (NotSupported, NULL));
}

/**
 * e_cal_backend_get_timezone:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @tzid: Unique identifier of a VTIMEZONE object. Note that this must not be
 * NULL.
 *
 * Returns the icaltimezone* corresponding to the TZID, or NULL if the TZID
 * can't be found.
 * This might be finished with e_data_cal_respond_get_timezone().
 **/
void
e_cal_backend_get_timezone (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *tzid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (tzid != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->get_timezone != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_get_timezone (cal, opid, EDC_OPENING_ERROR, NULL);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->get_timezone) (backend, cal, opid, cancellable, tzid);
}

/**
 * e_cal_backend_add_timezone
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @tzobj: The timezone object, in a string.
 *
 * Add a timezone object to the given backend.
 * This might be finished with e_data_cal_respond_add_timezone().
 **/
void
e_cal_backend_add_timezone (ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *tzobject)
{
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (tzobject != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->add_timezone != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_add_timezone (cal, opid, EDC_OPENING_ERROR);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->add_timezone) (backend, cal, opid, cancellable, tzobject);
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
	g_return_val_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->internal_get_timezone != NULL, NULL);

	return (* E_CAL_BACKEND_GET_CLASS (backend)->internal_get_timezone) (backend, tzid);
}

/**
 * e_cal_backend_start_view:
 * @backend: an #ECalBackend
 * @view: The view to be started.
 *
 * Starts a new live view on the given backend.
 */
void
e_cal_backend_start_view (ECalBackend *backend, EDataCalView *view)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->start_view != NULL);

	(* E_CAL_BACKEND_GET_CLASS (backend)->start_view) (backend, view);
}

/**
 * e_cal_backend_stop_view:
 * @backend: an #ECalBackend
 * @view: The view to be stopped.
 *
 * Stops a previously started live view on the given backend.
 *
 * Since: 3.2
 */
void
e_cal_backend_stop_view (ECalBackend *backend, EDataCalView *view)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	/* backward compatibility, do not force each backend define this function */
	if (!E_CAL_BACKEND_GET_CLASS (backend)->stop_view)
		return;

	(* E_CAL_BACKEND_GET_CLASS (backend)->stop_view) (backend, view);
}

static gboolean
object_created_cb (EDataCalView *view, gpointer calobj)
{
	if (e_data_cal_view_object_matches (view, calobj))
		e_data_cal_view_notify_objects_added_1 (view, calobj);

	return TRUE;
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

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_object_created (priv->notification_proxy, calobj);
		return;
	}

	e_cal_backend_foreach_view (backend, object_created_cb, (gpointer) calobj);
}

/**
 * e_cal_backend_notify_objects_added:
 *
 * Since: 2.24
 **/
void
e_cal_backend_notify_objects_added (ECalBackend *backend, EDataCalView *view, const GSList *objects)
{
	e_data_cal_view_notify_objects_added (view, objects);
}

static void
match_view_and_notify (EDataCalView *view, const gchar *old_object, const gchar *object)
{
	gboolean old_match = FALSE, new_match = FALSE;

	if (old_object)
		old_match = e_data_cal_view_object_matches (view, old_object);

	new_match = e_data_cal_view_object_matches (view, object);
	if (old_match && new_match)
		e_data_cal_view_notify_objects_modified_1 (view, object);
	else if (new_match)
		e_data_cal_view_notify_objects_added_1 (view, object);
	else if (old_match) {
		ECalComponent *comp = NULL;

		comp = e_cal_component_new_from_string (old_object);
		if (comp) {
			ECalComponentId *id = e_cal_component_get_id (comp);

			e_data_cal_view_notify_objects_removed_1 (view, id);

			e_cal_component_free_id (id);
			g_object_unref (comp);
		}
	}
}

struct call_data {
	const gchar *old_object;
	const gchar *object;
	const ECalComponentId *id;
};

static gboolean
call_match_and_notify (EDataCalView *view, gpointer user_data)
{
	struct call_data *cd = user_data;

	g_return_val_if_fail (user_data != NULL, FALSE);

	match_view_and_notify (view, cd->old_object, cd->object);

	return TRUE;
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
e_cal_backend_notify_object_modified (ECalBackend *backend, const gchar *old_object, const gchar *object)
{
	ECalBackendPrivate *priv;
	struct call_data cd;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_object_modified (priv->notification_proxy, old_object, object);
		return;
	}

	cd.old_object = old_object;
	cd.object = object;
	cd.id = NULL;

	e_cal_backend_foreach_view (backend, call_match_and_notify, &cd);
}

/**
 * e_cal_backend_notify_objects_modified:
 *
 * Since: 2.24
 **/
void
e_cal_backend_notify_objects_modified (ECalBackend *backend, EDataCalView *view, const GSList *objects)
{
	e_data_cal_view_notify_objects_modified (view, objects);
}

static gboolean
object_removed_cb (EDataCalView *view, gpointer user_data)
{
	struct call_data *cd = user_data;

	g_return_val_if_fail (user_data != NULL, FALSE);

	if (cd->object == NULL) {
		/* if object == NULL, it means the object has been completely
		   removed from the backend */
		if (!cd->old_object || e_data_cal_view_object_matches (view, cd->old_object))
			e_data_cal_view_notify_objects_removed_1 (view, cd->id);
	} else
		match_view_and_notify (view, cd->old_object, cd->object);

	return TRUE;
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
	struct call_data cd;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_object_removed (priv->notification_proxy, id, old_object, object);
		return;
	}

	cd.old_object = old_object;
	cd.object = object;
	cd.id = id;

	e_cal_backend_foreach_view (backend, object_removed_cb, &cd);
}

/**
 * e_cal_backend_notify_objects_removed:
 *
 * Since: 2.24
 **/
void
e_cal_backend_notify_objects_removed (ECalBackend *backend, EDataCalView *view, const GSList *ids)
{
	e_data_cal_view_notify_objects_removed (view, ids);
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
	GSList *l;

	if (priv->notification_proxy) {
		e_cal_backend_notify_error (priv->notification_proxy, message);
		return;
	}

	g_mutex_lock (priv->clients_mutex);

	for (l = priv->clients; l; l = l->next)
		e_data_cal_report_error (l->data, message);

	g_mutex_unlock (priv->clients_mutex);
}

/**
 * e_cal_backend_notify_readonly:
 * @backend: an #ECalBackend
 * @is_readonly: flag indicating readonly status
 *
 * Notifies all backend's clients about the current readonly state.
 * Meant to be used by backend implementations.
 **/
void
e_cal_backend_notify_readonly (ECalBackend *backend, gboolean is_readonly)
{
	ECalBackendPrivate *priv;
	GSList *l;

	priv = backend->priv;
	priv->readonly = is_readonly;

	if (priv->notification_proxy) {
		e_cal_backend_notify_readonly (priv->notification_proxy, is_readonly);
		return;
	}

	g_mutex_lock (priv->clients_mutex);

	for (l = priv->clients; l; l = l->next)
		e_data_cal_report_readonly (l->data, is_readonly);

	g_mutex_unlock (priv->clients_mutex);
}

/**
 * e_cal_backend_notify_online:
 * @backend: an #ECalBackend
 * @is_online: flag indicating whether @backend is connected and online
 *
 * Notifies clients of @backend's connection status indicated by @is_online.
 * Meant to be used by backend implementations.
 **/
void
e_cal_backend_notify_online (ECalBackend *backend, gboolean is_online)
{
	ECalBackendPrivate *priv;
	GSList *clients;

	priv = backend->priv;
	priv->online = is_online;

	if (priv->notification_proxy) {
		e_cal_backend_notify_online (priv->notification_proxy, is_online);
		return;
	}

	g_mutex_lock (priv->clients_mutex);

	for (clients = priv->clients; clients != NULL; clients = g_slist_next (clients))
		e_data_cal_report_online (E_DATA_CAL (clients->data), is_online);

	g_mutex_unlock (priv->clients_mutex);
}

/**
 * e_cal_backend_notify_auth_required:
 * @backend: an #ECalBackend
 * @is_self: Use %TRUE to indicate the authentication is required
 *    for the @backend, otheriwse the authentication is for any
 *    other source. Having @credentials %NULL means @is_self
 *    automatically.
 * @credentials: an #ECredentials that contains extra information for
 *    a source for which authentication is requested.
 *    This parameter can be NULL to indicate "for this calendar".
 *
 * Notifies clients that @backend requires authentication in order to
 * connect. This function call does not influence 'opening', but 
 * influences 'opened' property, which is set to %FALSE when @is_self
 * is %TRUE or @credentials is %NULL. Opening phase is finished
 * by e_cal_backend_notify_opened() if this is requested for @backend.
 *
 * See e_cal_backend_open() for a description how the whole opening
 * phase works.
 *
 * Meant to be used by backend implementations.
 **/
void
e_cal_backend_notify_auth_required (ECalBackend *backend, gboolean is_self, const ECredentials *credentials)
{
	ECalBackendPrivate *priv;
	GSList *clients;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_auth_required (priv->notification_proxy, is_self, credentials);
		return;
	}

	g_mutex_lock (priv->clients_mutex);

	if (is_self || !credentials)
		priv->opened = FALSE;

	for (clients = priv->clients; clients != NULL; clients = g_slist_next (clients))
		e_data_cal_report_auth_required (E_DATA_CAL (clients->data), credentials);

	g_mutex_unlock (priv->clients_mutex);
}

/**
 * e_cal_backend_notify_opened:
 * @backend: an #ECalBackend
 * @error: a #GError corresponding to the error encountered during
 *    the opening phase. Use %NULL for success. The @error is freed
 *    automatically if not %NULL.
 *
 * Notifies clients that @backend finished its opening phase.
 * See e_cal_backend_open() for more information how the opening
 * phase works. Calling this function changes 'opening' property,
 * same as 'opened'. 'opening' is set to %FALSE and the backend
 * is considered 'opened' only if the @error is %NULL.
 *
 * See also: e_cal_backend_respond_opened()
 *
 * Note: The @error is freed automatically if not %NULL.
 *
 * Meant to be used by backend implementations.
 **/
void
e_cal_backend_notify_opened (ECalBackend *backend, GError *error)
{
	ECalBackendPrivate *priv;
	GSList *clients;

	priv = backend->priv;
	g_mutex_lock (priv->clients_mutex);

	priv->opening = FALSE;
	priv->opened = error == NULL;

	for (clients = priv->clients; clients != NULL; clients = g_slist_next (clients))
		e_data_cal_report_opened (E_DATA_CAL (clients->data), error);

	g_mutex_unlock (priv->clients_mutex);

	if (error)
		g_error_free (error);
}

/**
 * e_cal_backend_respond_opened:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: an operation ID
 * @error: result error; can be %NULL, if it isn't then it's automatically freed
 *
 * This is a replacement for e_data_cal_respond_open() for cases where
 * the finish of 'open' method call also finishes backend opening phase.
 * This function covers calling of both e_data_cal_respond_open() and
 * e_cal_backend_notify_opened() with the same @error.
 *
 * See e_cal_backend_open() for more details how the opening phase works.
 **/
void
e_cal_backend_respond_opened (ECalBackend *backend, EDataCal *cal, guint32 opid, GError *error)
{
	GError *copy = NULL;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (cal != NULL);
	g_return_if_fail (opid != 0);

	if (error)
		copy = g_error_copy (error);

	e_data_cal_respond_open (cal, opid, error);
	e_cal_backend_notify_opened (backend, copy);
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

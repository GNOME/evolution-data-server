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

#include "e-cal-backend.h"
#include "e-cal-backend-cache.h"

#define E_CAL_BACKEND_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL_BACKEND, ECalBackendPrivate))

#define EDC_ERROR(_code)	e_data_cal_create_error (_code, NULL)
#define EDC_OPENING_ERROR	e_data_cal_create_error (Busy, _("Cannot process, calendar backend is opening"))
#define EDC_NOT_OPENED_ERROR	e_data_cal_create_error (NotOpened, NULL)

/* Private part of the CalBackend structure */
struct _ECalBackendPrivate {
	ESourceRegistry *registry;

	/* The kind of components for this backend */
	icalcomponent_kind kind;

	gboolean opening, opened, readonly, removed;

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
	PROP_REGISTRY
};

static void e_cal_backend_remove_client_private (ECalBackend *backend, EDataCal *cal, gboolean weak_unref);

G_DEFINE_TYPE (ECalBackend, e_cal_backend, E_TYPE_BACKEND);

static void
cal_backend_set_default_cache_dir (ECalBackend *backend)
{
	ESource *source;
	icalcomponent_kind kind;
	const gchar *component_type;
	const gchar *user_cache_dir;
	const gchar *uid;
	gchar *filename;

	user_cache_dir = e_get_user_cache_dir ();

	kind = e_cal_backend_get_kind (backend);
	source = e_backend_get_source (E_BACKEND (backend));

	uid = e_source_get_uid (source);
	g_return_if_fail (uid != NULL);

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

	filename = g_build_filename (
		user_cache_dir, component_type, uid, NULL);
	e_cal_backend_set_cache_dir (backend, filename);
	g_free (filename);
}

static void
cal_backend_get_backend_property (ECalBackend *backend,
                                  EDataCal *cal,
                                  guint32 opid,
                                  GCancellable *cancellable,
                                  const gchar *prop_name)
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
		e_data_cal_respond_get_backend_property (cal, opid, NULL, e_backend_get_online (E_BACKEND (backend)) ? "TRUE" : "FALSE");
	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_READONLY)) {
		e_data_cal_respond_get_backend_property (cal, opid, NULL, e_cal_backend_is_readonly (backend) ? "TRUE" : "FALSE");
	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CACHE_DIR)) {
		e_data_cal_respond_get_backend_property (cal, opid, NULL, e_cal_backend_get_cache_dir (backend));
	} else {
		e_data_cal_respond_get_backend_property (cal, opid, e_data_cal_create_error_fmt (NotSupported, _("Unknown calendar property '%s'"), prop_name), NULL);
	}
}

static void
cal_backend_set_backend_property (ECalBackend *backend,
                                  EDataCal *cal,
                                  guint32 opid,
                                  GCancellable *cancellable,
                                  const gchar *prop_name,
                                  const gchar *prop_value)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (cal != NULL);
	g_return_if_fail (prop_name != NULL);

	e_data_cal_respond_set_backend_property (cal, opid, e_data_cal_create_error_fmt (NotSupported, _("Cannot change value of calendar property '%s'"), prop_name));
}

static void
cal_backend_set_kind (ECalBackend *backend,
                      icalcomponent_kind kind)
{
	backend->priv->kind = kind;
}

static void
cal_backend_set_registry (ECalBackend *backend,
                          ESourceRegistry *registry)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (backend->priv->registry == NULL);

	backend->priv->registry = g_object_ref (registry);
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

		case PROP_REGISTRY:
			cal_backend_set_registry (
				E_CAL_BACKEND (object),
				g_value_get_object (value));
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

		case PROP_REGISTRY:
			g_value_set_object (
				value, e_cal_backend_get_registry (
				E_CAL_BACKEND (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cal_backend_dispose (GObject *object)
{
	ECalBackendPrivate *priv;

	priv = E_CAL_BACKEND_GET_PRIVATE (object);

	if (priv->registry != NULL) {
		g_object_unref (priv->registry);
		priv->registry = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_cal_backend_parent_class)->dispose (object);
}

static void
cal_backend_finalize (GObject *object)
{
	ECalBackendPrivate *priv;

	priv = E_CAL_BACKEND_GET_PRIVATE (object);

	g_assert (priv->clients == NULL);

	g_slist_free (priv->views);
	/* should be NULL, anyway */
	g_slist_free (priv->clients);

	g_mutex_free (priv->clients_mutex);
	g_mutex_free (priv->views_mutex);

	g_free (priv->cache_dir);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_backend_parent_class)->finalize (object);
}

static void
cal_backend_constructed (GObject *object)
{
	cal_backend_set_default_cache_dir (E_CAL_BACKEND (object));

	G_OBJECT_CLASS (e_cal_backend_parent_class)->constructed (object);
}

static gboolean
cal_backend_authenticate_sync (EBackend *backend,
                               ESourceAuthenticator *auth,
                               GCancellable *cancellable,
                               GError **error)
{
	ECalBackend *cal_backend;
	ESourceRegistry *registry;
	ESource *source;

	cal_backend = E_CAL_BACKEND (backend);
	registry = e_cal_backend_get_registry (cal_backend);
	source = e_backend_get_source (backend);

	return e_source_registry_authenticate_sync (
		registry, source, auth, cancellable, error);
}

static void
e_cal_backend_class_init (ECalBackendClass *class)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;

	g_type_class_add_private (class, sizeof (ECalBackendPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = cal_backend_set_property;
	object_class->get_property = cal_backend_get_property;
	object_class->dispose = cal_backend_dispose;
	object_class->finalize = cal_backend_finalize;
	object_class->constructed = cal_backend_constructed;

	backend_class = E_BACKEND_CLASS (class);
	backend_class->authenticate_sync = cal_backend_authenticate_sync;

	class->get_backend_property = cal_backend_get_backend_property;
	class->set_backend_property = cal_backend_set_backend_property;

	g_object_class_install_property (
		object_class,
		PROP_CACHE_DIR,
		g_param_spec_string (
			"cache-dir",
			"Cache Dir",
			"The backend's cache directory",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_KIND,
		g_param_spec_ulong (
			"kind",
			"Kind",
			"The kind of iCalendar components "
			"this backend manages",
			ICAL_NO_COMPONENT,
			ICAL_XLICMIMEPART_COMPONENT,
			ICAL_NO_COMPONENT,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_REGISTRY,
		g_param_spec_object (
			"registry",
			"Registry",
			"Data source registry",
			E_TYPE_SOURCE_REGISTRY,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_cal_backend_init (ECalBackend *backend)
{
	backend->priv = E_CAL_BACKEND_GET_PRIVATE (backend);

	backend->priv->clients = NULL;
	backend->priv->clients_mutex = g_mutex_new ();

	backend->priv->views = NULL;
	backend->priv->views_mutex = g_mutex_new ();

	backend->priv->readonly = TRUE;
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
 * e_cal_backend_get_registry:
 * @backend: an #ECalBackend
 *
 * Returns the data source registry to which #EBackend:source belongs.
 *
 * Returns: an #ESourceRegistry
 *
 * Since: 3.6
 **/
ESourceRegistry *
e_cal_backend_get_registry (ECalBackend *backend)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);

	return backend->priv->registry;
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
 *
 * Since: 3.2
 **/
gboolean
e_cal_backend_is_opened (ECalBackend *backend)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), FALSE);

	return backend->priv->opened;
}

/**
 * e_cal_backend_is_opening:
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
 *
 * Since: 3.2
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
 *
 * Since: 3.2
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
 *
 * Since: 3.2
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
 *
 * Since: 3.2
 **/
void
e_cal_backend_set_is_removed (ECalBackend *backend,
                              gboolean is_removed)
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

	if (g_strcmp0 (backend->priv->cache_dir, cache_dir) == 0)
		return;

	g_free (backend->priv->cache_dir);
	backend->priv->cache_dir = g_strdup (cache_dir);

	g_object_notify (G_OBJECT (backend), "cache-dir");
}

/**
 * e_cal_backend_create_cache_filename:
 * @backend: an #ECalBackend
 * @uid: a component UID
 * @filename: a filename to use; can be NULL
 * @fileindex: index of a file; used only when @filename is NULL
 *
 * Returns: a filename for an attachment in a local cache dir. Free returned
 * pointer with a g_free().
 *
 * Since: 3.4
 **/
gchar *
e_cal_backend_create_cache_filename (ECalBackend *backend,
                                     const gchar *uid,
                                     const gchar *filename,
                                     gint fileindex)
{
	g_return_val_if_fail (backend != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);

	return e_filename_mkdir_encoded (e_cal_backend_get_cache_dir (backend), uid, filename, fileindex);
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
e_cal_backend_get_backend_property (ECalBackend *backend,
                                    EDataCal *cal,
                                    guint32 opid,
                                    GCancellable *cancellable,
                                    const gchar *prop_name)
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
e_cal_backend_set_backend_property (ECalBackend *backend,
                                    EDataCal *cal,
                                    guint32 opid,
                                    GCancellable *cancellable,
                                    const gchar *prop_name,
                                    const gchar *prop_value)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (prop_value != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->set_backend_property != NULL);

	(* E_CAL_BACKEND_GET_CLASS (backend)->set_backend_property) (backend, cal, opid, cancellable, prop_name, prop_value);
}

static void
cal_destroy_cb (gpointer data,
                GObject *where_cal_was)
{
	e_cal_backend_remove_client_private (
		E_CAL_BACKEND (data),
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
e_cal_backend_add_client (ECalBackend *backend,
                          EDataCal *cal)
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
e_cal_backend_remove_client_private (ECalBackend *backend,
                                     EDataCal *cal,
                                     gboolean weak_unref)
{
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_CAL (cal));

	if (weak_unref)
		g_object_weak_unref (G_OBJECT (cal), cal_destroy_cb, backend);

	/* Make sure the backend stays alive while holding the mutex. */
	g_object_ref (backend);

	/* Disconnect */
	g_mutex_lock (backend->priv->clients_mutex);
	backend->priv->clients = g_slist_remove (backend->priv->clients, cal);

	if (backend->priv->clients == NULL)
		backend->priv->opening = FALSE;

	g_mutex_unlock (backend->priv->clients_mutex);

	g_object_unref (backend);
}

/**
 * e_cal_backend_remove_client:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 *
 * Removes a client from the list of connected clients to the given backend.
 */
void
e_cal_backend_remove_client (ECalBackend *backend,
                             EDataCal *cal)
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
 *
 * Since: 3.2
 */
void
e_cal_backend_add_view (ECalBackend *backend,
                        EDataCalView *view)
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
 * Since: 3.2
 **/
void
e_cal_backend_remove_view (ECalBackend *backend,
                           EDataCalView *view)
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
 *
 * Since: 3.2
 **/
void
e_cal_backend_foreach_view (ECalBackend *backend,
                            gboolean (*callback) (EDataCalView *view,
                                                  gpointer user_data),
                            gpointer user_data)
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
 *
 * Since: 3.2
 */
void
e_cal_backend_set_notification_proxy (ECalBackend *backend,
                                      ECalBackend *proxy)
{
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	backend->priv->notification_proxy = proxy;
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
e_cal_backend_open (ECalBackend *backend,
                    EDataCal *cal,
                    guint32 opid,
                    GCancellable *cancellable,
                    gboolean only_if_exists)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->open != NULL);

	g_mutex_lock (backend->priv->clients_mutex);

	if (e_cal_backend_is_opened (backend)) {
		gboolean online;

		g_mutex_unlock (backend->priv->clients_mutex);

		e_data_cal_report_readonly (cal, backend->priv->readonly);

		online = e_backend_get_online (E_BACKEND (backend));
		e_data_cal_report_online (cal, online);

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
e_cal_backend_remove (ECalBackend *backend,
                      EDataCal *cal,
                      guint32 opid,
                      GCancellable *cancellable)
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
e_cal_backend_refresh (ECalBackend *backend,
                       EDataCal *cal,
                       guint32 opid,
                       GCancellable *cancellable)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_refresh (cal, opid, EDC_OPENING_ERROR);
	else if (!E_CAL_BACKEND_GET_CLASS (backend)->refresh)
		e_data_cal_respond_refresh (cal, opid, EDC_ERROR (UnsupportedMethod));
	else if (!e_cal_backend_is_opened (backend))
		e_data_cal_respond_refresh (cal, opid, EDC_NOT_OPENED_ERROR);
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
e_cal_backend_get_object (ECalBackend *backend,
                          EDataCal *cal,
                          guint32 opid,
                          GCancellable *cancellable,
                          const gchar *uid,
                          const gchar *rid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->get_object != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_get_object (cal, opid, EDC_OPENING_ERROR, NULL);
	else if (!e_cal_backend_is_opened (backend))
		e_data_cal_respond_get_object (cal, opid, EDC_NOT_OPENED_ERROR, NULL);
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
e_cal_backend_get_object_list (ECalBackend *backend,
                               EDataCal *cal,
                               guint32 opid,
                               GCancellable *cancellable,
                               const gchar *sexp)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->get_object_list != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_get_object_list (cal, opid, EDC_OPENING_ERROR, NULL);
	else if (!e_cal_backend_is_opened (backend))
		e_data_cal_respond_get_object_list (cal, opid, EDC_NOT_OPENED_ERROR, NULL);
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
e_cal_backend_get_free_busy (ECalBackend *backend,
                             EDataCal *cal,
                             guint32 opid,
                             GCancellable *cancellable,
                             const GSList *users,
                             time_t start,
                             time_t end)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (start != -1 && end != -1);
	g_return_if_fail (start <= end);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->get_free_busy != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_get_free_busy (cal, opid, EDC_OPENING_ERROR);
	else if (!e_cal_backend_is_opened (backend))
		e_data_cal_respond_get_free_busy (cal, opid, EDC_NOT_OPENED_ERROR);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->get_free_busy) (backend, cal, opid, cancellable, users, start, end);
}

/**
 * e_cal_backend_create_objects:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @calobjs: The objects to create (list of gchar *).
 *
 * Calls the create_object method on the given backend.
 * This might be finished with e_data_cal_respond_create_objects().
 *
 * Since: 3.6
 **/
void
e_cal_backend_create_objects (ECalBackend *backend,
                              EDataCal *cal,
                              guint32 opid,
                              GCancellable *cancellable,
                              const GSList *calobjs)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobjs != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_create_objects (cal, opid, EDC_OPENING_ERROR, NULL, NULL);
	else if (!E_CAL_BACKEND_GET_CLASS (backend)->create_objects)
		e_data_cal_respond_create_objects (cal, opid, EDC_ERROR (UnsupportedMethod), NULL, NULL);
	else if (!e_cal_backend_is_opened (backend))
		e_data_cal_respond_create_objects (cal, opid, EDC_NOT_OPENED_ERROR, NULL, NULL);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->create_objects) (backend, cal, opid, cancellable, calobjs);
}

/**
 * e_cal_backend_modify_objects:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @calobjs: Objects to be modified (list of gchar *).
 * @mod: Type of modification.
 *
 * Calls the modify_objects method on the given backend.
 * This might be finished with e_data_cal_respond_modify_objects().
 *
 * Since: 3.6
 **/
void
e_cal_backend_modify_objects (ECalBackend *backend,
                              EDataCal *cal,
                              guint32 opid,
                              GCancellable *cancellable,
                              const GSList *calobjs,
                              CalObjModType mod)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobjs != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_modify_objects (cal, opid, EDC_OPENING_ERROR, NULL, NULL);
	else if (!E_CAL_BACKEND_GET_CLASS (backend)->modify_objects)
		e_data_cal_respond_modify_objects (cal, opid, EDC_ERROR (UnsupportedMethod), NULL, NULL);
	else if (!e_cal_backend_is_opened (backend))
		e_data_cal_respond_modify_objects (cal, opid, EDC_NOT_OPENED_ERROR, NULL, NULL);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->modify_objects) (backend, cal, opid, cancellable, calobjs, mod);
}

/**
 * e_cal_backend_remove_objects:
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @ids: List of #ECalComponentId objects identifying the objects to remove
 * @mod: Type of removal.
 *
 * Removes objects in a calendar backend.  The backend will notify all of its
 * clients about the change.
 * This might be finished with e_data_cal_respond_remove_objects().
 *
 * Since: 3.6
 **/
void
e_cal_backend_remove_objects (ECalBackend *backend,
                              EDataCal *cal,
                              guint32 opid,
                              GCancellable *cancellable,
                              const GSList *ids,
                              CalObjModType mod)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (ids != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->remove_objects != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_remove_objects (cal, opid, EDC_OPENING_ERROR, NULL, NULL, NULL);
	else if (!e_cal_backend_is_opened (backend))
		e_data_cal_respond_remove_objects (cal, opid, EDC_NOT_OPENED_ERROR, NULL, NULL, NULL);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->remove_objects) (backend, cal, opid, cancellable, ids, mod);
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
e_cal_backend_receive_objects (ECalBackend *backend,
                               EDataCal *cal,
                               guint32 opid,
                               GCancellable *cancellable,
                               const gchar *calobj)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobj != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->receive_objects != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_receive_objects (cal, opid, EDC_OPENING_ERROR);
	else if (!e_cal_backend_is_opened (backend))
		e_data_cal_respond_receive_objects (cal, opid, EDC_NOT_OPENED_ERROR);
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
e_cal_backend_send_objects (ECalBackend *backend,
                            EDataCal *cal,
                            guint32 opid,
                            GCancellable *cancellable,
                            const gchar *calobj)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobj != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->send_objects != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_send_objects (cal, opid, EDC_OPENING_ERROR, NULL, NULL);
	else if (!e_cal_backend_is_opened (backend))
		e_data_cal_respond_send_objects (cal, opid, EDC_NOT_OPENED_ERROR, NULL, NULL);
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
 *
 * Since: 3.2
 **/
void
e_cal_backend_get_attachment_uris (ECalBackend *backend,
                                   EDataCal *cal,
                                   guint32 opid,
                                   GCancellable *cancellable,
                                   const gchar *uid,
                                   const gchar *rid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->get_attachment_uris != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_get_attachment_uris (cal, opid, EDC_OPENING_ERROR, NULL);
	else if (!e_cal_backend_is_opened (backend))
		e_data_cal_respond_get_attachment_uris (cal, opid, EDC_NOT_OPENED_ERROR, NULL);
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
e_cal_backend_discard_alarm (ECalBackend *backend,
                             EDataCal *cal,
                             guint32 opid,
                             GCancellable *cancellable,
                             const gchar *uid,
                             const gchar *rid,
                             const gchar *auid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (auid != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_discard_alarm (cal, opid, EDC_OPENING_ERROR);
	else if (!E_CAL_BACKEND_GET_CLASS (backend)->discard_alarm)
		e_data_cal_respond_discard_alarm (cal, opid, e_data_cal_create_error (NotSupported, NULL));
	else if (!e_cal_backend_is_opened (backend))
		e_data_cal_respond_discard_alarm (cal, opid, EDC_NOT_OPENED_ERROR);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->discard_alarm) (backend, cal, opid, cancellable, uid, rid, auid);
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
e_cal_backend_get_timezone (ECalBackend *backend,
                            EDataCal *cal,
                            guint32 opid,
                            GCancellable *cancellable,
                            const gchar *tzid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (tzid != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->get_timezone != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_get_timezone (cal, opid, EDC_OPENING_ERROR, NULL);
	else if (!e_cal_backend_is_opened (backend))
		e_data_cal_respond_get_timezone (cal, opid, EDC_NOT_OPENED_ERROR, NULL);
	else
		(* E_CAL_BACKEND_GET_CLASS (backend)->get_timezone) (backend, cal, opid, cancellable, tzid);
}

/**
 * e_cal_backend_add_timezone
 * @backend: an #ECalBackend
 * @cal: an #EDataCal
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @tzobject: The timezone object, in a string.
 *
 * Add a timezone object to the given backend.
 * This might be finished with e_data_cal_respond_add_timezone().
 **/
void
e_cal_backend_add_timezone (ECalBackend *backend,
                            EDataCal *cal,
                            guint32 opid,
                            GCancellable *cancellable,
                            const gchar *tzobject)
{
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (tzobject != NULL);
	g_return_if_fail (E_CAL_BACKEND_GET_CLASS (backend)->add_timezone != NULL);

	if (e_cal_backend_is_opening (backend))
		e_data_cal_respond_add_timezone (cal, opid, EDC_OPENING_ERROR);
	else if (!e_cal_backend_is_opened (backend))
		e_data_cal_respond_add_timezone (cal, opid, EDC_NOT_OPENED_ERROR);
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
e_cal_backend_internal_get_timezone (ECalBackend *backend,
                                     const gchar *tzid)
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
 *
 * Since: 3.2
 */
void
e_cal_backend_start_view (ECalBackend *backend,
                          EDataCalView *view)
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
e_cal_backend_stop_view (ECalBackend *backend,
                         EDataCalView *view)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	/* backward compatibility, do not force each backend define this function */
	if (!E_CAL_BACKEND_GET_CLASS (backend)->stop_view)
		return;

	(* E_CAL_BACKEND_GET_CLASS (backend)->stop_view) (backend, view);
}

static gboolean
component_created_cb (EDataCalView *view,
                      gpointer data)
{
	ECalComponent *comp = data;

	if (e_data_cal_view_component_matches (view, comp))
		e_data_cal_view_notify_components_added_1 (view, comp);

	return TRUE;
}

/**
 * e_cal_backend_notify_component_created:
 * @backend: an #ECalBackend
 * @component: the newly created #ECalComponent
 *
 * Notifies each of the backend's listeners about a new object.
 *
 * Like e_cal_backend_notify_object_created() except takes an #ECalComponent
 * instead of an ical string representation and uses the #EDataCalView's
 * fields-of-interest to filter out unwanted information from ical strings
 * sent over the bus.
 *
 * Since: 3.4
 **/
void
e_cal_backend_notify_component_created (ECalBackend *backend,
                                        /* const */ ECalComponent *component)
{
	ECalBackendPrivate *priv;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_component_created (priv->notification_proxy, component);
		return;
	}

	e_cal_backend_foreach_view (backend, component_created_cb, component);
}

static void
match_view_and_notify_component (EDataCalView *view,
                                 ECalComponent *old_component,
                                 ECalComponent *new_component)
{
	gboolean old_match = FALSE, new_match = FALSE;

	if (old_component)
		old_match = e_data_cal_view_component_matches (view, old_component);

	new_match = e_data_cal_view_component_matches (view, new_component);

	if (old_match && new_match)
		e_data_cal_view_notify_components_modified_1 (view, new_component);
	else if (new_match)
		e_data_cal_view_notify_components_added_1 (view, new_component);
	else if (old_match) {

		ECalComponentId *id = e_cal_component_get_id (old_component);

		e_data_cal_view_notify_objects_removed_1 (view, id);

		e_cal_component_free_id (id);
	}
}

struct component_call_data {
	ECalComponent         *old_component;
	ECalComponent         *new_component;
	const ECalComponentId *id;
};

static gboolean
call_match_and_notify_component (EDataCalView *view,
                                 gpointer user_data)
{
	struct component_call_data *cd = user_data;

	g_return_val_if_fail (user_data != NULL, FALSE);

	match_view_and_notify_component (view, cd->old_component, cd->new_component);

	return TRUE;
}

/**
 * e_cal_backend_notify_component_modified:
 * @backend: an #ECalBackend
 * @old_component: the #ECalComponent before the modification
 * @new_component: the #ECalComponent after the modification
 *
 * Notifies each of the backend's listeners about a modified object.
 *
 * Like e_cal_backend_notify_object_modified() except takes an #ECalComponent
 * instead of an ical string representation and uses the #EDataCalView's
 * fields-of-interest to filter out unwanted information from ical strings
 * sent over the bus.
 *
 * Since: 3.4
 **/
void
e_cal_backend_notify_component_modified (ECalBackend *backend,
                                         /* const */ ECalComponent *old_component,
                                         /* const */ ECalComponent *new_component)
{
	ECalBackendPrivate *priv;
	struct component_call_data cd;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_component_modified (priv->notification_proxy, old_component, new_component);
		return;
	}

	cd.old_component = old_component;
	cd.new_component = new_component;
	cd.id            = NULL;

	e_cal_backend_foreach_view (backend, call_match_and_notify_component, &cd);
}

static gboolean
component_removed_cb (EDataCalView *view,
                      gpointer user_data)
{
	struct component_call_data *cd = user_data;

	g_return_val_if_fail (user_data != NULL, FALSE);

	if (cd->new_component == NULL) {
		/* if object == NULL, it means the object has been completely
		 * removed from the backend */
		if (!cd->old_component || e_data_cal_view_component_matches (view, cd->old_component))
			e_data_cal_view_notify_objects_removed_1 (view, cd->id);
	} else
		match_view_and_notify_component (view, cd->old_component, cd->new_component);

	return TRUE;
}

/**
 * e_cal_backend_notify_component_removed:
 * @backend: an #ECalBackend
 * @id: the Id of the removed object
 * @old_component: the removed component
 * @new_component: the component after the removal. This only applies to recurrent 
 * appointments that had an instance removed. In that case, this function
 * notifies a modification instead of a removal.
 *
 * Notifies each of the backend's listeners about a removed object.
 *
 * Like e_cal_backend_notify_object_removed() except takes an #ECalComponent
 * instead of an ical string representation and uses the #EDataCalView's
 * fields-of-interest to filter out unwanted information from ical strings
 * sent over the bus.
 *
 * Since: 3.4
 **/
void
e_cal_backend_notify_component_removed (ECalBackend *backend,
                                        const ECalComponentId *id,
                                        /* const */ ECalComponent *old_component,
                                        /* const */ ECalComponent *new_component)
{
	ECalBackendPrivate *priv;
	struct component_call_data cd;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_component_removed (priv->notification_proxy, id, old_component, new_component);
		return;
	}

	cd.old_component = old_component;
	cd.new_component = new_component;
	cd.id            = id;

	e_cal_backend_foreach_view (backend, component_removed_cb, &cd);
}

static gboolean
object_created_cb (EDataCalView *view,
                   gpointer data)
{
	const gchar *calobj = data;

	if (e_data_cal_view_object_matches (view, calobj))
		e_data_cal_view_notify_objects_added_1 (view, calobj);

	return TRUE;
}

/**
 * e_cal_backend_notify_object_created:
 * @backend: an #ECalBackend
 * @calobj: the newly created object
 *
 * Notifies each of the backend's listeners about a new object.
 *
 * e_data_cal_notify_object_created() calls this for you. You only need to
 * call e_cal_backend_notify_object_created() yourself to report objects
 * created by non-EDS clients.
 *
 * Deprecated: 3.4: Use e_cal_backend_notify_component_created() instead.
 **/
void
e_cal_backend_notify_object_created (ECalBackend *backend,
                                     const gchar *calobj)
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
 *
 * Deprecated: 3.4: Use e_data_cal_view_notify_objects_added() instead.
 **/
void
e_cal_backend_notify_objects_added (ECalBackend *backend,
                                    EDataCalView *view,
                                    const GSList *objects)
{
	e_data_cal_view_notify_objects_added (view, objects);
}

static void
match_view_and_notify_object (EDataCalView *view,
                              const gchar *old_object,
                              const gchar *object)
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

struct object_call_data {
	const gchar *old_object;
	const gchar *object;
	const ECalComponentId *id;
};

static gboolean
call_match_and_notify_object (EDataCalView *view,
                              gpointer user_data)
{
	struct object_call_data *cd = user_data;

	g_return_val_if_fail (user_data != NULL, FALSE);

	match_view_and_notify_object (view, cd->old_object, cd->object);

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
 * e_data_cal_notify_object_modified() calls this for you. You only need to
 * call e_cal_backend_notify_object_modified() yourself to report objects
 * modified by non-EDS clients.
 *
 * Deprecated: 3.4: Use e_cal_backend_notify_component_modified() instead.
 **/
void
e_cal_backend_notify_object_modified (ECalBackend *backend,
                                      const gchar *old_object,
                                      const gchar *object)
{
	ECalBackendPrivate *priv;
	struct object_call_data cd;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_object_modified (priv->notification_proxy, old_object, object);
		return;
	}

	cd.old_object = old_object;
	cd.object = object;
	cd.id = NULL;

	e_cal_backend_foreach_view (backend, call_match_and_notify_object, &cd);
}

/**
 * e_cal_backend_notify_objects_modified:
 *
 * Since: 2.24
 *
 * Deprecated: 3.4: Use e_data_cal_view_notify_objects_modified() instead.
 **/
void
e_cal_backend_notify_objects_modified (ECalBackend *backend,
                                       EDataCalView *view,
                                       const GSList *objects)
{
	e_data_cal_view_notify_objects_modified (view, objects);
}

static gboolean
object_removed_cb (EDataCalView *view,
                   gpointer user_data)
{
	struct object_call_data *cd = user_data;

	g_return_val_if_fail (user_data != NULL, FALSE);

	if (cd->object == NULL) {
		/* if object == NULL, it means the object has been completely
		 * removed from the backend */
		if (!cd->old_object || e_data_cal_view_object_matches (view, cd->old_object))
			e_data_cal_view_notify_objects_removed_1 (view, cd->id);
	} else
		match_view_and_notify_object (view, cd->old_object, cd->object);

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
 *
 * Deprecated: 3.4: Use e_cal_backend_notify_component_removed() instead.
 **/
void
e_cal_backend_notify_object_removed (ECalBackend *backend,
                                     const ECalComponentId *id,
                                     const gchar *old_object,
                                     const gchar *new_object)
{
	ECalBackendPrivate *priv;
	struct object_call_data cd;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_object_removed (priv->notification_proxy, id, old_object, new_object);
		return;
	}

	cd.old_object = old_object;
	cd.object = new_object;
	cd.id = id;

	e_cal_backend_foreach_view (backend, object_removed_cb, &cd);
}

/**
 * e_cal_backend_notify_objects_removed:
 *
 * Since: 2.24
 *
 * Deprecated: 3.4: Use e_data_cal_view_notify_objects_removed() instead.
 **/
void
e_cal_backend_notify_objects_removed (ECalBackend *backend,
                                      EDataCalView *view,
                                      const GSList *ids)
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
e_cal_backend_notify_error (ECalBackend *backend,
                            const gchar *message)
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
e_cal_backend_notify_readonly (ECalBackend *backend,
                               gboolean is_readonly)
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
 *
 * Since: 3.2
 **/
void
e_cal_backend_notify_online (ECalBackend *backend,
                             gboolean is_online)
{
	ECalBackendPrivate *priv;
	GSList *clients;

	priv = backend->priv;

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
 *
 * Since: 3.2
 **/
void
e_cal_backend_notify_opened (ECalBackend *backend,
                             GError *error)
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
 * e_cal_backend_notify_property_changed:
 * @backend: an #ECalBackend
 * @prop_name: property name, which changed
 * @prop_value: new property value
 *
 * Notifies client about property value change.
 *
 * Since: 3.2
 **/
void
e_cal_backend_notify_property_changed (ECalBackend *backend,
                                       const gchar *prop_name,
                                       const gchar *prop_value)
{
	ECalBackendPrivate *priv;
	GSList *clients;

	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (*prop_name != '\0');
	g_return_if_fail (prop_value != NULL);

	priv = backend->priv;
	g_mutex_lock (priv->clients_mutex);

	for (clients = priv->clients; clients != NULL; clients = g_slist_next (clients))
		e_data_cal_report_backend_property_changed (E_DATA_CAL (clients->data), prop_name, prop_value);

	g_mutex_unlock (priv->clients_mutex);
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
 * This function covers calling of both e_cal_backend_notify_opened() and
 * e_data_cal_respond_open() with the same @error.
 *
 * See e_cal_backend_open() for more details how the opening phase works.
 *
 * Since: 3.2
 **/
void
e_cal_backend_respond_opened (ECalBackend *backend,
                              EDataCal *cal,
                              guint32 opid,
                              GError *error)
{
	GError *copy = NULL;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (cal != NULL);
	g_return_if_fail (opid != 0);

	if (error)
		copy = g_error_copy (error);

	e_cal_backend_notify_opened (backend, copy);
	e_data_cal_respond_open (cal, opid, error);
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
e_cal_backend_empty_cache (ECalBackend *backend,
                           ECalBackendCache *cache)
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
		ECalComponentId *id;
		ECalComponent *comp = comps_in_cache->data;

		id = e_cal_component_get_id (comp);

		e_cal_backend_cache_remove_component (cache, id->uid, id->rid);

		e_cal_backend_notify_component_removed (backend, id, comp, NULL);

		e_cal_component_free_id (id);
		g_object_unref (comp);
	}

	g_list_free (comps_in_cache);

	e_file_cache_thaw_changes (E_FILE_CACHE (cache));
}

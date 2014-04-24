/*
 * e-data-factory.c
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
 */

/**
 * SECTION: e-data-factory
 * @include: libebackend/libebackend.h
 * @short_description: An abstract base class for a backend-based server
 **/

#include "e-data-factory.h"

#include <config.h>
#include <glib/gi18n-lib.h>

#include <libebackend/e-extensible.h>
#include <libebackend/e-backend-factory.h>
#include <libebackend/e-dbus-server.h>

#define E_DATA_FACTORY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_FACTORY, EDataFactoryPrivate))

struct _EDataFactoryPrivate {
	ESourceRegistry *registry;

	/* The mutex guards the 'backends' hash table.  The
	 * 'backend_factories' hash table doesn't really need
	 * guarding since it gets populated during construction
	 * and is read-only thereafter. */
	GMutex mutex;

	/* ESource UID -> GWeakRef (EBackend) */
	GHashTable *backends;

	/* Hash Key -> EBackendFactory */
	GHashTable *backend_factories;

	/* This is a hash table of client bus names to an array of
	 * EBackend references; one for every connection opened. */
	GHashTable *connections;
	GRecMutex connections_lock;

	/* This is a hash table of client bus names being watched.
	 * The value is the watcher ID for g_bus_unwatch_name(). */
	GHashTable *watched_names;
	GMutex watched_names_lock;
};

enum {
	BACKEND_CREATED,
	LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_REGISTRY
};

static guint signals[LAST_SIGNAL];

/* Forward Declarations */
static void	e_data_factory_initable_init	(GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (
	EDataFactory,
	e_data_factory,
	E_TYPE_DBUS_SERVER,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_data_factory_initable_init))

static GWeakRef *
data_factory_backends_lookup (EDataFactory *data_factory,
                              const gchar *uid)
{
	GHashTable *backends;
	GWeakRef *weak_ref;

	backends = data_factory->priv->backends;
	weak_ref = g_hash_table_lookup (backends, uid);

	if (weak_ref == NULL) {
		weak_ref = e_weak_ref_new (NULL);
		g_hash_table_insert (backends, g_strdup (uid), weak_ref);
	}

	return weak_ref;
}

static void
watched_names_value_free (gpointer value)
{
	g_bus_unwatch_name (GPOINTER_TO_UINT (value));
}

static void
data_factory_bus_acquired (EDBusServer *server,
			   GDBusConnection *connection)
{
	GDBusInterfaceSkeleton *skeleton_interface;
	EDataFactoryClass *class;
	GError *error = NULL;

	class = E_DATA_FACTORY_GET_CLASS (E_DATA_FACTORY (server));

	skeleton_interface = class->get_dbus_interface_skeleton (server);

	g_dbus_interface_skeleton_export (
		skeleton_interface,
		connection,
		class->factory_object_path,
		&error);

	if (error != NULL) {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		e_dbus_server_quit (server, E_DBUS_SERVER_EXIT_NORMAL);
		g_error_free (error);

		return;
	}

	/* Chain up to parent's bus_acquired() method. */
	E_DBUS_SERVER_CLASS (e_data_factory_parent_class)->
		bus_acquired (server, connection);
}

static void
data_factory_connections_add (EDataFactory *data_factory,
			      const gchar *name,
			      EBackend *backend)
{
	GHashTable *connections;
	GPtrArray *array;

	g_return_if_fail (name != NULL);
	g_return_if_fail (backend != NULL);

	g_rec_mutex_lock (&data_factory->priv->connections_lock);

	connections = data_factory->priv->connections;

	if (g_hash_table_size (connections) == 0)
		e_dbus_server_hold (E_DBUS_SERVER (data_factory));

	array = g_hash_table_lookup (connections, name);

	if (array == NULL) {
		array = g_ptr_array_new_with_free_func (
			(GDestroyNotify) g_object_unref);
		g_hash_table_insert (
			connections, g_strdup (name), array);
	}

	g_ptr_array_add (array, g_object_ref (backend));

	g_rec_mutex_unlock (&data_factory->priv->connections_lock);
}

static gboolean
data_factory_verify_backend_is_used (EDataFactory *data_factory,
				     const gchar *except_bus_name,
				     EBackend *backend)
{
	GHashTable *connections;
	GList *names, *l;
	GPtrArray *array;
	gboolean is_used = FALSE;

	g_return_val_if_fail (except_bus_name != NULL, TRUE);

	g_rec_mutex_lock (&data_factory->priv->connections_lock);

	connections = data_factory->priv->connections;
	names = g_hash_table_get_keys (connections);

	for (l = names; l != NULL && !is_used; l = g_list_next (l)) {
		const gchar *client_bus_name = l->data;
		gint ii;

		if (g_strcmp0 (client_bus_name, except_bus_name) == 0)
			continue;

		array = g_hash_table_lookup (connections, client_bus_name);
		for (ii = 0; ii < array->len; ii++) {
			EBackend *backend_in_use;

			backend_in_use = g_ptr_array_index (array, ii);

			if (backend_in_use == backend) {
				is_used = TRUE;
				break;
			}
		}
	}

	g_list_free (names);
	g_rec_mutex_unlock (&data_factory->priv->connections_lock);

	return is_used;
}

static gboolean
data_factory_connections_remove (EDataFactory *data_factory,
				 const gchar *name,
				 EBackend *backend)
{
	GHashTable *connections;
	GPtrArray *array;
	gboolean removed = FALSE;

	/* If backend is NULL, we remove all backends for name. */
	g_return_val_if_fail (name != NULL, FALSE);

	g_rec_mutex_lock (&data_factory->priv->connections_lock);

	connections = data_factory->priv->connections;
	array = g_hash_table_lookup (connections, name);

	if (array != NULL) {
		if (backend != NULL) {
			if (!data_factory_verify_backend_is_used (data_factory, name, backend))
				e_backend_prepare_shutdown (backend);

			removed = g_ptr_array_remove_fast (array, backend);
		} else if (array->len > 0) {
			gint ii;

			for (ii = 0; ii < array->len; ii++) {
				EBackend *backend;

				backend = g_ptr_array_index (array, ii);

				if (!data_factory_verify_backend_is_used (data_factory, name, backend))
					e_backend_prepare_shutdown (backend);
			}

			g_ptr_array_set_size (array, 0);
			removed = TRUE;
		}

		if (array->len == 0)
			g_hash_table_remove (connections, name);

		if (g_hash_table_size (connections) == 0)
			e_dbus_server_release (E_DBUS_SERVER (data_factory));
	}

	g_rec_mutex_unlock (&data_factory->priv->connections_lock);

	return removed;
}

static void
data_factory_connections_remove_all (EDataFactory *data_factory)
{
	GHashTable *connections;

	g_rec_mutex_lock (&data_factory->priv->connections_lock);

	connections = data_factory->priv->connections;

	if (g_hash_table_size (connections) > 0) {
		GSList *backends, *l;
		backends = e_data_factory_list_backends (data_factory);

		for (l = backends; l != NULL; l = g_slist_next (l)) {
			EBackend *backend = l->data;
			e_backend_prepare_shutdown (backend);
		}

		g_slist_free_full (backends, g_object_unref);

		g_hash_table_remove_all (connections);
		e_dbus_server_release (E_DBUS_SERVER (data_factory));
	}

	g_rec_mutex_unlock (&data_factory->priv->connections_lock);
}

static void
data_factory_closed_cb (EBackend *backend,
                        const gchar *sender,
                        EDataFactory *data_factory)
{
       data_factory_connections_remove (data_factory, sender, backend);
}

static void
data_factory_name_vanished_cb (GDBusConnection *connection,
			       const gchar *name,
			       gpointer user_data)
{
	GWeakRef *weak_ref = user_data;
	EDataFactory *data_factory;

	data_factory = g_weak_ref_get (weak_ref);

	if (data_factory != NULL) {
		data_factory_connections_remove (data_factory, name, NULL);

		/* Unwatching the bus name from here will corrupt the
		 * 'name' argument, and possibly also the 'user_data'.
		 *
		 * This is a GDBus bug.  Work around it by unwatching
		 * the bus name last.
		 *
		 * See: https://bugzilla.gnome.org/706088
		 */
		g_mutex_lock (&data_factory->priv->watched_names_lock);
		g_hash_table_remove (data_factory->priv->watched_names, name);
		g_mutex_unlock (&data_factory->priv->watched_names_lock);

		g_object_unref (data_factory);
	}
}

static void
data_factory_watched_names_add (EDataFactory *data_factory,
				GDBusConnection *connection,
				const gchar *name)
{
	GHashTable *watched_names;

	g_return_if_fail (name != NULL);

	g_mutex_lock (&data_factory->priv->watched_names_lock);

	watched_names = data_factory->priv->watched_names;

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
			data_factory_name_vanished_cb,
			e_weak_ref_new (data_factory),
			(GDestroyNotify) e_weak_ref_free);

		g_hash_table_insert (
			watched_names, g_strdup (name),
			GUINT_TO_POINTER (watcher_id));
	}

	g_mutex_unlock (&data_factory->priv->watched_names_lock);
}

static void
data_factory_bus_name_lost (EDBusServer *server,
			    GDBusConnection *connection)
{
	EDataFactory *data_factory;

	data_factory = E_DATA_FACTORY (server);

	data_factory_connections_remove_all (data_factory);

	/* Chain up to parent's bus_name_lost() method. */
	E_DBUS_SERVER_CLASS (e_data_factory_parent_class)->
		bus_name_lost (server, connection);
}

static void
data_factory_quit_server (EDBusServer *server,
			  EDBusServerExitCode exit_code)
{
	GDBusInterfaceSkeleton *skeleton_interface;
	EDataFactoryClass *class;

	class = E_DATA_FACTORY_GET_CLASS (E_DATA_FACTORY (server));

	skeleton_interface = class->get_dbus_interface_skeleton (server);
	g_dbus_interface_skeleton_unexport (skeleton_interface);

	/* This factory does not support reloading, so stop the signal
	 * emission and return without chaining up to prevent quitting. */
	if (exit_code == E_DBUS_SERVER_EXIT_RELOAD) {
		g_signal_stop_emission_by_name (server, "quit-server");
		return;
	}

	/* Chain up to parent's quit_server() method. */
	E_DBUS_SERVER_CLASS (e_data_factory_parent_class)->
		quit_server (server, exit_code);
}

static void
e_data_factory_get_property (GObject *object,
			     guint property_id,
			     GValue *value,
			     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_REGISTRY:
			g_value_set_object (
				value,
				e_data_factory_get_registry (
				E_DATA_FACTORY (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_factory_dispose (GObject *object)
{
	EDataFactory *data_factory;
	EDataFactoryPrivate *priv;

	data_factory = E_DATA_FACTORY (object);
	priv = data_factory->priv;

	g_hash_table_remove_all (priv->backends);
	g_hash_table_remove_all (priv->backend_factories);

	g_clear_object (&priv->registry);

	g_hash_table_remove_all (priv->connections);
	g_hash_table_remove_all (priv->watched_names);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_data_factory_parent_class)->dispose (object);
}

static void
data_factory_finalize (GObject *object)
{
	EDataFactory *data_factory;
	EDataFactoryPrivate *priv;

	data_factory = E_DATA_FACTORY (object);
	priv = data_factory->priv;

	g_mutex_clear (&priv->mutex);

	g_hash_table_destroy (priv->backends);
	g_hash_table_destroy (priv->backend_factories);

	g_hash_table_destroy (priv->connections);
	g_rec_mutex_clear (&priv->connections_lock);

	g_hash_table_destroy (priv->watched_names);
	g_mutex_clear (&priv->watched_names_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_factory_parent_class)->finalize (object);
}

static void
data_factory_constructed (GObject *object)
{
	EDataFactoryClass *class;
	EDataFactory *data_factory;
	GList *list, *link;

	data_factory = E_DATA_FACTORY (object);
	class = E_DATA_FACTORY_GET_CLASS (data_factory);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_data_factory_parent_class)->constructed (object);

	/* Collect all backend factories into a hash table. */

	list = e_extensible_list_extensions (
		E_EXTENSIBLE (object), class->backend_factory_type);

	for (link = list; link != NULL; link = g_list_next (link)) {
		EBackendFactory *backend_factory;
		const gchar *hash_key;

		backend_factory = E_BACKEND_FACTORY (link->data);
		hash_key = e_backend_factory_get_hash_key (backend_factory);

		if (hash_key != NULL) {
			g_hash_table_insert (
				data_factory->priv->backend_factories,
				g_strdup (hash_key),
				g_object_ref (backend_factory));
			g_debug (
				"Registering %s ('%s')\n",
				G_OBJECT_TYPE_NAME (backend_factory),
				hash_key);
		}
	}

	g_list_free (list);
}

static gboolean
data_factory_initable_init (GInitable *initable,
			    GCancellable *cancellable,
			    GError **error)
{
	EDataFactory *data_factory;

	data_factory = E_DATA_FACTORY (initable);

	data_factory->priv->registry = e_source_registry_new_sync (
		cancellable, error);

	return (data_factory->priv->registry != NULL);
}

static void
e_data_factory_initable_init (GInitableIface *iface)
{
	iface->init = data_factory_initable_init;
}

static void
e_data_factory_class_init (EDataFactoryClass *class)
{
	GObjectClass *object_class;
	EDBusServerClass *dbus_server_class;

	g_type_class_add_private (class, sizeof (EDataFactoryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->get_property = e_data_factory_get_property;
	object_class->dispose = data_factory_dispose;
	object_class->finalize = data_factory_finalize;
	object_class->constructed = data_factory_constructed;

	class->backend_factory_type = E_TYPE_BACKEND_FACTORY;

	dbus_server_class = E_DBUS_SERVER_CLASS (class);
	dbus_server_class->bus_acquired = data_factory_bus_acquired;
	dbus_server_class->bus_name_lost = data_factory_bus_name_lost;
	dbus_server_class->quit_server = data_factory_quit_server;

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

	/**
	 * EDataFactory::backend-created:
	 * @data_factory: the #EDataFactory which emitted the signal
	 * @backend: the newly-created #EBackend
	 *
	 * Emitted when a new #EBackend is instantiated by way of
	 * e_data_factory_ref_backend().  Extensions can connect to this
	 * signal to perform additional initialization on the #EBackend.
	 *
	 * Since: 3.8
	 **/
	signals[BACKEND_CREATED] = g_signal_new (
		"backend-created",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EDataFactoryClass, backend_created),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		E_TYPE_BACKEND);
}

static void
e_data_factory_init (EDataFactory *data_factory)
{
	data_factory->priv = E_DATA_FACTORY_GET_PRIVATE (data_factory);

	g_mutex_init (&data_factory->priv->mutex);
	g_rec_mutex_init (&data_factory->priv->connections_lock);
	g_mutex_init (&data_factory->priv->watched_names_lock);

	data_factory->priv->backends = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) e_weak_ref_free);

	data_factory->priv->backend_factories = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);

	data_factory->priv->connections = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_ptr_array_unref);

	data_factory->priv->watched_names = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) watched_names_value_free);
}

/**
 * e_data_factory_ref_backend:
 * @data_factory: an #EDataFactory
 * @hash_key: hash key for an #EBackendFactory
 * @source: an #ESource
 * @error: return location for a #GError, or %NULL
 *
 * Returns either a newly-created or existing #EBackend for #ESource.
 * The returned #EBackend is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * The @data_factory retains a weak reference to @backend so it can return
 * the same instance while @backend is in use.  When the last strong reference
 * to @backend is dropped, @data_factory will lose its weak reference and will
 * have to create a new #EBackend instance the next time the same @hash_key
 * and @source are requested.
 *
 * If no suitable #EBackendFactory exists, the function returns %NULL.
 *
 * Returns: an #EBackend for @source, or %NULL
 *
 * Since: 3.6
 **/
EBackend *
e_data_factory_ref_backend (EDataFactory *data_factory,
                            const gchar *hash_key,
                            ESource *source)
{
	return e_data_factory_ref_initable_backend (
		data_factory, hash_key, source, NULL, NULL);
}

/**
 * e_data_factory_ref_initable_backend:
 * @data_factory: an #EDataFactory
 * @hash_key: hash key for an #EBackendFactory
 * @source: an #ESource
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Similar to e_data_factory_ref_backend(), but allows for backends that
 * implement the #GInitable interface so they can fail gracefully if they
 * are unable to initialize critical resources, such as a cache database.
 *
 * Returns either a newly-created or existing #EBackend for #ESource.
 * The returned #EBackend is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * If the newly-created backend implements the #GInitable interface, then
 * g_initable_init() is also called on it using @cancellable and @error.
 *
 * The @data_factory retains a weak reference to @backend so it can return
 * the same instance while @backend is in use.  When the last strong reference
 * to @backend is dropped, @data_factory will lose its weak reference and will
 * have to create a new #EBackend instance the next time the same @hash_key
 * and @source are requested.
 *
 * If no suitable #EBackendFactory exists, or if the #EBackend fails to
 * initialize, the function sets @error and returns %NULL.
 *
 * Returns: an #EBackend for @source, or %NULL
 *
 * Since: 3.8
 **/
EBackend *
e_data_factory_ref_initable_backend (EDataFactory *data_factory,
                                     const gchar *hash_key,
                                     ESource *source,
                                     GCancellable *cancellable,
                                     GError **error)
{
	EBackendFactory *backend_factory;
	GWeakRef *weak_ref;
	EBackend *backend;
	const gchar *uid;

	g_return_val_if_fail (E_IS_DATA_FACTORY (data_factory), NULL);
	g_return_val_if_fail (hash_key != NULL, NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	uid = e_source_get_uid (source);
	g_return_val_if_fail (uid != NULL, NULL);

	g_mutex_lock (&data_factory->priv->mutex);

	/* The weak ref is already inserted in the hash table. */
	weak_ref = data_factory_backends_lookup (data_factory, uid);

	/* Check if we already have a backend for the given source. */
	backend = g_weak_ref_get (weak_ref);

	if (backend != NULL)
		goto exit;

	/* Find a suitable backend factory using the hash key. */
	backend_factory =
		e_data_factory_ref_backend_factory (data_factory, hash_key);

	if (backend_factory == NULL) {
		g_set_error (
			error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("No backend factory for hash key '%s'"),
			hash_key);
		goto exit;
	}

	/* Create a new backend for the given source and store it. */
	backend = e_backend_factory_new_backend (backend_factory, source);

	if (G_IS_INITABLE (backend)) {
		GInitable *initable = G_INITABLE (backend);

		if (!g_initable_init (initable, cancellable, error)) {
			g_object_unref (backend);
			backend = NULL;
		}
	}

	/* This still does the right thing if backend is NULL. */
	g_weak_ref_set (weak_ref, backend);

	g_object_unref (backend_factory);

	if (backend != NULL)
		g_signal_emit (
			data_factory, signals[BACKEND_CREATED], 0, backend);

exit:
	g_mutex_unlock (&data_factory->priv->mutex);

	return backend;
}

/**
 * e_data_factory_ref_backend_factory:
 * @data_factory: an #EDataFactory
 * @hash_key: hash key for an #EBackendFactory
 *
 * Returns the #EBackendFactory for @hash_key, or %NULL if no such factory
 * is registered.
 *
 * The returned #EBackendFactory is referenced for thread-safety.
 * Unreference the #EBackendFactory with g_object_unref() when finished
 * with it.
 *
 * Returns: the #EBackendFactory for @hash_key, or %NULL
 *
 * Since: 3.6
 **/
EBackendFactory *
e_data_factory_ref_backend_factory (EDataFactory *data_factory,
                                    const gchar *hash_key)
{
	GHashTable *backend_factories;
	EBackendFactory *backend_factory;

	g_return_val_if_fail (E_IS_DATA_FACTORY (data_factory), NULL);
	g_return_val_if_fail (hash_key != NULL, NULL);

	/* It should be safe to lookup backend factories without a mutex
	 * because once initially populated the hash table remains fixed.
	 *
	 * XXX Which might imply the returned factory doesn't *really* need
	 *     to be referenced for thread-safety, but better to do it when
	 *     not really needed than wish we had in the future. */

	backend_factories = data_factory->priv->backend_factories;
	backend_factory = g_hash_table_lookup (backend_factories, hash_key);

	if (backend_factory != NULL)
		g_object_ref (backend_factory);

	return backend_factory;
}

static void
data_factory_toggle_notify_cb (gpointer data,
			       GObject *backend,
			       gboolean is_last_ref)
{
	if (is_last_ref) {
		/* Take a strong reference before removing the
		 * toggle reference, to keep the backend alive. */
		g_object_ref (backend);

		g_object_remove_toggle_ref (
			backend, data_factory_toggle_notify_cb, data);

		g_signal_emit_by_name (backend, "shutdown");

		g_object_unref (backend);
	}
}

/**
 * e_data_factory_get_registry:
 * @data_factory: an #EDataFactory
 * @backend: an #EBackend
 *
 * Install a toggle reference on the backend, that can receive a signal to
 * shutdown once all client connections are closed.
 *
 * Since: 3.14
 **/
void
e_data_factory_set_backend_callbacks (EDataFactory *data_factory,
				      EBackend *backend)
{
	g_return_if_fail (E_IS_DATA_FACTORY (data_factory));
	g_return_if_fail (data_factory != NULL);
	g_return_if_fail (backend != NULL);

	/* Install a toggle reference on the backend
	 * so we can signal it to shut down once all
	 * client connections are closed.
	 */
	g_object_add_toggle_ref (
		G_OBJECT (backend),
		data_factory_toggle_notify_cb,
		NULL);

	g_signal_connect_object (
		backend, "closed",
		G_CALLBACK (data_factory_closed_cb),
		data_factory, 0);
}

/**
 * e_data_factory_get_registry:
 * @data_factory: an #EDataFactory
 *
 * Returns the #ESourceRegistry owned by @data_factory.
 *
 * Returns: the #ESourceRegistry
 *
 * Since: 3.14
 **/
ESourceRegistry *
e_data_factory_get_registry (EDataFactory *data_factory)
{
	g_return_val_if_fail (E_IS_DATA_FACTORY (data_factory), NULL);

	return data_factory->priv->registry;
}

/**
 * e_data_factory_list_backends:
 * @data_factory: an #EDataFactory
 *
 * Returns a list of backends connected to the @data_factory
 *
 * Returns: a #GSList of backends connected to the @data_factory.
 *          The list should be freed using
 *          g_slist_free_full (backends, g_object_unref)
 *
 * Since: 3.14
 **/
GSList *
e_data_factory_list_backends (EDataFactory *data_factory)
{
	GSList *backends = NULL;
	GHashTable *connections;
	GHashTable *backends_hash;
	GHashTableIter iter;
	gpointer key, value;

	g_return_val_if_fail (E_IS_DATA_FACTORY (data_factory), NULL);

	g_rec_mutex_lock (&data_factory->priv->connections_lock);

	connections = data_factory->priv->connections;
	backends_hash = g_hash_table_new (g_direct_hash, g_direct_equal);

	g_hash_table_iter_init (&iter, connections);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		GPtrArray *array = value;
		gint ii;

		for (ii = 0; ii < array->len; ii++) {
			EBackend *backend = g_ptr_array_index (array, ii);
			if (!g_hash_table_contains (backends_hash, backend)) {
				g_hash_table_insert (backends_hash, backend, GINT_TO_POINTER (1));
				backends = g_slist_prepend (backends, g_object_ref (backend));
			}
		}
	}

	g_hash_table_destroy (backends_hash);
	backends = g_slist_reverse (backends);
	g_rec_mutex_unlock (&data_factory->priv->connections_lock);

	return backends;
}

static EBackend *
data_factory_ref_backend (EDataFactory *data_factory,
			  ESource *source,
			  const gchar *extension_name,
			  GError **error)
{
	EBackend *backend;
	ESourceBackend *extension;
	gchar *backend_name;
	gchar *hash_key = NULL;

	g_return_val_if_fail (E_IS_DATA_FACTORY (data_factory), NULL);

	extension = e_source_get_extension (source, extension_name);
	backend_name = e_source_backend_dup_backend_name (extension);

	if (backend_name == NULL || *backend_name == '\0') {
		g_set_error (
			error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("No backend name in source '%s'"),
			e_source_get_display_name (source));
		g_free (backend_name);
		return NULL;
	}

	hash_key = g_strdup_printf ("%s:%s", backend_name, extension_name);

	backend = e_data_factory_ref_initable_backend (
		data_factory,
		hash_key,
		source,
		NULL,
		error);

	g_free (hash_key);
	g_free (backend_name);

	return backend;
}

/**
 * e_data_factory_construct_path:
 * @data_factory: an #EDataFactory
 *
 * Returns a new and unique object path for a D-Bus interface based
 * in the data object path prefix of the @data_factory
 *
 * Returns: a newly allocated string, representing the object path for
 *          the D-Bus interface.
 *
 * Since: 3.14
 **/
gchar *
e_data_factory_construct_path (EDataFactory *data_factory)
{
	EDataFactoryClass *class;
	static volatile gint counter = 1;

	g_return_val_if_fail (E_IS_DATA_FACTORY (data_factory), NULL);

	g_atomic_int_inc (&counter);

	class = E_DATA_FACTORY_GET_CLASS (data_factory);
	g_return_val_if_fail (class->data_object_path_prefix != NULL, NULL);

	return g_strdup_printf (
		"%s/%d/%u",
		class->data_object_path_prefix, getpid (), counter);
}

/**
 * e_data_factory_open_backend:
 * @data_factory: an #EDataFactory
 * @connection: a #GDBusConnection
 * @sender: a string
 * @uid: UID of an #ESource to open
 * @extension_name: an extension name
 * @error: return location for a #GError, or %NULL
 *
 * Returns the #EBackend data D-Bus object path
 *
 * Returns: a newly allocated string that represents the #EBackend
 *          data D-Bus object path.
 *
 * Since: 3.14
 **/
gchar *
e_data_factory_open_backend (EDataFactory *data_factory,
			     GDBusConnection *connection,
			     const gchar *sender,
			     const gchar *uid,
			     const gchar *extension_name,
			     GError **error)
{
	EDataFactoryClass *class;
	EBackend *backend;
	ESourceRegistry *registry;
	ESource *source;
	gchar *object_path;

	g_return_val_if_fail (E_IS_DATA_FACTORY (data_factory), NULL);

	if (uid == NULL || *uid == '\0') {
		g_set_error (
			error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Missing source UID"));
		return NULL;
	}

	registry = e_data_factory_get_registry (data_factory);
	source = e_source_registry_ref_source (registry, uid);

	if (source == NULL) {
		g_set_error (
			error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("No such source for UID '%s'"), uid);
		return NULL;
	}

	backend = data_factory_ref_backend (
		data_factory, source, extension_name, error);

	g_object_unref (source);

	if (backend == NULL)
		return NULL;

	class = E_DATA_FACTORY_GET_CLASS (data_factory);
	object_path = class->data_open (
		data_factory, backend, connection, error);

	if (object_path != NULL) {
		/* Watch the sender's bus name so we can clean
		 * up its connections if the bus name vanishes.
		 */
		data_factory_watched_names_add (
			data_factory, connection, sender);

		/* A client my create multiple Eclient instances for the
		 * same ESource, each of which calls close() individually.
		 * So we must track each and every connection made.
		 */
		data_factory_connections_add (
			data_factory, sender, backend);
	}

	g_clear_object (&backend);

	return object_path;
}

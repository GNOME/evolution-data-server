/*
 * e-data-factory.c
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
 * SECTION: e-data-factory
 * @short_description: an abstract base class for a D-Bus server
 * @include: libebackend/e-data-factory
 **/

#include "e-data-factory.h"

#include <config.h>

#include <libebackend/e-extensible.h>
#include <libebackend/e-backend-factory.h>

#define E_DATA_FACTORY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_FACTORY, EDataFactoryPrivate))

struct _EDataFactoryPrivate {
	/* The mutex guards the 'backends' hash table.  The
	 * 'backend_factories' hash table doesn't really need
	 * guarding since it gets populated during construction
	 * and is read-only thereafter. */
	GMutex *mutex;

	/* ESource UID -> EBackend */
	GHashTable *backends;

	/* Hash Key -> EBackendFactory */
	GHashTable *backend_factories;
};

G_DEFINE_ABSTRACT_TYPE (
	EDataFactory, e_data_factory, E_TYPE_DBUS_SERVER)

static void
data_factory_last_client_gone_cb (EBackend *backend,
                                  EDataFactory *factory)
{
	ESource *source;
	const gchar *uid;

	source = e_backend_get_source (backend);
	uid = e_source_peek_uid (source);
	g_return_if_fail (uid != NULL);

	g_mutex_lock (factory->priv->mutex);
	g_hash_table_remove (factory->priv->backends, uid);
	g_mutex_unlock (factory->priv->mutex);
}

static void
data_factory_dispose (GObject *object)
{
	EDataFactoryPrivate *priv;

	priv = E_DATA_FACTORY_GET_PRIVATE (object);

	g_hash_table_remove_all (priv->backends);
	g_hash_table_remove_all (priv->backend_factories);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_data_factory_parent_class)->dispose (object);
}

static void
data_factory_finalize (GObject *object)
{
	EDataFactoryPrivate *priv;

	priv = E_DATA_FACTORY_GET_PRIVATE (object);

	g_mutex_free (priv->mutex);

	g_hash_table_destroy (priv->backends);
	g_hash_table_destroy (priv->backend_factories);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_factory_parent_class)->finalize (object);
}

static void
data_factory_constructed (GObject *object)
{
	EDataFactoryClass *class;
	EDataFactoryPrivate *priv;
	GList *list, *link;

	class = E_DATA_FACTORY_GET_CLASS (object);
	priv = E_DATA_FACTORY_GET_PRIVATE (object);

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
				priv->backend_factories,
				g_strdup (hash_key),
				g_object_ref (backend_factory));
			g_print (
				"Registering %s ('%s')\n",
				G_OBJECT_TYPE_NAME (backend_factory),
				hash_key);
		}
	}

	g_list_free (list);
}

static void
data_factory_quit_server (EDBusServer *server,
                          EDBusServerExitCode exit_code)
{
	/* EDataFactory does not support reloading, so stop the signal
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
e_data_factory_class_init (EDataFactoryClass *class)
{
	GObjectClass *object_class;
	EDBusServerClass *dbus_server_class;

	g_type_class_add_private (class, sizeof (EDataFactoryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = data_factory_dispose;
	object_class->finalize = data_factory_finalize;
	object_class->constructed = data_factory_constructed;

	dbus_server_class = E_DBUS_SERVER_CLASS (class);
	dbus_server_class->quit_server = data_factory_quit_server;

	class->backend_factory_type = E_TYPE_BACKEND_FACTORY;
}

static void
e_data_factory_init (EDataFactory *factory)
{
	factory->priv = E_DATA_FACTORY_GET_PRIVATE (factory);

	factory->priv->mutex = g_mutex_new ();

	factory->priv->backends = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);

	factory->priv->backend_factories = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);
}

EBackend *
e_data_factory_get_backend (EDataFactory *factory,
                            const gchar *hash_key,
                            ESource *source)
{
	EBackendFactory *backend_factory;
	EBackend *backend;
	const gchar *uid;

	g_return_val_if_fail (E_IS_DATA_FACTORY (factory), NULL);
	g_return_val_if_fail (hash_key != NULL, NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	uid = e_source_peek_uid (source);
	g_return_val_if_fail (uid != NULL, NULL);

	g_mutex_lock (factory->priv->mutex);

	/* Check if we already have a backend for the given source. */
	backend = g_hash_table_lookup (factory->priv->backends, uid);

	if (backend != NULL)
		goto exit;

	/* Find a suitable backend factory using the hash key. */
	backend_factory = g_hash_table_lookup (
		factory->priv->backend_factories, hash_key);

	if (backend_factory == NULL)
		goto exit;

	/* Create a new backend for the given source and store it. */
	backend = e_backend_factory_new_backend (backend_factory, source);

	if (backend == NULL)
		goto exit;

	g_signal_connect (
		backend, "last-client-gone",
		G_CALLBACK (data_factory_last_client_gone_cb), factory);

	g_hash_table_insert (
		factory->priv->backends,
		g_strdup (uid), backend);

exit:
	g_mutex_unlock (factory->priv->mutex);

	return backend;
}


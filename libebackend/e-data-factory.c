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
 * @include: libebackend/libebackend.h
 * @short_description: An abstract base class for a backend-based server
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

	/* ESource UID -> GWeakRef (EBackend) */
	GHashTable *backends;

	/* Hash Key -> EBackendFactory */
	GHashTable *backend_factories;
};

G_DEFINE_ABSTRACT_TYPE (
	EDataFactory, e_data_factory, E_TYPE_DBUS_SERVER)

static GWeakRef *
data_factory_weak_ref_new (void)
{
	/* A GWeakRef allocated in zero-filled memory is sufficiently
	 * initialized without calling g_weak_ref_init(weak_ref, NULL). */
	return g_slice_new0 (GWeakRef);
}

static void
data_factory_weak_ref_free (GWeakRef *weak_ref)
{
	g_weak_ref_clear (weak_ref);
	g_slice_free (GWeakRef, weak_ref);
}

static GWeakRef *
data_factory_backends_lookup (EDataFactory *data_factory,
                              const gchar *uid)
{
	GHashTable *backends;
	GWeakRef *weak_ref;

	backends = data_factory->priv->backends;
	weak_ref = g_hash_table_lookup (backends, uid);

	if (weak_ref == NULL) {
		weak_ref = data_factory_weak_ref_new ();
		g_hash_table_insert (backends, g_strdup (uid), weak_ref);
	}

	return weak_ref;
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
e_data_factory_class_init (EDataFactoryClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EDataFactoryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = data_factory_dispose;
	object_class->finalize = data_factory_finalize;
	object_class->constructed = data_factory_constructed;

	class->backend_factory_type = E_TYPE_BACKEND_FACTORY;
}

static void
e_data_factory_init (EDataFactory *data_factory)
{
	data_factory->priv = E_DATA_FACTORY_GET_PRIVATE (data_factory);

	data_factory->priv->mutex = g_mutex_new ();

	data_factory->priv->backends = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) data_factory_weak_ref_free);

	data_factory->priv->backend_factories = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);
}

/**
 * e_data_factory_ref_backend:
 * @data_factory: an #EDataFactory
 * @hash_key: hash key for an #EBackendFactory
 * @source: an #ESource
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
	EBackendFactory *backend_factory;
	GWeakRef *weak_ref;
	EBackend *backend;
	const gchar *uid;

	g_return_val_if_fail (E_IS_DATA_FACTORY (data_factory), NULL);
	g_return_val_if_fail (hash_key != NULL, NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	uid = e_source_get_uid (source);
	g_return_val_if_fail (uid != NULL, NULL);

	g_mutex_lock (data_factory->priv->mutex);

	/* The weak ref is already inserted in the hash table. */
	weak_ref = data_factory_backends_lookup (data_factory, uid);

	/* Check if we already have a backend for the given source. */
	backend = g_weak_ref_get (weak_ref);

	if (backend != NULL)
		goto exit;

	/* Find a suitable backend factory using the hash key. */
	backend_factory =
		e_data_factory_ref_backend_factory (data_factory, hash_key);

	if (backend_factory == NULL)
		goto exit;

	/* Create a new backend for the given source and store it. */
	backend = e_backend_factory_new_backend (backend_factory, source);

	/* This still does the right thing if backend is NULL. */
	g_weak_ref_set (weak_ref, backend);

	g_object_unref (backend_factory);

exit:
	g_mutex_unlock (data_factory->priv->mutex);

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


/*
 * module-owncloud-backend.c
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libebackend/libebackend.h>

#include "owncloud-utils.h"

/* Standard GObject macros */
#define E_TYPE_OWNCLOUD_BACKEND \
	(e_owncloud_backend_get_type ())
#define E_OWNCLOUD_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_OWNCLOUD_BACKEND, EOwncloudBackend))

typedef struct _EOwncloudBackend EOwncloudBackend;
typedef struct _EOwncloudBackendClass EOwncloudBackendClass;

typedef struct _EOwncloudBackendFactory EOwncloudBackendFactory;
typedef struct _EOwncloudBackendFactoryClass EOwncloudBackendFactoryClass;

struct _EOwncloudBackend {
	ECollectionBackend parent;
};

struct _EOwncloudBackendClass {
	ECollectionBackendClass parent_class;
};

struct _EOwncloudBackendFactory {
	ECollectionBackendFactory parent;
};

struct _EOwncloudBackendFactoryClass {
	ECollectionBackendFactoryClass parent_class;
};

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_owncloud_backend_get_type (void);
GType e_owncloud_backend_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	EOwncloudBackend,
	e_owncloud_backend,
	E_TYPE_COLLECTION_BACKEND)

G_DEFINE_DYNAMIC_TYPE (
	EOwncloudBackendFactory,
	e_owncloud_backend_factory,
	E_TYPE_COLLECTION_BACKEND_FACTORY)

static void
owncloud_remove_unknown_sources_cb (gpointer resource_id,
                                    gpointer uid,
                                    gpointer user_data)
{
	ESourceRegistryServer *server = user_data;
	ESource *source;

	source = e_source_registry_server_ref_source (server, uid);

	if (source) {
		e_source_registry_server_remove_source (server, source);
		g_object_unref (source);
	}
}

static void
owncloud_source_found_cb (ECollectionBackend *collection,
                          OwnCloudSourceType source_type,
                          SoupURI *uri,
                          const gchar *display_name,
                          const gchar *color,
                          gpointer user_data)
{
	GHashTable *known_sources = user_data;
	ESourceRegistryServer *server;
	ESourceBackend *backend;
	ESource *source = NULL;
	const gchar *backend_name = NULL;
	const gchar *provider = NULL;
	const gchar *identity_prefix = NULL;
	const gchar *source_uid;
	gboolean is_new;
	gchar *url;
	gchar *identity;

	g_return_if_fail (collection != NULL);
	g_return_if_fail (uri != NULL);
	g_return_if_fail (display_name != NULL);
	g_return_if_fail (known_sources != NULL);

	switch (source_type) {
	case OwnCloud_Source_Contacts:
		backend_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
		provider = "webdav";
		identity_prefix = "contacts";
		break;
	case OwnCloud_Source_Events:
		backend_name = E_SOURCE_EXTENSION_CALENDAR;
		provider = "caldav";
		identity_prefix = "events";
		break;
	case OwnCloud_Source_Memos:
		backend_name = E_SOURCE_EXTENSION_MEMO_LIST;
		provider = "caldav";
		identity_prefix = "memos";
		break;
	case OwnCloud_Source_Tasks:
		backend_name = E_SOURCE_EXTENSION_TASK_LIST;
		provider = "caldav";
		identity_prefix = "tasks";
		break;
	}

	g_return_if_fail (backend_name != NULL);

	server = e_collection_backend_ref_server (collection);

	url = soup_uri_to_string (uri, FALSE);
	identity = g_strconcat (identity_prefix, "::", url, NULL);
	source_uid = g_hash_table_lookup (known_sources, identity);
	is_new = !source_uid;
	if (is_new) {
		ESource *master_source;

		source = e_collection_backend_new_child (collection, identity);
		g_warn_if_fail (source != NULL);

		if (source) {
			ESourceResource *resource;
			ESourceWebdav *master_webdav, *child_webdav;

			master_source = e_backend_get_source (E_BACKEND (collection));
			master_webdav = e_source_get_extension (master_source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
			child_webdav = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
			resource = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);

			e_source_webdav_set_soup_uri (child_webdav, uri);
			e_source_resource_set_identity (resource, identity);

			/* inherit ssl trust options */
			e_source_webdav_set_ssl_trust (child_webdav, e_source_webdav_get_ssl_trust (master_webdav));
		}
	} else {
		source = e_source_registry_server_ref_source (server, source_uid);
		g_warn_if_fail (source != NULL);

		g_hash_table_remove (known_sources, identity);
	}

	g_free (identity);
	g_free (url);

	/* these properties are synchronized always */
	if (source) {
		gint rr, gg, bb;

		backend = e_source_get_extension (source, backend_name);
		e_source_backend_set_backend_name (backend, provider);

		e_source_set_display_name (source, display_name);
		/* Also check whether the color format is as expected; it cannot
		   be used gdk_rgba_parse here, because it required gdk/gtk. */
		if (source_type != OwnCloud_Source_Contacts && color &&
		    sscanf (color, "#%02x%02x%02x", &rr, &gg, &bb) == 3)
			e_source_selectable_set_color (E_SOURCE_SELECTABLE (backend), color);

		if (is_new)
			e_source_registry_server_add_source (server, source);

		g_object_unref (source);
	}

	g_object_unref (server);
}

static void
owncloud_add_uid_to_hashtable (gpointer source,
                               gpointer known_sources)
{
	ESourceResource *resource;
	gchar *uid, *rid;

	if (!e_source_has_extension (source, E_SOURCE_EXTENSION_RESOURCE))
		return;

	resource = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);

	uid = e_source_dup_uid (source);
	if (!uid || !*uid) {
		g_free (uid);
		return;
	}

	rid = e_source_resource_dup_identity (resource);
	if (!rid || !*rid) {
		g_free (rid);
		g_free (uid);
		return;
	}

	g_hash_table_insert (known_sources, rid, uid);
}

static gpointer
owncloud_populate_thread (gpointer data)
{
	ECollectionBackend *collection = data;
	GHashTable *known_sources;
	GList *sources;

	g_return_val_if_fail (collection != NULL, NULL);

	/* resource-id => source's UID */
	known_sources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	sources = e_collection_backend_list_calendar_sources (collection);
	g_list_foreach (sources, owncloud_add_uid_to_hashtable, known_sources);
	g_list_free_full (sources, g_object_unref);

	sources = e_collection_backend_list_contacts_sources (collection);
	g_list_foreach (sources, owncloud_add_uid_to_hashtable, known_sources);
	g_list_free_full (sources, g_object_unref);

	if (owncloud_utils_search_server (collection, owncloud_source_found_cb, known_sources)) {
		ESourceRegistryServer *server;

		server = e_collection_backend_ref_server (collection);

		g_hash_table_foreach (known_sources, owncloud_remove_unknown_sources_cb, server);

		g_object_unref (server);
	}

	g_hash_table_destroy (known_sources);
	g_object_unref (collection);

	return NULL;
}

static void
owncloud_backend_populate (ECollectionBackend *collection)
{
	GList *list, *liter;
	ESourceRegistryServer *server;
	GThread *thread;

	/* Chain up to parent's populate() method. */
	E_COLLECTION_BACKEND_CLASS (e_owncloud_backend_parent_class)->populate (collection);

	server = e_collection_backend_ref_server (collection);
	list = e_collection_backend_claim_all_resources (collection);

	for (liter = list; liter; liter = g_list_next (liter)) {
		ESource *source = liter->data;

		if (e_source_has_extension (source, E_SOURCE_EXTENSION_RESOURCE)) {
			ESourceResource *resource;
			ESource *child;

			resource = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);
			child = e_collection_backend_new_child (collection, e_source_resource_get_identity (resource));
			if (child) {
				e_source_registry_server_add_source (server, source);
				g_object_unref (child);
			}
		}
	}

	g_list_free_full (list, g_object_unref);
	g_object_unref (server);

	thread = g_thread_new (NULL, owncloud_populate_thread, g_object_ref (collection));
	g_thread_unref (thread);
}

static void
e_owncloud_backend_class_init (EOwncloudBackendClass *class)
{
	ECollectionBackendClass *backend_class;

	backend_class = E_COLLECTION_BACKEND_CLASS (class);
	backend_class->populate = owncloud_backend_populate;
}

static void
e_owncloud_backend_class_finalize (EOwncloudBackendClass *class)
{
}

static void
e_owncloud_backend_init (EOwncloudBackend *backend)
{
}

static void
e_owncloud_backend_factory_class_init (EOwncloudBackendFactoryClass *class)
{
	ECollectionBackendFactoryClass *factory_class;

	factory_class = E_COLLECTION_BACKEND_FACTORY_CLASS (class);
	factory_class->factory_name = "owncloud";
	factory_class->backend_type = E_TYPE_OWNCLOUD_BACKEND;
}

static void
e_owncloud_backend_factory_class_finalize (EOwncloudBackendFactoryClass *class)
{
}

static void
e_owncloud_backend_factory_init (EOwncloudBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_owncloud_backend_register_type (type_module);
	e_owncloud_backend_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

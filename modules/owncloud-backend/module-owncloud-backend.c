/*
 * module-owncloud-backend.c
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libebackend/libebackend.h>
#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_OWNCLOUD_BACKEND \
	(e_owncloud_backend_get_type ())
#define E_OWNCLOUD_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_OWNCLOUD_BACKEND, EOwncloudBackend))
#define E_IS_OWNCLOUD_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_OWNCLOUD_BACKEND))

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

static void
owncloud_remove_unknown_sources_cb (gpointer resource_id,
                                    gpointer uid,
                                    gpointer user_data)
{
	ESourceRegistryServer *server = user_data;
	ESource *source;

	source = e_source_registry_server_ref_source (server, uid);

	if (source) {
		e_source_remove_sync (source, NULL, NULL);
		g_object_unref (source);
	}
}

static void
owncloud_add_found_source (ECollectionBackend *collection,
			   EWebDAVDiscoverSupports source_type,
			   SoupURI *uri,
			   const gchar *display_name,
			   const gchar *color,
			   GHashTable *known_sources)
{
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
	case E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS:
		backend_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
		provider = "webdav";
		identity_prefix = "contacts";
		break;
	case E_WEBDAV_DISCOVER_SUPPORTS_EVENTS:
		backend_name = E_SOURCE_EXTENSION_CALENDAR;
		provider = "caldav";
		identity_prefix = "events";
		break;
	case E_WEBDAV_DISCOVER_SUPPORTS_MEMOS:
		backend_name = E_SOURCE_EXTENSION_MEMO_LIST;
		provider = "caldav";
		identity_prefix = "memos";
		break;
	case E_WEBDAV_DISCOVER_SUPPORTS_TASKS:
		backend_name = E_SOURCE_EXTENSION_TASK_LIST;
		provider = "caldav";
		identity_prefix = "tasks";
		break;
	default:
		g_warn_if_reached ();
		return;
	}

	g_return_if_fail (backend_name != NULL);

	server = e_collection_backend_ref_server (collection);
	if (!server)
		return;

	url = soup_uri_to_string (uri, FALSE);
	identity = g_strconcat (identity_prefix, "::", url, NULL);
	source_uid = g_hash_table_lookup (known_sources, identity);
	is_new = !source_uid;
	if (is_new) {
		source = e_collection_backend_new_child (collection, identity);
		g_warn_if_fail (source != NULL);
	} else {
		source = e_source_registry_server_ref_source (server, source_uid);
		g_warn_if_fail (source != NULL);

		g_hash_table_remove (known_sources, identity);
	}

	if (source) {
		ESource *master_source;
		ESourceCollection *collection_extension;
		ESourceAuthentication *child_auth;
		ESourceResource *resource;
		ESourceWebdav *master_webdav, *child_webdav;

		master_source = e_backend_get_source (E_BACKEND (collection));
		master_webdav = e_source_get_extension (master_source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		collection_extension = e_source_get_extension (master_source, E_SOURCE_EXTENSION_COLLECTION);
		child_auth = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
		child_webdav = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		resource = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);

		e_source_authentication_set_user (child_auth, e_source_collection_get_identity (collection_extension));
		e_source_webdav_set_soup_uri (child_webdav, uri);
		e_source_resource_set_identity (resource, identity);

		if (is_new) {
			/* inherit ssl trust options */
			e_source_webdav_set_ssl_trust (child_webdav, e_source_webdav_get_ssl_trust (master_webdav));
		}
	}

	g_free (identity);
	g_free (url);

	/* these properties are synchronized always */
	if (source) {
		gint rr, gg, bb;

		backend = e_source_get_extension (source, backend_name);
		e_source_backend_set_backend_name (backend, provider);

		e_source_set_display_name (source, display_name);
		e_source_set_enabled (source, TRUE);

		/* Also check whether the color format is as expected; it cannot
		   be used gdk_rgba_parse here, because it required gdk/gtk. */
		if (is_new && source_type != E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS && color &&
		    sscanf (color, "#%02x%02x%02x", &rr, &gg, &bb) == 3) {
			gchar *safe_color;

			/* In case an #RRGGBBAA is returned */
			safe_color = g_strdup_printf ("#%02x%02x%02x", rr, gg, bb);

			e_source_selectable_set_color (E_SOURCE_SELECTABLE (backend), safe_color);

			g_free (safe_color);
		}

		if (is_new)
			e_source_registry_server_add_source (server, source);

		g_object_unref (source);
	}

	g_object_unref (server);
}

static void
owncloud_process_discovered_sources (ECollectionBackend *collection,
				     GSList *discovered_sources,
				     GHashTable *known_sources,
				     const EWebDAVDiscoverSupports *source_types,
				     gint n_source_types)
{
	GSList *link;
	gint ii;

	for (link = discovered_sources; link; link = g_slist_next (link)) {
		EWebDAVDiscoveredSource *discovered_source = link->data;
		SoupURI *soup_uri;

		if (!discovered_source || !discovered_source->href || !discovered_source->display_name)
			continue;

		soup_uri = soup_uri_new (discovered_source->href);
		if (!soup_uri)
			continue;

		for (ii = 0; ii < n_source_types; ii++) {
			if ((discovered_source->supports & source_types[ii]) == source_types[ii])
				owncloud_add_found_source (collection, source_types[ii], soup_uri,
					discovered_source->display_name, discovered_source->color, known_sources);
		}

		soup_uri_free (soup_uri);
	}
}

static ESourceAuthenticationResult
owncloud_backend_authenticate_sync (EBackend *backend,
				    const ENamedParameters *credentials,
				    gchar **out_certificate_pem,
				    GTlsCertificateFlags *out_certificate_errors,
				    GCancellable *cancellable,
				    GError **error)
{
	ECollectionBackend *collection = E_COLLECTION_BACKEND (backend);
	ESourceCollection *collection_extension;
	ESourceGoa *goa_extension;
	ESource *source;
	ESourceAuthenticationResult result;
	GHashTable *known_sources;
	GList *sources;
	GSList *discovered_sources = NULL;
	ENamedParameters *credentials_copy = NULL;
	gboolean any_success = FALSE, contacts_found = FALSE;
	GError *local_error = NULL;

	g_return_val_if_fail (collection != NULL, E_SOURCE_AUTHENTICATION_ERROR);

	source = e_backend_get_source (backend);
	collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);

	/* Ignore the request for non-GOA ownCloud sources by pretending success */
	if (!e_source_has_extension (source, E_SOURCE_EXTENSION_GOA))
		return E_SOURCE_AUTHENTICATION_ACCEPTED;

	goa_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_GOA);

	if (!e_source_collection_get_calendar_enabled (collection_extension) &&
	    !e_source_collection_get_contacts_enabled (collection_extension))
		return E_SOURCE_AUTHENTICATION_ACCEPTED;

	if (credentials && !e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_USERNAME)) {
		credentials_copy = e_named_parameters_new_clone (credentials);
		e_named_parameters_set (credentials_copy, E_SOURCE_CREDENTIAL_USERNAME, e_source_collection_get_identity (collection_extension));
		credentials = credentials_copy;
	}

	/* resource-id => source's UID */
	known_sources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	sources = e_collection_backend_list_calendar_sources (collection);
	g_list_foreach (sources, owncloud_add_uid_to_hashtable, known_sources);
	g_list_free_full (sources, g_object_unref);

	sources = e_collection_backend_list_contacts_sources (collection);
	g_list_foreach (sources, owncloud_add_uid_to_hashtable, known_sources);
	g_list_free_full (sources, g_object_unref);

	if (e_source_collection_get_calendar_enabled (collection_extension) && e_source_goa_get_calendar_url (goa_extension) &&
	    e_webdav_discover_sources_sync (source, e_source_goa_get_calendar_url (goa_extension), E_WEBDAV_DISCOVER_SUPPORTS_NONE,
		credentials, out_certificate_pem, out_certificate_errors,
		&discovered_sources, NULL, cancellable, &local_error)) {
		GSList *link;
		EWebDAVDiscoverSupports source_types[] = {
			E_WEBDAV_DISCOVER_SUPPORTS_EVENTS,
			E_WEBDAV_DISCOVER_SUPPORTS_MEMOS,
			E_WEBDAV_DISCOVER_SUPPORTS_TASKS,
			E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS
		};

		for (link = discovered_sources; link && !contacts_found; link = g_slist_next (link)) {
			EWebDAVDiscoveredSource *discovered_source = link->data;

			if (discovered_source)
				contacts_found = contacts_found || (discovered_source->supports & E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS) != 0;
		}

		owncloud_process_discovered_sources (collection, discovered_sources, known_sources, source_types,
			G_N_ELEMENTS (source_types) - (contacts_found ? 0 : 1));

		e_webdav_discover_free_discovered_sources (discovered_sources);
		discovered_sources = NULL;
		any_success = TRUE;
	}

	/* Skip search in this URL, if the previous one returned also contacts - it's quite likely it did */
	if (!contacts_found && !local_error && e_source_collection_get_contacts_enabled (collection_extension) &&
	    e_source_goa_get_contacts_url (goa_extension) &&
	    e_webdav_discover_sources_sync (source, e_source_goa_get_contacts_url (goa_extension), E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS,
		credentials, out_certificate_pem, out_certificate_errors,
		&discovered_sources, NULL, cancellable, &local_error)) {
		EWebDAVDiscoverSupports source_types[] = {
			E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS
		};

		owncloud_process_discovered_sources (collection, discovered_sources, known_sources, source_types, G_N_ELEMENTS (source_types));

		e_webdav_discover_free_discovered_sources (discovered_sources);
		discovered_sources = NULL;
		any_success = TRUE;
	}

	if (any_success) {
		ESourceRegistryServer *server;

		server = e_collection_backend_ref_server (collection);

		if (server) {
			g_hash_table_foreach (known_sources, owncloud_remove_unknown_sources_cb, server);
			g_object_unref (server);
		}

		g_clear_error (&local_error);
	}

	if (local_error == NULL) {
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;
		e_collection_backend_authenticate_children (collection, credentials);
	} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED) ||
		   g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_FORBIDDEN)) {
		result = E_SOURCE_AUTHENTICATION_REJECTED;
		g_clear_error (&local_error);
	} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED)) {
		result = E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED;
		g_propagate_error (error, local_error);
	} else {
		result = E_SOURCE_AUTHENTICATION_ERROR;
		g_propagate_error (error, local_error);
	}

	g_hash_table_destroy (known_sources);
	e_named_parameters_free (credentials_copy);

	return result;
}

static void
owncloud_backend_populate (ECollectionBackend *collection)
{
	GList *list, *liter;
	ESourceRegistryServer *server;
	ESourceCollection *collection_extension;
	ESource *source;

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

	source = e_backend_get_source (E_BACKEND (collection));
	collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);

	if (e_source_get_enabled (source) && (
	    e_source_collection_get_calendar_enabled (collection_extension) ||
	    e_source_collection_get_contacts_enabled (collection_extension))) {
		e_backend_schedule_credentials_required (E_BACKEND (collection),
			E_SOURCE_CREDENTIALS_REASON_REQUIRED, NULL, 0, NULL, NULL, G_STRFUNC);
	}
}

static void
e_owncloud_backend_class_init (EOwncloudBackendClass *class)
{
	EBackendClass *backend_class;
	ECollectionBackendClass *collection_backend_class;

	backend_class = E_BACKEND_CLASS (class);
	backend_class->authenticate_sync = owncloud_backend_authenticate_sync;

	collection_backend_class = E_COLLECTION_BACKEND_CLASS (class);
	collection_backend_class->populate = owncloud_backend_populate;
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

/*
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

#include "evolution-data-server-config.h"

#include <errno.h>
#include <glib/gstdio.h>

#include <libebackend/libebackend.h>

#include "evolution-source-registry-methods.h"

static gboolean
evolution_source_registry_migrate_owncloud_to_webdav (ESourceRegistryServer *server,
						      GKeyFile *key_file,
						      const gchar *uid)
{
	gboolean modified = FALSE;

	g_return_val_if_fail (key_file != NULL, FALSE);

	if (g_key_file_has_group (key_file, E_SOURCE_EXTENSION_COLLECTION) &&
	    g_key_file_has_key (key_file, E_SOURCE_EXTENSION_COLLECTION, "BackendName", NULL)) {
		gchar *backend_name;

		backend_name = g_key_file_get_string (key_file, E_SOURCE_EXTENSION_COLLECTION, "BackendName", NULL);
		if (g_strcmp0 (backend_name, "owncloud") == 0) {
			g_key_file_set_string (key_file, E_SOURCE_EXTENSION_COLLECTION, "BackendName", "webdav");
			modified = TRUE;
		}

		g_free (backend_name);
	}

	return modified;
}

#define PRIMARY_GROUP_NAME	"Data Source"

static gboolean
evolution_source_registry_migrate_webdav_book_to_carddav (ESourceRegistryServer *server,
							  GKeyFile *key_file,
							  const gchar *uid)
{
	gboolean modified = FALSE;

	g_return_val_if_fail (key_file != NULL, FALSE);

	if (g_key_file_has_group (key_file, E_SOURCE_EXTENSION_ADDRESS_BOOK) &&
	    g_key_file_has_key (key_file, E_SOURCE_EXTENSION_ADDRESS_BOOK, "BackendName", NULL)) {
		gchar *backend_name;

		backend_name = g_key_file_get_string (key_file, E_SOURCE_EXTENSION_ADDRESS_BOOK, "BackendName", NULL);
		if (g_strcmp0 (backend_name, "webdav") == 0) {
			g_key_file_set_string (key_file, E_SOURCE_EXTENSION_ADDRESS_BOOK, "BackendName", "carddav");
			modified = TRUE;
		}

		g_free (backend_name);
	}

	if (g_key_file_has_group (key_file, PRIMARY_GROUP_NAME) &&
	    g_key_file_has_key (key_file, PRIMARY_GROUP_NAME, "Parent", NULL)) {
		gchar *parent;

		parent = g_key_file_get_string (key_file, PRIMARY_GROUP_NAME, "Parent", NULL);
		if (g_strcmp0 (parent, "webdav-stub") == 0) {
			g_key_file_set_string (key_file, PRIMARY_GROUP_NAME, "Parent", "carddav-stub");
			modified = TRUE;
		}

		g_free (parent);
	}

	return modified;
}


static gboolean
evolution_source_registry_migrate_google_book_to_carddav (ESourceRegistryServer *server,
							  GKeyFile *key_file,
							  const gchar *uid)
{
	gboolean modified = FALSE;

	g_return_val_if_fail (key_file != NULL, FALSE);

	if (g_key_file_has_group (key_file, E_SOURCE_EXTENSION_ADDRESS_BOOK) &&
	    g_key_file_has_key (key_file, E_SOURCE_EXTENSION_ADDRESS_BOOK, "BackendName", NULL)) {
		gchar *backend_name;

		backend_name = g_key_file_get_string (key_file, E_SOURCE_EXTENSION_ADDRESS_BOOK, "BackendName", NULL);
		if (g_strcmp0 (backend_name, "google") == 0) {
			g_key_file_set_string (key_file, E_SOURCE_EXTENSION_ADDRESS_BOOK, "BackendName", "carddav");
			modified = TRUE;
		}

		g_free (backend_name);
	}

	if (modified && g_key_file_has_group (key_file, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		gchar *user;

		user = g_key_file_get_string (key_file, E_SOURCE_EXTENSION_AUTHENTICATION, "User", NULL);

		if (user && *user) {
			gchar *path;

			/* Unfortunately no mapping with the default book, thus either drop it or hard code the URL */
			path = g_strdup_printf ("/carddav/v1/principals/%s/lists/default/", user);

			g_key_file_set_string (key_file, E_SOURCE_EXTENSION_WEBDAV_BACKEND, "ResourcePath", path);
			g_key_file_set_string (key_file, E_SOURCE_EXTENSION_AUTHENTICATION, "Host", "www.googleapis.com");
			g_key_file_set_string (key_file, E_SOURCE_EXTENSION_AUTHENTICATION, "Method", "Google");
			g_key_file_set_integer (key_file, E_SOURCE_EXTENSION_AUTHENTICATION, "Port", 443);
			g_key_file_set_string (key_file, E_SOURCE_EXTENSION_AUTHENTICATION, "User", user);
			g_key_file_set_string (key_file, E_SOURCE_EXTENSION_SECURITY, "Method", "tls");

			g_free (path);
		}

		g_free (user);
	}

	return modified;
}

gboolean
evolution_source_registry_migrate_tweak_key_file (ESourceRegistryServer *server,
						  GKeyFile *key_file,
						  const gchar *uid)
{
	gboolean modified;

	modified = evolution_source_registry_migrate_owncloud_to_webdav (server, key_file, uid);
	modified = evolution_source_registry_migrate_webdav_book_to_carddav (server, key_file, uid) || modified;
	modified = evolution_source_registry_migrate_google_book_to_carddav (server, key_file, uid) || modified;

	return modified;
}

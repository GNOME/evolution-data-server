/*
 * module-cache-reaper.c
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

#include <errno.h>
#include <time.h>
#include <glib/gstdio.h>

#include <libedataserver/e-data-server-util.h>

#include <libebackend/e-extension.h>
#include <libebackend/e-source-registry-server.h>

#include "e-cache-reaper-utils.h"

/* Standard GObject macros */
#define E_TYPE_CACHE_REAPER \
	(e_cache_reaper_get_type ())
#define E_CACHE_REAPER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CACHE_REAPER, ECacheReaper))

/* Where abandoned cache directories go to die. */
#define TRASH_DIRECTORY_NAME "trash"

/* XXX These intervals are rather arbitrary and prone to bikeshedding.
 *     It's just what I decided on.  On startup we wait an hour to reap
 *     abandoned cache directories, and thereafter repeat every 24 hours. */
#define INITIAL_INTERVAL_SECONDS  ( 1 * (60 * 60))
#define REGULAR_INTERVAL_SECONDS  (24 * (60 * 60))

typedef struct _ECacheReaper ECacheReaper;
typedef struct _ECacheReaperClass ECacheReaperClass;

struct _ECacheReaper {
	EExtension parent;

	guint n_directories;
	GFile **cache_directories;
	GFile **trash_directories;

	guint reaping_timeout_id;
};

struct _ECacheReaperClass {
	EExtensionClass parent_class;
};

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_cache_reaper_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	ECacheReaper,
	e_cache_reaper,
	E_TYPE_EXTENSION)

static ESourceRegistryServer *
cache_reaper_get_server (ECacheReaper *extension)
{
	EExtensible *extensible;

	extensible = e_extension_get_extensible (E_EXTENSION (extension));

	return E_SOURCE_REGISTRY_SERVER (extensible);
}

static void
cache_reaper_trash_directory_reaped (GObject *source_object,
                                     GAsyncResult *result,
                                     gpointer unused)
{
	GFile *trash_directory;
	GError *error = NULL;

	trash_directory = G_FILE (source_object);

	e_reap_trash_directory_finish (trash_directory, result, &error);

	/* Ignore cancellations. */
	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		/* do nothing */

	} else if (error != NULL) {
		gchar *path;

		path = g_file_get_path (trash_directory);
		g_warning ("Failed to reap '%s': %s", path, error->message);
		g_free (path);
	}

	g_clear_error (&error);
}

static gboolean
cache_reaper_reap_trash_directories (gpointer user_data)
{
	ECacheReaper *extension = E_CACHE_REAPER (user_data);
	guint ii;

	g_message ("Reaping abandoned cache directories");

	for (ii = 0; ii < extension->n_directories; ii++)
		e_reap_trash_directory (
			extension->trash_directories[ii],
			G_PRIORITY_LOW, NULL,
			cache_reaper_trash_directory_reaped,
			NULL);

	/* Always explicitly reschedule since the initial
	 * interval is different than the regular interval. */
	extension->reaping_timeout_id =
		g_timeout_add_seconds (
			REGULAR_INTERVAL_SECONDS,
			cache_reaper_reap_trash_directories,
			extension);

	return FALSE;
}

static void
cache_reaper_move_directory (GFile *source_directory,
                             GFile *target_directory)
{
	GFileType file_type;
	GError *error = NULL;

	/* Make sure the source directory is really a directory. */

	file_type = g_file_query_file_type (
		source_directory,
		G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL);

	if (file_type == G_FILE_TYPE_DIRECTORY) {
		g_file_move (
			source_directory,
			target_directory,
			G_FILE_COPY_NOFOLLOW_SYMLINKS,
			NULL, NULL, NULL, &error);

		/* Update the target directory's modification time.
		 * This step is not critical, do not set the GError. */
		if (error == NULL) {
			time_t now = time (NULL);

			g_file_set_attribute (
				target_directory,
				G_FILE_ATTRIBUTE_TIME_MODIFIED,
				G_FILE_ATTRIBUTE_TYPE_UINT64,
				&now, G_FILE_QUERY_INFO_NONE,
				NULL, NULL);
		}
	}

	if (error != NULL) {
		gchar *path;

		path = g_file_get_path (source_directory);
		g_warning ("Failed to move '%s': %s", path, error->message);
		g_free (path);

		g_error_free (error);
	}
}

static void
cache_reaper_scan_cache_directory (ECacheReaper *extension,
                                   GFile *cache_directory,
                                   GFile *trash_directory)
{
	GFileEnumerator *file_enumerator;
	ESourceRegistryServer *server;
	GFileInfo *file_info;
	GError *error = NULL;

	server = cache_reaper_get_server (extension);

	file_enumerator = g_file_enumerate_children (
		cache_directory,
		G_FILE_ATTRIBUTE_STANDARD_NAME,
		G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
		NULL, &error);

	if (error != NULL) {
		g_warn_if_fail (file_enumerator == NULL);
		goto exit;
	}

	g_return_if_fail (G_IS_FILE_ENUMERATOR (file_enumerator));

	file_info = g_file_enumerator_next_file (
		file_enumerator, NULL, &error);

	while (file_info != NULL) {
		ESource *source;
		const gchar *name;

		name = g_file_info_get_name (file_info);

		/* Skip the trash directory, obviously. */
		if (g_strcmp0 (name, TRASH_DIRECTORY_NAME) == 0)
			goto next;

		source = e_source_registry_server_ref_source (server, name);

		if (source == NULL) {
			GFile *source_directory;
			GFile *target_directory;

			source_directory = g_file_get_child (
				cache_directory, name);
			target_directory = g_file_get_child (
				trash_directory, name);

			cache_reaper_move_directory (
				source_directory, target_directory);

			g_object_unref (source_directory);
			g_object_unref (target_directory);
		} else {
			g_object_unref (source);
		}

next:
		g_object_unref (file_info);

		file_info = g_file_enumerator_next_file (
			file_enumerator, NULL, &error);
	}

	g_object_unref (file_enumerator);

exit:
	if (error != NULL) {
		gchar *path;

		path = g_file_get_path (cache_directory);
		g_warning ("Failed to scan '%s': %s", path, error->message);
		g_free (path);

		g_error_free (error);
	}
}

static void
cache_reaper_scan_cache_directories (ECacheReaper *extension)
{
	guint ii;

	/* Scan the base cache directories for unregnized subdirectories.
	 * The subdirectories are named after data source UIDs, so compare
	 * their names to registered data sources and move any unrecognized
	 * subdirectories to the "trash" subdirectory to be reaped later. */

	g_message ("Scanning cache directories");

	for (ii = 0; ii < extension->n_directories; ii++)
		cache_reaper_scan_cache_directory (
			extension,
			extension->cache_directories[ii],
			extension->trash_directories[ii]);
}

static void
cache_reaper_move_cache_to_trash (ECacheReaper *extension,
                                  ESource *source,
                                  GFile *cache_directory,
                                  GFile *trash_directory)
{
	GFile *source_directory;
	GFile *target_directory;
	const gchar *uid;

	uid = e_source_get_uid (source);

	source_directory = g_file_get_child (cache_directory, uid);
	target_directory = g_file_get_child (trash_directory, uid);

	/* This is a no-op if the source directory does not exist. */
	cache_reaper_move_directory (source_directory, target_directory);

	g_object_unref (source_directory);
	g_object_unref (target_directory);
}

static void
cache_reaper_recover_cache_from_trash (ECacheReaper *extension,
                                       ESource *source,
                                       GFile *cache_directory,
                                       GFile *trash_directory)
{
	GFile *source_directory;
	GFile *target_directory;
	const gchar *uid;

	uid = e_source_get_uid (source);

	source_directory = g_file_get_child (trash_directory, uid);
	target_directory = g_file_get_child (cache_directory, uid);

	/* This is a no-op if the source directory does not exist. */
	cache_reaper_move_directory (source_directory, target_directory);

	g_object_unref (source_directory);
	g_object_unref (target_directory);
}

static void
cache_reaper_files_loaded_cb (ESourceRegistryServer *server,
                              ECacheReaper *extension)
{
	cache_reaper_scan_cache_directories (extension);

	/* Schedule the initial reaping. */
	if (extension->reaping_timeout_id == 0)
		extension->reaping_timeout_id =
			g_timeout_add_seconds (
				INITIAL_INTERVAL_SECONDS,
				cache_reaper_reap_trash_directories,
				extension);
}

static void
cache_reaper_source_added_cb (ESourceRegistryServer *server,
                              ESource *source,
                              ECacheReaper *extension)
{
	guint ii;

	/* The Cache Reaper is not too proud to dig through the
	 * trash on the off chance the newly-added source has a
	 * recoverable cache directory. */
	for (ii = 0; ii < extension->n_directories; ii++)
		cache_reaper_recover_cache_from_trash (
			extension, source,
			extension->cache_directories[ii],
			extension->trash_directories[ii]);
}

static void
cache_reaper_source_removed_cb (ESourceRegistryServer *server,
                                ESource *source,
                                ECacheReaper *extension)
{
	guint ii;

	/* Stage the removed source's cache directory for
	 * reaping by moving it to the "trash" directory. */
	for (ii = 0; ii < extension->n_directories; ii++)
		cache_reaper_move_cache_to_trash (
			extension, source,
			extension->cache_directories[ii],
			extension->trash_directories[ii]);
}

static void
cache_reaper_finalize (GObject *object)
{
	ECacheReaper *extension;
	guint ii;

	extension = E_CACHE_REAPER (object);

	for (ii = 0; ii < extension->n_directories; ii++) {
		g_object_unref (extension->cache_directories[ii]);
		g_object_unref (extension->trash_directories[ii]);
	}

	g_free (extension->cache_directories);
	g_free (extension->trash_directories);

	if (extension->reaping_timeout_id > 0)
		g_source_remove (extension->reaping_timeout_id);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cache_reaper_parent_class)->finalize (object);
}

static void
cache_reaper_constructed (GObject *object)
{
	EExtension *extension;
	EExtensible *extensible;

	extension = E_EXTENSION (object);
	extensible = e_extension_get_extensible (extension);

	g_signal_connect (
		extensible, "files-loaded",
		G_CALLBACK (cache_reaper_files_loaded_cb), extension);

	g_signal_connect (
		extensible, "source-added",
		G_CALLBACK (cache_reaper_source_added_cb), extension);

	g_signal_connect (
		extensible, "source-removed",
		G_CALLBACK (cache_reaper_source_removed_cb), extension);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_cache_reaper_parent_class)->constructed (object);
}

static void
e_cache_reaper_class_init (ECacheReaperClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = cache_reaper_finalize;
	object_class->constructed = cache_reaper_constructed;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_SOURCE_REGISTRY_SERVER;
}

static void
e_cache_reaper_class_finalize (ECacheReaperClass *class)
{
}

static void
e_cache_reaper_init (ECacheReaper *extension)
{
	GFile *base_directory;
	const gchar *user_cache_dir;
	guint n_directories, ii;

	/* These are component names from which
	 * the cache directory arrays are built. */
	const gchar *component_names[] = {
		"addressbook",
		"calendar",
		"mail",
		"memos",
		"tasks"
	};

	n_directories = G_N_ELEMENTS (component_names);

	extension->n_directories = n_directories;
	extension->cache_directories = g_new0 (GFile *, n_directories);
	extension->trash_directories = g_new0 (GFile *, n_directories);

	user_cache_dir = e_get_user_cache_dir ();
	base_directory = g_file_new_for_path (user_cache_dir);

	for (ii = 0; ii < n_directories; ii++) {
		GFile *cache_directory;
		GFile *trash_directory;
		GError *error = NULL;

		cache_directory = g_file_get_child (
			base_directory, component_names[ii]);
		trash_directory = g_file_get_child (
			cache_directory, TRASH_DIRECTORY_NAME);

		/* Cache directory is a parent of the trash
		 * directory so this is sufficient for both. */
		g_file_make_directory_with_parents (
			trash_directory, NULL, &error);

		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS))
			g_clear_error (&error);

		if (error != NULL) {
			gchar *path;

			path = g_file_get_path (trash_directory);
			g_warning (
				"Failed to make directory '%s': %s",
				path, error->message);
			g_free (path);

			g_error_free (error);
		}

		extension->cache_directories[ii] = cache_directory;
		extension->trash_directories[ii] = trash_directory;
	}

	g_object_unref (base_directory);
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_cache_reaper_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}


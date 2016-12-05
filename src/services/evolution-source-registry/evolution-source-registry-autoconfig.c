/*
 * evolution-source-registry-autoconfig.c
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

#include "evolution-data-server-config.h"

#include <errno.h>
#include <string.h>
#include <glib/gstdio.h>

#include <libebackend/libebackend.h>

typedef struct _MergeSourceData {
	gchar *path;
	GKeyFile *key_file;
} MergeSourceData;

typedef struct _EnvVarMap {
	const gchar *from;
	const gchar *to;
} EnvVarMap;

typedef void (*MergeSourcePopulateHashtableFunc)(GHashTable *source,
                                                 GKeyFile *key_file,
                                                 const gchar *basename,
                                                 const gchar *filename);

/* Forward Declarations */
gboolean	evolution_source_registry_merge_autoconfig_sources
						(ESourceRegistryServer *server,
						 GError **error);

static void
evolution_source_registry_free_merge_source_data (gpointer mem)
{
	MergeSourceData *source_data = (MergeSourceData *) mem;

	if (source_data == NULL)
		return;

	g_free (source_data->path);
	g_key_file_unref (source_data->key_file);
	g_free (source_data);
}

static void
populate_hashtable_autoconfig (GHashTable *sources,
                               GKeyFile *key_file,
                               const gchar *basename,
                               const gchar *filename)
{
	MergeSourceData *data;
	GError *local_error = NULL;
	gchar *uid, *val;

	val = g_key_file_get_value (
				key_file,
				E_SOURCE_EXTENSION_AUTOCONFIG,
				"Revision",
				&local_error);
	if (val == NULL) {
		e_source_registry_debug_print (
					"Autoconfig: Failed to read '%s': %s.\n",
					filename,
					local_error->message);
		g_error_free (local_error);
		return;
	}
	g_free (val);

	uid = g_strndup (basename, strlen (basename) - 7);

	data = g_new0 (MergeSourceData, 1);
	data->key_file = g_key_file_ref (key_file);
	data->path = g_strdup (filename);

	g_hash_table_insert (sources, uid, data);
	e_source_registry_debug_print (
				"Autoconfig: Found autoconfig source '%s'.\n",
				filename);
}

static void
populate_hashtable_home (GHashTable *sources,
                         GKeyFile *key_file,
                         const gchar *basename,
                         const gchar *filename)
{
	MergeSourceData *data;
	gchar *uid;

	if (!g_key_file_has_group (key_file, E_SOURCE_EXTENSION_AUTOCONFIG))
		return;

	uid = g_strndup (basename, strlen (basename) - 7);

	data = g_new0 (MergeSourceData, 1);
	data->key_file = g_key_file_ref (key_file);
	data->path = g_strdup (filename);
	g_hash_table_insert (sources, uid, data);
}

static gboolean
evolution_source_registry_read_directory (const gchar *path,
                                          GHashTable *sources,
                                          MergeSourcePopulateHashtableFunc func,
                                          GError **error)
{
	GDir *dir;
	const gchar *basename;

	dir = g_dir_open (path, 0, error);
	if (dir == NULL) {
		return FALSE;
	}

	while ((basename = g_dir_read_name (dir)) != NULL) {
		GKeyFile *key_file;
		gchar *filename;

		if (!g_str_has_suffix (basename, ".source"))
			continue;

		filename = g_build_filename (path, basename, NULL);

		key_file = g_key_file_new ();
		if (!g_key_file_load_from_file (
				key_file,
				filename,
				G_KEY_FILE_NONE,
				error)) {
			g_prefix_error (
				error,
				"Failed to load key file '%s': ",
				filename);
			g_free (filename);
			g_dir_close (dir);
			g_key_file_unref (key_file);
			return FALSE;
		}

		func (sources, key_file, basename, filename);
		g_free (filename);
		g_key_file_unref (key_file);
	}

	g_dir_close (dir);

	return TRUE;
}

static void
evolution_source_registry_clean_orphans (GHashTable *autoconfig_sources,
                                         GHashTable *home_sources)
{
	GList *keys;
	GList *index;
	keys = g_hash_table_get_keys (home_sources);

	for (index = keys; index != NULL; index = g_list_next (index)) {
		gchar *key = index->data;
		if (!g_hash_table_contains (autoconfig_sources, key)) {
			MergeSourceData *data = g_hash_table_lookup (home_sources, key);
			if (data != NULL) {
				/* if we fail to remove it, keep going */
				if (g_unlink (data->path) == -1) {
					e_source_registry_debug_print (
								"Autoconfig: Error cleaning orphan source '%s': %s.\n",
								data->path,
								g_strerror (errno));
				}
				e_source_registry_debug_print (
							"Autoconfig: Removed orphan autoconfig source '%s'.\n",
							data->path);

				g_hash_table_remove (home_sources, data->path);
			}
		}
	}

	g_list_free (keys);
}

static gboolean
evolution_source_registry_replace_env_vars_eval_cb (const GMatchInfo *match_info,
                                                    GString *result,
                                                    gpointer user_data)
{
	gchar *var_name, *source_path;
	const gchar *val;
	gint ii;
	EnvVarMap map[] = {
		{ "USER", g_get_user_name () },
		{ "REALNAME", g_get_real_name () },
		{ "HOST", g_get_host_name () }
	};

	var_name = g_match_info_fetch (match_info, 1);
	source_path = user_data;

	for (ii = 0; ii < G_N_ELEMENTS (map); ++ii) {
		if (g_strcmp0 (var_name, map[ii].from) == 0) {
			g_string_append (result, map[ii].to);
			goto exit;
		}
	}

	val = g_getenv (var_name);

	if (val != NULL) {
		g_string_append (result, val);
	} else {
		/* env var will be replaced by an empty string */
		e_source_registry_debug_print (
			"Autoconfig: Variable '${%s}' not found, used in '%s'.\n",
			var_name,
			source_path);
	}

 exit:
	g_free (var_name);

	return FALSE;
}

static gchar *
evolution_source_registry_replace_env_vars (const gchar *old, MergeSourceData *source)
{
	GRegex *regex;
	gchar *new;
	GError *local_error = NULL;

	g_return_val_if_fail (old != NULL, NULL);

	regex = g_regex_new ("\\$\\{(\\w+)\\}", 0, 0, NULL);

	g_return_val_if_fail (regex != NULL, g_strdup (old));

	new = g_regex_replace_eval (
				regex,
				old,
				-1,
				0,
				0,
				evolution_source_registry_replace_env_vars_eval_cb,
				source->path,
				&local_error);

	g_regex_unref (regex);

	if (new == NULL) {
		e_source_registry_debug_print (
					"Autoconfig: Replacing variables failed: %s.\n",
					local_error ? local_error->message : "Unknown error");;
		g_error_free (local_error);
		return g_strdup (old);
	}

	return new;
}

static void
evolution_source_registry_copy_source (MergeSourceData *target,
                                       MergeSourceData *source)
{
	gchar **groups = NULL;
	gsize ngroups;
	gint ii;

	groups = g_key_file_get_groups (source->key_file, &ngroups);

	for (ii = 0; ii < ngroups; ii++) {
		gsize nkeys;
		gint jj;
		gchar **keys;

		keys = g_key_file_get_keys (
				source->key_file,
				groups[ii],
				&nkeys,
				NULL);

		for (jj = 0; jj < nkeys; jj++) {
			gchar *new_val;
			gchar *val = g_key_file_get_value (
					source->key_file,
					groups[ii],
					keys[jj],
					NULL);

			new_val = evolution_source_registry_replace_env_vars (val, source);
			g_free (val);

			g_key_file_set_value (
				target->key_file,
				groups[ii],
				keys[jj],
				new_val);
			g_free (new_val);
		}
		g_strfreev (keys);
	}

	g_strfreev (groups);
}

static gboolean
evolution_source_registry_merge_source (GHashTable *home_sources,
                                        const gchar *key,
                                        MergeSourceData *autoconfig_key_file,
                                        GList **key_files_to_copy,
                                        GError **error)
{
	GKeyFile *new_keyfile;
	MergeSourceData *home_key_file;
	MergeSourceData *new_data;
	gboolean skip_copy;
	gchar *autoconfig_revision, *home_revision;

	g_return_val_if_fail (key_files_to_copy != NULL, FALSE);

	home_key_file = g_hash_table_lookup (home_sources, key);

	autoconfig_revision = g_key_file_get_value (
				autoconfig_key_file->key_file,
				E_SOURCE_EXTENSION_AUTOCONFIG,
				"Revision",
				error);
	if (autoconfig_revision == NULL) {
		g_prefix_error (
			error,
			"Failed to get revision of key file '%s': ",
			autoconfig_key_file->path);
		return FALSE;
	}

	home_revision = g_key_file_get_value (
				home_key_file->key_file,
				E_SOURCE_EXTENSION_AUTOCONFIG,
				"Revision",
				error);
	if (home_revision == NULL) {
		g_prefix_error (
			error,
			"Failed to get revision of key file '%s': ",
			home_key_file->path);
		g_free (autoconfig_revision);
		return FALSE;
	}

	skip_copy = g_strcmp0 (autoconfig_revision, home_revision) == 0;

	if (skip_copy) {
		e_source_registry_debug_print (
					"Autoconfig: Revisions of '%s' and '%s' are the same ('%s'). Skipping update.\n",
					home_key_file->path,
					autoconfig_key_file->path,
					home_revision);
		g_free (autoconfig_revision);
		g_free (home_revision);
		return TRUE;
	}

	e_source_registry_debug_print (
				"Autoconfig: '%s' (Revision '%s') will be updated from '%s' (Revision '%s').\n",
				home_key_file->path,
				home_revision,
				autoconfig_key_file->path,
				autoconfig_revision);

	g_free (autoconfig_revision);
	g_free (home_revision);

	new_keyfile = g_key_file_new ();

	if (!g_key_file_load_from_file (
			new_keyfile,
			home_key_file->path,
			G_KEY_FILE_NONE,
			error)) {
		g_prefix_error (
			error,
			"Failed to load key file '%s': ",
			home_key_file->path);
		g_key_file_unref (new_keyfile);
		return FALSE;
	}

	new_data = g_new0 (MergeSourceData, 1);
	new_data->path = g_strdup (home_key_file->path);
	new_data->key_file = new_keyfile;

	evolution_source_registry_copy_source (new_data, autoconfig_key_file);

	*key_files_to_copy = g_list_prepend (*key_files_to_copy, new_data);

	return TRUE;
}

static void
evolution_source_registry_generate_source_from_autoconfig (const gchar *key,
                                                           MergeSourceData *autoconfig_key_file,
                                                           GList **key_files_to_copy)
{
	GKeyFile *new_keyfile;
	MergeSourceData *new_data;
	gchar *dest_source_filename, *dest_source_path;

	g_return_if_fail (key_files_to_copy != NULL);

	dest_source_filename = g_strdup_printf ("%s.source", key);
	dest_source_path = g_build_filename (
					e_server_side_source_get_user_dir (),
					dest_source_filename,
					NULL);

	g_free (dest_source_filename);

	/* If we're here it means there was no file in home sources with an
	 * Autoconfig section that corresponds with autoconfig_key_file.
	 * In the unlikely event there's an existing file in home sources with
	 * the same name as autoconfig_key file, let's not include it in the
	 * list of files to copy to avoid data loss. */
	if (g_file_test (dest_source_path, G_FILE_TEST_EXISTS)) {
		e_source_registry_debug_print (
			"Autoconfig: Skipping '%s' due to an existing '%s' without '%s' section.\n",
			autoconfig_key_file->path, dest_source_path, E_SOURCE_EXTENSION_AUTOCONFIG);
		g_free (dest_source_path);
		return;
	}

	new_keyfile = g_key_file_new ();

	new_data = g_new0 (MergeSourceData, 1);
	new_data->path = dest_source_path;
	new_data->key_file = new_keyfile;

	evolution_source_registry_copy_source (new_data, autoconfig_key_file);

	*key_files_to_copy = g_list_prepend (*key_files_to_copy, new_data);

	e_source_registry_debug_print (
			"Autoconfig: New autoconfig source '%s'. It will be copied to '%s'.\n",
			autoconfig_key_file->path,
			new_data->path);
}

static GList *
evolution_source_registry_merge_sources (GHashTable *autoconfig_sources,
                                         GHashTable *home_sources)
{
	GHashTableIter iter;
	GList *key_files_to_copy = NULL;
	gpointer key, value;

	evolution_source_registry_clean_orphans (autoconfig_sources, home_sources);

	g_hash_table_iter_init (&iter, autoconfig_sources);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		MergeSourceData *autoconfig_key_file;

		autoconfig_key_file = (MergeSourceData *)value;
		if (g_hash_table_contains (home_sources, key)) {
			GError *local_error = NULL;

			if (!evolution_source_registry_merge_source (
					home_sources,
					key,
					autoconfig_key_file,
					&key_files_to_copy,
					&local_error)) {
				e_source_registry_debug_print (
						"Autoconfig: evolution_source_registry_merge_source() failed: %s.\n",
						local_error ? local_error->message : "Unknown error");
				g_clear_error (&local_error);
				continue;
			}
		} else {
			evolution_source_registry_generate_source_from_autoconfig (
				key,
				autoconfig_key_file,
				&key_files_to_copy);
		}
	}

	return key_files_to_copy;
}

static gboolean
evolution_source_registry_write_key_file (MergeSourceData *key_file_data,
                                          GError **error)
{
	return g_key_file_save_to_file (key_file_data->key_file, key_file_data->path, error);
}

static gboolean
evolution_source_registry_write_key_files (GList *list,
                                           GError **error)
{
	GList *index;

	for (index = list; index != NULL; index = g_list_next (index)) {
		MergeSourceData *data;

		data = index->data;

		if (data == NULL)
			continue;

		if (!evolution_source_registry_write_key_file (data, error))
			return FALSE;
	}

	return TRUE;
}

gboolean
evolution_source_registry_merge_autoconfig_sources (ESourceRegistryServer *server,
                                                    GError **error)
{
	GHashTable *home_sources = NULL, *autoconfig_sources = NULL;
	GList *key_files_to_copy = NULL;
	GError *local_error = NULL;
	gboolean success = FALSE;
	const gchar * const *config_dirs;
	gint ii;

	autoconfig_sources = g_hash_table_new_full (
				g_str_hash,
				g_str_equal,
				g_free,
				evolution_source_registry_free_merge_source_data);

	config_dirs = g_get_system_config_dirs ();
	for (ii = 0; config_dirs[ii]; ii++) {
		gchar *path = g_build_filename (
				config_dirs[ii],
				"evolution-data-server",
				"autoconfig",
				NULL);
		success = evolution_source_registry_read_directory (
				path,
				autoconfig_sources,
				populate_hashtable_autoconfig,
				&local_error);

		g_free (path);

		if (!success) {
			if (local_error != NULL &&
				g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
				g_clear_error (&local_error);
				continue;
			}

			goto exit;
		}
	}

	home_sources = g_hash_table_new_full (
			g_str_hash,
			g_str_equal,
			g_free,
			evolution_source_registry_free_merge_source_data);

	success = evolution_source_registry_read_directory (
			e_server_side_source_get_user_dir (),
			home_sources,
			populate_hashtable_home,
			&local_error);

	if (!success)
		goto exit;

	key_files_to_copy = evolution_source_registry_merge_sources (autoconfig_sources, home_sources);

	success = evolution_source_registry_write_key_files (key_files_to_copy, error);

 exit:
	if (autoconfig_sources != NULL)
		g_hash_table_unref (autoconfig_sources);
	if (home_sources != NULL)
		g_hash_table_unref (home_sources);
	if (key_files_to_copy != NULL)
		g_list_free_full (key_files_to_copy, evolution_source_registry_free_merge_source_data);

	if (local_error != NULL)
		g_propagate_error (error, local_error);

	return success;
}

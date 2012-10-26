/*
 * e-test-dbus-utils.c
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
#include <stdlib.h>
#include <string.h>

#include "e-test-dbus-utils.h"

#include <libedataserver/libedataserver.h>

static GOnce base_directories_once = G_ONCE_INIT;

static void
test_setup_xdg_data_home (const gchar *base_directory)
{
	gchar *filename;

	g_assert (base_directory != NULL);

	filename = g_build_filename (base_directory, "share", NULL);

	g_print ("XDG_DATA_HOME=%s\n", filename);
	g_assert (g_setenv ("XDG_DATA_HOME", filename, TRUE));
	g_assert_cmpstr (g_get_user_data_dir (), ==, filename);

	g_free (filename);
}

static void
test_setup_xdg_cache_home (const gchar *base_directory)
{
	gchar *filename;

	g_assert (base_directory != NULL);

	filename = g_build_filename (base_directory, "cache", NULL);

	g_print ("XDG_CACHE_HOME=%s\n", filename);
	g_assert (g_setenv ("XDG_CACHE_HOME", filename, TRUE));
	g_assert_cmpstr (g_get_user_cache_dir (), ==, filename);

	g_free (filename);
}

static void
test_setup_xdg_config_home (const gchar *base_directory)
{
	gchar *filename;

	g_assert (base_directory != NULL);

	filename = g_build_filename (base_directory, "config", NULL);

	g_print ("XDG_CONFIG_HOME=%s\n", filename);
	g_assert (g_setenv ("XDG_CONFIG_HOME", filename, TRUE));
	g_assert_cmpstr (g_get_user_config_dir (), ==, filename);

	g_free (filename);
}

static gpointer
test_setup_base_directories_once (gpointer unused)
{
	gchar *base_directory;

	/* Do not invoke g_get_user_cache_dir() here because that will
	 * permanently set the cache directory result and we won't be
	 * able to override it with an environment variable. */
	base_directory = g_build_filename (
		g_get_home_dir (),
		".cache", "evolution",
		"tmp", "test-home-XXXXXX", NULL);

	mkdtemp (base_directory);

	test_setup_xdg_data_home (base_directory);
	test_setup_xdg_cache_home (base_directory);
	test_setup_xdg_config_home (base_directory);

	return base_directory;
}

const gchar *
e_test_setup_base_directories (void)
{
	g_once (
		&base_directories_once,
		test_setup_base_directories_once, NULL);

	return base_directories_once.retval;
}

gboolean
e_test_clean_base_directories (GError **error)
{
	GFile *file;
	gboolean success = TRUE;
	const gchar *user_data_dir;
	const gchar *user_cache_dir;
	const gchar *user_config_dir;

	e_test_setup_base_directories ();

	user_data_dir = g_get_user_data_dir ();
	g_assert (strstr (user_data_dir, "test-home") != NULL);

	user_cache_dir = g_get_user_cache_dir ();
	g_assert (strstr (user_cache_dir, "test-home") != NULL);

	user_config_dir = g_get_user_config_dir ();
	g_assert (strstr (user_config_dir, "test-home") != NULL);

	if (success) {
		file = g_file_new_for_path (user_data_dir);
		success = e_file_recursive_delete_sync (file, NULL, error);
		g_object_unref (file);
	}

	if (success) {
		file = g_file_new_for_path (user_cache_dir);
		success = e_file_recursive_delete_sync (file, NULL, error);
		g_object_unref (file);
	}

	if (success) {
		file = g_file_new_for_path (user_config_dir);
		success = e_file_recursive_delete_sync (file, NULL, error);
		g_object_unref (file);
	}

	return success;
}

GTestDBus *
e_test_setup_dbus_session (void)
{
	GTestDBus *test_dbus;
	const gchar * const *system_data_dirs;
	gint ii;

	e_test_setup_base_directories ();

	system_data_dirs = g_get_system_data_dirs ();
	g_assert (system_data_dirs != NULL);

	test_dbus = g_test_dbus_new (G_TEST_DBUS_NONE);

	for (ii = 0; system_data_dirs[ii] != NULL; ii++) {
		gchar *service_dir;

		service_dir = g_build_filename (
			system_data_dirs[ii], "dbus-1", "services", NULL);
		g_print ("Adding service dir: %s\n", service_dir);
		g_test_dbus_add_service_dir (test_dbus, service_dir);
		g_free (service_dir);
	}

	return test_dbus;
}

/*
 * e-data-book-migrate.c
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
#include <glib/gstdio.h>
#include <libedataserver/e-data-server-util.h>

void e_data_book_migrate (void);

static gboolean
data_book_migrate_rename (const gchar *old_filename,
                          const gchar *new_filename)
{
	gboolean old_filename_is_dir;
	gboolean old_filename_exists;
	gboolean new_filename_exists;
	gboolean success = TRUE;

	old_filename_is_dir = g_file_test (old_filename, G_FILE_TEST_IS_DIR);
	old_filename_exists = g_file_test (old_filename, G_FILE_TEST_EXISTS);
	new_filename_exists = g_file_test (new_filename, G_FILE_TEST_EXISTS);

	if (!old_filename_exists)
		return TRUE;

	g_print ("  mv %s %s\n", old_filename, new_filename);

	/* It's safe to go ahead and move directories because rename()
	 * will fail if the new directory already exists with content.
	 * With regular files we have to be careful not to overwrite
	 * new files with old files. */
	if (old_filename_is_dir || !new_filename_exists) {
		if (g_rename (old_filename, new_filename) < 0) {
			g_printerr ("  FAILED: %s\n", g_strerror (errno));
			success = FALSE;
		}
	} else {
		g_printerr ("  FAILED: Destination file already exists\n");
		success = FALSE;
	}

	return success;
}

static gboolean
data_book_migrate_rmdir (const gchar *dirname)
{
	GDir *dir = NULL;
	gboolean success = TRUE;

	if (g_file_test (dirname, G_FILE_TEST_IS_DIR)) {
		g_print ("  rmdir %s\n", dirname);
		if (g_rmdir (dirname) < 0) {
			g_printerr ("  FAILED: %s", g_strerror (errno));
			if (errno == ENOTEMPTY) {
				dir = g_dir_open (dirname, 0, NULL);
				g_printerr (" (contents follows)");
			}
			g_printerr ("\n");
			success = FALSE;
		}
	}

	/* List the directory's contents to aid debugging. */
	if (dir != NULL) {
		const gchar *basename;

		/* Align the filenames beneath the error message. */
		while ((basename = g_dir_read_name (dir)) != NULL)
			g_print ("          %s\n", basename);

		g_dir_close (dir);
	}

	return success;
}

static void
data_book_migrate_process_corrections (GHashTable *corrections)
{
	GHashTableIter iter;
	gpointer old_filename;
	gpointer new_filename;

	g_hash_table_iter_init (&iter, corrections);

	while (g_hash_table_iter_next (&iter, &old_filename, &new_filename)) {
		data_book_migrate_rename (old_filename, new_filename);
		g_hash_table_iter_remove (&iter);
	}
}

static gboolean
data_book_migrate_move_contents (const gchar *src_directory,
                                 const gchar *dst_directory)
{
	GDir *dir;
	GHashTable *corrections;
	const gchar *basename;

	dir = g_dir_open (src_directory, 0, NULL);
	if (dir == NULL)
		return FALSE;

	/* This is to avoid renaming files while we're iterating over the
	 * directory.  POSIX says the outcome of that is unspecified. */
	corrections = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);

	g_mkdir_with_parents (dst_directory, 0700);

	while ((basename = g_dir_read_name (dir)) != NULL) {
		gchar *old_filename;
		gchar *new_filename;

		old_filename = g_build_filename (src_directory, basename, NULL);
		new_filename = g_build_filename (dst_directory, basename, NULL);

		g_hash_table_insert (corrections, old_filename, new_filename);
	}

	g_dir_close (dir);

	data_book_migrate_process_corrections (corrections);
	g_hash_table_destroy (corrections);

	/* It's tempting to want to remove the source directory here.
	 * Don't.  We might be iterating over the source directory's
	 * parent directory, and removing the source directory would
	 * screw up the iteration. */

	return TRUE;
}

static void
data_book_migrate_fix_groupwise_bug (const gchar *old_base_dir)
{
	GDir *dir;
	GHashTable *corrections;
	const gchar *basename;
	gchar *old_data_dir;
	gchar *old_cache_dir;

	/* The groupwise backend mistakenly put its addressbook
	 * cache files in ~/.evolution/addressbook instead of
	 * ~/.evolution/cache/addressbook.  Fix that before
	 * we migrate the cache directory. */

	old_data_dir = g_build_filename (old_base_dir, "addressbook", NULL);
	old_cache_dir = g_build_filename (old_base_dir, "cache", "addressbook", NULL);

	dir = g_dir_open (old_data_dir, 0, NULL);
	if (dir == NULL)
		goto exit;

	/* This is to avoid renaming files while we're iterating over the
	 * directory.  POSIX says the outcome of that is unspecified. */
	corrections = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);

	while ((basename = g_dir_read_name (dir)) != NULL) {
		gchar *old_filename;
		gchar *new_filename;

		if (!g_str_has_prefix (basename, "groupwise___"))
			continue;

		old_filename = g_build_filename (old_data_dir, basename, NULL);
		new_filename = g_build_filename (old_cache_dir, basename, NULL);

		g_hash_table_insert (corrections, old_filename, new_filename);
	}

	g_dir_close (dir);

	data_book_migrate_process_corrections (corrections);
	g_hash_table_destroy (corrections);

exit:
	g_free (old_data_dir);
	g_free (old_cache_dir);
}

static void
data_book_migrate_to_user_cache_dir (const gchar *old_base_dir)
{
	const gchar *new_cache_dir;
	gchar *old_cache_dir;
	gchar *src_directory;
	gchar *dst_directory;

	old_cache_dir = g_build_filename (old_base_dir, "cache", NULL);
	new_cache_dir = e_get_user_cache_dir ();

	g_print ("Migrating cached backend data\n");

	/* We don't want to move the source directory directly because the
	 * destination directory may already exist with content.  Instead
	 * we want to merge the content of the source directory into the
	 * destination directory.
	 *
	 * For example, given:
	 *
	 *    $(src_directory)/A   and   $(dst_directory)/B
	 *    $(src_directory)/C
	 *
	 * we want to end up with:
	 *
	 *    $(dst_directory)/A
	 *    $(dst_directory)/B
	 *    $(dst_directory)/C
	 *
	 * Any name collisions will be left in the source directory.
	 */

	src_directory = g_build_filename (old_cache_dir, "addressbook", NULL);
	dst_directory = g_build_filename (new_cache_dir, "addressbook", NULL);

	data_book_migrate_move_contents (src_directory, dst_directory);
	data_book_migrate_rmdir (src_directory);

	g_free (src_directory);
	g_free (dst_directory);

	/* Try to remove the old cache directory.  Good chance this will
	 * fail on the first try, since Evolution puts stuff here too. */
	data_book_migrate_rmdir (old_cache_dir);

	g_free (old_cache_dir);
}

static void
data_book_migrate_to_user_data_dir (const gchar *old_base_dir)
{
	const gchar *new_data_dir;
	gchar *src_directory;
	gchar *dst_directory;

	new_data_dir = e_get_user_data_dir ();

	g_print ("Migrating local backend data\n");

	/* We don't want to move the source directory directly because the
	 * destination directory may already exist with content.  Instead
	 * we want to merge the content of the source directory into the
	 * destination directory.
	 *
	 * For example, given:
	 *
	 *    $(src_directory)/A   and   $(dst_directory)/B
	 *    $(src_directory)/C
	 *
	 * we want to end up with:
	 *
	 *    $(dst_directory)/A
	 *    $(dst_directory)/B
	 *    $(dst_directory)/C
	 *
	 * Any name collisions will be left in the source directory.
	 */

	src_directory = g_build_filename (old_base_dir, "addressbook", "local", NULL);
	dst_directory = g_build_filename (new_data_dir, "addressbook", NULL);

	data_book_migrate_move_contents (src_directory, dst_directory);
	data_book_migrate_rmdir (src_directory);

	g_free (src_directory);
	g_free (dst_directory);
}

void
e_data_book_migrate (void)
{
	const gchar *home_dir;
	gchar *old_base_dir;

	/* XXX This blocks, but it's all just local directory
	 *     renames so it should be nearly instantaneous. */

	home_dir = g_get_home_dir ();
	old_base_dir = g_build_filename (home_dir, ".evolution", NULL);

	/* Is there even anything to migrate? */
	if (!g_file_test (old_base_dir, G_FILE_TEST_IS_DIR))
		goto exit;

	data_book_migrate_fix_groupwise_bug (old_base_dir);

	data_book_migrate_to_user_cache_dir (old_base_dir);
	data_book_migrate_to_user_data_dir (old_base_dir);

	/* Try to remove the old base directory.  Good chance this will
	 * fail on the first try, since Evolution puts stuff here too. */
	data_book_migrate_rmdir (old_base_dir);

exit:
	g_free (old_base_dir);
}

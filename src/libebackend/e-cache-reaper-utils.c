/*
 * e-cache-reaper-utils.c
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

#include "e-cache-reaper-utils.h"

#include <libedataserver/libedataserver.h>

#define REAPING_DIRECTORY_NAME ".reaping"

/* Helper for e_reap_trash_directory() */
static void
reap_trash_directory_thread (GTask *task,
                             gpointer source_object,
                             gpointer task_data,
                             GCancellable *cancellable)
{
	gint expiry_in_days = GPOINTER_TO_INT (task_data);
	GError *error = NULL;

	if (e_reap_trash_directory_sync (
		G_FILE (source_object),
		expiry_in_days,
		cancellable, &error)) {
		g_task_return_boolean (task, TRUE);
	} else {
		g_task_return_error (task, g_steal_pointer (&error));
	}
}

gboolean
e_reap_trash_directory_sync (GFile *trash_directory,
                             gint expiry_in_days,
                             GCancellable *cancellable,
                             GError **error)
{
	GFileEnumerator *file_enumerator;
	GQueue directories = G_QUEUE_INIT;
	GFile *reaping_directory;
	GFileInfo *file_info;
	const gchar *attributes;
	gboolean success = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (G_IS_FILE (trash_directory), FALSE);
	g_return_val_if_fail (expiry_in_days > 0, FALSE);

	reaping_directory = g_file_get_child (
		trash_directory, REAPING_DIRECTORY_NAME);

	attributes =
		G_FILE_ATTRIBUTE_STANDARD_NAME ","
		G_FILE_ATTRIBUTE_STANDARD_TYPE ","
		G_FILE_ATTRIBUTE_TIME_MODIFIED;

	file_enumerator = g_file_enumerate_children (
		trash_directory, attributes,
		G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
		cancellable, error);

	if (file_enumerator == NULL)
		return FALSE;

	file_info = g_file_enumerator_next_file (
		file_enumerator, cancellable, &local_error);

	while (file_info != NULL) {
		GFileType file_type;
		GDateTime *dt_modification;
		const gchar *name;
		gboolean reap_it;
		gint days_old = 0;

		name = g_file_info_get_name (file_info);
		file_type = g_file_info_get_file_type (file_info);
		dt_modification = g_file_info_get_modification_date_time (file_info);

		/* Calculate how many days ago the file was modified. */
		if (dt_modification) {
			GDateTime *dt_now;

			dt_now = g_date_time_new_now_utc ();
			days_old = g_date_time_difference (dt_now, dt_modification) / G_TIME_SPAN_DAY;
			g_date_time_unref (dt_now);
			g_date_time_unref (dt_modification);
		}

		reap_it =
			(file_type == G_FILE_TYPE_DIRECTORY) &&
			(days_old >= expiry_in_days);

		if (reap_it) {
			GFile *child;

			child = g_file_get_child (trash_directory, name);

			/* If we find an unfinished reaping directory, put
			 * it on the head of the queue so we reap it first. */
			if (g_file_equal (child, reaping_directory))
				g_queue_push_head (&directories, child);
			else
				g_queue_push_tail (&directories, child);
		}

		g_object_unref (file_info);

		file_info = g_file_enumerator_next_file (
			file_enumerator, cancellable, &local_error);
	}

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		success = FALSE;
	}

	g_object_unref (file_enumerator);

	/* Now delete the directories we've queued up. */
	while (success && !g_queue_is_empty (&directories)) {
		GFile *directory;

		directory = g_queue_pop_head (&directories);

		/* First we rename the directory to prevent it
		 * from being recovered while being deleted. */
		if (!g_file_equal (directory, reaping_directory))
			success = g_file_move (
				directory, reaping_directory,
				G_FILE_COPY_NONE, cancellable,
				NULL, NULL, error);

		if (success)
			success = e_file_recursive_delete_sync (
				reaping_directory, cancellable, error);

		g_object_unref (directory);
	}

	/* Flush the queue in case we aborted on an error. */
	while (!g_queue_is_empty (&directories))
		g_object_unref (g_queue_pop_head (&directories));

	g_object_unref (reaping_directory);

	return success;
}

void
e_reap_trash_directory (GFile *trash_directory,
                        gint expiry_in_days,
                        gint io_priority,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	GTask *task;

	g_return_if_fail (G_IS_FILE (trash_directory));
	g_return_if_fail (expiry_in_days > 0);

	task = g_task_new (trash_directory, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_reap_trash_directory);
	g_task_set_check_cancellable (task, TRUE);
	g_task_set_task_data (task, GINT_TO_POINTER (expiry_in_days), NULL);
	g_task_set_priority (task, io_priority);

	g_task_run_in_thread (task, reap_trash_directory_thread);

	g_object_unref (task);
}

gboolean
e_reap_trash_directory_finish (GFile *trash_directory,
                               GAsyncResult *result,
                               GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, trash_directory), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_reap_trash_directory), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}


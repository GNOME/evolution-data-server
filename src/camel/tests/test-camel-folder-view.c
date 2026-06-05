/*
 * SPDX-FileCopyrightText: (C) 2026 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <glib.h>

#include "camel/camel.h"

#include "camel-test.h"

/* Message IDs for threading tests (arbitrary non-zero values) */
#define MSG_ID_A  100
#define MSG_ID_B  200
#define MSG_ID_C  300
#define MSG_ID_D  400
#define MSG_ID_E  500
#define MSG_ID_F  600
#define MSG_ID_G  700

static CamelFolder *
create_test_folder (CamelStore *store,
		    const gchar *folder_name)
{
	CamelFolder *folder;
	GError *local_error = NULL;

	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (folder);

	return folder;
}

static void
add_flat_messages (CamelFolder *folder)
{
	gchar *part_a = test_build_part_string (MSG_ID_A, NULL, 0);
	gchar *part_b = test_build_part_string (MSG_ID_B, NULL, 0);
	gchar *part_c = test_build_part_string (MSG_ID_C, NULL, 0);
	gchar *part_d = test_build_part_string (MSG_ID_D, NULL, 0);
	gchar *part_e = test_build_part_string (MSG_ID_E, NULL, 0);
	gchar *part_f = test_build_part_string (MSG_ID_F, NULL, 0);
	gchar *part_g = test_build_part_string (MSG_ID_G, NULL, 0);

	test_add_messages (folder,
		"uid", "a", "subject", "Alpha", "from", "alice@test.com",
		"dsent", (gint64) 1000000, "dreceived", (gint64) 1000010,
		"size", (guint32) 100, "part", part_a, "",
		"uid", "b", "subject", "Beta", "from", "bob@test.com",
		"dsent", (gint64) 1000100, "dreceived", (gint64) 1000110,
		"size", (guint32) 200, "part", part_b, "",
		"uid", "c", "subject", "Charlie", "from", "charlie@test.com",
		"dsent", (gint64) 1000200, "dreceived", (gint64) 1000210,
		"size", (guint32) 50, "part", part_c, "",
		"uid", "d", "subject", "Delta", "from", "dave@test.com",
		"dsent", (gint64) 1000300, "dreceived", (gint64) 1000310,
		"size", (guint32) 500, "part", part_d, "",
		"uid", "e", "subject", "Echo", "from", "eve@test.com",
		"dsent", (gint64) 1000400, "dreceived", (gint64) 1000410,
		"size", (guint32) 150, "part", part_e, "",
		"uid", "f", "subject", "Foxtrot", "from", "frank@test.com",
		"dsent", (gint64) 1000500, "dreceived", (gint64) 1000510,
		"size", (guint32) 300, "part", part_f, "",
		"uid", "g", "subject", "Golf", "from", "gina@test.com",
		"dsent", (gint64) 1000600, "dreceived", (gint64) 1000610,
		"size", (guint32) 75, "part", part_g, "",
		NULL);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);
	g_free (part_d);
	g_free (part_e);
	g_free (part_f);
	g_free (part_g);
}

static void
add_threaded_messages (CamelFolder *folder)
{
	guint64 refs_b[] = { MSG_ID_A };
	guint64 refs_c[] = { MSG_ID_B, MSG_ID_A };
	guint64 refs_e[] = { MSG_ID_D };

	gchar *part_a = test_build_part_string (MSG_ID_A, NULL, 0);
	gchar *part_b = test_build_part_string (MSG_ID_B, refs_b, 1);
	gchar *part_c = test_build_part_string (MSG_ID_C, refs_c, 2);
	gchar *part_d = test_build_part_string (MSG_ID_D, NULL, 0);
	gchar *part_e = test_build_part_string (MSG_ID_E, refs_e, 1);
	gchar *part_f = test_build_part_string (MSG_ID_F, NULL, 0);

	/* Thread 1: A -> B -> C (A is root, B replies to A, C replies to B referencing A)
	 * Thread 2: D -> E (D is root, E replies to D)
	 * Thread 3: F (standalone) */
	test_add_messages (folder,
		"uid", "a", "subject", "Thread one", "from", "alice@test.com",
		"dsent", (gint64) 1000000, "dreceived", (gint64) 1000010,
		"part", part_a, "",
		"uid", "b", "subject", "Re: Thread one", "from", "bob@test.com",
		"dsent", (gint64) 1000100, "dreceived", (gint64) 1000110,
		"part", part_b, "",
		"uid", "c", "subject", "Re: Thread one", "from", "charlie@test.com",
		"dsent", (gint64) 1000200, "dreceived", (gint64) 1000210,
		"part", part_c, "",
		"uid", "d", "subject", "Thread two", "from", "dave@test.com",
		"dsent", (gint64) 1000300, "dreceived", (gint64) 1000310,
		"part", part_d, "",
		"uid", "e", "subject", "Re: Thread two", "from", "eve@test.com",
		"dsent", (gint64) 1000400, "dreceived", (gint64) 1000410,
		"part", part_e, "",
		"uid", "f", "subject", "Standalone", "from", "frank@test.com",
		"dsent", (gint64) 1000500, "dreceived", (gint64) 1000510,
		"part", part_f, "",
		NULL);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);
	g_free (part_d);
	g_free (part_e);
	g_free (part_f);
}

static void
test_folder_view_flat_no_filter (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderViewRow *first;
	CamelFolderViewRow *last;
	CamelFolderViewRow *info;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	/* Verify sorted by date_sent ascending */
	first = camel_folder_view_get_row (view, 0);

	last = camel_folder_view_get_row (view, 6);

	g_assert_nonnull (first);
	g_assert_nonnull (last);
	g_assert_cmpstr (camel_folder_view_row_get_uid (first), ==, "a");
	g_assert_cmpstr (camel_folder_view_row_get_uid (last), ==, "g");

	/* All should be depth 0 */
	info = camel_folder_view_get_row (view, 3);

	g_assert_cmpuint (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info)), ==, 0);
	g_assert_false (camel_folder_view_is_expandable (view, camel_folder_view_row_get_uid (info)));

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_flat_with_filter (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderViewRow *info;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_filter (view, "(match-all (header-contains \"Subject\" \"Alpha\"))");
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	/* Only "Alpha" should match */
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 1);

	info = camel_folder_view_get_row (view, 0);

	g_assert_nonnull (info);
	g_assert_cmpstr (camel_folder_view_row_get_subject (info), ==, "Alpha");

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_threaded (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderViewRow *info_b;
	CamelFolderViewRow *info_a;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	add_threaded_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	/* 6 messages, all expanded by default */
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 6);

	/* Find message "b" - should be at depth 1 (child of "a") */
	info_b = camel_folder_view_get_row (view, 1);

	if (info_b && g_strcmp0 (camel_folder_view_row_get_uid (info_b), "b") == 0)
		g_assert_cmpuint (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info_b)), ==, 1);

	/* Root "a" should be expandable */
	info_a = camel_folder_view_get_row (view, 0);

	if (info_a && g_strcmp0 (camel_folder_view_row_get_uid (info_a), "a") == 0) {
		g_assert_true (camel_folder_view_is_expandable (view, camel_folder_view_row_get_uid (info_a)));
		g_assert_true (camel_folder_view_get_expanded (view, camel_folder_view_row_get_uid (info_a)));
	}


	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_flat_threads (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	guint ii;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_threaded_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FLAT);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 6);

	/* No message should have depth > 1 */
	for (ii = 0; ii < camel_folder_view_get_row_count (view); ii++) {
		CamelFolderViewRow *info = camel_folder_view_get_row (view, ii);
		if (info) {
			g_assert_cmpuint (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info)), <=, 1);
		}
	}

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_thread_subject (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	gchar *part_1;
	gchar *part_2;
	gboolean found_child;
	guint ii;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	/* Messages with same subject but no References headers */
	part_1 = test_build_part_string (MSG_ID_A, NULL, 0);
	part_2 = test_build_part_string (MSG_ID_B, NULL, 0);

	test_add_messages (folder,
		"uid", "s1", "subject", "Important topic", "from", "alice@test.com",
		"dsent", (gint64) 1000000, "dreceived", (gint64) 1000010,
		"part", part_1, "",
		"uid", "s2", "subject", "Re: Important topic", "from", "bob@test.com",
		"dsent", (gint64) 1000100, "dreceived", (gint64) 1000110,
		"part", part_2, "",
		NULL);

	g_free (part_1);
	g_free (part_2);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_thread_subject (view, TRUE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	/* With thread-subject enabled, the "Re:" message should be grouped
	 * under the original - meaning at least one message has depth > 0 */
	found_child = FALSE;

	for (ii = 0; ii < camel_folder_view_get_row_count (view); ii++) {
		CamelFolderViewRow *info = camel_folder_view_get_row (view, ii);
		if (info) {
			if (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info)) > 0)
				found_child = TRUE;
		}
	}

	g_assert_true (found_child);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_sort (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderViewRow *first;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	/* Ascending by date - first should be "a" (earliest) */
	first = camel_folder_view_get_row (view, 0);

	g_assert_nonnull (first);
	g_assert_cmpstr (camel_folder_view_row_get_uid (first), ==, "a");

	/* Change to descending */
	camel_folder_view_freeze (view);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_DESCENDING);
	camel_folder_view_thaw (view);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	first = camel_folder_view_get_row (view, 0);

	g_assert_nonnull (first);
	g_assert_cmpstr (camel_folder_view_row_get_uid (first), ==, "g");

	/* Sort by subject ascending */
	camel_folder_view_freeze (view);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_SUBJECT, CAMEL_SORT_ASCENDING);
	camel_folder_view_thaw (view);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	first = camel_folder_view_get_row (view, 0);

	g_assert_nonnull (first);
	g_assert_cmpstr (camel_folder_view_row_get_subject (first), ==, "Alpha");

	/* Clear sort - falls back to UID order */
	camel_folder_view_freeze (view);
	camel_folder_view_clear_sort (view);
	camel_folder_view_thaw (view);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_sort_non_db_column (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderViewRow *row;
	gchar *part_a = test_build_part_string (MSG_ID_A, NULL, 0);
	gchar *part_b = test_build_part_string (MSG_ID_B, NULL, 0);
	gchar *part_c = test_build_part_string (MSG_ID_C, NULL, 0);

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	test_add_messages (folder,
		"uid", "a", "subject", "Alpha",
		"dsent", (gint64) 1000, "dreceived", (gint64) 1000,
		"part", part_a, "usertags", "1 5-score 2-30",
		"",
		"uid", "b", "subject", "Beta",
		"dsent", (gint64) 2000, "dreceived", (gint64) 2000,
		"part", part_b, "usertags", "1 5-score 2-10",
		"",
		"uid", "c", "subject", "Charlie",
		"dsent", (gint64) 3000, "dreceived", (gint64) 3000,
		"part", part_c, "usertags", "1 5-score 2-50",
		"",
		NULL);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_SCORE, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 3);

	/* Ascending by score: b(10), a(30), c(50) */
	row = camel_folder_view_get_row (view, 0);
	g_assert_cmpstr (camel_folder_view_row_get_uid (row), ==, "b");

	row = camel_folder_view_get_row (view, 1);
	g_assert_cmpstr (camel_folder_view_row_get_uid (row), ==, "a");

	row = camel_folder_view_get_row (view, 2);
	g_assert_cmpstr (camel_folder_view_row_get_uid (row), ==, "c");

	/* Descending by score: c(50), a(30), b(10) */
	camel_folder_view_freeze (view);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_SCORE, CAMEL_SORT_DESCENDING);
	camel_folder_view_thaw (view);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	row = camel_folder_view_get_row (view, 0);
	g_assert_cmpstr (camel_folder_view_row_get_uid (row), ==, "c");

	row = camel_folder_view_get_row (view, 1);
	g_assert_cmpstr (camel_folder_view_row_get_uid (row), ==, "a");

	row = camel_folder_view_get_row (view, 2);
	g_assert_cmpstr (camel_folder_view_row_get_uid (row), ==, "b");

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_expand_collapse (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderViewRow *info_a;
	gchar *state;
	guint expanded_count, collapsed_count;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_threaded_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	expanded_count = camel_folder_view_get_row_count (view);
	g_assert_cmpuint (expanded_count, ==, 6);

	/* Collapse root "a" - should hide its children */
	info_a = camel_folder_view_get_row (view, 0);

	if (info_a && g_strcmp0 (camel_folder_view_row_get_uid (info_a), "a") == 0) {
		camel_folder_view_set_expanded (view, camel_folder_view_row_get_uid (info_a), FALSE);

		collapsed_count = camel_folder_view_get_row_count (view);
		g_assert_cmpuint (collapsed_count, <, expanded_count);

		/* Re-expand */
		camel_folder_view_set_expanded (view, camel_folder_view_row_get_uid (info_a), TRUE);
		g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, expanded_count);
	}

	/* Save/load expand state round-trip */
	info_a = camel_folder_view_get_row (view, 0);

	if (info_a)
		camel_folder_view_set_expanded (view, camel_folder_view_row_get_uid (info_a), FALSE);

	state = camel_folder_view_save_expand_state (view);
	g_assert_nonnull (state);

	/* Re-expand, then reload saved state */
	if (info_a)
		camel_folder_view_set_expanded (view, camel_folder_view_row_get_uid (info_a), TRUE);

	camel_folder_view_load_expand_state (view, state);

	/* After loading, "a" should be collapsed again */
	if (info_a)
		g_assert_false (camel_folder_view_get_expanded (view, camel_folder_view_row_get_uid (info_a)));

	g_free (state);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

typedef struct _FolderChangedData {
	CamelFolderView *view;
	CamelFolderViewGeneration *old_generation;
	gboolean done;
	guint timeout_id;
} FolderChangedData;

static gpointer
folder_changed_worker_thread (gpointer user_data)
{
	FolderChangedData *data = user_data;

	camel_folder_view_process_pending_changes_sync (data->view, data->old_generation, NULL, NULL);
	g_clear_pointer (&data->old_generation, camel_folder_view_generation_unref);
	data->done = TRUE;
	g_main_context_wakeup (NULL);

	return NULL;
}

static gboolean
folder_changed_spawn_worker_cb (gpointer user_data)
{
	FolderChangedData *data = user_data;
	GThread *thread;

	data->timeout_id = 0;
	data->old_generation = camel_folder_view_ref_current_generation (data->view);
	thread = g_thread_new (NULL, folder_changed_worker_thread, data);
	g_thread_unref (thread);

	return G_SOURCE_REMOVE;
}

static void
view_folder_changed_cb (CamelFolderView *view,
			gpointer user_data)
{
	FolderChangedData *data = user_data;

	if (data->timeout_id)
		g_source_remove (data->timeout_id);
	data->timeout_id = g_timeout_add (10, folder_changed_spawn_worker_cb, data);
}

static void
wait_for_folder_changed (CamelFolderView *view)
{
	FolderChangedData data = { 0, };
	gulong handler_id;
	guint timeout_id;

	data.view = view;

	handler_id = g_signal_connect (view, "folder-changed",
		G_CALLBACK (view_folder_changed_cb), &data);

	timeout_id = g_timeout_add_seconds (DELAY_TIMEOUT_SECONDS,
		test_util_abort_on_timeout_cb, NULL);

	while (!data.done) {
		g_main_context_iteration (NULL, TRUE);
	}

	g_assert_true (g_source_remove (timeout_id));
	g_signal_handler_disconnect (view, handler_id);
}

static void
test_folder_view_folder_changed_add (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	gchar *part_h;
	guint count_before;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	count_before = camel_folder_view_get_row_count (view);
	g_assert_cmpuint (count_before, ==, 7);

	/* Add a new message */
	part_h = test_build_part_string (800, NULL, 0);

	test_add_messages (folder,
		"uid", "h", "subject", "Hotel", "from", "harry@test.com",
		"dsent", (gint64) 1000700, "dreceived", (gint64) 1000710,
		"part", part_h, "",
		NULL);

	g_free (part_h);

	/* Emit folder changed and wait for async delivery */
	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "h");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, count_before + 1);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_folder_changed_remove (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	guint count_before;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	count_before = camel_folder_view_get_row_count (view);

	/* Remove message "a" from summary */
	camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), "a");

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_remove_uid (changes, "a");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, count_before - 1);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

typedef struct {
	guint first;
	guint last;
} RowRange;

static void
capture_row_range_cb (CamelFolderView *view,
		      guint first_row,
		      guint last_row,
		      gpointer user_data)
{
	GArray *ranges = user_data;
	RowRange range = { first_row, last_row };

	g_array_append_val (ranges, range);
}

static void
assert_row_ranges (GArray *ranges,
		   const RowRange *expected,
		   guint n_expected,
		   const gchar *label)
{
	guint ii;

	if (ranges->len != n_expected)
		g_error ("%s: expected %u ranges, got %u", label, n_expected, ranges->len);

	for (ii = 0; ii < n_expected; ii++) {
		RowRange actual = g_array_index (ranges, RowRange, ii);

		if (actual.first != expected[ii].first || actual.last != expected[ii].last) {
			g_error ("%s: range %u: expected (%u,%u), got (%u,%u)",
				label, ii, expected[ii].first, expected[ii].last, actual.first, actual.last);
		}
	}
}

static void
test_folder_view_rows_inserted_scattered (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	GArray *inserted_ranges;
	GArray *removed_ranges;
	gulong inserted_id, removed_id;
	gchar *part_bar, *part_fan;
	const RowRange expected_inserted[] = { { 1, 1 }, { 6, 6 } };

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_SUBJECT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	inserted_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	removed_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	inserted_id = g_signal_connect (view, "rows-inserted", G_CALLBACK (capture_row_range_cb), inserted_ranges);
	removed_id = g_signal_connect (view, "rows-removed", G_CALLBACK (capture_row_range_cb), removed_ranges);

	part_bar = test_build_part_string (810, NULL, 0);
	part_fan = test_build_part_string (820, NULL, 0);

	test_add_messages (folder,
		"uid", "bar", "subject", "Bar", "from", "bar@test.com",
		"dsent", (gint64) 1000050, "dreceived", (gint64) 1000050,
		"part", part_bar, "",
		"uid", "fan", "subject", "Fan", "from", "fan@test.com",
		"dsent", (gint64) 1000450, "dreceived", (gint64) 1000450,
		"part", part_fan, "",
		NULL);

	g_free (part_bar);
	g_free (part_fan);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "bar");
	camel_folder_change_info_add_uid (changes, "fan");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 9);
	g_assert_cmpuint (removed_ranges->len, ==, 0);
	assert_row_ranges (inserted_ranges, expected_inserted, G_N_ELEMENTS (expected_inserted), "rows_inserted_scattered");

	g_signal_handler_disconnect (view, inserted_id);
	g_signal_handler_disconnect (view, removed_id);
	g_array_unref (inserted_ranges);
	g_array_unref (removed_ranges);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_rows_removed_scattered (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	GArray *inserted_ranges;
	GArray *removed_ranges;
	gulong inserted_id, removed_id;
	const RowRange expected_removed[] = { { 5, 5 }, { 1, 1 } };

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_SUBJECT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	inserted_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	removed_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	inserted_id = g_signal_connect (view, "rows-inserted", G_CALLBACK (capture_row_range_cb), inserted_ranges);
	removed_id = g_signal_connect (view, "rows-removed", G_CALLBACK (capture_row_range_cb), removed_ranges);

	camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), "b");
	camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), "f");

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_remove_uid (changes, "b");
	camel_folder_change_info_remove_uid (changes, "f");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 5);
	g_assert_cmpuint (inserted_ranges->len, ==, 0);
	assert_row_ranges (removed_ranges, expected_removed, G_N_ELEMENTS (expected_removed), "rows_removed_scattered");

	g_signal_handler_disconnect (view, inserted_id);
	g_signal_handler_disconnect (view, removed_id);
	g_array_unref (inserted_ranges);
	g_array_unref (removed_ranges);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_rows_inserted_at_zero_descending (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	GArray *inserted_ranges;
	gulong inserted_id;
	gchar *part_new;
	const RowRange expected_inserted[] = { { 0, 0 } };

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_DESCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	inserted_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	inserted_id = g_signal_connect (view, "rows-inserted", G_CALLBACK (capture_row_range_cb), inserted_ranges);

	part_new = test_build_part_string (830, NULL, 0);
	test_add_messages (folder,
		"uid", "newest", "subject", "Newest", "from", "newest@test.com",
		"dsent", (gint64) 2000000, "dreceived", (gint64) 2000000,
		"part", part_new, "",
		NULL);
	g_free (part_new);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "newest");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 8);
	assert_row_ranges (inserted_ranges, expected_inserted, G_N_ELEMENTS (expected_inserted), "rows_inserted_at_zero_descending");

	g_signal_handler_disconnect (view, inserted_id);
	g_array_unref (inserted_ranges);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_rows_inserted_contiguous (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	GArray *inserted_ranges;
	GArray *removed_ranges;
	gulong inserted_id, removed_id;
	gchar *part_1, *part_2, *part_3;
	const RowRange expected_inserted[] = { { 1, 3 } };

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_SUBJECT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	inserted_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	removed_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	inserted_id = g_signal_connect (view, "rows-inserted", G_CALLBACK (capture_row_range_cb), inserted_ranges);
	removed_id = g_signal_connect (view, "rows-removed", G_CALLBACK (capture_row_range_cb), removed_ranges);

	part_1 = test_build_part_string (811, NULL, 0);
	part_2 = test_build_part_string (812, NULL, 0);
	part_3 = test_build_part_string (813, NULL, 0);

	test_add_messages (folder,
		"uid", "bar1", "subject", "Bar1", "from", "bar1@test.com",
		"dsent", (gint64) 1000051, "dreceived", (gint64) 1000051,
		"part", part_1, "",
		"uid", "bar2", "subject", "Bar2", "from", "bar2@test.com",
		"dsent", (gint64) 1000052, "dreceived", (gint64) 1000052,
		"part", part_2, "",
		"uid", "bar3", "subject", "Bar3", "from", "bar3@test.com",
		"dsent", (gint64) 1000053, "dreceived", (gint64) 1000053,
		"part", part_3, "",
		NULL);

	g_free (part_1);
	g_free (part_2);
	g_free (part_3);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "bar1");
	camel_folder_change_info_add_uid (changes, "bar2");
	camel_folder_change_info_add_uid (changes, "bar3");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 10);
	g_assert_cmpuint (removed_ranges->len, ==, 0);
	assert_row_ranges (inserted_ranges, expected_inserted, G_N_ELEMENTS (expected_inserted), "rows_inserted_contiguous");

	g_signal_handler_disconnect (view, inserted_id);
	g_signal_handler_disconnect (view, removed_id);
	g_array_unref (inserted_ranges);
	g_array_unref (removed_ranges);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_rows_removed_contiguous (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	GArray *inserted_ranges;
	GArray *removed_ranges;
	gulong inserted_id, removed_id;
	const RowRange expected_removed[] = { { 2, 4 } };

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_SUBJECT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	inserted_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	removed_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	inserted_id = g_signal_connect (view, "rows-inserted", G_CALLBACK (capture_row_range_cb), inserted_ranges);
	removed_id = g_signal_connect (view, "rows-removed", G_CALLBACK (capture_row_range_cb), removed_ranges);

	camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), "c");
	camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), "d");
	camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), "e");

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_remove_uid (changes, "c");
	camel_folder_change_info_remove_uid (changes, "d");
	camel_folder_change_info_remove_uid (changes, "e");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 4);
	g_assert_cmpuint (inserted_ranges->len, ==, 0);
	assert_row_ranges (removed_ranges, expected_removed, G_N_ELEMENTS (expected_removed), "rows_removed_contiguous");

	g_signal_handler_disconnect (view, inserted_id);
	g_signal_handler_disconnect (view, removed_id);
	g_array_unref (inserted_ranges);
	g_array_unref (removed_ranges);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_rows_inserted_mixed (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	GArray *inserted_ranges;
	GArray *removed_ranges;
	gulong inserted_id, removed_id;
	gchar *part_bar, *part_1, *part_2;
	const RowRange expected_inserted[] = { { 1, 1 }, { 5, 6 } };

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_SUBJECT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	inserted_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	removed_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	inserted_id = g_signal_connect (view, "rows-inserted", G_CALLBACK (capture_row_range_cb), inserted_ranges);
	removed_id = g_signal_connect (view, "rows-removed", G_CALLBACK (capture_row_range_cb), removed_ranges);

	/* One isolated insertion ("Bar" between Alpha(0) and Beta(1)) plus
	 * a two-message contiguous block ("Delta1"/"Delta2" between
	 * Delta(3) and Echo(4)) in the same batch - expect one single-row
	 * range and one two-row range, not three single-row ranges. */
	part_bar = test_build_part_string (821, NULL, 0);
	part_1 = test_build_part_string (822, NULL, 0);
	part_2 = test_build_part_string (823, NULL, 0);

	test_add_messages (folder,
		"uid", "bar", "subject", "Bar", "from", "bar@test.com",
		"dsent", (gint64) 1000051, "dreceived", (gint64) 1000051,
		"part", part_bar, "",
		"uid", "delta1", "subject", "Delta1", "from", "delta1@test.com",
		"dsent", (gint64) 1000351, "dreceived", (gint64) 1000351,
		"part", part_1, "",
		"uid", "delta2", "subject", "Delta2", "from", "delta2@test.com",
		"dsent", (gint64) 1000352, "dreceived", (gint64) 1000352,
		"part", part_2, "",
		NULL);

	g_free (part_bar);
	g_free (part_1);
	g_free (part_2);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "bar");
	camel_folder_change_info_add_uid (changes, "delta1");
	camel_folder_change_info_add_uid (changes, "delta2");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 10);
	g_assert_cmpuint (removed_ranges->len, ==, 0);
	assert_row_ranges (inserted_ranges, expected_inserted, G_N_ELEMENTS (expected_inserted), "rows_inserted_mixed");

	g_signal_handler_disconnect (view, inserted_id);
	g_signal_handler_disconnect (view, removed_id);
	g_array_unref (inserted_ranges);
	g_array_unref (removed_ranges);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_rows_insert_remove_change_combined (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	GArray *inserted_ranges;
	GArray *removed_ranges;
	GArray *changed_ranges;
	gulong inserted_id, removed_id, changed_id;
	gchar *part_bar;
	const RowRange expected_removed[] = { { 5, 5 } };
	const RowRange expected_inserted[] = { { 1, 1 } };
	const RowRange expected_changed[] = { { 4, 4 } };

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_SUBJECT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	inserted_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	removed_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	changed_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	inserted_id = g_signal_connect (view, "rows-inserted", G_CALLBACK (capture_row_range_cb), inserted_ranges);
	removed_id = g_signal_connect (view, "rows-removed", G_CALLBACK (capture_row_range_cb), removed_ranges);
	changed_id = g_signal_connect (view, "rows-changed", G_CALLBACK (capture_row_range_cb), changed_ranges);

	/* One batch: add "bar" (lands at index 1), remove "f"/Foxtrot
	 * (index 5), and mark "d"/Delta (an unrelated survivor) changed,
	 * all in the same folder-changed notification. */
	part_bar = test_build_part_string (841, NULL, 0);
	test_add_messages (folder,
		"uid", "bar", "subject", "Bar", "from", "bar@test.com",
		"dsent", (gint64) 1000051, "dreceived", (gint64) 1000051,
		"part", part_bar, "",
		NULL);
	g_free (part_bar);

	camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), "f");

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "bar");
	camel_folder_change_info_remove_uid (changes, "f");
	camel_folder_change_info_change_uid (changes, "d");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	/* 7 - 1 (removed) + 1 (added) = 7 */
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);
	assert_row_ranges (removed_ranges, expected_removed, G_N_ELEMENTS (expected_removed), "insert_remove_change_combined (removed)");
	assert_row_ranges (inserted_ranges, expected_inserted, G_N_ELEMENTS (expected_inserted), "insert_remove_change_combined (inserted)");

	/* "d"/Delta is unaffected by the add/remove themselves, but "bar"
	 * landed above it, shifting it from index 3 to index 4 - the
	 * change notification must use its current (post-shift) position. */
	assert_row_ranges (changed_ranges, expected_changed, G_N_ELEMENTS (expected_changed), "insert_remove_change_combined (changed)");

	g_signal_handler_disconnect (view, inserted_id);
	g_signal_handler_disconnect (view, removed_id);
	g_signal_handler_disconnect (view, changed_id);
	g_array_unref (inserted_ranges);
	g_array_unref (removed_ranges);
	g_array_unref (changed_ranges);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_rows_inserted_reverse_threading (void)
{
	#define RT_ID_A 91000
	#define RT_ID_B 92000
	#define RT_ID_C 93000
	#define RT_ID_D 94000
	#define RT_ID_G 97000
	#define RT_ID_H 98000
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	GArray *inserted_ranges;
	GArray *removed_ranges;
	gulong inserted_id, removed_id;
	guint64 refs_d[] = { RT_ID_C, RT_ID_A };
	guint64 refs_h[] = { RT_ID_G, RT_ID_C };
	guint64 refs_c[] = { RT_ID_B, RT_ID_A };
	gchar *part_b, *part_d, *part_h, *part_c;
	const RowRange expected_removed[] = { { 1, 2 } };
	const RowRange expected_inserted[] = { { 1, 3 } };

	store = test_store_new ();
	folder = create_test_folder (store, "f2");

	part_b = test_build_part_string (RT_ID_B, NULL, 0);
	part_d = test_build_part_string (RT_ID_D, refs_d, 2);
	part_h = test_build_part_string (RT_ID_H, refs_h, 2);

	/* Step 1: b, d, h - d and h reference an absent common ancestor and
	 * end up flattened as siblings under b (the oldest). */
	test_add_messages (folder,
		"uid", "b", "subject", "B", "from", "b@test", "dsent", (gint64) 2000, "part", part_b, "",
		"uid", "d", "subject", "D", "from", "d@test", "dsent", (gint64) 4000, "part", part_d, "",
		"uid", "h", "subject", "H", "from", "h@test", "dsent", (gint64) 8000, "part", part_h, "",
		NULL);

	g_free (part_b);
	g_free (part_d);
	g_free (part_h);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	/* b(0), d(1), h(2) - flat, since their real parent (c) is missing */
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 3);

	inserted_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	removed_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	inserted_id = g_signal_connect (view, "rows-inserted", G_CALLBACK (capture_row_range_cb), inserted_ranges);
	removed_id = g_signal_connect (view, "rows-removed", G_CALLBACK (capture_row_range_cb), removed_ranges);

	/* Add c - both d and h resolve their nearer parent to c and get
	 * reparented out from under b: a "reverse threading" reposition. */
	part_c = test_build_part_string (RT_ID_C, refs_c, 2);
	test_add_messages (folder,
		"uid", "c", "subject", "C", "from", "c@test", "dsent", (gint64) 3000, "part", part_c, "",
		NULL);
	g_free (part_c);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "c");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	/* b(0), c(1), d(2), h(2)/index3 - c is new; d and h moved down */
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 4);
	assert_row_ranges (removed_ranges, expected_removed, G_N_ELEMENTS (expected_removed), "rows_inserted_reverse_threading (removed)");
	assert_row_ranges (inserted_ranges, expected_inserted, G_N_ELEMENTS (expected_inserted), "rows_inserted_reverse_threading (inserted)");

	g_signal_handler_disconnect (view, inserted_id);
	g_signal_handler_disconnect (view, removed_id);
	g_array_unref (inserted_ranges);
	g_array_unref (removed_ranges);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
	#undef RT_ID_A
	#undef RT_ID_B
	#undef RT_ID_C
	#undef RT_ID_D
	#undef RT_ID_G
	#undef RT_ID_H
}

static void
test_folder_view_child_arrives_before_parent (void)
{
	#define CB_ID_P 95100
	#define CB_ID_C 95200
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	CamelFolderViewRow *info;
	guint64 refs_c[] = { CB_ID_P };
	gchar *part_c, *part_p;

	store = test_store_new ();
	folder = create_test_folder (store, "f2");

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 0);

	/* "c" arrives first, referencing its not-yet-existing parent "p". */
	part_c = test_build_part_string (CB_ID_C, refs_c, 1);
	test_add_messages (folder,
		"uid", "c", "subject", "Child", "from", "c@test", "dsent", (gint64) 2000, "part", part_c, "",
		NULL);
	g_free (part_c);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "c");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 1);

	/* "p" arrives later, in its own separate change notification. */
	part_p = test_build_part_string (CB_ID_P, NULL, 0);
	test_add_messages (folder,
		"uid", "p", "subject", "Parent", "from", "p@test", "dsent", (gint64) 1000, "part", part_p, "",
		NULL);
	g_free (part_p);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "p");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	/* "c" should now be threaded as a child of "p", not sitting as a
	 * separate root at depth 0. */
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 2);

	info = camel_folder_view_get_row (view, 0);
	g_assert_nonnull (info);
	g_assert_cmpstr (camel_folder_view_row_get_uid (info), ==, "p");
	g_assert_cmpuint (camel_folder_view_get_depth (view, "p"), ==, 0);
	g_assert_true (camel_folder_view_is_expandable (view, "p"));

	g_assert_cmpuint (camel_folder_view_get_depth (view, "c"), ==, 1);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
	#undef CB_ID_P
	#undef CB_ID_C
}

static void
test_folder_view_child_before_parent_via_grandparent (void)
{
	#define CB_ID_ROOT 95300
	#define CB_ID_P 95400
	#define CB_ID_C 95500
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	guint64 refs_c[] = { CB_ID_P, CB_ID_ROOT };
	guint64 refs_p[] = { CB_ID_ROOT };
	gchar *part_root, *part_c, *part_p;

	store = test_store_new ();
	folder = create_test_folder (store, "f2");

	part_root = test_build_part_string (CB_ID_ROOT, NULL, 0);
	test_add_messages (folder,
		"uid", "root", "subject", "Root", "from", "root@test", "dsent", (gint64) 1000, "part", part_root, "",
		NULL);
	g_free (part_root);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 1);

	/* "c" arrives first, referencing its immediate (not-yet-existing)
	 * parent "p" and, beyond it, the already-existing "root". */
	part_c = test_build_part_string (CB_ID_C, refs_c, 2);
	test_add_messages (folder,
		"uid", "c", "subject", "Re: Root", "from", "c@test", "dsent", (gint64) 3000, "part", part_c, "",
		NULL);
	g_free (part_c);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "c");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	/* No placeholder for "p" survives: "c" is pruned up to be a direct
	 * child of "root" until "p" actually shows up. */
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 2);
	g_assert_cmpuint (camel_folder_view_get_depth (view, "c"), ==, 1);

	/* "p" arrives later, in its own separate change notification. */
	part_p = test_build_part_string (CB_ID_P, refs_p, 1);
	test_add_messages (folder,
		"uid", "p", "subject", "Re: Root", "from", "p@test", "dsent", (gint64) 2000, "part", part_p, "",
		NULL);
	g_free (part_p);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "p");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 3);

	/* "c" should now be threaded as a child of "p", which in turn is a
	 * child of "root" - not still sitting directly under "root". */
	g_assert_cmpuint (camel_folder_view_get_depth (view, "root"), ==, 0);
	g_assert_cmpuint (camel_folder_view_get_depth (view, "p"), ==, 1);
	g_assert_cmpuint (camel_folder_view_get_depth (view, "c"), ==, 2);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
	#undef CB_ID_ROOT
	#undef CB_ID_P
	#undef CB_ID_C
}

static void
test_folder_view_child_before_parent_present_at_initial_build (void)
{
	#define CB2_ID_P 95600
	#define CB2_ID_C 95700
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	guint64 refs_c[] = { CB2_ID_P };
	gchar *part_c, *part_p;

	store = test_store_new ();
	folder = create_test_folder (store, "f2");

	/* "c" is already present at the very first (full) rebuild, and its
	 * parent "p" does not exist yet at that time. */
	part_c = test_build_part_string (CB2_ID_C, refs_c, 1);
	test_add_messages (folder,
		"uid", "c", "subject", "Child", "from", "c@test", "dsent", (gint64) 2000, "part", part_c, "",
		NULL);
	g_free (part_c);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 1);
	g_assert_cmpuint (camel_folder_view_get_depth (view, "c"), ==, 0);

	/* "p" arrives later, incrementally. */
	part_p = test_build_part_string (CB2_ID_P, NULL, 0);
	test_add_messages (folder,
		"uid", "p", "subject", "Parent", "from", "p@test", "dsent", (gint64) 1000, "part", part_p, "",
		NULL);
	g_free (part_p);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "p");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 2);
	g_assert_cmpuint (camel_folder_view_get_depth (view, "p"), ==, 0);
	g_assert_cmpuint (camel_folder_view_get_depth (view, "c"), ==, 1);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
	#undef CB2_ID_P
	#undef CB2_ID_C
}

static void
test_folder_view_orphans_share_missing_ancestor (void)
{
	#define OM_ID_ROOT 95800
	#define OM_ID_REPLY1 95801
	#define OM_ID_REPLY2 95802
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	guint64 refs[] = { OM_ID_ROOT };
	gchar *part_reply1, *part_reply2;

	store = test_store_new ();
	folder = create_test_folder (store, "f2");

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 0);

	/* Both messages reply to the same ancestor ("root"), which was
	 * never delivered to this mailbox and never will be - a common
	 * pattern for mailing-list/forum notification threads. They
	 * arrive together, in a single change notification. */
	part_reply1 = test_build_part_string (OM_ID_REPLY1, refs, 1);
	part_reply2 = test_build_part_string (OM_ID_REPLY2, refs, 1);
	test_add_messages (folder,
		"uid", "reply1", "subject", "Re: Root", "from", "reply1@test", "dsent", (gint64) 1000, "part", part_reply1, "",
		"uid", "reply2", "subject", "Re: Root", "from", "reply2@test", "dsent", (gint64) 2000, "part", part_reply2, "",
		NULL);
	g_free (part_reply1);
	g_free (part_reply2);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "reply1");
	camel_folder_change_info_add_uid (changes, "reply2");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	/* Grouped as one thread (2 rows, not 2 unrelated roots): the
	 * older reply stands in as the visible root, the newer one is
	 * threaded as its child. No blank/placeholder row is shown. */
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 2);
	g_assert_cmpuint (camel_folder_view_get_depth (view, "reply1"), ==, 0);
	g_assert_true (camel_folder_view_is_expandable (view, "reply1"));
	g_assert_cmpuint (camel_folder_view_get_depth (view, "reply2"), ==, 1);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
	#undef OM_ID_ROOT
	#undef OM_ID_REPLY1
	#undef OM_ID_REPLY2
}

static void
test_folder_view_orphans_share_missing_ancestor_separate_batches (void)
{
	#define OS_ID_ROOT 95900
	#define OS_ID_REPLY1 95901
	#define OS_ID_REPLY2 95902
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	guint64 refs[] = { OS_ID_ROOT };
	gchar *part_reply1, *part_reply2;

	store = test_store_new ();
	folder = create_test_folder (store, "f2");

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	/* "reply1" arrives alone first, replying to a never-delivered
	 * ancestor - it is its own root for now. */
	part_reply1 = test_build_part_string (OS_ID_REPLY1, refs, 1);
	test_add_messages (folder,
		"uid", "reply1", "subject", "Re: Root", "from", "reply1@test", "dsent", (gint64) 1000, "part", part_reply1, "",
		NULL);
	g_free (part_reply1);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "reply1");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 1);
	g_assert_cmpuint (camel_folder_view_get_depth (view, "reply1"), ==, 0);

	/* "reply2" arrives later, in its own separate change notification,
	 * replying to the same missing ancestor. Since threaded views now
	 * rethread from scratch on every batch, this must still group with
	 * "reply1" correctly, unlike a surgical incremental patch would. */
	part_reply2 = test_build_part_string (OS_ID_REPLY2, refs, 1);
	test_add_messages (folder,
		"uid", "reply2", "subject", "Re: Root", "from", "reply2@test", "dsent", (gint64) 2000, "part", part_reply2, "",
		NULL);
	g_free (part_reply2);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "reply2");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 2);
	g_assert_cmpuint (camel_folder_view_get_depth (view, "reply1"), ==, 0);
	g_assert_true (camel_folder_view_is_expandable (view, "reply1"));
	g_assert_cmpuint (camel_folder_view_get_depth (view, "reply2"), ==, 1);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
	#undef OS_ID_ROOT
	#undef OS_ID_REPLY1
	#undef OS_ID_REPLY2
}

static void
test_folder_view_thread_subject_incremental (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	gchar *part_p, *part_c;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_thread_subject (view, TRUE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 0);

	/* "c" arrives first: no References headers, nothing to match it
	 * against yet, so it's its own root. */
	part_c = test_build_part_string (95800, NULL, 0);
	test_add_messages (folder,
		"uid", "c", "subject", "Re: Important topic", "from", "c@test",
		"dsent", (gint64) 2000, "part", part_c, "",
		NULL);
	g_free (part_c);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "c");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 1);
	g_assert_cmpuint (camel_folder_view_get_depth (view, "c"), ==, 0);

	/* "p" arrives later, in its own separate change notification, with
	 * the same normalized subject but still no References headers. */
	part_p = test_build_part_string (95900, NULL, 0);
	test_add_messages (folder,
		"uid", "p", "subject", "Important topic", "from", "p@test",
		"dsent", (gint64) 1000, "part", part_p, "",
		NULL);
	g_free (part_p);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "p");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	/* "c" should now be grouped under "p" by subject, not still sitting
	 * as a separate root at depth 0. */
	g_assert_cmpuint (camel_folder_view_get_depth (view, "p"), ==, 0);
	g_assert_true (camel_folder_view_is_expandable (view, "p"));
	g_assert_cmpuint (camel_folder_view_get_depth (view, "c"), ==, 1);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_rows_inserted_into_collapsed_thread (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	GArray *inserted_ranges;
	GArray *removed_ranges;
	gulong inserted_id, removed_id;
	guint64 refs_reply[] = { MSG_ID_A };
	gchar *part_root, *part_reply;
	guint row_count_before;

	store = test_store_new ();
	folder = create_test_folder (store, "f2");

	part_root = test_build_part_string (MSG_ID_A, NULL, 0);
	test_add_messages (folder,
		"uid", "root", "subject", "Root", "from", "root@test",
		"dsent", (gint64) 1000, "part", part_root, "",
		NULL);
	g_free (part_root);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 1);

	/* Collapse the root before it has any children; the collapsed
	 * state should stick once it gains one incrementally. */
	camel_folder_view_set_expanded (view, "root", FALSE);

	row_count_before = camel_folder_view_get_row_count (view);

	inserted_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	removed_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	inserted_id = g_signal_connect (view, "rows-inserted", G_CALLBACK (capture_row_range_cb), inserted_ranges);
	removed_id = g_signal_connect (view, "rows-removed", G_CALLBACK (capture_row_range_cb), removed_ranges);

	/* Add a reply threaded under the collapsed root: it stays hidden,
	 * so no rows-inserted/-removed should fire for it. */
	part_reply = test_build_part_string (MSG_ID_B, refs_reply, 1);
	test_add_messages (folder,
		"uid", "reply", "subject", "Re: Root", "from", "reply@test",
		"dsent", (gint64) 2000, "part", part_reply, "",
		NULL);
	g_free (part_reply);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "reply");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, row_count_before);
	g_assert_cmpuint (inserted_ranges->len, ==, 0);
	g_assert_cmpuint (removed_ranges->len, ==, 0);

	/* Expanding the root now reveals the child that arrived while collapsed. */
	camel_folder_view_set_expanded (view, "root", TRUE);
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, row_count_before + 1);

	g_signal_handler_disconnect (view, inserted_id);
	g_signal_handler_disconnect (view, removed_id);
	g_array_unref (inserted_ranges);
	g_array_unref (removed_ranges);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static gint64 make_timestamp (gint year, gint month, gint day, gint hour);

static void
test_folder_view_rows_grouped_header_churn (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	GArray *inserted_ranges;
	GArray *removed_ranges;
	gulong inserted_id, removed_id;
	gchar *part_h;
	gint64 now, new_dsent;
	gint now_year;
	GDateTime *dt_now;
	const RowRange expected_removed[] = { { 0, 0 } };
	const RowRange expected_inserted[] = { { 0, 2 } };

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_group_by (view, CAMEL_FOLDER_VIEW_GROUP_BY_DATE_SENT);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_DESCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	/* All 7 messages are from 1970 ("Older"): one header + 7 rows */
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 8);
	g_assert_true (camel_folder_view_is_group_row (view, 0));

	inserted_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	removed_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	inserted_id = g_signal_connect (view, "rows-inserted", G_CALLBACK (capture_row_range_cb), inserted_ranges);
	removed_id = g_signal_connect (view, "rows-removed", G_CALLBACK (capture_row_range_cb), removed_ranges);

	/* Add a message dated a few years ago: a brand new group that, under
	 * descending sort, lands *before* "Older" - pushing its header (and
	 * all 7 of its messages) down, even though none of them changed. */
	now = g_get_real_time () / G_USEC_PER_SEC;
	dt_now = g_date_time_new_from_unix_local (now);
	now_year = g_date_time_get_year (dt_now);
	g_date_time_unref (dt_now);
	new_dsent = make_timestamp (now_year - 3, 6, 15, 12);

	part_h = test_build_part_string (830, NULL, 0);
	test_add_messages (folder,
		"uid", "h", "subject", "Hotel", "from", "harry@test.com",
		"dsent", new_dsent, "dreceived", new_dsent,
		"part", part_h, "",
		NULL);
	g_free (part_h);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "h");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	/* 2 headers + 8 messages */
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 10);
	assert_row_ranges (removed_ranges, expected_removed, G_N_ELEMENTS (expected_removed), "rows_grouped_header_churn (removed)");
	assert_row_ranges (inserted_ranges, expected_inserted, G_N_ELEMENTS (expected_inserted), "rows_grouped_header_churn (inserted)");

	g_signal_handler_disconnect (view, inserted_id);
	g_signal_handler_disconnect (view, removed_id);
	g_array_unref (inserted_ranges);
	g_array_unref (removed_ranges);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_folder_changed_modify (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	/* The view should still have all messages after a flag change */

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_change_uid (changes, "a");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_thread_latest (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderViewRow *first;
	guint64 refs_b[] = { MSG_ID_A };
	gchar *part_a = test_build_part_string (MSG_ID_A, NULL, 0);
	gchar *part_b = test_build_part_string (MSG_ID_B, refs_b, 1);
	gchar *part_c = test_build_part_string (MSG_ID_C, NULL, 0);

	/* Thread 1: root A (dsent=1000) with child B (dsent=9000) - latest=9000
	 * Thread 2: root C (dsent=5000) with no children - latest=5000
	 * Without thread-latest: sorted by root date -> A(1000), C(5000)
	 * With thread-latest: sorted by latest in thread -> C(5000), A(9000) */

	store = test_store_new ();
	folder = create_test_folder (store, "f3");

	test_add_messages (folder,
		"uid", "a", "subject", "Thread A root", "from", "alice@test.com",
		"dsent", (gint64) 1000, "dreceived", (gint64) 1010,
		"part", part_a, "",
		"uid", "b", "subject", "Re: Thread A root", "from", "bob@test.com",
		"dsent", (gint64) 9000, "dreceived", (gint64) 9010,
		"part", part_b, "",
		"uid", "c", "subject", "Standalone C", "from", "charlie@test.com",
		"dsent", (gint64) 5000, "dreceived", (gint64) 5010,
		"part", part_c, "",
		NULL);

	/* Without thread-latest: A before C (sorted by root's dsent) */
	view = camel_folder_view_new (folder, TRUE);

	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	first = camel_folder_view_get_row (view, 0);

	g_assert_nonnull (first);
	g_assert_cmpstr (camel_folder_view_row_get_uid (first), ==, "a");

	/* Enable thread-latest: C before A (C's latest=5000, A's latest=9000) */
	camel_folder_view_freeze (view);
	camel_folder_view_set_thread_latest (view, TRUE);
	camel_folder_view_thaw (view);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	first = camel_folder_view_get_row (view, 0);

	g_assert_nonnull (first);
	g_assert_cmpstr (camel_folder_view_row_get_uid (first), ==, "c");

	/* Disable: back to A first */
	camel_folder_view_freeze (view);
	camel_folder_view_set_thread_latest (view, FALSE);
	camel_folder_view_thaw (view);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	first = camel_folder_view_get_row (view, 0);

	g_assert_nonnull (first);
	g_assert_cmpstr (camel_folder_view_row_get_uid (first), ==, "a");


	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);
}

static void
test_folder_view_group_by_date (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	gboolean found_group = FALSE;
	guint ii;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_group_by (view, CAMEL_FOLDER_VIEW_GROUP_BY_DATE_SENT);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	/* Should have more rows than 7 due to group headers */
	g_assert_cmpuint (camel_folder_view_get_row_count (view), >, 7);

	for (ii = 0; ii < camel_folder_view_get_row_count (view); ii++) {
		if (camel_folder_view_is_group_row (view, ii)) {
			const gchar *label = camel_folder_view_get_group_label (view, ii);

			g_assert_nonnull (label);
			g_assert_null (camel_folder_view_get_row (view, ii));
			found_group = TRUE;
		}
	}

	g_assert_true (found_group);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}
static CamelVeeStore *
create_vee_store (void)
{
	static const CamelProvider provider = { "vfolder", "test-vfolder", "Test Vee Folder provider", GETTEXT_PACKAGE, 0, };
	CamelSession *session;
	CamelVeeStore *vee_store;
	GError *local_error = NULL;

	session = test_session_new ();

	vee_store = g_initable_new (CAMEL_TYPE_VEE_STORE, NULL, &local_error,
		"uid", "vfolder",
		"display-name", "Test Vee Store",
		"provider", &provider,
		"session", session,
		"with-proxy-resolver", FALSE,
		NULL);
	g_assert_no_error (local_error);
	g_assert_nonnull (vee_store);

	g_clear_object (&session);

	return vee_store;
}

static CamelFolder *
create_vee_folder_with_subfolder (CamelVeeStore *vee_store,
				  CamelFolder *subfolder)
{
	CamelVeeFolder *vf;
	GError *local_error = NULL;
	gboolean success;

	vf = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf", 0));
	g_assert_nonnull (vf);

	success = camel_vee_folder_add_folder_sync (vf, subfolder,
		CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	success = camel_vee_folder_set_expression_sync (vf, "#t",
		CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	test_session_wait_for_pending_jobs ();

	return CAMEL_FOLDER (vf);
}

/* VeeFolder test 1: Flat view, no filter (in-memory path) */
static void
test_folder_view_vee_flat_no_filter (void)
{
	CamelStore *store;
	CamelFolder *real_folder, *vee_folder;
	CamelVeeStore *vee_store;
	CamelFolderView *view;
	CamelFolderViewRow *first;
	CamelFolderViewRow *last;
	CamelFolderViewRow *info;

	store = test_store_new ();
	real_folder = create_test_folder (store, "f1");
	add_flat_messages (real_folder);

	vee_store = create_vee_store ();
	vee_folder = create_vee_folder_with_subfolder (vee_store, real_folder);

	view = camel_folder_view_new (vee_folder, TRUE);
	g_assert_true (CAMEL_IS_VEE_FOLDER (vee_folder));
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	first = camel_folder_view_get_row (view, 0);

	last = camel_folder_view_get_row (view, 6);

	g_assert_nonnull (first);
	g_assert_nonnull (last);

	info = camel_folder_view_get_row (view, 3);

	g_assert_cmpuint (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info)), ==, 0);
	g_assert_false (camel_folder_view_is_expandable (view, camel_folder_view_row_get_uid (info)));

	g_clear_object (&view);
	g_clear_object (&vee_folder);
	g_clear_object (&vee_store);
	g_clear_object (&real_folder);
	g_clear_object (&store);
}

/* VeeFolder test 2: Flat view with filter (in-memory path) */
static void
test_folder_view_vee_flat_with_filter (void)
{
	CamelStore *store;
	CamelFolder *real_folder, *vee_folder;
	CamelVeeStore *vee_store;
	CamelFolderView *view;
	CamelFolderViewRow *info;

	store = test_store_new ();
	real_folder = create_test_folder (store, "f1");
	add_flat_messages (real_folder);

	vee_store = create_vee_store ();
	vee_folder = create_vee_folder_with_subfolder (vee_store, real_folder);

	view = camel_folder_view_new (vee_folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_filter (view, "(match-all (header-contains \"Subject\" \"Alpha\"))");
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), >=, 1);

	info = camel_folder_view_get_row (view, 0);

	g_assert_nonnull (info);
	g_assert_cmpstr (camel_folder_view_row_get_subject (info), ==, "Alpha");

	g_clear_object (&view);
	g_clear_object (&vee_folder);
	g_clear_object (&vee_store);
	g_clear_object (&real_folder);
	g_clear_object (&store);
}

/* VeeFolder test 3: Full threading (in-memory path) */
static void
test_folder_view_vee_threaded (void)
{
	CamelStore *store;
	CamelFolder *real_folder, *vee_folder;
	CamelVeeStore *vee_store;
	CamelFolderView *view;
	CamelFolderViewRow *info_b;

	store = test_store_new ();

	real_folder = create_test_folder (store, "f1");
	add_threaded_messages (real_folder);

	vee_store = create_vee_store ();
	vee_folder = create_vee_folder_with_subfolder (vee_store, real_folder);

	view = camel_folder_view_new (vee_folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 6);

	info_b = camel_folder_view_get_row (view, 1);

	if (info_b)
		g_assert_cmpuint (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info_b)), >=, 1);


	g_clear_object (&view);
	g_clear_object (&vee_folder);
	g_clear_object (&vee_store);
	g_clear_object (&real_folder);
	g_clear_object (&store);
}

/* VeeFolder test 3: Flat threads (in-memory path) */
static void
test_folder_view_vee_flat_threads (void)
{
	CamelStore *store;
	CamelFolder *real_folder, *vee_folder;
	CamelVeeStore *vee_store;
	CamelFolderView *view;
	guint ii;

	store = test_store_new ();
	real_folder = create_test_folder (store, "f1");
	add_threaded_messages (real_folder);

	vee_store = create_vee_store ();
	vee_folder = create_vee_folder_with_subfolder (vee_store, real_folder);

	view = camel_folder_view_new (vee_folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FLAT);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 6);

	for (ii = 0; ii < camel_folder_view_get_row_count (view); ii++) {
		CamelFolderViewRow *info = camel_folder_view_get_row (view, ii);
		if (info) {
			g_assert_cmpuint (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info)), <=, 1);
		}
	}

	g_clear_object (&view);
	g_clear_object (&vee_folder);
	g_clear_object (&vee_store);
	g_clear_object (&real_folder);
	g_clear_object (&store);
}

/* VeeFolder test 4: Thread by subject (in-memory path) */
static void
test_folder_view_vee_thread_subject (void)
{
	CamelStore *store;
	CamelFolder *real_folder, *vee_folder;
	CamelVeeStore *vee_store;
	CamelFolderView *view;
	gchar *part_1;
	gchar *part_2;
	gboolean found_child;
	guint ii;

	store = test_store_new ();
	real_folder = create_test_folder (store, "f1");

	part_1 = test_build_part_string (MSG_ID_A, NULL, 0);

	part_2 = test_build_part_string (MSG_ID_B, NULL, 0);

	test_add_messages (real_folder,
		"uid", "s1", "subject", "Important topic", "from", "alice@test.com",
		"dsent", (gint64) 1000000, "dreceived", (gint64) 1000010,
		"part", part_1, "",
		"uid", "s2", "subject", "Re: Important topic", "from", "bob@test.com",
		"dsent", (gint64) 1000100, "dreceived", (gint64) 1000110,
		"part", part_2, "",
		NULL);

	g_free (part_1);
	g_free (part_2);

	vee_store = create_vee_store ();
	vee_folder = create_vee_folder_with_subfolder (vee_store, real_folder);

	view = camel_folder_view_new (vee_folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_thread_subject (view, TRUE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	found_child = FALSE;

	for (ii = 0; ii < camel_folder_view_get_row_count (view); ii++) {
		CamelFolderViewRow *info = camel_folder_view_get_row (view, ii);
		if (info) {
			if (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info)) > 0)
				found_child = TRUE;
		}
	}

	g_assert_true (found_child);

	g_clear_object (&view);
	g_clear_object (&vee_folder);
	g_clear_object (&vee_store);
	g_clear_object (&real_folder);
	g_clear_object (&store);
}

/* VeeFolder test 5: Sort (in-memory path) */
static void
test_folder_view_vee_sort (void)
{
	CamelStore *store;
	CamelFolder *real_folder, *vee_folder;
	CamelVeeStore *vee_store;
	CamelFolderView *view;
	CamelFolderViewRow *first;
	CamelFolderViewRow *last;

	store = test_store_new ();
	real_folder = create_test_folder (store, "f1");
	add_flat_messages (real_folder);

	vee_store = create_vee_store ();
	vee_folder = create_vee_folder_with_subfolder (vee_store, real_folder);

	view = camel_folder_view_new (vee_folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	first = camel_folder_view_get_row (view, 0);

	last = camel_folder_view_get_row (view, 6);

	g_assert_nonnull (first);
	g_assert_nonnull (last);
	g_assert_cmpint (camel_folder_view_row_get_date_sent (first), <=,
	                  camel_folder_view_row_get_date_sent (last));

	camel_folder_view_freeze (view);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_DESCENDING);
	camel_folder_view_thaw (view);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	first = camel_folder_view_get_row (view, 0);

	last = camel_folder_view_get_row (view, 6);

	g_assert_nonnull (first);
	g_assert_nonnull (last);
	g_assert_cmpint (camel_folder_view_row_get_date_sent (first), >=,
	                  camel_folder_view_row_get_date_sent (last));


	g_clear_object (&view);
	g_clear_object (&vee_folder);
	g_clear_object (&vee_store);
	g_clear_object (&real_folder);
	g_clear_object (&store);
}

/* VeeFolder test 5: Expand/collapse (in-memory path) */
static void
test_folder_view_vee_expand_collapse (void)
{
	CamelStore *store;
	CamelFolder *real_folder, *vee_folder;
	CamelVeeStore *vee_store;
	CamelFolderView *view;
	CamelFolderViewRow *info_a;
	guint expanded_count;

	store = test_store_new ();
	real_folder = create_test_folder (store, "f1");
	add_threaded_messages (real_folder);

	vee_store = create_vee_store ();
	vee_folder = create_vee_folder_with_subfolder (vee_store, real_folder);

	view = camel_folder_view_new (vee_folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	expanded_count = camel_folder_view_get_row_count (view);
	g_assert_cmpuint (expanded_count, ==, 6);

	info_a = camel_folder_view_get_row (view, 0);

	if (info_a && camel_folder_view_is_expandable (view, camel_folder_view_row_get_uid (info_a))) {
		camel_folder_view_set_expanded (view, camel_folder_view_row_get_uid (info_a), FALSE);
		g_assert_cmpuint (camel_folder_view_get_row_count (view), <, expanded_count);

		camel_folder_view_set_expanded (view, camel_folder_view_row_get_uid (info_a), TRUE);
		g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, expanded_count);
	}


	g_clear_object (&view);
	g_clear_object (&vee_folder);
	g_clear_object (&vee_store);
	g_clear_object (&real_folder);
	g_clear_object (&store);
}

/* VeeFolder test 8: Folder changed - add (in-memory path) */
static void
test_folder_view_vee_folder_changed_add (void)
{
	CamelStore *store;
	CamelFolder *real_folder, *vee_folder;
	CamelVeeStore *vee_store;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	gchar *part_h;
	guint count_before;

	store = test_store_new ();
	real_folder = create_test_folder (store, "f1");
	add_flat_messages (real_folder);

	vee_store = create_vee_store ();
	vee_folder = create_vee_folder_with_subfolder (vee_store, real_folder);

	view = camel_folder_view_new (vee_folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	count_before = camel_folder_view_get_row_count (view);
	g_assert_cmpuint (count_before, ==, 7);

	part_h = test_build_part_string (800, NULL, 0);

	test_add_messages (real_folder,
		"uid", "h", "subject", "Hotel", "from", "harry@test.com",
		"dsent", (gint64) 1000700, "dreceived", (gint64) 1000710,
		"part", part_h, "",
		NULL);

	g_free (part_h);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "h");
	camel_folder_changed (real_folder, changes);
	camel_folder_change_info_free (changes);

	/* The vee folder will propagate changes asynchronously;
	 * wait for the view to update */
	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, count_before + 1);

	g_clear_object (&view);
	g_clear_object (&vee_folder);
	g_clear_object (&vee_store);
	g_clear_object (&real_folder);
	g_clear_object (&store);
}

/* VeeFolder test 9: Folder changed - remove (in-memory path) */
static void
test_folder_view_vee_folder_changed_remove (void)
{
	CamelStore *store;
	CamelFolder *real_folder, *vee_folder;
	CamelVeeStore *vee_store;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	guint count_before;

	store = test_store_new ();
	real_folder = create_test_folder (store, "f1");
	add_flat_messages (real_folder);

	vee_store = create_vee_store ();
	vee_folder = create_vee_folder_with_subfolder (vee_store, real_folder);

	view = camel_folder_view_new (vee_folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	count_before = camel_folder_view_get_row_count (view);
	g_assert_cmpuint (count_before, ==, 7);

	camel_folder_summary_remove_uid (camel_folder_get_folder_summary (real_folder), "a");

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_remove_uid (changes, "a");
	camel_folder_changed (real_folder, changes);
	camel_folder_change_info_free (changes);

	/* VeeFolder propagates subfolder changes asynchronously;
	 * wait for up to two view updates (VeeFolder propagation + view rebuild) */
	wait_for_folder_changed (view);

	if (camel_folder_view_get_row_count (view) != count_before - 1)
		wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, count_before - 1);

	g_clear_object (&view);
	g_clear_object (&vee_folder);
	g_clear_object (&vee_store);
	g_clear_object (&real_folder);
	g_clear_object (&store);
}

/* VeeFolder test 10: Folder changed - modify (in-memory path) */
static void
test_folder_view_vee_folder_changed_modify (void)
{
	CamelStore *store;
	CamelFolder *real_folder, *vee_folder;
	CamelVeeStore *vee_store;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;

	store = test_store_new ();
	real_folder = create_test_folder (store, "f1");
	add_flat_messages (real_folder);

	vee_store = create_vee_store ();
	vee_folder = create_vee_folder_with_subfolder (vee_store, real_folder);

	view = camel_folder_view_new (vee_folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_change_uid (changes, "a");
	camel_folder_changed (real_folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);

	g_clear_object (&view);
	g_clear_object (&vee_folder);
	g_clear_object (&vee_store);
	g_clear_object (&real_folder);
	g_clear_object (&store);
}

/* VeeFolder test 11: Group by date (in-memory path) */
static void
test_folder_view_vee_group_by_date (void)
{
	CamelStore *store;
	CamelFolder *real_folder, *vee_folder;
	CamelVeeStore *vee_store;
	CamelFolderView *view;
	gboolean found_group = FALSE;
	guint ii;

	store = test_store_new ();
	real_folder = create_test_folder (store, "f1");
	add_flat_messages (real_folder);

	vee_store = create_vee_store ();
	vee_folder = create_vee_folder_with_subfolder (vee_store, real_folder);

	view = camel_folder_view_new (vee_folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_group_by (view, CAMEL_FOLDER_VIEW_GROUP_BY_DATE_SENT);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), >, 7);

	for (ii = 0; ii < camel_folder_view_get_row_count (view); ii++) {
		if (camel_folder_view_is_group_row (view, ii)) {
			g_assert_nonnull (camel_folder_view_get_group_label (view, ii));
			g_assert_null (camel_folder_view_get_row (view, ii));
			found_group = TRUE;
		}
	}

	g_assert_true (found_group);

	g_clear_object (&view);
	g_clear_object (&vee_folder);
	g_clear_object (&vee_store);
	g_clear_object (&real_folder);
	g_clear_object (&store);
}

/*
 * Complex threading test: partial thread, then add/remove root.
 *
 * Full thread structure:
 *   A -> []
 *   B -> [A]
 *   C -> [B,A]
 *   D -> [C,A]
 *   E -> [A]
 *   F -> [A,B]
 *   G -> [F]
 *   H -> [G,C]
 *   I -> [H,D]
 *
 * We start with only B, C, E in the view (A is missing - could be
 * filtered out, not yet received, etc.). The algorithm should still
 * produce a sensible tree. Then we add A and verify the tree merges
 * correctly. Then we remove A and verify it returns to the original
 * state.
 */

/* Message IDs for the complex thread */
#define THREAD_ID_A  1000
#define THREAD_ID_B  2000
#define THREAD_ID_C  3000
#define THREAD_ID_E  5000

static void
verify_tree_without_a (CamelFolderView *view)
{
	guint row_count;
	CamelFolderViewRow *info;
	const gchar *uid;
	gboolean found_b = FALSE, found_c = FALSE, found_e = FALSE;
	guint depth_b = 0, depth_c = 0, depth_e = 0;
	guint ii;

	row_count = camel_folder_view_get_row_count (view);
	g_assert_cmpuint (row_count, ==, 3);

	for (ii = 0; ii < row_count; ii++) {
		info = camel_folder_view_get_row (view, ii);
		g_assert_nonnull (info);
		uid = camel_folder_view_row_get_uid (info);

		if (g_str_has_suffix (uid, "b")) {
			found_b = TRUE;
			depth_b = camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info));
		} else if (g_str_has_suffix (uid, "c")) {
			found_c = TRUE;
			depth_c = camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info));
		} else if (g_str_has_suffix (uid, "e")) {
			found_e = TRUE;
			depth_e = camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info));
		}
	}

	g_assert_true (found_b);
	g_assert_true (found_c);
	g_assert_true (found_e);

	/* Phantom-A collapsed: B is oldest -> swapped to root.
	 * E and C are children of B. */
	g_assert_cmpuint (depth_b, ==, 0);
	g_assert_cmpuint (depth_c, >=, 1);
	g_assert_cmpuint (depth_e, >=, 1);
}

static void
verify_tree_with_a (CamelFolderView *view)
{
	guint row_count;
	CamelFolderViewRow *info;
	const gchar *uid;
	gboolean found_a = FALSE, found_b = FALSE, found_c = FALSE, found_e = FALSE;
	guint depth_a = 0, depth_b = 0, depth_c = 0, depth_e = 0;
	guint ii;

	row_count = camel_folder_view_get_row_count (view);
	g_assert_cmpuint (row_count, ==, 4);

	for (ii = 0; ii < row_count; ii++) {
		info = camel_folder_view_get_row (view, ii);
		g_assert_nonnull (info);
		uid = camel_folder_view_row_get_uid (info);

		if (g_str_has_suffix (uid, "a")) {
			found_a = TRUE;
			depth_a = camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info));
		} else if (g_str_has_suffix (uid, "b")) {
			found_b = TRUE;
			depth_b = camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info));
		} else if (g_str_has_suffix (uid, "c")) {
			found_c = TRUE;
			depth_c = camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info));
		} else if (g_str_has_suffix (uid, "e")) {
			found_e = TRUE;
			depth_e = camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info));
		}
	}

	g_assert_true (found_a);
	g_assert_true (found_b);
	g_assert_true (found_c);
	g_assert_true (found_e);

	/* A is root, B and E under A, C under B */
	g_assert_cmpuint (depth_a, ==, 0);
	g_assert_cmpuint (depth_b, ==, 1);
	g_assert_cmpuint (depth_c, ==, 2);
	g_assert_cmpuint (depth_e, ==, 1);

	/* A should be expandable */
	for (ii = 0; ii < row_count; ii++) {
		info = camel_folder_view_get_row (view, ii);
		uid = camel_folder_view_row_get_uid (info);
		if (g_str_has_suffix (uid, "a")) {
			g_assert_true (camel_folder_view_is_expandable (view, camel_folder_view_row_get_uid (info)));
			break;
		}
	}
}

static void
test_folder_view_complex_threading (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	gchar *part_a;
	guint64 refs_b[] = { THREAD_ID_A };
	guint64 refs_c[] = { THREAD_ID_B, THREAD_ID_A };
	guint64 refs_e[] = { THREAD_ID_A };
	gchar *part_b = test_build_part_string (THREAD_ID_B, refs_b, 1);
	gchar *part_c = test_build_part_string (THREAD_ID_C, refs_c, 2);
	gchar *part_e = test_build_part_string (THREAD_ID_E, refs_e, 1);

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	/* Step 1: Add only B, C, E - A is missing */
	test_add_messages (folder,
		"uid", "b", "subject", "Reply B", "from", "bob@test.com",
		"dsent", (gint64) 2000000, "dreceived", (gint64) 2000010,
		"part", part_b, "",
		"uid", "c", "subject", "Reply C", "from", "charlie@test.com",
		"dsent", (gint64) 3000000, "dreceived", (gint64) 3000010,
		"part", part_c, "",
		"uid", "e", "subject", "Reply E", "from", "eve@test.com",
		"dsent", (gint64) 5000000, "dreceived", (gint64) 5000010,
		"part", part_e, "",
		NULL);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	verify_tree_without_a (view);

	/* Step 2: Add A - should merge B, C, E under it */
	part_a = test_build_part_string (THREAD_ID_A, NULL, 0);

	test_add_messages (folder,
		"uid", "a", "subject", "Original A", "from", "alice@test.com",
		"dsent", (gint64) 1000000, "dreceived", (gint64) 1000010,
		"part", part_a, "",
		NULL);

	g_free (part_a);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "a");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	verify_tree_with_a (view);

	/* Step 3: Remove A - should return to original structure */
	camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), "a");

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_remove_uid (changes, "a");

	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	verify_tree_without_a (view);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);

	g_free (part_b);
	g_free (part_c);
	g_free (part_e);
}

/*
 * Extended threading test: 12 incremental steps of add/remove on the
 * same thread tree, verifying structure at each step.
 *
 * Thread: A->[] B->[A] C->[B,A] D->[C,A] E->[A] F->[A,B] G->[F] H->[G,C] I->[H,D]
 */

#define TID_A 1000
#define TID_B 2000
#define TID_C 3000
#define TID_D 4000
#define TID_E 5000
#define TID_F 6000
#define TID_G 7000
#define TID_H 8000
#define TID_I 9000

typedef struct _NodeCheck {
	const gchar *uid;
	guint depth;
	gboolean expandable;
} NodeCheck;

static void
verify_view_structure (CamelFolderView *view,
		       const NodeCheck *expected,
		       guint n_expected,
		       const gchar *step_label)
{
	guint row_count, ii;

	row_count = camel_folder_view_get_row_count (view);
	if (row_count != n_expected) {
		g_error ("%s: expected %u rows, got %u", step_label, n_expected, row_count);
	}

	for (ii = 0; ii < n_expected; ii++) {
		CamelFolderViewRow *info = camel_folder_view_get_row (view, ii);
		const gchar *uid;
		guint depth;
		gboolean expandable;

		g_assert_nonnull (info);
		uid = camel_folder_view_row_get_uid (info);
		depth = camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info));
		expandable = camel_folder_view_is_expandable (view, camel_folder_view_row_get_uid (info));

		if (!g_str_has_suffix (uid, expected[ii].uid)) {
			g_error ("%s: row %u: expected uid ending '%s', got '%s'",
				step_label, ii, expected[ii].uid, uid);
		}
		if (depth != expected[ii].depth) {
			g_error ("%s: row %u (uid '%s'): expected depth %u, got %u",
				step_label, ii, uid, expected[ii].depth, depth);
		}
		if (expandable != expected[ii].expandable) {
			g_error ("%s: row %u (uid '%s'): expected expandable=%d, got %d",
				step_label, ii, uid, expected[ii].expandable, expandable);
		}
	}
}

static void
test_folder_view_threading_incremental (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;

	guint64 refs_b[] = { TID_A };
	guint64 refs_c[] = { TID_B, TID_A };
	guint64 refs_d[] = { TID_C, TID_A };
	guint64 refs_f[] = { TID_A, TID_B };
	guint64 refs_g[] = { TID_F };
	guint64 refs_h[] = { TID_G, TID_C };
	guint64 refs_i[] = { TID_H, TID_D };

	gchar *part_a = test_build_part_string (TID_A, NULL, 0);
	gchar *part_b = test_build_part_string (TID_B, refs_b, 1);
	gchar *part_c = test_build_part_string (TID_C, refs_c, 2);
	gchar *part_d = test_build_part_string (TID_D, refs_d, 2);
	gchar *part_f = test_build_part_string (TID_F, refs_f, 2);
	gchar *part_g = test_build_part_string (TID_G, refs_g, 1);
	gchar *part_h = test_build_part_string (TID_H, refs_h, 2);
	gchar *part_i = test_build_part_string (TID_I, refs_i, 2);

	store = test_store_new ();
	folder = create_test_folder (store, "f2");

	/* Step 1: Add B, D, H - three independent roots */
	test_add_messages (folder,
		"uid", "b", "subject", "B", "from", "b@test", "dsent", (gint64) 2000, "part", part_b, "",
		"uid", "d", "subject", "D", "from", "d@test", "dsent", (gint64) 4000, "part", part_d, "",
		"uid", "h", "subject", "H", "from", "h@test", "dsent", (gint64) 8000, "part", part_h, "",
		NULL);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	/* b(0) -> {d(1), h(1)} - phantom-A collapsed, B is oldest, sorted by date */
	{
		const NodeCheck expected[] = {
			{ "b", 0, TRUE },
			{ "d", 1, FALSE },
			{ "h", 1, FALSE },
		};
		verify_view_structure (view, expected, G_N_ELEMENTS (expected), "step 1");
	}

	/* Step 2: Add C -> b(0)->c(1)->{h(2),d(2)} */
	test_add_messages (folder,
		"uid", "c", "subject", "C", "from", "c@test", "dsent", (gint64) 3000, "part", part_c, "",
		NULL);
	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "c");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);
	wait_for_folder_changed (view);

	/* b(0)->c(1)->{d(2),h(2)} sorted by date */
	{
		const NodeCheck expected[] = {
			{ "b", 0, TRUE },
			{ "c", 1, TRUE },
			{ "d", 2, FALSE },
			{ "h", 2, FALSE },
		};
		verify_view_structure (view, expected, G_N_ELEMENTS (expected), "step 2");
	}

	/* Step 3: Remove C -> back to step 1 */
	camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), "c");
	changes = camel_folder_change_info_new ();
	camel_folder_change_info_remove_uid (changes, "c");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);
	wait_for_folder_changed (view);

	/* Same as step 1 */
	{
		const NodeCheck expected[] = {
			{ "b", 0, TRUE },
			{ "d", 1, FALSE },
			{ "h", 1, FALSE },
		};
		verify_view_structure (view, expected, G_N_ELEMENTS (expected), "step 3");
	}

	/* Step 4: Add C, F -> b(0)->{c(1)->{d(2),h(2)}, f(1)} */
	test_add_messages (folder,
		"uid", "c", "subject", "C", "from", "c@test", "dsent", (gint64) 3000, "part", part_c, "",
		"uid", "f", "subject", "F", "from", "f@test", "dsent", (gint64) 6000, "part", part_f, "",
		NULL);
	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "c");
	camel_folder_change_info_add_uid (changes, "f");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);
	wait_for_folder_changed (view);

	{
		const NodeCheck expected[] = {
			{ "b", 0, TRUE },
			{ "c", 1, TRUE },
			{ "d", 2, FALSE },
			{ "h", 2, FALSE },
			{ "f", 1, FALSE },
		};
		verify_view_structure (view, expected, G_N_ELEMENTS (expected), "step 4");
	}

	/* Step 5: sorted sub-levels */
	test_add_messages (folder,
		"uid", "g", "subject", "G", "from", "g@test", "dsent", (gint64) 7000, "part", part_g, "",
		NULL);
	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "g");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);
	wait_for_folder_changed (view);

	/* b(0)->{c(1)->d(2), f(1)->g(2)->h(3)} sorted by date */
	{
		const NodeCheck expected[] = {
			{ "b", 0, TRUE },
			{ "c", 1, TRUE },
			{ "d", 2, FALSE },
			{ "f", 1, TRUE },
			{ "g", 2, TRUE },
			{ "h", 3, FALSE },
		};
		verify_view_structure (view, expected, G_N_ELEMENTS (expected), "step 5");
	}

	/* Step 6: Remove G -> same as step 4 */
	camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), "g");
	changes = camel_folder_change_info_new ();
	camel_folder_change_info_remove_uid (changes, "g");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);
	wait_for_folder_changed (view);

	/* Same as step 4 */
	{
		const NodeCheck expected[] = {
			{ "b", 0, TRUE },
			{ "c", 1, TRUE },
			{ "d", 2, FALSE },
			{ "h", 2, FALSE },
			{ "f", 1, FALSE },
		};
		verify_view_structure (view, expected, G_N_ELEMENTS (expected), "step 6");
	}

	/* Step 7: Add G, A -> a(0)->{f(1)->g(2)->h(3), b(1)->c(2)->d(3)} */
	test_add_messages (folder,
		"uid", "g", "subject", "G", "from", "g@test", "dsent", (gint64) 7000, "part", part_g, "",
		"uid", "a", "subject", "A", "from", "a@test", "dsent", (gint64) 1000, "part", part_a, "",
		NULL);
	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "g");
	camel_folder_change_info_add_uid (changes, "a");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);
	wait_for_folder_changed (view);

	/* a(0)->{b(1)->c(2)->d(3), f(1)->g(2)->h(3)} sorted by date */
	{
		const NodeCheck expected[] = {
			{ "a", 0, TRUE },
			{ "b", 1, TRUE },
			{ "c", 2, TRUE },
			{ "d", 3, FALSE },
			{ "f", 1, TRUE },
			{ "g", 2, TRUE },
			{ "h", 3, FALSE },
		};
		verify_view_structure (view, expected, G_N_ELEMENTS (expected), "step 7");
	}

	/* Step 8: Add I -> a(0)->{f(1)->g(2)->h(3)->i(4), b(1)->c(2)->d(3)} */
	test_add_messages (folder,
		"uid", "i", "subject", "I", "from", "i@test", "dsent", (gint64) 9000, "part", part_i, "",
		NULL);
	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "i");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);
	wait_for_folder_changed (view);

	{
		const NodeCheck expected[] = {
			{ "a", 0, TRUE },
			{ "b", 1, TRUE },
			{ "c", 2, TRUE },
			{ "d", 3, FALSE },
			{ "f", 1, TRUE },
			{ "g", 2, TRUE },
			{ "h", 3, TRUE },
			{ "i", 4, FALSE },
		};
		verify_view_structure (view, expected, G_N_ELEMENTS (expected), "step 8");
	}

	/* Step 9: Remove H -> a(0)->{f(1)->g(2), b(1)->c(2)->d(3)->i(4)} */
	camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), "h");
	changes = camel_folder_change_info_new ();
	camel_folder_change_info_remove_uid (changes, "h");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);
	wait_for_folder_changed (view);

	{
		const NodeCheck expected[] = {
			{ "a", 0, TRUE },
			{ "b", 1, TRUE },
			{ "c", 2, TRUE },
			{ "d", 3, TRUE },
			{ "i", 4, FALSE },
			{ "f", 1, TRUE },
			{ "g", 2, FALSE },
		};
		verify_view_structure (view, expected, G_N_ELEMENTS (expected), "step 9");
	}

	/* Step 10: Add H -> same as step 8 */
	test_add_messages (folder,
		"uid", "h", "subject", "H", "from", "h@test", "dsent", (gint64) 8000, "part", part_h, "",
		NULL);
	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "h");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);
	wait_for_folder_changed (view);

	/* Same as step 8 */
	{
		const NodeCheck expected[] = {
			{ "a", 0, TRUE },
			{ "b", 1, TRUE },
			{ "c", 2, TRUE },
			{ "d", 3, FALSE },
			{ "f", 1, TRUE },
			{ "g", 2, TRUE },
			{ "h", 3, TRUE },
			{ "i", 4, FALSE },
		};
		verify_view_structure (view, expected, G_N_ELEMENTS (expected), "step 10");
	}

	/* Step 11: Remove G -> a(0)->{f(1), b(1)->c(2)->{h(3)->i(4),d(3)}} */
	camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), "g");
	changes = camel_folder_change_info_new ();
	camel_folder_change_info_remove_uid (changes, "g");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);
	wait_for_folder_changed (view);

	{
		const NodeCheck expected[] = {
			{ "a", 0, TRUE },
			{ "b", 1, TRUE },
			{ "c", 2, TRUE },
			{ "d", 3, FALSE },
			{ "h", 3, TRUE },
			{ "i", 4, FALSE },
			{ "f", 1, FALSE },
		};
		verify_view_structure (view, expected, G_N_ELEMENTS (expected), "step 11");
	}

	/* Step 12: Remove A -> b(0)->{c(1)->{d(2),h(2)->i(3)}, f(1)} */
	camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), "a");
	changes = camel_folder_change_info_new ();
	camel_folder_change_info_remove_uid (changes, "a");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);
	wait_for_folder_changed (view);

	{
		const NodeCheck expected[] = {
			{ "b", 0, TRUE },
			{ "c", 1, TRUE },
			{ "d", 2, FALSE },
			{ "h", 2, TRUE },
			{ "i", 3, FALSE },
			{ "f", 1, FALSE },
		};
		verify_view_structure (view, expected, G_N_ELEMENTS (expected), "step 12");
	}

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);
	g_free (part_d);
	g_free (part_f);
	g_free (part_g);
	g_free (part_h);
	g_free (part_i);
}

static void
test_folder_view_phantom_promotion_keeps_dependents (void)
{
	/* "b" and "f" both reply to the same missing ancestor "a" - the
	 * phantom placeholder for "a" gets collapsed and one of them
	 * promoted to stand in as the visible group head. "b" already has
	 * a real dependent ("c", which "d" and "h" thread under) from the
	 * ordinary reference-based pass that runs before this promotion;
	 * that subtree must stay intact and correctly sorted regardless of
	 * whether "b" or "f" ends up being the one promoted. */
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;

	guint64 refs_b[] = { TID_A };
	guint64 refs_c[] = { TID_B, TID_A };
	guint64 refs_d[] = { TID_C, TID_A };
	guint64 refs_f[] = { TID_A, TID_B };
	guint64 refs_h[] = { TID_G, TID_C };

	gchar *part_b = test_build_part_string (TID_B, refs_b, 1);
	gchar *part_c = test_build_part_string (TID_C, refs_c, 2);
	gchar *part_d = test_build_part_string (TID_D, refs_d, 2);
	gchar *part_f = test_build_part_string (TID_F, refs_f, 2);
	gchar *part_h = test_build_part_string (TID_H, refs_h, 2);

	store = test_store_new ();
	folder = create_test_folder (store, "f2");

	test_add_messages (folder,
		"uid", "b", "subject", "B", "from", "b@test", "dsent", (gint64) 2000, "part", part_b, "",
		"uid", "d", "subject", "D", "from", "d@test", "dsent", (gint64) 4000, "part", part_d, "",
		"uid", "h", "subject", "H", "from", "h@test", "dsent", (gint64) 8000, "part", part_h, "",
		"uid", "c", "subject", "C", "from", "c@test", "dsent", (gint64) 3000, "part", part_c, "",
		"uid", "f", "subject", "F", "from", "f@test", "dsent", (gint64) 6000, "part", part_f, "",
		NULL);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	{
		const NodeCheck expected[] = {
			{ "b", 0, TRUE },
			{ "c", 1, TRUE },
			{ "d", 2, FALSE },
			{ "h", 2, FALSE },
			{ "f", 1, FALSE },
		};
		verify_view_structure (view, expected, G_N_ELEMENTS (expected), "fresh rebuild");
	}

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);

	g_free (part_b);
	g_free (part_c);
	g_free (part_d);
	g_free (part_f);
	g_free (part_h);
}

static void
test_folder_view_threaded_change_only_keeps_shape (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	GArray *inserted_ranges;
	GArray *removed_ranges;
	GArray *changed_ranges;
	gulong inserted_id, removed_id, changed_id;
	guint64 refs_b[] = { MSG_ID_A };
	gchar *part_a = test_build_part_string (MSG_ID_A, NULL, 0);
	gchar *part_b = test_build_part_string (MSG_ID_B, refs_b, 1);
	gchar *part_c = test_build_part_string (MSG_ID_C, NULL, 0);
	guint ii, changed_row = G_MAXUINT;
	RowRange expected_changed[1];
	const NodeCheck expected[] = {
		{ "a", 0, TRUE },
		{ "b", 1, FALSE },
		{ "c", 0, FALSE },
	};

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	test_add_messages (folder,
		"uid", "a", "subject", "Root A", "from", "a@test", "dsent", (gint64) 1000, "part", part_a, "",
		"uid", "b", "subject", "Re: Root A", "from", "b@test", "dsent", (gint64) 2000, "part", part_b, "",
		"uid", "c", "subject", "Standalone C", "from", "c@test", "dsent", (gint64) 3000, "part", part_c, "",
		NULL);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	verify_view_structure (view, expected, G_N_ELEMENTS (expected), "before change");

	for (ii = 0; ii < camel_folder_view_get_row_count (view); ii++) {
		CamelFolderViewRow *info = camel_folder_view_get_row (view, ii);

		if (g_str_has_suffix (camel_folder_view_row_get_uid (info), "b")) {
			changed_row = ii;
			break;
		}
	}
	g_assert_cmpuint (changed_row, !=, G_MAXUINT);

	inserted_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	removed_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	changed_ranges = g_array_new (FALSE, FALSE, sizeof (RowRange));
	inserted_id = g_signal_connect (view, "rows-inserted", G_CALLBACK (capture_row_range_cb), inserted_ranges);
	removed_id = g_signal_connect (view, "rows-removed", G_CALLBACK (capture_row_range_cb), removed_ranges);
	changed_id = g_signal_connect (view, "rows-changed", G_CALLBACK (capture_row_range_cb), changed_ranges);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_change_uid (changes, "b");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	verify_view_structure (view, expected, G_N_ELEMENTS (expected), "after change");

	expected_changed[0].first = changed_row;
	expected_changed[0].last = changed_row;

	g_assert_cmpuint (removed_ranges->len, ==, 0);
	g_assert_cmpuint (inserted_ranges->len, ==, 0);
	assert_row_ranges (changed_ranges, expected_changed, 1, "threaded_change_only (changed)");

	g_signal_handler_disconnect (view, inserted_id);
	g_signal_handler_disconnect (view, removed_id);
	g_signal_handler_disconnect (view, changed_id);
	g_array_unref (inserted_ranges);
	g_array_unref (removed_ranges);
	g_array_unref (changed_ranges);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);
}

/* Test: Compressed threading depth */

#define DEEP_ID_A  10000
#define DEEP_ID_B  20000
#define DEEP_ID_C  30000
#define DEEP_ID_D  40000
#define DEEP_ID_E  50000

static void
add_deep_chain_messages (CamelFolder *folder)
{
	guint64 refs_b[] = { DEEP_ID_A };
	guint64 refs_c[] = { DEEP_ID_B, DEEP_ID_A };
	guint64 refs_d[] = { DEEP_ID_C, DEEP_ID_B, DEEP_ID_A };
	guint64 refs_e[] = { DEEP_ID_D, DEEP_ID_C, DEEP_ID_B, DEEP_ID_A };

	gchar *part_a = test_build_part_string (DEEP_ID_A, NULL, 0);
	gchar *part_b = test_build_part_string (DEEP_ID_B, refs_b, 1);
	gchar *part_c = test_build_part_string (DEEP_ID_C, refs_c, 2);
	gchar *part_d = test_build_part_string (DEEP_ID_D, refs_d, 3);
	gchar *part_e = test_build_part_string (DEEP_ID_E, refs_e, 4);

	/* Straight chain: A -> B -> C -> D -> E */
	test_add_messages (folder,
		"uid", "a", "subject", "Root", "from", "a@test",
		"dsent", (gint64) 1000, "part", part_a, "",
		"uid", "b", "subject", "Re: Root", "from", "b@test",
		"dsent", (gint64) 2000, "part", part_b, "",
		"uid", "c", "subject", "Re: Root", "from", "c@test",
		"dsent", (gint64) 3000, "part", part_c, "",
		"uid", "d", "subject", "Re: Root", "from", "d@test",
		"dsent", (gint64) 4000, "part", part_d, "",
		"uid", "e", "subject", "Re: Root", "from", "e@test",
		"dsent", (gint64) 5000, "part", part_e, "",
		NULL);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);
	g_free (part_d);
	g_free (part_e);
}

static void
test_folder_view_compressed_depth (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderViewRow *info;
	CamelFolderViewRow *info_d;
	CamelFolderViewRow *info_e;
	CamelFolderViewRow *loop_info;
	guint full_depths[6];
	gboolean found_compression;
	guint ii;

	store = test_store_new ();

	/* Part 1: Use threaded messages (A->B->C, D->E, F) -- short threads,
	 * compressed depth must match full depth for root, children, etc. */
	folder = create_test_folder (store, "f1");
	add_threaded_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_COMPRESSED);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 6);

	/* Root "a" must be at depth 0 */
	info = camel_folder_view_get_row (view, 0);

	g_assert_nonnull (info);
	g_assert_cmpstr (camel_folder_view_row_get_uid (info), ==, "a");
	g_assert_cmpuint (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info)), ==, 0);

	/* Child "b" at depth 1 */
	info = camel_folder_view_get_row (view, 1);

	g_assert_nonnull (info);
	g_assert_cmpstr (camel_folder_view_row_get_uid (info), ==, "b");
	g_assert_cmpuint (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info)), ==, 1);

	/* Grandchild "c" at depth 2 */
	info = camel_folder_view_get_row (view, 2);

	g_assert_nonnull (info);
	g_assert_cmpstr (camel_folder_view_row_get_uid (info), ==, "c");
	g_assert_cmpuint (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info)), ==, 2);

	/* Root "d" at depth 0, child "e" at depth 1 */
	info_d = camel_folder_view_get_row (view, 3);

	info_e = camel_folder_view_get_row (view, 4);

	g_assert_nonnull (info_d);
	g_assert_nonnull (info_e);
	g_assert_cmpstr (camel_folder_view_row_get_uid (info_d), ==, "d");
	g_assert_cmpstr (camel_folder_view_row_get_uid (info_e), ==, "e");
	g_assert_cmpuint (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info_d)), ==, 0);
	g_assert_cmpuint (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info_e)), ==, 1);

	/* Standalone "f" at depth 0 */
	info = camel_folder_view_get_row (view, 5);

	g_assert_nonnull (info);
	g_assert_cmpstr (camel_folder_view_row_get_uid (info), ==, "f");
	g_assert_cmpuint (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info)), ==, 0);

	/* Compressed depth must never exceed full depth */
	camel_folder_view_freeze (view);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_thaw (view);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	for (ii = 0; ii < 6; ii++) {
		loop_info = camel_folder_view_get_row (view, ii);
		full_depths[ii] = camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (loop_info));
	}

	camel_folder_view_freeze (view);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_COMPRESSED);
	camel_folder_view_thaw (view);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	for (ii = 0; ii < 6; ii++) {
		guint comp;

		loop_info = camel_folder_view_get_row (view, ii);
		comp = camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (loop_info));
		if (comp > full_depths[ii]) {
			g_error ("row %u (uid '%s'): compressed depth %u > full depth %u",
				ii, camel_folder_view_row_get_uid (loop_info),
				comp, full_depths[ii]);
		}
	}

	g_clear_object (&view);
	g_clear_object (&folder);

	/* Part 2: Deep straight chain A->B->C->D->E -- compression should
	 * reduce depth for intermediate nodes */
	folder = create_test_folder (store, "f2");
	add_deep_chain_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_COMPRESSED);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 5);

	/* Root must be depth 0 */
	info = camel_folder_view_get_row (view, 0);

	g_assert_cmpuint (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info)), ==, 0);

	/* First child must be depth 1 */
	info = camel_folder_view_get_row (view, 1);

	g_assert_cmpuint (camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (info)), ==, 1);

	/* Compressed depths must be <= full depths, and at least one
	 * intermediate node should have strictly less (compression) */
	camel_folder_view_freeze (view);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_thaw (view);
	camel_folder_view_rebuild_sync (view, NULL, NULL);
	found_compression = FALSE;

	for (ii = 0; ii < 5; ii++) {
		loop_info = camel_folder_view_get_row (view, ii);
		full_depths[ii] = camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (loop_info));
	}

	camel_folder_view_freeze (view);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_COMPRESSED);
	camel_folder_view_thaw (view);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	for (ii = 0; ii < 5; ii++) {
		guint comp;

		loop_info = camel_folder_view_get_row (view, ii);
		comp = camel_folder_view_get_depth (view, camel_folder_view_row_get_uid (loop_info));
		if (comp > full_depths[ii]) {
			g_error ("deep chain row %u: compressed depth %u > full depth %u",
				ii, comp, full_depths[ii]);
		}
		if (comp < full_depths[ii])
			found_compression = TRUE;
	}

	g_assert_true (found_compression);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

/* Replicates get_date_group_label() logic so the test can compute
   expected labels for arbitrary timestamps regardless of when it runs. */
static gchar *
compute_expected_label (gint64 msg_date,
                        gint64 now)
{
	GDateTime *dt_now, *dt_msg;
	gint now_year, now_month, now_day;
	gint msg_year, msg_month;
	gint prev_month, prev_month_year;
	static const gchar * const month_names[] = {
		"January", "February", "March", "April",
		"May", "June", "July", "August",
		"September", "October", "November", "December"
	};

	if (msg_date <= 0)
		return g_strdup ("Older");

	if (msg_date > now)
		return g_strdup ("Future");

	dt_now = g_date_time_new_from_unix_local (now);
	dt_msg = g_date_time_new_from_unix_local (msg_date);

	now_year = g_date_time_get_year (dt_now);
	now_month = g_date_time_get_month (dt_now);
	now_day = g_date_time_get_day_of_month (dt_now);
	msg_year = g_date_time_get_year (dt_msg);
	msg_month = g_date_time_get_month (dt_msg);

	g_date_time_unref (dt_now);
	g_date_time_unref (dt_msg);

	if (msg_year == now_year && msg_month == now_month) {
		gint64 diff = now - msg_date;

		if (diff < 86400)
			return g_strdup ("Today");
		if (diff < 172800)
			return g_strdup ("Yesterday");
		if (diff < 604800)
			return g_strdup ("Last 7 Days");

		return g_strdup ("This Month");
	}

	if (now - msg_date < 172800)
		return g_strdup ("Yesterday");

	if (now - msg_date < 604800 && now_day <= 7)
		return g_strdup ("Last 7 Days");

	if (now_month > 1) {
		prev_month = now_month - 1;
		prev_month_year = now_year;
	} else {
		prev_month = 12;
		prev_month_year = now_year - 1;
	}

	if (msg_year == prev_month_year && msg_month == prev_month)
		return g_strdup ("Last Month");

	if (msg_year == now_year)
		return g_strdup (month_names[msg_month - 1]);

	if (msg_year >= now_year - 5)
		return g_strdup_printf ("%d", msg_year);

	return g_strdup ("Older");
}

static gint64
make_timestamp (gint year,
                gint month,
                gint day,
                gint hour)
{
	GDateTime *dt;
	gint64 result;

	dt = g_date_time_new_local (year, month, day, hour, 0, 0);
	result = g_date_time_to_unix (dt);
	g_date_time_unref (dt);

	return result;
}

static void
test_folder_view_group_by_date_labels (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	GDateTime *dt_now, *dt_prev_month, *dt_early_month;
	GHashTable *seen_labels, *distinct;
	const gchar *current_group;
	CamelFolderViewRow *row_info;
	gchar *row_label;
	gint64 now, dsent;
	gint now_year, now_month, now_day;
	gint prev_month, prev_month_year;
	gint early_month;
	gint64 timestamps[9];
	gchar *expected_labels[9];
	gchar *parts[9];
	guint n_msgs = 0;
	guint ii, n_groups, n_distinct, total_rows;

	now = g_get_real_time () / G_USEC_PER_SEC;

	dt_now = g_date_time_new_from_unix_local (now);
	now_year = g_date_time_get_year (dt_now);
	now_month = g_date_time_get_month (dt_now);
	now_day = g_date_time_get_day_of_month (dt_now);
	g_date_time_unref (dt_now);

	/* 0: Future */
	timestamps[n_msgs] = now + 86400;
	n_msgs++;

	/* 1: Today */
	timestamps[n_msgs] = now - 3600;
	n_msgs++;

	/* 2: Yesterday - go back 25 hours */
	timestamps[n_msgs] = now - 25 * 3600;
	n_msgs++;

	/* 3: Last 7 Days - 4 days ago, noon */
	timestamps[n_msgs] = now - 4 * 86400;
	n_msgs++;

	/* 4: This Month - 15 days ago; only if day-of-month > 15
	   so that this stays in the same month and is older than 7 days */
	if (now_day > 15) {
		timestamps[n_msgs] = make_timestamp (now_year, now_month, 1, 12);
		n_msgs++;
	}

	/* 5: Last Month - middle of previous month */
	if (now_month > 1) {
		prev_month = now_month - 1;
		prev_month_year = now_year;
	} else {
		prev_month = 12;
		prev_month_year = now_year - 1;
	}
	dt_prev_month = g_date_time_new_local (prev_month_year, prev_month, 15, 12, 0, 0);
	timestamps[n_msgs] = g_date_time_to_unix (dt_prev_month);
	g_date_time_unref (dt_prev_month);
	n_msgs++;

	/* 6: Earlier month this year (month name) - only if there
	   is a month before "Last Month" still in the current year */
	early_month = (now_month > 1) ? now_month - 2 : 0;
	if (early_month >= 1) {
		dt_early_month = g_date_time_new_local (now_year, early_month, 15, 12, 0, 0);
		timestamps[n_msgs] = g_date_time_to_unix (dt_early_month);
		g_date_time_unref (dt_early_month);
		n_msgs++;
	}

	/* 7: Previous year */
	timestamps[n_msgs] = make_timestamp (now_year - 1, 6, 15, 12);
	n_msgs++;

	/* 8: Older - 7 years ago */
	timestamps[n_msgs] = make_timestamp (now_year - 7, 6, 15, 12);
	n_msgs++;

	/* Compute expected labels for each timestamp */
	for (ii = 0; ii < n_msgs; ii++) {
		expected_labels[ii] = compute_expected_label (timestamps[ii], now);
	}

	/* Build parts and add messages */
	for (ii = 0; ii < n_msgs; ii++) {
		parts[ii] = test_build_part_string (1000 + ii, NULL, 0);
	}

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	for (ii = 0; ii < n_msgs; ii++) {
		gchar uid_buf[16];

		g_snprintf (uid_buf, sizeof (uid_buf), "m%u", ii);
		test_add_messages (folder,
			"uid", uid_buf,
			"subject", expected_labels[ii],
			"from", "test@test.com",
			"dsent", timestamps[ii],
			"dreceived", timestamps[ii],
			"size", (guint32) 100,
			"part", parts[ii],
			"",
			NULL);
	}

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_group_by (view, CAMEL_FOLDER_VIEW_GROUP_BY_DATE_SENT);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_DESCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	total_rows = camel_folder_view_get_row_count (view);

	/* Collect group labels from the view */
	seen_labels = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	n_groups = 0;

	for (ii = 0; ii < total_rows; ii++) {
		if (camel_folder_view_is_group_row (view, ii)) {
			const gchar *label = camel_folder_view_get_group_label (view, ii);

			g_assert_nonnull (label);
			/* Each group label must appear exactly once */
			g_assert_false (g_hash_table_contains (seen_labels, label));
			g_hash_table_add (seen_labels, g_strdup (label));
			n_groups++;
		}
	}

	/* Every expected label must have a corresponding group */
	for (ii = 0; ii < n_msgs; ii++) {
		if (!g_hash_table_contains (seen_labels, expected_labels[ii])) {
			g_error ("Expected group '%s' (msg %u) not found in view",
				expected_labels[ii], ii);
		}
	}

	/* Number of groups must match the number of distinct expected labels */
	distinct = g_hash_table_new (g_str_hash, g_str_equal);
	for (ii = 0; ii < n_msgs; ii++) {
		g_hash_table_add (distinct, expected_labels[ii]);
	}
	n_distinct = g_hash_table_size (distinct);
	g_hash_table_unref (distinct);
	g_assert_cmpuint (n_groups, ==, n_distinct);

	/* Total rows = n_msgs (data) + n_groups (headers) */
	g_assert_cmpuint (total_rows, ==, n_msgs + n_groups);

	/* Verify each non-group row is under the correct group */
	current_group = NULL;
	for (ii = 0; ii < total_rows; ii++) {
		if (camel_folder_view_is_group_row (view, ii)) {
			current_group = camel_folder_view_get_group_label (view, ii);
		} else {
			row_info = camel_folder_view_get_row (view, ii);
			g_assert_nonnull (row_info);
			g_assert_nonnull (current_group);

			dsent = camel_folder_view_row_get_date_sent (row_info);
			row_label = compute_expected_label (dsent, now);
			g_assert_cmpstr (current_group, ==, row_label);
			g_free (row_label);
		}
	}

	g_hash_table_unref (seen_labels);

	for (ii = 0; ii < n_msgs; ii++) {
		g_free (expected_labels[ii]);
		g_free (parts[ii]);
	}

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_group_by_date_sparse (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	GDateTime *dt_now;
	GHashTable *seen_labels;
	const gchar *current_group;
	CamelFolderViewRow *row_info;
	gchar *row_label;
	gint64 now, dsent;
	gint now_year, now_month;
	gint64 timestamps[2];
	gchar *expected_labels[2];
	gchar *parts[2];
	guint ii, n_groups, total_rows;
	gint jan_month;

	now = g_get_real_time () / G_USEC_PER_SEC;

	dt_now = g_date_time_new_from_unix_local (now);
	now_year = g_date_time_get_year (dt_now);
	now_month = g_date_time_get_month (dt_now);
	g_date_time_unref (dt_now);

	/* Only two messages: Yesterday and January of this year.
	   All intermediate groups (Last 7 Days, This Month, Last Month,
	   and any months between January and now) must NOT appear. */

	/* 0: Yesterday */
	timestamps[0] = now - 25 * 3600;
	expected_labels[0] = compute_expected_label (timestamps[0], now);

	/* 1: January of this year - if current month is January or
	   February (where January would be "Last Month" or "This Month"),
	   use previous year January instead, which becomes a year label */
	if (now_month > 2) {
		jan_month = 1;
		timestamps[1] = make_timestamp (now_year, jan_month, 10, 12);
	} else {
		timestamps[1] = make_timestamp (now_year - 2, 6, 15, 12);
	}
	expected_labels[1] = compute_expected_label (timestamps[1], now);

	for (ii = 0; ii < 2; ii++) {
		parts[ii] = test_build_part_string (2000 + ii, NULL, 0);
	}

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	for (ii = 0; ii < 2; ii++) {
		gchar uid_buf[16];

		g_snprintf (uid_buf, sizeof (uid_buf), "s%u", ii);
		test_add_messages (folder,
			"uid", uid_buf,
			"subject", expected_labels[ii],
			"from", "test@test.com",
			"dsent", timestamps[ii],
			"dreceived", timestamps[ii],
			"size", (guint32) 100,
			"part", parts[ii],
			"",
			NULL);
	}

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_group_by (view, CAMEL_FOLDER_VIEW_GROUP_BY_DATE_SENT);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_DESCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	total_rows = camel_folder_view_get_row_count (view);

	seen_labels = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	n_groups = 0;

	for (ii = 0; ii < total_rows; ii++) {
		if (camel_folder_view_is_group_row (view, ii)) {
			const gchar *label = camel_folder_view_get_group_label (view, ii);

			g_assert_nonnull (label);
			g_assert_false (g_hash_table_contains (seen_labels, label));
			g_hash_table_add (seen_labels, g_strdup (label));
			n_groups++;
		}
	}

	/* Exactly 2 groups - no empty intermediate groups */
	g_assert_cmpuint (n_groups, ==, 2);
	g_assert_cmpuint (total_rows, ==, 4); /* 2 groups + 2 messages */

	for (ii = 0; ii < 2; ii++) {
		if (!g_hash_table_contains (seen_labels, expected_labels[ii])) {
			g_error ("Expected group '%s' (msg %u) not found in view",
				expected_labels[ii], ii);
		}
	}

	/* Verify each message is under the correct group */
	current_group = NULL;
	for (ii = 0; ii < total_rows; ii++) {
		if (camel_folder_view_is_group_row (view, ii)) {
			current_group = camel_folder_view_get_group_label (view, ii);
		} else {
			row_info = camel_folder_view_get_row (view, ii);
			g_assert_nonnull (row_info);
			g_assert_nonnull (current_group);

			dsent = camel_folder_view_row_get_date_sent (row_info);
			row_label = compute_expected_label (dsent, now);
			g_assert_cmpstr (current_group, ==, row_label);
			g_free (row_label);
		}
	}

	g_hash_table_unref (seen_labels);

	for (ii = 0; ii < 2; ii++) {
		g_free (expected_labels[ii]);
		g_free (parts[ii]);
	}

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_expand_state_save_smaller_and_flip (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view1, *view2;
	CamelFolderViewRow *row;
	gchar *state;
	gchar **lines;
	guint ii, uid_count;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_threaded_messages (folder);

	view1 = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view1, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view1, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view1, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view1), ==, 6);
	g_assert_true (camel_folder_view_is_expandable (view1, "a"));
	g_assert_true (camel_folder_view_is_expandable (view1, "d"));
	g_assert_false (camel_folder_view_is_expandable (view1, "f"));
	g_assert_true (camel_folder_view_get_expanded (view1, "a"));
	g_assert_true (camel_folder_view_get_expanded (view1, "d"));

	camel_folder_view_set_expanded (view1, "a", FALSE);

	state = camel_folder_view_save_expand_state (view1);
	g_assert_nonnull (state);

	lines = g_strsplit (state, "\n", -1);
	g_assert_cmpstr (lines[0], ==, "E");

	uid_count = 0;
	for (ii = 1; lines[ii]; ii++) {
		if (lines[ii][0])
			uid_count++;
	}
	g_assert_cmpuint (uid_count, ==, 1);
	g_strfreev (lines);
	g_free (state);

	camel_folder_view_set_expanded (view1, "d", FALSE);

	state = camel_folder_view_save_expand_state (view1);
	g_assert_nonnull (state);

	lines = g_strsplit (state, "\n", -1);
	g_assert_cmpstr (lines[0], ==, "C");

	uid_count = 0;
	for (ii = 1; lines[ii]; ii++) {
		if (lines[ii][0])
			uid_count++;
	}
	g_assert_cmpuint (uid_count, ==, 1);
	g_strfreev (lines);
	g_free (state);

	camel_folder_view_set_expanded (view1, "a", FALSE);
	camel_folder_view_set_expanded (view1, "d", TRUE);

	g_assert_false (camel_folder_view_get_expanded (view1, "a"));
	g_assert_true (camel_folder_view_get_expanded (view1, "d"));

	state = camel_folder_view_save_expand_state (view1);
	g_assert_nonnull (state);

	view2 = camel_folder_view_new (folder, FALSE);
	camel_folder_view_set_threading (view2, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view2, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view2, NULL, NULL);
	camel_folder_view_load_expand_state (view2, state);

	g_assert_false (camel_folder_view_get_expanded (view2, "a"));
	g_assert_true (camel_folder_view_get_expanded (view2, "d"));

	g_free (state);

	state = camel_folder_view_save_expand_state (view2);
	g_assert_nonnull (state);

	for (ii = 0; ii < camel_folder_view_get_row_count (view1); ii++) {
		row = camel_folder_view_get_row (view1, ii);
		if (row && camel_folder_view_is_expandable (view1, camel_folder_view_row_get_uid (row))) {
			const gchar *uid = camel_folder_view_row_get_uid (row);

			camel_folder_view_set_expanded (view1, uid, TRUE);
		}
	}

	camel_folder_view_load_expand_state (view1, state);
	g_assert_false (camel_folder_view_get_expanded (view1, "a"));
	g_assert_true (camel_folder_view_get_expanded (view1, "d"));

	g_free (state);
	g_clear_object (&view1);
	g_clear_object (&view2);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_expand_state_deep (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view1, *view2;
	gchar *state;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_threaded_messages (folder);

	view1 = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view1, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view1, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view1, NULL, NULL);

	g_assert_true (camel_folder_view_is_expandable (view1, "a"));
	g_assert_true (camel_folder_view_is_expandable (view1, "b"));
	g_assert_false (camel_folder_view_is_expandable (view1, "c"));
	g_assert_true (camel_folder_view_get_expanded (view1, "a"));
	g_assert_true (camel_folder_view_get_expanded (view1, "b"));

	camel_folder_view_set_expanded (view1, "b", FALSE);

	g_assert_true (camel_folder_view_get_expanded (view1, "a"));
	g_assert_false (camel_folder_view_get_expanded (view1, "b"));

	state = camel_folder_view_save_expand_state (view1);
	g_assert_nonnull (state);

	camel_folder_view_set_expanded (view1, "b", TRUE);
	g_assert_true (camel_folder_view_get_expanded (view1, "b"));

	camel_folder_view_load_expand_state (view1, state);
	g_assert_false (camel_folder_view_get_expanded (view1, "b"));
	g_assert_true (camel_folder_view_get_expanded (view1, "a"));

	view2 = camel_folder_view_new (folder, FALSE);
	camel_folder_view_set_threading (view2, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view2, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view2, NULL, NULL);
	camel_folder_view_load_expand_state (view2, state);

	g_assert_true (camel_folder_view_get_expanded (view2, "a"));
	g_assert_false (camel_folder_view_get_expanded (view2, "b"));
	g_assert_true (camel_folder_view_get_expanded (view2, "d"));

	g_free (state);
	g_clear_object (&view1);
	g_clear_object (&view2);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_expand_default_on_child_add (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view_exp, *view_col;
	CamelFolderChangeInfo *changes;
	FolderChangedData data_exp = { 0, }, data_col = { 0, };
	gchar *part_a, *part_b;
	guint64 refs_b[] = { MSG_ID_A };
	gulong handler_exp, handler_col;
	guint timeout_id;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	part_a = test_build_part_string (MSG_ID_A, NULL, 0);
	test_add_messages (folder,
		"uid", "a", "subject", "Root msg", "from", "alice@test.com",
		"dsent", (gint64) 1000000, "dreceived", (gint64) 1000010,
		"part", part_a, "",
		NULL);
	g_free (part_a);

	view_exp = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view_exp, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view_exp, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view_exp, NULL, NULL);

	view_col = camel_folder_view_new (folder, FALSE);
	camel_folder_view_set_threading (view_col, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_sort (view_col, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view_col, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view_exp), ==, 1);
	g_assert_cmpuint (camel_folder_view_get_row_count (view_col), ==, 1);
	g_assert_false (camel_folder_view_is_expandable (view_exp, "a"));
	g_assert_false (camel_folder_view_is_expandable (view_col, "a"));

	part_b = test_build_part_string (MSG_ID_B, refs_b, 1);
	test_add_messages (folder,
		"uid", "b", "subject", "Re: Root msg", "from", "bob@test.com",
		"dsent", (gint64) 1000100, "dreceived", (gint64) 1000110,
		"part", part_b, "",
		NULL);
	g_free (part_b);

	data_exp.view = view_exp;
	data_col.view = view_col;

	handler_exp = g_signal_connect (view_exp, "folder-changed",
		G_CALLBACK (view_folder_changed_cb), &data_exp);
	handler_col = g_signal_connect (view_col, "folder-changed",
		G_CALLBACK (view_folder_changed_cb), &data_col);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, "b");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	timeout_id = g_timeout_add_seconds (DELAY_TIMEOUT_SECONDS, test_util_abort_on_timeout_cb, NULL);
	while (!data_exp.done || !data_col.done) {
		g_main_context_iteration (NULL, TRUE);
	}
	g_assert_true (g_source_remove (timeout_id));

	g_signal_handler_disconnect (view_exp, handler_exp);
	g_signal_handler_disconnect (view_col, handler_col);

	g_assert_cmpuint (camel_folder_view_get_row_count (view_exp), ==, 2);
	g_assert_true (camel_folder_view_is_expandable (view_exp, "a"));
	g_assert_true (camel_folder_view_get_expanded (view_exp, "a"));

	g_assert_true (camel_folder_view_is_expandable (view_col, "a"));
	g_assert_false (camel_folder_view_get_expanded (view_col, "a"));
	g_assert_cmpuint (camel_folder_view_get_row_count (view_col), ==, 1);

	g_clear_object (&view_exp);
	g_clear_object (&view_col);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static gboolean
folder_view_has_uid (CamelFolderView *view,
		     const gchar *uid)
{
	guint ii;

	for (ii = 0; ii < camel_folder_view_get_row_count (view); ii++) {
		CamelFolderViewRow *row = camel_folder_view_get_row (view, ii);
		gboolean found;

		if (!row)
			continue;

		found = g_strcmp0 (camel_folder_view_row_get_uid (row), uid) == 0;

		if (found)
			return TRUE;
	}

	return FALSE;
}

static void
test_folder_view_threaded_show_deleted_removes_immediately (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	CamelMessageInfo *info;
	gchar *part_a = test_build_part_string (MSG_ID_A, NULL, 0);
	gchar *part_b = test_build_part_string (MSG_ID_B, NULL, 0);
	gchar *part_c = test_build_part_string (MSG_ID_C, NULL, 0);

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	test_add_messages (folder,
		"uid", "a", "subject", "Alpha",
		"dsent", (gint64) 1000, "part", part_a, "",
		"uid", "b", "subject", "Beta",
		"dsent", (gint64) 2000, "part", part_b, "",
		"uid", "c", "subject", "Gamma",
		"dsent", (gint64) 3000, "part", part_c,
		NULL);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_show_deleted (view, FALSE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 3);
	g_assert_true (folder_view_has_uid (view, "b"));

	info = camel_folder_get_message_info (folder, "b");
	g_assert_nonnull (info);
	camel_message_info_set_flags (info, CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);
	g_clear_object (&info);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_change_uid (changes, "b");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 2);
	g_assert_false (folder_view_has_uid (view, "b"));
	g_assert_true (folder_view_has_uid (view, "a"));
	g_assert_true (folder_view_has_uid (view, "c"));

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_threaded_arbitrary_filter_change_stays_lazy (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	CamelMessageInfo *info;
	gchar *part_a = test_build_part_string (MSG_ID_A, NULL, 0);
	gchar *part_b = test_build_part_string (MSG_ID_B, NULL, 0);
	gchar *part_c = test_build_part_string (MSG_ID_C, NULL, 0);

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	test_add_messages (folder,
		"uid", "a", "subject", "Alpha",
		"dsent", (gint64) 1000, "part", part_a, "",
		"uid", "b", "subject", "Beta",
		"dsent", (gint64) 2000, "part", part_b, "",
		"uid", "c", "subject", "Gamma",
		"dsent", (gint64) 3000, "part", part_c,
		NULL);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);

	/* A saved "unread only" style search - not one of the built-in
	   show-deleted/show-junk properties. Marking "b" seen makes it
	   stop matching, but the view must NOT drop it immediately: doing
	   so would cascade when the reader auto-marks the next selected
	   message as seen too, silently emptying the folder. */
	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_filter (view, "(match-all (not (system-flag \"seen\")))");
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 3);
	g_assert_true (folder_view_has_uid (view, "b"));

	info = camel_folder_get_message_info (folder, "b");
	g_assert_nonnull (info);
	camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
	g_clear_object (&info);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_change_uid (changes, "b");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 3);
	g_assert_true (folder_view_has_uid (view, "b"));

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_flat_thread_child_removed_immediately (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderChangeInfo *changes;
	CamelMessageInfo *info;
	guint64 refs_b[] = { MSG_ID_A };
	gchar *part_a = test_build_part_string (MSG_ID_A, NULL, 0);
	gchar *part_b = test_build_part_string (MSG_ID_B, refs_b, 1);
	gchar *part_c = test_build_part_string (MSG_ID_C, NULL, 0);

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	test_add_messages (folder,
		"uid", "a", "subject", "Root A",
		"dsent", (gint64) 1000, "part", part_a, "",
		"uid", "b", "subject", "Re: Root A",
		"dsent", (gint64) 2000, "part", part_b, "",
		"uid", "c", "subject", "Standalone C",
		"dsent", (gint64) 3000, "part", part_c,
		NULL);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FLAT);
	camel_folder_view_set_show_deleted (view, FALSE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 3);
	g_assert_cmpuint (camel_folder_view_get_depth (view, "b"), ==, 1);

	info = camel_folder_get_message_info (folder, "b");
	g_assert_nonnull (info);
	camel_message_info_set_flags (info, CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);
	g_clear_object (&info);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_change_uid (changes, "b");
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	wait_for_folder_changed (view);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 2);
	g_assert_false (folder_view_has_uid (view, "b"));
	g_assert_true (folder_view_has_uid (view, "a"));
	g_assert_true (folder_view_has_uid (view, "c"));

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_ensure_uid_survives_filter (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	gchar *part_a = test_build_part_string (MSG_ID_A, NULL, 0);
	gchar *part_b = test_build_part_string (MSG_ID_B, NULL, 0);
	gchar *part_c = test_build_part_string (MSG_ID_C, NULL, 0);

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	test_add_messages (folder,
		"uid", "a", "subject", "Alpha",
		"dsent", (gint64) 1000, "part", part_a, "",
		"uid", "b", "subject", "Beta",
		"flags", (guint32) CAMEL_MESSAGE_SEEN,
		"dsent", (gint64) 2000, "part", part_b, "",
		"uid", "c", "subject", "Gamma",
		"flags", (guint32) CAMEL_MESSAGE_SEEN,
		"dsent", (gint64) 3000, "part", part_c,
		NULL);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);

	/* "b" is already seen when the view is built, not part of the filter, but it should stay in the view */
	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_filter (view, "(match-all (not (system-flag \"seen\")))");
	camel_folder_view_set_ensure_uid (view, "b");
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 2);
	g_assert_true (folder_view_has_uid (view, "a"));
	g_assert_true (folder_view_has_uid (view, "b"));
	g_assert_false (folder_view_has_uid (view, "c"));

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_filter_flags (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	gchar *part_a = test_build_part_string (MSG_ID_A, NULL, 0);
	gchar *part_b = test_build_part_string (MSG_ID_B, NULL, 0);
	gchar *part_c = test_build_part_string (MSG_ID_C, NULL, 0);
	gchar *part_d = test_build_part_string (MSG_ID_D, NULL, 0);
	gchar *part_e = test_build_part_string (MSG_ID_E, NULL, 0);

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	test_add_messages (folder,
		"uid", "a", "subject", "Normal message", "part", part_a,
		"dsent", (gint64) 1000, "",
		"uid", "b", "subject", "Deleted message", "part", part_b,
		"flags", (guint32) CAMEL_MESSAGE_DELETED,
		"dsent", (gint64) 2000, "",
		"uid", "c", "subject", "Junk message", "part", part_c,
		"flags", (guint32) CAMEL_MESSAGE_JUNK,
		"dsent", (gint64) 3000, "",
		"uid", "d", "subject", "Seen message", "part", part_d,
		"flags", (guint32) CAMEL_MESSAGE_SEEN,
		"dsent", (gint64) 4000, "",
		"uid", "e", "subject", "Junk and deleted", "part", part_e,
		"flags", (guint32) (CAMEL_MESSAGE_JUNK | CAMEL_MESSAGE_DELETED),
		"dsent", (gint64) 5000,
		NULL);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);
	g_free (part_d);
	g_free (part_e);

	/* No filter - all 5 messages */
	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 5);
	g_clear_object (&view);

	/* Filter out deleted - 3 remain (a, c, d) */
	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_filter (view, "(match-all (not (system-flag \"deleted\")))");
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 3);
	g_assert_true (folder_view_has_uid (view, "a"));
	g_assert_true (folder_view_has_uid (view, "c"));
	g_assert_true (folder_view_has_uid (view, "d"));
	g_assert_false (folder_view_has_uid (view, "b"));
	g_assert_false (folder_view_has_uid (view, "e"));
	g_clear_object (&view);

	/* Filter out junk - 3 remain (a, b, d) */
	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_filter (view, "(match-all (not (system-flag \"junk\")))");
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 3);
	g_assert_true (folder_view_has_uid (view, "a"));
	g_assert_true (folder_view_has_uid (view, "b"));
	g_assert_true (folder_view_has_uid (view, "d"));
	g_assert_false (folder_view_has_uid (view, "c"));
	g_assert_false (folder_view_has_uid (view, "e"));
	g_clear_object (&view);

	/* Filter out both junk and deleted - 2 remain (a, d) */
	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_filter (view,
		"(match-all (and (not (system-flag \"junk\")) (not (system-flag \"deleted\"))))");
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 2);
	g_assert_true (folder_view_has_uid (view, "a"));
	g_assert_true (folder_view_has_uid (view, "d"));
	g_clear_object (&view);

	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_filter_combined (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	gchar *part_a = test_build_part_string (MSG_ID_A, NULL, 0);
	gchar *part_b = test_build_part_string (MSG_ID_B, NULL, 0);
	gchar *part_c = test_build_part_string (MSG_ID_C, NULL, 0);
	gchar *part_d = test_build_part_string (MSG_ID_D, NULL, 0);

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	test_add_messages (folder,
		"uid", "a", "subject", "Report draft", "part", part_a,
		"flags", (guint32) CAMEL_MESSAGE_DELETED,
		"dsent", (gint64) 1000, "",
		"uid", "b", "subject", "Report final", "part", part_b,
		"dsent", (gint64) 2000, "",
		"uid", "c", "subject", "Meeting notes", "part", part_c,
		"dsent", (gint64) 3000, "",
		"uid", "d", "subject", "Report summary", "part", part_d,
		"flags", (guint32) CAMEL_MESSAGE_JUNK,
		"dsent", (gint64) 4000,
		NULL);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);
	g_free (part_d);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_filter (view,
		"(match-all (and "
		"(header-contains \"Subject\" \"Report\") "
		"(not (system-flag \"deleted\")) "
		"(not (system-flag \"junk\"))))");
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 1);
	g_assert_true (folder_view_has_uid (view, "b"));
	g_clear_object (&view);

	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_filter_threaded (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderViewRow *row;
	guint64 refs_b[] = { MSG_ID_A };
	guint64 refs_c[] = { MSG_ID_B, MSG_ID_A };
	gchar *part_a = test_build_part_string (MSG_ID_A, NULL, 0);
	gchar *part_b = test_build_part_string (MSG_ID_B, refs_b, 1);
	gchar *part_c = test_build_part_string (MSG_ID_C, refs_c, 2);
	gchar *part_d = test_build_part_string (MSG_ID_D, NULL, 0);

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	/* Thread: A -> B -> C, standalone: D (deleted) */
	test_add_messages (folder,
		"uid", "a", "subject", "Thread start",
		"dsent", (gint64) 1000, "part", part_a, "",
		"uid", "b", "subject", "Re: Thread start",
		"dsent", (gint64) 2000, "part", part_b, "",
		"uid", "c", "subject", "Re: Thread start",
		"dsent", (gint64) 3000, "part", part_c, "",
		"uid", "d", "subject", "Standalone deleted",
		"flags", (guint32) CAMEL_MESSAGE_DELETED,
		"dsent", (gint64) 4000, "part", part_d,
		NULL);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);
	g_free (part_d);

	/* Threaded view with deleted filter - "d" excluded, thread A->B->C remains */
	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
	camel_folder_view_set_filter (view, "(match-all (not (system-flag \"deleted\")))");
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 3);
	g_assert_true (folder_view_has_uid (view, "a"));
	g_assert_true (folder_view_has_uid (view, "b"));
	g_assert_true (folder_view_has_uid (view, "c"));
	g_assert_false (folder_view_has_uid (view, "d"));

	/* Root should be expandable (has children) */
	row = camel_folder_view_get_row (view, 0);
	g_assert_nonnull (row);
	g_assert_cmpstr (camel_folder_view_row_get_uid (row), ==, "a");
	g_assert_true (camel_folder_view_is_expandable (view, "a"));

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_filter_change_rebuild (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	gchar *part_a = test_build_part_string (MSG_ID_A, NULL, 0);
	gchar *part_b = test_build_part_string (MSG_ID_B, NULL, 0);
	gchar *part_c = test_build_part_string (MSG_ID_C, NULL, 0);

	store = test_store_new ();
	folder = create_test_folder (store, "f1");

	test_add_messages (folder,
		"uid", "a", "subject", "Alpha report",
		"dsent", (gint64) 1000, "part", part_a, "",
		"uid", "b", "subject", "Beta update",
		"dsent", (gint64) 2000, "part", part_b, "",
		"uid", "c", "subject", "Alpha update",
		"dsent", (gint64) 3000, "part", part_c,
		NULL);

	g_free (part_a);
	g_free (part_b);
	g_free (part_c);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);

	/* First filter: subject contains "Alpha" - 2 matches (a, c) */
	camel_folder_view_set_filter (view, "(match-all (header-contains \"Subject\" \"Alpha\"))");
	camel_folder_view_rebuild_sync (view, NULL, NULL);
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 2);
	g_assert_true (folder_view_has_uid (view, "a"));
	g_assert_true (folder_view_has_uid (view, "c"));

	/* Change filter: subject contains "update" - 2 matches (b, c) */
	camel_folder_view_set_filter (view, "(match-all (header-contains \"Subject\" \"update\"))");
	camel_folder_view_rebuild_sync (view, NULL, NULL);
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 2);
	g_assert_true (folder_view_has_uid (view, "b"));
	g_assert_true (folder_view_has_uid (view, "c"));
	g_assert_false (folder_view_has_uid (view, "a"));

	/* Clear filter - all 3 */
	camel_folder_view_set_filter (view, NULL);
	camel_folder_view_rebuild_sync (view, NULL, NULL);
	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 3);

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

static void
test_folder_view_sort_only_resort (void)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderView *view;
	CamelFolderViewRow *row;

	store = test_store_new ();
	folder = create_test_folder (store, "f1");
	add_flat_messages (folder);

	view = camel_folder_view_new (folder, TRUE);
	camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_NONE);
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);
	row = camel_folder_view_get_row (view, 0);
	g_assert_cmpstr (camel_folder_view_row_get_uid (row), ==, "a");

	/* Sort-only change: flip to descending - fast path */
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT, CAMEL_SORT_DESCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);
	row = camel_folder_view_get_row (view, 0);
	g_assert_cmpstr (camel_folder_view_row_get_uid (row), ==, "g");
	row = camel_folder_view_get_row (view, 6);
	g_assert_cmpstr (camel_folder_view_row_get_uid (row), ==, "a");

	/* Sort by subject ascending - fast path, subject was in enabled_columns */
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_SUBJECT, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);
	row = camel_folder_view_get_row (view, 0);
	g_assert_cmpstr (camel_folder_view_row_get_subject (row), ==, "Alpha");
	row = camel_folder_view_get_row (view, 6);
	g_assert_cmpstr (camel_folder_view_row_get_subject (row), ==, "Golf");

	/* Sort-on-off cycle: clear sort, then re-sort by subject desc */
	camel_folder_view_clear_sort (view);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_SUBJECT, CAMEL_SORT_DESCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);
	row = camel_folder_view_get_row (view, 0);
	g_assert_cmpstr (camel_folder_view_row_get_subject (row), ==, "Golf");
	row = camel_folder_view_get_row (view, 6);
	g_assert_cmpstr (camel_folder_view_row_get_subject (row), ==, "Alpha");

	/* Sort by size ascending - fast path, size always populated */
	camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_SIZE, CAMEL_SORT_ASCENDING);
	camel_folder_view_rebuild_sync (view, NULL, NULL);

	g_assert_cmpuint (camel_folder_view_get_row_count (view), ==, 7);
	row = camel_folder_view_get_row (view, 0);
	g_assert_cmpstr (camel_folder_view_row_get_uid (row), ==, "c");

	g_clear_object (&view);
	g_clear_object (&folder);
	g_clear_object (&store);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/-/issues/");

	g_test_add_func ("/Camel/CamelFolderView/FlatNoFilter", test_folder_view_flat_no_filter);
	g_test_add_func ("/Camel/CamelFolderView/FlatWithFilter", test_folder_view_flat_with_filter);
	g_test_add_func ("/Camel/CamelFolderView/Threaded", test_folder_view_threaded);
	g_test_add_func ("/Camel/CamelFolderView/FlatThreads", test_folder_view_flat_threads);
	g_test_add_func ("/Camel/CamelFolderView/ThreadSubject", test_folder_view_thread_subject);
	g_test_add_func ("/Camel/CamelFolderView/Sort", test_folder_view_sort);
	g_test_add_func ("/Camel/CamelFolderView/SortNonDbColumn", test_folder_view_sort_non_db_column);
	g_test_add_func ("/Camel/CamelFolderView/ExpandCollapse", test_folder_view_expand_collapse);
	g_test_add_func ("/Camel/CamelFolderView/ExpandStateSaveSmallerAndFlip", test_folder_view_expand_state_save_smaller_and_flip);
	g_test_add_func ("/Camel/CamelFolderView/ExpandStateDeep", test_folder_view_expand_state_deep);
	g_test_add_func ("/Camel/CamelFolderView/ExpandDefaultOnChildAdd", test_folder_view_expand_default_on_child_add);
	g_test_add_func ("/Camel/CamelFolderView/FolderChangedAdd", test_folder_view_folder_changed_add);
	g_test_add_func ("/Camel/CamelFolderView/FolderChangedRemove", test_folder_view_folder_changed_remove);
	g_test_add_func ("/Camel/CamelFolderView/FolderChangedModify", test_folder_view_folder_changed_modify);
	g_test_add_func ("/Camel/CamelFolderView/RowsInsertedScattered", test_folder_view_rows_inserted_scattered);
	g_test_add_func ("/Camel/CamelFolderView/RowsInsertedAtZeroDescending", test_folder_view_rows_inserted_at_zero_descending);
	g_test_add_func ("/Camel/CamelFolderView/RowsRemovedScattered", test_folder_view_rows_removed_scattered);
	g_test_add_func ("/Camel/CamelFolderView/RowsInsertedContiguous", test_folder_view_rows_inserted_contiguous);
	g_test_add_func ("/Camel/CamelFolderView/RowsRemovedContiguous", test_folder_view_rows_removed_contiguous);
	g_test_add_func ("/Camel/CamelFolderView/RowsInsertedMixed", test_folder_view_rows_inserted_mixed);
	g_test_add_func ("/Camel/CamelFolderView/RowsInsertRemoveChangeCombined", test_folder_view_rows_insert_remove_change_combined);
	g_test_add_func ("/Camel/CamelFolderView/RowsInsertedReverseThreading", test_folder_view_rows_inserted_reverse_threading);
	g_test_add_func ("/Camel/CamelFolderView/ChildArrivesBeforeParent", test_folder_view_child_arrives_before_parent);
	g_test_add_func ("/Camel/CamelFolderView/ChildBeforeParentViaGrandparent", test_folder_view_child_before_parent_via_grandparent);
	g_test_add_func ("/Camel/CamelFolderView/ChildBeforeParentPresentAtInitialBuild", test_folder_view_child_before_parent_present_at_initial_build);
	g_test_add_func ("/Camel/CamelFolderView/OrphansShareMissingAncestor", test_folder_view_orphans_share_missing_ancestor);
	g_test_add_func ("/Camel/CamelFolderView/OrphansShareMissingAncestorSeparateBatches", test_folder_view_orphans_share_missing_ancestor_separate_batches);
	g_test_add_func ("/Camel/CamelFolderView/ThreadSubjectIncremental", test_folder_view_thread_subject_incremental);
	g_test_add_func ("/Camel/CamelFolderView/RowsInsertedIntoCollapsedThread", test_folder_view_rows_inserted_into_collapsed_thread);
	g_test_add_func ("/Camel/CamelFolderView/RowsGroupedHeaderChurn", test_folder_view_rows_grouped_header_churn);
	g_test_add_func ("/Camel/CamelFolderView/ThreadLatest", test_folder_view_thread_latest);
	g_test_add_func ("/Camel/CamelFolderView/GroupByDate", test_folder_view_group_by_date);
	g_test_add_func ("/Camel/CamelFolderView/GroupByDateLabels", test_folder_view_group_by_date_labels);
	g_test_add_func ("/Camel/CamelFolderView/GroupByDateSparse", test_folder_view_group_by_date_sparse);
	g_test_add_func ("/Camel/CamelFolderView/CompressedDepth", test_folder_view_compressed_depth);

	g_test_add_func ("/Camel/CamelFolderView/Threading/PartialAddRemoveRoot", test_folder_view_complex_threading);
	g_test_add_func ("/Camel/CamelFolderView/Threading/Incremental12Steps", test_folder_view_threading_incremental);
	g_test_add_func ("/Camel/CamelFolderView/Threading/PhantomPromotionKeepsDependents", test_folder_view_phantom_promotion_keeps_dependents);
	g_test_add_func ("/Camel/CamelFolderView/Threading/ChangeOnlyKeepsShape", test_folder_view_threaded_change_only_keeps_shape);

	g_test_add_func ("/Camel/CamelFolderView/Vee/FlatNoFilter", test_folder_view_vee_flat_no_filter);
	g_test_add_func ("/Camel/CamelFolderView/Vee/FlatWithFilter", test_folder_view_vee_flat_with_filter);
	g_test_add_func ("/Camel/CamelFolderView/Vee/Threaded", test_folder_view_vee_threaded);
	g_test_add_func ("/Camel/CamelFolderView/Vee/FlatThreads", test_folder_view_vee_flat_threads);
	g_test_add_func ("/Camel/CamelFolderView/Vee/ThreadSubject", test_folder_view_vee_thread_subject);
	g_test_add_func ("/Camel/CamelFolderView/Vee/Sort", test_folder_view_vee_sort);
	g_test_add_func ("/Camel/CamelFolderView/Vee/ExpandCollapse", test_folder_view_vee_expand_collapse);
	g_test_add_func ("/Camel/CamelFolderView/Vee/FolderChangedAdd", test_folder_view_vee_folder_changed_add);
	g_test_add_func ("/Camel/CamelFolderView/Vee/FolderChangedRemove", test_folder_view_vee_folder_changed_remove);
	g_test_add_func ("/Camel/CamelFolderView/Vee/FolderChangedModify", test_folder_view_vee_folder_changed_modify);
	g_test_add_func ("/Camel/CamelFolderView/Vee/GroupByDate", test_folder_view_vee_group_by_date);

	g_test_add_func ("/Camel/CamelFolderView/Threading/ShowDeletedRemovesImmediately", test_folder_view_threaded_show_deleted_removes_immediately);
	g_test_add_func ("/Camel/CamelFolderView/Threading/ArbitraryFilterChangeStaysLazy", test_folder_view_threaded_arbitrary_filter_change_stays_lazy);
	g_test_add_func ("/Camel/CamelFolderView/Threading/FlatThreadChildRemovedImmediately", test_folder_view_flat_thread_child_removed_immediately);
	g_test_add_func ("/Camel/CamelFolderView/EnsureUidSurvivesFilter", test_folder_view_ensure_uid_survives_filter);
	g_test_add_func ("/Camel/CamelFolderView/FilterFlags", test_folder_view_filter_flags);
	g_test_add_func ("/Camel/CamelFolderView/FilterCombined", test_folder_view_filter_combined);
	g_test_add_func ("/Camel/CamelFolderView/FilterThreaded", test_folder_view_filter_threaded);
	g_test_add_func ("/Camel/CamelFolderView/FilterChangeRebuild", test_folder_view_filter_change_rebuild);
	g_test_add_func ("/Camel/CamelFolderView/SortOnlyResort", test_folder_view_sort_only_resort);

	return g_test_run ();
}

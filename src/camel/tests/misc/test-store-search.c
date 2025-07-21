/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <glib.h>

#include "camel/camel.h"

#include "lib-camel-test-utils.c"

#define SIMULTANEOUS_TIMEOUT_SECS 10
#define SIMULTANEOUS_N_REPEATS 100

static void
test_store_search_check_folder_uids (GPtrArray *uids, /* const gchar * */
				     ...) G_GNUC_NULL_TERMINATED;

/* expects const gchar *uid arguments, terminated by NULL */
static void
test_store_search_check_folder_uids (GPtrArray *uids, /* const gchar * */
				     ...)
{
	va_list ap;
	const gchar *uid;
	GHashTable *expected;
	guint ii;

	expected = g_hash_table_new (g_str_hash, g_str_equal);

	va_start (ap, uids);

	uid = va_arg (ap, const gchar *);
	while (uid) {
		g_hash_table_add (expected, (gpointer) uid);

		uid = va_arg (ap, const gchar *);
	}

	va_end (ap);

	for (ii = 0; ii < uids->len; ii++) {
		uid = g_ptr_array_index (uids, ii);

		g_assert_true (g_hash_table_remove (expected, uid));
	}

	/* all expected had been returned */
	g_assert_cmpint (g_hash_table_size (expected), ==, 0);

	g_hash_table_destroy (expected);
}

static void
test_store_search_check_result (CamelStoreSearch *search,
				...) G_GNUC_NULL_TERMINATED;

/* expects pairs of guint32 folder_id, const gchar *uid, terminated by 0, NULL pair */
static void
test_store_search_check_result (CamelStoreSearch *search,
				...)
{
	va_list ap;
	guint32 folder_id;
	const gchar *uid;
	GHashTable *expected; /* gchar *keys ~> NULL */
	GHashTable *uids_per_folder_id; /* guint32 folder_id ~> GPtrArray * { const cghar* uid } */
	gchar *key;
	GPtrArray *items = NULL;
	GError *local_error = NULL;
	guint ii;
	guint expected_total;
	gboolean success;

	expected = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	uids_per_folder_id = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) g_ptr_array_unref);

	#define add_uid(_fid, _uid) G_STMT_START { \
		GPtrArray *uids = g_hash_table_lookup (uids_per_folder_id, GUINT_TO_POINTER (_fid)); \
		if (!uids) { \
			uids = g_ptr_array_new (); \
			g_hash_table_insert (uids_per_folder_id, GUINT_TO_POINTER (_fid), uids); \
		} \
		g_ptr_array_add (uids, (gpointer) _uid); \
		} G_STMT_END

	va_start (ap, search);

	folder_id = va_arg (ap, guint32);
	if (folder_id) {
		uid = va_arg (ap, const gchar *);
		if (uid)
			add_uid (folder_id, uid);
	} else {
		uid = NULL;
	}

	expected_total = folder_id ? 1 : 0;

	while (folder_id && uid) {
		key = g_strdup_printf ("%u-%s", folder_id, uid);
		g_hash_table_add (expected, key);

		folder_id = va_arg (ap, guint32);
		if (folder_id) {
			uid = va_arg (ap, const gchar *);
			if (uid) {
				expected_total++;
				add_uid (folder_id, uid);
			}
		} else {
			uid = NULL;
		}
	}

	va_end (ap);

	#undef add_uid

	success = camel_store_search_get_items_sync (search, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpint (items->len, ==, expected_total);

	for (ii = 0; ii < items->len; ii++) {
		CamelStoreSearchItem *item = g_ptr_array_index (items, ii);

		key = g_strdup_printf ("%u-%s", item->folder_id, item->uid);
		g_assert_true (g_hash_table_remove (expected, key));
		g_free (key);
	}

	/* all expected had been returned */
	g_assert_cmpint (g_hash_table_size (expected), ==, 0);

	g_hash_table_destroy (expected);
	g_clear_pointer (&items, g_ptr_array_unref);

	if (g_hash_table_size (uids_per_folder_id)) {
		CamelStore *store = camel_store_search_get_store (search);
		CamelStoreDB *sdb = camel_store_get_db (store);
		TestSession *test_session;

		items = camel_store_search_list_folders (search);
		g_assert_nonnull (items);

		for (ii = 0; ii < items->len; ii++) {
			TestFolder *test_folder = g_ptr_array_index (items, ii);
			GPtrArray *expected_uids;
			GPtrArray *uids = NULL;
			const gchar *folder_name;
			guint jj;

			g_assert_true (TEST_IS_FOLDER (test_folder));

			folder_name = camel_folder_get_full_name (CAMEL_FOLDER (test_folder));
			folder_id = camel_store_db_get_folder_id (sdb, folder_name);
			g_assert_cmpuint (folder_id, !=, 0);

			expected_uids = g_hash_table_lookup (uids_per_folder_id, GUINT_TO_POINTER (folder_id));

			success = camel_store_search_get_uids_sync (search, folder_name, &uids, NULL, &local_error);
			g_assert_no_error (local_error);
			g_assert_true (success);
			g_assert_nonnull (uids);

			if (expected_uids) {
				g_assert_cmpint (uids->len, ==, expected_uids->len);

				for (jj = 0; jj < uids->len; jj++) {
					uid = g_ptr_array_index (uids, jj);
					g_assert_true (g_ptr_array_find_with_equal_func (expected_uids, uid, g_str_equal, NULL));
				}
			} else {
				g_assert_cmpint (uids->len, ==, 0);
			}

			g_ptr_array_unref (uids);

			/* the call to the camel_store_search_get_uids_sync() doubles counts, thus divide by two */
			test_folder->n_called_get_message_info /= 2;
			test_folder->n_called_get_message /= 2;
			test_folder->n_called_search_header /= 2;
			test_folder->n_called_search_body /= 2;
		}

		g_ptr_array_unref (items);

		test_session = TEST_SESSION (camel_service_ref_session (CAMEL_SERVICE (store)));
		test_session->n_called_addressbook_contains /= 2;
		g_clear_object (&test_session);
	}

	g_hash_table_destroy (uids_per_folder_id);
}

static void
test_store_search_create (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolderThreadFlags thread_flags = 0;
	GPtrArray *items = NULL;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	g_assert_nonnull (store);

	search = camel_store_search_new (store);
	g_assert_nonnull (search);
	g_assert_true (camel_store_search_get_store (search) == store);
	test_store_search_check_result (search, 0, NULL);
	g_assert_cmpint (camel_store_search_get_match_threads_kind (search, &thread_flags), ==, CAMEL_MATCH_THREADS_KIND_NONE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);

	success = camel_store_search_get_items_sync (search, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 0);
	g_clear_pointer (&items, g_ptr_array_unref);

	/* rebuild without expression and folder, it's a no-op */
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);
	g_assert_cmpint (camel_store_search_get_match_threads_kind (search, &thread_flags), ==, CAMEL_MATCH_THREADS_KIND_NONE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);

	/* repeat after no-op rebuild */
	success = camel_store_search_get_items_sync (search, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 0);
	g_clear_pointer (&items, g_ptr_array_unref);

	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_subject (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolder *f1, *f2, *f3;
	GPtrArray *items = NULL;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"subject", "Message 11",
		"",
		"uid", "12",
		"subject", "Message 12",
		"",
		"uid", "13",
		"subject", "Subject 13",
		NULL);
	camel_store_search_add_folder (search, f1);

	/* the second folder has matches, but it's not included in the search */
	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"subject", "Message 21",
		"",
		"uid", "22",
		"subject", "Message 22",
		"",
		"uid", "23",
		"subject", "Subject 23",
		NULL);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"subject", "Different Subject Message",
		NULL);
	camel_store_search_add_folder (search, f3);

	camel_store_search_set_expression (search, "(header-contains \"subject\" \"age\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 3, "31", 0, NULL);

	/* adding folder without calling rebuild results in error when trying to read the items */
	camel_store_search_add_folder (search, f2);
	success = camel_store_search_get_items_sync (search, &items, NULL, &local_error);
	g_assert_error (local_error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
	g_assert_false (success);
	g_assert_null (items);
	g_clear_error (&local_error);

	success = camel_store_search_get_uids_sync (search, "f2", &items, NULL, &local_error);
	g_assert_error (local_error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
	g_assert_false (success);
	g_assert_null (items);
	g_clear_error (&local_error);

	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 2, "21", 2, "22", 3, "31", 0, NULL);

	/* removing folder without calling rebuild results in error when trying to read the items */
	camel_store_search_remove_folder (search, f1);
	success = camel_store_search_get_items_sync (search, &items, NULL, &local_error);
	g_assert_error (local_error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
	g_assert_false (success);
	g_assert_null (items);
	g_clear_error (&local_error);

	success = camel_store_search_get_uids_sync (search, "f2", &items, NULL, &local_error);
	g_assert_error (local_error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
	g_assert_false (success);
	g_assert_null (items);
	g_clear_error (&local_error);

	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "21", 2, "22", 3, "31", 0, NULL);

	/* expression change also requires rebuild */
	camel_store_search_set_expression (search, "(header-ends-with \"subject\" \"2\")");
	success = camel_store_search_get_items_sync (search, &items, NULL, &local_error);
	g_assert_error (local_error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
	g_assert_false (success);
	g_assert_null (items);
	g_clear_error (&local_error);

	success = camel_store_search_get_uids_sync (search, "f1", &items, NULL, &local_error);
	g_assert_error (local_error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
	g_assert_false (success);
	g_assert_null (items);
	g_clear_error (&local_error);

	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "22", 0, NULL);

	/* the folder is not there, but that's fine */
	camel_store_search_remove_folder (search, f1);

	/* the folder is already there, but that's fine too */
	camel_store_search_add_folder (search, f2);

	/* change both expression and the folders and then rebuild */
	camel_store_search_add_folder (search, f1);
	camel_store_search_remove_folder (search, f2);
	camel_store_search_set_expression (search, "(header-starts-with \"subject\" \"mess\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 0, NULL);

	camel_store_search_set_expression (search, "(not (header-starts-with \"subject\" \"mess\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(header-matches \"Subject\" \"subJECt 13\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 0, NULL);

	camel_store_search_add_folder (search, f2);
	camel_store_search_set_expression (search, "(header-has-words \"Subject\" \"messagE subjecT\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(header-has-words \"Subject\" \"esag different\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	camel_store_search_set_expression (search, "(header-has-words \"Subject\" \"message different\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(header-has-words \"Subject\" \"subject message different\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(header-has-words \"Subject\" \"message\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 2, "21", 2, "22", 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(header-soundex \"Subject\" \"mase\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 2, "21", 2, "22", 3, "31", 0, NULL);

	g_clear_object (&f3);
	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "32",
		NULL);
	camel_store_search_remove_folder (search, f2);
	camel_store_search_set_expression (search, "(header-exists \"Subject\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(not (header-exists \"Subject\")))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 3, "32", 0, NULL);

	camel_store_search_add_folder (search, f2);
	camel_store_search_set_expression (search, "(header-regex \"Subject\" \"^.*me.*ge.*$\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 2, "21", 2, "22", 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(header-regex \"Subject\" \"^.*ge [1-3]2$\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 2, "22", 0, NULL);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_addresses (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolder *f1, *f2, *f3;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"from", "loki@no.where",
		"to", "Thor <thor@no.where>",
		"",
		"uid", "12",
		"from", "Gwendoline <gwen@no.where>",
		"cc", "Peter <peter@no.where>",
		"",
		"uid", "13",
		"from", "Bruce <bruce@no.where>",
		"to", "Tony <tony@no.where>, Peeeter <peter@no.where>",
		"mlist", "interested.parties@no.where",
		NULL);
	camel_store_search_add_folder (search, f1);

	/* the second folder is not included in the first search */
	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"from", "spam@no.where",
		"cc", "interested.parties@no.where, I.M. <tony@no.where>",
		"",
		"uid", "22",
		"cc", "spam@no.where",
		"",
		"uid", "23",
		"mlist", "all@no.where",
		NULL);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"from", "Annie <aunt@no.no.no>",
		"to", "Bob <uncle@no.no.no>",
		"cc", "Little <boy@no.no.no>, Gwenie <gwen@no.where>",
		NULL);
	camel_store_search_add_folder (search, f3);

	camel_store_search_set_expression (search, "(header-contains \"from\" \"gwend\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 0, NULL);

	camel_store_search_set_expression (search, "(header-ends-with \"cc\" \"@no.where\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 3, "31", 0, NULL);

	camel_store_search_add_folder (search, f2);
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 2, "21", 2, "22", 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(header-matches \"from\" \"loki\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	camel_store_search_set_expression (search, "(header-matches \"from\" \"gwen@no.where\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 0, NULL);

	camel_store_search_set_expression (search, "(header-contains \"x-camel-mlist\" \"@no.where\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 2, "23", 0, NULL);

	camel_store_search_set_expression (search, "(header-matches \"x-camel-mlist\" \"all@no.where\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "23", 0, NULL);

	camel_store_search_set_expression (search,
		"(or (header-contains \"from\" \"peeeter\")"
		    "(header-contains \"to\" \"peeeter\")"
		    "(header-contains \"cc\" \"peeeter\")"
		")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 0, NULL);

	camel_store_search_set_expression (search,
		"(or (header-starts-with \"from\" \"spam@\")"
		    "(header-starts-with \"to\" \"spam@\")"
		    "(header-starts-with \"cc\" \"spam@\")"
		")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "21", 2, "22", 0, NULL);

	camel_store_search_set_expression (search,
		"(or (header-starts-with \"from\" \"tony@\")"
		    "(header-starts-with \"to\" \"tony@\")"
		    "(header-starts-with \"cc\" \"tony@\")"
		")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 2, "21", 0, NULL);

	camel_store_search_set_expression (search,
		"(and "
		   "(or (header-starts-with \"from\" \"tony@\")"
		      "(header-starts-with \"to\" \"tony@\")"
		      "(header-starts-with \"cc\" \"tony@\")"
		   ")"
		   "(not (header-exists \"x-camel-mlist\")"
		   "))"
		")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "21", 0, NULL);

	camel_store_search_set_expression (search, "(not (header-exists \"cc\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "13", 2, "23", 0, NULL);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_flags (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolder *f1, *f2, *f3;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"flags", (guint32) (0),
		"",
		"uid", "12",
		"flags", (guint32) (CAMEL_MESSAGE_SEEN),
		"",
		"uid", "13",
		"flags", (guint32) (CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_JUNK),
		NULL);
	camel_store_search_add_folder (search, f1);

	/* the second folder is not included in the first search */
	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"flags", (guint32) (CAMEL_MESSAGE_SEEN),
		"",
		"uid", "22",
		"flags", (guint32) (CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_JUNK),
		"",
		"uid", "23",
		"flags", (guint32) (CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_DELETED),
		NULL);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"flags", (guint32) (CAMEL_MESSAGE_ANSWERED),
		NULL);
	camel_store_search_add_folder (search, f3);

	camel_store_search_set_expression (search, "(not (system-flag \"seen\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "13", 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(system-flag \"seen\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 0, NULL);

	camel_store_search_add_folder (search, f2);

	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 2, "21", 2, "22", 2, "23", 0, NULL);

	camel_store_search_set_expression (search, "(system-flag \"answered\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(system-flag \"deleted\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 2, "23", 0, NULL);

	camel_store_search_set_expression (search, "(system-flag \"junk\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 2, "22", 0, NULL);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_user_flags (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolder *f1, *f2, *f3;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"labels", "lbl1",
		"",
		"uid", "12",
		"usertags", "0",
		"",
		"uid", "13",
		"usertags", "1 1-a 4-test",
		NULL);
	camel_store_search_add_folder (search, f1);

	/* the second folder is not included in the first search */
	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"usertags", "1 2-cc 2-ff",
		"",
		"uid", "22",
		"labels", "lbl11",
		"",
		"uid", "23",
		"labels", "lbl3 lbl2 lbl1",
		NULL);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"labels", "lbl2",
		NULL);
	camel_store_search_add_folder (search, f3);

	camel_store_search_set_expression (search,
		"(or "
		   "(= (user-tag \"label\") \"lbl1\")"
		   "(user-flag (+ \"$Label\" \"lbl1\"))"
		   "(user-flag \"lbl1\")"
		")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 0, NULL);

	camel_store_search_add_folder (search, f2);
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 2, "23", 0, NULL);

	camel_store_search_set_expression (search,
		"(or "
		   "(= (user-tag \"label\") \"lbl\")"
		   "(user-flag (+ \"$Label\" \"lbl\"))"
		   "(user-flag \"lbl\")"
		")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static CamelFolder *
test_store_search_fill_folder (CamelStore *store,
			       const gchar *folder_name,
			       ...) G_GNUC_NULL_TERMINATED;

static CamelFolder *
test_store_search_fill_folder (CamelStore *store,
			       const gchar *folder_name,
			       ...) /* message uid-s */
{
	CamelFolder *folder;
	CamelStoreDB *sdb;
	GString *bdata;
	GError *local_error = NULL;
	const gchar *uid;
	va_list ap;

	g_assert_nonnull (store);
	g_assert_nonnull (folder_name);

	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (folder);

	bdata = g_string_new ("");

	sdb = camel_store_get_db (store);

	va_start (ap, folder_name);

	for (uid = va_arg (ap, const gchar *); uid; uid = va_arg (ap, const gchar *)) {
		CamelMessageInfo *nfo;
		CamelMimeMessage *msg = NULL;
		CamelStoreDBMessageRecord record = { 0, };
		gboolean success;

		test_store_search_read_message_data (uid, NULL, &msg);
		g_assert_nonnull (msg);

		nfo = camel_message_info_new_from_message (NULL, msg);
		g_assert_nonnull (nfo);

		camel_message_info_set_uid (nfo, uid);
		success = camel_message_info_save (nfo, &record, bdata);
		g_assert_true (success);

		g_assert_cmpint (camel_store_db_get_folder_id (sdb, folder_name), !=, 0);

		record.folder_id = 0;
		success = camel_store_db_write_message (sdb, folder_name, &record, &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);

		camel_store_db_message_record_clear (&record);
		g_clear_object (&nfo);
		g_clear_object (&msg);
	}

	va_end (ap);

	g_string_free (bdata, TRUE);

	return folder;
}

static void
test_store_search_user_tags (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolder *f1, *f2, *f3;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"usertags", "2 1-a 4-test 1-b 3-tst",
		"",
		"uid", "12",
		"usertags", "1 3-nm1 2-12",
		"",
		"uid", "13",
		NULL);
	camel_store_search_add_folder (search, f1);

	/* the second folder is not included in the first search */
	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"usertags", "3 9-follow-up 9-Follow-Up 6-due-by 31-Thu, 15 May 2025 11:35:00 +0000 12-completed-on 0-",
		"",
		"uid", "22",
		"",
		"uid", "23",
		"usertags", "2 3-nmx 2-no 3-nm1 2-23",
		NULL);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"usertags", "1 9-follow-up 3-yes",
		NULL);
	camel_store_search_add_folder (search, f3);

	camel_store_search_set_expression (search, "(= (user-tag \"follow-up\") \"follow-up\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	camel_store_search_add_folder (search, f2);

	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "21", 0, NULL);

	/* non-existent transforms into NULL and the NULL is lower than 20, thus check for non-empty only */
	camel_store_search_set_expression (search, "(= (user-tag \"nm1\") 12)");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 0, NULL);

	camel_store_search_set_expression (search, "(= (user-tag \"nm1\") 23)");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "23", 0, NULL);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_uid (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolder *f1, *f2, *f3;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"",
		"uid", "12",
		"",
		"uid", "13",
		NULL);
	camel_store_search_add_folder (search, f1);

	/* the second folder is not included in the first search */
	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"",
		"uid", "22",
		"",
		"uid", "23",
		NULL);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		NULL);
	camel_store_search_add_folder (search, f3);

	camel_store_search_set_expression (search, "(uid \"33\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	camel_store_search_set_expression (search, "(uid \"12\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 0, NULL);

	camel_store_search_add_folder (search, f2);

	camel_store_search_set_expression (search, "(uid \"21\" \"11\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 2, "21", 0, NULL);

	camel_store_search_set_expression (search, "(uid \"22\" \"13\" \"31\" \"33\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 2, "22", 3, "31", 0, NULL);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_headers (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolder *f1, *f2, *f3;
	TestFolder *tf1, *tf2, *tf3;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = test_store_search_fill_folder (store, "f1", "11", "12", "13", NULL);
	g_assert_nonnull (f1);
	tf1 = TEST_FOLDER (f1);
	tf1->cache_message_info = FALSE;

	f2 = test_store_search_fill_folder (store, "f2", "21", "22", "23", NULL);
	g_assert_nonnull (f2);
	camel_store_search_add_folder (search, f2);
	tf2 = TEST_FOLDER (f2);
	tf2->cache_message_info = FALSE;

	f3 = test_store_search_fill_folder (store, "f3", "31", NULL);
	g_assert_nonnull (f3);
	camel_store_search_add_folder (search, f3);
	tf3 = TEST_FOLDER (f3);
	tf3->cache_message_info = FALSE;

	#define reset_folder_counts(_with_headers) \
		tf1->n_called_get_message_info = 0; \
		tf1->n_called_get_message = 0; \
		tf1->n_called_search_header = 0; \
		tf1->n_called_search_body = 0; \
		tf1->message_info_with_headers = _with_headers; \
		tf2->n_called_get_message_info = 0; \
		tf2->n_called_get_message = 0; \
		tf2->n_called_search_header = 0; \
		tf2->n_called_search_body = 0; \
		tf2->message_info_with_headers = _with_headers; \
		tf3->n_called_get_message_info = 0; \
		tf3->n_called_get_message = 0; \
		tf3->n_called_search_header = 0; \
		tf3->n_called_search_body = 0; \
		tf3->message_info_with_headers = _with_headers;

	reset_folder_counts (FALSE);
	camel_store_search_set_expression (search, "(header-contains \"\" \"value\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "23", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 0);
	g_assert_cmpint (tf1->n_called_get_message, ==, 0);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 3);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 1);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	camel_store_search_add_folder (search, f1);

	reset_folder_counts (TRUE);
	camel_store_search_set_expression (search, "(header-contains \"\" \"val\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "23", 3, "31", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf1->n_called_get_message, ==, 0);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 0);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 0);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	reset_folder_counts (FALSE);
	camel_store_search_set_expression (search, "(header-exists \"received\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 3, "31", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf1->n_called_get_message, ==, 3);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 3);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 1);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	reset_folder_counts (TRUE);
	camel_store_search_set_expression (search, "(header-has-words \"X-Custom-Header\" \"value\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "23", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf1->n_called_get_message, ==, 0);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 0);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 0);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	reset_folder_counts (FALSE);
	camel_store_search_set_expression (search, "(header-contains \"X-Custom-Header\" \"alu\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "23", 3, "31", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf1->n_called_get_message, ==, 3);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 3);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 1);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	reset_folder_counts (FALSE);
	camel_store_search_set_expression (search, "(and (header-contains \"subject\" \"mess\") (header-exists \"x-custom-header\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 3, "31", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf1->n_called_get_message, ==, 1);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf2->n_called_get_message, ==, 1);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 1);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	reset_folder_counts (TRUE);
	camel_store_search_set_expression (search, "(and (header-contains \"subject\" \"forecast\") (header-exists \"x-custom-header\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "23", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 0);
	g_assert_cmpint (tf1->n_called_get_message, ==, 0);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 2);
	g_assert_cmpint (tf2->n_called_get_message, ==, 0);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message, ==, 0);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	/* the same as above, only flipped order */
	reset_folder_counts (TRUE);
	camel_store_search_set_expression (search, "(and (header-exists \"x-custom-header\") (header-contains \"subject\" \"forecast\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "23", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 0);
	g_assert_cmpint (tf1->n_called_get_message, ==, 0);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 2);
	g_assert_cmpint (tf2->n_called_get_message, ==, 0);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message, ==, 0);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	reset_folder_counts (FALSE);
	camel_store_search_set_expression (search, "(and (header-contains \"subject\" \"forecast\") (header-exists \"x-custom-header\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "23", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 0);
	g_assert_cmpint (tf1->n_called_get_message, ==, 0);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 2);
	g_assert_cmpint (tf2->n_called_get_message, ==, 2);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message, ==, 0);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	/* the same as above, only flipped order */
	reset_folder_counts (FALSE);
	camel_store_search_set_expression (search, "(and (header-exists \"x-custom-header\") (header-contains \"subject\" \"forecast\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "23", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 0);
	g_assert_cmpint (tf1->n_called_get_message, ==, 0);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 2);
	g_assert_cmpint (tf2->n_called_get_message, ==, 2);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message, ==, 0);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	reset_folder_counts (TRUE);
	camel_store_search_set_expression (search, "(and (header-contains \"from\" \"tomáš\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 0);
	g_assert_cmpint (tf1->n_called_get_message, ==, 0);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message, ==, 0);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message, ==, 0);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	reset_folder_counts (TRUE);
	camel_store_search_set_expression (search, "(header-contains \"bcc\" \"tomáš\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf1->n_called_get_message, ==, 0);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 0);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 0);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	reset_folder_counts (FALSE);
	camel_store_search_set_expression (search, "(header-contains \"bcc\" \"tomáš\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf1->n_called_get_message, ==, 3);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 3);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 1);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	reset_folder_counts (TRUE);
	camel_store_search_set_expression (search, "(header-full-regex \"^.*máš.*$\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "13", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf1->n_called_get_message, ==, 0);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 0);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 0);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	reset_folder_counts (FALSE);
	camel_store_search_set_expression (search, "(header-full-regex \"^.*máš.*$\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "13", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf1->n_called_get_message, ==, 3);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 3);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 1);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	#undef reset_folder_counts

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_dates (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolder *f1, *f2, *f3;
	GError *local_error = NULL;
	gchar *expr;
	gint64 now = g_get_real_time () / G_USEC_PER_SEC;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	#define WEEKS(_w) ((_w) * 7 * 24 * 60 * 60)

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"dsent", (gint64) (now - WEEKS (10)),
		"dreceived", (gint64) (now - WEEKS (9)),
		"usertags", "3 9-follow-up 9-Follow-Up 6-due-by 31-Thu, 15 May 2025 11:35:00 +0000 12-completed-on 0-",
		"",
		"uid", "12",
		"dsent", (gint64) (now - WEEKS (2)),
		"",
		"uid", "13",
		"dreceived", (gint64) (now - WEEKS (3)),
		NULL);
	camel_store_search_add_folder (search, f1);

	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"dsent", (gint64) (now - WEEKS (3) - 60),
		"dreceived", (gint64) (now - WEEKS (3)),
		"",
		"uid", "22",
		"dsent", (gint64) (now - WEEKS (2)),
		"dreceived", (gint64) (now - WEEKS (1)),
		"usertags", "2 13-not-follow-up 2-no 6-due-by 31-Thu, 15 May 2025 11:35:00 +0000",
		"",
		"uid", "23",
		"dsent", (gint64) (now - WEEKS (1)),
		"dreceived", (gint64) (now - WEEKS (0.9)),
		NULL);
	camel_store_search_add_folder (search, f2);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"dsent", (gint64) (now - WEEKS (20)),
		"dreceived", (gint64) (now - WEEKS (19)),
		"usertags", "1 6-due-by 31-Mon, 26 May 2025 00:00:00 +0000",
		NULL);
	camel_store_search_add_folder (search, f3);

	expr = g_strdup_printf ("(= (compare-date (get-sent-date) %" G_GINT64_FORMAT ") 0)", now - WEEKS (2));
	camel_store_search_set_expression (search, expr);
	g_free (expr);
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 2, "22", 0, NULL);

	expr = g_strdup_printf ("(> (compare-date (get-sent-date) %" G_GINT64_FORMAT ") 0)", now - WEEKS (3) - 1);
	camel_store_search_set_expression (search, expr);
	g_free (expr);
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 2, "22", 2, "23", 0, NULL);

	/* includes also 'uid=12', because it does not have set the dreceived, which evaluates to 0, which is before time limit */
	expr = g_strdup_printf ("(< (compare-date (get-received-date) (- (get-current-date) %" G_GINT64_FORMAT ")) 0)", (gint64) (WEEKS (2.5)));
	camel_store_search_set_expression (search, expr);
	g_free (expr);
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 3, "31", 0, NULL);

	/* also picks 'uid=12' without received date */
	camel_store_search_set_expression (search, "(< (compare-date (get-received-date) (get-relative-months (- 0 1))) 0)");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(> (compare-date (get-received-date) (get-relative-months (- 0 1))) 0)");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 2, "21", 2, "22", 2, "23", 0, NULL);

	camel_store_search_set_expression (search, "(> (compare-date (get-received-date) (get-relative-months 1)) 0)");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	/* also picks 'uid=12' without received date */
	camel_store_search_set_expression (search, "(< (compare-date (get-received-date) (get-relative-months 1)) 0)");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 2, "22", 2, "23", 3, "31", 0, NULL);

	camel_store_search_set_expression (search,
		"(and "
		  "(not (= (user-tag \"follow-up\") \"\")) "
		  "(not (= (user-tag \"due-by\") \"\")) "
		  "(< (compare-date (make-time (user-tag \"due-by\")) (make-time \"Wed, 14 May 2025 11:00:00 +0000\")) 0))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	camel_store_search_set_expression (search,
		"(and "
		  "(not (= (user-tag \"follow-up\") \"\")) "
		  "(not (= (user-tag \"due-by\") \"\")) "
		  "(> (compare-date (make-time (user-tag \"due-by\")) (make-time \"Wed, 14 May 2025 11:00:00 +0000\")) 0))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 0, NULL);

	#undef WEEKS

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_size (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolder *f1, *f2, *f3;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"size", (guint32) 567,
		"",
		"uid", "12",
		"size", (guint32) (1024 * 50),
		"",
		"uid", "13",
		"size", (guint32) (1024 * 2),
		NULL);
	camel_store_search_add_folder (search, f1);

	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"size", (guint32) (1024 * 1024 * 10),
		"",
		"uid", "22",
		"",
		"uid", "23",
		"size", (guint32) (1024 * 125),
		NULL);
	camel_store_search_add_folder (search, f2);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"size", (guint32) (1024 * 8),
		NULL);
	camel_store_search_add_folder (search, f3);

	camel_store_search_set_expression (search, "(= (get-size) 8)");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(< (get-size) 10)");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "13", 2, "22", 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(> (get-size) 1024)");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "21", 0, NULL);

	camel_store_search_set_expression (search, "(< (get-size) 0)");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	camel_store_search_set_expression (search, "(> (get-size) 20480)");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_message_id (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolder *f1, *f2, *f3;
	CamelSummaryMessageID message_id;
	gchar *msgid1, *msgid2, *msgid3, *expr;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	message_id.id.id = camel_search_util_hash_message_id ("<123>", TRUE);
	msgid1 = g_strdup_printf ("%lu %lu 0", (gulong) message_id.id.part.hi, (gulong) message_id.id.part.lo);

	message_id.id.id = camel_search_util_hash_message_id ("<456>", TRUE);
	msgid2 = g_strdup_printf ("%lu %lu 0", (gulong) message_id.id.part.hi, (gulong) message_id.id.part.lo);

	message_id.id.id = camel_search_util_hash_message_id ("<789>", TRUE);
	msgid3 = g_strdup_printf ("%lu %lu 0", (gulong) message_id.id.part.hi, (gulong) message_id.id.part.lo);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"part", msgid1,
		"",
		"uid", "12",
		"part", "1234567890 0",
		"",
		"uid", "13",
		NULL);
	camel_store_search_add_folder (search, f1);

	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"",
		"uid", "22",
		"part", msgid2,
		"",
		"uid", "23",
		NULL);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"part", msgid3,
		NULL);
	camel_store_search_add_folder (search, f3);

	camel_store_search_set_expression (search, "(header-matches \"message-id\" \"<456>\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	camel_store_search_add_folder (search, f2);

	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "22", 0, NULL);

	camel_store_search_set_expression (search, "(header-matches \"message-id\" \"<123>\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 0, NULL);

	camel_store_search_set_expression (search,
		"(or "
		  "(header-matches \"message-id\" \"<456>\")"
		  "(header-matches \"message-id\" \"<123>\")"
		  "(header-matches \"message-id\" \"<999>\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 2, "22", 0, NULL);

	camel_store_search_set_expression (search, "(header-matches \"x-camel-msgid\" \"123 456\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	/* cut the " 0" from the end of the msgid, to be able to match it */
	msgid2[strlen (msgid2) - 2] = '\0';
	expr = g_strdup_printf ("(header-matches \"x-camel-msgid\" \"%s\")", msgid2);
	camel_store_search_set_expression (search, expr);
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "22", 0, NULL);
	g_free (expr);

	g_free (msgid1);
	g_free (msgid2);
	g_free (msgid3);
	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_message_location (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolder *f1, *f2, *f3;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"",
		"uid", "12",
		"",
		"uid", "13",
		NULL);
	camel_store_search_add_folder (search, f1);

	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"",
		"uid", "22",
		"",
		"uid", "23",
		NULL);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		NULL);
	camel_store_search_add_folder (search, f3);

	camel_store_search_set_expression (search, "(message-location \"folder://test-store-search/f2\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	camel_store_search_add_folder (search, f2);

	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "21", 2, "22", 2, "23", 0, NULL);

	camel_store_search_set_expression (search, "(not (message-location \"folder://test-store-search/f2\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(or (message-location \"folder://test-store-search/f2\")(message-location \"folder://test-store-search/nonexistent\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "21", 2, "22", 2, "23", 0, NULL);

	camel_store_search_set_expression (search, "(or (message-location \"folder://test-store-search/f3\")(message-location \"folder://test-store-search/f1\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 3, "31", 0, NULL);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_addressbook_contains (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelSession *session;
	CamelFolder *f1, *f2, *f3;
	TestSession *test_session;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	session = camel_service_ref_session (CAMEL_SERVICE (store));
	g_assert_nonnull (session);
	test_session = TEST_SESSION (session);
	g_assert_nonnull (test_session);

	search = camel_store_search_new (store);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"from", "loki@no.where",
		"to", "Thor <thor@no.where>",
		"",
		"uid", "12",
		"from", "Gwendoline <gwen@no.where>",
		"cc", "Peter <peter@no.where>",
		"",
		"uid", "13",
		"from", "Bruce <bruce@no.where>",
		"to", "Tony <tony@no.where>, Peeeter <peter@no.where>",
		"mlist", "interested.parties@no.where",
		NULL);
	camel_store_search_add_folder (search, f1);

	/* the second folder is not included in the first search */
	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"from", "spam@no.where",
		"cc", "interested.parties@no.where, I.M. <tony@no.where>",
		"",
		"uid", "22",
		"cc", "spam@no.where",
		"",
		"uid", "23",
		"mlist", "all@no.where",
		NULL);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"from", "Annie <aunt@no.no.no>",
		"to", "Bob <uncle@no.no.no>",
		"cc", "Little <boy@no.no.no>, Gwenie <gwen@no.where>",
		NULL);
	camel_store_search_add_folder (search, f3);

	test_session->n_called_addressbook_contains = 0;
	camel_store_search_set_expression (search, "(addressbook-contains \"book1\" \"from\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 0, NULL);
	g_assert_cmpint (test_session->n_called_addressbook_contains, ==, 4);

	camel_store_search_add_folder (search, f2);

	test_session->n_called_addressbook_contains = 0;
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 0, NULL);
	g_assert_cmpint (test_session->n_called_addressbook_contains, ==, 5);

	test_session->n_called_addressbook_contains = 0;
	camel_store_search_set_expression (search, "(or (addressbook-contains \"book1\" \"to\") (addressbook-contains \"book2\" \"from\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 1, "13", 0, NULL);
	g_assert_cmpint (test_session->n_called_addressbook_contains, ==, 7);

	test_session->n_called_addressbook_contains = 0;
	camel_store_search_set_expression (search, "(or (addressbook-contains \"book1\" \"cc\") (addressbook-contains \"book2\" \"cc\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "21", 3, "31", 0, NULL);
	g_assert_cmpint (test_session->n_called_addressbook_contains, ==, 7);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&session);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_body (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolder *f1, *f2, *f3;
	TestFolder *tf1, *tf2, *tf3;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = test_store_search_fill_folder (store, "f1", "11", "12", "13", NULL);
	g_assert_nonnull (f1);
	tf1 = TEST_FOLDER (f1);
	tf1->cache_message_info = FALSE;

	f2 = test_store_search_fill_folder (store, "f2", "21", "22", "23", NULL);
	g_assert_nonnull (f2);
	camel_store_search_add_folder (search, f2);
	tf2 = TEST_FOLDER (f2);
	tf2->cache_message_info = FALSE;

	f3 = test_store_search_fill_folder (store, "f3", "31", NULL);
	g_assert_nonnull (f3);
	camel_store_search_add_folder (search, f3);
	tf3 = TEST_FOLDER (f3);
	tf3->cache_message_info = FALSE;

	#define reset_folder_counts() \
		tf1->n_called_get_message_info = 0; \
		tf1->n_called_get_message = 0; \
		tf1->n_called_search_header = 0; \
		tf1->n_called_search_body = 0; \
		tf2->n_called_get_message_info = 0; \
		tf2->n_called_get_message = 0; \
		tf2->n_called_search_header = 0; \
		tf2->n_called_search_body = 0; \
		tf3->n_called_get_message_info = 0; \
		tf3->n_called_get_message = 0; \
		tf3->n_called_search_header = 0; \
		tf3->n_called_search_body = 0;

	reset_folder_counts ();
	camel_store_search_set_expression (search, "(body-contains \"blur\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 0);
	g_assert_cmpint (tf1->n_called_get_message, ==, 0);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 3);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 1);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 1);

	camel_store_search_add_folder (search, f1);

	reset_folder_counts ();
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf1->n_called_get_message, ==, 2);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 1);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 3);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 1);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 1);

	reset_folder_counts ();
	camel_store_search_set_expression (search, "(body-contains \"not there\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf1->n_called_get_message, ==, 3);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 1);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 3);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 1);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 1);

	reset_folder_counts ();
	camel_store_search_set_expression (search, "(body-contains \"šáša\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 3, "31", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf1->n_called_get_message, ==, 3);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 1);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 3);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 0);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	reset_folder_counts ();
	camel_store_search_set_expression (search, "(body-contains \"bla bla\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf1->n_called_get_message, ==, 2);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 1);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 3);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 1);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 1);

	reset_folder_counts ();
	camel_store_search_set_expression (search, "(body-contains \"mostly\" \"sunny\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "22", 2, "23", 3, "31", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf1->n_called_get_message, ==, 3);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 1);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 1);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 1);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 1);

	reset_folder_counts ();
	camel_store_search_set_expression (search, "(body-regex \"^.*sunny.*$\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "22", 2, "23", 0, NULL);
	g_assert_cmpint (tf1->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf1->n_called_get_message, ==, 3);
	g_assert_cmpint (tf1->n_called_search_header, ==, 0);
	g_assert_cmpint (tf1->n_called_search_body, ==, 0);
	g_assert_cmpint (tf2->n_called_get_message_info, ==, 3);
	g_assert_cmpint (tf2->n_called_get_message, ==, 1);
	g_assert_cmpint (tf2->n_called_search_header, ==, 0);
	g_assert_cmpint (tf2->n_called_search_body, ==, 0);
	g_assert_cmpint (tf3->n_called_get_message_info, ==, 1);
	g_assert_cmpint (tf3->n_called_get_message, ==, 1);
	g_assert_cmpint (tf3->n_called_search_header, ==, 0);
	g_assert_cmpint (tf3->n_called_search_body, ==, 0);

	#undef reset_folder_counts

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_has_folders (CamelStoreSearch *search,
			       CamelFolder *f1,
			       CamelFolder *f2,
			       CamelFolder *f3)
{
	GPtrArray *folders;
	guint ii, bit_hits, expected_n_folders, expected_bit_hits;

	expected_n_folders = (f1 ? 1 : 0) + (f2 ? 1 : 0) + (f3 ? 1 : 0);
	expected_bit_hits = (f1 ? 1 << 0 : 0) | (f2 ? 1 << 1 : 0) | (f3 ? 1 << 2 : 0);

	folders = camel_store_search_list_folders (search);
	g_assert_nonnull (folders);
	g_assert_cmpint (folders->len, ==, expected_n_folders);
	for (ii = 0, bit_hits = 0; ii < folders->len; ii++) {
		CamelFolder *folder = g_ptr_array_index (folders, ii);
		guint32 bit = 0;

		g_assert_nonnull (folder);

		if (folder == f1)
			bit = 1 << 0;
		else if (folder == f2)
			bit = 1 << 1;
		else if (folder == f3)
			bit = 1 << 2;
		else
			g_assert_not_reached ();

		g_assert_cmpint ((bit_hits & bit), ==, 0);
		bit_hits |= bit;
	}

	g_assert_cmpint (expected_bit_hits, ==, bit_hits);
	g_ptr_array_unref (folders);
}

static void
test_store_search_extras (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolderThreadFlags thread_flags = 0;
	CamelFolder *f1, *f2, *f3;
	GPtrArray *array;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"subject", "s11",
		"flags", (guint32) CAMEL_MESSAGE_SEEN,
		"",
		"uid", "12",
		"subject", "s12",
		"",
		"uid", "13",
		"subject", "s13",
		"to", "list@no.where",
		NULL);
	camel_store_search_add_folder (search, f1);

	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"subject", "s21",
		"to", "to@no.where",
		"flags", (guint32) CAMEL_MESSAGE_DELETED,
		"",
		"uid", "22",
		"subject", "s22",
		"",
		"uid", "23",
		"subject", "s23",
		"flags", (guint32) CAMEL_MESSAGE_ANSWERED,
		NULL);
	camel_store_search_add_folder (search, f2);
	test_store_search_has_folders (search, f1, f2, NULL);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"subject", "s31",
		"to", "Bob <bob@no.where>",
		"flags", (guint32) CAMEL_MESSAGE_DRAFT,
		NULL);
	camel_store_search_add_folder (search, f3);
	test_store_search_has_folders (search, f1, f2, f3);

	camel_store_search_remove_folder (search, f2);
	test_store_search_has_folders (search, f1, NULL, f3);
	camel_store_search_add_folder (search, f2);
	test_store_search_has_folders (search, f1, f2, f3);

	g_assert_cmpint (camel_store_search_get_match_threads_kind (search, &thread_flags), ==, CAMEL_MATCH_THREADS_KIND_NONE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);

	camel_store_search_set_expression (search,
		"(match-threads \"opt1,opt2\" (or "
		  "(header-exists \"to\")"
		  "(header-starts-with \"subject\" \"s1\")"
		  "(header-ends-with \"subject\" \"1\")))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 3, "31", 0, NULL);
	g_assert_cmpint (camel_store_search_get_match_threads_kind (search, &thread_flags), ==, CAMEL_MATCH_THREADS_KIND_NONE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);

	camel_store_search_set_expression (search,
		"(match-all (match-threads \"all\" (or "
		  "(header-exists \"to\")"
		  "(header-starts-with \"subject\" \"s1\")"
		  "(header-ends-with \"subject\" \"1\"))))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 3, "31", 0, NULL);
	g_assert_cmpint (camel_store_search_get_match_threads_kind (search, &thread_flags), ==, CAMEL_MATCH_THREADS_KIND_ALL);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);

	camel_store_search_set_expression (search,
		"(match-all (match-threads \"replies\" (or "
		  "(header-exists \"to\")"
		  "(header-starts-with \"subject\" \"s1\")"
		  "(header-ends-with \"subject\" \"1\"))))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 3, "31", 0, NULL);
	g_assert_cmpint (camel_store_search_get_match_threads_kind (search, &thread_flags), ==, CAMEL_MATCH_THREADS_KIND_REPLIES);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);

	camel_store_search_set_expression (search,
		"(match-all (match-threads \"replies_parents\" (or "
		  "(header-exists \"to\")"
		  "(header-starts-with \"subject\" \"s1\")"
		  "(header-ends-with \"subject\" \"1\"))))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 3, "31", 0, NULL);
	g_assert_cmpint (camel_store_search_get_match_threads_kind (search, &thread_flags), ==, CAMEL_MATCH_THREADS_KIND_REPLIES_AND_PARENTS);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);

	camel_store_search_set_expression (search,
		"(match-all (match-threads \"single\" (or "
		  "(header-exists \"to\")"
		  "(header-starts-with \"subject\" \"s1\")"
		  "(header-ends-with \"subject\" \"1\"))))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 3, "31", 0, NULL);
	g_assert_cmpint (camel_store_search_get_match_threads_kind (search, &thread_flags), ==, CAMEL_MATCH_THREADS_KIND_SINGLE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);

	camel_store_search_set_expression (search,
		"(match-all (match-threads \"no-subject,unknown\" (or "
		  "(header-exists \"to\")"
		  "(header-starts-with \"subject\" \"s1\")"
		  "(header-ends-with \"subject\" \"1\"))))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 3, "31", 0, NULL);
	g_assert_cmpint (camel_store_search_get_match_threads_kind (search, &thread_flags), ==, CAMEL_MATCH_THREADS_KIND_NONE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);

	camel_store_search_set_expression (search,
		"(match-all (match-threads \"no-subject,all\" (or "
		  "(header-exists \"to\")"
		  "(header-starts-with \"subject\" \"s1\")"
		  "(header-ends-with \"subject\" \"1\"))))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 3, "31", 0, NULL);
	g_assert_cmpint (camel_store_search_get_match_threads_kind (search, &thread_flags), ==, CAMEL_MATCH_THREADS_KIND_ALL);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);

	camel_store_search_set_expression (search,
		"(match-all (match-threads \"no-subject,replies\" (or "
		  "(header-exists \"to\")"
		  "(header-starts-with \"subject\" \"s1\")"
		  "(header-ends-with \"subject\" \"1\"))))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 3, "31", 0, NULL);
	g_assert_cmpint (camel_store_search_get_match_threads_kind (search, &thread_flags), ==, CAMEL_MATCH_THREADS_KIND_REPLIES);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);

	camel_store_search_set_expression (search,
		"(match-all (match-threads \"no-subject,replies_parents\" (or "
		  "(header-exists \"to\")"
		  "(header-starts-with \"subject\" \"s1\")"
		  "(header-ends-with \"subject\" \"1\"))))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 3, "31", 0, NULL);
	g_assert_cmpint (camel_store_search_get_match_threads_kind (search, &thread_flags), ==, CAMEL_MATCH_THREADS_KIND_REPLIES_AND_PARENTS);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);

	camel_store_search_set_expression (search,
		"(match-all (match-threads \"no-subject,single\" (or "
		  "(header-exists \"to\")"
		  "(header-starts-with \"subject\" \"s1\")"
		  "(header-ends-with \"subject\" \"1\"))))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 3, "31", 0, NULL);
	g_assert_cmpint (camel_store_search_get_match_threads_kind (search, &thread_flags), ==, CAMEL_MATCH_THREADS_KIND_SINGLE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);

	camel_store_search_set_expression (search,
		"(or "
		  "(header-exists \"to\")"
		  "(header-starts-with \"subject\" \"s1\")"
		  "(header-ends-with \"subject\" \"1\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 3, "31", 0, NULL);
	g_assert_cmpint (camel_store_search_get_match_threads_kind (search, &thread_flags), ==, CAMEL_MATCH_THREADS_KIND_NONE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);

	array = camel_store_search_dup_additional_columns (search);
	g_assert_null (array);

	#define get_item(_idx) ((CamelStoreSearchItem *) g_ptr_array_index (array, _idx))
	#define get_item_folder_id(_idx) get_item (_idx)->folder_id
	#define get_item_uid(_idx) get_item (_idx)->uid
	#define get_item_n_added_values(_idx) camel_store_search_item_get_n_additional_values (get_item (_idx))
	#define get_item_added_value(_itmidx, _validx) camel_store_search_item_get_additional_value (get_item (_itmidx), _validx)

	success = camel_store_search_get_items_sync (search, &array, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (array);
	g_assert_cmpint (array->len, ==, 5);
	g_assert_cmpint (get_item_folder_id (0), ==, 1);
	g_assert_cmpstr (get_item_uid (0), ==, "11");
	g_assert_cmpint (get_item_n_added_values (0), ==, 0);
	g_assert_cmpstr (get_item_added_value (0, 999), ==, NULL);
	g_assert_cmpint (get_item_folder_id (1), ==, 1);
	g_assert_cmpstr (get_item_uid (1), ==, "12");
	g_assert_cmpint (get_item_n_added_values (1), ==, 0);
	g_assert_cmpstr (get_item_added_value (1, 999), ==, NULL);
	g_assert_cmpint (get_item_folder_id (2), ==, 1);
	g_assert_cmpstr (get_item_uid (2), ==, "13");
	g_assert_cmpint (get_item_n_added_values (2), ==, 0);
	g_assert_cmpstr (get_item_added_value (2, 999), ==, NULL);
	g_assert_cmpint (get_item_folder_id (3), ==, 2);
	g_assert_cmpstr (get_item_uid (3), ==, "21");
	g_assert_cmpint (get_item_n_added_values (3), ==, 0);
	g_assert_cmpstr (get_item_added_value (3, 999), ==, NULL);
	g_assert_cmpint (get_item_folder_id (4), ==, 3);
	g_assert_cmpstr (get_item_uid (4), ==, "31");
	g_assert_cmpint (get_item_n_added_values (4), ==, 0);
	g_assert_cmpstr (get_item_added_value (4, 999), ==, NULL);
	g_ptr_array_unref (array);

	array = g_ptr_array_new ();
	g_ptr_array_add (array, (gpointer) "subject");
	camel_store_search_set_additional_columns (search, array);
	g_ptr_array_unref (array);

	array = camel_store_search_dup_additional_columns (search);
	g_assert_nonnull (array);
	g_assert_cmpint (array->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (array, 0), ==, "subject");
	g_clear_pointer (&array, g_ptr_array_unref);

	/* additional columns change requires rebuild */
	success = camel_store_search_get_items_sync (search, &array, NULL, &local_error);
	g_assert_error (local_error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
	g_assert_false (success);
	g_assert_null (array);
	g_clear_error (&local_error);

	success = camel_store_search_get_uids_sync (search, "f1", &array, NULL, &local_error);
	g_assert_error (local_error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
	g_assert_false (success);
	g_assert_null (array);
	g_clear_error (&local_error);

	/* rebuild */
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 3, "31", 0, NULL);

	success = camel_store_search_get_items_sync (search, &array, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (array);
	g_assert_cmpint (array->len, ==, 5);
	g_assert_cmpint (get_item_folder_id (0), ==, 1);
	g_assert_cmpstr (get_item_uid (0), ==, "11");
	g_assert_cmpint (get_item_n_added_values (0), ==, 1);
	g_assert_cmpstr (get_item_added_value (0, 0), ==, "s11");
	g_assert_cmpstr (get_item_added_value (0, 1), ==, NULL);
	g_assert_cmpint (get_item_folder_id (1), ==, 1);
	g_assert_cmpstr (get_item_uid (1), ==, "12");
	g_assert_cmpint (get_item_n_added_values (1), ==, 1);
	g_assert_cmpstr (get_item_added_value (1, 0), ==, "s12");
	g_assert_cmpstr (get_item_added_value (1, 1), ==, NULL);
	g_assert_cmpint (get_item_folder_id (2), ==, 1);
	g_assert_cmpstr (get_item_uid (2), ==, "13");
	g_assert_cmpint (get_item_n_added_values (2), ==, 1);
	g_assert_cmpstr (get_item_added_value (2, 0), ==, "s13");
	g_assert_cmpstr (get_item_added_value (2, 1), ==, NULL);
	g_assert_cmpint (get_item_folder_id (3), ==, 2);
	g_assert_cmpstr (get_item_uid (3), ==, "21");
	g_assert_cmpint (get_item_n_added_values (3), ==, 1);
	g_assert_cmpstr (get_item_added_value (3, 0), ==, "s21");
	g_assert_cmpstr (get_item_added_value (3, 1), ==, NULL);
	g_assert_cmpint (get_item_folder_id (4), ==, 3);
	g_assert_cmpstr (get_item_uid (4), ==, "31");
	g_assert_cmpint (get_item_n_added_values (4), ==, 1);
	g_assert_cmpstr (get_item_added_value (4, 0), ==, "s31");
	g_assert_cmpstr (get_item_added_value (4, 1), ==, NULL);
	g_ptr_array_unref (array);

	array = g_ptr_array_new ();
	camel_store_search_set_additional_columns (search, array);
	g_ptr_array_unref (array);

	array = camel_store_search_dup_additional_columns (search);
	g_assert_null (array);

	array = g_ptr_array_new ();
	g_ptr_array_add (array, (gpointer) "subject");
	camel_store_search_set_additional_columns (search, array);
	g_ptr_array_unref (array);

	array = camel_store_search_dup_additional_columns (search);
	g_assert_cmpint (array->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (array, 0), ==, "subject");
	g_ptr_array_unref (array);

	camel_store_search_set_additional_columns (search, NULL);
	array = camel_store_search_dup_additional_columns (search);
	g_assert_null (array);

	array = g_ptr_array_new ();
	g_ptr_array_add (array, (gpointer) "flags");
	g_ptr_array_add (array, (gpointer) "mail_to");
	g_ptr_array_add (array, (gpointer) "subject");
	camel_store_search_set_additional_columns (search, array);
	g_ptr_array_unref (array);

	array = camel_store_search_dup_additional_columns (search);
	g_assert_cmpint (array->len, ==, 3);
	g_assert_cmpstr (g_ptr_array_index (array, 0), ==, "flags");
	g_assert_cmpstr (g_ptr_array_index (array, 1), ==, "mail_to");
	g_assert_cmpstr (g_ptr_array_index (array, 2), ==, "subject");
	g_ptr_array_unref (array);

	/* rebuild */
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 3, "31", 0, NULL);

	success = camel_store_search_get_items_sync (search, &array, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (array);
	g_assert_cmpint (array->len, ==, 5);
	g_assert_cmpint (get_item_folder_id (0), ==, 1);
	g_assert_cmpstr (get_item_uid (0), ==, "11");
	g_assert_cmpint (get_item_n_added_values (0), ==, 3);
	g_assert_cmpstr (get_item_added_value (0, 0), ==, "16");
	g_assert_cmpstr (get_item_added_value (0, 1), ==, NULL);
	g_assert_cmpstr (get_item_added_value (0, 2), ==, "s11");
	g_assert_cmpstr (get_item_added_value (0, 3), ==, NULL);
	g_assert_cmpint (get_item_folder_id (1), ==, 1);
	g_assert_cmpstr (get_item_uid (1), ==, "12");
	g_assert_cmpint (get_item_n_added_values (1), ==, 3);
	g_assert_cmpstr (get_item_added_value (1, 0), ==, "0");
	g_assert_cmpstr (get_item_added_value (1, 1), ==, NULL);
	g_assert_cmpstr (get_item_added_value (1, 2), ==, "s12");
	g_assert_cmpstr (get_item_added_value (1, 3), ==, NULL);
	g_assert_cmpint (get_item_folder_id (2), ==, 1);
	g_assert_cmpstr (get_item_uid (2), ==, "13");
	g_assert_cmpint (get_item_n_added_values (2), ==, 3);
	g_assert_cmpstr (get_item_added_value (2, 0), ==, "0");
	g_assert_cmpstr (get_item_added_value (2, 1), ==, "list@no.where");
	g_assert_cmpstr (get_item_added_value (2, 2), ==, "s13");
	g_assert_cmpstr (get_item_added_value (2, 3), ==, NULL);
	g_assert_cmpint (get_item_folder_id (3), ==, 2);
	g_assert_cmpstr (get_item_uid (3), ==, "21");
	g_assert_cmpint (get_item_n_added_values (3), ==, 3);
	g_assert_cmpstr (get_item_added_value (3, 0), ==, "2");
	g_assert_cmpstr (get_item_added_value (3, 1), ==, "to@no.where");
	g_assert_cmpstr (get_item_added_value (3, 2), ==, "s21");
	g_assert_cmpstr (get_item_added_value (3, 3), ==, NULL);
	g_assert_cmpint (get_item_folder_id (4), ==, 3);
	g_assert_cmpstr (get_item_uid (4), ==, "31");
	g_assert_cmpint (get_item_n_added_values (4), ==, 3);
	g_assert_cmpstr (get_item_added_value (4, 0), ==, "4");
	g_assert_cmpstr (get_item_added_value (4, 1), ==, "Bob <bob@no.where>");
	g_assert_cmpstr (get_item_added_value (4, 2), ==, "s31");
	g_assert_cmpstr (get_item_added_value (4, 3), ==, NULL);
	g_ptr_array_unref (array);

	#undef get_item
	#undef get_item_folder_id
	#undef get_item_uid
	#undef get_item_n_added_values
	#undef get_item_added_value

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_index (void)
{
	CamelStore *store, *store2;
	CamelStoreSearchIndex *index, *index2;

	/* the index needs to be fast, it does not check whether it's really a CamelStore instance */
	store = GINT_TO_POINTER (1);
	store2 = GINT_TO_POINTER (2);

	index = camel_store_search_index_new ();
	g_assert_nonnull (index);
	g_assert_false (camel_store_search_index_contains (index, store, 3, "123"));
	g_assert_false (camel_store_search_index_contains (index, store2, 4, "444"));
	g_assert_false (camel_store_search_index_remove (index, store, 5, "555"));
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 0);

	camel_store_search_index_add (index, store2, 4, "222");
	g_assert_false (camel_store_search_index_contains (index, store, 3, "123"));
	g_assert_false (camel_store_search_index_contains (index, store2, 4, "444"));
	g_assert_true (camel_store_search_index_contains (index, store2, 4, "222"));
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 1);

	camel_store_search_index_add (index, store, 3, "123");
	g_assert_true (camel_store_search_index_contains (index, store, 3, "123"));
	g_assert_false (camel_store_search_index_contains (index, store2, 4, "444"));
	g_assert_true (camel_store_search_index_contains (index, store2, 4, "222"));
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 2);

	g_assert_false (camel_store_search_index_remove (index, store, 5, "555"));
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 2);
	g_assert_true (camel_store_search_index_remove (index, store, 3, "123"));
	g_assert_false (camel_store_search_index_contains (index, store, 3, "123"));
	g_assert_false (camel_store_search_index_contains (index, store2, 4, "444"));
	g_assert_true (camel_store_search_index_contains (index, store2, 4, "222"));
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 1);

	camel_store_search_index_add (index, store, 3, "123");
	g_assert_true (camel_store_search_index_contains (index, store, 3, "123"));
	g_assert_false (camel_store_search_index_contains (index, store2, 4, "444"));
	g_assert_true (camel_store_search_index_contains (index, store2, 4, "222"));
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 2);

	/* using the same indexes for the move does nothing */
	camel_store_search_index_move_from_existing (index, index);
	g_assert_true (camel_store_search_index_contains (index, store, 3, "123"));
	g_assert_false (camel_store_search_index_contains (index, store2, 4, "444"));
	g_assert_true (camel_store_search_index_contains (index, store2, 4, "222"));
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 2);

	index2 = camel_store_search_index_new ();
	g_assert_nonnull (index2);
	camel_store_search_index_add (index2, store2, 4, "444");
	camel_store_search_index_add (index2, store2, 1, "444");
	camel_store_search_index_add (index2, store2, 4, "111");
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 3);

	camel_store_search_index_move_from_existing (index, index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 5);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);

	camel_store_search_index_add (index2, store, 3, "123");
	camel_store_search_index_add (index2, store, 5, "111");
	camel_store_search_index_add (index2, store2, 1, "444");
	camel_store_search_index_add (index2, store2, 4, "111");
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 4);

	camel_store_search_index_move_from_existing (index, index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 6);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	g_assert_true (camel_store_search_index_contains (index, store, 3, "123"));
	g_assert_true (camel_store_search_index_contains (index, store, 5, "111"));
	g_assert_true (camel_store_search_index_contains (index, store2, 4, "444"));
	g_assert_true (camel_store_search_index_contains (index, store2, 1, "444"));
	g_assert_true (camel_store_search_index_contains (index, store2, 4, "222"));
	g_assert_true (camel_store_search_index_contains (index, store2, 4, "111"));
	g_assert_false (camel_store_search_index_contains (index, store, 2, "222"));
	g_assert_false (camel_store_search_index_contains (index, store2, 3, "333"));

	camel_store_search_index_unref (index);
	camel_store_search_index_unref (index2);
}

static void
test_store_search_match_threads (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelStoreSearchIndex *index;
	CamelFolder *f1, *f2, *f3;
	CamelMatchThreadsKind match_threads_kind;
	CamelFolderThreadFlags thread_flags = 0;
	GPtrArray *items = NULL;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"part", "1 1 0",
		"subject", "single root",
		"",
		"uid", "12",
		"part", "1 2 1 2 1",
		"subject", "reply to 21 from 12",
		"",
		"uid", "14",
		"part", "12 1 1 2 1",
		"subject", "reply to 21 b",
		"",
		"uid", "13",
		"part", "1 3 2 9 9 1 2",
		"subject", "reply to nonexistent 99, referencing 12",
		"",
		"uid", "15",
		"part", "1 31 1 1 2",
		"subject", "reply to 12",
		NULL);
	camel_store_search_add_folder (search, f1);

	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"part", "2 1 0",
		"subject", "root 21",
		"",
		"uid", "22",
		"part", "2 2 1 1 3",
		"subject", "reply to 13",
		"",
		"uid", "23",
		"part", "2 3 1 8 8",
		"subject", "reply to nonexistent 88",
		"",
		"uid", "24",
		"part", "2 4 0",
		"subject", "re: reply to nonexistent 88",
		NULL);
	camel_store_search_add_folder (search, f2);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"part", "3 1 0",
		"subject", "single root 31",
		"",
		"uid", "32",
		"part", "3 2 1 3 3",
		"subject", "reply 32",
		"",
		"uid", "33",
		"part", "3 3 1 2 3",
		"subject", "reply in 33",
		NULL);
	camel_store_search_add_folder (search, f3);

	/* The thread looks like:
	     11
	     21
	       12
		 13
		   22
		 15
	       14
	     23
	       33
		 32
	       24 (if threading by subject)
	     31
	     24 (if not threading by subject)
	 */

	camel_store_search_set_expression (search, "(header-contains \"subject\" \"root\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 2, "21", 3, "31", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_NONE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);

	camel_store_search_set_expression (search, "(match-threads \"single\" (header-contains \"subject\" \"root\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search, 1, "11", 2, "21", 3, "31", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_SINGLE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_add_match_threads_items_sync (search, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12); /* all messages from all folders in the @search */
	index = camel_store_search_ref_result_index (search);
	g_assert_nonnull (index);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 3);
	camel_store_search_index_apply_match_threads (index, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search, index);
	test_store_search_check_result (search, 1, "11", 3, "31", 0, NULL);
	g_clear_pointer (&index, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	camel_store_search_set_expression (search, "(match-threads \"all\" (header-contains \"subject\" \"root\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search, 1, "11", 2, "21", 3, "31", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_ALL);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_add_match_threads_items_sync (search, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12); /* all messages from all folders in the @search */
	index = camel_store_search_ref_result_index (search);
	g_assert_nonnull (index);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 3);
	camel_store_search_index_apply_match_threads (index, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search, index);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "14", 1, "13", 1, "15", 2, "21", 2, "22", 3, "31", 0, NULL);
	g_clear_pointer (&index, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	camel_store_search_set_expression (search, "(match-threads \"all\" (or (header-contains \"subject\" \"from 12\") (uid \"33\")))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search, 1, "12", 3, "33", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_ALL);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_add_match_threads_items_sync (search, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12); /* all messages from all folders in the @search */
	index = camel_store_search_ref_result_index (search);
	g_assert_nonnull (index);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 2);
	camel_store_search_index_apply_match_threads (index, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search, index);
	test_store_search_check_result (search, 1, "12", 1, "14", 1, "13", 1, "15", 2, "21", 2, "22", 2, "23", 2, "24", 3, "32", 3, "33", 0, NULL);
	g_clear_pointer (&index, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	camel_store_search_set_expression (search, "(match-threads \"no-subject,all\" (or (header-contains \"subject\" \"from 12\") (uid \"33\")))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search, 1, "12", 3, "33", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_ALL);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);
	success = camel_store_search_add_match_threads_items_sync (search, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12); /* all messages from all folders in the @search */
	index = camel_store_search_ref_result_index (search);
	g_assert_nonnull (index);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 2);
	camel_store_search_index_apply_match_threads (index, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search, index);
	test_store_search_check_result (search, 1, "12", 1, "14", 1, "13", 1, "15", 2, "21", 2, "22", 2, "23", 3, "32", 3, "33", 0, NULL);
	g_clear_pointer (&index, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	camel_store_search_set_expression (search, "(match-threads \"replies\" (uid \"13\" \"33\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search, 1, "13", 3, "33", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_add_match_threads_items_sync (search, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12); /* all messages from all folders in the @search */
	index = camel_store_search_ref_result_index (search);
	g_assert_nonnull (index);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 2);
	camel_store_search_index_apply_match_threads (index, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search, index);
	test_store_search_check_result (search, 1, "13", 2, "22", 3, "32", 3, "33", 0, NULL);
	g_clear_pointer (&index, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	camel_store_search_set_expression (search, "(match-threads \"no-subject,replies\" (uid \"13\" \"33\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search, 1, "13", 3, "33", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);
	success = camel_store_search_add_match_threads_items_sync (search, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12); /* all messages from all folders in the @search */
	index = camel_store_search_ref_result_index (search);
	g_assert_nonnull (index);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 2);
	camel_store_search_index_apply_match_threads (index, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search, index);
	test_store_search_check_result (search, 1, "13", 2, "22", 3, "32", 3, "33", 0, NULL);
	g_clear_pointer (&index, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	camel_store_search_set_expression (search, "(match-threads \"replies_parents\" (uid \"13\" \"33\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search, 1, "13", 3, "33", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES_AND_PARENTS);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_add_match_threads_items_sync (search, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12); /* all messages from all folders in the @search */
	index = camel_store_search_ref_result_index (search);
	g_assert_nonnull (index);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 2);
	camel_store_search_index_apply_match_threads (index, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search, index);
	test_store_search_check_result (search, 1, "12", 1, "13", 2, "21", 2, "22", 2, "23", 3, "32", 3, "33", 0, NULL);
	g_clear_pointer (&index, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	camel_store_search_set_expression (search, "(match-threads \"no-subject,replies_parents\" (uid \"13\" \"33\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search, 1, "13", 3, "33", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES_AND_PARENTS);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);
	success = camel_store_search_add_match_threads_items_sync (search, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12); /* all messages from all folders in the @search */
	index = camel_store_search_ref_result_index (search);
	g_assert_nonnull (index);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 2);
	camel_store_search_index_apply_match_threads (index, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search, index);
	test_store_search_check_result (search, 1, "12", 1, "13", 2, "21", 2, "22", 2, "23", 3, "32", 3, "33", 0, NULL);
	g_clear_pointer (&index, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_match_threads_multiple_stores (void)
{
	CamelSession *session;
	CamelStore *store1, *store2;
	CamelStoreSearch *search1, *search2;
	CamelStoreSearchIndex *index1, *index2;
	CamelFolder *f1, *f2, *f3;
	CamelMatchThreadsKind match_threads_kind;
	CamelFolderThreadFlags thread_flags = 0;
	GPtrArray *items = NULL;
	GError *local_error = NULL;
	gboolean success;

	session = test_session_new ();

	store1 = test_store_new_full (session, "test-store-search-1", "Test Store Search 1");
	store2 = test_store_new_full (session, "test-store-search-2", "Test Store Search 2");
	search1 = camel_store_search_new (store1);
	search2 = camel_store_search_new (store2);

	f1 = camel_store_get_folder_sync (store1, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"part", "1 1 0",
		"subject", "single root",
		"",
		"uid", "12",
		"part", "1 2 1 2 1",
		"subject", "reply to 21 from 12",
		"",
		"uid", "14",
		"part", "12 1 1 2 1",
		"subject", "reply to 21 b",
		"",
		"uid", "13",
		"part", "1 3 2 9 9 1 2",
		"subject", "reply to nonexistent 99, referencing 12",
		"",
		"uid", "15",
		"part", "1 31 1 1 2",
		"subject", "reply to 12",
		NULL);
	camel_store_search_add_folder (search1, f1);

	f2 = camel_store_get_folder_sync (store2, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"part", "2 1 0",
		"subject", "root 21",
		"",
		"uid", "22",
		"part", "2 2 1 1 3",
		"subject", "reply to 13",
		"",
		"uid", "23",
		"part", "2 3 1 8 8",
		"subject", "reply to nonexistent 88",
		"",
		"uid", "24",
		"part", "2 4 0",
		"subject", "re: reply to nonexistent 88",
		NULL);
	camel_store_search_add_folder (search2, f2);

	f3 = camel_store_get_folder_sync (store1, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"part", "3 1 0",
		"subject", "single root 31",
		"",
		"uid", "32",
		"part", "3 2 1 3 3",
		"subject", "reply 32",
		"",
		"uid", "33",
		"part", "3 3 1 2 3",
		"subject", "reply in 33",
		NULL);
	camel_store_search_add_folder (search1, f3);

	/* The thread looks like:
	     11
	     21
	       12
		 13
		   22
		 15
	       14
	     23
	       33
		 32
	       24 (if threading by subject)
	     31
	     24 (if not threading by subject)
	 */

	camel_store_search_set_expression (search1, "(header-contains \"subject\" \"root\")");
	camel_store_search_set_expression (search2, "(header-contains \"subject\" \"root\")");
	success = camel_store_search_rebuild_sync (search1, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search1, 1, "11", 3, "31", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search1, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_NONE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);
	success = camel_store_search_rebuild_sync (search2, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search2, 2, "21", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search2, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_NONE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);

	camel_store_search_set_expression (search1, "(match-threads \"single\" (header-contains \"subject\" \"root\"))");
	camel_store_search_set_expression (search2, "(match-threads \"single\" (header-contains \"subject\" \"root\"))");
	success = camel_store_search_rebuild_sync (search1, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search1, 1, "11", 3, "31", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search1, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_SINGLE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_rebuild_sync (search2, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search2, 2, "21", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search2, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_SINGLE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_add_match_threads_items_sync (search1, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 8); /* all messages from all folders in the @search1 */
	success = camel_store_search_add_match_threads_items_sync (search2, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12); /* all messages from all folders in the @search1 and the @search2 */
	index1 = camel_store_search_ref_result_index (search1);
	g_assert_nonnull (index1);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	index2 = camel_store_search_ref_result_index (search2);
	g_assert_nonnull (index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 1);
	camel_store_search_index_move_from_existing (index1, index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 3);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_unref (index2);
	camel_store_search_index_apply_match_threads (index1, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search1, index1);
	test_store_search_check_result (search1, 1, "11", 3, "31", 0, NULL);
	camel_store_search_set_result_index (search2, index1);
	test_store_search_check_result (search2, 0, NULL);
	g_clear_pointer (&index1, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	camel_store_search_set_expression (search1, "(match-threads \"all\" (header-contains \"subject\" \"root\"))");
	camel_store_search_set_expression (search2, "(match-threads \"all\" (header-contains \"subject\" \"root\"))");
	success = camel_store_search_rebuild_sync (search1, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search1, 1, "11", 3, "31", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search1, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_ALL);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_rebuild_sync (search2, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search2, 2, "21", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search2, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_ALL);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_add_match_threads_items_sync (search1, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 8);
	success = camel_store_search_add_match_threads_items_sync (search2, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12);
	index1 = camel_store_search_ref_result_index (search1);
	g_assert_nonnull (index1);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	index2 = camel_store_search_ref_result_index (search2);
	g_assert_nonnull (index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 1);
	camel_store_search_index_move_from_existing (index1, index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 3);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_unref (index2);
	camel_store_search_index_apply_match_threads (index1, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search1, index1);
	test_store_search_check_result (search1, 1, "11", 1, "12", 1, "14", 1, "13", 1, "15", 3, "31", 0, NULL);
	camel_store_search_set_result_index (search2, index1);
	test_store_search_check_result (search2, 2, "21", 2, "22", 0, NULL);
	g_clear_pointer (&index1, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	camel_store_search_set_expression (search1, "(match-threads \"all\" (or (header-contains \"subject\" \"from 12\") (uid \"33\")))");
	camel_store_search_set_expression (search2, "(match-threads \"all\" (or (header-contains \"subject\" \"from 12\") (uid \"33\")))");
	success = camel_store_search_rebuild_sync (search1, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search1, 1, "12", 3, "33", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search1, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_ALL);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_add_match_threads_items_sync (search1, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 8);
	success = camel_store_search_rebuild_sync (search2, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search2, 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search2, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_ALL);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_add_match_threads_items_sync (search2, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12);
	index1 = camel_store_search_ref_result_index (search1);
	g_assert_nonnull (index1);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	index2 = camel_store_search_ref_result_index (search2);
	g_assert_nonnull (index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_move_from_existing (index1, index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_unref (index2);
	camel_store_search_index_apply_match_threads (index1, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search1, index1);
	test_store_search_check_result (search1, 1, "12", 1, "14", 1, "13", 1, "15", 3, "32", 3, "33", 0, NULL);
	camel_store_search_set_result_index (search2, index1);
	test_store_search_check_result (search2, 2, "21", 2, "22", 2, "23", 2, "24", 0, NULL);
	g_clear_pointer (&index1, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	camel_store_search_set_expression (search1, "(match-threads \"no-subject,all\" (or (header-contains \"subject\" \"from 12\") (uid \"33\")))");
	camel_store_search_set_expression (search2, "(match-threads \"no-subject,all\" (or (header-contains \"subject\" \"from 12\") (uid \"33\")))");
	success = camel_store_search_rebuild_sync (search1, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search1, 1, "12", 3, "33", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search1, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_ALL);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);
	success = camel_store_search_rebuild_sync (search2, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search2, 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search2, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_ALL);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);
	success = camel_store_search_add_match_threads_items_sync (search1, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 8);
	success = camel_store_search_add_match_threads_items_sync (search2, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12);
	index1 = camel_store_search_ref_result_index (search1);
	g_assert_nonnull (index1);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	index2 = camel_store_search_ref_result_index (search2);
	g_assert_nonnull (index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_move_from_existing (index1, index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_unref (index2);
	camel_store_search_index_apply_match_threads (index1, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search1, index1);
	test_store_search_check_result (search1, 1, "12", 1, "14", 1, "13", 1, "15", 3, "32", 3, "33", 0, NULL);
	camel_store_search_set_result_index (search2, index1);
	test_store_search_check_result (search2, 2, "21", 2, "22", 2, "23", NULL);
	g_clear_pointer (&index1, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	camel_store_search_set_expression (search1, "(match-threads \"replies\" (uid \"13\" \"33\"))");
	camel_store_search_set_expression (search2, "(match-threads \"replies\" (uid \"13\" \"33\"))");
	success = camel_store_search_rebuild_sync (search1, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search1, 1, "13", 3, "33", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search1, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_rebuild_sync (search2, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search2, 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search2, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_add_match_threads_items_sync (search1, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 8);
	success = camel_store_search_add_match_threads_items_sync (search2, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12);
	index1 = camel_store_search_ref_result_index (search1);
	g_assert_nonnull (index1);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	index2 = camel_store_search_ref_result_index (search2);
	g_assert_nonnull (index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_move_from_existing (index1, index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_unref (index2);
	camel_store_search_index_apply_match_threads (index1, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search1, index1);
	test_store_search_check_result (search1, 1, "13", 3, "32", 3, "33", 0, NULL);
	camel_store_search_set_result_index (search2, index1);
	test_store_search_check_result (search2, 2, "22", NULL);
	g_clear_pointer (&index1, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	camel_store_search_set_expression (search1, "(match-threads \"no-subject,replies\" (uid \"13\" \"33\"))");
	camel_store_search_set_expression (search2, "(match-threads \"no-subject,replies\" (uid \"13\" \"33\"))");
	success = camel_store_search_rebuild_sync (search1, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search1, 1, "13", 3, "33", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search1, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);
	success = camel_store_search_rebuild_sync (search2, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search2, 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search2, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);
	success = camel_store_search_add_match_threads_items_sync (search1, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 8);
	success = camel_store_search_add_match_threads_items_sync (search2, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12);
	index1 = camel_store_search_ref_result_index (search1);
	g_assert_nonnull (index1);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	index2 = camel_store_search_ref_result_index (search2);
	g_assert_nonnull (index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_move_from_existing (index1, index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_unref (index2);
	camel_store_search_index_apply_match_threads (index1, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search1, index1);
	test_store_search_check_result (search1, 1, "13", 3, "32", 3, "33", 0, NULL);
	camel_store_search_set_result_index (search2, index1);
	test_store_search_check_result (search2, 2, "22", 0, NULL);
	g_clear_pointer (&index1, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	camel_store_search_set_expression (search1, "(match-threads \"replies_parents\" (uid \"13\" \"33\"))");
	camel_store_search_set_expression (search2, "(match-threads \"replies_parents\" (uid \"13\" \"33\"))");
	success = camel_store_search_rebuild_sync (search1, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search1, 1, "13", 3, "33", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search1, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES_AND_PARENTS);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_rebuild_sync (search2, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search2, 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search2, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES_AND_PARENTS);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_add_match_threads_items_sync (search1, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 8);
	success = camel_store_search_add_match_threads_items_sync (search2, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12);
	index1 = camel_store_search_ref_result_index (search1);
	g_assert_nonnull (index1);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	index2 = camel_store_search_ref_result_index (search2);
	g_assert_nonnull (index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_move_from_existing (index1, index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_unref (index2);
	camel_store_search_index_apply_match_threads (index1, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search1, index1);
	test_store_search_check_result (search1, 1, "12", 1, "13", 3, "32", 3, "33", 0, NULL);
	camel_store_search_set_result_index (search2, index1);
	test_store_search_check_result (search2, 2, "21", 2, "22", 2, "23", 0, NULL);
	g_clear_pointer (&index1, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	camel_store_search_set_expression (search1, "(match-threads \"no-subject,replies_parents\" (uid \"13\" \"33\"))");
	camel_store_search_set_expression (search2, "(match-threads \"no-subject,replies_parents\" (uid \"13\" \"33\"))");
	success = camel_store_search_rebuild_sync (search1, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search1, 1, "13", 3, "33", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search1, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES_AND_PARENTS);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);
	success = camel_store_search_rebuild_sync (search2, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search2, 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search2, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES_AND_PARENTS);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);
	success = camel_store_search_add_match_threads_items_sync (search1, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 8);
	success = camel_store_search_add_match_threads_items_sync (search2, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12);
	index1 = camel_store_search_ref_result_index (search1);
	g_assert_nonnull (index1);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	index2 = camel_store_search_ref_result_index (search2);
	g_assert_nonnull (index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_move_from_existing (index1, index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_unref (index2);
	camel_store_search_index_apply_match_threads (index1, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search1, index1);
	test_store_search_check_result (search1, 1, "12", 1, "13", 3, "32", 3, "33", 0, NULL);
	camel_store_search_set_result_index (search2, index1);
	test_store_search_check_result (search2, 2, "21", 2, "22", 2, "23", 0, NULL);
	g_clear_pointer (&index1, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search1);
	g_clear_object (&search2);
	g_clear_object (&store1);
	g_clear_object (&store2);

	/* mix up folders differently and unref indexes before using them */

	store1 = test_store_new_full (session, "test-store-search-1", "Test Store Search 1");
	store2 = test_store_new_full (session, "test-store-search-2", "Test Store Search 2");
	search1 = camel_store_search_new (store1);
	search2 = camel_store_search_new (store2);

	f1 = camel_store_get_folder_sync (store1, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"part", "1 1 0",
		"subject", "single root",
		"",
		"uid", "12",
		"part", "1 2 1 2 1",
		"subject", "reply to 21 from 12",
		"",
		"uid", "14",
		"part", "12 1 1 2 1",
		"subject", "reply to 21 b",
		"",
		"uid", "13",
		"part", "1 3 2 9 9 1 2",
		"subject", "reply to nonexistent 99, referencing 12",
		"",
		"uid", "15",
		"part", "1 31 1 1 2",
		"subject", "reply to 12",
		NULL);
	camel_store_search_add_folder (search1, f1);

	f2 = camel_store_get_folder_sync (store1, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"part", "2 1 0",
		"subject", "root 21",
		"",
		"uid", "22",
		"part", "2 2 1 1 3",
		"subject", "reply to 13",
		"",
		"uid", "23",
		"part", "2 3 1 8 8",
		"subject", "reply to nonexistent 88",
		"",
		"uid", "24",
		"part", "2 4 0",
		"subject", "re: reply to nonexistent 88",
		NULL);
	camel_store_search_add_folder (search1, f2);

	f3 = camel_store_get_folder_sync (store2, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"part", "3 1 0",
		"subject", "single root 31",
		"",
		"uid", "32",
		"part", "3 2 1 3 3",
		"subject", "reply 32",
		"",
		"uid", "33",
		"part", "3 3 1 2 3",
		"subject", "reply in 33",
		NULL);
	camel_store_search_add_folder (search2, f3);

	camel_store_search_set_expression (search1, "(match-threads \"replies_parents\" (uid \"13\" \"33\"))");
	camel_store_search_set_expression (search2, "(match-threads \"replies_parents\" (uid \"13\" \"33\"))");
	success = camel_store_search_rebuild_sync (search1, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search1, 1, "13", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search1, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES_AND_PARENTS);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_rebuild_sync (search2, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search2, 3, "33", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search2, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES_AND_PARENTS);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_add_match_threads_items_sync (search1, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 9);
	success = camel_store_search_add_match_threads_items_sync (search2, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12);
	index1 = camel_store_search_ref_result_index (search1);
	g_assert_nonnull (index1);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 1);
	index2 = camel_store_search_ref_result_index (search2);
	g_assert_nonnull (index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 1);
	camel_store_search_index_move_from_existing (index1, index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_unref (index2);
	camel_store_search_index_apply_match_threads (index1, items, match_threads_kind, thread_flags, NULL);
	g_clear_pointer (&items, g_ptr_array_unref);
	camel_store_search_set_result_index (search1, index1);
	camel_store_search_set_result_index (search2, index1);
	g_clear_pointer (&index1, camel_store_search_index_unref);
	test_store_search_check_result (search1, 1, "12", 1, "13", 2, "21", 2, "22", 2, "23", 0, NULL);
	test_store_search_check_result (search2, 3, "32", 3, "33", 0, NULL);

	camel_store_search_set_expression (search1, "(match-threads \"no-subject,replies_parents\" (uid \"13\" \"33\"))");
	camel_store_search_set_expression (search2, "(match-threads \"no-subject,replies_parents\" (uid \"13\" \"33\"))");
	success = camel_store_search_rebuild_sync (search1, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search1, 1, "13", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search1, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES_AND_PARENTS);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);
	success = camel_store_search_rebuild_sync (search2, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search2, 3, "33", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search2, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_REPLIES_AND_PARENTS);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);
	success = camel_store_search_add_match_threads_items_sync (search1, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 9);
	success = camel_store_search_add_match_threads_items_sync (search2, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 12);
	index1 = camel_store_search_ref_result_index (search1);
	g_assert_nonnull (index1);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 1);
	index2 = camel_store_search_ref_result_index (search2);
	g_assert_nonnull (index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 1);
	camel_store_search_index_move_from_existing (index1, index2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index1), ==, 2);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index2), ==, 0);
	camel_store_search_index_unref (index2);
	camel_store_search_index_apply_match_threads (index1, items, match_threads_kind, thread_flags, NULL);
	g_clear_pointer (&items, g_ptr_array_unref);
	camel_store_search_set_result_index (search1, index1);
	camel_store_search_set_result_index (search2, index1);
	g_clear_pointer (&index1, camel_store_search_index_unref);
	test_store_search_check_result (search1, 1, "12", 1, "13", 2, "21", 2, "22", 2, "23", 0, NULL);
	test_store_search_check_result (search2, 3, "32", 3, "33", 0, NULL);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search1);
	g_clear_object (&search2);
	g_clear_object (&store1);
	g_clear_object (&store2);
	g_clear_object (&session);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_match_threads_only_leaves (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelStoreSearchIndex *index;
	CamelFolder *folder;
	CamelMatchThreadsKind match_threads_kind;
	CamelFolderThreadFlags thread_flags = 0;
	GPtrArray *items = NULL;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	folder = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (folder);
	test_add_messages (folder,
		"uid", "11",
		"part", "1 1 0",
		"subject", "single root",
		"",
		"uid", "12",
		"part", "1 2 1 2 1",
		"subject", "reply to 21 from 12",
		"",
		"uid", "13",
		"part", "1 3 2 9 9 1 2",
		"subject", "reply to nonexistent 99, referencing 12",
		"",
		"uid", "14",
		"part", "1 4 1 2 1",
		"subject", "reply to 21 b",
		"",
		"uid", "15",
		"part", "1 5 1 2 1",
		"subject", "reply to 21",
		NULL);
	camel_store_search_add_folder (search, folder);

	/* The thread looks like:
	     11
	     12
	       13
	       14
	       15
	 */

	camel_store_search_set_expression (search, "(header-contains \"subject\" \"root\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_NONE);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_NONE);

	camel_store_search_set_expression (search, "(match-threads \"all\" (header-contains \"subject\" \"from\"))");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* it does know know what to do with the match-threads yet, it requires special (and manual) post-processing */
	test_store_search_check_result (search, 1, "12", 0, NULL);
	match_threads_kind = camel_store_search_get_match_threads_kind (search, &thread_flags);
	g_assert_cmpint (match_threads_kind, ==, CAMEL_MATCH_THREADS_KIND_ALL);
	g_assert_cmpint (thread_flags, ==, CAMEL_FOLDER_THREAD_FLAG_SUBJECT);
	success = camel_store_search_add_match_threads_items_sync (search, &items, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 5); /* all messages from all folders in the @search */
	index = camel_store_search_ref_result_index (search);
	g_assert_nonnull (index);
	g_assert_cmpuint (g_hash_table_size ((GHashTable *) index), ==, 1);
	camel_store_search_index_apply_match_threads (index, items, match_threads_kind, thread_flags, NULL);
	camel_store_search_set_result_index (search, index);
	test_store_search_check_result (search, 1, "12", 1, "13", 1, "14", 1, "15", 0, NULL);
	g_clear_pointer (&index, camel_store_search_index_unref);
	g_clear_pointer (&items, g_ptr_array_unref);

	g_clear_object (&folder);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_folder (void)
{
	CamelStore *store;
	CamelFolder *folder;
	GPtrArray *uids = NULL;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();

	folder = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (folder);
	test_add_messages (folder,
		"uid", "11",
		"subject", "Message 11",
		"",
		"uid", "12",
		"subject", "Message 12",
		"",
		"uid", "13",
		"subject", "Subject 13",
		"",
		"uid", "21",
		"subject", "Message 21",
		"",
		"uid", "22",
		"subject", "Message 22",
		"",
		"uid", "23",
		"subject", "Subject 23",
		"",
		"uid", "31",
		"subject", "Different Subject Message",
		NULL);

	success = camel_folder_search_sync (folder, "(header-contains \"subject\" \"nothing_known\")", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_cmpuint (uids->len, ==, 0);
	g_clear_pointer (&uids, g_ptr_array_unref);

	success = camel_folder_search_sync (folder, "(header-contains \"subject\" \"mess\")", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_cmpuint (uids->len, ==, 5);
	test_store_search_check_folder_uids (uids, "11", "12", "21", "22", "31", NULL);
	g_clear_pointer (&uids, g_ptr_array_unref);

	g_clear_object (&folder);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_folder_match_threads (void)
{
	CamelStore *store;
	CamelFolder *folder;
	GPtrArray *uids = NULL;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();

	folder = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (folder);
	test_add_messages (folder,
		"uid", "11",
		"part", "1 1 0",
		"subject", "single root",
		"",
		"uid", "12",
		"part", "1 2 1 2 1",
		"subject", "reply to 21 from 12",
		"",
		"uid", "14",
		"part", "12 1 1 2 1",
		"subject", "reply to 21 b",
		"",
		"uid", "13",
		"part", "1 3 2 9 9 1 2",
		"subject", "reply to nonexistent 99, referencing 12",
		"",
		"uid", "15",
		"part", "1 31 1 1 2",
		"subject", "reply to 12",
		"",
		"uid", "21",
		"part", "2 1 0",
		"subject", "root 21",
		"",
		"uid", "22",
		"part", "2 2 1 1 3",
		"subject", "reply to 13",
		"",
		"uid", "23",
		"part", "2 3 1 8 8",
		"subject", "reply to nonexistent 88",
		"",
		"uid", "24",
		"part", "2 4 0",
		"subject", "re: reply to nonexistent 88",
		"",
		"uid", "31",
		"part", "3 1 0",
		"subject", "single root 31",
		"",
		"uid", "32",
		"part", "3 2 1 3 3",
		"subject", "reply 32",
		"",
		"uid", "33",
		"part", "3 3 1 2 3",
		"subject", "reply in 33",
		NULL);

	/* The thread looks like:
	     11
	     21
	       12
		 13
		   22
		 15
	       14
	     23
	       33
		 32
	       24 (if threading by subject)
	     31
	     24 (if not threading by subject)
	 */

	success = camel_folder_search_sync (folder, "(match-threads \"single\" (header-contains \"subject\" \"root\"))", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_cmpint (uids->len, ==, 2);
	test_store_search_check_folder_uids (uids, "11", "31", NULL);
	g_clear_pointer (&uids, g_ptr_array_unref);

	success = camel_folder_search_sync (folder, "(match-threads \"all\" (header-contains \"subject\" \"root\"))", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_cmpint (uids->len, ==, 8);
	test_store_search_check_folder_uids (uids, "11", "12", "14", "13", "15", "21", "22", "31", NULL);
	g_clear_pointer (&uids, g_ptr_array_unref);

	success = camel_folder_search_sync (folder, "(match-threads \"all\" (or (header-contains \"subject\" \"from 12\") (uid \"33\")))", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_cmpint (uids->len, ==, 10);
	test_store_search_check_folder_uids (uids, "12", "14", "13", "15", "21", "22", "23", "24", "32", "33", NULL);
	g_clear_pointer (&uids, g_ptr_array_unref);

	success = camel_folder_search_sync (folder, "(match-threads \"no-subject,all\" (or (header-contains \"subject\" \"from 12\") (uid \"33\")))", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_cmpint (uids->len, ==, 9);
	test_store_search_check_folder_uids (uids, "12", "14", "13", "15", "21", "22", "23", "32", "33", NULL);
	g_clear_pointer (&uids, g_ptr_array_unref);

	success = camel_folder_search_sync (folder, "(match-threads \"replies\" (uid \"13\" \"33\"))", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_cmpint (uids->len, ==, 4);
	test_store_search_check_folder_uids (uids, "13", "22", "32", "33", NULL);
	g_clear_pointer (&uids, g_ptr_array_unref);

	success = camel_folder_search_sync (folder, "(match-threads \"no-subject,replies\" (uid \"13\" \"33\"))", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_cmpint (uids->len, ==, 4);
	test_store_search_check_folder_uids (uids, "13", "22", "32", "33", NULL);
	g_clear_pointer (&uids, g_ptr_array_unref);

	success = camel_folder_search_sync (folder, "(match-threads \"replies_parents\" (uid \"13\" \"33\"))", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_cmpint (uids->len, ==, 7);
	test_store_search_check_folder_uids (uids, "12", "13", "21", "22", "23", "32", "33", NULL);
	g_clear_pointer (&uids, g_ptr_array_unref);

	success = camel_folder_search_sync (folder, "(match-threads \"no-subject,replies_parents\" (uid \"13\" \"33\"))", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_cmpint (uids->len, ==, 7);
	test_store_search_check_folder_uids (uids, "12", "13", "21", "22", "23", "32", "33", NULL);
	g_clear_pointer (&uids, g_ptr_array_unref);

	g_clear_object (&folder);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_folder_match_threads_only_leaves (void)
{
	CamelStore *store;
	CamelFolder *folder;
	GPtrArray *uids = NULL;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();

	folder = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (folder);
	test_add_messages (folder,
		"uid", "11",
		"part", "1 1 0",
		"subject", "single root",
		"",
		"uid", "12",
		"part", "1 2 1 2 1",
		"subject", "reply to 21 from 12",
		"",
		"uid", "13",
		"part", "1 3 2 9 9 1 2",
		"subject", "reply to nonexistent 99, referencing 12",
		"",
		"uid", "14",
		"part", "1 4 1 2 1",
		"subject", "reply to 21 b",
		"",
		"uid", "15",
		"part", "1 5 1 2 1",
		"subject", "reply to 21",
		NULL);

	/* The thread looks like:
	     11
	     12
	       13
	       14
	       15
	 */

	success = camel_folder_search_sync (folder, "(match-threads \"all\" (header-contains \"subject\" \"from\"))", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_cmpint (uids->len, ==, 4);
	test_store_search_check_folder_uids (uids, "12", "13", "14", "15", NULL);
	g_clear_pointer (&uids, g_ptr_array_unref);

	g_clear_object (&folder);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_bool_sexp (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolder *f1, *f2, *f3;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"subject", "s11",
		"",
		"uid", "12",
		"subject", "s12",
		"",
		"uid", "13",
		"subject", "s13",
		NULL);
	camel_store_search_add_folder (search, f1);

	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"subject", "s21",
		"to", "to@no.where",
		"flags", (guint32) CAMEL_MESSAGE_DELETED,
		"",
		"uid", "22",
		"subject", "s22",
		"",
		"uid", "23",
		"subject", "s23",
		NULL);
	camel_store_search_add_folder (search, f2);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"subject", "s31",
		NULL);
	camel_store_search_add_folder (search, f3);

	camel_store_search_set_expression (search, "(match-all #t)");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 2, "22", 2, "23", 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "(match-all #f)");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	camel_store_search_set_expression (search, "#t");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "12", 1, "13", 2, "21", 2, "22", 2, "23", 3, "31", 0, NULL);

	camel_store_search_set_expression (search, "#f");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_match_index (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelStoreSearchIndex *index1, *index2;
	CamelFolder *f1, *f2;
	GPtrArray *indexes;
	GError *local_error = NULL;
	gchar *expr;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"subject", "s11",
		"",
		"uid", "12",
		"subject", "s12",
		"",
		"uid", "13",
		"subject", "s13",
		NULL);
	camel_store_search_add_folder (search, f1);

	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"subject", "s21",
		"",
		"uid", "22",
		"subject", "s22",
		"",
		"uid", "23",
		"subject", "s23",
		NULL);
	camel_store_search_add_folder (search, f2);

	camel_store_search_set_expression (search, "(header-contains \"subject\" \"2\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "12", 2, "21", 2, "22", 2, "23", 0, NULL);

	indexes = camel_store_search_list_match_indexes (search);
	g_assert_nonnull (indexes);
	g_assert_cmpuint (indexes->len, ==, 0);
	g_clear_pointer (&indexes, g_ptr_array_unref);

	index1 = camel_store_search_index_new ();
	camel_store_search_remove_match_index (search, index1);
	indexes = camel_store_search_list_match_indexes (search);
	g_assert_nonnull (indexes);
	g_assert_cmpuint (indexes->len, ==, 0);
	g_clear_pointer (&indexes, g_ptr_array_unref);

	camel_store_search_add_match_index (search, index1);
	indexes = camel_store_search_list_match_indexes (search);
	g_assert_nonnull (indexes);
	g_assert_cmpuint (indexes->len, ==, 1);
	g_assert_true (g_ptr_array_find (indexes, index1, NULL));
	g_clear_pointer (&indexes, g_ptr_array_unref);

	camel_store_search_remove_match_index (search, index1);
	indexes = camel_store_search_list_match_indexes (search);
	g_assert_nonnull (indexes);
	g_assert_cmpuint (indexes->len, ==, 0);
	g_clear_pointer (&indexes, g_ptr_array_unref);

	camel_store_search_add_match_index (search, index1);
	indexes = camel_store_search_list_match_indexes (search);
	g_assert_nonnull (indexes);
	g_assert_cmpuint (indexes->len, ==, 1);
	g_assert_true (g_ptr_array_find (indexes, index1, NULL));
	g_clear_pointer (&indexes, g_ptr_array_unref);

	camel_store_search_index_add (index1, store, 1, "11");

	expr = g_strdup_printf ("(in-match-index \"%p\")", index1);
	camel_store_search_set_expression (search, expr);
	g_free (expr);
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 0, NULL);

	index2 = camel_store_search_index_new ();

	camel_store_search_remove_match_index (search, index2);
	indexes = camel_store_search_list_match_indexes (search);
	g_assert_nonnull (indexes);
	g_assert_cmpuint (indexes->len, ==, 1);
	g_assert_true (g_ptr_array_find (indexes, index1, NULL));
	g_clear_pointer (&indexes, g_ptr_array_unref);

	camel_store_search_add_match_index (search, index2);
	indexes = camel_store_search_list_match_indexes (search);
	g_assert_nonnull (indexes);
	g_assert_cmpuint (indexes->len, ==, 2);
	g_assert_true (g_ptr_array_find (indexes, index1, NULL));
	g_assert_true (g_ptr_array_find (indexes, index2, NULL));
	g_clear_pointer (&indexes, g_ptr_array_unref);

	camel_store_search_remove_match_index (search, index1);
	indexes = camel_store_search_list_match_indexes (search);
	g_assert_nonnull (indexes);
	g_assert_cmpuint (indexes->len, ==, 1);
	g_assert_true (g_ptr_array_find (indexes, index2, NULL));
	g_clear_pointer (&indexes, g_ptr_array_unref);

	camel_store_search_add_match_index (search, index1);
	camel_store_search_remove_match_index (search, index2);
	indexes = camel_store_search_list_match_indexes (search);
	g_assert_nonnull (indexes);
	g_assert_cmpuint (indexes->len, ==, 1);
	g_assert_true (g_ptr_array_find (indexes, index1, NULL));
	g_clear_pointer (&indexes, g_ptr_array_unref);

	camel_store_search_remove_match_index (search, index1);
	indexes = camel_store_search_list_match_indexes (search);
	g_assert_nonnull (indexes);
	g_assert_cmpuint (indexes->len, ==, 0);
	g_clear_pointer (&indexes, g_ptr_array_unref);

	camel_store_search_add_match_index (search, index1);
	camel_store_search_add_match_index (search, index2);
	indexes = camel_store_search_list_match_indexes (search);
	g_assert_nonnull (indexes);
	g_assert_cmpuint (indexes->len, ==, 2);
	g_assert_true (g_ptr_array_find (indexes, index1, NULL));
	g_assert_true (g_ptr_array_find (indexes, index2, NULL));
	g_clear_pointer (&indexes, g_ptr_array_unref);

	camel_store_search_index_add (index2, store, 2, "22");

	expr = g_strdup_printf ("(in-match-index \"%p\")", index2);
	camel_store_search_set_expression (search, expr);
	g_free (expr);
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 2, "22", 0, NULL);

	expr = g_strdup_printf ("(or (header-contains \"subject\" \"3\") (in-match-index \"%p\") (in-match-index \"%p\"))", index1, index2);
	camel_store_search_set_expression (search, expr);
	g_free (expr);
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "11", 1, "13", 2, "22", 2, "23", 0, NULL);

	camel_store_search_index_add (index1, store, 1, "13");
	camel_store_search_index_add (index2, store, 1, "13");

	expr = g_strdup_printf ("(and (header-contains \"subject\" \"3\") (in-match-index \"%p\") (in-match-index \"%p\"))", index1, index2);
	camel_store_search_set_expression (search, expr);
	g_free (expr);
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 0, NULL);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&search);
	g_clear_object (&store);
	g_clear_pointer (&index1, camel_store_search_index_unref);
	g_clear_pointer (&index2, camel_store_search_index_unref);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

static void
test_store_search_summary_changes (void)
{
	CamelStore *store;
	CamelStoreSearch *search;
	CamelFolder *f1;
	CamelFolderSummary *summary;
	GError *local_error = NULL;
	gboolean success;

	store = test_store_new ();
	search = camel_store_search_new (store);

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	summary = camel_folder_get_folder_summary (f1);
	g_assert_nonnull (summary);
	test_add_messages (f1,
		"uid", "11",
		"subject", "s11",
		"",
		"uid", "12",
		"subject", "s12",
		"",
		"uid", "13",
		"subject", "s13",
		NULL);
	camel_store_search_add_folder (search, f1);

	camel_store_search_set_expression (search, "(header-contains \"subject\" \"3\")");
	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 0, NULL);

	#define add_info(_uid, _summ, _flags) G_STMT_START { \
		CamelMessageInfo *nfo; \
		nfo = camel_message_info_new (summary); \
		camel_message_info_set_uid (nfo, _uid); \
		camel_message_info_set_subject (nfo, _summ); \
		camel_message_info_set_flags (nfo, ~0, _flags); \
		camel_folder_summary_add (summary, nfo, TRUE); \
		g_clear_object (&nfo); \
		} G_STMT_END

	add_info ("31", "s31", 0);

	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 1, "31", 0, NULL);

	success = camel_folder_summary_save (summary, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "13", 1, "31", 0, NULL);

	add_info ("33", "s33", 0);

	camel_folder_summary_remove_uid (summary, "13");

	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "31", 1, "33", 0, NULL);

	success = camel_folder_summary_load (summary, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 1, "31", 1, "33", 0, NULL);

	success = camel_folder_summary_clear (summary, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	success = camel_store_search_rebuild_sync (search, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_store_search_check_result (search, 0, NULL);

	#define check_counts(_saved, _unread, _deleted, _junk, _junk_not_deleted, _visible) \
		g_assert_cmpuint (camel_folder_summary_get_saved_count (summary), ==, _saved); \
		g_assert_cmpuint (camel_folder_summary_get_unread_count (summary), ==, _unread); \
		g_assert_cmpuint (camel_folder_summary_get_deleted_count (summary), ==, _deleted); \
		g_assert_cmpuint (camel_folder_summary_get_junk_count (summary), ==, _junk); \
		g_assert_cmpuint (camel_folder_summary_get_junk_not_deleted_count (summary), ==, _junk_not_deleted); \
		g_assert_cmpuint (camel_folder_summary_get_visible_count (summary), ==, _visible);

	check_counts (0, 0, 0, 0, 0, 0);

	add_info ("1", "s1", 0);
	check_counts (1, 1, 0, 0, 0, 1);

	success = camel_folder_summary_save (summary, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	check_counts (1, 1, 0, 0, 0, 1);

	success = camel_folder_summary_load (summary, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	check_counts (1, 1, 0, 0, 0, 1);

	add_info ("2", "s2", CAMEL_MESSAGE_SEEN);
	check_counts (2, 1, 0, 0, 0, 2);

	success = camel_folder_summary_save (summary, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	check_counts (2, 1, 0, 0, 0, 2);

	success = camel_folder_summary_load (summary, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	check_counts (2, 1, 0, 0, 0, 2);

	#undef check_counts
	#undef add_info

	g_clear_object (&f1);
	g_clear_object (&search);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

typedef struct _SimultaneousData {
	GMainLoop *loop;
	CamelStore *store;
	CamelFolder *f1;
	gint n_pending;
} SimultaneousData;

static gboolean
test_simultaneous_timeout_cb (gpointer user_data)
{
	g_assert_not_reached ();

	return G_SOURCE_REMOVE;
}

/* only for testing purposes */
void _camel_folder_summary_unload_uid (CamelFolderSummary *self, const gchar *uid);

static gpointer
test_simultaneous_stress_thread (SimultaneousData *sd,
				const gchar *uid)
{
	CamelDB *cdb;
	CamelFolder *f1 = sd->f1;
	CamelFolderSummary *summary = camel_folder_get_folder_summary (f1);
	CamelMessageInfo *mi;
	GError *local_error = NULL;
	gboolean success;
	gboolean is_repeated = g_strcmp0 (uid, "11") == 0;
	guint ii;

	g_assert_nonnull (sd);

	cdb = CAMEL_DB (camel_store_get_db (sd->store));
	g_assert_nonnull (cdb);

	for (ii = 0; ii < SIMULTANEOUS_N_REPEATS; ii++) {
		mi = camel_folder_summary_get (summary, uid);
		g_assert_nonnull (mi);
		camel_message_info_set_size (mi, ii);
		g_clear_object (&mi);

		success = camel_folder_summary_save (summary, &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);

		mi = camel_folder_summary_get (summary, uid);
		g_assert_nonnull (mi);
		if (!is_repeated)
			g_assert_cmpuint (camel_message_info_get_size (mi), ==, ii);
		g_clear_object (&mi);

		_camel_folder_summary_unload_uid (summary, uid);

		mi = camel_folder_summary_get (summary, uid);
		g_assert_nonnull (mi);
		if (!is_repeated)
			g_assert_cmpuint (camel_message_info_get_size (mi), ==, ii);
		g_clear_object (&mi);
	}

	if (g_atomic_int_dec_and_test (&sd->n_pending))
		g_main_loop_quit (sd->loop);

	return NULL;
}

static gpointer
test_simultaneous_stress_thread1 (gpointer user_data)
{
	return test_simultaneous_stress_thread (user_data, "11");
}

static gpointer
test_simultaneous_stress_thread2 (gpointer user_data)
{
	return test_simultaneous_stress_thread (user_data, "12");
}

static gpointer
test_simultaneous_stress_thread3 (gpointer user_data)
{
	return test_simultaneous_stress_thread (user_data, "13");
}

static gpointer
test_simultaneous_stress_thread4 (gpointer user_data)
{
	return test_simultaneous_stress_thread (user_data, "11");
}

static gboolean
test_simultaneous_stress_start_idle_cb (gpointer user_data)
{
	SimultaneousData *sd = user_data;

	sd->n_pending = 4;

	g_thread_unref (g_thread_new ("test_simultaneous_stress_thread1", test_simultaneous_stress_thread1, user_data));
	g_thread_unref (g_thread_new ("test_simultaneous_stress_thread2", test_simultaneous_stress_thread2, user_data));
	g_thread_unref (g_thread_new ("test_simultaneous_stress_thread3", test_simultaneous_stress_thread3, user_data));
	g_thread_unref (g_thread_new ("test_simultaneous_stress_thread4", test_simultaneous_stress_thread4, user_data));

	return G_SOURCE_REMOVE;
}

static void
test_store_search_simultaneous_read_write_stress (void)
{
	SimultaneousData sd = { NULL, };
	CamelStore *store;
	CamelStoreDB *sdb;
	CamelFolder *f1;
	GMainLoop *loop;
	GError *local_error = NULL;
	guint source_id;

	store = test_store_new ();

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"subject", "s11",
		"flags", (guint32) (CAMEL_MESSAGE_SEEN),
		"",
		"uid", "12",
		"subject", "s12",
		"",
		"uid", "13",
		"subject", "s13",
		NULL);

	sdb = camel_store_get_db (store);
	g_assert_nonnull (sdb);

	loop = g_main_loop_new (NULL, FALSE);

	/* this is testing deadlock, thus set a short timeout for the test and run it in a thread */
	source_id = g_timeout_add_seconds (SIMULTANEOUS_TIMEOUT_SECS, test_simultaneous_timeout_cb, NULL);
	g_assert_cmpuint (source_id, !=, 0);

	sd.loop = loop;
	sd.store = store;
	sd.f1 = f1;

	g_idle_add (test_simultaneous_stress_start_idle_cb, &sd);
	g_main_loop_run (loop);

	g_source_remove (source_id);
	g_main_loop_unref (loop);
	g_clear_object (&f1);
	g_clear_object (&store);

	test_session_wait_for_pending_jobs ();
	test_session_check_finalized ();
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/-/issues/");

	g_test_add_func ("/CamelStoreSearch/Create", test_store_search_create);
	g_test_add_func ("/CamelStoreSearch/Subject", test_store_search_subject);
	g_test_add_func ("/CamelStoreSearch/Addresses", test_store_search_addresses);
	g_test_add_func ("/CamelStoreSearch/Flags", test_store_search_flags);
	g_test_add_func ("/CamelStoreSearch/UserTags", test_store_search_user_flags);
	g_test_add_func ("/CamelStoreSearch/UserFlags", test_store_search_user_tags);
	g_test_add_func ("/CamelStoreSearch/UID", test_store_search_uid);
	g_test_add_func ("/CamelStoreSearch/Headers", test_store_search_headers);
	g_test_add_func ("/CamelStoreSearch/Dates", test_store_search_dates);
	g_test_add_func ("/CamelStoreSearch/Size", test_store_search_size);
	g_test_add_func ("/CamelStoreSearch/MessageID", test_store_search_message_id);
	g_test_add_func ("/CamelStoreSearch/MessageLocation", test_store_search_message_location);
	g_test_add_func ("/CamelStoreSearch/AddressbookContains", test_store_search_addressbook_contains);
	g_test_add_func ("/CamelStoreSearch/Body", test_store_search_body);
	g_test_add_func ("/CamelStoreSearch/Extras", test_store_search_extras);
	g_test_add_func ("/CamelStoreSearch/Index", test_store_search_index);
	g_test_add_func ("/CamelStoreSearch/MatchThreads", test_store_search_match_threads);
	g_test_add_func ("/CamelStoreSearch/MatchThreadsMultipleStores", test_store_search_match_threads_multiple_stores);
	g_test_add_func ("/CamelStoreSearch/MatchThreadsOnlyLeaves", test_store_search_match_threads_only_leaves);
	g_test_add_func ("/CamelStoreSearch/Folder", test_store_search_folder);
	g_test_add_func ("/CamelStoreSearch/FolderMatchThreads", test_store_search_folder_match_threads);
	g_test_add_func ("/CamelStoreSearch/FolderMatchThreadsOnlyLeaves", test_store_search_folder_match_threads_only_leaves);
	g_test_add_func ("/CamelStoreSearch/BoolSExp", test_store_search_bool_sexp);
	g_test_add_func ("/CamelStoreSearch/MatchIndex", test_store_search_match_index);
	g_test_add_func ("/CamelStoreSearch/SummaryChanges", test_store_search_summary_changes);
	g_test_add_func ("/CamelStoreSearch/SimultaneousReadWriteStress", test_store_search_simultaneous_read_write_stress);

	return g_test_run ();
}

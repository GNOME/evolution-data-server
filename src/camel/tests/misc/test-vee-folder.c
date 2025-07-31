/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <glib.h>

#include "camel/camel.h"

#include "lib-camel-test-utils.c"

typedef enum _Op {
	OP_ADD,
	OP_REMOVE,
	OP_DUP_CHANGES
} Op;

typedef struct _WaitForData {
	gboolean done;
	Op op;
	const gchar *uid;
	CamelFolderChangeInfo *changes;
} WaitForData;

static void
test_vee_folder_changed_cb (CamelFolder *folder,
			    CamelFolderChangeInfo *changes,
			    gpointer user_data)
{
	WaitForData *wfd = user_data;
	GPtrArray *uids = NULL;

	switch (wfd->op) {
	case OP_ADD:
		uids = changes->uid_added;
		break;
	case OP_REMOVE:
		uids = changes->uid_removed;
		break;
	case OP_DUP_CHANGES:
		wfd->changes = camel_folder_change_info_copy (changes);
		wfd->done = TRUE;
		return;
	default:
		g_assert_not_reached ();
		break;
	}

	g_assert_nonnull (uids);

	if (g_ptr_array_find_with_equal_func (uids, wfd->uid, (GEqualFunc) g_str_has_suffix, NULL))
		wfd->done = TRUE;
}

static CamelFolderChangeInfo *
test_vee_folder_wait_for_change (CamelVeeFolder *vf,
				 Op op,
				 const gchar *uid)
{
	WaitForData wfd = { 0, };
	gulong handler_id;
	guint timeout_id;

	g_assert_true (op == OP_ADD || op == OP_REMOVE || op == OP_DUP_CHANGES);

	wfd.done = FALSE;
	wfd.op = op;
	wfd.uid = uid;
	wfd.changes = NULL;

	handler_id = g_signal_connect (vf, "changed", G_CALLBACK (test_vee_folder_changed_cb), &wfd);
	g_assert_cmpuint (handler_id, !=, 0);

	timeout_id = g_timeout_add_seconds (DELAY_TIMEOUT_SECONDS, test_util_abort_on_timeout_cb, NULL);
	g_assert_cmpuint (timeout_id, !=, 0);

	while (!wfd.done) {
		g_main_context_iteration (NULL, TRUE);
	}

	g_assert_true (g_source_remove (timeout_id));
	g_signal_handler_disconnect (vf, handler_id);

	test_session_wait_for_pending_jobs ();

	return wfd.changes;
}

static CamelFolderChangeInfo *
test_vee_folder_wait_for_change_info (gpointer folder)
{
	return test_vee_folder_wait_for_change (folder, OP_DUP_CHANGES, NULL);
}

typedef struct _SaveChangesData {
	CamelFolder *folder;
	CamelFolderChangeInfo **out_changes;
	guint *p_n_left;
} SaveChangesData;

static void
test_vee_folder_save_changes_cb (CamelFolder *folder,
				 CamelFolderChangeInfo *changes,
				 gpointer user_data)
{
	SaveChangesData *scd = user_data;

	g_assert_null (*(scd->out_changes));
	g_assert_cmpuint (*(scd->p_n_left), >, 0);

	*(scd->out_changes) = camel_folder_change_info_copy (changes);
	*(scd->p_n_left) = *(scd->p_n_left) - 1;
}

static void
test_vee_folder_wait_for_change_infos (gpointer folder,
				       CamelFolderChangeInfo **out_changes,
				       ...) G_GNUC_NULL_TERMINATED;

static void
test_vee_folder_wait_for_change_infos (gpointer folder,
				       CamelFolderChangeInfo **out_changes,
				       ...)
{
	GPtrArray *scd_array; /* SaveChangesData * */
	va_list ap;
	guint n_left = 0, ii;
	guint timeout_id;

	scd_array = g_ptr_array_new_with_free_func (g_free);
	va_start (ap, out_changes);

	while (folder) {
		SaveChangesData *scd;

		g_assert_true (CAMEL_IS_FOLDER (folder));
		g_assert_nonnull (out_changes);
		g_assert_null (*out_changes);

		n_left++;

		scd = g_new0 (SaveChangesData, 1);
		scd->folder = folder;
		scd->out_changes = out_changes;
		scd->p_n_left = &n_left;

		g_signal_connect (scd->folder, "changed", G_CALLBACK (test_vee_folder_save_changes_cb), scd);
		g_ptr_array_add (scd_array, scd);

		folder = va_arg (ap, CamelFolder *);
		out_changes = va_arg (ap, CamelFolderChangeInfo **);
	}

	va_end (ap);

	g_assert_cmpuint (n_left, >, 0);

	timeout_id = g_timeout_add_seconds (DELAY_TIMEOUT_SECONDS, test_util_abort_on_timeout_cb, NULL);
	g_assert_cmpuint (timeout_id, !=, 0);

	while (n_left > 0) {
		g_main_context_iteration (NULL, TRUE);
	}

	g_assert_true (g_source_remove (timeout_id));

	for (ii = 0; ii < scd_array->len; ii++) {
		SaveChangesData *scd = g_ptr_array_index (scd_array, ii);
		g_signal_handlers_disconnect_by_func (scd->folder, G_CALLBACK (test_vee_folder_save_changes_cb), scd);
	}

	g_ptr_array_unref (scd_array);
}

/* this mimics vee_folder_create_subfolder_id() with addition of the original UID;
   if that function changes this one should too */
static void
test_vee_folder_fill_vuid (gchar vuid[11],
			   gpointer fldr,
			   const gchar *original_uid)
{
	CamelFolder *folder = CAMEL_FOLDER (fldr);
	CamelStore *parent_store;
	GChecksum *checksum;
	guint8 *digest;
	gsize length;
	gint state = 0, save = 0;
	const gchar *service_uid;
	gint ii;

	if (original_uid)
		g_assert_cmpuint (strlen (original_uid), ==, 2);

	length = g_checksum_type_get_length (G_CHECKSUM_MD5);
	digest = g_alloca (length);

	parent_store = camel_folder_get_parent_store (folder);
	service_uid = camel_service_get_uid (CAMEL_SERVICE (parent_store));

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	g_checksum_update (checksum, (guchar *) service_uid, -1);
	g_checksum_update (checksum, (guchar *) camel_folder_get_full_name (folder), -1);

	g_checksum_get_digest (checksum, digest, &length);
	g_checksum_free (checksum);

	g_base64_encode_step (digest, 6, FALSE, vuid, &state, &save);
	g_base64_encode_close (FALSE, vuid, &state, &save);

	for (ii = 0; ii < 8; ii++) {
		if (vuid[ii] == '+')
			vuid[ii] = '.';
		if (vuid[ii] == '/')
			vuid[ii] = '_';
	}

	if (original_uid) {
		vuid[8] = original_uid[0];
		vuid[9] = original_uid[1];
		vuid[10] = '\0';
	} else {
		vuid[8] = '\0';
	}
}

static void
test_vee_folder_create_folders (CamelStore **out_store,
				CamelFolder **out_f1,
				CamelFolder **out_f2,
				CamelFolder **out_f3)
{
	CamelStore *store;
	CamelFolder *f1, *f2, *f3;
	GError *local_error = NULL;

	store = test_store_new ();

	f1 = camel_store_get_folder_sync (store, "f1", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f1);
	test_add_messages (f1,
		"uid", "11",
		"subject", "Message 11",
		"",
		"uid", "12",
		"subject", "msg 12",
		"",
		"uid", "13",
		"subject", "Subject 13",
		NULL);

	f2 = camel_store_get_folder_sync (store, "f2", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f2);
	test_add_messages (f2,
		"uid", "21",
		"subject", "Message 21",
		"",
		"uid", "22",
		"subject", "Subject 22",
		"",
		"uid", "23",
		"subject", "Subject 23",
		NULL);

	f3 = camel_store_get_folder_sync (store, "f3", 0, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (f3);
	test_add_messages (f3,
		"uid", "31",
		"subject", "Different Subject Message 31",
		NULL);

	*out_store = store;
	*out_f1 = f1;
	*out_f2 = f2;
	*out_f3 = f3;
}

static CamelVeeStore *
test_vee_folder_create_vee_store (void)
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

static const gchar *
test_vee_folder_last_chars (const gchar *str,
			    guint n_chars)
{
	guint len;

	if (!str)
		return str;

	len = strlen (str);
	if (n_chars >= len)
		return str;

	return str + len - n_chars;
}

static void
test_vee_folder_check_uids (gpointer fldr,
			    ...) G_GNUC_NULL_TERMINATED;

/* expects const gchar *uid arguments, terminated by NULL, referencing original folder UID */
static void
test_vee_folder_check_uids (gpointer fldr,
			    ...)
{
	CamelFolder *folder = CAMEL_FOLDER (fldr);
	CamelFolderSummary *summary = camel_folder_get_folder_summary (folder);
	GHashTable *expected;
	GPtrArray *uids;
	const gchar *uid;
	va_list ap;
	guint ii;

	expected = g_hash_table_new (g_str_hash, g_str_equal);

	va_start (ap, fldr);

	uid = va_arg (ap, const gchar *);
	while (uid) {
		g_hash_table_add (expected, (gpointer) uid);

		uid = va_arg (ap, const gchar *);
	}

	va_end (ap);

	uids = camel_folder_dup_uids (folder);
	if (!uids) {
		g_assert_cmpuint (g_hash_table_size (expected), ==, 0);
		g_hash_table_destroy (expected);
		return;
	}

	for (ii = 0; ii < uids->len; ii++) {
		CamelMessageInfo *mi;

		uid = g_ptr_array_index (uids, ii);
		g_assert_nonnull (uid);

		/* the vee-uid is a folder hash prefix followed by the original UID;
		   the test uses 2-letters long UID-s, thus compare only those last
		   two letters */
		g_assert_true (g_hash_table_remove (expected, test_vee_folder_last_chars (uid, 2)));

		mi = camel_folder_get_message_info (folder, uid);
		g_assert_nonnull (mi);
		g_assert_cmpstr (camel_message_info_get_uid (mi), ==, uid);
		g_clear_object (&mi);

		mi = camel_folder_summary_get (summary, uid);
		g_assert_nonnull (mi);
		g_assert_cmpstr (camel_message_info_get_uid (mi), ==, uid);
		g_clear_object (&mi);
	}

	/* all expected had been returned */
	g_assert_cmpint (g_hash_table_size (expected), ==, 0);

	g_hash_table_destroy (expected);
	g_ptr_array_unref (uids);
}

static gint
test_vee_folder_cmp_uids_array (gconstpointer ptr1,
				gconstpointer ptr2)
{
	const gchar *val1 = *((const gchar **) ptr1);
	const gchar *val2 = *((const gchar **) ptr2);

	return g_strcmp0 (test_vee_folder_last_chars (val1, 2), test_vee_folder_last_chars (val2, 2));
}

static void
test_vee_folder_check_uid_array (GPtrArray *returned_uids,
				 ...) G_GNUC_NULL_TERMINATED;

static void
test_vee_folder_check_uid_array (GPtrArray *returned_uids,
				 ...)
{
	GPtrArray *expected_uids; /* gchar * */
	const gchar *uid;
	va_list ap;
	guint ii;

	expected_uids = g_ptr_array_new ();
	va_start (ap, returned_uids);

	uid = va_arg (ap, const gchar *);
	while (uid) {
		g_ptr_array_add (expected_uids, (gpointer) uid);

		uid = va_arg (ap, const gchar *);
	}

	va_end (ap);

	g_assert_cmpuint (returned_uids->len, ==, expected_uids->len);

	g_ptr_array_sort (returned_uids, test_vee_folder_cmp_uids_array);
	g_ptr_array_sort (expected_uids, test_vee_folder_cmp_uids_array);

	for (ii = 0; ii < returned_uids->len; ii++) {
		const gchar *returned = g_ptr_array_index (returned_uids, ii);
		const gchar *expected = g_ptr_array_index (expected_uids, ii);

		g_assert_cmpstr (test_vee_folder_last_chars (returned, 2), ==, expected);
	}

	g_ptr_array_unref (expected_uids);
}

static void
test_vee_folder_create (void)
{
	CamelStore *store;
	CamelFolder *f1, *f2, *f3, *f4;
	CamelVeeStore *vee_store;

	test_vee_folder_create_folders (&store, &f1, &f2, &f3);
	g_assert_nonnull (store);
	g_assert_nonnull (f1);
	g_assert_nonnull (f2);
	g_assert_nonnull (f3);

	vee_store = test_vee_folder_create_vee_store ();
	g_assert_nonnull (vee_store);

	f4 = camel_vee_folder_new (CAMEL_STORE (vee_store), "vf", 0);
	g_assert_nonnull (f4);
	g_assert_true (CAMEL_IS_VEE_FOLDER (f4));

	test_session_wait_for_pending_jobs ();

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&f4);
	g_clear_object (&vee_store);
	g_clear_object (&store);

	test_session_check_finalized ();
}

static void
test_vee_folder_simple (void)
{
	CamelStore *store;
	CamelFolder *f1, *f2, *f3;
	CamelVeeFolder *vf;
	CamelVeeStore *vee_store;
	GError *local_error = NULL;
	gboolean success;

	test_vee_folder_create_folders (&store, &f1, &f2, &f3);
	g_assert_nonnull (store);
	g_assert_nonnull (f1);
	g_assert_nonnull (f2);
	g_assert_nonnull (f3);

	vee_store = test_vee_folder_create_vee_store ();
	g_assert_nonnull (vee_store);

	vf = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf", 0));
	g_assert_nonnull (vf);

	success = camel_vee_folder_add_folder_sync (vf, f1, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf, f2, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf, f3, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	test_session_wait_for_pending_jobs ();

	success = camel_vee_folder_set_expression_sync (vf, "(header-contains \"subject\" \"mess\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_check_uids (vf, "11", "21", "31", NULL);

	test_session_wait_for_pending_jobs ();

	success = camel_vee_folder_remove_folder_sync (vf, f2, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_check_uids (vf, "11", "31", NULL);

	test_session_wait_for_pending_jobs ();

	success = camel_vee_folder_add_folder_sync (vf, f2, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_check_uids (vf, "11", "21", "31", NULL);

	test_session_wait_for_pending_jobs ();

	success = camel_vee_folder_set_expression_sync (vf, "(header-contains \"subject\" \"bjec\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_check_uids (vf, "13", "22", "23", "31", NULL);

	test_session_wait_for_pending_jobs ();

	success = camel_vee_folder_remove_folder_sync (vf, f2, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_check_uids (vf, "13", "31", NULL);

	test_session_wait_for_pending_jobs ();

	success = camel_vee_folder_add_folder_sync (vf, f2, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_check_uids (vf, "13", "22", "23", "31", NULL);

	test_session_wait_for_pending_jobs ();

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&vf);
	g_clear_object (&vee_store);
	g_clear_object (&store);

	test_session_check_finalized ();
}

static void
test_vee_folder_nested (void)
{
	CamelStore *store;
	CamelFolder *f1, *f2, *f3;
	CamelVeeFolder *vf1, *vf2, *vf3;
	CamelVeeStore *vee_store;
	GPtrArray *uids = NULL;
	GError *local_error = NULL;
	gboolean success;

	test_vee_folder_create_folders (&store, &f1, &f2, &f3);
	g_assert_nonnull (store);
	g_assert_nonnull (f1);
	g_assert_nonnull (f2);
	g_assert_nonnull (f3);

	vee_store = test_vee_folder_create_vee_store ();
	g_assert_nonnull (vee_store);

	vf1 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf1", 0));
	g_assert_nonnull (vf1);
	success = camel_vee_folder_add_folder_sync (vf1, f1, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	vf2 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf2", 0));
	g_assert_nonnull (vf2);
	success = camel_vee_folder_add_folder_sync (vf2, f2, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	vf3 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf3", 0));
	g_assert_nonnull (vf3);
	success = camel_vee_folder_add_folder_sync (vf3, f3, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	success = camel_vee_folder_add_folder_sync (vf2, CAMEL_FOLDER (vf3), CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf1, CAMEL_FOLDER (vf2), CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	/* the folders are nested as this:
	   vf1
	      f1
	      vf2
	         f2
	         vf3
	            f3
	*/

	success = camel_vee_folder_set_expression_sync (vf3, "#t", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf2, "#t", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf1, "(header-contains \"subject\" \"mess\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, "11", "21", "31", NULL);
	test_vee_folder_check_uids (vf2, "21", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf3, "31", NULL);

	success = camel_vee_folder_remove_folder_sync (vf1, CAMEL_FOLDER (vf2), CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_wait_for_change (vf1, OP_REMOVE, "21");
	test_vee_folder_check_uids (vf1, "11", NULL);
	test_vee_folder_check_uids (vf2, "21", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf3, "31", NULL);

	success = camel_vee_folder_add_folder_sync (vf1, CAMEL_FOLDER (vf2), CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_wait_for_change (vf1, OP_ADD, "21");
	test_vee_folder_check_uids (vf1, "11", "21", "31", NULL);
	test_vee_folder_check_uids (vf2, "21", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf3, "31", NULL);

	success = camel_folder_search_sync (CAMEL_FOLDER (vf1), "#t", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpuint (uids->len, ==, 3);
	g_ptr_array_sort (uids, test_vee_folder_cmp_uids_array);
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[0], 2), ==, "11");
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[1], 2), ==, "21");
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[2], 2), ==, "31");
	g_ptr_array_unref (uids);

	success = camel_vee_folder_set_expression_sync (vf1, "(header-contains \"subject\" \"bjec\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, "13", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf2, "21", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf3, "31", NULL);

	success = camel_vee_folder_remove_folder_sync (vf2, CAMEL_FOLDER (vf3), CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_wait_for_change (vf1, OP_REMOVE, "31");
	test_vee_folder_check_uids (vf1, "13", "22", "23", NULL);
	test_vee_folder_check_uids (vf2, "21", "22", "23", NULL);
	test_vee_folder_check_uids (vf3, "31", NULL);

	success = camel_vee_folder_add_folder_sync (vf2, CAMEL_FOLDER (vf3), CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_wait_for_change (vf1, OP_ADD, "31");
	test_vee_folder_check_uids (vf1, "13", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf2, "21", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf3, "31", NULL);

	success = camel_vee_folder_set_expression_sync (vf2, "(header-contains \"subject\" \"mess\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_wait_for_change (vf1, OP_REMOVE, "22");
	test_vee_folder_check_uids (vf1, "13", "31", NULL);
	test_vee_folder_check_uids (vf2, "21", "31", NULL);
	test_vee_folder_check_uids (vf3, "31", NULL);

	success = camel_vee_folder_set_expression_sync (vf2, "(header-contains \"subject\" \"22\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_wait_for_change (vf1, OP_REMOVE, "31");
	test_vee_folder_check_uids (vf1, "13", "22", NULL);
	test_vee_folder_check_uids (vf2, "22", NULL);
	test_vee_folder_check_uids (vf3, "31", NULL);

	success = camel_folder_search_sync (CAMEL_FOLDER (vf1), "#t", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpuint (uids->len, ==, 2);
	g_ptr_array_sort (uids, test_vee_folder_cmp_uids_array);
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[0], 2), ==, "13");
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[1], 2), ==, "22");
	g_ptr_array_unref (uids);

	success = camel_folder_search_sync (CAMEL_FOLDER (vf1), "(header-contains \"subject\" \"13\")", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpuint (uids->len, ==, 1);
	g_ptr_array_sort (uids, test_vee_folder_cmp_uids_array);
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[0], 2), ==, "13");
	g_ptr_array_unref (uids);

	success = camel_folder_search_sync (CAMEL_FOLDER (vf1), "(header-contains \"subject\" \"31\")", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpuint (uids->len, ==, 0);
	g_ptr_array_unref (uids);

	success = camel_vee_folder_set_expression_sync (vf2, "#t", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_wait_for_change (vf1, OP_ADD, "31");
	test_vee_folder_check_uids (vf1, "13", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf2, "21", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf3, "31", NULL);

	success = camel_vee_folder_set_expression_sync (vf3, "(header-contains \"subject\" \"2\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_wait_for_change (vf1, OP_REMOVE, "31");
	test_vee_folder_check_uids (vf1, "13", "22", "23", NULL);
	test_vee_folder_check_uids (vf2, "21", "22", "23", NULL);
	test_vee_folder_check_uids (vf3, NULL);

	success = camel_vee_folder_add_folder_sync (vf2, f3, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	/* added f3 under vf2, thus the folders are nested as this:
	   vf1
	      f1
	      vf2
		 f2
		 f3
		 vf3
		    f3
	*/
	test_vee_folder_wait_for_change (vf1, OP_ADD, "31");
	test_vee_folder_check_uids (vf1, "13", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf2, "21", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf3, NULL);

	success = camel_folder_search_sync (CAMEL_FOLDER (vf1), "#t", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpuint (uids->len, ==, 4);
	g_ptr_array_sort (uids, test_vee_folder_cmp_uids_array);
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[0], 2), ==, "13");
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[1], 2), ==, "22");
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[2], 2), ==, "23");
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[3], 2), ==, "31");
	g_ptr_array_unref (uids);

	test_session_wait_for_pending_jobs ();

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&vf1);
	g_clear_object (&vf2);
	g_clear_object (&vf3);
	g_clear_object (&vee_store);
	g_clear_object (&store);

	test_session_check_finalized ();
}

static void
test_vee_folder_duplicates (void)
{
	CamelStore *store;
	CamelFolder *f1, *f2, *f3;
	CamelVeeFolder *vf1, *vf2, *vf3;
	CamelVeeStore *vee_store;
	GError *local_error = NULL;
	gboolean success;

	test_vee_folder_create_folders (&store, &f1, &f2, &f3);
	g_assert_nonnull (store);
	g_assert_nonnull (f1);
	g_assert_nonnull (f2);
	g_assert_nonnull (f3);

	vee_store = test_vee_folder_create_vee_store ();
	g_assert_nonnull (vee_store);

	vf2 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf2", 0));
	g_assert_nonnull (vf2);
	success = camel_vee_folder_add_folder_sync (vf2, f1, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf2, f2, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	vf3 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf3", 0));
	g_assert_nonnull (vf2);
	success = camel_vee_folder_add_folder_sync (vf3, f2, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf3, f3, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	vf1 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf1", 0));
	g_assert_nonnull (vf3);
	success = camel_vee_folder_add_folder_sync (vf1, CAMEL_FOLDER (vf2), CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf1, CAMEL_FOLDER (vf3), CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	/* the folders are nested as this:
	   vf1
	      vf2
	         f1
	         f2
	      vf3
		 f2
		 f3
	*/

	success = camel_vee_folder_set_expression_sync (vf2, "(header-contains \"subject\" \"2\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf3, "(header-contains \"subject\" \"1\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf1, "#t", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, "12", "21", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", "23", NULL);
	test_vee_folder_check_uids (vf3, "21", "31", NULL);

	success = camel_vee_folder_set_expression_sync (vf2, "(header-contains \"subject\" \"2\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf3, "(header-contains \"subject\" \"2\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf1, "(header-contains \"subject\" \"2\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, "12", "21", "22", "23", NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", "23", NULL);
	test_vee_folder_check_uids (vf3, "21", "22", "23", NULL);

	success = camel_vee_folder_set_expression_sync (vf2, "(header-contains \"subject\" \"2\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf3, "(header-contains \"subject\" \"2\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf1, "(header-contains \"subject\" \"1\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, "12", "21", NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", "23", NULL);
	test_vee_folder_check_uids (vf3, "21", "22", "23", NULL);

	success = camel_vee_folder_set_expression_sync (vf2, "(header-contains \"subject\" \"2\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf3, "(header-contains \"subject\" \"2\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf1, "(header-contains \"subject\" \"nothing\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", "23", NULL);
	test_vee_folder_check_uids (vf3, "21", "22", "23", NULL);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&vf1);
	g_clear_object (&vf2);
	g_clear_object (&vf3);
	g_clear_object (&vee_store);
	g_clear_object (&store);

	test_session_check_finalized ();
}

static void
test_vee_folder_changes (void)
{
	CamelStore *store;
	CamelFolder *f1, *f2, *f3;
	CamelVeeFolder *vf1, *vf2;
	CamelVeeStore *vee_store;
	CamelFolderChangeInfo *changes, *changes2 = NULL;
	CamelFolderSummary *v_summary, *f_summary;
	CamelMessageInfo *vmi, *mi;
	GError *local_error = NULL;
	gboolean success;
	gchar vuid[11];

	test_vee_folder_create_folders (&store, &f1, &f2, &f3);
	g_assert_nonnull (store);
	g_assert_nonnull (f1);
	g_assert_nonnull (f2);
	g_assert_nonnull (f3);

	vee_store = test_vee_folder_create_vee_store ();
	g_assert_nonnull (vee_store);

	vf2 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf2", 0));
	g_assert_nonnull (vf2);
	success = camel_vee_folder_add_folder_sync (vf2, f1, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf2, f2, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	vf1 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf1", 0));
	g_assert_nonnull (vf1);
	success = camel_vee_folder_add_folder_sync (vf1, CAMEL_FOLDER (vf2), CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf1, CAMEL_FOLDER (f3), CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	/* the folders are nested as this:
	   vf1
	      vf2
	         f1
	         f2
	      f3
	*/

	success = camel_vee_folder_set_expression_sync (vf2, "(header-contains \"subject\" \"2\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf1, "(not (system-flag \"seen\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	changes = test_vee_folder_wait_for_change_info (vf1);
	g_assert_nonnull (changes);
	g_assert_cmpuint (changes->uid_added->len, ==, 5);
	g_assert_cmpuint (changes->uid_changed->len, ==, 0);
	g_assert_cmpuint (changes->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes->uid_added, "12", "21", "22", "23", "31", NULL);
	g_clear_pointer (&changes, camel_folder_change_info_free);

	test_vee_folder_check_uids (vf1, "12", "21", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", "23", NULL);

	v_summary = camel_folder_get_folder_summary (CAMEL_FOLDER (vf1));
	f_summary = camel_folder_get_folder_summary (f2);

	test_vee_folder_fill_vuid (vuid, f2, "21");
	vmi = camel_folder_get_message_info (CAMEL_FOLDER (vf1), vuid);
	g_assert_nonnull (vmi);
	mi = camel_folder_get_message_info (f2, "21");
	g_assert_nonnull (mi);
	g_assert_cmpuint (camel_message_info_get_flags (mi) & CAMEL_MESSAGE_SEEN, ==, 0);
	g_assert_cmpuint (camel_message_info_get_flags (vmi) & CAMEL_MESSAGE_SEEN, ==, 0);
	g_assert_cmpuint (camel_folder_summary_get_info_flags (f_summary, camel_message_info_get_uid (mi)) & CAMEL_MESSAGE_SEEN, ==, 0);
	g_assert_cmpuint (camel_folder_summary_get_info_flags (v_summary, camel_message_info_get_uid (vmi)) & CAMEL_MESSAGE_SEEN, ==, 0);
	camel_message_info_set_flags (vmi, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
	g_assert_cmpuint (camel_message_info_get_flags (vmi) & CAMEL_MESSAGE_SEEN, !=, 0);
	g_assert_cmpuint (camel_message_info_get_flags (mi) & CAMEL_MESSAGE_SEEN, !=, 0);
	g_assert_cmpuint (camel_folder_summary_get_info_flags (f_summary, camel_message_info_get_uid (mi)) & CAMEL_MESSAGE_SEEN, !=, 0);
	g_assert_cmpuint (camel_folder_summary_get_info_flags (v_summary, camel_message_info_get_uid (vmi)) & CAMEL_MESSAGE_SEEN, !=, 0);
	test_vee_folder_check_uids (vf1, "12", "21", "22", "23", "31", NULL);

	test_vee_folder_wait_for_change_infos (f2, &changes, vf1, &changes2, NULL);

	g_assert_nonnull (changes);
	g_assert_cmpuint (changes->uid_added->len, ==, 0);
	g_assert_cmpuint (changes->uid_changed->len, ==, 1);
	g_assert_cmpuint (changes->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes->uid_changed, "21", NULL);
	g_clear_pointer (&changes, camel_folder_change_info_free);
	g_clear_object (&vmi);

	g_assert_nonnull (changes2);
	g_assert_cmpuint (changes2->uid_added->len, ==, 0);
	g_assert_cmpuint (changes2->uid_changed->len, ==, 1);
	g_assert_cmpuint (changes2->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes2->uid_changed, "21", NULL);
	g_clear_pointer (&changes2, camel_folder_change_info_free);
	g_clear_object (&vmi);

	test_vee_folder_check_uids (vf1, "12", "21", "22", "23", "31", NULL);

	/* rebuild vf1 when the changes are saved */
	success = camel_folder_summary_save (camel_folder_get_folder_summary (f2), &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	success = camel_folder_refresh_info_sync (CAMEL_FOLDER (vf1), NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_vee_folder_check_uids (vf1, "12", "22", "23", "31", NULL);

	changes = test_vee_folder_wait_for_change_info (vf1);
	g_assert_nonnull (changes);
	g_assert_cmpuint (changes->uid_added->len, ==, 0);
	g_assert_cmpuint (changes->uid_changed->len, ==, 0);
	g_assert_cmpuint (changes->uid_removed->len, ==, 1);
	test_vee_folder_check_uid_array (changes->uid_removed, "21", NULL);
	g_clear_pointer (&changes, camel_folder_change_info_free);

	camel_message_info_set_flags (mi, CAMEL_MESSAGE_SEEN, 0);
	g_assert_cmpuint (camel_message_info_get_flags (mi) & CAMEL_MESSAGE_SEEN, ==, 0);
	g_assert_cmpuint (camel_folder_summary_get_info_flags (f_summary, camel_message_info_get_uid (mi)) & CAMEL_MESSAGE_SEEN, ==, 0);

	/* rebuild vf1 when the changes are _not_ saved */
	success = camel_folder_refresh_info_sync (CAMEL_FOLDER (vf1), NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	changes = test_vee_folder_wait_for_change_info (vf1);
	g_assert_nonnull (changes);
	g_assert_cmpuint (changes->uid_added->len, ==, 1);
	g_assert_cmpuint (changes->uid_changed->len, ==, 0);
	g_assert_cmpuint (changes->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes->uid_added, "21", NULL);
	g_clear_pointer (&changes, camel_folder_change_info_free);

	vmi = camel_folder_get_message_info (CAMEL_FOLDER (vf1), vuid);
	camel_message_info_set_flags (mi, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
	g_assert_cmpuint (camel_message_info_get_flags (mi) & CAMEL_MESSAGE_SEEN, !=, 0);
	g_assert_cmpuint (camel_message_info_get_flags (vmi) & CAMEL_MESSAGE_SEEN, !=, 0);
	g_assert_cmpuint (camel_folder_summary_get_info_flags (f_summary, camel_message_info_get_uid (mi)) & CAMEL_MESSAGE_SEEN, !=, 0);
	/* this change is propagated on idle, not immediately */
	g_assert_cmpuint (camel_folder_summary_get_info_flags (v_summary, camel_message_info_get_uid (vmi)) & CAMEL_MESSAGE_SEEN, ==, 0);

	changes = test_vee_folder_wait_for_change_info (vf1);
	g_assert_nonnull (changes);
	g_assert_cmpuint (changes->uid_added->len, ==, 0);
	g_assert_cmpuint (changes->uid_changed->len, ==, 1);
	g_assert_cmpuint (changes->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes->uid_changed, "21", NULL);
	g_clear_pointer (&changes, camel_folder_change_info_free);
	g_assert_cmpuint (camel_folder_summary_get_info_flags (v_summary, camel_message_info_get_uid (vmi)) & CAMEL_MESSAGE_SEEN, !=, 0);

	g_clear_object (&mi);
	g_clear_object (&vmi);

	test_vee_folder_fill_vuid (vuid, f2, "22");
	vmi = camel_folder_summary_peek_loaded (v_summary, vuid);
	g_assert_nonnull (vmi);
	g_clear_object (&vmi);

	/* no rebuild on vf1 called, thus it still knows about the 21, which should not be in it anymore */
	test_vee_folder_check_uids (vf1, "12", "21", "22", "23", "31", NULL);

	/* clean up loaded message infos */
	success = camel_vee_folder_set_expression_sync (vf1, "#f", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	changes = test_vee_folder_wait_for_change_info (vf1);
	g_assert_nonnull (changes);
	g_assert_cmpuint (changes->uid_added->len, ==, 0);
	g_assert_cmpuint (changes->uid_changed->len, ==, 0);
	g_assert_cmpuint (changes->uid_removed->len, ==, 5);
	test_vee_folder_check_uid_array (changes->uid_removed, "12", "21", "22", "23", "31", NULL);
	g_clear_pointer (&changes, camel_folder_change_info_free);

	test_vee_folder_fill_vuid (vuid, f2, "22");
	g_assert_null (camel_folder_summary_peek_loaded (v_summary, vuid));
	success = camel_vee_folder_set_expression_sync (vf1, "(not (system-flag \"seen\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	/* loaded during rebuild, when added to the VeeSummary */
	vmi = camel_folder_summary_peek_loaded (v_summary, vuid);
	g_assert_nonnull (vmi);
	g_clear_object (&vmi);

	changes = test_vee_folder_wait_for_change_info (vf1);
	g_assert_nonnull (changes);
	g_assert_cmpuint (changes->uid_added->len, ==, 4);
	g_assert_cmpuint (changes->uid_changed->len, ==, 0);
	g_assert_cmpuint (changes->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes->uid_added, "12", "22", "23", "31", NULL);
	g_clear_pointer (&changes, camel_folder_change_info_free);

	mi = camel_folder_get_message_info (f1, "11");
	g_assert_nonnull (mi);
	g_assert_cmpuint (camel_message_info_get_flags (mi) & CAMEL_MESSAGE_DRAFT, ==, 0);
	camel_message_info_set_flags (mi, CAMEL_MESSAGE_DRAFT, CAMEL_MESSAGE_DRAFT);
	g_assert_cmpuint (camel_message_info_get_flags (mi) & CAMEL_MESSAGE_DRAFT, !=, 0);
	g_clear_object (&mi);

	/* not part of the vFolder */
	test_vee_folder_fill_vuid (vuid, f1, "11");
	g_assert_null (camel_folder_summary_peek_loaded (v_summary, vuid));

	mi = camel_folder_get_message_info (f2, "22");
	g_assert_nonnull (mi);
	g_assert_cmpuint (camel_message_info_get_flags (mi) & CAMEL_MESSAGE_JUNK, ==, 0);
	camel_message_info_set_flags (mi, CAMEL_MESSAGE_JUNK, CAMEL_MESSAGE_JUNK);
	g_assert_cmpuint (camel_message_info_get_flags (mi) & CAMEL_MESSAGE_JUNK, !=, 0);
	g_clear_object (&mi);

	test_vee_folder_fill_vuid (vuid, f2, "22");
	vmi = camel_folder_summary_peek_loaded (v_summary, vuid);
	g_assert_nonnull (vmi);
	g_clear_object (&vmi);

	changes = test_vee_folder_wait_for_change_info (vf1);
	g_assert_nonnull (changes);
	g_assert_cmpuint (changes->uid_added->len, ==, 0);
	g_assert_cmpuint (changes->uid_changed->len, ==, 1);
	g_assert_cmpuint (changes->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes->uid_changed, "22", NULL);
	g_clear_pointer (&changes, camel_folder_change_info_free);

	test_vee_folder_check_uids (vf1, "12", "22", "23", "31", NULL);

	/* let the 21 satisfy the expression again */
	mi = camel_folder_get_message_info (f2, "21");
	g_assert_nonnull (mi);
	g_assert_cmpuint (camel_message_info_get_flags (mi) & CAMEL_MESSAGE_SEEN, !=, 0);
	g_assert_cmpuint (camel_folder_summary_get_info_flags (f_summary, camel_message_info_get_uid (mi)) & CAMEL_MESSAGE_SEEN, !=, 0);
	camel_message_info_set_flags (mi, CAMEL_MESSAGE_SEEN, 0);
	g_assert_cmpuint (camel_message_info_get_flags (mi) & CAMEL_MESSAGE_SEEN, ==, 0);
	g_assert_cmpuint (camel_folder_summary_get_info_flags (f_summary, camel_message_info_get_uid (mi)) & CAMEL_MESSAGE_SEEN, ==, 0);
	g_clear_object (&mi);

	/* no rebuild, thus not in the folder yet */
	test_vee_folder_check_uids (vf1, "12", "22", "23", "31", NULL);
	/* but the rebuild should be scheduled under the hood */
	changes = test_vee_folder_wait_for_change_info (vf1);
	test_vee_folder_check_uids (vf1, "12", "21", "22", "23", "31", NULL);
	g_assert_nonnull (changes);
	g_assert_cmpuint (changes->uid_added->len, ==, 1);
	g_assert_cmpuint (changes->uid_changed->len, ==, 0);
	g_assert_cmpuint (changes->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes->uid_added, "21", NULL);
	g_clear_pointer (&changes, camel_folder_change_info_free);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&vf1);
	g_clear_object (&vf2);
	g_clear_object (&vee_store);
	g_clear_object (&store);

	test_session_check_finalized ();
}

static void
test_vee_folder_same_uids (void)
{
	CamelStore *store;
	CamelFolder *f1, *f2, *f3;
	CamelVeeFolder *vf1, *vf2, *vf3;
	CamelVeeStore *vee_store;
	GError *local_error = NULL;
	GPtrArray *uids;
	gchar vuid[11];
	gboolean success;

	test_vee_folder_create_folders (&store, &f1, &f2, &f3);
	g_assert_nonnull (store);
	g_assert_nonnull (f1);
	g_assert_nonnull (f2);
	g_assert_nonnull (f3);

	test_add_messages (f1,
		"uid", "00",
		"subject", "same f1",
		NULL);
	test_add_messages (f2,
		"uid", "00",
		"subject", "same f2",
		NULL);
	test_add_messages (f3,
		"uid", "00",
		"subject", "same f3",
		NULL);

	vee_store = test_vee_folder_create_vee_store ();
	g_assert_nonnull (vee_store);

	vf2 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf2", 0));
	g_assert_nonnull (vf2);
	success = camel_vee_folder_add_folder_sync (vf2, f1, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf2, f2, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	vf3 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf3", 0));
	g_assert_nonnull (vf2);
	success = camel_vee_folder_add_folder_sync (vf3, f3, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	vf1 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf1", 0));
	g_assert_nonnull (vf3);
	success = camel_vee_folder_add_folder_sync (vf1, CAMEL_FOLDER (vf2), CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf1, CAMEL_FOLDER (vf3), CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	/* the folders are nested as this:
	   vf1
	      vf2
	         f1
	         f2
	      vf3
		 f3
	*/

	success = camel_vee_folder_set_expression_sync (vf2, "(header-contains \"subject\" \"2\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf3, "(header-contains \"subject\" \"1\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf1, "#t", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, "12", "21", "22", "23", "31", "00", NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", "23", "00", NULL);
	test_vee_folder_check_uids (vf3, "31", NULL);

	success = camel_vee_folder_set_expression_sync (vf2, "(header-contains \"subject\" \"same\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf3, "#t", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_set_expression_sync (vf1, "(header-contains \"subject\" \"same\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf3, "31", "00", NULL);

	uids = camel_folder_dup_uids (CAMEL_FOLDER (vf2));
	g_assert_nonnull (uids);
	g_assert_cmpuint (uids->len, ==, 2);
	test_vee_folder_fill_vuid (vuid, f1, "00");
	g_assert_true (g_ptr_array_find_with_equal_func (uids, vuid, g_str_equal, NULL));
	test_vee_folder_fill_vuid (vuid, f2, "00");
	g_assert_true (g_ptr_array_find_with_equal_func (uids, vuid, g_str_equal, NULL));
	g_clear_pointer (&uids, g_ptr_array_unref);

	uids = camel_folder_dup_uids (CAMEL_FOLDER (vf1));
	g_assert_nonnull (uids);
	g_assert_cmpuint (uids->len, ==, 3);
	test_vee_folder_fill_vuid (vuid, f1, "00");
	g_assert_true (g_ptr_array_find_with_equal_func (uids, vuid, g_str_equal, NULL));
	test_vee_folder_fill_vuid (vuid, f2, "00");
	g_assert_true (g_ptr_array_find_with_equal_func (uids, vuid, g_str_equal, NULL));
	test_vee_folder_fill_vuid (vuid, f3, "00");
	g_assert_true (g_ptr_array_find_with_equal_func (uids, vuid, g_str_equal, NULL));
	g_clear_pointer (&uids, g_ptr_array_unref);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&vf1);
	g_clear_object (&vf2);
	g_clear_object (&vf3);
	g_clear_object (&vee_store);
	g_clear_object (&store);

	test_session_check_finalized ();
}

static void
test_vee_folder_match_threads (gconstpointer user_data)
{
	CamelSession *session;
	CamelStore *store1, *store2;
	CamelFolder *f1, *f2, *f3;
	CamelVeeStore *vee_store;
	CamelVeeFolder *vf;
	GError *local_error = NULL;
	gboolean success;

	session = test_session_new ();
	store1 = test_store_new_full (session, "store1", "Test Store 1");
	if (GPOINTER_TO_UINT (user_data) == 1) {
		store2 = g_object_ref (store1);
	} else {
		store2 = test_store_new_full (session, "store2", "Test Store 2");
	}

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

	vee_store = test_vee_folder_create_vee_store ();
	g_assert_nonnull (vee_store);

	vf = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf", 0));
	g_assert_nonnull (vf);
	success = camel_vee_folder_add_folder_sync (vf, f1, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf, f2, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf, f3, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	success = camel_vee_folder_set_expression_sync (vf, "(header-contains \"subject\" \"root\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf, "11", "21", "31", NULL);

	success = camel_vee_folder_set_expression_sync (vf, "(match-threads \"single\" (header-contains \"subject\" \"root\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf, "11", "31", NULL);

	success = camel_vee_folder_set_expression_sync (vf, "(match-threads \"all\" (header-contains \"subject\" \"root\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf, "11", "12", "14", "13", "15", "21", "22", "31", NULL);

	success = camel_vee_folder_set_expression_sync (vf, "(match-threads \"all\" (or (header-contains \"subject\" \"from 12\") (uid \"33\")))",
		CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf, "12", "14", "13", "15", "21", "22", "23", "24", "32", "33", NULL);

	success = camel_vee_folder_set_expression_sync (vf, "(match-threads \"no-subject,all\" (or (header-contains \"subject\" \"from 12\") (uid \"33\")))",
		CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf, "12", "14", "13", "15", "21", "22", "23", "32", "33", NULL);

	success = camel_vee_folder_set_expression_sync (vf, "(match-threads \"replies\" (uid \"13\" \"33\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf, "13", "22", "32", "33", NULL);

	success = camel_vee_folder_set_expression_sync (vf, "(match-threads \"no-subject,replies\" (uid \"13\" \"33\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf, "13", "22", "32", "33", NULL);

	success = camel_vee_folder_set_expression_sync (vf, "(match-threads \"replies_parents\" (uid \"13\" \"33\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf, "12", "13", "21", "22", "23", "32", "33", NULL);

	success = camel_vee_folder_set_expression_sync (vf, "(match-threads \"no-subject,replies_parents\" (uid \"13\" \"33\"))",
		CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf, "12", "13", "21", "22", "23", "32", "33", NULL);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&store1);
	g_clear_object (&store2);
	g_clear_object (&vf);
	g_clear_object (&vee_store);
	g_clear_object (&session);

	test_session_check_finalized ();
}

static void
test_vee_folder_match_threads_nested (gconstpointer user_data)
{
	CamelSession *session;
	CamelStore *store1, *store2;
	CamelFolder *f1, *f2, *f3;
	CamelVeeStore *vee_store;
	CamelVeeFolder *vf1, *vf2;
	GPtrArray *uids = NULL;
	GError *local_error = NULL;
	gboolean success;

	session = test_session_new ();
	store1 = test_store_new_full (session, "store1", "Test Store 1");
	if (GPOINTER_TO_UINT (user_data) == 1) {
		store2 = g_object_ref (store1);
	} else {
		store2 = test_store_new_full (session, "store2", "Test Store 2");
	}

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

	vee_store = test_vee_folder_create_vee_store ();
	g_assert_nonnull (vee_store);

	vf1 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf1", 0));
	g_assert_nonnull (vf1);
	vf2 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf2", 0));
	g_assert_nonnull (vf2);
	success = camel_vee_folder_add_folder_sync (vf1, f1, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf2, f2, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf2, f3, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf1, CAMEL_FOLDER (vf2), CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	test_session_wait_for_pending_jobs ();

	success = camel_vee_folder_set_expression_sync (vf2, "(match-threads \"all\" (header-contains \"subject\" \"33\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf2, "23", "24", "32", "33", NULL);

	success = camel_vee_folder_set_expression_sync (vf1, "(header-contains \"subject\" \"11\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, NULL);

	success = camel_vee_folder_set_expression_sync (vf1, "(header-contains \"subject\" \"32\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, "32", NULL);

	success = camel_vee_folder_set_expression_sync (vf1, "(uid \"23\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, "23", NULL);

	success = camel_vee_folder_set_expression_sync (vf2, "(match-threads \"all\" (uid \"12\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf2, NULL);

	success = camel_vee_folder_set_expression_sync (vf1, "(match-threads \"all\" (uid \"12\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, "12", "13", "14", "15", "21", "22", NULL);

	success = camel_vee_folder_set_expression_sync (vf1, "(match-threads \"replies_parents\" (uid \"13\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, "12", "13", "21", "22", NULL);

	success = camel_vee_folder_set_expression_sync (vf2, "(uid \"33\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf2, "33", NULL);

	success = camel_vee_folder_set_expression_sync (vf1, "(match-threads \"all\" (uid \"12\" \"33\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, "12", "13", "14", "15", "21", "22", "23", "24", "32", "33", NULL);

	success = camel_vee_folder_set_expression_sync (vf1, "(match-threads \"all\" (uid \"12\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, "12", "13", "14", "15", "21", "22", NULL);

	success = camel_vee_folder_set_expression_sync (vf1, "(match-threads \"replies_parents\" (uid \"33\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	test_session_wait_for_pending_jobs ();
	test_vee_folder_check_uids (vf1, "23", "32", "33", NULL);

	success = camel_folder_search_sync (CAMEL_FOLDER (vf1), "(header-contains \"subject\" \"32\")", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpuint (uids->len, ==, 1);
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[0], 2), ==, "32");
	g_clear_pointer (&uids, g_ptr_array_unref);

	success = camel_folder_search_sync (CAMEL_FOLDER (vf1), "(match-threads \"replies_parents\" (header-contains \"subject\" \"32\"))", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpuint (uids->len, ==, 3);
	g_ptr_array_sort (uids, test_vee_folder_cmp_uids_array);
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[0], 2), ==, "23");
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[1], 2), ==, "32");
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[2], 2), ==, "33");
	g_clear_pointer (&uids, g_ptr_array_unref);

	test_session_wait_for_pending_jobs ();

	success = camel_folder_search_sync (CAMEL_FOLDER (vf1), "(match-threads \"all\" (header-contains \"subject\" \"32\"))", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpuint (uids->len, ==, 4);
	g_ptr_array_sort (uids, test_vee_folder_cmp_uids_array);
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[0], 2), ==, "23");
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[1], 2), ==, "24");
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[2], 2), ==, "32");
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[3], 2), ==, "33");
	g_clear_pointer (&uids, g_ptr_array_unref);

	success = camel_folder_search_sync (CAMEL_FOLDER (vf1), "(uid \"23\")", &uids, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpuint (uids->len, ==, 1);
	g_ptr_array_sort (uids, test_vee_folder_cmp_uids_array);
	g_assert_cmpstr (test_vee_folder_last_chars (uids->pdata[0], 2), ==, "23");
	g_clear_pointer (&uids, g_ptr_array_unref);

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&store1);
	g_clear_object (&store2);
	g_clear_object (&vf1);
	g_clear_object (&vf2);
	g_clear_object (&vee_store);
	g_clear_object (&session);

	test_session_check_finalized ();
}

#ifdef ENABLE_MAINTAINER_MODE
static void
test_vee_folder_skip_notification (gconstpointer user_data)
{
	gboolean use_auto_update = GPOINTER_TO_UINT (user_data) == 1;
	CamelStore *store;
	CamelFolder *f1, *f2, *f3;
	CamelVeeFolder *vf1, *vf2;
	CamelVeeStore *vee_store;
	CamelFolderChangeInfo *changes1 = NULL, *changes2 = NULL, *changes3 = NULL;
	CamelMessageInfo *vmi, *mi;
	GError *local_error = NULL;
	gint vf1_n_schedule_rebuilds = 0, vf1_n_run_rebuilds = 0;
	gint vf2_n_schedule_rebuilds = 0, vf2_n_run_rebuilds = 0;
	gboolean success;
	gchar vuid[11];

	#define reset_rebuild_counts() \
		vf1_n_schedule_rebuilds = 0; \
		vf1_n_run_rebuilds = 0; \
		vf2_n_schedule_rebuilds = 0; \
		vf2_n_run_rebuilds = 0;

	test_vee_folder_create_folders (&store, &f1, &f2, &f3);
	g_assert_nonnull (store);
	g_assert_nonnull (f1);
	g_assert_nonnull (f2);
	g_assert_nonnull (f3);

	vee_store = test_vee_folder_create_vee_store ();
	g_assert_nonnull (vee_store);

	vf2 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf2", 0));
	g_assert_nonnull (vf2);
	camel_vee_folder_set_auto_update (vf2, use_auto_update);
	g_signal_connect_swapped (vf2, "rebuild-schedule-test-signal", G_CALLBACK (g_atomic_int_inc), &vf2_n_schedule_rebuilds);
	g_signal_connect_swapped (vf2, "rebuild-run-test-signal", G_CALLBACK (g_atomic_int_inc), &vf2_n_run_rebuilds);
	success = camel_vee_folder_add_folder_sync (vf2, f1, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf2, f2, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	vf1 = CAMEL_VEE_FOLDER (camel_vee_folder_new (CAMEL_STORE (vee_store), "vf1", 0));
	g_assert_nonnull (vf1);
	camel_vee_folder_set_auto_update (vf1, use_auto_update);
	g_signal_connect_swapped (vf1, "rebuild-schedule-test-signal", G_CALLBACK (g_atomic_int_inc), &vf1_n_schedule_rebuilds);
	g_signal_connect_swapped (vf1, "rebuild-run-test-signal", G_CALLBACK (g_atomic_int_inc), &vf1_n_run_rebuilds);
	success = camel_vee_folder_add_folder_sync (vf1, CAMEL_FOLDER (vf2), CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	success = camel_vee_folder_add_folder_sync (vf1, CAMEL_FOLDER (f3), CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);

	/* the folders are nested as this:
	   vf1
	      vf2
	         f1
	         f2
	      f3
	*/

	success = camel_vee_folder_set_expression_sync (vf2, "(header-contains \"subject\" \"2\")", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 1);
	success = camel_vee_folder_set_expression_sync (vf1, "#t", CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	success = camel_vee_folder_set_expression_sync (vf1, "(not (system-flag \"seen\"))", CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 1);

	changes1 = test_vee_folder_wait_for_change_info (vf1);
	g_assert_nonnull (changes1);
	g_assert_cmpuint (changes1->uid_added->len, ==, 5);
	g_assert_cmpuint (changes1->uid_changed->len, ==, 0);
	g_assert_cmpuint (changes1->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes1->uid_added, "12", "21", "22", "23", "31", NULL);
	g_clear_pointer (&changes1, camel_folder_change_info_free);

	test_vee_folder_check_uids (vf1, "12", "21", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", "23", NULL);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 1);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 1);

	reset_rebuild_counts ();

	test_vee_folder_fill_vuid (vuid, f1, "12");
	vmi = camel_folder_get_message_info (CAMEL_FOLDER (vf1), vuid);
	g_assert_nonnull (vmi);
	mi = camel_folder_get_message_info (f1, "12");
	g_assert_nonnull (mi);
	g_assert_cmpuint (camel_message_info_get_flags (mi) & CAMEL_MESSAGE_SEEN, ==, 0);
	g_assert_cmpuint (camel_message_info_get_flags (vmi) & CAMEL_MESSAGE_SEEN, ==, 0);
	camel_message_info_set_flags (vmi, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
	g_assert_cmpuint (camel_message_info_get_flags (vmi) & CAMEL_MESSAGE_SEEN, !=, 0);
	g_assert_cmpuint (camel_message_info_get_flags (mi) & CAMEL_MESSAGE_SEEN, !=, 0);
	test_vee_folder_check_uids (vf1, "12", "21", "22", "23", "31", NULL);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	changes1 = test_vee_folder_wait_for_change_info (vf1);
	g_assert_nonnull (changes1);
	g_assert_cmpuint (changes1->uid_added->len, ==, 0);
	g_assert_cmpuint (changes1->uid_changed->len, ==, 1);
	g_assert_cmpuint (changes1->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes1->uid_changed, "12", NULL);
	g_clear_pointer (&changes1, camel_folder_change_info_free);
	test_vee_folder_check_uids (vf1, "12", "21", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", "23", NULL);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	/* this changes both vf2 and vf1, but without a rebuild need */
	camel_message_info_set_flags (mi, CAMEL_MESSAGE_DRAFT, CAMEL_MESSAGE_DRAFT);

	test_vee_folder_wait_for_change_infos (f1, &changes1, vf1, &changes2, NULL);

	g_assert_nonnull (changes1);
	g_assert_cmpuint (changes1->uid_added->len, ==, 0);
	g_assert_cmpuint (changes1->uid_changed->len, ==, 1);
	g_assert_cmpuint (changes1->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes1->uid_changed, "12", NULL);
	g_clear_pointer (&changes1, camel_folder_change_info_free);

	g_assert_nonnull (changes2);
	g_assert_cmpuint (changes2->uid_added->len, ==, 0);
	g_assert_cmpuint (changes2->uid_changed->len, ==, 1);
	g_assert_cmpuint (changes2->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes2->uid_changed, "12", NULL);
	g_clear_pointer (&changes2, camel_folder_change_info_free);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	success = camel_folder_refresh_info_sync (CAMEL_FOLDER (vf1), NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	test_vee_folder_wait_for_change_infos (vf1, &changes1, NULL);

	g_assert_nonnull (changes1);
	g_assert_cmpuint (changes1->uid_added->len, ==, 0);
	g_assert_cmpuint (changes1->uid_changed->len, ==, 0);
	g_assert_cmpuint (changes1->uid_removed->len, ==, 1);
	test_vee_folder_check_uid_array (changes1->uid_removed, "12", NULL);
	g_clear_pointer (&changes1, camel_folder_change_info_free);

	test_vee_folder_check_uids (vf1, "21", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", "23", NULL);

	reset_rebuild_counts ();

	/* the 12 is not part of the vf1, but its change may or may not make it part of the folder,
	   thus it invokes rebuild of the vf1 under the hood */
	camel_message_info_set_flags (mi, CAMEL_MESSAGE_JUNK, CAMEL_MESSAGE_JUNK);

	test_vee_folder_wait_for_change_infos (f1, &changes1, vf2, &changes2, NULL);

	g_assert_nonnull (changes1);
	g_assert_cmpuint (changes1->uid_added->len, ==, 0);
	g_assert_cmpuint (changes1->uid_changed->len, ==, 1);
	g_assert_cmpuint (changes1->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes1->uid_changed, "12", NULL);
	g_clear_pointer (&changes1, camel_folder_change_info_free);

	g_assert_nonnull (changes2);
	g_assert_cmpuint (changes2->uid_added->len, ==, 0);
	g_assert_cmpuint (changes2->uid_changed->len, ==, 1);
	g_assert_cmpuint (changes2->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes2->uid_changed, "12", NULL);
	g_clear_pointer (&changes2, camel_folder_change_info_free);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, use_auto_update ? 1 : 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	test_session_wait_for_pending_jobs ();

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, use_auto_update ? 1 : 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, use_auto_update ? 1 : 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	g_clear_object (&vmi);
	g_clear_object (&mi);

	test_vee_folder_check_uids (vf1, "21", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", "23", NULL);

	reset_rebuild_counts ();

	/* delete message 23 from the f2 */
	mi = camel_folder_get_message_info (f2, "23");
	g_assert_nonnull (mi);
	camel_message_info_set_flags (mi, CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);
	g_clear_object (&mi);

	changes1 = test_vee_folder_wait_for_change_info (vf1);
	g_assert_nonnull (changes1);
	g_assert_cmpuint (changes1->uid_added->len, ==, 0);
	g_assert_cmpuint (changes1->uid_changed->len, ==, 1);
	g_assert_cmpuint (changes1->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes1->uid_changed, "23", NULL);
	g_clear_pointer (&changes1, camel_folder_change_info_free);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	success = camel_folder_synchronize_sync (f2, TRUE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	test_vee_folder_check_uids (vf1, "21", "22", "23", "31", NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", "23", NULL);

	test_vee_folder_wait_for_change_infos (f2, &changes1, vf2, &changes2, vf1, &changes3, NULL);

	g_assert_nonnull (changes1);
	g_assert_cmpuint (changes1->uid_added->len, ==, 0);
	g_assert_cmpuint (changes1->uid_changed->len, ==, 0);
	g_assert_cmpuint (changes1->uid_removed->len, ==, 1);
	test_vee_folder_check_uid_array (changes1->uid_removed, "23", NULL);
	g_clear_pointer (&changes1, camel_folder_change_info_free);

	g_assert_nonnull (changes2);
	g_assert_cmpuint (changes2->uid_added->len, ==, 0);
	g_assert_cmpuint (changes2->uid_changed->len, ==, 0);
	g_assert_cmpuint (changes2->uid_removed->len, ==, 1);
	test_vee_folder_check_uid_array (changes2->uid_removed, "23", NULL);
	g_clear_pointer (&changes2, camel_folder_change_info_free);

	g_assert_nonnull (changes3);
	g_assert_cmpuint (changes3->uid_added->len, ==, 0);
	g_assert_cmpuint (changes3->uid_changed->len, ==, 0);
	g_assert_cmpuint (changes3->uid_removed->len, ==, 1);
	test_vee_folder_check_uid_array (changes3->uid_removed, "23", NULL);
	g_clear_pointer (&changes3, camel_folder_change_info_free);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	test_vee_folder_check_uids (vf1, "21", "22", "31", NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", NULL);

	/* add a new message => maybe rebuild needed */
	test_add_messages (f2,
		"uid", "29",
		"subject", "Message 29",
		NULL);
	changes1 = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes1, "29");
	camel_folder_changed (f2, changes1);
	g_clear_pointer (&changes1, camel_folder_change_info_free);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	if (use_auto_update) {
		test_vee_folder_wait_for_change_infos (f2, &changes1, vf2, &changes2, vf1, &changes3, NULL);

		g_assert_nonnull (changes2);
		g_assert_cmpuint (changes2->uid_added->len, ==, 1);
		g_assert_cmpuint (changes2->uid_changed->len, ==, 0);
		g_assert_cmpuint (changes2->uid_removed->len, ==, 0);
		test_vee_folder_check_uid_array (changes2->uid_added, "29", NULL);
		g_clear_pointer (&changes2, camel_folder_change_info_free);

		g_assert_nonnull (changes3);
		g_assert_cmpuint (changes3->uid_added->len, ==, 1);
		g_assert_cmpuint (changes3->uid_changed->len, ==, 0);
		g_assert_cmpuint (changes3->uid_removed->len, ==, 0);
		test_vee_folder_check_uid_array (changes3->uid_added, "29", NULL);
		g_clear_pointer (&changes3, camel_folder_change_info_free);
	} else {
		test_vee_folder_wait_for_change_infos (f2, &changes1, NULL);
	}

	g_assert_nonnull (changes1);
	g_assert_cmpuint (changes1->uid_added->len, ==, 1);
	g_assert_cmpuint (changes1->uid_changed->len, ==, 0);
	g_assert_cmpuint (changes1->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes1->uid_added, "29", NULL);
	g_clear_pointer (&changes1, camel_folder_change_info_free);

	if (use_auto_update) {
		g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 1);
		g_assert_cmpint (vf1_n_run_rebuilds, ==, 1);
		g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 1);
		g_assert_cmpint (vf2_n_run_rebuilds, ==, 1);
	} else {
		test_vee_folder_check_uids (vf1, "21", "22", "31", NULL);
		test_vee_folder_check_uids (vf2, "12", "21", "22", NULL);

		g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
		g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
		g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
		g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

		success = camel_folder_refresh_info_sync (CAMEL_FOLDER (vf2), NULL, &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);

		changes1 = test_vee_folder_wait_for_change_info (vf2);
		g_assert_nonnull (changes1);
		g_assert_cmpuint (changes1->uid_added->len, ==, 1);
		g_assert_cmpuint (changes1->uid_changed->len, ==, 0);
		g_assert_cmpuint (changes1->uid_removed->len, ==, 0);
		test_vee_folder_check_uid_array (changes1->uid_added, "29", NULL);
		g_clear_pointer (&changes1, camel_folder_change_info_free);

		g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
		g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
		g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
		g_assert_cmpint (vf2_n_run_rebuilds, ==, 1);

		test_vee_folder_check_uids (vf1, "21", "22", "31", NULL);
		test_vee_folder_check_uids (vf2, "12", "21", "22", "29", NULL);

		success = camel_folder_refresh_info_sync (CAMEL_FOLDER (vf1), NULL, &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);

		changes1 = test_vee_folder_wait_for_change_info (vf1);
		g_assert_nonnull (changes1);
		g_assert_cmpuint (changes1->uid_added->len, ==, 1);
		g_assert_cmpuint (changes1->uid_changed->len, ==, 0);
		g_assert_cmpuint (changes1->uid_removed->len, ==, 0);
		test_vee_folder_check_uid_array (changes1->uid_added, "29", NULL);
		g_clear_pointer (&changes1, camel_folder_change_info_free);

		g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
		g_assert_cmpint (vf1_n_run_rebuilds, ==, 1);
		g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
		g_assert_cmpint (vf2_n_run_rebuilds, ==, 1);
	}

	test_vee_folder_check_uids (vf1, "21", "22", "29", "31", NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", "29", NULL);

	reset_rebuild_counts ();

	/* the "11" is not part of the vf2 nor vf1 */
	test_vee_folder_fill_vuid (vuid, f1, "11");
	vmi = camel_folder_get_message_info (CAMEL_FOLDER (vf1), vuid);
	g_assert_null (vmi);
	vmi = camel_folder_get_message_info (CAMEL_FOLDER (vf2), vuid);
	g_assert_null (vmi);
	mi = camel_folder_get_message_info (f1, "11");
	g_assert_nonnull (mi);
	g_assert_cmpuint (camel_message_info_get_flags (mi) & CAMEL_MESSAGE_SEEN, ==, 0);
	camel_message_info_set_flags (mi, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	camel_message_info_set_flags (mi, CAMEL_MESSAGE_SEEN, 0);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	camel_message_info_set_flags (mi, CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);
	g_clear_object (&mi);

	success = camel_folder_synchronize_sync (f1, TRUE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	test_vee_folder_check_uids (vf1, "21", "22", "29", "31", NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", "29", NULL);

	/* the "12" is part of the vf2 only */
	test_vee_folder_fill_vuid (vuid, f1, "12");
	vmi = camel_folder_get_message_info (CAMEL_FOLDER (vf2), vuid);
	g_assert_nonnull (vmi);
	mi = camel_folder_get_message_info (f1, "12");
	g_assert_nonnull (mi);
	g_assert_cmpuint (camel_message_info_get_flags (vmi) & CAMEL_MESSAGE_SEEN, !=, 0);
	camel_message_info_set_flags (vmi, CAMEL_MESSAGE_SEEN, 0);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	camel_message_info_set_flags (mi, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	changes1 = test_vee_folder_wait_for_change_info (vf2);
	g_assert_nonnull (changes1);
	g_assert_cmpuint (changes1->uid_added->len, ==, 0);
	g_assert_cmpuint (changes1->uid_changed->len, ==, 1);
	g_assert_cmpuint (changes1->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes1->uid_changed, "12", NULL);
	g_clear_pointer (&changes1, camel_folder_change_info_free);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, use_auto_update ? 1 : 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, use_auto_update ? 1 : 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	g_clear_object (&mi);
	g_clear_object (&vmi);

	test_vee_folder_check_uids (vf1, "21", "22", "29", "31", NULL);
	test_vee_folder_check_uids (vf2, "12", "21", "22", "29", NULL);

	reset_rebuild_counts ();

	/* the "31" is part of the vf1 only */
	test_vee_folder_fill_vuid (vuid, f3, "31");
	vmi = camel_folder_get_message_info (CAMEL_FOLDER (vf1), vuid);
	g_assert_nonnull (vmi);
	mi = camel_folder_get_message_info (f1, "12");
	g_assert_nonnull (mi);
	g_assert_cmpuint (camel_message_info_get_flags (vmi) & CAMEL_MESSAGE_SEEN, ==, 0);
	camel_message_info_set_flags (vmi, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	camel_message_info_set_flags (mi, CAMEL_MESSAGE_SEEN, 0);

	test_vee_folder_wait_for_change_infos (f3, &changes3, vf1, &changes1, NULL);

	g_assert_nonnull (changes3);
	g_assert_cmpuint (changes3->uid_added->len, ==, 0);
	g_assert_cmpuint (changes3->uid_changed->len, ==, 1);
	g_assert_cmpuint (changes3->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes3->uid_changed, "31", NULL);
	g_clear_pointer (&changes3, camel_folder_change_info_free);

	g_assert_nonnull (changes1);
	g_assert_cmpuint (changes1->uid_added->len, ==, 0);
	g_assert_cmpuint (changes1->uid_changed->len, ==, 1);
	g_assert_cmpuint (changes1->uid_removed->len, ==, 0);
	test_vee_folder_check_uid_array (changes1->uid_changed, "31", NULL);
	g_clear_pointer (&changes1, camel_folder_change_info_free);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, use_auto_update ? 1 : 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	g_clear_object (&mi);
	g_clear_object (&vmi);

	if (!use_auto_update) {
		success = camel_folder_refresh_info_sync (CAMEL_FOLDER (vf1), NULL, &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);
	}

	changes1 = test_vee_folder_wait_for_change_info (vf1);
	g_assert_nonnull (changes1);
	g_assert_cmpuint (changes1->uid_added->len, ==, 1);
	g_assert_cmpuint (changes1->uid_changed->len, ==, 0);
	g_assert_cmpuint (changes1->uid_removed->len, ==, 1);
	test_vee_folder_check_uid_array (changes1->uid_added, "12", NULL);
	test_vee_folder_check_uid_array (changes1->uid_removed, "31", NULL);
	g_clear_pointer (&changes1, camel_folder_change_info_free);

	g_assert_cmpint (vf1_n_schedule_rebuilds, ==, use_auto_update ? 1 : 0);
	g_assert_cmpint (vf1_n_run_rebuilds, ==, 1);
	g_assert_cmpint (vf2_n_schedule_rebuilds, ==, 0);
	g_assert_cmpint (vf2_n_run_rebuilds, ==, 0);

	#undef reset_rebuild_counts

	g_clear_object (&f1);
	g_clear_object (&f2);
	g_clear_object (&f3);
	g_clear_object (&vf1);
	g_clear_object (&vf2);
	g_clear_object (&vee_store);
	g_clear_object (&store);

	test_session_check_finalized ();
}
#endif /* ENABLE_MAINTAINER_MODE */

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/-/issues/");

	g_test_add_func ("/CamelVeeFolder/Create", test_vee_folder_create);
	g_test_add_func ("/CamelVeeFolder/Simple", test_vee_folder_simple);
	g_test_add_func ("/CamelVeeFolder/Nested", test_vee_folder_nested);
	g_test_add_func ("/CamelVeeFolder/Duplicates", test_vee_folder_duplicates);
	g_test_add_func ("/CamelVeeFolder/Changes", test_vee_folder_changes);
	g_test_add_func ("/CamelVeeFolder/SameUIDs", test_vee_folder_same_uids);
	g_test_add_data_func ("/CamelVeeFolder/MatchThreadsOneStore", GUINT_TO_POINTER (1), test_vee_folder_match_threads);
	g_test_add_data_func ("/CamelVeeFolder/MatchThreadsMultipleStores", GUINT_TO_POINTER (2), test_vee_folder_match_threads);
	g_test_add_data_func ("/CamelVeeFolder/MatchThreadsNestedOneStore", GUINT_TO_POINTER (1), test_vee_folder_match_threads_nested);
	g_test_add_data_func ("/CamelVeeFolder/MatchThreadsNestedMultipleStores", GUINT_TO_POINTER (2), test_vee_folder_match_threads_nested);
	#ifdef ENABLE_MAINTAINER_MODE
	g_test_add_data_func ("/CamelVeeFolder/SkipNotificationAutoUpdate", GUINT_TO_POINTER (1), test_vee_folder_skip_notification);
	g_test_add_data_func ("/CamelVeeFolder/SkipNotificationWithoutAutoUpdate", GUINT_TO_POINTER (0), test_vee_folder_skip_notification);
	#endif

	return g_test_run ();
}

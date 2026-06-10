/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * test-folder-search.c -- folder search with indexing
 */

#include <string.h>

#include "camel-test.h"
#include "camel-test-provider.h"
#include "messages.h"
#include "folders.h"
#include "session.h"

static void
test_folder_search_sub (CamelFolder *folder,
                        const gchar *expr,
                        gint expected)
{
	GPtrArray *uids = NULL;
	GHashTable *hash;
	gint i;
	GError *error = NULL;
	gboolean success;

	success = camel_folder_search_sync (folder, expr, &uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_cmpuint (uids->len, ==, expected);

	/* check the uid's are actually unique, too */
	hash = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < uids->len; i++) {
		g_assert_null (g_hash_table_lookup (hash, uids->pdata[i]));
		g_hash_table_insert (hash, uids->pdata[i], uids->pdata[i]);
	}
	g_hash_table_destroy (hash);
	g_ptr_array_unref (uids);
}

static void
test_folder_search (CamelFolder *folder,
                    const gchar *expr,
                    gint expected)
{
	gchar *matchall;

	matchall = g_strdup_printf ("(match-all %s)", expr);
	test_folder_search_sub (folder, matchall, expected);
	g_free (matchall);
}

static struct {
	gint counts[3];
	const gchar *expr;
} searches[] = {
	{ { 1, 1, 0 }, "(header-matches \"subject\" \"Test1 message99 subject\")" },

	{ { 100, 50, 0 }, "(header-contains \"subject\" \"subject\")" },
	{ { 100, 50, 0 }, "(header-contains \"subject\" \"Subject\")" },

	{ { 100, 50, 0 }, "(body-contains \"content\")" },
	{ { 100, 50, 0 }, "(body-contains \"Content\")" },

	{ { 0, 0, 0 }, "(user-flag \"every7\")" },
	{ { 100 / 13 + 1, 50 / 13 + 1, 0 }, "(user-flag \"every13\")" },
	{ { 1, 1, 0 }, "(= \"7tag1\" (user-tag \"every7\"))" },
	{ { 100 / 11 + 1, 50 / 11 + 1, 0 }, "(= \"11tag\" (user-tag \"every11\"))" },

	{ { 100 / 13 + 100 / 17 + 1, 50 / 13 + 50 / 17 + 2, 0 }, "(user-flag \"every13\" \"every17\")" },
	{ { 100 / 13 + 100 / 17 + 1, 50 / 13 + 50 / 17 + 2, 0 }, "(or (user-flag \"every13\") (user-flag \"every17\"))" },
	{ { 1, 0, 0 }, "(and (user-flag \"every13\") (user-flag \"every17\"))" },

	{ { 0, 0, 0 }, "(and (header-contains \"subject\" \"Test1\") (header-contains \"subject\" \"Test2\"))" },
	/* we get 11 here as the header-contains is a substring match */
	{ { 11, 6, 0 }, "(and (header-contains \"subject\" \"Test1\") (header-contains \"subject\" \"subject\"))" },
	{ { 1, 1, 0 }, "(and (header-contains \"subject\" \"Test19\") (header-contains \"subject\" \"subject\"))" },
	{ { 0, 0, 0 }, "(and (header-contains \"subject\" \"Test191\") (header-contains \"subject\" \"subject\"))" },
	{ { 1, 1, 0 }, "(and (header-contains \"subject\" \"Test1\") (header-contains \"subject\" \"message99\"))" },

	{ { 22, 11, 0 }, "(or (header-contains \"subject\" \"Test1\") (header-contains \"subject\" \"Test2\"))" },
	{ { 2, 1, 0 }, "(or (header-contains \"subject\" \"Test16\") (header-contains \"subject\" \"Test99\"))" },
	{ { 1, 1, 0 }, "(or (header-contains \"subject\" \"Test123\") (header-contains \"subject\" \"Test99\"))" },
	{ { 100, 50, 0 }, "(or (header-contains \"subject\" \"Test1\") (header-contains \"subject\" \"subject\"))" },
	{ { 11, 6, 0 }, "(or (header-contains \"subject\" \"Test1\") (header-contains \"subject\" \"message99\"))" },

	/* 72000 is 24*60*100 == half the 'sent date' of the messages */
	{ { 100 / 2, 50 / 2, 0 }, "(> 72000 (get-sent-date))" },
	{ { 100 / 2 - 1, 50 / 2, 0 }, "(< 72000 (get-sent-date))" },
	{ { 1, 0, 0 }, "(= 72000 (get-sent-date))" },
	{ { 0, 0, 0 }, "(= 72001 (get-sent-date))" },

	{ { (100 / 2 - 1) / 17 + 1, (50 / 2 - 1) / 17 + 1, 0 }, "(and (user-flag \"every17\") (< 72000 (get-sent-date)))" },
	{ { (100 / 2 - 1) / 17 + 1, (50 / 2 - 1) / 17, 0 }, "(and (user-flag \"every17\") (> 72000 (get-sent-date)))" },
	{ { (100 / 2 - 1) / 13 + 1, (50 / 2 - 1) / 13 + 1, 0 }, "(and (user-flag \"every13\") (< 72000 (get-sent-date)))" },
	{ { (100 / 2 - 1) / 13 + 1, (50 / 2 - 1) / 13 + 1, 0 }, "(and (user-flag \"every13\") (> 72000 (get-sent-date)))" },

	{ { 100 / 2 + 100 / 2 / 17, 50 / 2 + 50 / 2 / 17, 0 }, "(or (user-flag \"every17\") (< 72000 (get-sent-date)))" },
	{ { 100 / 2 + 100 / 2 / 17 + 1, 50 / 2 + 50 / 2 / 17 + 1, 0 }, "(or (user-flag \"every17\") (> 72000 (get-sent-date)))" },
	{ { 100 / 2 + 100 / 2 / 13, 50 / 2 + 50 / 2 / 13 + 1, 0 }, "(or (user-flag \"every13\") (< 72000 (get-sent-date)))" },
	{ { 100 / 2 + 100 / 2 / 13 + 1, 50 / 2 + 50 / 2 / 13 + 1, 0 }, "(or (user-flag \"every13\") (> 72000 (get-sent-date)))" },
};

static void
run_search (CamelFolder *folder,
            gint m)
{
	gint i, j;

	g_assert_true (m == 50 || m == 100 || m == 0);

	j = 0;
	if (m == 50)
		j = 1;
	else if (m == 0)
		j = 2;

	for (i = 0; i < G_N_ELEMENTS (searches); i++) {
		test_folder_search (folder, searches[i].expr, searches[i].counts[j]);
	}
}

static const gchar *local_drivers[] = { "local" };

static void
test_folder_search_provider (gconstpointer user_data)
{
	gint param = GPOINTER_TO_INT (user_data);
	gint provider_index = param / 2;
	gint indexed = param % 2;
	const gchar *providers[] = { "mbox", "mh", "maildir" };
	const gchar *provider = providers[provider_index];
	CamelService *service;
	CamelSession *session;
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderSummary *summary;
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	gchar *store_uri;
	gchar *uid;
	gchar *content;
	gchar *subject;
	gchar *tag;
	gint j;
	gint flags;
	GPtrArray *uids;
	GError *error = NULL;

	camel_test_provider_init (1, local_drivers);

	session = camel_test_session_new (camel_test_get_dir ());

	store_uri = g_strdup_printf ("%s:///%s/%s", provider, camel_test_get_dir (), provider);

	service = camel_session_add_service (
		session, store_uri, store_uri,
		CAMEL_PROVIDER_STORE, &error);
	g_assert_no_error (error);
	g_assert_true (CAMEL_IS_STORE (service));
	store = CAMEL_STORE (service);
	g_clear_error (&error);

	if (indexed)
		flags = CAMEL_STORE_FOLDER_CREATE | CAMEL_STORE_FOLDER_BODY_INDEX;
	else
		flags = CAMEL_STORE_FOLDER_CREATE;
	folder = camel_store_get_folder_sync (
		store, "testbox", flags, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);
	g_clear_error (&error);

	test_folder_counts (folder, 0, 0);

	for (j = 0; j < 100; j++) {
		msg = test_message_create_simple ();
		content = g_strdup_printf ("data%d content\n", j);
		test_message_set_content_simple (
			(CamelMimePart *) msg, 0, "text/plain",
						content, strlen (content));
		g_free (content);
		subject = g_strdup_printf ("Test%d message%d subject", j, 100 - j);
		camel_mime_message_set_subject (msg, subject);
		camel_mime_message_set_date (msg, j * 60 * 24, 0);

		camel_folder_append_message_sync (
			folder, msg, NULL, NULL, NULL, &error);
		g_assert_no_error (error);
		g_clear_error (&error);

		g_free (subject);

		g_assert_cmpuint (G_OBJECT (msg)->ref_count, ==, 1);
		g_clear_object (&msg);
	}

	summary = camel_folder_get_folder_summary (folder);
	g_assert_true (CAMEL_IS_FOLDER_SUMMARY (summary));

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpuint (uids->len, ==, 100);
	for (j = 0; j < 100; j++) {
		uid = uids->pdata[j];

		if ((j / 13) * 13 == j) {
			info = camel_folder_summary_get (summary, uid);
			g_assert_true (CAMEL_IS_MESSAGE_INFO (info));
			camel_message_info_set_user_flag (info, "every13", TRUE);
			g_clear_object (&info);
		}
		if ((j / 17) * 17 == j) {
			info = camel_folder_summary_get (summary, uid);
			g_assert_true (CAMEL_IS_MESSAGE_INFO (info));
			camel_message_info_set_user_flag (info, "every17", TRUE);
			g_clear_object (&info);
		}
		if ((j / 7) * 7 == j) {
			info = camel_folder_summary_get (summary, uid);
			g_assert_true (CAMEL_IS_MESSAGE_INFO (info));
			tag = g_strdup_printf ("7tag%d", j / 7);
			camel_message_info_set_user_tag (info, "every7", tag);
			g_free (tag);
			g_clear_object (&info);
		}
		if ((j / 11) * 11 == j) {
			info = camel_folder_summary_get (summary, uid);
			g_assert_true (CAMEL_IS_MESSAGE_INFO (info));
			camel_message_info_set_user_tag (info, "every11", "11tag");
			g_clear_object (&info);
		}
	}
	g_clear_pointer (&uids, g_ptr_array_unref);

	run_search (folder, 100);

	camel_folder_synchronize_sync (folder, FALSE, NULL, NULL);
	run_search (folder, 100);

	camel_folder_synchronize_sync (folder, TRUE, NULL, NULL);
	run_search (folder, 100);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpuint (uids->len, ==, 100);
	for (j = 0; j < uids->len; j += 2) {
		camel_folder_delete_message (folder, uids->pdata[j]);
	}
	g_clear_pointer (&uids, g_ptr_array_unref);
	run_search (folder, 100);

	camel_folder_synchronize_sync (folder, FALSE, NULL, &error);
	g_assert_no_error (error);
	run_search (folder, 100);
	g_clear_error (&error);

	camel_folder_expunge_sync (folder, NULL, &error);
	g_assert_no_error (error);
	run_search (folder, 50);
	g_clear_error (&error);

	g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 1);
	g_clear_object (&folder);
	folder = camel_store_get_folder_sync (
		store, "testbox",
		flags & ~(CAMEL_STORE_FOLDER_CREATE),
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);
	g_clear_error (&error);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpuint (uids->len, ==, 50);
	for (j = 0; j < uids->len; j++) {
		camel_folder_delete_message (folder, uids->pdata[j]);
	}
	g_clear_pointer (&uids, g_ptr_array_unref);
	run_search (folder, 50);

	camel_folder_synchronize_sync (folder, FALSE, NULL, &error);
	g_assert_no_error (error);
	run_search (folder, 50);
	g_clear_error (&error);

	camel_folder_expunge_sync (folder, NULL, &error);
	g_assert_no_error (error);
	run_search (folder, 0);
	g_clear_error (&error);

	g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 1);
	g_clear_object (&folder);

	camel_store_delete_folder_sync (store, "testbox", NULL, &error);
	g_assert_no_error (error);
	g_clear_error (&error);

	g_assert_cmpuint (G_OBJECT (store)->ref_count, ==, 1);
	g_object_unref (store);
	g_free (store_uri);

	g_assert_cmpuint (G_OBJECT (session)->ref_count, ==, 1);
	g_clear_object (&session);
	camel_test_shutdown ();
}

gint
main (gint argc,
      gchar **argv)
{
	const gchar *providers[] = { "mbox", "mh", "maildir" };
	gint i, indexed;

	camel_test_init (&argc, &argv);

	for (i = 0; i < G_N_ELEMENTS (providers); i++) {
		for (indexed = 0; indexed < 2; indexed++) {
			gchar *test_path;

			test_path = g_strdup_printf (
				"/Camel/folder/search/%s/%s",
				providers[i],
				indexed ? "indexed" : "nonindexed");
			g_test_add_data_func (
				test_path,
				GINT_TO_POINTER (i * 2 + indexed),
				test_folder_search_provider);
			g_free (test_path);
		}
	}

	return g_test_run ();
}

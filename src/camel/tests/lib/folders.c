/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <string.h>

#include "camel-test.h"
#include "folders.h"
#include "messages.h"

/* check the total/unread is what we think it should be */
void
test_folder_counts (CamelFolder *folder,
                    gint total,
                    gint unread)
{
	GPtrArray *s;
	gint i, myunread;
	CamelMessageInfo *info;

	s = camel_folder_dup_uids (folder);
	g_assert_nonnull (s);
	g_assert_cmpint (s->len, ==, total);
	myunread = s->len;
	for (i = 0; i < s->len; i++) {
		info = camel_folder_get_message_info (folder, s->pdata[i]);
		if (camel_message_info_get_flags (info) & CAMEL_MESSAGE_SEEN)
			myunread--;
		g_clear_object (&info);
	}
	g_assert_cmpint (unread, ==, myunread);
	g_clear_pointer (&s, g_ptr_array_unref);
}

static gint
safe_strcmp (const gchar *a,
             const gchar *b)
{
	if (a == NULL && b == NULL)
		return 0;
	if (a == NULL)
		return 1;
	if (b == NULL)
		return -1;
	return strcmp (a, b);
}

void
test_message_info (CamelMimeMessage *msg,
                   const CamelMessageInfo *info)
{
	g_assert_cmpint (
		safe_strcmp (camel_message_info_get_subject (info), camel_mime_message_get_subject (msg)), ==, 0);

	g_assert_cmpint (camel_message_info_get_date_sent (info), ==, camel_mime_message_get_date (msg, NULL));
}

/* check a message is present */
void
test_folder_message (CamelFolder *folder,
                     const gchar *uid)
{
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	GPtrArray *s;
	gint i;
	gint found;
	GError *error = NULL;

	info = camel_folder_get_message_info (folder, uid);
	g_assert_nonnull (info);
	g_assert_cmpstr (camel_message_info_get_uid (info), ==, uid);
	g_clear_object (&info);

	msg = camel_folder_get_message_sync (folder, uid, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (msg);

	test_message_info (msg, info);

	g_object_unref (msg);

	s = camel_folder_dup_uids (folder);
	g_assert_nonnull (s);
	found = 0;
	for (i = 0; i < s->len; i++) {
		if (strcmp (s->pdata[i], uid) == 0)
			found++;
	}
	g_assert_cmpint (found, ==, 1);
	g_clear_pointer (&s, g_ptr_array_unref);

	g_clear_error (&error);
}

/* check message not present */
void
test_folder_not_message (CamelFolder *folder,
                         const gchar *uid)
{
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	GPtrArray *s;
	gint i;
	gint found;
	GError *error = NULL;

	info = camel_folder_get_message_info (folder, uid);
	g_assert_null (info);

	msg = camel_folder_get_message_sync (folder, uid, NULL, &error);
	g_assert_nonnull (error);
	g_assert_null (msg);
	g_clear_error (&error);

	s = camel_folder_dup_uids (folder);
	g_assert_nonnull (s);
	found = 0;
	for (i = 0; i < s->len; i++) {
		if (strcmp (s->pdata[i], uid) == 0)
			found++;
	}
	g_assert_cmpint (found, ==, 0);
	g_clear_pointer (&s, g_ptr_array_unref);

	g_clear_error (&error);
}

/* test basic store operations on folders */
void
test_folder_basic (CamelSession *session,
                   const gchar *storename,
                   gint local,
                   gint spool)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelService *service;
	GError *error = NULL;

	service = camel_session_add_service (
		session, storename, storename, CAMEL_PROVIDER_STORE, &error);
	g_assert_no_error (error);
	g_assert_true (CAMEL_IS_STORE (service));
	store = CAMEL_STORE (service);

	/* local providers == no inbox */
	folder = camel_store_get_inbox_folder_sync (store, NULL, &error);
	if (local) {
		if (folder) {
			g_assert_no_error (error);
			g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 1);
			g_object_unref (folder);
		} else {
			g_assert_nonnull (error);
		}
	} else {
		g_assert_no_error (error);
		g_assert_nonnull (folder);
		g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 2);
		g_object_unref (folder);
	}
	g_clear_error (&error);

	folder = camel_store_get_folder_sync (
		store, "unknown", 0, NULL, &error);
	g_assert_nonnull (error);
	g_assert_null (folder);
	g_clear_error (&error);

	if (!spool) {
		folder = camel_store_get_folder_sync (
			store, "testbox", CAMEL_STORE_FOLDER_CREATE,
			NULL, &error);
		g_assert_no_error (error);
		g_assert_nonnull (folder);
		if (local) {
			g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 1);
		} else {
			g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 2);
		}
		g_object_unref (folder);
		g_clear_error (&error);

		folder = camel_store_get_folder_sync (
			store, "testbox", 0, NULL, &error);
		g_assert_no_error (error);
		g_assert_nonnull (folder);
		if (local) {
			g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 1);
		} else {
			g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 2);
		}
		g_object_unref (folder);
		g_clear_error (&error);

		camel_store_rename_folder_sync (
			store, "unknown1", "unknown2", NULL, &error);
		g_assert_nonnull (error);
		g_clear_error (&error);

		camel_store_rename_folder_sync (
			store, "testbox", "testbox2", NULL, &error);
		g_assert_no_error (error);
		g_clear_error (&error);

		folder = camel_store_get_folder_sync (
			store, "testbox", 0, NULL, &error);
		g_assert_nonnull (error);
		g_assert_null (folder);
		g_clear_error (&error);

		folder = camel_store_get_folder_sync (
			store, "testbox2", 0, NULL, &error);
		g_assert_no_error (error);
		g_assert_nonnull (folder);
		if (local) {
			g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 1);
		} else {
			g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 2);
		}
		g_object_unref (folder);
	}

	camel_store_delete_folder_sync (store, "unknown", NULL, &error);
	g_assert_nonnull (error);
	g_clear_error (&error);

	if (!spool) {
		camel_store_delete_folder_sync (
			store, "testbox2", NULL, &error);
		g_assert_no_error (error);
		g_clear_error (&error);
	}

	folder = camel_store_get_folder_sync (
		store, "testbox2", 0, NULL, &error);
	g_assert_nonnull (error);
	g_assert_null (folder);
	g_clear_error (&error);

	g_assert_cmpuint (G_OBJECT (store)->ref_count, ==, 1);
	g_object_unref (store);
}

/* todo: cross-check everything with folder_info checks as well */
void
test_folder_message_ops (CamelSession *session,
                         const gchar *name,
                         gint local,
                         const gchar *mailbox)
{
	CamelStore *store;
	CamelService *service;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	gint j;
	gint indexed, max;
	GPtrArray *uids;
	CamelMessageInfo *info;
	GError *error = NULL;

	max = local ? 2 : 1;

	for (indexed = 0; indexed < max; indexed++) {
		gint flags;

		service = camel_session_add_service (
			session, name, name, CAMEL_PROVIDER_STORE, &error);
		g_assert_no_error (error);
		g_assert_true (CAMEL_IS_STORE (service));
		store = CAMEL_STORE (service);
		g_clear_error (&error);

		if (indexed)
			flags = CAMEL_STORE_FOLDER_CREATE | CAMEL_STORE_FOLDER_BODY_INDEX;
		else
			flags = CAMEL_STORE_FOLDER_CREATE;
		folder = camel_store_get_folder_sync (
			store, mailbox, flags, NULL, &error);

		if (folder == NULL) {
			gchar *mbox = g_strdup_printf ("INBOX/%s", mailbox);
			mailbox = mbox;
			g_clear_error (&error);
			folder = camel_store_get_folder_sync (
				store, mailbox, flags, NULL, &error);
		}

		g_assert_no_error (error);
		g_assert_nonnull (folder);

		test_folder_counts (folder, 0, 0);
		test_folder_not_message (folder, "0");
		test_folder_not_message (folder, "");

		for (j = 0; j < 10; j++) {
			gchar *content, *subject;

			msg = test_message_create_simple ();
			content = g_strdup_printf ("Test message %d contents\n\n", j);
			test_message_set_content_simple (
				(CamelMimePart *) msg, 0, "text/plain",
							content, strlen (content));
			g_free (content);
			subject = g_strdup_printf ("Test message %d", j);
			camel_mime_message_set_subject (msg, subject);

			camel_folder_append_message_sync (
				folder, msg, NULL, NULL, NULL, &error);
			g_assert_no_error (error);

			test_folder_counts (folder, j + 1, j + 1);

			uids = camel_folder_dup_uids (folder);
			g_assert_nonnull (uids);
			g_assert_cmpint (uids->len, ==, j + 1);
			if (uids->len > j)
				test_folder_message (folder, uids->pdata[j]);

			if (uids->len > j) {
				info = camel_folder_get_message_info (folder, uids->pdata[j]);
				g_assert_nonnull (info);
				g_assert_cmpstr (camel_message_info_get_subject (info), ==, subject);
				g_clear_object (&info);
			}
			g_clear_pointer (&uids, g_ptr_array_unref);

			g_free (subject);

			g_assert_cmpuint (G_OBJECT (msg)->ref_count, ==, 1);
			g_object_unref (msg);
		}

		if (local) {
			g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 1);
		} else {
			g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 2);
		}
		g_object_unref (folder);

		folder = camel_store_get_folder_sync (
			store, mailbox, flags, NULL, &error);
		g_assert_no_error (error);
		g_assert_nonnull (folder);
		g_clear_error (&error);

		test_folder_counts (folder, 10, 10);

		uids = camel_folder_dup_uids (folder);
		g_assert_nonnull (uids);
		g_assert_cmpint (uids->len, ==, 10);
		for (j = 0; j < 10; j++) {
			gchar *subject = g_strdup_printf ("Test message %d", j);

			test_folder_message (folder, uids->pdata[j]);

			info = camel_folder_get_message_info (folder, uids->pdata[j]);
			g_assert_cmpstr (camel_message_info_get_subject (info), ==, subject);
			g_free (subject);
			g_clear_object (&info);
		}

		camel_folder_delete_message (folder, uids->pdata[0]);
		test_folder_counts (folder, 10, 9);
		camel_folder_expunge_sync (folder, NULL, &error);
		g_assert_no_error (error);
		g_clear_error (&error);
		test_folder_not_message (folder, uids->pdata[0]);
		test_folder_counts (folder, 9, 9);

		g_clear_pointer (&uids, g_ptr_array_unref);

		uids = camel_folder_dup_uids (folder);
		g_assert_nonnull (uids);
		g_assert_cmpint (uids->len, ==, 9);
		for (j = 0; j < 9; j++) {
			gchar *subject = g_strdup_printf ("Test message %d", j + 1);

			test_folder_message (folder, uids->pdata[j]);

			info = camel_folder_get_message_info (folder, uids->pdata[j]);
			g_assert_cmpstr (camel_message_info_get_subject (info), ==, subject);
			g_free (subject);
			g_clear_object (&info);
		}

		camel_folder_delete_message (folder, uids->pdata[8]);
		test_folder_counts (folder, 9, 8);
		camel_folder_expunge_sync (folder, NULL, &error);
		g_assert_no_error (error);
		g_clear_error (&error);
		test_folder_not_message (folder, uids->pdata[8]);
		test_folder_counts (folder, 8, 8);

		g_clear_pointer (&uids, g_ptr_array_unref);

		uids = camel_folder_dup_uids (folder);
		g_assert_nonnull (uids);
		g_assert_cmpint (uids->len, ==, 8);
		for (j = 0; j < 8; j++) {
			gchar *subject = g_strdup_printf ("Test message %d", j + 1);

			test_folder_message (folder, uids->pdata[j]);

			info = camel_folder_get_message_info (folder, uids->pdata[j]);
			g_assert_cmpstr (camel_message_info_get_subject (info), ==, subject);
			g_free (subject);
			g_clear_object (&info);
		}

		for (j = 0; j < 8; j++) {
			camel_folder_delete_message (folder, uids->pdata[j]);
		}
		test_folder_counts (folder, 8, 0);
		camel_folder_expunge_sync (folder, NULL, &error);
		g_assert_no_error (error);
		g_clear_error (&error);
		for (j = 0; j < 8; j++) {
			test_folder_not_message (folder, uids->pdata[j]);
		}
		test_folder_counts (folder, 0, 0);

		g_clear_pointer (&uids, g_ptr_array_unref);

		if (local) {
			g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 1);
		} else {
			g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 2);
		}
		g_object_unref (folder);

		if (g_ascii_strcasecmp (mailbox, "INBOX") != 0) {
			camel_store_delete_folder_sync (
				store, mailbox, NULL, &error);
			g_assert_no_error (error);
			g_clear_error (&error);
		}

		if (!local) {
			camel_service_disconnect_sync (
				CAMEL_SERVICE (store), TRUE, NULL, &error);
			g_assert_no_error (error);
			g_clear_error (&error);
		}

		g_assert_cmpuint (G_OBJECT (store)->ref_count, ==, 1);
		g_object_unref (store);
	}
}

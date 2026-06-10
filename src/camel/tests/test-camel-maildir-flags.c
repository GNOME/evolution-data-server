/*
 * SPDX-FileCopyrightText: (C) 2026 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <string.h>
#include <glib/gstdio.h>

#include "camel-test.h"
#include "camel-test-provider.h"
#include "session.h"

#include <camel/camel.h>
#include "providers/local/camel-maildir-summary.h"

static const gchar *local_drivers[] = { "local" };

static gchar *
find_message_file_in_cur (const gchar *cur_path,
                          const gchar *uid)
{
	GDir *dir;
	const gchar *entry;
	gchar *result = NULL;
	gsize uid_len;

	uid_len = strlen (uid);

	dir = g_dir_open (cur_path, 0, NULL);
	g_assert_nonnull (dir);

	while ((entry = g_dir_read_name (dir)) != NULL) {
		if (strncmp (entry, uid, uid_len) == 0 &&
		    (entry[uid_len] == ':' || entry[uid_len] == '\0')) {
			result = g_build_filename (cur_path, entry, NULL);
			break;
		}
	}

	g_dir_close (dir);

	return result;
}

static void
rename_message_flags (const gchar *cur_path,
                      const gchar *uid,
                      const gchar *new_flags)
{
	gchar *old_path;
	gchar *new_name;
	gchar *new_path;

	old_path = find_message_file_in_cur (cur_path, uid);
	g_assert_nonnull (old_path);

	new_name = g_strdup_printf ("%s%c2,%s", uid, CAMEL_MAILDIR_FILENAME_FLAG_SEP, new_flags);
	new_path = g_build_filename (cur_path, new_name, NULL);

	g_assert_cmpint (g_rename (old_path, new_path), ==, 0);

	g_free (old_path);
	g_free (new_name);
	g_free (new_path);
}

static gboolean
message_file_has_flag_char (const gchar *cur_path,
                            const gchar *uid,
                            gchar flag_char)
{
	gchar *path;
	gchar sep_pattern[4];
	const gchar *flags_part;
	gboolean has_flag = FALSE;

	path = find_message_file_in_cur (cur_path, uid);
	g_assert_nonnull (path);

	sep_pattern[0] = CAMEL_MAILDIR_FILENAME_FLAG_SEP;
	sep_pattern[1] = '2';
	sep_pattern[2] = ',';
	sep_pattern[3] = '\0';

	flags_part = strstr (path, sep_pattern);
	if (flags_part) {
		flags_part += 3;
		has_flag = strchr (flags_part, flag_char) != NULL;
	}

	g_free (path);

	return has_flag;
}

static CamelMimeMessage *
create_message (const gchar *subject)
{
	CamelMimeMessage *msg;
	CamelInternetAddress *addr;

	msg = camel_mime_message_new ();
	camel_mime_message_set_subject (msg, subject);
	camel_mime_message_set_date (msg, 0, 0);

	addr = camel_internet_address_new ();
	camel_internet_address_add (addr, "sender", "sender@example.com");
	camel_mime_message_set_from (msg, addr);
	g_object_unref (addr);

	addr = camel_internet_address_new ();
	camel_internet_address_add (addr, "recipient", "recipient@example.com");
	camel_mime_message_set_recipients (msg, "To", addr);
	g_object_unref (addr);

	camel_mime_part_set_content (
		CAMEL_MIME_PART (msg), "Test body\n",
		strlen ("Test body\n"), "text/plain");

	return msg;
}

static void
test_maildir_external_flag_added (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelLocalSettings *local_settings;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	gchar *store_path;
	gchar *cur_path;
	gchar *uid = NULL;
	GError *error = NULL;

	session = g_object_new (CAMEL_TYPE_TEST_SESSION,
		"user-data-dir", camel_test_get_dir (),
		"user-cache-dir", camel_test_get_dir (),
		NULL);

	store_path = g_build_filename (camel_test_get_dir (), "test-added", NULL);

	service = camel_session_add_service (session, "test-added", "maildir", CAMEL_PROVIDER_STORE, &error);
	g_assert_no_error (error);
	store = CAMEL_STORE (service);

	local_settings = CAMEL_LOCAL_SETTINGS (camel_service_ref_settings (service));
	camel_local_settings_set_path (local_settings, store_path);
	g_object_unref (local_settings);

	g_mkdir_with_parents (store_path, 0700);

	folder = camel_store_get_folder_sync (store, "testbox", CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	cur_path = g_build_filename (store_path, ".testbox", "cur", NULL);

	msg = create_message ("External flag test");
	camel_folder_append_message_sync (folder, msg, NULL, &uid, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (uid);
	g_object_unref (msg);

	/* The message starts as unread */
	info = camel_folder_get_message_info (folder, uid);
	g_assert_nonnull (info);
	g_assert_false ((camel_message_info_get_flags (info) & CAMEL_MESSAGE_SEEN) != 0);
	g_clear_object (&info);

	/* Sync to disk so the filename is stable */
	camel_folder_synchronize_sync (folder, FALSE, NULL, NULL);

	/* Simulate an external client marking the message as Seen */
	rename_message_flags (cur_path, uid, "S");
	g_assert_true (message_file_has_flag_char (cur_path, uid, 'S'));

	/* Close and reopen the folder, then refresh to pick up
	 * on-disk changes (as Evolution does when a user clicks
	 * on a folder). */
	g_clear_object (&folder);

	folder = camel_store_get_folder_sync (store, "testbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);

	/* After refresh, the Seen flag should be picked up from disk */
	info = camel_folder_get_message_info (folder, uid);
	g_assert_nonnull (info);
	g_assert_true ((camel_message_info_get_flags (info) & CAMEL_MESSAGE_SEEN) != 0);
	g_clear_object (&info);

	/* The on-disk file should still have 'S' */
	g_assert_true (message_file_has_flag_char (cur_path, uid, 'S'));

	g_clear_object (&folder);
	g_free (uid);
	g_free (cur_path);
	g_object_unref (store);
	g_free (store_path);
	g_clear_object (&session);
}

static void
test_maildir_external_flag_removed (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelLocalSettings *local_settings;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	gchar *store_path;
	gchar *cur_path;
	gchar *uid = NULL;
	GError *error = NULL;

	session = g_object_new (CAMEL_TYPE_TEST_SESSION,
		"user-data-dir", camel_test_get_dir (),
		"user-cache-dir", camel_test_get_dir (),
		NULL);

	store_path = g_build_filename (camel_test_get_dir (), "test-removed", NULL);

	service = camel_session_add_service (session, "test-removed", "maildir", CAMEL_PROVIDER_STORE, &error);
	g_assert_no_error (error);
	store = CAMEL_STORE (service);

	local_settings = CAMEL_LOCAL_SETTINGS (camel_service_ref_settings (service));
	camel_local_settings_set_path (local_settings, store_path);
	g_object_unref (local_settings);

	g_mkdir_with_parents (store_path, 0700);

	folder = camel_store_get_folder_sync (store, "testbox", CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	cur_path = g_build_filename (store_path, ".testbox", "cur", NULL);

	msg = create_message ("External unflag test");
	camel_folder_append_message_sync (folder, msg, NULL, &uid, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (uid);
	g_object_unref (msg);

	/* Mark as Seen via Evolution */
	info = camel_folder_get_message_info (folder, uid);
	g_assert_nonnull (info);
	camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
	g_assert_true ((camel_message_info_get_flags (info) & CAMEL_MESSAGE_SEEN) != 0);
	g_clear_object (&info);

	/* Sync to disk so the filename gets the 'S' flag */
	camel_folder_synchronize_sync (folder, FALSE, NULL, NULL);
	g_assert_true (message_file_has_flag_char (cur_path, uid, 'S'));

	/* Simulate an external tool marking it unread */
	rename_message_flags (cur_path, uid, "");
	g_assert_false (message_file_has_flag_char (cur_path, uid, 'S'));

	/* Close and reopen the folder, then refresh */
	g_clear_object (&folder);

	folder = camel_store_get_folder_sync (store, "testbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);

	/* After refresh, the Seen flag should be cleared */
	info = camel_folder_get_message_info (folder, uid);
	g_assert_nonnull (info);
	g_assert_false ((camel_message_info_get_flags (info) & CAMEL_MESSAGE_SEEN) != 0);
	g_clear_object (&info);

	/* The on-disk file should still have no 'S' */
	g_assert_false (message_file_has_flag_char (cur_path, uid, 'S'));

	g_clear_object (&folder);
	g_free (uid);
	g_free (cur_path);
	g_object_unref (store);
	g_free (store_path);
	g_clear_object (&session);
}

static void
test_maildir_external_multiple_flags (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelLocalSettings *local_settings;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	gchar *store_path;
	gchar *cur_path;
	gchar *uid = NULL;
	guint32 flags;
	GError *error = NULL;

	session = g_object_new (CAMEL_TYPE_TEST_SESSION,
		"user-data-dir", camel_test_get_dir (),
		"user-cache-dir", camel_test_get_dir (),
		NULL);

	store_path = g_build_filename (camel_test_get_dir (), "test-multi", NULL);

	service = camel_session_add_service (session, "test-multi", "maildir", CAMEL_PROVIDER_STORE, &error);
	g_assert_no_error (error);
	store = CAMEL_STORE (service);

	local_settings = CAMEL_LOCAL_SETTINGS (camel_service_ref_settings (service));
	camel_local_settings_set_path (local_settings, store_path);
	g_object_unref (local_settings);

	g_mkdir_with_parents (store_path, 0700);

	folder = camel_store_get_folder_sync (store, "testbox", CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	cur_path = g_build_filename (store_path, ".testbox", "cur", NULL);

	msg = create_message ("Multiple flags test");
	camel_folder_append_message_sync (folder, msg, NULL, &uid, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (uid);
	g_object_unref (msg);

	/* Sync to disk */
	camel_folder_synchronize_sync (folder, FALSE, NULL, NULL);

	/* Simulate an external client setting Seen, Replied, and Flagged */
	rename_message_flags (cur_path, uid, "FRS");

	/* Close and reopen, then refresh */
	g_clear_object (&folder);

	folder = camel_store_get_folder_sync (store, "testbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);

	info = camel_folder_get_message_info (folder, uid);
	g_assert_nonnull (info);
	flags = camel_message_info_get_flags (info);
	g_assert_true ((flags & CAMEL_MESSAGE_SEEN) != 0);
	g_assert_true ((flags & CAMEL_MESSAGE_ANSWERED) != 0);
	g_assert_true ((flags & CAMEL_MESSAGE_FLAGGED) != 0);
	g_assert_false ((flags & CAMEL_MESSAGE_DELETED) != 0);
	g_assert_false ((flags & CAMEL_MESSAGE_DRAFT) != 0);
	g_clear_object (&info);

	g_assert_true (message_file_has_flag_char (cur_path, uid, 'S'));
	g_assert_true (message_file_has_flag_char (cur_path, uid, 'R'));
	g_assert_true (message_file_has_flag_char (cur_path, uid, 'F'));

	g_clear_object (&folder);
	g_free (uid);
	g_free (cur_path);
	g_object_unref (store);
	g_free (store_path);
	g_clear_object (&session);
}

static void
test_maildir_external_multiple_flags_removed (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelLocalSettings *local_settings;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	gchar *store_path;
	gchar *cur_path;
	gchar *uid = NULL;
	guint32 flags;
	GError *error = NULL;

	session = g_object_new (CAMEL_TYPE_TEST_SESSION,
		"user-data-dir", camel_test_get_dir (),
		"user-cache-dir", camel_test_get_dir (),
		NULL);

	store_path = g_build_filename (camel_test_get_dir (), "test-multi-rm", NULL);

	service = camel_session_add_service (session, "test-multi-rm", "maildir", CAMEL_PROVIDER_STORE, &error);
	g_assert_no_error (error);
	store = CAMEL_STORE (service);

	local_settings = CAMEL_LOCAL_SETTINGS (camel_service_ref_settings (service));
	camel_local_settings_set_path (local_settings, store_path);
	g_object_unref (local_settings);

	g_mkdir_with_parents (store_path, 0700);

	folder = camel_store_get_folder_sync (store, "testbox", CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	cur_path = g_build_filename (store_path, ".testbox", "cur", NULL);

	msg = create_message ("Multiple flags removal test");
	camel_folder_append_message_sync (folder, msg, NULL, &uid, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (uid);
	g_object_unref (msg);

	/* Set Seen, Answered, and Flagged via Evolution */
	info = camel_folder_get_message_info (folder, uid);
	g_assert_nonnull (info);
	camel_message_info_set_flags (info,
		CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_FLAGGED,
		CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_FLAGGED);
	flags = camel_message_info_get_flags (info);
	g_assert_true ((flags & CAMEL_MESSAGE_SEEN) != 0);
	g_assert_true ((flags & CAMEL_MESSAGE_ANSWERED) != 0);
	g_assert_true ((flags & CAMEL_MESSAGE_FLAGGED) != 0);
	g_clear_object (&info);

	/* Sync to disk so the filename gets FRS */
	camel_folder_synchronize_sync (folder, FALSE, NULL, NULL);
	g_assert_true (message_file_has_flag_char (cur_path, uid, 'S'));
	g_assert_true (message_file_has_flag_char (cur_path, uid, 'R'));
	g_assert_true (message_file_has_flag_char (cur_path, uid, 'F'));

	/* Simulate an external tool removing all flags */
	rename_message_flags (cur_path, uid, "");
	g_assert_false (message_file_has_flag_char (cur_path, uid, 'S'));
	g_assert_false (message_file_has_flag_char (cur_path, uid, 'R'));
	g_assert_false (message_file_has_flag_char (cur_path, uid, 'F'));

	/* Close and reopen, then refresh */
	g_clear_object (&folder);

	folder = camel_store_get_folder_sync (store, "testbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);

	/* After refresh, all three flags should be cleared */
	info = camel_folder_get_message_info (folder, uid);
	g_assert_nonnull (info);
	flags = camel_message_info_get_flags (info);
	g_assert_false ((flags & CAMEL_MESSAGE_SEEN) != 0);
	g_assert_false ((flags & CAMEL_MESSAGE_ANSWERED) != 0);
	g_assert_false ((flags & CAMEL_MESSAGE_FLAGGED) != 0);
	g_clear_object (&info);

	/* The on-disk file should still have no flags */
	g_assert_false (message_file_has_flag_char (cur_path, uid, 'S'));
	g_assert_false (message_file_has_flag_char (cur_path, uid, 'R'));
	g_assert_false (message_file_has_flag_char (cur_path, uid, 'F'));

	g_clear_object (&folder);
	g_free (uid);
	g_free (cur_path);
	g_object_unref (store);
	g_free (store_path);
	g_clear_object (&session);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);
	camel_test_provider_init (1, local_drivers);

	g_test_add_func ("/Camel/maildir/flags/external-flag-added", test_maildir_external_flag_added);
	g_test_add_func ("/Camel/maildir/flags/external-flag-removed", test_maildir_external_flag_removed);
	g_test_add_func ("/Camel/maildir/flags/external-multiple-flags", test_maildir_external_multiple_flags);
	g_test_add_func ("/Camel/maildir/flags/external-multiple-flags-removed", test_maildir_external_multiple_flags_removed);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

/*
 * SPDX-FileCopyrightText: (C) 2026 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <string.h>

#include "camel-test.h"
#include "camel-test-provider.h"
#include "session.h"

#include <camel/camel.h>

static const gchar *local_drivers[] = { "local" };

static CamelMimeMessage *
create_plain_message (const gchar *subject,
                      const gchar *body_text)
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

	camel_mime_part_set_content (CAMEL_MIME_PART (msg), body_text, strlen (body_text), "text/plain");

	return msg;
}

static CamelMimeMessage *
create_html_message (const gchar *subject,
                     const gchar *body_text)
{
	CamelMimeMessage *msg;
	CamelInternetAddress *addr;
	gchar *html;

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

	html = g_strdup_printf ("<html><body><p>%s</p></body></html>", body_text);
	camel_mime_part_set_content (CAMEL_MIME_PART (msg), html, strlen (html), "text/html");
	g_free (html);

	return msg;
}

static CamelMimeMessage *
create_multipart_message (const gchar *subject,
                          const gchar *plain_text,
                          const gchar *html_text)
{
	CamelMimeMessage *msg;
	CamelInternetAddress *addr;
	CamelMultipart *mp;
	CamelMimePart *part;
	gchar *html;

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

	mp = camel_multipart_new ();
	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (mp), "multipart/alternative");
	camel_multipart_set_boundary (mp, NULL);

	part = camel_mime_part_new ();
	camel_mime_part_set_content (part, plain_text, strlen (plain_text), "text/plain");
	camel_multipart_add_part (mp, part);
	g_object_unref (part);

	html = g_strdup_printf ("<html><body><p>%s</p></body></html>", html_text);
	part = camel_mime_part_new ();
	camel_mime_part_set_content (part, html, strlen (html), "text/html");
	camel_multipart_add_part (mp, part);
	g_object_unref (part);
	g_free (html);

	camel_medium_set_content (CAMEL_MEDIUM (msg), CAMEL_DATA_WRAPPER (mp));
	g_object_unref (mp);

	return msg;
}

static void
assert_body_search (CamelFolder *folder,
                    const gchar *word,
                    guint expected_count)
{
	GPtrArray *uids = NULL;
	GError *error = NULL;
	gchar *sexp;
	gboolean success;

	sexp = g_strdup_printf ("(match-all (body-contains \"%s\"))", word);
	success = camel_folder_search_sync (folder, sexp, &uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_cmpuint (uids->len, ==, expected_count);
	g_ptr_array_unref (uids);
	g_free (sexp);
}

static void
assert_index_find (CamelIndex *index,
                   const gchar *word,
                   guint expected_count)
{
	CamelIndexCursor *idc;
	guint count = 0;

	idc = camel_index_find (index, word);
	if (idc) {
		while (camel_index_cursor_next (idc) != NULL) {
			count++;
		}
		g_object_unref (idc);
	}
	g_assert_cmpuint (count, ==, expected_count);
}

static void
test_body_search (gconstpointer user_data)
{
	gboolean use_index = GPOINTER_TO_INT (user_data);
	CamelService *service;
	CamelSession *session;
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderSummary *summary;
	CamelIndex *body_index;
	CamelLocalSettings *local_settings;
	CamelMimeMessage *msg;
	gchar *store_path;
	gchar *uid0 = NULL;
	gint flags;
	GError *error = NULL;

	session = g_object_new (
		CAMEL_TYPE_TEST_SESSION,
		"user-data-dir", camel_test_get_dir (),
		"user-cache-dir", camel_test_get_dir (),
		NULL);

	store_path = g_build_filename (camel_test_get_dir (), "maildir", NULL);

	service = camel_session_add_service (session, "test-maildir", "maildir", CAMEL_PROVIDER_STORE, &error);
	g_assert_no_error (error);
	g_assert_true (CAMEL_IS_STORE (service));
	store = CAMEL_STORE (service);

	local_settings = CAMEL_LOCAL_SETTINGS (camel_service_ref_settings (service));
	camel_local_settings_set_path (local_settings, store_path);
	g_object_unref (local_settings);

	g_mkdir_with_parents (store_path, 0700);

	flags = CAMEL_STORE_FOLDER_CREATE;
	if (use_index)
		flags |= CAMEL_STORE_FOLDER_BODY_INDEX;

	folder = camel_store_get_folder_sync (store, use_index ? "testbox-indexed" : "testbox-plain", flags, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	/* msg 0: text/plain with unique word "xylophone" and shared "panorama" */
	msg = create_plain_message ("Plain test", "panorama xylophone content");
	camel_folder_append_message_sync (folder, msg, NULL, &uid0, NULL, &error);
	g_assert_no_error (error);
	g_object_unref (msg);

	/* msg 1: text/html with unique word "kaleidoscope" */
	msg = create_html_message ("HTML test", "panorama kaleidoscope");
	camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_object_unref (msg);

	/* msg 2: multipart/alternative, plain has "periscope", html has "telescope" */
	msg = create_multipart_message ("Multipart test", "panorama periscope plaintext", "panorama telescope htmlpart");
	camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_object_unref (msg);

	/* msg 3: text/plain with no matching words */
	msg = create_plain_message ("Unrelated test", "nothing relevant here");
	camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_object_unref (msg);

	camel_folder_synchronize_sync (folder, FALSE, NULL, NULL);

	/* verify index is populated (only when indexing is on) */
	summary = camel_folder_get_folder_summary (folder);
	body_index = summary ? camel_folder_summary_get_index (summary) : NULL;

	if (use_index) {
		g_assert_nonnull (body_index);
		camel_index_sync (body_index);
		assert_index_find (body_index, "panorama", 3);
		assert_index_find (body_index, "xylophone", 1);
		assert_index_find (body_index, "kaleidoscope", 1);
		assert_index_find (body_index, "periscope", 1);
		assert_index_find (body_index, "telescope", 1);
		assert_index_find (body_index, "nothing", 1);
	} else {
		g_assert_null (body_index);
	}

	/* search via camel_folder_search_sync */
	assert_body_search (folder, "panorama", 3);
	assert_body_search (folder, "xylophone", 1);
	assert_body_search (folder, "kaleidoscope", 1);
	assert_body_search (folder, "periscope", 1);
	assert_body_search (folder, "telescope", 1);
	assert_body_search (folder, "nothing", 1);

	/* delete the "xylophone" message and expunge */
	g_assert_nonnull (uid0);
	camel_folder_delete_message (folder, uid0);
	camel_folder_expunge_sync (folder, NULL, &error);
	g_assert_no_error (error);

	/* "xylophone" should be gone, "panorama" down to 2 */
	assert_body_search (folder, "xylophone", 0);
	assert_body_search (folder, "panorama", 2);
	assert_body_search (folder, "kaleidoscope", 1);
	assert_body_search (folder, "nothing", 1);

	if (use_index) {
		assert_index_find (body_index, "xylophone", 0);
		assert_index_find (body_index, "panorama", 2);
	}

	/* reopen folder and verify persistence */
	g_clear_object (&folder);
	folder = camel_store_get_folder_sync (store, use_index ? "testbox-indexed" : "testbox-plain", flags & ~CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	assert_body_search (folder, "xylophone", 0);
	assert_body_search (folder, "panorama", 2);
	assert_body_search (folder, "kaleidoscope", 1);
	assert_body_search (folder, "periscope", 1);
	assert_body_search (folder, "telescope", 1);
	assert_body_search (folder, "nothing", 1);

	if (use_index) {
		summary = camel_folder_get_folder_summary (folder);
		body_index = summary ? camel_folder_summary_get_index (summary) : NULL;
		g_assert_nonnull (body_index);
		assert_index_find (body_index, "panorama", 2);
		assert_index_find (body_index, "xylophone", 0);
	}

	g_clear_object (&folder);
	g_free (uid0);

	g_object_unref (store);
	g_free (store_path);
	g_clear_object (&session);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	g_test_init (&argc, &argv, NULL);
	camel_test_init ();
	camel_test_provider_init (1, local_drivers);

	g_test_add_data_func ("/Camel/Folder/BodySearch/maildir/indexed", GINT_TO_POINTER (1), test_body_search);
	g_test_add_data_func ("/Camel/Folder/BodySearch/maildir/nonindexed", GINT_TO_POINTER (0), test_body_search);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

/*
 * SPDX-FileCopyrightText: (C) 2026 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <string.h>
#include <glib.h>

#include <camel/camel.h>

#include "camel-test.h"
#include "camel-test-provider.h"
#include "dovecot-helper.h"

typedef struct _Pop3TestSession Pop3TestSession;
typedef struct _Pop3TestSessionClass Pop3TestSessionClass;

struct _Pop3TestSession {
	CamelSession parent;
};

struct _Pop3TestSessionClass {
	CamelSessionClass parent_class;
};

GType pop3_test_session_get_type (void);

G_DEFINE_TYPE (Pop3TestSession, pop3_test_session, CAMEL_TYPE_SESSION)

static gboolean
pop3_test_session_authenticate_sync (CamelSession *session,
				     CamelService *service,
				     const gchar *mechanism,
				     GCancellable *cancellable,
				     GError **error)
{
	CamelAuthenticationResult result;

	result = camel_service_authenticate_sync (service, mechanism, cancellable, error);

	/* camel_session_authenticate_sync() requires an error to be set
	 * whenever this callback reports failure, including a plain
	 * rejection (as opposed to a connection/protocol error, which
	 * camel_service_authenticate_sync() above already sets). */
	if (result != CAMEL_AUTHENTICATION_ACCEPTED && error != NULL && *error == NULL) {
		g_set_error_literal (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			"Authentication rejected");
	}

	return result == CAMEL_AUTHENTICATION_ACCEPTED;
}

static CamelCertTrust
pop3_test_session_trust_prompt (CamelSession *session,
				CamelService *service,
				GTlsCertificate *certificate,
				GTlsCertificateFlags errors)
{
	return CAMEL_CERT_TRUST_FULLY;
}

static void
pop3_test_session_class_init (Pop3TestSessionClass *klass)
{
	CamelSessionClass *session_class;

	session_class = CAMEL_SESSION_CLASS (klass);
	session_class->authenticate_sync = pop3_test_session_authenticate_sync;
	session_class->trust_prompt = pop3_test_session_trust_prompt;
}

static void
pop3_test_session_init (Pop3TestSession *session)
{
}

static DovecotTestServer *test_server = NULL;
static const gchar *pop3_drivers[] = { "pop3" };

static CamelSession *
test_pop3_session_new (void)
{
	return g_object_new (
		pop3_test_session_get_type (),
		"user-data-dir", camel_test_get_dir (),
		"user-cache-dir", camel_test_get_dir (),
		NULL);
}

static CamelService *
test_pop3_create_service_full (CamelSession *session,
			       const gchar *uid,
			       CamelNetworkSecurityMethod security_method)
{
	CamelService *service;
	CamelSettings *settings;
	guint16 port;
	GError *error = NULL;

	service = camel_session_add_service (session, uid, "pop", CAMEL_PROVIDER_STORE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (service);

	if (security_method == CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT)
		port = dovecot_test_server_get_tls_port (test_server);
	else
		port = dovecot_test_server_get_port (test_server);

	settings = camel_service_ref_settings (service);

	camel_network_settings_set_host (CAMEL_NETWORK_SETTINGS (settings), dovecot_test_server_get_host (test_server));
	camel_network_settings_set_port (CAMEL_NETWORK_SETTINGS (settings), port);
	camel_network_settings_set_user (CAMEL_NETWORK_SETTINGS (settings), dovecot_test_server_get_user (test_server));
	camel_network_settings_set_security_method (CAMEL_NETWORK_SETTINGS (settings), security_method);

	g_object_unref (settings);

	camel_service_set_password (service, dovecot_test_server_get_password (test_server));

	return service;
}

static CamelService *
test_pop3_create_service (CamelSession *session,
			  const gchar *uid)
{
	return test_pop3_create_service_full (session, uid, CAMEL_NETWORK_SECURITY_METHOD_NONE);
}

static void
test_flush_main_context (void)
{
	while (g_main_context_iteration (NULL, FALSE)) {
	}
}

static void
test_pop3_connect_service (CamelService *service)
{
	GError *error = NULL;
	gboolean success;

	success = camel_service_connect_sync (service, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	test_flush_main_context ();
}

static void
test_pop3_disconnect_service (CamelService *service)
{
	GError *error = NULL;
	gboolean success;

	success = camel_service_disconnect_sync (service, TRUE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	test_flush_main_context ();
}

static void
test_pop3_teardown (CamelSession *session,
		    CamelService *service)
{
	test_pop3_disconnect_service (service);
	camel_session_remove_service (session, service);
	test_flush_main_context ();
	g_object_unref (service);
	test_flush_main_context ();
	g_object_unref (session);
	test_flush_main_context ();
}

/* Used after a synchronize_sync() call that already sent QUIT to the server
 * (i.e. an expunge that actually deleted something): issuing a second clean
 * QUIT via camel_service_disconnect_sync(TRUE) would just talk to a socket
 * the server may have already closed, so tear down without re-sending it. */
static void
test_pop3_teardown_after_expunge (CamelSession *session,
				  CamelService *service)
{
	GError *error = NULL;
	gboolean success;

	success = camel_service_disconnect_sync (service, FALSE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	test_flush_main_context ();

	camel_session_remove_service (session, service);
	test_flush_main_context ();
	g_object_unref (service);
	test_flush_main_context ();
	g_object_unref (session);
	test_flush_main_context ();
}

static void
test_pop3_reconnect_service (CamelSession *session,
			     CamelService **inout_service,
			     const gchar *uid)
{
	test_pop3_disconnect_service (*inout_service);
	camel_session_remove_service (session, *inout_service);
	test_flush_main_context ();
	g_object_unref (*inout_service);
	test_flush_main_context ();

	*inout_service = test_pop3_create_service (session, uid);
	test_pop3_connect_service (*inout_service);
}

static CamelMimeMessage *
test_create_message (const gchar *subject,
		     const gchar *body)
{
	CamelMimeMessage *msg;
	CamelInternetAddress *addr;

	msg = camel_mime_message_new ();
	camel_mime_message_set_subject (msg, subject);
	camel_mime_message_set_date (msg, CAMEL_MESSAGE_DATE_CURRENT, 0);

	addr = camel_internet_address_new ();
	camel_internet_address_add (addr, "Test User", "test@example.com");
	camel_mime_message_set_from (msg, addr);
	camel_mime_message_set_recipients (msg, CAMEL_RECIPIENT_TYPE_TO, addr);
	g_object_unref (addr);

	camel_mime_part_set_content (CAMEL_MIME_PART (msg), body, strlen (body), "text/plain");

	return msg;
}

static void
inject_message_into_maildir (const gchar *subject,
			     const gchar *body)
{
	static guint32 counter = 0;
	CamelMimeMessage *msg;
	CamelStream *stream;
	GByteArray *byte_array;
	gchar *new_dir;
	gchar *filename;
	gchar *full_path;
	GError *error = NULL;
	gssize written;

	msg = test_create_message (subject, body);

	byte_array = g_byte_array_new ();
	stream = camel_stream_mem_new_with_byte_array (byte_array);
	written = camel_data_wrapper_write_to_stream_sync (CAMEL_DATA_WRAPPER (msg), stream, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (written, >=, 0);
	g_object_unref (msg);

	new_dir = g_build_filename (dovecot_test_server_get_maildir_path (test_server), "new", NULL);
	filename = g_strdup_printf ("camel-test-pop3-%08x-%u", g_test_rand_int (), counter++);
	full_path = g_build_filename (new_dir, filename, NULL);

	/* byte_array is owned by (and freed with) stream, so it must be read
	 * before dropping the stream reference. */
	g_file_set_contents (full_path, (const gchar *) byte_array->data, byte_array->len, &error);
	g_assert_no_error (error);

	g_object_unref (stream);
	g_free (full_path);
	g_free (filename);
	g_free (new_dir);
}

static void
test_pop3_purge_mailbox (const gchar *svc_uid)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelSettings *settings;
	GPtrArray *uids;
	GError *error = NULL;
	gboolean success;
	guint ii;

	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, svc_uid);
	store = CAMEL_STORE (service);

	settings = camel_service_ref_settings (service);
	g_object_set (settings, "keep-on-server", FALSE, "delete-expunged", TRUE, NULL);
	g_object_unref (settings);

	test_pop3_connect_service (service);

	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	uids = camel_folder_dup_uids (folder);
	for (ii = 0; ii < uids->len; ii++) {
		camel_folder_set_message_flags (folder, uids->pdata[ii],
			CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);
	}
	g_ptr_array_unref (uids);

	success = camel_folder_synchronize_sync (folder, TRUE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_object_unref (folder);

	test_pop3_teardown_after_expunge (session, service);
}

static gchar *
get_single_uid (CamelFolder *folder)
{
	GPtrArray *uids;
	gchar *uid;

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpint (uids->len, ==, 1);
	uid = g_strdup (uids->pdata[0]);
	g_ptr_array_unref (uids);

	return uid;
}

static gchar *
find_uid_by_subject (CamelFolder *folder,
		     GPtrArray *uids,
		     const gchar *subject)
{
	guint ii;

	for (ii = 0; ii < uids->len; ii++) {
		CamelMimeMessage *msg;
		const gchar *uid = uids->pdata[ii];
		GError *error = NULL;
		gboolean matches;

		msg = camel_folder_get_message_sync (folder, uid, NULL, &error);
		g_assert_no_error (error);
		g_assert_nonnull (msg);

		matches = g_strcmp0 (camel_mime_message_get_subject (msg), subject) == 0;
		g_object_unref (msg);

		if (matches)
			return g_strdup (uid);
	}

	return NULL;
}

static void
test_connect (void)
{
	CamelSession *session;
	CamelService *service;

	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-connect");

	test_pop3_connect_service (service);

	test_pop3_teardown (session, service);
}

static void
test_connect_starttls (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	GError *error = NULL;
	gboolean success;

	test_pop3_purge_mailbox ("test-starttls-purge");

	session = test_pop3_session_new ();
	service = test_pop3_create_service_full (session, "test-starttls",
		CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT);
	store = CAMEL_STORE (service);

	test_pop3_connect_service (service);

	/* Round-trip a real command over the upgraded connection, not just
	 * the handshake. */
	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 0);

	g_object_unref (folder);

	test_pop3_teardown (session, service);
}

static void
test_connect_tls (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	GError *error = NULL;
	gboolean success;

	test_pop3_purge_mailbox ("test-tls-purge");

	session = test_pop3_session_new ();
	service = test_pop3_create_service_full (session, "test-tls",
		CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT);
	store = CAMEL_STORE (service);

	test_pop3_connect_service (service);

	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 0);

	g_object_unref (folder);

	test_pop3_teardown (session, service);
}

static void
test_empty_inbox (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	GError *error = NULL;
	gboolean success;

	test_pop3_purge_mailbox ("test-empty-purge");

	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-empty");
	store = CAMEL_STORE (service);

	test_pop3_connect_service (service);

	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 0);

	g_object_unref (folder);

	test_pop3_teardown (session, service);
}

static void
test_wrong_password_rejected (void)
{
	CamelSession *session;
	CamelService *service;
	GError *error = NULL;
	gboolean success;

	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-wrong-password");

	camel_service_set_password (service, "definitely-not-the-password");

	success = camel_service_connect_sync (service, NULL, &error);
	g_assert_false (success);
	g_assert_nonnull (error);
	g_clear_error (&error);

	test_flush_main_context ();

	camel_session_remove_service (session, service);
	test_flush_main_context ();
	g_object_unref (service);
	test_flush_main_context ();
	g_object_unref (session);
	test_flush_main_context ();
}

static void
test_invalid_folder_name (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	GError *error = NULL;

	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-invalid-folder");
	store = CAMEL_STORE (service);

	test_pop3_connect_service (service);

	folder = camel_store_get_folder_sync (store, "bogus", 0, NULL, &error);
	g_assert_null (folder);
	g_assert_error (error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID);
	g_clear_error (&error);

	/* Folder name matching is case-insensitive. */
	folder = camel_store_get_folder_sync (store, "InBoX", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);
	g_object_unref (folder);

	test_pop3_teardown (session, service);
}

static void
test_no_folder_hierarchy (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolderInfo *fi;
	GError *error = NULL;

	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-no-hierarchy");
	store = CAMEL_STORE (service);

	test_pop3_connect_service (service);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_null (fi);
	g_assert_error (error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER);
	g_clear_error (&error);

	test_pop3_teardown (session, service);
}

static void
test_fetch_message (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelMimeMessage *fetched;
	CamelDataWrapper *content;
	CamelStream *stream;
	GByteArray *byte_array;
	gchar *uid;
	GError *error = NULL;
	const gchar *subject = "Fetch Test Subject";
	const gchar *body = "This is the test body for fetch.";

	test_pop3_purge_mailbox ("test-fetch-purge");
	inject_message_into_maildir (subject, body);

	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-fetch");
	store = CAMEL_STORE (service);

	test_pop3_connect_service (service);

	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 1);

	uid = get_single_uid (folder);

	fetched = camel_folder_get_message_sync (folder, uid, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fetched);
	g_assert_cmpstr (camel_mime_message_get_subject (fetched), ==, subject);

	content = camel_medium_get_content (CAMEL_MEDIUM (fetched));
	g_assert_nonnull (content);

	byte_array = g_byte_array_new ();
	stream = camel_stream_mem_new_with_byte_array (byte_array);
	camel_data_wrapper_decode_to_stream_sync (content, stream, NULL, &error);
	g_assert_no_error (error);

	g_assert_cmpint (byte_array->len, >, 0);
	g_assert_true (memmem (byte_array->data, byte_array->len, body, strlen (body)) != NULL);

	g_object_unref (stream);
	g_object_unref (fetched);
	g_free (uid);
	g_object_unref (folder);

	test_pop3_teardown (session, service);
}

static void
test_uid_stable_across_reconnect (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	gchar *uid_a;
	gchar *uid_b;
	GError *error = NULL;

	test_pop3_purge_mailbox ("test-uid-purge");
	inject_message_into_maildir ("UID Stability Test", "Body.\n");

	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-uid-1");
	store = CAMEL_STORE (service);

	test_pop3_connect_service (service);

	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	uid_a = get_single_uid (folder);
	g_object_unref (folder);

	test_pop3_reconnect_service (session, &service, "test-uid-2");
	store = CAMEL_STORE (service);

	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	uid_b = get_single_uid (folder);

	g_assert_cmpstr (uid_a, ==, uid_b);

	g_free (uid_b);
	g_free (uid_a);
	g_object_unref (folder);

	test_pop3_teardown (session, service);
}

static void
test_multiple_messages (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	GPtrArray *uids;
	gchar *uid_a;
	gchar *uid_b;
	gchar *uid_c;
	GError *error = NULL;

	test_pop3_purge_mailbox ("test-multi-purge");
	inject_message_into_maildir ("Multi Message A", "Body A.\n");
	inject_message_into_maildir ("Multi Message B", "Body B.\n");
	inject_message_into_maildir ("Multi Message C", "Body C.\n");

	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-multi");
	store = CAMEL_STORE (service);

	test_pop3_connect_service (service);

	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 3);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpint (uids->len, ==, 3);

	uid_a = find_uid_by_subject (folder, uids, "Multi Message A");
	uid_b = find_uid_by_subject (folder, uids, "Multi Message B");
	uid_c = find_uid_by_subject (folder, uids, "Multi Message C");

	g_assert_nonnull (uid_a);
	g_assert_nonnull (uid_b);
	g_assert_nonnull (uid_c);
	g_assert_cmpstr (uid_a, !=, uid_b);
	g_assert_cmpstr (uid_b, !=, uid_c);

	g_free (uid_c);
	g_free (uid_b);
	g_free (uid_a);
	g_ptr_array_unref (uids);
	g_object_unref (folder);

	test_pop3_teardown (session, service);
}

static void
test_invalid_uid_fetch (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelMimeMessage *fetched;
	GError *error = NULL;

	test_pop3_purge_mailbox ("test-invalid-uid-purge");

	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-invalid-uid");
	store = CAMEL_STORE (service);

	test_pop3_connect_service (service);

	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	fetched = camel_folder_get_message_sync (folder, "no-such-uid", NULL, &error);
	g_assert_null (fetched);
	g_assert_error (error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID_UID);
	g_clear_error (&error);

	g_object_unref (folder);

	test_pop3_teardown (session, service);
}

static void
test_offline_operations (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelMimeMessage *fetched;
	gchar *uid;
	GError *error = NULL;
	gboolean success;

	test_pop3_purge_mailbox ("test-offline-purge");
	inject_message_into_maildir ("Offline Test", "Offline test body.\n");

	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-offline");
	store = CAMEL_STORE (service);

	test_pop3_connect_service (service);

	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	uid = get_single_uid (folder);

	success = camel_service_disconnect_sync (service, TRUE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	test_flush_main_context ();

	/* Merely being disconnected isn't enough: camel_folder_*_sync()
	 * calls transparently reconnect a dropped connection as long as the
	 * session is online (see folder_maybe_connect_sync() in
	 * camel-folder.c). The "must be working online" checks in the POP3
	 * provider only trigger once the session itself is offline too. */
	camel_session_set_online (session, FALSE);

	fetched = camel_folder_get_message_sync (folder, uid, NULL, &error);
	g_assert_null (fetched);
	g_assert_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE);
	g_clear_error (&error);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_false (success);
	g_assert_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE);
	g_clear_error (&error);

	g_free (uid);
	g_object_unref (folder);

	test_pop3_teardown_after_expunge (session, service);
}

static void
test_delete_and_expunge (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelSettings *settings;
	GPtrArray *uids;
	gchar *delete_uid;
	gchar *keep_uid;
	GError *error = NULL;
	gboolean success;

	test_pop3_purge_mailbox ("test-delete-purge");
	inject_message_into_maildir ("Keep Me", "Keep body.\n");
	inject_message_into_maildir ("Delete Me", "Delete body.\n");

	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-delete-1");
	store = CAMEL_STORE (service);

	/* Explicit, even though FALSE differs from the library default of
	 * TRUE: this test wants messages to actually leave the server. */
	settings = camel_service_ref_settings (service);
	g_object_set (settings, "keep-on-server", FALSE, NULL);
	g_object_unref (settings);

	test_pop3_connect_service (service);

	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 2);

	uids = camel_folder_dup_uids (folder);
	delete_uid = find_uid_by_subject (folder, uids, "Delete Me");
	g_assert_nonnull (delete_uid);

	camel_folder_set_message_flags (folder, delete_uid, CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);

	success = camel_folder_synchronize_sync (folder, TRUE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_free (delete_uid);
	g_ptr_array_unref (uids);
	g_object_unref (folder);

	test_pop3_teardown_after_expunge (session, service);

	/* Verify from a fresh connection that the deletion really reached
	 * the server, rather than only the local in-memory folder state. */
	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-delete-2");
	store = CAMEL_STORE (service);

	test_pop3_connect_service (service);

	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 1);

	uids = camel_folder_dup_uids (folder);
	g_assert_null (find_uid_by_subject (folder, uids, "Delete Me"));

	keep_uid = find_uid_by_subject (folder, uids, "Keep Me");
	g_assert_nonnull (keep_uid);
	g_free (keep_uid);

	g_ptr_array_unref (uids);
	g_object_unref (folder);

	test_pop3_teardown (session, service);
}

static void
test_keep_on_server_skips_delete (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelSettings *settings;
	gchar *uid;
	GError *error = NULL;
	gboolean success;

	test_pop3_purge_mailbox ("test-keep-purge");
	inject_message_into_maildir ("Persistent Message", "Persistent body.\n");

	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-keep-1");
	store = CAMEL_STORE (service);

	settings = camel_service_ref_settings (service);
	g_object_set (settings, "keep-on-server", TRUE, "delete-expunged", FALSE, NULL);
	g_object_unref (settings);

	test_pop3_connect_service (service);

	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	uid = get_single_uid (folder);

	camel_folder_set_message_flags (folder, uid, CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);

	success = camel_folder_synchronize_sync (folder, TRUE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_service_get_connection_status (service), ==, CAMEL_SERVICE_CONNECTED);

	g_free (uid);
	g_object_unref (folder);

	test_pop3_teardown (session, service);

	/* Confirm from a fresh connection that the message really is still on the server. */
	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-keep-2");
	store = CAMEL_STORE (service);

	test_pop3_connect_service (service);

	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 1);

	g_object_unref (folder);

	test_pop3_teardown (session, service);
}

static void
test_disable_extensions_fallback_uid (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelSettings *settings;
	CamelMimeMessage *fetched;
	gchar *uid;
	GError *error = NULL;
	const gchar *subject = "No Extensions Test";

	test_pop3_purge_mailbox ("test-noext-purge");
	inject_message_into_maildir (subject, "Body without UIDL.\n");

	session = test_pop3_session_new ();
	service = test_pop3_create_service (session, "test-noext");
	store = CAMEL_STORE (service);

	settings = camel_service_ref_settings (service);
	g_object_set (settings, "disable-extensions", TRUE, NULL);
	g_object_unref (settings);

	test_pop3_connect_service (service);

	folder = camel_store_get_folder_sync (store, "inbox", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 1);

	/* With UIDL disabled, the folder synthesizes a uid from an MD5 of
	 * the message's headers (see cmd_builduid in camel-pop3-folder.c)
	 * instead of trusting a server-provided UIDL response. */
	uid = get_single_uid (folder);
	g_assert_nonnull (uid);
	g_assert_cmpuint (strlen (uid), >, 0);

	fetched = camel_folder_get_message_sync (folder, uid, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fetched);
	g_assert_cmpstr (camel_mime_message_get_subject (fetched), ==, subject);

	g_object_unref (fetched);
	g_free (uid);
	g_object_unref (folder);

	test_pop3_teardown (session, service);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);
	camel_test_provider_init (1, pop3_drivers);

	test_server = dovecot_test_server_new_pop3 ();
	if (!test_server) {
		g_print ("Dovecot not installed, skipping\n");
		camel_test_shutdown ();
		return 0;
	}

	g_test_add_func ("/Camel/POP3/Connect", test_connect);
	g_test_add_func ("/Camel/POP3/ConnectStartTls", test_connect_starttls);
	g_test_add_func ("/Camel/POP3/ConnectTls", test_connect_tls);
	g_test_add_func ("/Camel/POP3/EmptyInbox", test_empty_inbox);
	g_test_add_func ("/Camel/POP3/WrongPasswordRejected", test_wrong_password_rejected);
	g_test_add_func ("/Camel/POP3/InvalidFolderName", test_invalid_folder_name);
	g_test_add_func ("/Camel/POP3/NoFolderHierarchy", test_no_folder_hierarchy);
	g_test_add_func ("/Camel/POP3/FetchMessage", test_fetch_message);
	g_test_add_func ("/Camel/POP3/UidStableAcrossReconnect", test_uid_stable_across_reconnect);
	g_test_add_func ("/Camel/POP3/MultipleMessages", test_multiple_messages);
	g_test_add_func ("/Camel/POP3/InvalidUidFetch", test_invalid_uid_fetch);
	g_test_add_func ("/Camel/POP3/OfflineOperations", test_offline_operations);
	g_test_add_func ("/Camel/POP3/DeleteAndExpunge", test_delete_and_expunge);
	g_test_add_func ("/Camel/POP3/KeepOnServerSkipsDelete", test_keep_on_server_skips_delete);
	g_test_add_func ("/Camel/POP3/DisableExtensionsFallbackUid", test_disable_extensions_fallback_uid);

	ret = g_test_run ();

	dovecot_test_server_free (test_server);
	test_server = NULL;

	camel_test_shutdown ();

	return ret;
}

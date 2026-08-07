/*
 * SPDX-FileCopyrightText: (C) 2026 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <camel/camel.h>

#include "camel-test.h"
#include "camel-test-provider.h"

typedef struct _ExternalServer {
	gchar *host;
	guint16 port;
	guint16 tls_port;
	gchar *user;
	gchar *password;
	CamelNetworkSecurityMethod security_method;
	gchar *group;
	gchar *group2;
} ExternalServer;

typedef struct _NntpTestSession NntpTestSession;
typedef struct _NntpTestSessionClass NntpTestSessionClass;

struct _NntpTestSession {
	CamelSession parent;
};

struct _NntpTestSessionClass {
	CamelSessionClass parent_class;
};

GType nntp_test_session_get_type (void);

G_DEFINE_TYPE (NntpTestSession, nntp_test_session, CAMEL_TYPE_SESSION)

static gboolean
nntp_test_session_authenticate_sync (CamelSession *session,
				     CamelService *service,
				     const gchar *mechanism,
				     GCancellable *cancellable,
				     GError **error)
{
	CamelAuthenticationResult result;

	result = camel_service_authenticate_sync (service, mechanism, cancellable, error);

	return result == CAMEL_AUTHENTICATION_ACCEPTED;
}

static CamelCertTrust
nntp_test_session_trust_prompt (CamelSession *session,
				CamelService *service,
				GTlsCertificate *certificate,
				GTlsCertificateFlags errors)
{
	return CAMEL_CERT_TRUST_FULLY;
}

static void
nntp_test_session_class_init (NntpTestSessionClass *klass)
{
	CamelSessionClass *session_class;

	session_class = CAMEL_SESSION_CLASS (klass);
	session_class->authenticate_sync = nntp_test_session_authenticate_sync;
	session_class->trust_prompt = nntp_test_session_trust_prompt;
}

static void
nntp_test_session_init (NntpTestSession *session)
{
}

static ExternalServer *external_server = NULL;
static const gchar *nntp_drivers[] = { "nntp" };

/*
 * External server configuration file format (GKeyFile / .ini style):
 *
 *   [Server]
 *   host=news.example.com
 *   port=11119
 *   tls-port=11129
 *   group=camel.test.group
 *   group2=camel.test.group2
 *   user=testuser
 *   password=testpass
 *   security-method=none
 *
 * Required keys: host, port, tls-port, group, group2
 * Optional keys: user, password (default: unset, i.e. anonymous access),
 *                security-method (none | ssl | starttls; default: none)
 *
 * Unlike the IMAPx provider test, there is no local/embedded NNTP server
 * fallback: a real NNTP server (e.g. INN) needs a newsgroup created ahead
 * of time by an administrative tool (ctlinnd), which is outside what an
 * NNTP client can do for itself.
 */
static void
parse_use_server_arg (gint *argc,
		      gchar ***argv)
{
	GKeyFile *key_file;
	gchar *filename = NULL;
	gchar *security_str;
	GError *error = NULL;
	gint ii;

	for (ii = 1; ii < *argc; ii++) {
		if (g_strcmp0 ((*argv)[ii], "--use-server") == 0 && ii + 1 < *argc) {
			filename = g_strdup ((*argv)[ii + 1]);
			memmove (&(*argv)[ii], &(*argv)[ii + 2],
				(*argc - ii - 1) * sizeof (gchar *));
			*argc -= 2;
			break;
		}
	}

	if (!filename)
		return;

	key_file = g_key_file_new ();

	if (!g_key_file_load_from_file (key_file, filename, G_KEY_FILE_NONE, &error))
		g_error ("Failed to load server config '%s': %s", filename, error->message);

	external_server = g_new0 (ExternalServer, 1);

	external_server->host = g_key_file_get_string (key_file, "Server", "host", &error);
	if (!external_server->host)
		g_error ("Missing 'host' in [Server]: %s", error->message);

	external_server->port = (guint16) g_key_file_get_integer (key_file, "Server", "port", &error);
	if (error)
		g_error ("Missing or invalid 'port' in [Server]: %s", error->message);

	external_server->tls_port = (guint16) g_key_file_get_integer (key_file, "Server", "tls-port", &error);
	if (error)
		g_error ("Missing or invalid 'tls-port' in [Server]: %s", error->message);

	external_server->group = g_key_file_get_string (key_file, "Server", "group", &error);
	if (!external_server->group)
		g_error ("Missing 'group' in [Server]: %s", error->message);

	external_server->group2 = g_key_file_get_string (key_file, "Server", "group2", &error);
	if (!external_server->group2)
		g_error ("Missing 'group2' in [Server]: %s", error->message);

	external_server->user = g_key_file_get_string (key_file, "Server", "user", NULL);
	external_server->password = g_key_file_get_string (key_file, "Server", "password", NULL);

	security_str = g_key_file_get_string (key_file, "Server", "security-method", NULL);
	if (!security_str || g_ascii_strcasecmp (security_str, "none") == 0)
		external_server->security_method = CAMEL_NETWORK_SECURITY_METHOD_NONE;
	else if (g_ascii_strcasecmp (security_str, "ssl") == 0)
		external_server->security_method = CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT;
	else if (g_ascii_strcasecmp (security_str, "starttls") == 0)
		external_server->security_method = CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT;
	else
		g_error ("Unknown security-method '%s' (expected: none, ssl, starttls)", security_str);
	g_free (security_str);

	g_key_file_free (key_file);
	g_free (filename);
}

static void
external_server_free (ExternalServer *server)
{
	if (!server)
		return;

	g_free (server->host);
	g_free (server->user);
	g_free (server->password);
	g_free (server->group);
	g_free (server->group2);
	g_free (server);
}

static CamelSession *
test_nntp_session_new (void)
{
	return g_object_new (
		nntp_test_session_get_type (),
		"user-data-dir", camel_test_get_dir (),
		"user-cache-dir", camel_test_get_dir (),
		NULL);
}

static CamelService *
test_nntp_create_service (CamelSession *session,
			  const gchar *uid)
{
	CamelService *service;
	CamelSettings *settings;
	GError *error = NULL;

	service = camel_session_add_service (session, uid, "nntp", CAMEL_PROVIDER_STORE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (service);

	settings = camel_service_ref_settings (service);

	camel_network_settings_set_host (CAMEL_NETWORK_SETTINGS (settings), external_server->host);
	camel_network_settings_set_port (CAMEL_NETWORK_SETTINGS (settings), external_server->port);
	camel_network_settings_set_security_method (CAMEL_NETWORK_SETTINGS (settings), external_server->security_method);

	if (external_server->user && *external_server->user)
		camel_network_settings_set_user (CAMEL_NETWORK_SETTINGS (settings), external_server->user);

	g_object_unref (settings);

	if (external_server->password && *external_server->password)
		camel_service_set_password (service, external_server->password);

	return service;
}

static void
test_flush_main_context (void)
{
	while (g_main_context_iteration (NULL, FALSE)) {
	}
}

static void
test_nntp_connect_service (CamelService *service)
{
	GError *error = NULL;
	gboolean success;

	success = camel_offline_store_set_online_sync (CAMEL_OFFLINE_STORE (service), TRUE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	success = camel_service_connect_sync (service, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	test_flush_main_context ();
}

static void
test_nntp_disconnect_service (CamelService *service)
{
	GError *error = NULL;
	gboolean success;

	success = camel_service_disconnect_sync (service, TRUE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	if (CAMEL_IS_NETWORK_SERVICE (service))
		camel_network_service_can_reach_sync (CAMEL_NETWORK_SERVICE (service), NULL, NULL);

	test_flush_main_context ();
}

static void
test_nntp_teardown (CamelSession *session,
		    CamelService *service)
{
	test_nntp_disconnect_service (service);
	camel_session_remove_service (session, service);
	test_flush_main_context ();
	g_object_unref (service);
	test_flush_main_context ();
	g_object_unref (session);
	test_flush_main_context ();
}

static gboolean
test_folder_info_contains (CamelFolderInfo *fi,
			   const gchar *full_name)
{
	while (fi) {
		if (g_strcmp0 (fi->full_name, full_name) == 0)
			return TRUE;
		if (fi->child && test_folder_info_contains (fi->child, full_name))
			return TRUE;
		fi = fi->next;
	}

	return FALSE;
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
	camel_internet_address_add (addr, "Camel Test", "camel-test@example.com");
	camel_mime_message_set_from (msg, addr);
	g_object_unref (addr);

	camel_mime_part_set_content (CAMEL_MIME_PART (msg), body, strlen (body), "text/plain");

	return msg;
}

/* Newsgroups can't be created/cleared by a client, so tests share one
 * long-lived group; find messages by a per-run-unique Subject rather than
 * assuming a known message count or UID. */
static gchar *
find_uid_by_subject (CamelFolder *folder,
		     GPtrArray *uids,
		     const gchar *subject)
{
	guint ii;

	for (ii = 0; ii < uids->len; ii++) {
		CamelMessageInfo *info;
		const gchar *uid = g_ptr_array_index (uids, ii);
		gboolean matches;

		info = camel_folder_get_message_info (folder, uid);
		if (!info)
			continue;

		matches = g_strcmp0 (camel_message_info_get_subject (info), subject) == 0;
		g_clear_object (&info);

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

	session = test_nntp_session_new ();
	service = test_nntp_create_service (session, "test-connect");

	test_nntp_connect_service (service);

	test_nntp_teardown (session, service);
}

static void
test_connect_starttls (void)
{
	CamelSession *session;
	CamelService *service;
	CamelSettings *settings;
	CamelStore *store;
	CamelFolderInfo *fi;
	GError *error = NULL;

	session = test_nntp_session_new ();
	service = test_nntp_create_service (session, "test-connect-starttls");
	store = CAMEL_STORE (service);

	settings = camel_service_ref_settings (service);
	camel_network_settings_set_security_method (CAMEL_NETWORK_SETTINGS (settings),
		CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT);
	g_object_unref (settings);

	test_nntp_connect_service (service);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	g_assert_true (test_folder_info_contains (fi, external_server->group));
	camel_folder_info_free (fi);

	test_nntp_teardown (session, service);
}

static void
test_connect_tls (void)
{
	CamelSession *session;
	CamelService *service;
	CamelSettings *settings;
	CamelStore *store;
	CamelFolderInfo *fi;
	GError *error = NULL;

	session = test_nntp_session_new ();
	service = test_nntp_create_service (session, "test-connect-tls");
	store = CAMEL_STORE (service);

	settings = camel_service_ref_settings (service);
	camel_network_settings_set_port (CAMEL_NETWORK_SETTINGS (settings), external_server->tls_port);
	camel_network_settings_set_security_method (CAMEL_NETWORK_SETTINGS (settings),
		CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT);
	g_object_unref (settings);

	test_nntp_connect_service (service);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	g_assert_true (test_folder_info_contains (fi, external_server->group));
	camel_folder_info_free (fi);

	test_nntp_teardown (session, service);
}

static void
test_list_groups (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolderInfo *fi;
	GError *error = NULL;

	session = test_nntp_session_new ();
	service = test_nntp_create_service (session, "test-list-groups");
	store = CAMEL_STORE (service);

	test_nntp_connect_service (service);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);

	g_assert_true (test_folder_info_contains (fi, external_server->group));

	camel_folder_info_free (fi);

	test_nntp_teardown (session, service);
}

static void
test_subscription_state (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolderInfo *fi;
	GError *error = NULL;
	gboolean success;
	gboolean was_subscribed;

	session = test_nntp_session_new ();
	service = test_nntp_create_service (session, "test-sub-state");
	store = CAMEL_STORE (service);

	test_nntp_connect_service (service);

	/* subscribe/unsubscribe look the group up in the store's local
	 * summary, which is only populated by a LIST (get_folder_info_sync) */
	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	camel_folder_info_free (fi);

	was_subscribed = camel_subscribable_folder_is_subscribed (CAMEL_SUBSCRIBABLE (store), external_server->group);

	if (!was_subscribed) {
		success = camel_subscribable_subscribe_folder_sync (CAMEL_SUBSCRIBABLE (store), external_server->group, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);
	}

	g_assert_true (camel_subscribable_folder_is_subscribed (CAMEL_SUBSCRIBABLE (store), external_server->group));

	success = camel_subscribable_unsubscribe_folder_sync (CAMEL_SUBSCRIBABLE (store), external_server->group, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_false (camel_subscribable_folder_is_subscribed (CAMEL_SUBSCRIBABLE (store), external_server->group));

	success = camel_subscribable_subscribe_folder_sync (CAMEL_SUBSCRIBABLE (store), external_server->group, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_true (camel_subscribable_folder_is_subscribed (CAMEL_SUBSCRIBABLE (store), external_server->group));

	test_nntp_teardown (session, service);
}

static void
test_post_and_fetch_message (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderInfo *fi;
	CamelMimeMessage *msg;
	CamelMimeMessage *fetched;
	CamelDataWrapper *content;
	CamelStream *stream;
	GByteArray *byte_array;
	GPtrArray *uids;
	gchar *subject;
	gchar *uid;
	GError *error = NULL;
	gboolean success;
	const gchar *body = "This is the Camel NNTP provider test body.";

	session = test_nntp_session_new ();
	service = test_nntp_create_service (session, "test-post-fetch");
	store = CAMEL_STORE (service);

	test_nntp_connect_service (service);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	camel_folder_info_free (fi);

	folder = camel_store_get_folder_sync (store, external_server->group, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	subject = g_strdup_printf ("Camel NNTP Test %08x", g_test_rand_int ());
	msg = test_create_message (subject, body);

	success = camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	/* camel_nntp_command() only re-issues GROUP (which is what pulls in
	 * new articles) when the target group differs from the connection's
	 * cached "current group" -- so this should be the first refresh done
	 * on this connection/folder pair, not a second one after an earlier
	 * refresh already selected the same group. */
	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpint (uids->len, >, 0);

	uid = find_uid_by_subject (folder, uids, subject);
	g_assert_nonnull (uid);

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
	g_ptr_array_unref (uids);
	g_free (subject);
	g_object_unref (folder);

	test_nntp_teardown (session, service);
}

static void
test_reconnect_after_disconnect (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderInfo *fi;
	CamelMimeMessage *msg;
	CamelMimeMessage *fetched;
	GPtrArray *uids;
	gchar *subject_a;
	gchar *subject_b;
	gchar *uid_a;
	gchar *uid_b;
	GError *error = NULL;
	gboolean success;
	const gchar *body = "Reconnect test body.";

	session = test_nntp_session_new ();
	service = test_nntp_create_service (session, "test-reconnect");
	store = CAMEL_STORE (service);

	test_nntp_connect_service (service);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	camel_folder_info_free (fi);

	folder = camel_store_get_folder_sync (store, external_server->group, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	subject_a = g_strdup_printf ("Camel NNTP Reconnect A %08x", g_test_rand_int ());
	msg = test_create_message (subject_a, body);
	success = camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	/* First refresh on this connection: GROUP gets selected for the
	 * first time here, so this is guaranteed to pick up article A. */
	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder);
	uid_a = find_uid_by_subject (folder, uids, subject_a);
	g_assert_nonnull (uid_a);

	fetched = camel_folder_get_message_sync (folder, uid_a, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (camel_mime_message_get_subject (fetched), ==, subject_a);
	g_clear_object (&fetched);
	g_ptr_array_unref (uids);

	/* Simulate the server (or a proactive idle-disconnect) dropping the
	 * connection between two user actions. camel_folder_*_sync() calls
	 * are expected to transparently reconnect via folder_maybe_connect_sync(). */
	success = camel_service_disconnect_sync (CAMEL_SERVICE (service), TRUE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_service_get_connection_status (CAMEL_SERVICE (service)), !=, CAMEL_SERVICE_CONNECTED);

	subject_b = g_strdup_printf ("Camel NNTP Reconnect B %08x", g_test_rand_int ());
	msg = test_create_message (subject_b, body);
	success = camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	g_assert_cmpint (camel_service_get_connection_status (CAMEL_SERVICE (service)), ==, CAMEL_SERVICE_CONNECTED);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder);
	uid_b = find_uid_by_subject (folder, uids, subject_b);
	g_assert_nonnull (uid_b);

	fetched = camel_folder_get_message_sync (folder, uid_b, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (camel_mime_message_get_subject (fetched), ==, subject_b);

	g_object_unref (fetched);
	g_ptr_array_unref (uids);
	g_free (uid_b);
	g_free (uid_a);
	g_free (subject_b);
	g_free (subject_a);
	g_object_unref (folder);

	test_nntp_teardown (session, service);
}

/* create/rename/delete/transfer are all structurally unsupported by NNTP
 * (newsgroups are server-administered, not client-managed), and the
 * provider is expected to fail them with a clear error rather than
 * silently no-op or crash. */
static void
test_unsupported_operations (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder_a;
	CamelFolder *folder_b;
	CamelFolderInfo *fi;
	CamelMimeMessage *msg;
	GPtrArray *uids;
	GPtrArray *transferred_uids = NULL;
	gchar *subject;
	GError *error = NULL;
	gboolean success;

	session = test_nntp_session_new ();
	service = test_nntp_create_service (session, "test-unsupported");
	store = CAMEL_STORE (service);

	test_nntp_connect_service (service);

	fi = camel_store_create_folder_sync (store, "", "camel.test.new", NULL, &error);
	g_assert_null (fi);
	g_assert_error (error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID);
	g_clear_error (&error);

	g_assert_false (camel_store_rename_folder_sync (store, external_server->group, "camel.test.renamed", NULL, &error));
	g_assert_error (error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID);
	g_clear_error (&error);

	g_assert_false (camel_store_delete_folder_sync (store, external_server->group, NULL, &error));
	g_assert_error (error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID);
	g_clear_error (&error);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	camel_folder_info_free (fi);

	folder_a = camel_store_get_folder_sync (store, external_server->group, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder_a);

	/* transfer's generic camel_folder_*_sync() wrapper short-circuits to
	 * success for an empty UID array or same source/destination folder,
	 * before ever reaching the NNTP-specific rejection -- so a real UID
	 * and a genuinely different destination folder are needed to
	 * actually exercise nntp_folder_transfer_messages_to_sync(). */
	folder_b = camel_store_get_folder_sync (store, external_server->group2, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder_b);

	subject = g_strdup_printf ("Camel NNTP Transfer Test %08x", g_test_rand_int ());
	msg = test_create_message (subject, "Transfer rejection test body.");
	success = camel_folder_append_message_sync (folder_a, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	success = camel_folder_refresh_info_sync (folder_a, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder_a);
	g_assert_cmpint (uids->len, >, 0);

	g_assert_false (camel_folder_transfer_messages_to_sync (
		folder_a, uids, folder_b, FALSE, &transferred_uids, NULL, &error));
	g_assert_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE);
	g_clear_error (&error);
	g_assert_null (transferred_uids);

	g_ptr_array_unref (uids);
	g_free (subject);
	g_object_unref (folder_b);
	g_object_unref (folder_a);

	test_nntp_teardown (session, service);
}

static void
test_subscribed_only_listing (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolderInfo *fi;
	GError *error = NULL;
	gboolean success;

	session = test_nntp_session_new ();
	service = test_nntp_create_service (session, "test-sub-listing");
	store = CAMEL_STORE (service);

	test_nntp_connect_service (service);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	camel_folder_info_free (fi);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_SUBSCRIBED, NULL, &error);
	g_assert_no_error (error);
	g_assert_false (test_folder_info_contains (fi, external_server->group));
	g_clear_pointer (&fi, camel_folder_info_free);

	success = camel_subscribable_subscribe_folder_sync (CAMEL_SUBSCRIBABLE (store), external_server->group, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_SUBSCRIBED, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (test_folder_info_contains (fi, external_server->group));
	g_clear_pointer (&fi, camel_folder_info_free);

	success = camel_subscribable_unsubscribe_folder_sync (CAMEL_SUBSCRIBABLE (store), external_server->group, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_SUBSCRIBED, NULL, &error);
	g_assert_no_error (error);
	g_assert_false (test_folder_info_contains (fi, external_server->group));
	g_clear_pointer (&fi, camel_folder_info_free);

	test_nntp_teardown (session, service);
}

static void
test_invalid_article_fetch (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderInfo *fi;
	CamelMimeMessage *fetched;
	GError *error = NULL;
	gchar *bogus_uid;

	session = test_nntp_session_new ();
	service = test_nntp_create_service (session, "test-invalid-article");
	store = CAMEL_STORE (service);

	test_nntp_connect_service (service);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	camel_folder_info_free (fi);

	folder = camel_store_get_folder_sync (store, external_server->group, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	bogus_uid = g_strdup_printf ("999999999,<nonexistent-%08x@camel-test>", g_test_rand_int ());

	fetched = camel_folder_get_message_sync (folder, bogus_uid, NULL, &error);
	g_assert_null (fetched);
	g_assert_error (error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID_UID);
	g_clear_error (&error);

	g_free (bogus_uid);
	g_object_unref (folder);

	test_nntp_teardown (session, service);
}

static void
test_local_message_flags (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderInfo *fi;
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	GPtrArray *uids;
	gchar *subject;
	gchar *uid;
	guint32 flags;
	GError *error = NULL;
	gboolean success;
	const gchar *body = "Local flags test body.";

	session = test_nntp_session_new ();
	service = test_nntp_create_service (session, "test-flags");
	store = CAMEL_STORE (service);

	test_nntp_connect_service (service);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	camel_folder_info_free (fi);

	folder = camel_store_get_folder_sync (store, external_server->group, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	subject = g_strdup_printf ("Camel NNTP Flags Test %08x", g_test_rand_int ());
	msg = test_create_message (subject, body);
	success = camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder);
	uid = find_uid_by_subject (folder, uids, subject);
	g_assert_nonnull (uid);

	info = camel_folder_get_message_info (folder, uid);
	g_assert_nonnull (info);
	flags = camel_message_info_get_flags (info);
	g_assert_false ((flags & (CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_FLAGGED)) != 0);
	g_clear_object (&info);

	camel_folder_set_message_flags (folder, uid,
		CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_FLAGGED,
		CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_FLAGGED);

	success = camel_folder_synchronize_sync (folder, FALSE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	info = camel_folder_get_message_info (folder, uid);
	g_assert_nonnull (info);
	flags = camel_message_info_get_flags (info);
	g_assert_true ((flags & CAMEL_MESSAGE_SEEN) != 0);
	g_assert_true ((flags & CAMEL_MESSAGE_FLAGGED) != 0);
	g_clear_object (&info);

	g_free (uid);
	g_ptr_array_unref (uids);
	g_free (subject);
	g_object_unref (folder);

	test_nntp_teardown (session, service);
}

static void
test_group_switching (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder1;
	CamelFolder *folder2;
	CamelFolderInfo *fi;
	CamelMimeMessage *msg;
	GPtrArray *uids;
	gchar *subject1;
	gchar *subject2;
	gchar *uid1;
	gchar *uid2;
	GError *error = NULL;
	gboolean success;
	const gchar *body = "Group switching test body.";

	session = test_nntp_session_new ();
	service = test_nntp_create_service (session, "test-group-switch");
	store = CAMEL_STORE (service);

	test_nntp_connect_service (service);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	g_assert_true (test_folder_info_contains (fi, external_server->group2));
	camel_folder_info_free (fi);

	folder1 = camel_store_get_folder_sync (store, external_server->group, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder1);

	folder2 = camel_store_get_folder_sync (store, external_server->group2, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder2);

	subject1 = g_strdup_printf ("Camel NNTP Switch 1 %08x", g_test_rand_int ());
	msg = test_create_message (subject1, body);
	success = camel_folder_append_message_sync (folder1, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	subject2 = g_strdup_printf ("Camel NNTP Switch 2 %08x", g_test_rand_int ());
	msg = test_create_message (subject2, body);
	success = camel_folder_append_message_sync (folder2, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	/* Select folder1 (first GROUP on this connection), then folder2 (a
	 * different group -> should re-issue GROUP), then back to folder1 --
	 * each refresh should reselect since the "current group" alternates. */
	success = camel_folder_refresh_info_sync (folder1, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder1);
	uid1 = find_uid_by_subject (folder1, uids, subject1);
	g_assert_nonnull (uid1);
	g_free (uid1);
	g_ptr_array_unref (uids);

	success = camel_folder_refresh_info_sync (folder2, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder2);
	uid2 = find_uid_by_subject (folder2, uids, subject2);
	g_assert_nonnull (uid2);
	g_free (uid2);
	g_ptr_array_unref (uids);

	/* Back to folder1: should reselect its GROUP again, or a second
	 * message appended there afterward would never be noticed. */
	g_free (subject1);
	subject1 = g_strdup_printf ("Camel NNTP Switch 1b %08x", g_test_rand_int ());
	msg = test_create_message (subject1, body);
	success = camel_folder_append_message_sync (folder1, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	success = camel_folder_refresh_info_sync (folder1, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder1);
	uid1 = find_uid_by_subject (folder1, uids, subject1);
	g_assert_nonnull (uid1);

	g_free (uid1);
	g_ptr_array_unref (uids);
	g_free (subject2);
	g_free (subject1);
	g_object_unref (folder2);
	g_object_unref (folder1);

	test_nntp_teardown (session, service);
}

static gboolean
test_wait_for_connection_status (CamelService *service,
				 CamelServiceConnectionStatus want_status,
				 gboolean want_equal,
				 gint64 timeout_seconds)
{
	gint64 deadline;
	gboolean matches;

	deadline = g_get_monotonic_time () + timeout_seconds * G_USEC_PER_SEC;
	matches = (camel_service_get_connection_status (service) == want_status) == want_equal;

	while (!matches && g_get_monotonic_time () < deadline) {
		while (g_main_context_iteration (NULL, FALSE)) {
		}

		g_usleep (50000);

		matches = (camel_service_get_connection_status (service) == want_status) == want_equal;
	}

	return matches;
}

static void
test_disconnect_after_idle (void)
{
	CamelSession *session;
	CamelService *service;
	CamelSettings *settings;
	CamelStore *store;
	CamelFolderInfo *fi;
	GError *error = NULL;

	session = test_nntp_session_new ();
	service = test_nntp_create_service (session, "test-disconnect-after-idle");
	store = CAMEL_STORE (service);

	settings = camel_service_ref_settings (service);
	g_object_set (settings, "disconnect-after-idle", 1, NULL);
	g_object_unref (settings);

	test_nntp_connect_service (service);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	camel_folder_info_free (fi);

	g_assert_cmpint (camel_service_get_connection_status (CAMEL_SERVICE (service)), ==, CAMEL_SERVICE_CONNECTED);

	g_assert_true (test_wait_for_connection_status (CAMEL_SERVICE (service), CAMEL_SERVICE_CONNECTED, FALSE, 10));

	test_nntp_teardown (session, service);
}

static void
test_disconnect_after_idle_reset_by_activity (void)
{
	CamelSession *session;
	CamelService *service;
	CamelSettings *settings;
	CamelStore *store;
	CamelFolderInfo *fi;
	GError *error = NULL;

	session = test_nntp_session_new ();
	service = test_nntp_create_service (session, "test-disconnect-idle-reset");
	store = CAMEL_STORE (service);

	settings = camel_service_ref_settings (service);
	g_object_set (settings, "disconnect-after-idle", 2, NULL);
	g_object_unref (settings);

	test_nntp_connect_service (service);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	camel_folder_info_free (fi);

	/* Less than the 2-second idle timeout: activity here should reset
	 * the countdown, not just let it keep ticking from the first LIST. */
	g_usleep (G_USEC_PER_SEC);

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	camel_folder_info_free (fi);

	g_assert_cmpint (camel_service_get_connection_status (CAMEL_SERVICE (service)), ==, CAMEL_SERVICE_CONNECTED);

	/* Still well within the 2-second window since the second LIST above;
	 * a naive one-shot timer started at the first LIST would have fired
	 * by now instead of being reset. */
	g_assert_true (test_wait_for_connection_status (CAMEL_SERVICE (service), CAMEL_SERVICE_CONNECTED, TRUE, 1));

	/* Now let the (reset) countdown actually elapse. */
	g_assert_true (test_wait_for_connection_status (CAMEL_SERVICE (service), CAMEL_SERVICE_CONNECTED, FALSE, 10));

	test_nntp_teardown (session, service);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	parse_use_server_arg (&argc, &argv);

	camel_test_init (&argc, &argv);
	camel_test_provider_init (1, nntp_drivers);

	if (!external_server) {
		g_test_message ("Pass --use-server <config.conf> to run the NNTP provider tests "
			"against a real server (see the comment above parse_use_server_arg "
			"for the file format)");
		camel_test_shutdown ();
		return 0;
	}

	g_test_add_func ("/Camel/NNTP/Connect", test_connect);
	g_test_add_func ("/Camel/NNTP/ConnectStartTls", test_connect_starttls);
	g_test_add_func ("/Camel/NNTP/ConnectTls", test_connect_tls);
	g_test_add_func ("/Camel/NNTP/ListGroups", test_list_groups);
	g_test_add_func ("/Camel/NNTP/SubscriptionState", test_subscription_state);
	g_test_add_func ("/Camel/NNTP/PostAndFetchMessage", test_post_and_fetch_message);
	g_test_add_func ("/Camel/NNTP/ReconnectAfterDisconnect", test_reconnect_after_disconnect);
	g_test_add_func ("/Camel/NNTP/UnsupportedOperations", test_unsupported_operations);
	g_test_add_func ("/Camel/NNTP/SubscribedOnlyListing", test_subscribed_only_listing);
	g_test_add_func ("/Camel/NNTP/InvalidArticleFetch", test_invalid_article_fetch);
	g_test_add_func ("/Camel/NNTP/LocalMessageFlags", test_local_message_flags);
	g_test_add_func ("/Camel/NNTP/GroupSwitching", test_group_switching);
	g_test_add_func ("/Camel/NNTP/DisconnectAfterIdle", test_disconnect_after_idle);
	g_test_add_func ("/Camel/NNTP/DisconnectAfterIdleResetByActivity", test_disconnect_after_idle_reset_by_activity);

	ret = g_test_run ();

	external_server_free (external_server);
	external_server = NULL;

	camel_test_shutdown ();

	return ret;
}

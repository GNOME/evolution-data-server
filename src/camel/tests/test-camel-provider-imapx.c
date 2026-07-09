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
#include "dovecot-helper.h"

typedef struct _ExternalServer {
	gchar *host;
	guint16 port;
	gchar *user;
	gchar *password;
	CamelNetworkSecurityMethod security_method;
	gchar *folder_prefix;
} ExternalServer;

typedef struct _ImapxTestSession ImapxTestSession;
typedef struct _ImapxTestSessionClass ImapxTestSessionClass;

struct _ImapxTestSession {
	CamelSession parent;
};

struct _ImapxTestSessionClass {
	CamelSessionClass parent_class;
};

GType imapx_test_session_get_type (void);

G_DEFINE_TYPE (ImapxTestSession, imapx_test_session, CAMEL_TYPE_SESSION)

static gboolean
imapx_test_session_authenticate_sync (CamelSession *session,
				      CamelService *service,
				      const gchar *mechanism,
				      GCancellable *cancellable,
				      GError **error)
{
	CamelAuthenticationResult result;

	result = camel_service_authenticate_sync (service, mechanism, cancellable, error);

	return result == CAMEL_AUTHENTICATION_ACCEPTED;
}

static void
imapx_test_session_class_init (ImapxTestSessionClass *klass)
{
	CamelSessionClass *session_class;

	session_class = CAMEL_SESSION_CLASS (klass);
	session_class->authenticate_sync = imapx_test_session_authenticate_sync;
}

static void
imapx_test_session_init (ImapxTestSession *session)
{
}

static DovecotTestServer *test_server = NULL;
static ExternalServer *external_server = NULL;
static const gchar *imapx_drivers[] = { "imapx" };

/*
 * External server configuration file format (GKeyFile / .ini style):
 *
 *   [Server]
 *   host=imap.example.com
 *   port=143
 *   user=testuser
 *   password=testpass
 *   security-method=none
 *
 * Required keys: host, port, user, password
 * Optional keys: security-method (none | ssl | starttls; default: none)
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

	if (!filename) {
		return;
	}

	key_file = g_key_file_new ();

	if (!g_key_file_load_from_file (key_file, filename, G_KEY_FILE_NONE, &error)) {
		g_error ("Failed to load server config '%s': %s", filename, error->message);
	}

	external_server = g_new0 (ExternalServer, 1);

	external_server->host = g_key_file_get_string (key_file, "Server", "host", &error);
	if (!external_server->host) {
		g_error ("Missing 'host' in [Server]: %s", error->message);
	}

	external_server->port = (guint16) g_key_file_get_integer (key_file, "Server", "port", &error);
	if (error) {
		g_error ("Missing or invalid 'port' in [Server]: %s", error->message);
	}

	external_server->user = g_key_file_get_string (key_file, "Server", "user", &error);
	if (!external_server->user) {
		g_error ("Missing 'user' in [Server]: %s", error->message);
	}

	external_server->password = g_key_file_get_string (key_file, "Server", "password", &error);
	if (!external_server->password) {
		g_error ("Missing 'password' in [Server]: %s", error->message);
	}

	security_str = g_key_file_get_string (key_file, "Server", "security-method", NULL);
	if (!security_str || g_ascii_strcasecmp (security_str, "none") == 0) {
		external_server->security_method = CAMEL_NETWORK_SECURITY_METHOD_NONE;
	} else if (g_ascii_strcasecmp (security_str, "ssl") == 0) {
		external_server->security_method = CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT;
	} else if (g_ascii_strcasecmp (security_str, "starttls") == 0) {
		external_server->security_method = CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT;
	} else {
		g_error ("Unknown security-method '%s' (expected: none, ssl, starttls)", security_str);
	}
	g_free (security_str);

	g_key_file_free (key_file);
	g_free (filename);
}

static void
external_server_free (ExternalServer *server)
{
	if (!server) {
		return;
	}

	g_free (server->host);
	g_free (server->user);
	g_free (server->password);
	g_free (server->folder_prefix);
	g_free (server);
}

static gchar *
test_folder_path (const gchar *name)
{
	if (external_server) {
		return g_strdup_printf ("%s/%s", external_server->folder_prefix, name);
	}

	return g_strdup (name);
}

static CamelSession *
test_imapx_session_new (void)
{
	return g_object_new (
		imapx_test_session_get_type (),
		"user-data-dir", camel_test_get_dir (),
		"user-cache-dir", camel_test_get_dir (),
		NULL);
}

static CamelService *
test_imapx_create_service (CamelSession *session,
			   const gchar *uid)
{
	CamelService *service;
	CamelSettings *settings;
	const gchar *host;
	const gchar *user;
	const gchar *password;
	CamelNetworkSecurityMethod security_method;
	guint16 port;
	GError *error = NULL;

	service = camel_session_add_service (session, uid, "imapx",
		CAMEL_PROVIDER_STORE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (service);

	if (external_server) {
		host = external_server->host;
		port = external_server->port;
		user = external_server->user;
		password = external_server->password;
		security_method = external_server->security_method;
	} else {
		host = dovecot_test_server_get_host (test_server);
		port = dovecot_test_server_get_port (test_server);
		user = dovecot_test_server_get_user (test_server);
		password = dovecot_test_server_get_password (test_server);
		security_method = CAMEL_NETWORK_SECURITY_METHOD_NONE;
	}

	settings = camel_service_ref_settings (service);

	camel_network_settings_set_host (CAMEL_NETWORK_SETTINGS (settings), host);
	camel_network_settings_set_port (CAMEL_NETWORK_SETTINGS (settings), port);
	camel_network_settings_set_user (CAMEL_NETWORK_SETTINGS (settings), user);
	camel_network_settings_set_security_method (CAMEL_NETWORK_SETTINGS (settings),
		security_method);

	g_object_set (settings, "use-idle", FALSE, NULL);

	g_object_unref (settings);

	camel_service_set_password (service, password);

	return service;
}

static void
test_flush_main_context (void)
{
	while (g_main_context_iteration (NULL, FALSE)) {
	}
}

static void
test_imapx_connect_service (CamelService *service)
{
	CamelFolderInfo *fi;
	GError *error = NULL;
	gboolean success;

	success = camel_offline_store_set_online_sync (
		CAMEL_OFFLINE_STORE (service), TRUE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	success = camel_service_connect_sync (service, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	fi = camel_store_get_folder_info_sync (CAMEL_STORE (service),
		NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	camel_folder_info_free (fi);

	test_flush_main_context ();
}

static void
test_imapx_disconnect_service (CamelService *service)
{
	GError *error = NULL;
	gboolean success;

	success = camel_service_disconnect_sync (service, TRUE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	if (CAMEL_IS_NETWORK_SERVICE (service)) {
		camel_network_service_can_reach_sync (
			CAMEL_NETWORK_SERVICE (service), NULL, NULL);
	}

	test_flush_main_context ();
}

static void
test_imapx_teardown (CamelSession *session,
		     CamelService *service)
{
	test_imapx_disconnect_service (service);
	camel_session_remove_service (session, service);
	test_flush_main_context ();
	g_object_unref (service);
	test_flush_main_context ();
	g_object_unref (session);
	test_flush_main_context ();
}

static void
test_imapx_reconnect_service (CamelSession *session,
			      CamelService **inout_service,
			      const gchar *uid)
{
	CamelFolderInfo *fi;
	GError *error = NULL;

	test_imapx_disconnect_service (*inout_service);
	camel_session_remove_service (session, *inout_service);
	test_flush_main_context ();
	g_object_unref (*inout_service);
	test_flush_main_context ();

	*inout_service = test_imapx_create_service (session, uid);
	test_imapx_connect_service (*inout_service);

	fi = camel_store_get_folder_info_sync (CAMEL_STORE (*inout_service),
		NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	camel_folder_info_free (fi);
}

static gboolean
test_folder_info_contains (CamelFolderInfo *fi,
			   const gchar *full_name)
{
	while (fi) {
		if (g_strcmp0 (fi->full_name, full_name) == 0) {
			return TRUE;
		}
		if (fi->child && test_folder_info_contains (fi->child, full_name)) {
			return TRUE;
		}
		fi = fi->next;
	}

	return FALSE;
}

static gint
test_folder_info_count (CamelFolderInfo *fi)
{
	gint count = 0;

	while (fi) {
		count++;
		count += test_folder_info_count (fi->child);
		fi = fi->next;
	}

	return count;
}

static void
test_imapx_create_folder (CamelStore *store,
			  const gchar *parent,
			  const gchar *name)
{
	CamelFolderInfo *fi;
	gchar *actual_parent;
	gchar *full_name;
	GError *error = NULL;
	gboolean success;

	if (external_server) {
		if (parent && *parent) {
			actual_parent = g_strdup_printf ("%s/%s",
				external_server->folder_prefix, parent);
		} else {
			actual_parent = g_strdup (external_server->folder_prefix);
		}
	} else {
		actual_parent = g_strdup (parent ? parent : "");
	}

	fi = camel_store_create_folder_sync (store, actual_parent, name, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	camel_folder_info_free (fi);

	if (*actual_parent) {
		full_name = g_strdup_printf ("%s/%s", actual_parent, name);
	} else {
		full_name = g_strdup (name);
	}

	success = camel_subscribable_subscribe_folder_sync (
		CAMEL_SUBSCRIBABLE (store), full_name, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	test_flush_main_context ();

	g_free (full_name);
	g_free (actual_parent);
}

static void
test_imapx_delete_folder (CamelStore *store,
			  const gchar *folder_name)
{
	gchar *full_name;

	full_name = test_folder_path (folder_name);

	camel_subscribable_unsubscribe_folder_sync (
		CAMEL_SUBSCRIBABLE (store), full_name, NULL, NULL);
	camel_store_delete_folder_sync (store, full_name, NULL, NULL);
	test_flush_main_context ();

	g_free (full_name);
}

static void
test_cleanup_folder_tree (CamelStore *store,
			  CamelFolderInfo *fi)
{
	while (fi) {
		if (fi->child) {
			test_cleanup_folder_tree (store, fi->child);
		}
		camel_subscribable_unsubscribe_folder_sync (
			CAMEL_SUBSCRIBABLE (store), fi->full_name, NULL, NULL);
		camel_store_delete_folder_sync (store, fi->full_name, NULL, NULL);
		fi = fi->next;
	}
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

static CamelMimeMessage *
test_create_multipart_message (const gchar *subject)
{
	CamelMimeMessage *msg;
	CamelInternetAddress *addr;
	CamelMultipart *mp_mixed;
	CamelMultipart *mp_alt;
	CamelMimePart *part;
	const gchar *plain_text = "This is the plain text version.";
	const gchar *html_text = "<html><body><p>This is the HTML version.</p></body></html>";
	const gchar *attachment_data = "Binary attachment content here.";

	msg = camel_mime_message_new ();
	camel_mime_message_set_subject (msg, subject);
	camel_mime_message_set_date (msg, CAMEL_MESSAGE_DATE_CURRENT, 0);

	addr = camel_internet_address_new ();
	camel_internet_address_add (addr, "Test User", "test@example.com");
	camel_mime_message_set_from (msg, addr);
	camel_mime_message_set_recipients (msg, CAMEL_RECIPIENT_TYPE_TO, addr);
	g_object_unref (addr);

	mp_alt = camel_multipart_new ();
	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (mp_alt), "multipart/alternative");
	camel_multipart_set_boundary (mp_alt, NULL);

	part = camel_mime_part_new ();
	camel_mime_part_set_content (part, plain_text, strlen (plain_text), "text/plain");
	camel_multipart_add_part (mp_alt, part);
	g_object_unref (part);

	part = camel_mime_part_new ();
	camel_mime_part_set_content (part, html_text, strlen (html_text), "text/html");
	camel_multipart_add_part (mp_alt, part);
	g_object_unref (part);

	mp_mixed = camel_multipart_new ();
	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (mp_mixed), "multipart/mixed");
	camel_multipart_set_boundary (mp_mixed, NULL);

	part = camel_mime_part_new ();
	camel_medium_set_content (CAMEL_MEDIUM (part), CAMEL_DATA_WRAPPER (mp_alt));
	camel_multipart_add_part (mp_mixed, part);
	g_object_unref (part);
	g_object_unref (mp_alt);

	part = camel_mime_part_new ();
	camel_mime_part_set_content (part, attachment_data, strlen (attachment_data),
		"application/octet-stream");
	camel_mime_part_set_disposition (part, "attachment");
	camel_mime_part_set_filename (part, "test.bin");
	camel_multipart_add_part (mp_mixed, part);
	g_object_unref (part);

	camel_medium_set_content (CAMEL_MEDIUM (msg), CAMEL_DATA_WRAPPER (mp_mixed));
	g_object_unref (mp_mixed);

	return msg;
}

static void
test_connect (void)
{
	CamelSession *session;
	CamelService *service;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-connect");

	test_imapx_connect_service (service);
	test_imapx_disconnect_service (service);

	camel_session_remove_service (session, service);
	test_flush_main_context ();
	g_object_unref (service);
	test_flush_main_context ();
	g_object_unref (session);
	test_flush_main_context ();
}

static void
test_list_folders (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolderInfo *fi;
	gchar *path1;
	gchar *path2;
	gchar *path3;
	GError *error = NULL;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-list-folders");
	store = CAMEL_STORE (service);

	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "ListTest1");
	test_imapx_create_folder (store, "", "ListTest2");
	test_imapx_create_folder (store, "ListTest1", "SubFolder");

	path1 = test_folder_path ("ListTest1");
	path2 = test_folder_path ("ListTest2");
	path3 = test_folder_path ("ListTest1/SubFolder");

	fi = camel_store_get_folder_info_sync (store, NULL,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);

	g_assert_true (test_folder_info_contains (fi, "INBOX"));
	g_assert_true (test_folder_info_contains (fi, path1));
	g_assert_true (test_folder_info_contains (fi, path2));
	g_assert_true (test_folder_info_contains (fi, path3));
	g_assert_cmpint (test_folder_info_count (fi), >=, 4);

	camel_folder_info_free (fi);

	/* Verify after reconnect */
	test_imapx_reconnect_service (session, &service, "test-list-folders-2");
	store = CAMEL_STORE (service);

	fi = camel_store_get_folder_info_sync (store, NULL,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);

	g_assert_true (test_folder_info_contains (fi, "INBOX"));
	g_assert_true (test_folder_info_contains (fi, path1));
	g_assert_true (test_folder_info_contains (fi, path2));
	g_assert_true (test_folder_info_contains (fi, path3));

	camel_folder_info_free (fi);

	/* Cleanup */
	test_imapx_delete_folder (store, "ListTest1/SubFolder");
	test_imapx_delete_folder (store, "ListTest1");
	test_imapx_delete_folder (store, "ListTest2");

	g_free (path3);
	g_free (path2);
	g_free (path1);

	test_imapx_teardown (session, service);
}

static void
test_create_delete_folder (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolderInfo *fi;
	gchar *path;
	GError *error = NULL;
	gboolean success;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-create-delete");
	store = CAMEL_STORE (service);

	test_imapx_connect_service (service);

	/* Create folder and verify immediately */
	test_imapx_create_folder (store, "", "NewFolder");

	path = test_folder_path ("NewFolder");

	fi = camel_store_get_folder_info_sync (store, NULL,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (test_folder_info_contains (fi, path));
	camel_folder_info_free (fi);

	/* Verify after reconnect */
	test_imapx_reconnect_service (session, &service, "test-create-delete-2");
	store = CAMEL_STORE (service);

	fi = camel_store_get_folder_info_sync (store, NULL,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (test_folder_info_contains (fi, path));
	camel_folder_info_free (fi);

	/* Unsubscribe and delete folder, then verify */
	success = camel_subscribable_unsubscribe_folder_sync (
		CAMEL_SUBSCRIBABLE (store), path, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	success = camel_store_delete_folder_sync (store, path, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	fi = camel_store_get_folder_info_sync (store, NULL,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_false (test_folder_info_contains (fi, path));
	camel_folder_info_free (fi);

	/* Verify deletion after reconnect */
	test_imapx_reconnect_service (session, &service, "test-create-delete-3");
	store = CAMEL_STORE (service);

	fi = camel_store_get_folder_info_sync (store, NULL,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_false (test_folder_info_contains (fi, path));
	camel_folder_info_free (fi);

	g_free (path);

	test_imapx_teardown (session, service);
}

static void
test_rename_folder (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolderInfo *fi;
	gchar *old_path;
	gchar *new_path;
	GError *error = NULL;
	gboolean success;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-rename");
	store = CAMEL_STORE (service);

	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "OrigName");

	old_path = test_folder_path ("OrigName");
	new_path = test_folder_path ("RenamedFolder");

	success = camel_store_rename_folder_sync (store, old_path, new_path, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	fi = camel_store_get_folder_info_sync (store, NULL,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_false (test_folder_info_contains (fi, old_path));
	g_assert_true (test_folder_info_contains (fi, new_path));
	camel_folder_info_free (fi);

	/* Verify after reconnect */
	test_imapx_reconnect_service (session, &service, "test-rename-2");
	store = CAMEL_STORE (service);

	fi = camel_store_get_folder_info_sync (store, NULL,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_false (test_folder_info_contains (fi, old_path));
	g_assert_true (test_folder_info_contains (fi, new_path));
	camel_folder_info_free (fi);

	/* Cleanup */
	test_imapx_delete_folder (store, "RenamedFolder");

	g_free (new_path);
	g_free (old_path);

	test_imapx_teardown (session, service);
}

static void
test_append_message (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	GPtrArray *uids;
	gchar *appended_uid = NULL;
	gchar *folder_name;
	GError *error = NULL;
	gboolean success;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-append");
	store = CAMEL_STORE (service);

	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "AppendTest");

	folder_name = test_folder_path ("AppendTest");
	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	msg = test_create_message ("Test Append Subject", "Test append body content.\n");

	success = camel_folder_append_message_sync (folder, msg, NULL, &appended_uid, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 1);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpint (uids->len, ==, 1);
	g_ptr_array_unref (uids);

	g_free (appended_uid);
	g_object_unref (folder);

	/* Verify after reconnect */
	test_imapx_reconnect_service (session, &service, "test-append-2");
	store = CAMEL_STORE (service);

	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 1);

	g_object_unref (folder);

	/* Cleanup */
	test_imapx_delete_folder (store, "AppendTest");

	g_free (folder_name);

	test_imapx_teardown (session, service);
}

static void
test_fetch_message (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	CamelMimeMessage *fetched;
	CamelDataWrapper *content;
	CamelStream *stream;
	GByteArray *byte_array;
	GPtrArray *uids;
	gchar *folder_name;
	GError *error = NULL;
	gboolean success;
	const gchar *subject = "Fetch Test Subject";
	const gchar *body = "This is the test body for fetch.";

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-fetch");
	store = CAMEL_STORE (service);

	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "FetchTest");

	folder_name = test_folder_path ("FetchTest");
	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);

	msg = test_create_message (subject, body);
	success = camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpint (uids->len, ==, 1);

	fetched = camel_folder_get_message_sync (folder, uids->pdata[0], NULL, &error);
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
	g_ptr_array_unref (uids);
	g_object_unref (folder);

	/* Verify after reconnect */
	test_imapx_reconnect_service (session, &service, "test-fetch-2");
	store = CAMEL_STORE (service);

	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpint (uids->len, ==, 1);

	fetched = camel_folder_get_message_sync (folder, uids->pdata[0], NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (camel_mime_message_get_subject (fetched), ==, subject);

	g_object_unref (fetched);
	g_ptr_array_unref (uids);
	g_object_unref (folder);

	/* Cleanup */
	test_imapx_delete_folder (store, "FetchTest");

	g_free (folder_name);

	test_imapx_teardown (session, service);
}

static void
test_message_flags (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	GPtrArray *uids;
	gchar *folder_name;
	guint32 flags;
	GError *error = NULL;
	gboolean success;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-flags");
	store = CAMEL_STORE (service);

	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "FlagsTest");

	folder_name = test_folder_path ("FlagsTest");
	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);

	msg = test_create_message ("Flags Test", "Body.\n");
	success = camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpint (uids->len, ==, 1);

	camel_folder_set_message_flags (folder, uids->pdata[0],
		CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_FLAGGED,
		CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_FLAGGED);

	success = camel_folder_synchronize_sync (folder, FALSE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	info = camel_folder_get_message_info (folder, uids->pdata[0]);
	g_assert_nonnull (info);
	flags = camel_message_info_get_flags (info);
	g_assert_true ((flags & CAMEL_MESSAGE_SEEN) != 0);
	g_assert_true ((flags & CAMEL_MESSAGE_FLAGGED) != 0);
	g_clear_object (&info);

	g_ptr_array_unref (uids);
	g_object_unref (folder);

	/* Verify after reconnect */
	test_imapx_reconnect_service (session, &service, "test-flags-2");
	store = CAMEL_STORE (service);

	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpint (uids->len, ==, 1);

	info = camel_folder_get_message_info (folder, uids->pdata[0]);
	g_assert_nonnull (info);
	flags = camel_message_info_get_flags (info);
	g_assert_true ((flags & CAMEL_MESSAGE_SEEN) != 0);
	g_assert_true ((flags & CAMEL_MESSAGE_FLAGGED) != 0);
	g_clear_object (&info);

	g_ptr_array_unref (uids);
	g_object_unref (folder);

	/* Cleanup */
	test_imapx_delete_folder (store, "FlagsTest");

	g_free (folder_name);

	test_imapx_teardown (session, service);
}

static void
test_expunge (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	GPtrArray *uids;
	gchar *folder_name;
	GError *error = NULL;
	gboolean success;
	gint ii;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-expunge");
	store = CAMEL_STORE (service);

	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "ExpungeTest");

	folder_name = test_folder_path ("ExpungeTest");
	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);

	for (ii = 0; ii < 3; ii++) {
		gchar *subject;

		subject = g_strdup_printf ("Expunge message %d", ii);
		msg = test_create_message (subject, "Body.\n");
		success = camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);
		g_object_unref (msg);
		g_free (subject);
	}

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 3);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpint (uids->len, ==, 3);

	camel_folder_delete_message (folder, uids->pdata[1]);
	success = camel_folder_expunge_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_ptr_array_unref (uids);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 2);

	g_object_unref (folder);

	/* Verify after reconnect */
	test_imapx_reconnect_service (session, &service, "test-expunge-2");
	store = CAMEL_STORE (service);

	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 2);

	g_object_unref (folder);

	/* Cleanup */
	test_imapx_delete_folder (store, "ExpungeTest");

	g_free (folder_name);

	test_imapx_teardown (session, service);
}

static void
test_transfer_messages (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder_a;
	CamelFolder *folder_b;
	CamelMimeMessage *msg;
	GPtrArray *uids;
	GPtrArray *transferred_uids = NULL;
	gchar *path_a;
	gchar *path_b;
	GError *error = NULL;
	gboolean success;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-transfer");
	store = CAMEL_STORE (service);

	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "TransferA");
	test_imapx_create_folder (store, "", "TransferB");

	path_a = test_folder_path ("TransferA");
	path_b = test_folder_path ("TransferB");

	folder_a = camel_store_get_folder_sync (store, path_a, 0, NULL, &error);
	g_assert_no_error (error);

	msg = test_create_message ("Transfer Test", "Transfer body.\n");
	success = camel_folder_append_message_sync (folder_a, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	success = camel_folder_refresh_info_sync (folder_a, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder_a), ==, 1);

	folder_b = camel_store_get_folder_sync (store, path_b, 0, NULL, &error);
	g_assert_no_error (error);

	uids = camel_folder_dup_uids (folder_a);
	g_assert_cmpint (uids->len, ==, 1);

	success = camel_folder_transfer_messages_to_sync (folder_a, uids,
		folder_b, TRUE, &transferred_uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	if (transferred_uids) {
		g_ptr_array_unref (transferred_uids);
	}
	g_ptr_array_unref (uids);

	success = camel_folder_refresh_info_sync (folder_a, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	success = camel_folder_refresh_info_sync (folder_b, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder_b), ==, 1);

	g_object_unref (folder_a);
	g_object_unref (folder_b);

	/* Verify after reconnect */
	test_imapx_reconnect_service (session, &service, "test-transfer-2");
	store = CAMEL_STORE (service);

	folder_a = camel_store_get_folder_sync (store, path_a, 0, NULL, &error);
	g_assert_no_error (error);
	folder_b = camel_store_get_folder_sync (store, path_b, 0, NULL, &error);
	g_assert_no_error (error);

	success = camel_folder_refresh_info_sync (folder_a, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	success = camel_folder_refresh_info_sync (folder_b, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder_b), ==, 1);

	g_object_unref (folder_a);
	g_object_unref (folder_b);

	/* Cleanup */
	test_imapx_delete_folder (store, "TransferA");
	test_imapx_delete_folder (store, "TransferB");

	g_free (path_b);
	g_free (path_a);

	test_imapx_teardown (session, service);
}

static void
test_refresh_info (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelSession *session2;
	CamelService *service2;
	CamelFolder *folder2;
	CamelMimeMessage *msg;
	gchar *folder_name;
	GError *error = NULL;
	gboolean success;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-refresh");
	store = CAMEL_STORE (service);

	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "RefreshTest");

	folder_name = test_folder_path ("RefreshTest");
	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 0);

	/* Append a message via a second connection */
	session2 = test_imapx_session_new ();
	service2 = test_imapx_create_service (session2, "test-refresh-ext");
	test_imapx_connect_service (service2);

	folder2 = camel_store_get_folder_sync (CAMEL_STORE (service2),
		folder_name, 0, NULL, &error);
	g_assert_no_error (error);

	msg = test_create_message ("Refresh Test Message", "Delivered via second connection.\n");
	success = camel_folder_append_message_sync (folder2, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);
	g_object_unref (folder2);

	test_imapx_teardown (session2, service2);

	/* First connection should not see the new message yet (IDLE disabled) */
	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 0);

	/* Refresh and verify */
	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 1);

	g_object_unref (folder);

	/* Cleanup */
	test_imapx_delete_folder (store, "RefreshTest");

	g_free (folder_name);

	test_imapx_teardown (session, service);
}

static void
test_copy_messages (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder_a;
	CamelFolder *folder_b;
	CamelMimeMessage *msg;
	GPtrArray *uids;
	GPtrArray *transferred_uids = NULL;
	gchar *path_a;
	gchar *path_b;
	GError *error = NULL;
	gboolean success;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-copy");
	store = CAMEL_STORE (service);
	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "CopyA");
	test_imapx_create_folder (store, "", "CopyB");

	path_a = test_folder_path ("CopyA");
	path_b = test_folder_path ("CopyB");

	folder_a = camel_store_get_folder_sync (store, path_a, 0, NULL, &error);
	g_assert_no_error (error);

	msg = test_create_message ("Copy Test", "Copy body.\n");
	success = camel_folder_append_message_sync (folder_a, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	success = camel_folder_refresh_info_sync (folder_a, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder_a), ==, 1);

	folder_b = camel_store_get_folder_sync (store, path_b, 0, NULL, &error);
	g_assert_no_error (error);

	uids = camel_folder_dup_uids (folder_a);
	g_assert_cmpint (uids->len, ==, 1);

	success = camel_folder_transfer_messages_to_sync (folder_a, uids,
		folder_b, FALSE, &transferred_uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	if (transferred_uids) {
		g_ptr_array_unref (transferred_uids);
	}
	g_ptr_array_unref (uids);

	success = camel_folder_refresh_info_sync (folder_a, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	success = camel_folder_refresh_info_sync (folder_b, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder_a), ==, 1);
	g_assert_cmpint (camel_folder_get_message_count (folder_b), ==, 1);

	g_object_unref (folder_a);
	g_object_unref (folder_b);

	test_imapx_delete_folder (store, "CopyA");
	test_imapx_delete_folder (store, "CopyB");

	g_free (path_b);
	g_free (path_a);
	test_imapx_teardown (session, service);
}

static void
test_nested_folder_rename (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolderInfo *fi;
	gchar *old_parent;
	gchar *new_parent;
	gchar *old_child;
	gchar *new_child;
	GError *error = NULL;
	gboolean success;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-nested-rename");
	store = CAMEL_STORE (service);
	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "NestA");
	test_imapx_create_folder (store, "NestA", "NestB");

	old_parent = test_folder_path ("NestA");
	new_parent = test_folder_path ("NestD");
	old_child = test_folder_path ("NestA/NestB");
	new_child = test_folder_path ("NestD/NestB");

	success = camel_store_rename_folder_sync (store, old_parent, new_parent, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	fi = camel_store_get_folder_info_sync (store, NULL,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);

	g_assert_false (test_folder_info_contains (fi, old_parent));
	g_assert_false (test_folder_info_contains (fi, old_child));
	g_assert_true (test_folder_info_contains (fi, new_parent));
	g_assert_true (test_folder_info_contains (fi, new_child));

	camel_folder_info_free (fi);

	test_imapx_delete_folder (store, "NestD/NestB");
	test_imapx_delete_folder (store, "NestD");

	g_free (new_child);
	g_free (old_child);
	g_free (new_parent);
	g_free (old_parent);
	test_imapx_teardown (session, service);
}

static void
test_message_info (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	CamelInternetAddress *addr;
	GPtrArray *uids;
	gchar *folder_name;
	GError *error = NULL;
	gboolean success;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-info");
	store = CAMEL_STORE (service);
	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "InfoTest");
	folder_name = test_folder_path ("InfoTest");
	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);

	msg = camel_mime_message_new ();
	camel_mime_message_set_subject (msg, "Info Test Subject");
	camel_mime_message_set_date (msg, CAMEL_MESSAGE_DATE_CURRENT, 0);

	addr = camel_internet_address_new ();
	camel_internet_address_add (addr, "Sender Name", "sender@example.com");
	camel_mime_message_set_from (msg, addr);
	g_object_unref (addr);

	addr = camel_internet_address_new ();
	camel_internet_address_add (addr, "Recipient One", "recipient1@example.com");
	camel_internet_address_add (addr, "Recipient Two", "recipient2@example.com");
	camel_mime_message_set_recipients (msg, CAMEL_RECIPIENT_TYPE_TO, addr);
	g_object_unref (addr);

	addr = camel_internet_address_new ();
	camel_internet_address_add (addr, "CC User", "cc@example.com");
	camel_mime_message_set_recipients (msg, CAMEL_RECIPIENT_TYPE_CC, addr);
	g_object_unref (addr);

	camel_mime_part_set_content (CAMEL_MIME_PART (msg),
		"Info test body.\n", 16, "text/plain");

	success = camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpint (uids->len, ==, 1);

	info = camel_folder_get_message_info (folder, uids->pdata[0]);
	g_assert_nonnull (info);

	g_assert_cmpstr (camel_message_info_get_subject (info), ==, "Info Test Subject");
	g_assert_nonnull (camel_message_info_get_from (info));
	g_assert_nonnull (strstr (camel_message_info_get_from (info), "sender@example.com"));
	g_assert_nonnull (camel_message_info_get_to (info));
	g_assert_nonnull (strstr (camel_message_info_get_to (info), "recipient1@example.com"));
	g_assert_nonnull (camel_message_info_get_cc (info));
	g_assert_nonnull (strstr (camel_message_info_get_cc (info), "cc@example.com"));
	g_assert_cmpint ((gint) camel_message_info_get_size (info), >, 0);
	g_assert_cmpint ((gint64) camel_message_info_get_date_sent (info), >, 0);

	g_clear_object (&info);
	g_ptr_array_unref (uids);
	g_object_unref (folder);

	test_imapx_delete_folder (store, "InfoTest");
	g_free (folder_name);
	test_imapx_teardown (session, service);
}

static void
test_multipart_message (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	CamelMimeMessage *fetched;
	CamelDataWrapper *content;
	CamelDataWrapper *sub_content;
	CamelMultipart *mp_mixed;
	CamelMultipart *mp_alt;
	CamelMimePart *part;
	CamelContentType *ct;
	GPtrArray *uids;
	gchar *folder_name;
	GError *error = NULL;
	gboolean success;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-multipart");
	store = CAMEL_STORE (service);
	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "MultipartTest");
	folder_name = test_folder_path ("MultipartTest");
	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);

	msg = test_create_multipart_message ("Multipart Test");
	success = camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpint (uids->len, ==, 1);

	fetched = camel_folder_get_message_sync (folder, uids->pdata[0], NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fetched);

	content = camel_medium_get_content (CAMEL_MEDIUM (fetched));
	g_assert_nonnull (content);
	g_assert_true (CAMEL_IS_MULTIPART (content));
	mp_mixed = CAMEL_MULTIPART (content);
	g_assert_cmpint (camel_multipart_get_number (mp_mixed), ==, 2);

	part = camel_multipart_get_part (mp_mixed, 0);
	g_assert_nonnull (part);
	sub_content = camel_medium_get_content (CAMEL_MEDIUM (part));
	g_assert_nonnull (sub_content);
	g_assert_true (CAMEL_IS_MULTIPART (sub_content));
	mp_alt = CAMEL_MULTIPART (sub_content);
	g_assert_cmpint (camel_multipart_get_number (mp_alt), ==, 2);

	part = camel_multipart_get_part (mp_alt, 0);
	g_assert_nonnull (part);
	ct = camel_mime_part_get_content_type (part);
	g_assert_nonnull (ct);
	g_assert_true (camel_content_type_is (ct, "text", "plain"));

	part = camel_multipart_get_part (mp_alt, 1);
	g_assert_nonnull (part);
	ct = camel_mime_part_get_content_type (part);
	g_assert_nonnull (ct);
	g_assert_true (camel_content_type_is (ct, "text", "html"));

	part = camel_multipart_get_part (mp_mixed, 1);
	g_assert_nonnull (part);
	ct = camel_mime_part_get_content_type (part);
	g_assert_nonnull (ct);
	g_assert_true (camel_content_type_is (ct, "application", "octet-stream"));

	g_object_unref (fetched);
	g_ptr_array_unref (uids);
	g_object_unref (folder);

	test_imapx_delete_folder (store, "MultipartTest");
	g_free (folder_name);
	test_imapx_teardown (session, service);
}

static void
test_folder_counts (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderSummary *summary;
	CamelMimeMessage *msg;
	GPtrArray *uids;
	gchar *folder_name;
	gchar *subject;
	GError *error = NULL;
	gboolean success;
	gint ii;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-counts");
	store = CAMEL_STORE (service);
	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "CountsTest");
	folder_name = test_folder_path ("CountsTest");
	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);

	for (ii = 0; ii < 3; ii++) {
		subject = g_strdup_printf ("Count message %d", ii);
		msg = test_create_message (subject, "Body.\n");
		success = camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);
		g_object_unref (msg);
		g_free (subject);
	}

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 3);

	summary = camel_folder_get_folder_summary (folder);
	g_assert_nonnull (summary);
	g_assert_cmpuint (camel_folder_summary_get_unread_count (summary), ==, 3);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpint (uids->len, ==, 3);

	camel_folder_set_message_flags (folder, uids->pdata[0],
		CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
	success = camel_folder_synchronize_sync (folder, FALSE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpuint (camel_folder_summary_get_unread_count (summary), ==, 2);

	camel_folder_delete_message (folder, uids->pdata[1]);
	success = camel_folder_expunge_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_ptr_array_unref (uids);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 2);
	g_assert_cmpuint (camel_folder_summary_get_unread_count (summary), ==, 1);

	g_object_unref (folder);

	test_imapx_delete_folder (store, "CountsTest");
	g_free (folder_name);
	test_imapx_teardown (session, service);
}

static void
test_user_flags (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	GPtrArray *uids;
	gchar *folder_name;
	GError *error = NULL;
	gboolean success;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-user-flags");
	store = CAMEL_STORE (service);
	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "UserFlagsTest");
	folder_name = test_folder_path ("UserFlagsTest");
	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);

	msg = test_create_message ("User Flags Test", "Body.\n");
	success = camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpint (uids->len, ==, 1);

	info = camel_folder_get_message_info (folder, uids->pdata[0]);
	g_assert_nonnull (info);

	g_assert_false (camel_message_info_get_user_flag (info, "custom-label"));
	camel_message_info_set_user_flag (info, "custom-label", TRUE);
	g_clear_object (&info);

	success = camel_folder_synchronize_sync (folder, FALSE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_ptr_array_unref (uids);
	g_object_unref (folder);

	test_imapx_reconnect_service (session, &service, "test-user-flags-2");
	store = CAMEL_STORE (service);

	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	uids = camel_folder_dup_uids (folder);
	g_assert_cmpint (uids->len, ==, 1);

	info = camel_folder_get_message_info (folder, uids->pdata[0]);
	g_assert_nonnull (info);
	g_assert_true (camel_message_info_get_user_flag (info, "custom-label"));
	g_clear_object (&info);

	g_ptr_array_unref (uids);
	g_object_unref (folder);

	test_imapx_delete_folder (store, "UserFlagsTest");
	g_free (folder_name);
	test_imapx_teardown (session, service);
}

static void
test_server_search (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	GPtrArray *words;
	GPtrArray *result_uids = NULL;
	gchar *folder_name;
	GError *error = NULL;
	gboolean success;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-search");
	store = CAMEL_STORE (service);
	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "SearchTest");
	folder_name = test_folder_path ("SearchTest");
	folder = camel_store_get_folder_sync (store, folder_name, 0, NULL, &error);
	g_assert_no_error (error);

	msg = test_create_message ("Alpha Report", "Body one.\n");
	success = camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	msg = test_create_message ("Beta Analysis", "Body two.\n");
	success = camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	msg = test_create_message ("Alpha Summary", "Body three.\n");
	success = camel_folder_append_message_sync (folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_object_unref (msg);

	success = camel_folder_refresh_info_sync (folder, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_folder_get_message_count (folder), ==, 3);

	words = g_ptr_array_new ();
	g_ptr_array_add (words, (gpointer) "Alpha");

	success = camel_folder_search_header_sync (folder, "Subject", words,
		&result_uids, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (result_uids);
	g_assert_cmpint (result_uids->len, ==, 2);

	g_ptr_array_unref (result_uids);
	g_ptr_array_unref (words);
	g_object_unref (folder);

	test_imapx_delete_folder (store, "SearchTest");
	g_free (folder_name);
	test_imapx_teardown (session, service);
}

static void
test_subscription_state (void)
{
	CamelSession *session;
	CamelService *service;
	CamelStore *store;
	gchar *path;
	GError *error = NULL;
	gboolean success;

	session = test_imapx_session_new ();
	service = test_imapx_create_service (session, "test-sub-state");
	store = CAMEL_STORE (service);
	test_imapx_connect_service (service);

	test_imapx_create_folder (store, "", "SubStateTest");
	path = test_folder_path ("SubStateTest");

	g_assert_true (camel_subscribable_folder_is_subscribed (
		CAMEL_SUBSCRIBABLE (store), path));

	success = camel_subscribable_unsubscribe_folder_sync (
		CAMEL_SUBSCRIBABLE (store), path, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_false (camel_subscribable_folder_is_subscribed (
		CAMEL_SUBSCRIBABLE (store), path));

	success = camel_subscribable_subscribe_folder_sync (
		CAMEL_SUBSCRIBABLE (store), path, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_true (camel_subscribable_folder_is_subscribed (
		CAMEL_SUBSCRIBABLE (store), path));

	test_imapx_delete_folder (store, "SubStateTest");

	g_free (path);
	test_imapx_teardown (session, service);
}

gint
main (gint argc,
      gchar **argv)
{
	CamelSession *setup_session;
	CamelService *setup_service;
	CamelFolderInfo *fi;
	GError *error = NULL;
	gboolean success;
	gint ret;

	parse_use_server_arg (&argc, &argv);

	camel_test_init (&argc, &argv);
	camel_test_provider_init (1, imapx_drivers);

	if (external_server) {
		external_server->folder_prefix = g_strdup_printf (
			"camel-test-%08x", g_random_int ());

		setup_session = test_imapx_session_new ();
		setup_service = test_imapx_create_service (setup_session, "test-setup");
		test_imapx_connect_service (setup_service);

		fi = camel_store_create_folder_sync (CAMEL_STORE (setup_service),
			"", external_server->folder_prefix, NULL, &error);
		g_assert_no_error (error);
		g_assert_nonnull (fi);
		camel_folder_info_free (fi);

		success = camel_subscribable_subscribe_folder_sync (
			CAMEL_SUBSCRIBABLE (setup_service),
			external_server->folder_prefix, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		test_imapx_teardown (setup_session, setup_service);
	} else {
		test_server = dovecot_test_server_new ();
		if (!test_server) {
			g_print ("Dovecot not installed, skipping\n");
			camel_test_shutdown ();
			return 0;
		}
	}

	g_test_add_func ("/Camel/IMAPx/Connect", test_connect);
	g_test_add_func ("/Camel/IMAPx/ListFolders", test_list_folders);
	g_test_add_func ("/Camel/IMAPx/CreateDeleteFolder", test_create_delete_folder);
	g_test_add_func ("/Camel/IMAPx/RenameFolder", test_rename_folder);
	g_test_add_func ("/Camel/IMAPx/AppendMessage", test_append_message);
	g_test_add_func ("/Camel/IMAPx/FetchMessage", test_fetch_message);
	g_test_add_func ("/Camel/IMAPx/MessageFlags", test_message_flags);
	g_test_add_func ("/Camel/IMAPx/Expunge", test_expunge);
	g_test_add_func ("/Camel/IMAPx/TransferMessages", test_transfer_messages);
	g_test_add_func ("/Camel/IMAPx/RefreshInfo", test_refresh_info);
	g_test_add_func ("/Camel/IMAPx/CopyMessages", test_copy_messages);
	g_test_add_func ("/Camel/IMAPx/NestedFolderRename", test_nested_folder_rename);
	g_test_add_func ("/Camel/IMAPx/MessageInfo", test_message_info);
	g_test_add_func ("/Camel/IMAPx/MultipartMessage", test_multipart_message);
	g_test_add_func ("/Camel/IMAPx/FolderCounts", test_folder_counts);
	g_test_add_func ("/Camel/IMAPx/UserFlags", test_user_flags);
	g_test_add_func ("/Camel/IMAPx/ServerSearch", test_server_search);
	g_test_add_func ("/Camel/IMAPx/SubscriptionState", test_subscription_state);

	ret = g_test_run ();

	if (external_server) {
		setup_session = test_imapx_session_new ();
		setup_service = test_imapx_create_service (setup_session, "test-cleanup");
		test_imapx_connect_service (setup_service);

		fi = camel_store_get_folder_info_sync (CAMEL_STORE (setup_service),
			external_server->folder_prefix,
			CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, NULL);
		if (fi) {
			test_cleanup_folder_tree (CAMEL_STORE (setup_service), fi);
			camel_folder_info_free (fi);
		}

		camel_subscribable_unsubscribe_folder_sync (
			CAMEL_SUBSCRIBABLE (setup_service),
			external_server->folder_prefix, NULL, NULL);
		camel_store_delete_folder_sync (CAMEL_STORE (setup_service),
			external_server->folder_prefix, NULL, NULL);

		test_imapx_teardown (setup_session, setup_service);

		external_server_free (external_server);
		external_server = NULL;
	} else {
		dovecot_test_server_free (test_server);
		test_server = NULL;
	}

	camel_test_shutdown ();

	return ret;
}

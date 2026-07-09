/*
 * SPDX-FileCopyrightText: (C) 2026 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#ifdef G_OS_UNIX
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <pwd.h>
#endif

#include "dovecot-helper.h"

#define DOVECOT_TEST_USER "testuser"
#define DOVECOT_TEST_PASSWORD "testpass"
#define DOVECOT_STARTUP_TIMEOUT_SECS 10
#define DOVECOT_STARTUP_POLL_MS 100

struct _DovecotTestServer {
	gchar *tmp_dir;
	gchar *dovecot_path;
	guint16 port;
	GSubprocess *process;
};

static DovecotTestServer *global_server_for_atexit = NULL;

static guint16
pick_free_port (void)
{
#ifdef G_OS_UNIX
	struct sockaddr_in addr;
	socklen_t addr_len;
	int fd;
	guint16 port = 0;

	memset (&addr, 0, sizeof (addr));

	fd = socket (AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		return 0;
	}

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	addr.sin_port = 0;

	if (bind (fd, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
		close (fd);
		return 0;
	}

	addr_len = sizeof (addr);
	if (getsockname (fd, (struct sockaddr *) &addr, &addr_len) < 0) {
		close (fd);
		return 0;
	}

	port = ntohs (addr.sin_port);
	close (fd);

	return port;
#else
	return 0;
#endif
}

static gboolean
wait_for_port (guint16 port,
	       gint timeout_secs)
{
#ifdef G_OS_UNIX
	gint64 deadline;
	struct sockaddr_in addr;

	memset (&addr, 0, sizeof (addr));

	deadline = g_get_monotonic_time () + (gint64) timeout_secs * G_USEC_PER_SEC;

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	addr.sin_port = htons (port);

	while (g_get_monotonic_time () < deadline) {
		int fd;

		fd = socket (AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			return FALSE;
		}

		if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) == 0) {
			close (fd);
			return TRUE;
		}

		close (fd);
		g_usleep (DOVECOT_STARTUP_POLL_MS * 1000);
	}

	return FALSE;
#else
	return FALSE;
#endif
}

static gchar *
find_dovecot_binary (void)
{
	const gchar *known_paths[] = {
		"/usr/sbin/dovecot",
		"/usr/local/sbin/dovecot",
		NULL
	};
	gchar *path;
	gint ii;

	path = g_find_program_in_path ("dovecot");
	if (path) {
		return path;
	}

	for (ii = 0; known_paths[ii]; ii++) {
		if (g_file_test (known_paths[ii], G_FILE_TEST_IS_EXECUTABLE)) {
			return g_strdup (known_paths[ii]);
		}
	}

	return NULL;
}

static gboolean
create_directory_tree (DovecotTestServer *server)
{
	const gchar *subdirs[] = {
		"run",
		"lib",
		"log",
		"home",
		NULL
	};
	gint ii;

	for (ii = 0; subdirs[ii]; ii++) {
		gchar *path;

		path = g_build_filename (server->tmp_dir, subdirs[ii], NULL);

		if (g_mkdir_with_parents (path, 0755) != 0) {
			g_warning ("Failed to create directory: %s", path);
			g_free (path);
			return FALSE;
		}

		g_free (path);
	}

	return TRUE;
}

static gboolean
write_passwd_file (DovecotTestServer *server)
{
	gchar *path;
	gchar *contents;
	gboolean success;
	GError *error = NULL;

	path = g_build_filename (server->tmp_dir, "passwd", NULL);

	contents = g_strdup_printf (
		"%s:{PLAIN}%s\n",
		DOVECOT_TEST_USER,
		DOVECOT_TEST_PASSWORD);

	success = g_file_set_contents (path, contents, -1, &error);
	if (!success) {
		g_warning ("Failed to write passwd file: %s", error->message);
		g_clear_error (&error);
	}

	g_free (contents);
	g_free (path);

	return success;
}

static gboolean
write_dovecot_conf (DovecotTestServer *server)
{
	gchar *conf_path;
	gchar *passwd_path;
	gchar *contents;
	struct passwd *pw;
	gboolean success;
	GError *error = NULL;

	pw = getpwuid (getuid ());
	if (!pw) {
		g_warning ("Failed to get passwd entry for uid %u", (guint) getuid ());
		return FALSE;
	}

	conf_path = g_build_filename (server->tmp_dir, "dovecot.conf", NULL);
	passwd_path = g_build_filename (server->tmp_dir, "passwd", NULL);

	contents = g_strdup_printf (
		"dovecot_config_version = 2.4.4\n"
		"dovecot_storage_version = 2.4.4\n"
		"\n"
		"protocols {\n"
		"  imap = yes\n"
		"}\n"
		"\n"
		"listen = 127.0.0.1\n"
		"ssl = no\n"
		"auth_allow_cleartext = yes\n"
		"base_dir = %s/run\n"
		"state_dir = %s/lib\n"
		"log_path = %s/log/dovecot.log\n"
		"\n"
		"default_internal_user = %s\n"
		"default_internal_group = %s\n"
		"default_login_user = %s\n"
		"\n"
		"first_valid_uid = %u\n"
		"first_valid_gid = %u\n"
		"\n"
		"mail_driver = maildir\n"
		"mail_home = %s/home\n"
		"mail_path = %s/Maildir\n"
		"\n"
		"namespace inbox {\n"
		"  inbox = yes\n"
		"  separator = /\n"
		"}\n"
		"\n"
		"passdb passwd-file {\n"
		"  passwd_file_path = %s\n"
		"}\n"
		"\n"
		"userdb static {\n"
		"  fields {\n"
		"    uid = %u\n"
		"    gid = %u\n"
		"    home = %s/home\n"
		"    mail_path = %s/Maildir\n"
		"  }\n"
		"}\n"
		"\n"
		"service anvil {\n"
		"  chroot =\n"
		"  user = %s\n"
		"  group = %s\n"
		"}\n"
		"\n"
		"service imap-login {\n"
		"  chroot =\n"
		"  user = %s\n"
		"  group = %s\n"
		"  inet_listener imap {\n"
		"    port = %u\n"
		"  }\n"
		"}\n"
		"\n"
		"service imap {\n"
		"  user = %s\n"
		"  group = %s\n"
		"}\n"
		"\n"
		"service auth {\n"
		"  user = %s\n"
		"  group = %s\n"
		"}\n"
		"\n"
		"service auth-worker {\n"
		"  user = %s\n"
		"  group = %s\n"
		"}\n"
		"\n"
		"service dict {\n"
		"  user = %s\n"
		"  group = %s\n"
		"}\n"
		"\n"
		"service dict-async {\n"
		"  user = %s\n"
		"  group = %s\n"
		"}\n"
		"\n"
		"service imap-hibernate {\n"
		"  user = %s\n"
		"  group = %s\n"
		"}\n"
		"\n"
		"service stats {\n"
		"  user = %s\n"
		"  group = %s\n"
		"}\n"
		"\n"
		"service log {\n"
		"  user = %s\n"
		"  group = %s\n"
		"}\n"
		"\n"
		"service config {\n"
		"  user = %s\n"
		"  group = %s\n"
		"}\n",
		/* base_dir, state_dir, log_path */
		server->tmp_dir, server->tmp_dir, server->tmp_dir,
		/* default_internal_user, default_internal_group, default_login_user */
		pw->pw_name, pw->pw_name, pw->pw_name,
		/* first_valid_uid, first_valid_gid */
		(guint) getuid (), (guint) getgid (),
		/* mail_home, mail_path */
		server->tmp_dir, server->tmp_dir,
		/* passdb passwd_file_path */
		passwd_path,
		/* userdb static uid, gid, home, mail_path */
		(guint) getuid (), (guint) getgid (), server->tmp_dir, server->tmp_dir,
		/* service anvil: user, group */
		pw->pw_name, pw->pw_name,
		/* service imap-login: user, group, port */
		pw->pw_name, pw->pw_name, (guint) server->port,
		/* service imap: user, group */
		pw->pw_name, pw->pw_name,
		/* service auth: user, group */
		pw->pw_name, pw->pw_name,
		/* service auth-worker: user, group */
		pw->pw_name, pw->pw_name,
		/* service dict: user, group */
		pw->pw_name, pw->pw_name,
		/* service dict-async: user, group */
		pw->pw_name, pw->pw_name,
		/* service imap-hibernate: user, group */
		pw->pw_name, pw->pw_name,
		/* service stats: user, group */
		pw->pw_name, pw->pw_name,
		/* service log: user, group */
		pw->pw_name, pw->pw_name,
		/* service config: user, group */
		pw->pw_name, pw->pw_name);

	success = g_file_set_contents (conf_path, contents, -1, &error);
	if (!success) {
		g_warning ("Failed to write dovecot.conf: %s", error->message);
		g_clear_error (&error);
	}

	g_free (contents);
	g_free (conf_path);
	g_free (passwd_path);

	return success;
}

static void
atexit_cleanup (void)
{
	if (global_server_for_atexit) {
		dovecot_test_server_free (global_server_for_atexit);
		global_server_for_atexit = NULL;
	}
}

DovecotTestServer *
dovecot_test_server_new (void)
{
	DovecotTestServer *server;
	gchar *dovecot_path;
	gchar *conf_path;
	GError *error = NULL;

	dovecot_path = find_dovecot_binary ();
	if (!dovecot_path) {
		if (g_getenv ("CAMEL_TEST_REQUIRE_DOVECOT")) {
			g_error ("Dovecot binary not found, but CAMEL_TEST_REQUIRE_DOVECOT is set");
		}
		return NULL;
	}

	server = g_new0 (DovecotTestServer, 1);
	server->dovecot_path = dovecot_path;

	server->tmp_dir = g_dir_make_tmp ("camel-test-imap-XXXXXX", &error);
	if (!server->tmp_dir) {
		g_warning ("Failed to create temp directory: %s", error->message);
		g_clear_error (&error);
		g_free (server->dovecot_path);
		g_free (server);
		return NULL;
	}

	if (!create_directory_tree (server)) {
		dovecot_test_server_free (server);
		return NULL;
	}

	server->port = pick_free_port ();
	if (server->port == 0) {
		g_warning ("Failed to pick a free port");
		dovecot_test_server_free (server);
		return NULL;
	}

	if (!write_passwd_file (server)) {
		dovecot_test_server_free (server);
		return NULL;
	}

	if (!write_dovecot_conf (server)) {
		dovecot_test_server_free (server);
		return NULL;
	}

	conf_path = g_build_filename (server->tmp_dir, "dovecot.conf", NULL);

	server->process = g_subprocess_new (
		G_SUBPROCESS_FLAGS_STDERR_SILENCE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE,
		&error,
		server->dovecot_path,
		"-F",
		"-c", conf_path,
		NULL);

	g_free (conf_path);

	if (!server->process) {
		g_warning ("Failed to start Dovecot: %s", error->message);
		g_clear_error (&error);
		dovecot_test_server_free (server);
		return NULL;
	}

	if (!wait_for_port (server->port, DOVECOT_STARTUP_TIMEOUT_SECS)) {
		g_warning ("Dovecot did not start listening on port %u within %d seconds",
			server->port, DOVECOT_STARTUP_TIMEOUT_SECS);
		dovecot_test_server_free (server);
		return NULL;
	}

	global_server_for_atexit = server;
	atexit (atexit_cleanup);

	return server;
}

void
dovecot_test_server_free (DovecotTestServer *server)
{
	if (!server) {
		return;
	}

	if (global_server_for_atexit == server) {
		global_server_for_atexit = NULL;
	}

	if (server->process) {
#ifdef G_OS_UNIX
		g_subprocess_send_signal (server->process, SIGTERM);
#else
		g_subprocess_force_exit (server->process);
#endif
		g_subprocess_wait (server->process, NULL, NULL);
		g_clear_object (&server->process);
	}

	if (server->tmp_dir) {
		GSubprocess *rm_proc;

		rm_proc = g_subprocess_new (
			G_SUBPROCESS_FLAGS_STDERR_SILENCE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE,
			NULL,
			"rm", "-rf", server->tmp_dir, NULL);
		if (rm_proc) {
			g_subprocess_wait (rm_proc, NULL, NULL);
			g_object_unref (rm_proc);
		}
	}

	g_free (server->tmp_dir);
	g_free (server->dovecot_path);
	g_free (server);
}

const gchar *
dovecot_test_server_get_host (DovecotTestServer *server)
{
	g_return_val_if_fail (server != NULL, NULL);

	return "127.0.0.1";
}

guint16
dovecot_test_server_get_port (DovecotTestServer *server)
{
	g_return_val_if_fail (server != NULL, 0);

	return server->port;
}

const gchar *
dovecot_test_server_get_user (DovecotTestServer *server)
{
	g_return_val_if_fail (server != NULL, NULL);

	return DOVECOT_TEST_USER;
}

const gchar *
dovecot_test_server_get_password (DovecotTestServer *server)
{
	g_return_val_if_fail (server != NULL, NULL);

	return DOVECOT_TEST_PASSWORD;
}

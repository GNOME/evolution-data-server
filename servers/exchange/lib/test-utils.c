/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <libedataserverui/e-passwords.h>
#include <gtk/gtk.h>

#include "e2k-context.h"
#include "e2k-global-catalog.h"
#include "e2k-uri.h"

#include "test-utils.h"

extern const gchar *test_program_name;

/**
 * test_ask_password:
 * @prompt: prompt string
 *
 * Prints @prompt followed by ": " and waits for the user to type
 * a password (with echoing disabled).
 *
 * Return value: the password (or %NULL if stdin is not a tty).
 **/
gchar *
test_ask_password (const gchar *prompt)
{
	gchar *password;
	struct termios t;
	gint old_lflag;
	gchar buf[80];

	if (tcgetattr (STDIN_FILENO, &t) != 0)
		return NULL;

	old_lflag = t.c_lflag;
	t.c_lflag = (t.c_lflag | ICANON | ECHONL) & ~ECHO;
	tcsetattr (STDIN_FILENO, TCSANOW, &t);

	fprintf (stderr, "%s: ", prompt);
	fflush (stdout);

	/* For some reason, fgets can return EINTR on
	 * Linux if ECHO is false...
	 */
	do
		password = fgets (buf, sizeof (buf), stdin);
	while (password == NULL && errno == EINTR);

	t.c_lflag = old_lflag;
	tcsetattr (STDIN_FILENO, TCSANOW, &t);

	if (!password)
		exit (1);
	return g_strndup (password, strcspn (password, "\n"));
}

/**
 * test_get_password:
 * @user: username to get the password for
 * @host: Exchange (or global catalog) server name
 *
 * Tries to get a password for @user on @host, by looking it up in
 * the Evolution password database or by prompting the user.
 *
 * Return value: the password, or %NULL if it could not be determined.
 **/
const gchar *
test_get_password (const gchar *user, const gchar *host)
{
	static gchar *password = NULL;
	gchar *prompt;

	if (password)
		return password;

	if (host) {
		gchar *key;

		key = g_strdup_printf ("exchange://%s@%s", user, host);
		password = e_passwords_get_password ("Exchange", key);
		g_free (key);
	}

	if (!password) {
		if (host) {
			prompt = g_strdup_printf ("Password for %s@%s",
						  user, host);
		} else
			prompt = g_strdup_printf ("Password for %s", user);

		password = test_ask_password (prompt);
		g_free (prompt);
	}

	return password;
}

/**
 * test_get_context:
 * @uri: an Exchange HTTP/HTTPS URI
 *
 * Creates an %E2kContext based on @uri. If @uri does not contain a
 * username, the user's local username will be used. If it does not
 * contain a password, test_get_password() will be called to get one.
 *
 * Return value: the new %E2kContext (always; if an error occurs,
 * test_get_context() will exit the program).
 **/
E2kContext *
test_get_context (const gchar *uri)
{
	E2kContext *ctx;
	E2kUri *euri;

	ctx = e2k_context_new (uri);
	if (!ctx) {
		fprintf (stderr, "Could not parse %s as URI\n", uri);
		exit (1);
	}

	euri = e2k_uri_new (uri);
	if (!euri->user)
		euri->user = g_strdup (g_get_user_name ());
	if (!euri->passwd)
		euri->passwd = g_strdup (test_get_password (euri->user, euri->host));

	e2k_context_set_auth (ctx, euri->user, euri->domain,
			      euri->authmech, euri->passwd);

	e2k_uri_free (euri);
	return ctx;
}

/**
 * test_get_gc:
 * @server: the global catalog server to contact
 *
 * Creates an %E2kGlobalCatalog for the server @server.
 * test_get_password() will be called to get a password.
 *
 * Return value: the new %E2kGlobalCatalog (always; if an error occurs,
 * test_get_gc() will exit the program).
 **/
E2kGlobalCatalog *
test_get_gc (const gchar *server)
{
	E2kGlobalCatalog *gc;
	const gchar *password;
	gchar *user, *p;

	if (strchr (server, '@')) {
		user = g_strdup (server);
		p = strchr (user, '@');
		*p = '\0';
		server = p + 1;
	} else
		user = g_strdup (g_get_user_name ());

	password = test_get_password (user, server);
	gc = e2k_global_catalog_new (server, -1, user, NULL, password, E2K_AUTOCONFIG_USE_GAL_DEFAULT);
	if (!gc) {
		fprintf (stderr, "Could not create GC\n");
		exit (1);
	}
	g_free (user);

	return gc;
}

static gchar **global_argv;
static gint global_argc;
static GMainLoop *loop;

/**
 * test_main:
 * @argc: argc
 * @argv: argv
 *
 * test-utils.o includes a main() function that calls various
 * initialization routines, starts the main loop, and then calls
 * test_main(). So test_main() is the entry point for a
 * test-utils-using program.
 **/

/**
 * test_quit:
 *
 * Cleanly quits a test program.
 **/
void
test_quit (void)
{
	g_main_loop_quit (loop);
}

/**
 * test_abort_if_http_error:
 * @status: an HTTP status code
 *
 * Checks if @status is an HTTP or libsoup error, and if so, prints
 * the error message and exits.
 **/
void
test_abort_if_http_error (E2kHTTPStatus status)
{
	if (E2K_HTTP_STATUS_IS_TRANSPORT_ERROR (status)) {
		fprintf (stderr, "\n%s\n", soup_status_get_phrase (status));
		exit (1);
	} else if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		fprintf (stderr, "\n%d\n", status);
		exit (1);
	}
}

static gboolean
idle_run (gpointer data)
{
	test_main (global_argc, global_argv);
	return FALSE;
}

gint
main (gint argc, gchar **argv)
{
	gtk_init (&argc, &argv);

	global_argc = argc;
	global_argv = argv;

	loop = g_main_loop_new (NULL, TRUE);
	g_idle_add (idle_run, NULL);
	g_main_loop_run (loop);

	return 0;
}

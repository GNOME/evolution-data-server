/*
 * evolution-user-prompter.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#include <config.h>
#include <locale.h>
#include <libintl.h>
#include <glib/gi18n.h>

#include "prompt-user.h"

static gboolean opt_keep_running = FALSE;

static GOptionEntry entries[] = {

	{ "keep-running", 'r', 0, G_OPTION_ARG_NONE, &opt_keep_running,
	  N_("Keep running after the last client is closed"), NULL },
	{ NULL }
};

gint
main (gint argc,
      gchar **argv)
{
	GOptionContext *context;
	EDBusServer *server;
	GError *error = NULL;

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	prompt_user_init (&argc, &argv);

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
	g_option_context_parse (context, &argc, &argv, &error);
	g_option_context_free (context);

	if (error != NULL) {
		g_printerr ("%s\n", error->message);
		exit (EXIT_FAILURE);
	}

	e_gdbus_templates_init_main_thread ();

	server = e_user_prompter_server_new ();
	g_signal_connect (
		server, "prompt",
		G_CALLBACK (prompt_user_show), NULL);

	g_print ("Prompter is up and running...\n");

	/* This SHOULD keep the server's use
	 * count from ever reaching zero. */
	if (opt_keep_running)
		e_dbus_server_hold (server);

	e_dbus_server_run (server, TRUE);

	g_object_unref (server);

	g_print ("Bye.\n");

	return 0;
}

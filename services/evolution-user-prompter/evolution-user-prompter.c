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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <locale.h>
#include <libintl.h>

#include "prompt-user.h"

gint
main (gint argc,
      gchar **argv)
{
	EDBusServer *server;

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	prompt_user_init (&argc, &argv);

	e_gdbus_templates_init_main_thread ();

	server = e_user_prompter_server_new ();
	g_signal_connect (server, "prompt", G_CALLBACK (prompt_user_show), NULL);

	g_print ("Prompter is up and running...\n");

	e_dbus_server_run (server, TRUE);

	g_object_unref (server);

	g_print ("Bye.\n");

	return 0;
}

/*
 * e-source-registry.c
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
#include <stdlib.h>
#include <glib/gi18n.h>

#ifdef ENABLE_MAINTAINER_MODE
#include <gtk/gtk.h>
#endif

#include <libebackend/libebackend.h>

/* Forward Declarations */
void evolution_source_registry_migrate_basedir (void);
void evolution_source_registry_migrate_sources (void);

gint
main (gint argc,
      gchar **argv)
{
	EDBusServer *server;
	EDBusServerExitCode exit_code;
	GError *error = NULL;

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

#ifdef ENABLE_MAINTAINER_MODE
	/* This is only to load gtk-modules, like
	 * bug-buddy's gnomesegvhandler, if possible */
	gtk_init_check (&argc, &argv);
#else
	g_type_init ();
#endif

	e_gdbus_templates_init_main_thread ();

reload:
	/* Migrate user data from ~/.evolution to XDG base directories. */
	evolution_source_registry_migrate_basedir ();

	/* Migrate ESource data from GConf XML blobs to key files.
	 * Do this AFTER XDG base directory migration since the key
	 * files are saved according to XDG base directory settings. */
	evolution_source_registry_migrate_sources ();

	server = e_source_registry_server_new ();

	/* Failure here is fatal.  Don't even try to keep going. */
	e_source_registry_server_load_all (
		E_SOURCE_REGISTRY_SERVER (server), &error);

	if (error != NULL) {
		g_printerr ("%s\n", error->message);
		g_object_unref (server);
		exit (EXIT_FAILURE);
	}

	g_print ("Server is up and running...\n");

	/* Keep the server from quitting on its own.
	 * We don't have a way of tracking number of
	 * active clients, so once the server is up,
	 * it's up until the session bus closes. */
	e_dbus_server_hold (server);

	exit_code = e_dbus_server_run (server, FALSE);

	g_object_unref (server);

	if (exit_code == E_DBUS_SERVER_EXIT_RELOAD) {
		g_print ("Reloading...\n");
		goto reload;
	}

	g_print ("Bye.\n");

	return 0;
}

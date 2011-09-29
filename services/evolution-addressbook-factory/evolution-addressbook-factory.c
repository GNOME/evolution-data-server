/*
 * evolution-addressbook-factory.c
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

#ifdef G_OS_WIN32
#include <windows.h>
#include <conio.h>
#ifndef PROCESS_DEP_ENABLE
#define PROCESS_DEP_ENABLE 0x00000001
#endif
#ifndef PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION
#define PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION 0x00000002
#endif
#endif

#include <libedata-book/e-data-book-factory.h>

/* Keep running after the last client is closed. */
static gboolean opt_keep_running = FALSE;

static GOptionEntry entries[] = {

	{ "keep-running", 'r', 0, G_OPTION_ARG_NONE, &opt_keep_running,
	  N_("Keep running after the last client is closed"), NULL },
	{ NULL }
};

/* Forward Declarations */
void evolution_addressbook_factory_migrate_basedir (void);

gint
main (gint argc,
      gchar **argv)
{
	GOptionContext *context;
	EDBusServer *server;
	GError *error = NULL;

#ifdef G_OS_WIN32
	/* Reduce risks */
	{
		typedef BOOL (WINAPI *t_SetDllDirectoryA) (LPCSTR lpPathName);
		t_SetDllDirectoryA p_SetDllDirectoryA;

		p_SetDllDirectoryA = GetProcAddress (
			GetModuleHandle ("kernel32.dll"),
			"SetDllDirectoryA");

		if (p_SetDllDirectoryA != NULL)
			p_SetDllDirectoryA ("");
	}
#ifndef _WIN64
	{
		typedef BOOL (WINAPI *t_SetProcessDEPPolicy) (DWORD dwFlags);
		t_SetProcessDEPPolicy p_SetProcessDEPPolicy;

		p_SetProcessDEPPolicy = GetProcAddress (
			GetModuleHandle ("kernel32.dll"),
			"SetProcessDEPPolicy");

		if (p_SetProcessDEPPolicy != NULL)
			p_SetProcessDEPPolicy (
				PROCESS_DEP_ENABLE |
				PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION);
	}
#endif
#endif

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

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
	g_option_context_parse (context, &argc, &argv, &error);
	g_option_context_free (context);

	if (error != NULL) {
		g_printerr ("%s\n", error->message);
		exit (EXIT_FAILURE);
	}

	/* Migrate user data from ~/.evolution to XDG base directories. */
	evolution_addressbook_factory_migrate_basedir ();

	server = g_initable_new (
		E_TYPE_DATA_BOOK_FACTORY, NULL, &error, NULL);

	if (error != NULL) {
		g_printerr ("%s\n", error->message);
		exit (EXIT_FAILURE);
	}

	g_print ("Server is up and running...\n");

	/* This SHOULD keep the server's use
	 * count from ever reaching zero. */
	if (opt_keep_running)
		e_dbus_server_hold (server);

	e_dbus_server_run (server);

	g_object_unref (server);

	g_print ("Bye.\n");

	return 0;
}

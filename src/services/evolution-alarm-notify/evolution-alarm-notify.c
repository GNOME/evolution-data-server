/*
 * SPDX-FileCopyrightText: (C) 2018 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <locale.h>
#include <libintl.h>
#include <glib/gi18n.h>

#ifndef G_OS_WIN32
#include <gio/gdesktopappinfo.h>
#endif

#include <libedataserver/libedataserver.h>
#include <libedataserverui/libedataserverui.h>
#include "libedataserverui/libedataserverui-private.h"

#include "e-alarm-notify.h"

#ifdef G_OS_UNIX
#include <glib-unix.h>

static gboolean
handle_term_signal (gpointer data)
{
	g_application_quit (data);

	return FALSE;
}
#endif

/* Copy of e_util_is_running_gnome() from Evolution */
static gboolean
e_alarm_notify_is_running_gnome (void)
{
#ifdef G_OS_WIN32
	return FALSE;
#else
	static gint runs_gnome = -1;

	if (runs_gnome == -1) {
		const gchar *desktop;
		desktop = g_getenv ("XDG_CURRENT_DESKTOP");
		runs_gnome = 0;
		if (desktop != NULL) {
			gint ii;
			gchar **desktops = g_strsplit (desktop, ":", -1);
			for (ii = 0; desktops[ii]; ii++) {
				if (!g_ascii_strcasecmp (desktops[ii], "gnome")) {
					runs_gnome = 1;
					break;
				}
			}
			g_strfreev (desktops);
		}

		if (runs_gnome) {
			GDesktopAppInfo *app_info;

			app_info = g_desktop_app_info_new ("gnome-notifications-panel.desktop");
			if (!app_info) {
				runs_gnome = 0;
			}

			g_clear_object (&app_info);
		}
	}

	return runs_gnome != 0;
#endif
}

/* Copy of e_util_is_running_flatpak() from Evolution */
static gboolean
e_alarm_notify_is_running_flatpak (void)
{
#ifdef G_OS_UNIX
	static gint is_flatpak = -1;

	if (is_flatpak == -1) {
		if (g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS) ||
		    g_getenv ("EVOLUTION_FLATPAK") != NULL) /* Only for debugging purposes */
			is_flatpak = 1;
		else
			is_flatpak = 0;
	}

	return is_flatpak == 1;
#else
	return FALSE;
#endif
}


gint
main (gint argc,
      gchar **argv)
{
	EAlarmNotify *alarm_notify;
	gint exit_status;
	GError *error = NULL;

#ifdef G_OS_WIN32
	e_util_win32_initialize ();
#endif

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* Workaround https://bugzilla.gnome.org/show_bug.cgi?id=674885 */
	g_type_ensure (G_TYPE_DBUS_CONNECTION);
	g_type_ensure (G_TYPE_DBUS_PROXY);
	g_type_ensure (G_BUS_TYPE_SESSION);

	if (argc > 1 && e_alarm_notify_is_running_gnome () && !e_alarm_notify_is_running_flatpak ()) {
		gint ii;

		for (ii = 1; ii < argc; ii++) {
			if (g_strcmp0 (argv[ii], "--autostart") == 0) {
				/* running under GNOME, which handles notifications on its own since 51.beta */
				return 0;
			}
		}
	}

	gtk_init (&argc, &argv);

	e_xml_initialize_in_main ();
	_libedataserverui_init_icon_theme ();

	alarm_notify = e_alarm_notify_new (NULL, &error);

	if (error != NULL) {
		g_printerr ("%s\n", error->message);
		g_error_free (error);
		exit (EXIT_FAILURE);
	}

#ifdef G_OS_UNIX
	g_unix_signal_add_full (
		G_PRIORITY_DEFAULT, SIGTERM,
		handle_term_signal, alarm_notify, NULL);
#endif

	exit_status = g_application_run (G_APPLICATION (alarm_notify), argc, argv);

	g_object_unref (alarm_notify);

	return exit_status;
}

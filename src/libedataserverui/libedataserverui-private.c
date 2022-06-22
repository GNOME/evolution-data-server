/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2018 Red Hat, Inc. (www.redhat.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-data-server-config.h"

#include <glib.h>
#include <gtk/gtk.h>

#include "libedataserver/libedataserver.h"
#include "libedataserver/libedataserver-private.h"

#include "libedataserverui-private.h"

/*
 * _libedataserverui_load_modules:
 *
 * Usually called in a GObject::constructed() method to ensure
 * the modules from the UI module directories are loaded.
 *
 * Since: 3.30
 **/
void
_libedataserverui_load_modules (void)
{
	static gboolean modules_loaded = FALSE;

	/* Load modules only once. */
	if (!modules_loaded) {
		GList *module_types;

		modules_loaded = TRUE;

		module_types = e_module_load_all_in_directory_and_prefixes (E_DATA_SERVER_UIMODULEDIR, E_DATA_SERVER_PREFIX);
		g_list_free_full (module_types, (GDestroyNotify) g_type_module_unuse);
	}
}

/*
 * _libedataserverui_init_icon_theme:
 *
 * Adds fallback icons to the gtk+ default theme search path.
 *
 * Since: 3.44
 */
void
_libedataserverui_init_icon_theme (void)
{
	static gboolean icons_added = FALSE;

	#if GTK_CHECK_VERSION(4, 0, 0)
	if (!icons_added) {
		GdkDisplayManager *manager;

		manager = gdk_display_manager_get ();
		if (manager) {
			GSList *displays, *link;

			displays = gdk_display_manager_list_displays (manager);
			icons_added = displays != NULL;

			for (link = displays; link; link = g_slist_next (link)) {
				GdkDisplay *display = link->data;
				GtkIconTheme *icon_theme = gtk_icon_theme_get_for_display (display);

				if (icon_theme)
					gtk_icon_theme_add_search_path (icon_theme, E_DATA_SERVER_ICONDIR);
			}

			g_slist_free (displays);
		}
	}
	#else
	/* The screen can be NULL when building the documentation */
	if (!icons_added && gdk_screen_get_default ()) {
		icons_added = TRUE;

		gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (), E_DATA_SERVER_ICONDIR);
	}
	#endif
}

#if GTK_CHECK_VERSION(4, 0, 0)
typedef struct
{
	gint response_id;
	GMainLoop *loop;
} RunInfo;

static void
shutdown_loop (RunInfo *run_info)
{
	if (g_main_loop_is_running (run_info->loop))
		g_main_loop_quit (run_info->loop);
}

static void
unmap_cb (GtkDialog *dialog,
	  RunInfo *run_info)
{
	shutdown_loop (run_info);
}

static void
response_cb (GtkDialog *dialog,
	     gint response_id,
	     RunInfo *run_info)
{
	run_info->response_id = response_id;
	shutdown_loop (run_info);
}

static gboolean
close_requested_cb (GtkDialog *dialog,
		    RunInfo *run_info)
{
	shutdown_loop (run_info);
	return GDK_EVENT_STOP;
}

static gint
_eds_gtk4_dialog_run (GtkDialog *dialog)
{
	RunInfo run_info;
	gulong close_request_id, reponse_id, unmap_id;

	close_request_id = g_signal_connect (dialog, "close-request", G_CALLBACK (close_requested_cb), &run_info);
	reponse_id = g_signal_connect (dialog, "response", G_CALLBACK (response_cb), &run_info);
	unmap_id = g_signal_connect (dialog, "unmap", G_CALLBACK (unmap_cb), &run_info);

	run_info.response_id = GTK_RESPONSE_NONE;
	run_info.loop = g_main_loop_new (NULL, FALSE);

	if (!gtk_widget_get_visible (GTK_WIDGET (dialog)))
		gtk_window_present (GTK_WINDOW (dialog));

	g_main_loop_run (run_info.loop);
	g_clear_pointer (&run_info.loop, g_main_loop_unref);

	g_signal_handler_disconnect (dialog, close_request_id);
	g_signal_handler_disconnect (dialog, reponse_id);
	g_signal_handler_disconnect (dialog, unmap_id);

	return run_info.response_id;
}

static const gchar *
_eds_gtk4_entry_get_text (GtkEntry *entry)
{
	return gtk_entry_buffer_get_text (gtk_entry_get_buffer (entry));
}

static void
_eds_gtk4_entry_set_text (GtkEntry *entry,
			  const gchar *text)
{
	gtk_entry_buffer_set_text (gtk_entry_get_buffer (entry), text, -1);
}

static void
_eds_gtk4_box_pack_start (GtkBox *box,
			  GtkWidget *child,
			  gboolean expand,
			  gboolean fill,
			  guint padding)
{
	if (gtk_orientable_get_orientation (GTK_ORIENTABLE (box)) == GTK_ORIENTATION_VERTICAL) {
		if (expand)
			gtk_widget_set_hexpand (child, TRUE);
		if (fill)
			gtk_widget_set_halign (child, GTK_ALIGN_FILL);
		if (padding) {
			gtk_widget_set_margin_start (child, padding + gtk_widget_get_margin_start (child));
			gtk_widget_set_margin_end (child, padding + gtk_widget_get_margin_end (child));
		}
	} else {
		if (expand)
			gtk_widget_set_vexpand (child, TRUE);
		if (fill)
			gtk_widget_set_valign (child, GTK_ALIGN_FILL);
		if (padding) {
			gtk_widget_set_margin_top (child, padding + gtk_widget_get_margin_top (child));
			gtk_widget_set_margin_bottom (child, padding + gtk_widget_get_margin_bottom (child));
		}
	}

	gtk_box_append (box, child);
}
#endif

gint
_libedataserverui_dialog_run (GtkDialog *dialog)
{
	#if GTK_CHECK_VERSION(4, 0, 0)
	return _eds_gtk4_dialog_run (dialog);
	#else
	return gtk_dialog_run (dialog);
	#endif
}

const gchar *
_libedataserverui_entry_get_text (GtkEntry *entry)
{
	#if GTK_CHECK_VERSION(4, 0, 0)
	return _eds_gtk4_entry_get_text (entry);
	#else
	return gtk_entry_get_text (entry);
	#endif
}

void
_libedataserverui_entry_set_text (GtkEntry *entry,
				  const gchar *text)
{
	#if GTK_CHECK_VERSION(4, 0, 0)
	_eds_gtk4_entry_set_text (entry, text);
	#else
	gtk_entry_set_text (entry, text);
	#endif
}

void
_libedataserverui_box_pack_start (GtkBox *box,
				  GtkWidget *child,
				  gboolean expand,
				  gboolean fill,
				  guint padding)
{
	#if GTK_CHECK_VERSION(4, 0, 0)
	_eds_gtk4_box_pack_start (box, child, expand, fill, padding);
	#else
	gtk_box_pack_start (box, child, expand, fill, padding);
	#endif
}

/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* test-source-combo-box.c - Test for ESourceComboBox.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-source-combo-box.h"

#include <gtk/gtk.h>

static void
source_changed_cb (ESourceComboBox *combo_box)
{
	ESource *source;

	source = e_source_combo_box_get_active (combo_box);
	g_print ("source selected: \"%s\"\n", e_source_peek_name (source));
}

static gint
on_idle_create_widget (const gchar *gconf_path)
{
	GtkWidget *window;
	GtkWidget *combo_box;
	ESourceList *source_list;
	GConfClient *gconf_client;

	gconf_client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (gconf_client, gconf_path);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	combo_box = e_source_combo_box_new (source_list);
	g_signal_connect (
		combo_box, "changed",
		G_CALLBACK (source_changed_cb), NULL);

	gtk_container_add (GTK_CONTAINER (window), combo_box);
	gtk_widget_show_all (window);

	g_object_unref (gconf_client);
	g_object_unref (source_list);

	return FALSE;
}

gint
main (gint argc, gchar **argv)
{
	const gchar *gconf_path;

	gtk_init (&argc, &argv);

	if (argc < 2)
		gconf_path = "/apps/evolution/calendar/sources";
	else
		gconf_path = argv[1];

	g_idle_add ((GSourceFunc) on_idle_create_widget, (gpointer) gconf_path);

	gtk_main ();

	return 0;
}

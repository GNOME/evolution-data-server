/*
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
 */

#include <libedataserverui/libedataserverui.h>

static gboolean
on_idle_create_widget (void)
{
	GtkWidget *window;
	GtkWidget *vbox;
	GtkWidget *entry;
	GtkEntryCompletion *completion;

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 400, 200);

	g_signal_connect (
		window, "delete-event",
		G_CALLBACK (gtk_main_quit), NULL);

	vbox = gtk_vbox_new (FALSE, 3);
	gtk_container_add (GTK_CONTAINER (window), vbox);

	entry = gtk_entry_new ();
	completion = e_category_completion_new ();
	gtk_entry_set_completion (GTK_ENTRY (entry), completion);
	gtk_box_pack_start (GTK_BOX (vbox), entry, TRUE, TRUE, 0);

	gtk_widget_show_all (window);

	return FALSE;
}

gint
main (gint argc,
      gchar **argv)
{
	gtk_init (&argc, &argv);

	g_idle_add ((GSourceFunc) on_idle_create_widget, NULL);

	gtk_main ();

	return 0;
}

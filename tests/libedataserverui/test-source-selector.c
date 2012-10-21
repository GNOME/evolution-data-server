/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* test-source-list-selector.c - Test program for the ESourceListSelector
 * widget.
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

#include <libedataserverui/libedataserverui.h>

static const gchar *extension_name;

static void
dump_selection (ESourceSelector *selector)
{
	GSList *selection = e_source_selector_get_selection (selector);

	g_print ("Current selection:\n");
	if (selection == NULL) {
		g_print ("\t(None)\n");
	} else {
		GSList *p;

		for (p = selection; p != NULL; p = p->next) {
			ESource *source = E_SOURCE (p->data);
			ESourceBackend *extension;

			extension = e_source_get_extension (
				source, extension_name);

			g_print (
				"\tSource %s (backend %s)\n",
				e_source_get_display_name (source),
				e_source_backend_get_backend_name (extension));
		}
	}

	e_source_selector_free_selection (selection);
}

static void
selection_changed_callback (ESourceSelector *selector)
{
	g_print ("Selection changed!\n");
	dump_selection (selector);
}

static gint
on_idle_create_widget (ESourceRegistry *registry)
{
	GtkWidget *window;
	GtkWidget *vbox;
	GtkWidget *selector;
	GtkWidget *scrolled_window;
	GtkWidget *check;

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 200, 300);

	g_signal_connect (
		window, "delete-event",
		G_CALLBACK (gtk_main_quit), NULL);

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_add (GTK_CONTAINER (window), vbox);

	selector = e_source_selector_new (registry, extension_name);
	g_signal_connect (
		selector, "selection_changed",
		G_CALLBACK (selection_changed_callback), NULL);

	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (
		GTK_SCROLLED_WINDOW (scrolled_window),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (
		GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (scrolled_window), selector);
	gtk_box_pack_start (GTK_BOX (vbox), scrolled_window, TRUE, TRUE, 0);

	check = gtk_check_button_new_with_label ("Show colors");
	gtk_box_pack_start (GTK_BOX (vbox), check, FALSE, TRUE, 0);

	g_object_bind_property (
		selector, "show-colors",
		check, "active",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	check = gtk_check_button_new_with_label ("Show toggles");
	gtk_box_pack_start (GTK_BOX (vbox), check, FALSE, TRUE, 0);

	g_object_bind_property (
		selector, "show-toggles",
		check, "active",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	gtk_widget_show_all (window);

	return FALSE;
}

gint
main (gint argc,
      gchar **argv)
{
	ESourceRegistry *registry;
	GError *error = NULL;

	gtk_init (&argc, &argv);

	if (argc < 2)
		extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	else
		extension_name = argv[1];

	registry = e_source_registry_new_sync (NULL, &error);

	if (error != NULL) {
		g_error (
			"Failed to load ESource registry: %s",
			error->message);
		g_assert_not_reached ();
	}

	g_idle_add ((GSourceFunc) on_idle_create_widget, registry);

	gtk_main ();

	return 0;
}

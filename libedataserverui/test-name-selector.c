/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-name-selector.c - Test for name selector components.
 *
 * Copyright (C) 2004 Novell, Inc.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Hans Petter Jansson <hpj@novell.com>
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "e-name-selector-model.h"
#include "e-name-selector-dialog.h"
#include "e-name-selector-entry.h"
#include <gtk/gtk.h>
#include <libgnomeui/gnome-ui-init.h>
#include <bonobo/bonobo-main.h>
#include <camel/camel.h>

static void
close_dialog (GtkWidget *widget, int response, gpointer data)
{
	GtkWidget *dialog = data;
	
	gtk_widget_destroy (dialog);
	bonobo_main_quit ();
}

static gboolean
start_test (void)
{
	ENameSelectorModel  *name_selector_model;
	ENameSelectorDialog *name_selector_dialog;
	ENameSelectorEntry  *name_selector_entry;
	EDestinationStore   *destination_store;
	GtkWidget           *container;

	destination_store = e_destination_store_new ();
	name_selector_model = e_name_selector_model_new ();

	e_name_selector_model_add_section (name_selector_model, "to", "To", destination_store);
	e_name_selector_model_add_section (name_selector_model, "cc", "Cc", NULL);
	e_name_selector_model_add_section (name_selector_model, "bcc", "Bcc", NULL);

	name_selector_dialog = e_name_selector_dialog_new ();
	e_name_selector_dialog_set_model (name_selector_dialog, name_selector_model);

	name_selector_entry = e_name_selector_entry_new ();
	e_name_selector_entry_set_destination_store (name_selector_entry, destination_store);

 	g_signal_connect (name_selector_dialog, "response", G_CALLBACK (close_dialog), name_selector_dialog);
	gtk_widget_show (GTK_WIDGET (name_selector_dialog));

	container = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_container_add (GTK_CONTAINER (container), GTK_WIDGET (name_selector_entry));
	gtk_widget_show_all (container);

	g_object_unref (name_selector_model);
	g_object_unref (destination_store);
	return FALSE;
}

int
main (int argc, char **argv)
{
	GnomeProgram *program;

	program = gnome_program_init ("test-name-selector", "0.0",
				      LIBGNOMEUI_MODULE, argc, argv,
				      NULL);

	if (bonobo_init (&argc, argv) == FALSE)
		g_error ("Could not initialize Bonobo.");

	camel_init (NULL, 0);
	camel_mime_utils_init ();

	g_idle_add ((GSourceFunc) start_test, NULL);

	bonobo_main ();

	return 0;
}

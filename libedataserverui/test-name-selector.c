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
#include <gtk/gtk.h>
#include <libgnomeui/gnome-ui-init.h>

static gboolean
start_test (const char *gconf_path)
{
	ENameSelectorModel  *name_selector_model;
	ENameSelectorDialog *name_selector_dialog;

	name_selector_dialog = e_name_selector_dialog_new ();
	name_selector_model  = e_name_selector_dialog_peek_model (name_selector_dialog);

	e_name_selector_model_add_section (name_selector_model, "to", "To", NULL);
	e_name_selector_model_add_section (name_selector_model, "cc", "Cc", NULL);
	e_name_selector_model_add_section (name_selector_model, "bcc", "Bcc", NULL);

	gtk_widget_show (GTK_WIDGET (name_selector_dialog));

	return FALSE;
}

int
main (int argc, char **argv)
{
	GnomeProgram *program;
	const char *gconf_path;

	program = gnome_program_init ("test-source-selector", "0.0",
				      LIBGNOMEUI_MODULE, argc, argv,
				      NULL);

	if (bonobo_init (&argc, argv) == FALSE)
		g_error ("Could not initialize Bonobo.");

	if (argc < 2)
		gconf_path = "/apps/evolution/addressbook/sources";
	else
		gconf_path = argv [1];

	camel_init (NULL, 0);
	camel_mime_utils_init ();

	g_idle_add ((GSourceFunc) start_test, (void *) gconf_path);

	bonobo_main ();

	return 0;
}

/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-contact-store.c - Test program for EContactStore.
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
 * Author: Hans Petter Jansson <hpj@novell.com>
 */

#include <libedataserverui/libedataserverui.h>

static void
entry_changed (GtkWidget *entry,
               EContactStore *contact_store)
{
	const gchar *text;
	EBookQuery  *query;

	text = gtk_entry_get_text (GTK_ENTRY (entry));

	query = e_book_query_any_field_contains (text);
	e_contact_store_set_query (contact_store, query);
	e_book_query_unref (query);
}

static GtkTreeViewColumn *
create_text_column_for_field (EContactField field_id)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer   *cell_renderer;

	column = gtk_tree_view_column_new ();
	cell_renderer = GTK_CELL_RENDERER (gtk_cell_renderer_text_new ());
	gtk_tree_view_column_pack_start (column, cell_renderer, TRUE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_title (column, e_contact_pretty_name (field_id));
	gtk_tree_view_column_add_attribute (column, cell_renderer, "text", field_id);
	gtk_tree_view_column_set_sort_column_id (column, field_id);

	return column;
}

static gint
start_test (const gchar *param)
{
	EContactStore *contact_store;
	GtkTreeModel *model_sort;
	GtkWidget *scrolled_window;
	GtkWidget *window;
	GtkWidget *tree_view;
	GtkWidget *box;
	GtkWidget *entry;
	GtkTreeViewColumn *column;
	EBookClient *book_client;
	EBookQuery *book_query;

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

	box = gtk_vbox_new (FALSE, 2);
	gtk_container_add (GTK_CONTAINER (window), box);

	entry = gtk_entry_new ();
	gtk_box_pack_start (GTK_BOX (box), entry, FALSE, TRUE, 0);

	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_box_pack_start (GTK_BOX (box), scrolled_window, TRUE, TRUE, 0);

	contact_store = e_contact_store_new ();
	model_sort = gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (contact_store));
	tree_view = GTK_WIDGET (gtk_tree_view_new ());
	gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view), model_sort);

	column = create_text_column_for_field (E_CONTACT_FILE_AS);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

	column = create_text_column_for_field (E_CONTACT_FULL_NAME);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

	column = create_text_column_for_field (E_CONTACT_EMAIL_1);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

	gtk_container_add (GTK_CONTAINER (scrolled_window), tree_view);

#if 0  /* ACCOUNT_MGMT */
	book_client = e_book_client_new_default (NULL);
	g_warn_if_fail (e_client_open_sync (E_CLIENT (book_client), TRUE, NULL, NULL));
	e_contact_store_add_client (contact_store, book_client);
	g_object_unref (book_client);
#endif /* ACCOUNT_MGMT */

	book_query = e_book_query_any_field_contains ("");
	e_contact_store_set_query (contact_store, book_query);
	e_book_query_unref (book_query);

	g_signal_connect (entry, "changed", G_CALLBACK (entry_changed), contact_store);

	gtk_widget_show_all (window);

	return FALSE;
}

gint
main (gint argc,
      gchar **argv)
{
	const gchar *param;

	gtk_init (&argc, &argv);

	if (argc < 2)
		param = "???";
	else
		param = argv[1];

	g_idle_add ((GSourceFunc) start_test, (gpointer) param);

	gtk_main ();

	return 0;
}

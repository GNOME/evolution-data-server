/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2005 Novell, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <glib/gi18n.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtktreeview.h>
#include <glade/glade-xml.h>
#include <libedataserver/e-categories.h>
#include "e-categories-dialog.h"

struct _ECategoriesDialogPrivate {
	GladeXML *gui;
	GtkWidget *categories_entry;
	GtkWidget *categories_list;

	GHashTable *selected_categories;
};

static GObjectClass *parent_class = NULL;

/* GObject methods */

G_DEFINE_TYPE (ECategoriesDialog, e_categories_dialog, GTK_TYPE_DIALOG)

static void
e_categories_dialog_dispose (GObject *object)
{
	ECategoriesDialogPrivate *priv = E_CATEGORIES_DIALOG (object)->priv;

	if (priv->selected_categories) {
		g_hash_table_destroy (priv->selected_categories);
		priv->selected_categories = NULL;
	}

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
e_categories_dialog_finalize (GObject *object)
{
	ECategoriesDialogPrivate *priv = E_CATEGORIES_DIALOG (object)->priv;

	g_free (priv);
	E_CATEGORIES_DIALOG (object)->priv = NULL;

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
e_categories_dialog_class_init (ECategoriesDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = e_categories_dialog_dispose;
	object_class->finalize = e_categories_dialog_finalize;

	parent_class = g_type_class_peek_parent (klass);
}

static void
e_categories_dialog_init (ECategoriesDialog *dialog)
{
	ECategoriesDialogPrivate *priv;
	GList *cat_list;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkListStore *model;

	priv = g_new0 (ECategoriesDialogPrivate, 1);
	priv->selected_categories = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	dialog->priv = priv;

	/* load the UI from our Glade file */
	priv->gui = glade_xml_new (E_DATA_SERVER_UI_GLADEDIR "/e-categories-dialog.glade", NULL, NULL);
	if (!priv->gui) {
		g_warning (G_STRLOC ": can't load e-categories-dialog.glade file");
		return;
	}

	priv->categories_entry = glade_xml_get_widget (priv->gui, "entry-categories");
	priv->categories_list = glade_xml_get_widget (priv->gui, "categories-list");

	/* set up the categories list */
	model = gtk_list_store_new (2, G_TYPE_BOOLEAN, G_TYPE_STRING);
	cat_list = e_categories_get_list ();
	while (cat_list != NULL) {
		GtkTreeIter iter;

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, 0, FALSE, 1, cat_list->data, -1);

		cat_list = g_list_remove (cat_list, cat_list->data);
	}
	gtk_tree_view_set_model (GTK_TREE_VIEW (priv->categories_list), model);

	renderer = gtk_cell_renderer_toggle_new ();
	column = gtk_tree_view_column_new_with_attributes ("?", renderer, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (priv->categories_list), column);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Category"), renderer, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (priv->categories_list), column);

	/* free memory */
	g_object_unref (model);
}

/**
 * e_categories_dialog_new:
 * @initial_category_list: Comma-separated list of initial categories.
 *
 * Creates a new %ECategoriesDialog widget.
 *
 * Return value: A pointer to the newly created %ECategoriesDialog widget.
 */
GtkWidget *
e_categories_dialog_new (const char *initial_category_list)
{
	ECategoriesDialog *dialog;

	dialog = E_CATEGORIES_DIALOG (g_object_new (E_TYPE_CATEGORIES_DIALOG, NULL));
	if (initial_category_list)
		e_categories_dialog_set_categories (dialog, initial_category_list);

	return GTK_WIDGET (dialog);
}

/**
 * e_categories_dialog_get_categories:
 * @dialog: An #ECategoriesDialog widget.
 *
 * Gets a comma-separated list of the categories currently selected on the dialog.
 *
 * Return value: comma-separated list of categories.
 */
const char *
e_categories_dialog_get_categories (ECategoriesDialog *dialog)
{
	ECategoriesDialogPrivate *priv;

	g_return_val_if_fail (E_IS_CATEGORIES_DIALOG (dialog), NULL);

	priv = dialog->priv;

	return gtk_entry_get_text (GTK_ENTRY (priv->categories_entry));
}

/**
 * e_categories_dialog_set_categories:
 * @dialog: An #ECategoriesDialog widget.
 * @categories: Comma-separated list of categories.
 *
 * Sets the list of categories selected on the dialog.
 */
void
e_categories_dialog_set_categories (ECategoriesDialog *dialog, const char *categories)
{
	ECategoriesDialogPrivate *priv;

	g_return_if_fail (E_IS_CATEGORIES_DIALOG (dialog));

	/* FIXME: update model and hash table */
	gtk_entry_set_text (GTK_ENTRY (priv->categories_entry), categories);
}

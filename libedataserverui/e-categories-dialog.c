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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtkbox.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkcellrenderertoggle.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtktable.h>
#include <gtk/gtkstock.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtktreeview.h>
#include <libgnomeui/gnome-file-entry.h>
#include <glade/glade-xml.h>
#include <libedataserver/e-categories.h>
#include "e-categories-dialog.h"

struct _ECategoriesDialogPrivate {
	GladeXML *gui;
	GtkWidget *categories_entry;
	GtkWidget *categories_list;
	GtkWidget *new_button;
	GtkWidget *edit_button;
	GtkWidget *delete_button;

	GHashTable *selected_categories;
};

static GObjectClass *parent_class = NULL;

/* Category properties dialog */

typedef struct {
	ECategoriesDialog *parent;
	GladeXML *gui;
	GtkWidget *the_dialog;
	GtkWidget *category_name;
	GtkWidget *category_color;
	GtkWidget *category_icon;
} CategoryPropertiesDialog;

static CategoryPropertiesDialog *
load_properties_dialog (ECategoriesDialog *parent)
{
	CategoryPropertiesDialog *prop_dialog;
	GtkWidget *table;

	prop_dialog = g_new0 (CategoryPropertiesDialog, 1);

	prop_dialog->gui = glade_xml_new (E_DATA_SERVER_UI_GLADEDIR "/e-categories-dialog.glade", "properties-dialog", NULL);
	if (!prop_dialog->gui) {
		g_free (prop_dialog);
		return NULL;
	}

	prop_dialog->parent = parent;

	prop_dialog->the_dialog = glade_xml_get_widget (prop_dialog->gui, "properties-dialog");
	gtk_window_set_transient_for (GTK_WINDOW (prop_dialog->the_dialog), GTK_WINDOW (parent));

	prop_dialog->category_name = glade_xml_get_widget (prop_dialog->gui, "category-name");
	prop_dialog->category_color = glade_xml_get_widget (prop_dialog->gui, "category-color");

	/* create the icon file entry */
	table = glade_xml_get_widget (prop_dialog->gui, "table-category-properties");
	prop_dialog->category_icon = gnome_file_entry_new ("category-icon-history-id", _("Category Icon"));
	gtk_table_attach (GTK_TABLE (table), prop_dialog->category_icon, 1, 2, 2, 3, GTK_FILL, GTK_FILL, 3, 3);
	gtk_widget_show (prop_dialog->category_icon);

	return prop_dialog;
}

static void
free_properties_dialog (CategoryPropertiesDialog *prop_dialog)
{
	if (prop_dialog->the_dialog) {
		gtk_widget_destroy (prop_dialog->the_dialog);
		prop_dialog->the_dialog = NULL;
	}

	if (prop_dialog->gui) {
		g_object_unref (prop_dialog->gui);
		prop_dialog->gui = NULL;
	}

	g_free (prop_dialog);
}

/* GObject methods */

G_DEFINE_TYPE (ECategoriesDialog, e_categories_dialog, GTK_TYPE_DIALOG)

static void
e_categories_dialog_dispose (GObject *object)
{
	ECategoriesDialogPrivate *priv = E_CATEGORIES_DIALOG (object)->priv;

	if (priv->gui) {
		g_object_unref (priv->gui);
		priv->gui = NULL;
	}

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
add_comma_sep_categories (gpointer key, gpointer value, gpointer user_data)
{
	GString **str = user_data;

	if (strlen ((GString *) (*str)->str) > 0)
		*str = g_string_append (*str, ",");

	*str = g_string_append (*str, (const char *) key);
}

static void
category_toggled_cb (GtkCellRenderer *renderer, const gchar *path, gpointer user_data)
{
	ECategoriesDialogPrivate *priv;
	GtkTreeIter iter;
	GtkTreeModel *model;
	ECategoriesDialog *dialog = user_data;

	priv = dialog->priv;
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->categories_list));

	if (gtk_tree_model_get_iter_from_string (model, &iter, path)) {
		gboolean place_bool;
		gchar *place_string;
		GString *str;

		gtk_tree_model_get (model, &iter, 0, &place_bool, 1, &place_string, -1);
		if (place_bool) {
			g_hash_table_remove (priv->selected_categories, place_string);
			gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, FALSE, -1);
		} else {
			g_hash_table_insert (priv->selected_categories, g_strdup (place_string), g_strdup (place_string));
			gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, TRUE, -1);
		}

		str = g_string_new ("");
		g_hash_table_foreach (priv->selected_categories, (GHFunc) add_comma_sep_categories, &str);
		gtk_entry_set_text (GTK_ENTRY (priv->categories_entry), str->str);

		/* free memory */
		g_string_free (str, TRUE);
		g_free (place_string);

		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_OK, TRUE);
	}
}

static void
entry_changed_cb (GtkEditable *editable, gpointer user_data)
{
	ECategoriesDialog *dialog = user_data;

	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_OK, TRUE);
}

static void
new_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	ECategoriesDialog *dialog;
	CategoryPropertiesDialog *prop_dialog;

	dialog = user_data;

	prop_dialog = load_properties_dialog (dialog);
	if (!prop_dialog)
		return;

	if (gtk_dialog_run (GTK_DIALOG (prop_dialog->the_dialog)) == GTK_RESPONSE_OK) {
		const char *category_name, *category_icon;
		GtkTreeIter iter;

		category_name = gtk_entry_get_text (GTK_ENTRY (prop_dialog->category_name));
		if (e_categories_exist (category_name)) {
			GtkWidget *error_dialog;

			error_dialog = gtk_message_dialog_new (
				GTK_WINDOW (prop_dialog->the_dialog),
				0, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
				_("There is already a category '%s' in the configuration. Please use another name"),
				category_name);

			gtk_dialog_run (GTK_DIALOG (error_dialog));
			gtk_widget_destroy (error_dialog);
		} else {
			/* FIXME: get color */
			category_icon = gtk_entry_get_text (
				GTK_ENTRY (gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (prop_dialog->category_icon))));

			e_categories_add (category_name, NULL, category_icon ? category_icon : NULL, TRUE);

			gtk_list_store_append (
				GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (prop_dialog->parent->priv->categories_list))),
				&iter);
			gtk_list_store_set (
				GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (prop_dialog->parent->priv->categories_list))),
				0, FALSE, 1, category_name, -1);
		}
	}

	free_properties_dialog (prop_dialog);
}

static void
edit_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	ECategoriesDialog *dialog;
	ECategoriesDialogPrivate *priv;
	CategoryPropertiesDialog *prop_dialog;
	GtkTreeIter iter;
	GtkTreeModel *model;
	char *category_name;

	dialog = user_data;
	priv = dialog->priv;

	/* get the currently selected item */
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->categories_list));

	if (!gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->categories_list)),
					      NULL, &iter))
		return;

	/* load the properties dialog */
	prop_dialog = load_properties_dialog (dialog);
	if (!prop_dialog)
		return;

	gtk_tree_model_get (model, &iter, 1, &category_name, -1);
	gtk_entry_set_text (GTK_ENTRY (prop_dialog->category_name), category_name);
	gtk_widget_set_sensitive (prop_dialog->category_name, FALSE);
	gtk_entry_set_text (
		GTK_ENTRY (gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (prop_dialog->category_icon))),
		e_categories_get_icon_file_for (category_name));

	if (gtk_dialog_run (GTK_DIALOG (prop_dialog->the_dialog)) == GTK_RESPONSE_OK) {
		const char *category_icon;

		/* FIXME: get color */
		category_icon = gtk_entry_get_text (
			GTK_ENTRY (gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (prop_dialog->category_icon))));

		if (category_icon)
			e_categories_set_icon_file_for (category_name, category_icon);
	}

	g_free (category_name);
	free_properties_dialog (prop_dialog);
}

static void
delete_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	ECategoriesDialog *dialog;
	ECategoriesDialogPrivate *priv;
	GtkTreeIter iter;
	GtkTreeModel *model;
	char *category_name;

	dialog = user_data;
	priv = dialog->priv;

	/* get the currently selected item */
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->categories_list));

	if (!gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->categories_list)),
					      NULL, &iter))
		return;

	gtk_tree_model_get (model, &iter, 1, &category_name, -1);
	e_categories_remove (category_name);
	gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
}

static void
e_categories_dialog_init (ECategoriesDialog *dialog)
{
	ECategoriesDialogPrivate *priv;
	GList *cat_list;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkListStore *model;
	GtkWidget *main_widget;

	priv = g_new0 (ECategoriesDialogPrivate, 1);
	priv->selected_categories = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	dialog->priv = priv;

	/* load the UI from our Glade file */
	priv->gui = glade_xml_new (E_DATA_SERVER_UI_GLADEDIR "/e-categories-dialog.glade", "table-categories", NULL);
	if (!priv->gui) {
		g_warning (G_STRLOC ": can't load e-categories-dialog.glade file");
		return;
	}

	main_widget = glade_xml_get_widget (priv->gui, "table-categories");
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), main_widget, TRUE, TRUE, 0);

	priv->categories_entry = glade_xml_get_widget (priv->gui, "entry-categories");
	priv->categories_list = glade_xml_get_widget (priv->gui, "categories-list");

	priv->new_button = glade_xml_get_widget (priv->gui, "button-new");
	g_signal_connect (G_OBJECT (priv->new_button), "clicked", G_CALLBACK (new_button_clicked_cb), dialog);
	priv->edit_button = glade_xml_get_widget (priv->gui, "button-edit");
	g_signal_connect (G_OBJECT (priv->edit_button), "clicked", G_CALLBACK (edit_button_clicked_cb), dialog);
	priv->delete_button = glade_xml_get_widget (priv->gui, "button-delete");
	g_signal_connect (G_OBJECT (priv->delete_button), "clicked", G_CALLBACK (delete_button_clicked_cb), dialog);

	gtk_dialog_add_buttons (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_OK, FALSE);
	gtk_window_set_title (GTK_WINDOW (dialog), _("Categories"));

	/* set up the categories list */
	model = gtk_list_store_new (2, G_TYPE_BOOLEAN, G_TYPE_STRING);
	cat_list = e_categories_get_list ();
	while (cat_list != NULL) {
		GtkTreeIter iter;

		/* only add categories that are user-visible */
		if (e_categories_is_searchable ((const char *) cat_list->data)) {
			gtk_list_store_append (model, &iter);
			gtk_list_store_set (model, &iter, 0, FALSE, 1, cat_list->data, -1);
		}

		cat_list = g_list_remove (cat_list, cat_list->data);
	}
	gtk_tree_view_set_model (GTK_TREE_VIEW (priv->categories_list), GTK_TREE_MODEL (model));

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer), "toggled", G_CALLBACK (category_toggled_cb), dialog);
	column = gtk_tree_view_column_new_with_attributes ("?", renderer,
							   "active", 0, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (priv->categories_list), column);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Category"), renderer,
							   "text", 1, NULL);
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

	g_signal_connect (G_OBJECT (dialog->priv->categories_entry), "changed", entry_changed_cb, dialog);

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
	gchar **arr;
	GtkTreeIter iter;
	GtkTreeModel *model;

	g_return_if_fail (E_IS_CATEGORIES_DIALOG (dialog));

	priv = dialog->priv;

	/* clean up the table of selected categories */
	g_hash_table_foreach_remove (priv->selected_categories, (GHRFunc) gtk_true, NULL);

	arr = g_strsplit (categories, ",", 0);
	if (arr) {
		int i = 0;
		while (arr[i] != NULL) {
			g_hash_table_insert (priv->selected_categories, g_strdup (arr[i]), g_strdup (arr[i]));
			i++;
		}

		g_strfreev (arr);
	}

	/* set the widgets */
	gtk_entry_set_text (GTK_ENTRY (priv->categories_entry), categories);

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->categories_list));
	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			char *place_string;

			gtk_tree_model_get (model, &iter, 1, &place_string, -1);
			if (g_hash_table_lookup (priv->selected_categories, place_string))
				gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, TRUE, -1);
			else
				gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, FALSE, -1);

			g_free (place_string);
		} while (gtk_tree_model_iter_next (model, &iter));
	}
}

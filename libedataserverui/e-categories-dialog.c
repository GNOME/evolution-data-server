/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n-lib.h>
#include "libedataserver/e-categories.h"
#include "libedataserver/libedataserver-private.h"
#include "e-categories-dialog.h"
#include "e-category-completion.h"

#define E_CATEGORIES_DIALOG_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CATEGORIES_DIALOG, ECategoriesDialogPrivate))

G_DEFINE_TYPE (ECategoriesDialog, e_categories_dialog, GTK_TYPE_DIALOG)

struct _ECategoriesDialogPrivate {
	GtkBuilder *gui;
	GtkWidget *categories_entry;
	GtkWidget *categories_list;
	GtkWidget *new_button;
	GtkWidget *edit_button;
	GtkWidget *delete_button;

	GHashTable *selected_categories;

	guint ignore_category_changes : 1;
};

enum {
	COLUMN_ACTIVE,
	COLUMN_ICON,
	COLUMN_CATEGORY,
	N_COLUMNS
};

static gpointer parent_class;

/* Category properties dialog */

typedef struct {
	ECategoriesDialog *parent;
	GtkBuilder *gui;
	GtkWidget *the_dialog;
	GtkWidget *category_name;
	GtkWidget *category_icon;
} CategoryPropertiesDialog;

static void
update_preview (GtkFileChooser *chooser, gpointer user_data)
{
	GtkImage *image;
	gchar *filename;

	g_return_if_fail (chooser != NULL);

	image = GTK_IMAGE (gtk_file_chooser_get_preview_widget (chooser));
	g_return_if_fail (image != NULL);

	filename = gtk_file_chooser_get_preview_filename (chooser);

	gtk_image_set_from_file (image, filename);
	gtk_file_chooser_set_preview_widget_active (chooser, filename != NULL);

	g_free (filename);
}

static void
file_chooser_response (GtkDialog *dialog, gint response_id, GtkFileChooser *button)
{
	g_return_if_fail (button != NULL);

	if (response_id == GTK_RESPONSE_NO) {
		gtk_file_chooser_unselect_all (button);
	}
}

static CategoryPropertiesDialog *
load_properties_dialog (ECategoriesDialog *parent)
{
	CategoryPropertiesDialog *prop_dialog;
	const gchar *ui_to_load[] = { "properties-dialog", NULL };
	gchar *uifile;
	GError *error = NULL;
	GtkWidget *table;

	prop_dialog = g_new0 (CategoryPropertiesDialog, 1);

	uifile = g_build_filename (E_DATA_SERVER_UI_UIDIR,
				      "e-categories-dialog.ui",
				      NULL);
	prop_dialog->gui = gtk_builder_new ();
	gtk_builder_set_translation_domain (prop_dialog->gui, GETTEXT_PACKAGE);

	if (!gtk_builder_add_objects_from_file (prop_dialog->gui, uifile, (gchar **) ui_to_load, &error)) {
		g_object_unref (prop_dialog->gui);
		g_free (prop_dialog);
		g_free (uifile);

		g_warning ("%s: Failed to load e-categories-dialog.ui, %s", G_STRFUNC, error ? error->message : "Unknown error");

		if (error)
			g_error_free (error);

		return NULL;
	}

	g_free (uifile);

	prop_dialog->parent = parent;

	prop_dialog->the_dialog = GTK_WIDGET (gtk_builder_get_object (prop_dialog->gui, "properties-dialog"));
	gtk_window_set_transient_for (GTK_WINDOW (prop_dialog->the_dialog), GTK_WINDOW (parent));

	prop_dialog->category_name = GTK_WIDGET (gtk_builder_get_object (prop_dialog->gui, "category-name"));
	table = GTK_WIDGET (gtk_builder_get_object (prop_dialog->gui, "table-category-properties"));

	if (table) {
		GtkFileChooser *chooser;
		GtkWidget *dialog, *button;
		GtkWidget *image = gtk_image_new ();

		dialog = gtk_file_chooser_dialog_new ( _("Category Icon"),
			NULL,
			GTK_FILE_CHOOSER_ACTION_OPEN,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			NULL);

		button = gtk_button_new_with_mnemonic (_("_No Image"));
		gtk_button_set_image (GTK_BUTTON (button), gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_BUTTON));
		gtk_widget_show (button);
		gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button, GTK_RESPONSE_NO);
		gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT);
		button = NULL;

		chooser = GTK_FILE_CHOOSER (gtk_file_chooser_button_new_with_dialog (dialog));

		gtk_file_chooser_set_local_only (chooser, TRUE);

		g_signal_connect (dialog, "response", (GCallback) file_chooser_response, chooser);

		prop_dialog->category_icon = GTK_WIDGET (chooser);
		gtk_widget_show (prop_dialog->category_icon);
		gtk_table_attach (GTK_TABLE (table), prop_dialog->category_icon, 1, 2, 2, 3, GTK_FILL, GTK_FILL, 0, 0);

		gtk_widget_show (image);

		gtk_file_chooser_set_preview_widget (chooser, image);
		gtk_file_chooser_set_preview_widget_active (chooser, TRUE);

		g_signal_connect (G_OBJECT (chooser), "update-preview", (GCallback) update_preview, NULL);
	}

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

static void
categories_dialog_build_model (ECategoriesDialog *dialog)
{
	GtkTreeView *tree_view;
	GtkListStore *store;
	GList *list, *iter;

	store = gtk_list_store_new (
		N_COLUMNS, G_TYPE_BOOLEAN, GDK_TYPE_PIXBUF, G_TYPE_STRING);

	gtk_tree_sortable_set_sort_column_id (
		GTK_TREE_SORTABLE (store),
		COLUMN_CATEGORY, GTK_SORT_ASCENDING);

	list = e_categories_get_list ();
	for (iter = list; iter != NULL; iter = iter->next) {
		const gchar *category_name = iter->data;
		const gchar *filename;
		GdkPixbuf *pixbuf = NULL;
		GtkTreeIter iter;
		gboolean active;

		/* Only add user-visible categories. */
		if (!e_categories_is_searchable (category_name))
			continue;

		active = (g_hash_table_lookup (
			dialog->priv->selected_categories,
			category_name) != NULL);

		filename = e_categories_get_icon_file_for (category_name);
		if (filename != NULL)
			pixbuf = gdk_pixbuf_new_from_file (filename, NULL);

		gtk_list_store_append (store, &iter);

		gtk_list_store_set (
			store, &iter,
			COLUMN_ACTIVE, active,
			COLUMN_ICON, pixbuf,
			COLUMN_CATEGORY, category_name,
			-1);

		if (pixbuf != NULL)
			g_object_unref (pixbuf);
	}

	tree_view = GTK_TREE_VIEW (dialog->priv->categories_list);
	gtk_tree_view_set_model (tree_view, GTK_TREE_MODEL (store));

	/* This has to be reset everytime we install a new model. */
	gtk_tree_view_set_search_column (tree_view, COLUMN_CATEGORY);

	g_list_free (list);
	g_object_unref (store);
}

static void
categories_dialog_listener_cb (gpointer useless_pointer,
                               ECategoriesDialog *dialog)
{
	/* Don't rebuild the model if we're in
	 * the middle of changing it ourselves. */
	if (dialog->priv->ignore_category_changes)
		return;

	categories_dialog_build_model (dialog);
}

static void
add_comma_sep_categories (gpointer key, gpointer value, gpointer user_data)
{
	GString **str = user_data;

	if (strlen ((*str)->str) > 0)
		*str = g_string_append (*str, ",");

	*str = g_string_append (*str, (const gchar *) key);
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

		gtk_tree_model_get (model, &iter,
				    COLUMN_ACTIVE, &place_bool,
				    COLUMN_CATEGORY, &place_string,
				    -1);
		if (place_bool) {
			g_hash_table_remove (priv->selected_categories, place_string);
			gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_ACTIVE, FALSE, -1);
		} else {
			g_hash_table_insert (priv->selected_categories, g_strdup (place_string), g_strdup (place_string));
			gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_ACTIVE, TRUE, -1);
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

static gchar *
check_category_name (const gchar *name)
{
	GString *str = NULL;
	gchar *p = (gchar *) name;

	str = g_string_new ("");
	while (*p) {
		switch (*p) {
		case ',' :
			break;
		default :
			str = g_string_append_c (str, *p);
		}
		p++;
	}

	p = str->str;
	g_string_free (str, FALSE);

	return p;
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

	do {
		if (gtk_dialog_run (GTK_DIALOG (prop_dialog->the_dialog)) == GTK_RESPONSE_OK) {
			const gchar *category_name;
			gchar *correct_category_name;

			category_name = gtk_entry_get_text (GTK_ENTRY (prop_dialog->category_name));
			correct_category_name = check_category_name (category_name);

			if (e_categories_exist (correct_category_name)) {
				GtkWidget *error_dialog;

				error_dialog = gtk_message_dialog_new (
					GTK_WINDOW (prop_dialog->the_dialog),
					0, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
					_("There is already a category '%s' in the configuration. Please use another name"),
					category_name);

				gtk_dialog_run (GTK_DIALOG (error_dialog));
				gtk_widget_destroy (error_dialog);
				g_free (correct_category_name);
			} else {
				gchar *category_icon;

				category_icon = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (prop_dialog->category_icon));

				e_categories_add (correct_category_name, NULL, category_icon, TRUE);

				g_free (category_icon);
				g_free (correct_category_name);

				break;
			}
		} else
			break;
	} while (TRUE);

	free_properties_dialog (prop_dialog);
}

static void
edit_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	ECategoriesDialog *dialog;
	ECategoriesDialogPrivate *priv;
	CategoryPropertiesDialog *prop_dialog;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeView *tree_view;
	GList *selected;
	gchar *category_name;
	const gchar *icon_file;

	dialog = user_data;
	priv = dialog->priv;

	tree_view = GTK_TREE_VIEW (dialog->priv->categories_list);
	selection = gtk_tree_view_get_selection (tree_view);
	selected = gtk_tree_selection_get_selected_rows (selection, &model);
	g_return_if_fail (g_list_length (selected) == 1);

	/* load the properties dialog */
	prop_dialog = load_properties_dialog (dialog);
	if (!prop_dialog)
		return;

	gtk_tree_model_get_iter (model, &iter, selected->data);
	gtk_tree_model_get (model, &iter, COLUMN_CATEGORY, &category_name, -1);
	gtk_entry_set_text (GTK_ENTRY (prop_dialog->category_name), category_name);
	gtk_widget_set_sensitive (prop_dialog->category_name, FALSE);

	icon_file = e_categories_get_icon_file_for (category_name);
	if (icon_file)
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (prop_dialog->category_icon), icon_file);

	if (gtk_dialog_run (GTK_DIALOG (prop_dialog->the_dialog)) == GTK_RESPONSE_OK) {
		gchar *category_icon;

		category_icon = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (prop_dialog->category_icon));

		e_categories_set_icon_file_for (category_name, category_icon);

		if (category_icon) {
			GdkPixbuf *icon = NULL;

			icon = gdk_pixbuf_new_from_file (category_icon, NULL);
			if (icon) {
				gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_ICON, icon, -1);
				g_object_unref (icon);
			}

			g_free (category_icon);
		} else {
			gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_ICON, NULL, -1);
		}

		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_OK, TRUE);
	}

	g_free (category_name);
	free_properties_dialog (prop_dialog);

	g_list_foreach (selected, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (selected);
}

static void
categories_dialog_delete_cb (ECategoriesDialog *dialog)
{
	GtkTreeSelection *selection;
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GList *selected, *item;

	tree_view = GTK_TREE_VIEW (dialog->priv->categories_list);
	selection = gtk_tree_view_get_selection (tree_view);
	selected = gtk_tree_selection_get_selected_rows (selection, &model);

	/* Remove categories in reverse order to avoid invalidating
	 * tree paths as we iterate over the list.  Note, the list is
	 * probably already sorted but we sort again just to be safe. */
	selected = g_list_reverse (g_list_sort (
		selected, (GCompareFunc) gtk_tree_path_compare));

	/* Prevent the model from being rebuilt every time we
	 * remove a category, since we're already modifying it. */
	dialog->priv->ignore_category_changes = TRUE;

	for (item = selected; item != NULL; item = item->next) {
		GtkTreePath *path = item->data;
		GtkTreeIter iter;
		gchar *category;
		gint column_id;

		column_id = COLUMN_CATEGORY;
		gtk_tree_model_get_iter (model, &iter, path);
		gtk_tree_model_get (model, &iter, column_id, &category, -1);
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
		e_categories_remove (category);
		g_free (category);
	}

	dialog->priv->ignore_category_changes = FALSE;

	/* If we only removed one category, try to select another. */
	if (g_list_length (selected) == 1) {
		GtkTreePath *path = selected->data;

		gtk_tree_selection_select_path (selection, path);
		if (!gtk_tree_selection_path_is_selected (selection, path))
			if (gtk_tree_path_prev (path))
				gtk_tree_selection_select_path (selection, path);
	}

	g_list_foreach (selected, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (selected);
}

static void
categories_dialog_selection_changed_cb (ECategoriesDialog *dialog,
                                        GtkTreeSelection *selection)
{
	GtkWidget *widget;
	gint n_rows;

	n_rows = gtk_tree_selection_count_selected_rows (selection);

	widget = dialog->priv->edit_button;
	gtk_widget_set_sensitive (widget, n_rows == 1);

	widget = dialog->priv->delete_button;
	gtk_widget_set_sensitive (widget, n_rows >= 1);
}

static gboolean
categories_dialog_key_press_event (ECategoriesDialog *dialog,
                                   GdkEventKey *event)
{
	GtkButton *button;

	button = GTK_BUTTON (dialog->priv->delete_button);

	if (event->keyval == GDK_Delete) {
		gtk_button_clicked (button);
		return TRUE;
	}

	return FALSE;
}

static void
categories_dialog_dispose (GObject *object)
{
	ECategoriesDialogPrivate *priv;

	priv = E_CATEGORIES_DIALOG_GET_PRIVATE (object);

	if (priv->gui != NULL) {
		g_object_unref (priv->gui);
		priv->gui = NULL;
	}

	g_hash_table_remove_all (priv->selected_categories);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
categories_dialog_finalize (GObject *object)
{
	ECategoriesDialogPrivate *priv;

	priv = E_CATEGORIES_DIALOG_GET_PRIVATE (object);

	e_categories_unregister_change_listener (
		G_CALLBACK (categories_dialog_listener_cb), object);

	g_hash_table_destroy (priv->selected_categories);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
e_categories_dialog_class_init (ECategoriesDialogClass *class)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (ECategoriesDialogPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = categories_dialog_dispose;
	object_class->finalize = categories_dialog_finalize;
}

static void
e_categories_dialog_init (ECategoriesDialog *dialog)
{
	GtkCellRenderer *renderer;
	GtkEntryCompletion *completion;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	GtkTreeView *tree_view;
	GtkWidget *main_widget;
	GtkWidget *content_area;
	gchar *uifile;
	const gchar *ui_to_load[] = {"table-categories", NULL};
	GError *error = NULL;

	dialog->priv = E_CATEGORIES_DIALOG_GET_PRIVATE (dialog);
	dialog->priv->selected_categories = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	/* load the UI from our UI file */
	uifile = g_build_filename (E_DATA_SERVER_UI_UIDIR,
				      "e-categories-dialog.ui",
				      NULL);
	dialog->priv->gui = gtk_builder_new ();
	gtk_builder_set_translation_domain (dialog->priv->gui, GETTEXT_PACKAGE);

	if (!gtk_builder_add_objects_from_file (dialog->priv->gui, uifile, (gchar **) ui_to_load, &error)) {
		g_free (uifile);
		g_object_unref (dialog->priv->gui);
		dialog->priv->gui = NULL;

		g_warning ("%s: can't load e-categories-dialog.ui file, %s", G_STRFUNC, error ? error->message : "Unknown error");

		if (error)
			g_error_free (error);

		return;
	}

	g_free (uifile);

	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	main_widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->gui, "table-categories"));
	gtk_box_pack_start (GTK_BOX (content_area), main_widget, TRUE, TRUE, 0);

	dialog->priv->categories_entry = GTK_WIDGET (gtk_builder_get_object (dialog->priv->gui, "entry-categories"));
	dialog->priv->categories_list = GTK_WIDGET (gtk_builder_get_object (dialog->priv->gui, "categories-list"));

	tree_view = GTK_TREE_VIEW (dialog->priv->categories_list);
	selection = gtk_tree_view_get_selection (tree_view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	g_signal_connect_swapped (
		selection, "changed",
		G_CALLBACK (categories_dialog_selection_changed_cb), dialog);

	g_signal_connect_swapped (
		dialog->priv->categories_list, "key-press-event",
		G_CALLBACK (categories_dialog_key_press_event), dialog);

	completion = e_category_completion_new ();
	gtk_entry_set_completion (GTK_ENTRY (dialog->priv->categories_entry), completion);
	g_object_unref (completion);

	dialog->priv->new_button = GTK_WIDGET (gtk_builder_get_object (dialog->priv->gui, "button-new"));
	g_signal_connect (G_OBJECT (dialog->priv->new_button), "clicked", G_CALLBACK (new_button_clicked_cb), dialog);
	dialog->priv->edit_button = GTK_WIDGET (gtk_builder_get_object (dialog->priv->gui, "button-edit"));
	g_signal_connect (G_OBJECT (dialog->priv->edit_button), "clicked", G_CALLBACK (edit_button_clicked_cb), dialog);
	dialog->priv->delete_button = GTK_WIDGET (gtk_builder_get_object (dialog->priv->gui, "button-delete"));
	g_signal_connect_swapped (
		G_OBJECT (dialog->priv->delete_button), "clicked",
		G_CALLBACK (categories_dialog_delete_cb), dialog);

	gtk_dialog_add_buttons (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_OK, FALSE);
	gtk_window_set_title (GTK_WINDOW (dialog), _("Categories"));

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer), "toggled", G_CALLBACK (category_toggled_cb), dialog);
	column = gtk_tree_view_column_new_with_attributes ("?", renderer,
							   "active", COLUMN_ACTIVE, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (dialog->priv->categories_list), column);

	renderer = gtk_cell_renderer_pixbuf_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Icon"), renderer,
							   "pixbuf", COLUMN_ICON, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (dialog->priv->categories_list), column);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Category"), renderer,
							   "text", COLUMN_CATEGORY, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (dialog->priv->categories_list), column);

	categories_dialog_build_model (dialog);

	e_categories_register_change_listener (
		G_CALLBACK (categories_dialog_listener_cb), dialog);
}

/**
 * e_categories_dialog_new:
 * @categories: Comma-separated list of categories
 *
 * Creates a new #ECategoriesDialog widget and sets the initial selection
 * to @categories.
 *
 * Returns: a new #ECategoriesDialog
 **/
GtkWidget *
e_categories_dialog_new (const gchar *categories)
{
	ECategoriesDialog *dialog;

	dialog = E_CATEGORIES_DIALOG (g_object_new (E_TYPE_CATEGORIES_DIALOG, NULL));
	if (categories)
		e_categories_dialog_set_categories (dialog, categories);

	g_signal_connect (G_OBJECT (dialog->priv->categories_entry), "changed",
			  G_CALLBACK (entry_changed_cb), dialog);

	return GTK_WIDGET (dialog);
}

/**
 * e_categories_dialog_get_categories:
 * @dialog: An #ECategoriesDialog
 *
 * Gets a comma-separated list of the categories currently selected
 * in the dialog.
 *
 * Returns: a comma-separated list of categories
 **/
const gchar *
e_categories_dialog_get_categories (ECategoriesDialog *dialog)
{
	GtkEntry *entry;
	const gchar *text;

	g_return_val_if_fail (E_IS_CATEGORIES_DIALOG (dialog), NULL);

	entry = GTK_ENTRY (dialog->priv->categories_entry);

	text = gtk_entry_get_text (entry);
	if (text) {
		gint len = strlen (text), old_len = len;

		while (len > 0 && (text [len -1] == ' ' || text [len - 1] == ','))
			len--;

		if (old_len != len) {
			gchar *tmp = g_strndup (text, len);

			gtk_entry_set_text (entry, tmp);

			g_free (tmp);
		}
	}

	return gtk_entry_get_text (entry);
}

/**
 * e_categories_dialog_set_categories:
 * @dialog: An #ECategoriesDialog
 * @categories: Comma-separated list of categories
 *
 * Sets the list of categories selected on the dialog.
 **/
void
e_categories_dialog_set_categories (ECategoriesDialog *dialog,
                                    const gchar *categories)
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
		gint i = 0;
		while (arr[i] != NULL) {
			arr[i] = g_strstrip (arr[i]);

			if (arr[i][0])
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
			gchar *place_string;

			gtk_tree_model_get (model, &iter, COLUMN_CATEGORY, &place_string, -1);
			if (g_hash_table_lookup (priv->selected_categories, place_string))
				gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_ACTIVE, TRUE, -1);
			else
				gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_ACTIVE, FALSE, -1);

			g_free (place_string);
		} while (gtk_tree_model_iter_next (model, &iter));
	}
}

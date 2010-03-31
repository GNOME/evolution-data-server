/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-name-selector-dialog.c - Dialog that lets user pick EDestinations.
 *
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
 *
 * Author: Hans Petter Jansson <hpj@novell.com>
 */

#include <config.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n-lib.h>
#include <libedataserverui/e-source-combo-box.h>
#include <libedataserverui/e-destination-store.h>
#include <libedataserverui/e-contact-store.h>
#include <libedataserverui/e-book-auth-util.h>
#include "libedataserver/e-sexp.h"
#include "libedataserver/e-categories.h"
#include "libedataserver/libedataserver-private.h"
#include "e-name-selector-dialog.h"
#include "e-name-selector-entry.h"

typedef struct {
	gchar        *name;

	GtkBox       *section_box;
	GtkLabel     *label;
	GtkButton    *transfer_button;
	GtkButton    *remove_button;
	GtkTreeView  *destination_view;
}
Section;

typedef struct {
	GtkTreeView *view;
	GtkButton   *button;
	ENameSelectorDialog *dlg_ptr;
} SelData;

typedef struct _ENameSelectorDialogPrivate	ENameSelectorDialogPrivate;
struct _ENameSelectorDialogPrivate
{
	guint destination_index;
	GSList *user_query_fields;
};

static void     search_changed                (ENameSelectorDialog *name_selector_dialog);
static void     source_changed                (ENameSelectorDialog *name_selector_dialog, ESourceComboBox *source_combo_box);
static void     transfer_button_clicked       (ENameSelectorDialog *name_selector_dialog, GtkButton *transfer_button);
static void     contact_selection_changed     (ENameSelectorDialog *name_selector_dialog);
static void     setup_name_selector_model     (ENameSelectorDialog *name_selector_dialog);
static void     shutdown_name_selector_model  (ENameSelectorDialog *name_selector_dialog);
static void     contact_activated             (ENameSelectorDialog *name_selector_dialog, GtkTreePath *path);
static void     destination_activated         (ENameSelectorDialog *name_selector_dialog, GtkTreePath *path,
					       GtkTreeViewColumn *column, GtkTreeView *tree_view);
static gboolean destination_key_press         (ENameSelectorDialog *name_selector_dialog, GdkEventKey *event, GtkTreeView *tree_view);
static void remove_button_clicked (GtkButton *button, SelData *data);
static void     remove_books                  (ENameSelectorDialog *name_selector_dialog);
static void     contact_column_formatter      (GtkTreeViewColumn *column, GtkCellRenderer *cell,
					       GtkTreeModel *model, GtkTreeIter *iter,
					       ENameSelectorDialog *name_selector_dialog);
static void     destination_column_formatter  (GtkTreeViewColumn *column, GtkCellRenderer *cell,
					       GtkTreeModel *model, GtkTreeIter *iter,
					       ENameSelectorDialog *name_selector_dialog);

/* ------------------ *
 * Class/object setup *
 * ------------------ */

G_DEFINE_TYPE (ENameSelectorDialog, e_name_selector_dialog, GTK_TYPE_DIALOG)

#define E_NAME_SELECTOR_DIALOG_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE ((obj), E_TYPE_NAME_SELECTOR_DIALOG, ENameSelectorDialogPrivate))

static void
e_name_selector_dialog_get_property (GObject *object, guint prop_id,
				     GValue *value, GParamSpec *pspec)
{
}

static void
e_name_selector_dialog_set_property (GObject *object, guint prop_id,
				     const GValue *value, GParamSpec *pspec)
{
}

static void
e_name_selector_dialog_populate_categories (ENameSelectorDialog *name_selector_dialog)
{
	GtkWidget *combo_box;
	GList *category_list, *iter;

	/* "Any Category" is preloaded. */
	combo_box = GTK_WIDGET (gtk_builder_get_object (
		name_selector_dialog->gui, "combobox-category"));
	if (gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box)) == -1)
		gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);

	/* Categories are already sorted. */
	category_list = e_categories_get_list ();
	for (iter = category_list; iter != NULL; iter = iter->next)
		gtk_combo_box_append_text (
			GTK_COMBO_BOX (combo_box), iter->data);
	g_list_free (category_list);

	g_signal_connect_swapped (
		combo_box, "changed",
		G_CALLBACK (search_changed), name_selector_dialog);
}

static void
e_name_selector_dialog_init (ENameSelectorDialog *name_selector_dialog)
{
	GtkTreeSelection  *contact_selection;
	GtkTreeViewColumn *column;
	GtkCellRenderer   *cell_renderer;
	GtkWidget         *widget;
	GtkWidget         *container;
	GtkWidget         *content_area;
	GtkWidget	  *label;
	GtkWidget         *parent;
	GtkTreeSelection  *selection;
	ESourceList       *source_list;
	gchar             *uifile;
	GConfClient *gconf_client;
	gchar *uid;
	GError *error = NULL;

	ENameSelectorDialogPrivate *priv = E_NAME_SELECTOR_DIALOG_GET_PRIVATE (name_selector_dialog);
	priv->destination_index = 0;
	priv->user_query_fields = NULL;

	/* Get GtkBuilder GUI */
	uifile = g_build_filename (E_DATA_SERVER_UI_UIDIR,
				"e-name-selector-dialog.ui",
				NULL);
	name_selector_dialog->gui = gtk_builder_new ();
	gtk_builder_set_translation_domain (name_selector_dialog->gui, GETTEXT_PACKAGE);

	if (!gtk_builder_add_from_file (name_selector_dialog->gui, uifile, &error)) {
		g_free (uifile);
		g_object_unref (name_selector_dialog->gui);
		name_selector_dialog->gui = NULL;

		g_warning ("%s: Cannot load e-name-selector-dialog.ui file, %s", G_STRFUNC, error ? error->message : "Unknown error");

		if (error)
			g_error_free (error);

		return;
	}

	g_free (uifile);

	widget = GTK_WIDGET (gtk_builder_get_object (name_selector_dialog->gui, "name-selector-box"));
	if (!widget) {
		g_warning ("%s: Cannot load e-name-selector-dialog.ui file", G_STRFUNC);
		g_object_unref (name_selector_dialog->gui);
		name_selector_dialog->gui = NULL;
		return;
	}

	/* Get addressbook sources */

	if (!e_book_get_addressbooks (&source_list, NULL)) {
		g_warning ("ENameSelectorDialog can't find any addressbooks!");
		g_object_unref (name_selector_dialog->gui);
		return;
	}

	/* Reparent it to inside ourselves */

	content_area = gtk_dialog_get_content_area (
		GTK_DIALOG (name_selector_dialog));

	g_object_ref (widget);
	parent = gtk_widget_get_parent (widget);
	gtk_container_remove (GTK_CONTAINER (parent), widget);
	gtk_box_pack_start (GTK_BOX (content_area), widget, TRUE, TRUE, 0);
	g_object_unref (widget);

	/* Store pointers to relevant widgets */

	name_selector_dialog->contact_view = GTK_TREE_VIEW (
		gtk_builder_get_object (name_selector_dialog->gui, "source-tree-view"));
	name_selector_dialog->status_label = GTK_LABEL (
		gtk_builder_get_object (name_selector_dialog->gui, "status-message"));
	name_selector_dialog->destination_box = GTK_BOX (
		gtk_builder_get_object (name_selector_dialog->gui, "destination-box"));
	name_selector_dialog->search_entry = GTK_ENTRY (
		gtk_builder_get_object (name_selector_dialog->gui, "search"));

	/* Create size group for transfer buttons */

	name_selector_dialog->button_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	/* Set up contacts view */

	column = gtk_tree_view_column_new ();
	cell_renderer = GTK_CELL_RENDERER (gtk_cell_renderer_text_new ());
	gtk_tree_view_column_pack_start (column, cell_renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (column, cell_renderer,
						 (GtkTreeCellDataFunc) contact_column_formatter,
						 name_selector_dialog, NULL);

	selection = gtk_tree_view_get_selection (name_selector_dialog->contact_view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
	gtk_tree_view_append_column (name_selector_dialog->contact_view, column);
	g_signal_connect_swapped (name_selector_dialog->contact_view, "row-activated",
				  G_CALLBACK (contact_activated), name_selector_dialog);

	/* Listen for changes to the contact selection */

	contact_selection = gtk_tree_view_get_selection (name_selector_dialog->contact_view);
	g_signal_connect_swapped (contact_selection, "changed",
				  G_CALLBACK (contact_selection_changed), name_selector_dialog);

	/* Set up our data structures */

	name_selector_dialog->name_selector_model = e_name_selector_model_new ();
	name_selector_dialog->sections            = g_array_new (FALSE, FALSE, sizeof (Section));

	gconf_client = gconf_client_get_default();
	uid = gconf_client_get_string (gconf_client, "/apps/evolution/addressbook/display/primary_addressbook",
			NULL);
	/* read user_query_fields here, because we are using it in call of setup_name_selector_model */
	priv->user_query_fields = gconf_client_get_list (gconf_client, USER_QUERY_FIELDS, GCONF_VALUE_STRING, NULL);
	g_object_unref (gconf_client);

	setup_name_selector_model (name_selector_dialog);

	/* Create source menu */

	widget = e_source_combo_box_new (source_list);
	g_signal_connect_swapped (
		widget, "changed",
		G_CALLBACK (source_changed), name_selector_dialog);
	g_object_unref (source_list);

	if (uid) {
		e_source_combo_box_set_active_uid (
			E_SOURCE_COMBO_BOX (widget), uid);
		g_free (uid);
	}

	label = GTK_WIDGET (gtk_builder_get_object (name_selector_dialog->gui, "AddressBookLabel"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), widget);

	gtk_widget_show (widget);

	container = GTK_WIDGET (gtk_builder_get_object (name_selector_dialog->gui, "source-menu-box"));
	gtk_box_pack_start (GTK_BOX (container), widget, TRUE, TRUE, 0);

	e_name_selector_dialog_populate_categories (name_selector_dialog);

	/* Set up search-as-you-type signal */

	widget = GTK_WIDGET (gtk_builder_get_object (name_selector_dialog->gui, "search"));
	g_signal_connect_swapped (widget, "changed", G_CALLBACK (search_changed), name_selector_dialog);

	/* Display initial source */

	/* TODO: Remember last used source */

	/* Set up dialog defaults */

	gtk_dialog_add_buttons (GTK_DIALOG (name_selector_dialog),
				GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
				NULL);

	gtk_dialog_set_default_response (GTK_DIALOG (name_selector_dialog), GTK_RESPONSE_CLOSE);
	gtk_window_set_modal            (GTK_WINDOW (name_selector_dialog), TRUE);
	gtk_window_set_default_size     (GTK_WINDOW (name_selector_dialog), 472, 512);
	gtk_window_set_resizable        (GTK_WINDOW (name_selector_dialog), TRUE);
	gtk_dialog_set_has_separator    (GTK_DIALOG (name_selector_dialog), FALSE);
	gtk_container_set_border_width  (GTK_CONTAINER (name_selector_dialog), 4);
	gtk_window_set_title            (GTK_WINDOW (name_selector_dialog), _("Select Contacts from Address Book"));
	gtk_widget_grab_focus (widget);
}

/* Partial, repeatable destruction. Release references. */
static void
e_name_selector_dialog_dispose (GObject *object)
{
	ENameSelectorDialog *name_selector_dialog = E_NAME_SELECTOR_DIALOG (object);

	remove_books (name_selector_dialog);
	shutdown_name_selector_model (name_selector_dialog);

	if (G_OBJECT_CLASS (e_name_selector_dialog_parent_class)->dispose)
		G_OBJECT_CLASS (e_name_selector_dialog_parent_class)->dispose (object);
}

/* Final, one-time destruction. Free all. */
static void
e_name_selector_dialog_finalize (GObject *object)
{
	ENameSelectorDialog *name_selector_dialog = E_NAME_SELECTOR_DIALOG (object);
	ENameSelectorDialogPrivate *priv = E_NAME_SELECTOR_DIALOG_GET_PRIVATE (name_selector_dialog);

	if (priv && priv->user_query_fields) {
		g_slist_foreach (priv->user_query_fields, (GFunc)g_free, NULL);
		g_slist_free (priv->user_query_fields);
		priv->user_query_fields = NULL;
	}

	g_array_free (name_selector_dialog->sections, TRUE);
	g_object_unref (name_selector_dialog->button_size_group);

	if (G_OBJECT_CLASS (e_name_selector_dialog_parent_class)->finalize)
		G_OBJECT_CLASS (e_name_selector_dialog_parent_class)->finalize (object);
}

static void
e_name_selector_dialog_class_init (ENameSelectorDialogClass *name_selector_dialog_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (name_selector_dialog_class);

	object_class->get_property = e_name_selector_dialog_get_property;
	object_class->set_property = e_name_selector_dialog_set_property;
	object_class->dispose      = e_name_selector_dialog_dispose;
	object_class->finalize     = e_name_selector_dialog_finalize;

	/* Install properties */

	/* Install signals */

	g_type_class_add_private (object_class, sizeof(ENameSelectorDialogPrivate));

}

/**
 * e_name_selector_dialog_new:
 *
 * Creates a new #ENameSelectorDialog.
 *
 * Returns: A new #ENameSelectorDialog.
 **/
ENameSelectorDialog *
e_name_selector_dialog_new (void)
{
	  return g_object_new (E_TYPE_NAME_SELECTOR_DIALOG, NULL);
}

/* --------- *
 * Utilities *
 * --------- */

static gchar *
escape_sexp_string (const gchar *string)
{
	GString *gstring;
	gchar   *encoded_string;

	gstring = g_string_new ("");
	e_sexp_encode_string (gstring, string);

	encoded_string = gstring->str;
	g_string_free (gstring, FALSE);

	return encoded_string;
}

static void
sort_iter_to_contact_store_iter (ENameSelectorDialog *name_selector_dialog, GtkTreeIter *iter,
				 gint *email_n)
{
	ETreeModelGenerator *contact_filter;
	GtkTreeIter          child_iter;
	gint                 email_n_local;

	contact_filter = e_name_selector_model_peek_contact_filter (name_selector_dialog->name_selector_model);

	gtk_tree_model_sort_convert_iter_to_child_iter (name_selector_dialog->contact_sort,
							&child_iter, iter);
	e_tree_model_generator_convert_iter_to_child_iter (contact_filter, iter, &email_n_local, &child_iter);

	if (email_n)
		*email_n = email_n_local;
}

static void
add_destination (ENameSelectorModel *name_selector_model, EDestinationStore *destination_store, EContact *contact, gint email_n)
{
	EDestination *destination;
	GList *email_list, *nth;

	/* get the correct index of an email in the contact */
	email_list = e_name_selector_model_get_contact_emails_without_used (name_selector_model, contact, FALSE);
	while (nth = g_list_nth (email_list, email_n), nth && nth->data == NULL) {
		email_n++;
	}
	e_name_selector_model_free_emails_list (email_list);

	/* Transfer (actually, copy into a destination and let the model filter out the
	 * source automatically) */

	destination = e_destination_new ();
	e_destination_set_contact (destination, contact, email_n);
	e_destination_store_append_destination (destination_store, destination);
	g_object_unref (destination);
}

static void
remove_books (ENameSelectorDialog *name_selector_dialog)
{
	EContactStore *contact_store;
	GList         *books;
	GList         *l;

	if (!name_selector_dialog->name_selector_model)
		return;

	contact_store = e_name_selector_model_peek_contact_store (name_selector_dialog->name_selector_model);

	/* Remove books (should be just one) being viewed */
	books = e_contact_store_get_books (contact_store);
	for (l = books; l; l = g_list_next (l)) {
		EBook *book = l->data;
		e_contact_store_remove_book (contact_store, book);
	}
	g_list_free (books);

	/* See if we have a book pending; stop loading it if so */
	if (name_selector_dialog->pending_book) {
		e_book_cancel (name_selector_dialog->pending_book, NULL);
		g_object_unref (name_selector_dialog->pending_book);
		name_selector_dialog->pending_book = NULL;
	}
}

/* ------------------ *
 * Section management *
 * ------------------ */

static gint
find_section_by_transfer_button (ENameSelectorDialog *name_selector_dialog, GtkButton *transfer_button)
{
	gint i;

	for (i = 0; i < name_selector_dialog->sections->len; i++) {
		Section *section = &g_array_index (name_selector_dialog->sections, Section, i);

		if (section->transfer_button == transfer_button)
			return i;
	}

	return -1;
}

static gint
find_section_by_tree_view (ENameSelectorDialog *name_selector_dialog, GtkTreeView *tree_view)
{
	gint i;

	for (i = 0; i < name_selector_dialog->sections->len; i++) {
		Section *section = &g_array_index (name_selector_dialog->sections, Section, i);

		if (section->destination_view == tree_view)
			return i;
	}

	return -1;
}

static gint
find_section_by_name (ENameSelectorDialog *name_selector_dialog, const gchar *name)
{
	gint i;

	for (i = 0; i < name_selector_dialog->sections->len; i++) {
		Section *section = &g_array_index (name_selector_dialog->sections, Section, i);

		if (!strcmp (name, section->name))
			return i;
	}

	return -1;
}

static void
selection_changed (GtkTreeSelection *selection, SelData *data)
{
	GtkTreeSelection *contact_selection;
	gboolean          have_selection = FALSE;

	contact_selection = gtk_tree_view_get_selection (data->view);
	if (gtk_tree_selection_count_selected_rows (contact_selection) > 0)
		have_selection = TRUE;
	gtk_widget_set_sensitive (GTK_WIDGET (data->button), have_selection);
}

static gint
add_section (ENameSelectorDialog *name_selector_dialog,
	     const gchar *name, const gchar *pretty_name, EDestinationStore *destination_store)
{
	Section            section;
	GtkTreeViewColumn *column;
	GtkCellRenderer   *cell_renderer;
	GtkWidget	  *vbox, *hbox, *chbox;
	GtkWidget	  *widget, *image, *label;
	SelData		  *data;
	GtkTreeSelection  *selection;
	gchar		  *text;

	g_assert (name != NULL);
	g_assert (pretty_name != NULL);
	g_assert (E_IS_DESTINATION_STORE (destination_store));

	memset (&section, 0, sizeof (Section));

	section.name = g_strdup (name);
	section.section_box      = GTK_BOX (gtk_hbox_new (FALSE, 12));
	section.label = GTK_LABEL (gtk_label_new_with_mnemonic (pretty_name));
	section.transfer_button  = GTK_BUTTON (gtk_button_new());
	section.remove_button  = GTK_BUTTON (gtk_button_new());
	section.destination_view = GTK_TREE_VIEW (gtk_tree_view_new ());

	gtk_label_set_mnemonic_widget (GTK_LABEL (section.label), GTK_WIDGET (section.destination_view));

	if (pango_parse_markup (pretty_name, -1, '_', NULL,
				&text, NULL, NULL))  {
		atk_object_set_name (gtk_widget_get_accessible (
					GTK_WIDGET (section.destination_view)), text);
		g_free (text);
	}

	/* Set up transfer button */
	g_signal_connect_swapped (section.transfer_button, "clicked",
				  G_CALLBACK (transfer_button_clicked), name_selector_dialog);

	/*data for the remove callback*/
	data = g_malloc0(sizeof(SelData));
	data->view = section.destination_view;
	data->dlg_ptr = name_selector_dialog;

	/*Associate to an object destroy so that it gets freed*/
	g_object_set_data_full ((GObject *)section.destination_view, "sel-remove-data", data, g_free);

	g_signal_connect(section.remove_button, "clicked",
				  G_CALLBACK (remove_button_clicked), data);

	/* Set up view */
	column = gtk_tree_view_column_new ();
	cell_renderer = GTK_CELL_RENDERER (gtk_cell_renderer_text_new ());
	gtk_tree_view_column_pack_start (column, cell_renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (column, cell_renderer,
						 (GtkTreeCellDataFunc) destination_column_formatter,
						 name_selector_dialog, NULL);
	gtk_tree_view_append_column (section.destination_view, column);
	gtk_tree_view_set_headers_visible (section.destination_view, FALSE);
	gtk_tree_view_set_model (section.destination_view, GTK_TREE_MODEL (destination_store));

	vbox = gtk_vbox_new (FALSE, 0);
	chbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (chbox), vbox, FALSE, FALSE, 12);

	/* Pack button (in an alignment) */
	widget = gtk_alignment_new (0.5, 0.0, 0.0, 0.0);
	gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET (section.transfer_button));
	gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, TRUE, 6);
	gtk_size_group_add_widget (name_selector_dialog->button_size_group, GTK_WIDGET (section.transfer_button));

	/*to get the image embedded in the button*/
	widget = gtk_alignment_new (0.7, 0.5, 0.0, 0.0);
	gtk_container_add (GTK_CONTAINER (section.transfer_button), GTK_WIDGET (widget));

	hbox = gtk_hbox_new (FALSE, 2);
	gtk_widget_show (GTK_WIDGET(hbox));
	gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET(hbox));

	label = gtk_label_new_with_mnemonic (_("_Add"));
	gtk_widget_show (label);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

	image = gtk_image_new_from_stock ("gtk-go-forward", GTK_ICON_SIZE_BUTTON);
	gtk_widget_show (image);
	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

	widget = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
	gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET (section.remove_button));
	gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, TRUE, 0);
	gtk_size_group_add_widget (name_selector_dialog->button_size_group, GTK_WIDGET (section.remove_button));
	gtk_widget_set_sensitive (GTK_WIDGET (section.remove_button), FALSE);

	widget = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
	gtk_container_add (GTK_CONTAINER (section.remove_button), widget);

	hbox = gtk_hbox_new (FALSE, 2);
	gtk_widget_show (hbox);
	gtk_container_add (GTK_CONTAINER (widget), hbox);

	image = gtk_image_new_from_stock ("gtk-go-back", GTK_ICON_SIZE_BUTTON);
	gtk_widget_show (image);
	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

	label = gtk_label_new_with_mnemonic (_("_Remove"));
	gtk_widget_show (label);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

	vbox = gtk_vbox_new (FALSE, 0);

	widget = gtk_alignment_new (0.5, 0.0, 0.0, 0.0);
	gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET (section.label));
	gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, TRUE, 0);

	/* Pack view (in a scrolled window) */
	widget = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (widget), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (widget), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET (section.destination_view));
	gtk_box_pack_start (GTK_BOX (chbox), widget, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), chbox, TRUE, TRUE, 0);
	gtk_box_pack_start (section.section_box, vbox, TRUE, TRUE, 0);

	/*data for 'changed' callback*/
	data = g_malloc0(sizeof(SelData));
	data->view = section.destination_view;
	data->button = section.remove_button;
	g_object_set_data_full ((GObject *)section.destination_view, "sel-change-data", data, g_free);
	selection = gtk_tree_view_get_selection(section.destination_view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	g_signal_connect(selection, "changed",
				  G_CALLBACK (selection_changed), data);

	g_signal_connect_swapped (section.destination_view, "row-activated",
				  G_CALLBACK (destination_activated), name_selector_dialog);
	g_signal_connect_swapped (section.destination_view, "key-press-event",
				  G_CALLBACK (destination_key_press), name_selector_dialog);

	gtk_widget_show_all (GTK_WIDGET (section.section_box));

	/* Pack this section's box into the dialog */
	gtk_box_pack_start (name_selector_dialog->destination_box,
			    GTK_WIDGET (section.section_box), TRUE, TRUE, 0);

	g_array_append_val (name_selector_dialog->sections, section);

	/* Make sure UI is consistent */
	contact_selection_changed (name_selector_dialog);

	return name_selector_dialog->sections->len - 1;
}

static void
free_section (ENameSelectorDialog *name_selector_dialog, gint n)
{
	Section *section;

	g_assert (n >= 0);
	g_assert (n < name_selector_dialog->sections->len);

	section = &g_array_index (name_selector_dialog->sections, Section, n);

	g_free (section->name);
	gtk_widget_destroy (GTK_WIDGET (section->section_box));
}

static void
model_section_added (ENameSelectorDialog *name_selector_dialog, const gchar *name)
{
	gchar             *pretty_name;
	EDestinationStore *destination_store;

	e_name_selector_model_peek_section (name_selector_dialog->name_selector_model, name,
					    &pretty_name, &destination_store);
	add_section (name_selector_dialog, name, pretty_name, destination_store);
	g_free (pretty_name);
}

static void
model_section_removed (ENameSelectorDialog *name_selector_dialog, const gchar *name)
{
	gint section_index;

	section_index = find_section_by_name (name_selector_dialog, name);
	g_assert (section_index >= 0);

	free_section (name_selector_dialog, section_index);
	g_array_remove_index (name_selector_dialog->sections, section_index);
}

/* -------------------- *
 * Addressbook selector *
 * -------------------- */

static void
status_message(EBookView *view, const gchar *message, ENameSelectorDialog *dialog)
{
	if (message == NULL)
		gtk_label_set_text(dialog->status_label, "");
	else
		gtk_label_set_text(dialog->status_label, message);
}

static void
sequence_complete(EBookView *view, EBookViewStatus status, ENameSelectorDialog *dialog)
{
	status_message(view, NULL, dialog);
}

static void
book_opened (EBook *book, EBookStatus status, gpointer data)
{
	ENameSelectorDialog *name_selector_dialog = E_NAME_SELECTOR_DIALOG (data);
	EContactStore       *contact_store;
	EBookView           *view;

	if (status != E_BOOK_ERROR_OK) {
		/* TODO: Handle errors gracefully */
		gtk_label_set_text(name_selector_dialog->status_label, "Error loading addressbook");
		g_warning ("ENameSelectorDialog failed to open book!");
		return;
	}

	contact_store = e_name_selector_model_peek_contact_store (name_selector_dialog->name_selector_model);
	e_contact_store_add_book (contact_store, book);
	view = find_contact_source_by_book_return_view(contact_store, book);
	g_signal_connect(view, "status_message", G_CALLBACK(status_message), name_selector_dialog);
	g_signal_connect(view, "sequence_complete", G_CALLBACK(sequence_complete), name_selector_dialog);

	g_object_unref (book);
	name_selector_dialog->pending_book = NULL;
}

static void
source_changed (ENameSelectorDialog *name_selector_dialog,
                ESourceComboBox *source_combo_box)
{
	ESource *source;

	source = e_source_combo_box_get_active (source_combo_box);

	/* Remove any previous books being shown or loaded */
	remove_books (name_selector_dialog);

	/* Start loading selected book */
	name_selector_dialog->pending_book = e_load_book_source (source, book_opened,
								 name_selector_dialog);
}

/* --------------- *
 * Other UI events *
 * --------------- */

static void
search_changed (ENameSelectorDialog *name_selector_dialog)
{
	ENameSelectorDialogPrivate *priv = E_NAME_SELECTOR_DIALOG_GET_PRIVATE (name_selector_dialog);
	EContactStore *contact_store;
	EBookQuery    *book_query;
	GtkWidget     *combo_box;
	const gchar   *text;
	gchar         *text_escaped;
	gchar         *query_string;
	gchar         *category;
	gchar         *category_escaped;
	gchar         *user_fields_str;

	combo_box = GTK_WIDGET (gtk_builder_get_object (
		name_selector_dialog->gui, "combobox-category"));
	if (gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box)) == -1)
		gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);

	category = gtk_combo_box_get_active_text (GTK_COMBO_BOX (combo_box));
	category_escaped = escape_sexp_string (category);

	text = gtk_entry_get_text (name_selector_dialog->search_entry);
	text_escaped = escape_sexp_string (text);

	user_fields_str = ens_util_populate_user_query_fields (priv->user_query_fields, text, text_escaped);

	if (!strcmp (category, _("Any Category")))
		query_string = g_strdup_printf (
			"(or (beginswith \"file_as\" %s) "
			"    (beginswith \"full_name\" %s) "
			"    (beginswith \"email\" %s) "
			"    (beginswith \"nickname\" %s)%s))",
			text_escaped, text_escaped,
			text_escaped, text_escaped,
			user_fields_str ? user_fields_str : "");
	else
		query_string = g_strdup_printf (
			"(and (is \"category_list\" %s) "
			"(or (beginswith \"file_as\" %s) "
			"    (beginswith \"full_name\" %s) "
			"    (beginswith \"email\" %s) "
			"    (beginswith \"nickname\" %s)%s))",
			category_escaped, text_escaped, text_escaped,
			text_escaped, text_escaped,
			user_fields_str ? user_fields_str : "");

	book_query = e_book_query_from_string (query_string);

	contact_store = e_name_selector_model_peek_contact_store (
		name_selector_dialog->name_selector_model);
	e_contact_store_set_query (contact_store, book_query);

	e_book_query_unref (book_query);

	g_free (query_string);
	g_free (text_escaped);
	g_free (category_escaped);
	g_free (category);
	g_free (user_fields_str);
}

static void
contact_selection_changed (ENameSelectorDialog *name_selector_dialog)
{
	GtkTreeSelection *contact_selection;
	gboolean          have_selection = FALSE;
	gint              i;

	contact_selection = gtk_tree_view_get_selection (name_selector_dialog->contact_view);
	if (gtk_tree_selection_count_selected_rows (contact_selection))
		have_selection = TRUE;

	for (i = 0; i < name_selector_dialog->sections->len; i++) {
		Section *section = &g_array_index (name_selector_dialog->sections, Section, i);
		gtk_widget_set_sensitive (GTK_WIDGET (section->transfer_button), have_selection);
	}
}

static void
contact_activated (ENameSelectorDialog *name_selector_dialog, GtkTreePath *path)
{
	EContactStore     *contact_store;
	EDestinationStore *destination_store;
	EContact          *contact;
	GtkTreeIter       iter;
	Section           *section;
	gint               email_n;

	ENameSelectorDialogPrivate *priv;

	/* When a contact is activated, we transfer it to the first destination on our list */

	contact_store = e_name_selector_model_peek_contact_store (name_selector_dialog->name_selector_model);

	/* If we have no sections, we can't transfer */
	if (name_selector_dialog->sections->len == 0)
		return;

	/* Get the contact to be transferred */

	if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (name_selector_dialog->contact_sort),
				      &iter, path))
		g_assert_not_reached ();

	sort_iter_to_contact_store_iter (name_selector_dialog, &iter, &email_n);

	contact = e_contact_store_get_contact (contact_store, &iter);
	if (!contact) {
		g_warning ("ENameSelectorDialog could not get selected contact!");
		return;
	}

	priv = E_NAME_SELECTOR_DIALOG_GET_PRIVATE (name_selector_dialog);

	section = &g_array_index (name_selector_dialog->sections, Section, priv->destination_index);
	if (!e_name_selector_model_peek_section (name_selector_dialog->name_selector_model,
						 section->name, NULL, &destination_store)) {
		g_warning ("ENameSelectorDialog has a section unknown to the model!");
		return;
	}

	add_destination (name_selector_dialog->name_selector_model, destination_store, contact, email_n);
}

static void
destination_activated (ENameSelectorDialog *name_selector_dialog, GtkTreePath *path,
		       GtkTreeViewColumn *column, GtkTreeView *tree_view)
{
	gint               section_index;
	EDestinationStore *destination_store;
	EDestination      *destination;
	Section           *section;
	GtkTreeIter        iter;

	/* When a destination is activated, we remove it from the section */

	section_index = find_section_by_tree_view (name_selector_dialog, tree_view);
	if (section_index < 0) {
		g_warning ("ENameSelectorDialog got activation from unknown view!");
		return;
	}

	section = &g_array_index (name_selector_dialog->sections, Section, section_index);
	if (!e_name_selector_model_peek_section (name_selector_dialog->name_selector_model,
						 section->name, NULL, &destination_store)) {
		g_warning ("ENameSelectorDialog has a section unknown to the model!");
		return;
	}

	if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (destination_store), &iter, path))
		g_assert_not_reached ();

	destination = e_destination_store_get_destination (destination_store, &iter);
	g_assert (destination);

	e_destination_store_remove_destination (destination_store, destination);
}

static gboolean
remove_selection (ENameSelectorDialog *name_selector_dialog, GtkTreeView *tree_view)
{
	gint               section_index;
	EDestinationStore *destination_store;
	EDestination      *destination;
	Section           *section;
	GtkTreeSelection  *selection;
	GList		  *rows, *l;

	section_index = find_section_by_tree_view (name_selector_dialog, tree_view);
	if (section_index < 0) {
		g_warning ("ENameSelectorDialog got key press from unknown view!");
		return FALSE;
	}

	section = &g_array_index (name_selector_dialog->sections, Section, section_index);
	if (!e_name_selector_model_peek_section (name_selector_dialog->name_selector_model,
						 section->name, NULL, &destination_store)) {
		g_warning ("ENameSelectorDialog has a section unknown to the model!");
		return FALSE;
	}

	selection = gtk_tree_view_get_selection (tree_view);
	if (!gtk_tree_selection_count_selected_rows (selection)) {
		g_warning ("ENameSelectorDialog remove button clicked, but no selection!");
		return FALSE;
	}

	rows = gtk_tree_selection_get_selected_rows (selection, NULL);
	rows = g_list_reverse (rows);

	for (l = rows; l; l = g_list_next(l)) {
		GtkTreeIter iter;
		GtkTreePath *path = l->data;

		if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (destination_store),
					      &iter, path))
			g_assert_not_reached ();

		gtk_tree_path_free (path);

		destination = e_destination_store_get_destination (destination_store, &iter);
		g_assert (destination);

		e_destination_store_remove_destination (destination_store, destination);
	}
	g_list_free (rows);

	return TRUE;
}

static void
remove_button_clicked (GtkButton *button, SelData *data)
{
	GtkTreeView *view;
	ENameSelectorDialog *name_selector_dialog;

	view = data->view;
	name_selector_dialog = data->dlg_ptr;
	remove_selection (name_selector_dialog, view);
}

static gboolean
destination_key_press (ENameSelectorDialog *name_selector_dialog,
		       GdkEventKey *event, GtkTreeView *tree_view)
{

	/* we only care about DEL key */
	if (event->keyval != GDK_Delete)
		return FALSE;
	return remove_selection (name_selector_dialog, tree_view);

}

static void
transfer_button_clicked (ENameSelectorDialog *name_selector_dialog, GtkButton *transfer_button)
{
	EContactStore     *contact_store;
	EDestinationStore *destination_store;
	GtkTreeSelection  *selection;
	EContact          *contact;
	gint               section_index;
	Section           *section;
	gint               email_n;
	GList		  *rows, *l;

	/* Get the contact to be transferred */

	contact_store = e_name_selector_model_peek_contact_store (name_selector_dialog->name_selector_model);
	selection = gtk_tree_view_get_selection (name_selector_dialog->contact_view);

	if (!gtk_tree_selection_count_selected_rows (selection)) {
		g_warning ("ENameSelectorDialog transfer button clicked, but no selection!");
		return;
	}

	/* Get the target section */
	section_index = find_section_by_transfer_button (name_selector_dialog, transfer_button);
	if (section_index < 0) {
		g_warning ("ENameSelectorDialog got click from unknown button!");
		return;
	}

	section = &g_array_index (name_selector_dialog->sections, Section, section_index);
	if (!e_name_selector_model_peek_section (name_selector_dialog->name_selector_model,
						 section->name, NULL, &destination_store)) {
		g_warning ("ENameSelectorDialog has a section unknown to the model!");
		return;
	}

	rows = gtk_tree_selection_get_selected_rows (selection, NULL);
	rows = g_list_reverse (rows);

	for (l = rows; l; l = g_list_next(l)) {
		GtkTreeIter iter;
		GtkTreePath *path = l->data;

		if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (name_selector_dialog->contact_sort),
				      &iter, path)) {
			gtk_tree_path_free (path);
			return;
		}

		gtk_tree_path_free (path);
		sort_iter_to_contact_store_iter (name_selector_dialog, &iter, &email_n);

		contact = e_contact_store_get_contact (contact_store, &iter);
		if (!contact) {
			g_warning ("ENameSelectorDialog could not get selected contact!");
			g_list_free (rows);
			return;
		}

		add_destination (name_selector_dialog->name_selector_model, destination_store, contact, email_n);
	}
	g_list_free (rows);
}

/* --------------------- *
 * Main model management *
 * --------------------- */

static void
setup_name_selector_model (ENameSelectorDialog *name_selector_dialog)
{
	EContactStore       *contact_store;
	ETreeModelGenerator *contact_filter;
	GList               *new_sections;
	GList               *l;

	/* Create new destination sections in UI */

	new_sections = e_name_selector_model_list_sections (name_selector_dialog->name_selector_model);

	for (l = new_sections; l; l = g_list_next (l)) {
		gchar             *name = l->data;
		gchar             *pretty_name;
		EDestinationStore *destination_store;

		e_name_selector_model_peek_section (name_selector_dialog->name_selector_model,
						    name, &pretty_name, &destination_store);

		add_section (name_selector_dialog, name, pretty_name, destination_store);

		g_free (pretty_name);
		g_free (name);
	}

	g_list_free (new_sections);

	/* Connect to section add/remove signals */

	g_signal_connect_swapped (name_selector_dialog->name_selector_model, "section-added",
				  G_CALLBACK (model_section_added), name_selector_dialog);
	g_signal_connect_swapped (name_selector_dialog->name_selector_model, "section-removed",
				  G_CALLBACK (model_section_removed), name_selector_dialog);

	/* Get contact store and its filter wrapper */

	contact_store  = e_name_selector_model_peek_contact_store  (name_selector_dialog->name_selector_model);
	contact_filter = e_name_selector_model_peek_contact_filter (name_selector_dialog->name_selector_model);

	/* Create sorting model on top of filter, assign it to view */

	name_selector_dialog->contact_sort = GTK_TREE_MODEL_SORT (
		gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (contact_filter)));

	/* sort on full name as we display full name in name selector dialog */
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (name_selector_dialog->contact_sort),
					      E_CONTACT_FULL_NAME, GTK_SORT_ASCENDING);

	gtk_tree_view_set_model (name_selector_dialog->contact_view,
				 GTK_TREE_MODEL (name_selector_dialog->contact_sort));

	/* Make sure UI is consistent */

	search_changed (name_selector_dialog);
	contact_selection_changed (name_selector_dialog);
}

static void
shutdown_name_selector_model (ENameSelectorDialog *name_selector_dialog)
{
	gint i;

	/* Rid UI of previous destination sections */

	for (i = 0; i < name_selector_dialog->sections->len; i++)
		free_section (name_selector_dialog, i);

	g_array_set_size (name_selector_dialog->sections, 0);

	/* Free sorting model */

	if (name_selector_dialog->contact_sort) {
		g_object_unref (name_selector_dialog->contact_sort);
		name_selector_dialog->contact_sort = NULL;
	}

	/* Free backend model */

	if (name_selector_dialog->name_selector_model) {
		g_signal_handlers_disconnect_matched (name_selector_dialog->name_selector_model,
						      G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, name_selector_dialog);

		g_object_unref (name_selector_dialog->name_selector_model);
		name_selector_dialog->name_selector_model = NULL;
	}
}

static void
contact_column_formatter (GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model,
			  GtkTreeIter *iter, ENameSelectorDialog *name_selector_dialog)
{
	EContactStore *contact_store;
	EContact      *contact;
	GtkTreeIter    contact_store_iter;
	GList         *email_list;
	gchar         *string;
	gchar         *full_name_str;
	gchar         *email_str;
	gint           email_n;

	contact_store_iter = *iter;
	sort_iter_to_contact_store_iter (name_selector_dialog, &contact_store_iter, &email_n);

	contact_store = e_name_selector_model_peek_contact_store (name_selector_dialog->name_selector_model);
	contact = e_contact_store_get_contact (contact_store, &contact_store_iter);
	email_list = e_name_selector_model_get_contact_emails_without_used (name_selector_dialog->name_selector_model, contact, TRUE);
	email_str = g_list_nth_data (email_list, email_n);
	full_name_str = e_contact_get (contact, E_CONTACT_FULL_NAME);

	if (e_contact_get (contact, E_CONTACT_IS_LIST)) {
		if (!full_name_str)
			full_name_str = e_contact_get (contact, E_CONTACT_FILE_AS);
		string = g_strdup_printf ("%s", full_name_str ? full_name_str : "?");
	} else {
		string = g_strdup_printf ("%s%s<%s>", full_name_str ? full_name_str : "",
					  full_name_str ? " " : "",
					  email_str ? email_str : "");
	}

	g_free (full_name_str);
	e_name_selector_model_free_emails_list (email_list);

	g_object_set (cell, "text", string, NULL);
	g_free (string);
}

static void
destination_column_formatter (GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model,
			      GtkTreeIter *iter, ENameSelectorDialog *name_selector_dialog)
{
	EDestinationStore *destination_store = E_DESTINATION_STORE (model);
	EDestination      *destination;
	GString           *buffer;

	destination = e_destination_store_get_destination (destination_store, iter);
	g_assert (destination);

	buffer = g_string_new (e_destination_get_name (destination));

	if (!e_destination_is_evolution_list (destination)) {
		const gchar *email;

		email = e_destination_get_email (destination);
		if (email == NULL || *email == '\0')
			email = "?";
		g_string_append_printf (buffer, " <%s>", email);
	}

	g_object_set (cell, "text", buffer->str, NULL);
	g_string_free (buffer, TRUE);
}

/* ----------------------- *
 * ENameSelectorDialog API *
 * ----------------------- */

/**
 * e_name_selector_dialog_peek_model:
 * @name_selector_dialog: an #ENameSelectorDialog
 *
 * Gets the #ENameSelectorModel used by @name_selector_model.
 *
 * Returns: The #ENameSelectorModel being used.
 **/
ENameSelectorModel *
e_name_selector_dialog_peek_model (ENameSelectorDialog *name_selector_dialog)
{
	g_return_val_if_fail (E_IS_NAME_SELECTOR_DIALOG (name_selector_dialog), NULL);

	return name_selector_dialog->name_selector_model;
}

/**
 * e_name_selector_dialog_set_model:
 * @name_selector_dialog: an #ENameSelectorDialog
 * @model: an #ENameSelectorModel
 *
 * Sets the model being used by @name_selector_dialog to @model.
 **/
void
e_name_selector_dialog_set_model (ENameSelectorDialog *name_selector_dialog,
				  ENameSelectorModel  *model)
{
	g_return_if_fail (E_IS_NAME_SELECTOR_DIALOG (name_selector_dialog));
	g_return_if_fail (E_IS_NAME_SELECTOR_MODEL (model));

	if (model == name_selector_dialog->name_selector_model)
		return;

	shutdown_name_selector_model (name_selector_dialog);
	name_selector_dialog->name_selector_model = g_object_ref (model);

	setup_name_selector_model (name_selector_dialog);
}

/**
 * e_name_selector_dialog_set_destination_index:
 * @name_selector_dialog: an #ENameSelectorDialog
 * @index: index of the destination section, starting from 0.
 *
 * Sets the index number of the destination section.
 **/
void
e_name_selector_dialog_set_destination_index (ENameSelectorDialog *name_selector_dialog,
					      guint                index)
{
	ENameSelectorDialogPrivate *priv;

	priv = E_NAME_SELECTOR_DIALOG_GET_PRIVATE (name_selector_dialog);

	if (index >= name_selector_dialog->sections->len)
		return;

	priv->destination_index = index;
}

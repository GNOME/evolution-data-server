/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-selector-dialog.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
 * Author: Rodrigo Moya <rodrigo@novell.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <glib-object.h>
#include "e-source-selector.h"
#include "e-source-selector-dialog.h"

struct _ESourceSelectorDialogPrivate {
	GtkWidget *source_selector;
	ESourceList *source_list;
	ESource *selected_source;
};

static GObjectClass *parent_class = NULL;

/* GObject methods */

G_DEFINE_TYPE (ESourceSelectorDialog, e_source_selector_dialog, GTK_TYPE_DIALOG)

static void
e_source_selector_dialog_dispose (GObject *object)
{
	ESourceSelectorDialogPrivate *priv = E_SOURCE_SELECTOR_DIALOG (object)->priv;

	if (priv->source_list) {
		g_object_unref (priv->source_list);
		priv->source_list = NULL;
	}

	if (priv->selected_source) {
		g_object_unref (priv->selected_source);
		priv->selected_source = NULL;
	}

	(*G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
e_source_selector_dialog_finalize (GObject *object)
{
	ESourceSelectorDialogPrivate *priv = E_SOURCE_SELECTOR_DIALOG (object)->priv;

	g_free (priv);
	E_SOURCE_SELECTOR_DIALOG (object)->priv = NULL;

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
e_source_selector_dialog_class_init (ESourceSelectorDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = e_source_selector_dialog_dispose;
	object_class->finalize = e_source_selector_dialog_finalize;

	parent_class = g_type_class_peek_parent (klass);
}

static void
e_source_selector_dialog_init (ESourceSelectorDialog *dialog)
{
	ESourceSelectorDialogPrivate *priv;
	GtkWidget *action_area;
	GtkWidget *content_area;

	priv = g_new0 (ESourceSelectorDialogPrivate, 1);
	priv->selected_source = NULL;
	dialog->priv = priv;

	action_area = gtk_dialog_get_action_area (GTK_DIALOG (dialog));
	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	/* prepare the dialog */
	gtk_window_set_title (GTK_WINDOW (dialog), _("Select destination"));
	gtk_window_set_default_size (GTK_WINDOW (dialog), 320, 240);
	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_widget_ensure_style (GTK_WIDGET (dialog));
	gtk_container_set_border_width (GTK_CONTAINER (content_area), 0);
	gtk_container_set_border_width (GTK_CONTAINER (action_area), 12);
	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_OK, GTK_RESPONSE_OK,
				NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_OK, FALSE);
}

/* Public API */

static void
row_activated_cb (GtkTreeView *tree_view, GtkTreePath *path,
		  GtkTreeViewColumn *column, GtkWidget *dialog)
{
        gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

static void
primary_selection_changed_cb (ESourceSelector *selector, gpointer user_data)
{
	ESourceSelectorDialog *dialog = user_data;
	ESourceSelectorDialogPrivate *priv = dialog->priv;

	if (priv->selected_source)
		g_object_unref (priv->selected_source);
	priv->selected_source = e_source_selector_peek_primary_selection (selector);

	/* FIXME: add an API to "except-source" or to get the ESourceSelector from outside */
	if (priv->selected_source) {
		ESource *except_source = g_object_get_data (G_OBJECT (dialog), "except-source");

		if (except_source) {
			const gchar *except_uid, *selected_uid;

			except_uid = e_source_peek_uid (except_source);
			selected_uid = e_source_peek_uid (priv->selected_source);

			if (except_uid && selected_uid && g_str_equal (except_uid, selected_uid))
				priv->selected_source = NULL;
		}
	}

	if (priv->selected_source) {
		g_object_ref (priv->selected_source);
		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_OK, TRUE);
	} else
		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_OK, FALSE);
}

static GtkWidget *
setup_dialog (GtkWindow *parent, ESourceSelectorDialog *dialog, ESourceList *source_list)
{
	GtkWidget *vbox, *label, *scroll, *hbox, *spacer;
	GtkWidget *content_area;
	gchar *label_text;
	ESourceSelectorDialogPrivate *priv = dialog->priv;

	priv->source_list = g_object_ref (source_list);

	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	vbox = gtk_vbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
	gtk_container_add (GTK_CONTAINER (content_area), vbox);
	gtk_widget_show (vbox);

	label_text = g_strdup_printf ("<b>%s</b>", _("_Destination"));
	label = gtk_label_new_with_mnemonic (label_text);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	g_free (label_text);
	gtk_widget_show (label);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show (hbox);

	spacer = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (hbox), spacer, FALSE, FALSE, 0);
	gtk_widget_show (spacer);

	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll),
					     GTK_SHADOW_IN);
	gtk_widget_show (scroll);
	priv->source_selector = e_source_selector_new (source_list);
	e_source_selector_show_selection (E_SOURCE_SELECTOR (priv->source_selector), FALSE);
	g_signal_connect (G_OBJECT (priv->source_selector), "row_activated",
			  G_CALLBACK (row_activated_cb), dialog);
	g_signal_connect (G_OBJECT (priv->source_selector), "primary_selection_changed",
			  G_CALLBACK (primary_selection_changed_cb), dialog);
	gtk_widget_show (priv->source_selector);
	gtk_container_add (GTK_CONTAINER (scroll), priv->source_selector);
	gtk_box_pack_start (GTK_BOX (hbox), scroll, TRUE, TRUE, 0);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label), priv->source_selector);

	return GTK_WIDGET (dialog);
}

/**
 * e_source_selector_dialog_new:
 * @parent: Parent window.
 * @source_list: A source list.
 *
 * Create a new source selector dialog for the given @list.
 *
 * Returns: The newly created widget.
 */
GtkWidget *
e_source_selector_dialog_new (GtkWindow *parent, ESourceList *source_list)
{
	ESourceSelectorDialog *dialog;

	g_return_val_if_fail (E_IS_SOURCE_LIST (source_list), NULL);

	dialog = g_object_new (E_TYPE_SOURCE_SELECTOR_DIALOG, NULL);

	return setup_dialog (parent, dialog, source_list);
}

/**
 * e_source_selector_dialog_select_default_source:
 * @dialog: An #ESourceSelectorDialog widget.
 *
 * Selects default source in the dialog.
 *
 * Returns: Whether found any default source.
 *
 * Since: 2.28
 **/
gboolean
e_source_selector_dialog_select_default_source (ESourceSelectorDialog *dialog)
{
	ESourceSelectorDialogPrivate *priv;

	g_return_val_if_fail (E_IS_SOURCE_SELECTOR_DIALOG (dialog), FALSE);

	priv = dialog->priv;

	if (priv->source_list) {
		ESource *default_source = NULL;
		GSList *groups, *g;

		groups = e_source_list_peek_groups (priv->source_list);
		for (g = groups; g != NULL && !default_source; g = g->next) {
			ESourceGroup *group = E_SOURCE_GROUP (g->data);
			GSList *sources, *s;

			sources = e_source_group_peek_sources (group);

			for (s = sources; s != NULL && !default_source; s = s->next) {
				ESource *source = E_SOURCE (s->data);

				if (source && e_source_get_property (source, "default"))
					default_source = source;
			}

		}

		if (default_source)
			e_source_selector_set_primary_selection (E_SOURCE_SELECTOR (priv->source_selector), default_source);

		return default_source != NULL;
	}

	return FALSE;
}

/**
 * e_source_selector_dialog_peek_primary_selection:
 * @dialog: An #ESourceSelectorDialog widget.
 *
 * Peek the currently selected source in the given @dialog.
 *
 * Returns: the selected ESource.
 */
ESource *
e_source_selector_dialog_peek_primary_selection (ESourceSelectorDialog *dialog)
{
	ESourceSelectorDialogPrivate *priv;

	g_return_val_if_fail (E_IS_SOURCE_SELECTOR_DIALOG (dialog), NULL);

	priv = dialog->priv;
	return priv->selected_source;
}

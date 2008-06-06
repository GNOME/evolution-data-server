/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-name-selector-dialog.c - Dialog that lets user pick EDestinations.
 *
 * Copyright (C) 2004 Novell, Inc.
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
 * Authors: Hans Petter Jansson <hpj@novell.com>
 */

#ifndef E_NAME_SELECTOR_DIALOG_H
#define E_NAME_SELECTOR_DIALOG_H

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libebook/e-book.h>
#include <libedataserverui/e-contact-store.h>
#include <libedataserverui/e-name-selector-model.h>

G_BEGIN_DECLS

#define E_TYPE_NAME_SELECTOR_DIALOG            (e_name_selector_dialog_get_type ())
#define E_NAME_SELECTOR_DIALOG(obj)            (GTK_CHECK_CAST ((obj), e_name_selector_dialog_get_type (), ENameSelectorDialog))
#define E_NAME_SELECTOR_DIALOG_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), e_name_selector_dialog_get_type (), ENameSelectorDialogClass))
#define E_IS_NAME_SELECTOR_DIALOG(obj)         (GTK_CHECK_TYPE (obj, e_name_selector_dialog_get_type ()))
#define E_IS_NAME_SELECTOR_DIALOG_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), e_name_selector_dialog_get_type ()))
#define E_NAME_SELECTOR_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_NAME_SELECTOR_DIALOG_TYPE, ENameSelectorDialogClass))

typedef struct _ENameSelectorDialog      ENameSelectorDialog;
typedef struct _ENameSelectorDialogClass ENameSelectorDialogClass;

struct _ENameSelectorDialogClass {
	GtkDialogClass parent_class;
};

struct _ENameSelectorDialog {
	GtkDialog           parent;

	/* Private */

	EBook              *pending_book;
	gpointer            unused;	/* Maintain ABI compatibility */
	ENameSelectorModel *name_selector_model;
	GtkTreeModelSort   *contact_sort;

	GladeXML           *gui;
	GtkTreeView        *contact_view;
	GtkLabel           *status_label;
	GtkBox             *destination_box;
	GtkEntry           *search_entry;
	GtkSizeGroup       *button_size_group;

	GArray             *sections;
};

GType                e_name_selector_dialog_get_type   (void);
ENameSelectorDialog *e_name_selector_dialog_new        (void);

ENameSelectorModel  *e_name_selector_dialog_peek_model (ENameSelectorDialog *name_selector_dialog);
void                 e_name_selector_dialog_set_model  (ENameSelectorDialog *name_selector_dialog,
							ENameSelectorModel  *model);
void                 e_name_selector_dialog_set_destination_index (ENameSelectorDialog *name_selector_dialog,
								   guint                index);


G_END_DECLS

#endif /* E_NAME_SELECTOR_DIALOG_H */

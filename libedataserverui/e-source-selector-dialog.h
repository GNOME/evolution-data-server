/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-selector-dialog.h
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

#ifndef E_SOURCE_SELECTOR_DIALOG_H
#define E_SOURCE_SELECTOR_DIALOG_H

#include <gtk/gtk.h>
#include "libedataserver/e-source-list.h"

/* Standard GObject macros */
#define E_TYPE_SOURCE_SELECTOR_DIALOG \
	(e_source_selector_dialog_get_type ())
#define E_SOURCE_SELECTOR_DIALOG(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_SELECTOR_DIALOG, ESourceSelectorDialog))
#define E_SOURCE_SELECTOR_DIALOG_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_SELECTOR_DIALOG, ESourceSelectorDialogClass))
#define E_IS_SOURCE_SELECTOR_DIALOG(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_SELECTOR_DIALOG))
#define E_IS_SOURCE_SELECTOR_DIALOG_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_SELECTOR_DIALOG))
#define E_SOURCE_SELECTOR_DIALOG_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_SELECTOR_DIALOG, ESourceSelectorDialogClass))

G_BEGIN_DECLS

typedef struct _ESourceSelectorDialog ESourceSelectorDialog;
typedef struct _ESourceSelectorDialogClass ESourceSelectorDialogClass;
typedef struct _ESourceSelectorDialogPrivate ESourceSelectorDialogPrivate;

struct _ESourceSelectorDialog {
	GtkDialog parent;
	ESourceSelectorDialogPrivate *priv;
};

struct _ESourceSelectorDialogClass {
	GtkDialogClass parent_class;
};

GType		e_source_selector_dialog_get_type (void);
GtkWidget *	e_source_selector_dialog_new	(GtkWindow *parent,
						 ESourceList *source_list);
gboolean	e_source_selector_dialog_select_default_source
						(ESourceSelectorDialog *dialog);
ESource *	e_source_selector_dialog_peek_primary_selection
						(ESourceSelectorDialog *dialog);

G_END_DECLS

#endif /* E_SOURCE_SELECTOR_DIALOG_H */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-name-selector.h - Unified context for contact/destination selection UI.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Authors: Hans Petter Jansson <hpj@novell.com>
 */

#ifndef E_NAME_SELECTOR_H
#define E_NAME_SELECTOR_H

#include <glib.h>

#include "libedataserver/e-source-list.h"
#include <libedataserverui/e-name-selector-model.h>
#include <libedataserverui/e-name-selector-dialog.h>
#include <libedataserverui/e-name-selector-entry.h>
#include <libedataserverui/e-name-selector-list.h>

G_BEGIN_DECLS

#define E_TYPE_NAME_SELECTOR		(e_name_selector_get_type ())
#define E_NAME_SELECTOR(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_NAME_SELECTOR, ENameSelector))
#define E_NAME_SELECTOR_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_NAME_SELECTOR, ENameSelectorClass))
#define E_IS_NAME_SELECTOR(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_NAME_SELECTOR))
#define E_IS_NAME_SELECTOR_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), E_TYPE_NAME_SELECTOR))

typedef struct _ENameSelector       ENameSelector;
typedef struct _ENameSelectorClass  ENameSelectorClass;

struct _ENameSelector {
	GObject              parent;

	/* Private */

	ENameSelectorModel  *model;
	ENameSelectorDialog *dialog;

	GArray              *sections;
};

struct _ENameSelectorClass {
	GObjectClass parent_class;
};

GType                e_name_selector_get_type           (void);
ENameSelector       *e_name_selector_new                (void);

ENameSelectorModel  *e_name_selector_peek_model         (ENameSelector *name_selector);
ENameSelectorDialog *e_name_selector_peek_dialog        (ENameSelector *name_selector);
ENameSelectorEntry  *e_name_selector_peek_section_entry (ENameSelector *name_selector, const gchar *name);
ENameSelectorList   *e_name_selector_peek_section_list  (ENameSelector *name_selector, const gchar *name);

G_END_DECLS

#endif /* E_NAME_SELECTOR_H */

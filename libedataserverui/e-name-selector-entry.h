/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-name-selector-entry.c - Single-line text entry widget for EDestinations.
 *
 * Copyright (C) 2004 Novell, Inc.
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
 *
 * Authors: Hans Petter Jansson <hpj@novell.com>
 */

#ifndef E_NAME_SELECTOR_ENTRY_H
#define E_NAME_SELECTOR_ENTRY_H

#include <gtk/gtkentry.h>
#include <libebook/e-contact.h>
#include <libedataserverui/e-contact-store.h>
#include <libedataserverui/e-destination-store.h>

G_BEGIN_DECLS

#define E_TYPE_NAME_SELECTOR_ENTRY            (e_name_selector_entry_get_type ())
#define E_NAME_SELECTOR_ENTRY(obj)            (GTK_CHECK_CAST ((obj), e_name_selector_entry_get_type (), ENameSelectorEntry))
#define E_NAME_SELECTOR_ENTRY_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), e_name_selector_entry_get_type (), ENameSelectorEntryClass))
#define E_IS_NAME_SELECTOR_ENTRY(obj)         (GTK_CHECK_TYPE (obj, e_name_selector_entry_get_type ()))
#define E_IS_NAME_SELECTOR_ENTRY_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), e_name_selector_entry_get_type ()))
#define E_NAME_SELECTOR_ENTRY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_NAME_SELECTOR_ENTRY_TYPE, ENameSelectorEntryClass))

typedef struct ENameSelectorEntry      ENameSelectorEntry;
typedef struct ENameSelectorEntryClass ENameSelectorEntryClass;

struct ENameSelectorEntryClass {
	GtkEntryClass parent_class;
};

struct ENameSelectorEntry {
	GtkEntry            parent;

	/* Private */

	PangoAttrList      *attr_list;
	ESourceList        *source_list;
	EContactStore      *contact_store;
	EDestinationStore  *destination_store;
	GtkEntryCompletion *entry_completion;

	guint               type_ahead_complete_cb_id;
	guint               update_completions_cb_id;
};

GType               e_name_selector_entry_get_type               (void);
ENameSelectorEntry *e_name_selector_entry_new                    (void);

EContactStore      *e_name_selector_entry_peek_contact_store     (ENameSelectorEntry *name_selector_entry);
void                e_name_selector_entry_set_contact_store      (ENameSelectorEntry *name_selector_entry,
								  EContactStore *contact_store);

EDestinationStore  *e_name_selector_entry_peek_destination_store (ENameSelectorEntry *name_selector_entry);
void                e_name_selector_entry_set_destination_store  (ENameSelectorEntry *name_selector_entry,
								  EDestinationStore *destination_store);

G_END_DECLS

#endif /* E_NAME_SELECTOR_ENTRY_H */

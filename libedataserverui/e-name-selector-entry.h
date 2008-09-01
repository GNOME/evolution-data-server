/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-name-selector-entry.c - Single-line text entry widget for EDestinations.
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
 * Authors: Hans Petter Jansson <hpj@novell.com>
 */

#ifndef E_NAME_SELECTOR_ENTRY_H
#define E_NAME_SELECTOR_ENTRY_H

#include <gtk/gtk.h>
#include <libebook/e-contact.h>
#include <libedataserverui/e-contact-store.h>
#include <libedataserverui/e-destination-store.h>
#include <libedataserverui/e-tree-model-generator.h>

G_BEGIN_DECLS

#define E_TYPE_NAME_SELECTOR_ENTRY            (e_name_selector_entry_get_type ())
#define E_NAME_SELECTOR_ENTRY(obj)            (GTK_CHECK_CAST ((obj), e_name_selector_entry_get_type (), ENameSelectorEntry))
#define E_NAME_SELECTOR_ENTRY_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), e_name_selector_entry_get_type (), ENameSelectorEntryClass))
#define E_IS_NAME_SELECTOR_ENTRY(obj)         (GTK_CHECK_TYPE (obj, e_name_selector_entry_get_type ()))
#define E_IS_NAME_SELECTOR_ENTRY_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), e_name_selector_entry_get_type ()))
#define E_NAME_SELECTOR_ENTRY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_NAME_SELECTOR_ENTRY_TYPE, ENameSelectorEntryClass))

#define MINIMUM_QUERY_LENGTH "/apps/evolution/addressbook/completion/minimum_query_length"
#define FORCE_SHOW_ADDRESS   "/apps/evolution/addressbook/completion/show_address"
#define USER_QUERY_FIELDS "/apps/evolution/addressbook/completion/user_query_fields"

typedef struct _ENameSelectorEntry      ENameSelectorEntry;
typedef struct _ENameSelectorEntryClass ENameSelectorEntryClass;

struct _ENameSelectorEntryClass {
	GtkEntryClass parent_class;
	void (*updated) (ENameSelectorEntry *entry, char *email);
	void *reserved1;
	void *reserved2;
};

struct _ENameSelectorEntry {
	GtkEntry             parent;

	/* Private */

	PangoAttrList       *attr_list;
	ESourceList         *source_list;
	EContactStore       *contact_store;
	ETreeModelGenerator *email_generator;
	EDestinationStore   *destination_store;
	GtkEntryCompletion  *entry_completion;

	guint                type_ahead_complete_cb_id;
	guint                update_completions_cb_id;

	EDestination        *popup_destination;

	/* TEMPORARY */
	gpointer             (*contact_editor_func) (EBook *, EContact *, gboolean, gboolean);
	gpointer             (*contact_list_editor_func) (EBook *, EContact *, gboolean, gboolean);
};

GType               e_name_selector_entry_get_type               (void);
ENameSelectorEntry *e_name_selector_entry_new                    (void);

EContactStore      *e_name_selector_entry_peek_contact_store     (ENameSelectorEntry *name_selector_entry);
void                e_name_selector_entry_set_contact_store      (ENameSelectorEntry *name_selector_entry,
								  EContactStore *contact_store);

EDestinationStore  *e_name_selector_entry_peek_destination_store (ENameSelectorEntry *name_selector_entry);
void                e_name_selector_entry_set_destination_store  (ENameSelectorEntry *name_selector_entry,
								  EDestinationStore *destination_store);

/* TEMPORARY API - DO NOT USE */
void                e_name_selector_entry_set_contact_editor_func      (ENameSelectorEntry *name_selector_entry,
									gpointer func);
void                e_name_selector_entry_set_contact_list_editor_func (ENameSelectorEntry *name_selector_entry,
									gpointer func);

gchar *ens_util_populate_user_query_fields (GSList *user_query_fields, const char *cue_str, const char *encoded_cue_str);

G_END_DECLS

#endif /* E_NAME_SELECTOR_ENTRY_H */

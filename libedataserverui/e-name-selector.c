/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-name-selector.c - Unified context for contact/destination selection UI.
 *
 * Copyright (C) 2004 Novell, Inc.
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>
#include <libebook/e-book.h>
#include <libebook/e-contact.h>
#include <libedataserverui/e-contact-store.h>
#include <libedataserverui/e-destination-store.h>
#include <libedataserverui/e-book-auth-util.h>
#include "e-name-selector.h"

#define PRIVATE_SOURCE_BOOKS_KEY "private-source-books"

typedef struct {
	gchar              *name;
	ENameSelectorEntry *entry;
}
Section;

typedef struct {
	EBook *book;
	guint  is_completion_book : 1;
}
SourceBook;

static void  e_name_selector_init       (ENameSelector		 *name_selector);
static void  e_name_selector_class_init (ENameSelectorClass	 *name_selector_class);
static void  e_name_selector_finalize   (GObject                 *object);

/* ------------------ *
 * Class/object setup *
 * ------------------ */

G_DEFINE_TYPE (ENameSelector, e_name_selector, G_TYPE_OBJECT);

static void
source_books_destroy (GArray *source_books)
{
	gint i;

	for (i = 0; i < source_books->len; i++) {
		SourceBook *source_book = &g_array_index (source_books, SourceBook, i);

		g_object_unref (source_book->book);
	}

	g_array_free (source_books, TRUE);
}

static void
e_name_selector_class_init (ENameSelectorClass *name_selector_class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (name_selector_class);

	object_class->finalize = e_name_selector_finalize;
}

static void
e_name_selector_init (ENameSelector *name_selector)
{
	ESourceList *source_list;
	GArray      *source_books;
	GSList      *groups;
	GSList      *l;

	name_selector->sections = g_array_new (FALSE, FALSE, sizeof (Section));
	name_selector->model = e_name_selector_model_new ();

	/* Make a list of books */

	source_books = g_array_new (FALSE, FALSE, sizeof (SourceBook));

	/* This should be a private field, but we use g_object_set_data() to maintain
	 * ABI compatibility in the GNOME 2.10 branch */
	g_object_set_data_full (G_OBJECT (name_selector), PRIVATE_SOURCE_BOOKS_KEY, source_books,
				(GDestroyNotify) source_books_destroy);

	if (!e_book_get_addressbooks (&source_list, NULL)) {
		g_warning ("ENameSelector can't find any addressbooks!");
		return;
	}

	groups = e_source_list_peek_groups (source_list);

	for (l = groups; l; l = g_slist_next (l)) {
		ESourceGroup *group   = l->data;
		GSList       *sources = e_source_group_peek_sources (group);
		GSList       *m;

		for (m = sources; m; m = g_slist_next (m)) {
			ESource      *source = m->data;
			const gchar  *completion;
			SourceBook    source_book;

			/* We're only loading completion books for now, as we don't want
			 * unnecessary auth prompts */
			completion = e_source_get_property (source, "completion");
			if (!completion || g_ascii_strcasecmp (completion, "true"))
				continue;

			source_book.book = e_load_book_source (source, NULL, NULL);
			if (!source_book.book)
				continue;

			source_book.is_completion_book = TRUE;

			g_array_append_val (source_books, source_book);
		}
	}

	g_object_unref (source_list);
}

static void
e_name_selector_finalize (GObject *object)
{
	if (G_OBJECT_CLASS (e_name_selector_parent_class)->finalize)
		G_OBJECT_CLASS (e_name_selector_parent_class)->finalize (object);
}

/**
 * e_name_selector_new:
 *
 * Creates a new #ENameSelector.
 *
 * Return value: A new #ENameSelector.
 **/
ENameSelector *
e_name_selector_new (void)
{
	return E_NAME_SELECTOR (g_object_new (E_TYPE_NAME_SELECTOR, NULL));
}

/* ------- *
 * Helpers *
 * ------- */

static gint
add_section (ENameSelector *name_selector, const gchar *name)
{
	Section section;

	g_assert (name != NULL);

	memset (&section, 0, sizeof (Section));
	section.name = g_strdup (name);

	g_array_append_val (name_selector->sections, section);
	return name_selector->sections->len - 1;
}

static gint
find_section_by_name (ENameSelector *name_selector, const gchar *name)
{
	gint i;

	g_assert (name != NULL);

	for (i = 0; i < name_selector->sections->len; i++) {
		Section *section = &g_array_index (name_selector->sections, Section, i);

		if (!strcmp (name, section->name))
			return i;
	}

	return -1;
}

/* ----------------- *
 * ENameSelector API *
 * ----------------- */

/**
 * e_name_selector_peek_model:
 * @name_selector: an #ENameSelector
 *
 * Gets the #ENameSelectorModel used by @name_selector.
 *
 * Return value: The #ENameSelectorModel used by @name_selector.
 **/
ENameSelectorModel *
e_name_selector_peek_model (ENameSelector *name_selector)
{
	g_return_val_if_fail (E_IS_NAME_SELECTOR (name_selector), NULL);

	return name_selector->model;
}

/**
 * e_name_selector_peek_dialog:
 * @name_selelctor: an #ENameSelector
 *
 * Gets the #ENameSelectorDialog used by @name_selector.
 *
 * Return value: The #ENameSelectorDialog used by @name_selector.
 **/
ENameSelectorDialog *
e_name_selector_peek_dialog (ENameSelector *name_selector)
{
	g_return_val_if_fail (E_IS_NAME_SELECTOR (name_selector), NULL);

	if (!name_selector->dialog) {
		name_selector->dialog = e_name_selector_dialog_new ();
		e_name_selector_dialog_set_model (name_selector->dialog, name_selector->model);
		g_signal_connect (name_selector->dialog, "delete-event",
				  G_CALLBACK (gtk_widget_hide_on_delete), name_selector);
	}

	return name_selector->dialog;
}

/**
 * e_name_selector_peek_section_entry:
 * @name_selector: an #ENameSelector
 * @name: the name of the section to peek
 *
 * Gets the #ENameSelectorEntry for the section specified by @name.
 *
 * Return value: The #ENameSelectorEntry for the named section, or %NULL if it
 * doesn't exist in the #ENameSelectorModel.
 **/
ENameSelectorEntry *
e_name_selector_peek_section_entry (ENameSelector *name_selector, const gchar *name)
{
	EDestinationStore *destination_store;
	Section *section;
	gint     n;

	g_return_val_if_fail (E_IS_NAME_SELECTOR (name_selector), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	if (!e_name_selector_model_peek_section (name_selector->model, name,
						 NULL, &destination_store))
		return NULL;

	n = find_section_by_name (name_selector, name);
	if (n < 0)
		n = add_section (name_selector, name);

	section = &g_array_index (name_selector->sections, Section, n);

	if (!section->entry) {
		GArray        *source_books;
		EContactStore *contact_store;
		gchar         *text;
		gint           i;

		section->entry = e_name_selector_entry_new ();
		if (pango_parse_markup (name, -1, '_', NULL,
					&text, NULL, NULL))  {
			atk_object_set_name (gtk_widget_get_accessible (GTK_WIDGET (section->entry)), text);
			g_free (text);
		}
		e_name_selector_entry_set_destination_store (section->entry, destination_store);

		/* Create a contact store for the entry and assign our already-open books to it */

		contact_store = e_contact_store_new ();
		source_books = g_object_get_data (G_OBJECT (name_selector), PRIVATE_SOURCE_BOOKS_KEY);

		for (i = 0; i < source_books->len; i++) {
			SourceBook *source_book = &g_array_index (source_books, SourceBook, i);

			if (source_book->is_completion_book)
				e_contact_store_add_book (contact_store, source_book->book);
		}

		e_name_selector_entry_set_contact_store (section->entry, contact_store);
		g_object_unref (contact_store);
	}

	return section->entry;
}

/**
 * e_name_selector_peek_section_list:
 * @name_selector: an #ENameSelector
 * @name: the name of the section to peek
 *
 * Gets the #ENameSelectorList for the section specified by @name.
 *
 * Return value: The #ENameSelectorList for the named section, or %NULL if it
 * doesn't exist in the #ENameSelectorModel.
 **/

ENameSelectorList *
e_name_selector_peek_section_list (ENameSelector *name_selector, const gchar *name)
{
	EDestinationStore *destination_store;
	Section *section;
	gint     n;

	g_return_val_if_fail (E_IS_NAME_SELECTOR (name_selector), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	if (!e_name_selector_model_peek_section (name_selector->model, name,
						 NULL, &destination_store))
		return NULL;

	n = find_section_by_name (name_selector, name);
	if (n < 0)
		n = add_section (name_selector, name);

	section = &g_array_index (name_selector->sections, Section, n);

	if (!section->entry) {
		GArray        *source_books;
		EContactStore *contact_store;
		gchar         *text;
		gint           i;

		section->entry = (ENameSelectorEntry *) e_name_selector_list_new ();
		if (pango_parse_markup (name, -1, '_', NULL,
					&text, NULL, NULL))  {
			atk_object_set_name (gtk_widget_get_accessible (GTK_WIDGET (section->entry)), text);
			g_free (text);
	}
		e_name_selector_entry_set_destination_store (section->entry, destination_store);

		/* Create a contact store for the entry and assign our already-open books to it */

		contact_store = e_contact_store_new ();
		source_books = g_object_get_data (G_OBJECT (name_selector), PRIVATE_SOURCE_BOOKS_KEY);

		for (i = 0; i < source_books->len; i++) {
			SourceBook *source_book = &g_array_index (source_books, SourceBook, i);

			if (source_book->is_completion_book)
				e_contact_store_add_book (contact_store, source_book->book);
		}

		e_name_selector_entry_set_contact_store (section->entry, contact_store);
		g_object_unref (contact_store);
	}

	return (ENameSelectorList *)section->entry;
}


/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-name-selector.c - Unified context for contact/destination selection UI.
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

#define E_NAME_SELECTOR_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_NAME_SELECTOR, ENameSelectorPrivate))

typedef struct {
	gchar *name;
	ENameSelectorEntry *entry;
} Section;

typedef struct {
	EBook *book;
	guint is_completion_book : 1;
} SourceBook;

struct _ENameSelectorPrivate {
	ENameSelectorModel *model;
	ENameSelectorDialog *dialog;

	GArray *sections;

	GThread *load_book_thread;
	gboolean load_cancelled;
	GArray *source_books;
};

G_DEFINE_TYPE (ENameSelector, e_name_selector, G_TYPE_OBJECT)

static gpointer
load_books_thread (gpointer user_data)
{
	ENameSelector *name_selector = user_data;
	ENameSelectorPrivate *priv;
	ESourceList *source_list;
	GSList      *groups;
	GSList      *l;

	/* XXX This thread is necessary because the e_book_new can block.
	   See gnome's bug #540779 for more information. */

	g_return_val_if_fail (name_selector != NULL, NULL);

	priv = E_NAME_SELECTOR_GET_PRIVATE (name_selector);

	if (!e_book_get_addressbooks (&source_list, NULL)) {
		g_warning ("ENameSelector can't find any addressbooks!");
		return NULL;
	}

	groups = e_source_list_peek_groups (source_list);

	for (l = groups; l && !priv->load_cancelled; l = g_slist_next (l)) {
		ESourceGroup *group   = l->data;
		GSList       *sources = e_source_group_peek_sources (group);
		GSList       *m;

		for (m = sources; m && !priv->load_cancelled; m = g_slist_next (m)) {
			ESource      *source = m->data;
			const gchar  *completion;
			SourceBook    source_book;

			/* We're only loading completion books for now, as we don't want
			 * unnecessary auth prompts */
			completion = e_source_get_property (source, "completion");
			if (!completion || g_ascii_strcasecmp (completion, "true"))
				continue;

			source_book.book = e_load_book_source_async (source, NULL, NULL);
			if (!source_book.book)
				continue;

			source_book.is_completion_book = TRUE;

			g_array_append_val (priv->source_books, source_book);

			if (!priv->load_cancelled) {
				EContactStore *store;

				if (name_selector->priv->sections) {
					gint j;

					for (j = 0; j < name_selector->priv->sections->len && !priv->load_cancelled; j++) {
						Section *section = &g_array_index (name_selector->priv->sections, Section, j);

						if (section->entry) {
							store = e_name_selector_entry_peek_contact_store (section->entry);
							if (store)
								e_contact_store_add_book (store, source_book.book);
						}
					}
				}
			}
		}
	}

	g_object_unref (source_list);

	return NULL;
}

static void
name_selector_finalize (GObject *object)
{
	ENameSelectorPrivate *priv;

	priv = E_NAME_SELECTOR_GET_PRIVATE (object);

	if (priv->load_book_thread) {
		priv->load_cancelled = TRUE;
		g_thread_join (priv->load_book_thread);
		priv->load_book_thread = NULL;
	}

	if (priv->source_books) {
		gint i;

		for (i = 0; i < priv->source_books->len; i++) {
			SourceBook *source_book = &g_array_index (priv->source_books, SourceBook, i);

			if (source_book->book)
				g_object_unref (source_book->book);
		}

		g_array_free (priv->source_books, TRUE);
		priv->source_books = NULL;
	}

	/* Chain up to parent's finalize() methods. */
	G_OBJECT_CLASS (e_name_selector_parent_class)->finalize (object);
}

static void
e_name_selector_class_init (ENameSelectorClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (ENameSelectorPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = name_selector_finalize;
}

static void
e_name_selector_init (ENameSelector *name_selector)
{
	GArray *sections;
	GArray *source_books;
	GThread *load_book_thread;

	sections = g_array_new (FALSE, FALSE, sizeof (Section));
	source_books = g_array_new (FALSE, FALSE, sizeof (SourceBook));

	load_book_thread = g_thread_create (
		load_books_thread, name_selector, TRUE, NULL);

	name_selector->priv = E_NAME_SELECTOR_GET_PRIVATE (name_selector);
	name_selector->priv->sections = sections;
	name_selector->priv->model = e_name_selector_model_new ();
	name_selector->priv->source_books = source_books;
	name_selector->priv->load_book_thread = load_book_thread;
}

/**
 * e_name_selector_new:
 *
 * Creates a new #ENameSelector.
 *
 * Returns: A new #ENameSelector.
 **/
ENameSelector *
e_name_selector_new (void)
{
	return g_object_new (E_TYPE_NAME_SELECTOR, NULL);
}

/* ------- *
 * Helpers *
 * ------- */

static gint
add_section (ENameSelector *name_selector, const gchar *name)
{
	GArray *array;
	Section section;

	g_assert (name != NULL);

	memset (&section, 0, sizeof (Section));
	section.name = g_strdup (name);

	array = name_selector->priv->sections;
	g_array_append_val (array, section);
	return array->len - 1;
}

static gint
find_section_by_name (ENameSelector *name_selector, const gchar *name)
{
	GArray *array;
	gint i;

	g_assert (name != NULL);

	array = name_selector->priv->sections;

	for (i = 0; i < array->len; i++) {
		Section *section = &g_array_index (array, Section, i);

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
 * Returns: The #ENameSelectorModel used by @name_selector.
 **/
ENameSelectorModel *
e_name_selector_peek_model (ENameSelector *name_selector)
{
	g_return_val_if_fail (E_IS_NAME_SELECTOR (name_selector), NULL);

	return name_selector->priv->model;
}

/**
 * e_name_selector_peek_dialog:
 * @name_selelctor: an #ENameSelector
 *
 * Gets the #ENameSelectorDialog used by @name_selector.
 *
 * Returns: The #ENameSelectorDialog used by @name_selector.
 **/
ENameSelectorDialog *
e_name_selector_peek_dialog (ENameSelector *name_selector)
{
	g_return_val_if_fail (E_IS_NAME_SELECTOR (name_selector), NULL);

	if (name_selector->priv->dialog == NULL) {
		ENameSelectorDialog *dialog;
		ENameSelectorModel *model;

		dialog = e_name_selector_dialog_new ();
		name_selector->priv->dialog = dialog;

		model = e_name_selector_peek_model (name_selector);
		e_name_selector_dialog_set_model (dialog, model);

		g_signal_connect (
			dialog, "delete-event",
			G_CALLBACK (gtk_widget_hide_on_delete), name_selector);
	}

	return name_selector->priv->dialog;
}

/**
 * e_name_selector_show_dialog:
 * @name_selector: an #ENameSelector
 * @for_transient_widget: a widget parent or %NULL
 *
 * Shows the associated dialog, and sets the transient parent to the
 * GtkWindow top-level of "for_transient_widget if set (it should be)
 **/
void
e_name_selector_show_dialog (ENameSelector *name_selector,
			     GtkWidget     *for_transient_widget)
{
	GtkWindow *top = NULL;
	ENameSelectorDialog *dialog;

	g_return_if_fail (E_IS_NAME_SELECTOR (name_selector));

	dialog = e_name_selector_peek_dialog (name_selector);
	if (for_transient_widget)
		top = GTK_WINDOW (gtk_widget_get_toplevel (for_transient_widget));
	if (top)
		gtk_window_set_transient_for (GTK_WINDOW (dialog), top);

	gtk_widget_show (GTK_WIDGET (dialog));
}

/**
 * e_name_selector_peek_section_entry:
 * @name_selector: an #ENameSelector
 * @name: the name of the section to peek
 *
 * Gets the #ENameSelectorEntry for the section specified by @name.
 *
 * Returns: The #ENameSelectorEntry for the named section, or %NULL if it
 * doesn't exist in the #ENameSelectorModel.
 **/
ENameSelectorEntry *
e_name_selector_peek_section_entry (ENameSelector *name_selector, const gchar *name)
{
	ENameSelectorPrivate *priv;
	ENameSelectorModel *model;
	EDestinationStore *destination_store;
	Section *section;
	gint     n;

	g_return_val_if_fail (E_IS_NAME_SELECTOR (name_selector), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	priv = E_NAME_SELECTOR_GET_PRIVATE (name_selector);
	model = e_name_selector_peek_model (name_selector);

	if (!e_name_selector_model_peek_section (
		model, name, NULL, &destination_store))
		return NULL;

	n = find_section_by_name (name_selector, name);
	if (n < 0)
		n = add_section (name_selector, name);

	section = &g_array_index (name_selector->priv->sections, Section, n);

	if (!section->entry) {
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

		for (i = 0; i < priv->source_books->len; i++) {
			SourceBook *source_book = &g_array_index (priv->source_books, SourceBook, i);

			if (source_book->is_completion_book && source_book->book)
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
 * Returns: The #ENameSelectorList for the named section, or %NULL if it
 * doesn't exist in the #ENameSelectorModel.
 **/

ENameSelectorList *
e_name_selector_peek_section_list (ENameSelector *name_selector, const gchar *name)
{
	ENameSelectorPrivate *priv;
	ENameSelectorModel *model;
	EDestinationStore *destination_store;
	Section *section;
	gint     n;

	g_return_val_if_fail (E_IS_NAME_SELECTOR (name_selector), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	priv = E_NAME_SELECTOR_GET_PRIVATE (name_selector);
	model = e_name_selector_peek_model (name_selector);

	if (!e_name_selector_model_peek_section (
		model, name, NULL, &destination_store))
		return NULL;

	n = find_section_by_name (name_selector, name);
	if (n < 0)
		n = add_section (name_selector, name);

	section = &g_array_index (name_selector->priv->sections, Section, n);

	if (!section->entry) {
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
		for (i = 0; i < priv->source_books->len; i++) {
			SourceBook *source_book = &g_array_index (priv->source_books, SourceBook, i);

			if (source_book->is_completion_book && source_book->book)
				e_contact_store_add_book (contact_store, source_book->book);
		}

		e_name_selector_entry_set_contact_store (section->entry, contact_store);
		g_object_unref (contact_store);
	}

	return (ENameSelectorList *)section->entry;
}


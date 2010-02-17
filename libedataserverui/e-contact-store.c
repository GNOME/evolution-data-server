/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-contact-store.c - Contacts store with GtkTreeModel interface.
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
#include <glib/gi18n-lib.h>
#include "e-contact-store.h"

#define ITER_IS_VALID(contact_store, iter) ((iter)->stamp == (contact_store)->stamp)
#define ITER_GET(iter)                     GPOINTER_TO_INT (iter->user_data)
#define ITER_SET(contact_store, iter, index)              \
G_STMT_START {                                            \
	(iter)->stamp = (contact_store)->stamp;               \
	(iter)->user_data = GINT_TO_POINTER (index);        \
} G_STMT_END

static void e_contact_store_tree_model_init (GtkTreeModelIface *iface);

G_DEFINE_TYPE_EXTENDED (EContactStore, e_contact_store, G_TYPE_OBJECT, 0,
	G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL, e_contact_store_tree_model_init))

static void         e_contact_store_finalize        (GObject            *object);
static GtkTreeModelFlags e_contact_store_get_flags       (GtkTreeModel       *tree_model);
static gint         e_contact_store_get_n_columns   (GtkTreeModel       *tree_model);
static GType        e_contact_store_get_column_type (GtkTreeModel       *tree_model,
						     gint                index);
static gboolean     e_contact_store_get_iter        (GtkTreeModel       *tree_model,
						     GtkTreeIter        *iter,
						     GtkTreePath        *path);
static GtkTreePath *e_contact_store_get_path        (GtkTreeModel       *tree_model,
						     GtkTreeIter        *iter);
static void         e_contact_store_get_value       (GtkTreeModel       *tree_model,
						     GtkTreeIter        *iter,
						     gint                column,
						     GValue             *value);
static gboolean     e_contact_store_iter_next       (GtkTreeModel       *tree_model,
						     GtkTreeIter        *iter);
static gboolean     e_contact_store_iter_children   (GtkTreeModel       *tree_model,
						     GtkTreeIter        *iter,
						     GtkTreeIter        *parent);
static gboolean     e_contact_store_iter_has_child  (GtkTreeModel       *tree_model,
						     GtkTreeIter        *iter);
static gint         e_contact_store_iter_n_children (GtkTreeModel       *tree_model,
						     GtkTreeIter        *iter);
static gboolean     e_contact_store_iter_nth_child  (GtkTreeModel       *tree_model,
						     GtkTreeIter        *iter,
						     GtkTreeIter        *parent,
						     gint                n);
static gboolean     e_contact_store_iter_parent     (GtkTreeModel       *tree_model,
						     GtkTreeIter        *iter,
						     GtkTreeIter        *child);

typedef struct
{
	EBook     *book;

	EBookView *book_view;
	GPtrArray *contacts;

	EBookView *book_view_pending;
	GPtrArray *contacts_pending;
}
ContactSource;

static void free_contact_ptrarray (GPtrArray *contacts);
static void clear_contact_source  (EContactStore *contact_store, ContactSource *source);
static void stop_view             (EContactStore *contact_store, EBookView *view);

/* ------------------ *
 * Class/object setup *
 * ------------------ */

static GObjectClass *parent_class = NULL;

static void
e_contact_store_class_init (EContactStoreClass *class)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (class);
	object_class = (GObjectClass *) class;

	object_class->finalize = e_contact_store_finalize;
}

static void
e_contact_store_tree_model_init (GtkTreeModelIface *iface)
{
	iface->get_flags       = e_contact_store_get_flags;
	iface->get_n_columns   = e_contact_store_get_n_columns;
	iface->get_column_type = e_contact_store_get_column_type;
	iface->get_iter        = e_contact_store_get_iter;
	iface->get_path        = e_contact_store_get_path;
	iface->get_value       = e_contact_store_get_value;
	iface->iter_next       = e_contact_store_iter_next;
	iface->iter_children   = e_contact_store_iter_children;
	iface->iter_has_child  = e_contact_store_iter_has_child;
	iface->iter_n_children = e_contact_store_iter_n_children;
	iface->iter_nth_child  = e_contact_store_iter_nth_child;
	iface->iter_parent     = e_contact_store_iter_parent;
}

static void
e_contact_store_init (EContactStore *contact_store)
{
	contact_store->stamp           = g_random_int ();
	contact_store->query           = NULL;
	contact_store->contact_sources = g_array_new (FALSE, FALSE, sizeof (ContactSource));
}

static void
e_contact_store_finalize (GObject *object)
{
	EContactStore *contact_store = E_CONTACT_STORE (object);
	gint           i;

	/* Free sources and cached contacts */

	for (i = 0; i < contact_store->contact_sources->len; i++) {
		ContactSource *source = &g_array_index (contact_store->contact_sources, ContactSource, i);

		clear_contact_source (contact_store, source);

		free_contact_ptrarray (source->contacts);
		g_object_unref (source->book);
	}

	g_array_free (contact_store->contact_sources, TRUE);
	if (contact_store->query)
		e_book_query_unref (contact_store->query);

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/**
 * e_contact_store_new:
 *
 * Creates a new #EContactStore.
 *
 * Return value: A new #EContactStore.
 **/
EContactStore *
e_contact_store_new (void)
{
	return E_CONTACT_STORE (g_object_new (E_TYPE_CONTACT_STORE, NULL));
}

/* ------------------ *
 * Row update helpers *
 * ------------------ */

static void
row_deleted (EContactStore *contact_store, gint n)
{
	GtkTreePath *path;

	path = gtk_tree_path_new ();
	gtk_tree_path_append_index (path, n);
	gtk_tree_model_row_deleted (GTK_TREE_MODEL (contact_store), path);
	gtk_tree_path_free (path);
}

static void
row_inserted (EContactStore *contact_store, gint n)
{
	GtkTreePath *path;
	GtkTreeIter  iter;

	path = gtk_tree_path_new ();
	gtk_tree_path_append_index (path, n);

	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (contact_store), &iter, path))
		gtk_tree_model_row_inserted (GTK_TREE_MODEL (contact_store), path, &iter);

	gtk_tree_path_free (path);
}

static void
row_changed (EContactStore *contact_store, gint n)
{
	GtkTreePath *path;
	GtkTreeIter  iter;

	path = gtk_tree_path_new ();
	gtk_tree_path_append_index (path, n);

	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (contact_store), &iter, path))
		gtk_tree_model_row_changed (GTK_TREE_MODEL (contact_store), path, &iter);

	gtk_tree_path_free (path);
}

/* ---------------------- *
 * Contact source helpers *
 * ---------------------- */

static gint
find_contact_source_by_book (EContactStore *contact_store, EBook *book)
{
	gint i;

	for (i = 0; i < contact_store->contact_sources->len; i++) {
		ContactSource *source;

		source = &g_array_index (contact_store->contact_sources, ContactSource, i);
		if (source->book == book)
			return i;
	}

	return -1;
}

EBookView *
find_contact_source_by_book_return_view(EContactStore *contact_store, EBook *book)
{
	gint i;

	ContactSource *source = NULL;

	for (i = 0; i < contact_store->contact_sources->len; i++) {
		source = &g_array_index (contact_store->contact_sources, ContactSource, i);
		if (source->book == book)
			break;
	}
	return source->book_view;;
}

static gint
find_contact_source_by_view (EContactStore *contact_store, EBookView *book_view)
{
	gint i;

	for (i = 0; i < contact_store->contact_sources->len; i++) {
		ContactSource *source;

		source = &g_array_index (contact_store->contact_sources, ContactSource, i);
		if (source->book_view         == book_view ||
		    source->book_view_pending == book_view)
			return i;
	}

	return -1;
}

static gint
find_contact_source_by_offset (EContactStore *contact_store, gint offset)
{
	gint i;

	for (i = 0; i < contact_store->contact_sources->len; i++) {
		ContactSource *source;

		source = &g_array_index (contact_store->contact_sources, ContactSource, i);
		if (source->contacts->len > offset)
			return i;

		offset -= source->contacts->len;
	}

	return -1;
}

static gint
find_contact_source_by_pointer (EContactStore *contact_store, ContactSource *source)
{
	gint i;

	i = ((gchar *) source - (gchar *) contact_store->contact_sources->data) / sizeof (ContactSource);

	if (i < 0 || i >= contact_store->contact_sources->len)
		return -1;

	return i;
}

static gint
get_contact_source_offset (EContactStore *contact_store, gint contact_source_index)
{
	gint offset = 0;
	gint i;

	g_assert (contact_source_index < contact_store->contact_sources->len);

	for (i = 0; i < contact_source_index; i++) {
		ContactSource *source;

		source = &g_array_index (contact_store->contact_sources, ContactSource, i);
		offset += source->contacts->len;
	}

	return offset;
}

static gint
count_contacts (EContactStore *contact_store)
{
	gint count = 0;
	gint i;

	for (i = 0; i < contact_store->contact_sources->len; i++) {
		ContactSource *source;

		source = &g_array_index (contact_store->contact_sources, ContactSource, i);
		count += source->contacts->len;
	}

	return count;
}

static gint
find_contact_by_view_and_uid (EContactStore *contact_store, EBookView *find_view, const gchar *find_uid)
{
	gint           source_index;
	ContactSource *source;
	GPtrArray     *contacts;
	gint           i;

	g_return_val_if_fail (find_uid != NULL, -1);

	source_index = find_contact_source_by_view (contact_store, find_view);
	if (source_index < 0)
		return -1;

	source = &g_array_index (contact_store->contact_sources, ContactSource, source_index);

	if (find_view == source->book_view)
		contacts = source->contacts;          /* Current view */
	else
		contacts = source->contacts_pending;  /* Pending view */

	for (i = 0; i < contacts->len; i++) {
		EContact    *contact = g_ptr_array_index (contacts, i);
		const gchar *uid     = e_contact_get_const (contact, E_CONTACT_UID);

		if (uid && !strcmp (find_uid, uid))
			return i;
	}

	return -1;
}

static gint
find_contact_by_uid (EContactStore *contact_store, const gchar *find_uid)
{
	gint i;

	for (i = 0; i < contact_store->contact_sources->len; i++) {
		ContactSource *source = &g_array_index (contact_store->contact_sources, ContactSource, i);
		gint           j;

		for (j = 0; j < source->contacts->len; j++) {
			EContact    *contact = g_ptr_array_index (source->contacts, j);
			const gchar *uid     = e_contact_get_const (contact, E_CONTACT_UID);

			if (!strcmp (find_uid, uid))
				return get_contact_source_offset (contact_store, i) + j;
		}
	}

	return -1;
}

static EBook *
get_book_at_row (EContactStore *contact_store, gint row)
{
	ContactSource *source;
	gint           source_index;

	source_index = find_contact_source_by_offset (contact_store, row);
	if (source_index < 0)
		return NULL;

	source = &g_array_index (contact_store->contact_sources, ContactSource, source_index);
	return source->book;
}

static EContact *
get_contact_at_row (EContactStore *contact_store, gint row)
{
	ContactSource *source;
	gint           source_index;
	gint           offset;

	source_index = find_contact_source_by_offset (contact_store, row);
	if (source_index < 0)
		return NULL;

	source = &g_array_index (contact_store->contact_sources, ContactSource, source_index);
	offset = get_contact_source_offset (contact_store, source_index);
	row -= offset;

	g_assert (row < source->contacts->len);

	return g_ptr_array_index (source->contacts, row);
}

static gboolean
find_contact_source_details_by_view (EContactStore *contact_store, EBookView *book_view,
				     ContactSource **contact_source, gint *offset)
{
	gint           source_index;

	source_index = find_contact_source_by_view (contact_store, book_view);
	if (source_index < 0)
		return FALSE;

	*contact_source = &g_array_index (contact_store->contact_sources, ContactSource, source_index);
	*offset = get_contact_source_offset (contact_store, source_index);

	return TRUE;
}

/* ------------------------- *
 * EBookView signal handlers *
 * ------------------------- */

static void
view_contacts_added (EContactStore *contact_store, const GList *contacts, EBookView *book_view)
{
	ContactSource *source;
	gint           offset;
	const GList   *l;

	if (!find_contact_source_details_by_view (contact_store, book_view, &source, &offset)) {
		g_warning ("EContactStore got 'contacts_added' signal from unknown EBookView!");
		return;
	}

	for (l = contacts; l; l = g_list_next (l)) {
		EContact *contact = l->data;

		g_object_ref (contact);

		if (book_view == source->book_view) {
			/* Current view */
			g_ptr_array_add (source->contacts, contact);
			row_inserted (contact_store, offset + source->contacts->len - 1);
		} else {
			/* Pending view */
			g_ptr_array_add (source->contacts_pending, contact);
		}
	}
}

static void
view_contacts_removed (EContactStore *contact_store, const GList *uids, EBookView *book_view)
{
	ContactSource *source;
	gint           offset;
	const GList   *l;

	if (!find_contact_source_details_by_view (contact_store, book_view, &source, &offset)) {
		g_warning ("EContactStore got 'contacts_removed' signal from unknown EBookView!");
		return;
	}

	for (l = uids; l; l = g_list_next (l)) {
		const gchar *uid = l->data;
		gint         n   = find_contact_by_view_and_uid (contact_store, book_view, uid);
		EContact    *contact;

		if (n < 0) {
			g_warning ("EContactStore got 'contacts_removed' on unknown contact!");
			continue;
		}

		if (book_view == source->book_view) {
			/* Current view */
			contact = g_ptr_array_index (source->contacts, n);
			g_object_unref (contact);
			g_ptr_array_remove_index (source->contacts, n);
			row_deleted (contact_store, offset + n);
		} else {
			/* Pending view */
			contact = g_ptr_array_index (source->contacts_pending, n);
			g_object_unref (contact);
			g_ptr_array_remove_index (source->contacts_pending, n);
		}
	}
}

static void
view_contacts_changed (EContactStore *contact_store, const GList *contacts, EBookView *book_view)
{
	GPtrArray     *cached_contacts;
	ContactSource *source;
	gint           offset;
	const GList   *l;

	if (!find_contact_source_details_by_view (contact_store, book_view, &source, &offset)) {
		g_warning ("EContactStore got 'contacts_changed' signal from unknown EBookView!");
		return;
	}

	if (book_view == source->book_view)
		cached_contacts = source->contacts;
	else
		cached_contacts = source->contacts_pending;

	for (l = contacts; l; l = g_list_next (l)) {
		EContact    *cached_contact;
		EContact    *contact = l->data;
		const gchar *uid     = e_contact_get_const (contact, E_CONTACT_UID);
		gint         n       = find_contact_by_view_and_uid (contact_store, book_view, uid);

		if (n < 0) {
			g_warning ("EContactStore got change notification on unknown contact!");
			continue;
		}

		cached_contact = g_ptr_array_index (cached_contacts, n);

		/* Update cached contact */
		if (cached_contact != contact) {
			g_object_unref (cached_contact);
			cached_contacts->pdata [n] = g_object_ref (contact);
		}

		/* Emit changes for current view only */
		if (book_view == source->book_view)
			row_changed (contact_store, offset + n);
	}
}

static void
view_sequence_complete (EContactStore *contact_store, EBookViewStatus status, EBookView *book_view)
{
	ContactSource *source;
	gint           offset;
	gint           i;

	if (!find_contact_source_details_by_view (contact_store, book_view, &source, &offset)) {
		g_warning ("EContactStore got 'sequence_complete' signal from unknown EBookView!");
		return;
	}

	/* If current view finished, do nothing */
	if (book_view == source->book_view) {
		stop_view (contact_store, source->book_view);
		return;
	}

	g_assert (book_view == source->book_view_pending);

	/* However, if it was a pending view, calculate and emit the differences between that
	 * and the current view, and move the pending view up to current.
	 *
	 * This is O(m * n), and can be sped up with a temporary hash table if needed. */

	/* Deletions */
	for (i = 0; i < source->contacts->len; i++) {
		EContact    *old_contact = g_ptr_array_index (source->contacts, i);
		const gchar *old_uid     = e_contact_get_const (old_contact, E_CONTACT_UID);
		gint         result;

		result = find_contact_by_view_and_uid (contact_store, source->book_view_pending, old_uid);
		if (result < 0) {
			/* Contact is not in new view; removed */
			g_object_unref (old_contact);
			g_ptr_array_remove_index (source->contacts, i);
			row_deleted (contact_store, offset + i);
			i--;  /* Stay in place */
		}
	}

	/* Insertions */
	for (i = 0; i < source->contacts_pending->len; i++) {
		EContact    *new_contact = g_ptr_array_index (source->contacts_pending, i);
		const gchar *new_uid     = e_contact_get_const (new_contact, E_CONTACT_UID);
		gint         result;

		result = find_contact_by_view_and_uid (contact_store, source->book_view, new_uid);
		if (result < 0) {
			/* Contact is not in old view; inserted */
			g_ptr_array_add (source->contacts, new_contact);
			row_inserted (contact_store, offset + source->contacts->len - 1);
		} else {
			/* Contact already in old view; drop the new one */
			g_object_unref (new_contact);
		}
	}

	/* Move pending view up to current */
	stop_view (contact_store, source->book_view);
	g_object_unref (source->book_view);
	source->book_view = source->book_view_pending;
	source->book_view_pending = NULL;

	/* Free array of pending contacts (members have been either moved or unreffed) */
	g_ptr_array_free (source->contacts_pending, TRUE);
	source->contacts_pending = NULL;
}

/* --------------------- *
 * View/Query management *
 * --------------------- */

static void
start_view (EContactStore *contact_store, EBookView *view)
{
	g_signal_connect_swapped (view, "contacts_added",
				  G_CALLBACK (view_contacts_added), contact_store);
	g_signal_connect_swapped (view, "contacts_removed",
				  G_CALLBACK (view_contacts_removed), contact_store);
	g_signal_connect_swapped (view, "contacts_changed",
				  G_CALLBACK (view_contacts_changed), contact_store);
	g_signal_connect_swapped (view, "sequence_complete",
				  G_CALLBACK (view_sequence_complete), contact_store);

	e_book_view_start (view);
}

static void
stop_view (EContactStore *contact_store, EBookView *view)
{
	e_book_view_stop (view);

	g_signal_handlers_disconnect_matched (view, G_SIGNAL_MATCH_DATA,
					      0, 0, NULL, NULL, contact_store);
}

static void
clear_contact_ptrarray (GPtrArray *contacts)
{
	gint i;

	for (i = 0; i < contacts->len; i++) {
		EContact *contact = g_ptr_array_index (contacts, i);
		g_object_unref (contact);
	}

	g_ptr_array_set_size (contacts, 0);
}

static void
free_contact_ptrarray (GPtrArray *contacts)
{
	clear_contact_ptrarray (contacts);
	g_ptr_array_free (contacts, TRUE);
}

static void
clear_contact_source (EContactStore *contact_store, ContactSource *source)
{
	gint source_index;
	gint offset;

	source_index = find_contact_source_by_pointer (contact_store, source);
	g_assert (source_index >= 0);

	offset = get_contact_source_offset (contact_store, source_index);
	g_assert (offset >= 0);

	/* Inform listeners that contacts went away */

	if (source->contacts && source->contacts->len > 0) {
		GtkTreePath *path = gtk_tree_path_new ();
		gint         i;

		gtk_tree_path_append_index (path, source->contacts->len);

		for (i = source->contacts->len - 1; i >= 0; i--) {
			EContact *contact = g_ptr_array_index (source->contacts, i);

			g_object_unref (contact);
			g_ptr_array_remove_index_fast (source->contacts, i);

			gtk_tree_path_prev (path);
			gtk_tree_model_row_deleted (GTK_TREE_MODEL (contact_store), path);
		}

		gtk_tree_path_free (path);
	}

	/* Free main and pending views, clear cached contacts */

	if (source->book_view) {
		stop_view (contact_store, source->book_view);
		g_object_unref (source->book_view);

		source->book_view = NULL;
	}

	if (source->book_view_pending) {
		stop_view (contact_store, source->book_view_pending);
		g_object_unref (source->book_view_pending);
		free_contact_ptrarray (source->contacts_pending);

		source->book_view_pending = NULL;
		source->contacts_pending  = NULL;
	}
}

static void
query_contact_source (EContactStore *contact_store, ContactSource *source)
{
	EBookView *view;

	g_assert (source->book != NULL);

	if (!contact_store->query) {
		clear_contact_source (contact_store, source);
		return;
	}

	if (!e_book_is_opened (source->book) ||
	    !e_book_get_book_view (source->book, contact_store->query, NULL, -1, &view, NULL))
		view = NULL;

	if (source->book_view) {
		if (source->book_view_pending) {
			stop_view (contact_store, source->book_view_pending);
			g_object_unref (source->book_view_pending);
			free_contact_ptrarray (source->contacts_pending);
		}

		source->book_view_pending = view;

		if (source->book_view_pending) {
			source->contacts_pending = g_ptr_array_new ();
			start_view (contact_store, view);
		} else {
			source->contacts_pending = NULL;
		}
	} else {
		source->book_view = view;

		if (source->book_view) {
			start_view (contact_store, view);
		}
	}
}

/* ----------------- *
 * EContactStore API *
 * ----------------- */

/**
 * e_contact_store_get_book:
 * @contact_store: an #EContactStore
 * @iter: a #GtkTreeIter from @contact_store
 *
 * Gets the #EBook that provided the contact at @iter.
 *
 * Return value: An #EBook.
 **/
EBook *
e_contact_store_get_book (EContactStore *contact_store, GtkTreeIter *iter)
{
	gint index;

	g_return_val_if_fail (E_IS_CONTACT_STORE (contact_store), NULL);
	g_return_val_if_fail (ITER_IS_VALID (contact_store, iter), NULL);

	index = ITER_GET (iter);

	return get_book_at_row (contact_store, index);
}

/**
 * e_contact_store_get_contact:
 * @contact_store: an #EContactStore
 * @iter: a #GtkTreeIter from @contact_store
 *
 * Gets the #EContact at @iter.
 *
 * Return value: An #EContact.
 **/
EContact *
e_contact_store_get_contact (EContactStore *contact_store, GtkTreeIter *iter)
{
	gint index;

	g_return_val_if_fail (E_IS_CONTACT_STORE (contact_store), NULL);
	g_return_val_if_fail (ITER_IS_VALID (contact_store, iter), NULL);

	index = ITER_GET (iter);

	return get_contact_at_row (contact_store, index);
}

/**
 * e_contact_store_find_contact:
 * @contact_store: an #EContactStore
 * @uid: a unique contact identifier
 * @iter: a destination #GtkTreeIter to set
 *
 * Sets @iter to point to the contact row matching @uid.
 *
 * Return value: %TRUE if the contact was found, and @iter was set. %FALSE otherwise.
 **/
gboolean
e_contact_store_find_contact (EContactStore *contact_store, const gchar *uid,
			      GtkTreeIter *iter)
{
	gint index;

	g_return_val_if_fail (E_IS_CONTACT_STORE (contact_store), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	index = find_contact_by_uid (contact_store, uid);
	if (index < 0)
		return FALSE;

	ITER_SET (contact_store, iter, index);
	return TRUE;
}

/**
 * e_contact_store_get_books:
 * @contact_store: an #EContactStore
 *
 * Gets the list of books that provide contacts for @contact_store.
 *
 * Return value: A #GList of pointers to #EBook. The caller owns the list,
 * but not the books.
 **/
GList *
e_contact_store_get_books (EContactStore *contact_store)
{
	GList *book_list = NULL;
	gint   i;

	g_return_val_if_fail (E_IS_CONTACT_STORE (contact_store), NULL);

	for (i = 0; i < contact_store->contact_sources->len; i++) {
		ContactSource *source;

		source = &g_array_index (contact_store->contact_sources, ContactSource, i);
		book_list = g_list_prepend (book_list, source->book);
	}

	return book_list;
}

/**
 * e_contact_store_add_book:
 * @contact_store: an #EContactStore
 * @book: an #EBook
 *
 * Adds @book to the list of books that provide contacts for @contact_store.
 **/
void
e_contact_store_add_book (EContactStore *contact_store, EBook *book)
{
	ContactSource  source;
	ContactSource *indexed_source;

	g_return_if_fail (E_IS_CONTACT_STORE (contact_store));
	g_return_if_fail (E_IS_BOOK (book));

	if (find_contact_source_by_book (contact_store, book) >= 0) {
		g_warning ("Same book added more than once to EContactStore!");
		return;
	}

	memset (&source, 0, sizeof (ContactSource));
	source.book     = g_object_ref (book);
	source.contacts = g_ptr_array_new ();
	g_array_append_val (contact_store->contact_sources, source);

	indexed_source = &g_array_index (contact_store->contact_sources, ContactSource,
					 contact_store->contact_sources->len - 1);

	query_contact_source (contact_store, indexed_source);
}

/**
 * e_contact_store_remove_book:
 * @contact_store: an #EContactStore
 * @book: an #EBook
 *
 * Removes @book from the list of books that provide contacts for @contact_store.
 **/
void
e_contact_store_remove_book (EContactStore *contact_store, EBook *book)
{
	ContactSource *source;
	gint           source_index;

	g_return_if_fail (E_IS_CONTACT_STORE (contact_store));
	g_return_if_fail (E_IS_BOOK (book));

	source_index = find_contact_source_by_book (contact_store, book);
	if (source_index < 0) {
		g_warning ("Tried to remove unknown book from EContactStore!");
		return;
	}

	source = &g_array_index (contact_store->contact_sources, ContactSource, source_index);
	clear_contact_source (contact_store, source);
	free_contact_ptrarray (source->contacts);
	g_object_unref (book);

	g_array_remove_index (contact_store->contact_sources, source_index);  /* Preserve order */
}

/**
 * e_contact_store_set_query:
 * @contact_store: an #EContactStore
 * @book_query: an #EBookQuery
 *
 * Sets @book_query to be the query used to fetch contacts from the books
 * assigned to @contact_store.
 **/
void
e_contact_store_set_query (EContactStore *contact_store, EBookQuery *book_query)
{
	gint i;

	g_return_if_fail (E_IS_CONTACT_STORE (contact_store));

	if (book_query == contact_store->query)
		return;

	if (contact_store->query)
		e_book_query_unref (contact_store->query);

	contact_store->query = book_query;
	if (book_query)
		e_book_query_ref (book_query);

	/* Query books */
	for (i = 0; i < contact_store->contact_sources->len; i++) {
		ContactSource *contact_source;

		contact_source = &g_array_index (contact_store->contact_sources, ContactSource, i);
		query_contact_source (contact_store, contact_source);
	}
}

/**
 * e_contact_store_peek_query:
 * @contact_store: an #EContactStore
 *
 * Gets the query that's being used to fetch contacts from the books
 * assigned to @contact_store.
 *
 * Return value: The #EBookQuery being used.
 **/
EBookQuery *
e_contact_store_peek_query (EContactStore *contact_store)
{
	g_return_val_if_fail (E_IS_CONTACT_STORE (contact_store), NULL);

	return contact_store->query;
}

/* ---------------- *
 * GtkTreeModel API *
 * ---------------- */

static GtkTreeModelFlags
e_contact_store_get_flags (GtkTreeModel *tree_model)
{
	g_return_val_if_fail (E_IS_CONTACT_STORE (tree_model), 0);

	return GTK_TREE_MODEL_LIST_ONLY;
}

static gint
e_contact_store_get_n_columns (GtkTreeModel *tree_model)
{
	g_return_val_if_fail (E_IS_CONTACT_STORE (tree_model), 0);

	return E_CONTACT_FIELD_LAST;
}

static GType
get_column_type (EContactStore *contact_store, gint column)
{
	const gchar  *field_name;
	GObjectClass *contact_class;
	GParamSpec   *param_spec;
	GType         value_type;

	/* Silently suppress requests for columns lower than the first EContactField.
	 * GtkTreeView automatically queries the type of all columns up to the maximum
	 * provided, and we have to return a valid value type, so let it be a generic
	 * pointer. */
	if (column < E_CONTACT_FIELD_FIRST) {
		return G_TYPE_POINTER;
	}

	field_name = e_contact_field_name (column);
	contact_class = g_type_class_ref (E_TYPE_CONTACT);
	param_spec = g_object_class_find_property (contact_class, field_name);
	value_type = G_PARAM_SPEC_VALUE_TYPE (param_spec);
	g_type_class_unref (contact_class);

	return value_type;
}

static GType
e_contact_store_get_column_type (GtkTreeModel *tree_model,
				 gint          index)
{
	EContactStore *contact_store = E_CONTACT_STORE (tree_model);

	g_return_val_if_fail (E_IS_CONTACT_STORE (tree_model), G_TYPE_INVALID);
	g_return_val_if_fail (index >= 0 && index < E_CONTACT_FIELD_LAST, G_TYPE_INVALID);

	return get_column_type (contact_store, index);
}

static gboolean
e_contact_store_get_iter (GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreePath *path)
{
	EContactStore *contact_store = E_CONTACT_STORE (tree_model);
	gint           index;

	g_return_val_if_fail (E_IS_CONTACT_STORE (tree_model), FALSE);
	g_return_val_if_fail (gtk_tree_path_get_depth (path) > 0, FALSE);

	index = gtk_tree_path_get_indices (path)[0];
	if (index >= count_contacts (contact_store))
		return FALSE;

	ITER_SET (contact_store, iter, index);
	return TRUE;
}

static GtkTreePath *
e_contact_store_get_path (GtkTreeModel *tree_model,
			  GtkTreeIter  *iter)
{
	EContactStore *contact_store = E_CONTACT_STORE (tree_model);
	GtkTreePath   *path;
	gint           index;

	g_return_val_if_fail (E_IS_CONTACT_STORE (tree_model), NULL);
	g_return_val_if_fail (ITER_IS_VALID (contact_store, iter), NULL);

	index = ITER_GET (iter);
	path = gtk_tree_path_new ();
	gtk_tree_path_append_index (path, index);

	return path;
}

static gboolean
e_contact_store_iter_next (GtkTreeModel  *tree_model,
			   GtkTreeIter   *iter)
{
	EContactStore *contact_store = E_CONTACT_STORE (tree_model);
	gint           index;

	g_return_val_if_fail (E_IS_CONTACT_STORE (tree_model), FALSE);
	g_return_val_if_fail (ITER_IS_VALID (contact_store, iter), FALSE);

	index = ITER_GET (iter);

	if (index + 1 < count_contacts (contact_store)) {
		ITER_SET (contact_store, iter, index + 1);
		return TRUE;
	}

	return FALSE;
}

static gboolean
e_contact_store_iter_children (GtkTreeModel *tree_model,
			       GtkTreeIter  *iter,
			       GtkTreeIter  *parent)
{
	EContactStore *contact_store = E_CONTACT_STORE (tree_model);

	g_return_val_if_fail (E_IS_CONTACT_STORE (tree_model), FALSE);

	/* This is a list, nodes have no children. */
	if (parent)
		return FALSE;

	/* But if parent == NULL we return the list itself as children of the root. */
	if (count_contacts (contact_store) <= 0)
		return FALSE;

	ITER_SET (contact_store, iter, 0);
	return TRUE;
}

static gboolean
e_contact_store_iter_has_child (GtkTreeModel *tree_model,
				GtkTreeIter  *iter)
{
	g_return_val_if_fail (E_IS_CONTACT_STORE (tree_model), FALSE);

	if (iter == NULL)
		return TRUE;

	return FALSE;
}

static gint
e_contact_store_iter_n_children (GtkTreeModel *tree_model,
				 GtkTreeIter  *iter)
{
	EContactStore *contact_store = E_CONTACT_STORE (tree_model);

	g_return_val_if_fail (E_IS_CONTACT_STORE (tree_model), -1);

	if (iter == NULL)
		return count_contacts (contact_store);

	g_return_val_if_fail (ITER_IS_VALID (contact_store, iter), -1);
	return 0;
}

static gboolean
e_contact_store_iter_nth_child (GtkTreeModel *tree_model,
				GtkTreeIter  *iter,
				GtkTreeIter  *parent,
				gint          n)
{
	EContactStore *contact_store = E_CONTACT_STORE (tree_model);

	g_return_val_if_fail (E_IS_CONTACT_STORE (tree_model), FALSE);

	if (parent)
		return FALSE;

	if (n < count_contacts (contact_store)) {
		ITER_SET (contact_store, iter, n);
		return TRUE;
	}

	return FALSE;
}

static gboolean
e_contact_store_iter_parent (GtkTreeModel *tree_model,
			     GtkTreeIter  *iter,
			     GtkTreeIter  *child)
{
	return FALSE;
}

static void
e_contact_store_get_value (GtkTreeModel *tree_model,
			   GtkTreeIter  *iter,
			   gint          column,
			   GValue       *value)
{
	EContactStore *contact_store = E_CONTACT_STORE (tree_model);
	EContact      *contact;
	const gchar   *field_name;
	gint           row;

	g_return_if_fail (E_IS_CONTACT_STORE (tree_model));
	g_return_if_fail (column < E_CONTACT_FIELD_LAST);
	g_return_if_fail (ITER_IS_VALID (contact_store, iter));

	g_value_init (value, get_column_type (contact_store, column));

	row = ITER_GET (iter);
	contact = get_contact_at_row (contact_store, row);
	if (!contact || column < E_CONTACT_FIELD_FIRST)
		return;

	field_name = e_contact_field_name (column);
	g_object_get_property (G_OBJECT (contact), field_name, value);
}

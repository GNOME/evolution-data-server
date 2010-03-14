/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author: Ross Burton <ross@linux.intel.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <dbus/dbus.h>
#include <libebook/e-contact.h>
#include "e-data-book-view.h"

extern DBusGConnection *connection;

static gboolean impl_BookView_start (EDataBookView *view, GError **error);
static gboolean impl_BookView_stop (EDataBookView *view, GError **error);
static gboolean impl_BookView_dispose (EDataBookView *view, GError **eror);

#include "e-data-book-view-glue.h"

static void reset_array (GArray *array);

G_DEFINE_TYPE (EDataBookView, e_data_book_view, G_TYPE_OBJECT);
#define E_DATA_BOOK_VIEW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_DATA_BOOK_VIEW, EDataBookViewPrivate))

#define THRESHOLD 32

struct _EDataBookViewPrivate {
	EDataBook *book;
	EBookBackend *backend;

	gchar * card_query;
	EBookBackendSExp *card_sexp;
	gint max_results;

	gboolean running;
	GMutex *pending_mutex;

	GArray *adds;
	GArray *changes;
	GArray *removes;

	GHashTable *ids;
	guint idle_id;
};

enum {
	CONTACTS_ADDED,
	CONTACTS_CHANGED,
	CONTACTS_REMOVED,
	STATUS_MESSAGE,
	COMPLETE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void e_data_book_view_dispose (GObject *object);
static void e_data_book_view_finalize (GObject *object);

static void
e_data_book_view_class_init (EDataBookViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = e_data_book_view_dispose;
	object_class->finalize = e_data_book_view_finalize;

	signals[CONTACTS_ADDED] =
		g_signal_new ("contacts-added",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_STRV);

	signals[CONTACTS_CHANGED] =
		g_signal_new ("contacts-changed",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_STRV);

	signals[CONTACTS_REMOVED] =
		g_signal_new ("contacts-removed",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_STRV);

	signals[STATUS_MESSAGE] =
		g_signal_new ("status-message",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);

	signals[COMPLETE] =
		g_signal_new ("complete",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (EDataBookViewPrivate));

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (klass), &dbus_glib_e_data_book_view_object_info);
}

static void
e_data_book_view_init (EDataBookView *book_view)
{
	EDataBookViewPrivate *priv = E_DATA_BOOK_VIEW_GET_PRIVATE (book_view);
	book_view->priv = priv;

	priv->running = FALSE;
	priv->pending_mutex = g_mutex_new ();

	priv->adds = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD);
	priv->changes = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD);
	priv->removes = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD);

	priv->ids = g_hash_table_new_full (g_str_hash, g_str_equal,
					   g_free, NULL);
}

static void
book_destroyed_cb (gpointer data, GObject *dead)
{
	EDataBookView *view = E_DATA_BOOK_VIEW (data);
	EDataBookViewPrivate *priv = view->priv;

	/* The book has just died, so unset the pointer so we don't try and remove a
	   dead weak reference. */
	view->priv->book = NULL;

	/* If the view is running stop it here. */
	if (priv->running) {
		e_book_backend_stop_book_view (priv->backend, view);
		priv->running = FALSE;
	}
}

static void
send_pending_adds (EDataBookView *view)
{
	EDataBookViewPrivate *priv = view->priv;

	if (priv->adds->len == 0)
		return;

	g_signal_emit (view, signals[CONTACTS_ADDED], 0, priv->adds->data);
	reset_array (priv->adds);
}

static void
send_pending_changes (EDataBookView *view)
{
	EDataBookViewPrivate *priv = view->priv;

	if (priv->changes->len == 0)
		return;

	g_signal_emit (view, signals[CONTACTS_CHANGED], 0, priv->changes->data);
	reset_array (priv->changes);
}

static void
send_pending_removes (EDataBookView *view)
{
	EDataBookViewPrivate *priv = view->priv;

	if (priv->removes->len == 0)
		return;

	g_signal_emit (view, signals[CONTACTS_REMOVED], 0, priv->removes->data);
	reset_array (priv->removes);
}

/*
 * Queue @vcard to be sent as a change notification. This takes ownership of
 * @vcard.
 */
static void
notify_change (EDataBookView *view, gchar *vcard)
{
	EDataBookViewPrivate *priv = view->priv;
	send_pending_adds (view);
	send_pending_removes (view);

	g_array_append_val (priv->changes, vcard);
}

/*
 * Queue @id to be sent as a change notification. This takes ownership of @id.
 */
static void
notify_remove (EDataBookView *view, gchar *id)
{
	EDataBookViewPrivate *priv = view->priv;

	send_pending_adds (view);
	send_pending_changes (view);

	g_array_append_val (priv->removes, id);
	g_hash_table_remove (priv->ids, id);
}

/*
 * Queue @id and @vcard to be sent as a change notification. This takes ownership of
 * @vcard but not @id.
 */
static void
notify_add (EDataBookView *view, const gchar *id, gchar *vcard)
{
	EDataBookViewPrivate *priv = view->priv;
	send_pending_changes (view);
	send_pending_removes (view);

	if (priv->adds->len == THRESHOLD) {
		send_pending_adds (view);
	}
	g_array_append_val (priv->adds, vcard);
	g_hash_table_insert (priv->ids, g_strdup (id),
			     GUINT_TO_POINTER (1));
}

static void
reset_array (GArray *array)
{
	gint i = 0;
	gchar *tmp = NULL;

	/* Free stored strings */
	for (i = 0; i < array->len; i++)
		{
			tmp = g_array_index (array, gchar *, i);
			g_free (tmp);
		}

	/* Force the array size to 0 */
	g_array_set_size (array, 0);
}

/**
 * e_data_book_view_notify_update:
 * @book_view: an #EDataBookView
 * @contact: an #EContact
 *
 * Notify listeners that @contact has changed. This can
 * trigger an add, change or removal event depending on
 * whether the change causes the contact to start matching,
 * no longer match, or stay matching the query specified
 * by @book_view.
 **/
void
e_data_book_view_notify_update (EDataBookView *book_view,
                                EContact      *contact)
{
	EDataBookViewPrivate *priv = book_view->priv;
	gboolean currently_in_view, want_in_view;
	const gchar *id;
	gchar *vcard;

	if (!priv->running)
		return;

	g_mutex_lock (priv->pending_mutex);

	id = e_contact_get_const (contact, E_CONTACT_UID);

	currently_in_view =
		g_hash_table_lookup (priv->ids, id) != NULL;
	want_in_view =
		e_book_backend_sexp_match_contact (priv->card_sexp, contact);

	if (want_in_view) {
		vcard = e_vcard_to_string (E_VCARD (contact),
					   EVC_FORMAT_VCARD_30);

		if (currently_in_view)
			notify_change (book_view, vcard);
		else
			notify_add (book_view, id, vcard);
	} else {
		if (currently_in_view)
			notify_remove (book_view, g_strdup (id));
		/* else nothing; we're removing a card that wasn't there */
	}

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_book_view_notify_update_vcard:
 * @book_view: an #EDataBookView
 * @vcard: a plain vCard
 *
 * Notify listeners that @vcard has changed. This can
 * trigger an add, change or removal event depending on
 * whether the change causes the contact to start matching,
 * no longer match, or stay matching the query specified
 * by @book_view.  This method should be preferred over
 * #e_data_book_view_notify_update when the native
 * representation of a contact is a vCard.
 **/
void
e_data_book_view_notify_update_vcard (EDataBookView *book_view, gchar *vcard)
{
	EDataBookViewPrivate *priv = book_view->priv;
	gboolean currently_in_view, want_in_view;
	const gchar *id;
	EContact *contact;

	if (!priv->running)
		return;

	g_mutex_lock (priv->pending_mutex);

	contact = e_contact_new_from_vcard (vcard);
	id = e_contact_get_const (contact, E_CONTACT_UID);
	currently_in_view =
		g_hash_table_lookup (priv->ids, id) != NULL;
	want_in_view =
		e_book_backend_sexp_match_contact (priv->card_sexp, contact);

	if (want_in_view) {
		if (currently_in_view)
			notify_change (book_view, vcard);
		else
			notify_add (book_view, id, vcard);
	} else {
		if (currently_in_view)
			notify_remove (book_view, g_strdup (id));
		else
			/* else nothing; we're removing a card that wasn't there */
			g_free (vcard);
	}
	/* Do this last so that id is still valid when notify_ is called */
	g_object_unref (contact);

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_book_view_notify_update_prefiltered_vcard:
 * @book_view: an #EDataBookView
 * @id: the UID of this contact
 * @vcard: a plain vCard
 *
 * Notify listeners that @vcard has changed. This can
 * trigger an add, change or removal event depending on
 * whether the change causes the contact to start matching,
 * no longer match, or stay matching the query specified
 * by @book_view.  This method should be preferred over
 * #e_data_book_view_notify_update when the native
 * representation of a contact is a vCard.
 *
 * The important difference between this method and
 * #e_data_book_view_notify_update and #e_data_book_view_notify_update_vcard is
 * that it doesn't match the contact against the book view query to see if it
 * should be included, it assumes that this has been done and the contact is
 * known to exist in the view.
 **/
void
e_data_book_view_notify_update_prefiltered_vcard (EDataBookView *book_view, const gchar *id, gchar *vcard)
{
	EDataBookViewPrivate *priv = book_view->priv;
	gboolean currently_in_view;

	if (!priv->running)
		return;

	g_mutex_lock (priv->pending_mutex);

	currently_in_view =
		g_hash_table_lookup (priv->ids, id) != NULL;

	if (currently_in_view)
		notify_change (book_view, vcard);
	else
		notify_add (book_view, id, vcard);

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_book_view_notify_remove:
 * @book_view: an #EDataBookView
 * @id: a unique contact ID
 *
 * Notify listeners that a contact specified by @id
 * was removed from @book_view.
 **/
void
e_data_book_view_notify_remove (EDataBookView *book_view, const gchar *id)
{
	EDataBookViewPrivate *priv = E_DATA_BOOK_VIEW_GET_PRIVATE (book_view);

	if (!priv->running)
		return;

	g_mutex_lock (priv->pending_mutex);

	if (g_hash_table_lookup (priv->ids, id))
		notify_remove (book_view, g_strdup (id));

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_book_view_notify_complete:
 * @book_view: an #EDataBookView
 * @status: the status of the query
 *
 * Notifies listeners that all pending updates on @book_view
 * have been sent. The listener's information should now be
 * in sync with the backend's.
 **/
void
e_data_book_view_notify_complete (EDataBookView *book_view, EDataBookStatus status)
{
	EDataBookViewPrivate *priv = book_view->priv;

	if (!priv->running)
		return;

	g_mutex_lock (priv->pending_mutex);

	send_pending_adds (book_view);
	send_pending_changes (book_view);
	send_pending_removes (book_view);

	g_mutex_unlock (priv->pending_mutex);

	/* We're done now, so tell the backend to stop?  TODO: this is a bit different to
	   how the CORBA backend works... */

	g_signal_emit (book_view, signals[COMPLETE], 0, status);
}

/**
 * e_data_book_view_notify_status_message:
 * @book_view: an #EDataBookView
 * @message: a text message
 *
 * Provides listeners with a human-readable text describing the
 * current backend operation. This can be used for progress
 * reporting.
 **/
void
e_data_book_view_notify_status_message (EDataBookView *book_view, const gchar *message)
{
	EDataBookViewPrivate *priv = E_DATA_BOOK_VIEW_GET_PRIVATE (book_view);

	if (!priv->running)
		return;

	g_signal_emit (book_view, signals[STATUS_MESSAGE], 0, message);
}

/**
 * e_data_book_view_new:
 * @book: The #EDataBook to search
 * @path: The object path that this book view should have
 * @card_query: The query as a string
 * @card_sexp: The query as an #EBookBackendSExp
 * @max_results: The maximum number of results to return
 *
 * Create a new #EDataBookView for the given #EBook, filtering on #card_sexp,
 * and place it on DBus at the object path #path.
 */
EDataBookView *
e_data_book_view_new (EDataBook *book, const gchar *path, const gchar *card_query, EBookBackendSExp *card_sexp, gint max_results)
{
	EDataBookView *view;
	EDataBookViewPrivate *priv;

	view = g_object_new (E_TYPE_DATA_BOOK_VIEW, NULL);
	priv = view->priv;

	priv->book = book;
	/* Attach a weak reference to the book, so if it dies the book view is destroyed too */
	g_object_weak_ref (G_OBJECT (priv->book), book_destroyed_cb, view);
	priv->backend = g_object_ref (e_data_book_get_backend (book));
	priv->card_query = g_strdup (card_query);
	priv->card_sexp = card_sexp;
	priv->max_results = max_results;

	dbus_g_connection_register_g_object (connection, path, G_OBJECT (view));

	return view;
}

static void
e_data_book_view_dispose (GObject *object)
{
	EDataBookView *book_view = E_DATA_BOOK_VIEW (object);
	EDataBookViewPrivate *priv = book_view->priv;

	if (priv->book) {
		/* Remove the weak reference */
		g_object_weak_unref (G_OBJECT (priv->book), book_destroyed_cb, book_view);
		priv->book = NULL;
	}

	if (priv->backend) {
		e_book_backend_remove_book_view (priv->backend, book_view);
		g_object_unref (priv->backend);
		priv->backend = NULL;
	}

	if (priv->card_sexp) {
		g_object_unref (priv->card_sexp);
		priv->card_sexp = NULL;
	}

	if (priv->idle_id) {
		g_source_remove (priv->idle_id);
		priv->idle_id = 0;
	}

	G_OBJECT_CLASS (e_data_book_view_parent_class)->dispose (object);
}

static void
e_data_book_view_finalize (GObject *object)
{
	EDataBookView *book_view = E_DATA_BOOK_VIEW (object);
	EDataBookViewPrivate *priv = book_view->priv;

	reset_array (priv->adds);
	reset_array (priv->changes);
	reset_array (priv->removes);
	g_array_free (priv->adds, TRUE);
	g_array_free (priv->changes, TRUE);
	g_array_free (priv->removes, TRUE);

	g_free (priv->card_query);

	g_mutex_free (priv->pending_mutex);

	g_hash_table_destroy (priv->ids);

	G_OBJECT_CLASS (e_data_book_view_parent_class)->finalize (object);
}

static gboolean
bookview_idle_start (gpointer data)
{
	EDataBookView *book_view = data;

	book_view->priv->running = TRUE;
	book_view->priv->idle_id = 0;

	e_book_backend_start_book_view (book_view->priv->backend, book_view);
	return FALSE;
}

static gboolean
impl_BookView_start (EDataBookView *book_view, GError **error)
{
	book_view->priv->idle_id = g_idle_add (bookview_idle_start, book_view);
	return TRUE;
}

static gboolean
bookview_idle_stop (gpointer data)
{
	EDataBookView *book_view = data;

	e_book_backend_stop_book_view (book_view->priv->backend, book_view);

	book_view->priv->running = FALSE;
	book_view->priv->idle_id = 0;

	return FALSE;
}

static gboolean
impl_BookView_stop (EDataBookView *book_view, GError **error)
{
	if (book_view->priv->idle_id)
		g_source_remove (book_view->priv->idle_id);

	book_view->priv->idle_id = g_idle_add (bookview_idle_stop, book_view);
	return TRUE;
}

static gboolean
impl_BookView_dispose (EDataBookView *book_view, GError **eror)
{
	g_object_unref (book_view);

	return TRUE;
}

void
e_data_book_view_set_thresholds (EDataBookView *book_view,
                                 gint minimum_grouping_threshold,
                                 gint maximum_grouping_threshold)
{
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (book_view));

	g_debug ("e_data_book_view_set_thresholds does nothing in eds-dbus");
}

/**
 * e_data_book_view_get_card_query:
 * @book_view: an #EDataBookView
 *
 * Gets the text representation of the s-expression used
 * for matching contacts to @book_view.
 *
 * Returns: The textual s-expression used.
 **/
const gchar *
e_data_book_view_get_card_query (EDataBookView *book_view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (book_view), NULL);

	return book_view->priv->card_query;
}

/**
 * e_data_book_view_get_card_sexp:
 * @book_view: an #EDataBookView
 *
 * Gets the s-expression used for matching contacts to
 * @book_view.
 *
 * Returns: The #EBookBackendSExp used.
 **/
EBookBackendSExp*
e_data_book_view_get_card_sexp (EDataBookView *book_view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (book_view), NULL);

	return book_view->priv->card_sexp;
}

/**
 * e_data_book_view_get_max_results:
 * @book_view: an #EDataBookView
 *
 * Gets the maximum number of results returned by
 * @book_view's query.
 *
 * Returns: The maximum number of results returned.
 **/
gint
e_data_book_view_get_max_results (EDataBookView *book_view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (book_view), 0);

	return book_view->priv->max_results;
}

/**
 * e_data_book_view_get_backend:
 * @book_view: an #EDataBookView
 *
 * Gets the backend that @book_view is querying.
 *
 * Returns: The associated #EBookBackend.
 **/
EBookBackend*
e_data_book_view_get_backend (EDataBookView *book_view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (book_view), NULL);

	return book_view->priv->backend;
}

/**
 * e_data_book_view_ref
 * @book_view: an #EBookView
 *
 * Increase the reference count of the book view. This is a function to aid
 * the transition from Bonobo to DBUS.
 *
 * Since: 2.26
 */
void
e_data_book_view_ref (EDataBookView *book_view)
{
	g_object_ref (book_view);
}

/**
 * e_data_book_view_unref
 * @book_view: an #EBookView
 *
 * Decrease the reference count of the book view. This is a function to aid
 * the transition from Bonobo to DBUS.
 *
 * Since: 2.26
 */
void
e_data_book_view_unref (EDataBookView *book_view)
{
	g_object_unref (book_view);
}

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
#include <libebook/e-contact.h>
#include "libedataserver/e-data-server-util.h"
#include "e-data-book-view.h"

#include "e-gdbus-book-view.h"

static void reset_array (GArray *array);
static void ensure_pending_flush_timeout (EDataBookView *view);

G_DEFINE_TYPE (EDataBookView, e_data_book_view, G_TYPE_OBJECT);

#define THRESHOLD_ITEMS   32	/* how many items can be hold in a cache, before propagated to UI */
#define THRESHOLD_SECONDS  2	/* how long to wait until notifications are propagated to UI; in seconds */

struct _EDataBookViewPrivate {
	EGdbusBookView *gdbus_object;

	EDataBook *book;
	EBookBackend *backend;

	gchar * card_query;
	EBookBackendSExp *card_sexp;

	gboolean running;
	GMutex *pending_mutex;

	GArray *adds;
	GArray *changes;
	GArray *removes;

	GHashTable *ids;
	guint idle_id;

	guint flush_id;

	/* which fields is listener interested in */
	GHashTable *fields_of_interest;
};

static void e_data_book_view_dispose (GObject *object);
static void e_data_book_view_finalize (GObject *object);

static void
e_data_book_view_class_init (EDataBookViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (EDataBookViewPrivate));

	object_class->dispose = e_data_book_view_dispose;
	object_class->finalize = e_data_book_view_finalize;
}

static guint
str_ic_hash (gconstpointer key)
{
	guint32 hash = 5381;
	const gchar *str = key;
	gint ii;

	if (!str)
		return hash;

	for (ii = 0; str[ii]; ii++) {
		hash = hash * 33 + g_ascii_tolower (str[ii]);
	}

	return hash;
}

static gboolean
str_ic_equal (gconstpointer a, gconstpointer b)
{
	const gchar *stra = a, *strb = b;
	gint ii;

	if (!stra && !strb)
		return TRUE;

	if (!stra || !strb)
		return FALSE;

	for (ii = 0; stra[ii] && strb[ii]; ii++) {
		if (g_ascii_tolower (stra[ii]) != g_ascii_tolower (strb[ii]))
			return FALSE;
	}

	return stra[ii] == strb[ii];
}

/**
 * e_data_book_view_register_gdbus_object:
 *
 * Since: 2.32
 **/
guint
e_data_book_view_register_gdbus_object (EDataBookView *query, GDBusConnection *connection, const gchar *object_path, GError **error)
{
	g_return_val_if_fail (query != NULL, 0);
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (query), 0);
	g_return_val_if_fail (connection != NULL, 0);
	g_return_val_if_fail (object_path != NULL, 0);

	return e_gdbus_book_view_register_object (query->priv->gdbus_object, connection, object_path, error);
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

	e_gdbus_book_view_emit_objects_added (view->priv->gdbus_object, (const gchar * const *) priv->adds->data);
	reset_array (priv->adds);
}

static void
send_pending_changes (EDataBookView *view)
{
	EDataBookViewPrivate *priv = view->priv;

	if (priv->changes->len == 0)
		return;

	e_gdbus_book_view_emit_objects_modified (view->priv->gdbus_object, (const gchar * const *) priv->changes->data);
	reset_array (priv->changes);
}

static void
send_pending_removes (EDataBookView *view)
{
	EDataBookViewPrivate *priv = view->priv;

	if (priv->removes->len == 0)
		return;

	e_gdbus_book_view_emit_objects_removed (view->priv->gdbus_object, (const gchar * const *) priv->removes->data);
	reset_array (priv->removes);
}

static gboolean
pending_flush_timeout_cb (gpointer data)
{
	EDataBookView *view = data;
	EDataBookViewPrivate *priv = view->priv;

	g_mutex_lock (priv->pending_mutex);

	priv->flush_id = 0;

	send_pending_adds (view);
	send_pending_changes (view);
	send_pending_removes (view);

	g_mutex_unlock (priv->pending_mutex);

	return FALSE;
}

static void
ensure_pending_flush_timeout (EDataBookView *view)
{
	EDataBookViewPrivate *priv = view->priv;

	if (priv->flush_id)
		return;

	priv->flush_id = g_timeout_add_seconds (THRESHOLD_SECONDS, pending_flush_timeout_cb, view);
}

/*
 * Queue @vcard to be sent as a change notification.
 */
static void
notify_change (EDataBookView *view, const gchar *vcard)
{
	EDataBookViewPrivate *priv = view->priv;
	gchar *utf8_vcard;

	send_pending_adds (view);
	send_pending_removes (view);

	if (priv->changes->len == THRESHOLD_ITEMS) {
		send_pending_changes (view);
	}

	utf8_vcard = e_util_utf8_make_valid (vcard);
	g_array_append_val (priv->changes, utf8_vcard);

	ensure_pending_flush_timeout (view);
}

/*
 * Queue @id to be sent as a change notification.
 */
static void
notify_remove (EDataBookView *view, const gchar *id)
{
	EDataBookViewPrivate *priv = view->priv;
	gchar *valid_id;

	send_pending_adds (view);
	send_pending_changes (view);

	if (priv->removes->len == THRESHOLD_ITEMS) {
		send_pending_removes (view);
	}

	valid_id = e_util_utf8_make_valid (id);
	g_array_append_val (priv->removes, valid_id);
	g_hash_table_remove (priv->ids, valid_id);

	ensure_pending_flush_timeout (view);
}

/*
 * Queue @id and @vcard to be sent as a change notification.
 */
static void
notify_add (EDataBookView *view, const gchar *id, const gchar *vcard)
{
	EDataBookViewPrivate *priv = view->priv;
	gchar *utf8_vcard;

	send_pending_changes (view);
	send_pending_removes (view);

	if (priv->adds->len == THRESHOLD_ITEMS) {
		send_pending_adds (view);
	}

	utf8_vcard = e_util_utf8_make_valid (vcard);
	g_array_append_val (priv->adds, utf8_vcard);
	g_hash_table_insert (priv->ids, e_util_utf8_make_valid (id),
			     GUINT_TO_POINTER (1));

	ensure_pending_flush_timeout (view);
}

static gboolean
impl_DataBookView_setFieldsOfInterest (EGdbusBookView *object, GDBusMethodInvocation *invocation, const gchar * const *in_fields_of_interest, EDataBookView *view)
{
	EDataBookViewPrivate *priv;
	gint ii;

	g_return_val_if_fail (in_fields_of_interest != NULL, TRUE);

	priv = view->priv;

	if (priv->fields_of_interest)
		g_hash_table_destroy (priv->fields_of_interest);
	priv->fields_of_interest = NULL;

	for (ii = 0; in_fields_of_interest[ii]; ii++) {
		const gchar *field = in_fields_of_interest[ii];

		if (!*field)
			continue;

		if (!priv->fields_of_interest)
			priv->fields_of_interest = g_hash_table_new_full (str_ic_hash, str_ic_equal, g_free, NULL);

		g_hash_table_insert (priv->fields_of_interest, g_strdup (field), GINT_TO_POINTER (1));
	}

	e_gdbus_book_view_complete_set_fields_of_interest (object, invocation, NULL);

	return TRUE;
}

static void
reset_array (GArray *array)
{
	gint i = 0;
	gchar *tmp = NULL;

	/* Free stored strings */
	for (i = 0; i < array->len; i++) {
		tmp = g_array_index (array, gchar *, i);
		g_free (tmp);
	}

	/* Force the array size to 0 */
	g_array_set_size (array, 0);
}

static gboolean
id_is_in_view (EDataBookView *book_view, const gchar *id)
{
	gchar *valid_id;
	gboolean res;

	g_return_val_if_fail (book_view != NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	valid_id = e_util_utf8_make_valid (id);
	res = g_hash_table_lookup (book_view->priv->ids, valid_id) != NULL;
	g_free (valid_id);

	return res;
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
e_data_book_view_notify_update (EDataBookView *book_view, const EContact *contact)
{
	EDataBookViewPrivate *priv = book_view->priv;
	gboolean currently_in_view, want_in_view;
	const gchar *id;
	gchar *vcard;

	if (!priv->running)
		return;

	g_mutex_lock (priv->pending_mutex);

	id = e_contact_get_const ((EContact *) contact, E_CONTACT_UID);

	currently_in_view = id_is_in_view (book_view, id);
	want_in_view =
		e_book_backend_sexp_match_contact (priv->card_sexp, (EContact *) contact);

	if (want_in_view) {
		vcard = e_vcard_to_string (E_VCARD (contact),
					   EVC_FORMAT_VCARD_30);

		if (currently_in_view)
			notify_change (book_view, vcard);
		else
			notify_add (book_view, id, vcard);

		g_free (vcard);
	} else {
		if (currently_in_view)
			notify_remove (book_view, id);
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

	if (!priv->running) {
		g_free (vcard);
		return;
	}

	g_mutex_lock (priv->pending_mutex);

	contact = e_contact_new_from_vcard (vcard);
	id = e_contact_get_const (contact, E_CONTACT_UID);
	currently_in_view = id_is_in_view (book_view, id);
	want_in_view =
		e_book_backend_sexp_match_contact (priv->card_sexp, contact);

	if (want_in_view) {
		if (currently_in_view)
			notify_change (book_view, vcard);
		else
			notify_add (book_view, id, vcard);
	} else {
		if (currently_in_view)
			notify_remove (book_view, id);
	}

	/* Do this last so that id is still valid when notify_ is called */
	g_object_unref (contact);
	g_free (vcard);

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

	if (!priv->running) {
		g_free (vcard);
		return;
	}

	g_mutex_lock (priv->pending_mutex);

	currently_in_view = id_is_in_view (book_view, id);

	if (currently_in_view)
		notify_change (book_view, vcard);
	else
		notify_add (book_view, id, vcard);

	g_free (vcard);

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
	EDataBookViewPrivate *priv = book_view->priv;

	if (!priv->running)
		return;

	g_mutex_lock (priv->pending_mutex);

	if (id_is_in_view (book_view, id))
		notify_remove (book_view, id);

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_book_view_notify_complete:
 * @book_view: an #EDataBookView
 * @error: the error of the query, if any
 *
 * Notifies listeners that all pending updates on @book_view
 * have been sent. The listener's information should now be
 * in sync with the backend's.
 **/
void
e_data_book_view_notify_complete (EDataBookView *book_view, const GError *error)
{
	EDataBookViewPrivate *priv = book_view->priv;
	gchar **strv_error;

	if (!priv->running)
		return;

	g_mutex_lock (priv->pending_mutex);

	send_pending_adds (book_view);
	send_pending_changes (book_view);
	send_pending_removes (book_view);

	g_mutex_unlock (priv->pending_mutex);

	strv_error = e_gdbus_templates_encode_error (error);
	e_gdbus_book_view_emit_complete (priv->gdbus_object, (const gchar * const *) strv_error);
	g_strfreev (strv_error);
}

/**
 * e_data_book_view_notify_progress:
 * @book_view: an #EDataBookView
 * @percent: percent done; use -1 when not available
 * @message: a text message
 *
 * Provides listeners with a human-readable text describing the
 * current backend operation. This can be used for progress
 * reporting.
 **/
void
e_data_book_view_notify_progress (EDataBookView *book_view, guint percent, const gchar *message)
{
	EDataBookViewPrivate *priv = book_view->priv;
	gchar *gdbus_message = NULL;

	if (!priv->running)
		return;

	e_gdbus_book_view_emit_progress (priv->gdbus_object, percent, e_util_ensure_gdbus_string (message, &gdbus_message));

	g_free (gdbus_message);
}

/**
 * e_data_book_view_new:
 * @book: The #EDataBook to search
 * @card_query: The query as a string
 * @card_sexp: The query as an #EBookBackendSExp
 *
 * Create a new #EDataBookView for the given #EBook, filtering on #card_sexp,
 * and place it on DBus at the object path #path.
 */
EDataBookView *
e_data_book_view_new (EDataBook *book, const gchar *card_query, EBookBackendSExp *card_sexp)
{
	EDataBookView *view;
	EDataBookViewPrivate *priv;

	view = g_object_new (E_TYPE_DATA_BOOK_VIEW, NULL);
	priv = view->priv;

	priv->book = book;
	/* Attach a weak reference to the book, so if it dies the book view is destroyed too */
	g_object_weak_ref (G_OBJECT (priv->book), book_destroyed_cb, view);
	priv->backend = g_object_ref (e_data_book_get_backend (book));
	priv->card_query = e_util_utf8_make_valid (card_query);
	priv->card_sexp = card_sexp;

	return view;
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
impl_DataBookView_start (EGdbusBookView *object, GDBusMethodInvocation *invocation, EDataBookView *book_view)
{
	book_view->priv->idle_id = g_idle_add (bookview_idle_start, book_view);

	e_gdbus_book_view_complete_start (object, invocation, NULL);

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
impl_DataBookView_stop (EGdbusBookView *object, GDBusMethodInvocation *invocation, EDataBookView *book_view)
{
	if (book_view->priv->idle_id)
		g_source_remove (book_view->priv->idle_id);

	book_view->priv->idle_id = g_idle_add (bookview_idle_stop, book_view);

	e_gdbus_book_view_complete_stop (object, invocation, NULL);

	return TRUE;
}

static gboolean
impl_DataBookView_dispose (EGdbusBookView *object, GDBusMethodInvocation *invocation, EDataBookView *book_view)
{
	e_gdbus_book_view_complete_dispose (object, invocation, NULL);

	e_book_backend_stop_book_view (book_view->priv->backend, book_view);
	book_view->priv->running = FALSE;

	g_object_unref (book_view);

	return TRUE;
}

static void
e_data_book_view_init (EDataBookView *book_view)
{
	EDataBookViewPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (
		book_view, E_TYPE_DATA_BOOK_VIEW, EDataBookViewPrivate);

	book_view->priv = priv;

	priv->gdbus_object = e_gdbus_book_view_stub_new ();
	g_signal_connect (priv->gdbus_object, "handle-start", G_CALLBACK (impl_DataBookView_start), book_view);
	g_signal_connect (priv->gdbus_object, "handle-stop", G_CALLBACK (impl_DataBookView_stop), book_view);
	g_signal_connect (priv->gdbus_object, "handle-dispose", G_CALLBACK (impl_DataBookView_dispose), book_view);
	g_signal_connect (priv->gdbus_object, "handle-set-fields-of-interest", G_CALLBACK (impl_DataBookView_setFieldsOfInterest), book_view);

	priv->fields_of_interest = NULL;
	priv->running = FALSE;
	priv->pending_mutex = g_mutex_new ();

	priv->adds = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD_ITEMS);
	priv->changes = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD_ITEMS);
	priv->removes = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD_ITEMS);

	priv->ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	priv->flush_id = 0;
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

	g_mutex_lock (priv->pending_mutex);

	if (priv->flush_id) {
		g_source_remove (priv->flush_id);
		priv->flush_id = 0;
	}

	g_mutex_unlock (priv->pending_mutex);

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

	if (priv->fields_of_interest)
		g_hash_table_destroy (priv->fields_of_interest);

	g_mutex_free (priv->pending_mutex);

	g_hash_table_destroy (priv->ids);

	G_OBJECT_CLASS (e_data_book_view_parent_class)->finalize (object);
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
 * e_data_book_view_get_fields_of_interest:
 * @view: A view object.
 *
 * Returns: Hash table of field names which the listener is interested in.
 * Backends can return fully populated objects, but the listener advertised
 * that it will use only these. Returns %NULL for all available fields.
 *
 * Note: The data pointer in the hash table has no special meaning, it's
 * only GINT_TO_POINTER(1) for easier checking. Also, field names are
 * compared case insensitively.
 **/
/* const */ GHashTable *
e_data_book_view_get_fields_of_interest (EDataBookView *view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (view), NULL);

	return view->priv->fields_of_interest;
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

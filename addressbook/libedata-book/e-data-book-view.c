/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * pas-book-view.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib.h>
#include <bonobo/bonobo-main.h>
#include "e-book-backend.h"
#include "e-book-backend-sexp.h"
#include "e-data-book-view.h"

#define d(x)

static BonoboObjectClass *e_data_book_view_parent_class;

struct _EDataBookViewPrivate {
	GNOME_Evolution_Addressbook_BookViewListener  listener;

#define DEFAULT_INITIAL_THRESHOLD 20
#define DEFAULT_THRESHOLD_MAX 3000

	GMutex *mutex;

	GMutex *pending_mutex;

	CORBA_sequence_GNOME_Evolution_Addressbook_VCard adds;
	int next_threshold;
	int threshold_max;
	int threshold_min;

	CORBA_sequence_GNOME_Evolution_Addressbook_VCard changes;
	CORBA_sequence_GNOME_Evolution_Addressbook_ContactId removes;

	EBookBackend *backend;
	char *card_query;
	EBookBackendSExp *card_sexp;
	GHashTable *ids;

	int max_results;
};

static void
view_listener_died_cb (gpointer cnx, gpointer user_data)
{
	EDataBookView *book_view = E_DATA_BOOK_VIEW (user_data);

	if (book_view) {
		e_book_backend_stop_book_view (e_data_book_view_get_backend (book_view), book_view);
		bonobo_object_unref (book_view);
	}
}

static void
send_pending_adds (EDataBookView *book_view, gboolean reset)
{
	CORBA_Environment ev;
	CORBA_sequence_GNOME_Evolution_Addressbook_VCard *adds;

	adds = &book_view->priv->adds;
	if (adds->_length == 0)
		return;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookViewListener_notifyContactsAdded (
		book_view->priv->listener, adds, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("send_pending_adds: Exception signaling BookViewListener!\n");
	}

	CORBA_exception_free (&ev);

	CORBA_free (adds->_buffer);
	adds->_buffer = NULL;
	adds->_maximum = 0;
	adds->_length = 0;

	if (reset)
		book_view->priv->next_threshold = book_view->priv->threshold_min;
}

static void
send_pending_changes (EDataBookView *book_view)
{
	CORBA_Environment ev;
	CORBA_sequence_GNOME_Evolution_Addressbook_VCard *changes;

	changes = &book_view->priv->changes;
	if (changes->_length == 0)
		return;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookViewListener_notifyContactsChanged (
		book_view->priv->listener, changes, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("send_pending_changes: Exception signaling BookViewListener!\n");
	}

	CORBA_exception_free (&ev);

	CORBA_free (changes->_buffer);
	changes->_buffer = NULL;
	changes->_maximum = 0;
	changes->_length = 0;
}

static void
send_pending_removes (EDataBookView *book_view)
{
	CORBA_Environment ev;
	CORBA_sequence_GNOME_Evolution_Addressbook_VCard *removes;

	removes = &book_view->priv->removes;
	if (removes->_length == 0)
		return;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookViewListener_notifyContactsRemoved (
		book_view->priv->listener, removes, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("send_pending_removes: Exception signaling BookViewListener!\n");
	}

	CORBA_exception_free (&ev);

	CORBA_free (removes->_buffer);
	removes->_buffer = NULL;
	removes->_maximum = 0;
	removes->_length = 0;
}

#define MAKE_REALLOC(type)						\
static void								\
CORBA_sequence_ ## type ## _realloc (CORBA_sequence_ ## type *seq,	\
				     CORBA_unsigned_long      new_max)	\
{									\
	type *new_buf;							\
	int i;								\
	new_buf = CORBA_sequence_ ## type ## _allocbuf (new_max);	\
	for (i = 0; i < seq->_maximum; i ++)				\
		new_buf[i] = CORBA_string_dup (seq->_buffer[i]);	\
	CORBA_free (seq->_buffer);					\
	seq->_buffer = new_buf;						\
	seq->_maximum = new_max;					\
}

MAKE_REALLOC (GNOME_Evolution_Addressbook_VCard)
MAKE_REALLOC (GNOME_Evolution_Addressbook_ContactId)

static void
notify_change (EDataBookView *book_view, const char *vcard)
{
	CORBA_sequence_GNOME_Evolution_Addressbook_VCard *changes;

	send_pending_adds (book_view, TRUE);
	send_pending_removes (book_view);

	changes = &book_view->priv->changes;

	if (changes->_length == changes->_maximum) {
		CORBA_sequence_GNOME_Evolution_Addressbook_VCard_realloc (
			changes, 2 * (changes->_maximum + 1));
	}

	changes->_buffer[changes->_length++] = CORBA_string_dup (vcard);
}

static void
notify_remove (EDataBookView *book_view, const char *id)
{
	CORBA_sequence_GNOME_Evolution_Addressbook_ContactId *removes;

	send_pending_adds (book_view, TRUE);
	send_pending_changes (book_view);

	removes = &book_view->priv->removes;

	if (removes->_length == removes->_maximum) {
		CORBA_sequence_GNOME_Evolution_Addressbook_ContactId_realloc (
			removes, 2 * (removes->_maximum + 1));
	}

	removes->_buffer[removes->_length++] = CORBA_string_dup (id);
	g_hash_table_remove (book_view->priv->ids, id);
}

static void
notify_add (EDataBookView *book_view, const char *id, const char *vcard)
{
	CORBA_sequence_GNOME_Evolution_Addressbook_VCard *adds;
	EDataBookViewPrivate *priv = book_view->priv;

	send_pending_changes (book_view);
	send_pending_removes (book_view);

	adds = &priv->adds;

	if (adds->_length == adds->_maximum) {
		send_pending_adds (book_view, FALSE);

		adds->_buffer = CORBA_sequence_GNOME_Evolution_Addressbook_VCard_allocbuf (priv->next_threshold);
		adds->_maximum = priv->next_threshold;

		if (priv->next_threshold < priv->threshold_max) {
			priv->next_threshold = MIN (2 * priv->next_threshold,
						    priv->threshold_max);
		}
	}

	adds->_buffer[adds->_length++] = CORBA_string_dup (vcard);
	g_hash_table_insert (book_view->priv->ids, g_strdup (id),
			     GUINT_TO_POINTER (1));
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
			     EContact    *contact)
{
	gboolean currently_in_view, want_in_view;
	const char *id=NULL;
	char *vcard;

	g_mutex_lock (book_view->priv->pending_mutex);

	id = e_contact_get_const (contact, E_CONTACT_UID);
	if (!id) {
		g_object_unref (contact);
		g_mutex_unlock (book_view->priv->pending_mutex);
		return;
	}

	currently_in_view =
		g_hash_table_lookup (book_view->priv->ids, id) != NULL;
	want_in_view = e_book_backend_sexp_match_contact (
		book_view->priv->card_sexp, contact);

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

	g_mutex_unlock (book_view->priv->pending_mutex);
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
e_data_book_view_notify_update_vcard (EDataBookView *book_view, char *vcard)
{
	gboolean currently_in_view, want_in_view;
	const char *id = NULL;
	EContact *contact;

	g_mutex_lock (book_view->priv->pending_mutex);

	contact = e_contact_new_from_vcard (vcard);
	id = e_contact_get_const (contact, E_CONTACT_UID);
	if (!id) {
		free (vcard);
		g_object_unref (contact);
		g_mutex_unlock (book_view->priv->pending_mutex);
		return;
	}

	currently_in_view =
		g_hash_table_lookup (book_view->priv->ids, id) != NULL;
	want_in_view =
		e_book_backend_sexp_match_contact (book_view->priv->card_sexp, contact);

	if (want_in_view) {
		if (currently_in_view)
			notify_change (book_view, vcard);
		else
			notify_add (book_view, id, vcard);
	} else {
		if (currently_in_view)
			notify_remove (book_view, id);
	}

	g_free (vcard);
	g_object_unref (contact);
	g_mutex_unlock (book_view->priv->pending_mutex);
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
e_data_book_view_notify_update_prefiltered_vcard (EDataBookView *book_view, const char *id, char *vcard)
{
	gboolean currently_in_view;

	g_mutex_lock (book_view->priv->pending_mutex);

	currently_in_view =
		g_hash_table_lookup (book_view->priv->ids, id) != NULL;

	if (currently_in_view)
		notify_change (book_view, vcard);
	else
		notify_add (book_view, id, vcard);

	g_free (vcard);
	g_mutex_unlock (book_view->priv->pending_mutex);
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
e_data_book_view_notify_remove (EDataBookView *book_view,
			     const char  *id)
{
	g_mutex_lock (book_view->priv->pending_mutex);

	if (g_hash_table_lookup (book_view->priv->ids, id))
		notify_remove (book_view, id);

	g_mutex_unlock (book_view->priv->pending_mutex);
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
e_data_book_view_notify_complete (EDataBookView *book_view,
			       GNOME_Evolution_Addressbook_CallStatus status)
{
	CORBA_Environment ev;

	g_mutex_lock (book_view->priv->pending_mutex);

	send_pending_adds (book_view, TRUE);
	send_pending_changes (book_view);
	send_pending_removes (book_view);

	g_mutex_unlock (book_view->priv->pending_mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookViewListener_notifySequenceComplete (
		book_view->priv->listener, status, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("e_data_book_view_notify_complete: Exception signaling BookViewListener!\n");
	}

	CORBA_exception_free (&ev);
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
e_data_book_view_notify_status_message (EDataBookView *book_view,
				     const char  *message)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookViewListener_notifyProgress (
		book_view->priv->listener, message, 0, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("e_data_book_view_notify_status_message: Exception signaling BookViewListener!\n");
	}

	CORBA_exception_free (&ev);
}

static void
e_data_book_view_construct (EDataBookView                *book_view,
			    EBookBackend                 *backend,
			    GNOME_Evolution_Addressbook_BookViewListener  listener,
			    const char                   *card_query,
			    EBookBackendSExp             *card_sexp,
			    int                           max_results)
{
	EDataBookViewPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (book_view != NULL);
	g_return_if_fail (listener != CORBA_OBJECT_NIL);

	priv = book_view->priv;

	CORBA_exception_init (&ev);

	priv->listener = bonobo_object_dup_ref (listener, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning("Unable to duplicate listener object in pas-book-view.c\n");
		CORBA_exception_free (&ev);
		return;
	}

	CORBA_exception_free (&ev);

	priv->backend = backend;
	priv->card_query = g_strdup (card_query);
	priv->card_sexp = card_sexp;
	priv->max_results = max_results;

	ORBit_small_listen_for_broken (e_data_book_view_get_listener (book_view), G_CALLBACK (view_listener_died_cb), book_view);
}

static void
impl_GNOME_Evolution_Addressbook_BookView_start (PortableServer_Servant servant,
						 CORBA_Environment *ev)
{
	EDataBookView *view = E_DATA_BOOK_VIEW (bonobo_object (servant));

	e_book_backend_start_book_view (e_data_book_view_get_backend (view), view);
}

static void
impl_GNOME_Evolution_Addressbook_BookView_stop (PortableServer_Servant servant,
						CORBA_Environment *ev)
{
	EDataBookView *view = E_DATA_BOOK_VIEW (bonobo_object (servant));

	e_book_backend_stop_book_view (e_data_book_view_get_backend (view), view);
}

static void
impl_GNOME_Evolution_Addressbook_BookView_dispose (PortableServer_Servant servant,
						   CORBA_Environment *ev)
{
	EDataBookView *view = E_DATA_BOOK_VIEW (bonobo_object (servant));

	d(printf("in impl_GNOME_Evolution_Addressbook_BookView_dispose\n"));
	ORBit_small_unlisten_for_broken (e_data_book_view_get_listener (view), G_CALLBACK (view_listener_died_cb));

	bonobo_object_unref (view);
}

/**
 * e_data_book_view_get_card_query:
 * @book_view: an #EDataBookView
 *
 * Gets the text representation of the s-expression used
 * for matching contacts to @book_view.
 *
 * Return value: The textual s-expression used.
 **/
const char*
e_data_book_view_get_card_query (EDataBookView *book_view)
{
	g_return_val_if_fail (book_view, NULL);

	return book_view->priv->card_query;
}

/**
 * e_data_book_view_get_card_sexp:
 * @book_view: an #EDataBookView
 *
 * Gets the s-expression used for matching contacts to
 * @book_view.
 *
 * Return value: The #EBookBackendSExp used.
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
 * Return value: The maximum number of results returned.
 **/
int
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
 * Return value: The associated #EBookBackend.
 **/
EBookBackend*
e_data_book_view_get_backend (EDataBookView *book_view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (book_view), NULL);

	return book_view->priv->backend;
}

/**
 * e_data_book_view_get_mutex:
 * @book_view: an #EDataBookView
 *
 * Gets the internal mutex of @book_view.
 *
 * Return value: The #GMutex for @book_view.
 **/
GMutex*
e_data_book_view_get_mutex (EDataBookView *book_view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (book_view), NULL);

	return book_view->priv->mutex;
}

/**
 * e_data_book_view_get_listener:
 * @book_view: an #EDataBookView
 *
 * Gets the CORBA listener that @book_view is dispatching
 * events to.
 *
 * Return value: The CORBA book view listener.
 **/
GNOME_Evolution_Addressbook_BookViewListener
e_data_book_view_get_listener (EDataBookView  *book_view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (book_view), CORBA_OBJECT_NIL);

	return book_view->priv->listener;
}

/**
 * e_data_book_view_set_thresholds:
 * @book_view: an #EDataBookView
 * @minimum_grouping_threshold: the minimum threshold
 * @maximum_grouping_threshold: the maximum threshold
 *
 * Sets the thresholds for grouping add events together
 * for efficiency reasons. The minimum threshold specifies
 * how many adds must be grouped (as long as there are
 * pending adds), and the maximum threshold specifies
 * the maximum number of adds that can be grouped.
 **/
void
e_data_book_view_set_thresholds (EDataBookView *book_view,
				 int minimum_grouping_threshold,
				 int maximum_grouping_threshold)
{
	book_view->priv->threshold_min = minimum_grouping_threshold;
	book_view->priv->threshold_max = maximum_grouping_threshold;
}

/**
 * e_data_book_view_new:
 * @backend: an #EBookBackend to view
 * @listener: a CORBA listener to reveive notifications
 * @card_query: an s-expression representing the query
 * @card_sexp:
 * @max_results: the maximum number of results for the query
 *
 * Return value: A new #EDataBookView.
 **/
EDataBookView *
e_data_book_view_new (EBookBackend *backend,
		      GNOME_Evolution_Addressbook_BookViewListener  listener,
		      const char *card_query,
		      EBookBackendSExp *card_sexp,
		      int max_results)
{
	static GStaticMutex mutex = G_STATIC_MUTEX_INIT;
	static PortableServer_POA poa = NULL;
	EDataBookView *book_view;

	g_static_mutex_lock (&mutex);
	if (poa == NULL)
		poa = bonobo_poa_get_threaded (ORBIT_THREAD_HINT_PER_OBJECT, NULL);
	g_static_mutex_unlock (&mutex);

	book_view = g_object_new (E_TYPE_DATA_BOOK_VIEW, "poa", poa, NULL);

	e_data_book_view_construct (book_view, backend, listener, card_query, card_sexp, max_results);

	return book_view;
}

static void
e_data_book_view_dispose (GObject *object)
{
	EDataBookView *book_view = E_DATA_BOOK_VIEW (object);

	d(printf ("disposing of EDataBookView\n"));
	if (book_view->priv) {
		bonobo_object_release_unref (book_view->priv->listener, NULL);

		if (book_view->priv->adds._buffer)
			CORBA_free (book_view->priv->adds._buffer);
		if (book_view->priv->changes._buffer)
			CORBA_free (book_view->priv->changes._buffer);
		if (book_view->priv->removes._buffer)
			CORBA_free (book_view->priv->removes._buffer);

		g_free (book_view->priv->card_query);
		g_object_unref (book_view->priv->card_sexp);

		g_mutex_free (book_view->priv->pending_mutex);

		g_mutex_free (book_view->priv->mutex);

		g_hash_table_destroy (book_view->priv->ids);

		g_free (book_view->priv);
		book_view->priv = NULL;
	}

	if (G_OBJECT_CLASS (e_data_book_view_parent_class)->dispose)
		G_OBJECT_CLASS (e_data_book_view_parent_class)->dispose (object);
}

static void
e_data_book_view_class_init (EDataBookViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	POA_GNOME_Evolution_Addressbook_BookView__epv *epv;

	e_data_book_view_parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_data_book_view_dispose;

	epv = &klass->epv;

	epv->start                = impl_GNOME_Evolution_Addressbook_BookView_start;
	epv->stop                 = impl_GNOME_Evolution_Addressbook_BookView_stop;
	epv->dispose              = impl_GNOME_Evolution_Addressbook_BookView_dispose;

}

static void
e_data_book_view_init (EDataBookView *book_view)
{
	book_view->priv = g_new0 (EDataBookViewPrivate, 1);

	book_view->priv->pending_mutex = g_mutex_new();
	book_view->priv->mutex = g_mutex_new();

	book_view->priv->threshold_min = DEFAULT_INITIAL_THRESHOLD;
	book_view->priv->threshold_max = DEFAULT_THRESHOLD_MAX;
	book_view->priv->next_threshold = book_view->priv->threshold_min;

	book_view->priv->ids = g_hash_table_new_full (g_str_hash, g_str_equal,
						      g_free, NULL);
}

BONOBO_TYPE_FUNC_FULL (
		       EDataBookView,
		       GNOME_Evolution_Addressbook_BookView,
		       BONOBO_TYPE_OBJECT,
		       e_data_book_view);

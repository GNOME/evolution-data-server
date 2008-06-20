/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Exports the BookViewListener interface.  Maintains a queue of messages
 * which come in on the interface.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#include <config.h>
#include <bonobo/bonobo-main.h>
#include "e-book-view-listener.h"
#include "e-book-view.h"
#include "e-contact.h"

#define d(x)

static EBookViewStatus e_book_view_listener_convert_status (GNOME_Evolution_Addressbook_CallStatus status);

enum {
	RESPONSE,
	LAST_SIGNAL
};

static guint e_book_view_listener_signals [LAST_SIGNAL];

static BonoboObjectClass          *parent_class;

#define READ_END 0
#define WRITE_END 1

struct _EBookViewListenerPrivate {
	guint stopped      : 1;
	GAsyncQueue *queue;

	GMutex *idle_mutex;
	guint idle_id;
};

static void
free_response (EBookViewListenerResponse *response)
{
	g_return_if_fail (response != NULL);

	g_list_foreach (response->ids, (GFunc)g_free, NULL);
	g_list_free (response->ids);
	g_list_foreach (response->contacts, (GFunc) g_object_unref, NULL);
	g_list_free (response->contacts);
	g_free (response->message);
	g_free (response);
}

static gboolean
main_thread_get_response (gpointer data)
{
	EBookViewListener *listener = data;
	EBookViewListenerResponse *response;

	bonobo_object_ref (listener);

	g_mutex_lock (listener->priv->idle_mutex);

	/* remove the idle call */

	while ((response = g_async_queue_try_pop (listener->priv->queue)) != NULL) {

		g_signal_emit (listener, e_book_view_listener_signals [RESPONSE], 0, response);

		free_response (response);
	}

	listener->priv->idle_id = -1;

	g_mutex_unlock (listener->priv->idle_mutex);

	bonobo_object_unref (listener);

	return FALSE;
}

static void
e_book_view_listener_queue_response (EBookViewListener         *listener,
				     EBookViewListenerResponse *response)
{
	if (response == NULL)
		return;

	if (listener->priv->stopped) {
		free_response (response);
		return;
	}

	g_mutex_lock (listener->priv->idle_mutex);

	g_async_queue_push (listener->priv->queue, response);

	if (listener->priv->idle_id == -1)
		listener->priv->idle_id = g_idle_add (main_thread_get_response, listener);
	g_mutex_unlock (listener->priv->idle_mutex);
}

/* Add, Remove, Modify */
static void
e_book_view_listener_queue_status_event (EBookViewListener          *listener,
					 EBookViewListenerOperation  op,
					 EBookViewStatus             status)
{
	EBookViewListenerResponse *resp;

	if (listener->priv->stopped)
		return;

	resp = g_new0 (EBookViewListenerResponse, 1);

	resp->op        = op;
	resp->status    = status;

	e_book_view_listener_queue_response (listener, resp);
}

/* Add, Remove, Modify */
static void
e_book_view_listener_queue_idlist_event (EBookViewListener          *listener,
					 EBookViewListenerOperation  op,
					 const GNOME_Evolution_Addressbook_ContactIdList *ids)
{
	EBookViewListenerResponse *resp;
	int i;

	if (listener->priv->stopped)
		return;

	resp = g_new0 (EBookViewListenerResponse, 1);

	resp->op        = op;
	resp->status    = E_BOOK_VIEW_STATUS_OK;

	for (i = 0; i < ids->_length; i ++) {
		resp->ids = g_list_prepend (resp->ids, g_strdup (ids->_buffer[i]));
	}

	e_book_view_listener_queue_response (listener, resp);
}

/* Add, Remove, Modify */
static void
e_book_view_listener_queue_sequence_event (EBookViewListener          *listener,
					   EBookViewListenerOperation  op,
					   const GNOME_Evolution_Addressbook_VCardList  *vcards)
{
	EBookViewListenerResponse *resp;
	int i;

	if (listener->priv->stopped)
		return;

	resp = g_new0 (EBookViewListenerResponse, 1);

	resp->op        = op;
	resp->status    = E_BOOK_VIEW_STATUS_OK;

	for ( i = 0; i < vcards->_length; i++ ) {
		resp->contacts = g_list_prepend (resp->contacts, e_contact_new_from_vcard (vcards->_buffer[i]));
	}
	resp->contacts = g_list_reverse (resp->contacts);

	e_book_view_listener_queue_response (listener, resp);
}

/* Status Message */
static void
e_book_view_listener_queue_message_event (EBookViewListener          *listener,
					  EBookViewListenerOperation  op,
					  const char                 *message)
{
	EBookViewListenerResponse *resp;

	if (listener->priv->stopped)
		return;

	resp = g_new0 (EBookViewListenerResponse, 1);

	resp->op        = op;
	resp->status    = E_BOOK_VIEW_STATUS_OK;
	resp->message   = g_strdup(message);

	e_book_view_listener_queue_response (listener, resp);
}

static void
impl_BookViewListener_notify_contacts_added (PortableServer_Servant servant,
					     const GNOME_Evolution_Addressbook_VCardList *vcards,
					     CORBA_Environment *ev)
{
	EBookViewListener *listener = E_BOOK_VIEW_LISTENER (bonobo_object (servant));

	d(printf ("%p: impl_BookViewListener_notify_contacts_added (%p)\n", g_thread_self(), listener));

	e_book_view_listener_queue_sequence_event (
		listener, ContactsAddedEvent, vcards);
}

static void
impl_BookViewListener_notify_contacts_removed (PortableServer_Servant servant,
					       const GNOME_Evolution_Addressbook_ContactIdList *ids,
					       CORBA_Environment *ev)
{
	EBookViewListener *listener = E_BOOK_VIEW_LISTENER (bonobo_object (servant));

	d(printf ("%p: impl_BookViewListener_notify_contacts_removed (%p)\n", g_thread_self(), listener));

	e_book_view_listener_queue_idlist_event (listener, ContactsRemovedEvent, ids);
}

static void
impl_BookViewListener_notify_contacts_changed (PortableServer_Servant servant,
					       const GNOME_Evolution_Addressbook_VCardList *vcards,
					       CORBA_Environment *ev)
{
	EBookViewListener *listener = E_BOOK_VIEW_LISTENER (bonobo_object (servant));

	d(printf ("%p: impl_BookViewListener_notify_contacts_changed (%p)\n", g_thread_self(), listener));

	e_book_view_listener_queue_sequence_event (
		listener, ContactsModifiedEvent, vcards);
}

static void
impl_BookViewListener_notify_sequence_complete (PortableServer_Servant servant,
						const GNOME_Evolution_Addressbook_CallStatus status,
						CORBA_Environment *ev)
{
	EBookViewListener *listener = E_BOOK_VIEW_LISTENER (bonobo_object (servant));

	d(printf ("%p: impl_BookViewListener_notify_sequence_complete (%p)\n", g_thread_self(), listener));

	e_book_view_listener_queue_status_event (listener, SequenceCompleteEvent,
						 e_book_view_listener_convert_status (status));
}

static void
impl_BookViewListener_notify_progress (PortableServer_Servant  servant,
				       const char             *message,
				       const CORBA_short       percent,
				       CORBA_Environment      *ev)
{
	EBookViewListener *listener = E_BOOK_VIEW_LISTENER (bonobo_object (servant));

	d(printf ("%p: impl_BookViewListener_notify_progress (%p,`%s')\n", g_thread_self(), listener, message));

	e_book_view_listener_queue_message_event (listener, StatusMessageEvent, message);
}

static EBookViewStatus
e_book_view_listener_convert_status (const GNOME_Evolution_Addressbook_CallStatus status)
{
	switch (status) {
	case GNOME_Evolution_Addressbook_Success:
		return E_BOOK_VIEW_STATUS_OK;
	case GNOME_Evolution_Addressbook_SearchTimeLimitExceeded:
		return E_BOOK_VIEW_STATUS_TIME_LIMIT_EXCEEDED;
	case GNOME_Evolution_Addressbook_SearchSizeLimitExceeded:
		return E_BOOK_VIEW_STATUS_SIZE_LIMIT_EXCEEDED;
	case GNOME_Evolution_Addressbook_InvalidQuery:
		return E_BOOK_VIEW_ERROR_INVALID_QUERY;
	case GNOME_Evolution_Addressbook_QueryRefused:
		return E_BOOK_VIEW_ERROR_QUERY_REFUSED;
	case GNOME_Evolution_Addressbook_OtherError:
	default:
		return E_BOOK_VIEW_ERROR_OTHER_ERROR;
	}
}

/**
 * e_book_view_listener_new:
 *
 * Creates and returns a new #EBookViewListener.
 *
 * Return value: a new #EBookViewListener
 */
EBookViewListener *
e_book_view_listener_new (void)
{
	static GStaticMutex mutex = G_STATIC_MUTEX_INIT;
	static PortableServer_POA poa = NULL;
	EBookViewListener *listener;

	g_static_mutex_lock (&mutex);
	if (poa == NULL)
		poa = bonobo_poa_get_threaded (ORBIT_THREAD_HINT_PER_OBJECT, NULL);
	g_static_mutex_unlock (&mutex);

	listener = g_object_new (E_TYPE_BOOK_VIEW_LISTENER, "poa", poa, NULL);

	listener->priv->queue = g_async_queue_new();
	listener->priv->idle_mutex = g_mutex_new();
	listener->priv->idle_id = -1;

	return listener;
}

static void
e_book_view_listener_init (EBookViewListener *listener)
{
	listener->priv                 = g_new0 (EBookViewListenerPrivate, 1);
	listener->priv->stopped        = TRUE;
}

/**
 * e_book_view_listener_start:
 * @listener: an #EBookViewListener
 *
 * Makes @listener start generating events.
 **/
void
e_book_view_listener_start (EBookViewListener *listener)
{
	g_return_if_fail (E_IS_BOOK_VIEW_LISTENER (listener));
	d(printf ("%p: e_book_view_listener_start (%p)\n", g_thread_self(), listener));
	listener->priv->stopped = FALSE;
}

/**
 * e_book_view_listener_stop:
 * @listener: an #EBookViewListener
 *
 * Makes @listener stop generating events.
 **/
void
e_book_view_listener_stop (EBookViewListener *listener)
{
	g_return_if_fail (E_IS_BOOK_VIEW_LISTENER (listener));
	d(printf ("%p: e_book_view_listener_stop (%p)\n", g_thread_self(), listener));
	listener->priv->stopped = TRUE;

	if (listener->priv->idle_id != -1) {
		g_source_remove (listener->priv->idle_id);
		listener->priv->idle_id = -1;
	}
}

static void
e_book_view_listener_dispose (GObject *object)
{
	EBookViewListener *listener = E_BOOK_VIEW_LISTENER (object);

	d(printf ("%p: in e_book_view_listener_dispose (%p)\n", g_thread_self(), object));

	if (listener->priv) {
		EBookViewListenerResponse *response;

		if (listener->priv->idle_id != -1)
			g_source_remove (listener->priv->idle_id);

		g_mutex_free (listener->priv->idle_mutex);

		/* Free pending events */
		while ((response = g_async_queue_try_pop (listener->priv->queue)) != NULL)
			free_response (response);

		g_async_queue_unref (listener->priv->queue);

		g_free (listener->priv);
		listener->priv = NULL;
	}

	if (G_OBJECT_CLASS (parent_class)->dispose)
		G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_book_view_listener_class_init (EBookViewListenerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	POA_GNOME_Evolution_Addressbook_BookViewListener__epv *epv;

	parent_class = g_type_class_ref (BONOBO_TYPE_OBJECT);

	e_book_view_listener_signals [RESPONSE] =
		g_signal_new ("response",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EBookViewListenerClass, response),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	object_class->dispose = e_book_view_listener_dispose;

	epv = &klass->epv;
	epv->notifyContactsChanged  = impl_BookViewListener_notify_contacts_changed;
	epv->notifyContactsRemoved  = impl_BookViewListener_notify_contacts_removed;
	epv->notifyContactsAdded    = impl_BookViewListener_notify_contacts_added;
	epv->notifySequenceComplete = impl_BookViewListener_notify_sequence_complete;
	epv->notifyProgress         = impl_BookViewListener_notify_progress;
}

BONOBO_TYPE_FUNC_FULL (
		       EBookViewListener,
		       GNOME_Evolution_Addressbook_BookViewListener,
		       BONOBO_TYPE_OBJECT,
		       e_book_view_listener);

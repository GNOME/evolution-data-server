/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 2000, Ximian, Inc.
 */

#include <config.h>
#include <pthread.h>
#include "e-data-book-marshal.h"
#include "e-data-book-view.h"
#include "e-book-backend.h"

struct _EBookBackendPrivate {
	GMutex *open_mutex;

	GMutex *clients_mutex;
	GList *clients;

	ESource *source;
	gboolean loaded, writable, removed;

	GMutex *views_mutex;
	EList *views;
};

/* Signal IDs */
enum {
	LAST_CLIENT_GONE,
	LAST_SIGNAL
};

static guint e_book_backend_signals[LAST_SIGNAL];

static GObjectClass *parent_class;

gboolean
e_book_backend_construct (EBookBackend *backend)
{
	return TRUE;
}

GNOME_Evolution_Addressbook_CallStatus
e_book_backend_load_source (EBookBackend           *backend,
			    ESource                *source,
			    gboolean                only_if_exists)
{
	GNOME_Evolution_Addressbook_CallStatus status;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);
	g_return_val_if_fail (source, FALSE);
	g_return_val_if_fail (backend->priv->loaded == FALSE, FALSE);

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->load_source);

	status = (* E_BOOK_BACKEND_GET_CLASS (backend)->load_source) (backend, source, only_if_exists);

	if (status == GNOME_Evolution_Addressbook_Success || status == GNOME_Evolution_Addressbook_InvalidServerVersion) {
		g_object_ref (source);
		backend->priv->source = source;
	}

	return status;
}

/**
 * e_book_backend_get_source:
 * @backend: An addressbook backend.
 * 
 * Queries the source that an addressbook backend is serving.
 * 
 * Return value: ESource for the backend.
 **/
ESource *
e_book_backend_get_source (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	return backend->priv->source;
}

void
e_book_backend_open (EBookBackend *backend,
		     EDataBook    *book,
		     guint32       opid,
		     gboolean      only_if_exists)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	g_mutex_lock (backend->priv->open_mutex);

	if (backend->priv->loaded) {
		e_data_book_respond_open (
			book, opid, GNOME_Evolution_Addressbook_Success);

		e_data_book_report_writable (book, backend->priv->writable);
	} else {
		GNOME_Evolution_Addressbook_CallStatus status =
			e_book_backend_load_source (backend, e_data_book_get_source (book), only_if_exists);

		e_data_book_respond_open (book, opid, status);

		if (status == GNOME_Evolution_Addressbook_Success || status == GNOME_Evolution_Addressbook_InvalidServerVersion)
			e_data_book_report_writable (book, backend->priv->writable);
	}

	g_mutex_unlock (backend->priv->open_mutex);
}

void
e_book_backend_remove (EBookBackend *backend,
		       EDataBook    *book,
		       guint32       opid)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->remove);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->remove) (backend, book, opid);
}

void
e_book_backend_create_contact (EBookBackend *backend,
			       EDataBook    *book,
			       guint32       opid,
			       const char   *vcard)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (vcard);

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->create_contact);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->create_contact) (backend, book, opid, vcard);
}

void
e_book_backend_remove_contacts (EBookBackend *backend,
				EDataBook    *book,
				guint32       opid,
				GList        *id_list)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (id_list);

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->remove_contacts);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->remove_contacts) (backend, book, opid, id_list);
}

void
e_book_backend_modify_contact (EBookBackend *backend,
			       EDataBook    *book,
			       guint32       opid,
			       const char   *vcard)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (vcard);

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->modify_contact);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->modify_contact) (backend, book, opid, vcard);
}

void
e_book_backend_get_contact (EBookBackend *backend,
			    EDataBook    *book,
			    guint32       opid,
			    const char   *id)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (id);

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->get_contact);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->get_contact) (backend, book, opid, id);
}

void
e_book_backend_get_contact_list (EBookBackend *backend,
				 EDataBook    *book,
				 guint32       opid,
				 const char   *query)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (query);

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->get_contact_list);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->get_contact_list) (backend, book, opid, query);
}

void
e_book_backend_start_book_view (EBookBackend  *backend,
				EDataBookView *book_view)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (book_view));

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->start_book_view);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->start_book_view) (backend, book_view);
}

void
e_book_backend_stop_book_view (EBookBackend  *backend,
			       EDataBookView *book_view)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (book_view));

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->stop_book_view);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->stop_book_view) (backend, book_view);
}

void
e_book_backend_get_changes (EBookBackend *backend,
			    EDataBook    *book,
			    guint32       opid,
			    const char   *change_id)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (change_id);

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->get_changes);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->get_changes) (backend, book, opid, change_id);
}

void
e_book_backend_authenticate_user (EBookBackend *backend,
				  EDataBook    *book,
				  guint32       opid,
				  const char   *user,
				  const char   *passwd,
				  const char   *auth_method)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (user && passwd && auth_method);

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->authenticate_user);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->authenticate_user) (backend, book, opid, user, passwd, auth_method);
}

void
e_book_backend_get_required_fields (EBookBackend *backend,
				     EDataBook    *book,
				     guint32       opid)

{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->get_required_fields);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->get_required_fields) (backend, book, opid);
}

void
e_book_backend_get_supported_fields (EBookBackend *backend,
				     EDataBook    *book,
				     guint32       opid)

{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->get_supported_fields);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->get_supported_fields) (backend, book, opid);
}

void
e_book_backend_get_supported_auth_methods (EBookBackend *backend,
					   EDataBook    *book,
					   guint32       opid)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->get_supported_auth_methods);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->get_supported_auth_methods) (backend, book, opid);
}

GNOME_Evolution_Addressbook_CallStatus
e_book_backend_cancel_operation (EBookBackend *backend,
				 EDataBook    *book)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->cancel_operation);

	return (* E_BOOK_BACKEND_GET_CLASS (backend)->cancel_operation) (backend, book);
}

static void
book_destroy_cb (gpointer data, GObject *where_book_was)
{
	EBookBackend *backend = E_BOOK_BACKEND (data);

	e_book_backend_remove_client (backend, (EDataBook *)where_book_was);
}

static void
listener_died_cb (gpointer cnx, gpointer user_data)
{
	EDataBook *book = E_DATA_BOOK (user_data);

	e_book_backend_remove_client (e_data_book_get_backend (book), book);
}

static void
last_client_gone (EBookBackend *backend)
{
	g_signal_emit (backend, e_book_backend_signals[LAST_CLIENT_GONE], 0);
}

EList*
e_book_backend_get_book_views (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	return g_object_ref (backend->priv->views);
}

void
e_book_backend_add_book_view (EBookBackend *backend,
			      EDataBookView *view)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_mutex_lock (backend->priv->views_mutex);

	e_list_append (backend->priv->views, view);

	g_mutex_unlock (backend->priv->views_mutex);
}

void
e_book_backend_remove_book_view (EBookBackend *backend,
				 EDataBookView *view)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_mutex_lock (backend->priv->views_mutex);

	e_list_remove (backend->priv->views, view);

	g_mutex_unlock (backend->priv->views_mutex);
}

/**
 * e_book_backend_add_client:
 * @backend: An addressbook backend.
 * @book: the corba object representing the client connection.
 *
 * Adds a client to an addressbook backend.
 *
 * Return value: TRUE on success, FALSE on failure to add the client.
 */
gboolean
e_book_backend_add_client (EBookBackend      *backend,
			   EDataBook         *book)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), FALSE);

	bonobo_object_set_immortal (BONOBO_OBJECT (book), TRUE);

	g_object_weak_ref (G_OBJECT (book), book_destroy_cb, backend);

	ORBit_small_listen_for_broken (e_data_book_get_listener (book), G_CALLBACK (listener_died_cb), book);

	g_mutex_lock (backend->priv->clients_mutex);
	backend->priv->clients = g_list_prepend (backend->priv->clients, book);
	g_mutex_unlock (backend->priv->clients_mutex);

	return TRUE;
}

void
e_book_backend_remove_client (EBookBackend *backend,
			      EDataBook    *book)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	/* up our backend's refcount here so that last_client_gone
	   doesn't end up unreffing us (while we're holding the
	   lock) */
	g_object_ref (backend);

	/* Disconnect */
	g_mutex_lock (backend->priv->clients_mutex);
	backend->priv->clients = g_list_remove (backend->priv->clients, book);

	/* When all clients go away, notify the parent factory about it so that
	 * it may decide whether to kill the backend or not.
	 */
	if (!backend->priv->clients)
		last_client_gone (backend);

	g_mutex_unlock (backend->priv->clients_mutex);

	g_object_unref (backend);
}

char *
e_book_backend_get_static_capabilities (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);
	
	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->get_static_capabilities);

	return E_BOOK_BACKEND_GET_CLASS (backend)->get_static_capabilities (backend);
}

gboolean
e_book_backend_is_loaded (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	return backend->priv->loaded;
}

void
e_book_backend_set_is_loaded (EBookBackend *backend, gboolean is_loaded)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	backend->priv->loaded = is_loaded;
}

gboolean
e_book_backend_is_writable (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);
	
	return backend->priv->writable;
}

void
e_book_backend_set_is_writable (EBookBackend *backend, gboolean is_writable)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	
	backend->priv->writable = is_writable;
}

gboolean
e_book_backend_is_removed (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);
	
	return backend->priv->removed;
}

void
e_book_backend_set_is_removed (EBookBackend *backend, gboolean is_removed)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	
	backend->priv->removed = is_removed;
}

void 
e_book_backend_set_mode (EBookBackend *backend,
			 GNOME_Evolution_Addressbook_BookMode  mode)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_assert (E_BOOK_BACKEND_GET_CLASS (backend)->set_mode);

        (* E_BOOK_BACKEND_GET_CLASS (backend)->set_mode) (backend,  mode);	

}

GNOME_Evolution_Addressbook_BookChangeItem*
e_book_backend_change_add_new     (const char *vcard)
{
	GNOME_Evolution_Addressbook_BookChangeItem* new_change = GNOME_Evolution_Addressbook_BookChangeItem__alloc();

	new_change->changeType= GNOME_Evolution_Addressbook_ContactAdded;
	new_change->vcard = CORBA_string_dup (vcard);

	return new_change;
}

GNOME_Evolution_Addressbook_BookChangeItem*
e_book_backend_change_modify_new  (const char *vcard)
{
	GNOME_Evolution_Addressbook_BookChangeItem* new_change = GNOME_Evolution_Addressbook_BookChangeItem__alloc();

	new_change->changeType= GNOME_Evolution_Addressbook_ContactModified;
	new_change->vcard = CORBA_string_dup (vcard);

	return new_change;
}

GNOME_Evolution_Addressbook_BookChangeItem*
e_book_backend_change_delete_new  (const char *vcard)
{
	GNOME_Evolution_Addressbook_BookChangeItem* new_change = GNOME_Evolution_Addressbook_BookChangeItem__alloc();

	new_change->changeType= GNOME_Evolution_Addressbook_ContactDeleted;
	new_change->vcard = CORBA_string_dup (vcard);

	return new_change;
}



static void
e_book_backend_foreach_view (EBookBackend *backend,
			     void (*callback) (EDataBookView *, gpointer),
			     gpointer user_data)
{
	EList *views;
	EDataBookView *view;
	EIterator *iter;

	views = e_book_backend_get_book_views (backend);
	iter = e_list_get_iterator (views);

	while (e_iterator_is_valid (iter)) {
		view = (EDataBookView*)e_iterator_get (iter);

		bonobo_object_ref (view);
		callback (view, user_data);
		bonobo_object_unref (view);

		e_iterator_next (iter);
	}

	g_object_unref (iter);
	g_object_unref (views);
}


static void
view_notify_update (EDataBookView *view, gpointer contact)
{
	e_data_book_view_notify_update (view, contact);
}

/**
 * e_book_backend_notify_update:
 * @backend: an addressbook backend
 * @contact: a new or modified contact
 *
 * Notifies all of @backend's book views about the new or modified
 * contacts @contact.
 *
 * e_data_book_respond_create() and e_data_book_respond_modify() call this
 * function for you. You only need to call this from your backend if
 * contacts are created or modified by another (non-PAS-using) client.
 **/
void
e_book_backend_notify_update (EBookBackend *backend, EContact *contact)
{
	e_book_backend_foreach_view (backend, view_notify_update, contact);
}


static void
view_notify_remove (EDataBookView *view, gpointer id)
{
	e_data_book_view_notify_remove (view, id);
}

/**
 * e_book_backend_notify_remove:
 * @backend: an addressbook backend
 * @id: a contact id
 *
 * Notifies all of @backend's book views that the contact with UID
 * @id has been removed.
 *
 * e_data_book_respond_remove_contacts() calls this function for you. You
 * only need to call this from your backend if contacts are removed by
 * another (non-PAS-using) client.
 **/
void
e_book_backend_notify_remove (EBookBackend *backend, const char *id)
{
	e_book_backend_foreach_view (backend, view_notify_remove, (gpointer)id);
}


static void
view_notify_complete (EDataBookView *view, gpointer unused)
{
	e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_Success);
}

/**
 * e_book_backend_notify_complete:
 * @backend: an addressbook backend
 *
 * Notifies all of @backend's book views that the current set of
 * notifications is complete; use this after a series of
 * e_book_backend_notify_update() and e_book_backend_notify_remove() calls.
 **/
void
e_book_backend_notify_complete (EBookBackend *backend)
{
	e_book_backend_foreach_view (backend, view_notify_complete, NULL);
}


/** e_book_backend_notify_writable
 * @backend: an addressbook backend
 * @is_writable: flag indicating writable status
 *
 * Notifies all backends clients about the current writable state 
 **/
void 
e_book_backend_notify_writable (EBookBackend *backend, gboolean is_writable)
{
	EBookBackendPrivate *priv;
	GList *clients;
	
	priv = backend->priv;
	g_mutex_lock (priv->clients_mutex);
	
	for (clients = priv->clients; clients != NULL; clients = g_list_next (clients))
		e_data_book_report_writable (E_DATA_BOOK (clients->data), is_writable);

	g_mutex_unlock (priv->clients_mutex);

}

void 
e_book_backend_notify_connection_status (EBookBackend *backend, gboolean is_online)
{
	EBookBackendPrivate *priv;
	GList *clients;
	
	priv = backend->priv;
	g_mutex_lock (priv->clients_mutex);
	
	for (clients = priv->clients; clients != NULL; clients = g_list_next (clients))
		e_data_book_report_connection_status (E_DATA_BOOK (clients->data), is_online);

	g_mutex_unlock (priv->clients_mutex);




}

void
e_book_backend_notify_auth_required (EBookBackend *backend)
{
	EBookBackendPrivate *priv;
	GList *clients;
	
	priv = backend->priv;
	g_mutex_lock (priv->clients_mutex);
	
	for (clients = priv->clients; clients != NULL; clients = g_list_next (clients))
		e_data_book_report_auth_required (E_DATA_BOOK (clients->data));
	g_mutex_unlock (priv->clients_mutex);


}

static void
e_book_backend_init (EBookBackend *backend)
{
	EBookBackendPrivate *priv;

	priv          = g_new0 (EBookBackendPrivate, 1);
	priv->clients = NULL;
	priv->views   = e_list_new((EListCopyFunc) NULL, (EListFreeFunc) NULL, NULL);
	priv->open_mutex = g_mutex_new ();
	priv->clients_mutex = g_mutex_new ();
	priv->views_mutex = g_mutex_new ();

	backend->priv = priv;
}

static void
e_book_backend_dispose (GObject *object)
{
	EBookBackend *backend;

	backend = E_BOOK_BACKEND (object);

	if (backend->priv) {
		g_list_free (backend->priv->clients);

		if (backend->priv->views) {
			g_object_unref (backend->priv->views);
			backend->priv->views = NULL;
		}

		g_mutex_free (backend->priv->open_mutex);
		g_mutex_free (backend->priv->clients_mutex);
		g_mutex_free (backend->priv->views_mutex);

		g_free (backend->priv);
		backend->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_book_backend_class_init (EBookBackendClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);

	object_class = (GObjectClass *) klass;

	object_class->dispose = e_book_backend_dispose;

	e_book_backend_signals[LAST_CLIENT_GONE] =
		g_signal_new ("last_client_gone",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EBookBackendClass, last_client_gone),
			      NULL, NULL,
			      e_data_book_marshal_NONE__NONE,
			      G_TYPE_NONE, 0);
}

/**
 * e_book_backend_get_type:
 */
GType
e_book_backend_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (EBookBackendClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_book_backend_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (EBookBackend),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_book_backend_init
		};

		type = g_type_register_static (G_TYPE_OBJECT, "EBookBackend", &info, 0);
	}

	return type;
}

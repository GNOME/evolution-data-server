/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * pas-book.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-arg.h>
#include "libedataserver/e-list.h"
#include <libebook/e-contact.h>
#include "e-data-book-view.h"
#include "e-book-backend.h"
#include "e-book-backend-sexp.h"
#include "e-data-book.h"

static BonoboObjectClass *e_data_book_parent_class;
POA_GNOME_Evolution_Addressbook_Book__vepv e_data_book_vepv;

struct _EDataBookPrivate {
	EBookBackend                               *backend;
	GNOME_Evolution_Addressbook_BookListener  listener;
	ESource                                    *source;
};

static void
impl_GNOME_Evolution_Addressbook_Book_open (PortableServer_Servant servant,
					    const CORBA_long opid,
					    const CORBA_boolean only_if_exists,
					    CORBA_Environment *ev)
{
	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));

	printf ("impl_GNOME_Evolution_Addressbook_Book_open (%p)\n", book);

	e_book_backend_open (e_data_book_get_backend (book), book, opid, only_if_exists);
}

static void
impl_GNOME_Evolution_Addressbook_Book_remove (PortableServer_Servant servant,
					      const CORBA_long opid,
					      CORBA_Environment *ev)
{
	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));

	printf ("impl_GNOME_Evolution_Addressbook_Book_remove\n");

	e_book_backend_remove (e_data_book_get_backend (book), book, opid);
}

static void
impl_GNOME_Evolution_Addressbook_Book_getContact (PortableServer_Servant servant,
						  const CORBA_long opid,
						  const CORBA_char *id,
						  CORBA_Environment *ev)
{
	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));

	printf ("impl_GNOME_Evolution_Addressbook_Book_getContact\n");

	e_book_backend_get_contact (e_data_book_get_backend (book), book, opid, id);
}

static void
impl_GNOME_Evolution_Addressbook_Book_getContactList (PortableServer_Servant servant,
						      const CORBA_long opid,
						      const CORBA_char *query,
						      CORBA_Environment *ev)
{
	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));

	printf ("impl_GNOME_Evolution_Addressbook_Book_getContactList\n");

	e_book_backend_get_contact_list (e_data_book_get_backend (book), book, opid, query);
}

static void
impl_GNOME_Evolution_Addressbook_Book_authenticateUser (PortableServer_Servant servant,
							const CORBA_long opid,
							const char* user,
							const char* passwd,
							const char* auth_method,
							CORBA_Environment *ev)
{
	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));

	e_book_backend_authenticate_user (e_data_book_get_backend (book), book,
					  opid, user, passwd, auth_method);
}

static void
impl_GNOME_Evolution_Addressbook_Book_addContact (PortableServer_Servant servant,
						  const CORBA_long opid,
						  const CORBA_char *vcard,
						  CORBA_Environment *ev)
{
	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));

	e_book_backend_create_contact (e_data_book_get_backend (book), book, opid, vcard);
}

static void
impl_GNOME_Evolution_Addressbook_Book_removeContacts (PortableServer_Servant servant,
						      const CORBA_long opid,
						      const GNOME_Evolution_Addressbook_ContactIdList *ids,
						      CORBA_Environment *ev)
{
	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));
	int i;
	GList *id_list = NULL;

	for (i = 0; i < ids->_length; i ++)
		id_list = g_list_append (id_list, ids->_buffer[i]);

	e_book_backend_remove_contacts (e_data_book_get_backend (book), book, opid, id_list);

	g_list_free (id_list);
}

static void
impl_GNOME_Evolution_Addressbook_Book_modifyContact (PortableServer_Servant servant,
						     const CORBA_long opid,
						     const CORBA_char *vcard,
						     CORBA_Environment *ev)
{
	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));

	e_book_backend_modify_contact (e_data_book_get_backend (book), book, opid, vcard);
}

static void
impl_GNOME_Evolution_Addressbook_Book_getBookView (PortableServer_Servant servant,
						   const CORBA_long opid,
						   const GNOME_Evolution_Addressbook_BookViewListener listener,
						   const CORBA_char *search,
						   const GNOME_Evolution_Addressbook_stringlist* requested_fields,
						   const CORBA_long max_results,
						   CORBA_Environment *ev)
{
	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));
	EBookBackend *backend = e_data_book_get_backend (book);
	EBookBackendSExp *card_sexp;
	EDataBookView *view;

	g_warning ("impl_GNOME_Evolution_Addressbook_Book_getBookView (%s)\n", search);

	/* we handle this entirely here, since it doesn't require any
	   backend involvement now that we have e_data_book_view_start to
	   actually kick off the search. */

	card_sexp = e_book_backend_sexp_new (search);
	if (!card_sexp) {
		e_data_book_respond_get_book_view (book, opid, GNOME_Evolution_Addressbook_InvalidQuery, NULL);
		return;
	}

	/* XXX still need to add requested_fields here */
	view = e_data_book_view_new (backend, listener, search, card_sexp, max_results);

	if (!view) {
		g_object_unref (card_sexp);
		e_data_book_respond_get_book_view (book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
		return;
	}

	e_book_backend_add_book_view (backend, view);

	e_data_book_respond_get_book_view (book, opid, GNOME_Evolution_Addressbook_Success, view);
}


static void
impl_GNOME_Evolution_Addressbook_Book_getChanges (PortableServer_Servant servant,
						  const CORBA_long opid,
						  const CORBA_char *change_id,
						  CORBA_Environment *ev)
{
	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));

	e_book_backend_get_changes (e_data_book_get_backend (book), book, opid, change_id);
}

static char *
impl_GNOME_Evolution_Addressbook_Book_getStaticCapabilities (PortableServer_Servant servant,
							     CORBA_Environment *ev)
{
	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));
	char *temp;
	char *ret_val;

	temp = e_book_backend_get_static_capabilities (book->priv->backend);
	ret_val = CORBA_string_dup(temp);
	g_free(temp);
	return ret_val;
}


static void
impl_GNOME_Evolution_Addressbook_Book_getRequiredFields (PortableServer_Servant servant,
							  const CORBA_long opid,
							  CORBA_Environment *ev)

{

	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));

	e_book_backend_get_required_fields (e_data_book_get_backend (book), book, opid);

}

static void
impl_GNOME_Evolution_Addressbook_Book_getSupportedFields (PortableServer_Servant servant,
							  const CORBA_long opid,
							  CORBA_Environment *ev)
{
	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));

	e_book_backend_get_supported_fields (e_data_book_get_backend (book), book, opid);
}

static void
impl_GNOME_Evolution_Addressbook_Book_getSupportedAuthMethods (PortableServer_Servant servant,
							       const CORBA_long opid,
							       CORBA_Environment *ev)
{
	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));

	e_book_backend_get_supported_auth_methods (e_data_book_get_backend (book), book, opid);
}

static GNOME_Evolution_Addressbook_CallStatus
impl_GNOME_Evolution_Addressbook_Book_cancelOperation (PortableServer_Servant servant,
						       CORBA_Environment *ev)
{
	EDataBook *book = E_DATA_BOOK (bonobo_object (servant));

	return e_book_backend_cancel_operation (e_data_book_get_backend (book), book);
}


/**
 * e_data_book_get_backend:
 * @book: an #EDataBook
 *
 * Gets the #EBookBackend being used to store data for @book.
 *
 * Return value: The #EBookBackend being used.
 **/
EBookBackend *
e_data_book_get_backend (EDataBook *book)
{
	g_return_val_if_fail (book && E_IS_DATA_BOOK (book), NULL);

	return book->priv->backend;
}

/**
 * e_data_book_get_listener:
 * @book: an #EDataBook
 *
 * Gets the CORBA listener associated with @book.
 *
 * Return value: A #GNOME_Evolution_Addressbook_BookListener.
 **/
GNOME_Evolution_Addressbook_BookListener
e_data_book_get_listener (EDataBook *book)
{
	g_return_val_if_fail (book && E_IS_DATA_BOOK (book), CORBA_OBJECT_NIL);

	return book->priv->listener;
}

/**
 * e_data_book_get_source:
 * @book: an #EDataBook
 *
 * Gets the #ESource associated with @book.
 *
 * Return value: An #ESource.
 **/
ESource *
e_data_book_get_source (EDataBook *book)
{
	return book->priv->source;
}

/**
 * e_data_book_respond_open:
 * @book: an #EDataBook
 * @opid: the operation ID that generated the response
 * @status: the outcome of the operation
 *
 * Respond to an 'open' request specified by @opid on @book,
 * indicating @status as the outcome.
 **/
void
e_data_book_respond_open (EDataBook                              *book,
			  guint32                                 opid,
			  GNOME_Evolution_Addressbook_CallStatus  status)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	GNOME_Evolution_Addressbook_BookListener_notifyBookOpened (book->priv->listener, opid, status, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("e_data_book_respond_open: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
}

/**
 * e_data_book_respond_remove:
 * @book: an #EDataBook
 * @opid: the operation ID that generated the response
 * @status: the outcome of the operation
 *
 * Respond to a 'remove' request to remove all of @book's data,
 * specified by @opid, indicating @status as the outcome.
 **/
void
e_data_book_respond_remove (EDataBook                           *book,
			    guint32                              opid,
			    GNOME_Evolution_Addressbook_CallStatus  status)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	GNOME_Evolution_Addressbook_BookListener_notifyBookRemoved (book->priv->listener, opid, status, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("e_data_book_respond_remove: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
}

/**
 * e_data_book_respond_create:
 * @book: an #EDataBook
 * @opid: the operation ID that generated the response
 * @status: the outcome of the operation
 * @contact: the contact created, or %NULL
 *
 * Respond to a 'create' request specified by @opid on @book,
 * indicating @status as the outcome.
 **/
void
e_data_book_respond_create (EDataBook                              *book,
			    guint32                                 opid,
			    GNOME_Evolution_Addressbook_CallStatus  status,
			    EContact                               *contact)
{
	CORBA_Environment ev;

	if (status == GNOME_Evolution_Addressbook_Success) {
		e_book_backend_notify_update (book->priv->backend, contact);
		e_book_backend_notify_complete (book->priv->backend);
	}

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookListener_notifyContactCreated (
		book->priv->listener, opid, status,
		status == GNOME_Evolution_Addressbook_Success ? e_contact_get_const (contact, E_CONTACT_UID) : "",
		&ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("e_data_book_respond_create: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
}

/**
 * e_data_book_respond_remove_contacts:
 * @book: an #EDataBook
 * @opid: the operation ID that generated the response
 * @status: the outcome of the operation
 * @ids: a list of contact IDs removed
 *
 * Respond to a 'remove contacts' request specified by @opid on @book,
 * indicating @status as the outcome.
 **/
void
e_data_book_respond_remove_contacts (EDataBook                              *book,
				     guint32                                 opid,
				     GNOME_Evolution_Addressbook_CallStatus  status,
				     GList                                  *ids)
{
	CORBA_Environment ev;
	GList *i;

	CORBA_exception_init (&ev);

	if (ids) {
		for (i = ids; i; i = i->next)
			e_book_backend_notify_remove (book->priv->backend, i->data);
		e_book_backend_notify_complete (book->priv->backend);
	}

	GNOME_Evolution_Addressbook_BookListener_notifyContactsRemoved (
		book->priv->listener, opid, status, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("e_data_book_respond_remove: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
}

/**
 * e_data_book_respond_modify:
 * @book: an #EDataBook
 * @opid: the operation ID that generated the response
 * @status: the outcome of the operation
 * @contact: the modified #EContact
 *
 * Respond to a 'modify' request specified by @opid on @book,
 * indicating @status as the outcome.
 **/
void
e_data_book_respond_modify (EDataBook                              *book,
			    guint32                                 opid,
			    GNOME_Evolution_Addressbook_CallStatus  status,
			    EContact                               *contact)
{
	CORBA_Environment ev;

	if (status == GNOME_Evolution_Addressbook_Success) {
		e_book_backend_notify_update (book->priv->backend, contact);
		e_book_backend_notify_complete (book->priv->backend);
	}

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookListener_notifyContactModified (
		book->priv->listener, opid, status, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("e_data_book_respond_modify: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
}

/**
 * e_data_book_respond_authenticate_user:
 * @book: an #EDataBook
 * @opid: the operation ID that generated the response
 * @status: the outcome of the operation
 *
 * Respond to an 'authenticate' request specified by @opid on @book,
 * indicating @status as the outcome.
 **/
void
e_data_book_respond_authenticate_user (EDataBook                              *book,
				       guint32                                 opid,
				       GNOME_Evolution_Addressbook_CallStatus  status)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookListener_notifyAuthenticationResult (
		book->priv->listener, opid, status, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("e_data_book_respond_authenticate_user: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
}

/**
 * e_data_book_respond_get_required_fields:
 * @book: an #EDataBook
 * @opid: the operation ID that generated the response
 * @status: the outcome of the operation
 * @fields: a list of required field names
 *
 * Respond to a 'get required fields' request specified by @opid on @book,
 * indicating @status as the outcome.
 **/
void
e_data_book_respond_get_required_fields (EDataBook                              *book,
					  guint32                                 opid,
					  GNOME_Evolution_Addressbook_CallStatus  status,
					  GList                                  *fields)
{

	CORBA_Environment ev;
	GNOME_Evolution_Addressbook_stringlist stringlist;
	int num_fields;
	int i;
	GList *iter;

	CORBA_exception_init (&ev);

	num_fields = g_list_length (fields);

	stringlist._buffer = CORBA_sequence_CORBA_string_allocbuf (num_fields);
	stringlist._maximum = num_fields;
	stringlist._length = num_fields;

	for (i = 0, iter = fields; iter; iter = iter->next, i ++) {
		stringlist._buffer[i] = CORBA_string_dup ((char*)iter->data);
	}

	printf ("calling GNOME_Evolution_Addressbook_BookListener_notifyRequiredFields\n");

	GNOME_Evolution_Addressbook_BookListener_notifyRequiredFields (
			book->priv->listener, opid, status,
			&stringlist,
			&ev);

	CORBA_exception_free (&ev);

	CORBA_free(stringlist._buffer);
}

/**
 * e_data_book_respond_get_supported_fields:
 * @book: an #EDataBook
 * @opid: the operation ID that generated the response
 * @status: the outcome of the operation
 * @fields: a list of supported field names
 *
 * Respond to a 'get supported fields' request specified by @opid on @book,
 * indicating @status as the outcome.
 **/
void
e_data_book_respond_get_supported_fields (EDataBook                              *book,
					  guint32                                 opid,
					  GNOME_Evolution_Addressbook_CallStatus  status,
					  GList                                  *fields)
{
	CORBA_Environment ev;
	GNOME_Evolution_Addressbook_stringlist stringlist;
	int num_fields;
	int i;
	GList *iter;

	CORBA_exception_init (&ev);

	num_fields = g_list_length (fields);

	stringlist._buffer = CORBA_sequence_CORBA_string_allocbuf (num_fields);
	stringlist._maximum = num_fields;
	stringlist._length = num_fields;

	for (i = 0, iter = fields; iter; iter = iter->next, i ++) {
		stringlist._buffer[i] = CORBA_string_dup ((char*)iter->data);
	}

	printf ("calling GNOME_Evolution_Addressbook_BookListener_notifySupportedFields\n");

	GNOME_Evolution_Addressbook_BookListener_notifySupportedFields (
			book->priv->listener, opid, status,
			&stringlist,
			&ev);

	CORBA_exception_free (&ev);

	CORBA_free(stringlist._buffer);
}

/**
 * e_data_book_respond_get_supported_auth_methods:
 * @book: an #EDataBook
 * @opid: the operation ID that generated the response
 * @status: the outcome of the operation
 * @auth_methods: a list of names for supported auth methods
 *
 * Respond to a 'get supported auth methods' request specified by @opid on @book,
 * indicating @status as the outcome.
 **/
void
e_data_book_respond_get_supported_auth_methods (EDataBook                              *book,
						guint32                                 opid,
						GNOME_Evolution_Addressbook_CallStatus  status,
						GList                                  *auth_methods)
{
	CORBA_Environment ev;
	GNOME_Evolution_Addressbook_stringlist stringlist;
	int num_auth_methods;
	GList *iter;
	int i;

	CORBA_exception_init (&ev);

	num_auth_methods = g_list_length (auth_methods);

	stringlist._buffer = CORBA_sequence_CORBA_string_allocbuf (num_auth_methods);
	stringlist._maximum = num_auth_methods;
	stringlist._length = num_auth_methods;

	for (i = 0, iter = auth_methods; iter; iter = iter->next, i ++) {
		stringlist._buffer[i] = CORBA_string_dup ((char*)iter->data);
	}

	GNOME_Evolution_Addressbook_BookListener_notifySupportedAuthMethods (
			book->priv->listener, opid, status,
			&stringlist,
			&ev);

	CORBA_exception_free (&ev);

	CORBA_free(stringlist._buffer);
}

static void
view_destroy(gpointer data, GObject *where_object_was)
{
	EDataBook           *book = (EDataBook *)data;
	e_book_backend_remove_book_view (book->priv->backend, (EDataBookView*)where_object_was);
}

/**
 * e_data_book_respond_get_book_view:
 * @book: an #EDataBook
 * @opid: the operation ID that generated the response
 * @status: the outcome of the operation
 * @book_view: the #EDataBookView created
 *
 * Respond to a 'get book view' request specified by @opid on @book,
 * indicating @status as the outcome.
 **/
void
e_data_book_respond_get_book_view (EDataBook                              *book,
				   guint32                                 opid,
				   GNOME_Evolution_Addressbook_CallStatus  status,
				   EDataBookView                          *book_view)
{
	CORBA_Environment ev;
	CORBA_Object      object = CORBA_OBJECT_NIL;

	printf ("e_data_book_respond_get_book_view\n");

	CORBA_exception_init (&ev);

	if (book_view) {
		object = bonobo_object_corba_objref(BONOBO_OBJECT(book_view));

		g_object_weak_ref (G_OBJECT (book_view), view_destroy, book);
	}

	GNOME_Evolution_Addressbook_BookListener_notifyViewRequested (
		book->priv->listener, opid, status, object, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("e_data_book_respond_get_book_view: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
}

/**
 * e_data_book_respond_get_contact:
 * @book: an #EDataBook
 * @opid: the operation ID that generated the response
 * @status: the outcome of the operation
 * @vcard: the found VCard, or %NULL
 *
 * Respond to a 'get contact' request specified by @opid on @book,
 * indicating @status as the outcome.
 **/
void
e_data_book_respond_get_contact (EDataBook                              *book,
				 guint32                                 opid,
				 GNOME_Evolution_Addressbook_CallStatus  status,
				 char                                   *vcard)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookListener_notifyContactRequested (book->priv->listener,
									 opid,
									 status,
									 vcard,
									 &ev);

	if (ev._major != CORBA_NO_EXCEPTION)
		g_message ("could not notify listener of get-contact response");

	CORBA_exception_free (&ev);
}

/**
 * e_data_book_respond_get_contact_list:
 * @book: an #EDataBook
 * @opid: the operation ID that generated the response
 * @status: the outcome of the operation
 * @card_list: a list of VCard strings
 *
 * Respond to a 'get contact list' request specified by @opid on @book,
 * indicating @status as the outcome.
 **/
void
e_data_book_respond_get_contact_list (EDataBook                              *book,
				      guint32                                 opid,
				      GNOME_Evolution_Addressbook_CallStatus  status,
				      GList                                  *card_list)
{
	CORBA_Environment ev;
	GNOME_Evolution_Addressbook_stringlist stringlist;
	int num_cards;
	int i;
	GList *l;

	CORBA_exception_init (&ev);

	num_cards = g_list_length (card_list);

	stringlist._buffer = CORBA_sequence_CORBA_string_allocbuf (num_cards);
	stringlist._maximum = num_cards;
	stringlist._length = num_cards;

	for (i = 0, l = card_list; l; l = l->next, i ++)
		stringlist._buffer[i] = CORBA_string_dup (l->data);

	g_list_foreach (card_list, (GFunc)g_free, NULL);
	g_list_free (card_list);


	GNOME_Evolution_Addressbook_BookListener_notifyContactListRequested (book->priv->listener,
									     opid,
									     status,
									     &stringlist,
									     &ev);

	if (ev._major != CORBA_NO_EXCEPTION)
		g_message ("could not notify listener of get-contact-list response");

	CORBA_exception_free (&ev);

	CORBA_free(stringlist._buffer);
}

/**
 * e_data_book_respond_get_changes:
 * @book: an #EDataBook
 * @opid: the operation ID that generated the response
 * @status: the outcome of the operation
 * @changes: a list of GNOME_Evolution_Addressbook_BookChangeItem items
 *
 * Respond to a 'get changes' request specified by @opid on @book,
 * indicating @status as the outcome.
 **/
void
e_data_book_respond_get_changes (EDataBook                              *book,
				 guint32                                 opid,
				 GNOME_Evolution_Addressbook_CallStatus  status,
				 GList                                  *changes)
{
	CORBA_Environment ev;
	GNOME_Evolution_Addressbook_BookChangeList changelist;
	int num_changes;
	int i;
	GList *l;

	CORBA_exception_init (&ev);

	num_changes = g_list_length (changes);

	changelist._buffer = CORBA_sequence_GNOME_Evolution_Addressbook_BookChangeItem_allocbuf (num_changes);
	changelist._maximum = num_changes;
	changelist._length = num_changes;

	for (i = 0, l = changes; l; l = l->next, i ++) {
		GNOME_Evolution_Addressbook_BookChangeItem *change = (GNOME_Evolution_Addressbook_BookChangeItem*)l->data;
		changelist._buffer[i] = *change;
		changelist._buffer[i].vcard = CORBA_string_dup (change->vcard);
	}

	g_list_foreach (changes, (GFunc)CORBA_free, NULL);
	g_list_free (changes);

	GNOME_Evolution_Addressbook_BookListener_notifyChangesRequested (book->priv->listener,
									 opid,
									 status,
									 &changelist,
									 &ev);

	if (ev._major != CORBA_NO_EXCEPTION)
		g_message ("could not notify listener of get-changes response");

	CORBA_exception_free (&ev);

	CORBA_free(changelist._buffer);
}

/**
 * e_data_book_report_writable:
 * @book: an #EDataBook
 * @writable: %TRUE if @book is writeable, %FALSE otherwise
 *
 * Notify listeners that @book's writeable status has changed
 * to @writable.
 **/
void
e_data_book_report_writable (EDataBook                           *book,
			     gboolean                           writable)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookListener_notifyWritable (
		book->priv->listener, (CORBA_boolean) writable, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("e_data_book_report_writable: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
}

/**
 * e_data_book_report_connection_status:
 * @book: an #EDataBook
 * @is_online: %TRUE if the book is connected, %FALSE otherwise
 *
 * Notify listeners that @book's online status has changed
 * to @is_online.
 **/
void
e_data_book_report_connection_status (EDataBook   *book,
				      gboolean    is_online)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookListener_notifyConnectionStatus (
		book->priv->listener, (CORBA_boolean) is_online, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("e_data_book_report_connection_status: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);

}

/**
 * e_data_book_report_connection_status:
 * @book: an #EDataBook
 *
 * Notify listeners that @book requires authentication.
 **/
void
e_data_book_report_auth_required (EDataBook *book)
{

	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookListener_notifyAuthRequired (
			 book->priv->listener,  &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("e_data_book_report_auth_required: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);

}

static void
e_data_book_construct (EDataBook                *book,
		       EBookBackend             *backend,
		       ESource *source,
		       GNOME_Evolution_Addressbook_BookListener listener)
{
	EDataBookPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (book != NULL);
	g_return_if_fail (source != NULL);

	priv = book->priv;

	CORBA_exception_init (&ev);
	book->priv->listener = CORBA_Object_duplicate (listener, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("e_data_book_construct(): could not duplicate the listener");
		CORBA_exception_free (&ev);
		return;
	}

	CORBA_exception_free (&ev);

	g_object_ref (source);

	priv->backend   = g_object_ref(backend);
	priv->source    = source;
}

/**
 * e_data_book_new:
 * @backend: an #EBookBackend
 * @source: an #ESource
 * @listener: a #GNOME_Evolution_Addressbook_BookListener CORBA object
 *
 * Create a new #EDataBook using @backend for storage, @source as the
 * storage location and @listener for reporting status.
 *
 * Return value: A new #EDataBook.
 **/
EDataBook *
e_data_book_new (EBookBackend                               *backend,
		 ESource *source,
		 GNOME_Evolution_Addressbook_BookListener  listener)
{
	static GStaticMutex mutex = G_STATIC_MUTEX_INIT;
	static PortableServer_POA poa = NULL;
	EDataBook *book;

	g_static_mutex_lock (&mutex);
	if (poa == NULL)
		poa = bonobo_poa_get_threaded (ORBIT_THREAD_HINT_PER_REQUEST, NULL);
	g_static_mutex_unlock (&mutex);

	book = g_object_new (E_TYPE_DATA_BOOK, "poa", poa, NULL);

	e_data_book_construct (book, backend, source, listener);

	return book;
}

static void
e_data_book_dispose (GObject *object)
{
	EDataBook *book = E_DATA_BOOK (object);

	if (book->priv) {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);
		CORBA_Object_release (book->priv->listener, &ev);

		if (ev._major != CORBA_NO_EXCEPTION)
			g_message ("e_data_book_construct(): could not release the listener");

		CORBA_exception_free (&ev);

		g_object_unref (book->priv->source);
		g_object_unref (book->priv->backend);
		g_free (book->priv);
		book->priv = NULL;
	}

	if (G_OBJECT_CLASS (e_data_book_parent_class)->dispose)
		G_OBJECT_CLASS (e_data_book_parent_class)->dispose (object);
}

static void
e_data_book_class_init (EDataBookClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	POA_GNOME_Evolution_Addressbook_Book__epv *epv;

	e_data_book_parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_data_book_dispose;

	epv = &klass->epv;

	epv->open                    = impl_GNOME_Evolution_Addressbook_Book_open;
	epv->remove                  = impl_GNOME_Evolution_Addressbook_Book_remove;
	epv->getContact              = impl_GNOME_Evolution_Addressbook_Book_getContact;
	epv->getContactList          = impl_GNOME_Evolution_Addressbook_Book_getContactList;
	epv->authenticateUser        = impl_GNOME_Evolution_Addressbook_Book_authenticateUser;
	epv->addContact              = impl_GNOME_Evolution_Addressbook_Book_addContact;
	epv->removeContacts          = impl_GNOME_Evolution_Addressbook_Book_removeContacts;
	epv->modifyContact           = impl_GNOME_Evolution_Addressbook_Book_modifyContact;
	epv->getStaticCapabilities   = impl_GNOME_Evolution_Addressbook_Book_getStaticCapabilities;
	epv->getSupportedFields      = impl_GNOME_Evolution_Addressbook_Book_getSupportedFields;
	epv->getRequiredFields       = impl_GNOME_Evolution_Addressbook_Book_getRequiredFields;
	epv->getSupportedAuthMethods = impl_GNOME_Evolution_Addressbook_Book_getSupportedAuthMethods;
	epv->getBookView             = impl_GNOME_Evolution_Addressbook_Book_getBookView;
	epv->getChanges              = impl_GNOME_Evolution_Addressbook_Book_getChanges;
	epv->cancelOperation         = impl_GNOME_Evolution_Addressbook_Book_cancelOperation;

}

static void
e_data_book_init (EDataBook *book)
{
	book->priv                = g_new0 (EDataBookPrivate, 1);
}

BONOBO_TYPE_FUNC_FULL (
		       EDataBook,
		       GNOME_Evolution_Addressbook_Book,
		       BONOBO_TYPE_OBJECT,
		       e_data_book);

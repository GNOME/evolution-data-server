/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <config.h>

#include <pthread.h>

#include <string.h>

#include "e-book.h"
#include "e-vcard.h"

#include <bonobo-activation/bonobo-activation.h>

#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-main.h>
#include <gconf/gconf-client.h>

#include <libgnome/gnome-i18n.h>
#include <libedataserver/e-component-listener.h>

#include "e-book-marshal.h"
#include "e-book-listener.h"

GMainContext *_ebook_context;

static GObjectClass *parent_class;

#define e_return_error_if_fail(expr,error_code)	G_STMT_START{		\
     if G_LIKELY(expr) { } else						\
       {								\
	 g_log (G_LOG_DOMAIN,						\
		G_LOG_LEVEL_CRITICAL,					\
		"file %s: line %d (%s): assertion `%s' failed",		\
		__FILE__,						\
		__LINE__,						\
		__PRETTY_FUNCTION__,					\
		#expr);							\
	 g_set_error (error, E_BOOK_ERROR, (error_code),                \
		"file %s: line %d (%s): assertion `%s' failed",		\
		__FILE__,						\
		__LINE__,						\
		__PRETTY_FUNCTION__,					\
		#expr);							\
	 return FALSE;							\
       };				}G_STMT_END

/* XXX we need a better error message here */
#define E_BOOK_CHECK_STATUS(status,error) G_STMT_START{			\
	if ((status) == E_BOOK_ERROR_OK) {				\
		return TRUE;						\
	}								\
	else {								\
		g_set_error ((error), E_BOOK_ERROR, (status), "EBookStatus returned %d", (status));	\
		return FALSE;						\
	}				}G_STMT_END

enum {
	OPEN_PROGRESS,
	WRITABLE_STATUS,
	BACKEND_DIED,
	LAST_SIGNAL
};

static guint e_book_signals [LAST_SIGNAL];

typedef struct {
	GMutex *mutex;
	GCond *cond;
	EBookStatus status;
	char *id;
	GList *list;
	EContact *contact;

	EBookView *view;
	EBookViewListener *listener;
} EBookOp;

typedef enum {
	E_BOOK_URI_NOT_LOADED,
	E_BOOK_URI_LOADING,
	E_BOOK_URI_LOADED
} EBookLoadState;

struct _EBookPrivate {
	GList *book_factories;
	GList *iter;

	/* cached capabilites */
	char *cap;
	gboolean cap_queried;

	/* cached writable status */
	gboolean writable;

	EBookListener         *listener;
	EComponentListener    *comp_listener;

	GNOME_Evolution_Addressbook_Book         corba_book;

	EBookLoadState         load_state;


	EBookOp *current_op;

	GMutex *mutex;

	/* Need to keep URI around, since the getter returns const */
	gchar *uri;
	ESource *source;

	gulong listener_signal;
	gulong died_signal;
};


/* Error quark */
GQuark
e_book_error_quark (void)
{
  static GQuark q = 0;
  if (q == 0)
    q = g_quark_from_static_string ("e-book-error-quark");

  return q;
}



/* EBookOp calls */

static EBookOp*
e_book_new_op (EBook *book)
{
	EBookOp *op = g_new0 (EBookOp, 1);

	op->mutex = g_mutex_new ();
	op->cond = g_cond_new ();

	book->priv->current_op = op;

	return op;
}

static EBookOp*
e_book_get_op (EBook *book)
{
	if (!book->priv->current_op) {
		g_warning ("unexpected response");
		return NULL;
	}
		
	return book->priv->current_op;
}

static void
e_book_op_free (EBookOp *op)
{
	g_cond_free (op->cond);
	g_mutex_free (op->mutex);
	g_free (op);
}

static void
e_book_op_remove (EBook *book,
		  EBookOp *op)
{
	if (book->priv->current_op != op)
		g_warning ("cannot remove op, it's not current");

	book->priv->current_op = NULL;
}

static void
e_book_clear_op (EBook *book,
		 EBookOp *op)
{
	e_book_op_remove (book, op);
	g_mutex_unlock (op->mutex);
	e_book_op_free (op);
}



/**
 * e_book_add_contact:
 * @book: an #EBook
 * @contact: an #EContact
 * @error: a #GError to set on failure.
 *
 * adds @contact to @book.
 *
 * Return value: #TRUE if successful, #FALSE otherwise.
 **/
gboolean
e_book_add_contact (EBook           *book,
		    EContact        *contact,
		    GError         **error)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;
	char *vcard_str;

	printf ("e_book_add_contact\n");

	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (contact && E_IS_CONTACT (contact), E_BOOK_ERROR_INVALID_ARG);

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_URI_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_URI_NOT_LOADED,
			     /* translators: the placeholders will be filled by
			      * function names, e.g.
			      * "e_book_add_contact" on book before 
			      * "e_book_load_uri */
			     _("\"%s\" on book before \"%s\""),
			     "e_book_add_contact", "e_book_load_uri");
		return FALSE;
	}

	if (book->priv->current_op != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	vcard_str = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	our_op = e_book_new_op (book);

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	/* will eventually end up calling e_book_response_add_contact */
	GNOME_Evolution_Addressbook_Book_addContact (book->priv->corba_book,
						     (const GNOME_Evolution_Addressbook_VCard) vcard_str, &ev);

	g_free (vcard_str);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_book_clear_op (book, our_op);

		CORBA_exception_free (&ev);

		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
			     _("CORBA exception making \"%s\" call"),
			     "Book::addContact");
		return FALSE;
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	e_contact_set (contact, E_CONTACT_UID, our_op->id);
	g_free (our_op->id);

	e_book_clear_op (book, our_op);

	E_BOOK_CHECK_STATUS (status, error);
}

static void
e_book_response_add_contact (EBook       *book,
			     EBookStatus  status,
			     char        *id)
{
	EBookOp *op;

	printf ("e_book_response_add_contact\n");

	op = e_book_get_op (book);

	if (op == NULL) {
	  g_warning ("e_book_response_add_contact: Cannot find operation ");
	  return;
	}

	g_mutex_lock (op->mutex);

	op->status = status;
	op->id = g_strdup (id);

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
}



/**
 * e_book_commit_contact:
 * @book: an #EBook
 * @contact: an #EContact
 * @error: a #GError to set on failure
 *
 * applies the changes made to @contact to the stored version in
 * @book.
 *
 * Return value: #TRUE if successful, #FALSE otherwise
 **/
gboolean
e_book_commit_contact (EBook           *book,
		       EContact        *contact,
		       GError         **error)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;
	char *vcard_str;

	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (contact && E_IS_CONTACT (contact), E_BOOK_ERROR_INVALID_ARG);

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_URI_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_URI_NOT_LOADED,
			     _("\"%s\" on book before \"%s\""),
			     "e_book_commit_contact", "e_book_load_uri");
		return FALSE;
	}

	if (book->priv->current_op != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	vcard_str = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	our_op = e_book_new_op (book);

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	/* will eventually end up calling _e_book_response_generic */
	GNOME_Evolution_Addressbook_Book_modifyContact (book->priv->corba_book,
							(const GNOME_Evolution_Addressbook_VCard) vcard_str, &ev);

	g_free (vcard_str);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_book_clear_op (book, our_op);

		CORBA_exception_free (&ev);

		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
			     _("CORBA exception making \"%s\" call"),
			     "Book::modifyContact");
		return FALSE;
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	e_contact_set (contact, E_CONTACT_UID, our_op->id);
	g_free (our_op->id);

	/* remove the op from the book's hash of operations */
	e_book_clear_op (book, our_op);

	E_BOOK_CHECK_STATUS (status, error);
}


/**
 * e_book_get_supported_fields:
 * @book: an #EBook
 * @fields: a #GList
 * @error: a #GError to set on failure
 *
 * Queries @book for the list of fields it supports. Mostly for use
 * by the contact editor so it knows what fields to sensitize.
 *
 * Return value: #TRUE if successful, #FALSE otherwise
 **/
gboolean
e_book_get_supported_fields  (EBook            *book,
			      GList           **fields,
			      GError          **error)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;

	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (fields,                   E_BOOK_ERROR_INVALID_ARG);

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_URI_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_URI_NOT_LOADED,
			     _("\"%s\" on book before \"%s\""),
			     "e_book_get_supported_fields", "e_book_load_uri");
		return FALSE;
	}

	if (book->priv->current_op != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
	}

	our_op = e_book_new_op (book);

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	/* will eventually end up calling
	   _e_book_response_get_supported_fields */
	GNOME_Evolution_Addressbook_Book_getSupportedFields(book->priv->corba_book, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_book_clear_op (book, our_op);

		CORBA_exception_free (&ev);

		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
			     _("CORBA exception making \"%s\" call"),
			     "Book::getSupportedFields");
		return FALSE;
	}


	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*fields = our_op->list;

	e_book_clear_op (book, our_op);

	E_BOOK_CHECK_STATUS (status, error);
}

static void
e_book_response_get_supported_fields (EBook       *book,
				      EBookStatus  status,
				      GList       *fields)
{
	EBookOp *op;

	op = e_book_get_op (book);

	if (op == NULL) {
	  g_warning ("e_book_response_get_supported_fields: Cannot find operation ");
	  return;
	}

	g_mutex_lock (op->mutex);

	op->status = status;
	op->list = fields;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
}


/**
 * e_book_get_supported_auth_methods:
 * @book: an #EBook
 * @auth_methods: a #GList
 * @error: a #GError to set on failure
 *
 * Queries @book for the list of authentication methods it supports.
 *
 * Return value: #TRUE if successful, #FALSE otherwise
 **/
gboolean
e_book_get_supported_auth_methods (EBook            *book,
				   GList           **auth_methods,
				   GError          **error)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;

	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (auth_methods,             E_BOOK_ERROR_INVALID_ARG);

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_URI_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_URI_NOT_LOADED,
			     _("\"%s\" on book before \"%s\""),
			     "e_book_get_supported_auth_methods",
			     "e_book_load_uri");
		return FALSE;
	}

	if (book->priv->current_op != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book);

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	/* will eventually end up calling
	   e_book_response_get_supported_fields */
	GNOME_Evolution_Addressbook_Book_getSupportedAuthMethods(book->priv->corba_book, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_book_clear_op (book, our_op);

		CORBA_exception_free (&ev);

		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
			     _("CORBA exception making \"%s\" call"),
			     "Book::getSupportedAuthMethods");
		return FALSE;
	}


	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*auth_methods = our_op->list;

	e_book_clear_op (book, our_op);

	E_BOOK_CHECK_STATUS (status, error);
}

static void
e_book_response_get_supported_auth_methods (EBook                 *book,
					    EBookStatus            status,
					    GList                 *auth_methods)
{
	EBookOp *op;

	op = e_book_get_op (book);

	if (op == NULL) {
	  g_warning ("e_book_response_get_supported_auth_methods: Cannot find operation ");
	  return;
	}

	g_mutex_lock (op->mutex);

	op->status = status;
	op->list = auth_methods;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
}



/**
 * e_book_authenticate_user:
 * @book: an #EBook
 * @user: a string
 * @passwd: a string
 * @auth_method: a string
 * @error: a #GError to set on failure
 *
 * Authenticates @user with @passwd, using the auth method
 * @auth_method.  @auth_method must be one of the authentication
 * methods returned using e_book_get_supported_auth_methods.
 *
 * Return value: #TRUE if successful, #FALSE otherwise
 **/
gboolean
e_book_authenticate_user (EBook         *book,
			  const char    *user,
			  const char    *passwd,
			  const char    *auth_method,
			  GError       **error)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;

	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (user,                     E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (passwd,                   E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (auth_method,              E_BOOK_ERROR_INVALID_ARG);

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_URI_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_URI_NOT_LOADED,
			     _("\"%s\" on book before \"%s\""),
			     "e_book_authenticate_user", "e_book_load_uri");
		return FALSE;
	}

	if (book->priv->current_op != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book);

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	/* will eventually end up calling
	   e_book_response_generic */
	GNOME_Evolution_Addressbook_Book_authenticateUser (book->priv->corba_book,
							   user, passwd,
							   auth_method,
							   &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_book_clear_op (book, our_op);

		CORBA_exception_free (&ev);

		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
			     _("CORBA exception making \"%s\" call"),
			     "Book::authenticateUser");
		return FALSE;
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;

	e_book_clear_op (book, our_op);

	E_BOOK_CHECK_STATUS (status, error);
}


/**
 * e_book_get_contact:
 * @book: an #EBook
 * @id: a string
 * @contact: an #EContact
 * @error: a #GError to set on failure
 *
 * Fills in @contact with the contents of the vcard in @book
 * corresponding to @id.
 *
 * Return value: #TRUE if successful, #FALSE otherwise
 **/
gboolean
e_book_get_contact (EBook       *book,
		    const char  *id,
		    EContact   **contact,
		    GError     **error)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;

	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (id,                       E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (contact,                     E_BOOK_ERROR_INVALID_ARG);

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_URI_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_URI_NOT_LOADED,
			     _("\"%s\" on book before \"%s\""),
			     "e_book_get_contact", "e_book_load_uri");
		return FALSE;
	}

	if (book->priv->current_op != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book);

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	/* will eventually end up calling e_book_response_generic */
	GNOME_Evolution_Addressbook_Book_getContact (book->priv->corba_book,
						     (const GNOME_Evolution_Addressbook_VCard) id, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_book_clear_op (book, our_op);

		CORBA_exception_free (&ev);

		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
			     _("CORBA exception making \"%s\" call"),
			     "Book::getContact");
		return FALSE;
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*contact = our_op->contact;

	e_book_clear_op (book, our_op);

	E_BOOK_CHECK_STATUS (status, error);
}

static void
e_book_response_get_contact (EBook       *book,
			     EBookStatus  status,
			     EContact    *contact)
{
	EBookOp *op;

	printf ("e_book_response_get_contact\n");

	op = e_book_get_op (book);

	if (op == NULL) {
	  g_warning ("e_book_response_get_contact: Cannot find operation ");
	  return;
	}

	g_mutex_lock (op->mutex);

	op->status = status;
	op->contact = contact;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
}


/**
 * e_book_remove_contact:
 * @book: an #EBook
 * @id: a string
 * @error: a #GError to set on failure
 *
 * Removes the contact with id @id from @book.
 *
 * Return value: #TRUE if successful, #FALSE otherwise
 **/
gboolean
e_book_remove_contact (EBook       *book,
		       const char  *id,
		       GError     **error)
{
	GList *list;
	gboolean rv;

	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (id,                       E_BOOK_ERROR_INVALID_ARG);

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_URI_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_URI_NOT_LOADED,
			     _("\"%s\" on book before \"%s\""),
			     "e_book_remove_contact", "e_book_load_uri");
		return FALSE;
	}

	if (book->priv->current_op != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	g_mutex_lock (book->priv->mutex);

	list = g_list_append (NULL, (char*)id);

	rv = e_book_remove_contacts (book, list, error);

	return rv;
}

/**
 * e_book_remove_contacts:
 * @book: an #EBook
 * @ids: an #GList of const char *id's
 * @error: a #GError to set on failure
 *
 * Removes the contacts with ids from the list @ids from @book.  This is
 * always more efficient than calling e_book_remove_contact_by_id if you
 * have more than one id to remove, as some backends can implement it
 * as a batch request.
 *
 * Return value: #TRUE if successful, #FALSE otherwise
 **/
gboolean
e_book_remove_contacts (EBook    *book,
			GList    *ids,
			GError  **error)
{
	GNOME_Evolution_Addressbook_ContactIdList idlist;
	CORBA_Environment ev;
	GList *iter;
	int num_ids, i;
	EBookOp *our_op;
	EBookStatus status;

	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (ids,                      E_BOOK_ERROR_INVALID_ARG);

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_URI_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_URI_NOT_LOADED,
			     _("\"%s\" on book before \"%s\""),
			     "e_book_remove_contacts", "e_book_load_uri");
		return FALSE;
	}

	if (book->priv->current_op != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book);

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	num_ids = g_list_length (ids);
	idlist._buffer = CORBA_sequence_GNOME_Evolution_Addressbook_ContactId_allocbuf (num_ids);
	idlist._maximum = num_ids;
	idlist._length = num_ids;

	for (iter = ids, i = 0; iter; iter = iter->next)
		idlist._buffer[i++] = CORBA_string_dup (iter->data);

	/* will eventually end up calling e_book_response_generic */
	GNOME_Evolution_Addressbook_Book_removeContacts (book->priv->corba_book, &idlist, &ev);

	CORBA_free(idlist._buffer);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_book_clear_op (book, our_op);

		CORBA_exception_free (&ev);

		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
			     _("CORBA exception making \"%s\" call"),
			     "Book::removeContacts");
		return FALSE;
	}
	
	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;

	e_book_clear_op (book, our_op);

	E_BOOK_CHECK_STATUS (status, error);
}


/**
 * e_book_get_book_view:
 * @book: an #EBook
 * @query: an #EBookQuery
 * @requested_fields: a #GList containing the names of fields to return, or NULL for all
 * @max_results: the maximum number of contacts to show (or 0 for all)
 * @book_view: A #EBookView pointer, will be set to the view
 * @error: a #GError to set on failure
 *
 * Query @book with @query, creating a #EBookView in @book_view with the fields
 * specified by @requested_fields and limited at @max_results records. On an
 * error, @error is set and #FALSE returned.
 *
 * Return value: #TRUE if successful, #FALSE otherwise
 **/
gboolean
e_book_get_book_view (EBook       *book,
		      EBookQuery  *query,
		      GList       *requested_fields,
		      int          max_results,
		      EBookView  **book_view,
		      GError     **error)
{
	GNOME_Evolution_Addressbook_stringlist stringlist;
	CORBA_Environment ev;
	EBookOp *our_op;
	EBookStatus status;
	int num_fields, i;
	GList *iter;
	char *query_string;

	e_return_error_if_fail (book && E_IS_BOOK (book),       E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (query,                          E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (book_view,                      E_BOOK_ERROR_INVALID_ARG);

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_URI_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_URI_NOT_LOADED,
			     _("\"%s\" on book before \"%s\""),
			     "e_book_get_book_view", "e_book_load_uri");
		return FALSE;
	}

	if (book->priv->current_op != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book);

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	our_op->listener = e_book_view_listener_new();

	num_fields = g_list_length (requested_fields);

	stringlist._buffer = CORBA_sequence_CORBA_string_allocbuf (num_fields);
	stringlist._maximum = num_fields;
	stringlist._length = num_fields;

	for (i = 0, iter = requested_fields; iter; iter = iter->next, i ++) {
		stringlist._buffer[i] = CORBA_string_dup ((char*)iter->data);
	}

	query_string = e_book_query_to_string (query);

	/* will eventually end up calling e_book_response_get_book_view */
	GNOME_Evolution_Addressbook_Book_getBookView (book->priv->corba_book,
						      bonobo_object_corba_objref(BONOBO_OBJECT(our_op->listener)),
						      query_string,
						      &stringlist, max_results, &ev);

	CORBA_free(stringlist._buffer);
	g_free (query_string);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_book_clear_op (book, our_op);

		CORBA_exception_free (&ev);

		g_warning ("corba exception._major = %d\n", ev._major);

		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
			     _("CORBA exception making \"%s\" call"),
			     "Book::authenticateUser");
		return FALSE;
	}
	
	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*book_view = our_op->view;

	e_book_clear_op (book, our_op);

	E_BOOK_CHECK_STATUS (status, error);
}

static void
e_book_response_get_book_view (EBook       *book,
			       EBookStatus  status,
			       GNOME_Evolution_Addressbook_BookView corba_book_view)
{

	EBookOp *op;

	printf ("e_book_response_get_book_view\n");

	op = e_book_get_op (book);

	if (op == NULL) {
	  g_warning ("e_book_response_get_book_view: Cannot find operation ");
	  return;
	}

	g_mutex_lock (op->mutex);

	op->status = status;
	op->view = e_book_view_new (corba_book_view, op->listener);

	bonobo_object_unref(BONOBO_OBJECT(op->listener));

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
}



/**
 * e_book_get_contacts:
 * @book: an #EBook
 * @query: an #EBookQuery
 * @contacts: a #GList pointer, will be set to the list of contacts
 * @error: a #GError to set on failure
 *
 * Query @book with @query, setting @contacts to the list of contacts which
 * matched. On failed, @error will be set and #FALSE returned.
 *
 * Return value: #TRUE on success, #FALSE otherwise
 **/
gboolean
e_book_get_contacts (EBook       *book,
		     EBookQuery  *query,
		     GList      **contacts,
		     GError     **error)
{
	CORBA_Environment ev;
	EBookOp *our_op;
	EBookStatus status;
	char *query_string;

	e_return_error_if_fail (book && E_IS_BOOK (book),       E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (query,                          E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (contacts,                       E_BOOK_ERROR_INVALID_ARG);

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_URI_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_URI_NOT_LOADED,
			     _("\"%s\" on book before \"%s\""),
			     "e_book_get_contacts", "e_book_load_uri");
		return FALSE;
	}

	if (book->priv->current_op != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book);

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	query_string = e_book_query_to_string (query);

	/* will eventually end up calling e_book_response_get_contacts */
	GNOME_Evolution_Addressbook_Book_getContactList (book->priv->corba_book, query_string, &ev);

	g_free (query_string);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_book_clear_op (book, our_op);

		CORBA_exception_free (&ev);

		g_warning ("corba exception._major = %d\n", ev._major);

		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
			     _("CORBA exception making \"%s\" call"),
			     "Book::getContactList");
		return FALSE;
	}
	
	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*contacts = our_op->list;

	e_book_clear_op (book, our_op);

	E_BOOK_CHECK_STATUS (status, error);
}

static void
e_book_response_get_contacts (EBook       *book,
			      EBookStatus  status,
			      GList       *contact_list)
{

	EBookOp *op;

	op = e_book_get_op (book);

	if (op == NULL) {
	  g_warning ("e_book_response_get_contacts: Cannot find operation ");
	  return;
	}

	g_mutex_lock (op->mutex);

	op->status = status;
	op->list = contact_list;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
}


gboolean
e_book_get_changes (EBook       *book,
		    char        *changeid,
		    GList      **changes,
		    GError     **error)
{
	CORBA_Environment ev;
	EBookOp *our_op;
	EBookStatus status;

	e_return_error_if_fail (book && E_IS_BOOK (book),       E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (changeid,                       E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (changes,                        E_BOOK_ERROR_INVALID_ARG);

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_URI_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_URI_NOT_LOADED,
			     _("\"%s\" on book before \"%s\""),
			     "e_book_get_changes", "e_book_load_uri");
		return FALSE;
	}

	if (book->priv->current_op != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book);

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	/* will eventually end up calling e_book_response_get_changes */
	GNOME_Evolution_Addressbook_Book_getChanges (book->priv->corba_book, changeid, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_book_clear_op (book, our_op);

		CORBA_exception_free (&ev);

		g_warning ("corba exception._major = %d\n", ev._major);

		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
			     _("CORBA exception making \"%s\" call"),
			     "Book::getChanges");
		return FALSE;
	}
	
	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;
	*changes = our_op->list;

	e_book_clear_op (book, our_op);

	E_BOOK_CHECK_STATUS (status, error);
}

static void
e_book_response_get_changes (EBook       *book,
			     EBookStatus  status,
			     GList       *change_list)
{

	EBookOp *op;

	op = e_book_get_op (book);

	if (op == NULL) {
	  g_warning ("e_book_response_get_contacts: Cannot find operation ");
	  return;
	}

	g_mutex_lock (op->mutex);

	op->status = status;
	op->list = change_list;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
}

void
e_book_free_change_list (GList *change_list)
{
	GList *l;
	for (l = change_list; l; l = l->next) {
		EBookChange *change = l->data;

		g_object_unref (change->contact);
		g_free (change);
	}

	g_list_free (change_list);
}



static void
e_book_response_generic (EBook       *book,
			 EBookStatus  status)
{
	EBookOp *op;

	op = e_book_get_op (book);

	if (op == NULL) {
	  g_warning ("e_book_response_generic: Cannot find operation ");
	  return;
	}

	g_mutex_lock (op->mutex);

	op->status = status;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
}

/**
 * e_book_cancel:
 * @book: an #EBook
 * @error: a #GError to set on failure
 *
 * Used to cancel an already running operation on @book.  This
 * function makes a synchronous CORBA to the backend telling it to
 * cancel the operation.  If the operation wasn't cancellable (either
 * transiently or permanently) or had already comopleted on the server
 * side, this function will return E_BOOK_STATUS_COULD_NOT_CANCEL, and
 * the operation will continue uncancelled.  If the operation could be
 * cancelled, this function will return E_BOOK_ERROR_OK, and the
 * blocked e_book function corresponding to current operation will
 * return with a status of E_BOOK_STATUS_CANCELLED.
 *
 * Return value: #TRUE on success, #FALSE otherwise
 **/
gboolean
e_book_cancel (EBook   *book,
	       GError **error)
{
	EBookOp *op;
	EBookStatus status;
	gboolean rv;
	CORBA_Environment ev;

	e_return_error_if_fail (book && E_IS_BOOK (book),       E_BOOK_ERROR_INVALID_ARG);

	g_mutex_lock (book->priv->mutex);

	if (book->priv->current_op == NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_COULD_NOT_CANCEL,
			     _("e_book_cancel: there is no current operation"));
		return FALSE;
	}

	op = book->priv->current_op;

	g_mutex_lock (op->mutex);

	g_mutex_unlock (book->priv->mutex);

	status = GNOME_Evolution_Addressbook_Book_cancelOperation(book->priv->corba_book, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		g_mutex_unlock (op->mutex);

		CORBA_exception_free (&ev);

		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
			     _("CORBA exception making \"%s\" call"),
			     "Book::cancelOperation");
		return FALSE;
	}

	CORBA_exception_free (&ev);

	if (status == E_BOOK_ERROR_OK) {
		op->status = E_BOOK_ERROR_CANCELLED;

		g_cond_signal (op->cond);

		rv = TRUE;
	}
	else {
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_COULD_NOT_CANCEL,
			     _("e_book_cancel: couldn't cancel"));
		rv = FALSE;
	}

	g_mutex_unlock (op->mutex);

	return rv;
}

static void
e_book_response_open (EBook       *book,
		      EBookStatus  status)
{
	EBookOp *op;

	op = e_book_get_op (book);

	if (op == NULL) {
	  g_warning ("e_book_response_open: Cannot find operation ");
	  return;
	}

	g_mutex_lock (op->mutex);

	op->status = status;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
}



/**
 * e_book_remove:
 * @book: an #EBook
 * @error: a #GError to set on failure
 *
 * Remove the backing data for this #EBook. For example, with the file backend this
 * deletes the database file. You cannot get it back!
 *
 * Return value: #TRUE on success, #FALSE on failure.
 */
gboolean
e_book_remove (EBook   *book,
	       GError **error)
{
	CORBA_Environment ev;
	EBookOp *our_op;
	EBookStatus status;

	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG);

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_URI_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_URI_NOT_LOADED,
			     _("\"%s\" on book before \"%s\""),
			     "e_book_remove", "e_book_load_uri");
		return FALSE;
	}

	if (book->priv->current_op != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book);

	g_mutex_lock (our_op->mutex);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	/* will eventually end up calling e_book_response_remove */
	GNOME_Evolution_Addressbook_Book_remove (book->priv->corba_book, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_book_clear_op (book, our_op);

		CORBA_exception_free (&ev);

		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
			     _("CORBA exception making \"%s\" call"),
			     "Book::remove");
		return FALSE;
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	g_cond_wait (our_op->cond, our_op->mutex);

	status = our_op->status;

	e_book_clear_op (book, our_op);

	E_BOOK_CHECK_STATUS (status, error);
}

static void
e_book_response_remove (EBook       *book,
			EBookStatus  status)
{
	EBookOp *op;

	printf ("e_book_response_remove\n");

	op = e_book_get_op (book);

	if (op == NULL) {
	  g_warning ("e_book_response_remove: Cannot find operation ");
	  return;
	}

	g_mutex_lock (op->mutex);

	op->status = status;

	g_cond_signal (op->cond);

	g_mutex_unlock (op->mutex);
}

typedef struct
{
	EBook *book;
	gboolean writable;
}  EBookWritableData;

static gboolean
e_book_idle_writable (gpointer data)
{
	EBookWritableData *write_data = data;
	
	g_signal_emit (G_OBJECT (write_data->book), e_book_signals [WRITABLE_STATUS], 0, write_data->writable);

	g_object_unref (write_data->book);
	g_free (write_data);
	
	return FALSE;
}



static void
e_book_handle_response (EBookListener *listener, EBookListenerResponse *resp, EBook *book)
{
	switch (resp->op) {
	case CreateContactResponse:
		e_book_response_add_contact (book, resp->status, resp->id);
		break;
	case RemoveContactResponse:
	case ModifyContactResponse:
	case AuthenticationResponse:
		e_book_response_generic (book, resp->status);
		break;
	case GetContactResponse: {
		EContact *contact = e_contact_new_from_vcard (resp->vcard);
		e_book_response_get_contact (book, resp->status, contact);
		break;
	}
	case GetContactListResponse:
		e_book_response_get_contacts (book, resp->status, resp->list);
		break;
	case GetBookViewResponse:
		e_book_response_get_book_view(book, resp->status, resp->book_view);
		break;
	case GetChangesResponse:
		e_book_response_get_changes(book, resp->status, resp->list);
		break;
	case OpenBookResponse:
		e_book_response_open (book, resp->status);
		break;
	case RemoveBookResponse:
		e_book_response_remove (book, resp->status);
		break;
	case GetSupportedFieldsResponse:
		e_book_response_get_supported_fields (book, resp->status, resp->list);
		break;
	case GetSupportedAuthMethodsResponse:
		e_book_response_get_supported_auth_methods (book, resp->status, resp->list);
		break;
	case WritableStatusEvent: 
	{
		EBookWritableData *write_data;

		book->priv->writable = resp->writable;
	
		write_data = g_new0 (EBookWritableData, 1);

		write_data->book = g_object_ref (book);
		write_data->writable = book->priv->writable;
		
		g_idle_add (e_book_idle_writable, write_data);
	}
	
		break;
	default:
		g_error ("EBook: Unknown response code %d!\n",
			 resp->op);
	}
}



/**
 * e_book_unload_uri:
 * @book: an #EBook
 * @error: an #GError to set on failure
 *
 * Unload the URI that this book had previously loaded.
 *
 * Return value: #TRUE on success, #FALSE otherwise.
 */
static gboolean
e_book_unload_uri (EBook   *book,
		   GError **error)
{
	CORBA_Environment ev;

	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (book->priv->load_state != E_BOOK_URI_NOT_LOADED, E_BOOK_ERROR_URI_NOT_LOADED);

	if (book->priv->load_state == E_BOOK_URI_LOADED) {
		/* Release the remote GNOME_Evolution_Addressbook_Book in the PAS. */
		CORBA_exception_init (&ev);

		bonobo_object_release_unref  (book->priv->corba_book, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("e_book_unload_uri: Exception releasing "
				   "remote book interface!\n");
		}

		CORBA_exception_free (&ev);

		e_book_listener_stop (book->priv->listener);
		bonobo_object_unref (BONOBO_OBJECT (book->priv->listener));

		book->priv->listener   = NULL;
		book->priv->load_state = E_BOOK_URI_NOT_LOADED;
		g_free (book->priv->cap);
		book->priv->cap = NULL;
		book->priv->cap_queried = FALSE;
		book->priv->writable = FALSE;
	}
	else if (book->priv->load_state == E_BOOK_URI_LOADING) {
		e_book_cancel (book, error);
	}

	return TRUE;
}



/**
 * e_book_load_uri:
 */

static void
backend_died_cb (EComponentListener *cl, gpointer user_data)
{
	EBook *book = user_data;
                                                                                                                              
	book->priv->load_state = E_BOOK_URI_NOT_LOADED;
        g_signal_emit (book, e_book_signals [BACKEND_DIED], 0);
}

static GList *
activate_factories_for_uri (EBook *book, const char *uri)
{
	CORBA_Environment ev;
	Bonobo_ServerInfoList *info_list = NULL;
	int i;
	char *protocol, *query, *colon;
	GList *factories = NULL;

	colon = strchr (uri, ':');
	if (!colon) {
		g_warning ("e_book_load_uri: Unable to determine protocol in the URI\n");
		return FALSE;
	}

	protocol = g_strndup (uri, colon-uri);
	query = g_strdup_printf ("repo_ids.has ('IDL:GNOME/Evolution/DataServer/BookFactory:" BASE_VERSION "')"
				 " AND addressbook:supported_protocols.has ('%s')", protocol
				 );

	CORBA_exception_init (&ev);
	
	info_list = bonobo_activation_query (query, NULL, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		char *exc_text = bonobo_exception_get_text (&ev);
		g_warning ("Cannot perform bonobo-activation query for book factories: %s", exc_text);
		g_free (exc_text);
		CORBA_exception_free (&ev);
		goto done;
		return NULL;
	}

	if (info_list->_length == 0) {
		g_warning ("Can't find installed BookFactory that handles protocol '%s'.", protocol);
		CORBA_exception_free (&ev);
		goto done;
	}

	CORBA_exception_free (&ev);

	for (i = 0; i < info_list->_length; i ++) {
		const Bonobo_ServerInfo *info;
		GNOME_Evolution_Addressbook_BookFactory factory;

		info = info_list->_buffer + i;

		factory = bonobo_activation_activate_from_id (info->iid, 0, NULL, NULL);

		if (factory == CORBA_OBJECT_NIL)
			g_warning ("e_book_construct: Could not obtain a handle "
				   "to the Personal Addressbook Server with IID `%s'\n", info->iid);
		else
			factories = g_list_append (factories, factory);
	}

 done:
	if (info_list)
		CORBA_free (info_list);
	g_free (query);
	g_free (protocol);

	return factories;
}

/* XXX hm, should this function hold a lock on book->priv->mutex?  It
   doesn't seem to require it, but there are unlocked writes to
   book->priv->load_state, which other functions read.. */
static gboolean
fetch_corba_book (EBook       *book,
		  ESource     *source,
		  gboolean     only_if_exists,
		  GError     **error)
{
	GNOME_Evolution_Addressbook_Book corba_book = CORBA_OBJECT_NIL;
	gchar *uri;
	gchar *source_xml;
	GList *factories;
	GList *l;
	EBookStatus status = E_BOOK_ERROR_OTHER_ERROR;
	gboolean rv = FALSE;

	uri = e_source_get_uri (source);
	if (!uri) {
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_OTHER_ERROR,
			     _("e_book_load_uri: Invalid source."));
		book->priv->load_state = E_BOOK_URI_NOT_LOADED;
		return FALSE;
	}

	/* try to find a list of factories that can handle the protocol */
	factories = activate_factories_for_uri (book, uri);
	if (!factories) {
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_PROTOCOL_NOT_SUPPORTED,
			     _("e_book_load_uri: no factories available for uri `%s'"), uri);
		book->priv->load_state = E_BOOK_URI_NOT_LOADED;
		return FALSE;
	}


	/*
	 * Create our local BookListener interface.
	 */
	book->priv->listener = e_book_listener_new ();
	if (book->priv->listener == NULL) {
		g_warning ("e_book_load_uri: Could not create EBookListener!\n");
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_OTHER_ERROR,
			     _("e_book_load_uri: Could not create EBookListener"));
		book->priv->load_state = E_BOOK_URI_NOT_LOADED;
		return FALSE;
	}
	book->priv->listener_signal = g_signal_connect (book->priv->listener, "response",
							G_CALLBACK (e_book_handle_response), book);

	g_free (book->priv->uri);
	book->priv->uri = uri;

	g_object_ref (source);
	if (book->priv->source)
		g_object_unref (book->priv->source);
	book->priv->source = source;

	source_xml = e_source_to_standalone_xml (source);

	for (l = factories; l; l = l->next) {
		GNOME_Evolution_Addressbook_BookFactory factory = l->data;
		EBookOp *our_op;
		CORBA_Environment ev;

		/* we don't bother locking the book's mutex here
		   before creating the op since there should be no way
		   another thread could get to a place where they
		   could add an op (as the load_state !=
		   E_BOOK_URI_LOADED) */
		our_op = e_book_new_op (book);

		g_mutex_lock (our_op->mutex);

		CORBA_exception_init (&ev);

		corba_book = GNOME_Evolution_Addressbook_BookFactory_getBook (factory, source_xml,
								      bonobo_object_corba_objref (BONOBO_OBJECT (book->priv->listener)),
								      &ev);

		if (ev._major != CORBA_NO_EXCEPTION) {
			e_book_clear_op (book, our_op);

			CORBA_exception_free (&ev);
			continue;
		}

		GNOME_Evolution_Addressbook_Book_open (corba_book,
						       only_if_exists,
						       &ev);

		if (ev._major != CORBA_NO_EXCEPTION) {
			/* kill the listener so the book will die */
			g_signal_handler_disconnect (book->priv->listener, book->priv->listener_signal);
			bonobo_object_unref (book->priv->listener);
			book->priv->listener = NULL;

			e_book_clear_op (book, our_op);

			CORBA_exception_free (&ev);
			continue;
		}

		CORBA_exception_free (&ev);

		/* wait for something to happen (both cancellation and a
		   successful response will notity us via our cv */
		g_cond_wait (our_op->cond, our_op->mutex);

		status = our_op->status;

		/* remove the op from the book's hash of operations */
		e_book_clear_op (book, our_op);

		if (status == E_BOOK_ERROR_CANCELLED
		    || status == E_BOOK_ERROR_OK) {
			rv = TRUE;
			break;
		}
	}

	g_free (source_xml);

	/* free up the factories */
	for (l = factories; l; l = l->next)
		CORBA_Object_release ((CORBA_Object)l->data, NULL);

	if (rv == TRUE) {
		if (status == E_BOOK_ERROR_OK) {
			book->priv->corba_book = corba_book;
			book->priv->load_state = E_BOOK_URI_LOADED;
			book->priv->comp_listener = e_component_listener_new (book->priv->corba_book);
			book->priv->died_signal = g_signal_connect (book->priv->comp_listener,
								    "component_died",
								    G_CALLBACK (backend_died_cb), book);
		} else {
			/* Cancelled */
			book->priv->load_state = E_BOOK_URI_NOT_LOADED;
			g_signal_handler_disconnect (book->priv->listener, book->priv->listener_signal);
			bonobo_object_unref (book->priv->listener);
			book->priv->listener = NULL;
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CANCELLED,
				     _("e_book_load_uri: cancelled"));
		}
	}
	else {
		book->priv->load_state = E_BOOK_URI_NOT_LOADED;
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_PROTOCOL_NOT_SUPPORTED,
			     _("e_book_load_uri: no factories available for uri `%s'"), uri);
	}
		
	return rv;
}

/**
 * e_book_load_source:
 * @book: an #EBook
 * @source: the #ESource to load
 * @only_if_exists: if #TRUE, fail if this source doesn't already exist otherwise create it first
 * @error: a #GError to set on failure
 *
 * Load the specified source into the book.
 *
 * Return value: #TRUE if the source was loaded, #FALSE otherwise.
 */
gboolean
e_book_load_source (EBook *book,
		    ESource *source,
		    gboolean only_if_exists,
		    GError **error)
{
	e_return_error_if_fail (book && E_IS_BOOK (book),       E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (source && E_IS_SOURCE (source), E_BOOK_ERROR_INVALID_ARG);

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_URI_NOT_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_URI_ALREADY_LOADED,
			     _("\"%s\" on book after \"%s\""),
			     "e_book_load_source", "e_book_load_source");
		return FALSE;
	}

	/* we have to set this here so that future calls to
	   e_book_load* will fail.  It will be cleared to
	   E_BOOK_URI_NOT_LOADED in fetch_corba_book if there's an
	   error. */
	book->priv->load_state = E_BOOK_URI_LOADING;

	g_mutex_unlock (book->priv->mutex);

	return fetch_corba_book (book, source, only_if_exists, error);
}

/**
 * e_book_load_uri:
 * @book: an #EBook
 * @uri: the URI to load
 * @only_if_exists: if #TRUE, fail if this URI doesn't already exist otherwise create it first
 * @error: a #GError to set on failure
 *
 * Create an #ESource from #url and load the source. See e_book_load_source().
 *
 * Return value: #TRUE if the URI was loaded, #FALSE otherwise.
 */
gboolean
e_book_load_uri (EBook        *book,
		 const char   *uri,
		 gboolean      only_if_exists,
		 GError      **error)
{
	ESourceGroup *group;
	ESource *source;
	gboolean rv;

	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (uri,                      E_BOOK_ERROR_INVALID_ARG);

	group = e_source_group_new ("", uri);
	source = e_source_new ("", "");
	e_source_set_group (source, group);

	rv = e_book_load_source (book, source, only_if_exists, error);

	g_object_unref (source);
	g_object_unref (group);

	return rv;
}

/**
 * e_book_load_local_addressbook:
 * @book: an #EBook
 * @error: a #GError
 *
 * Load the local address book into #book.
 *
 * Return value: #TRUE on success, #FALSE otherwise.
 */
gboolean
e_book_load_local_addressbook (EBook   *book,
			       GError **error)
{
	char *filename;
	char *uri;
	gboolean rv;

	e_return_error_if_fail (book && E_IS_BOOK (book),       E_BOOK_ERROR_INVALID_ARG);

	filename = g_build_filename (g_get_home_dir(),
				     ".evolution/addressbook/local/system",
				     NULL);
	uri = g_strdup_printf ("file://%s", filename);

	g_free (filename);
	
	rv = e_book_load_uri (book, uri, FALSE, error);
	
	g_free (uri);

	return rv;
}

/**
 * e_book_get_uri:
 * @book: an #EBook
 *
 * Get the URI that this book has loaded. This string should not be freed.
 *
 * Return value: The URI.
 */
const char *
e_book_get_uri (EBook *book)
{
	g_return_val_if_fail (book && E_IS_BOOK (book), NULL);

	return book->priv->uri;
}

/**
 * e_book_get_source:
 * @book: an #EBook
 *
 * Get the #ESource that this book has loaded.
 *
 * Return value: The source.
 */
ESource *
e_book_get_source (EBook *book)
{
	g_return_val_if_fail (book && E_IS_BOOK (book), NULL);

	return book->priv->source;
}

/**
 * e_book_get_static_capabilities:
 * @book: an #EBook
 * @error: an #GError to set on failure
 *
 * Get the list of capabilities which the backend for this address book
 * supports. This string should not be freed.
 *
 * Return value: The capabilities list
 */
const char *
e_book_get_static_capabilities (EBook   *book,
				GError **error)
{
	g_return_val_if_fail (book && E_IS_BOOK (book), NULL);

	if (!book->priv->cap_queried) {
		CORBA_Environment ev;
		char *temp;

		CORBA_exception_init (&ev);

		if (book->priv->load_state != E_BOOK_URI_LOADED) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_URI_NOT_LOADED,
				     _("\"%s\" on book before \"%s\""),
				     "e_book_get_static_capabilities", "e_book_load_uri");
			return g_strdup ("");
		}

		temp = GNOME_Evolution_Addressbook_Book_getStaticCapabilities(book->priv->corba_book, &ev);

		if (ev._major != CORBA_NO_EXCEPTION) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
				     _("CORBA exception making \"%s\" call"),
				     "Book::getStaticCapabilities");
			CORBA_exception_free (&ev);
			return g_strdup ("");
		}

		book->priv->cap = g_strdup(temp);
		book->priv->cap_queried = TRUE;

		CORBA_free(temp);

		CORBA_exception_free (&ev);
	}

	return book->priv->cap;
}

/**
 * e_book_check_static_capability:
 * @book: an #EBook
 * @cap: A capability string
 *
 * Check to see if the backend for this address book supports the capability
 * @cap.
 *
 * Return value: #TRUE if the backend supports @cap, #FALSE otherwise.
 */
gboolean
e_book_check_static_capability (EBook *book,
				const char  *cap)
{
	const char *caps;

	g_return_val_if_fail (book && E_IS_BOOK (book), FALSE);

	caps = e_book_get_static_capabilities (book, NULL);

	/* XXX this is an inexact test but it works for our use */
	if (caps && strstr (caps, cap))
		return TRUE;

	return FALSE;
}

/**
 * e_book_is_writable:
 * @book: an #EBook
 * 
 * Check if this book is writable.
 * 
 * Return value: #TRUE if this book is writable, otherwise #FALSE.
 */
gboolean
e_book_is_writable (EBook *book)
{
	g_return_val_if_fail (book && E_IS_BOOK (book), FALSE);

	return book->priv->writable;
}




#define SELF_UID_KEY "/apps/evolution/addressbook/self/self_uid"
gboolean
e_book_get_self (EContact **contact, EBook **book, GError **error)
{
	GError *e = NULL;
	GConfClient *gconf;
	char *uid;

	*book = e_book_new ();

	if (!e_book_load_local_addressbook (*book, &e)) {
		g_propagate_error (error, e);
		return FALSE;
	}

	gconf = gconf_client_get_default();
	uid = gconf_client_get_string (gconf, SELF_UID_KEY, NULL);
	g_object_unref (gconf);

	if (!uid) {
		g_object_unref (*book);
		*book = NULL;
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_NO_SELF_CONTACT,
			     _("e_book_get_self: there was no self contact uid stored in gconf"));
		return FALSE;
	}

	if (!e_book_get_contact (*book, uid, contact, &e)) {
		g_object_unref (*book);
		*book = NULL;
		g_propagate_error (error, e);
		return FALSE;
	}

	return TRUE;
}

gboolean
e_book_set_self (EBook *book, EContact *contact, GError **error)
{
	GConfClient *gconf;

	e_return_error_if_fail (book && E_IS_BOOK (book),          E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (contact && E_IS_CONTACT (contact), E_BOOK_ERROR_INVALID_ARG);

	gconf = gconf_client_get_default();
	gconf_client_set_string (gconf, SELF_UID_KEY, e_contact_get_const (contact, E_CONTACT_UID), NULL);
	g_object_unref (gconf);

	return TRUE;
}

gboolean
e_book_is_self (EContact *contact)
{
	GConfClient *gconf;
	char *uid;
	gboolean rv;

	/* XXX this should probably be e_return_error_if_fail, but we
	   need a GError** arg for that */
	g_return_val_if_fail (contact && E_IS_CONTACT (contact), FALSE);

	gconf = gconf_client_get_default();
	uid = gconf_client_get_string (gconf, SELF_UID_KEY, NULL);
	g_object_unref (gconf);

	rv = (uid && strcmp (uid, e_contact_get_const (contact, E_CONTACT_UID)));

	g_free (uid);

	return rv;
}



/**
 * e_book_get_default_addressbook:
 * @book: The address of a pointer to an #EBook
 * @error: A #GError pointer
 * 
 * Convience function to open the default address book. Pass it the address of a
 * #EBook* and if this returns #TRUE, #book is set to an opened #EBook.
 * 
 * Return value: #TRUE if the default addressbook was opened, otherwise #FALSE.
 */
gboolean
e_book_get_default_addressbook (EBook **book, GError **error)
{
	ESourceList *sources;
	GSList *g;
	GError *err = NULL;
	ESource *default_source = NULL;
	gboolean rv = TRUE;

	e_return_error_if_fail (*book, E_BOOK_ERROR_INVALID_ARG);

	if (!e_book_get_addressbooks (&sources, &err)) {
		g_propagate_error (error, err);
		return FALSE;
	}

	for (g = e_source_list_peek_groups (sources); g; g = g->next) {
		ESourceGroup *group = E_SOURCE_GROUP (g->data);
		GSList *s;
		for (s = e_source_group_peek_sources (group); s; s = s->next) {
			ESource *source = E_SOURCE (s->data);

			if (e_source_get_property (source, "default")) {
				default_source = source;
				break;
			}
		}

		if (default_source)
			break;
	}

	*book = e_book_new ();

	if (default_source) {
		if (!e_book_load_source (*book, default_source, TRUE, &err)) {
			g_propagate_error (error, err);
			rv = FALSE;
			goto done;
		}
	}
	else {
		if (!e_book_load_local_addressbook (*book, &err)) {
			g_propagate_error (error, err);
			rv = FALSE;
			goto done;
		}
	}

 done:
	if (!rv) {
		g_object_unref (*book);
		*book = NULL;
	}
	g_object_unref (sources);
	return rv;
}

/**
 * e_book_set_default_addressbook:
 * @book: An #EBook pointer
 * @error: A #GError pointer
 * 
 * sets the #ESource of the #EBook as the "default" addressbook.  This is the source
 * that will be loaded in the e_book_get_default_addressbook call.
 * 
 * Return value: #TRUE if the setting was stored in libebook's ESourceList, otherwise #FALSE.
 */
gboolean
e_book_set_default_addressbook (EBook *book, GError **error)
{
	ESource *source;

	e_return_error_if_fail (book && E_IS_BOOK (book),                        E_BOOK_ERROR_INVALID_ARG);
	e_return_error_if_fail (book->priv->load_state == E_BOOK_URI_NOT_LOADED, E_BOOK_ERROR_URI_ALREADY_LOADED);

	source = e_book_get_source (book);

	return e_book_set_default_source (source, error);
}


/**
 * e_book_set_default_addressbook:
 * @source: An #ESource pointer
 * @error: A #GError pointer
 * 
 * sets @source as the "default" addressbook.  This is the source that
 * will be loaded in the e_book_get_default_addressbook call.
 * 
 * Return value: #TRUE if the setting was stored in libebook's ESourceList, otherwise #FALSE.
 */
gboolean
e_book_set_default_source (ESource *source, GError **error)
{
	ESourceList *sources;
	const char *uid;
	GError *err = NULL;
	GSList *g;

	e_return_error_if_fail (source && E_IS_SOURCE (source), E_BOOK_ERROR_INVALID_ARG);

	uid = e_source_peek_uid (source);

	if (!e_book_get_addressbooks (&sources, &err)) {
		g_propagate_error (error, err);
		return FALSE;
	}

	/* make sure the source is actually in the ESourceList.  if
	   it's not we don't bother adding it, just return an error */
	source = e_source_list_peek_source_by_uid (sources, uid);
	if (!source) {
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_NO_SUCH_SOURCE,
			     _("e_book_set_default_source: there was no source for uid `%s' stored in gconf."), uid);
		g_object_unref (sources);
		return FALSE;
	}

	/* loop over all the sources clearing out any "default"
	   properties we find */
	for (g = e_source_list_peek_groups (sources); g; g = g->next) {
		GSList *s;
		for (s = e_source_group_peek_sources (E_SOURCE_GROUP (g->data));
		     s; s = s->next) {
			e_source_set_property (E_SOURCE (s->data), "default", NULL);
		}
	}

	/* set the "default" property on the source */
	e_source_set_property (source, "default", "true");

	if (!e_source_list_sync (sources, &err)) {
		g_propagate_error (error, err);
		return FALSE;
	}

	return TRUE;
}

/**
 * e_book_get_addressbooks:
 * @addressbook_sources: A pointer to a ESourceList* to set
 * @error: A pointer to a GError* to set on error
 *
 * Populate *addressbook_sources with the list of all sources which have been
 * added to Evolution.
 *
 * Return value: #TRUE if @addressbook_sources was set, otherwise #FALSE.
 */
gboolean
e_book_get_addressbooks (ESourceList **addressbook_sources, GError **error)
{
	GConfClient *gconf;

	e_return_error_if_fail (addressbook_sources, E_BOOK_ERROR_INVALID_ARG);

	gconf = gconf_client_get_default();
	*addressbook_sources = e_source_list_new_for_gconf (gconf, "/apps/evolution/addressbook/sources");
	g_object_unref (gconf);

	return TRUE;
}


static void*
startup_mainloop (void *arg)
{
	GMainLoop *loop = g_main_loop_new (_ebook_context, FALSE);
	g_main_loop_run (loop);
	return NULL;
}

/* one-time start up for libebook */
static void
e_book_activate()
{
	static GStaticMutex e_book_lock = G_STATIC_MUTEX_INIT;
	static gboolean activated = FALSE;

	g_static_mutex_lock (&e_book_lock);
	if (!activated) {
		pthread_t ebook_mainloop_thread;
		activated = TRUE;

		_ebook_context = g_main_context_new ();

		if (!bonobo_is_initialized ())
			bonobo_init (NULL, NULL);

		pthread_create(&ebook_mainloop_thread, NULL, startup_mainloop, NULL);
	}
	g_static_mutex_unlock (&e_book_lock);
}



/**
 * e_book_new:
 *
 * Creates a new #EBook. Use e_book_load_uri() or
 * e_book_load_local_addressbook() to access a data store.  Alternatively use
 * e_book_get_default_addressbook().
 *
 * Return value: a new but unopened #EBook.
 **/
EBook*
e_book_new (void)
{
	e_book_activate ();
	return g_object_new (E_TYPE_BOOK, NULL);
}


static void
e_book_init (EBook *book)
{
	book->priv             = g_new0 (EBookPrivate, 1);
	book->priv->load_state = E_BOOK_URI_NOT_LOADED;
	book->priv->uri        = NULL;
	book->priv->source     = NULL;
	book->priv->mutex      = g_mutex_new ();
}

static void
e_book_dispose (GObject *object)
{
	EBook             *book = E_BOOK (object);

	if (book->priv) {
		CORBA_Environment  ev;
		GList *l;

		if (book->priv->comp_listener) {
			g_signal_handler_disconnect (book->priv->comp_listener, book->priv->died_signal);
			g_object_unref (book->priv->comp_listener);
			book->priv->comp_listener = NULL;
		}

		if (book->priv->load_state == E_BOOK_URI_LOADED)
			e_book_unload_uri (book, NULL);

		CORBA_exception_init (&ev);

		for (l = book->priv->book_factories; l; l = l->next) {
			CORBA_Object_release ((CORBA_Object)l->data, &ev);
			if (ev._major != CORBA_NO_EXCEPTION) {
				g_warning ("EBook: Exception while releasing BookFactory\n");

				CORBA_exception_free (&ev);
				CORBA_exception_init (&ev);
			}
		}
		
		CORBA_exception_free (&ev);

		if (book->priv->listener) {
			g_signal_handler_disconnect (book->priv->listener, book->priv->listener_signal);
			bonobo_object_unref (book->priv->listener);
			book->priv->listener = NULL;
		}
		
		g_free (book->priv->cap);

		g_free (book->priv->uri);

		if (book->priv->source)
			g_object_unref (book->priv->source);

		g_free (book->priv);
		book->priv = NULL;
	}

	if (G_OBJECT_CLASS (parent_class)->dispose)
		G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_book_class_init (EBookClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_ref (G_TYPE_OBJECT);

	e_book_signals [WRITABLE_STATUS] =
		g_signal_new ("writable_status",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EBookClass, writable_status),
			      NULL, NULL,
			      e_book_marshal_NONE__BOOL,
			      G_TYPE_NONE, 1,
			      G_TYPE_BOOLEAN);

	e_book_signals [BACKEND_DIED] =
		g_signal_new ("backend_died",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EBookClass, backend_died),
			      NULL, NULL,
			      e_book_marshal_NONE__NONE,
			      G_TYPE_NONE, 0);

	object_class->dispose = e_book_dispose;
}

/**
 * e_book_get_type:
 */
GType
e_book_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (EBookClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_book_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (EBook),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_book_init
		};

		type = g_type_register_static (G_TYPE_OBJECT, "EBook", &info, 0);
	}

	return type;
}

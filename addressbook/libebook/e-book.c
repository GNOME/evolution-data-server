/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <config.h>

#include <string.h>

#include "e-book.h"
#include "e-book-view-private.h"
#include "e-vcard.h"

#include <bonobo-activation/bonobo-activation.h>

#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-main.h>
#include <gconf/gconf-client.h>

#include <glib/gi18n-lib.h>
#include "libedataserver/e-flag.h"
#include "libedataserver/e-component-listener.h"

#include "e-book-listener.h"

#define d(x)

GMainContext *_ebook_context;

static GObjectClass *parent_class;

#ifdef __PRETTY_FUNCTION__
#define e_return_error_if_fail(expr,error_code,retval)	G_STMT_START{	\
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
	 return (retval);						\
       };				}G_STMT_END
#else
#define e_return_error_if_fail(expr,error_code,retval)	G_STMT_START{	\
     if G_LIKELY(expr) { } else						\
       {								\
	 g_log (G_LOG_DOMAIN,						\
		G_LOG_LEVEL_CRITICAL,					\
		"file %s: line %d: assertion `%s' failed",		\
		__FILE__,						\
		__LINE__,						\
		#expr);							\
	 g_set_error (error, E_BOOK_ERROR, (error_code),                \
		"file %s: line %d: assertion `%s' failed",		\
		__FILE__,						\
		__LINE__,						\
		#expr);							\
	 return (retval);						\
       };				}G_STMT_END
#endif

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
	CONNECTION_STATUS,
	AUTH_REQUIRED,
	BACKEND_DIED,
	LAST_SIGNAL
};

static guint e_book_signals [LAST_SIGNAL];

typedef struct {
	gint32 opid;
	gint idle_id;
	gboolean synchronous;
	EFlag *flag;
	EBook *book;
	EBookStatus status;
	char *id;
	GList *list;
	EList *elist;
	EContact *contact;

	EBookView *view;
	EBookViewListener *listener;

	/* callbacks/closure for async calls */
	union {
		EBookIdCallback id;
		EBookCallback status;
		EBookContactCallback contact;
		EBookListCallback list;
		EBookBookViewCallback book_view;
		EBookEListCallback elist;
	} cb;
	gpointer closure;
} EBookOp;

typedef enum {
	E_BOOK_SOURCE_NOT_LOADED,
	E_BOOK_SOURCE_LOADING,
	E_BOOK_SOURCE_LOADED
} EBookLoadState;

struct _EBookPrivate {
	/* cached capabilites */
	char *cap;
	gboolean cap_queried;

	/* cached writable status */
	gboolean writable;
	gboolean connected;

	EBookListener         *listener;
	EComponentListener    *comp_listener;

	GNOME_Evolution_Addressbook_Book         corba_book;

	EBookLoadState         load_state;

	GList *pending_idles;
	GHashTable *id_to_op;
	guint32 current_op_id;

	GMutex *mutex;

	/* Need to keep URI around, since the getter returns const */
	gchar *uri;
	ESource *source;

	gulong listener_signal;
	gulong died_signal;

	gint writable_idle_id;
	gint connection_idle_id;
	gint auth_idle_id;
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
e_book_new_op (EBook *book, gboolean sync)
{
	EBookOp *op = g_new0 (EBookOp, 1);

	op->flag = e_flag_new ();

	op->synchronous = sync;
	op->opid = book->priv->current_op_id++;

	g_hash_table_insert (book->priv->id_to_op,
			     &op->opid, op);

	return op;
}

static EBookOp*
e_book_get_op (EBook *book, int opid)
{
	return (EBookOp*)g_hash_table_lookup (book->priv->id_to_op,
					      &opid);
}

static EBookOp*
e_book_get_current_sync_op (EBook *book)
{
	guint32 opid = 0;
	return (EBookOp*)g_hash_table_lookup (book->priv->id_to_op,
					      &opid);
}

static void
e_book_op_free (EBookOp *op)
{
	e_flag_free (op->flag);
	g_free (op);
}

static void
e_book_op_remove (EBook *book,
		  EBookOp *op)
{
	g_hash_table_remove (book->priv->id_to_op,
			     &op->opid);
}

static void
e_book_clear_op (EBook *book,
		 EBookOp *op)
{
	e_book_op_remove (book, op);
	e_flag_clear (op->flag);
	e_book_op_free (op);
}


static gboolean
do_add_contact (gboolean          sync,
		EBook            *book,
		EContact         *contact,
		GError          **error,  /* for sync case */
		EBookIdCallback   cb, /* for async case */
		gpointer          closure)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;
	char *vcard_str;

	d(printf ("do_add_contact\n"));

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_SOURCE_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_SOURCE_NOT_LOADED,
				     /* translators: the placeholders will be filled by
				      * function names, e.g.
				      * "e_book_add_contact" on book before
				      * "e_book_open */
				     _("\"%s\" on book before \"%s\""),
				     "e_book_add_contact", "e_book_open");
		}
		else {
			g_warning (_("\"%s\" on book before \"%s\""),
				   "e_book_add_contact", "e_book_open");
		}
		return FALSE;
	}

	if (sync && e_book_get_current_sync_op (book) != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book, sync);

	g_mutex_unlock (book->priv->mutex);

	vcard_str = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	CORBA_exception_init (&ev);

	our_op->cb.id = cb;
	our_op->closure = closure;

	/* will eventually end up calling e_book_response_add_contact */
	GNOME_Evolution_Addressbook_Book_addContact (book->priv->corba_book, our_op->opid,
						     (const GNOME_Evolution_Addressbook_VCard) vcard_str, &ev);

	g_free (vcard_str);

	if (ev._major != CORBA_NO_EXCEPTION) {

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		CORBA_exception_free (&ev);

		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
				     _("CORBA exception making \"%s\" call"),
				     "Book::addContact");
		}
		else {
			g_warning (_("CORBA exception making \"%s\" call"),
				   "Book::addContact");
		}
		return FALSE;
	}

	CORBA_exception_free (&ev);

	if (sync) {
		/* wait for something to happen (both cancellation and a
		   successful response will notity us via our cv */
		e_flag_wait (our_op->flag);

		status = our_op->status;
		e_contact_set (contact, E_CONTACT_UID, our_op->id);
		g_free (our_op->id);

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		E_BOOK_CHECK_STATUS (status, error);
	}
	else {
		return TRUE;
	}
}

/**
 * e_book_add_contact:
 * @book: an #EBook
 * @contact: an #EContact
 * @error: a #GError to set on failure
 *
 * Adds @contact to @book.
 *
 * Return value: %TRUE if successful, %FALSE otherwise.
 **/
gboolean
e_book_add_contact (EBook           *book,
		    EContact        *contact,
		    GError         **error)
{
	d(printf ("e_book_add_contact\n"));

	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (contact && E_IS_CONTACT (contact), E_BOOK_ERROR_INVALID_ARG, FALSE);

	return do_add_contact (TRUE,
			       book, contact, error,
			       NULL, NULL);
}

/**
 * e_book_async_add_contact:
 * @book: an #EBook
 * @contact: an #EContact
 * @cb: function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Adds @contact to @book without blocking.
 *
 * Return value: %TRUE if the operation was started, %FALSE otherwise.
 **/
gboolean
e_book_async_add_contact (EBook                 *book,
			  EContact              *contact,
			  EBookIdCallback        cb,
			  gpointer               closure)
{
	d(printf ("e_book_async_add_contact\n"));

	g_return_val_if_fail (book && E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (contact && E_IS_CONTACT (contact), FALSE);

	return !do_add_contact (FALSE,
				book, contact, NULL,
				cb, closure);
}

static gboolean
emit_async_add_contact_response (gpointer data)
{
	EBookOp *op = data;
	EBook *book = op->book;

	if (op->cb.id)
		op->cb.id (book, op->status, op->id, op->closure);

	g_mutex_lock (book->priv->mutex);

	g_free (op->id);

	book->priv->pending_idles = g_list_remove (book->priv->pending_idles,
						   GINT_TO_POINTER (op->idle_id));

	e_book_clear_op (book, op);

	g_mutex_unlock (book->priv->mutex);
	g_object_unref (book);

	return FALSE;
}

static void
e_book_response_add_contact (EBook       *book,
			     guint32      opid,
			     EBookStatus  status,
			     char        *id)
{
	EBookOp *op;

	d(printf ("e_book_response_add_contact\n"));

	g_mutex_lock (book->priv->mutex);

	op = e_book_get_op (book, opid);

	if (op == NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_warning ("e_book_response_add_contact: Cannot find operation ");
		return;
	}

	op->id = g_strdup (id);
	op->status = status;

	if (op->synchronous)
		e_flag_set (op->flag);
	else {
		op->book = g_object_ref (book);

		op->idle_id = g_idle_add (emit_async_add_contact_response, op);
		book->priv->pending_idles = g_list_prepend (book->priv->pending_idles,
							    GINT_TO_POINTER (op->idle_id));
	}

	g_mutex_unlock (book->priv->mutex);
}



static gboolean
do_commit_contact (gboolean        sync,
		   EBook          *book,
		   EContact       *contact,
		   GError        **error,
		   EBookCallback   cb,
		   gpointer        closure)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;
	char *vcard_str;

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_SOURCE_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_SOURCE_NOT_LOADED,
				     _("\"%s\" on book before \"%s\""),
				     "e_book_commit_contact", "e_book_open");
		}
		else {
			g_warning (_("\"%s\" on book before \"%s\""),
				   "e_book_commit_contact", "e_book_open");
		}
		return FALSE;
	}

	if (sync && e_book_get_current_sync_op (book) != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	vcard_str = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	our_op = e_book_new_op (book, sync);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	our_op->cb.status = cb;
	our_op->closure = closure;

	/* will eventually end up calling _e_book_response_generic */
	GNOME_Evolution_Addressbook_Book_modifyContact (book->priv->corba_book, our_op->opid,
							(const GNOME_Evolution_Addressbook_VCard) vcard_str, &ev);

	g_free (vcard_str);

	if (ev._major != CORBA_NO_EXCEPTION) {

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		CORBA_exception_free (&ev);

		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
				     _("CORBA exception making \"%s\" call"),
				     "Book::modifyContact");
		}
		else {
			g_warning (_("CORBA exception making \"%s\" call"),
				   "Book::modifyContact");
		}
		return FALSE;
	}

	CORBA_exception_free (&ev);

	if (sync) {
		/* wait for something to happen (both cancellation and a
		   successful response will notity us via our cv */
		e_flag_wait (our_op->flag);

		status = our_op->status;
		g_free (our_op->id);

		/* remove the op from the book's hash of operations */
		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		E_BOOK_CHECK_STATUS (status, error);
	}
	else {
		return TRUE;
	}
}


/**
 * e_book_commit_contact:
 * @book: an #EBook
 * @contact: an #EContact
 * @error: a #GError to set on failure
 *
 * Applies the changes made to @contact to the stored version in
 * @book.
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 **/
gboolean
e_book_commit_contact (EBook           *book,
		       EContact        *contact,
		       GError         **error)
{
	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (contact && E_IS_CONTACT (contact), E_BOOK_ERROR_INVALID_ARG, FALSE);

	return do_commit_contact (TRUE,
				  book, contact, error,
				  NULL, NULL);

}

/**
 * e_book_async_commit_contact:
 * @book: an #EBook
 * @contact: an #EContact
 * @cb: function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Applies the changes made to @contact to the stored version in
 * @book without blocking.
 *
 * Return value: %TRUE if the operation was started, %FALSE otherwise.
 **/
guint
e_book_async_commit_contact (EBook                 *book,
			     EContact              *contact,
			     EBookCallback          cb,
			     gpointer               closure)
{
	return !do_commit_contact (FALSE,
				   book, contact, NULL,
				   cb, closure);
}

static gboolean
do_get_required_fields (gboolean             sync,
			 EBook               *book,
			 GList              **fields,
			 GError             **error,
			 EBookEListCallback   cb,
			 gpointer             closure)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_SOURCE_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_SOURCE_NOT_LOADED,
				     _("\"%s\" on book before \"%s\""),
				     "e_book_get_required_fields", "e_book_open");
		}
		else {
			g_warning (_("\"%s\" on book before \"%s\""),
				   "e_book_get_required_fields", "e_book_open");
		}
		return FALSE;
	}

	if (sync && e_book_get_current_sync_op (book) != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book, sync);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	our_op->cb.elist = cb;
	our_op->closure = closure;

	/* will eventually end up calling
	   _e_book_response_get_supported_fields */
	GNOME_Evolution_Addressbook_Book_getRequiredFields(book->priv->corba_book, our_op->opid, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		CORBA_exception_free (&ev);

		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
				     _("CORBA exception making \"%s\" call"),
				     "Book::getRequiredFields");
		}
		else {
			g_warning (_("CORBA exception making \"%s\" call"),
				   "Book::getRequiredFields");
		}

		return FALSE;
	}


	CORBA_exception_free (&ev);

	if (sync) {
		/* wait for something to happen (both cancellation and a
		   successful response will notify us via our cv */
		e_flag_wait (our_op->flag);

		status = our_op->status;
		*fields = our_op->list;

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		E_BOOK_CHECK_STATUS (status, error);
	}
	else {
		return TRUE;
	}
}

static gboolean
do_get_supported_fields (gboolean             sync,
			 EBook               *book,
			 GList              **fields,
			 GError             **error,
			 EBookEListCallback   cb,
			 gpointer             closure)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_SOURCE_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_SOURCE_NOT_LOADED,
				     _("\"%s\" on book before \"%s\""),
				     "e_book_get_supported_fields", "e_book_open");
		}
		else {
			g_warning (_("\"%s\" on book before \"%s\""),
				   "e_book_get_supported_fields", "e_book_open");
		}
		return FALSE;
	}

	if (sync && e_book_get_current_sync_op (book) != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book, sync);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	our_op->cb.elist = cb;
	our_op->closure = closure;

	/* will eventually end up calling
	   _e_book_response_get_supported_fields */
	GNOME_Evolution_Addressbook_Book_getSupportedFields(book->priv->corba_book, our_op->opid, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		CORBA_exception_free (&ev);

		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
				     _("CORBA exception making \"%s\" call"),
				     "Book::getSupportedFields");
		}
		else {
			g_warning (_("CORBA exception making \"%s\" call"),
				   "Book::getSupportedFields");
		}

		return FALSE;
	}


	CORBA_exception_free (&ev);

	if (sync) {
		/* wait for something to happen (both cancellation and a
		   successful response will notify us via our cv */
		e_flag_wait (our_op->flag);

		status = our_op->status;
		*fields = our_op->list;

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		E_BOOK_CHECK_STATUS (status, error);
	}
	else {
		return TRUE;
	}
}

/**
 * e_book_get_required_fields:
 * @book: an #EBook
 * @fields: a #GList of fields to set on success
 * @error: a #GError to set on failure
 *
 * Gets a list of fields that are required to be filled in for
 * all contacts in this @book. The list will contain pointers
 * to allocated strings, and both the #GList and the strings
 * must be freed by the caller.
 *
 * Return value: %TRUE if successful, %FALSE otherwise.
 **/
gboolean
e_book_get_required_fields  (EBook            *book,
			      GList           **fields,
			      GError          **error)
{
	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (fields,                   E_BOOK_ERROR_INVALID_ARG, FALSE);

	return do_get_required_fields (TRUE,
					book, fields, error,
					NULL, NULL);
}

/**
 * e_book_async_get_required_fields:
 * @book: an #EBook
 * @cb: function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Gets a list of fields that are required to be filled in for
 * all contacts in this @book. This function does not block.
 *
 * Return value: %TRUE if the operation was started, %FALSE otherwise.
 **/
guint
e_book_async_get_required_fields (EBook              *book,
				   EBookEListCallback  cb,
				   gpointer            closure)
{
	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	return !do_get_required_fields (FALSE,
					 book, NULL, NULL,
					 cb, closure);
}

/**
 * e_book_get_supported_fields:
 * @book: an #EBook
 * @fields: a #GList of fields to set on success
 * @error: a #GError to set on failure
 *
 * Gets a list of fields that can be stored for contacts
 * in this @book. Other fields may be discarded. The list
 * will contain pointers to allocated strings, and both the
 * #GList and the strings must be freed by the caller.
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 **/
gboolean
e_book_get_supported_fields  (EBook            *book,
			      GList           **fields,
			      GError          **error)
{
	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (fields,                   E_BOOK_ERROR_INVALID_ARG, FALSE);

	return do_get_supported_fields (TRUE,
					book, fields, error,
					NULL, NULL);
}

/**
 * e_book_async_get_supported_fields:
 * @book: an #EBook
 * @cb: function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Gets a list of fields that can be stored for contacts
 * in this @book. Other fields may be discarded. This
 * function does not block.
 *
 * Return value: %TRUE if successful, %FALSE otherwise.
 **/
guint
e_book_async_get_supported_fields (EBook              *book,
				   EBookEListCallback  cb,
				   gpointer            closure)
{
	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	return !do_get_supported_fields (FALSE,
					 book, NULL, NULL,
					 cb, closure);
}

static gboolean
emit_async_elist_response (gpointer data)
{
	EBookOp *op = data;
	EBook *book = op->book;

	if (op->cb.elist)
		op->cb.elist (book, op->status, op->elist, op->closure);

	g_mutex_lock (book->priv->mutex);

	g_object_unref (op->elist);

	book->priv->pending_idles = g_list_remove (book->priv->pending_idles,
						   GINT_TO_POINTER (op->idle_id));

	e_book_clear_op (book, op);

	g_mutex_unlock (book->priv->mutex);
	g_object_unref (book);

	return FALSE;
}

static void
e_book_response_get_required_fields (EBook       *book,
				      guint32      opid,
				      EBookStatus  status,
				      GList       *fields)

{
	EBookOp *op;

	g_mutex_lock (book->priv->mutex);

	op = e_book_get_op (book, opid);

	if (op == NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_warning ("e_book_response_get_required_fields: Cannot find operation ");
		return;
	}

	op->status = status;
	if (op->synchronous) {
		op->list = fields;
		e_flag_set (op->flag);
	} else {
		GList *l;
		EList *efields = e_list_new ((EListCopyFunc) g_strdup,
					     (EListFreeFunc) g_free,
					     NULL);

		for (l = fields; l; l = l->next)
			e_list_append (efields, l->data);

		op->book = g_object_ref (book);
		op->elist = efields;

		op->idle_id = g_idle_add (emit_async_elist_response, op);
		book->priv->pending_idles = g_list_prepend (book->priv->pending_idles,
							    GINT_TO_POINTER (op->idle_id));
	}

	g_mutex_unlock (book->priv->mutex);
}

static void
e_book_response_get_supported_fields (EBook       *book,
				      guint32      opid,
				      EBookStatus  status,
				      GList       *fields)
{
	EBookOp *op;

	g_mutex_lock (book->priv->mutex);

	op = e_book_get_op (book, opid);

	if (op == NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_warning ("e_book_response_get_supported_fields: Cannot find operation ");
		return;
	}

	op->status = status;
	if (op->synchronous) {
		op->list = fields;
		e_flag_set (op->flag);
	} else {
		GList *l;
		EList *efields = e_list_new ((EListCopyFunc) g_strdup,
					     (EListFreeFunc) g_free,
					     NULL);

		for (l = fields; l; l = l->next)
			e_list_append (efields, l->data);

		op->book = g_object_ref (book);
		op->elist = efields;

		op->idle_id = g_idle_add (emit_async_elist_response, op);
		book->priv->pending_idles = g_list_prepend (book->priv->pending_idles,
							    GINT_TO_POINTER (op->idle_id));
	}

	g_mutex_unlock (book->priv->mutex);
}


static gboolean
do_get_supported_auth_methods (gboolean             sync,
			       EBook               *book,
			       GList              **auth_methods,
			       GError             **error,
			       EBookEListCallback   cb,
			       gpointer             closure)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_SOURCE_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_SOURCE_NOT_LOADED,
				     _("\"%s\" on book before \"%s\""),
				     "e_book_get_supported_auth_methods",
				     "e_book_open");
		}
		else {
			g_warning (_("\"%s\" on book before \"%s\""),
				   "e_book_get_supported_auth_methods",
				   "e_book_open");
		}
		return FALSE;
	}

	if (sync && e_book_get_current_sync_op (book) != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book, sync);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	our_op->cb.elist = cb;
	our_op->closure = closure;

	/* will eventually end up calling
	   e_book_response_get_supported_fields */
	GNOME_Evolution_Addressbook_Book_getSupportedAuthMethods(book->priv->corba_book, our_op->opid, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		CORBA_exception_free (&ev);

		if (error)
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
				     _("CORBA exception making \"%s\" call"),
				     "Book::getSupportedAuthMethods");
		else
			g_warning (_("CORBA exception making \"%s\" call"),
				   "Book::getSupportedAuthMethods");
		return FALSE;
	}


	CORBA_exception_free (&ev);

	if (sync) {
		/* wait for something to happen (both cancellation and a
		   successful response will notity us via our cv */
		e_flag_wait (our_op->flag);

		status = our_op->status;
		*auth_methods = our_op->list;

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		E_BOOK_CHECK_STATUS (status, error);
	}
	else {
		return TRUE;
	}
}

/**
 * e_book_get_supported_auth_methods:
 * @book: an #EBook
 * @auth_methods: a #GList of auth methods to set on success
 * @error: a #GError to set on failure
 *
 * Queries @book for the list of authentication methods it supports.
 * The list will contain pointers to allocated strings, and both the
 * #GList and the strings must be freed by the caller.
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 **/
gboolean
e_book_get_supported_auth_methods (EBook            *book,
				   GList           **auth_methods,
				   GError          **error)
{
	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (auth_methods,             E_BOOK_ERROR_INVALID_ARG, FALSE);

	return do_get_supported_auth_methods (TRUE,
					      book, auth_methods, error,
					      NULL, NULL);
}

/**
 * e_book_async_get_supported_auth_methods:
 * @book: an #EBook
 * @cb: function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Queries @book for the list of authentication methods it supports.
 * This function does not block.
 *
 * Return value: %TRUE if successful, %FALSE otherwise.
 **/
guint
e_book_async_get_supported_auth_methods (EBook              *book,
					 EBookEListCallback  cb,
					 gpointer            closure)
{
	g_return_val_if_fail (book && E_IS_BOOK (book), TRUE);

	return !do_get_supported_auth_methods (FALSE,
					       book, NULL, NULL,
					       cb, closure);
}

static void
e_book_response_get_supported_auth_methods (EBook                 *book,
					    guint32                opid,
					    EBookStatus            status,
					    GList                 *auth_methods)
{
	EBookOp *op;

	g_mutex_lock (book->priv->mutex);

	op = e_book_get_op (book, opid);

	if (op == NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_warning ("e_book_response_get_supported_auth_methods: Cannot find operation ");
		return;
	}

	op->status = status;
	if (op->synchronous) {
		op->list = auth_methods;
		e_flag_set (op->flag);
	} else {
		GList *l;
		EList *emethods = e_list_new ((EListCopyFunc) g_strdup,
					      (EListFreeFunc) g_free,
					      NULL);

		for (l = auth_methods; l; l = l->next)
			e_list_append (emethods, l->data);

		op->book = g_object_ref (book);
		op->elist = emethods;

		op->idle_id = g_idle_add (emit_async_elist_response, op);
		book->priv->pending_idles = g_list_prepend (book->priv->pending_idles,
							    GINT_TO_POINTER (op->idle_id));
	}

	g_mutex_unlock (book->priv->mutex);
}



static gboolean
do_authenticate_user (gboolean        sync,
		      EBook          *book,
		      const char     *user,
		      const char     *passwd,
		      const char     *auth_method,
		      GError        **error,
		      EBookCallback   cb,
		      gpointer        closure)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_SOURCE_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_SOURCE_NOT_LOADED,
				     _("\"%s\" on book before \"%s\""),
				     "e_book_authenticate_user", "e_book_open");
		}
		else {
			g_warning (_("\"%s\" on book before \"%s\""),
				   "e_book_authenticate_user", "e_book_open");
		}

		return FALSE;
	}

	if (sync && e_book_get_current_sync_op (book) != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book, sync);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	our_op->cb.status = cb;
	our_op->closure = closure;

	/* will eventually end up calling
	   e_book_response_generic */
	GNOME_Evolution_Addressbook_Book_authenticateUser (book->priv->corba_book, our_op->opid,
							   user, passwd,
							   auth_method,
							   &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		CORBA_exception_free (&ev);

		if (error)
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
				     _("CORBA exception making \"%s\" call"),
				     "Book::authenticateUser");
		else
			g_warning (_("CORBA exception making \"%s\" call"),
				   "Book::authenticateUser");

		return FALSE;
	}

	CORBA_exception_free (&ev);

	if (sync) {
		/* wait for something to happen (both cancellation and a
		   successful response will notity us via our cv */
		e_flag_wait (our_op->flag);

		status = our_op->status;

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		E_BOOK_CHECK_STATUS (status, error);
	}
	else {
		return TRUE;
	}
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
 * Return value: %TRUE if successful, %FALSE otherwise
 **/
gboolean
e_book_authenticate_user (EBook         *book,
			  const char    *user,
			  const char    *passwd,
			  const char    *auth_method,
			  GError       **error)
{
	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (user,                     E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (passwd,                   E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (auth_method,              E_BOOK_ERROR_INVALID_ARG, FALSE);

	return do_authenticate_user (TRUE,
				     book, user, passwd, auth_method, error,
				     NULL, NULL);
}

/**
 * e_book_async_authenticate_user:
 * @book: an #EBook
 * @user: user name
 * @passwd: password
 * @auth_method: string indicating authentication method
 * @cb: function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Authenticate @user with @passwd, using the auth method
 * @auth_method. @auth_method must be one of the authentication
 * methods returned using e_book_get_supported_auth_methods.
 * This function does not block.
 *
 * Return value: %FALSE if successful, %TRUE otherwise.
 **/
guint
e_book_async_authenticate_user (EBook                 *book,
				const char            *user,
				const char            *passwd,
				const char            *auth_method,
				EBookCallback         cb,
				gpointer              closure)
{
	g_return_val_if_fail (book && E_IS_BOOK (book), TRUE);
	g_return_val_if_fail (user,                     TRUE);
	g_return_val_if_fail (passwd,                   TRUE);
	g_return_val_if_fail (auth_method,              TRUE);

	return !do_authenticate_user (FALSE,
				      book, user, passwd, auth_method, NULL,
				      cb, closure);
}



static gboolean
do_get_contact (gboolean sync,
		EBook *book,
		const char *id,
		EContact **contact,
		GError **error,
		EBookContactCallback cb,
		gpointer closure)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_SOURCE_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_SOURCE_NOT_LOADED,
				     _("\"%s\" on book before \"%s\""),
				     "e_book_get_contact", "e_book_open");
		}
		else {
			g_warning (_("\"%s\" on book before \"%s\""),
				   "e_book_get_contact", "e_book_open");
		}

		return FALSE;
	}

	if (sync && e_book_get_current_sync_op (book) != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book, sync);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	our_op->cb.contact = cb;
	our_op->closure = closure;

	/* will eventually end up calling e_book_response_generic */
	GNOME_Evolution_Addressbook_Book_getContact (book->priv->corba_book, our_op->opid,
						     (const GNOME_Evolution_Addressbook_VCard) id, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		CORBA_exception_free (&ev);

		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
				     _("CORBA exception making \"%s\" call"),
				     "Book::getContact");
		}
		else {
			g_warning (_("CORBA exception making \"%s\" call"),
				   "Book::getContact");
		}
		return FALSE;
	}

	CORBA_exception_free (&ev);

	if (sync) {
		/* wait for something to happen (both cancellation and a
		   successful response will notity us via our cv */
		e_flag_wait (our_op->flag);

		status = our_op->status;
		*contact = our_op->contact;

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		E_BOOK_CHECK_STATUS (status, error);
	}
	else {
		return TRUE;
	}
}

/**
 * e_book_get_contact:
 * @book: an #EBook
 * @id: a unique string ID specifying the contact
 * @contact: an #EContact
 * @error: a #GError to set on failure
 *
 * Fills in @contact with the contents of the vcard in @book
 * corresponding to @id.
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 **/
gboolean
e_book_get_contact (EBook       *book,
		    const char  *id,
		    EContact   **contact,
		    GError     **error)
{
	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (id,                       E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (contact,                  E_BOOK_ERROR_INVALID_ARG, FALSE);

	return do_get_contact (TRUE,
			       book, id, contact, error,
			       NULL, NULL);
}

/**
 * e_book_async_get_contact:
 * @book: an #EBook
 * @id: a unique string ID specifying the contact
 * @cb: function to call when operation finishes
 * @closure: data to pass to callback function
 *
 * Retrieves a contact specified by @id from @book.
 *
 * Return value: %FALSE if successful, %TRUE otherwise
 **/
guint
e_book_async_get_contact (EBook                 *book,
			  const char            *id,
			  EBookContactCallback   cb,
			  gpointer               closure)
{
	g_return_val_if_fail (book && E_IS_BOOK (book), TRUE);
	g_return_val_if_fail (id,                       TRUE);

	return !do_get_contact (FALSE,
				book, id, NULL, NULL,
				cb, closure);
}

static gboolean
emit_async_get_contact_response (gpointer data)
{
	EBookOp *op = data;
	EBook *book = op->book;

	if (op->cb.contact)
		op->cb.contact (book, op->status, op->contact, op->closure);

	g_mutex_lock (book->priv->mutex);

	g_object_unref (op->contact);

	book->priv->pending_idles = g_list_remove (book->priv->pending_idles,
						   GINT_TO_POINTER (op->idle_id));

	e_book_clear_op (book, op);

	g_mutex_unlock (book->priv->mutex);
	g_object_unref (book);

	return FALSE;
}

static void
e_book_response_get_contact (EBook       *book,
			     guint32      opid,
			     EBookStatus  status,
			     EContact    *contact)
{
	EBookOp *op;

	d(printf ("e_book_response_get_contact\n"));

	g_mutex_lock (book->priv->mutex);

	op = e_book_get_op (book, opid);

	if (op == NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_warning ("e_book_response_get_contact: Cannot find operation ");
		return;
	}

	op->status = status;
	op->contact = contact;

	if (op->synchronous)
		e_flag_set (op->flag);
	else {
		op->book = g_object_ref (book);

		op->idle_id = g_idle_add (emit_async_get_contact_response, op);
		book->priv->pending_idles = g_list_prepend (book->priv->pending_idles,
							    GINT_TO_POINTER (op->idle_id));
	}

	g_mutex_unlock (book->priv->mutex);
}


static gboolean
do_remove_contacts (gboolean sync,
		    EBook *book,
		    GList *ids,
		    GError **error,
		    EBookCallback cb,
		    gpointer closure)
{
	GNOME_Evolution_Addressbook_ContactIdList idlist;
	CORBA_Environment ev;
	GList *iter;
	int num_ids, i;
	EBookOp *our_op;
	EBookStatus status;

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_SOURCE_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_SOURCE_NOT_LOADED,
				     _("\"%s\" on book before \"%s\""),
				     "e_book_remove_contacts", "e_book_open");
		}
		else {
			g_warning (_("\"%s\" on book before \"%s\""),
				   "e_book_remove_contacts", "e_book_open");
		}
		return FALSE;
	}

	if (sync && e_book_get_current_sync_op (book) != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book, sync);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	num_ids = g_list_length (ids);
	idlist._buffer = CORBA_sequence_GNOME_Evolution_Addressbook_ContactId_allocbuf (num_ids);
	idlist._maximum = num_ids;
	idlist._length = num_ids;

	for (iter = ids, i = 0; iter; iter = iter->next)
		idlist._buffer[i++] = CORBA_string_dup (iter->data);

	our_op->cb.status = cb;
	our_op->closure = closure;

	/* will eventually end up calling e_book_response_generic */
	GNOME_Evolution_Addressbook_Book_removeContacts (book->priv->corba_book, our_op->opid, &idlist, &ev);

	CORBA_free(idlist._buffer);

	if (ev._major != CORBA_NO_EXCEPTION) {

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		CORBA_exception_free (&ev);

		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
				     _("CORBA exception making \"%s\" call"),
				     "Book::removeContacts");
		}
		else {
			g_warning (_("CORBA exception making \"%s\" call"),
				   "Book::removeContacts");
		}
		return FALSE;
	}

	CORBA_exception_free (&ev);

	if (sync) {
		/* wait for something to happen (both cancellation and a
		   successful response will notity us via our cv */
		e_flag_wait (our_op->flag);

		status = our_op->status;

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		E_BOOK_CHECK_STATUS (status, error);
	}
	else {
		return TRUE;
	}
}

/**
 * e_book_remove_contact:
 * @book: an #EBook
 * @id: a string
 * @error: a #GError to set on failure
 *
 * Removes the contact with id @id from @book.
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 **/
gboolean
e_book_remove_contact (EBook       *book,
		       const char  *id,
		       GError     **error)
{
	GList *list;
	gboolean rv;

	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (id,                       E_BOOK_ERROR_INVALID_ARG, FALSE);

	list = g_list_append (NULL, (char*)id);

	rv = e_book_remove_contacts (book, list, error);

	g_list_free (list);

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
 * Return value: %TRUE if successful, %FALSE otherwise
 **/
gboolean
e_book_remove_contacts (EBook    *book,
			GList    *ids,
			GError  **error)
{
	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (ids,                      E_BOOK_ERROR_INVALID_ARG, FALSE);

	return do_remove_contacts (TRUE,
				   book, ids, error,
				   NULL, NULL);
}

/**
 * e_book_async_remove_contact:
 * @book: an #EBook
 * @contact: an #EContact
 * @cb: a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Removes @contact from @book.
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 **/
guint
e_book_async_remove_contact (EBook                 *book,
			     EContact              *contact,
			     EBookCallback          cb,
			     gpointer               closure)
{
	const char *id;

	g_return_val_if_fail (E_IS_BOOK (book), TRUE);
	g_return_val_if_fail (E_IS_CONTACT (contact), TRUE);

	id = e_contact_get_const (contact, E_CONTACT_UID);

	return e_book_async_remove_contact_by_id (book, id, cb, closure);
}

/**
 * e_book_async_remove_contact_by_id:
 * @book: an #EBook
 * @id: a unique ID string specifying the contact
 * @cb: a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Removes the contact with id @id from @book.
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 **/
guint
e_book_async_remove_contact_by_id (EBook                 *book,
				   const char            *id,
				   EBookCallback          cb,
				   gpointer               closure)
{
	GList *list;

	g_return_val_if_fail (E_IS_BOOK (book), TRUE);
	g_return_val_if_fail (id != NULL, TRUE);

	list = g_list_append (NULL, g_strdup (id));

	return e_book_async_remove_contacts (book, list, cb, closure);
}

/**
 * e_book_async_remove_contacts:
 * @book: an #EBook
 * @ids: a #GList of const char *id's
 * @cb: a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Removes the contacts with ids from the list @ids from @book.  This is
 * always more efficient than calling e_book_remove_contact_by_id() if you
 * have more than one id to remove, as some backends can implement it
 * as a batch request.
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 **/
guint
e_book_async_remove_contacts (EBook                 *book,
			      GList                 *ids,
			      EBookCallback          cb,
			      gpointer               closure)
{
	g_return_val_if_fail (book && E_IS_BOOK (book), TRUE);
	g_return_val_if_fail (ids,                      TRUE);

	return !do_remove_contacts (FALSE,
				    book, ids, NULL,
				    cb, closure);
}


static gboolean
do_get_book_view (gboolean sync,
		  EBook *book,
		  EBookQuery *query,
		  GList *requested_fields,
		  int max_results,
		  EBookView **book_view,
		  GError **error,
		  EBookBookViewCallback cb,
		  gpointer closure)
{
	GNOME_Evolution_Addressbook_stringlist stringlist;
	CORBA_Environment ev;
	EBookOp *our_op;
	EBookStatus status;
	int num_fields, i;
	GList *iter;
	char *query_string;

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_SOURCE_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_SOURCE_NOT_LOADED,
				     _("\"%s\" on book before \"%s\""),
				     "e_book_get_book_view", "e_book_open");
		}
		else {
			g_warning (_("\"%s\" on book before \"%s\""),
				   "e_book_get_book_view", "e_book_open");
		}
		return FALSE;
	}

	if (sync && e_book_get_current_sync_op (book) != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book, sync);

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

	our_op->cb.book_view = cb;
	our_op->closure = closure;

	/* will eventually end up calling e_book_response_get_book_view */
	GNOME_Evolution_Addressbook_Book_getBookView (book->priv->corba_book,
						      our_op->opid,
						      bonobo_object_corba_objref(BONOBO_OBJECT(our_op->listener)),
						      query_string,
						      &stringlist, max_results, &ev);

	CORBA_free(stringlist._buffer);
	g_free (query_string);

	if (ev._major != CORBA_NO_EXCEPTION) {

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		CORBA_exception_free (&ev);

		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
				     _("CORBA exception making \"%s\" call"),
				     "Book::authenticateUser");
		}
		else {
			g_warning (_("CORBA exception making \"%s\" call"),
				   "Book::authenticateUser");
		}

		return FALSE;
	}

	CORBA_exception_free (&ev);

	if (sync) {
		/* wait for something to happen (both cancellation and a
		   successful response will notity us via our cv */
		e_flag_wait (our_op->flag);

		status = our_op->status;
		*book_view = our_op->view;

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		E_BOOK_CHECK_STATUS (status, error);
	}
	else {
		return TRUE;
	}
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
 * error, @error is set and %FALSE returned.
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 **/
gboolean
e_book_get_book_view (EBook       *book,
		      EBookQuery  *query,
		      GList       *requested_fields,
		      int          max_results,
		      EBookView  **book_view,
		      GError     **error)
{
	e_return_error_if_fail (book && E_IS_BOOK (book),       E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (query,                          E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (book_view,                      E_BOOK_ERROR_INVALID_ARG, FALSE);

	return do_get_book_view (TRUE,
				 book, query, requested_fields, max_results, book_view, error,
				 NULL, NULL);
}

/**
 * e_book_async_get_book_view:
 * @book: an #EBook
 * @query: an #EBookQuery
 * @requested_fields: a #GList containing the names of fields to return, or NULL for all
 * @max_results: the maximum number of contacts to show (or 0 for all)
 * @cb: a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Query @book with @query, creating a #EBookView with the fields
 * specified by @requested_fields and limited at @max_results records.
 *
 * Return value: %FALSE if successful, %TRUE otherwise
 **/
guint
e_book_async_get_book_view (EBook                 *book,
			    EBookQuery            *query,
			    GList                 *requested_fields,
			    int                    max_results,
			    EBookBookViewCallback  cb,
			    gpointer               closure)
{
	g_return_val_if_fail (book && E_IS_BOOK (book),       TRUE);
	g_return_val_if_fail (query,                          TRUE);

	return !do_get_book_view (FALSE,
				  book, query, requested_fields, max_results, NULL, NULL,
				  cb, closure);
}

static gboolean
emit_async_get_book_view_response (gpointer data)
{
	EBookOp *op = data;
	EBook *book = op->book;

	if (op->cb.book_view)
		op->cb.book_view (book, op->status, op->view, op->closure);

	g_mutex_lock (book->priv->mutex);

	g_object_unref (op->view);

	book->priv->pending_idles = g_list_remove (book->priv->pending_idles,
						   GINT_TO_POINTER (op->idle_id));

	e_book_clear_op (book, op);

	g_mutex_unlock (book->priv->mutex);
	g_object_unref (book);

	return FALSE;
}

static void
e_book_response_get_book_view (EBook       *book,
			       guint32      opid,
			       EBookStatus  status,
			       GNOME_Evolution_Addressbook_BookView corba_book_view)
{

	EBookOp *op;

	d(printf ("e_book_response_get_book_view\n"));

	g_mutex_lock (book->priv->mutex);

	op = e_book_get_op (book, opid);

	if (op == NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_warning ("e_book_response_get_book_view: Cannot find operation ");
		return;
	}

	op->status = status;
	op->view = e_book_view_new (corba_book_view, op->listener);
	e_book_view_set_book (op->view, book);

	bonobo_object_unref(BONOBO_OBJECT(op->listener));

	if (op->synchronous)
		e_flag_set (op->flag);
	else {
		op->book = g_object_ref (book);

		op->idle_id = g_idle_add (emit_async_get_book_view_response, op);
		book->priv->pending_idles = g_list_prepend (book->priv->pending_idles,
							    GINT_TO_POINTER (op->idle_id));
	}

	g_mutex_unlock (book->priv->mutex);
}

static gboolean
do_get_contacts (gboolean sync,
		 EBook *book,
		 EBookQuery *query,
		 GList **contacts,
		 GError **error,
		 EBookListCallback cb,
		 gpointer closure)
{
	CORBA_Environment ev;
	EBookOp *our_op;
	EBookStatus status;
	char *query_string;

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_SOURCE_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_SOURCE_NOT_LOADED,
				     _("\"%s\" on book before \"%s\""),
				     "e_book_get_contacts", "e_book_open");
		}
		else {
			g_warning (_("\"%s\" on book before \"%s\""),
				   "e_book_get_contacts", "e_book_open");
		}

		return FALSE;
	}

	if (sync && e_book_get_current_sync_op (book) != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book, sync);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	query_string = e_book_query_to_string (query);

	our_op->cb.list = cb;
	our_op->closure = closure;

	/* will eventually end up calling e_book_response_get_contacts */
	GNOME_Evolution_Addressbook_Book_getContactList (book->priv->corba_book, our_op->opid, query_string, &ev);

	g_free (query_string);

	if (ev._major != CORBA_NO_EXCEPTION) {

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		CORBA_exception_free (&ev);

		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
				     _("CORBA exception making \"%s\" call"),
				     "Book::getContactList");
		}
		else {
			g_warning (_("CORBA exception making \"%s\" call"),
				   "Book::getContactList");
		}

		return FALSE;
	}

	CORBA_exception_free (&ev);

	if (sync) {
		/* wait for something to happen (both cancellation and a
		   successful response will notity us via our cv */
		e_flag_wait (our_op->flag);

		status = our_op->status;
		*contacts = our_op->list;

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		E_BOOK_CHECK_STATUS (status, error);
	}
	else {
		return TRUE;
	}
}

/**
 * e_book_get_contacts:
 * @book: an #EBook
 * @query: an #EBookQuery
 * @contacts: a #GList pointer, will be set to the list of contacts
 * @error: a #GError to set on failure
 *
 * Query @book with @query, setting @contacts to the list of contacts which
 * matched. On failed, @error will be set and %FALSE returned.
 *
 * Return value: %TRUE on success, %FALSE otherwise
 **/
gboolean
e_book_get_contacts (EBook       *book,
		     EBookQuery  *query,
		     GList      **contacts,
		     GError     **error)
{
	e_return_error_if_fail (book && E_IS_BOOK (book),       E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (query,                          E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (contacts,                       E_BOOK_ERROR_INVALID_ARG, FALSE);

	return do_get_contacts (TRUE,
				book, query, contacts, error,
				NULL, NULL);
}

/**
 * e_book_async_get_contacts:
 * @book: an #EBook
 * @query: an #EBookQuery
 * @cb: a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Query @book with @query.
 *
 * Return value: %FALSE on success, %TRUE otherwise
 **/
guint
e_book_async_get_contacts (EBook             *book,
			   EBookQuery        *query,
			   EBookListCallback  cb,
			   gpointer           closure)
{
	g_return_val_if_fail (book && E_IS_BOOK (book),       TRUE);
	g_return_val_if_fail (query,                          TRUE);

	return !do_get_contacts (FALSE,
				 book, query, NULL, NULL,
				 cb, closure);
}

static gboolean
emit_async_get_contacts_response (gpointer data)
{
	EBookOp *op = data;
	EBook *book = op->book;

	if (op->cb.list)
		op->cb.list (book, op->status, op->list, op->closure);

	g_mutex_lock (book->priv->mutex);

	g_list_foreach (op->list, (GFunc)g_object_unref, NULL);
	g_list_free (op->list);

	book->priv->pending_idles = g_list_remove (book->priv->pending_idles,
						   GINT_TO_POINTER (op->idle_id));

	e_book_clear_op (book, op);

	g_mutex_unlock (book->priv->mutex);
	g_object_unref (book);

	return FALSE;
}


static void
e_book_response_get_contacts (EBook       *book,
			      guint32      opid,
			      EBookStatus  status,
			      GList       *contact_list)
{

	EBookOp *op;

	g_mutex_lock (book->priv->mutex);

	op = e_book_get_op (book, opid);

	if (op == NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_warning ("e_book_response_get_contacts: Cannot find operation ");
		return;
	}

	op->list = g_list_copy (contact_list);
	g_list_foreach (op->list, (GFunc)g_object_ref, NULL);
	op->status = status;

	if (op->synchronous)
		e_flag_set (op->flag);
	else {
		op->book = g_object_ref (book);
		op->idle_id = g_idle_add (emit_async_get_contacts_response, op);
		book->priv->pending_idles = g_list_prepend (book->priv->pending_idles,
							    GINT_TO_POINTER (op->idle_id));
	}

	g_mutex_unlock (book->priv->mutex);
}


static gboolean
do_get_changes (gboolean sync,
		EBook *book,
		char *changeid,
		GList **changes,
		GError **error,
		EBookListCallback cb,
		gpointer closure)
{
	CORBA_Environment ev;
	EBookOp *our_op;
	EBookStatus status;

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_SOURCE_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_SOURCE_NOT_LOADED,
				     _("\"%s\" on book before \"%s\""),
				     "e_book_get_changes", "e_book_open");
		}
		else {
			g_warning (_("\"%s\" on book before \"%s\""),
				   "e_book_get_changes", "e_book_open");
		}
		return FALSE;
	}

	if (sync && e_book_get_current_sync_op (book) != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book, sync);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	our_op->cb.list = cb;
	our_op->closure = closure;

	/* will eventually end up calling e_book_response_get_changes */
	GNOME_Evolution_Addressbook_Book_getChanges (book->priv->corba_book, our_op->opid, changeid, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		CORBA_exception_free (&ev);

		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
				     _("CORBA exception making \"%s\" call"),
				     "Book::getChanges");
		}
		else {
			g_warning (_("CORBA exception making \"%s\" call"),
				   "Book::getChanges");
		}

		return FALSE;
	}

	CORBA_exception_free (&ev);

	if (sync) {
		/* wait for something to happen (both cancellation and a
		   successful response will notity us via our cv */
		e_flag_wait (our_op->flag);

		status = our_op->status;
		*changes = our_op->list;

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		E_BOOK_CHECK_STATUS (status, error);
	}
	else {
		return TRUE;
	}
}

/**
 * e_book_get_changes:
 * @book: an #EBook
 * @changeid:  the change ID
 * @changes: return location for a #GList of #EBookChange items
 * @error: a #GError to set on failure.
 *
 * Get the set of changes since the previous call to #e_book_get_changes for a
 * given change ID.
 *
 * Return value: TRUE on success, FALSE otherwise
 */
gboolean
e_book_get_changes (EBook       *book,
		    char        *changeid,
		    GList      **changes,
		    GError     **error)
{
	e_return_error_if_fail (book && E_IS_BOOK (book),       E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (changeid,                       E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (changes,                        E_BOOK_ERROR_INVALID_ARG, FALSE);

	return do_get_changes (TRUE,
			       book, changeid, changes, error,
			       NULL, NULL);
}

/**
 * e_book_async_get_changes:
 * @book: an #EBook
 * @changeid:  the change ID
 * @cb: function to call when operation finishes
 * @closure: data to pass to callback function
 *
 * Get the set of changes since the previous call to #e_book_async_get_changes
 * for a given change ID.
 *
 * Return value: TRUE on success, FALSE otherwise
 */
guint
e_book_async_get_changes (EBook             *book,
			  char              *changeid,
			  EBookListCallback  cb,
			  gpointer           closure)
{
	g_return_val_if_fail (book && E_IS_BOOK (book),       TRUE);
	g_return_val_if_fail (changeid,                       TRUE);

	return !do_get_changes (FALSE,
				book, changeid, NULL, NULL,
				cb, closure);
}

static gboolean
emit_async_get_changes_response (gpointer data)
{
	EBookOp *op = data;
	EBook *book = op->book;

	if (op->cb.list)
		op->cb.list (book, op->status, op->list, op->closure);

	g_mutex_lock (book->priv->mutex);

	e_book_free_change_list (op->list);

	book->priv->pending_idles = g_list_remove (book->priv->pending_idles,
						   GINT_TO_POINTER (op->idle_id));

	e_book_clear_op (book, op);

	g_mutex_unlock (book->priv->mutex);
	g_object_unref (book);

	return FALSE;
}

static void
e_book_response_get_changes (EBook       *book,
			     guint32      opid,
			     EBookStatus  status,
			     GList       *change_list)
{

	EBookOp *op;

	g_mutex_lock (book->priv->mutex);

	op = e_book_get_op (book, opid);

	if (op == NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_warning ("e_book_response_get_changes: Cannot find operation ");
		return;
	}

	op->status = status;
	op->list = change_list;

	if (op->synchronous)
		e_flag_set (op->flag);
	else {
		op->book = g_object_ref (book);
		op->idle_id = g_idle_add (emit_async_get_changes_response, op);
		book->priv->pending_idles = g_list_prepend (book->priv->pending_idles,
							    GINT_TO_POINTER (op->idle_id));
	}

	g_mutex_unlock (book->priv->mutex);
}

/**
 * e_book_free_change_list:
 * @change_list: a #GList of #EBookChange items
 *
 * Free the contents of #change_list, and the list itself.
 */
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



static gboolean
emit_async_generic_response (gpointer data)
{
	EBookOp *op = data;
	EBook *book = op->book;

	if (op->cb.status)
		op->cb.status (book, op->status, op->closure);

	g_mutex_lock (book->priv->mutex);

	book->priv->pending_idles = g_list_remove (book->priv->pending_idles,
						   GINT_TO_POINTER (op->idle_id));

	e_book_clear_op (book, op);

	g_mutex_unlock (book->priv->mutex);
	g_object_unref (book);

	return FALSE;
}

static void
e_book_response_generic (EBook       *book,
			 guint32      opid,
			 EBookStatus  status)
{
	EBookOp *op;
	d(printf("e_book_response_generic\n"));
	g_mutex_lock (book->priv->mutex);

	op = e_book_get_op (book, opid);

	if (op == NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_warning ("e_book_response_generic: Cannot find operation ");
		return;
	}

	op->status = status;
	if (op->synchronous)
		e_flag_set (op->flag);
	else {
		op->book = g_object_ref (book);
		op->idle_id = g_idle_add (emit_async_generic_response, op);
		book->priv->pending_idles = g_list_prepend (book->priv->pending_idles,
							    GINT_TO_POINTER (op->idle_id));
	}

	g_mutex_unlock (book->priv->mutex);
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
 * Return value: %TRUE on success, %FALSE otherwise
 **/
gboolean
e_book_cancel (EBook   *book,
	       GError **error)
{
	EBookOp *op;
	EBookStatus status;
	gboolean rv;
	CORBA_Environment ev;

	e_return_error_if_fail (book && E_IS_BOOK (book),       E_BOOK_ERROR_INVALID_ARG, FALSE);

	g_mutex_lock (book->priv->mutex);

	if (e_book_get_current_sync_op (book) == NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_COULD_NOT_CANCEL,
			     _("%s: there is no current operation"), "e_book_cacnel");
		return FALSE;
	}

	op = e_book_get_current_sync_op (book);

	g_mutex_unlock (book->priv->mutex);

	status = GNOME_Evolution_Addressbook_Book_cancelOperation(book->priv->corba_book, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		CORBA_exception_free (&ev);

		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
			     _("CORBA exception making \"%s\" call"),
			     "Book::cancelOperation");
		return FALSE;
	}

	CORBA_exception_free (&ev);

	if (status == E_BOOK_ERROR_OK) {
		op->status = E_BOOK_ERROR_CANCELLED;
		e_flag_set (op->flag);
		rv = TRUE;
	}
	else {
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_COULD_NOT_CANCEL,
			     _("%s: could not cancel"), "e_book_cancel");
		rv = FALSE;
	}

	return rv;
}



static gboolean
do_open (gboolean sync,
	 EBook *book,
	 gboolean only_if_exists,
	 GError **error,
	 EBookCallback cb,
	 gpointer closure)
{
	CORBA_Environment ev;
	EBookOp *our_op;
	EBookStatus status;

	g_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != E_BOOK_SOURCE_NOT_LOADED) {
		g_mutex_unlock (book->priv->mutex);
		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_SOURCE_ALREADY_LOADED,
				     _("\"%s\" on book after \"%s\""),
				     "e_book_open", "e_book_open");
		}
		else {
			g_warning (_("\"%s\" on book after \"%s\""),
				   "e_book_open", "e_book_open");
		}

		return FALSE;
	}

	if (sync && e_book_get_current_sync_op (book) != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book, sync);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	our_op->cb.status = cb;
	our_op->closure = closure;

	/* will eventually end up calling e_book_response_remove */
	GNOME_Evolution_Addressbook_Book_open (book->priv->corba_book, our_op->opid, only_if_exists, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		CORBA_exception_free (&ev);

		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
				     _("CORBA exception making \"%s\" call"),
				     "Book::open");
		}
		else {
			g_warning (_("CORBA exception making \"%s\" call"),
				   "Book::open");
		}

		return FALSE;
	}

	CORBA_exception_free (&ev);

	if (sync) {
		/* wait for something to happen (both cancellation and a
		   successful response will notity us via our cv */
		e_flag_wait (our_op->flag);

		status = our_op->status;

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		if (status == E_BOOK_ERROR_CANCELLED) {
			/* Cancelled */
			book->priv->load_state = E_BOOK_SOURCE_NOT_LOADED;
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CANCELLED,
				     _("%s: canceled"), "e_book_open");
			return FALSE;
		}
		else if (status == E_BOOK_ERROR_OK) {
			book->priv->load_state = E_BOOK_SOURCE_LOADED;
			return TRUE;
		}
		else {
			E_BOOK_CHECK_STATUS (status, error);
		}
	}
	else {
		return TRUE;
	}
}

/**
 * e_book_open:
 * @book: an #EBook
 * @only_if_exists: if %TRUE, fail if this book doesn't already exist, otherwise create it first
 * @error: a #GError to set on failure
 *
 * Opens the addressbook, making it ready for queries and other operations.
 *
 * Return value: %TRUE if the book was successfully opened, %FALSE otherwise.
 */
gboolean
e_book_open (EBook     *book,
	     gboolean   only_if_exists,
	     GError   **error)
{
	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG, FALSE);

	return do_open (TRUE,
			book, only_if_exists, error,
			NULL, NULL);
}

/**
 * e_book_async_open:
 * @book: an #EBook
 * @only_if_exists: if %TRUE, fail if this book doesn't already exist, otherwise create it first
 * @open_response: a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Opens the addressbook, making it ready for queries and other operations.
 * This function does not block.
 *
 * Return value: %FALSE if successful, %TRUE otherwise.
 **/
guint
e_book_async_open (EBook                 *book,
		   gboolean               only_if_exists,
		   EBookCallback          open_response,
		   gpointer               closure)
{
	g_return_val_if_fail (book && E_IS_BOOK (book), FALSE);

	return !do_open (FALSE,
			 book, only_if_exists, NULL,
			 open_response, closure);
}

static gboolean
emit_async_open_response (gpointer data)
{
	EBookOp *op = data;
	EBook *book = op->book;

	d(printf ("in async_open_response\n"));

	g_mutex_lock (book->priv->mutex);

	if (op->status == E_BOOK_ERROR_OK)
		book->priv->load_state = E_BOOK_SOURCE_LOADED;
	else
		book->priv->load_state = E_BOOK_SOURCE_NOT_LOADED;

	g_mutex_unlock (book->priv->mutex);

	if (op->cb.status)
		op->cb.status (book, op->status, op->closure);

	g_mutex_lock (book->priv->mutex);

	book->priv->pending_idles = g_list_remove (book->priv->pending_idles,
						   GINT_TO_POINTER (op->idle_id));

	e_book_clear_op (book, op);

	g_mutex_unlock (book->priv->mutex);
	g_object_unref (book);

	return FALSE;
}

static void
e_book_response_open (EBook       *book,
		      guint32      opid,
		      EBookStatus  status)
{
	EBookOp *op;

	d(printf ("in e_book_response_open\n"));

	g_mutex_lock (book->priv->mutex);

	op = e_book_get_op (book, opid);

	if (op == NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_warning ("e_book_response_open: Cannot find operation ");
		return;
	}

	op->status = status;
	if (op->synchronous)
		e_flag_set (op->flag);
	else {
		op->book = g_object_ref (book);
		op->idle_id = g_idle_add (emit_async_open_response, op);
		book->priv->pending_idles = g_list_prepend (book->priv->pending_idles,
							    GINT_TO_POINTER (op->idle_id));
	}

	g_mutex_unlock (book->priv->mutex);
}


static gboolean
do_remove (gboolean sync,
	   EBook *book,
	   GError **error,
	   EBookCallback cb,
	   gpointer closure)
{
	CORBA_Environment ev;
	EBookOp *our_op;
	EBookStatus status;

	g_mutex_lock (book->priv->mutex);

	if (sync && e_book_get_current_sync_op (book) != NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_BUSY,
			     _("book busy"));
		return FALSE;
	}

	our_op = e_book_new_op (book, sync);

	g_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	our_op->cb.status = cb;
	our_op->closure = closure;

	/* will eventually end up calling e_book_response_remove */
	GNOME_Evolution_Addressbook_Book_remove (book->priv->corba_book, our_op->opid, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		CORBA_exception_free (&ev);

		if (error) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION,
				     _("CORBA exception making \"%s\" call"),
				     "Book::remove");
		}
		else {
			g_warning (_("CORBA exception making \"%s\" call"),
				   "Book::remove");
		}

		return FALSE;
	}

	CORBA_exception_free (&ev);

	if (sync) {
		/* wait for something to happen (both cancellation and a
		   successful response will notity us via our cv */
		e_flag_wait (our_op->flag);

		status = our_op->status;

		g_mutex_lock (book->priv->mutex);
		e_book_clear_op (book, our_op);
		g_mutex_unlock (book->priv->mutex);

		E_BOOK_CHECK_STATUS (status, error);
	}
	else {
		return TRUE;
	}
}

/**
 * e_book_remove:
 * @book: an #EBook
 * @error: a #GError to set on failure
 *
 * Removes the backing data for this #EBook. For example, with the file backend this
 * deletes the database file. You cannot get it back!
 *
 * Return value: %TRUE on success, %FALSE on failure.
 */
gboolean
e_book_remove (EBook   *book,
	       GError **error)
{
	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG, FALSE);

	return do_remove (TRUE,
			  book, error,
			  NULL, NULL);

}

/**
 * e_book_async_remove:
 * @book: an #EBook
 * @cb: a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Remove the backing data for this #EBook. For example, with the file backend this
 * deletes the database file. You cannot get it back!
 *
 * Return value: %FALSE if successful, %TRUE otherwise.
 **/
guint
e_book_async_remove (EBook   *book,
		     EBookCallback cb,
		     gpointer closure)
{
	g_return_val_if_fail (book && E_IS_BOOK (book), TRUE);

	return !do_remove (FALSE,
			   book, NULL,
			   cb, closure);

}

static void
e_book_response_remove (EBook       *book,
			guint32      opid,
			EBookStatus  status)
{
	EBookOp *op;

	d(printf ("e_book_response_remove\n"));

	g_mutex_lock (book->priv->mutex);

	op = e_book_get_op (book, opid);

	if (op == NULL) {
		g_mutex_unlock (book->priv->mutex);
		g_warning ("e_book_response_remove: Cannot find operation ");
		return;
	}

	op->status = status;
	if (op->synchronous)
		e_flag_set (op->flag);
	else {
		op->book = g_object_ref (book);
		op->idle_id = g_idle_add (emit_async_generic_response, op);
		book->priv->pending_idles = g_list_prepend (book->priv->pending_idles,
							    GINT_TO_POINTER (op->idle_id));
	}

	g_mutex_unlock (book->priv->mutex);
}

static gboolean
e_book_idle_writable (gpointer data)
{
	EBook *book = data;
	gboolean writable;

	g_mutex_lock (book->priv->mutex);
	writable = book->priv->writable;
	book->priv->writable_idle_id = 0;
	g_mutex_unlock (book->priv->mutex);

	g_signal_emit (G_OBJECT (book), e_book_signals [WRITABLE_STATUS], 0, writable);

	g_object_unref (book);

	return FALSE;
}

static gboolean
e_book_idle_connection (gpointer data)
{
	EBook *book = data;
	gboolean connected;

	if (!book->priv)
		return FALSE;

	g_mutex_lock (book->priv->mutex);
	connected = book->priv->connected;
	book->priv->connection_idle_id = 0;
	g_mutex_unlock (book->priv->mutex);

	g_signal_emit (G_OBJECT (book), e_book_signals [CONNECTION_STATUS], 0, connected);

	g_object_unref (book);

	return FALSE;
}

static gboolean
e_book_idle_auth_required (gpointer data)
{
	EBook *book = data;
	gboolean connected;

	if (!book->priv)
		return FALSE;

	g_mutex_lock (book->priv->mutex);
	connected = book->priv->connected;
	book->priv->auth_idle_id = 0;
	g_mutex_unlock (book->priv->mutex);

	g_signal_emit (G_OBJECT (book), e_book_signals [AUTH_REQUIRED], 0);

	g_object_unref (book);

	return FALSE;


}

static void
e_book_handle_response (EBookListener *listener, EBookListenerResponse *resp, EBook *book)
{
	EContact *contact;

	switch (resp->op) {
	case CreateContactResponse:
		e_book_response_add_contact (book, resp->opid, resp->status, resp->id);
		break;
	case RemoveContactResponse:
	case ModifyContactResponse:
	case AuthenticationResponse:
		e_book_response_generic (book, resp->opid, resp->status);
		break;
	case GetContactResponse:
		contact = e_contact_new_from_vcard (resp->vcard);
		e_book_response_get_contact (book, resp->opid, resp->status, contact);
		break;
	case GetContactListResponse:
		e_book_response_get_contacts (book, resp->opid, resp->status, resp->list);
		break;
	case GetBookViewResponse:
		e_book_response_get_book_view(book, resp->opid, resp->status, resp->book_view);
		break;
	case GetChangesResponse:
		e_book_response_get_changes(book, resp->opid, resp->status, resp->list);
		break;
	case OpenBookResponse:
		e_book_response_open (book, resp->opid, resp->status);
		break;
	case RemoveBookResponse:
		e_book_response_remove (book, resp->opid, resp->status);
		break;
	case GetSupportedFieldsResponse:
		e_book_response_get_supported_fields (book, resp->opid, resp->status, resp->list);
		break;
	case GetRequiredFieldsResponse:
		e_book_response_get_required_fields (book, resp->opid, resp->status, resp->list);
		break;
	case GetSupportedAuthMethodsResponse:
		e_book_response_get_supported_auth_methods (book, resp->opid, resp->status, resp->list);
		break;
	case WritableStatusEvent:
		book->priv->writable = resp->writable;

		g_mutex_lock (book->priv->mutex);
		if (book->priv->writable_idle_id == 0) {
			g_object_ref (book);
			book->priv->writable_idle_id = g_idle_add (e_book_idle_writable, book);
		}
		g_mutex_unlock (book->priv->mutex);

		break;
	case LinkStatusEvent:
		book->priv->connected = resp->connected;
		g_mutex_lock (book->priv->mutex);
		if (book->priv->connection_idle_id == 0) {
			g_object_ref (book);
			book->priv->connection_idle_id = g_idle_add (e_book_idle_connection, book);
		}
		g_mutex_unlock (book->priv->mutex);
		break;
	case AuthRequiredEvent:
		g_mutex_lock (book->priv->mutex);
		if (book->priv->auth_idle_id == 0) {
			g_object_ref (book);
			book->priv->auth_idle_id = g_idle_add (e_book_idle_auth_required, book);
		}
		g_mutex_unlock (book->priv->mutex);
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
 * Return value: %TRUE on success, %FALSE otherwise.
 */
static gboolean
e_book_unload_uri (EBook   *book,
		   GError **error)
{
	CORBA_Environment ev;

	e_return_error_if_fail (book && E_IS_BOOK (book), E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (book->priv->load_state != E_BOOK_SOURCE_NOT_LOADED, E_BOOK_ERROR_SOURCE_NOT_LOADED, FALSE);

	if (book->priv->load_state == E_BOOK_SOURCE_LOADED) {
		/* Release the remote GNOME_Evolution_Addressbook_Book in the PAS. */
		CORBA_exception_init (&ev);

		bonobo_object_release_unref  (book->priv->corba_book, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("e_book_unload_uri: Exception releasing "
				   "remote book interface!\n");
		}

		CORBA_exception_free (&ev);

		book->priv->corba_book = CORBA_OBJECT_NIL;
		e_book_listener_stop (book->priv->listener);
		bonobo_object_unref (BONOBO_OBJECT (book->priv->listener));

		book->priv->listener   = NULL;
		book->priv->load_state = E_BOOK_SOURCE_NOT_LOADED;
		g_free (book->priv->cap);
		book->priv->cap = NULL;
		book->priv->cap_queried = FALSE;
		book->priv->writable = FALSE;
	}
	else if (book->priv->load_state == E_BOOK_SOURCE_LOADING) {
		e_book_cancel (book, error);
	}

	return TRUE;
}




/* Set a flag for operation from the operation hash table */
static void
find_key_value (gpointer key, gpointer value, gpointer data)
{
	EBookOp *op;

	op = value;

	if (op == NULL) {
		g_warning ("find_key_value: Cannot find operation ");
		return;
	}

	op->status = E_BOOK_ERROR_SOURCE_NOT_LOADED;
	if (op->synchronous)
		e_flag_set (op->flag);
}

/**
 * e_book_load_uri:
 */

static void
backend_died_cb (EComponentListener *cl, gpointer user_data)
{
	EBook *book = (EBook *)user_data;
	
	d(printf ("backend_died_cb\n"));	

	g_mutex_lock (book->priv->mutex);
	g_hash_table_foreach (book->priv->id_to_op, find_key_value, NULL);
	g_mutex_unlock (book->priv->mutex);

	book->priv->load_state = E_BOOK_SOURCE_NOT_LOADED;
        g_signal_emit (book, e_book_signals [BACKEND_DIED], 0);
}

static GList *
activate_factories_for_uri (EBook *book, const char *uri)
{
	CORBA_Environment ev;
	Bonobo_ServerInfoList *info_list = NULL;
	int i;
	char *query;
	GList *factories = NULL;

	query = "repo_ids.has ('IDL:GNOME/Evolution/DataServer/BookFactory:" API_VERSION "')";

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
		g_warning ("Can't find installed BookFactories");
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

	return factories;
}

/* XXX hm, should this function hold a lock on book->priv->mutex?  It
   doesn't seem to require it, but there are unlocked writes to
   book->priv->load_state, which other functions read.. */
static gboolean
fetch_corba_book (EBook       *book,
		  ESource     *source,
		  GError     **error)
{
	GNOME_Evolution_Addressbook_Book corba_book = CORBA_OBJECT_NIL;
	gchar *uri;
	gchar *source_xml;
	GList *factories;
	GList *l;
	gboolean rv = FALSE;

	uri = e_source_get_uri (source);
	if (!uri) {
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_OTHER_ERROR,
			     _("%s: Invalid source."), "e_book_load_uri");
		return FALSE;
	}

	/* try to find a list of factories that can handle the protocol */
	factories = activate_factories_for_uri (book, uri);
	if (!factories) {
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_PROTOCOL_NOT_SUPPORTED,
			     _("%s: no factories available for URI `%s'"), "e_book_load_uri", uri);
		return FALSE;
	}


	/*
	 * Create our local BookListener interface.
	 */
	book->priv->listener = e_book_listener_new ();
	if (book->priv->listener == NULL) {
		g_warning ("e_book_load_uri: Could not create EBookListener!\n");
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_OTHER_ERROR,
			     _("%s: Could not create EBookListener"), "e_book_load_uri");
		return FALSE;
	}
	book->priv->listener_signal = g_signal_connect_object (book->priv->listener, "response",
							       G_CALLBACK (e_book_handle_response),
							       book, 0);

	g_free (book->priv->uri);
	book->priv->uri = uri;

	g_object_ref (source);
	if (book->priv->source)
		g_object_unref (book->priv->source);
	book->priv->source = source;

	source_xml = e_source_to_standalone_xml (source);

	for (l = factories; l; l = l->next) {
		GNOME_Evolution_Addressbook_BookFactory factory = l->data;
		CORBA_Environment ev;

		CORBA_exception_init (&ev);

		corba_book = GNOME_Evolution_Addressbook_BookFactory_getBook (factory, source_xml,
									      bonobo_object_corba_objref (BONOBO_OBJECT (book->priv->listener)),
									      &ev);

		if (ev._major != CORBA_NO_EXCEPTION) {
			CORBA_exception_free (&ev);
			continue;
		}
		else if (corba_book != CORBA_OBJECT_NIL) {
			rv = TRUE;
			break;
		}
	}

	g_free (source_xml);

	/* free up the factories */
	for (l = factories; l; l = l->next)
		CORBA_Object_release ((CORBA_Object)l->data, NULL);

	g_list_free (factories);

	if (rv == TRUE) {
		book->priv->corba_book = corba_book;
		book->priv->comp_listener = e_component_listener_new (book->priv->corba_book);
		book->priv->died_signal = g_signal_connect (book->priv->comp_listener,
							    "component_died",
							    G_CALLBACK (backend_died_cb), book);
	}
	else {
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_PROTOCOL_NOT_SUPPORTED,
			     _("%s: no factories available for URI `%s'"), "e_book_new", uri);
	}

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

		if (book->priv->load_state != E_BOOK_SOURCE_LOADED) {
			g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_SOURCE_NOT_LOADED,
				     _("\"%s\" on book before \"%s\""),
				     "e_book_get_static_capabilities", "e_book_open");
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
 * Return value: %TRUE if the backend supports @cap, %FALSE otherwise.
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
 * e_book_is_opened:
 * @book: and #EBook
 *
 * Check if this book has been opened.
 *
 * Return value: %TRUE if this book has been opened, otherwise %FALSE.
 */
gboolean
e_book_is_opened (EBook *book)
{
	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	if (book->priv->load_state != E_BOOK_SOURCE_LOADED)
		return FALSE;

	return TRUE;
}

/**
 * e_book_is_writable:
 * @book: an #EBook
 *
 * Check if this book is writable.
 *
 * Return value: %TRUE if this book is writable, otherwise %FALSE.
 */
gboolean
e_book_is_writable (EBook *book)
{
	g_return_val_if_fail (book && E_IS_BOOK (book), FALSE);

	return book->priv->writable;
}

/**
 * e_book_is_online:
 * @book: an #EBook
 *
 * Check if this book is connected.
 *
 * Return value: %TRUE if this book is connected, otherwise %FALSE.
 **/
gboolean
e_book_is_online (EBook *book)
{
	g_return_val_if_fail (book && E_IS_BOOK (book), FALSE);

	return book->priv->connected;

}

#define SELF_UID_KEY "/apps/evolution/addressbook/self/self_uid"

/**
 * e_book_get_self:
 * @contact: an #EContact pointer to set
 * @book: an #EBook pointer to set
 * @error: a #GError to set on failure
 *
 * Get the #EContact referring to the user of the address book
 * and set it in @contact and @book.
 *
 * Return value: %TRUE if successful, otherwise %FALSE.
 **/
gboolean
e_book_get_self (EContact **contact, EBook **book, GError **error)
{
	GError *e = NULL;
	GConfClient *gconf;
	gboolean status;
	char *uid;

	*book = e_book_new_system_addressbook (&e);

	if (!*book) {
		if (error)
			g_propagate_error (error, e);
		return FALSE;
	}

	status = e_book_open (*book, FALSE, &e);
	if (status == FALSE) {
		g_object_unref (*book);
		*book = NULL;
		if (error)
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
			     _("%s: there was no self contact UID stored in gconf"), "e_book_get_self");
		return FALSE;
	}

	if (!e_book_get_contact (*book, uid, contact, &e)) {
		g_object_unref (*book);
		*book = NULL;
		g_free (uid);
		if (error)
			g_propagate_error (error, e);
		return FALSE;
	}

	g_free (uid);

	return TRUE;
}

/**
 * e_book_set_self:
 * @book: an #EBook
 * @contact: an #EContact
 * @error: a #GError to set on failure
 *
 * Specify that @contact residing in @book is the #EContact that
 * refers to the user of the address book.
 *
 * Return value: %TRUE if successful, %FALSE otherwise.
 **/
gboolean
e_book_set_self (EBook *book, EContact *contact, GError **error)
{
	GConfClient *gconf;

	e_return_error_if_fail (book && E_IS_BOOK (book),          E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (contact && E_IS_CONTACT (contact), E_BOOK_ERROR_INVALID_ARG, FALSE);

	gconf = gconf_client_get_default();
	gconf_client_set_string (gconf, SELF_UID_KEY, e_contact_get_const (contact, E_CONTACT_UID), NULL);
	g_object_unref (gconf);

	return TRUE;
}

/**
 * e_book_is_self:
 * @contact: an #EContact
 *
 * Check if @contact is the user of the address book.
 *
 * Return value: %TRUE if @contact is the user, %FALSE otherwise.
 **/
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

	rv = (uid && !strcmp (uid, e_contact_get_const (contact, E_CONTACT_UID)));

	g_free (uid);

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
 * Return value: %TRUE if the setting was stored in libebook's ESourceList, otherwise %FALSE.
 */
gboolean
e_book_set_default_addressbook (EBook *book, GError **error)
{
	ESource *source;

	e_return_error_if_fail (book && E_IS_BOOK (book),                        E_BOOK_ERROR_INVALID_ARG, FALSE);
	e_return_error_if_fail (book->priv->load_state == E_BOOK_SOURCE_NOT_LOADED, E_BOOK_ERROR_SOURCE_ALREADY_LOADED, FALSE);

	source = e_book_get_source (book);

	return e_book_set_default_source (source, error);
}


/**
 * e_book_set_default_source:
 * @source: An #ESource pointer
 * @error: A #GError pointer
 *
 * sets @source as the "default" addressbook.  This is the source that
 * will be loaded in the e_book_get_default_addressbook call.
 *
 * Return value: %TRUE if the setting was stored in libebook's ESourceList, otherwise %FALSE.
 */
gboolean
e_book_set_default_source (ESource *source, GError **error)
{
	ESourceList *sources;
	const char *uid;
	GError *err = NULL;
	GSList *g;

	e_return_error_if_fail (source && E_IS_SOURCE (source), E_BOOK_ERROR_INVALID_ARG, FALSE);

	uid = e_source_peek_uid (source);

	if (!e_book_get_addressbooks (&sources, &err)) {
		if (error)
			g_propagate_error (error, err);
		return FALSE;
	}

	/* make sure the source is actually in the ESourceList.  if
	   it's not we don't bother adding it, just return an error */
	source = e_source_list_peek_source_by_uid (sources, uid);
	if (!source) {
		g_set_error (error, E_BOOK_ERROR, E_BOOK_ERROR_NO_SUCH_SOURCE,
			     _("%s: there was no source for uid `%s' stored in gconf."), "e_book_set_default_source", uid);
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
		if (error)
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
 * Return value: %TRUE if @addressbook_sources was set, otherwise %FALSE.
 */
gboolean
e_book_get_addressbooks (ESourceList **addressbook_sources, GError **error)
{
	GConfClient *gconf;

	e_return_error_if_fail (addressbook_sources, E_BOOK_ERROR_INVALID_ARG, FALSE);

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
e_book_activate(void)
{
	static GStaticMutex e_book_lock = G_STATIC_MUTEX_INIT;
	static gboolean activated = FALSE;

	g_static_mutex_lock (&e_book_lock);
	if (!activated) {
		GThread *ebook_mainloop_thread;
		activated = TRUE;

		_ebook_context = g_main_context_new ();

		if (!bonobo_is_initialized ())
			bonobo_init (NULL, NULL);

		ebook_mainloop_thread = g_thread_create(startup_mainloop, NULL, FALSE, NULL);
	}
	g_static_mutex_unlock (&e_book_lock);
}


/**
 * e_book_new:
 * @source: An #ESource pointer
 * @error: A #GError pointer
 *
 * Creates a new #EBook corresponding to the given source.  There are
 * only two operations that are valid on this book at this point:
 * e_book_open(), and e_book_remove().
 *
 * Return value: a new but unopened #EBook.
 */
EBook*
e_book_new (ESource *source, GError **error)
{
	EBook *book;

	e_return_error_if_fail (source && E_IS_SOURCE (source), E_BOOK_ERROR_INVALID_ARG, NULL);

	e_book_activate ();

	book = g_object_new (E_TYPE_BOOK, NULL);

	book->priv->source = g_object_ref (source);

	if (!fetch_corba_book (book, source, error)) {
		g_object_unref (book);
		return NULL;
	}

	return book;
}

/**
 * e_book_new_from_uri:
 * @uri: the URI to load
 * @error: A #GError pointer
 *
 * Creates a new #EBook corresponding to the given uri.  See the
 * documentation for e_book_new for further information.
 *
 * Return value: a new but unopened #EBook.
 */
EBook*
e_book_new_from_uri (const char *uri, GError **error)
{
	ESourceGroup *group;
	ESource *source;
	EBook *book;

	e_return_error_if_fail (uri, E_BOOK_ERROR_INVALID_ARG, NULL);

	group = e_source_group_new ("", uri);
	source = e_source_new ("", "");
	e_source_set_group (source, group);
	e_source_set_relative_uri (source, NULL);

	book = e_book_new (source, error);

	g_object_unref (source);
	g_object_unref (group);

	return book;
}

/**
 * e_book_new_system_addressbook:
 * @error: A #GError pointer
 *
 * Creates a new #EBook corresponding to the user's system
 * addressbook.  See the documentation for e_book_new for further
 * information.
 *
 * Return value: a new but unopened #EBook.
 */
EBook*
e_book_new_system_addressbook    (GError **error)
{
	ESourceList *sources;
	GSList *g;
	GError *err = NULL;
	ESource *system_source = NULL;
	EBook *book;

	if (!e_book_get_addressbooks (&sources, &err)) {
		if (error)
			g_propagate_error (error, err);
		return NULL;
	}

	for (g = e_source_list_peek_groups (sources); g; g = g->next) {
		ESourceGroup *group = E_SOURCE_GROUP (g->data);
		GSList *s;
		for (s = e_source_group_peek_sources (group); s; s = s->next) {
			ESource *source = E_SOURCE (s->data);

			if (e_source_get_property (source, "system")) {
				system_source = source;
				break;
			}
		}

		if (system_source)
			break;
	}

	if (system_source) {
		book = e_book_new (system_source, &err);
	}
	else {
		char *filename;
		char *uri;

		filename = g_build_filename (g_get_home_dir(),
					     ".evolution/addressbook/local/system",
					     NULL);
		uri = g_filename_to_uri (filename, NULL, NULL);

		g_free (filename);

		book = e_book_new_from_uri (uri, error);

		g_free (uri);
	}

	if (!book) {
		if (error)
			g_propagate_error (error, err);
	}

	g_object_unref (sources);

	return book;
}

/**
 * e_book_new_default_addressbook:
 * @error: A #GError pointer
 *
 * Creates a new #EBook corresponding to the user's default
 * addressbook.  See the documentation for e_book_new for further
 * information.
 *
 * Return value: a new but unopened #EBook.
 */
EBook*
e_book_new_default_addressbook   (GError **error)
{
	ESourceList *sources;
	GSList *g;
	GError *err = NULL;
	ESource *default_source = NULL;
	EBook *book;

	if (!e_book_get_addressbooks (&sources, &err)) {
		if (error)
			g_propagate_error (error, err);
		return NULL;
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

	if (default_source)
		book = e_book_new (default_source, &err);
	else
		book = e_book_new_system_addressbook (&err);

	if (!book) {
		if (error)
			g_propagate_error (error, err);
	}

	g_object_unref (sources);

	return book;
}

static void
e_book_init (EBook *book)
{
	book->priv                = g_new0 (EBookPrivate, 1);
	book->priv->load_state    = E_BOOK_SOURCE_NOT_LOADED;
	book->priv->uri           = NULL;
	book->priv->source        = NULL;
	book->priv->mutex         = g_mutex_new ();
	book->priv->id_to_op      = g_hash_table_new (g_int_hash, g_int_equal);
	book->priv->current_op_id = 1;
}

static void
e_book_dispose (GObject *object)
{
	EBook             *book = E_BOOK (object);

	if (book->priv) {
		if (book->priv->comp_listener) {
			g_signal_handler_disconnect (book->priv->comp_listener, book->priv->died_signal);
			g_object_unref (book->priv->comp_listener);
			book->priv->comp_listener = NULL;
		}

		if (book->priv->load_state == E_BOOK_SOURCE_LOADED)
			e_book_unload_uri (book, NULL);

		if (book->priv->corba_book) {
			CORBA_Environment  ev;

			CORBA_exception_init (&ev);
			bonobo_object_release_unref  (book->priv->corba_book, &ev);
			if (ev._major != CORBA_NO_EXCEPTION)
				g_warning ("%s: Exception releasing remote book interface!\n", G_STRFUNC);
			CORBA_exception_free (&ev);
		}

		if (book->priv->listener) {
			e_book_listener_stop (book->priv->listener);

			/* GLib bug compatibility */
			if (g_signal_handler_is_connected (book->priv->listener, book->priv->listener_signal))
				g_signal_handler_disconnect (book->priv->listener, book->priv->listener_signal);
			bonobo_object_unref (book->priv->listener);
			book->priv->listener = NULL;
		}

		g_free (book->priv->cap);

		g_free (book->priv->uri);

		if (book->priv->source)
			g_object_unref (book->priv->source);

		/* XXX free up the remaining ops? */
		g_hash_table_destroy (book->priv->id_to_op);

		g_mutex_free (book->priv->mutex);

		if (book->priv->connection_idle_id)
			g_source_remove (book->priv->connection_idle_id);

		if (book->priv->auth_idle_id)
			g_source_remove (book->priv->auth_idle_id);

		if (book->priv->writable_idle_id)
			g_source_remove (book->priv->writable_idle_id);

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
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1,
			      G_TYPE_BOOLEAN);

	e_book_signals [CONNECTION_STATUS] =
		g_signal_new ("connection_status",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EBookClass, connection_status),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1,
			      G_TYPE_BOOLEAN);

	e_book_signals [AUTH_REQUIRED] =
		g_signal_new ("auth_required",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EBookClass, auth_required),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	e_book_signals [BACKEND_DIED] =
		g_signal_new ("backend_died",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EBookClass, backend_died),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
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

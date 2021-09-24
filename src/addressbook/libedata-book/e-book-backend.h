/*
 * e-book-backend.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2012 Intel Corporation
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Nat Friedman (nat@ximian.com)
 *          Tristan Van Berkom <tristanvb@openismus.com>
 */

#if !defined (__LIBEDATA_BOOK_H_INSIDE__) && !defined (LIBEDATA_BOOK_COMPILATION)
#error "Only <libedata-book/libedata-book.h> should be included directly."
#endif

#ifndef E_BOOK_BACKEND_H
#define E_BOOK_BACKEND_H

#include <libebook-contacts/libebook-contacts.h>
#include <libebackend/libebackend.h>

#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-cursor.h>
#include <libedata-book/e-data-book-direct.h>
#include <libedata-book/e-data-book-view.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_BACKEND \
	(e_book_backend_get_type ())
#define E_BOOK_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_BACKEND, EBookBackend))
#define E_BOOK_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_BACKEND, EBookBackendClass))
#define E_IS_BOOK_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_BACKEND))
#define E_IS_BOOK_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_BACKEND))
#define E_BOOK_BACKEND_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_BACKEND, EBookBackendClass))

G_BEGIN_DECLS

typedef struct _EBookBackend EBookBackend;
typedef struct _EBookBackendClass EBookBackendClass;
typedef struct _EBookBackendPrivate EBookBackendPrivate;

/**
 * EBookBackend:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 */
struct _EBookBackend {
	/*< private >*/
	EBackend parent;
	EBookBackendPrivate *priv;
};

/**
 * EBookBackendClass:
 * @use_serial_dispatch_queue: Whether a serial dispatch queue should
 *                             be used for this backend or not. The default is %TRUE.
 * @impl_get_backend_property: Fetch a property value by name from the backend
 * @impl_open: Open the backend
 * @impl_refresh: Refresh the backend
 * @impl_create_contacts: Add and store the passed vcards
 * @impl_modify_contacts: Modify the existing contacts using the passed vcards
 * @impl_remove_contacts: Remove the contacts specified by the passed UIDs
 * @impl_get_contact: Fetch a contact by UID
 * @impl_get_contact_list: Fetch a list of contacts based on a search expression
 * @impl_get_contact_list_uids: Fetch a list of contact UIDs based on a search expression
 * @impl_start_view: Start up the specified view
 * @impl_stop_view: Stop the specified view
 * @impl_notify_update: Notify changes which might have occured for a given contact
 * @impl_get_direct_book: For addressbook backends which support Direct Read Access,
 *                   report some information on how to access the addressbook persistance directly
 * @impl_configure_direct: For addressbook backends which support Direct Read Access, configure a
 *                    backend instantiated on the client side for Direct Read Access, using data
 *                    reported from the server via the @get_direct_book method.
 * @impl_set_locale: Store & remember the passed locale setting
 * @impl_dup_locale: Return the currently set locale setting (must be a string duplicate, for thread safety).
 * @impl_create_cursor: Create an #EDataBookCursor
 * @impl_delete_cursor: Delete an #EDataBookCursor previously created by this backend
 * @impl_contains_email: Checkes whether the backend contains an email address
 * @closed: A signal notifying that the backend was closed
 * @shutdown: A signal notifying that the backend is being shut down
 *
 * Class structure for the #EBookBackend class.
 *
 * These virtual methods must be implemented when writing
 * an addressbook backend.
 */
struct _EBookBackendClass {
	/*< private >*/
	EBackendClass parent_class;

	/*< public >*/

	/* Set this to TRUE to use a serial dispatch queue, instead
	 * of a concurrent dispatch queue.  A serial dispatch queue
	 * executes one method at a time in the order in which they
	 * were called.  This is generally slower than a concurrent
	 * dispatch queue, but helps avoid thread-safety issues. */
	gboolean use_serial_dispatch_queue;

	gchar *		(*impl_get_backend_property)
						 (EBookBackend *backend,
						 const gchar *prop_name);

	void		(*impl_open)		(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable);
	void		(*impl_refresh)		(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable);
	void		(*impl_create_contacts)	(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar * const *vcards,
						 guint32 opflags); /* bit-or of EBookOperationFlags */
	void		(*impl_modify_contacts)	(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar * const *vcards,
						 guint32 opflags); /* bit-or of EBookOperationFlags */
	void		(*impl_remove_contacts)	(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar * const *uids,
						 guint32 opflags); /* bit-or of EBookOperationFlags */
	void		(*impl_get_contact)	(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *id);
	void		(*impl_get_contact_list)(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *query);
	void		(*impl_get_contact_list_uids)
						(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *query);

	void		(*impl_start_view)	(EBookBackend *backend,
						 EDataBookView *view);
	void		(*impl_stop_view)	(EBookBackend *backend,
						 EDataBookView *view);

	void		(*impl_notify_update)	(EBookBackend *backend,
						 const EContact *contact);

	EDataBookDirect *
			(*impl_get_direct_book)	(EBookBackend *backend);
	void		(*impl_configure_direct)(EBookBackend *backend,
						 const gchar *config);

	gboolean	(*impl_set_locale)	(EBookBackend *backend,
						 const gchar *locale,
						 GCancellable *cancellable,
						 GError **error);
	gchar *		(*impl_dup_locale)	(EBookBackend *backend);
	EDataBookCursor *
			(*impl_create_cursor)	(EBookBackend *backend,
						 EContactField *sort_fields,
						 EBookCursorSortType *sort_types,
						 guint n_fields,
						 GError **error);
	gboolean	(*impl_delete_cursor)	(EBookBackend *backend,
						 EDataBookCursor *cursor,
						 GError **error);

	/* Signals */
	void		(*closed)		(EBookBackend *backend,
						 const gchar *sender);
	void		(*shutdown)		(EBookBackend *backend);

	void		(*impl_contains_email)	(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *email_address);

	/* Padding for future expansion */
	gpointer reserved_padding[19];
};

GType		e_book_backend_get_type		(void) G_GNUC_CONST;

const gchar *	e_book_backend_get_cache_dir	(EBookBackend *backend);
gchar *		e_book_backend_dup_cache_dir	(EBookBackend *backend);
void		e_book_backend_set_cache_dir	(EBookBackend *backend,
						 const gchar *cache_dir);
EDataBook *	e_book_backend_ref_data_book	(EBookBackend *backend);
void		e_book_backend_set_data_book	(EBookBackend *backend,
						 EDataBook *data_book);
GProxyResolver *
		e_book_backend_ref_proxy_resolver
						(EBookBackend *backend);
ESourceRegistry *
		e_book_backend_get_registry	(EBookBackend *backend);
gboolean	e_book_backend_get_writable	(EBookBackend *backend);
void		e_book_backend_set_writable	(EBookBackend *backend,
						 gboolean writable);

gboolean	e_book_backend_is_opened	(EBookBackend *backend);
gboolean	e_book_backend_is_readonly	(EBookBackend *backend);

gchar *		e_book_backend_get_backend_property
						(EBookBackend *backend,
						 const gchar *prop_name);
gboolean	e_book_backend_open_sync	(EBookBackend *backend,
						 GCancellable *cancellable,
						 GError **error);
void		e_book_backend_open		(EBookBackend *backend,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_book_backend_open_finish	(EBookBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_book_backend_refresh_sync	(EBookBackend *backend,
						 GCancellable *cancellable,
						 GError **error);
void		e_book_backend_refresh		(EBookBackend *backend,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_book_backend_refresh_finish	(EBookBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_book_backend_create_contacts_sync
						(EBookBackend *backend,
						 const gchar * const *vcards,
						 guint32 opflags, /* bit-or of EBookOperationFlags */
						 GQueue *out_contacts,
						 GCancellable *cancellable,
						 GError **error);
void		e_book_backend_create_contacts	(EBookBackend *backend,
						 const gchar * const *vcards,
						 guint32 opflags, /* bit-or of EBookOperationFlags */
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_book_backend_create_contacts_finish
						(EBookBackend *backend,
						 GAsyncResult *result,
						 GQueue *out_contacts,
						 GError **error);
gboolean	e_book_backend_modify_contacts_sync
						(EBookBackend *backend,
						 const gchar * const *vcards,
						 guint32 opflags, /* bit-or of EBookOperationFlags */
						 GCancellable *cancellable,
						 GError **error);
void		e_book_backend_modify_contacts	(EBookBackend *backend,
						 const gchar * const *vcards,
						 guint32 opflags, /* bit-or of EBookOperationFlags */
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_book_backend_modify_contacts_finish
						(EBookBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_book_backend_remove_contacts_sync
						(EBookBackend *backend,
						 const gchar * const *uids,
						 guint32 opflags, /* bit-or of EBookOperationFlags */
						 GCancellable *cancellable,
						 GError **error);
void		e_book_backend_remove_contacts	(EBookBackend *backend,
						 const gchar * const *uids,
						 guint32 opflags, /* bit-or of EBookOperationFlags */
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_book_backend_remove_contacts_finish
						(EBookBackend *backend,
						 GAsyncResult *result,
						 GError **error);
EContact *	e_book_backend_get_contact_sync	(EBookBackend *backend,
						 const gchar *uid,
						 GCancellable *cancellable,
						 GError **error);
void		e_book_backend_get_contact	(EBookBackend *backend,
						 const gchar *uid,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
EContact *	e_book_backend_get_contact_finish
						(EBookBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_book_backend_get_contact_list_sync
						(EBookBackend *backend,
						 const gchar *query,
						 GQueue *out_contacts,
						 GCancellable *cancellable,
						 GError **error);
void		e_book_backend_get_contact_list	(EBookBackend *backend,
						 const gchar *query,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_book_backend_get_contact_list_finish
						(EBookBackend *backend,
						 GAsyncResult *result,
						 GQueue *out_contacts,
						 GError **error);
gboolean	e_book_backend_get_contact_list_uids_sync
						(EBookBackend *backend,
						 const gchar *query,
						 GQueue *out_uids,
						 GCancellable *cancellable,
						 GError **error);
void		e_book_backend_get_contact_list_uids
						(EBookBackend *backend,
						 const gchar *query,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_book_backend_get_contact_list_uids_finish
						(EBookBackend *backend,
						 GAsyncResult *result,
						 GQueue *out_uids,
						 GError **error);
gboolean	e_book_backend_contains_email_sync
						(EBookBackend *backend,
						 const gchar *email_address,
						 GCancellable *cancellable,
						 GError **error);
void		e_book_backend_contains_email	(EBookBackend *backend,
						 const gchar *email_address,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_book_backend_contains_email_finish
						(EBookBackend *backend,
						 GAsyncResult *result,
						 GError **error);

void		e_book_backend_start_view	(EBookBackend *backend,
						 EDataBookView *view);
void		e_book_backend_stop_view	(EBookBackend *backend,
						 EDataBookView *view);
void		e_book_backend_add_view		(EBookBackend *backend,
						 EDataBookView *view);
void		e_book_backend_remove_view	(EBookBackend *backend,
						 EDataBookView *view);
GList *		e_book_backend_list_views	(EBookBackend *backend);

typedef gboolean (*EBookBackendForeachViewFunc)	(EBookBackend *backend,
						 EDataBookView *view,
						 gpointer user_data);

gboolean	e_book_backend_foreach_view	(EBookBackend *backend,
						 EBookBackendForeachViewFunc func,
						 gpointer user_data);
void		e_book_backend_foreach_view_notify_progress
						(EBookBackend *backend,
						 gboolean only_completed_views,
						 gint percent,
						 const gchar *message);

void		e_book_backend_notify_update	(EBookBackend *backend,
						 const EContact *contact);
void		e_book_backend_notify_remove	(EBookBackend *backend,
						 const gchar *id);
void		e_book_backend_notify_complete	(EBookBackend *backend);

void		e_book_backend_notify_error	(EBookBackend *backend,
						 const gchar *message);
void		e_book_backend_notify_property_changed
						(EBookBackend *backend,
						 const gchar *prop_name,
						 const gchar *prop_value);

EDataBookDirect *
		e_book_backend_get_direct_book	(EBookBackend *backend);
void		e_book_backend_configure_direct	(EBookBackend *backend,
						 const gchar *config);

void		e_book_backend_sync		(EBookBackend *backend);

gboolean	e_book_backend_set_locale	(EBookBackend *backend,
						 const gchar *locale,
						 GCancellable *cancellable,
						 GError **error);
gchar *		e_book_backend_dup_locale	(EBookBackend *backend);

EDataBookCursor *
		e_book_backend_create_cursor	(EBookBackend *backend,
						 EContactField *sort_fields,
						 EBookCursorSortType *sort_types,
						 guint n_fields,
						 GError **error);
gboolean	e_book_backend_delete_cursor	(EBookBackend *backend,
						 EDataBookCursor *cursor,
						 GError **error);

GSimpleAsyncResult *
		e_book_backend_prepare_for_completion
						(EBookBackend *backend,
						 guint32 opid,
						 GQueue **result_queue);
/**
 * EBookBackendCustomOpFunc:
 * @book_backend: an #EBookBackend
 * @user_data: a function user data, as provided to e_book_backend_schedule_custom_operation()
 * @cancellable: an optional #GCancellable, as provided to e_book_backend_schedule_custom_operation()
 * @error: return location for a #GError, or %NULL
 *
 * A callback prototype being called in a dedicated thread, scheduled
 * by e_book_backend_schedule_custom_operation().
 *
 * Since: 3.26
 **/
typedef void	(* EBookBackendCustomOpFunc)	(EBookBackend *book_backend,
						 gpointer user_data,
						 GCancellable *cancellable,
						 GError **error);

void		e_book_backend_schedule_custom_operation
						(EBookBackend *book_backend,
						 GCancellable *use_cancellable,
						 EBookBackendCustomOpFunc func,
						 gpointer user_data,
						 GDestroyNotify user_data_free);


G_END_DECLS

#endif /* E_BOOK_BACKEND_H */

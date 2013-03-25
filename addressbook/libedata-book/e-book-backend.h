/*
 * e-book-backend.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#if !defined (__LIBEDATA_BOOK_H_INSIDE__) && !defined (LIBEDATA_BOOK_COMPILATION)
#error "Only <libedata-book/libedata-book.h> should be included directly."
#endif

#ifndef E_BOOK_BACKEND_H
#define E_BOOK_BACKEND_H

#include <libebook-contacts/libebook-contacts.h>
#include <libebackend/libebackend.h>

#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include <libedata-book/e-data-book-direct.h>

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

/**
 * CLIENT_BACKEND_PROPERTY_ONLINE:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
#define CLIENT_BACKEND_PROPERTY_ONLINE			"online"

/**
 * CLIENT_BACKEND_PROPERTY_READONLY:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
#define CLIENT_BACKEND_PROPERTY_READONLY		"readonly"

/**
 * CLIENT_BACKEND_PROPERTY_CACHE_DIR:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
#define CLIENT_BACKEND_PROPERTY_CACHE_DIR		"cache-dir"

/**
 * CLIENT_BACKEND_PROPERTY_CAPABILITIES:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
#define CLIENT_BACKEND_PROPERTY_CAPABILITIES		"capabilities"

/**
 * BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
#define BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS		"required-fields"

/**
 * BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
#define BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS		"supported-fields"

/**
 * BOOK_BACKEND_PROPERTY_REVISION:
 *
 * The current overall revision string, this can be used as
 * a quick check to see if data has changed at all since the
 * last time the addressbook revision was observed.
 *
 * Since: 3.4
 **/
#define BOOK_BACKEND_PROPERTY_REVISION			"revision"

G_BEGIN_DECLS

typedef struct _EBookBackend EBookBackend;
typedef struct _EBookBackendClass EBookBackendClass;
typedef struct _EBookBackendPrivate EBookBackendPrivate;

struct _EBookBackend {
	EBackend parent;
	EBookBackendPrivate *priv;
};

struct _EBookBackendClass {
	EBackendClass parent_class;

	/* Virtual methods */
	void		(*get_backend_property)	(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *prop_name);

	/* This method is deprecated. */
	void		(*set_backend_property)	(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *prop_name,
						 const gchar *prop_value);

	void		(*open)			(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 gboolean only_if_exists);

	void		(*refresh)		(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable);
	void		(*create_contacts)	(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const GSList *vcards);
	void		(*remove_contacts)	(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const GSList *id_list);
	void		(*modify_contacts)	(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const GSList *vcards);
	void		(*get_contact)		(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *id);
	void		(*get_contact_list)	(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *query);
	void		(*get_contact_list_uids)
						(EBookBackend *backend,
						 EDataBook *book,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *query);

	void		(*start_view)		(EBookBackend *backend,
						 EDataBookView *book_view);
	void		(*stop_view)		(EBookBackend *backend,
						 EDataBookView *book_view);

	void		(*notify_update)	(EBookBackend *backend,
						 const EContact *contact);

	EDataBookDirect *
			(*get_direct_book)	(EBookBackend *backend);
	void		(*configure_direct)	(EBookBackend *backend,
						 const gchar *config);

	void		(*sync)			(EBookBackend *backend);

	/* Signals */
	void		(*closed)		(EBookBackend *backend,
						 const gchar *sender);
};

GType		e_book_backend_get_type		(void) G_GNUC_CONST;

const gchar *	e_book_backend_get_cache_dir	(EBookBackend *backend);
void		e_book_backend_set_cache_dir	(EBookBackend *backend,
						 const gchar *cache_dir);
EDataBook *	e_book_backend_ref_data_book	(EBookBackend *backend);
void		e_book_backend_set_data_book	(EBookBackend *backend,
						 EDataBook *data_book);
ESourceRegistry *
		e_book_backend_get_registry	(EBookBackend *backend);
gboolean	e_book_backend_get_writable	(EBookBackend *backend);
void		e_book_backend_set_writable	(EBookBackend *backend,
						 gboolean writable);

gboolean	e_book_backend_is_opened	(EBookBackend *backend);
gboolean	e_book_backend_is_readonly	(EBookBackend *backend);
gboolean	e_book_backend_is_removed	(EBookBackend *backend);

gchar *		e_book_backend_get_backend_property_sync
						(EBookBackend *backend,
						 const gchar *prop_name,
						 GCancellable *cancellable,
						 GError **error);
void		e_book_backend_get_backend_property
						(EBookBackend *backend,
						 const gchar *prop_name,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gchar *		e_book_backend_get_backend_property_finish
						(EBookBackend *backend,
						 GAsyncResult *result,
						 GError **error);
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
						 GQueue *out_contacts,
						 GCancellable *cancellable,
						 GError **error);
void		e_book_backend_create_contacts	(EBookBackend *backend,
						 const gchar * const *vcards,
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
						 GCancellable *cancellable,
						 GError **error);
void		e_book_backend_modify_contacts	(EBookBackend *backend,
						 const gchar * const *vcards,
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
						 GCancellable *cancellable,
						 GError **error);
void		e_book_backend_remove_contacts	(EBookBackend *backend,
						 const gchar * const *uids,
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

void		e_book_backend_start_view	(EBookBackend *backend,
						 EDataBookView *view);
void		e_book_backend_stop_view	(EBookBackend *backend,
						 EDataBookView *view);
void		e_book_backend_add_view		(EBookBackend *backend,
						 EDataBookView *view);
void		e_book_backend_remove_view	(EBookBackend *backend,
						 EDataBookView *view);
GList *		e_book_backend_list_views	(EBookBackend *backend);

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

/* protected functions for subclasses */
void		e_book_backend_set_is_removed	(EBookBackend *backend,
						 gboolean is_removed);

GSimpleAsyncResult *
		e_book_backend_prepare_for_completion
						(EBookBackend *backend,
						 guint32 opid,
						 GQueue **result_queue);

G_END_DECLS

#endif /* E_BOOK_BACKEND_H */

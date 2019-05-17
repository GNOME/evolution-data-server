/*
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

#ifndef E_BOOK_BACKEND_SYNC_H
#define E_BOOK_BACKEND_SYNC_H

#include <libebook-contacts/libebook-contacts.h>
#include <libebackend/libebackend.h>

#include <libedata-book/e-book-backend.h>
#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-cursor.h>
#include <libedata-book/e-data-book-direct.h>
#include <libedata-book/e-data-book-view.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_BACKEND_SYNC \
	(e_book_backend_sync_get_type ())
#define E_BOOK_BACKEND_SYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_BACKEND_SYNC, EBookBackendSync))
#define E_BOOK_BACKEND_SYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_BACKEND_SYNC, EBookBackendSyncClass))
#define E_IS_BOOK_BACKEND_SYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_BACKEND_SYNC))
#define E_IS_BOOK_BACKEND_SYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_BACKEND_SYNC))
#define E_BOOK_BACKEND_SYNC_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_BACKEND_SYNC, EBookBackendSyncClass))

G_BEGIN_DECLS

typedef struct _EBookBackendSync EBookBackendSync;
typedef struct _EBookBackendSyncClass EBookBackendSyncClass;
typedef struct _EBookBackendSyncPrivate EBookBackendSyncPrivate;

/**
 * EBookBackendSync:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 */
struct _EBookBackendSync {
	/*< private >*/
	EBookBackend parent;
	EBookBackendSyncPrivate *priv;
};

/**
 * EBookBackendSyncClass:
 * @open_sync: Open the backend
 * @refresh_sync: Refresh the backend
 * @create_contacts_sync: Add and store the passed vcards
 * @modify_contacts_sync: Modify the existing contacts using the passed vcards
 * @remove_contacts_sync: Remove the contacts specified by the passed UIDs
 * @get_contact_sync: Fetch a contact by UID
 * @get_contact_list_sync: Fetch a list of contacts based on a search expression
 * @get_contact_list_uids_sync: Fetch a list of contact UIDs based on a search expression (optional)
 *
 * Class structure for the #EBookBackendSync class.
 *
 * These virtual methods must be implemented when writing
 * an addressbook backend.
 */
struct _EBookBackendSyncClass {
	/*< private >*/
	EBookBackendClass parent_class;

	/*< public >*/

	gboolean	(*open_sync)		(EBookBackendSync *backend,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*refresh_sync)		(EBookBackendSync *backend,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*create_contacts_sync)	(EBookBackendSync *backend,
						 const gchar * const *vcards,
						 guint32 opflags, /* bit-or of EBookOperationFlags */
						 GSList **out_contacts, /* EContact * */
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*modify_contacts_sync)	(EBookBackendSync *backend,
						 const gchar * const *vcards,
						 guint32 opflags, /* bit-or of EBookOperationFlags */
						 GSList **out_contacts, /* EContact * */
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*remove_contacts_sync)	(EBookBackendSync *backend,
						 const gchar * const *uids,
						 guint32 opflags, /* bit-or of EBookOperationFlags */
						 GSList **out_removed_uids, /* gchar * */
						 GCancellable *cancellable,
						 GError **error);
	EContact *	(*get_contact_sync)	(EBookBackendSync *backend,
						 const gchar *uid,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*get_contact_list_sync)
						(EBookBackendSync *backend,
						 const gchar *query,
						 GSList **out_contacts,
						 GCancellable *cancellable,
						 GError **error);

	/* This method is optional.  By default, it simply calls
	 * get_contact_list_sync() and extracts UID strings from
	 * the matched EContacts.  Backends may override this if
	 * they can implement it more efficiently. */
	gboolean	(*get_contact_list_uids_sync)
						(EBookBackendSync *backend,
						 const gchar *query,
						 GSList **out_uids,
						 GCancellable *cancellable,
						 GError **error);

	/* Padding for future expansion */
	gpointer reserved_padding[20];
};

GType		e_book_backend_sync_get_type	(void) G_GNUC_CONST;

gboolean	e_book_backend_sync_open	(EBookBackendSync *backend,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_book_backend_sync_refresh	(EBookBackendSync *backend,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_book_backend_sync_create_contacts
						(EBookBackendSync *backend,
						 const gchar * const *vcards,
						 guint32 opflags, /* bit-or of EBookOperationFlags */
						 GSList **out_contacts, /* EContact * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_book_backend_sync_modify_contacts
						(EBookBackendSync *backend,
						 const gchar * const *vcards,
						 guint32 opflags, /* bit-or of EBookOperationFlags */
						 GSList **out_contacts, /* EContact * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_book_backend_sync_remove_contacts
						(EBookBackendSync *backend,
						 const gchar * const *uids,
						 guint32 opflags, /* bit-or of EBookOperationFlags */
						 GSList **out_removed_uids, /* gchar * */
						 GCancellable *cancellable,
						 GError **error);
EContact *	e_book_backend_sync_get_contact	(EBookBackendSync *backend,
						 const gchar *uid,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_book_backend_sync_get_contact_list
						(EBookBackendSync *backend,
						 const gchar *query,
						 GSList **out_contacts, /* EContact * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_book_backend_sync_get_contact_list_uids
						(EBookBackendSync *backend,
						 const gchar *query,
						 GSList **out_uids, /* gchar * */
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* E_BOOK_BACKEND_SYNC_H */

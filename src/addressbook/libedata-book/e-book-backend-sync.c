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

/**
 * SECTION: e-book-backend-sync
 * @include: libedata-book/libedata-book.h
 * @short_description: An abstract class for implementing synchronous addressbook backends
 *
 * This is a descendant of the #EBookBackend, providing synchronous variants
 * of the main methods.
 *
 * Since: 3.34
 **/

#include "evolution-data-server-config.h"

#include <glib.h>

#include "e-data-book-view.h"
#include "e-data-book.h"
#include "e-book-backend.h"
#include "e-book-backend-sync.h"

struct _EBookBackendSyncPrivate {
	guint dummy;
};

G_DEFINE_TYPE (EBookBackendSync, e_book_backend_sync, E_TYPE_BOOK_BACKEND)

static void
book_backend_sync_open (EBookBackend *backend,
			EDataBook *book,
			guint32 opid,
			GCancellable *cancellable)
{
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_BACKEND_SYNC (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	e_book_backend_sync_open (E_BOOK_BACKEND_SYNC (backend), cancellable, &error);

	e_data_book_respond_open (book, opid, error);
}

static void
book_backend_sync_refresh (EBookBackend *backend,
			   EDataBook *book,
			   guint32 opid,
			   GCancellable *cancellable)
{
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_BACKEND_SYNC (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	e_book_backend_sync_refresh (E_BOOK_BACKEND_SYNC (backend), cancellable, &error);

	e_data_book_respond_refresh (book, opid, error);
}

static void
book_backend_sync_create_contacts (EBookBackend *backend,
				   EDataBook *book,
				   guint32 opid,
				   GCancellable *cancellable,
				   const gchar * const *vcards,
				   guint32 opflags)
{
	GSList *contacts = NULL;
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_BACKEND_SYNC (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	e_book_backend_sync_create_contacts (E_BOOK_BACKEND_SYNC (backend), vcards, opflags, &contacts, cancellable, &error);

	e_data_book_respond_create_contacts (book, opid, error, contacts);

	g_slist_free_full (contacts, g_object_unref);
}

static void
book_backend_sync_modify_contacts (EBookBackend *backend,
				   EDataBook *book,
				   guint32 opid,
				   GCancellable *cancellable,
				   const gchar * const *vcards,
				   guint32 opflags)
{
	GSList *contacts = NULL;
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_BACKEND_SYNC (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	e_book_backend_sync_modify_contacts (E_BOOK_BACKEND_SYNC (backend), vcards, opflags, &contacts, cancellable, &error);

	e_data_book_respond_modify_contacts (book, opid, error, contacts);

	g_slist_free_full (contacts, g_object_unref);
}

static void
book_backend_sync_remove_contacts (EBookBackend *backend,
				   EDataBook *book,
				   guint32 opid,
				   GCancellable *cancellable,
				   const gchar * const *uids,
				   guint32 opflags)
{
	GSList *removed_uids = NULL;
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_BACKEND_SYNC (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	e_book_backend_sync_remove_contacts (E_BOOK_BACKEND_SYNC (backend), uids, opflags, &removed_uids, cancellable, &error);

	e_data_book_respond_remove_contacts (book, opid, error, removed_uids);

	g_slist_free_full (removed_uids, g_free);
}

static void
book_backend_sync_get_contact (EBookBackend *backend,
			       EDataBook *book,
			       guint32 opid,
			       GCancellable *cancellable,
			       const gchar *uid)
{
	EContact *contact;
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_BACKEND_SYNC (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	contact = e_book_backend_sync_get_contact (E_BOOK_BACKEND_SYNC (backend), uid, cancellable, &error);

	e_data_book_respond_get_contact (book, opid, error, contact);

	g_clear_object (&contact);
}

static void
book_backend_sync_get_contact_list (EBookBackend *backend,
				    EDataBook *book,
				    guint32 opid,
				    GCancellable *cancellable,
				    const gchar *query)
{
	GSList *contacts = NULL;
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_BACKEND_SYNC (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	e_book_backend_sync_get_contact_list (E_BOOK_BACKEND_SYNC (backend), query, &contacts, cancellable, &error);

	e_data_book_respond_get_contact_list (book, opid, error, contacts);

	g_slist_free_full (contacts, g_object_unref);
}

static void
book_backend_sync_get_contact_list_uids (EBookBackend *backend,
					 EDataBook *book,
					 guint32 opid,
					 GCancellable *cancellable,
					 const gchar *query)
{
	GSList *uids = NULL;
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_BACKEND_SYNC (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	e_book_backend_sync_get_contact_list_uids (E_BOOK_BACKEND_SYNC (backend), query, &uids, cancellable, &error);

	e_data_book_respond_get_contact_list_uids (book, opid, error, uids);

	g_slist_free_full (uids, g_free);
}

static gboolean
book_backend_sync_get_contact_list_uids_sync (EBookBackendSync *backend,
					      const gchar *query,
					      GSList **out_uids,
					      GCancellable *cancellable,
					      GError **error)
{
	GSList *contacts = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), FALSE);
	g_return_val_if_fail (out_uids != NULL, FALSE);

	*out_uids = NULL;

	success = e_book_backend_sync_get_contact_list (backend, query, &contacts, cancellable, error);

	if (success) {
		GSList *link;

		for (link = contacts; link; link = g_slist_next (link)) {
			EContact *contact = link->data;
			gchar *uid;

			uid = e_contact_get (contact, E_CONTACT_UID);
			*out_uids = g_slist_prepend (*out_uids, uid);
		}
	}

	g_slist_free_full (contacts, g_object_unref);

	return success;
}

static void
e_book_backend_sync_class_init (EBookBackendSyncClass *klass)
{
	EBookBackendClass *book_backend_class;

	g_type_class_add_private (klass, sizeof (EBookBackendSyncPrivate));

	klass->get_contact_list_uids_sync = book_backend_sync_get_contact_list_uids_sync;

	book_backend_class = E_BOOK_BACKEND_CLASS (klass);
	book_backend_class->impl_open = book_backend_sync_open;
	book_backend_class->impl_refresh = book_backend_sync_refresh;
	book_backend_class->impl_create_contacts = book_backend_sync_create_contacts;
	book_backend_class->impl_modify_contacts = book_backend_sync_modify_contacts;
	book_backend_class->impl_remove_contacts = book_backend_sync_remove_contacts;
	book_backend_class->impl_get_contact = book_backend_sync_get_contact;
	book_backend_class->impl_get_contact_list = book_backend_sync_get_contact_list;
	book_backend_class->impl_get_contact_list_uids = book_backend_sync_get_contact_list_uids;
}

static void
e_book_backend_sync_init (EBookBackendSync *backend)
{
	backend->priv = G_TYPE_INSTANCE_GET_PRIVATE (backend, E_TYPE_BOOK_BACKEND_SYNC, EBookBackendSyncPrivate);
}

/**
 * e_book_backend_sync_open:
 * @backend: an #EBookBackendSync
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * "Opens" the @backend.  Opening a backend is something of an outdated
 * concept, but the operation is hanging around for a little while longer.
 * This usually involves some custom initialization logic, and testing of
 * remote authentication if applicable.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.34
 **/
gboolean
e_book_backend_sync_open (EBookBackendSync *backend,
			  GCancellable *cancellable,
			  GError **error)
{
	EBookBackendSyncClass *klass;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), FALSE);

	klass = E_BOOK_BACKEND_SYNC_GET_CLASS (backend);
	g_return_val_if_fail (klass != NULL, FALSE);

	if (klass->open_sync)
		return klass->open_sync (backend, cancellable, error);

	g_propagate_error (error, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));

	return FALSE;
}

/**
 * e_book_backend_sync_refresh:
 * @backend: an #EBookBackendSync
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Initiates a refresh for @backend, if the @backend supports refreshing.
 * The actual refresh operation completes on its own time.  This function
 * merely initiates the operation.
 *
 * If an error occurs while initiating the refresh, the function will set
 * @error and return %FALSE.  If the @backend does not support refreshing,
 * the function will set an %E_CLIENT_ERROR_NOT_SUPPORTED error and return
 * %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.34
 **/
gboolean
e_book_backend_sync_refresh (EBookBackendSync *backend,
			     GCancellable *cancellable,
			     GError **error)
{
	EBookBackendSyncClass *klass;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), FALSE);

	klass = E_BOOK_BACKEND_SYNC_GET_CLASS (backend);
	g_return_val_if_fail (klass != NULL, FALSE);

	if (klass->refresh_sync)
		return klass->refresh_sync (backend, cancellable, error);

	g_propagate_error (error, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));

	return FALSE;
}

/**
 * e_book_backend_sync_create_contacts:
 * @backend: an #EBookBackendSync
 * @vcards: a %NULL-terminated array of vCard strings
 * @opflags: bit-or of #EBookOperationFlags
 * @out_contacts: (out) (element-type EContact): a #GSList in which to deposit results
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates one or more new contacts from @vcards, and deposits an #EContact
 * instance for each newly-created contact in @out_contacts.
 *
 * The returned #EContact instances are referenced for thread-safety and
 * must be unreferenced with g_object_unref() when finished with them.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.34
 **/
gboolean
e_book_backend_sync_create_contacts (EBookBackendSync *backend,
				     const gchar * const *vcards,
				     guint32 opflags,
				     GSList **out_contacts,
				     GCancellable *cancellable,
				     GError **error)
{
	EBookBackendSyncClass *klass;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), FALSE);

	klass = E_BOOK_BACKEND_SYNC_GET_CLASS (backend);
	g_return_val_if_fail (klass != NULL, FALSE);

	if (klass->create_contacts_sync)
		return klass->create_contacts_sync (backend, vcards, opflags, out_contacts, cancellable, error);

	g_propagate_error (error, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));

	return FALSE;
}

/**
 * e_book_backend_sync_modify_contacts:
 * @backend: an #EBookBackendSync
 * @vcards: a %NULL-terminated array of vCard strings
 * @opflags: bit-or of #EBookOperationFlags
 * @out_contacts: (out) (element-type EContact): a #GSList to deposit the modified contacts to
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Modifies one or more contacts according to @vcards.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.34
 **/
gboolean
e_book_backend_sync_modify_contacts (EBookBackendSync *backend,
				     const gchar * const *vcards,
				     guint32 opflags,
				     GSList **out_contacts,
				     GCancellable *cancellable,
				     GError **error)
{
	EBookBackendSyncClass *klass;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), FALSE);

	klass = E_BOOK_BACKEND_SYNC_GET_CLASS (backend);
	g_return_val_if_fail (klass != NULL, FALSE);

	if (klass->modify_contacts_sync)
		return klass->modify_contacts_sync (backend, vcards, opflags, out_contacts, cancellable, error);

	g_propagate_error (error, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));

	return FALSE;
}

/**
 * e_book_backend_sync_remove_contacts:
 * @backend: an #EBookBackendSync
 * @uids: a %NULL-terminated array of contact ID strings
 * @opflags: bit-or of #EBookOperationFlags
 * @out_removed_uids: (out) (element-type utf8): a #GSList of removed UIDs
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Removes one or more contacts according to @uids.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.34
 **/
gboolean
e_book_backend_sync_remove_contacts (EBookBackendSync *backend,
				     const gchar * const *uids,
				     guint32 opflags,
				     GSList **out_removed_uids,
				     GCancellable *cancellable,
				     GError **error)
{
	EBookBackendSyncClass *klass;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), FALSE);

	klass = E_BOOK_BACKEND_SYNC_GET_CLASS (backend);
	g_return_val_if_fail (klass != NULL, FALSE);

	if (klass->remove_contacts_sync)
		return klass->remove_contacts_sync (backend, uids, opflags, out_removed_uids, cancellable, error);

	g_propagate_error (error, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));

	return FALSE;
}

/**
 * e_book_backend_sync_get_contact:
 * @backend: an #EBookBackendSync
 * @uid: a contact ID
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Obtains an #EContact for @uid.
 *
 * The returned #EContact is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * If an error occurs, the function will set @error and return %NULL.
 *
 * Returns: (transfer full): an #EContact, or %NULL
 *
 * Since: 3.34
 **/
EContact *
e_book_backend_sync_get_contact (EBookBackendSync *backend,
				 const gchar *uid,
				 GCancellable *cancellable,
				 GError **error)
{
	EBookBackendSyncClass *klass;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), FALSE);

	klass = E_BOOK_BACKEND_SYNC_GET_CLASS (backend);
	g_return_val_if_fail (klass != NULL, FALSE);

	if (klass->get_contact_sync)
		return klass->get_contact_sync (backend, uid, cancellable, error);

	g_propagate_error (error, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));

	return FALSE;
}

/**
 * e_book_backend_sync_get_contact_list:
 * @backend: an #EBookBackendSync
 * @query: a search query in S-expression format
 * @out_contacts: (out) (element-type EContact): a #GSList in which to deposit results
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Obtains a set of #EContact instances which satisfy the criteria specified
 * in @query, and deposits them in @out_contacts.
 *
 * The returned #EContact instances are referenced for thread-safety and
 * must be unreferenced with g_object_unref() when finished with them.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 * Note that an empty result set does not necessarily imply an error.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.34
 **/
gboolean
e_book_backend_sync_get_contact_list (EBookBackendSync *backend,
				      const gchar *query,
				      GSList **out_contacts,
				      GCancellable *cancellable,
				      GError **error)
{
	EBookBackendSyncClass *klass;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), FALSE);

	klass = E_BOOK_BACKEND_SYNC_GET_CLASS (backend);
	g_return_val_if_fail (klass != NULL, FALSE);

	if (klass->get_contact_list_sync)
		return klass->get_contact_list_sync (backend, query, out_contacts, cancellable, error);

	g_propagate_error (error, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));

	return FALSE;
}

/**
 * e_book_backend_sync_get_contact_list_uids:
 * @backend: an #EBookBackendSync
 * @query: a search query in S-expression format
 * @out_uids: (out) (element-type utf8): a #GSList in which to deposit results
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Obtains a set of ID strings for contacts which satisfy the criteria
 * specified in @query, and deposits them in @out_uids.
 *
 * The returned ID strings must be freed with g_free() with finished
 * with them.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 * Note that an empty result set does not necessarily imply an error.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.34
 **/
gboolean
e_book_backend_sync_get_contact_list_uids (EBookBackendSync *backend,
					   const gchar *query,
					   GSList **out_uids,
					   GCancellable *cancellable,
					   GError **error)
{
	EBookBackendSyncClass *klass;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), FALSE);

	klass = E_BOOK_BACKEND_SYNC_GET_CLASS (backend);
	g_return_val_if_fail (klass != NULL, FALSE);

	if (klass->get_contact_list_uids_sync)
		return klass->get_contact_list_uids_sync (backend, query, out_uids, cancellable, error);

	g_propagate_error (error, e_client_error_create (E_CLIENT_ERROR_NOT_SUPPORTED, NULL));

	return FALSE;
}

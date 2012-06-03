/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libedataserver/libedataserver.h>

#include "e-book-backend-sync.h"

G_DEFINE_TYPE (EBookBackendSync, e_book_backend_sync, E_TYPE_BOOK_BACKEND)

/**
 * e_book_backend_sync_construct:
 * @backend: an #EBookBackendSync
 *
 * Does nothing.
 *
 * Returns: %TRUE.
 **/
gboolean
e_book_backend_sync_construct (EBookBackendSync *backend)
{
	return TRUE;
}

/**
 * e_book_backend_sync_open:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @only_if_exists: whether open only if exists
 * @error: #GError to set, when something fails
 *
 * Opens @backend, which can involve connecting it to a remote server.
 *
 * Since: 3.2
 **/
void
e_book_backend_sync_open (EBookBackendSync *backend,
                          EDataBook *book,
                          GCancellable *cancellable,
                          gboolean only_if_exists,
                          GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->open_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->open_sync) (backend, book, cancellable, only_if_exists, error);
}

/**
 * e_book_backend_sync_create_contacts:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @vcards: a #GSList of vCard representations of contacts
 * @added_contacts: a pointer to a location to store the resulting #EContact list
 * @error: #GError to set, when something fails
 *
 * Creates new contacts with the contents of @vcards in @backend.
 *
 * Since: 3.4
 **/
void
e_book_backend_sync_create_contacts (EBookBackendSync *backend,
                                     EDataBook *book,
                                     GCancellable *cancellable,
                                     const GSList *vcards,
                                     GSList **added_contacts,
                                     GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (vcards, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (added_contacts, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->create_contacts_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->create_contacts_sync) (backend, book, cancellable, vcards, added_contacts, error);
}

/**
 * e_book_backend_sync_remove:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @error: #GError to set, when something fails
 *
 * Remove @book's database and storage overhead from the storage
 * medium. This will delete all contacts in @book.
 **/
void
e_book_backend_sync_remove (EBookBackendSync *backend,
                            EDataBook *book,
                            GCancellable *cancellable,
                            GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_sync) (backend, book, cancellable, error);
}

/**
 * e_book_backend_sync_refresh:
 * @backend: An EBookBackendSync object.
 * @book: An EDataBook object.
 * @cancellable: a #GCancellable for the operation
 * @error: Out parameter for a #GError.
 *
 * Calls the refresh_sync method on the given backend.
 *
 * Since: 3.2
 */
void
e_book_backend_sync_refresh (EBookBackendSync *backend,
                             EDataBook *book,
                             GCancellable *cancellable,
                             GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->refresh_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->refresh_sync) (backend, book, cancellable, error);
}

/**
 * e_book_backend_sync_get_backend_property:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @prop_name: Property name whose value to retrieve.
 * @prop_value: Return value of the @prop_name.
 * @error: #GError to set, when something fails
 *
 * Calls the get_backend_property_sync method on the given backend.
 *
 * Returns whether processed this property. Returning FALSE means to pass
 * the call to the EBookBackend parent class, thus neither @error should be
 * set in this case.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sync_get_backend_property (EBookBackendSync *backend,
                                          EDataBook *book,
                                          GCancellable *cancellable,
                                          const gchar *prop_name,
                                          gchar **prop_value,
                                          GError **error)
{
	e_return_data_book_error_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_val_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_val_if_fail (prop_name, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_val_if_fail (prop_value, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_val_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_backend_property_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_backend_property_sync) (backend, book, cancellable, prop_name, prop_value, error);
}

/**
 * e_book_backend_sync_set_backend_property:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @prop_name: Property name to set.
 * @prop_value: New value of the @prop_name.
 * @error: #GError to set, when something fails
 *
 * Calls the set_backend_property_sync method on the given backend.
 *
 * Returns whether processed this property. Returning FALSE means to pass
 * the call to the EBookBackend parent class, thus neither @error should be
 * set in this case.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sync_set_backend_property (EBookBackendSync *backend,
                                          EDataBook *book,
                                          GCancellable *cancellable,
                                          const gchar *prop_name,
                                          const gchar *prop_value,
                                          GError **error)
{
	e_return_data_book_error_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_val_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_val_if_fail (prop_name, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_val_if_fail (prop_value, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_val_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->set_backend_property_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->set_backend_property_sync) (backend, book, cancellable, prop_name, prop_value, error);
}

/**
 * e_book_backend_sync_remove_contacts:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @id_list: a #GSList of pointers to unique contact ID strings
 * @removed_ids: a pointer to a location to store a list of the contacts actually removed
 * @error: #GError to set, when something fails
 *
 * Removes the contacts specified by @id_list from @backend. The returned list
 * of removed contacts is in the same format as the passed-in list, and must be
 * freed by the caller.
 **/
void
e_book_backend_sync_remove_contacts (EBookBackendSync *backend,
                                     EDataBook *book,
                                     GCancellable *cancellable,
                                     const GSList *id_list,
                                     GSList **removed_ids,
                                     GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (id_list, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (removed_ids, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_contacts_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_contacts_sync) (backend, book, cancellable, id_list, removed_ids, error);
}

/**
 * e_book_backend_sync_modify_contacts:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @vcards: the string representations of contacts
 * @modified_contacts: a pointer to a location to store the resulting
 * #EContact objects
 * @error: #GError to set, when something fails
 *
 * Modifies the contacts specified by the IDs embedded in @vcards, to
 * reflect the full contents of @vcards.
 *
 * Since: 3.4
 **/
void
e_book_backend_sync_modify_contacts (EBookBackendSync *backend,
                                    EDataBook *book,
                                    GCancellable *cancellable,
                                    const GSList *vcards,
                                    GSList **modified_contacts,
                                    GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (vcards, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (modified_contacts, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->modify_contacts_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->modify_contacts_sync) (backend, book, cancellable, vcards, modified_contacts, error);
}

/**
 * e_book_backend_sync_get_contact:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @id: a unique contact ID
 * @vcard: a pointer to a location to store the resulting VCard string
 *
 * Gets a contact from @book.
 **/
void
e_book_backend_sync_get_contact (EBookBackendSync *backend,
                                 EDataBook *book,
                                 GCancellable *cancellable,
                                 const gchar *id,
                                 gchar **vcard,
                                 GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (id, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (vcard, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_sync) (backend, book, cancellable, id, vcard, error);
}

/**
 * e_book_backend_sync_get_contact_list:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @query: an s-expression of the query to perform
 * @contacts: a pointer to a location to store the resulting list of VCard strings
 * @error: #GError to set, when something fails
 *
 * Gets a list of contacts from @book. The list and its elements must be freed
 * by the caller.
 **/
void
e_book_backend_sync_get_contact_list (EBookBackendSync *backend,
                                      EDataBook *book,
                                      GCancellable *cancellable,
                                      const gchar *query,
                                      GSList **contacts,
                                      GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (query, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (contacts, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_list_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_list_sync) (backend, book, cancellable, query, contacts, error);
}

/**
 * e_book_backend_sync_get_contact_list_uids:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @query: an s-expression of the query to perform
 * @contacts_uids: a pointer to a location to store the resulting list of UID strings
 * @error: #GError to set, when something fails
 *
 * Gets a list of contact UIDS from @book. The list and its elements must be freed
 * by the caller.
 *
 * Since: 3.2
 **/
void
e_book_backend_sync_get_contact_list_uids (EBookBackendSync *backend,
                                           EDataBook *book,
                                           GCancellable *cancellable,
                                           const gchar *query,
                                           GSList **contacts_uids,
                                           GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (query, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (contacts_uids, E_DATA_BOOK_STATUS_INVALID_ARG);

	if (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_list_uids_sync != NULL) {
		(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_list_uids_sync) (backend, book, cancellable, query, contacts_uids, error);
	} else {
		/* inefficient fallback code */
		GSList *vcards = NULL;
		GError *local_error = NULL;

		e_book_backend_sync_get_contact_list (backend, book, cancellable, query, &vcards, &local_error);

		if (local_error) {
			g_propagate_error (error, local_error);
		} else {
			GSList *v;

			*contacts_uids = NULL;

			for (v = vcards; v; v = v->next) {
				EVCard *card = e_vcard_new_from_string (v->data);
				EVCardAttribute *attr;

				if (!card)
					continue;

				attr = e_vcard_get_attribute (card, EVC_UID);

				if (attr)
					*contacts_uids = g_slist_prepend (*contacts_uids, e_vcard_attribute_get_value (attr));

				g_object_unref (card);
			}

			*contacts_uids = g_slist_reverse (*contacts_uids);
		}

		g_slist_foreach (vcards, (GFunc) g_free, NULL);
		g_slist_free (vcards);
	}
}

static void
book_backend_open (EBookBackend *backend,
                   EDataBook *book,
                   guint32 opid,
                   GCancellable *cancellable,
                   gboolean only_if_exists)
{
	GError *error = NULL;

	e_book_backend_sync_open (E_BOOK_BACKEND_SYNC (backend), book, cancellable, only_if_exists, &error);

	e_data_book_respond_open (book, opid, error);
}

static void
book_backend_remove (EBookBackend *backend,
                     EDataBook *book,
                     guint32 opid,
                     GCancellable *cancellable)
{
	GError *error = NULL;

	e_book_backend_sync_remove (E_BOOK_BACKEND_SYNC (backend), book, cancellable, &error);

	e_data_book_respond_remove (book, opid, error);
}

static void
book_backend_refresh (EBookBackend *backend,
                      EDataBook *book,
                      guint32 opid,
                      GCancellable *cancellable)
{
	GError *error = NULL;

	e_book_backend_sync_refresh (E_BOOK_BACKEND_SYNC (backend), book, cancellable, &error);

	e_data_book_respond_refresh (book, opid, error);
}

static void
book_backend_get_backend_property (EBookBackend *backend,
                                   EDataBook *book,
                                   guint32 opid,
                                   GCancellable *cancellable,
                                   const gchar *prop_name)
{
	GError *error = NULL;
	gchar *prop_value = NULL;

	if (e_book_backend_sync_get_backend_property (E_BOOK_BACKEND_SYNC (backend), book, cancellable, prop_name, &prop_value, &error))
		e_data_book_respond_get_backend_property (book, opid, error, prop_value);
	else
		(* E_BOOK_BACKEND_CLASS (e_book_backend_sync_parent_class)->get_backend_property) (backend, book, opid, cancellable, prop_name);

	g_free (prop_value);
}

static void
book_backend_set_backend_property (EBookBackend *backend,
                                   EDataBook *book,
                                   guint32 opid,
                                   GCancellable *cancellable,
                                   const gchar *prop_name,
                                   const gchar *prop_value)
{
	GError *error = NULL;

	if (e_book_backend_sync_set_backend_property (E_BOOK_BACKEND_SYNC (backend), book, cancellable, prop_name, prop_value, &error))
		e_data_book_respond_set_backend_property (book, opid, error);
	else
		(* E_BOOK_BACKEND_CLASS (e_book_backend_sync_parent_class)->set_backend_property) (backend, book, opid, cancellable, prop_name, prop_value);
}

static void
book_backend_create_contacts (EBookBackend *backend,
                              EDataBook *book,
                              guint32 opid,
                              GCancellable *cancellable,
                              const GSList *vcards)
{
	GError *error = NULL;
	GSList *added_contacts = NULL;

	e_book_backend_sync_create_contacts (E_BOOK_BACKEND_SYNC (backend), book, cancellable, vcards, &added_contacts, &error);

	e_data_book_respond_create_contacts (book, opid, error, added_contacts);

	e_util_free_object_slist (added_contacts);
}

static void
book_backend_remove_contacts (EBookBackend *backend,
                              EDataBook *book,
                              guint32 opid,
                              GCancellable *cancellable,
                              const GSList *id_list)
{
	GError *error = NULL;
	GSList *ids = NULL;

	e_book_backend_sync_remove_contacts (E_BOOK_BACKEND_SYNC (backend), book, cancellable, id_list, &ids, &error);

	e_data_book_respond_remove_contacts (book, opid, error, ids);

	if (ids) {
		g_slist_foreach (ids, (GFunc) g_free, NULL);
		g_slist_free (ids);
	}
}

static void
book_backend_modify_contacts (EBookBackend *backend,
                             EDataBook *book,
                             guint32 opid,
                             GCancellable *cancellable,
                             const GSList *vcards)
{
	GError *error = NULL;
	GSList *modified_contacts = NULL;

	e_book_backend_sync_modify_contacts (E_BOOK_BACKEND_SYNC (backend), book, cancellable, vcards, &modified_contacts, &error);

	e_data_book_respond_modify_contacts (book, opid, error, modified_contacts);

	e_util_free_object_slist (modified_contacts);
}

static void
book_backend_get_contact (EBookBackend *backend,
                          EDataBook *book,
                          guint32 opid,
                          GCancellable *cancellable,
                          const gchar *id)
{
	GError *error = NULL;
	gchar *vcard = NULL;

	e_book_backend_sync_get_contact (E_BOOK_BACKEND_SYNC (backend), book, cancellable, id, &vcard, &error);

	e_data_book_respond_get_contact (book, opid, error, vcard);

	g_free (vcard);
}

static void
book_backend_get_contact_list (EBookBackend *backend,
                               EDataBook *book,
                               guint32 opid,
                               GCancellable *cancellable,
                               const gchar *query)
{
	GError *error = NULL;
	GSList *cards = NULL;

	e_book_backend_sync_get_contact_list (E_BOOK_BACKEND_SYNC (backend), book, cancellable, query, &cards, &error);

	e_data_book_respond_get_contact_list (book, opid, error, cards);

	g_slist_foreach (cards, (GFunc) g_free, NULL);
	g_slist_free (cards);
}

static void
book_backend_get_contact_list_uids (EBookBackend *backend,
                                    EDataBook *book,
                                    guint32 opid,
                                    GCancellable *cancellable,
                                    const gchar *query)
{
	GError *error = NULL;
	GSList *uids = NULL;

	e_book_backend_sync_get_contact_list_uids (E_BOOK_BACKEND_SYNC (backend), book, cancellable, query, &uids, &error);

	e_data_book_respond_get_contact_list_uids (book, opid, error, uids);

	g_slist_foreach (uids, (GFunc) g_free, NULL);
	g_slist_free (uids);
}

static gboolean
book_backend_sync_get_backend_property (EBookBackendSync *backend,
                                        EDataBook *book,
                                        GCancellable *cancellable,
                                        const gchar *prop_name,
                                        gchar **prop_value,
                                        GError **error)
{
	/* to indicate to pass to the EBookBackend parent class */
	return FALSE;
}

static gboolean
book_backend_sync_set_backend_property (EBookBackendSync *backend,
                                        EDataBook *book,
                                        GCancellable *cancellable,
                                        const gchar *prop_name,
                                        const gchar *prop_value,
                                        GError **error)
{
	/* to indicate to pass to the EBookBackend parent class */
	return FALSE;
}

static void
e_book_backend_sync_init (EBookBackendSync *backend)
{
}

static void
e_book_backend_sync_class_init (EBookBackendSyncClass *class)
{
	EBookBackendClass *backend_class = E_BOOK_BACKEND_CLASS (class);

	backend_class->open			= book_backend_open;
	backend_class->remove			= book_backend_remove;
	backend_class->refresh			= book_backend_refresh;
	backend_class->get_backend_property	= book_backend_get_backend_property;
	backend_class->set_backend_property	= book_backend_set_backend_property;
	backend_class->create_contacts		= book_backend_create_contacts;
	backend_class->remove_contacts		= book_backend_remove_contacts;
	backend_class->modify_contacts		= book_backend_modify_contacts;
	backend_class->get_contact		= book_backend_get_contact;
	backend_class->get_contact_list		= book_backend_get_contact_list;
	backend_class->get_contact_list_uids	= book_backend_get_contact_list_uids;

	class->get_backend_property_sync	= book_backend_sync_get_backend_property;
	class->set_backend_property_sync	= book_backend_sync_set_backend_property;
}

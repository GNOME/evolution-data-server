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

#include "e-book-backend-sync.h"

G_DEFINE_TYPE (EBookBackendSync, e_book_backend_sync, E_TYPE_BOOK_BACKEND)

struct _EBookBackendSyncPrivate {
	gint mumble;
};

static GObjectClass *parent_class;

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
 * e_book_backend_sync_create_contact:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @vcard: a VCard representation of a contact
 * @contact: a pointer to a location to store the resulting #EContact
 * @error: #GError to set, when something fails
 *
 * Creates a new contact with the contents of @vcard in @backend.
 **/
void
e_book_backend_sync_create_contact (EBookBackendSync *backend,
				    EDataBook *book,
				    GCancellable *cancellable,
				    const gchar *vcard,
				    EContact **contact,
				    GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (vcard, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (contact, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->create_contact_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->create_contact_sync) (backend, book, cancellable, vcard, contact, error);
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
e_book_backend_sync_refresh  (EBookBackendSync *backend, EDataBook *book, GCancellable *cancellable, GError **error)
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
e_book_backend_sync_get_backend_property (EBookBackendSync *backend, EDataBook *book, GCancellable *cancellable, const gchar *prop_name, gchar **prop_value, GError **error)
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
e_book_backend_sync_set_backend_property (EBookBackendSync *backend, EDataBook *book, GCancellable *cancellable, const gchar *prop_name, const gchar *prop_value, GError **error)
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
 * e_book_backend_sync_modify_contact:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @vcard: the string representation of a contact
 * @contact: a pointer to a location to store the resulting #EContact
 * @error: #GError to set, when something fails
 *
 * Modifies the contact specified by the ID embedded in @vcard, to
 * reflect the full contents of @vcard.
 **/
void
e_book_backend_sync_modify_contact (EBookBackendSync *backend,
				    EDataBook *book,
				    GCancellable *cancellable,
				    const gchar *vcard,
				    EContact **contact,
				    GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (vcard, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (contact, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->modify_contact_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->modify_contact_sync) (backend, book, cancellable, vcard, contact, error);
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
 * e_book_backend_sync_authenticate_user:
 * @backend: an #EBookBackendSync
 * @cancellable: a #GCancellable for the operation
 * @credentials: an #ECredentials to authenticate with
 * @error: #GError to set, when something fails
 *
 * Authenticates @backend with given @credentials.
 **/
void
e_book_backend_sync_authenticate_user (EBookBackendSync *backend,
				       GCancellable *cancellable,
				       ECredentials *credentials,
				       GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (credentials, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->authenticate_user_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->authenticate_user_sync) (backend, cancellable, credentials, error);
}

static void
book_backend_open (EBookBackend *backend,
		      EDataBook    *book,
		      guint32       opid,
		      GCancellable *cancellable,
		      gboolean only_if_exists)
{
	GError *error = NULL;

	e_book_backend_sync_open (E_BOOK_BACKEND_SYNC (backend), book, cancellable, only_if_exists, &error);

	e_data_book_respond_open (book, opid, error);
}

static void
book_backend_remove (EBookBackend *backend,
			EDataBook    *book,
			guint32       opid,
			GCancellable *cancellable)
{
	GError *error = NULL;

	e_book_backend_sync_remove (E_BOOK_BACKEND_SYNC (backend), book, cancellable, &error);

	e_data_book_respond_remove (book, opid, error);
}

static void
book_backend_refresh (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable)
{
	GError *error = NULL;

	e_book_backend_sync_refresh (E_BOOK_BACKEND_SYNC (backend), book, cancellable, &error);

	e_data_book_respond_refresh (book, opid, error);
}

static void
book_backend_get_backend_property (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *prop_name)
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
book_backend_set_backend_property (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *prop_name, const gchar *prop_value)
{
	GError *error = NULL;

	if (e_book_backend_sync_set_backend_property (E_BOOK_BACKEND_SYNC (backend), book, cancellable, prop_name, prop_value, &error))
		e_data_book_respond_set_backend_property (book, opid, error);
	else
		(* E_BOOK_BACKEND_CLASS (e_book_backend_sync_parent_class)->set_backend_property) (backend, book, opid, cancellable, prop_name, prop_value);
}

static void
book_backend_create_contact (EBookBackend *backend,
			     EDataBook    *book,
			     guint32       opid,
			     GCancellable *cancellable,
			     const gchar   *vcard)
{
	GError *error = NULL;
	EContact *contact = NULL;

	e_book_backend_sync_create_contact (E_BOOK_BACKEND_SYNC (backend), book, cancellable, vcard, &contact, &error);

	e_data_book_respond_create (book, opid, error, contact);

	if (contact)
		g_object_unref (contact);
}

static void
book_backend_remove_contacts (EBookBackend *backend,
			      EDataBook    *book,
			      guint32       opid,
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
book_backend_modify_contact (EBookBackend *backend,
			     EDataBook    *book,
			     guint32       opid,
			     GCancellable *cancellable,
			     const gchar   *vcard)
{
	GError *error = NULL;
	EContact *contact = NULL;

	e_book_backend_sync_modify_contact (E_BOOK_BACKEND_SYNC (backend), book, cancellable, vcard, &contact, &error);

	e_data_book_respond_modify (book, opid, error, contact);

	if (contact)
		g_object_unref (contact);
}

static void
book_backend_get_contact (EBookBackend *backend,
			  EDataBook    *book,
			  guint32       opid,
			  GCancellable *cancellable,
			  const gchar   *id)
{
	GError *error = NULL;
	gchar *vcard = NULL;

	e_book_backend_sync_get_contact (E_BOOK_BACKEND_SYNC (backend), book, cancellable, id, &vcard, &error);

	e_data_book_respond_get_contact (book, opid, error, vcard);

	if (vcard)
		g_free (vcard);
}

static void
book_backend_get_contact_list (EBookBackend *backend,
			       EDataBook    *book,
			       guint32       opid,
			       GCancellable *cancellable,
			       const gchar   *query)
{
	GError *error = NULL;
	GSList *cards = NULL;

	e_book_backend_sync_get_contact_list (E_BOOK_BACKEND_SYNC (backend), book, cancellable, query, &cards, &error);

	e_data_book_respond_get_contact_list (book, opid, error, cards);

	g_slist_foreach (cards, (GFunc) g_free, NULL);
	g_slist_free (cards);
}

static void
book_backend_authenticate_user (EBookBackend *backend,
				GCancellable *cancellable,
				ECredentials *credentials)
{
	GError *error = NULL;

	e_book_backend_sync_authenticate_user (E_BOOK_BACKEND_SYNC (backend), cancellable, credentials, &error);

	e_book_backend_notify_opened (backend, error);
}

static gboolean
book_backend_sync_get_backend_property (EBookBackendSync *backend, EDataBook *book, GCancellable *cancellable, const gchar *prop_name, gchar **prop_value, GError **error)
{
	/* to indicate to pass to the EBookBackend parent class */
	return FALSE;
}

static gboolean
book_backend_sync_set_backend_property (EBookBackendSync *backend, EDataBook *book, GCancellable *cancellable, const gchar *prop_name, const gchar *prop_value, GError **error)
{
	/* to indicate to pass to the EBookBackend parent class */
	return FALSE;
}

static void
e_book_backend_sync_init (EBookBackendSync *backend)
{
	EBookBackendSyncPrivate *priv;

	priv          = g_new0 (EBookBackendSyncPrivate, 1);

	backend->priv = priv;
}

static void
e_book_backend_sync_dispose (GObject *object)
{
	EBookBackendSync *backend;

	backend = E_BOOK_BACKEND_SYNC (object);

	if (backend->priv) {
		g_free (backend->priv);

		backend->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_book_backend_sync_class_init (EBookBackendSyncClass *klass)
{
	GObjectClass *object_class;
	EBookBackendClass *backend_class = E_BOOK_BACKEND_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class = (GObjectClass *) klass;
	object_class->dispose = e_book_backend_sync_dispose;

	backend_class->open			= book_backend_open;
	backend_class->authenticate_user	= book_backend_authenticate_user;
	backend_class->remove			= book_backend_remove;
	backend_class->refresh			= book_backend_refresh;
	backend_class->get_backend_property	= book_backend_get_backend_property;
	backend_class->set_backend_property	= book_backend_set_backend_property;
	backend_class->create_contact		= book_backend_create_contact;
	backend_class->remove_contacts		= book_backend_remove_contacts;
	backend_class->modify_contact		= book_backend_modify_contact;
	backend_class->get_contact		= book_backend_get_contact;
	backend_class->get_contact_list		= book_backend_get_contact_list;

	klass->get_backend_property_sync	= book_backend_sync_get_backend_property;
	klass->set_backend_property_sync	= book_backend_sync_set_backend_property;
}

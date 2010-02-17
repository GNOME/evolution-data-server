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
 * Return value: %TRUE.
 **/
gboolean
e_book_backend_sync_construct (EBookBackendSync *backend)
{
	return TRUE;
}

/**
 * e_book_backend_sync_create_contact:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @opid: the unique ID of the operation
 * @vcard: a VCard representation of a contact
 * @contact: a pointer to a location to store the resulting #EContact
 *
 * Creates a new contact with the contents of @vcard in @backend.
 *
 * Return value: An #EBookBackendSyncStatus indicating the outcome of the operation.
 **/
EBookBackendSyncStatus
e_book_backend_sync_create_contact (EBookBackendSync *backend,
				    EDataBook *book,
				    guint32 opid,
				    const gchar *vcard,
				    EContact **contact)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (vcard, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (contact, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->create_contact_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->create_contact_sync) (backend, book, opid, vcard, contact);
}

/**
 * e_book_backend_sync_remove:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @opid: the unique ID of the operation
 *
 * Remove @book's database and storage overhead from the storage
 * medium. This will delete all contacts in @book.
 *
 * Return value: An #EBookBackendSyncStatus indicating the outcome of the operation.
 **/
EBookBackendSyncStatus
e_book_backend_sync_remove (EBookBackendSync *backend,
			    EDataBook *book,
			    guint32 opid)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_sync) (backend, book, opid);
}

/**
 * e_book_backend_sync_remove_contacts:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @opid: the unique ID of the operation
 * @id_list: a #GList of pointers to unique contact ID strings
 * @removed_ids: a pointer to a location to store a list of the contacts actually removed
 *
 * Removes the contacts specified by @id_list from @backend. The returned list
 * of removed contacts is in the same format as the passed-in list, and must be
 * freed by the caller.
 *
 * Return value: An #EBookBackendSyncStatus indicating the outcome of the operation.
 **/
EBookBackendSyncStatus
e_book_backend_sync_remove_contacts (EBookBackendSync *backend,
				     EDataBook *book,
				     guint32 opid,
				     GList *id_list,
				     GList **removed_ids)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (id_list, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (removed_ids, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_contacts_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_contacts_sync) (backend, book, opid, id_list, removed_ids);
}

/**
 * e_book_backend_sync_modify_contact:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @opid: the unique ID of the operation
 * @vcard: the string representation of a contact
 * @contact: a pointer to a location to store the resulting #EContact
 *
 * Modifies the contact specified by the ID embedded in @vcard, to
 * reflect the full contents of @vcard.
 *
 * Return value: An #EBookBackendSyncStatus indicating the outcome of the operation.
 **/
EBookBackendSyncStatus
e_book_backend_sync_modify_contact (EBookBackendSync *backend,
				    EDataBook *book,
				    guint32 opid,
				    const gchar *vcard,
				    EContact **contact)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (vcard, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (contact, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->modify_contact_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->modify_contact_sync) (backend, book, opid, vcard, contact);
}

/**
 * e_book_backend_sync_get_contact:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @opid: the unique ID of the operation
 * @id: a unique contact ID
 * @vcard: a pointer to a location to store the resulting VCard string
 *
 * Gets a contact from @book.
 *
 * Return value: An #EBookBackendSyncStatus indicating the outcome of the operation.
 **/
EBookBackendSyncStatus
e_book_backend_sync_get_contact (EBookBackendSync *backend,
				 EDataBook *book,
				 guint32 opid,
				 const gchar *id,
				 gchar **vcard)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (id, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (vcard, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_sync) (backend, book, opid, id, vcard);
}

/**
 * e_book_backend_sync_get_contact_list:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @opid: the unique ID of the operation
 * @query: an s-expression of the query to perform
 * @contacts: a pointer to a location to store the resulting list of VCard strings
 *
 * Gets a list of contacts from @book. The list and its elements must be freed
 * by the caller.
 *
 * Return value: An #EBookBackendSyncStatus indicating the outcome of the operation.
 **/
EBookBackendSyncStatus
e_book_backend_sync_get_contact_list (EBookBackendSync *backend,
				      EDataBook *book,
				      guint32 opid,
				      const gchar *query,
				      GList **contacts)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (query, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (contacts, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_list_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_list_sync) (backend, book, opid, query, contacts);
}

/**
 * e_book_backend_sync_get_changes:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @opid: the unique ID of the operation
 * @change_id: a unique changes ID
 * @changes: a pointer to a location to store the resulting list of changes
 *
 * Gets the changes made to @book since the last call to this function.
 * The returned list will contain items of CORBA type
 * #GNOME_Evolution_Addressbook_BookChangeItem.
 *
 * Return value: An #EBookBackendSyncStatus indicating the outcome of the operation.
 **/
EBookBackendSyncStatus
e_book_backend_sync_get_changes (EBookBackendSync *backend,
				 EDataBook *book,
				 guint32 opid,
				 const gchar *change_id,
				 GList **changes)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (change_id, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (changes, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_changes_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_changes_sync) (backend, book, opid, change_id, changes);
}

/**
 * e_book_backend_sync_authenticate_user:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @opid: the unique ID of the operation
 * @user: the user's name
 * @passwd: the user's password
 * @auth_method: the authentication method desired
 *
 * Authenticates @user against @book.
 *
 * Return value: An #EBookBackendSyncStatus indicating the outcome of the operation.
 **/
EBookBackendSyncStatus
e_book_backend_sync_authenticate_user (EBookBackendSync *backend,
				       EDataBook *book,
				       guint32 opid,
				       const gchar *user,
				       const gchar *passwd,
				       const gchar *auth_method)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (user && passwd && auth_method, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->authenticate_user_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->authenticate_user_sync) (backend, book, opid, user, passwd, auth_method);
}

/**
 * e_book_backend_sync_get_required_fields:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @opid: the unique ID of the operation
 * @fields: a pointer to a location to store the fields
 *
 * Gets a list of the fields required for all contacts in @book. The
 * fields are represented by strings from #e_contact_field_name. The list
 * and its contents must be freed by the caller.
 *
 * Return value: An #EBookBackendSyncStatus indicating the outcome of the operation.
 **/
EBookBackendSyncStatus
e_book_backend_sync_get_required_fields (EBookBackendSync *backend,
					  EDataBook *book,
					  guint32 opid,
					  GList **fields)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (fields, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_required_fields_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_required_fields_sync) (backend, book, opid, fields);
}

/**
 * e_book_backend_sync_get_supported_fields:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @opid: the unique ID of the operation
 * @fields: a pointer to a location to store the fields
 *
 * Gets a list of the fields supported for contacts in @book. Other fields
 * may not be stored. The fields are represented by strings from #e_contact_field_name.
 * The list and its contents must be freed by the caller.
 *
 * Return value: An #EBookBackendSyncStatus indicating the outcome of the operation.
 **/
EBookBackendSyncStatus
e_book_backend_sync_get_supported_fields (EBookBackendSync *backend,
					  EDataBook *book,
					  guint32 opid,
					  GList **fields)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (fields, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_supported_fields_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_supported_fields_sync) (backend, book, opid, fields);
}

/**
 * e_book_backend_sync_get_supported_auth_methods:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @opid: the unique ID of the operation
 * @methods: a pointer to a location to store the methods
 *
 * Gets a list of the authentication methods supported by @book. The
 * methods are represented by strings. The list and its contents must
 * be freed by the caller.
 *
 * Return value: An #EBookBackendSyncStatus indicating the outcome of the operation.
 **/
EBookBackendSyncStatus
e_book_backend_sync_get_supported_auth_methods (EBookBackendSync *backend,
						EDataBook *book,
						guint32 opid,
						GList **methods)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (methods, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_supported_auth_methods_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_supported_auth_methods_sync) (backend, book, opid, methods);
}

static void
_e_book_backend_remove (EBookBackend *backend,
			EDataBook    *book,
			guint32       opid)
{
	EBookBackendSyncStatus status;

	status = e_book_backend_sync_remove (E_BOOK_BACKEND_SYNC (backend), book, opid);

	e_data_book_respond_remove (book, opid, status);
}

static void
_e_book_backend_create_contact (EBookBackend *backend,
				EDataBook    *book,
				guint32       opid,
				const gchar   *vcard)
{
	EBookBackendSyncStatus status;
	EContact *contact = NULL;

	status = e_book_backend_sync_create_contact (E_BOOK_BACKEND_SYNC (backend), book, opid, vcard, &contact);

	e_data_book_respond_create (book, opid, status, contact);

	if (contact)
		g_object_unref (contact);
}

static void
_e_book_backend_remove_contacts (EBookBackend *backend,
				 EDataBook    *book,
				 guint32       opid,
				 GList        *id_list)
{
	EBookBackendSyncStatus status;
	GList *ids = NULL;

	status = e_book_backend_sync_remove_contacts (E_BOOK_BACKEND_SYNC (backend), book, opid, id_list, &ids);

	e_data_book_respond_remove_contacts (book, opid, status, ids);

	if (ids)
		g_list_free (ids);
}

static void
_e_book_backend_modify_contact (EBookBackend *backend,
				EDataBook    *book,
				guint32       opid,
				const gchar   *vcard)
{
	EBookBackendSyncStatus status;
	EContact *contact = NULL;

	status = e_book_backend_sync_modify_contact (E_BOOK_BACKEND_SYNC (backend), book, opid, vcard, &contact);

	e_data_book_respond_modify (book, opid, status, contact);

	if (contact)
		g_object_unref (contact);
}

static void
_e_book_backend_get_contact (EBookBackend *backend,
			     EDataBook    *book,
			     guint32       opid,
			     const gchar   *id)
{
	EBookBackendSyncStatus status;
	gchar *vcard = NULL;

	status = e_book_backend_sync_get_contact (E_BOOK_BACKEND_SYNC (backend), book, opid, id, &vcard);

	e_data_book_respond_get_contact (book, opid, status, vcard);

	if (vcard)
		g_free (vcard);
}

static void
_e_book_backend_get_contact_list (EBookBackend *backend,
				  EDataBook    *book,
				  guint32       opid,
				  const gchar   *query)
{
	EBookBackendSyncStatus status;
	GList *cards = NULL;

	status = e_book_backend_sync_get_contact_list (E_BOOK_BACKEND_SYNC (backend), book, opid, query, &cards);

	e_data_book_respond_get_contact_list (book, opid, status, cards);
}

static void
_e_book_backend_get_changes (EBookBackend *backend,
			     EDataBook    *book,
			     guint32       opid,
			     const gchar   *change_id)
{
	EBookBackendSyncStatus status;
	GList *changes = NULL;

	status = e_book_backend_sync_get_changes (E_BOOK_BACKEND_SYNC (backend), book, opid, change_id, &changes);

	e_data_book_respond_get_changes (book, opid, status, changes);
}

static void
_e_book_backend_authenticate_user (EBookBackend *backend,
				   EDataBook    *book,
				   guint32       opid,
				   const gchar   *user,
				   const gchar   *passwd,
				   const gchar   *auth_method)
{
	EBookBackendSyncStatus status;

	status = e_book_backend_sync_authenticate_user (E_BOOK_BACKEND_SYNC (backend), book, opid, user, passwd, auth_method);

	e_data_book_respond_authenticate_user (book, opid, status);
}

static void
_e_book_backend_get_required_fields (EBookBackend *backend,
				      EDataBook    *book,
				      guint32       opid)
{
	EBookBackendSyncStatus status;
	GList *fields = NULL;

	status = e_book_backend_sync_get_required_fields (E_BOOK_BACKEND_SYNC (backend), book, opid, &fields);

	e_data_book_respond_get_required_fields (book, opid, status, fields);

	if (fields) {
		g_list_foreach (fields, (GFunc)g_free, NULL);
		g_list_free (fields);
	}
}

static void
_e_book_backend_get_supported_fields (EBookBackend *backend,
				      EDataBook    *book,
				      guint32       opid)
{
	EBookBackendSyncStatus status;
	GList *fields = NULL;

	status = e_book_backend_sync_get_supported_fields (E_BOOK_BACKEND_SYNC (backend), book, opid, &fields);

	e_data_book_respond_get_supported_fields (book, opid, status, fields);

	if (fields) {
		g_list_foreach (fields, (GFunc)g_free, NULL);
		g_list_free (fields);
	}
}

static void
_e_book_backend_get_supported_auth_methods (EBookBackend *backend,
					    EDataBook    *book,
					    guint32       opid)
{
	EBookBackendSyncStatus status;
	GList *methods = NULL;

	status = e_book_backend_sync_get_supported_auth_methods (E_BOOK_BACKEND_SYNC (backend), book, opid, &methods);

	e_data_book_respond_get_supported_auth_methods (book, opid, status, methods);

	if (methods) {
		g_list_foreach (methods, (GFunc)g_free, NULL);
		g_list_free (methods);
	}
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

	backend_class->remove = _e_book_backend_remove;
	backend_class->create_contact = _e_book_backend_create_contact;
	backend_class->remove_contacts = _e_book_backend_remove_contacts;
	backend_class->modify_contact = _e_book_backend_modify_contact;
	backend_class->get_contact = _e_book_backend_get_contact;
	backend_class->get_contact_list = _e_book_backend_get_contact_list;
	backend_class->get_changes = _e_book_backend_get_changes;
	backend_class->authenticate_user = _e_book_backend_authenticate_user;
	backend_class->get_required_fields = _e_book_backend_get_required_fields;
	backend_class->get_supported_fields = _e_book_backend_get_supported_fields;
	backend_class->get_supported_auth_methods = _e_book_backend_get_supported_auth_methods;

	object_class->dispose = e_book_backend_sync_dispose;
}

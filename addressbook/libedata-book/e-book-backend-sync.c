/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-data-book-marshal.h"
#include "e-book-backend-sync.h"

struct _EBookBackendSyncPrivate {
  int mumble;
};

static GObjectClass *parent_class;

gboolean
e_book_backend_sync_construct (EBookBackendSync *backend)
{
	return TRUE;
}

EBookBackendSyncStatus
e_book_backend_sync_create_contact (EBookBackendSync *backend,
				    EDataBook *book,
				    const char *vcard,
				    EContact **contact)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (vcard, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (contact, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->create_contact_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->create_contact_sync) (backend, book, vcard, contact);
}

EBookBackendSyncStatus
e_book_backend_sync_remove (EBookBackendSync *backend,
			    EDataBook *book)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_sync) (backend, book);
}

EBookBackendSyncStatus
e_book_backend_sync_remove_contacts (EBookBackendSync *backend,
				     EDataBook *book,
				     GList *id_list,
				     GList **removed_ids)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (id_list, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (removed_ids, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_contacts_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_contacts_sync) (backend, book, id_list, removed_ids);
}

EBookBackendSyncStatus
e_book_backend_sync_modify_contact (EBookBackendSync *backend,
				    EDataBook *book,
				    const char *vcard,
				    EContact **contact)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (vcard, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (contact, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->modify_contact_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->modify_contact_sync) (backend, book, vcard, contact);
}

EBookBackendSyncStatus
e_book_backend_sync_get_contact (EBookBackendSync *backend,
				 EDataBook *book,
				 const char *id,
				 char **vcard)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (id, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (vcard, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_sync) (backend, book, id, vcard);
}

EBookBackendSyncStatus
e_book_backend_sync_get_contact_list (EBookBackendSync *backend,
				      EDataBook *book,
				      const char *query,
				      GList **contacts)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (query, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (contacts, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_list_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_list_sync) (backend, book, query, contacts);
}

EBookBackendSyncStatus
e_book_backend_sync_get_changes (EBookBackendSync *backend,
				 EDataBook *book,
				 const char *change_id,
				 GList **changes)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (change_id, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (changes, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_changes_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_changes_sync) (backend, book, change_id, changes);
}

EBookBackendSyncStatus
e_book_backend_sync_authenticate_user (EBookBackendSync *backend,
				       EDataBook *book,
				       const char *user,
				       const char *passwd,
				       const char *auth_method)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (user && passwd && auth_method, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->authenticate_user_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->authenticate_user_sync) (backend, book, user, passwd, auth_method);
}

EBookBackendSyncStatus
e_book_backend_sync_get_supported_fields (EBookBackendSync *backend,
					  EDataBook *book,
					  GList **fields)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (fields, GNOME_Evolution_Addressbook_OtherError);
	
	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_supported_fields_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_supported_fields_sync) (backend, book, fields);
}

EBookBackendSyncStatus
e_book_backend_sync_get_supported_auth_methods (EBookBackendSync *backend,
						EDataBook *book,
						GList **methods)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (methods, GNOME_Evolution_Addressbook_OtherError);

	g_assert (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_supported_auth_methods_sync);

	return (* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_supported_auth_methods_sync) (backend, book, methods);
}

static void
_e_book_backend_remove (EBookBackend *backend,
			EDataBook    *book)
{
	EBookBackendSyncStatus status;

	status = e_book_backend_sync_remove (E_BOOK_BACKEND_SYNC (backend), book);

	e_data_book_respond_remove (book, status);
}

static void
_e_book_backend_create_contact (EBookBackend *backend,
				EDataBook    *book,
				const char *vcard)
{
	EBookBackendSyncStatus status;
	EContact *contact;

	status = e_book_backend_sync_create_contact (E_BOOK_BACKEND_SYNC (backend), book, vcard, &contact);

	e_data_book_respond_create (book, status, contact);

	g_object_unref (contact);
}

static void
_e_book_backend_remove_contacts (EBookBackend *backend,
				 EDataBook    *book,
				 GList      *id_list)
{
	EBookBackendSyncStatus status;
	GList *ids = NULL;

	status = e_book_backend_sync_remove_contacts (E_BOOK_BACKEND_SYNC (backend), book, id_list, &ids);

	e_data_book_respond_remove_contacts (book, status, ids);

	g_list_free (ids);
}

static void
_e_book_backend_modify_contact (EBookBackend *backend,
				EDataBook    *book,
				const char *vcard)
{
	EBookBackendSyncStatus status;
	EContact *contact;

	status = e_book_backend_sync_modify_contact (E_BOOK_BACKEND_SYNC (backend), book, vcard, &contact);

	e_data_book_respond_modify (book, status, contact);

	g_object_unref (contact);
}

static void
_e_book_backend_get_contact (EBookBackend *backend,
			     EDataBook    *book,
			     const char *id)
{
	EBookBackendSyncStatus status;
	char *vcard;

	status = e_book_backend_sync_get_contact (E_BOOK_BACKEND_SYNC (backend), book, id, &vcard);

	e_data_book_respond_get_contact (book, status, vcard);

	g_free (vcard);
}

static void
_e_book_backend_get_contact_list (EBookBackend *backend,
				  EDataBook    *book,
				  const char *query)
{
	EBookBackendSyncStatus status;
	GList *cards = NULL;

	status = e_book_backend_sync_get_contact_list (E_BOOK_BACKEND_SYNC (backend), book, query, &cards);

	e_data_book_respond_get_contact_list (book, status, cards);
}

static void
_e_book_backend_get_changes (EBookBackend *backend,
			     EDataBook    *book,
			     const char *change_id)
{
	EBookBackendSyncStatus status;
	GList *changes = NULL;

	status = e_book_backend_sync_get_changes (E_BOOK_BACKEND_SYNC (backend), book, change_id, &changes);

	e_data_book_respond_get_changes (book, status, changes);

	/* XXX free view? */
}

static void
_e_book_backend_authenticate_user (EBookBackend *backend,
				   EDataBook    *book,
				   const char *user,
				   const char *passwd,
				   const char *auth_method)
{
	EBookBackendSyncStatus status;

	status = e_book_backend_sync_authenticate_user (E_BOOK_BACKEND_SYNC (backend), book, user, passwd, auth_method);

	e_data_book_respond_authenticate_user (book, status);
}

static void
_e_book_backend_get_supported_fields (EBookBackend *backend,
				      EDataBook    *book)
{
	EBookBackendSyncStatus status;
	GList *fields = NULL;

	status = e_book_backend_sync_get_supported_fields (E_BOOK_BACKEND_SYNC (backend), book, &fields);

	e_data_book_respond_get_supported_fields (book, status, fields);

	g_list_foreach (fields, (GFunc)g_free, NULL);
	g_list_free (fields);
}

static void
_e_book_backend_get_supported_auth_methods (EBookBackend *backend,
					    EDataBook    *book)
{
	EBookBackendSyncStatus status;
	GList *methods = NULL;

	status = e_book_backend_sync_get_supported_auth_methods (E_BOOK_BACKEND_SYNC (backend), book, &methods);

	e_data_book_respond_get_supported_auth_methods (book, status, methods);

	g_list_foreach (methods, (GFunc)g_free, NULL);
	g_list_free (methods);
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
	backend_class->get_supported_fields = _e_book_backend_get_supported_fields;
	backend_class->get_supported_auth_methods = _e_book_backend_get_supported_auth_methods;

	object_class->dispose = e_book_backend_sync_dispose;
}

/**
 * e_book_backend_get_type:
 */
GType
e_book_backend_sync_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (EBookBackendSyncClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_book_backend_sync_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (EBookBackendSync),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_book_backend_sync_init
		};

		type = g_type_register_static (E_TYPE_BOOK_BACKEND, "EBookBackendSync", &info, 0);
	}

	return type;
}

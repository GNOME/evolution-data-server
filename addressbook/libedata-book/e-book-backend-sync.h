/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 */

#ifndef __E_BOOK_BACKEND_SYNC_H__
#define __E_BOOK_BACKEND_SYNC_H__

#include <glib.h>
#include <libedata-book/e-data-book-types.h>
#include <libedata-book/e-book-backend.h>

G_BEGIN_DECLS

#define E_TYPE_BOOK_BACKEND_SYNC         (e_book_backend_sync_get_type ())
#define E_BOOK_BACKEND_SYNC(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_SYNC, EBookBackendSync))
#define E_BOOK_BACKEND_SYNC_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_SYNC, EBookBackendSyncClass))
#define E_IS_BOOK_BACKEND_SYNC(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_SYNC))
#define E_IS_BOOK_BACKEND_SYNC_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_SYNC))
#define E_BOOK_BACKEND_SYNC_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((k), E_TYPE_BOOK_BACKEND_SYNC, EBookBackendSyncClass))

typedef struct _EBookBackendSyncPrivate EBookBackendSyncPrivate;

typedef GNOME_Evolution_Addressbook_CallStatus EBookBackendSyncStatus;

struct _EBookBackendSync {
	EBookBackend parent_object;
	EBookBackendSyncPrivate *priv;
};

struct _EBookBackendSyncClass {
	EBookBackendClass parent_class;

	/* Virtual methods */
	EBookBackendSyncStatus (*remove_sync) (EBookBackendSync *backend, EDataBook *book, guint32 opid);
	EBookBackendSyncStatus (*create_contact_sync)  (EBookBackendSync *backend, EDataBook *book,
							guint32 opid,
							const gchar *vcard, EContact **contact);
	EBookBackendSyncStatus (*remove_contacts_sync) (EBookBackendSync *backend, EDataBook *book,
							guint32 opid,
							GList *id_list, GList **removed_ids);
	EBookBackendSyncStatus (*modify_contact_sync)  (EBookBackendSync *backend, EDataBook *book,
							guint32 opid,
							const gchar *vcard, EContact **contact);
	EBookBackendSyncStatus (*get_contact_sync) (EBookBackendSync *backend, EDataBook *book,
						    guint32 opid,
						    const gchar *id, gchar **vcard);
	EBookBackendSyncStatus (*get_contact_list_sync) (EBookBackendSync *backend, EDataBook *book,
							 guint32 opid,
							 const gchar *query, GList **contacts);
	EBookBackendSyncStatus (*get_changes_sync) (EBookBackendSync *backend, EDataBook *book,
						    guint32 opid,
						    const gchar *change_id, GList **changes);
	EBookBackendSyncStatus (*authenticate_user_sync) (EBookBackendSync *backend, EDataBook *book,
							  guint32 opid,
							  const gchar *user,
							  const gchar *passwd,
							  const gchar *auth_method);

	EBookBackendSyncStatus (*get_required_fields_sync) (EBookBackendSync *backend, EDataBook *book,
							     guint32 opid,
							     GList **fields);

	EBookBackendSyncStatus (*get_supported_fields_sync) (EBookBackendSync *backend, EDataBook *book,
							     guint32 opid,
							     GList **fields);
	EBookBackendSyncStatus (*get_supported_auth_methods_sync) (EBookBackendSync *backend, EDataBook *book,
								   guint32 opid,
								   GList **methods);

	/* Padding for future expansion */
	void (*_pas_reserved0) (void);
	void (*_pas_reserved1) (void);
	void (*_pas_reserved2) (void);
	void (*_pas_reserved3) (void);
	void (*_pas_reserved4) (void);

};

gboolean    e_book_backend_sync_construct                (EBookBackendSync             *backend);

GType       e_book_backend_sync_get_type                 (void);

EBookBackendSyncStatus e_book_backend_sync_remove  (EBookBackendSync *backend, EDataBook *book, guint32 opid);
EBookBackendSyncStatus e_book_backend_sync_create_contact  (EBookBackendSync *backend, EDataBook *book, guint32 opid, const gchar *vcard, EContact **contact);
EBookBackendSyncStatus e_book_backend_sync_remove_contacts (EBookBackendSync *backend, EDataBook *book, guint32 opid, GList *id_list, GList **removed_ids);
EBookBackendSyncStatus e_book_backend_sync_modify_contact  (EBookBackendSync *backend, EDataBook *book, guint32 opid, const gchar *vcard, EContact **contact);
EBookBackendSyncStatus e_book_backend_sync_get_contact (EBookBackendSync *backend, EDataBook *book, guint32 opid, const gchar *id, gchar **vcard);
EBookBackendSyncStatus e_book_backend_sync_get_contact_list (EBookBackendSync *backend, EDataBook *book, guint32 opid, const gchar *query, GList **contacts);
EBookBackendSyncStatus e_book_backend_sync_get_changes (EBookBackendSync *backend, EDataBook *book, guint32 opid, const gchar *change_id, GList **changes);
EBookBackendSyncStatus e_book_backend_sync_authenticate_user (EBookBackendSync *backend, EDataBook *book, guint32 opid, const gchar *user, const gchar *passwd, const gchar *auth_method);

EBookBackendSyncStatus e_book_backend_sync_get_required_fields (EBookBackendSync *backend, EDataBook *book, guint32 opid, GList **fields);
EBookBackendSyncStatus e_book_backend_sync_get_supported_fields (EBookBackendSync *backend, EDataBook *book, guint32 opid, GList **fields);
EBookBackendSyncStatus e_book_backend_sync_get_supported_auth_methods (EBookBackendSync *backend, EDataBook *book, guint32 opid, GList **methods);

G_END_DECLS

#endif /* __E_BOOK_BACKEND_SYNC_H__ */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * An abstract class which defines the API to a given backend.
 * There will be one EBookBackend object for every URI which is loaded.
 *
 * Two people will call into the EBookBackend API:
 *
 * 1. The PASBookFactory, when it has been asked to load a book.
 *    It will create a new EBookBackend if one is not already running
 *    for the requested URI.  It will call e_book_backend_add_client to
 *    add a new client to an existing EBookBackend server.
 *
 * 2. A PASBook, when a client has requested an operation on the
 *    GNOME_Evolution_Addressbook_Book interface.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifndef __E_BOOK_BACKEND_H__
#define __E_BOOK_BACKEND_H__

#include <glib.h>
#include <glib-object.h>
#include <libebook/e-contact.h>
#include <libedata-book/e-data-book-types.h>
#include <libedata-book/e-data-book.h>
#include <libedataserver/e-list.h>
#include <libedataserver/e-source.h>

G_BEGIN_DECLS

#define E_TYPE_BOOK_BACKEND         (e_book_backend_get_type ())
#define E_BOOK_BACKEND(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND, EBookBackend))
#define E_BOOK_BACKEND_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND, EBookBackendClass))
#define E_IS_BOOK_BACKEND(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND))
#define E_IS_BOOK_BACKEND_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND))
#define E_BOOK_BACKEND_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((k), E_TYPE_BOOK_BACKEND, EBookBackendClass))

typedef struct _EBookBackendPrivate EBookBackendPrivate;

struct _EBookBackend {
	GObject parent_object;
	EBookBackendPrivate *priv;
};

struct _EBookBackendClass {
	GObjectClass parent_class;

	/* Virtual methods */
	GNOME_Evolution_Addressbook_CallStatus (*load_source) (EBookBackend *backend, ESource *source, gboolean only_if_exists);
	void (*remove) (EBookBackend *backend, EDataBook *book, guint32 opid);
        gchar *(*get_static_capabilities) (EBookBackend *backend);

	void (*create_contact)  (EBookBackend *backend, EDataBook *book, guint32 opid, const gchar *vcard);
	void (*remove_contacts) (EBookBackend *backend, EDataBook *book, guint32 opid, GList *id_list);
	void (*modify_contact)  (EBookBackend *backend, EDataBook *book, guint32 opid, const gchar *vcard);
	void (*get_contact) (EBookBackend *backend, EDataBook *book, guint32 opid, const gchar *id);
	void (*get_contact_list) (EBookBackend *backend, EDataBook *book, guint32 opid, const gchar *query);
	void (*start_book_view) (EBookBackend *backend, EDataBookView *book_view);
	void (*stop_book_view) (EBookBackend *backend, EDataBookView *book_view);
	void (*get_changes) (EBookBackend *backend, EDataBook *book, guint32 opid, const gchar *change_id);
	void (*authenticate_user) (EBookBackend *backend, EDataBook *book, guint32 opid, const gchar *user, const gchar *passwd, const gchar *auth_method);
	void (*get_required_fields) (EBookBackend *backend, EDataBook *bokk, guint32 opid);
	void (*get_supported_fields) (EBookBackend *backend, EDataBook *book, guint32 opid);
	void (*get_supported_auth_methods) (EBookBackend *backend, EDataBook *book, guint32 opid);
	GNOME_Evolution_Addressbook_CallStatus (*cancel_operation) (EBookBackend *backend, EDataBook *book);
	void (*set_mode) (EBookBackend *backend, GNOME_Evolution_Addressbook_BookMode mode);

	/* Notification signals */
	void (* last_client_gone) (EBookBackend *backend);

	void (*sync) (EBookBackend *backend);

	/* Padding for future expansion */
	void (*_pas_reserved1) (void);
	void (*_pas_reserved2) (void);
	void (*_pas_reserved3) (void);
	void (*_pas_reserved4) (void);
};

gboolean    e_book_backend_construct                (EBookBackend             *backend);

GNOME_Evolution_Addressbook_CallStatus
            e_book_backend_load_source              (EBookBackend             *backend,
						     ESource                  *source,
						     gboolean                  only_if_exists);
ESource    *e_book_backend_get_source               (EBookBackend             *backend);

gboolean    e_book_backend_add_client               (EBookBackend             *backend,
						     EDataBook                *book);
void        e_book_backend_remove_client            (EBookBackend             *backend,
						     EDataBook                *book);
gboolean    e_book_backend_has_out_of_proc_clients (EBookBackend *backend);
gchar       *e_book_backend_get_static_capabilities  (EBookBackend             *backend);

gboolean    e_book_backend_is_loaded                (EBookBackend             *backend);

gboolean    e_book_backend_is_writable              (EBookBackend             *backend);

gboolean    e_book_backend_is_removed               (EBookBackend             *backend);

void        e_book_backend_open                     (EBookBackend             *backend,
						     EDataBook                *book,
						     guint32                   opid,
						     gboolean                  only_if_exists);
void        e_book_backend_remove                   (EBookBackend             *backend,
						     EDataBook                *book,
						     guint32                   opid);
void        e_book_backend_create_contact           (EBookBackend             *backend,
						     EDataBook                *book,
						     guint32                   opid,
						     const gchar               *vcard);
void        e_book_backend_remove_contacts          (EBookBackend             *backend,
						     EDataBook                *book,
						     guint32                   opid,
						     GList                    *id_list);
void        e_book_backend_modify_contact           (EBookBackend             *backend,
						     EDataBook                *book,
						     guint32                   opid,
						     const gchar               *vcard);
void        e_book_backend_get_contact              (EBookBackend             *backend,
						     EDataBook                *book,
						     guint32                   opid,
						     const gchar               *id);
void        e_book_backend_get_contact_list         (EBookBackend             *backend,
						     EDataBook                *book,
						     guint32                   opid,
						     const gchar               *query);
void        e_book_backend_get_changes              (EBookBackend             *backend,
						     EDataBook                *book,
						     guint32                   opid,
						     const gchar               *change_id);
void        e_book_backend_authenticate_user        (EBookBackend             *backend,
						     EDataBook                *book,
						     guint32                   opid,
						     const gchar               *user,
						     const gchar               *passwd,
						     const gchar               *auth_method);
void        e_book_backend_get_supported_fields     (EBookBackend             *backend,
						     EDataBook                *book,
						     guint32                   opid);

void        e_book_backend_get_required_fields      (EBookBackend             *backend,
						     EDataBook                *book,
						     guint32                   opid);

void        e_book_backend_get_supported_auth_methods (EBookBackend           *backend,
						       EDataBook              *book,
						       guint32                 opid);
GNOME_Evolution_Addressbook_CallStatus e_book_backend_cancel_operation (EBookBackend             *backend,
									EDataBook                *book);
void        e_book_backend_set_mode (EBookBackend           *backend,
				     GNOME_Evolution_Addressbook_BookMode                mode);

void        e_book_backend_start_book_view            (EBookBackend           *backend,
						       EDataBookView          *view);
void        e_book_backend_stop_book_view             (EBookBackend           *backend,
						       EDataBookView          *view);

void        e_book_backend_add_book_view              (EBookBackend           *backend,
						       EDataBookView          *view);

void        e_book_backend_remove_book_view           (EBookBackend           *backend,
						       EDataBookView          *view);

EList      *e_book_backend_get_book_views             (EBookBackend           *backend);

void        e_book_backend_notify_update              (EBookBackend           *backend,
						       EContact               *contact);
void        e_book_backend_notify_remove              (EBookBackend           *backend,
						       const gchar             *id);
void        e_book_backend_notify_complete            (EBookBackend           *backend);
void        e_book_backend_notify_writable            (EBookBackend *backend, gboolean is_writable);
void        e_book_backend_notify_connection_status   (EBookBackend *backend, gboolean is_online);
void        e_book_backend_notify_auth_required       (EBookBackend *backend);
void        e_book_backend_sync                       (EBookBackend *backend);

GType       e_book_backend_get_type                 (void);

/* protected functions for subclasses */
void        e_book_backend_set_is_loaded            (EBookBackend             *backend,
						     gboolean                  is_loaded);
void        e_book_backend_set_is_writable          (EBookBackend             *backend,
						     gboolean                  is_writable);
void        e_book_backend_set_is_removed           (EBookBackend             *backend,
						     gboolean                  is_removed);

/* useful for implementing _get_changes in backends */
GNOME_Evolution_Addressbook_BookChangeItem* e_book_backend_change_add_new     (const gchar *vcard);
GNOME_Evolution_Addressbook_BookChangeItem* e_book_backend_change_modify_new  (const gchar *vcard);
GNOME_Evolution_Addressbook_BookChangeItem* e_book_backend_change_delete_new  (const gchar *id);

G_END_DECLS

#endif /* __E_BOOK_BACKEND_H__ */


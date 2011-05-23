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
#include <gio/gio.h>

#include <libebook/e-contact.h>
#include <libedata-book/e-data-book-types.h>
#include <libedata-book/e-data-book.h>
#include <libedataserver/e-source.h>
#include <libedataserver/e-credentials.h>

G_BEGIN_DECLS

#define E_TYPE_BOOK_BACKEND         (e_book_backend_get_type ())
#define E_BOOK_BACKEND(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND, EBookBackend))
#define E_BOOK_BACKEND_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND, EBookBackendClass))
#define E_IS_BOOK_BACKEND(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND))
#define E_IS_BOOK_BACKEND_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND))
#define E_BOOK_BACKEND_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((k), E_TYPE_BOOK_BACKEND, EBookBackendClass))

#define CLIENT_BACKEND_PROPERTY_OPENED			"opened"
#define CLIENT_BACKEND_PROPERTY_OPENING			"opening"
#define CLIENT_BACKEND_PROPERTY_ONLINE			"online"
#define CLIENT_BACKEND_PROPERTY_READONLY		"readonly"
#define CLIENT_BACKEND_PROPERTY_CACHE_DIR		"cache-dir"
#define CLIENT_BACKEND_PROPERTY_CAPABILITIES		"capabilities"
#define BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS		"required-fields"
#define BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS		"supported-fields"
#define BOOK_BACKEND_PROPERTY_SUPPORTED_AUTH_METHODS	"supported-auth-methods"

typedef struct _EBookBackendPrivate EBookBackendPrivate;

struct _EBookBackend {
	GObject parent_object;
	EBookBackendPrivate *priv;
};

struct _EBookBackendClass {
	GObjectClass parent_class;

	/* Virtual methods */
        void	(* get_backend_property)	(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *prop_name);
        void	(* set_backend_property)	(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *prop_name, const gchar *prop_value);

	void	(* open)			(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, gboolean only_if_exists);
	void	(* remove)			(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable);
	void	(* set_online)			(EBookBackend *backend, gboolean is_online);
	void	(* authenticate_user)		(EBookBackend *backend, GCancellable *cancellable, ECredentials *credentials);

	void	(* refresh)			(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable);
	void	(* create_contact)		(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *vcard);
	void	(* remove_contacts)		(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const GSList *id_list);
	void	(* modify_contact)		(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *vcard);
	void	(* get_contact)			(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *id);
	void	(* get_contact_list)		(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *query);

	void	(* start_book_view)		(EBookBackend *backend, EDataBookView *book_view);
	void	(* stop_book_view)		(EBookBackend *backend, EDataBookView *book_view);

	/* Notification signals */
	void	(* last_client_gone)		(EBookBackend *backend);
	void	(* sync)			(EBookBackend *backend);
};

GType		e_book_backend_get_type		(void);

const gchar *	e_book_backend_get_cache_dir	(EBookBackend *backend);
void		e_book_backend_set_cache_dir	(EBookBackend *backend, const gchar *cache_dir);

ESource *	e_book_backend_get_source	(EBookBackend *backend);

gboolean	e_book_backend_add_client	(EBookBackend *backend, EDataBook *book);
void		e_book_backend_remove_client	(EBookBackend *backend, EDataBook *book);

gboolean	e_book_backend_is_online	(EBookBackend *backend);
gboolean	e_book_backend_is_opened	(EBookBackend *backend);
gboolean	e_book_backend_is_opening	(EBookBackend *backend);
gboolean	e_book_backend_is_readonly	(EBookBackend *backend);
gboolean	e_book_backend_is_removed	(EBookBackend *backend);
void		e_book_backend_set_online	(EBookBackend *backend, gboolean is_online);
void		e_book_backend_authenticate_user(EBookBackend *backend, GCancellable *cancellable, ECredentials *credentials);

void		e_book_backend_get_backend_property (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *prop_name);
void		e_book_backend_set_backend_property (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *prop_name, const gchar *prop_value);

void		e_book_backend_open		(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, gboolean only_if_exists);
void		e_book_backend_remove		(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable);
void		e_book_backend_refresh		(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable);
void		e_book_backend_create_contact	(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *vcard);
void		e_book_backend_remove_contacts	(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const GSList *id_list);
void		e_book_backend_modify_contact	(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *vcard);
void		e_book_backend_get_contact	(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *id);
void		e_book_backend_get_contact_list	(EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *query);

void		e_book_backend_start_book_view	(EBookBackend *backend, EDataBookView *view);
void		e_book_backend_stop_book_view	(EBookBackend *backend, EDataBookView *view);
void		e_book_backend_add_book_view	(EBookBackend *backend, EDataBookView *view);
void		e_book_backend_remove_book_view	(EBookBackend *backend, EDataBookView *view);
void		e_book_backend_foreach_view	(EBookBackend *backend, gboolean (* callback) (EDataBookView *view, gpointer user_data), gpointer user_data);

void		e_book_backend_notify_update	(EBookBackend *backend, const EContact *contact);
void		e_book_backend_notify_remove	(EBookBackend *backend, const gchar *id);
void		e_book_backend_notify_complete	(EBookBackend *backend);

void		e_book_backend_notify_error	(EBookBackend *backend, const gchar *message);
void		e_book_backend_notify_readonly	(EBookBackend *backend, gboolean is_readonly);
void		e_book_backend_notify_online	(EBookBackend *backend, gboolean is_online);
void		e_book_backend_notify_auth_required (EBookBackend *backend, gboolean is_self, const ECredentials *credentials);
void		e_book_backend_notify_opened	(EBookBackend *backend, GError *error);

void		e_book_backend_sync		(EBookBackend *backend);

/* protected functions for subclasses */
void		e_book_backend_set_is_removed	(EBookBackend *backend, gboolean is_removed);

void		e_book_backend_respond_opened	(EBookBackend *backend, EDataBook *book, guint32 opid, GError *error);

G_END_DECLS

#endif /* __E_BOOK_BACKEND_H__ */

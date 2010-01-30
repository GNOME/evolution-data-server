/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * The Evolution addressbook client object.
 *
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifndef __E_BOOK_H__
#define __E_BOOK_H__

#include <glib.h>
#include <glib-object.h>

#include "libedataserver/e-list.h"
#include "libedataserver/e-source.h"
#include "libedataserver/e-source-list.h"
#include <libebook/e-contact.h>
#include <libebook/e-book-query.h>
#include <libebook/e-book-view.h>
#include <libebook/e-book-types.h>

#define E_TYPE_BOOK        (e_book_get_type ())
#define E_BOOK(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK, EBook))
#define E_BOOK_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), E_TYPE_BOOK, EBookClass))
#define E_IS_BOOK(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK))
#define E_IS_BOOK_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK))
#define E_BOOK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK, EBookClass))

G_BEGIN_DECLS

typedef struct _EBook        EBook;
typedef struct _EBookClass   EBookClass;
typedef struct _EBookPrivate EBookPrivate;

typedef void (*EBookCallback) (EBook *book, EBookStatus status, gpointer closure);
typedef void (*EBookOpenProgressCallback)     (EBook          *book,
					       const gchar     *status_message,
					       short           percent,
					       gpointer        closure);
typedef void (*EBookIdCallback)       (EBook *book, EBookStatus status, const gchar *id, gpointer closure);
typedef void (*EBookContactCallback)  (EBook *book, EBookStatus status, EContact *contact, gpointer closure);
typedef void (*EBookListCallback)     (EBook *book, EBookStatus status, GList *list, gpointer closure);
typedef void (*EBookBookViewCallback) (EBook *book, EBookStatus status, EBookView *book_view, gpointer closure);
typedef void (*EBookEListCallback)   (EBook *book, EBookStatus status, EList *list, gpointer closure);

struct _EBook {
	GObject       parent;
	/*< private >*/
	EBookPrivate *priv;
};

struct _EBookClass {
	GObjectClass parent;

	/*
	 * Signals.
	 */
	void (* writable_status) (EBook *book, gboolean writable);
	void (* connection_status) (EBook *book, gboolean connected);
	void (* auth_required) (EBook *book);
	void (* backend_died)    (EBook *book);

	/* Padding for future expansion */
	void (*_ebook_reserved0) (void);
	void (*_ebook_reserved1) (void);
	void (*_ebook_reserved2) (void);
	void (*_ebook_reserved3) (void);
	void (*_ebook_reserved4) (void);
};

/* Creating a new addressbook. */
EBook    *e_book_new                       (ESource *source, GError **error);
EBook    *e_book_new_from_uri              (const gchar *uri, GError **error);
EBook    *e_book_new_system_addressbook    (GError **error);
EBook    *e_book_new_default_addressbook   (GError **error);

/* loading addressbooks */
gboolean e_book_open                       (EBook       *book,
					    gboolean     only_if_exists,
					    GError     **error);

guint    e_book_async_open                 (EBook         *book,
					    gboolean       only_if_exists,
					    EBookCallback  open_response,
					    gpointer       closure);

gboolean e_book_remove                     (EBook       *book,
					    GError     **error);
guint    e_book_async_remove               (EBook   *book,
					    EBookCallback cb,
					    gpointer closure);
gboolean e_book_get_required_fields       (EBook       *book,
					    GList      **fields,
					    GError     **error);

guint    e_book_async_get_required_fields (EBook              *book,
					    EBookEListCallback  cb,
					    gpointer            closure);

gboolean e_book_get_supported_fields       (EBook       *book,
					    GList      **fields,
					    GError     **error);

guint    e_book_async_get_supported_fields (EBook              *book,
					    EBookEListCallback  cb,
					    gpointer            closure);

gboolean e_book_get_supported_auth_methods       (EBook       *book,
						  GList      **auth_methods,
						  GError     **error);

guint    e_book_async_get_supported_auth_methods (EBook              *book,
						  EBookEListCallback  cb,
						  gpointer            closure);

/* User authentication. */
gboolean e_book_authenticate_user          (EBook       *book,
					    const gchar  *user,
					    const gchar  *passwd,
					    const gchar  *auth_method,
					    GError     **error);

guint e_book_async_authenticate_user       (EBook                 *book,
					    const gchar            *user,
					    const gchar            *passwd,
					    const gchar            *auth_method,
					    EBookCallback         cb,
					    gpointer              closure);

/* Fetching contacts. */
gboolean e_book_get_contact                (EBook       *book,
					    const gchar  *id,
					    EContact   **contact,
					    GError     **error);

guint     e_book_async_get_contact         (EBook                 *book,
					    const gchar            *id,
					    EBookContactCallback   cb,
					    gpointer               closure);

/* Deleting contacts. */
gboolean e_book_remove_contact             (EBook       *book,
					    const gchar  *id,
					    GError     **error);

guint    e_book_async_remove_contact       (EBook                 *book,
					    EContact              *contact,
					    EBookCallback          cb,
					    gpointer               closure);
guint    e_book_async_remove_contact_by_id (EBook                 *book,
					    const gchar            *id,
					    EBookCallback          cb,
					    gpointer               closure);

gboolean e_book_remove_contacts            (EBook       *book,
					    GList       *ids,
					    GError     **error);

guint    e_book_async_remove_contacts      (EBook                 *book,
					    GList                 *ids,
					    EBookCallback          cb,
					    gpointer               closure);

/* Adding contacts. */
gboolean e_book_add_contact                (EBook           *book,
					    EContact        *contact,
					    GError         **error);

gboolean e_book_async_add_contact          (EBook           *book,
					    EContact        *contact,
					    EBookIdCallback  cb,
					    gpointer         closure);

/* Modifying contacts. */
gboolean e_book_commit_contact             (EBook       *book,
					    EContact    *contact,
					    GError     **error);

guint e_book_async_commit_contact          (EBook                 *book,
					    EContact              *contact,
					    EBookCallback          cb,
					    gpointer               closure);

/* Returns a live view of a query. */
gboolean e_book_get_book_view              (EBook       *book,
					    EBookQuery  *query,
					    GList       *requested_fields,
					    gint          max_results,
					    EBookView  **book_view,
					    GError     **error);

guint e_book_async_get_book_view           (EBook                 *book,
					    EBookQuery            *query,
					    GList                 *requested_fields,
					    gint                    max_results,
					    EBookBookViewCallback  cb,
					    gpointer               closure);

/* Returns a static snapshot of a query. */
gboolean e_book_get_contacts               (EBook       *book,
					    EBookQuery  *query,
					    GList      **contacts,
					    GError     **error);

guint     e_book_async_get_contacts        (EBook             *book,
					    EBookQuery        *query,
					    EBookListCallback  cb,
					    gpointer           closure);

/* Needed for syncing */
gboolean e_book_get_changes                (EBook       *book,
					    const gchar *changeid,
					    GList      **changes,
					    GError     **error);

guint    e_book_async_get_changes          (EBook             *book,
					    const gchar       *changeid,
					    EBookListCallback  cb,
					    gpointer           closure);

void     e_book_free_change_list           (GList       *change_list);

const gchar *e_book_get_uri                 (EBook       *book);
ESource    *e_book_get_source              (EBook       *book);

const gchar *e_book_get_static_capabilities (EBook    *book,
					    GError  **error);
gboolean    e_book_check_static_capability (EBook       *book,
					    const gchar  *cap);
gboolean    e_book_is_opened               (EBook       *book);
gboolean    e_book_is_writable             (EBook       *book);

gboolean    e_book_is_online               (EBook       *book);

/* Cancel a pending operation. */
gboolean    e_book_cancel                  (EBook   *book,
					    GError **error);

gboolean    e_book_cancel_async_op	   (EBook   *book,
					    GError **error);

/* Identity */
gboolean    e_book_get_self                (EContact **contact, EBook **book, GError **error);
gboolean    e_book_set_self                (EBook *book, EContact *contact, GError **error);
gboolean    e_book_is_self                 (EContact *contact);

/* Addressbook Discovery */
gboolean    e_book_set_default_addressbook (EBook  *book, GError **error);
gboolean    e_book_set_default_source      (ESource *source, GError **error);
gboolean    e_book_get_addressbooks        (ESourceList** addressbook_sources, GError **error);

GType        e_book_get_type                  (void);

G_END_DECLS

#endif /* __E_BOOK_H__ */

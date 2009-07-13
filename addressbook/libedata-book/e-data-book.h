/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * A wrapper object which exports the GNOME_Evolution_Addressbook_Book CORBA interface
 * and which maintains a request queue.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifndef __E_DATA_BOOK_H__
#define __E_DATA_BOOK_H__

#include <bonobo/bonobo-object.h>
#include "libedataserver/e-list.h"
#include "libedataserver/e-source.h"
#include <libedata-book/Evolution-DataServer-Addressbook.h>
#include <libedata-book/e-data-book-types.h>

G_BEGIN_DECLS

#define E_TYPE_DATA_BOOK        (e_data_book_get_type ())
#define E_DATA_BOOK(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DATA_BOOK, EDataBook))
#define E_DATA_BOOK_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_DATA_BOOK, EDataBookClass))
#define E_IS_DATA_BOOK(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DATA_BOOK))
#define E_IS_DATA_BOOK_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DATA_BOOK))
#define E_DATA_BOOK_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_DATA_BOOK, EDataBookClass))

typedef struct _EDataBookPrivate EDataBookPrivate;

struct _EDataBook {
	BonoboObject       parent_object;
	EDataBookPrivate    *priv;
};

struct _EDataBookClass {
	BonoboObjectClass parent_class;

	POA_GNOME_Evolution_Addressbook_Book__epv epv;

	/* Padding for future expansion */
	void (*_pas_reserved0) (void);
	void (*_pas_reserved1) (void);
	void (*_pas_reserved2) (void);
	void (*_pas_reserved3) (void);
	void (*_pas_reserved4) (void);
};

EDataBook                *e_data_book_new                    (EBookBackend                               *backend,
							      ESource                                  *source,
							 GNOME_Evolution_Addressbook_BookListener  listener);
GNOME_Evolution_Addressbook_BookListener e_data_book_get_listener (EDataBook                         *book);
EBookBackend             *e_data_book_get_backend            (EDataBook                                *book);
ESource                *e_data_book_get_source             (EDataBook                                *book);

void                    e_data_book_respond_open           (EDataBook                                *book,
							    guint32                                   opid,
							    GNOME_Evolution_Addressbook_CallStatus    status);
void                    e_data_book_respond_remove         (EDataBook                                *book,
							    guint32                                   opid,
							    GNOME_Evolution_Addressbook_CallStatus  status);
void                    e_data_book_respond_create         (EDataBook                                *book,
							    guint32                                   opid,
							    GNOME_Evolution_Addressbook_CallStatus  status,
							    EContact                               *contact);
void                    e_data_book_respond_remove_contacts (EDataBook                                *book,
							     guint32                                   opid,
							     GNOME_Evolution_Addressbook_CallStatus  status,
							     GList                                  *ids);
void                    e_data_book_respond_modify         (EDataBook                                *book,
							    guint32                                   opid,
							    GNOME_Evolution_Addressbook_CallStatus  status,
							    EContact                               *contact);
void                    e_data_book_respond_authenticate_user (EDataBook                                *book,
							       guint32                                   opid,
							       GNOME_Evolution_Addressbook_CallStatus  status);
void                    e_data_book_respond_get_supported_fields (EDataBook                              *book,
								  guint32                                 opid,
								  GNOME_Evolution_Addressbook_CallStatus  status,
								  GList                                  *fields);
void                    e_data_book_respond_get_required_fields (EDataBook                              *book,
								  guint32                                 opid,
								  GNOME_Evolution_Addressbook_CallStatus  status,
								  GList                                  *fields);
void                    e_data_book_respond_get_supported_auth_methods (EDataBook                              *book,
									guint32                                 opid,
									GNOME_Evolution_Addressbook_CallStatus  status,
									GList                                  *fields);

void                    e_data_book_respond_get_book_view  (EDataBook                              *book,
							    guint32                                 opid,
							    GNOME_Evolution_Addressbook_CallStatus  status,
							    EDataBookView                          *book_view);
void                    e_data_book_respond_get_contact    (EDataBook                              *book,
							    guint32                                 opid,
							    GNOME_Evolution_Addressbook_CallStatus  status,
							    const gchar                            *vcard);
void                    e_data_book_respond_get_contact_list (EDataBook                              *book,
							      guint32                                 opid,
							      GNOME_Evolution_Addressbook_CallStatus  status,
							      GList *cards);
void                    e_data_book_respond_get_changes    (EDataBook                              *book,
							    guint32                                 opid,
							    GNOME_Evolution_Addressbook_CallStatus  status,
							    GList                                  *changes);

void                    e_data_book_report_writable        (EDataBook                         *book,
							    gboolean                           writable);
void                    e_data_book_report_connection_status (EDataBook                        *book,
							      gboolean                         is_online);

void                    e_data_book_report_auth_required     (EDataBook                       *book);

GType                   e_data_book_get_type               (void);

G_END_DECLS

#endif /* ! __E_DATA_BOOK_H__ */

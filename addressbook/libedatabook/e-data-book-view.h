/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * A wrapper object which exports the GNOME_Evolution_Addressbook_Book CORBA interface
 * and which maintains a request queue.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 2000, Ximian, Inc.
 */

#ifndef __E_DATA_BOOK_VIEW_H__
#define __E_DATA_BOOK_VIEW_H__

#include <bonobo/bonobo-object.h>
#include <glib.h>
#include <glib-object.h>
#include "addressbook.h"
#include <libedatabook/e-data-book-types.h>
#include <libebook/e-contact.h>

#define E_TYPE_DATA_BOOK_VIEW        (e_data_book_view_get_type ())
#define E_DATA_BOOK_VIEW(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DATA_BOOK_VIEW, EDataBookView))
#define E_DATA_BOOK_VIEW_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_DATA_BOOK_VIEW, EDataBookViewClass))
#define E_IS_DATA_BOOK_VIEW(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DATA_BOOK_VIEW))
#define E_IS_DATA_BOOK_VIEW_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DATA_BOOK_VIEW))
#define E_DATA_BOOK_VIEW_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_DATA_BOOK_VIEW, EDataBookView))

typedef struct _EDataBookViewPrivate EDataBookViewPrivate;

struct _EDataBookView {
	BonoboObject     parent_object;
	EDataBookViewPrivate *priv;
};

struct _EDataBookViewClass {
	BonoboObjectClass parent_class;

	POA_GNOME_Evolution_Addressbook_BookView__epv epv;
};


EDataBookView *e_data_book_view_new                    (EBookBackend                 *backend,
						   GNOME_Evolution_Addressbook_BookViewListener  listener,
						   const char                 *card_query,
						   EBookBackendSExp         *card_sexp);

const char*  e_data_book_view_get_card_query         (EDataBookView                *book_view);
EBookBackendSExp* e_data_book_view_get_card_sexp   (EDataBookView                *book_view);
EBookBackend*  e_data_book_view_get_backend            (EDataBookView                *book_view);
GNOME_Evolution_Addressbook_BookViewListener e_data_book_view_get_listener (EDataBookView  *book_view);

void         e_data_book_view_notify_update          (EDataBookView                *book_view,
						   EContact                   *contact);
void         e_data_book_view_notify_remove          (EDataBookView                *book_view,
						   const char                 *id);
void         e_data_book_view_notify_complete        (EDataBookView                *book_view,
						   GNOME_Evolution_Addressbook_CallStatus);
void         e_data_book_view_notify_status_message  (EDataBookView                *book_view,
						   const char                 *message);

GType        e_data_book_view_get_type               (void);

#endif /* ! __E_DATA_BOOK_VIEW_H__ */

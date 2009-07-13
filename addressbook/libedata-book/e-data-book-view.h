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

#ifndef __E_DATA_BOOK_VIEW_H__
#define __E_DATA_BOOK_VIEW_H__

#include <bonobo/bonobo-object.h>
#include <glib.h>
#include <glib-object.h>
#include <libebook/e-contact.h>
#include <libedata-book/Evolution-DataServer-Addressbook.h>
#include <libedata-book/e-data-book-types.h>

G_BEGIN_DECLS

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

EDataBookView *e_data_book_view_new                  (EBookBackend                 *backend,
						      GNOME_Evolution_Addressbook_BookViewListener  listener,
						      const gchar                   *card_query,
						      EBookBackendSExp             *card_sexp,
						      gint                           max_results);

void              e_data_book_view_set_thresholds    (EDataBookView *book_view,
						      gint minimum_grouping_threshold,
						      gint maximum_grouping_threshold);

const gchar *       e_data_book_view_get_card_query    (EDataBookView                *book_view);
EBookBackendSExp* e_data_book_view_get_card_sexp     (EDataBookView                *book_view);
gint               e_data_book_view_get_max_results   (EDataBookView                *book_view);
EBookBackend*     e_data_book_view_get_backend       (EDataBookView                *book_view);
GNOME_Evolution_Addressbook_BookViewListener e_data_book_view_get_listener (EDataBookView  *book_view);
GMutex*           e_data_book_view_get_mutex         (EDataBookView                *book_view);

void         e_data_book_view_notify_update          (EDataBookView                *book_view,
						      EContact                     *contact);

void         e_data_book_view_notify_update_vcard    (EDataBookView                *book_view,
						      gchar                         *vcard);
void         e_data_book_view_notify_update_prefiltered_vcard (EDataBookView       *book_view,
                                                               const gchar          *id,
                                                               gchar                *vcard);

void         e_data_book_view_notify_remove          (EDataBookView                *book_view,
						      const gchar                   *id);
void         e_data_book_view_notify_complete        (EDataBookView                *book_view,
						      GNOME_Evolution_Addressbook_CallStatus);
void         e_data_book_view_notify_status_message  (EDataBookView                *book_view,
						      const gchar                   *message);
void         e_data_book_view_ref                    (EDataBookView                *book_view);
void         e_data_book_view_unref                  (EDataBookView                *book_view);

GType        e_data_book_view_get_type               (void);

G_END_DECLS

#endif /* ! __E_DATA_BOOK_VIEW_H__ */

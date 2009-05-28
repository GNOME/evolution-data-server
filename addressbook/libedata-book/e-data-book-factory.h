/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#include <bonobo/bonobo-object.h>
#include <libedata-book/Evolution-DataServer-Addressbook.h>
#include <libedata-book/e-book-backend.h>
#include <libedata-book/e-book-backend-factory.h>

#ifndef __E_DATA_BOOK_FACTORY_H__
#define __E_DATA_BOOK_FACTORY_H__

G_BEGIN_DECLS

#define E_TYPE_DATA_BOOK_FACTORY        (e_data_book_factory_get_type ())
#define E_DATA_BOOK_FACTORY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DATA_BOOK_FACTORY, EDataBookFactory))
#define E_DATA_BOOK_FACTORY_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_DATA_BOOK_FACTORY, EDataBookFactoryClass))
#define E_IS_DATA_BOOK_FACTORY(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DATA_BOOK_FACTORY))
#define E_IS_DATA_BOOK_FACTORY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DATA_BOOK_FACTORY))
#define E_DATA_BOOK_FACTORY_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_DATA_BOOK_FACTORY, EDataBookFactoryClass))

typedef struct _EDataBookFactoryPrivate EDataBookFactoryPrivate;

typedef struct {
	BonoboObject            parent_object;
	EDataBookFactoryPrivate *priv;
} EDataBookFactory;

typedef struct {
	BonoboObjectClass parent_class;

	POA_GNOME_Evolution_Addressbook_BookFactory__epv epv;

	/* Notification signals */
	void (* last_book_gone) (EDataBookFactory *factory);
} EDataBookFactoryClass;

EDataBookFactory *e_data_book_factory_new                  (void);

void              e_data_book_factory_register_backend     (EDataBookFactory    *factory,
							    EBookBackendFactory *backend_factory);

gint               e_data_book_factory_get_n_backends       (EDataBookFactory    *factory);

void		  e_data_book_factory_register_backends    (EDataBookFactory    *factory);

void              e_data_book_factory_dump_active_backends (EDataBookFactory    *factory);

gboolean          e_data_book_factory_activate             (EDataBookFactory    *factory, const gchar *iid);
void              e_data_book_factory_set_backend_mode             (EDataBookFactory    *factory, gint mode);

GType             e_data_book_factory_get_type             (void);

G_END_DECLS

#endif /* ! __E_DATA_BOOK_FACTORY_H__ */

/* e-book-backend-google-factory.h - Google contact backend factory.
 *
 * Copyright (C) 2008 Joergen Scheibengruber
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Author: Joergen Scheibengruber <joergen.scheibengruber AT googlemail.com>
 */

#ifndef __E_BOOK_BACKEND_GOOGLE_FACTORY_H__
#define __E_BOOK_BACKEND_GOOGLE_FACTORY_H__

#include <glib-object.h>
#include <libedata-book/e-book-backend-factory.h>

G_BEGIN_DECLS

#define E_TYPE_BOOK_BACKEND_GOOGLE_FACTORY         (e_book_backend_exchange_factory_get_type ())
#define E_BOOK_BACKEND_GOOGLE_FACTORY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_GOOGLE_FACTORY, EBookBackendGoogleFactory))
#define E_BOOK_BACKEND_GOOGLE_FACTORY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_GOOGLE_FACTORY, EBookBackendGoogleFactoryClass))
#define E_IS_BOOK_BACKEND_GOOGLE_FACTORY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_GOOGLE_FACTORY))
#define E_IS_BOOK_BACKEND_GOOGLE_FACTORY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_GOOGLE_FACTORY))
#define E_BOOK_BACKEND_GOOGLE_FACTORY_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_BACKEND_GOOGLE_FACTORY, EBookBackendGoogleFactoryClass))

typedef struct _EBookBackendGoogleFactory      EBookBackendGoogleFactory;
typedef struct _EBookBackendGoogleFactoryClass EBookBackendGoogleFactoryClass;

struct _EBookBackendGoogleFactory {
    EBookBackendFactory      parent_object;
};

struct _EBookBackendGoogleFactoryClass{
    EBookBackendFactoryClass parent_class;
};

GType e_book_backend_google_factory_get_type (GTypeModule *module);

G_END_DECLS

#endif /* __E_BOOK_BACKEND_GOOGLE_FACTORY_H__ */
 

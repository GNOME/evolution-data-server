/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-book-backend-factory.h
 *
 * Copyright (C) 2004  Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Chris Toshok <toshok@ximian.com>
 */

#ifndef _E_BOOK_BACKEND_FACTORY_H_
#define _E_BOOK_BACKEND_FACTORY_H_

#include <glib-object.h>
#include "e-book-backend.h"

G_BEGIN_DECLS

#define E_TYPE_BOOK_BACKEND_FACTORY        (e_book_backend_factory_get_type ())
#define E_BOOK_BACKEND_FACTORY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_FACTORY, EBookBackendFactory))
#define E_BOOK_BACKEND_FACTORY_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_FACTORY, EBookBackendFactoryClass))
#define E_IS_BOOK_BACKEND_FACTORY(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_FACTORY))
#define E_IS_BOOK_BACKEND_FACTORY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_FACTORY))
#define E_BOOK_BACKEND_FACTORY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_BOOK_BACKEND_FACTORY, EBookBackendFactoryClass))

typedef struct _EBookBackendFactoryPrivate EBookBackendFactoryPrivate;

typedef struct {
	GObject            parent_object;
} EBookBackendFactory;

typedef struct {
	GObjectClass parent_class;

	const char*   (*get_protocol) (EBookBackendFactory *factory);
	EBookBackend* (*new_backend)  (EBookBackendFactory *factory);
} EBookBackendFactoryClass;

GType                e_book_backend_factory_get_type             (void);

const char*          e_book_backend_factory_get_protocol         (EBookBackendFactory *factory);
EBookBackend*        e_book_backend_factory_new_backend          (EBookBackendFactory *factory);

G_END_DECLS

#endif /* _E_BOOK_BACKEND_FACTORY_H_ */

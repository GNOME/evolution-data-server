/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-book-backend-factory.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Chris Toshok <toshok@ximian.com>
 */

#if !defined (__LIBEDATA_BOOK_H_INSIDE__) && !defined (LIBEDATA_BOOK_COMPILATION)
#error "Only <libedata-book/libedata-book.h> should be included directly."
#endif

#ifndef E_BOOK_BACKEND_FACTORY_H
#define E_BOOK_BACKEND_FACTORY_H

#include <libebackend/libebackend.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_BACKEND_FACTORY \
	(e_book_backend_factory_get_type ())
#define E_BOOK_BACKEND_FACTORY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_BACKEND_FACTORY, EBookBackendFactory))
#define E_BOOK_BACKEND_FACTORY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_BACKEND_FACTORY, EBookBackendFactoryClass))
#define E_IS_BOOK_BACKEND_FACTORY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_BACKEND_FACTORY))
#define E_IS_BOOK_BACKEND_FACTORY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_BACKEND_FACTORY))
#define E_BOOK_BACKEND_FACTORY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_BACKEND_FACTORY, EBookBackendFactoryClass))

G_BEGIN_DECLS

typedef struct _EBookBackendFactory EBookBackendFactory;
typedef struct _EBookBackendFactoryClass EBookBackendFactoryClass;
typedef struct _EBookBackendFactoryPrivate EBookBackendFactoryPrivate;

struct _EBookBackendFactory {
	EBackendFactory parent;
	EBookBackendFactoryPrivate *priv;
};

struct _EBookBackendFactoryClass {
	EBackendFactoryClass parent_class;

	/* Subclasses just need to set these
	 * class members, we handle the rest. */
	const gchar *factory_name;
	GType backend_type;
};

GType		e_book_backend_factory_get_type		(void) G_GNUC_CONST;

G_END_DECLS

#endif /* E_BOOK_BACKEND_FACTORY_H */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2012 Openismus GmbH (www.openismus.com)
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author: Tristan Van Berkom <tristanvb@openismus.com>
 */

#if !defined (__LIBEDATA_BOOK_H_INSIDE__) && !defined (LIBEDATA_BOOK_COMPILATION)
#error "Only <libedata-book/libedata-book.h> should be included directly."
#endif

#ifndef __E_DATA_BOOK_DIRECT_H__
#define __E_DATA_BOOK_DIRECT_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define E_TYPE_DATA_BOOK_DIRECT        (e_data_book_direct_get_type ())
#define E_DATA_BOOK_DIRECT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DATA_BOOK_DIRECT, EDataBookDirect))
#define E_DATA_BOOK_DIRECT_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_DATA_BOOK_DIRECT, EDataBookDirectClass))
#define E_IS_DATA_BOOK_DIRECT(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DATA_BOOK_DIRECT))
#define E_IS_DATA_BOOK_DIRECT_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DATA_BOOK_DIRECT))
#define E_DATA_BOOK_DIRECT_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_DATA_BOOK_DIRECT, EDataBookDirect))

typedef struct _EDataBookDirect EDataBookDirect;
typedef struct _EDataBookDirectClass EDataBookDirectClass;
typedef struct _EDataBookDirectPrivate EDataBookDirectPrivate;

struct _EDataBookDirect {
	GObject parent;
	EDataBookDirectPrivate *priv;
};

struct _EDataBookDirectClass {
	GObjectClass parent;
};

GType			e_data_book_direct_get_type		 (void);
EDataBookDirect *	e_data_book_direct_new			 (const gchar *backend_path,
								  const gchar *backend_factory_name,
								  const gchar *config);

gboolean                e_data_book_direct_register_gdbus_object (EDataBookDirect *direct,
								  GDBusConnection *connection,
								  const gchar *object_path,
								  GError **error);

G_END_DECLS

#endif /* __E_DATA_BOOK_DIRECT_H__ */

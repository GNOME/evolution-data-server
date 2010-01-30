/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
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
 */

#ifndef __E_DATA_BOOK_FACTORY_H__
#define __E_DATA_BOOK_FACTORY_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define E_TYPE_DATA_BOOK_FACTORY        (e_data_book_factory_get_type ())
#define E_DATA_BOOK_FACTORY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DATA_BOOK_FACTORY, EDataBookFactory))
#define E_DATA_BOOK_FACTORY_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_DATA_BOOK_FACTORY, EDataBookFactoryClass))
#define E_IS_DATA_BOOK_FACTORY(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DATA_BOOK_FACTORY))
#define E_IS_DATA_BOOK_FACTORY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DATA_BOOK_FACTORY))
#define E_DATA_BOOK_FACTORY_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_DATA_BOOK_FACTORY, EDataBookFactoryClass))

typedef struct _EDataBookFactoryPrivate EDataBookFactoryPrivate;

typedef struct {
	GObject parent;
	EDataBookFactoryPrivate *priv;
} EDataBookFactory;

typedef struct {
	GObjectClass parent;
} EDataBookFactoryClass;

typedef enum {
	E_DATA_BOOK_FACTORY_ERROR_GENERIC
} EDataBookFactoryError;

GQuark e_data_book_factory_error_quark (void);
#define E_DATA_BOOK_FACTORY_ERROR e_data_book_factory_error_quark ()

GType e_data_book_factory_get_type (void);

void e_data_book_factory_set_backend_mode (EDataBookFactory *factory, gint mode);

G_END_DECLS

#endif /* __E_DATA_BOOK_FACTORY_H__ */

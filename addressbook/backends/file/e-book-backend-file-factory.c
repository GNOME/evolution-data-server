/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-file-factory.c - File contact backend factory.
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
 * Authors: Chris Toshok <toshok@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "libebackend/e-data-server-module.h"
#include "libedata-book/e-book-backend-factory.h"
#include "e-book-backend-file.h"

typedef struct _EBookBackendFileFactory EBookBackendFileFactory;
typedef struct _EBookBackendFileFactoryClass EBookBackendFileFactoryClass;

struct _EBookBackendFileFactory {
	EBookBackendFactory parent;
};

struct _EBookBackendFileFactoryClass {
	EBookBackendFactoryClass parent_class;
};

GType e_book_backend_file_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	EBookBackendFileFactory,
	e_book_backend_file_factory,
	E_TYPE_BOOK_BACKEND_FACTORY)

static const gchar *
book_backend_file_factory_get_protocol (EBookBackendFactory *factory)
{
	return "local";
}

static EBookBackend *
book_backend_file_factory_new_backend (EBookBackendFactory *factory)
{
	return e_book_backend_file_new ();
}

static void
e_book_backend_file_factory_class_init (EBookBackendFileFactoryClass *class)
{
	EBookBackendFactoryClass *factory_class;

	factory_class = E_BOOK_BACKEND_FACTORY_CLASS (class);
	factory_class->get_protocol = book_backend_file_factory_get_protocol;
	factory_class->new_backend = book_backend_file_factory_new_backend;
}

static void
e_book_backend_file_factory_class_finalize (EBookBackendFileFactoryClass *class)
{
}

static void
e_book_backend_file_factory_init (EBookBackendFileFactory *factory)
{
}

void
eds_module_initialize (GTypeModule *type_module)
{
	e_book_backend_file_factory_register_type (type_module);
}

void
eds_module_shutdown (void)
{
}

void
eds_module_list_types (const GType **types,
                       gint *num_types)
{
	*types = &e_book_backend_file_factory_type_id;
	*num_types = 1;
}


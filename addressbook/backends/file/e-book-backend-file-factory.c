/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-file-factory.c - File contact backend factory.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Chris Toshok <toshok@ximian.com>
 */

#include <config.h>

#include "e-book-backend-file.h"

#define FACTORY_NAME "local"

typedef EBookBackendFactory EBookBackendFileFactory;
typedef EBookBackendFactoryClass EBookBackendFileFactoryClass;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_book_backend_file_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	EBookBackendFileFactory,
	e_book_backend_file_factory,
	E_TYPE_BOOK_BACKEND_FACTORY)

static void
e_book_backend_file_factory_class_init (EBookBackendFactoryClass *class)
{
	class->factory_name = FACTORY_NAME;
	class->backend_type = E_TYPE_BOOK_BACKEND_FILE;
}

static void
e_book_backend_file_factory_class_finalize (EBookBackendFactoryClass *class)
{
}

static void
e_book_backend_file_factory_init (EBookBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_book_backend_file_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}


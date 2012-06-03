/* e-book-backend-google-factory.c - Google contact backend factory.
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

#include <config.h>

#include "e-book-backend-google.h"

#define FACTORY_NAME "google"

typedef EBookBackendFactory EBookBackendGoogleFactory;
typedef EBookBackendFactoryClass EBookBackendGoogleFactoryClass;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_book_backend_google_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	EBookBackendGoogleFactory,
	e_book_backend_google_factory,
	E_TYPE_BOOK_BACKEND_FACTORY)

static void
e_book_backend_google_factory_class_init (EBookBackendFactoryClass *class)
{
	class->factory_name = FACTORY_NAME;
	class->backend_type = E_TYPE_BOOK_BACKEND_GOOGLE;
}

static void
e_book_backend_google_factory_class_finalize (EBookBackendFactoryClass *class)
{
}

static void
e_book_backend_google_factory_init (EBookBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_book_backend_google_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

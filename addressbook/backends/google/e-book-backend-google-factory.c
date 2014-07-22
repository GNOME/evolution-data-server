/* e-book-backend-google-factory.c - Google contact backend factory.
 *
 * Copyright (C) 2008 Joergen Scheibengruber
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Joergen Scheibengruber <joergen.scheibengruber AT googlemail.com>
 */

#include <config.h>

#include "e-book-backend-google.h"

#define FACTORY_NAME "google"

typedef EBookBackendFactory EBookBackendGoogleFactory;
typedef EBookBackendFactoryClass EBookBackendGoogleFactoryClass;

static EModule *e_module;

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
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

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
	e_module = E_MODULE (type_module);

	e_book_backend_google_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
	e_module = NULL;
}

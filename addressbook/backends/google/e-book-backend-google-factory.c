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

#include <libebackend/e-data-server-module.h>
#include <libedata-book/e-book-backend-factory.h>
#include "e-book-backend-google-factory.h"
#include "e-book-backend-google.h"

static GType google_type;

static void
e_book_backend_google_factory_instance_init (EBookBackendGoogleFactory *factory)
{
}

static const char *
_get_protocol (EBookBackendFactory *factory)
{
    return "google";
}

static EBookBackend*
_new_backend (EBookBackendFactory *factory)
{
    return e_book_backend_google_new ();
}

static void
e_book_backend_google_factory_class_init (EBookBackendGoogleFactoryClass *klass)
{
  E_BOOK_BACKEND_FACTORY_CLASS (klass)->get_protocol = _get_protocol;
  E_BOOK_BACKEND_FACTORY_CLASS (klass)->new_backend = _new_backend;
}

GType
e_book_backend_google_factory_get_type (GTypeModule *module)
{
    static GType  type = 0;

    if (!type) {
        GTypeInfo info = {
            sizeof (EBookBackendGoogleFactoryClass),
            NULL, /* base_class_init */
            NULL, /* base_class_finalize */
            (GClassInitFunc)  e_book_backend_google_factory_class_init,
            NULL, /* class_finalize */
            NULL, /* class_data */
            sizeof (EBookBackend),
            0,    /* n_preallocs */
            (GInstanceInitFunc) e_book_backend_google_factory_instance_init
        };

        type = g_type_module_register_type (module, E_TYPE_BOOK_BACKEND_FACTORY,
                                            "EBookBackendGoogleFactory", &info, 0);
    }
    return type;
}

void
eds_module_initialize (GTypeModule *module)
{
    google_type = e_book_backend_google_factory_get_type (module);
}

void
eds_module_shutdown   (void)
{
}

void
eds_module_list_types (const GType **types, int *num_types)
{
    *types = &google_type;
    *num_types = 1;
}

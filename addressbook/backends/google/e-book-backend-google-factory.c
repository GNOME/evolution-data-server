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
#include "e-book-backend-google.h"

E_BOOK_BACKEND_FACTORY_SIMPLE (google, Google, e_book_backend_google_new)

GType google_type;

void
eds_module_initialize (GTypeModule *module)
{
    google_type = _google_factory_get_type (module);
}

void
eds_module_shutdown   (void)
{
}

void
eds_module_list_types (const GType **types, gint *num_types)
{
    *types = &google_type;
    *num_types = 1;
}

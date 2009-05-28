/* e-book-backend-webdav-factory.c - Webdav contact backend.
 *
 * Copyright (C) 2008 Matthias Braun <matze@braunis.de>
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
 * Author: Matthias Braun <matze@braunis.de>
 */
#include <string.h>

#include "libebackend/e-data-server-module.h"
#include "libedata-book/e-book-backend-factory.h"
#include "e-book-backend-webdav.h"

E_BOOK_BACKEND_FACTORY_SIMPLE (webdav, Webdav, e_book_backend_webdav_new)

static GType webdav_type;

void eds_module_initialize(GTypeModule *module)
{
	webdav_type = _webdav_factory_get_type(module);
}

void eds_module_shutdown(void)
{
}

void eds_module_list_types(const GType **types, gint *num_types)
{
	*types     = &webdav_type;
	*num_types = 1;
}

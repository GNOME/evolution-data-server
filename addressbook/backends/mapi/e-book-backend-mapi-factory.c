/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Srinivasa Ragavan <sragavan@novell.com>
 *  Copyright (C) 2007 Novell, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libedataserver/e-data-server-module.h>
#include "libedata-book/e-book-backend-factory.h"
#include "e-book-backend-mapi.h"

E_BOOK_BACKEND_FACTORY_SIMPLE (mapi,
			       MAPI,
			       e_book_backend_mapi_new)

static GType mapi_type;

void	eds_module_initialize (GTypeModule *module)
{
	mapi_type = _mapi_factory_get_type (module);
}

void	eds_module_shutdown   (void)
{
}

void	eds_module_list_types (const GType **types, int *num_types)
{
	*types = &mapi_type;
	*num_types = 1;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version. 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "libedataserver/e-data-server-module.h"
#include "libedata-book/e-book-backend-factory.h"
#include "e-book-backend-galldap.h"

E_BOOK_BACKEND_FACTORY_SIMPLE (galldap, GALLDAP, e_book_backend_galldap_new)

static GType  galldap_type;

void
eds_module_initialize (GTypeModule *module)
{
	galldap_type = _galldap_factory_get_type (module);
}

void
eds_module_shutdown   (void)
{
}

void
eds_module_list_types (const GType **types, int *num_types)
{
	*types = & galldap_type;
	*num_types = 1;
}


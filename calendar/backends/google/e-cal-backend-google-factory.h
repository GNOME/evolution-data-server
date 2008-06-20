/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Ebby Wiselyn <ebbywiselyn@gmail.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef _E_CAL_BACKEND_GOOGLE_FACTORY_H
#define _E_CAL_BACKEND_GOOGLE_FACTORY_H

#include<glib.h>
#include "libedata-cal/e-cal-backend-factory.h"

G_BEGIN_DECLS

void eds_module_initialize (GTypeModule *module);
void eds_module_shutdown (void);
void eds_module_list_types (const GType **types, int *num_types);

G_END_DECLS

#endif


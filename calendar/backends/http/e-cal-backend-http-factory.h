/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-cal-backend-http-factory.h
 *
 * Copyright (C) 2004  Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
 *
 * Author: Chris Toshok <toshok@ximian.com>
 */

#ifndef _E_CAL_BACKEND_HTTP_FACTORY_H_
#define _E_CAL_BACKEND_HTTP_FACTORY_H_

#include <glib-object.h>
#include "libedata-cal/e-cal-backend-factory.h"

G_BEGIN_DECLS

void                 eds_module_initialize (GTypeModule *module);
void                 eds_module_shutdown   (void);
void                 eds_module_list_types (const GType **types, int *num_types);

G_END_DECLS

#endif /* _E_CAL_BACKEND_HTTP_FACTORY_H_ */

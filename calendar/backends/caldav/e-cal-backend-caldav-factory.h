/*
 * Evolution calendar - caldav backend factory
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Christian Kellner <gicmo@gnome.org>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef E_CAL_BACKEND_CALDAV_FACTORY_H
#define E_CAL_BACKEND_CALDAV_FACTORY_H

#include <glib-object.h>
#include "libedata-cal/e-cal-backend-factory.h"

G_BEGIN_DECLS

void                 eds_module_initialize (GTypeModule *module);
void                 eds_module_shutdown   (void);
void                 eds_module_list_types (const GType **types, gint *num_types);

G_END_DECLS

#endif /* E_CAL_BACKEND_CALDAV_FACTORY_H */

/* Evolution calendar - caldav backend factory
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Author: Christian Kellner <gicmo@gnome.org>
 */

#ifndef E_CAL_BACKEND_CALDAV_FACTORY_H
#define E_CAL_BACKEND_CALDAV_FACTORY_H

#include <glib-object.h>
#include "libedata-cal/e-cal-backend-factory.h"

G_BEGIN_DECLS

void                 eds_module_initialize (GTypeModule *module);
void                 eds_module_shutdown   (void);
void                 eds_module_list_types (const GType **types, int *num_types);

G_END_DECLS

#endif /* E_CAL_BACKEND_CALDAV_FACTORY_H */

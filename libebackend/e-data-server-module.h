/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-data-server-module.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Chris Toshok <toshok@ximian.com>
 */

#ifndef _E_DATA_SERVER_MODULE_H
#define _E_DATA_SERVER_MODULE_H

#include <glib-object.h>

G_BEGIN_DECLS

void   e_data_server_module_init             (void);
GList *e_data_server_get_extensions_for_type (GType type);
void   e_data_server_extension_list_free     (GList *list);
void   e_data_server_module_remove_unused    (void);

/* Add a type to the module interface - allows EDS to add its own modules
 * without putting them in separate shared libraries */
void   e_data_server_module_add_type         (GType  type);

/* The following three functions should exist in modules that are
   written to be dynamically loaded */
void                 eds_module_initialize (GTypeModule *module);
void                 eds_module_shutdown   (void);
void                 eds_module_list_types (const GType **types, gint *num_types);

G_END_DECLS

#endif /* _E_DATA_SERVER_MODULE_H */

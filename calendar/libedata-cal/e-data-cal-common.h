/* Evolution calendar server - common declarations
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef CAL_COMMON_H
#define CAL_COMMON_H

#include <glib.h>

G_BEGIN_DECLS



typedef struct _ECalBackend ECalBackend;
typedef struct _ECalBackendClass ECalBackendClass;

typedef struct _EDataCal EDataCal;
typedef struct _EDataCalClass EDataCalClass;

typedef struct _EDataCalView EDataCalView;
typedef struct _EDataCalViewClass EDataCalViewClass;

typedef struct _ECalBackendSExp ECalBackendSExp;
typedef struct _ECalBackendSExpClass ECalBackendSExpClass;

/* Opaque type for backend method context */
typedef gpointer EServerMethodContext;



G_END_DECLS

#endif

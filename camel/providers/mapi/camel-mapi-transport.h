/*
 *  Copyright (C) Jean-Baptiste Arnoult 2007.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
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

/* camel-openchange-transport.h: Openchange-based transport class */


#ifndef CAMEL_OPENCHANGE_TRANSPORT_H
#define CAMEL_OPENCHANGE_TRANSPORT_H 1

#include <camel/camel-transport.h>

/**
 * DATA STRUCTURES
 */

typedef struct {
	CamelTransport parent_object;	
} CamelOpenchangeTransport;


typedef struct {
	CamelTransportClass parent_class;
} CamelOpenchangeTransportClass;

/**
 * PROTOTYPES
 */

#ifndef __BEGIN_DECLS
#ifdef __cplusplus
#define __BEGIN_DECLS		extern "C" {
#define __END_DECLS		}
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif
#endif

__BEGIN_DECLS
/* Standard Camel function */
CamelType camel_openchange_transport_get_type (void);
__END_DECLS

#endif /* CAMEL_OPENCHANGE_TRANSPORT_H */

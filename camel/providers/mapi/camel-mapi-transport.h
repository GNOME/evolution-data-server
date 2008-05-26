/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Johnny Jacob <jjohnny@novell.com>
 *   
 * Copyright (C) 2007, Novell Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 3 of the GNU Lesser General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef CAMEL_MAPI_TRANSPORT_H
#define CAMEL_MAPI_TRANSPORT_H 1

#include <libmapi/libmapi.h>
#include <camel/camel-transport.h>
#include <exchange-mapi-connection.h>

#define CAMEL_MAPI_TRANSPORT_TYPE     (camel_mapi_transport_get_type ())
#define CAMEL_MAPI_TRANSPORT(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_MAPI_TRANSPORT_TYPE, CamelMapiTransport))
#define CAMEL_MAPI_TRANSPORT_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_MAPI_TRANSPORT_TYPE, CamelMapiTransportClass))
#define CAMEL_IS_MAPI_TRANSPORT(o)    (CAMEL_CHECK_TYPE((o), CAMEL_MAPI_TRANSPORT_TYPE))

G_BEGIN_DECLS

typedef struct {
	CamelTransport parent_object;
	gboolean connected ;

} CamelMapiTransport;


typedef struct {
	CamelTransportClass parent_class;

} CamelMapiTransportClass;


/* Standard Camel function */
CamelType camel_mapi_transport_get_type (void);

G_END_DECLS

#endif /* CAMEL_MAPI_TRANSPORT_H */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-groupwise-transport.h : class for an groupwise transport */

/* 
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
 *
 * Copyright (C) 2004 Novell, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */


#ifndef CAMEL_GROUPWISE_TRANSPORT_H
#define CAMEL_GROUPWISE_TRANSPORT_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include <camel/camel-transport.h>

#define CAMEL_GROUPWISE_TRANSPORT_TYPE     (camel_groupwise_transport_get_type ())
#define CAMEL_GROUPWISE_TRANSPORT(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_GROUPWISE_TRANSPORT_TYPE, CamelGroupwiseTransport))
#define CAMEL_GROUPWISE_TRANSPORT_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_GROUPWISE_TRANSPORT_TYPE, CamelGroupwiseTransportClass))
#define CAMEL_IS_GROUPWISE_TRANSPORT(o)    (CAMEL_CHECK_TYPE((o), CAMEL_GROUPWISE_TRANSPORT_TYPE))


typedef struct {
	CamelTransport parent_object;
	gboolean connected ;

} CamelGroupwiseTransport;


typedef struct {
	CamelTransportClass parent_class;

} CamelGroupwiseTransportClass;


/* Standard Camel function */
CamelType camel_groupwise_transport_get_type (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_GROUPWISE_TRANSPORT_H */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-transport.h : Abstract class for an email transport */

/*
 *
 * Author :
 *  Dan Winship <danw@ximian.com>
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_TRANSPORT_H
#define CAMEL_TRANSPORT_H

#include <camel/camel-address.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-service.h>

#define CAMEL_TRANSPORT_TYPE     (camel_transport_get_type ())
#define CAMEL_TRANSPORT(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_TRANSPORT_TYPE, CamelTransport))
#define CAMEL_TRANSPORT_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_TRANSPORT_TYPE, CamelTransportClass))
#define CAMEL_IS_TRANSPORT(o)    (CAMEL_CHECK_TYPE((o), CAMEL_TRANSPORT_TYPE))

G_BEGIN_DECLS

typedef struct _CamelTransport CamelTransport;
typedef struct _CamelTransportClass CamelTransportClass;
typedef struct _CamelTransportPrivate CamelTransportPrivate;

enum {
	CAMEL_TRANSPORT_ARG_FIRST  = CAMEL_SERVICE_ARG_FIRST + 100
};

struct _CamelTransport {
	CamelService parent;
	CamelTransportPrivate *priv;
};

struct _CamelTransportClass {
	CamelServiceClass parent_class;

	gboolean (*send_to) (CamelTransport *transport,
			     CamelMimeMessage *message,
			     CamelAddress *from, CamelAddress *recipients,
			     CamelException *ex);
};

/* public methods */
gboolean camel_transport_send_to (CamelTransport *transport,
				  CamelMimeMessage *message,
				  CamelAddress *from,
				  CamelAddress *recipients,
				  CamelException *ex);

/* Standard Camel function */
CamelType camel_transport_get_type (void);

G_END_DECLS

#endif /* CAMEL_TRANSPORT_H */

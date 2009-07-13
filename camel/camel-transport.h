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

#ifndef CAMEL_TRANSPORT_H
#define CAMEL_TRANSPORT_H 1

#include <glib.h>
#include <camel/camel-service.h>

#define CAMEL_TRANSPORT_TYPE     (camel_transport_get_type ())
#define CAMEL_TRANSPORT(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_TRANSPORT_TYPE, CamelTransport))
#define CAMEL_TRANSPORT_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_TRANSPORT_TYPE, CamelTransportClass))
#define CAMEL_IS_TRANSPORT(o)    (CAMEL_CHECK_TYPE((o), CAMEL_TRANSPORT_TYPE))

G_BEGIN_DECLS

enum {
	CAMEL_TRANSPORT_ARG_FIRST  = CAMEL_SERVICE_ARG_FIRST + 100
};

struct _CamelTransport
{
	CamelService parent_object;

	struct _CamelTransportPrivate *priv;
};

typedef struct {
	CamelServiceClass parent_class;

	gboolean (*send_to) (CamelTransport *transport,
			     CamelMimeMessage *message,
			     CamelAddress *from, CamelAddress *recipients,
			     CamelException *ex);
} CamelTransportClass;

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

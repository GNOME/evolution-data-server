/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-sendmail-transport.h: Sendmail-based transport class */

/* 
 *
 * Author : 
 *  Dan Winship <danw@ximian.com>
 *
 * Copyright 2000 Ximian, Inc. (www.ximian.com)
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


#ifndef CAMEL_SENDMAIL_TRANSPORT_H
#define CAMEL_SENDMAIL_TRANSPORT_H 1

#include "camel-transport.h"

#define CAMEL_SENDMAIL_TRANSPORT_TYPE     (camel_sendmail_transport_get_type ())
#define CAMEL_SENDMAIL_TRANSPORT(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_SENDMAIL_TRANSPORT_TYPE, CamelSendmailTransport))
#define CAMEL_SENDMAIL_TRANSPORT_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_SENDMAIL_TRANSPORT_TYPE, CamelSendmailTransportClass))
#define CAMEL_IS_SENDMAIL_TRANSPORT(o)    (CAMEL_CHECK_TYPE((o), CAMEL_SENDMAIL_TRANSPORT_TYPE))

G_BEGIN_DECLS

typedef struct {
	CamelTransport parent_object;

} CamelSendmailTransport;


typedef struct {
	CamelTransportClass parent_class;

} CamelSendmailTransportClass;


/* Standard Camel function */
CamelType camel_sendmail_transport_get_type (void);

G_END_DECLS

#endif /* CAMEL_SENDMAIL_TRANSPORT_H */

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

/* Standard GObject macros */
#define CAMEL_TYPE_TRANSPORT \
	(camel_transport_get_type ())
#define CAMEL_TRANSPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_TRANSPORT, CamelTransport))
#define CAMEL_TRANSPORT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_TRANSPORT, CamelTransportClass))
#define CAMEL_IS_TRANSPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_TRANSPORT))
#define CAMEL_IS_TRANSPORT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_TRANSPORT))
#define CAMEL_TRANSPORT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_TRANSPORT, CamelTransportClass))

G_BEGIN_DECLS

typedef struct _CamelTransport CamelTransport;
typedef struct _CamelTransportClass CamelTransportClass;
typedef struct _CamelTransportPrivate CamelTransportPrivate;

/**
 * CamelTransportLock:
 *
 * Since: 2.32
 **/
typedef enum {
	CAMEL_TRANSPORT_SEND_LOCK
} CamelTransportLock;

struct _CamelTransport {
	CamelService parent;
	CamelTransportPrivate *priv;
};

struct _CamelTransportClass {
	CamelServiceClass parent_class;

	/* Synchronous I/O Methods */
	gboolean	(*send_to_sync)		(CamelTransport *transport,
						 CamelMimeMessage *message,
						 CamelAddress *from,
						 CamelAddress *recipients,
						 GCancellable *cancellable,
						 GError **error);

	/* Asynchronous I/O Methods (all have defaults) */
	void		(*send_to)		(CamelTransport *transport,
						 CamelMimeMessage *message,
						 CamelAddress *from,
						 CamelAddress *recipients,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	gboolean	(*send_to_finish)	(CamelTransport *transport,
						 GAsyncResult *result,
						 GError **error);
};

GType		camel_transport_get_type	(void);
void		camel_transport_lock		(CamelTransport *transport,
						 CamelTransportLock lock);
void		camel_transport_unlock		(CamelTransport *transport,
						 CamelTransportLock lock);

gboolean	camel_transport_send_to_sync	(CamelTransport *transport,
						 CamelMimeMessage *message,
						 CamelAddress *from,
						 CamelAddress *recipients,
						 GCancellable *cancellable,
						 GError **error);
void		camel_transport_send_to		(CamelTransport *transport,
						 CamelMimeMessage *message,
						 CamelAddress *from,
						 CamelAddress *recipients,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	camel_transport_send_to_finish	(CamelTransport *transport,
						 GAsyncResult *result,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_TRANSPORT_H */

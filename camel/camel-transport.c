/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-transport.c : Abstract class for an email transport */

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-address.h"
#include "camel-mime-message.h"
#include "camel-transport.h"

struct _CamelTransportPrivate {
	GMutex *send_lock;   /* for locking send operations */
};

static CamelServiceClass *parent_class = NULL;

static void
transport_finalize (CamelObject *object)
{
	CamelTransportPrivate *priv = CAMEL_TRANSPORT (object)->priv;

	g_mutex_free (priv->send_lock);

	g_free (priv);
}

static void
camel_transport_class_init (CamelTransportClass *class)
{
	parent_class = CAMEL_SERVICE_CLASS (camel_type_get_global_classfuncs (camel_service_get_type ()));
}

static void
camel_transport_init (CamelTransport *transport)
{
	transport->priv = g_malloc0 (sizeof (struct _CamelTransportPrivate));

	transport->priv->send_lock = g_mutex_new ();
}

CamelType
camel_transport_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (CAMEL_SERVICE_TYPE,
					    "CamelTransport",
					    sizeof (CamelTransport),
					    sizeof (CamelTransportClass),
					    (CamelObjectClassInitFunc) camel_transport_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_transport_init,
					    (CamelObjectFinalizeFunc) transport_finalize);
	}

	return type;
}

/**
 * camel_transport_send_to:
 * @transport: a #CamelTransport object
 * @message: a #CamelMimeMessage to send
 * @from: a #CamelAddress to send from
 * @recipients: a #CamelAddress containing all recipients
 * @ex: a #CamelException
 *
 * Sends the message to the given recipients, regardless of the contents
 * of @message. If the message contains a "Bcc" header, the transport
 * is responsible for stripping it.
 *
 * Return %TRUE on success or %FALSE on fail
 **/
gboolean
camel_transport_send_to (CamelTransport *transport,
                         CamelMimeMessage *message,
                         CamelAddress *from,
                         CamelAddress *recipients,
                         CamelException *ex)
{
	CamelTransportClass *class;
	gboolean sent;

	g_return_val_if_fail (CAMEL_IS_TRANSPORT (transport), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), FALSE);
	g_return_val_if_fail (CAMEL_IS_ADDRESS (from), FALSE);
	g_return_val_if_fail (CAMEL_IS_ADDRESS (recipients), FALSE);

	class = CAMEL_TRANSPORT_GET_CLASS (transport);
	g_return_val_if_fail (class->send_to != NULL, FALSE);

	camel_transport_lock (transport, CT_SEND_LOCK);
	sent = class->send_to (transport, message, from, recipients, ex);
	camel_transport_unlock (transport, CT_SEND_LOCK);

	return sent;
}

/**
 * camel_transport_lock:
 * @transport: a #CamelTransport
 * @lock: lock type to lock
 *
 * Locks #transport's #lock. Unlock it with camel_transport_unlock().
 *
 * Since: 2.31.1
 **/
void
camel_transport_lock (CamelTransport *transport, CamelTransportLock lock)
{
	g_return_if_fail (transport != NULL);
	g_return_if_fail (CAMEL_IS_TRANSPORT (transport));
	g_return_if_fail (transport->priv != NULL);

	switch (lock) {
	case CT_SEND_LOCK:
		g_mutex_lock (transport->priv->send_lock);
		break;
	default:
		g_return_if_reached ();
	}
}

/**
 * camel_transport_unlock:
 * @transport: a #CamelTransport
 * @lock: lock type to unlock
 *
 * Unlocks #transport's #lock, previously locked with camel_transport_lock().
 *
 * Since: 2.31.1
 **/
void
camel_transport_unlock (CamelTransport *transport, CamelTransportLock lock)
{
	g_return_if_fail (transport != NULL);
	g_return_if_fail (CAMEL_IS_TRANSPORT (transport));
	g_return_if_fail (transport->priv != NULL);

	switch (lock) {
	case CT_SEND_LOCK:
		g_mutex_unlock (transport->priv->send_lock);
		break;
	default:
		g_return_if_reached ();
	}
}

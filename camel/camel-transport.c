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
#include "camel-debug.h"
#include "camel-mime-message.h"
#include "camel-transport.h"

#define CAMEL_TRANSPORT_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_TRANSPORT, CamelTransportPrivate))

typedef struct _AsyncContext AsyncContext;

struct _CamelTransportPrivate {
	GMutex send_lock;   /* for locking send operations */
};

struct _AsyncContext {
	/* arguments */
	CamelAddress *from;
	CamelAddress *recipients;
	CamelMimeMessage *message;
};

G_DEFINE_ABSTRACT_TYPE (CamelTransport, camel_transport, CAMEL_TYPE_SERVICE)

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->from != NULL)
		g_object_unref (async_context->from);

	if (async_context->recipients != NULL)
		g_object_unref (async_context->recipients);

	if (async_context->message != NULL)
		g_object_unref (async_context->message);

	g_slice_free (AsyncContext, async_context);
}

static void
transport_finalize (GObject *object)
{
	CamelTransportPrivate *priv;

	priv = CAMEL_TRANSPORT_GET_PRIVATE (object);

	g_mutex_clear (&priv->send_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_transport_parent_class)->finalize (object);
}

static void
camel_transport_class_init (CamelTransportClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelTransportPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = transport_finalize;
}

static void
camel_transport_init (CamelTransport *transport)
{
	transport->priv = CAMEL_TRANSPORT_GET_PRIVATE (transport);

	g_mutex_init (&transport->priv->send_lock);
}

/**
 * camel_transport_send_to_sync:
 * @transport: a #CamelTransport
 * @message: a #CamelMimeMessage to send
 * @from: a #CamelAddress to send from
 * @recipients: a #CamelAddress containing all recipients
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Sends the message to the given recipients, regardless of the contents
 * of @message.  If the message contains a "Bcc" header, the transport
 * is responsible for stripping it.
 *
 * Returns: %TRUE on success or %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_transport_send_to_sync (CamelTransport *transport,
                              CamelMimeMessage *message,
                              CamelAddress *from,
                              CamelAddress *recipients,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelTransportClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_TRANSPORT (transport), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), FALSE);
	g_return_val_if_fail (CAMEL_IS_ADDRESS (from), FALSE);
	g_return_val_if_fail (CAMEL_IS_ADDRESS (recipients), FALSE);

	class = CAMEL_TRANSPORT_GET_CLASS (transport);
	g_return_val_if_fail (class->send_to_sync != NULL, FALSE);

	g_mutex_lock (&transport->priv->send_lock);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		g_mutex_unlock (&transport->priv->send_lock);
		return FALSE;
	}

	success = class->send_to_sync (
		transport, message, from, recipients, cancellable, error);
	CAMEL_CHECK_GERROR (transport, send_to_sync, success, error);

	g_mutex_unlock (&transport->priv->send_lock);

	return success;
}

/* Helper for camel_transport_send_to() */
static void
transport_send_to_thread (GTask *task,
                          gpointer source_object,
                          gpointer task_data,
                          GCancellable *cancellable)
{
	gboolean success;
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = (AsyncContext *) task_data;

	success = camel_transport_send_to_sync (
		CAMEL_TRANSPORT (source_object),
		async_context->message,
		async_context->from,
		async_context->recipients,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * camel_transport_send_to:
 * @transport: a #CamelTransport
 * @message: a #CamelMimeMessage to send
 * @from: a #CamelAddress to send from
 * @recipients: a #CamelAddress containing all recipients
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Sends the message asynchronously to the given recipients, regardless of
 * the contents of @message.  If the message contains a "Bcc" header, the
 * transport is responsible for stripping it.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_transport_send_to_finish() to get the result of the operation.
 *
 * Since: 3.0
 **/
void
camel_transport_send_to (CamelTransport *transport,
                         CamelMimeMessage *message,
                         CamelAddress *from,
                         CamelAddress *recipients,
                         gint io_priority,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (CAMEL_IS_TRANSPORT (transport));
	g_return_if_fail (CAMEL_IS_MIME_MESSAGE (message));
	g_return_if_fail (CAMEL_IS_ADDRESS (from));
	g_return_if_fail (CAMEL_IS_ADDRESS (recipients));

	async_context = g_slice_new0 (AsyncContext);
	async_context->from = g_object_ref (from);
	async_context->recipients = g_object_ref (recipients);
	async_context->message = g_object_ref (message);

	task = g_task_new (transport, cancellable, callback, user_data);
	g_task_set_source_tag (task, camel_transport_send_to);
	g_task_set_priority (task, io_priority);

	g_task_set_task_data (
		task, async_context,
		(GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, transport_send_to_thread);

	g_object_unref (task);
}

/**
 * camel_transport_send_to_finish:
 * @transport: a #CamelTransport
 * @result: a #GAsyncResult
 * @error: return locaton for a #GError, or %NULL
 *
 * Finishes the operation started with camel_transport_send_to().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_transport_send_to_finish (CamelTransport *transport,
                                GAsyncResult *result,
                                GError **error)
{
	g_return_val_if_fail (CAMEL_IS_TRANSPORT (transport), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, transport), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, camel_transport_send_to), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

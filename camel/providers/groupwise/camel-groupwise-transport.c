/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-groupwise-transport.c : class for an groupwise transport */

/*
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
 *	    Parthasarathi Susarla <sparthasarathi@novell.com>
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

#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-groupwise-store.h"
#include "camel-groupwise-transport.h"
#include "camel-groupwise-utils.h"

#define REPLY_VIEW "default message attachments threading"

#define CAMEL_GROUPWISE_TRANSPORT_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_GROUPWISE_TRANSPORT, CamelGroupwiseTransportPrivate))

G_DEFINE_TYPE (CamelGroupwiseTransport, camel_groupwise_transport, CAMEL_TYPE_TRANSPORT)

struct _CamelGroupwiseTransportPrivate {
	CamelGroupwiseStore *store;
};

static void
groupwise_transport_dispose (GObject *object)
{
	CamelGroupwiseTransportPrivate *priv;

	priv = CAMEL_GROUPWISE_TRANSPORT_GET_PRIVATE (object);

	if (priv->store != NULL) {
		g_object_unref (priv->store);
		priv->store = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_groupwise_transport_parent_class)->
		dispose (object);
}

static gchar *
groupwise_transport_get_name (CamelService *service,
                              gboolean brief)
{
	CamelURL *url;

	url = camel_service_get_camel_url (service);

	if (brief)
		return g_strdup_printf (
			_("GroupWise server %s"),
			url->host);
	else
		return g_strdup_printf (
			_("GroupWise mail delivery via %s"),
			url->host);
}

static gboolean
groupwise_transport_connect_sync (CamelService *service,
                                  GCancellable *cancellable,
                                  GError **error)
{
	return TRUE;
}

static gboolean
groupwise_send_to_sync (CamelTransport *transport,
                        CamelMimeMessage *message,
                        CamelAddress *from,
                        CamelAddress *recipients,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelGroupwiseTransportPrivate *priv;
	CamelService *service;
	CamelSession *session;
	CamelURL *service_url;
	EGwItem *item ,*temp_item=NULL;
	EGwConnection *cnc = NULL;
	EGwConnectionStatus status = 0;
	GSList *sent_item_list = NULL;
	gchar *reply_request = NULL;
	EGwItemLinkInfo *info = NULL;

	if (!transport) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("Authentication failed"));
		return FALSE;
	}

	priv = CAMEL_GROUPWISE_TRANSPORT_GET_PRIVATE (transport);

	service = CAMEL_SERVICE (transport);
	session = camel_service_get_session (service);
	service_url = camel_service_get_camel_url (service);

	camel_operation_push_message (cancellable, _("Sending Message") );

	/*camel groupwise store and cnc*/
	cnc = cnc_lookup (priv->store->priv);
	if (!cnc) {
		g_warning ("||| Eh!!! Failure |||\n");
		camel_operation_pop_message (cancellable);
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("Authentication failed"));
		return FALSE;
	}

	item = camel_groupwise_util_item_from_message (cnc, message, from);

	reply_request = g_strdup (camel_medium_get_header (CAMEL_MEDIUM (message), "X-GW-ORIG-ITEM-ID"));
	if (reply_request) {
		g_strstrip (reply_request);
		status = e_gw_connection_reply_item (cnc, reply_request, REPLY_VIEW, &temp_item);
		if (status != E_GW_CONNECTION_STATUS_OK)
			g_warning ("Could not send a replyRequest...continuing without!!\n");
		else {
			info = e_gw_item_get_link_info (temp_item);
			e_gw_item_set_link_info (item, info);
		}
		g_free (reply_request);
	}

	/*Send item*/
	status = e_gw_connection_send_item (cnc, item, &sent_item_list);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		g_warning (" Error Sending mail");
		camel_operation_pop_message (cancellable);
		e_gw_item_set_link_info (item, NULL);
		g_object_unref (item);
		if (temp_item)
			g_object_unref (temp_item);

		/* FIXME: 58652 should be changed with an enum.*/
		if (status == 58652)
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("You have exceeded this account's storage limit. Your messages are queued in your Outbox. Resend by pressing Send/Receive after deleting/archiving some of your mail.\n"));
		else
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("Could not send message: %s"),
				_("Unknown error"));
		status = 0;
		return FALSE;
	}
	e_gw_item_set_link_info (item, NULL);

	e_gw_item_set_recipient_list (item, NULL);

	if (temp_item)
		g_object_unref (temp_item);
	g_object_unref (item);

	camel_operation_pop_message (cancellable);

	return TRUE;
}

static void
camel_groupwise_transport_class_init (CamelGroupwiseTransportClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelTransportClass *transport_class;

	g_type_class_add_private (
		class, sizeof (CamelGroupwiseTransportPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = groupwise_transport_dispose;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->get_name = groupwise_transport_get_name;
	service_class->connect_sync = groupwise_transport_connect_sync;

	transport_class = CAMEL_TRANSPORT_CLASS (class);
	transport_class->send_to_sync = groupwise_send_to_sync;
}

static void
camel_groupwise_transport_init (CamelGroupwiseTransport *transport)
{
	transport->priv = CAMEL_GROUPWISE_TRANSPORT_GET_PRIVATE (transport);
}

void
camel_groupwise_transport_set_store (CamelGroupwiseTransport *transport,
                                     CamelGroupwiseStore *store)
{
	g_return_if_fail (CAMEL_IS_GROUPWISE_TRANSPORT (transport));
	g_return_if_fail (CAMEL_IS_GROUPWISE_STORE (store));

	transport->priv->store = g_object_ref (store);
}

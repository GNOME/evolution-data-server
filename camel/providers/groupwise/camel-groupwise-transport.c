/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-groupwise-transport.c : class for an groupwise transport */

/* 
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
 *	    Parthasarathi Susarla <sparthasarathi@novell.com>
 *
 * Copyright (C) 2004 Novell, Inc.
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

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "camel-data-wrapper.h"
#include "camel-medium.h"
#include "camel-mime-message.h"
#include "camel-mime-utils.h"
#include "camel-multipart.h"
#include "camel-session.h" 
#include "camel-stream-mem.h"
#include "camel-stream.h"

#include "camel-groupwise-store.h"
#include "camel-groupwise-transport.h"
#include "camel-groupwise-utils.h"

static gboolean groupwise_send_to (CamelTransport *transport,
				  CamelMimeMessage *message,
				  CamelAddress *from,
				  CamelAddress *recipients,
				  CamelException *ex);

static gboolean groupwise_transport_connect (CamelService *service, CamelException *ex);
static char *groupwise_transport_get_name (CamelService *service, gboolean brief);
static void groupwise_transport_construct (CamelService *service, CamelSession *session,
					   CamelProvider *provider, CamelURL *url, CamelException *ex);


static CamelTransportClass *parent_class = NULL;



static void
camel_groupwise_transport_class_init (CamelGroupwiseTransportClass *camel_groupwise_transport_class)
{
	CamelTransportClass *camel_transport_class =
		CAMEL_TRANSPORT_CLASS (camel_groupwise_transport_class);

	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_groupwise_transport_class);
	
	parent_class = CAMEL_TRANSPORT_CLASS (camel_type_get_global_classfuncs (camel_transport_get_type ()));
	
	camel_service_class->connect = groupwise_transport_connect;
	camel_service_class->get_name = groupwise_transport_get_name;
	camel_service_class->construct = groupwise_transport_construct;
	
	/* virtual method overload */
	camel_transport_class->send_to = groupwise_send_to;
}

static void
camel_groupwise_transport_init (CamelTransport *transport)
{
	return;
}

static void
groupwise_transport_construct (CamelService *service, CamelSession *session,
		CamelProvider *provider, CamelURL *url,
		CamelException *ex)
{
	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);
	if (camel_exception_is_set (ex))
		return;
}

CamelType
camel_groupwise_transport_get_type (void)
{
	static CamelType camel_groupwise_transport_type = CAMEL_INVALID_TYPE;

	if (camel_groupwise_transport_type == CAMEL_INVALID_TYPE) {
		camel_groupwise_transport_type =
			camel_type_register (CAMEL_TRANSPORT_TYPE,
					     "CamelGroupwiseTransport",
					     sizeof (CamelGroupwiseTransport),
					     sizeof (CamelGroupwiseTransportClass),
					     (CamelObjectClassInitFunc) camel_groupwise_transport_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_groupwise_transport_init,
					     NULL);
	}

	return camel_groupwise_transport_type;
}

static char *groupwise_transport_get_name (CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup_printf (_("GroupWise server %s"), service->url->host);
	else {
		return g_strdup_printf (_("GroupWise mail delivery via %s"),
				service->url->host);
	}
}


static gboolean
groupwise_transport_connect (CamelService *service, CamelException *ex)
{
	return TRUE;

}


static gboolean
groupwise_send_to (CamelTransport *transport, 
		   CamelMimeMessage *message,
		   CamelAddress *from, 
		   CamelAddress *recipients,
		   CamelException *ex)
{
	CamelService *service;
	CamelStore *store =  NULL;
	CamelGroupwiseStore *groupwise_store = NULL;
	CamelGroupwiseStorePrivate *priv = NULL;
	EGwItem *item ,*temp_item=NULL;
	EGwConnection *cnc = NULL;
	EGwConnectionStatus status = 0;
	GSList *sent_item_list = NULL;
	char *url = NULL;
	const char *reply_request = NULL;
	EGwItemLinkInfo *info = NULL;

	if (!transport) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE, _("Authentication failed"));
		return FALSE;
	}

	service = CAMEL_SERVICE(transport);
	url = camel_url_to_string (service->url,
			           (CAMEL_URL_HIDE_PASSWORD |
				    CAMEL_URL_HIDE_PARAMS   |
				    CAMEL_URL_HIDE_AUTH) );

	camel_operation_start (NULL, _("Sending Message") );

	/*camel groupwise store and cnc*/
	store = camel_session_get_store (service->session, url, ex );
	g_free (url);
	if (!store) {
		g_warning ("ERROR: Could not get a pointer to the store");
		camel_exception_set (ex, CAMEL_EXCEPTION_STORE_INVALID, _("Cannot get folder: Invalid operation on this store"));
		return FALSE;
	}
	groupwise_store = CAMEL_GROUPWISE_STORE (store);
	priv = groupwise_store->priv;

	cnc = cnc_lookup (priv);
	if (!cnc) {
		g_warning ("||| Eh!!! Failure |||\n");
		camel_operation_end (NULL);
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE, _("Authentication failed"));
		return FALSE;
	}
	
	item = camel_groupwise_util_item_from_message (cnc, message, from);
	
	reply_request = (char *)camel_medium_get_header (CAMEL_MEDIUM (message), "In-Reply-To");
	if (reply_request) {
		char *id;
		int len = strlen (reply_request);

		id = (char *)g_malloc0 (len-1);
		id = memcpy(id, reply_request+2, len-3);
		status = e_gw_connection_reply_item (cnc, id, NULL, &temp_item);
		if (status != E_GW_CONNECTION_STATUS_OK) 
			g_warning ("Could not send a replyRequest...continuing without!!\n");
		else {
			info = e_gw_item_get_link_info (temp_item);
			e_gw_item_set_link_info (item, info);
		}

		g_free (id);
	}
	
	
	/*Send item*/
	status = e_gw_connection_send_item (cnc, item, &sent_item_list);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		g_warning (" Error Sending mail");
		camel_operation_end (NULL);
		e_gw_item_set_link_info (item, NULL);
		g_object_unref (item);
		if (temp_item)
			g_object_unref (temp_item);

		/* FIXME: 58652 should be changed with an enum.*/
		if (status == 58652)
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE, _("You have exceeded this account's storage limit. Your messages are queued in your Outbox. Resend by pressing Send/Receive after deleting/archiving some of your mail.\n"));
		else
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,_("Could not send message: %s"),_("Unknown error"));
		status = 0;
		return FALSE;
	}
	e_gw_item_set_link_info (item, NULL);

	e_gw_item_set_recipient_list (item, NULL);

	if (temp_item)
		g_object_unref (temp_item);
	g_object_unref (item);

	camel_operation_end (NULL);

	return TRUE;
}


/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-groupwise-transport.c : class for an groupwise transport */

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-groupwise-transport.h"
#include "camel-groupwise-store.h"

#include "camel-i18n.h"
#include "camel-session.h" 
#include "camel-stream.h"
#include "camel-stream-mem.h"
#include "camel-medium.h"
#include "camel-data-wrapper.h"
#include "camel-mime-message.h"
#include "camel-multipart.h"
#include "camel-mime-utils.h"

#include <string.h>


static gboolean groupwise_send_to (CamelTransport *transport,
				  CamelMimeMessage *message,
				  CamelAddress *from,
				  CamelAddress *recipients,
				  CamelException *ex) ;

static gboolean groupwise_connect (CamelService *service, CamelException *ex) ;
static char *groupwise_transport_get_name (CamelService *service, gboolean brief) ;
static void groupwise_transport_construct (CamelService *service, CamelSession *session,
					   CamelProvider *provider, CamelURL *url, CamelException *ex) ;


static CamelTransportClass *parent_class = NULL ;



static void
camel_groupwise_transport_class_init (CamelGroupwiseTransportClass *camel_groupwise_transport_class)
{
	CamelTransportClass *camel_transport_class =
		CAMEL_TRANSPORT_CLASS (camel_groupwise_transport_class);

	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_groupwise_transport_class);
	
	parent_class = CAMEL_TRANSPORT_CLASS (camel_type_get_global_classfuncs (camel_transport_get_type ()));
	
	camel_service_class->connect = groupwise_connect ;
	camel_service_class->get_name = groupwise_transport_get_name ;
	camel_service_class->construct = groupwise_transport_construct ;
	
	/* virtual method overload */
	camel_transport_class->send_to = groupwise_send_to ;
}

static void
camel_groupwise_transport_init (CamelTransport *transport)
{
	return ;
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
		return g_strdup_printf (_("Groupwise server %s"), service->url->host);
	else {
		return g_strdup_printf (_("Groupwise mail delivery via %s"),
				service->url->host);
	}
}


static gboolean
groupwise_connect (CamelService *service, CamelException *ex)
{
	return TRUE ;

}


static gboolean
groupwise_send_to (CamelTransport *transport, CamelMimeMessage *message,
		  CamelAddress *from, CamelAddress *recipients,
		  CamelException *ex)
{
	CamelService *service = CAMEL_SERVICE(transport) ;

	CamelStore *store =  NULL ;
	
	CamelGroupwiseStore *groupwise_store = NULL;
	CamelGroupwiseStorePrivate *priv = NULL;
	
	CamelStreamMem *content ;
	
	EGwItem *item ;
	EGwConnection *cnc = NULL;
	EGwConnectionStatus status ;
	EGwItemRecipient *recipient ;
	
	int total_add ;

	CamelDataWrapper *dw ;
	CamelMimePart *mime_part = CAMEL_MIME_PART(message) ;
	
	guint part_count ;
	GSList *list = NULL, *recipient_list = NULL ;
	char *url = NULL ;
	char *buffer = NULL ;
	int i ;

	item = e_gw_item_new_empty () ;
	url = camel_url_to_string (service->url,
			(CAMEL_URL_HIDE_PASSWORD|
			 CAMEL_URL_HIDE_PARAMS|
			 CAMEL_URL_HIDE_AUTH) );
	
	camel_operation_start (NULL, _("Sending message")) ;

	/* Get a pointer to the store and the CNC. The idea is to get the session information,
	 * so that we neednt make a connection again.
	 */
	store = camel_session_get_store (service->session, url, ex ) ;
	if (!store) {
		g_print ("ERROR: Could not get a pointer to the store") ;
		camel_operation_end (NULL) ;
		return FALSE ;
	}
	groupwise_store = CAMEL_GROUPWISE_STORE (store) ;
	priv = groupwise_store->priv ;

	cnc = cnc_lookup (priv) ;
	if (!cnc) {
		g_print ("||| Eh!!! Failure |||\n") ;
		camel_operation_end (NULL) ;
		return FALSE ;
	}

	/*
	 * Get the mime parts from CamelMimemessge 
	 */
	dw = camel_medium_get_content_object (CAMEL_MEDIUM (mime_part));
	if(!dw) {
		g_print ("ERROR: Could not get Datawrapper") ;
		camel_operation_end (NULL) ;
		return FALSE ;
	}

	content = (CamelStreamMem *)camel_stream_mem_new();
	camel_data_wrapper_decode_to_stream(dw, (CamelStream *)content);

	if (CAMEL_IS_MULTIPART (dw)) {
		part_count = camel_multipart_get_number (CAMEL_MULTIPART(dw)) ;
		g_print ("Multipart message : %d\n",part_count) ;
		for (i=0 ; i<part_count ; i++) {
			CamelContentType *type  ;
			CamelMimePart *part ;
			CamelStreamMem *part_content = (CamelStreamMem *)camel_stream_mem_new();
			const char *disposition, *filename ; 
		
			part = camel_multipart_get_part (CAMEL_MULTIPART(dw), i) ;

			camel_data_wrapper_decode_to_stream(CAMEL_DATA_WRAPPER(part), (CamelStream *)part_content);
			type = camel_mime_part_get_content_type(part) ;
			filename = camel_mime_part_get_filename (part) ;
			disposition = camel_mime_part_get_disposition (part) ;
	
			g_free ((char *)filename) ;
			g_free ((char *)disposition) ;
			camel_content_type_unref (type) ;
			camel_object_unref (part_content) ;
		}

	} else {
		CamelContentType *type  ;
		CamelStream *stream = camel_stream_mem_new () ;
		int count ;
		
		type = camel_data_wrapper_get_mime_type_field(dw) ;
		g_print ("Does not contain multiple parts : %s\n",type->type) ;

		count = camel_data_wrapper_decode_to_stream (dw, stream) ;


	}

	/*Populate the item structure to send it to the GW server*/
	g_print ("|| Subject : %s |||\n", camel_mime_message_get_subject (message)) ;
	total_add = camel_address_length (recipients) ;
	for (i=0 ; i<total_add ; i++) {
		const char *name = NULL, *addr = NULL ;
		if(camel_internet_address_get ((CamelInternetAddress *)recipients, i , &name, &addr )) {
			
			recipient = g_new0 (EGwItemRecipient, 1);
		
			recipient->email = g_strdup (addr) ;
			recipient->display_name = g_strdup (name) ;
			recipient->type = E_GW_ITEM_RECIPIENT_TO;
			recipient->status = E_GW_ITEM_STAT_NONE ;
			recipient_list= g_slist_append (recipient_list, recipient) ;	
		}
	}

	e_gw_item_set_recipient_list (item, recipient_list) ;

	e_gw_item_set_item_type (item, E_GW_ITEM_TYPE_MAIL) ;
	e_gw_item_set_subject (item, camel_mime_message_get_subject(message)) ;
	
	buffer = g_malloc0 (content->buffer->len+1) ;
	buffer = memcpy (buffer, content->buffer->data, content->buffer->len) ;
	e_gw_item_set_message (item, buffer);

	
	/*Send item*/
	status = e_gw_connection_send_item (cnc, item, &list) ;
	if (status != E_GW_CONNECTION_STATUS_OK) {
		g_print (" Error Sending mail") ;
		camel_operation_end (NULL) ;
		g_free (buffer) ;
		return FALSE ;
	}
	
	e_gw_item_set_recipient_list (item, NULL) ;
	
	g_object_unref (item) ;

	camel_object_unref(content) ;
	
	camel_operation_end (NULL) ;

	g_free (buffer) ;

	return TRUE;
}
	



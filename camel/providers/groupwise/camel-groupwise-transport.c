/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-groupwise-transport.c : class for an groupwise transport */

/* 
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
 *	    Parthasarathi Susarla <sparthasarathi@novell.com>
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
#include <libsoup/soup-misc.h>


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
groupwise_send_to (CamelTransport *transport, 
		   CamelMimeMessage *message,
		   CamelAddress *from, 
		   CamelAddress *recipients,
		   CamelException *ex)
{
	CamelService *service = CAMEL_SERVICE(transport) ;

	CamelStore *store =  NULL ;

	CamelGroupwiseStore *groupwise_store = NULL;
	CamelGroupwiseStorePrivate *priv = NULL;


	EGwItem *item ;
	EGwConnection *cnc = NULL;
	EGwConnectionStatus status ;
	EGwItemRecipient *recipient ;
	EGwItemOrganizer *org = g_new0 (EGwItemOrganizer, 1) ;

	char *display_name = NULL, *email = NULL, *send_options = NULL ;

	int total_add ;

	CamelMultipart *mp ;

	GSList *sent_item_list = NULL, *recipient_list = NULL, *attach_list = NULL ;
	char *url = NULL ;
	int i ;
	/*Egroupwise item*/
	item = e_gw_item_new_empty () ;

	url = camel_url_to_string (service->url,
			           (CAMEL_URL_HIDE_PASSWORD |
				    CAMEL_URL_HIDE_PARAMS   |
				    CAMEL_URL_HIDE_AUTH) ) ;

	camel_operation_start (NULL, _("Sending Message") ) ;

	/*camel groupwise store and cnc*/
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

	/*poulate recipient list*/
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

	/** Get the mime parts from CamelMimemessge **/
	mp = (CamelMultipart *)camel_medium_get_content_object (CAMEL_MEDIUM (message));
	if(!mp) {
		g_print ("ERROR: Could not get content object") ;
		camel_operation_end (NULL) ;
		return FALSE ;
	}

	if (CAMEL_IS_MULTIPART (mp)) {
		/*contains multiple parts*/
		guint part_count ;
		
		part_count = camel_multipart_get_number (mp) ;
		for ( i=0 ; i<part_count ; i++) {
			CamelContentType *type ;
			CamelMimePart *part ;
			CamelStreamMem *content = (CamelStreamMem *)camel_stream_mem_new () ;
			CamelDataWrapper *dw = camel_data_wrapper_new () ;
			EGwItemAttachment *attachment = g_new0 (EGwItemAttachment, 1) ;
			const char *disposition, *filename ;
			char *buffer = NULL ;
			char *mime_type = NULL ;
			int len ;

			part = camel_multipart_get_part (mp, i) ;
			dw = camel_medium_get_content_object (CAMEL_MEDIUM (part)) ;
			

			camel_data_wrapper_write_to_stream(dw, (CamelStream *)content) ;
			buffer = g_malloc0 (content->buffer->len+1) ;
			g_print (">>>>>> length:%d |||\n", content->buffer->len) ;
			buffer = memcpy (buffer, content->buffer->data, content->buffer->len) ;
			g_print (">>>>>> buffer: \n %s\n", buffer) ;
			len = content->buffer->len ;

			filename = camel_mime_part_get_filename (part) ;
			if (!filename) {
				/*the message*/
				e_gw_item_set_message (item, buffer) ;
			} else {
				mime_type = camel_data_wrapper_get_mime_type (dw) ;
				g_print (">>>>mime:%s |||\n", mime_type) ;
				type = camel_mime_part_get_content_type(part) ;
				disposition = camel_mime_part_get_disposition (part) ;
				attachment->data = g_malloc0 (content->buffer->len+1) ;
				attachment->data = memcpy (attachment->data, content->buffer->data, content->buffer->len) ;
				attachment->name = g_strdup (filename) ;
				attachment->contentType = g_strdup_printf ("%s/%s", type->type, type->subtype) ;
				g_print (">>>>>> %s/%s <<<<<< \n", type->type, type->subtype) ;
				attachment->size = content->buffer->len ;
				
				attach_list = g_slist_append (attach_list, attachment) ;
				g_free ((char *)disposition) ;
				g_free ((char *)mime_type) ;
				camel_content_type_unref (type) ;
			}
			g_free (buffer) ;
			g_free ((char *)filename) ;
			camel_object_unref (content) ;

		} /*end of for*/
		
	} else {
		/*only message*/
		CamelStreamMem *content = (CamelStreamMem *)camel_stream_mem_new () ;
		CamelDataWrapper *dw = camel_data_wrapper_new () ;
		char *buffer = NULL ;
			
		dw = camel_medium_get_content_object (CAMEL_MEDIUM (mp)) ;
		camel_data_wrapper_write_to_stream(dw, (CamelStream *)content) ;
		buffer = g_malloc0 (content->buffer->len+1) ;
		g_print (">>>>>> length:%d |||\n", content->buffer->len) ;
		buffer = memcpy (buffer, content->buffer->data, content->buffer->len) ;
				
		e_gw_item_set_message (item, buffer) ;
		
		g_free (buffer) ;
		camel_object_unref (content) ;
	}
	/*Populate EGwItem*/
	/*From Address*/
	camel_internet_address_get ((CamelInternetAddress *)from, 0 , &display_name, &email) ;
	g_print ("from : %s : %s\n", display_name,email) ;
	org->display_name = g_strdup (display_name) ;
	org->email = g_strdup (email) ;
	e_gw_item_set_organizer (item, org) ;
	/*recipient list*/
	e_gw_item_set_recipient_list (item, recipient_list) ;
	/*Item type is mail*/
	e_gw_item_set_item_type (item, E_GW_ITEM_TYPE_MAIL) ;
	/*subject*/
	e_gw_item_set_subject (item, camel_mime_message_get_subject(message)) ;
	/*attachmets*/
	e_gw_item_set_attach_id_list (item, attach_list) ;
	
	/*send options*/
	e_gw_item_set_sendoptions (item, TRUE) ;

	if ((char *)camel_medium_get_header (CAMEL_MEDIUM(message), X_REPLY_CONVENIENT)) 
		e_gw_item_set_reply_request (item, TRUE) ;
	
	send_options = (char *)camel_medium_get_header (CAMEL_MEDIUM(message), X_REPLY_WITHIN) ;
	if (send_options) 
		e_gw_item_set_reply_within (item, send_options) ;

	send_options = (char *)camel_medium_get_header (CAMEL_MEDIUM(message),X_EXPIRE_AFTER) ;
	if (send_options)
		e_gw_item_set_expires (item, send_options) ;

	send_options = (char *)camel_medium_get_header (CAMEL_MEDIUM(message), X_DELAY_UNTIL) ;
	if (send_options)
		e_gw_item_set_delay_until (item, send_options) ;

	send_options = (char *)camel_medium_get_header (CAMEL_MEDIUM(message), X_TRACK_WHEN) ;
	if (send_options) {
		switch (atoi(send_options)) {
			case 1: e_gw_item_set_track_info (item, E_GW_ITEM_DELIVERED);
				break;
			case 2: e_gw_item_set_track_info (item, E_GW_ITEM_DELIVERED_OPENED);
				break;
			case 3: e_gw_item_set_track_info (item, E_GW_ITEM_ALL);
				break;
			default: e_gw_item_set_track_info (item, E_GW_ITEM_NONE);
				 break;
		}
	}

	if ((char *)camel_medium_get_header (CAMEL_MEDIUM(message), X_AUTODELETE))
		e_gw_item_set_autodelete (item, TRUE) ;

	send_options = (char *)camel_medium_get_header (CAMEL_MEDIUM (message), X_RETURN_NOTIFY_OPEN) ;
	if (send_options) {
		switch (atoi(send_options)) {
			case 0: e_gw_item_set_notify_opened (item, E_GW_ITEM_NOTIFY_NONE);
				break;
			case 1: e_gw_item_set_notify_opened (item, E_GW_ITEM_NOTIFY_MAIL);
		}
	}
	send_options = (char *)camel_medium_get_header (CAMEL_MEDIUM (message), X_RETURN_NOTIFY_DECLINE) ;
	if (send_options) {
		switch (atoi(send_options)) {
			case 0: e_gw_item_set_notify_declined (item, E_GW_ITEM_NOTIFY_NONE);
				break;
			case 1: e_gw_item_set_notify_declined (item, E_GW_ITEM_NOTIFY_MAIL);
		}
	}

	/*Send item*/
	status = e_gw_connection_send_item (cnc, item, &sent_item_list) ;
	if (status != E_GW_CONNECTION_STATUS_OK) {
		g_print (" Error Sending mail") ;
		camel_operation_end (NULL) ;
		return FALSE ;
	}

	e_gw_item_set_recipient_list (item, NULL) ;

	g_object_unref (item) ;

	camel_operation_end (NULL) ;

	return TRUE;

}


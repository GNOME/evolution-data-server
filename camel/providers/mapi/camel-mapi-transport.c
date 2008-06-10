/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Johnny Jacob <jjohnny@novell.com>
 *   
 * Copyright (C) 2007, Novell Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 3 of the GNU Lesser General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <libmapi/libmapi.h>
#include <gen_ndr/exchange.h>

#include <camel/camel-data-wrapper.h>
#include <camel/camel-exception.h>
#include <camel/camel-mime-filter-crlf.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-multipart.h>
#include <camel/camel-session.h>
#include <camel/camel-stream-filter.h>
#include <camel/camel-stream-mem.h>


#include "camel-mapi-transport.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <camel/camel-sasl.h>
#include <camel/camel-utf8.h>
#include <camel/camel-tcp-stream-raw.h>

#ifdef HAVE_SSL
#include <camel/camel-tcp-stream-ssl.h>
#endif


#include <camel/camel-private.h>
#include <camel/camel-i18n.h>
#include <camel/camel-net-utils.h>
#include "camel-mapi-store.h"
#include "camel-mapi-folder.h"
#include "camel-mapi-store-summary.h"
#include <camel/camel-session.h>
#include <camel/camel-store-summary.h>
#define d(x) x

#include <camel/camel-seekable-stream.h>

#define STREAM_SIZE 4000

CamelStore *get_store(void);

void	set_store(CamelStore *);

static void
mapi_item_add_recipient (const char *recipients, OlMailRecipientType type, GSList **recipient_list);
static int
mapi_message_item_send (MapiItem *item, GSList *attachments, GSList *recipients);


static void
mapi_item_debug_dump (MapiItem *item)
{
	printf("-----------------\n\n");
        printf("%s(%d):%s: \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	printf("item->header.from : %s\n",item->header.from);
	//Use Recipient List
	printf("item->header.subject : %s\n",item->header.subject);
	//printf("item->msg.body_stream : %s\n",item->msg.body_stream);
	printf("item->msg.body_plain_text : %s\n",item->msg.body_plain_text);
	printf("-----------------\n\n");
}

static void
mapi_item_set_from(MapiItem *item, const char *from)
{
	if (item->header.from) { 
		free(item->header.from);
	}
	item->header.from = strdup(from);
}

static void
mapi_item_set_subject(MapiItem *item, const char *subject)
{
	if (item->header.subject)
		free(item->header.subject);

	item->header.subject = strdup(subject);
}

#define MAX_READ_SIZE 0x1000

static void
mapi_item_set_body_stream (MapiItem *item, CamelStream *body, MapiItemPartType part_type)
{
	guint8 *buf = g_new0 (guint8 , STREAM_SIZE);
	guint32	read_size = 0;
	ExchangeMAPIStream *stream = g_new0 (ExchangeMAPIStream, 1);

	camel_seekable_stream_seek((CamelSeekableStream *)body, 0, CAMEL_STREAM_SET);

	stream->value = g_byte_array_new ();

	while((read_size = camel_stream_read(body, (char *)buf, STREAM_SIZE))){
		if (read_size == -1) 
			return;

		stream->value = g_byte_array_append (stream->value, (char *) buf, read_size);
	}

	switch (part_type) {
	case PART_TYPE_TEXT_HTML :
		stream->proptag = PR_HTML;
		break;
	case PART_TYPE_PLAIN_TEXT:
		stream->proptag = PR_BODY;
		break;
	}

	if (stream->value->len < MAX_READ_SIZE)
		item->msg.body_parts = g_slist_append (item->msg.body_parts, stream);
	else
		item->generic_streams = g_slist_append (item->generic_streams, stream);

}

static gboolean
mapi_item_add_attach(MapiItem *item, const gchar *filename, const char *description, 
		     CamelStream *content_stream, int content_size)
{
	guint8 *buf = g_new0 (guint8 , STREAM_SIZE);
	guint32	read_size;
	ExchangeMAPIAttachment *item_attach;
	item_attach = g_new0 (ExchangeMAPIAttachment, 1);

	//?? :	attach->id = contents->n_attach;
	if (filename) {
		item_attach->filename = g_strdup(filename);
	}

	item_attach->value = g_byte_array_new ();

	camel_seekable_stream_seek((CamelSeekableStream *)content_stream, 0, CAMEL_STREAM_SET);
	while((read_size = camel_stream_read(content_stream, (char *)buf, STREAM_SIZE))){
		item_attach->value = g_byte_array_append (item_attach->value, buf, read_size);
	}

	item->attachments = g_slist_append(item->attachments, item_attach);

	return TRUE;
}

static gboolean 
mapi_do_multipart(CamelMultipart *mp, MapiItem *item)
{
	CamelDataWrapper *dw;
	CamelStream *content_stream;
	CamelContentType *type;
	CamelMimePart *part;
	gint n_part, i_part;
	const gchar *filename;
	const gchar *description;
	const gchar *content_id;
	gint content_size;

	n_part = camel_multipart_get_number(mp);
	for (i_part = 0; i_part < n_part; i_part++) {
		/* getting part */
		part = camel_multipart_get_part(mp, i_part);
		dw = camel_medium_get_content_object (CAMEL_MEDIUM (part));
		if (CAMEL_IS_MULTIPART(dw)) {
			/* recursive */
			if (!mapi_do_multipart(CAMEL_MULTIPART(dw), item))
				return FALSE;
			continue ;
		}
		/* filename */
		filename = camel_mime_part_get_filename(part);

		dw = camel_medium_get_content_object(CAMEL_MEDIUM(part));
		content_stream = camel_stream_mem_new();
		content_size = camel_data_wrapper_decode_to_stream (dw, (CamelStream *) content_stream);
		camel_seekable_stream_seek((CamelSeekableStream *)content_stream, 0, CAMEL_STREAM_SET);

		description = camel_mime_part_get_description(part);
		content_id = camel_mime_part_get_content_id(part);
		
		type = camel_mime_part_get_content_type(part);

		if (i_part == 0 && camel_content_type_is (type, "text", "plain")) {
			mapi_item_set_body_stream (item, content_stream, PART_TYPE_PLAIN_TEXT);
		} else if (camel_content_type_is (type, "text", "html")) {
			mapi_item_set_body_stream (item, content_stream, PART_TYPE_TEXT_HTML);
		} else {
			mapi_item_add_attach(item, filename, description, 
					     content_stream, content_size);
		}
	}

	return TRUE;
}


static gboolean
mapi_send_to (CamelTransport *transport, CamelMimeMessage *message,
	      CamelAddress *from, CamelAddress *recipients, CamelException *ex)
{
	CamelDataWrapper *dw = NULL;
	CamelContentType *type;
	CamelStream *content_stream;
	CamelMultipart *multipart;
	const CamelInternetAddress *to, *cc, *bcc;
	MapiItem *item = g_new0 (MapiItem, 1);
	const char *namep;
	const char *addressp;
	const char *content_type;		
	gint st;
	ssize_t	content_size;
	GSList *recipient_list = NULL;
	GSList *attach_list = NULL;
	gint i = 0;
	/* headers */

	if (!camel_internet_address_get((const CamelInternetAddress *)from, 0, &namep, &addressp)) {
		printf("index\n");
		return (FALSE);
	}
	/** WARNING: double check **/
	mapi_item_set_from (item, namep);

	to = camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_TO);
	for (i = 0; camel_internet_address_get(to, i, &namep, &addressp); i++){
		mapi_item_add_recipient (addressp, olTo, &recipient_list);
	}

	cc = camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_CC);
	for (i = 0; camel_internet_address_get(cc, i, &namep, &addressp); i++) {
		mapi_item_add_recipient (addressp, olCC, &recipient_list);
	}

	bcc = camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_BCC);
	for (i = 0; camel_internet_address_get(bcc, i, &namep, &addressp); i++) {
		mapi_item_add_recipient (addressp, olBCC, &recipient_list);
	}
	
	if (camel_mime_message_get_subject(message)) {
		mapi_item_set_subject(item, camel_mime_message_get_subject(message));
	}

	/* contents body */
	multipart = (CamelMultipart *)camel_medium_get_content_object (CAMEL_MEDIUM (message));

	if (CAMEL_IS_MULTIPART(multipart)) {
		if (mapi_do_multipart(CAMEL_MULTIPART(multipart), item))
			printf("camel message multi part error\n");
	} else {
		content_stream = (CamelStream *)camel_stream_mem_new();
		dw = camel_medium_get_content_object (CAMEL_MEDIUM (message));
		type = camel_mime_part_get_content_type((CamelMimePart *)message);
		content_type = camel_content_type_simple (type);
		content_size = camel_data_wrapper_write_to_stream(dw, (CamelStream *)content_stream);
		mapi_item_set_body_stream (item, content_stream, PART_TYPE_PLAIN_TEXT);
	}
	
	mapi_item_debug_dump (item);

	/* send */
	st = mapi_message_item_send(item, attach_list, recipient_list);

	if (st == -1) {
		printf("[!] cannot send(%s)\n", item->header.to);
		mapi_errstr("Cannot Send", GetLastError()); 
		return (FALSE);
	}
	
	return (TRUE);
}


static char*
mapi_transport_get_name(CamelService *service, gboolean brief)
{
	if (brief) {
		return g_strdup_printf (_("MAPI server %s"), service->url->host);
	} else {
		return g_strdup_printf (_("MAPI service for %s on %s"),
					service->url->user, service->url->host);
	}
}


static void
camel_mapi_transport_class_init(CamelMapiTransportClass *camel_mapi_transport_class)
{
	CamelTransportClass *camel_transport_class =
		CAMEL_TRANSPORT_CLASS (camel_mapi_transport_class);
	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_mapi_transport_class);
  
	camel_service_class->get_name = mapi_transport_get_name;
	camel_transport_class->send_to = mapi_send_to;
}

static void
camel_mapi_transport_init (CamelTransport *transport)
{

}

CamelType
camel_mapi_transport_get_type (void)
{
	static CamelType camel_mapi_transport_type = CAMEL_INVALID_TYPE;
  
	if (camel_mapi_transport_type == CAMEL_INVALID_TYPE) {
		camel_mapi_transport_type =
			camel_type_register (CAMEL_TRANSPORT_TYPE,
					     "CamelMapiTransport",
					     sizeof (CamelMapiTransport),
					     sizeof (CamelMapiTransportClass),
					     (CamelObjectClassInitFunc) camel_mapi_transport_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_mapi_transport_init,
					     NULL);
	}

	return camel_mapi_transport_type;
}

static gint
mail_build_props (struct SPropValue **value, struct SPropTagArray *SPropTagArray, gpointer data)
{

	MapiItem *item = (MapiItem *) data;
	struct SPropValue *props;
	GSList *l;

	uint32_t *msgflag = g_new0 (uint32_t, 1);
	uint32_t *editor = g_new0 (uint32_t, 1);
	int i=0;

	props = g_new0 (struct SPropValue, 6);

	set_SPropValue_proptag(&props[i++], PR_CONVERSATION_TOPIC, g_strdup (item->header.subject));
	set_SPropValue_proptag(&props[i++], PR_NORMALIZED_SUBJECT, g_strdup (item->header.subject));

	*msgflag = MSGFLAG_UNSENT;
	set_SPropValue_proptag(&props[i++], PR_MESSAGE_FLAGS, (void *)msgflag);

	for (l = item->msg.body_parts; l; l = l->next) {
		ExchangeMAPIStream *stream = (ExchangeMAPIStream *) (l->data);
		struct SBinary_short *bin = g_new0 (struct SBinary_short, 1);

		bin->cb = stream->value->len;
		bin->lpb = (uint8_t *)stream->value->data;
		if (stream->proptag == PR_HTML)
			set_SPropValue_proptag(&props[i++], stream->proptag, (void *)bin);
		else if (stream->proptag == PR_BODY)
			set_SPropValue_proptag(&props[i++], stream->proptag, (void *)stream->value->data);
	}

	/*  FIXME : */
	/* editor = EDITOR_FORMAT_PLAINTEXT; */
	/* set_SPropValue_proptag(&props[i++], PR_MSG_EDITOR_FORMAT, (const void *)editor); */

	*value = props;
	return i;
}

static void
mapi_item_add_recipient (const char *recipients, OlMailRecipientType type, GSList **recipient_list)
{
	if (!recipients)
		return ;

	ExchangeMAPIRecipient *recipient = g_new0 (ExchangeMAPIRecipient, 1);

	recipient->email_id = recipients;
	recipient->type = type;

	*recipient_list = g_slist_append (*recipient_list, recipient);
}

static int
mapi_message_item_send (MapiItem *item, GSList *attachments, GSList *recipients)
{
	guint64 fid = 0;

	exchange_mapi_create_item (olFolderOutbox, fid, NULL, NULL, mail_build_props, item, recipients, item->attachments, item->generic_streams);

	return 0;
}

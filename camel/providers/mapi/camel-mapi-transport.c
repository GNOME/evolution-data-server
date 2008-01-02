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
CamelStore *get_store(void);

void	set_store(CamelStore *);

static void
mapi_item_add_recipient (const char *recipients, ExchangeMAPIRecipientType type, GSList **recipient_list);


static void
mapi_item_debug_dump (MapiItem *item)
{
	printf("-----------------\n\n");
        printf("%s(%d):%s: \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	printf("item->header.from : %s\n",item->header.from);
	printf("item->header.to : %s\n",item->header.to);
	printf("item->header.cc : %s\n",item->header.cc);
	printf("item->header.bcc : %s\n",item->header.bcc);
	printf("item->header.subject : %s\n",item->header.subject);
	printf("item->msg.body_stream : %s\n",item->msg.body_stream);
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
mapi_item_add_recipient_bcc(MapiItem *item, const char *bcc)
{
	int len = 0;

	if (!bcc)
		return ;
	if (item->header.bcc)
		len = strlen(item->header.bcc);
	item->header.bcc = realloc(item->header.bcc, len + strlen(bcc) + 2);
	if (len){
		item->header.bcc[len] = ',';
		memcpy(item->header.bcc + len + 1, bcc, strlen(bcc));
		item->header.bcc[len + 1 + strlen(bcc)] = 0;
	}
	else{
		memcpy(item->header.bcc + len, bcc, strlen(bcc));
		item->header.bcc[len + strlen(bcc)] = 0;
	}
	
}

static void
mapi_item_add_recipient_cc(MapiItem *item, const char *cc)
{
	int len = 0;

	if (!cc)
		return ;
	if (item->header.cc)
		len = strlen(item->header.cc);
	item->header.cc = realloc(item->header.cc, len + strlen(cc) + 2);
	if (len){
		item->header.cc[len] = ',';
		memcpy(item->header.cc + len + 1, cc, strlen(cc));
		item->header.cc[len + 1 + strlen(cc)] = 0;
	}
	else{
		memcpy(item->header.cc + len, cc, strlen(cc));
		item->header.cc[len + strlen(cc)] = 0;
	}
	
}


static void 
mapi_item_add_recipient_to(MapiItem *item, const char *to)
{
	int len = 0;

	if (!to) return ;

	if (item->header.to)
		len = strlen(item->header.to);

	item->header.to = realloc(item->header.to, len + strlen(to) + 2);
	if (len){
		item->header.to[len] = ',';
		memcpy(item->header.to + len + 1, to, strlen(to));
		item->header.to[len + 1 + strlen(to)] = 0;
	}
	else{
		memcpy(item->header.to + len, to, strlen(to));
		item->header.to[len + strlen(to)] = 0;
	}
}

static void
mapi_item_set_subject(MapiItem *item, const char *subject)
{
	if (item->header.subject)
		free(item->header.subject);

	item->header.subject = strdup(subject);
}

static void
mapi_item_set_body_stream (MapiItem *item, CamelStream *body)
{
	item->msg.body_stream = body;
}


static char**
mapi_get_cmdline_recipients(TALLOC_CTX *mem_ctx, const char *recipients)
{
	char		**usernames;
	char		*tmp = NULL;
	uint32_t	j = 0;

	/* no recipients */
	if (!recipients) {
		return NULL;
	}

	if ((tmp = strtok((char *)recipients, ",")) == NULL) {
		DEBUG(2, ("Invalid recipient string format\n"));
		return NULL;
	}
	
	usernames = talloc_array(mem_ctx, char *, 2);
	usernames[0] = talloc_strdup(mem_ctx, tmp);
	
	for (j = 1; (tmp = strtok(NULL, ",")) != NULL; j++) {
		usernames = talloc_realloc(mem_ctx, usernames, char *, j+2);
		usernames[j] = talloc_strdup(mem_ctx, tmp);
	}
	usernames[j] = 0;

	return (usernames);
}

static gboolean
mapi_set_usernames_RecipientType(uint32_t *index, struct SRowSet *rowset, char **usernames, struct FlagList *flaglist,
					enum ulRecipClass RecipClass)
{
	uint32_t	i;
	uint32_t	count = *index;
	static uint32_t	counter = 0;

	if (count == 0) counter = 0;
	if (!usernames) return FALSE;

	for (i = 0; usernames[i]; i++) {
		if (flaglist->ulFlags[count] == MAPI_RESOLVED) {
			SetRecipientType(&(rowset->aRow[counter]), RecipClass);
			counter++;
		}
		count++;
	}
	
	*index = count;
	
	return TRUE;
}

static char**
mapi_collapse_recipients(TALLOC_CTX *mem_ctx, char **mapi_to, char **mapi_cc, char **mapi_bcc)
{
	uint32_t	count;
	uint32_t       	i;
	char		**usernames;

	if (!mapi_to && !mapi_cc && !mapi_bcc) return NULL;

	count = 0;
	for (i = 0; mapi_to  && mapi_to[i];  i++,  count++);
	for (i = 0; mapi_cc  && mapi_cc[i];  i++,  count++);
	for (i = 0; mapi_bcc && mapi_bcc[i]; i++, count++);

	usernames = talloc_array(mem_ctx, char *, count + 1);
	count = 0;

	for (i = 0; mapi_to && mapi_to[i]; i++, count++) {
		usernames[count] = talloc_strdup(mem_ctx, mapi_to[i]);
		printf("%s(%d):%s:username[%d]=%s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, count , usernames[count]);
	}

	for (i = 0; mapi_cc && mapi_cc[i]; i++, count++) {
		usernames[count] = talloc_strdup(mem_ctx, mapi_cc[i]);
	}

	for (i = 0; mapi_bcc && mapi_bcc[i]; i++, count++) {
		usernames[count] = talloc_strdup(mem_ctx, mapi_bcc[i]);
	}

	usernames[count++] = 0;

	return usernames;
}

static gboolean
mapi_send_to (CamelTransport *transport, CamelMimeMessage *message,
	      CamelAddress *from, CamelAddress *recipients, CamelException *ex)
{
	CamelDataWrapper *dw;
	CamelContentType *type;
	CamelStream *content_stream;
	const CamelInternetAddress *to, *cc, *bcc;
	MapiItem *item = g_new0 (MapiItem, 1);
	const char *namep;
	const char *addressp;
	const char *content_type;		
	int i;
	int st;
	ssize_t	sz;
	GSList *recipient_list = NULL;
	/* headers */

	if (!camel_internet_address_get((const CamelInternetAddress *)from, 0, &namep, &addressp)) {
		printf("index\n");
		return (FALSE);
	}
	/** WARNING: double check **/
	mapi_item_set_from (item, namep);

	to = camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_TO);
	for (i = 0; camel_internet_address_get(to, i, &namep, &addressp); i++){
		mapi_item_add_recipient (addressp, RECIPIENT_TO, &recipient_list);
	}

	cc = camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_CC);
	for (i = 0; camel_internet_address_get(cc, i, &namep, &addressp); i++) {
		mapi_item_add_recipient (addressp, RECIPIENT_CC, &recipient_list);
	}

	bcc = camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_BCC);
	for (i = 0; camel_internet_address_get(bcc, i, &namep, &addressp); i++) {
		mapi_item_add_recipient (addressp, RECIPIENT_BCC, &recipient_list);
	}
	
	if (camel_mime_message_get_subject(message)) {
		mapi_item_set_subject(item, camel_mime_message_get_subject(message));
	}
	mapi_item_debug_dump (item);

	/* contents body */
	dw = camel_medium_get_content_object (CAMEL_MEDIUM (message));

	if (CAMEL_IS_MULTIPART(dw)) {
/* 		if (do_multipart(CAMEL_MULTIPART(dw), &headers, &contents) == FALSE) { */
/* 			printf("camel message multi part error\n"); */
/* 		} */
	} else {
		content_stream = (CamelStream *)camel_stream_mem_new();
		type = camel_mime_part_get_content_type((CamelMimePart *)message);
		content_type = camel_content_type_simple (type);
		sz = camel_data_wrapper_write_to_stream(dw, (CamelStream *)content_stream);
		mapi_item_set_body_stream (item, content_stream);
	}
	
	
	/* send */
	st = mapi_message_item_send(item, recipient_list);

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
	uint32_t msgflag;
	int i=0;

	props = g_new0 (struct SPropValue, 4);

	set_SPropValue_proptag(&props[i++], PR_CONVERSATION_TOPIC, g_strdup (item->header.subject));
	set_SPropValue_proptag(&props[i++], PR_NORMALIZED_SUBJECT, g_strdup (item->header.subject));
/* TODO : Handle Body stream */
/* 	set_SPropValue_proptag(&props[i++], PR_BODY, g_strdup (item->msg.body_stream)); */
	msgflag = MSGFLAG_UNSENT;
	set_SPropValue_proptag(&props[i++], PR_MESSAGE_FLAGS, (void *)&msgflag);

	*value = props;
	return i;
}

static void
mapi_item_add_recipient (const char *recipients, ExchangeMAPIRecipientType type, GSList **recipient_list)
{
	int len = 0;

	if (!recipients)
		return ;
	ExchangeMAPIRecipient *recipient = g_new0 (ExchangeMAPIRecipient, 1);

	recipient->email_id = recipients;
	recipient->type = type;

	*recipient_list = g_slist_append (*recipient_list, recipient);
}

int
mapi_message_item_send (MapiItem *item, GSList *recipients)
{
	guint64 fid = 0;

	//Process the reciepient table.
	//Process Body Stream.
	exchange_mapi_create_item (olFolderOutbox, fid, NULL, NULL, mail_build_props, item, recipients, NULL);

	return 0;
}

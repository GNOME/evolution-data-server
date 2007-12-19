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
mapi_item_debug_dump (MapiItem *item)
{
        printf("%s(%d):%s: \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	printf("item->header.from : %s\n",item->header.from);
	printf("item->header.to : %s\n",item->header.to);
	printf("item->header.cc : %s\n",item->header.cc);
	printf("item->header.bcc : %s\n",item->header.bcc);
	printf("item->header.subject : %s\n",item->header.subject);
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
mapi_item_add_recipient(MapiItem *item, const char *to)
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

#if 0
//TODO : Replace this in servers/MAPI
#define CN_MSG_PROPS 2

static int
mapi_message_item_send(MapiItem *item)//, oc_message_contents_t *contents)
{
	enum MAPISTATUS	retval;
	TALLOC_CTX *mem_ctx;
	mapi_object_t obj_message;
	mapi_object_t			obj_attach;
	mapi_object_t			obj_body;
	mapi_object_t			obj_store;
	mapi_object_t			obj_outbox;
	mapi_object_t			obj_stream;
	mapi_id_t			id_outbox;
	struct SRowSet			*SRowSet = NULL;
	struct SPropTagArray		*SPropTagArray;
	struct SPropValue		SPropValue;
	struct SPropValue		props[CN_MSG_PROPS];
	struct FlagList			*flaglist = NULL;
	uint32_t			msgflag;
	char				**usernames = NULL;
	char				**usernames_to;
	char				**usernames_cc;
	char				**usernames_bcc;
	int				i_attach;
	uint32_t			read_size;
	uint8_t				buf[STREAM_SIZE];
	const oc_message_attach_t	*attach = NULL;
	DATA_BLOB			blob;
	struct SPropValue		props_attach[3];
	unsigned long			cn_props_attach;
	uint32_t			index = 0;
 
	mem_ctx = talloc_init("mapi_message_send");

	/* init objects */


	/* session::OpenMsgStore() */
	retval = OpenMsgStore(&obj_store);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	/* id_outbox = store->GeOutboxFolder() */
	retval = GetOutboxFolder(&obj_store, &id_outbox);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	/* outbox = store->OpenFolder(id_outbox) */
	retval = OpenFolder(&obj_store, id_outbox, &obj_outbox);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	printf("%s(%d):%s:creating a new message \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	/* message = outbox->CreateMessage() */
	retval = CreateMessage(&obj_outbox, &obj_message);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	/* message->ModifyRecipients() */
	SPropTagArray = set_SPropTagArray(mem_ctx, 0x6,
					  PR_OBJECT_TYPE,
					  PR_DISPLAY_TYPE,
					  PR_7BIT_DISPLAY_NAME,
					  PR_DISPLAY_NAME,
					  PR_SMTP_ADDRESS,
					  PR_GIVEN_NAME);

	/* fixme: fill with cc comma sep content.
	   usernames += strplit(headers->cc, ',');
	*/
	printf("%s(%d):%s:item->header.to = %s\n", __FILE__, __LINE__, __PRETTY_FUNCTION__, item->header.to);
	usernames_to = mapi_get_cmdline_recipients(mem_ctx, item->header.to);
	usernames_cc = mapi_get_cmdline_recipients(mem_ctx, item->header.cc);
	usernames_bcc = mapi_get_cmdline_recipients(mem_ctx, item->header.bcc);

	usernames = mapi_collapse_recipients(mem_ctx, usernames_to, usernames_cc, usernames_bcc);

	retval = ResolveNames((const char **)usernames, SPropTagArray, &SRowSet, &flaglist, 0);
	mapi_errstr("ResolveNames", GetLastError());
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	mapi_set_usernames_RecipientType(&index, SRowSet, usernames_to,  flaglist, MAPI_TO);
	mapi_set_usernames_RecipientType(&index, SRowSet, usernames_cc,  flaglist, MAPI_CC);
	mapi_set_usernames_RecipientType(&index, SRowSet, usernames_bcc, flaglist, MAPI_BCC);

	/* FIXME no saving mail */
	if (index == 0) {
		printf("no valid recipients set\n");
		return -1;
	}

	retval = ModifyRecipients(&obj_message, SRowSet);
	mapi_errstr("ModifyRecpients1", GetLastError());
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	SPropValue.ulPropTag = PR_SEND_INTERNET_ENCODING;
	SPropValue.value.l = 0;
	SRowSet_propcpy(mem_ctx, SRowSet, SPropValue);


/* 	mapidump_Recipients((const char **)usernames_to, SRowSet, flaglist); */
	retval = ModifyRecipients(&obj_message, SRowSet);
	mapi_errstr("ModifyRecpients2", GetLastError());
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	/* message->SetProps() */
	msgflag = MSGFLAG_UNSENT;
	set_SPropValue_proptag(&props[0], PR_SUBJECT, (void *)item->header.subject);
	set_SPropValue_proptag(&props[1], PR_MESSAGE_FLAGS, (void *)&msgflag);
	printf("%s(%d):%s:setting props \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	retval = SetProps(&obj_message, props, CN_MSG_PROPS);
	mapi_errstr("SetProps", GetLastError());
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	/* Create a stream and write body */
	retval = OpenStream(&obj_message, PR_BODY, 2, &obj_body);
	mapi_errstr("OpenStream", GetLastError());
	MAPI_RETVAL_IF(retval, retval, mem_ctx);
	printf("%s(%d):%s:opening stream \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	/* WriteStream body */
	camel_seekable_stream_seek((CamelSeekableStream *)item->msg.body_stream, 0, CAMEL_STREAM_SET);
	while((read_size = camel_stream_read(item->msg.body_stream, (char *)buf, STREAM_SIZE))){
		if (read_size == -1)
			return (-1);
		blob.length = read_size;
		blob.data = talloc_size(mem_ctx, read_size);
		memcpy(blob.data, buf, read_size);
		errno = 0;
		retval = WriteStream(&obj_body, &blob, &read_size);
		mapi_errstr("WriteStream", GetLastError());
		talloc_free(blob.data);
	}

	mapi_object_release(&obj_stream);
	printf("%s(%d):%s:before attachment \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	/* Attachment related operations */
# if 0
	for (i_attach = 0; i_attach < contents->n_attach; i_attach++) {
		attach = (const oc_message_attach_t*)g_list_nth_data(contents->l_attach, i_attach);
		mapi_object_init(&obj_attach);
		mapi_object_init(&obj_stream);
		retval = CreateAttach(&obj_message, &obj_attach);
/* 		mapi_errstr("CreateAttach", GetLastError()); */
		MAPI_RETVAL_IF(retval, retval, mem_ctx);
    
		/* send by value */
		props_attach[0].ulPropTag = PR_ATTACH_METHOD;
		props_attach[0].value.l = ATTACH_BY_VALUE;
		props_attach[1].ulPropTag = PR_RENDERING_POSITION;
		props_attach[1].value.l = 0;
		props_attach[2].ulPropTag = PR_ATTACH_FILENAME;
		props_attach[2].value.lpszA = attach->filename;
		cn_props_attach = 3;
    
		/* SetProps */
		retval = SetProps(&obj_attach, props_attach, cn_props_attach);
/* 		mapi_errstr("SetProps", GetLastError()); */
		MAPI_RETVAL_IF(retval, retval, mem_ctx);
    
		/* OpenStream on CreateAttach handle */
		retval = OpenStream(&obj_attach, PR_ATTACH_DATA_BIN, 2, &obj_stream);
/* 		mapi_errstr("OpenStream", GetLastError()); */
		MAPI_RETVAL_IF(retval, retval, mem_ctx);

		/* WriteStream attach */
		camel_seekable_stream_seek((CamelSeekableStream *)attach->buf_content, 0, CAMEL_STREAM_SET);
		while((read_size = camel_stream_read(attach->buf_content, (char *)buf, STREAM_SIZE))){
			if (read_size == -1) return read_size;
			blob.length = read_size;
			blob.data = talloc_size(mem_ctx, read_size);
			memcpy(blob.data, buf, read_size);
			errno = 0;
			retval = WriteStream(&obj_stream, &blob, &read_size);
/* 			mapi_errstr("WriteStream", GetLastError()); */
			talloc_free(blob.data);   
		}
		
		/* message->SaveChanges() */
		retval = SaveChanges(&obj_message, &obj_attach, KEEP_OPEN_READWRITE);
/* 		mapi_errstr("SaveChanges", GetLastError()); */
		MAPI_RETVAL_IF(retval, retval, mem_ctx);

		mapi_object_release(&obj_attach);
		mapi_object_release(&obj_stream);
	}
#endif
	/* message->SubmitMessage() */
	printf("%s(%d):%s:submitting messages \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	retval = SubmitMessage(&obj_message);
	mapi_errstr("SubmitMessage", GetLastError());
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	/* objects->Release() */
	mapi_object_release(&obj_body);
	mapi_object_release(&obj_message);
	mapi_object_release(&obj_outbox);
	mapi_object_release(&obj_store);
	talloc_free(mem_ctx);
	
	return 0;
}
#endif

static gboolean
mapi_send_to (CamelTransport *transport, CamelMimeMessage *message,
	      CamelAddress *from, CamelAddress *recipients, CamelException *ex)
{
	CamelDataWrapper *dw;
	CamelContentType *type;
	CamelStream *content_stream;
	const CamelInternetAddress *to, *cc, *bcc;
/* 	oc_message_contents_t contents; */
/* 	oc_message_headers_t headers; */
	MapiItem *item = g_new0 (MapiItem, 1);
	const char *namep;
	const char *addressp;
	const char *content_type;		
	int i;
	int st;
	ssize_t	sz;

	/* headers */

	if (!camel_internet_address_get((const CamelInternetAddress *)from, 0, &namep, &addressp)) {
		printf("index\n");
		return (FALSE);
	}
	/** WARNING: double check **/
	mapi_item_set_from (item, namep);

/* 	oc_thread_connect_lock(); */
/* 	mapi_initialize(); */
	
	to = camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_TO);
	for (i = 0; camel_internet_address_get(to, i, &namep, &addressp); i++){
		mapi_item_add_recipient(item, addressp);
	}

	cc = camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_CC);
	for (i = 0; camel_internet_address_get(cc, i, &namep, &addressp); i++) {
		mapi_item_add_recipient_cc(item, addressp);
	}

	bcc = camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_BCC);
	for (i = 0; camel_internet_address_get(bcc, i, &namep, &addressp); i++) {
		mapi_item_add_recipient_bcc(item, addressp);
	}
	
	if (camel_mime_message_get_subject(message)) {
		mapi_item_set_subject(item, camel_mime_message_get_subject(message));
	}
	mapi_item_debug_dump (item);

	/* contents body */
	dw = camel_medium_get_content_object (CAMEL_MEDIUM (message));
	//	oc_message_contents_init(&contents);
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
	st = mapi_message_item_send(item);
	//	oc_thread_connect_unlock();
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

int
mail_build_props (struct SPropValue **value, struct SPropTagArray *SPropTagArray, gpointer data)
{
	printf("%s(%d):%s:reached \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	MapiItem *item = (MapiItem *) data;
	struct SPropValue *props;
	uint32_t msgflag;
	int i=0;

	props = g_new0 (struct SPropValue, 4);

	printf("%s(%d):%s:item->header.subject : %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, item->header.subject);
	printf("%s(%d):%s:item->msg.body : %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, item->msg.body);

	set_SPropValue_proptag(&props[i++], PR_CONVERSATION_TOPIC, g_strdup (item->header.subject));
	set_SPropValue_proptag(&props[i++], PR_NORMALIZED_SUBJECT, g_strdup (item->header.subject));
	//set_SPropValue_proptag(&props[i++], PR_BODY, g_strdup (item->msg.body));
/* 	msgflag = MSGFLAG_UNSENT; */
/* 	set_SPropValue_proptag(&props[i++], PR_MESSAGE_FLAGS, (void *)&msgflag); */

	*value = props;
	return i;
}


int
mapi_message_item_send (MapiItem *item)
{
	printf("%s(%d):%s:REACHED \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	guint64 fid = 0;

	exchange_mapi_create_item (olFolderOutbox, fid, NULL, NULL, mail_build_props, item, NULL, NULL);

	return 0;
}

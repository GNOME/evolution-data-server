/*
 *  Copyright (C) Jean-Baptiste Arnoult 2007.
 *  Copyright (C) Fabien Le-Mentec 2007.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */



/* todos
   . QueryRows attachment should not be done in one shot
   . modify the oc_message_send() to send attach
   . add usernames completion
*/


#include <oc.h>
#include <camel/camel-string-utils.h>
#include <camel/camel-seekable-stream.h>
#include <camel/camel-stream-mem.h>
#include <camel/camel-folder.h>

bool gl_init = FALSE;
/* helper routines
 */

char *mapi_ids_to_uid(mapi_id_t id_folder, mapi_id_t id_message)
{
	char	buffer[64];

	sprintf(buffer, "%llx/%llx", id_folder, id_message);
	return strdup(buffer);
}

char *folder_mapi_ids_to_uid(mapi_id_t id_folder)
{
	char	buffer[64];

	sprintf(buffer, "%llx", id_folder);
	return strdup(buffer);
}


int uid_to_mapi_ids(const char *s, mapi_id_t *id_folder, mapi_id_t *id_message)
{
	int	n;

	if (!s) return -1;
	n = sscanf(s, "%llx/%llx", id_folder, id_message);
	if (n != 2) return -1;

	return 0;
}

int folder_uid_to_mapi_ids(const char *s, mapi_id_t *id_folder)
{
	int	n;

	if (!s) return -1;
	n = sscanf(s, "%llx", id_folder);
	if (n != 1) return -1;

	return 0;
}


/* oc folder functions
 */

int oc_inbox_list_message_ids(char ***ids, int *n_ids,  oc_message_headers_t ***headers_save, char *folder_id)
{
	enum MAPISTATUS		retval;
	TALLOC_CTX		*mem_ctx;
	mapi_object_t		obj_store;
	mapi_object_t		obj_inbox;
	mapi_object_t		obj_table;
	uint64_t		id_inbox;
	struct SPropTagArray	*SPropTagArray;
	struct SRowSet		rowset;
	uint32_t		i;
	uint32_t		index;
	uint32_t		count;
	char			*tmp_id;
	oc_message_headers_t	**headers = NULL;
	struct FILETIME		*delivery_date;
	NTTIME			ntdate;
	long			*flags;
	uint32_t		*importance;
	mapi_id_t		*fid = NULL;
	mapi_id_t		*mid = NULL;
	*ids = 0;
	*n_ids = 0;

	mem_ctx = talloc_init("oc_inbox_list_message_ids");

/* 	oc_thread_connect_lock(); */
/* 	if (m_oc_initialize() == -1) { */
/* 		oc_thread_connect_unlock(); */
/* 		return -1; */
/* 	} */

	/* init objects */
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_inbox);
	mapi_object_init(&obj_table);

	/* session::OpenMsgStore() */
	retval = OpenMsgStore(&obj_store);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	retval = folder_uid_to_mapi_ids(folder_id, &id_inbox);
	if (retval != 0) return (-1);
	/* inbox = store->OpenFolder() */
	retval = OpenFolder(&obj_store, id_inbox, &obj_inbox);
/* 	mapi_errstr("OpenFolder", GetLastError()); */
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	/* table = inbox->GetContentsTable() */
	retval = GetContentsTable(&obj_inbox, &obj_table);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	SPropTagArray = set_SPropTagArray(mem_ctx, 0x9, 
					  PR_FID, 
					  PR_MID,
					  PR_CONVERSATION_TOPIC,
					  PR_SENT_REPRESENTING_NAME,
					  PR_DISPLAY_TO,
					  PR_DISPLAY_CC,
					  PR_MESSAGE_FLAGS,
					  PR_MESSAGE_DELIVERY_TIME,
					  PR_IMPORTANCE);
	retval = SetColumns(&obj_table, SPropTagArray);
	MAPIFreeBuffer(SPropTagArray);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	/* Iterate through messages */
	{

		retval = GetRowCount(&obj_table, &count);
		MAPI_RETVAL_IF(retval, retval, mem_ctx);
		*n_ids = count;
		*ids = malloc(count * sizeof(char*));
		if (headers_save)
			headers = malloc(count * sizeof(*headers));
		index = 0;
		do {
			retval = QueryRows(&obj_table, 0x32, TBL_ADVANCE, &rowset);
			MAPI_RETVAL_IF(retval, retval, mem_ctx);
			for (i = 0; i < rowset.cRows; i++) {
/* 				mapidump_SRow(&(rowset.aRow[i]), "\t"); */
				fid = (mapi_id_t *)find_SPropValue_data(&(rowset.aRow[i]), PR_FID);
				mid = (mapi_id_t *)find_SPropValue_data(&(rowset.aRow[i]), PR_MID);

				if (fid && mid && ((tmp_id = mapi_ids_to_uid(*fid, *mid)) != NULL)) {
					(*ids)[index] = strdup(tmp_id);
					if (headers_save) {
						headers[index] = malloc(sizeof(**headers));
						oc_message_headers_init(headers[index]);
						if (find_SPropValue_data(&(rowset.aRow[i]), PR_CONVERSATION_TOPIC))
							headers[index]->subject = strdup((char*)find_SPropValue_data(&(rowset.aRow[i]), PR_CONVERSATION_TOPIC));
						if (find_SPropValue_data(&(rowset.aRow[i]), PR_SENT_REPRESENTING_NAME))
							headers[index]->from = strdup((char*)find_SPropValue_data(&(rowset.aRow[i]), PR_SENT_REPRESENTING_NAME));
						if (find_SPropValue_data(&(rowset.aRow[i]), PR_DISPLAY_TO))
							headers[index]->to = strdup((char*)find_SPropValue_data(&(rowset.aRow[i]), PR_DISPLAY_TO));
						if (find_SPropValue_data(&(rowset.aRow[i]), PR_DISPLAY_CC))
							headers[index]->cc = strdup((char*)find_SPropValue_data(&(rowset.aRow[i]), PR_DISPLAY_CC));
						flags = (long *)find_SPropValue_data(&(rowset.aRow[i]), PR_MESSAGE_FLAGS);
						delivery_date = (struct FILETIME *)find_SPropValue_data(&(rowset.aRow[i]), PR_MESSAGE_DELIVERY_TIME);

						/* testing */
						importance = (uint32_t *)find_SPropValue_data(&(rowset.aRow[i]), PR_IMPORTANCE);

						if ((*flags & MSGFLAG_READ) != 0)
							headers[index]->flags |= CAMEL_MESSAGE_SEEN;
						if ((*flags & MSGFLAG_HASATTACH) != 0)
							headers[index]->flags |= CAMEL_MESSAGE_ATTACHMENTS;
						
						if (*importance == 2)
							headers[index]->flags |= CAMEL_MESSAGE_FLAGGED;
						if (delivery_date) {
							ntdate = delivery_date->dwHighDateTime;
							ntdate = ntdate << 32;
							ntdate |= delivery_date->dwLowDateTime;
							headers[index]->send = nt_time_to_unix(ntdate);
						}
					}
					index++;
				}
			}
			*n_ids = index;
		} while (rowset.cRows > 0);
		if (headers_save)
			*headers_save = headers;
	}
	
	/* release mapi objects */
	mapi_object_release(&obj_table);
	mapi_object_release(&obj_inbox);
	mapi_object_release(&obj_store);
	
	talloc_free(mem_ctx);
	
	return 0;
}


/* oc message headers */

static void oc_message_headers_reset(oc_message_headers_t *headers)
{
	headers->subject = NULL;
	headers->from = NULL;
	headers->to = NULL;
	headers->cc = NULL;
	headers->bcc = NULL;
	headers->flags = CAMEL_MESSAGE_SECURE|CAMEL_MESSAGE_USER|CAMEL_MESSAGE_FOLDER_FLAGGED;
}


void oc_message_headers_init(oc_message_headers_t *headers)
{
	oc_message_headers_reset(headers);
}



int oc_message_headers_get_by_id(oc_message_headers_t *headers, const char *id)
{
	enum MAPISTATUS			retval;
	mapi_object_t			obj_message;
	mapi_object_t			obj_store;
	mapi_id_t			id_folder;
	mapi_id_t			id_message;
	int				ret;
	int				*flags;
	uint32_t			*importance;
	TALLOC_CTX			*mem_ctx;
	struct SPropTagArray		*proptags;
	struct SPropValue		*vals;
	uint32_t			cn_vals;
	struct FILETIME			*delivery_date;
	NTTIME				ntdate;
	char				*tmp;

	/* init objects */
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_message);
	mem_ctx = talloc_init("os_get_header_by_id");
	oc_message_headers_reset(headers);

	ret = uid_to_mapi_ids(id, &id_folder, &id_message);
	if (ret == -1) return ret;

	retval = OpenMsgStore(&obj_store);
	MAPI_RETVAL_IF(retval, retval, NULL);

	retval = OpenMessage(&obj_store, id_folder, id_message, &obj_message, 0);
	MAPI_RETVAL_IF(retval, retval, NULL);

	proptags = set_SPropTagArray(mem_ctx, 0x7,
				     PR_CONVERSATION_TOPIC,
				     PR_SENT_REPRESENTING_NAME,
				     PR_DISPLAY_TO,
				     PR_DISPLAY_CC,
				     PR_MESSAGE_FLAGS,
				     PR_MESSAGE_DELIVERY_TIME,
				     PR_IMPORTANCE);
	
	retval = GetProps(&obj_message, proptags, &vals, &cn_vals);
	MAPIFreeBuffer(proptags);
/* 	mapi_errstr("GetProps", GetLastError()); */
	if (retval != MAPI_E_SUCCESS) return -1;
	if ((tmp = get_SPropValue(vals, PR_CONVERSATION_TOPIC)) != NULL) {
		headers->subject = strdup(tmp);
		tmp = NULL;
	}
	if ((tmp = get_SPropValue(vals, PR_SENT_REPRESENTING_NAME)) != NULL) {
		headers->from = strdup(tmp);
		tmp = NULL;
	}
	if ((tmp = get_SPropValue(vals, PR_DISPLAY_TO)) != NULL) {
		headers->to = strdup(tmp);
		tmp = NULL;
	}
	if ((tmp = get_SPropValue(vals, PR_DISPLAY_CC)) != NULL) {
		headers->cc = strdup(tmp);
		tmp = NULL;
	}

	flags =  get_SPropValue(vals, PR_MESSAGE_FLAGS);
	delivery_date = get_SPropValue(vals, PR_MESSAGE_DELIVERY_TIME);
	importance = get_SPropValue(vals, PR_IMPORTANCE);

	if (flags && (*flags & MSGFLAG_READ) != 0)
		headers->flags |= CAMEL_MESSAGE_SEEN;
	if (flags && (*flags & MSGFLAG_HASATTACH) != 0)
		headers->flags |= CAMEL_MESSAGE_ATTACHMENTS;

	if (importance && *importance == 2)
		headers->flags |= CAMEL_MESSAGE_FLAGGED;
	
	if (delivery_date) {
		ntdate = delivery_date->dwHighDateTime;
		ntdate = ntdate << 32;
		ntdate |= delivery_date->dwLowDateTime;
		headers->send = nt_time_to_unix(ntdate);
	}

	mapi_object_release(&obj_message);
	mapi_object_release(&obj_store);
	talloc_free(mem_ctx);
	return 0;
}


void oc_message_headers_set_from(oc_message_headers_t *headers, const char *from)
{
	if (headers->from) { 
		free(headers->from);
	}
	headers->from = strdup(from);
}


void oc_message_headers_set_subject(oc_message_headers_t *headers, const char *subject)
{
	if (headers->subject) {
		free(headers->subject);
	}
	headers->subject = strdup(subject);
}


void oc_message_headers_add_recipient(oc_message_headers_t *headers, const char *to)
{
	int	len = 0;

	if (!to)
		return ;
	if (headers->to)
		len = strlen(headers->to);
	headers->to = realloc(headers->to, len + strlen(to) + 2);
	if (len){
		headers->to[len] = ',';
		memcpy(headers->to + len + 1, to, strlen(to));
		headers->to[len + 1 + strlen(to)] = 0;
	}
	else{
		memcpy(headers->to + len, to, strlen(to));
		headers->to[len + strlen(to)] = 0;
	}
	
}


void oc_message_headers_add_recipient_cc(oc_message_headers_t *headers, const char *cc)
{
	int	len = 0;

	if (!cc)
		return ;
	if (headers->cc)
		len = strlen(headers->cc);
	headers->cc = realloc(headers->cc, len + strlen(cc) + 2);
	if (len){
		headers->cc[len] = ',';
		memcpy(headers->cc + len + 1, cc, strlen(cc));
		headers->cc[len + 1 + strlen(cc)] = 0;
	}
	else{
		memcpy(headers->cc + len, cc, strlen(cc));
		headers->cc[len + strlen(cc)] = 0;
	}
	
}


void oc_message_headers_add_recipient_bcc(oc_message_headers_t *headers, const char *bcc)
{
	int	len = 0;

	if (!bcc)
		return ;
	if (headers->bcc)
		len = strlen(headers->bcc);
	headers->bcc = realloc(headers->bcc, len + strlen(bcc) + 2);
	if (len){
		headers->bcc[len] = ',';
		memcpy(headers->bcc + len + 1, bcc, strlen(bcc));
		headers->bcc[len + 1 + strlen(bcc)] = 0;
	}
	else{
		memcpy(headers->bcc + len, bcc, strlen(bcc));
		headers->bcc[len + strlen(bcc)] = 0;
	}
	
}



void oc_message_headers_release(oc_message_headers_t *headers)
{
	if (!headers) return ;
	if (headers->subject) {
		free(headers->subject);
	}
	if (headers->from) {
		free(headers->from);
	}
	if (headers->to){
		free(headers->to);
	}

	if (headers->cc) {
		free(headers->cc);
	}

	if (headers->bcc) {
		free(headers->bcc);
	}
   
	oc_message_headers_reset(headers);
}


/* oc message attach
 */

static void oc_message_attach_reset(oc_message_attach_t *attach)
{
	attach->id = 0;
	attach->filename = 0;
	attach->description = 0;
	attach->sz_content = 0;
	attach->buf_content = camel_stream_mem_new();
}


static void oc_message_attach_init(oc_message_attach_t *attach)
{
	oc_message_attach_reset(attach);
}


static void oc_message_attach_release(oc_message_attach_t *attach)
{
	if (attach->filename) {
		free(attach->filename);
	}
	if (attach->description) {
		free(attach->description);
	}
	if (attach->buf_content) {
		camel_object_unref(attach->buf_content);
	}

	oc_message_attach_reset(attach);
}


/* oc message contents
 */

static void oc_message_contents_reset(oc_message_contents_t *contents)
{
	contents->body = camel_stream_mem_new();
	contents->n_attach = 0;
	contents->l_attach = 0;
}


void oc_message_contents_init(oc_message_contents_t *contents)
{
	oc_message_contents_reset(contents);
}


static int read_attach_stream(TALLOC_CTX *mem_ctx, mapi_object_t *obj_attach, mapi_object_t *obj_stream,
			      uint8_t **buf_data, uint32_t *sz_data)
{
	enum MAPISTATUS status;
	uint32_t	cn_read;
	int		done;
	unsigned char	buf[4096];
	int		off_data;

	/* reset */
	*buf_data = 0;
	*sz_data = 0;
	done = 0;

	/* alloc buffer */
	*sz_data = 0;
	*buf_data = 0;

	/* read attachment */
	while (done == 0){
		status = ReadStream(obj_stream,
				    buf, sizeof(buf),
				    &cn_read);
		if ((status != MAPI_E_SUCCESS) || (cn_read == 0)){
			done = 1;
		}
		else {
			off_data = *sz_data;
			*sz_data += cn_read;
			*buf_data = talloc_realloc_fn(mem_ctx, *buf_data, *sz_data);
			memcpy(*buf_data + off_data, buf, cn_read);
		}
	}
  
	if (*sz_data == 0) return -1;

	return 0;
}

int oc_message_contents_get_by_id(oc_message_contents_t *contents, const char *id)
{
	TALLOC_CTX		*mem_ctx;
	enum MAPISTATUS		retval;
	struct SPropTagArray	*SPropTagArray;	
	mapi_object_t		obj_message;
	mapi_object_t		obj_store;
	mapi_object_t		obj_table;
	mapi_object_t		obj_attach;
	mapi_object_t		obj_body;
	mapi_object_t		obj_stream;
	mapi_id_t		id_folder;
	mapi_id_t		id_message;
	uint32_t		cn_vals;
	struct SPropValue	*vals;
	struct SRowSet		rows_attach;
	int			ret;
	int			i_attach;
	uint32_t		n_attach;
	unsigned char		*buf_content;
	uint32_t		sz_content = 0;
	oc_message_attach_t	*attach;
	size_t			read_size;
	int			tot = 0;

	mem_ctx = talloc_init("oc_message_contents_get_by_id");

	mapi_object_init(&obj_message);
	mapi_object_init(&obj_body);
	oc_message_contents_reset(contents);

	ret = uid_to_mapi_ids(id, &id_folder, &id_message);
	if (ret == -1) return ret;

	mapi_object_init(&obj_store);
	retval = OpenMsgStore(&obj_store);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	retval = OpenMessage(&obj_store, id_folder, id_message, &obj_message, 0);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	mapi_object_init(&obj_stream);
	retval = OpenStream(&obj_message, PR_BODY, 0, &obj_stream);
/* 	mapi_errstr("OpenStream", GetLastError()); */
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	buf_content = talloc_zero_size(mem_ctx, STREAM_SIZE);

	while((ReadStream(&obj_stream, buf_content, STREAM_SIZE, &read_size)) == MAPI_E_SUCCESS){
		tot += read_size;
		if (!read_size) break ;
/* 		mapi_errstr("ReadStream", GetLastError()); */
		camel_stream_write(contents->body, (char *)buf_content, read_size);
		memset(buf_content, 0, STREAM_SIZE);
	}
/* 	mapi_errstr("ReadStream", GetLastError()); */
	talloc_free(buf_content);
	mapi_object_release(&obj_stream);

	/* attach */
	mapi_object_init(&obj_table);
	retval = GetAttachmentTable(&obj_message, &obj_table);
	if (retval == MAPI_E_SUCCESS) {
		SPropTagArray = set_SPropTagArray(mem_ctx, 0x1, PR_ATTACH_NUM);
		retval = SetColumns(&obj_table, SPropTagArray);
		MAPIFreeBuffer(SPropTagArray);
		MAPI_RETVAL_IF(retval, retval, mem_ctx);

		retval = GetRowCount(&obj_table, &n_attach);
		MAPI_RETVAL_IF(retval, retval, mem_ctx);

		retval = QueryRows(&obj_table, n_attach, TBL_ADVANCE, &rows_attach);
		MAPI_RETVAL_IF(retval, retval, mem_ctx);

		for (i_attach = 0; i_attach < rows_attach.cRows; ++i_attach) {
			mapi_object_init(&obj_attach);
			retval = OpenAttach(&obj_message, rows_attach.aRow[i_attach].lpProps[0].value.l, &obj_attach);
			if (retval != MAPI_E_SUCCESS) continue ;

			mapi_object_init(&obj_stream);
			retval = OpenStream(&obj_attach, PR_ATTACH_DATA_BIN, 0,&obj_stream);
			if (retval == MAPI_E_SUCCESS){
				buf_content = talloc_zero_size(mem_ctx, sz_content);
				if (read_attach_stream(mem_ctx, &obj_attach, &obj_stream, &buf_content, &sz_content) != -1) {
					attach = malloc(sizeof(oc_message_attach_t));
					oc_message_attach_init(attach);
					
					attach->id = rows_attach.aRow[i_attach].lpProps[0].value.l;
					attach->sz_content = sz_content;
					camel_stream_write(attach->buf_content, (char *)buf_content, sz_content);

					talloc_free(buf_content);

					SPropTagArray = set_SPropTagArray(mem_ctx, 1, PR_ATTACH_FILENAME);
					retval = GetProps(&obj_attach, SPropTagArray, &vals, &cn_vals);
					MAPIFreeBuffer(SPropTagArray);
					attach->filename = strdup(vals[0].value.lpszA);
					
					attach->description = strdup(attach->filename);
	  
					contents->l_attach = g_list_append(contents->l_attach, (gpointer) attach);
					if (contents->l_attach) {
						contents->n_attach++;
					}
				}

				mapi_object_release(&obj_stream);
			}

			mapi_object_release(&obj_attach);
		}
	}
  
	mapi_object_release(&obj_table);
	mapi_object_release(&obj_message);
	mapi_object_release(&obj_body);
	mapi_object_release(&obj_store);
	
	talloc_free(mem_ctx);
	return 0;
}


void oc_message_contents_release(oc_message_contents_t *contents)
{
	GList			*node;
	oc_message_attach_t	*attach;

	if (contents->body) {
		camel_object_unref(contents->body);
	}
	contents->body = 0;
	if (contents->l_attach){
		for (node = contents->l_attach; node; node = g_list_next(node)) {
			attach = (oc_message_attach_t *)node->data;
			oc_message_attach_release(attach);
			free(attach);
		}
		g_list_free(contents->l_attach);
	}
  
	oc_message_contents_reset(contents);
}


void oc_message_contents_set_body(oc_message_contents_t *contents, CamelStream *body)
{
	contents->body = body;
}


int oc_message_contents_get_attach(oc_message_contents_t *contents, int id, const oc_message_attach_t **attach)
{
	*attach = 0;

	if (contents->n_attach < id)
		return -1;
	
	*attach = (const oc_message_attach_t*)g_list_nth_data(contents->l_attach, id);
	if (*attach == 0) return -1;

	return 0;
}


int oc_message_contents_add_attach(oc_message_contents_t *contents,
				   const char *filename,
				   const char *description,
				   CamelStream *buf_content,
				   int sz_content)
{
	oc_message_attach_t	*attach;

	attach = malloc(sizeof(oc_message_attach_t));
	if (!attach) return -1;

	oc_message_attach_init(attach);
	attach->id = contents->n_attach;
	if (filename) {
		attach->filename = strdup(filename);
	}
	if (description) {
		attach->description = strdup(description);
	}
	attach->sz_content = sz_content;
	attach->buf_content = buf_content;

	contents->l_attach = g_list_append(contents->l_attach, attach);
	if (contents->l_attach == 0) {
		oc_message_attach_release(attach);
		free(attach);
		return -1;
	}

	contents->n_attach++;
	
	return 0;
}

static gboolean set_usernames_RecipientType(uint32_t *index, struct SRowSet *rowset, char **usernames, struct FlagList *flaglist,
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

static char **collapse_recipients(TALLOC_CTX *mem_ctx, char **mapi_to, char **mapi_cc, char **mapi_bcc)
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

static char **get_cmdline_recipients(TALLOC_CTX *mem_ctx, const char *recipients)
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


#define CN_MSG_PROPS 2

int oc_message_send(oc_message_headers_t *headers, oc_message_contents_t *contents)
{
	enum MAPISTATUS			retval;
	TALLOC_CTX			*mem_ctx;
	mapi_object_t			obj_message;
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
 
	mem_ctx = talloc_init("oc_message_send");

	/* init objects */
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_stream);
	mapi_object_init(&obj_outbox);
	mapi_object_init(&obj_message);
	mapi_object_init(&obj_body);

	/* session::OpenMsgStore() */
	retval = OpenMsgStore(&obj_store);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	/* id_outbox = store->GeOutboxFolder() */
	retval = GetOutboxFolder(&obj_store, &id_outbox);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	/* outbox = store->OpenFolder(id_outbox) */
	retval = OpenFolder(&obj_store, id_outbox, &obj_outbox);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

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
	usernames_to = get_cmdline_recipients(mem_ctx, headers->to);
	usernames_cc = get_cmdline_recipients(mem_ctx, headers->cc);
	usernames_bcc = get_cmdline_recipients(mem_ctx, headers->bcc);

	usernames = collapse_recipients(mem_ctx, usernames_to, usernames_cc, usernames_bcc);

	retval = ResolveNames((const char **)usernames, SPropTagArray, &SRowSet, &flaglist, 0);
/* 	mapi_errstr("ResolveNames", GetLastError()); */
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	set_usernames_RecipientType(&index, SRowSet, usernames_to,  flaglist, MAPI_TO);
	set_usernames_RecipientType(&index, SRowSet, usernames_cc,  flaglist, MAPI_CC);
	set_usernames_RecipientType(&index, SRowSet, usernames_bcc, flaglist, MAPI_BCC);

	/* FIXME no saving mail */
	if (index == 0) {
		printf("no valid recipients set\n");
		return -1;
	}

	retval = ModifyRecipients(&obj_message, SRowSet);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	SPropValue.ulPropTag = PR_SEND_INTERNET_ENCODING;
	SPropValue.value.l = 0;
	SRowSet_propcpy(mem_ctx, SRowSet, SPropValue);


/* 	mapidump_Recipients((const char **)usernames_to, SRowSet, flaglist); */
	retval = ModifyRecipients(&obj_message, SRowSet);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	/* message->SetProps() */
	msgflag = MSGFLAG_UNSENT;
	set_SPropValue_proptag(&props[0], PR_SUBJECT, (void *)headers->subject);
	set_SPropValue_proptag(&props[1], PR_MESSAGE_FLAGS, (void *)&msgflag);

	retval = SetProps(&obj_message, props, CN_MSG_PROPS);
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	/* Create a stream and write body */
	retval = OpenStream(&obj_message, PR_BODY, 2, &obj_body);
/* 	mapi_errstr("OpenStream", GetLastError()); */
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	/* WriteStream body */
	camel_seekable_stream_seek((CamelSeekableStream *)contents->body, 0, CAMEL_STREAM_SET);
	while((read_size = camel_stream_read(contents->body, (char *)buf, STREAM_SIZE))){
		if (read_size == -1)
			return (-1);
		blob.length = read_size;
		blob.data = talloc_size(mem_ctx, read_size);
		memcpy(blob.data, buf, read_size);
		errno = 0;
		retval = WriteStream(&obj_body, &blob, &read_size);
/* 		mapi_errstr("WriteStream", GetLastError()); */
		talloc_free(blob.data);
	}

	mapi_object_release(&obj_stream);

	/* Attachment related operations */

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

	/* message->SubmitMessage() */
	retval = SubmitMessage(&obj_message);
/* 	mapi_errstr("SubmitMessage", GetLastError()); */
	MAPI_RETVAL_IF(retval, retval, mem_ctx);

	/* objects->Release() */
	mapi_object_release(&obj_body);
	mapi_object_release(&obj_message);
	mapi_object_release(&obj_outbox);
	mapi_object_release(&obj_store);
	talloc_free(mem_ctx);
	
	return 0;
}


/* oc global functions
 */

int oc_initialize(const char *profdb, const char *profname)
{
	enum MAPISTATUS	retval;

	retval = MAPIInitialize(profdb);
/* 	mapi_errstr("MAPIInitialize", GetLastError()); */
	MAPI_RETVAL_IF(retval, retval, NULL);

	retval = MapiLogonEx(&global_mapi_ctx->session, profname, NULL);
/* 	mapi_errstr("MapiLogonEx", GetLastError()); */
	MAPI_RETVAL_IF(retval, retval, NULL);

	return 0;
}


int oc_uninitialize(void)
{
	MAPIUninitialize();
	return 0;
}



/* delete mail */
int oc_delete_mail_by_uid(char *id)
{
	enum MAPISTATUS	retval;
	mapi_object_t	obj_store;
	mapi_object_t	obj_inbox;
	mapi_id_t	id_folder;
	mapi_id_t	id_message;
	int		ret;

	/* init mapi object */
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_inbox);
	
	/* get folder id && message id */
	ret = uid_to_mapi_ids(id, &id_folder, &id_message);
	if (ret == -1) return ret;
	
	/* open store */
	retval = OpenMsgStore(&obj_store);
	MAPI_RETVAL_IF(retval, retval, NULL);
	
	/* inbox = store->OpenFolder() */
	retval = OpenFolder(&obj_store, id_folder, &obj_inbox);
/* 	mapi_errstr("OpenFolder", GetLastError()); */
	MAPI_RETVAL_IF(retval, retval, NULL);

	retval = DeleteMessage(&obj_inbox, &id_message, 1);
	MAPI_RETVAL_IF(retval, retval, NULL);

	mapi_object_release(&obj_store);
	mapi_object_release(&obj_inbox);

	return (0);
}

int	oc_message_update_flags_by_id(char *id, int flags)
{
	enum MAPISTATUS			retval;
	mapi_object_t			obj_store;
	mapi_object_t			obj_inbox;
	mapi_object_t			obj_table;
	mapi_object_t			obj_message;
	mapi_id_t			id_folder;
	mapi_id_t			id_message;
	struct mapi_SPropValue_array	properties_array;
	int				ret;
	uint32_t			*msg_flags;
	uint32_t			*importance;

	/* init mapi object */
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_inbox);
	mapi_object_init(&obj_table);
	mapi_object_init(&obj_message);

	/* get folder id && message id */
	ret = uid_to_mapi_ids(id, &id_folder, &id_message);
	if (ret == -1) return ret;

	/* open store */
	retval = OpenMsgStore(&obj_store);
	MAPI_RETVAL_IF(retval, retval, NULL);

	/* inbox = store->OpenFolder() */
	retval = OpenFolder(&obj_store, id_folder, &obj_inbox);
	MAPI_RETVAL_IF(retval, retval, NULL);

	retval = OpenMessage(&obj_store, id_folder, id_message, &obj_message, 0);
	MAPI_RETVAL_IF(retval, retval, NULL);

	retval = GetPropsAll(&obj_message, &properties_array);
	MAPI_RETVAL_IF(retval, retval, NULL);

	msg_flags = (uint32_t *)find_mapi_SPropValue_data(&properties_array, PR_MESSAGE_FLAGS);
	importance = (uint32_t *)find_mapi_SPropValue_data(&properties_array, PR_IMPORTANCE);

	/**
	 * If none of the flags is set:
	 * - If MSGREAD_FLAG is already set, do nothing
	 * - If MSGREAD_FLAG is not set, set it immediatly
	 *
	 */
	
	{
		uint8_t				flag = 0;
		struct SPropValue		props[1];
		/* 
		 * If message priority has been set to important and
		 * was not on the server 
		 */
		if ((flags & CAMEL_MESSAGE_FLAGGED)) {
			*importance = 2;
			set_SPropValue_proptag(&props[0], PR_IMPORTANCE, (void *)importance);
			retval = SetProps(&obj_message, props, 1);
			MAPI_RETVAL_IF(retval, retval, NULL);			
			retval = SaveChangesMessage(&obj_inbox, &obj_message);
			MAPI_RETVAL_IF(retval, retval, NULL);
		} else {
			*importance = 1;
			set_SPropValue_proptag(&props[0], PR_IMPORTANCE, (void *)importance);
			retval = SetProps(&obj_message, props, 1);
			MAPI_RETVAL_IF(retval, retval, NULL);
			retval = SaveChangesMessage(&obj_inbox, &obj_message);
			MAPI_RETVAL_IF(retval, retval, NULL);
		}

		/* If the message was unseen on the server and marked
		 * as read in evolution 
		 */
		if ((flags & CAMEL_MESSAGE_SEEN) && (*msg_flags | MSGFLAG_READ)) {
			retval = SetReadFlags(&obj_inbox, &obj_message, flag);
			MAPI_RETVAL_IF(retval, retval, NULL);
		} else if ((flags | CAMEL_MESSAGE_SEEN) && (*msg_flags & MSGFLAG_READ)) {
			flag = CLEAR_READ_FLAG;
			retval = SetReadFlags(&obj_inbox, &obj_message, flag);
			MAPI_RETVAL_IF(retval, retval, NULL);
		}
	}
	
	mapi_object_release(&obj_message);
	mapi_object_release(&obj_table);
	mapi_object_release(&obj_inbox);
	mapi_object_release(&obj_store);

	return 0;
}

/*
** update the flags (READ/UNREAD, FLAGGED/NON_FLAGGED)
** on server for n message id
*/
int	oc_message_update_flags_by_n_id(int n_id, char **id, int *flags)
{
	enum MAPISTATUS			retval;
	mapi_object_t			obj_store;
	mapi_object_t			obj_inbox;
	mapi_object_t			obj_table;
	mapi_object_t			obj_message;
	mapi_id_t			id_folder;
	mapi_id_t			id_folder_new;
	mapi_id_t			id_message;
	struct mapi_SPropValue_array	properties_array;
	int				ret;
	uint32_t			*msg_flags;
	uint32_t			*importance;
	int				i_id = 0;

	/* init mapi object */
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_inbox);
	mapi_object_init(&obj_table);

	/* open store */
	retval = OpenMsgStore(&obj_store);
	MAPI_RETVAL_IF(retval, retval, NULL);

	/* inbox = store->OpenFolder() */
	do {
		/* get folder id && message id */
		do {
			ret = uid_to_mapi_ids(id[i_id], &id_folder, &id_message);
			if (ret == -1)
				i_id++;
		} while (ret == -1);

		retval = OpenFolder(&obj_store, id_folder, &obj_inbox);
		MAPI_RETVAL_IF(retval, retval, NULL);
		do {
			mapi_object_init(&obj_message);
			retval = OpenMessage(&obj_store, id_folder, id_message, &obj_message, 0);
			MAPI_RETVAL_IF(retval, retval, NULL);
			id_folder_new = id_folder;
			retval = GetPropsAll(&obj_message, &properties_array);
			MAPI_RETVAL_IF(retval, retval, NULL);
			
			msg_flags = (uint32_t *)find_mapi_SPropValue_data(&properties_array, PR_MESSAGE_FLAGS);
			importance = (uint32_t *)find_mapi_SPropValue_data(&properties_array, PR_IMPORTANCE);
			
			/**
			 * If none of the flags is set:
			 * - If MSGREAD_FLAG is already set, do nothing
			 * - If MSGREAD_FLAG is not set, set it immediatly
			 *
			 */
			
			{
				uint8_t				flag = 0;
				struct SPropValue		props[1];
				/* 
				 * If message priority has been set to important and
				 * was not on the server 
				 */
				if ((flags[i_id] & CAMEL_MESSAGE_FLAGGED)) {
					*importance = 2;
					set_SPropValue_proptag(&props[0], PR_IMPORTANCE, (void *)importance);
					retval = SetProps(&obj_message, props, 1);
					MAPI_RETVAL_IF(retval, retval, NULL);			
					retval = SaveChangesMessage(&obj_inbox, &obj_message);
					MAPI_RETVAL_IF(retval, retval, NULL);
				} else {
					*importance = 1;
					set_SPropValue_proptag(&props[0], PR_IMPORTANCE, (void *)importance);
					retval = SetProps(&obj_message, props, 1);
					MAPI_RETVAL_IF(retval, retval, NULL);
					retval = SaveChangesMessage(&obj_inbox, &obj_message);
					MAPI_RETVAL_IF(retval, retval, NULL);
				}
				
				/* If the message was unseen on the server and marked
				 * as read in evolution 
				 */
				if ((flags[i_id] & CAMEL_MESSAGE_SEEN) && (*msg_flags | MSGFLAG_READ)) {
					retval = SetReadFlags(&obj_inbox, &obj_message, flag);
					MAPI_RETVAL_IF(retval, retval, NULL);
				} else if ((flags[i_id] | CAMEL_MESSAGE_SEEN) && (*msg_flags & MSGFLAG_READ)) {
					flag = CLEAR_READ_FLAG;
					retval = SetReadFlags(&obj_inbox, &obj_message, flag);
					MAPI_RETVAL_IF(retval, retval, NULL);
				}
			}
			i_id++;
			mapi_object_release(&obj_message);
/* 			/\* get folder id && message id *\/ */
/* 			ret = uid_to_mapi_ids(id[i_id], &id_folder_new, &id_message); */
			/* get folder id && message id */
			if (i_id < n_id) {
				do {
					ret = uid_to_mapi_ids(id[i_id], &id_folder_new, &id_message);
					if (ret == -1)
						i_id++;
				} while (ret == -1 && i_id < n_id);
			}
		} while (i_id < n_id && id_folder_new == id_folder);
	} while (i_id < n_id);
	mapi_object_release(&obj_table);
	mapi_object_release(&obj_inbox);
	mapi_object_release(&obj_store);
	
	return 0;
}

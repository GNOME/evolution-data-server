/*
 *  Copyright (C) Jean-Baptiste Arnoult 2007.
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

#include <oc.h>
#include "camel-mapi-folder.h"
#include <camel/camel-offline-folder.h>
#include <camel/camel-folder.h>
#include "camel-mapi-store.h"
#include <time.h>
#include <camel/camel-mime-part.h>
#include <camel/camel-mime-utils.h>
#include <camel/camel-string-utils.h>
#include <camel/camel-object.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-data-wrapper.h>
#include <camel/camel-multipart.h>
#include <camel/camel-private.h>
#include <string.h>
#include <camel/camel-stream-buffer.h>
#include <libmapi/libmapi.h>


 #include <pthread.h>
 #define DEBUG_FN( ) printf("----%u %s\n", (unsigned int)pthread_self(), __FUNCTION__);

 static CamelFolderClass *parent_class = NULL;

static void		openchange_refresh_info (CamelFolder *, CamelException *);
static void		openchange_expunge (CamelFolder *, CamelException *);
static void		openchange_sync (CamelFolder *, gboolean, CamelException *);
static CamelMimeMessage	*openchange_get_message (CamelFolder *, const char *, 
						 CamelException *);
static void		openchange_append_message (CamelFolder *, CamelMimeMessage *,
						   const CamelMessageInfo *, char **, 
						   CamelException *);
static void		openchange_transfer_messages_to (CamelFolder *, GPtrArray *, 
							 CamelFolder *,
							 GPtrArray **, gboolean, 
							 CamelException *);
static GPtrArray	*openchange_search_by_expression (CamelFolder *, const char *, CamelException *);
CamelFolder		*camel_openchange_folder_new(CamelStore *, const char *, guint32, CamelException *);
gboolean		openchange_set_message_flags(CamelFolder *, const char *, guint32, guint32);


CamelMessageContentInfo *get_content(CamelMessageInfoBase *mi)
{
	return (NULL);
}

CamelMessageInfo *openchange_get_message_info(CamelFolder *folder, const char *uid)
{ 
	CamelMessageInfo	*msg = NULL;
	CamelMessageInfoBase	*mi = (CamelMessageInfoBase *)msg ;
	int			status = 0;
	oc_message_headers_t	headers;

	if (folder->summary) {
		oc_thread_fs_lock(((CamelOpenchangeFolder *)folder)->oc_thread);
		msg = camel_folder_summary_uid(folder->summary, uid);
		oc_thread_fs_unlock(((CamelOpenchangeFolder *)folder)->oc_thread);
	}
	if (msg != NULL) {
		mi = (CamelMessageInfoBase *)msg ;
		return (msg);
	}
	msg = camel_message_info_new(folder->summary);
	mi = (CamelMessageInfoBase *)msg ;
	oc_message_headers_init(&headers);
	oc_thread_connect_lock();
	status = oc_message_headers_get_by_id(&headers, uid);
	oc_thread_connect_unlock();

	if (headers.subject) mi->subject = (char *)camel_pstring_strdup(headers.subject);
	if (headers.from) mi->from = (char *)camel_pstring_strdup(headers.from);
	if (headers.to) mi->to = (char *)camel_pstring_strdup(headers.to);
	if (headers.cc) mi->cc = (char *)camel_pstring_strdup(headers.cc);
	mi->flags = headers.flags;


	mi->user_flags = NULL;
	mi->user_tags = NULL;
	mi->date_received = 0;
	mi->date_sent = headers.send;
	mi->content = NULL;
	mi->summary = folder->summary;
	if (uid) mi->uid = g_strdup(uid);
	oc_message_headers_release(&headers);
	return (msg);
}


static void camel_openchange_folder_class_init(CamelFolderClass *klass)
{
	CamelFolderClass *folder_class = (CamelFolderClass *) klass;

	parent_class = (CamelFolderClass *) camel_type_get_global_classfuncs (CAMEL_OFFLINE_FOLDER_TYPE);

	folder_class->sync = openchange_sync;
	folder_class->refresh_info = openchange_refresh_info;
	folder_class->expunge = openchange_expunge;
	folder_class->get_message = openchange_get_message;
	folder_class->get_message_info = openchange_get_message_info;
	folder_class->append_message = openchange_append_message;
	folder_class->transfer_messages_to = openchange_transfer_messages_to;
	folder_class->search_by_expression = openchange_search_by_expression;
	folder_class->set_message_flags = openchange_set_message_flags;
 }

static void camel_openchange_folder_init(CamelOpenchangeFolder *object, gpointer klass)
{
	CamelOpenchangeFolder *folders = (CamelOpenchangeFolder *) object;

	((CamelFolder *) folders)->folder_flags |= CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY |CAMEL_FOLDER_HAS_SEARCH_CAPABILITY;
	folders->utf7_name = NULL;
	folders->cachedir = NULL;
	folders->journal = NULL;
	folders->search = NULL;
	folders->folder_uid = NULL;
	oc_thread_initialize(&(folders->oc_thread), (CamelFolder*)folders);
}

static void camel_openchange_folder_finalize(CamelObject * object)
{
	oc_thread_fs_kill_all_thread((((CamelOpenchangeFolder *)object)->oc_thread));
}

CamelType camel_openchange_folder_get_type(void)
{
	static CamelType camel_openchange_folder_type = CAMEL_INVALID_TYPE;

	if (camel_openchange_folder_type == CAMEL_INVALID_TYPE) {
		camel_openchange_folder_type = camel_type_register(camel_folder_get_type (),
								   "CamelOpenchangeFolder",
								   sizeof(CamelOpenchangeFolder),
								   sizeof(CamelOfflineFolderClass),
								   (CamelObjectClassInitFunc) camel_openchange_folder_class_init,
								   NULL,
								   (CamelObjectInitFunc) camel_openchange_folder_init,
								   (CamelObjectFinalizeFunc) camel_openchange_folder_finalize);
	}
	return camel_openchange_folder_type;
}

/*
** this function create the summary on the folder creation
** NO return  value
** if BUG, this function is return but this has not consequence 
** in evolution whithout the message list
*/
void	openchange_construct_summary(CamelFolder *folder, CamelException *ex, char *file_name)
{
	char			*path;
	int			retval;
	int			i_id = 0;
	char			**uid = NULL;
	int			n_id = 0;
	CamelMessageInfoBase	*info;
	oc_message_headers_t	**headers = NULL;

	/* creating summary if none */
	oc_thread_fs_lock(((CamelOpenchangeFolder*)folder)->oc_thread);
	if (!folder->summary){
		
		path = g_strdup_printf("%s/%s/%s", getenv("HOME"), PATH_FOLDER, file_name);
		folder->summary = camel_folder_summary_new(folder);
		camel_folder_summary_set_filename(folder->summary, path);
	}
	/* try to load summay, return if ok */
	retval = camel_folder_summary_load(folder->summary);
	oc_thread_fs_unlock(((CamelOpenchangeFolder*)folder)->oc_thread);
	if (retval == -1) return ;
	((CamelOpenchangeFolder *)folder)->modified = NULL;
	((CamelOpenchangeFolder *)folder)->modified_key = NULL;
	((CamelOpenchangeFolder *)folder)->n_modified = 0;
	/* lock all mutex and kill all thread */
	return ;
	oc_thread_fs_kill_all_thread(((CamelOpenchangeFolder *)folder)->oc_thread);
	oc_thread_connect_lock();
	oc_thread_fs_lock(((CamelOpenchangeFolder *)folder)->oc_thread);
	oc_thread_i_lock(((CamelOpenchangeFolder *)folder)->oc_thread);
	m_oc_initialize();
	retval = oc_inbox_list_message_ids(&uid, &n_id, &headers, ((CamelOpenchangeFolder *)folder)->folder_id);
	if (retval == -1) {
		printf("oc_list_message_ids : ERROR\n");
		goto end;
	}
	for (i_id = 0; i_id < n_id; i_id++) {
		if (folder->summary && uid[i_id] && ((info = (CamelMessageInfoBase *)camel_folder_summary_uid(folder->summary, uid[i_id])) != NULL)) {
			info->flags = headers[i_id]->flags;
			camel_folder_summary_touch(folder->summary);
		}
		oc_message_headers_release(headers[i_id]);
		free(headers[i_id]);
		free(uid[i_id]);
	}
	free(uid);
	/* save the summary */
/* 	camel_folder_summary_save(folder->summary); */
end:
	/* unlock all muttex */
	oc_thread_connect_unlock();
	oc_thread_fs_unlock(((CamelOpenchangeFolder *)folder)->oc_thread);
	oc_thread_i_unlock(((CamelOpenchangeFolder *)folder)->oc_thread);
}

CamelFolder *camel_openchange_folder_new(CamelStore *parent_store, const char *full_name, guint32 flags, CamelException *ex)
{
	CamelFolder	*folder;
	char		*name;
	CamelOpenchangeFolderInfo *fi;

	fi = (CamelOpenchangeFolderInfo *)((CamelOpenchangeStore *)parent_store)->fi;
	while (fi && strcmp(((CamelFolderInfo *)fi)->full_name, full_name) != 0){
		fi = (CamelOpenchangeFolderInfo *)((CamelFolderInfo *)fi)->next;
	}
	if (!fi) return NULL;
	name = g_strdup(full_name);
	folder = (CamelFolder *)camel_object_new(camel_openchange_folder_get_type());
	((CamelOpenchangeFolder *)folder)->folder_id = fi->fid;
	camel_folder_construct(folder, parent_store, camel_url_encode((full_name), NULL), name);
	openchange_construct_summary(folder, ex, fi->file_name);
	folder->parent_store = parent_store;
	return (folder);
}


void	updating_server_from_summary(gpointer data, gpointer user_data)
{
	char			*uid = (char*)data;
	CamelFolder		*folder = (CamelFolder *)user_data;
	CamelMessageInfoBase	*info;
	
	info = (CamelMessageInfoBase *)camel_folder_summary_uid(folder->summary, uid);
	if (info){
		oc_message_update_flags_by_id(uid, info->flags);
		if ((info->flags & CAMEL_MESSAGE_DELETED) == CAMEL_MESSAGE_DELETED){
			oc_delete_mail_by_uid(uid);
			camel_folder_summary_remove_uid(folder->summary, uid);
		}
	}	
}

static void openchange_sync(CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelMessageInfoBase	*info = NULL;
	int			i_id = 0;
	char			**uid = NULL;
	int			n_id = 0;
	int			retval;
	int			n_msg;
	int			i_msg;
	int			n_modif;
	int			i_modif;
	oc_message_headers_t	**headers = NULL;
	CamelOpenchangeFolder	*oc_folder = (CamelOpenchangeFolder *) folder;
	int			update;
	char			*key;
	char			**uid_list = NULL;
	int			*flags = NULL;
	int			i_list;
	CamelFolderChangeInfo *changes;

	oc_thread_fs_kill_all_thread(((CamelOpenchangeFolder *)folder)->oc_thread);
	oc_thread_connect_lock();
	oc_thread_fs_lock(((CamelOpenchangeFolder *)folder)->oc_thread);
	oc_thread_i_lock(((CamelOpenchangeFolder *)folder)->oc_thread);
	/* updating our flags on server */
	n_modif = oc_folder->n_modified;
	if (oc_folder->modified && n_modif > 0) {
		uid_list = malloc(n_modif * sizeof(*uid_list));
		flags = malloc(n_modif * sizeof(*flags));
		memset(uid_list, 0, n_modif * sizeof(*uid_list));
		memset(flags, 0, n_modif * sizeof(*flags));
		for (i_modif = 0, update = 1, i_list = 0; i_modif < n_modif; i_modif++, info = NULL) {
			key = (char *)g_ptr_array_index(oc_folder->modified_key, i_modif);
			if (key)
				info = (CamelMessageInfoBase *)camel_folder_summary_uid(folder->summary, key);
			if (info) {
				if ((info->flags & CAMEL_MESSAGE_DELETED) == CAMEL_MESSAGE_DELETED){
					oc_delete_mail_by_uid(info->uid);
					camel_folder_summary_remove_uid(folder->summary, info->uid);
					changes = camel_folder_change_info_new();
					if (info->uid) {
						camel_folder_change_info_remove_uid(changes, info->uid);
						camel_object_trigger_event(folder, "folder_changed", changes);
					}
					camel_folder_change_info_free(changes);
				}
				else {
					uid_list[i_list] = key;
					flags[i_list] = info->flags;
					i_list++;
				}
			}
		}
		if (i_list)
			oc_message_update_flags_by_n_id(i_list, uid_list, flags);
		g_ptr_array_free(oc_folder->modified_key, 0);
		g_hash_table_destroy(oc_folder->modified);
		oc_folder->modified = NULL;
		oc_folder->modified_key = NULL;
	}
	oc_folder->n_modified = 0;
	n_msg = camel_folder_summary_count(folder->summary);
	/* update the deleted mail from server to eplugin */
	for (i_msg = 0; i_msg < n_msg; i_msg++) {
		if ((info = (CamelMessageInfoBase *)camel_folder_summary_index(folder->summary, i_msg)) == NULL)
			break ;
		for (i_id = 0; i_id < n_id; i_id++) {
			if (uid[i_id] && info->uid && strcmp(uid[i_id], info->uid) == 0)
				break ;
		}
		if (i_id >= n_id)
			camel_folder_summary_remove_uid(folder->summary, info->uid);
	}
	/* update flags(eplugin->server) */
	retval = oc_inbox_list_message_ids(&uid, &n_id, &headers, ((CamelOpenchangeFolder *)folder)->folder_id);
	if (retval == -1)
		printf("oc_list_message_ids : ERROR\n");
	
	for (i_id = 0; i_id < n_id; i_id++) {
		if (folder->summary && uid[i_id] && ((info = (CamelMessageInfoBase *)camel_folder_summary_uid(folder->summary, uid[i_id])) != NULL)) {
			info->flags = headers[i_id]->flags;
			camel_folder_summary_touch(folder->summary);
		}
		oc_message_headers_release(headers[i_id]);
		free(headers[i_id]);
		free(uid[i_id]);
	}
	free(uid);
	camel_folder_summary_save(folder->summary);
	oc_thread_connect_unlock();
	oc_thread_fs_unlock(((CamelOpenchangeFolder *)folder)->oc_thread);
	oc_thread_i_unlock(((CamelOpenchangeFolder *)folder)->oc_thread);
}

static void openchange_expunge(CamelFolder *folder, CamelException *ex)
{
	openchange_sync(folder, TRUE, ex);
}

/*
** this function refresh the summary
*/
static void openchange_refresh_info(CamelFolder *folder, CamelException *ex)
{
	openchange_thread_t	*th = ((CamelOpenchangeFolder*)folder)->oc_thread;

	/* creating a summary if none */
	if (!folder->summary){
		oc_thread_fs_lock(((CamelOpenchangeFolder*)folder)->oc_thread);
		folder->summary = camel_folder_summary_new(folder);
		oc_thread_fs_unlock(((CamelOpenchangeFolder*)folder)->oc_thread);
	}
	if (oc_thread_fs_add_if_none(th) == -1)
		printf("Error while listing the mail\n");
}

/*
** this function return a message from Exchange sever
*/
static CamelMimeMessage *openchange_get_message(CamelFolder *folder, const char *uid, CamelException *ex)
{
	char				*text;
	CamelMessageInfoBase		*header;
	CamelMultipart			*mp = camel_multipart_new();
	CamelMimeMessage		*msg = camel_mime_message_new();
	CamelMimePart			*part;
	CamelDataWrapper		*dw;
	CamelStream			*cache, *stream = NULL;
	CamelInternetAddress		*addr;
	const oc_message_attach_t	*attach = NULL;
	oc_message_contents_t		content;
	char				buf[STREAM_SIZE];
	char				*tmp;
	char				flag = 0;
	char				*p_id;
	int				status;
	int				sz_r;
	int				len = 0;
	int				i_attach;
	
	oc_thread_fs_lock(((CamelOpenchangeFolder *)folder)->oc_thread);
	header = (CamelMessageInfoBase *)camel_folder_summary_uid(folder->summary, uid);
	oc_thread_fs_unlock(((CamelOpenchangeFolder *)folder)->oc_thread);
	if (!header) header = (CamelMessageInfoBase *)openchange_get_message_info(folder, uid);
	if (!header) return (NULL);
	
	if (header->uid) camel_mime_message_set_message_id(msg, header->uid);
	if (header->subject) camel_mime_message_set_subject(msg, header->subject);
	
	camel_mime_message_set_date(msg, header->date_sent, 0);
/* 	camel_mime_message_set_date(msg, (~0), 0); */
	if (header->from) {
		/* add reply to */
		addr = camel_internet_address_new();
		camel_internet_address_add(addr, header->from, header->from);
		camel_mime_message_set_reply_to(msg,addr);
		
		/* add from */
		addr = camel_internet_address_new();
		camel_internet_address_add(addr, header->from, header->from);
		camel_mime_message_set_from(msg,addr);
	}

	/* set recipient */
        /* modifing to */
	{
		char *tmp_addr = NULL;
		int index, len;
		
		addr = camel_internet_address_new();
		for (index = 0; header->to[index]; index += len){
			if (header->to[index] == ';')
				index++;
			for (len = 0; header->to[index + len] &&
				     header->to[index + len] != ';'; len++)
				;
			tmp_addr = malloc(/* tmp_addr, */ len + 1);
			memcpy(tmp_addr, header->to + index, len);
			tmp_addr[len] = 0;
			if (len) camel_internet_address_add(addr, tmp_addr, tmp_addr);
		}
		if (index != 0)
			camel_mime_message_set_recipients(msg, "To", addr);
	}
        /* modifing cc */
	{
		char *tmp_addr = NULL;
		int index, len;
		
		addr = camel_internet_address_new();
		for (index = 0; header->cc[index]; index += len){
			if (header->cc[index] == ';')
				index++;
			for (len = 0; header->cc[index + len] &&
				     header->cc[index + len] != ';'; len++)
				;
			tmp_addr = malloc(/* tmp_addr, */ len + 1);
			memcpy(tmp_addr, header->cc + index, len);
			tmp_addr[len] = 0;
			if (len) camel_internet_address_add(addr, tmp_addr, tmp_addr);
		}
		if (index != 0)
			camel_mime_message_set_recipients(msg, "Cc", addr);
	}
	
	oc_message_contents_init(&content);
	oc_thread_connect_lock();
	status = oc_message_contents_get_by_id(&content, uid);
	oc_thread_connect_unlock();
	if (status == -1) return (msg);
	
	if ((content.body && content.n_attach != 0) || content.n_attach > 1) {
		camel_data_wrapper_set_mime_type(CAMEL_DATA_WRAPPER(mp), "multipart/parallel");
		camel_multipart_set_boundary(mp, NULL);
		camel_multipart_set_preface(mp, "This is a message in MIME format.  Get with the decade\n");
		camel_multipart_set_postface(mp, "\nSome useless after-message text you should never see\n\n");
		flag = 1;
	}
	/* adding body */
	if (content.body) {
		part = camel_mime_part_new();
		text = NULL;
		dw = camel_data_wrapper_new();
		memset(buf, 0, STREAM_SIZE);
		camel_seekable_stream_seek((CamelSeekableStream *)content.body, 0, CAMEL_STREAM_SET);
		len = 0;
		while ((sz_r = camel_stream_read(content.body, buf, STREAM_SIZE)) > 0) {
			tmp = malloc(strlen(buf) + len + 1);
			memset(tmp, 0, strlen(buf) + len + 1);
			if (len) strncpy(tmp, text, len);
			if (strlen(buf)) strncpy(tmp + len, buf, strlen(buf));
			if (text) free(text);
			text = tmp;
			len += strlen(buf);
			memset(buf, 0, STREAM_SIZE);
		}
		if (!text)
			text = " ";
		camel_mime_part_set_content(part, text, strlen(text), "text/plain");
		p_id = g_strdup_printf("%d", content.n_attach);
		camel_mime_part_set_content_id(part, p_id);
		g_free(p_id);
		
		camel_multipart_add_part(mp, part);
		camel_object_unref(dw);
		camel_object_unref(part);
	}
	/* adding attach */
	for (i_attach = 0; i_attach < content.n_attach; ++i_attach) {
		oc_thread_connect_lock();
		status = oc_message_contents_get_attach(&content, i_attach, &attach);
		oc_thread_connect_unlock();
		if (status == -1){
			printf("error while getting attach\n");
			break ;
		}
		
		part = camel_mime_part_new();
		dw = camel_data_wrapper_new();
		camel_data_wrapper_construct_from_stream(dw, attach->buf_content);
		camel_medium_set_content_object (CAMEL_MEDIUM(part), dw);
		if (attach->filename) camel_mime_part_set_filename(part, attach->filename);
		if (attach->description) camel_mime_part_set_description (part, attach->description);
		
		p_id = g_strdup_printf("%d", i_attach);
		camel_mime_part_set_content_id(part, p_id);
		g_free(p_id);
		
		camel_multipart_add_part(mp, part);
		camel_object_unref(dw);
		camel_object_unref(part);
	}
	/* set the message content */
	camel_medium_set_content_object(CAMEL_MEDIUM(msg), CAMEL_DATA_WRAPPER(mp));
	if (((CamelOpenchangeFolder *)folder)->cache && (cache = camel_data_cache_add (((CamelOpenchangeFolder *)folder)->cache, "cache", uid, NULL))) {
		if (camel_stream_write_to_stream (stream, cache) == -1 || camel_stream_flush (cache) == -1) {
			camel_data_cache_remove (((CamelOpenchangeFolder *)folder)->cache, "cache", uid, NULL);
		}
		camel_object_unref (cache);
	}
	
	oc_message_contents_release(&content);
	return (msg);
}

static void openchange_append_message(CamelFolder *folder, CamelMimeMessage *message,
				      const CamelMessageInfo *info, char **appended_uid, CamelException *ex)
{
	DEBUG_FN();
}

static void openchange_transfer_messages_to(CamelFolder *src, GPtrArray *uids, CamelFolder *dest,
					    GPtrArray **transferred_uids, gboolean move, CamelException *ex)
{
	DEBUG_FN();
}

static GPtrArray *openchange_search_by_expression (CamelFolder *folder, const char *expr, CamelException *ex)
{
	GPtrArray	*matches = NULL;
	char		**uids = NULL;
	int		n_uids;
	int		i_uid;
	int		status;

	oc_thread_connect_lock();
	/* WARNING : NULL*/
	status = oc_inbox_list_message_ids(&uids, &n_uids, NULL, ((CamelOpenchangeFolder *)folder)->folder_id);
	oc_thread_connect_unlock();
	if (status == -1) return NULL;
	
	matches = g_ptr_array_new();
	for (i_uid = 0; i_uid < n_uids; ++i_uid){
		if (uids[i_uid]){
			g_ptr_array_add(matches, g_strdup(uids[i_uid]));
			free(uids[i_uid]);
		}
	}
	if (uids) free(uids);
	return (matches);
}

gboolean openchange_set_message_flags(CamelFolder *folder, const char *uid, guint32 flags, guint32 set)
{
	CamelMessageInfoBase	*info;
	CamelOpenchangeFolder	*oc_folder = (CamelOpenchangeFolder *)folder;
	guint32	new_flags;
	CamelFolderChangeInfo *changes;

	if (!uid || !folder) return (FALSE);
	
	if (folder->summary == NULL) return (FALSE);
	if (oc_folder->modified_key == NULL)
		oc_folder->modified_key = g_ptr_array_new();
	if (oc_folder->modified == NULL)
		oc_folder->modified = g_hash_table_new(g_str_hash, g_str_equal);
	oc_thread_fs_lock(((CamelOpenchangeFolder *)folder)->oc_thread);
	info = (CamelMessageInfoBase *)camel_folder_summary_uid(folder->summary, uid);
	if (info == NULL){
		oc_thread_fs_unlock(((CamelOpenchangeFolder *)folder)->oc_thread);
		return FALSE;
	}
	if (g_hash_table_steal(oc_folder->modified, uid) != true) {
 		g_ptr_array_add(oc_folder->modified_key, info->uid);
		oc_folder->n_modified--;
	}
	g_hash_table_insert(oc_folder->modified, info->uid, info);
	oc_folder->n_modified = g_hash_table_size(oc_folder->modified);
	new_flags = (info->flags & ~flags) | (set & flags);
	oc_thread_fs_unlock(((CamelOpenchangeFolder *)folder)->oc_thread);
	if (new_flags != info->flags) {
		info->flags = new_flags;
		camel_folder_summary_touch(folder->summary);
		changes = camel_folder_change_info_new();
		if (info->uid) {
			camel_folder_change_info_change_uid(changes, info->uid);
			camel_object_trigger_event(info->summary->folder, "folder_changed", changes);
		}
		camel_folder_change_info_free(changes);
	}
	return TRUE;
}

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

#include "camel-mapi-private.h"
#include "camel-mapi-summary.h"

#define DEBUG_FN( ) printf("----%u %s\n", (unsigned int)pthread_self(), __FUNCTION__);

static CamelOfflineFolderClass *parent_class = NULL;

struct _CamelMapiFolderPrivate {
	//FIXME : ??
	GSList *item_list;
#ifdef ENABLE_THREADS
	GStaticMutex search_lock;	/* for locking the search object */
	GStaticRecMutex cache_lock;	/* for locking the cache object */
#endif

};

typedef enum  {
	MAPI_ITEM_TYPE_MAIL=1,
	MAPI_ITEM_TYPE_APPOINTMENT,
	MAPI_ITEM_TYPE_CONTACT,
	MAPI_ITEM_TYPE_JOURNAL,
	MAPI_ITEM_TYPE_TASK
} MapiItemType;


typedef struct {
	gchar *subject;
	gchar *from;
	gchar *to;
	gchar *cc;
	gchar *bcc;

	int flags;
	glong size;
	time_t recieved_time;
	time_t send_time;
} MapiItemHeader;

typedef struct {
	gchar *body;
} MapiItemMessage;


typedef struct  {
	mapi_id_t fid;
	mapi_id_t mid;

	MapiItemHeader header;
	MapiItemMessage msg;
}MapiItem;

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
	mapi_initialize();
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

static GPtrArray *
mapi_folder_search_by_expression (CamelFolder *folder, const char *expression, CamelException *ex)
{
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER(folder);
	GPtrArray *matches;

	CAMEL_MAPI_FOLDER_LOCK(mapi_folder, search_lock);
	camel_folder_search_set_folder (mapi_folder->search, folder);
	matches = camel_folder_search_search(mapi_folder->search, expression, NULL, ex);
	CAMEL_MAPI_FOLDER_UNLOCK(mapi_folder, search_lock);

	return matches;
}


static int
mapi_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	CamelFolder *folder = (CamelFolder *)object;
	int i, count = 0;
	guint32 tag;

	//FIXME : HACK . FOR NOW !
	return 0;

	for (i=0 ; i<args->argc ; i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {

			case CAMEL_OBJECT_ARG_DESCRIPTION:
				if (folder->description == NULL) {
					CamelURL *uri = ((CamelService *)folder->parent_store)->url;

					folder->description = g_strdup_printf("%s@%s:%s", uri->user, uri->host, folder->full_name);
				}
				*arg->ca_str = folder->description;
				break;
			default:
				count++;
				continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	if (count)
		return ((CamelObjectClass *)parent_class)->getv(object, ex, args);

	return 0;

}

static void
mapi_refresh_info(CamelFolder *folder, CamelException *ex)
{
	printf("%s(%d):%s:reached \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	CamelMapiSummary *summary = (CamelMapiSummary *) folder->summary;
	CamelStoreInfo *si;
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (folder->parent_store);
	/*
	 * Checking for the summary->time_string here since the first the a
	 * user views a folder, the read cursor is in progress, and the getQM
	 * should not interfere with the process
	 */
	//	if (summary->time_string && (strlen (summary->time_string) > 0))  {
	if(1){
		mapi_refresh_folder(folder, ex);
		si = camel_store_summary_path ((CamelStoreSummary *)((CamelMapiStore *)folder->parent_store)->summary, folder->full_name);
		if (si) {
			guint32 unread, total;
			camel_object_get (folder, NULL, CAMEL_FOLDER_TOTAL, &total, CAMEL_FOLDER_UNREAD, &unread, NULL);
			if (si->total != total || si->unread != unread) {
				si->total = total;
				si->unread = unread;
				camel_store_summary_touch ((CamelStoreSummary *)((CamelMapiStore *)folder->parent_store)->summary);
			}
			camel_store_summary_info_free ((CamelStoreSummary *)((CamelMapiStore *)folder->parent_store)->summary, si);
		}
		camel_folder_summary_save (folder->summary);
		camel_store_summary_save ((CamelStoreSummary *)((CamelMapiStore *)folder->parent_store)->summary);
	} else {
		/* We probably could not get the messages the first time. (get_folder) failed???!
		 * so do a get_folder again. And hope that it works
		 */
		g_print("Reloading folder...something wrong with the summary....\n");
		//		gw_store_reload_folder (gw_store, folder, 0, ex);
	}
//#endif

}

static MapiItemType
mapi_item_class_to_type (const char *type)
{
	MapiItemType item_type = MAPI_FOLDER_TYPE_MAIL;

	if (!strcmp (type, IPF_APPOINTMENT)) 
		item_type = MAPI_FOLDER_TYPE_APPOINTMENT;
	else if (!strcmp (type, IPF_CONTACT))
		item_type = MAPI_FOLDER_TYPE_CONTACT;
	else if (!strcmp (type, IPF_JOURNAL))
		item_type = MAPI_FOLDER_TYPE_JOURNAL;
	else if (!strcmp (type, IPF_TASK))
		item_type = MAPI_FOLDER_TYPE_TASK;

	/* Else it has to be a mail folder only. It is assumed in MAPI code as well. */

	return item_type;
}


static void
debug_mapi_property_dump (struct mapi_SPropValue_array *properties)
{
	gint i = 0;

	for (i = 0; i < properties->cValues; i++) { 
		for (i = 0; i < properties->cValues; i++) {
			struct mapi_SPropValue *lpProp = &properties->lpProps[i];
			const char *tmp =  get_proptag_name (lpProp->ulPropTag);
			struct timeval t;
			if (tmp && *tmp)
				printf("\n%s \t",tmp);
			else
				printf("\n%x \t", lpProp->ulPropTag);
			switch(lpProp->ulPropTag & 0xFFFF) {
			case PT_BOOLEAN:
				printf(" (bool) - %d", lpProp->value.b);
				break;
			case PT_I2:
				printf(" (uint16_t) - %d", lpProp->value.i);
				break;
			case PT_LONG:
				printf(" (long) - %ld", lpProp->value.l);
				break;
			case PT_DOUBLE:
				printf (" (double) -  %lf", lpProp->value.dbl);
				break;
			case PT_I8:
				printf (" (int) - %d", lpProp->value.d);
				break;
			case PT_SYSTIME:
/* 				get_mapi_SPropValue_array_date_timeval (&t, properties, lpProp->ulPropTag); */
/* 				printf (" (struct FILETIME *) - %p\t[%s]\t", &lpProp->value.ft, icaltime_as_ical_string (icaltime_from_timet_with_zone (t.tv_sec, 0, utc_zone))); */
				break;
			case PT_ERROR:
				printf (" (error) - %p", lpProp->value.err);
				break;
			case PT_STRING8:
				printf(" (string) - %s", lpProp->value.lpszA ? lpProp->value.lpszA : "null" );
				break;
			case PT_UNICODE:
				printf(" (unicodestring) - %s", lpProp->value.lpszW ? lpProp->value.lpszW : "null");
				break;
			case PT_BINARY:
//				printf(" (struct SBinary_short *) - %p", &lpProp->value.bin);
				break;
			case PT_MV_STRING8:
 				printf(" (struct mapi_SLPSTRArray *) - %p", &lpProp->value.MVszA);
				break;
			default:
				printf(" - NONE NULL");
			}
		}
	}
}
static gboolean
fetch_items_cb (struct mapi_SPropValue_array *array, const mapi_id_t fid, const mapi_id_t mid, gpointer data)
{
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER(data);
	GSList *slist = mapi_folder->priv->item_list;
	long *flags;
	struct FILETIME *delivery_date;
	NTTIME ntdate;

	MapiItem *item = g_new0(MapiItem , 1);

	item->fid = fid;
	item->mid = mid;

	/* FixME : which on of this will fetch the subject. */
/* 	item->header.subject = find_mapi_SPropValue_data (array, PR_CONVERSATION_TOPIC); */
/* 	item->header.subject = find_mapi_SPropValue_data (array, PR_NORMALIZED_SUBJECT); */
/* 	item->header.subject = find_mapi_SPropValue_data (array, PR_CONVERSATION_TOPIC_UNICODE); */
/* 	item->header.subject = find_mapi_SPropValue_data (array, PR_SUBJECT); */
	item->header.subject = find_mapi_SPropValue_data (array, PR_URL_NAME);
	item->header.to = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_TO));
	item->header.cc = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_CC));
	item->header.bcc = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_BCC));
	item->header.from = g_strdup (find_mapi_SPropValue_data (array, PR_SENT_REPRESENTING_NAME));
	item->header.size = *(glong *)(find_mapi_SPropValue_data (array, PR_MESSAGE_SIZE));

	delivery_date = (struct FILETIME *)find_mapi_SPropValue_data(array, PR_MESSAGE_DELIVERY_TIME);
	if (delivery_date) {
		ntdate = delivery_date->dwHighDateTime;
		ntdate = ntdate << 32;
		ntdate |= delivery_date->dwLowDateTime;
		item->header.recieved_time = nt_time_to_unix(ntdate);
	}

	flags = (long *)find_mapi_SPropValue_data (array, PR_MESSAGE_FLAGS);
	if ((*flags & MSGFLAG_READ) != 0)
		item->header.flags |= CAMEL_MESSAGE_SEEN;
	if ((*flags & MSGFLAG_HASATTACH) != 0)
		item->header.flags |= CAMEL_MESSAGE_ATTACHMENTS;

/* 	printf("%s(%d):%s:subject : %s \n from : %s\nto : %s\n cc : %s\n", __FILE__, */
/* 	       __LINE__, __PRETTY_FUNCTION__, item->header.subject, */
/* 	       item->header.from, item->header.to, item->header.cc); */
//	debug_mapi_property_dump (array);

	slist = g_slist_append (slist, item);
	mapi_folder->priv->item_list = slist;
}

static void
mapi_update_cache (CamelFolder *folder, GList *list, CamelException *ex, gboolean uid_flag) 
{
	CamelMapiMessageInfo *mi = NULL;
	CamelMessageInfo *pmi = NULL;
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (folder->parent_store);
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER(folder);
	CamelMapiStorePrivate *priv = mapi_store->priv;

	guint32 item_status, status_flags = 0;
	CamelFolderChangeInfo *changes = NULL;
	gboolean exists = FALSE;
	GString *str = g_string_new (NULL);
	const char *priority = NULL;
	gchar *folder_id = NULL;
	gboolean is_junk = FALSE;
	gboolean status;
	GList *item_list = list;
	int total_items = g_list_length (item_list), i=0;

	gboolean is_proxy = folder->parent_store->flags & CAMEL_STORE_WRITE;

	changes = camel_folder_change_info_new ();
	folder_id = camel_mapi_store_folder_id_lookup (mapi_store, folder->full_name);

	if (!folder_id) {
		d(printf("\nERROR - Folder id not present. Cannot refresh info\n"));
		camel_folder_change_info_free (changes);
		return;
	}

/* 	if (!strcmp (folder->full_name, JUNK_FOLDER)) { */
/* 		is_junk = TRUE; */
/* 	} */

	camel_operation_start (NULL, _("Fetching summary information for new messages in %s"), folder->name);

	for ( ; item_list != NULL ; item_list = g_list_next (item_list) ) {
		MapiItem *temp_item ;
		MapiItem *item;
		char *temp_date = NULL;
		guint64 id;
		GSList *recp_list = NULL;
		CamelStream *cache_stream, *t_cache_stream;
		CamelMimeMessage *mail_msg = NULL;
		const char *recurrence_key = NULL;
		int rk;

		exists = FALSE;
		status_flags = 0;

		if (uid_flag == FALSE) {
 			temp_item = (MapiItem *)item_list->data;
			id = temp_item->mid;
			item = temp_item;
		}
		//fixme
/* 		} else  */
/* 			id = (char *) item_list->data; */

		camel_operation_progress (NULL, (100*i)/total_items);

		/************************ First populate summary *************************/
		mi = NULL;
		pmi = NULL;
		pmi = camel_folder_summary_uid (folder->summary, g_strdup_printf ("%016llx",id));
		if (pmi) {
			exists = TRUE;
			camel_message_info_ref (pmi);
			mi = (CamelMapiMessageInfo *)pmi;
		}

		if (!exists) {
//			type = exchange_mapi_folder_get_item_type (item);
			//FIXME
/* 			if ((type == E_GW_ITEM_TYPE_CONTACT) || (type == E_GW_ITEM_TYPE_UNKNOWN)) { */
/* 				exists = FALSE; */
/* 				continue; */
/* 			} */

			mi = (CamelMapiMessageInfo *)camel_message_info_new (folder->summary); 
			if (mi->info.content == NULL) {
				mi->info.content = camel_folder_summary_content_info_new (folder->summary);
				mi->info.content->type = camel_content_type_new ("multipart", "mixed");	
			}
		}
		
/* 		rk = e_gw_item_get_recurrence_key (item); */
/* 		if (rk > 0) { */
/* 			recurrence_key = g_strdup_printf("%d", rk);  */
/* 			camel_message_info_set_user_tag ((CamelMessageInfo*)mi, "recurrence-key", recurrence_key); */
/* 		}  */

		/*all items in the Junk Mail folder should have this flag set*/
/* 		if (is_junk) */
/* 			mi->info.flags |= CAMEL_GW_MESSAGE_JUNK; */

		mi->info.flags = item->header.flags;

/* 		if (item_status & E_GW_ITEM_STAT_REPLIED) */
/* 			status_flags |= CAMEL_MESSAGE_ANSWERED; */
/* 		if (exists)  */
/* 			mi->info.flags |= status_flags; */
/* 		else  */
/* 			mi->info.flags = status_flags; */

/* 		priority = e_gw_item_get_priority (item); */
/* 		if (priority && !(g_ascii_strcasecmp (priority,"High"))) { */
/* 			mi->info.flags |= CAMEL_MESSAGE_FLAGGED; */
/* 		} */

/* 		if (e_gw_item_has_attachment (item)) */
/* 			mi->info.flags |= CAMEL_MESSAGE_ATTACHMENTS; */
/*                 if (is_proxy) */
/*                         mi->info.flags |= CAMEL_MESSAGE_USER_NOT_DELETABLE; */
		
/* 		mi->server_flags = mi->info.flags; */

/* 		org = e_gw_item_get_organizer (item);  */
/* 		if (org) { */
/* 			GString *str; */
/* 			int i; */
/* 			str = g_string_new (""); */
/* 			if (org->display_name && org->display_name[0] && org->email != NULL && org->email[0] != '\0') { */
/* 				for (i = 0; org->display_name[i] != '<' &&  */
/* 						org->display_name[i] != '\0'; */
/* 						i++); */

/* 				org->display_name[i] = '\0'; */
/* 				str = g_string_append (str, org->display_name); */
/* 				str = g_string_append (str, " "); */
/* 			} */

/*                         if (org->display_name[0] == '\0') {  */

/* 				str = g_string_append (str, org->email); */
/* 				str = g_string_append (str, " "); */
/* 			} */
/* 			if (org->email && org->email[0]) {  */
/* 				g_string_append (str, "<"); */
/* 				str = g_string_append (str, org->email); */
/* 				g_string_append (str, ">"); */
/* 			} */
/* 			g_string_free (str, TRUE); */
/* 		} */
/* 		g_string_truncate (str, 0); */
/* 		recp_list = e_gw_item_get_recipient_list (item); */
/* 		if (recp_list) { */
/* 			GSList *rl; */
/* 			int i = 0; */
/* 			for (rl = recp_list; rl != NULL; rl = rl->next) { */
/* 				EGwItemRecipient *recp = (EGwItemRecipient *) rl->data; */
/* 				if (recp->type == E_GW_ITEM_RECIPIENT_TO) { */
/* 					if (i) */
/* 						str = g_string_append (str, ", "); */
/* 					g_string_append_printf (str,"%s <%s>", recp->display_name, recp->email); */
/* 					i++; */
/* 				} */
/* 			} */
/* 			if (exists) */
/* 				camel_pstring_free(mi->info.to); */
/* 			mi->info.to = camel_pstring_strdup (str->str); */
/* 			g_string_truncate (str, 0); */
/* 		} */

/* 		if (type == E_GW_ITEM_TYPE_APPOINTMENT */
/* 				|| type ==  E_GW_ITEM_TYPE_NOTE  */
/* 				|| type ==  E_GW_ITEM_TYPE_TASK ) { */
/* 			temp_date = e_gw_item_get_start_date (item); */
/* 			if (temp_date) { */
/* 				time_t time = e_gw_connection_get_date_from_string (temp_date); */
/* 				time_t actual_time = camel_header_decode_date (ctime(&time), NULL); */
/* 				mi->info.date_sent = mi->info.date_received = actual_time; */
/* 			} */
/* 		} else { */
/* 			temp_date = e_gw_item_get_delivered_date(item); */
/* 			if (temp_date) { */
/* 				time_t time = e_gw_connection_get_date_from_string (temp_date); */
/* 				time_t actual_time = camel_header_decode_date (ctime(&time), NULL); */
/* 				mi->info.date_sent = mi->info.date_received = actual_time; */
/* 			} else { */
/* 				time_t time; */
/* 				time_t actual_time; */
/* 				temp_date = e_gw_item_get_creation_date (item); */
/* 				time = e_gw_connection_get_date_from_string (temp_date); */
/* 				actual_time = camel_header_decode_date (ctime(&time), NULL); */
/* 				mi->info.date_sent = mi->info.date_received = actual_time; */
/* 			} */
/* 		} */

		if (!exists) {
			mi->info.uid = g_strdup (mapi_ids_to_uid(item->fid, item->mid));
			mi->info.subject = camel_pstring_strdup(item->header.subject);
			mi->info.date_sent = mi->info.date_received = item->header.recieved_time;
			mi->info.from = camel_pstring_strdup (item->header.from);
			mi->info.to = camel_pstring_strdup (item->header.to);
			mi->info.size = (guint32) item->header.size;
		}

		if (exists) {
			camel_folder_change_info_change_uid (changes, mi->info.uid);
			camel_message_info_free (pmi);
		} else {
			camel_folder_summary_add (folder->summary,(CamelMessageInfo *)mi);
			camel_folder_change_info_add_uid (changes, mi->info.uid);
			camel_folder_change_info_recent_uid (changes, mi->info.uid);
		}

		/********************* Summary ends *************************/
		if (!strcmp (folder->full_name, "Junk Mail"))
			continue;

		/******************** Begine Caching ************************/
		//add to cache if its a new message
/* 		t_cache_stream  = camel_data_cache_get (mapi_folder->cache, "cache", id, ex); */
/* 		if (t_cache_stream) { */
/* 			camel_object_unref (t_cache_stream); */

/* 			mail_msg = groupwise_folder_item_to_msg (folder, item, ex); */
/* 			if (mail_msg) */
/* 				camel_medium_set_header (CAMEL_MEDIUM (mail_msg), "X-Evolution-Source", groupwise_base_url_lookup (priv)); */

/* 			CAMEL_MAPI_FOLDER_REC_LOCK (folder, cache_lock); */
/* 			if ((cache_stream = camel_data_cache_add (gw_folder->cache, "cache", id, NULL))) { */
/* 				if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *) mail_msg, 	cache_stream) == -1 || camel_stream_flush (cache_stream) == -1) */
/* 					camel_data_cache_remove (gw_folder->cache, "cache", id, NULL); */
/* 				camel_object_unref (cache_stream); */
/* 			} */

/* 			camel_object_unref (mail_msg); */
/* 			CAMEL_MAPI_FOLDER_REC_UNLOCK (folder, cache_lock); */
/* 		} */
		/******************** Caching stuff ends *************************/
		i++;
	}
	camel_operation_end (NULL);
	//	g_free (container_id);
	g_string_free (str, TRUE);
	camel_object_trigger_event (folder, "folder_changed", changes);

	camel_folder_change_info_free (changes);
	//TASK 2.
	printf("%s(%d):%s:reached \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
}

static void 
mapi_sync_summary (CamelFolder *folder, CamelException *ex)
{
	camel_folder_summary_save (folder->summary);
	camel_store_summary_save ((CamelStoreSummary *)((CamelMapiStore *)folder->parent_store)->summary);
}

void
mapi_refresh_folder(CamelFolder *folder, CamelException *ex)
{
	printf("%s(%d):%s:reached \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (folder->parent_store);
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER (folder);
	CamelMapiStorePrivate *priv = mapi_store->priv;
	CamelMapiSummary *summary = (CamelMapiSummary *)folder->summary;
	CamelSession *session = ((CamelService *)folder->parent_store)->session;
	gboolean is_proxy = folder->parent_store->flags & CAMEL_STORE_PROXY;
	gboolean is_locked = TRUE;
	gboolean status;
	GList *list = NULL;
	GSList *slist = NULL, *sl;
	//	guint64 *folder_id = g_new0 (guint64, 1);
	gchar *folder_id = NULL;
	char *time_string = NULL, *t_str = NULL;
	struct _folder_update_msg *msg;
	gboolean check_all = FALSE;
	/* Sync-up the (un)read changes before getting updates,
	so that the getFolderList will reflect the most recent changes too */
	//	groupwise_sync (folder, FALSE, ex);

	if (((CamelOfflineStore *) mapi_store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_warning ("In offline mode. Cannot refresh!!!\n");
		return;
	}

	//creating a copy
	folder_id = camel_mapi_store_folder_id_lookup (mapi_store, folder->full_name);
	//	printf("%s(%d):%s:folder_id : %d \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, *folder_id);
	if (!folder_id) {
		printf ("\nERROR - Folder id not present. Cannot refresh info for %s\n", folder->full_name);
		return;
	}

	if (camel_folder_is_frozen (folder) ) {
		mapi_folder->need_refresh = TRUE;
	}

	CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);

/* 	if (!camel_mapi_store_connected (mapi_store, ex))  */
/* 		goto end1; */

#if 0
	if (!strcmp (folder->full_name, "Trash")) {

		status = e_gw_connection_get_items (cnc, container_id, "peek recipient distribution created delivered attachments subject status size", NULL, &list);
		if (status != E_GW_CONNECTION_STATUS_OK) {
			if (status ==E_GW_CONNECTION_STATUS_OTHER) {
				g_warning ("Trash full....Empty Trash!!!!\n");
				camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Trash Folder Full. Please Empty."));
				goto end1;
				/*groupwise_expunge (folder, ex);*/
			} else
				camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Authentication failed"));
			goto end1;
		}
		if (list || g_list_length(list)) {
			camel_folder_summary_clear (folder->summary);
			gw_update_summary (folder, list, ex);
			g_list_foreach (list, (GFunc) g_object_unref, NULL);
			g_list_free (list);
			list = NULL;
		}
		goto end1;


	}
#endif
		       //	time_string =  g_strdup (((CamelGroupwiseSummary *) folder->summary)->time_string);
		       //	t_str = g_strdup (time_string); 

	/*Get the New Items*/
	if (!is_proxy) {
		char *source;

/* 		if ( !strcmp (folder->full_name, RECEIVED) || !strcmp(folder->full_name, SENT) ) { */
/* 			source = NULL; */
/* 		} else { */
/* 			source = "sent received"; */
/* 		} */
		mapi_id_t temp_folder_id;
		folder_uid_to_mapi_ids (folder_id, &temp_folder_id);
		status = exchange_mapi_connection_fetch_items (NULL, NULL, fetch_items_cb, temp_folder_id, folder);
		if (!status) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Fetch items failed"));
			goto end2;
		}

		/*
		 * The value in t_str is the one that has to be used for the next set of calls. 
		 * so store this value in the summary.
		 */
/* 		if (summary->time_string) */
/* 			g_free (summary->time_string); */


		//summary->time_string = g_strdup (t_str);
		       //		((CamelGroupwiseSummary *) folder->summary)->time_string = g_strdup (t_str);
		camel_folder_summary_touch (folder->summary);
		mapi_sync_summary (folder, ex);
/* 		g_free (t_str);	 */
/* 		t_str = NULL; */

		/*
		   for ( sl = slist ; sl != NULL; sl = sl->next) 
		   list = g_list_append (list, sl->data);*/

/* 		if (slist && g_slist_length(slist) != 0) */
/* 			check_all = TRUE; */

/* 		g_slist_free (slist); */
/* 		slist = NULL; */

/* 		t_str = g_strdup (time_string); */

/* 		/\*Get those items which have been modifed*\/ */

/* 		status = e_gw_connection_get_quick_messages (cnc, container_id, */
/* 				"peek id", */
/* 				&t_str, "Modified", NULL, source, -1, &slist); */

/* 		if (status != E_GW_CONNECTION_STATUS_OK) { */
/* 			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Authentication failed")); */
/* 			goto end3; */
/* 		} */

		/* The storing of time-stamp to summary code below should be commented if the 
		   above commented code is uncommented */

		/*n	if (summary->time_string)
			g_free (summary->time_string);

			summary->time_string = g_strdup (t_str);

			g_free (t_str), t_str = NULL;*/

/* 		for ( sl = slist ; sl != NULL; sl = sl->next)  */
/* 			list = g_list_prepend (list, sl->data); */

/* 		if (!check_all && slist && g_slist_length(slist) != 0) */
/* 			check_all = TRUE; */

/* 		g_slist_free (slist); */
/* 		slist = NULL; */

/* 		if (gw_store->current_folder != folder) { */
/* 			gw_store->current_folder = folder; */
/* 		} */

		if (mapi_folder->priv->item_list) {
			printf("%s(%d):%s:gonna call \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
			mapi_update_cache (folder, mapi_folder->priv->item_list, ex, FALSE);
		}
	}


	CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
	is_locked = FALSE;

	/*
	 * The New and Modified items in the server have been updated in the summary. 
	 * Now we have to make sure that all the delted items in the server are deleted
	 * from Evolution as well. So we get the id's of all the items on the sever in 
	 * this folder, and update the summary.
	 */
	/*create a new session thread for the update all operation*/
/* 	if (check_all || is_proxy) { */
/* 		msg = camel_session_thread_msg_new (session, &update_ops, sizeof(*msg)); */
/* 		msg->cnc = cnc; */
/* 		msg->t_str = g_strdup (time_string); */
/* 		msg->container_id = g_strdup (container_id); */
/* 		msg->folder = folder; */
/* 		camel_object_ref (folder); */
/* 		camel_folder_freeze (folder); */
/* 		camel_session_thread_queue (session, &msg->msg, 0); */
/* 		/\*thread creation and queueing done*\/ */
/* 	} */

end3: 
	//	g_list_foreach (list, (GFunc) g_object_unref, NULL);
	//	g_list_free (list);
	list = NULL;
end2:
/* 	g_free (t_str); */
/* 	g_free (time_string); */
//	g_free (container_id);
end1:
	if (is_locked)
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
	return;

}

static gpointer
fetch_item_cb (struct mapi_SPropValue_array *array, mapi_id_t fid, mapi_id_t mid)
{
	debug_mapi_property_dump (array);
	printf("%s(%d):%s:reached \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	long *flags;
	struct FILETIME *delivery_date;
	NTTIME ntdate;

	MapiItem *item = g_new0(MapiItem , 1);

	item->fid = fid;
	item->mid = mid;

	/* FixME : which on of this will fetch the subject. */
/* 	item->header.subject = find_mapi_SPropValue_data (array, PR_CONVERSATION_TOPIC); */
/* 	item->header.subject = find_mapi_SPropValue_data (array, PR_NORMALIZED_SUBJECT); */
/* 	item->header.subject = find_mapi_SPropValue_data (array, PR_CONVERSATION_TOPIC_UNICODE); */
/* 	item->header.subject = find_mapi_SPropValue_data (array, PR_SUBJECT); */
	item->header.subject = find_mapi_SPropValue_data (array, PR_URL_NAME);
	item->header.to = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_TO));
	item->header.cc = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_CC));
	item->header.bcc = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_BCC));
	item->header.from = g_strdup (find_mapi_SPropValue_data (array, PR_SENT_REPRESENTING_NAME));
	item->header.size = *(glong *)(find_mapi_SPropValue_data (array, PR_MESSAGE_SIZE));
	item->msg.body = g_strdup (find_mapi_SPropValue_data (array, PR_BODY));

	delivery_date = (struct FILETIME *)find_mapi_SPropValue_data(array, PR_MESSAGE_DELIVERY_TIME);
	if (delivery_date) {
		ntdate = delivery_date->dwHighDateTime;
		ntdate = ntdate << 32;
		ntdate |= delivery_date->dwLowDateTime;
		item->header.recieved_time = nt_time_to_unix(ntdate);
	}

	flags = (long *)find_mapi_SPropValue_data (array, PR_MESSAGE_FLAGS);
	if ((*flags & MSGFLAG_READ) != 0)
		item->header.flags |= CAMEL_MESSAGE_SEEN;
	if ((*flags & MSGFLAG_HASATTACH) != 0)
		item->header.flags |= CAMEL_MESSAGE_ATTACHMENTS;

	return item;
}


static void
mapi_msg_set_recipient_list (CamelMimeMessage *msg, MapiItem *item)
{
	CamelInternetAddress *addr = NULL;
	{
		char *tmp_addr = NULL;
		int index, len;
		
		addr = camel_internet_address_new();
		for (index = 0; item->header.to[index]; index += len){
			if (item->header.to[index] == ';')
				index++;
			for (len = 0; item->header.to[index + len] &&
				     item->header.to[index + len] != ';'; len++)
				;
			tmp_addr = malloc(/* tmp_addr, */ len + 1);
			memcpy(tmp_addr, item->header.to + index, len);
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
		for (index = 0; item->header.cc[index]; index += len){
			if (item->header.cc[index] == ';')
				index++;
			for (len = 0; item->header.cc[index + len] &&
				     item->header.cc[index + len] != ';'; len++)
				;
			tmp_addr = malloc(/* tmp_addr, */ len + 1);
			memcpy(tmp_addr, item->header.cc + index, len);
			tmp_addr[len] = 0;
			if (len) camel_internet_address_add(addr, tmp_addr, tmp_addr);
		}
		if (index != 0)
			camel_mime_message_set_recipients(msg, "Cc", addr);
	}
}


static void
mapi_populate_details_from_item (CamelMimeMessage *msg, MapiItem *item)
{
	char *dtstring = NULL;
	char *temp_str = NULL;
	time_t recieved_time;
	CamelInternetAddress *addr = NULL;

	temp_str = item->header.subject;
	printf("%s(%d):%s:SUBJECT : %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, temp_str);
	if(temp_str) 
		camel_mime_message_set_subject (msg, temp_str);
	else
		printf("%s(%d):%s:subject is null \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);

	recieved_time = item->header.recieved_time;
/* 	if(dtstring) { */
		int offset = 0;
		time_t actual_time = camel_header_decode_date (ctime(&recieved_time), &offset);
		camel_mime_message_set_date (msg, actual_time, offset);
/* 	} else { */
/* 		time_t time; */
/* 		time_t actual_time; */
/* 		int offset = 0; */
/* 		dtstring = e_gw_item_get_creation_date (item); */
/* 		time = e_gw_connection_get_date_from_string (dtstring); */
/* 		actual_time = camel_header_decode_date (ctime(&time), NULL); */
/* 		camel_mime_message_set_date (msg, actual_time, offset); */
/* 	} */
	if (item->header.from) {
		/* add reply to */
		addr = camel_internet_address_new();
		camel_internet_address_add(addr, item->header.from, item->header.from);
		camel_mime_message_set_reply_to(msg,addr);
		
		/* add from */
		addr = camel_internet_address_new();
		camel_internet_address_add(addr, item->header.from, item->header.from);
		camel_mime_message_set_from(msg,addr);
	}
}


static void
mapi_populate_msg_body_from_item (CamelMultipart *multipart, MapiItem *item, char *body)
{
	CamelMimePart *part;
	//	EGwItemType type;
	const char *temp_body = NULL;

	part = camel_mime_part_new ();
	camel_mime_part_set_encoding(part, CAMEL_TRANSFER_ENCODING_8BIT);

/* 	if (!body) { */
/* 		temp_body = e_gw_item_get_message (item); */
/* 		if(!temp_body){ */
/* 			int len = 0; */
/* 			EGwConnectionStatus status; */
/* 			status = e_gw_connection_get_attachment (cnc,  */
/* 					e_gw_item_get_msg_body_id (item), 0, -1,  */
/* 					(const char **)&temp_body, &len); */
/* 			if (status != E_GW_CONNECTION_STATUS_OK) { */
/* 				g_warning ("Could not get Messagebody\n"); */
/* 			} */
/* 		} */
/* 	} */

/* 	type = mapi_item_class_to_type (item); */
/* 	switch (type) { */

/* 		case MAPI_ITEM_TYPE_APPOINTMENT: */
/* 		case MAPI_ITEM_TYPE_TASK: */
/* 		case MAPI_ITEM_TYPE_NOTE: */
/* 			{ */
/* /\* 				char *cal_buffer = NULL; *\/ */
/* /\* 				int len = 0; *\/ */
/* /\* 				if (type==MAPI_ITEM_TYPE_APPOINTMENT) *\/ */
/* /\* 					convert_to_calendar (item, &cal_buffer, &len); *\/ */
/* /\* 				else if (type == E_GW_ITEM_TYPE_TASK) *\/ */
/* /\* 					convert_to_task (item, &cal_buffer, &len); *\/ */
/* /\* 				else  *\/ */
/* /\* 					convert_to_note (item, &cal_buffer, &len); *\/ */

/* /\* 				camel_mime_part_set_content(part, cal_buffer, len, "text/calendar"); *\/ */
/* /\* 				g_free (cal_buffer); *\/ */
/* 				break; */
/* 			} */
/* 		case MAPI_ITEM_TYPE_MAIL: */
/* 			if (body)  */
/* 				camel_mime_part_set_content(part, body, strlen(body), "text/html"); */
/* /\* 			else if (temp_body) *\/ */
/* /\* 				camel_mime_part_set_content(part, temp_body, strlen(temp_body), e_gw_item_get_msg_content_type (item)); *\/ */
/* 			else */
/* 				camel_mime_part_set_content(part, " ", strlen(" "), "text/html"); */
/* 			break; */

/* 		default: */
/* 			break; */

/* 	} */
	if (body)
		camel_mime_part_set_content(part, body, strlen(body), "text/plain");
	else
		camel_mime_part_set_content(part, " ", strlen(" "), "text/html");

	camel_multipart_set_boundary (multipart, NULL);
	camel_multipart_add_part (multipart, part);
	camel_object_unref (part);
}


static CamelMimeMessage *
mapi_folder_item_to_msg( CamelFolder *folder,
		MapiItem *item,
		CamelException *ex )
{

	CamelMimeMessage *msg = NULL;
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE(folder->parent_store);
	CamelMapiStorePrivate  *priv = mapi_store->priv;
	const char *container_id = NULL;

	//	mapi_populate_details_from_item (msg, item);
	//	mapi_populate_msg_body_from_item (CamelMultipart *multipart, MapiItem *item, char *body);
	MapiItemType type;
	CamelMultipart *multipart = NULL;

	int errno;
	char *body = NULL;
	int body_len = 0;
	const char *uid = NULL;
	CamelStream *temp_stream;

	//	uid = mapi_ids_to_uid(item->fid, item->mid);

	msg = camel_mime_message_new ();

	multipart = camel_multipart_new ();

	//	camel_mime_message_set_message_id (msg, uid);
	body = item->msg.body;

/* 	type = mapi_item_get_item_type (item); */
/* 	if (type == E_GW_ITEM_TYPE_NOTIFICATION) */
/* 		camel_medium_add_header ( CAMEL_MEDIUM (msg), "X-Notification", "shared-folder"); */

	mapi_populate_msg_body_from_item (multipart, item, body);
	/*Set recipient details*/
	mapi_msg_set_recipient_list (msg, item);
	mapi_populate_details_from_item (msg, item);

	camel_medium_set_content_object(CAMEL_MEDIUM (msg), CAMEL_DATA_WRAPPER(multipart));
	camel_object_unref (multipart);

end:
	if (body)
		g_free (body);

	return msg;
}


static CamelMimeMessage *
mapi_folder_get_message( CamelFolder *folder, const char *uid, CamelException *ex )
{
	printf("%s(%d):%s:REACHED \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	CamelMimeMessage *msg = NULL;
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER(folder);
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE(folder->parent_store);
	CamelMapiStorePrivate  *priv = mapi_store->priv;
	CamelMapiMessageInfo *mi = NULL;
	char *folder_id;
	CamelStream *stream, *cache_stream;
	int errno;

	/* see if it is there in cache */

	mi = (CamelMapiMessageInfo *) camel_folder_summary_uid (folder->summary, uid);
	if (mi == NULL) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				_("Cannot get message: %s\n  %s"), uid, _("No such message"));
		return NULL;
	}
	cache_stream  = camel_data_cache_get (mapi_folder->cache, "cache", uid, ex);
	stream = camel_stream_mem_new ();
	if (cache_stream) {
		msg = camel_mime_message_new ();
		camel_stream_reset (stream);
		camel_stream_write_to_stream (cache_stream, stream);
		camel_stream_reset (stream);
		if (camel_data_wrapper_construct_from_stream ((CamelDataWrapper *) msg, stream) == -1) {
			if (errno == EINTR) {
				camel_exception_setv (ex, CAMEL_EXCEPTION_USER_CANCEL, _("User canceled"));
				camel_object_unref (msg);
				camel_object_unref (cache_stream);
				camel_object_unref (stream);
				camel_message_info_free (&mi->info);
				return NULL;
			} else {
				camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot get message %s: %s"),
						uid, g_strerror (errno));
				camel_object_unref (msg);
				msg = NULL;
			}
		}
		camel_object_unref (cache_stream);
	}
	camel_object_unref (stream);

	if (msg != NULL) {
		camel_message_info_free (&mi->info);
		return msg;
	}

	if (((CamelOfflineStore *) mapi_store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				_("This message is not available in offline mode."));
		camel_message_info_free (&mi->info);
		return NULL;
	}

	/* Check if we are really offline */
/* 	if (!camel_mapi_store_connected (mapi_store, ex)) { */
/* 		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE, */
/* 				_("This message is not available in offline mode.")); */
/* 		camel_message_info_free (&mi->info); */
/* 		return NULL; */
/* 	} */

	mapi_id_t id_folder;
	mapi_id_t id_message;
	MapiItem *item;
	uid_to_mapi_ids(uid, &id_folder, &id_message);

	folder_id =  g_strdup (camel_mapi_store_folder_id_lookup (mapi_store, folder->full_name)) ;
	//	printf("%s(%d):%s:folder_id : %s|%s|%s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, folder_id, folder_mapi_ids_to_uid(id_message), folder_mapi_ids_to_uid(id_folder));
	//	cnc = cnc_lookup (priv);
	//Insert MAPI call for GetItem.
	item = exchange_mapi_connection_fetch_item (NULL, id_folder, id_message, fetch_item_cb);
	//	status = e_gw_connection_get_item (cnc, container_id, uid, "peek default distribution recipient message attachments subject notification created recipientStatus status", &item);

//TODO : Later.
/* 	if (status != E_GW_CONNECTION_STATUS_OK) { */
/* 		g_free (container_id); */
/* 		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Could not get message")); */
/* 		camel_message_info_free (&mi->info); */
/* 		return NULL; */
/* 	} */

	if (!item)
		printf("%s(%d):%s:PANIC : Item is null \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);

	msg = mapi_folder_item_to_msg (folder, item, ex);

	if (!msg) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Could not get message"));
		//		g_free (container_id);
		camel_message_info_free (&mi->info);

		return NULL;
	}

/* 	if (msg) */
/* 		camel_medium_set_header (CAMEL_MEDIUM (msg), "X-Evolution-Source", groupwise_base_url_lookup (priv)); */

	/* add to cache */
	CAMEL_MAPI_FOLDER_REC_LOCK (folder, cache_lock);
	if ((cache_stream = camel_data_cache_add (mapi_folder->cache, "cache", uid, NULL))) {
		if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *) msg, cache_stream) == -1
				|| camel_stream_flush (cache_stream) == -1)
			camel_data_cache_remove (mapi_folder->cache, "cache", uid, NULL);
		camel_object_unref (cache_stream);
	}

	CAMEL_MAPI_FOLDER_REC_UNLOCK (folder, cache_lock);

	camel_message_info_free (&mi->info);
//	g_free (container_id);
	return msg;
}


CamelMessageInfo*
mapi_get_message_info(CamelFolder *folder, const char *uid)
{ 
#if 0
	CamelMessageInfo	*msg_info = NULL;
	CamelMessageInfoBase	*mi = (CamelMessageInfoBase *)msg ;
	int			status = 0;
	oc_message_headers_t	headers;

	if (folder->summary) {
		msg_info = camel_folder_summary_uid(folder->summary, uid);
	}
	if (msg_info != NULL) {
		mi = (CamelMessageInfoBase *)msg_info ;
		return (msg_info);
	}
	msg_info = camel_message_info_new(folder->summary);
	mi = (CamelMessageInfoBase *)msg_info ;
	//TODO :
/* 	oc_message_headers_init(&headers); */
/* 	oc_thread_connect_lock(); */
/* 	status = oc_message_headers_get_by_id(&headers, uid); */
/* 	oc_thread_connect_unlock(); */

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
#endif
	return NULL;
}

static void
mapi_folder_search_free (CamelFolder *folder, GPtrArray *uids)
{
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER(folder);

	g_return_if_fail (mapi_folder->search);

	CAMEL_MAPI_FOLDER_LOCK(mapi_folder, search_lock);

	camel_folder_search_free_result (mapi_folder->search, uids);

	CAMEL_MAPI_FOLDER_UNLOCK(mapi_folder, search_lock);

}

static void
camel_mapi_folder_finalize (CamelObject *object)
{
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER (object);

	if (mapi_folder->priv)
		g_free(mapi_folder->priv);
	if (mapi_folder->cache)
		camel_object_unref (mapi_folder->cache);
/* 	if (gw_folder->search) */
/* 		camel_object_unref (gw_folder->search); */

}

static void
camel_mapi_folder_class_init (CamelMapiFolderClass *camel_mapi_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_mapi_folder_class);

	parent_class = CAMEL_OFFLINE_FOLDER_CLASS (camel_type_get_global_classfuncs (camel_offline_folder_get_type ()));

	((CamelObjectClass *) camel_mapi_folder_class)->getv = mapi_getv;

	camel_folder_class->get_message = mapi_folder_get_message;
/* 	camel_folder_class->rename = mapi_folder_rename; */
	camel_folder_class->search_by_expression = mapi_folder_search_by_expression;
/* 	camel_folder_class->get_message_info = mapi_get_message_info; */
/* 	camel_folder_class->search_by_uids = mapi_folder_search_by_uids;  */
	camel_folder_class->search_free = mapi_folder_search_free;
/* 	camel_folder_class->append_message = mapi_append_message; */
	camel_folder_class->refresh_info = mapi_refresh_info;
/* 	camel_folder_class->sync = mapi_sync; */
/* 	camel_folder_class->expunge = mapi_expunge; */
/* 	camel_folder_class->transfer_messages_to = mapi_transfer_messages_to; */
}

static void
camel_mapi_folder_init (gpointer object, gpointer klass)
{
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER (object);
	CamelFolder *folder = CAMEL_FOLDER (object);


	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN;

	folder->folder_flags = CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY | CAMEL_FOLDER_HAS_SEARCH_CAPABILITY;

	mapi_folder->priv = g_malloc0 (sizeof(*mapi_folder->priv));

#ifdef ENABLE_THREADS
	g_static_mutex_init(&mapi_folder->priv->search_lock);
	g_static_rec_mutex_init(&mapi_folder->priv->cache_lock);
#endif

	mapi_folder->need_rescan = TRUE;
}

CamelType
camel_mapi_folder_get_type (void)
{
	static CamelType camel_mapi_folder_type = CAMEL_INVALID_TYPE;


	if (camel_mapi_folder_type == CAMEL_INVALID_TYPE) {
		camel_mapi_folder_type =
			camel_type_register (camel_offline_folder_get_type (),
					"CamelMapiFolder",
					sizeof (CamelMapiFolder),
					sizeof (CamelMapiFolderClass),
					(CamelObjectClassInitFunc) camel_mapi_folder_class_init,
					NULL,
					(CamelObjectInitFunc) camel_mapi_folder_init,
					(CamelObjectFinalizeFunc) camel_mapi_folder_finalize);
	}

	return camel_mapi_folder_type;
}

CamelFolder *
camel_mapi_folder_new(CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex)
{

	CamelFolder	*folder = NULL;
	CamelMapiFolder *mapi_folder;
	char *summary_file, *state_file, *journal_file;
	char *short_name;


	folder = CAMEL_FOLDER (camel_object_new(camel_mapi_folder_get_type ()) );

	mapi_folder = CAMEL_MAPI_FOLDER(folder);
	short_name = strrchr (folder_name, '/');
	if (short_name)
		short_name++;
	else
		short_name = (char *) folder_name;
	camel_folder_construct (folder, store, folder_name, short_name);

	summary_file = g_strdup_printf ("%s-%s/summary",folder_name, "dir");
	folder->summary = camel_mapi_summary_new(folder, summary_file);
	g_free(summary_file);

	if (!folder->summary) {
		camel_object_unref (CAMEL_OBJECT (folder));
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				_("Could not load summary for %s"),
				folder_name);
		return NULL;
	}

	/* set/load persistent state */
	state_file = g_strdup_printf ("%s/cmeta", g_strdup_printf ("%s-%s",folder_name, "dir"));
	camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state_file, NULL);
	g_free(state_file);
	camel_object_state_read(folder);

	mapi_folder->cache = camel_data_cache_new (g_strdup_printf ("%s-%s",folder_name, "dir"),0 ,ex);
	if (!mapi_folder->cache) {
		camel_object_unref (folder);
		return NULL;
	}

/* 	journal_file = g_strdup_printf ("%s/journal", g_strdup_printf ("%s-%s",folder_name, "dir")); */
/* 	mapi_folder->journal = camel_mapi_journal_new (mapi_folder, journal_file); */
/* 	g_free (journal_file); */
/* 	if (!mapi_folder->journal) { */
/* 		camel_object_unref (folder); */
/* 		return NULL; */
/* 	} */

	if (!strcmp (folder_name, "Mailbox")) {
		if (camel_url_get_param (((CamelService *) store)->url, "filter"))
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;
	}

	mapi_folder->search = camel_folder_search_new ();
	if (!mapi_folder->search) {
		camel_object_unref (folder);
		return NULL;
	}

	return folder;

/* 	char		*name; */
/* 	CamelOpenchangeFolderInfo *fi; */

/* 	fi = (CamelOpenchangeFolderInfo *)((CamelMapiStore *)parent_store)->fi; */
/* 	while (fi && strcmp(((CamelFolderInfo *)fi)->full_name, full_name) != 0){ */
/* 		fi = (CamelOpenchangeFolderInfo *)((CamelFolderInfo *)fi)->next; */
/* 	} */
/* 	if (!fi) return NULL; */
/* 	name = g_strdup(full_name); */
/* 	folder = (CamelFolder *)camel_object_new(camel_openchange_folder_get_type()); */
/* 	((CamelOpenchangeFolder *)folder)->folder_id = fi->fid; */
/* 	camel_folder_construct(folder, parent_store, camel_url_encode((full_name), NULL), name); */
/* 	openchange_construct_summary(folder, ex, fi->file_name); */
/* 	folder->parent_store = parent_store; */
/* 	return (folder); */
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

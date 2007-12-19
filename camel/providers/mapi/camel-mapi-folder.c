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
#define d(x) x

static CamelOfflineFolderClass *parent_class = NULL;

struct _CamelMapiFolderPrivate {
	//FIXME : ??
	GSList *item_list;
#ifdef ENABLE_THREADS
	GStaticMutex search_lock;	/* for locking the search object */
	GStaticRecMutex cache_lock;	/* for locking the cache object */
#endif

};

static CamelMimeMessage *mapi_folder_item_to_msg( CamelFolder *folder, MapiItem *item, CamelException *ex );


CamelMessageContentInfo *get_content(CamelMessageInfoBase *mi)
{
	return (NULL);
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
/* 		camel_store_summary_save ((CamelStoreSummary *)((CamelMapiStore *)folder->parent_store)->summary); */
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
	else if (!strcmp (type, IPF_STICKYNOTE))
		item_type = MAPI_FOLDER_TYPE_MEMO;
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
fetch_items_cb (struct mapi_SPropValue_array *array, const mapi_id_t fid, const mapi_id_t mid, GSList *recipients, GSList *attachments, gpointer data)
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
			mi = (CamelMapiMessageInfo *)camel_message_info_new (folder->summary); 
			if (mi->info.content == NULL) {
				mi->info.content = camel_folder_summary_content_info_new (folder->summary);
				mi->info.content->type = camel_content_type_new ("multipart", "mixed");	
			}
		}
		
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

/* 		if (mapi_item_has_attachment (item)) */
/* 			mi->info.flags |= CAMEL_MESSAGE_ATTACHMENTS; */

		if (!exists) {
			mi->info.uid = g_strdup (exchange_mapi_util_mapi_ids_to_uid(item->fid, item->mid));
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
		t_cache_stream  = camel_data_cache_get (mapi_folder->cache, "cache", mi->info.uid, ex);
		if (t_cache_stream) {
			camel_object_unref (t_cache_stream);

			mail_msg = mapi_folder_item_to_msg (folder, item, ex);
/* 			if (mail_msg) */
/* 				camel_medium_set_header (CAMEL_MEDIUM (mail_msg), "X-Evolution-Source", groupwise_base_url_lookup (priv)); */

			CAMEL_MAPI_FOLDER_REC_LOCK (folder, cache_lock);
			if ((cache_stream = camel_data_cache_add (mapi_folder->cache, "cache", id, NULL))) {
				if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *) mail_msg, 	cache_stream) == -1 || camel_stream_flush (cache_stream) == -1)
					camel_data_cache_remove (mapi_folder->cache, "cache", id, NULL);
				camel_object_unref (cache_stream);
			}

			camel_object_unref (mail_msg);
			CAMEL_MAPI_FOLDER_REC_UNLOCK (folder, cache_lock);
		}
		/******************** Caching stuff ends *************************/
		i++;
	}
	camel_operation_end (NULL);
	//	g_free (container_id);
	g_string_free (str, TRUE);
	camel_object_trigger_event (folder, "folder_changed", changes);

	camel_folder_change_info_free (changes);
	//TASK 2.

}

static void 
mapi_sync_summary (CamelFolder *folder, CamelException *ex)
{
/* 	camel_folder_summary_save (folder->summary); */
/* 	camel_store_summary_save ((CamelStoreSummary *)((CamelMapiStore *)folder->parent_store)->summary); */
}

void
mapi_refresh_folder(CamelFolder *folder, CamelException *ex)
{
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

	if (!camel_mapi_store_connected (mapi_store, ex))
		goto end1;

		       //	time_string =  g_strdup (((CamelGroupwiseSummary *) folder->summary)->time_string);
		       //	t_str = g_strdup (time_string); 

	/*Get the New Items*/
	if (!is_proxy) {
		char *source;
		struct SPropTagArray *SPropTagArray;
/* 		if ( !strcmp (folder->full_name, RECEIVED) || !strcmp(folder->full_name, SENT) ) { */
/* 			source = NULL; */
/* 		} else { */
/* 			source = "sent received"; */
/* 		} */
		mapi_id_t temp_folder_id;
		TALLOC_CTX *mem_ctx;
		mem_ctx = talloc_init("Evolution");
		exchange_mapi_util_mapi_id_from_string (folder_id, &temp_folder_id);

		SPropTagArray = set_SPropTagArray( mem_ctx, 8,
						   PR_URL_NAME,
						   PR_MESSAGE_SIZE,
						   PR_MESSAGE_DELIVERY_TIME,
						   PR_MESSAGE_FLAGS,
						   PR_SENT_REPRESENTING_NAME,
						   PR_DISPLAY_TO,
						   PR_DISPLAY_CC,
						   PR_DISPLAY_BCC);

		if (!camel_mapi_store_connected (mapi_store, ex)) {
			//TODO : Fix exception string.
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					     _("This message is not available in offline mode."));
			goto end2;
		}

		status = exchange_mapi_connection_fetch_items (temp_folder_id, SPropTagArray, NULL, NULL, fetch_items_cb, folder);

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


		/* summary->time_string = g_strdup (t_str); */
/* 		((CamelGroupwiseSummary *) folder->summary)->time_string = g_strdup (t_str); */
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
fetch_item_cb (struct mapi_SPropValue_array *array, mapi_id_t fid, mapi_id_t mid, GSList *recipients, GSList *attachments)
{
	debug_mapi_property_dump (array);
	long *flags;
	struct FILETIME *delivery_date;
	NTTIME ntdate;

	MapiItem *item = g_new0(MapiItem , 1);

	item->fid = fid;
	item->mid = mid;

	/* FixME : which on of this will fetch the subject. */
	item->header.to = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_TO));
	item->header.cc = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_CC));
	item->header.bcc = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_BCC));
	item->header.from = g_strdup (find_mapi_SPropValue_data (array, PR_SENT_REPRESENTING_NAME));
	item->header.size = *(glong *)(find_mapi_SPropValue_data (array, PR_MESSAGE_SIZE));

	item->msg.body = g_strdup (find_mapi_SPropValue_data (array, PR_BODY));
	printf("%s(%d):%s:item->msg.body : %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, item->msg.body);

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
	if(temp_str) 
		camel_mime_message_set_subject (msg, temp_str);

	recieved_time = item->header.recieved_time;

	int offset = 0;
	time_t actual_time = camel_header_decode_date (ctime(&recieved_time), &offset);
	camel_mime_message_set_date (msg, actual_time, offset);

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
	const char *temp_body = NULL;

	part = camel_mime_part_new ();
	camel_mime_part_set_encoding(part, CAMEL_TRANSFER_ENCODING_8BIT);

/*TODO: type = mapi_item_class_to_type (item); */

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

	MapiItemType type;
	CamelMultipart *multipart = NULL;

	int errno;
	char *body = NULL;
	int body_len = 0;
	const char *uid = NULL;
	CamelStream *temp_stream;

	msg = camel_mime_message_new ();

	multipart = camel_multipart_new ();

	camel_mime_message_set_message_id (msg, uid);
	body = item->msg.body;

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
	CamelMimeMessage *msg = NULL;
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER(folder);
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE(folder->parent_store);
	CamelMapiStorePrivate  *priv = mapi_store->priv;
	CamelMapiMessageInfo *mi = NULL;
	char *folder_id;
	CamelStream *stream, *cache_stream;
	int errno;

	/* see if it is there in cache */

	printf("%s(%d):%s:uid : %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, uid);
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
	if (!camel_mapi_store_connected (mapi_store, ex)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				_("This message is not available in offline mode."));
		camel_message_info_free (&mi->info);
		return NULL;
	}

	mapi_id_t id_folder;
	mapi_id_t id_message;
	MapiItem *item;

	exchange_mapi_util_mapi_ids_from_uid (uid, &id_folder, &id_message);

	folder_id =  g_strdup (camel_mapi_store_folder_id_lookup (mapi_store, folder->full_name)) ;
	item = exchange_mapi_connection_fetch_item (id_folder, id_message, NULL, fetch_item_cb);

	if (item == NULL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Could not get message"));
		camel_message_info_free (&mi->info);
		return NULL;
	}

	msg = mapi_folder_item_to_msg (folder, item, ex);

	if (!msg) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Could not get message"));
		camel_message_info_free (&mi->info);

		return NULL;
	}

/* 	if (msg) */
/* 		camel_medium_set_header (CAMEL_MEDIUM (msg), "X-Evolution-Source", mapi_base_url_lookup (priv)); */

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
camel_mapi_folder_new(CamelStore *store, const char *folder_name, const char *folder_dir, guint32 flags, CamelException *ex)
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

	summary_file = g_strdup_printf ("%s/%s/summary",folder_dir, folder_name);

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
	state_file = g_strdup_printf ("%s/cmeta", g_strdup_printf ("%s/%s",folder_dir, folder_name));
	camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state_file, NULL);
	g_free(state_file);
	camel_object_state_read(folder);

	mapi_folder->cache = camel_data_cache_new (g_strdup_printf ("%s/%s",folder_dir, folder_name),0 ,ex);
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
}



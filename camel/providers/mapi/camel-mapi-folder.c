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

#include <pthread.h>
#include <string.h>
#include <time.h>

#include <camel/camel-folder-search.h>
#include <camel/camel-mime-part.h>
#include <camel/camel-mime-utils.h>
#include <camel/camel-string-utils.h>
#include <camel/camel-object.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-data-wrapper.h>
#include <camel/camel-multipart.h>
#include <camel/camel-private.h>
#include <camel/camel-stream-buffer.h>
#include <camel/camel-stream-mem.h>

#include <libmapi/libmapi.h>
#include <exchange-mapi-utils.h>

#include "camel-mapi-store.h"
#include "camel-mapi-folder.h"
#include "camel-mapi-private.h"
#include "camel-mapi-summary.h"

#define DEBUG_FN( ) printf("----%u %s\n", (unsigned int)pthread_self(), __FUNCTION__);
#define d(x)

static CamelOfflineFolderClass *parent_class = NULL;

struct _CamelMapiFolderPrivate {
	//FIXME : ??
	GSList *item_list;
#ifdef ENABLE_THREADS
	GStaticMutex search_lock;	/* for locking the search object */
	GStaticRecMutex cache_lock;	/* for locking the cache object */
#endif

};

/*for syncing flags back to server*/
typedef struct {
	guint32 changed;
	guint32 bits;
} flags_diff_t;

static CamelMimeMessage *mapi_folder_item_to_msg( CamelFolder *folder, MapiItem *item, CamelException *ex );

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
	CamelStoreInfo *si;
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
	}
	//#endif

}

/* here, there is no point setting 'out' param */
static gboolean
fetch_items_cb (struct mapi_SPropValue_array *array, const mapi_id_t fid, const mapi_id_t mid, 
		GSList *streams, GSList *recipients, GSList *attachments, gpointer in, gpointer out)
{
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER(in);
	GSList *slist = mapi_folder->priv->item_list;
	long *flags;
	struct FILETIME *delivery_date;
	NTTIME ntdate;

	MapiItem *item = g_new0(MapiItem , 1);

	item->fid = fid;
	item->mid = mid;

	/* FixME : which on of this will fetch the subject. */
	item->header.subject = g_strdup (find_mapi_SPropValue_data (array, PR_NORMALIZED_SUBJECT));
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

	slist = g_slist_append (slist, item);
	mapi_folder->priv->item_list = slist;

	return TRUE;
}

static void
mapi_update_cache (CamelFolder *folder, GSList *list, CamelException *ex, gboolean uid_flag) 
{
	CamelMapiMessageInfo *mi = NULL;
	CamelMessageInfo *pmi = NULL;
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (folder->parent_store);
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER(folder);

	guint32 status_flags = 0;
	CamelFolderChangeInfo *changes = NULL;
	gboolean exists = FALSE;
	GString *str = g_string_new (NULL);
	const gchar *folder_id = NULL;
	GSList *item_list = list;
	int total_items = g_slist_length (item_list), i=0;

	changes = camel_folder_change_info_new ();
	folder_id = camel_mapi_store_folder_id_lookup (mapi_store, folder->full_name);

	if (!folder_id) {
		d(printf("\nERROR - Folder id not present. Cannot refresh info\n"));
		camel_folder_change_info_free (changes);
		return;
	}

	camel_operation_start (NULL, _("Fetching summary information for new messages in %s"), folder->name);

	for ( ; item_list != NULL ; item_list = g_slist_next (item_list) ) {
		MapiItem *temp_item ;
		MapiItem *item;
		guint64 id;
		CamelStream *cache_stream, *t_cache_stream;
		CamelMimeMessage *mail_msg = NULL;

		exists = FALSE;
		status_flags = 0;

		if (uid_flag == FALSE) {
 			temp_item = (MapiItem *)item_list->data;
			id = temp_item->mid;
			item = temp_item;
		}

		//		d(printf("%s(%d): Entering %s: item->mid %016llX || item->fid %016llX\n", __FILE__, __LINE__, __PRETTY_FUNCTION__, item->mid, item->fid));

		//fixme
/* 		} else  */
/* 			id = (char *) item_list->data; */

		camel_operation_progress (NULL, (100*i)/total_items);

		/************************ First populate summary *************************/
		mi = NULL;
		pmi = NULL;
		char *msg_uid = exchange_mapi_util_mapi_ids_to_uid (item->fid, item->mid);
		pmi = camel_folder_summary_uid (folder->summary, msg_uid);

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
		

		mi->info.flags = item->header.flags;

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

			CAMEL_MAPI_FOLDER_REC_LOCK (folder, cache_lock);
			if ((cache_stream = camel_data_cache_add (mapi_folder->cache, "cache", msg_uid, NULL))) {
				if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *) mail_msg, 	cache_stream) == -1 || camel_stream_flush (cache_stream) == -1)
					camel_data_cache_remove (mapi_folder->cache, "cache", msg_uid, NULL);
				camel_object_unref (cache_stream);
			}

			camel_object_unref (mail_msg);
			CAMEL_MAPI_FOLDER_REC_UNLOCK (folder, cache_lock);
		}
		/******************** Caching stuff ends *************************/
		g_free (msg_uid);
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
	camel_folder_summary_save (folder->summary);
	camel_store_summary_save ((CamelStoreSummary *)((CamelMapiStore *)folder->parent_store)->summary);
}

static void
mapi_utils_do_flags_diff (flags_diff_t *diff, guint32 old, guint32 _new)
{
	diff->changed = old ^ _new;
	diff->bits = _new & diff->changed;
}

static void
mapi_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (folder->parent_store);
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER (folder);
	CamelMessageInfo *info = NULL;
	CamelMapiMessageInfo *mapi_info = NULL;

	GSList *read_items = NULL, *unread_items = NULL;
	flags_diff_t diff, unset_flags;
	const char *folder_id;
	mapi_id_t fid;
	int count, i;

	GSList *deleted_items, *deleted_head;
	deleted_items = deleted_head = NULL;

	if (((CamelOfflineStore *) mapi_store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL || 
			((CamelService *)mapi_store)->status == CAMEL_SERVICE_DISCONNECTED) {
		mapi_sync_summary (folder, ex);
		return;
	}

	folder_id =  camel_mapi_store_folder_id_lookup (mapi_store, folder->full_name) ;
	exchange_mapi_util_mapi_id_from_string (folder_id, &fid);

	CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);
	if (!camel_mapi_store_connected (mapi_store, ex)) {
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
		camel_exception_clear (ex);
		return;
	}
	CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);

	count = camel_folder_summary_count (folder->summary);
	CAMEL_MAPI_FOLDER_REC_LOCK (folder, cache_lock);
	for (i=0 ; i < count ; i++) {
		info = camel_folder_summary_index (folder->summary, i);
		mapi_info = (CamelMapiMessageInfo *) info;

		if (mapi_info && (mapi_info->info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED)) {
			const char *uid;
			mapi_id_t *mid = g_new0 (mapi_id_t, 1); /* FIXME : */
			mapi_id_t temp_fid;

			uid = camel_message_info_uid (info);
			guint32 flags= camel_message_info_flags (info);

			/* Why are we getting so much noise here :-/ */
			if (!exchange_mapi_util_mapi_ids_from_uid (uid, &temp_fid, mid))
				continue;

			mapi_utils_do_flags_diff (&diff, mapi_info->server_flags, mapi_info->info.flags);
			mapi_utils_do_flags_diff (&unset_flags, flags, mapi_info->server_flags);

			diff.changed &= folder->permanent_flags;
			if (!diff.changed) {
				camel_message_info_free(info);
				continue;
			} else {
				if (diff.bits & CAMEL_MESSAGE_DELETED) {
					if (diff.bits & CAMEL_MESSAGE_SEEN) 
						read_items = g_slist_prepend (read_items, mid);
					if (deleted_items)
						deleted_items = g_slist_prepend (deleted_items, mid);
					else {
						g_slist_free (deleted_head);
						deleted_head = NULL;
						deleted_head = deleted_items = g_slist_prepend (deleted_items, mid);
					}

					CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);

					}
				}
				
				if (diff.bits & CAMEL_MESSAGE_SEEN) {
					read_items = g_slist_prepend (read_items, mid);
				} else if (unset_flags.bits & CAMEL_MESSAGE_SEEN) {
					unread_items = g_slist_prepend (unread_items, mid);
				}
		}
		camel_message_info_free (info);
	}
	
	CAMEL_MAPI_FOLDER_REC_UNLOCK (folder, cache_lock);

	/* 
	   Sync up the READ changes before deleting the message. 
	   Note that if a message is marked as unread and then deleted,
	   Evo doesnt not take care of it, as I find that scenario to be impractical.
	*/

	if (read_items) {
		CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);
		exchange_mapi_set_flags (0, fid, read_items, 0);
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
		g_slist_free (read_items);
	}

	if (deleted_items) {
		CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);
		exchange_mapi_remove_items(0, fid, deleted_items);
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
	}
	/*Remove them from cache*/
	while (deleted_items) {
		char* deleted_msg_uid = g_strdup_printf ("%016llX%016llX", fid, *(mapi_id_t *)deleted_items->data);

		CAMEL_MAPI_FOLDER_REC_LOCK (folder, cache_lock);
		camel_folder_summary_remove_uid (folder->summary, deleted_msg_uid);
		camel_data_cache_remove(mapi_folder->cache, "cache", deleted_msg_uid, NULL);
		CAMEL_MAPI_FOLDER_REC_UNLOCK (folder, cache_lock);

		deleted_items = g_slist_next (deleted_items);
	}


	if (unread_items) {
		CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);
		/* TODO */
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
		g_slist_free (unread_items);
	}

	if (expunge) {
		CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);
		/* TODO */
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
	}

	CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);
	mapi_sync_summary (folder, ex);
	CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
}


void
mapi_refresh_folder(CamelFolder *folder, CamelException *ex)
{

	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (folder->parent_store);
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER (folder);

	gboolean is_proxy = folder->parent_store->flags & CAMEL_STORE_PROXY;
	gboolean is_locked = TRUE;
	gboolean status;
	GList *list = NULL;
	const gchar *folder_id = NULL;

	const guint32 summary_prop_list[] = {
		PR_NORMALIZED_SUBJECT,
		PR_MESSAGE_SIZE,
		PR_MESSAGE_DELIVERY_TIME,
		PR_MESSAGE_FLAGS,
		PR_SENT_REPRESENTING_NAME,
		PR_DISPLAY_TO,
		PR_DISPLAY_CC,
		PR_DISPLAY_BCC
	};


	/* Sync-up the (un)read changes before getting updates,
	so that the getFolderList will reflect the most recent changes too */
	mapi_sync (folder, FALSE, ex);

	if (((CamelOfflineStore *) mapi_store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_warning ("In offline mode. Cannot refresh!!!\n");
		return;
	}

	//creating a copy
	folder_id = camel_mapi_store_folder_id_lookup (mapi_store, folder->full_name);
	if (!folder_id) {
		d(printf ("\nERROR - Folder id not present. Cannot refresh info for %s\n", folder->full_name));
		return;
	}

	if (camel_folder_is_frozen (folder) ) {
		mapi_folder->need_refresh = TRUE;
	}

	CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);

	if (!camel_mapi_store_connected (mapi_store, ex))
		goto end1;

	/*Get the New Items*/
	if (!is_proxy) {
		mapi_id_t temp_folder_id;
		exchange_mapi_util_mapi_id_from_string (folder_id, &temp_folder_id);

		if (!camel_mapi_store_connected (mapi_store, ex)) {
			//TODO : Fix exception string.
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					     _("This message is not available in offline mode."));
			goto end2;
		}

		status = exchange_mapi_connection_fetch_items (temp_folder_id, summary_prop_list, 
							       G_N_ELEMENTS (summary_prop_list), NULL, NULL, 
							       fetch_items_cb, folder, 0);

		if (!status) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Fetch items failed"));
			goto end2;
		}

		camel_folder_summary_touch (folder->summary);
		mapi_sync_summary (folder, ex);

		if (mapi_folder->priv->item_list) {
			mapi_update_cache (folder, mapi_folder->priv->item_list, ex, FALSE);
		}
	}


	CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
	is_locked = FALSE;

	list = NULL;
end2:
	//TODO:
end1:
	if (is_locked)
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
	return;

}

/* In a fetch_item_callback, the data would be returned with the 'out' param, 
 * while the 'in' param would always be NULL */
static gboolean
fetch_item_cb 	(struct mapi_SPropValue_array *array, mapi_id_t fid, mapi_id_t mid, 
		GSList *streams, GSList *recipients, GSList *attachments, gpointer in, gpointer out)
{
	exchange_mapi_debug_property_dump (array);
	long *flags;
	struct FILETIME *delivery_date;
	NTTIME ntdate;

	MapiItem *item = g_new0(MapiItem , 1);

	item->fid = fid;
	item->mid = mid;

	/* FixME : which on of this will fetch the subject. */
	item->header.subject = g_strdup (find_mapi_SPropValue_data (array, PR_NORMALIZED_SUBJECT));
	item->header.to = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_TO));
	item->header.cc = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_CC));
	item->header.bcc = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_BCC));
	item->header.from = g_strdup (find_mapi_SPropValue_data (array, PR_SENT_REPRESENTING_NAME));
	item->header.size = *(glong *)(find_mapi_SPropValue_data (array, PR_MESSAGE_SIZE));

	item->msg.body_plain_text = g_strdup (find_mapi_SPropValue_data (array, PR_BODY));

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

	//Fetch Attachments here.
	printf("%s(%d):%s:Number of Attachments : %d \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, g_slist_length (attachments));
	item->attachments = attachments;

	out = item; 
	return TRUE;
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
		camel_mime_message_set_reply_to(msg, addr);
		
		/* add from */
		addr = camel_internet_address_new();
		camel_internet_address_add(addr, item->header.from, item->header.from);
		camel_mime_message_set_from(msg, addr);
	}
}


static void
mapi_populate_msg_body_from_item (CamelMultipart *multipart, MapiItem *item, char *body)
{
	CamelMimePart *part;

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
	CamelMultipart *multipart = NULL;

	GSList *attach_list = NULL;
	int errno;
	char *body = NULL;
	const char *uid = NULL;

	attach_list = item->attachments;

	msg = camel_mime_message_new ();

	multipart = camel_multipart_new ();

	camel_mime_message_set_message_id (msg, uid);
	body = item->msg.body_plain_text;

	mapi_populate_msg_body_from_item (multipart, item, body);
	/*Set recipient details*/
	mapi_msg_set_recipient_list (msg, item);
	mapi_populate_details_from_item (msg, item);

	if (attach_list) {
		GSList *al = attach_list;
		for (al = attach_list; al != NULL; al = al->next) {
			ExchangeMAPIAttachment *attach = (ExchangeMAPIAttachment *)al->data;
			CamelMimePart *part;

			printf("%s(%d):%s:Attachment --\n\tFileName : %s \n\tMIME Tag : %s\n\tLength : %d\n",
			       __FILE__, __LINE__, __PRETTY_FUNCTION__, 
				 attach->filename, attach->mime_type, attach->value->len );

			if (attach->value->len <= 0) {
				continue;
			}
			part = camel_mime_part_new ();

			camel_mime_part_set_filename(part, g_strdup(attach->filename));
			//Auto generate content-id
			camel_mime_part_set_content_id (part, NULL);
			camel_mime_part_set_content(part, attach->value->data, attach->value->len, attach->mime_type);
			camel_content_type_set_param (((CamelDataWrapper *) part)->mime_type, "name", attach->filename);

			camel_multipart_set_boundary(multipart, NULL);
			camel_multipart_add_part (multipart, part);
			camel_object_unref (part);
			
		}
		exchange_mapi_util_free_attachment_list (&attach_list);
	}

	camel_medium_set_content_object(CAMEL_MEDIUM (msg), CAMEL_DATA_WRAPPER(multipart));
	camel_object_unref (multipart);

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
	item = exchange_mapi_connection_fetch_item (id_folder, id_message, NULL, 0, 
						    NULL, fetch_item_cb, NULL, 
						    MAPI_OPTIONS_FETCH_ATTACHMENTS | MAPI_OPTIONS_FETCH_BODY_STREAM | MAPI_OPTIONS_FETCH_BODY_STREAM);

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
}


static CamelMessageInfo*
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
	camel_folder_class->sync = mapi_sync;
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
	char *summary_file, *state_file;
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



/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-groupwise-folder.c: class for an groupwise folder */

/* 
 * Authors:
 *  Sivaiah Nallagatla <snallagatla@novell.com>
 *  parthasarathi susarla <sparthasarathi@novell.com>
 *   
 *
 * Copyright (C) 2004, Novell Inc.
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
#include <errno.h>
#include <time.h>
#include <libedataserver/e-msgport.h>
#include "camel-groupwise-folder.h"
#include "camel-groupwise-store.h"
#include "camel-folder.h"
#include "camel-folder-search.h"
#include "camel-groupwise-store.h"
#include "camel-groupwise-summary.h"
#include "camel-groupwise-utils.h"
#include "camel-i18n.h" 
#include "camel-private.h"
#include "camel-groupwise-private.h"
#include "camel-stream-mem.h"
#include <e-gw-connection.h>
#include <e-gw-item.h>

#include <string.h>

static CamelObjectClass *parent_class = NULL;
static CamelDiscoFolderClass *disco_folder_class = NULL ;

struct _CamelGroupwiseFolderPrivate {

#ifdef ENABLE_THREADS
	EMutex *search_lock;    // for locking the search object 
	EMutex *cache_lock;     // for locking the cache object 
#endif

};

/*prototypes*/
void groupwise_transfer_online ( CamelFolder *source, 
				      GPtrArray *uids, 
				      CamelFolder *destination, 
				      GPtrArray **transferred_uids, 
				      gboolean delete_originals, 
				      CamelException *ex) ;

static int gw_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args) ;
void convert_to_calendar (EGwItem *item, char **str, int *len) ;

#define d(x) x

static CamelMimeMessage *
groupwise_folder_get_message( CamelFolder *folder,
			       const char *uid,
			       CamelException *ex )
{
	CamelMimeMessage *msg = NULL ;
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER(folder) ;
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE(folder->parent_store) ;
	CamelGroupwiseStorePrivate  *priv = gw_store->priv;
	CamelGroupwiseMessageInfo *mi = NULL ;
	char *temp_name, *folder_name, *container_id, *body ;
	CamelInternetAddress *from_addr, *to_addr, *cc_addr, *bcc_addr ;

	GSList *recipient_list, *attach_list ;
	
	EGwItemOrganizer *org ;
	EGwItemType type ;
	EGwConnectionStatus status ;
	EGwConnection *cnc ;
	EGwItem *item ;
	char *dtstring ;
	CamelStream *stream, *cache_stream;
	CamelMultipart *multipart ;
	int errno;
	
	/* see if it is there in cache */
	CAMEL_SERVICE_LOCK (folder->parent_store, connect_lock);

	mi = (CamelGroupwiseMessageInfo *) camel_folder_summary_uid (folder->summary, uid);
	if (mi == NULL) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     _("Cannot get message: %s\n  %s"), uid, _("No such message"));
		CAMEL_SERVICE_UNLOCK (folder->parent_store, connect_lock);
		return NULL;
	}

	CAMEL_GROUPWISE_FOLDER_LOCK (folder, cache_lock);
	cache_stream  = camel_data_cache_get (gw_folder->cache, "cache", uid, ex);
	stream = camel_stream_mem_new ();
	if (cache_stream) {
		msg = camel_mime_message_new ();
		camel_stream_reset (stream);
		camel_stream_write_to_stream (cache_stream, stream);
		;
		camel_stream_reset (stream);
		if (camel_data_wrapper_construct_from_stream ((CamelDataWrapper *) msg, stream) == -1) {
			if (errno == EINTR) {
				camel_exception_setv (ex, CAMEL_EXCEPTION_USER_CANCEL, _("User cancelled"));
				camel_object_unref (msg);
				camel_object_unref (stream);
				return NULL;
			} else {
				camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot get message %s: %s"),
						      uid, g_strerror (errno));
				camel_object_unref (msg);
				msg = NULL;
			}
		}
			
	
		camel_object_unref (stream);
		camel_object_unref (cache_stream);
		
	}
	CAMEL_GROUPWISE_FOLDER_UNLOCK (folder, cache_lock);
	if (msg != NULL) {
		CAMEL_SERVICE_UNLOCK (folder->parent_store, connect_lock);
		return msg;
	}
	if (camel_disco_store_status (CAMEL_DISCO_STORE (gw_store)) == CAMEL_DISCO_STORE_OFFLINE) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				     _("This message is not available in offline mode."));
		CAMEL_SERVICE_UNLOCK (folder->parent_store, connect_lock);
		return NULL;
	}
		
	folder_name = g_strdup(folder->name) ;
	temp_name = strrchr (folder_name,'/') ;
	if(temp_name == NULL) {
		container_id =  g_strdup (container_id_lookup (priv,g_strdup(folder_name))) ;
	}
	else {
		temp_name++ ;
		container_id =  g_strdup (container_id_lookup (priv,g_strdup(temp_name))) ;
	}

	
	/*Create and populate the MIME Message structure*/
	msg = camel_mime_message_new () ;

	multipart = camel_multipart_new () ;


	cnc = cnc_lookup (priv) ;

	status = e_gw_connection_get_item (cnc, container_id, uid, "default distribution recipient message attachments subject notification created", &item) ;
	if (status != E_GW_CONNECTION_STATUS_OK) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Could not get message"));
		return NULL;
	}
	
	type = e_gw_item_get_item_type(item) ;
	if (type == E_GW_ITEM_TYPE_UNKNOWN) {
		/*XXX: Free memory allocations*/
 		return NULL ;
	} else if (type == E_GW_ITEM_TYPE_NOTIFICATION) {
		camel_medium_add_header ( CAMEL_MEDIUM (msg), "X-Notification", "shared-folder") ;
	} else if (type == E_GW_ITEM_TYPE_MAIL) {
 	}
	
	org = e_gw_item_get_organizer (item) ;
	recipient_list = e_gw_item_get_recipient_list (item) ;

	/*Addresses*/
	from_addr = camel_internet_address_new () ;
	to_addr = camel_internet_address_new () ;
	cc_addr = camel_internet_address_new () ;
	bcc_addr = camel_internet_address_new () ;

	if (recipient_list) {
		GSList *rl ;
		
		for (rl = recipient_list ; rl != NULL ; rl = rl->next) {
			EGwItemRecipient *recp = (EGwItemRecipient *) rl->data;

			if (recp->type == E_GW_ITEM_RECIPIENT_TO) {
				camel_internet_address_add (to_addr, recp->display_name, recp->email ) ;
				
			} else if (recp->type == E_GW_ITEM_RECIPIENT_CC) {
				camel_internet_address_add (cc_addr, recp->display_name, recp->email ) ;
				
			} else if (recp->type == E_GW_ITEM_RECIPIENT_BC) {
				camel_internet_address_add (bcc_addr, recp->display_name, recp->email ) ;

			}
		}
		
		camel_mime_message_set_recipients (msg, "To", to_addr) ;
		camel_mime_message_set_recipients (msg, "Cc", cc_addr) ;
		camel_mime_message_set_recipients (msg, "Bcc", bcc_addr) ;
	}
	if (org)
		camel_internet_address_add (from_addr,org->display_name,org->email) ;
	

	/*Content and content-type*/
	body = g_strdup(e_gw_item_get_message(item));
	if (body) {
		CamelMimePart *part ;
		part = camel_mime_part_new () ;

		camel_mime_part_set_encoding(part, CAMEL_TRANSFER_ENCODING_8BIT);
		camel_multipart_set_boundary (multipart, NULL);
		if (type == E_GW_ITEM_TYPE_APPOINTMENT) {
			char *cal_buffer = NULL ;
			int len ;
			convert_to_calendar (item, &cal_buffer, &len) ;
			camel_mime_part_set_content(part, cal_buffer, len, "text/calendar") ;
		} else
			camel_mime_part_set_content(part, body, strlen(body), e_gw_item_get_msg_content_type (item)) ;
		camel_multipart_add_part (multipart, part) ;
		camel_object_unref (part) ;

	}else {
		CamelMimePart *part ;
		part = camel_mime_part_new () ;
		camel_multipart_set_boundary (multipart, NULL);
		camel_mime_part_set_encoding(part, CAMEL_TRANSFER_ENCODING_8BIT);
		if (type == E_GW_ITEM_TYPE_APPOINTMENT) {
			camel_mime_part_set_content(part, " ", strlen(" "),"text/calendar") ;
		} else
			camel_mime_part_set_content(part, " ", strlen(" "),"text/html") ;
		camel_multipart_add_part (multipart, part) ;
		camel_object_unref (part) ;
	}
	
	camel_mime_message_set_subject (msg, e_gw_item_get_subject(item) ) ;
	dtstring = e_gw_item_get_creation_date (item) ;
	if (dtstring) 
		camel_mime_message_set_date (msg, e_gw_connection_get_date_from_string (dtstring), 0 ) ;
	camel_mime_message_set_from (msg, from_addr) ;
	

	/* Attachments
	 * XXX:Free attach list
	 */
	attach_list = e_gw_item_get_attach_id_list (item) ;
	if (attach_list) {
		GSList *al ;

		for (al = attach_list ; al != NULL ; al = al->next) {
			EGwItemAttachment *attach = (EGwItemAttachment *)al->data ;
			unsigned char *attachment ;
			int len ;
			CamelMimePart *part ;

			status = e_gw_connection_get_attachment (cnc, g_strdup(attach->id), 0, -1, (unsigned char **)&attachment, &len) ;
			if (status != E_GW_CONNECTION_STATUS_OK) {
				camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Could not get message"));
				return NULL;
			}
			if (attach && (len !=0) ) {
				part = camel_mime_part_new () ;

				camel_data_wrapper_set_mime_type(CAMEL_DATA_WRAPPER (multipart), "multipart/digest") ;
				camel_multipart_set_boundary(multipart, NULL);

				camel_mime_part_set_filename(part, g_strdup(attach->name)) ;
				camel_mime_part_set_content(part, attachment, len, attach->contentType) ;

				camel_multipart_add_part (multipart, part) ;

				camel_object_unref (part) ;
			}
			g_free (attachment) ;
		}
		

	}
	camel_medium_set_content_object(CAMEL_MEDIUM (msg), CAMEL_DATA_WRAPPER(multipart));

	camel_object_unref (multipart) ;

	g_object_unref (item) ;
	
	if (body)
		g_free (body) ;
	/* add to cache */
	CAMEL_GROUPWISE_FOLDER_LOCK (folder, cache_lock);
	if ((cache_stream = camel_data_cache_add (gw_folder->cache, "cache", uid, NULL))) {
		if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *) msg, cache_stream) == -1
		    || camel_stream_flush (cache_stream) == -1)
			camel_data_cache_remove (gw_folder->cache, "cache", uid, NULL);
		camel_object_unref (cache_stream);
	}
	CAMEL_GROUPWISE_FOLDER_UNLOCK (folder, cache_lock);
	CAMEL_SERVICE_UNLOCK (folder->parent_store, connect_lock);

	return msg ;

}


static void
groupwise_folder_rename (CamelFolder *folder, const char *new)
{
	CamelGroupwiseFolder *gw_folder = (CamelGroupwiseFolder *)folder ;
	CamelGroupwiseStore *gw_store = (CamelGroupwiseStore *) folder->parent_store ;
	CamelGroupwiseStorePrivate *priv = gw_store->priv ;

	char *folder_dir, *summary_path, *state_file, *storage_path = storage_path_lookup (priv) ;
	char *folders ;

	folders = g_strconcat (storage_path, "/folders", NULL) ;
	folder_dir = e_path_to_physical (folders, new) ;
	g_free (folders) ;

	summary_path = g_strdup_printf ("%s/summary", folder_dir) ;

	CAMEL_GROUPWISE_FOLDER_LOCK (folder, cache_lock) ;
	g_free (gw_folder->cache->path) ;
	gw_folder->cache->path = g_strdup (folder_dir) ;
	CAMEL_GROUPWISE_FOLDER_UNLOCK (folder, cache_lock) ;

	camel_folder_summary_set_filename (folder->summary, summary_path) ;

	state_file = g_strdup_printf ("%s/cmeta", folder_dir) ;
	camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state_file, NULL) ;
	g_free (state_file) ;

	g_free (summary_path) ;
	g_free (folder_dir) ;

	 ((CamelFolderClass *)disco_folder_class)->rename(folder, new);
}

static GPtrArray *
groupwise_folder_search_by_expression (CamelFolder *folder, const char *expression, CamelException *ex)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER(folder) ;
	GPtrArray *matches ;
	
	CAMEL_GROUPWISE_FOLDER_LOCK(folder, search_lock);
	camel_folder_search_set_folder (gw_folder->search, folder);
	matches = camel_folder_search_search(gw_folder->search, expression, NULL, ex);
	CAMEL_GROUPWISE_FOLDER_UNLOCK(gw_folder, search_lock);
	
	return matches ;
}

static GPtrArray *
groupwise_folder_search_by_uids(CamelFolder *folder, const char *expression, GPtrArray *uids, CamelException *ex)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER(folder) ;
	GPtrArray *matches ;

	if (uids->len == 0)
		return g_ptr_array_new() ;
	
	CAMEL_GROUPWISE_FOLDER_LOCK(folder, search_lock);

	camel_folder_search_set_folder(gw_folder->search, folder);
	matches = camel_folder_search_search(gw_folder->search, expression, uids, ex);

	CAMEL_GROUPWISE_FOLDER_UNLOCK(gw_folder, search_lock);

	return matches ;
}

static void
groupwise_folder_search_free (CamelFolder *folder, GPtrArray *uids)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER(folder) ;
	
	g_return_if_fail (gw_folder->search);

	CAMEL_GROUPWISE_FOLDER_LOCK(folder, search_lock);

	camel_folder_search_free_result (gw_folder->search, uids);

	CAMEL_GROUPWISE_FOLDER_UNLOCK(gw_folder, search_lock);
	
}


static void
groupwise_sync_offline (CamelFolder *folder, CamelException *ex)
{
	camel_folder_summary_save (folder->summary);
}



CamelFolder *
camel_gw_folder_new(CamelStore *store, const char *folder_name, const char *folder_dir, CamelException *ex) 
{
	CamelFolder *folder ;
	CamelGroupwiseFolder *gw_folder ;
	char *summary_file, *state_file ;
	char *short_name;


	folder = CAMEL_FOLDER (camel_object_new(camel_groupwise_folder_get_type ()) ) ;

	gw_folder = CAMEL_GROUPWISE_FOLDER(folder) ;
	short_name = strrchr (folder_name, '/');
	if (short_name)
		short_name++;
	else
		short_name = (char *) folder_name;
	camel_folder_construct (folder, store, folder_name, short_name) ;

	summary_file = g_strdup_printf ("%s/summary",folder_dir) ;
	folder->summary = camel_groupwise_summary_new(folder, summary_file) ;
	//	camel_folder_summary_clear (folder->summary) ;
	g_free(summary_file) ;
	if (!folder->summary) {
		camel_object_unref (CAMEL_OBJECT (folder));
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not load summary for %s"),
				      folder_name);
		return NULL;
	}

	/* set/load persistent state */
	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state_file, NULL);
	g_free(state_file);
	camel_object_state_read(folder);

	//	gw_folder = CAMEL_GROUPWISE_FOLDER (folder) ;
	gw_folder->cache = camel_data_cache_new (folder_dir,0 ,ex) ;
	if (!gw_folder->cache) {
		camel_object_unref (folder) ;
		return NULL ;
	}
	gw_folder->search = camel_folder_search_new ();
	if (!gw_folder->search) {
		camel_object_unref (folder) ;
		return NULL ;
	}
	
	return folder ;
}


static void 
groupwise_refresh_info(CamelFolder *folder, CamelException *ex)
{
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (folder->parent_store) ;
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (folder) ;
	CamelGroupwiseStorePrivate *priv = gw_store->priv ;
	int status ;
	GList *list = NULL;
	char *container_id = NULL ;

	container_id = g_strdup (container_id_lookup(priv, g_strdup (folder->name))) ;
	if (!container_id) {
		g_print ("\nERROR - Container id not present. Cannot refresh info\n") ;
		return ;
	}

	if (camel_disco_store_status (CAMEL_DISCO_STORE (gw_store)) == CAMEL_DISCO_STORE_OFFLINE) {
		g_free (container_id) ;
		return ;
	}
	
	if (camel_folder_is_frozen (folder) ) {
		gw_folder->need_refresh = TRUE ;
	}

	status = e_gw_connection_get_items (cnc_lookup(priv), container_id, "default distribution attachments message subject created recipient notification", NULL, &list) ;
	if (status != E_GW_CONNECTION_STATUS_OK) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Authentication failed"));
		g_free (container_id) ;
		return ;
	}

	CAMEL_SERVICE_LOCK (gw_store, connect_lock);

	if (gw_store->current_folder != folder) {
		gw_store->current_folder = folder ;
	} else if(gw_folder->need_rescan) {
		//	gw_rescan (folder, summary_count, ex) ;
		gw_update_summary (folder, list, ex) ;
	}


	CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);

	g_free (container_id) ;
	return ;
}



void
gw_update_summary ( CamelFolder *folder, GList *item_list,CamelException *ex) 
{
	CamelGroupwiseMessageInfo *mi ;
	GPtrArray *msg ;
	GSList *attach_list ;
	guint32 item_status, status_flags;
	
//	CAMEL_SERVICE_ASSERT_LOCKED (gw_store, connect_lock);

	
	camel_folder_summary_clear (folder->summary) ;

	msg = g_ptr_array_new () ;
	for ( ; item_list != NULL ; item_list = g_list_next (item_list) ) {
		EGwItem *item = (EGwItem *)item_list->data ;
		EGwItemType type ;
		EGwItemOrganizer *org ;
		char *date = NULL, *temp_date = NULL ;
		
		mi = camel_message_info_new (folder->summary) ; 
		if (mi->info.content == NULL) {
			mi->info.content = camel_folder_summary_content_info_new (folder->summary);
			mi->info.content->type = camel_content_type_new ("multipart", "mixed");
		}

		type = e_gw_item_get_item_type (item) ;

		if (type == E_GW_ITEM_TYPE_MAIL) {
			
		} else if (type == E_GW_ITEM_TYPE_APPOINTMENT) {

		} else if (type == E_GW_ITEM_TYPE_TASK) {

		} else if (type == E_GW_ITEM_TYPE_CONTACT) {
			continue ;
		
		} else if (type == E_GW_ITEM_TYPE_UNKNOWN) {
			continue ;

		} else if (type == E_GW_ITEM_TYPE_NOTIFICATION) {
			g_print ("|| Its a shared folder notification ||\n") ;
		}
		status_flags = 0;
		item_status = e_gw_item_get_item_status (item);
		if (item_status & E_GW_ITEM_STAT_READ)
			status_flags |= CAMEL_MESSAGE_SEEN;
		if (item_status & E_GW_ITEM_STAT_DELETED)
			status_flags |= CAMEL_MESSAGE_DELETED;
		if (item_status & E_GW_ITEM_STAT_REPLIED)
			status_flags |= CAMEL_MESSAGE_ANSWERED;
		mi->info.flags |= status_flags;
		
		attach_list = e_gw_item_get_attach_id_list (item) ;
		if (attach_list) 
			mi->info.flags |= CAMEL_MESSAGE_ATTACHMENTS;
		
		org = e_gw_item_get_organizer (item) ; 
		if (org) {
			mi->info.from = g_strconcat(org->display_name,"<",org->email,">",NULL) ;
			mi->info.to = g_strdup(e_gw_item_get_to (item)) ;
		}
		
		temp_date = e_gw_item_get_creation_date(item) ;
		if (temp_date) {
			date = e_gw_connection_format_date_string(temp_date) ;
			mi->info.date_sent = mi->info.date_received = e_gw_connection_get_date_from_string (date) ;
			/*	mi->date_sent = camel_header_decode_date(date,NULL) ;
				mi->date_received = camel_header_decode_date(date,NULL) ;*/
		}
		mi->info.uid = g_strdup (e_gw_item_get_id (item)) ;
		mi->info.subject = g_strdup (e_gw_item_get_subject(item)) ;


		camel_folder_summary_add (folder->summary,(CamelMessageInfo *)mi) ;
		g_ptr_array_add (msg, mi) ;
		g_free(date) ;
	}

	/*	for (seq=0 ; seq<msg->len ; seq++) {
		if ( (mi = msg->pdata[seq]) )
		//camel_folder_summary_info_free(folder->summary, mi);
		camel_folder_info_free(mi);
		} */
	g_ptr_array_free (msg, TRUE) ;


}

static void
groupwise_append_message (CamelFolder *folder, CamelMimeMessage *message,
		const CamelMessageInfo *info, char **appended_uid,
		CamelException *ex)
{
	g_print ("||| Groupwise append online |||\n") ;
}


static void
groupwise_cache_message (CamelDiscoFolder *disco_folder, const char *uid,
			 CamelException *ex)
{
	
	groupwise_folder_get_message (disco_folder, uid, ex);
		
}

static int
uid_compar (const void *va, const void *vb)
{
	const char **sa = (const char **)va, **sb = (const char **)vb;
	unsigned long a, b;

	a = strtoul (*sa, NULL, 10);
	b = strtoul (*sb, NULL, 10);
	if (a < b)
		return -1;
	else if (a == b)
		return 0;
	else
		return 1;
}



void 
groupwise_transfer_online ( CamelFolder *source, GPtrArray *uids, 
			  CamelFolder *destination, GPtrArray **transferred_uids, 
			  gboolean delete_originals, CamelException *ex)
{
	int count, index = 0 ;
	GList *item_ids = NULL, *temp_list = NULL ;
	char *source_container_id = NULL, *dest_container_id = NULL;
	
	CamelGroupwiseStore *gw_store= CAMEL_GROUPWISE_STORE(source->parent_store) ;
	CamelGroupwiseStorePrivate  *priv = gw_store->priv;
	
	EGwConnectionStatus status ;
	EGwConnection *cnc = cnc_lookup (priv) ;
 
 	count = camel_folder_summary_count (destination->summary) ;
 	qsort (uids->pdata, uids->len, sizeof (void *), uid_compar) ;

	while (index < uids->len) {
		item_ids = g_list_append (item_ids, g_ptr_array_index (uids, index));
		temp_list = g_list_append (temp_list, g_ptr_array_index (uids, index));
		index ++;
	}
	
	source_container_id = container_id_lookup (priv, g_strdup (source->name)) ;
	dest_container_id = container_id_lookup (priv, g_strdup (destination->name)) ;
	status = e_gw_connection_add_items (cnc, dest_container_id, item_ids) ;
	if (status != E_GW_CONNECTION_STATUS_OK) {
		g_print ("Warning!! Could not move items\n") ;
		return ;
	}

	if (delete_originals) {
		status = e_gw_connection_remove_items (cnc, source_container_id, temp_list) ;
		if (status != E_GW_CONNECTION_STATUS_OK) {
			g_print ("Warning!! Could not delete items\n") ;
			return ;
		}
	}
	
	if (transferred_uids)
		*transferred_uids = NULL ;
}

static void
groupwise_expunge_uids_online (CamelFolder *folder, GPtrArray *uids, CamelException *ex)
{
	    
	int index = 0, i = 0 ;
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE(folder->parent_store) ;
	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	char *container_id;
	EGwConnection *cnc;
	EGwConnectionStatus status ;
	CamelFolderChangeInfo *changes ;
	GList *item_ids = NULL;
	
	CAMEL_SERVICE_LOCK (groupwise_store, connect_lock);
	
	changes = camel_folder_change_info_new () ;

	cnc = cnc_lookup (priv) ;
	while (index < uids->len) {
		item_ids = g_list_append (item_ids, g_ptr_array_index (uids, index));
		index ++;
	}
	container_id =  g_strdup (container_id_lookup (priv,g_strdup(folder->name))) ;
	status = e_gw_connection_remove_items (cnc, container_id, item_ids);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		g_print ("ERROR!!! Could not delete items\n") ;
		camel_folder_change_info_free (changes) ;
		return ;			
	}
	for (i=0 ;  i<uids->len ; i++)
		camel_folder_change_info_remove_uid (changes, uids->pdata[i]) ;

	camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", changes) ;
	CAMEL_SERVICE_UNLOCK (groupwise_store, connect_lock);
	
	g_free (container_id) ;
	camel_folder_change_info_free (changes) ;
}


static void
camel_groupwise_folder_class_init (CamelGroupwiseFolderClass *camel_groupwise_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_groupwise_folder_class);
	CamelDiscoFolderClass *camel_disco_folder_class = CAMEL_DISCO_FOLDER_CLASS (camel_groupwise_folder_class);
	
	disco_folder_class = CAMEL_DISCO_FOLDER_CLASS (camel_type_get_global_classfuncs (camel_disco_folder_get_type ())) ;

	((CamelObjectClass *)camel_groupwise_folder_class)->getv = gw_getv;

	camel_folder_class->get_message = groupwise_folder_get_message ;
	camel_folder_class->rename = groupwise_folder_rename ;
	camel_folder_class->search_by_expression = groupwise_folder_search_by_expression ;
	camel_folder_class->search_by_uids = groupwise_folder_search_by_uids ; 
	camel_folder_class->search_free = groupwise_folder_search_free ;
	camel_folder_class->append_message = groupwise_append_message ;
	
	camel_disco_folder_class->refresh_info_online = groupwise_refresh_info ;
	//camel_disco_folder_class->sync_online = groupwise_sync_online;
	camel_disco_folder_class->sync_offline = groupwise_sync_offline;
	/* We don't sync flags at resync time: the online code will
	 * deal with it eventually.
	 */
	camel_disco_folder_class->sync_resyncing = groupwise_sync_offline;
	camel_disco_folder_class->expunge_uids_online = groupwise_expunge_uids_online;
	/*camel_disco_folder_class->expunge_uids_offline = groupwise_expunge_uids_offline;
	  camel_disco_folder_class->expunge_uids_resyncing = groupwise_expunge_uids_resyncing;
	  camel_disco_folder_class->append_online = groupwise_append_online;
	 camel_disco_folder_class->append_offline = groupwise_append_offline;
	  camel_disco_folder_class->append_resyncing = groupwise_append_resyncing;*/
	camel_disco_folder_class->transfer_online = groupwise_transfer_online;
	 /* camel_disco_folder_class->transfer_offline = groupwise_transfer_offline;
	  camel_disco_folder_class->transfer_resyncing = groupwise_transfer_resyncing;*/
	camel_disco_folder_class->cache_message = groupwise_cache_message;

	
}

static void
camel_groupwise_folder_init (gpointer object, gpointer klass)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (object);
	CamelFolder *folder = CAMEL_FOLDER (object);

	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN;

	folder->folder_flags = CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY | CAMEL_FOLDER_HAS_SEARCH_CAPABILITY;

	gw_folder->priv = g_malloc0(sizeof(*gw_folder->priv));

	gw_folder->priv->search_lock = e_mutex_new(E_MUTEX_SIMPLE);
	gw_folder->priv->cache_lock = e_mutex_new(E_MUTEX_REC);

	gw_folder->need_rescan = TRUE;


}

static void
camel_groupwise_folder_finalize (CamelObject *object)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (object);
	if (gw_folder->priv)
		g_free(gw_folder->priv) ;
	if (gw_folder->cache)
		camel_object_unref ( CAMEL_OBJECT (gw_folder->cache)) ;
	if (gw_folder->search)
		camel_object_unref (CAMEL_OBJECT (gw_folder->search));

}

CamelType
camel_groupwise_folder_get_type (void)
{
	static CamelType camel_groupwise_folder_type = CAMEL_INVALID_TYPE;
	
	
	if (camel_groupwise_folder_type == CAMEL_INVALID_TYPE) {
		parent_class = camel_disco_folder_get_type () ;
		camel_groupwise_folder_type =
			camel_type_register (CAMEL_DISCO_FOLDER_TYPE, "CamelGroupwiseFolder",
					     sizeof (CamelGroupwiseFolder),
					     sizeof (CamelGroupwiseFolderClass),
					     (CamelObjectClassInitFunc) camel_groupwise_folder_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_groupwise_folder_init,
					     (CamelObjectFinalizeFunc) camel_groupwise_folder_finalize);
	}
	
	return camel_groupwise_folder_type;
}

/*
  static void
  gw_rescan (CamelFolder *folder, int exists, CamelException *ex) 
  {
  CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (folder) ;
  CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (folder->parent_class) ;

  return ;
  }*/

static int
gw_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	CamelFolder *folder = (CamelFolder *)object ;
	int i, count = 0 ;
	guint32 tag ;
	
	for (i=0 ; i<args->argc ; i++) {
		CamelArgGet *arg = &args->argv[i] ;

		tag = arg->tag ;

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

void 
convert_to_calendar (EGwItem *item, char **str, int *len)
{
	EGwItemOrganizer *org = e_gw_item_get_organizer(item) ;

	*str = g_strconcat ("BEGIN:VCALENDAR","\n", NULL) ;
	*str = g_strconcat (*str, "METHOD:REQUEST", "\n", NULL) ;
	*str = g_strconcat (*str, "BEGIN:VEVENT", "\n", NULL) ;
	*str = g_strconcat (*str, "X-GWITEM-TYPE:APPOINTMENT", "\n", NULL) ;
	*str = g_strconcat (*str, "DTSTART:", e_gw_item_get_start_date (item), "\n", NULL) ;
	*str = g_strconcat (*str, "SUMMARY:", e_gw_item_get_subject (item), "\n", NULL) ;
	*str = g_strconcat (*str, "DTSTAMP:", e_gw_item_get_creation_date (item), "\n", NULL) ;
	*str = g_strconcat (*str, "X-GWMESSAGEID:", e_gw_item_get_id(item), "\n", NULL) ;
	*str = g_strconcat (*str, "X-GWRECORDID:", e_gw_item_get_id (item), "\n", NULL) ;
	if (org)
	*str = g_strconcat (*str, "ORGANIZER;CN= ",org->display_name, ";ROLE= CHAIR", "\n", " MAILTO:", org->email, "\n",  NULL) ;
	*str = g_strconcat (*str, "DESCRIPTION:", e_gw_item_get_message(item), "\n", NULL) ;
	*str = g_strconcat (*str, "LOCATION:", e_gw_item_get_place (item), "\n", NULL) ;
	*str = g_strconcat (*str, "UID:", e_gw_item_get_icalid (item), "\n", NULL) ;
	*str = g_strconcat (*str, "END:VEVENT", "\n", NULL) ;
	*str = g_strconcat (*str, "END:VCALENDAR","\n", NULL) ;

	*len = strlen (*str) ;
}


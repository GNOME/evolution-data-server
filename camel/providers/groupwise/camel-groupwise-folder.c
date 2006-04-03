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
 * modify it under the terms of version 2 of the GNU Lesser General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
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
#include "camel-groupwise-journal.h"
#include "camel-groupwise-utils.h"
#include "camel-stream-mem.h"
#include "camel-string-utils.h"
#include "camel-session.h"
#include <e-gw-connection.h>
#include <e-gw-item.h>

#include <libsoup/soup-misc.h>

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define ADD_JUNK_ENTRY 1
#define REMOVE_JUNK_ENTRY -1
#define JUNK_FOLDER "Junk Mail"
#define READ_CURSOR_MAX_IDS 500
#define MAX_ATTACHMENT_SIZE 1*1024*1024   /*In bytes*/

static CamelOfflineFolderClass *parent_class = NULL;

struct _CamelGroupwiseFolderPrivate {

#ifdef ENABLE_THREADS
	EMutex *search_lock;    /* for locking the search object */
	EMutex *cache_lock;     /* for locking the cache object */
#endif

};

/*prototypes*/
static void groupwise_transfer_messages_to (CamelFolder *source, GPtrArray *uids, CamelFolder *destination, GPtrArray **transferred_uids, gboolean delete_originals, CamelException *ex);
static int gw_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args);
void convert_to_calendar (EGwItem *item, char **str, int *len);
static void convert_to_task (EGwItem *item, char **str, int *len);
static void gw_update_all_items ( CamelFolder *folder, GList *item_list, CamelException *ex);
static void groupwise_populate_details_from_item (CamelMimeMessage *msg, EGwItem *item);
static void groupwise_populate_msg_body_from_item (EGwConnection *cnc, CamelMultipart *multipart, EGwItem *item, char *body);
static void groupwise_msg_set_recipient_list (CamelMimeMessage *msg, EGwItem *item);
static void gw_update_cache ( CamelFolder *folder, GList *item_list, CamelException *ex, gboolean uid_flag);
static CamelMimeMessage *groupwise_folder_item_to_msg ( CamelFolder *folder, EGwItem *item, CamelException *ex );


#define d(x) 

static CamelMimeMessage *
groupwise_folder_get_message( CamelFolder *folder, const char *uid, CamelException *ex )
{
	CamelMimeMessage *msg = NULL;
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER(folder);
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE(folder->parent_store);
	CamelGroupwiseStorePrivate  *priv = gw_store->priv;
	CamelGroupwiseMessageInfo *mi = NULL;
	char *container_id;
	EGwConnectionStatus status;
	EGwConnection *cnc;
	EGwItem *item;
	CamelStream *stream, *cache_stream;
	int errno;

	/* see if it is there in cache */

	mi = (CamelGroupwiseMessageInfo *) camel_folder_summary_uid (folder->summary, uid);
	if (mi == NULL) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				_("Cannot get message: %s\n  %s"), uid, _("No such message"));
		return NULL;
	}
	cache_stream  = camel_data_cache_get (gw_folder->cache, "cache", uid, ex);
	stream = camel_stream_mem_new ();
	if (cache_stream) {
		msg = camel_mime_message_new ();
		camel_stream_reset (stream);
		camel_stream_write_to_stream (cache_stream, stream);
		camel_stream_reset (stream);
		if (camel_data_wrapper_construct_from_stream ((CamelDataWrapper *) msg, stream) == -1) {
			if (errno == EINTR) {
				camel_exception_setv (ex, CAMEL_EXCEPTION_USER_CANCEL, _("User cancelled"));
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

	if (((CamelOfflineStore *) gw_store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				_("This message is not available in offline mode."));
		camel_message_info_free (&mi->info);
		return NULL;
	}

	/* Check if we are really offline */
	if (!camel_groupwise_store_connected (gw_store, ex)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				_("This message is not available in offline mode."));
		camel_message_info_free (&mi->info);
		return NULL;
	}

	container_id =  g_strdup (camel_groupwise_store_container_id_lookup (gw_store, folder->full_name)) ;

	cnc = cnc_lookup (priv);
	status = e_gw_connection_get_item (cnc, container_id, uid, "peek default distribution recipient message attachments subject notification created recipientStatus status", &item);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		g_free (container_id);
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Could not get message"));
		camel_message_info_free (&mi->info);
		return NULL;
	}

	msg = groupwise_folder_item_to_msg (folder, item, ex);
	if (!msg) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Could not get message"));
		g_free (container_id);
		camel_message_info_free (&mi->info);

		return NULL;
	}

	if (msg)
		camel_medium_set_header (CAMEL_MEDIUM (msg), "X-Evolution-Source", groupwise_base_url_lookup (priv));

	if(!strcmp (folder->full_name, "Sent Items")) 
		goto end; /*Dont cache if its sent items, since we need to Track Status*/

	/* add to cache */
	CAMEL_GROUPWISE_FOLDER_LOCK (folder, cache_lock);
	if ((cache_stream = camel_data_cache_add (gw_folder->cache, "cache", uid, NULL))) {
		if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *) msg, cache_stream) == -1
				|| camel_stream_flush (cache_stream) == -1)
			camel_data_cache_remove (gw_folder->cache, "cache", uid, NULL);
		camel_object_unref (cache_stream);
	}

	CAMEL_GROUPWISE_FOLDER_UNLOCK (folder, cache_lock);

end:
	camel_message_info_free (&mi->info);
	g_free (container_id);
	return msg;
}

static void
groupwise_populate_details_from_item (CamelMimeMessage *msg, EGwItem *item)
{
	char *dtstring = NULL;
	char *temp_str = NULL;

	temp_str = (char *)e_gw_item_get_subject(item);
	if(temp_str)
		camel_mime_message_set_subject (msg, temp_str);
	dtstring = e_gw_item_get_delivered_date (item);
	if(dtstring) {
		int offset = 0;
		time_t time = e_gw_connection_get_date_from_string (dtstring);
		time_t actual_time = camel_header_decode_date (ctime(&time), &offset);
		camel_mime_message_set_date (msg, actual_time, offset);
	} else {
		time_t time;
		time_t actual_time;
		int offset = 0;
		dtstring = e_gw_item_get_creation_date (item);
		time = e_gw_connection_get_date_from_string (dtstring);
		actual_time = camel_header_decode_date (ctime(&time), NULL);
		camel_mime_message_set_date (msg, actual_time, offset);
	}
}

static void
groupwise_populate_msg_body_from_item (EGwConnection *cnc, CamelMultipart *multipart, EGwItem *item, char *body)
{
	CamelMimePart *part;
	EGwItemType type;
	const char *temp_body = NULL;

	part = camel_mime_part_new ();
	camel_mime_part_set_encoding(part, CAMEL_TRANSFER_ENCODING_8BIT);

	if (!body) {
		temp_body = e_gw_item_get_message (item);
		if(!temp_body){
			int len = 0;
			EGwConnectionStatus status;
			status = e_gw_connection_get_attachment (cnc, 
					e_gw_item_get_msg_body_id (item), 0, -1, 
					(const char **)&temp_body, &len);
			if (status != E_GW_CONNECTION_STATUS_OK) {
				g_warning ("Could not get Messagebody\n");
			}
		}
	}

	type = e_gw_item_get_item_type (item);
	switch (type) {

		case E_GW_ITEM_TYPE_APPOINTMENT:
		case E_GW_ITEM_TYPE_TASK:
			{
				char *cal_buffer = NULL;
				int len = 0;
				if (type==E_GW_ITEM_TYPE_APPOINTMENT)
					convert_to_calendar (item, &cal_buffer, &len);
				else
					convert_to_task (item, &cal_buffer, &len);
				camel_mime_part_set_content(part, cal_buffer, len, "text/calendar");
				g_free (cal_buffer);
				break;
			}
		case E_GW_ITEM_TYPE_NOTIFICATION:
		case E_GW_ITEM_TYPE_MAIL:
		case E_GW_ITEM_TYPE_NOTE:
			if (body) 
				camel_mime_part_set_content(part, body, strlen(body), "text/html");
			else if (temp_body)
				camel_mime_part_set_content(part, temp_body, strlen(temp_body), e_gw_item_get_msg_content_type (item));
			else
				camel_mime_part_set_content(part, " ", strlen(" "), "text/html");
			break;

		default:
			break;

	}

	camel_multipart_set_boundary (multipart, NULL);
	camel_multipart_add_part (multipart, part);
	camel_object_unref (part);
}

static void
groupwise_msg_set_recipient_list (CamelMimeMessage *msg, EGwItem *item)
{
	GSList *recipient_list;
	EGwItemOrganizer *org;
	struct _camel_header_address *ha;
	char *subs_email;
	struct _camel_header_address *to_list = NULL, *cc_list = NULL, *bcc_list=NULL;

	org = e_gw_item_get_organizer (item);
	recipient_list = e_gw_item_get_recipient_list (item);

	if (recipient_list) {
		GSList *rl;
		char *status_opt = NULL;
		gboolean enabled;

		for (rl = recipient_list ; rl != NULL ; rl = rl->next) {
			EGwItemRecipient *recp = (EGwItemRecipient *) rl->data;
			enabled = recp->status_enabled;

			if (!recp->email) {
				ha=camel_header_address_new_group(recp->display_name);
			} else {
				ha=camel_header_address_new_name(recp->display_name,recp->email);
			}

			if (recp->type == E_GW_ITEM_RECIPIENT_TO) {
				if (recp->status_enabled) 
					status_opt = g_strconcat (status_opt ? status_opt : "" , "TO", ";",NULL);
				camel_header_address_list_append(&to_list, ha);
			} else if (recp->type == E_GW_ITEM_RECIPIENT_CC) {
				if (recp->status_enabled) 
					status_opt = g_strconcat (status_opt ? status_opt : "", "CC", ";",NULL);
				camel_header_address_list_append(&cc_list,ha);

			} else if (recp->type == E_GW_ITEM_RECIPIENT_BC) {
				if (recp->status_enabled) 
					status_opt = g_strconcat (status_opt ? status_opt : "", "BCC", ";",NULL);
				camel_header_address_list_append(&bcc_list,ha);
			} else {
				camel_header_address_unref(ha);
			}
			if (recp->status_enabled) {
				status_opt = g_strconcat (status_opt, 
						recp->display_name,";",
						recp->email,";",
						recp->delivered_date ? recp->delivered_date :  "", ";",
						recp->opened_date ? recp->opened_date : "", ";", 
						recp->accepted_date ? recp->accepted_date : "", ";",
						recp->deleted_date ? recp->deleted_date : "", ";", 
						recp->declined_date ? recp->declined_date : "", ";",
						recp->completed_date ? recp->completed_date : "", ";",
						recp->undelivered_date ? recp->undelivered_date : "", ";", 
						"::", NULL);

			}
		}
		if (enabled) {
			camel_medium_add_header ( CAMEL_MEDIUM (msg), "X-gw-status-opt", (const char *)status_opt);
			g_free (status_opt);
		}
	}

	if(to_list) { 
		subs_email=camel_header_address_list_encode(to_list);
		camel_medium_set_header( CAMEL_MEDIUM(msg), "To", subs_email);
		g_free(subs_email);
		camel_header_address_list_clear(&to_list);
	}

	if(cc_list) { 
		subs_email=camel_header_address_list_encode(cc_list);
		camel_medium_set_header( CAMEL_MEDIUM(msg), "Cc", subs_email);
		g_free(subs_email);
		camel_header_address_list_clear(&cc_list);
	}

	if(bcc_list) { 
		subs_email=camel_header_address_list_encode(bcc_list);
		camel_medium_set_header( CAMEL_MEDIUM(msg), "Bcc", subs_email);
		g_free(subs_email);
		camel_header_address_list_clear(&bcc_list);
	}

	if (org) {
		if (org->display_name && org->email) 
			ha=camel_header_address_new_name(org->display_name,org->email);
		else 
			ha=camel_header_address_new_group(org->display_name);

		subs_email=camel_header_address_list_encode(ha);	
		camel_medium_set_header( CAMEL_MEDIUM(msg), "From", subs_email);
		camel_header_address_unref(ha);
		g_free(subs_email);
	}
}

static void
groupwise_folder_rename (CamelFolder *folder, const char *new)
{
	CamelGroupwiseFolder *gw_folder = (CamelGroupwiseFolder *)folder;
	CamelGroupwiseStore *gw_store = (CamelGroupwiseStore *) folder->parent_store;
	CamelGroupwiseStorePrivate *priv = gw_store->priv;

	char *folder_dir, *summary_path, *state_file, *storage_path = storage_path_lookup (priv);
	char *folders;

	folders = g_strconcat (storage_path, "/folders", NULL);
	folder_dir = e_path_to_physical (folders, new);
	g_free (folders);

	summary_path = g_strdup_printf ("%s/summary", folder_dir);

	CAMEL_GROUPWISE_FOLDER_LOCK (folder, cache_lock);
	g_free (gw_folder->cache->path);
	gw_folder->cache->path = g_strdup (folder_dir);
	CAMEL_GROUPWISE_FOLDER_UNLOCK (folder, cache_lock);

	((CamelFolderClass *)parent_class)->rename(folder, new);
	camel_folder_summary_set_filename (folder->summary, summary_path);

	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state_file, NULL);
	g_free (state_file);

	g_free (summary_path);
	g_free (folder_dir);
}

static GPtrArray *
groupwise_folder_search_by_expression (CamelFolder *folder, const char *expression, CamelException *ex)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER(folder);
	GPtrArray *matches;

	CAMEL_GROUPWISE_FOLDER_LOCK(folder, search_lock);
	camel_folder_search_set_folder (gw_folder->search, folder);
	matches = camel_folder_search_search(gw_folder->search, expression, NULL, ex);
	CAMEL_GROUPWISE_FOLDER_UNLOCK(gw_folder, search_lock);

	return matches;
}

static GPtrArray *
groupwise_folder_search_by_uids(CamelFolder *folder, const char *expression, GPtrArray *uids, CamelException *ex)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER(folder);
	GPtrArray *matches;

	if (uids->len == 0)
		return g_ptr_array_new();

	CAMEL_GROUPWISE_FOLDER_LOCK(folder, search_lock);

	camel_folder_search_set_folder(gw_folder->search, folder);
	matches = camel_folder_search_search(gw_folder->search, expression, uids, ex);

	CAMEL_GROUPWISE_FOLDER_UNLOCK(gw_folder, search_lock);

	return matches;
}

static void
groupwise_folder_search_free (CamelFolder *folder, GPtrArray *uids)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER(folder);

	g_return_if_fail (gw_folder->search);

	CAMEL_GROUPWISE_FOLDER_LOCK(folder, search_lock);

	camel_folder_search_free_result (gw_folder->search, uids);

	CAMEL_GROUPWISE_FOLDER_UNLOCK(gw_folder, search_lock);

}

/******************* functions specific to Junk Mail Handling**************/
static void 
free_node (EGwJunkEntry *entry)
{
	if (entry) {
		g_free (entry->id);
		g_free (entry->match);
		g_free (entry->matchType);
		g_free (entry->lastUsed);
		g_free (entry->modified);
		g_free (entry);
	}
}

static void
update_junk_list (CamelStore *store, CamelMessageInfo *info, int flag)
{
	gchar **email = NULL, *from = NULL;	
	CamelGroupwiseStore *gw_store= CAMEL_GROUPWISE_STORE(store);
	CamelGroupwiseStorePrivate  *priv = gw_store->priv;
	EGwConnection *cnc = cnc_lookup (priv);

	if (!(from = g_strdup (camel_message_info_from (info))))
		goto error;

	email = g_strsplit_set (from, "<>", -1);

	if (!email || !email[1])
		goto error;

	if (flag == ADD_JUNK_ENTRY)
		e_gw_connection_create_junk_entry (cnc, email[1], "email", "junk");
	else if (flag == REMOVE_JUNK_ENTRY) {
		GList *list = NULL;
		EGwJunkEntry *entry;
		if (e_gw_connection_get_junk_entries (cnc, &list)== E_GW_CONNECTION_STATUS_OK){
			while (list) {
				entry = list->data;
				if (!g_ascii_strcasecmp (entry->match, email[1])) { 
					e_gw_connection_remove_junk_entry (cnc, entry->id);
				}
				list = list->next;
			}
			g_list_foreach (list, (GFunc) free_node, NULL);
		}
	}

error:
	g_free (from);
	g_strfreev (email);
}

static void 
move_to_mailbox (CamelFolder *folder, CamelMessageInfo *info, CamelException *ex)
{
	CamelFolder *dest;
	GPtrArray *uids;
	const char *uid = camel_message_info_uid (info);

	uids = g_ptr_array_new ();
	g_ptr_array_add (uids, (gpointer) uid);

	dest = camel_store_get_folder (folder->parent_store, "Mailbox", 0, ex);
	if (dest)
		groupwise_transfer_messages_to (folder, uids, dest, NULL, TRUE, ex);
	else
		g_warning ("No Mailbox folder found");

	update_junk_list (folder->parent_store, info, REMOVE_JUNK_ENTRY);
}

static void 
move_to_junk (CamelFolder *folder, CamelMessageInfo *info, CamelException *ex)
{
	CamelFolder *dest;
	CamelFolderInfo *fi;
	GPtrArray *uids;
	const char *uid = camel_message_info_uid (info);

	uids = g_ptr_array_new ();
	g_ptr_array_add (uids, (gpointer) uid);

	dest = camel_store_get_folder (folder->parent_store, JUNK_FOLDER, 0, ex);
	if (dest)
		groupwise_transfer_messages_to (folder, uids, dest, NULL, TRUE, ex);
	else {
		fi = create_junk_folder (folder->parent_store);
		dest = camel_store_get_folder (folder->parent_store, JUNK_FOLDER, 0, ex);
		if (!dest)
			g_warning ("Could not get JunkFolder:Message not moved");
		else
			groupwise_transfer_messages_to (folder, uids, dest, NULL, TRUE, ex);
	}

	update_junk_list (folder->parent_store, info, ADD_JUNK_ENTRY);

}

/********************* back to folder functions*************************/

static void 
groupwise_sync_summary (CamelFolder *folder, CamelException *ex)
{
	camel_folder_summary_save (folder->summary);
	camel_store_summary_save ((CamelStoreSummary *)((CamelGroupwiseStore *)folder->parent_store)->summary);
}

static void
groupwise_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (folder->parent_store);
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (folder);
	CamelGroupwiseStorePrivate *priv = gw_store->priv;
	CamelMessageInfo *info = NULL;
	CamelGroupwiseMessageInfo *gw_info;
	GList *read_items = NULL;
	flags_diff_t diff;
	const char *container_id;
	EGwConnectionStatus status;
	EGwConnection *cnc;
	int count, i;

	if (((CamelOfflineStore *) gw_store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL || 
			((CamelService *)gw_store)->status == CAMEL_SERVICE_DISCONNECTED) {
		groupwise_sync_summary (folder, ex);
		return;
	}
	cnc = cnc_lookup (priv);

	container_id =  camel_groupwise_store_container_id_lookup (gw_store, folder->full_name) ;

	CAMEL_SERVICE_LOCK (gw_store, connect_lock);
	if (!camel_groupwise_store_connected (gw_store, ex)) {
		CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
		camel_exception_clear (ex);
		return;
	}
	CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);

	count = camel_folder_summary_count (folder->summary);
	CAMEL_GROUPWISE_FOLDER_LOCK (folder, cache_lock);
	for (i=0 ; i <count ; i++) {
		guint32 flags = 0;
		info = camel_folder_summary_index (folder->summary, i);
		gw_info = (CamelGroupwiseMessageInfo *) info;

		/**Junk Mail handling**/
		if(!info)
			continue;
		flags = camel_message_info_flags (info);	

		if ((flags & CAMEL_MESSAGE_JUNK) && !(flags & CAMEL_GW_MESSAGE_JUNK)) /*marked a message junk*/
			move_to_junk (folder, info, ex);

		else if ((flags & CAMEL_MESSAGE_JUNK) && (flags & CAMEL_GW_MESSAGE_JUNK)) /*message was marked as junk, now unjunk*/ 
			move_to_mailbox (folder, info, ex);

		if (gw_info && (gw_info->info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED)) {
			do_flags_diff (&diff, gw_info->server_flags, gw_info->info.flags);
			diff.changed &= folder->permanent_flags;

			/* weed out flag changes that we can't sync to the server */
			if (!diff.changed) {
				camel_message_info_free(info);
				continue;
			} else {
				const char *uid = camel_message_info_uid (info);
				if (diff.changed & CAMEL_MESSAGE_SEEN)
					read_items = g_list_append (read_items, (char *)uid);
				if (diff.changed & CAMEL_MESSAGE_DELETED) {
					CAMEL_SERVICE_LOCK (gw_store, connect_lock);
					status = e_gw_connection_remove_item (cnc, container_id, uid);
					if (status == E_GW_CONNECTION_STATUS_OK) {
						CAMEL_GROUPWISE_FOLDER_LOCK (folder, cache_lock);
						camel_folder_summary_remove (folder->summary, info);
						camel_data_cache_remove(gw_folder->cache, "cache", uid, ex);
						CAMEL_GROUPWISE_FOLDER_UNLOCK (folder, cache_lock);
						i--; count--;
					}
					CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
				}
			}
		}
		camel_message_info_free (info);
	}
	CAMEL_GROUPWISE_FOLDER_UNLOCK (folder, cache_lock);

	if (read_items && g_list_length (read_items)) {
		CAMEL_SERVICE_LOCK (gw_store, connect_lock);
		e_gw_connection_mark_read (cnc, read_items);
		CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
	}

	if (expunge) {
		CAMEL_SERVICE_LOCK (gw_store, connect_lock);
		status = e_gw_connection_purge_deleted_items (cnc);
		if (status == E_GW_CONNECTION_STATUS_OK) {
			g_message ("Purged deleted items in %s", folder->name);
		}
		CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
	}

	CAMEL_SERVICE_LOCK (gw_store, connect_lock);
	groupwise_sync_summary (folder, ex);
	CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
}

CamelFolder *
camel_gw_folder_new(CamelStore *store, const char *folder_name, const char *folder_dir, CamelException *ex) 
{
	CamelFolder *folder;
	CamelGroupwiseFolder *gw_folder;
	char *summary_file, *state_file, *journal_file;
	char *short_name;


	folder = CAMEL_FOLDER (camel_object_new(camel_groupwise_folder_get_type ()) );

	gw_folder = CAMEL_GROUPWISE_FOLDER(folder);
	short_name = strrchr (folder_name, '/');
	if (short_name)
		short_name++;
	else
		short_name = (char *) folder_name;
	camel_folder_construct (folder, store, folder_name, short_name);

	summary_file = g_strdup_printf ("%s/summary",folder_dir);
	folder->summary = camel_groupwise_summary_new(folder, summary_file);
	g_free(summary_file);
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

	gw_folder->cache = camel_data_cache_new (folder_dir,0 ,ex);
	if (!gw_folder->cache) {
		camel_object_unref (folder);
		return NULL;
	}

	journal_file = g_strdup_printf ("%s/journal",folder_dir);
	gw_folder->journal = camel_groupwise_journal_new (gw_folder, journal_file);
	g_free (journal_file);
	if (!gw_folder->journal) {
		camel_object_unref (folder);
		return NULL;
	}

	if (!strcmp (folder_name, "Mailbox")) {
		if (camel_url_get_param (((CamelService *) store)->url, "filter"))
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;
	}

	gw_folder->search = camel_folder_search_new ();
	if (!gw_folder->search) {
		camel_object_unref (folder);
		return NULL;
	}

	return folder;
}

struct _folder_update_msg {
	CamelSessionThreadMsg msg;

	EGwConnection *cnc;
	CamelFolder *folder;
	char *container_id;
	char *t_str;
	GSList *slist;
};

static void
update_update (CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _folder_update_msg *m = (struct _folder_update_msg *)msg;
	EGwConnectionStatus status;
	CamelException *ex = NULL;

	GList *item_list, *items_full_list = NULL, *last_element=NULL;
	int cursor = 0;
	const char *position = E_GW_CURSOR_POSITION_END;
	gboolean done;

	status = e_gw_connection_create_cursor (m->cnc, m->container_id, "id", NULL, &cursor);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		g_warning ("ERROR update update\n");
		return ;
	}

	done = FALSE;
	m->slist = NULL;

	while (!done) {
		item_list = NULL;
		status = e_gw_connection_get_all_mail_uids (m->cnc, m->container_id, cursor, FALSE, READ_CURSOR_MAX_IDS, position, &item_list);
		if (status != E_GW_CONNECTION_STATUS_OK) {
			g_warning ("ERROR update update\n");
			e_gw_connection_destroy_cursor (m->cnc, m->container_id, cursor);
			return;
		}

		if (!item_list  || g_list_length (item_list) == 0)
			done = TRUE;
		else {

			/* item_list is prepended to items_full_list and not the other way
			   because when we have a large number of items say 50000, 
			   for each iteration there will be more elements in items_full_list 
			   and less elements in item_list */

			last_element = g_list_last (item_list);
			if (items_full_list) {
				last_element->next = items_full_list;
				items_full_list->prev = last_element;
			}
			items_full_list = item_list;
		}
		position = E_GW_CURSOR_POSITION_CURRENT;
	}
	e_gw_connection_destroy_cursor (m->cnc, m->container_id, cursor);

	/* Take out only the first part in the list until the @ since it is guaranteed
	   to be unique only until that symbol */

	/*if (items_full_list) {
	  int i;
	  item_list = items_full_list;

	  while (item_list->next) {
	  i = 0;
	  while (((const char *)item_list->data)[i++]!='@');
	  ((char *)item_list->data)[i-1] = '\0';
	  item_list = item_list->next;
	  }

	  i = 0;	
	  while (((const char *)item_list->data)[i++]!='@');
	  ((char *)item_list->data)[i-1] = '\0';
	  }*/

	g_print ("\nNumber of items in the folder: %d \n", g_list_length(items_full_list));
	gw_update_all_items (m->folder, items_full_list, ex);
}

static void
update_free (CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _folder_update_msg *m = (struct _folder_update_msg *)msg;

	g_free (m->t_str);
	g_free (m->container_id);
	camel_object_unref (m->folder);
	camel_folder_thaw (m->folder);
	g_slist_foreach (m->slist, (GFunc) g_free, NULL);
	g_slist_free (m->slist);
	m->slist = NULL;
}

static CamelSessionThreadOps update_ops = {
	update_update,
	update_free,
};

static void
groupwise_refresh_info(CamelFolder *folder, CamelException *ex)
{
	CamelGroupwiseSummary *summary;
	CamelStoreInfo *si;
	summary = (CamelGroupwiseSummary *) folder->summary;
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (folder->parent_store);
	/*
	 * Checking for the summary->time_string here since the first the a
	 * user views a folder, the read cursor is in progress, and the getQM
	 * should not interfere with the process
	 */
	if (summary->time_string && (strlen (summary->time_string) > 0))  {
		groupwise_refresh_folder(folder, ex);
		si = camel_store_summary_path ((CamelStoreSummary *)((CamelGroupwiseStore *)folder->parent_store)->summary, folder->full_name);
		if (si) {
			guint32 unread, total;
			camel_object_get (folder, NULL, CAMEL_FOLDER_TOTAL, &total, CAMEL_FOLDER_UNREAD, &unread, NULL);
			if (si->total != total || si->unread != unread) {
				si->total = total;
				si->unread = unread;
				camel_store_summary_touch ((CamelStoreSummary *)((CamelGroupwiseStore *)folder->parent_store)->summary);
			}
			camel_store_summary_info_free ((CamelStoreSummary *)((CamelGroupwiseStore *)folder->parent_store)->summary, si);
		}
		camel_folder_summary_save (folder->summary);
		camel_store_summary_save ((CamelStoreSummary *)((CamelGroupwiseStore *)folder->parent_store)->summary);
	} else {
		/* We probably could not get the messages the first time. (get_folder) failed???!
		 * so do a get_folder again. And hope that it works
		 */
		g_print("Reloading folder...something wrong with the summary....\n");
		gw_store_reload_folder (gw_store, folder, 0, ex);
	}
}

void
groupwise_refresh_folder(CamelFolder *folder, CamelException *ex)
{
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (folder->parent_store);
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (folder);
	CamelGroupwiseStorePrivate *priv = gw_store->priv;
	CamelGroupwiseSummary *summary = (CamelGroupwiseSummary *)folder->summary;
	EGwConnection *cnc = cnc_lookup (priv);
	CamelSession *session = ((CamelService *)folder->parent_store)->session;
	gboolean is_proxy = folder->parent_store->flags & CAMEL_STORE_PROXY;
	gboolean is_locked = TRUE;
	int status;
	GList *list = NULL;
	GSList *slist = NULL, *sl;
	char *container_id = NULL;
	char *time_string = NULL, *t_str = NULL;
	struct _folder_update_msg *msg;
	gboolean check_all = FALSE;

	if (((CamelOfflineStore *) gw_store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_warning ("In offline mode. Cannot refresh!!!\n");
		return;
	}

	container_id = g_strdup (camel_groupwise_store_container_id_lookup (gw_store, folder->full_name)) ;

	if (!container_id) {
		g_warning ("\nERROR - Container id not present. Cannot refresh info for %s\n", folder->full_name);
		return;
	}

	if (camel_folder_is_frozen (folder) ) {
		gw_folder->need_refresh = TRUE;
	}

	CAMEL_SERVICE_LOCK (gw_store, connect_lock);

	if (!camel_groupwise_store_connected (gw_store, ex)) 
		goto end1;

	if (!strcmp (folder->full_name, "Trash")) {
		status = e_gw_connection_get_items (cnc, container_id, "peek recipient distribution created delivered attachments subject status size", NULL, &list);
		if (status != E_GW_CONNECTION_STATUS_OK) {
			if (status ==E_GW_CONNECTION_STATUS_OTHER) {
				g_warning ("Trash full....Empty Trash!!!!\n");
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

	time_string =  g_strdup (((CamelGroupwiseSummary *) folder->summary)->time_string);
	t_str = g_strdup (time_string); 


	/*Get the New Items*/
	if (!is_proxy) {
		status = e_gw_connection_get_quick_messages (cnc, container_id,
				"peek id",
				&t_str, "New", NULL, NULL, -1, &slist);
		if (status != E_GW_CONNECTION_STATUS_OK) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Authentication failed"));
			goto end2;
		}

		/*
		 * The value in t_str is the one that has to be used for the next set of calls. 
		 * so store this value in the summary.
		 */
		if (summary->time_string)
			g_free (summary->time_string);


		summary->time_string = g_strdup (t_str);
		g_free (t_str);	
		t_str = NULL;

		/*
		   for ( sl = slist ; sl != NULL; sl = sl->next) 
		   list = g_list_append (list, sl->data);*/

		if (slist && g_slist_length(slist) != 0)
			check_all = TRUE;

		g_slist_free (slist);
		slist = NULL;

		t_str = g_strdup (time_string);

		/*Get those items which have been modifed*/

		status = e_gw_connection_get_quick_messages (cnc, container_id,
				"peek id",
				&t_str, "Modified", NULL, NULL, -1, &slist);

		if (status != E_GW_CONNECTION_STATUS_OK) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Authentication failed"));
			goto end3;
		}

		/* The storing of time-stamp to summary code below should be commented if the 
		   above commented code is uncommented */

		/*	if (summary->time_string)
			g_free (summary->time_string);

			summary->time_string = g_strdup (t_str);

			g_free (t_str), t_str = NULL;*/

		for ( sl = slist ; sl != NULL; sl = sl->next) 
			list = g_list_prepend (list, sl->data);

		g_slist_free (slist);
		slist = NULL;

		if (gw_store->current_folder != folder) {
			gw_store->current_folder = folder;
		}

		if (list) {
			gw_update_cache (folder, list, ex, FALSE);
		}
	}


	CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
	is_locked = FALSE;

	/*
	 * The New and Modified items in the server have been updated in the summary. 
	 * Now we have to make sure that all the delted items in the server are deleted
	 * from Evolution as well. So we get the id's of all the items on the sever in 
	 * this folder, and update the summary.
	 */
	/*create a new session thread for the update all operation*/
	if (check_all || is_proxy) {
		msg = camel_session_thread_msg_new (session, &update_ops, sizeof(*msg));
		msg->cnc = cnc;
		msg->t_str = g_strdup (time_string);
		msg->container_id = g_strdup (container_id);
		msg->folder = folder;
		camel_object_ref (folder);
		camel_folder_freeze (folder);
		camel_session_thread_queue (session, &msg->msg, 0);
		/*thread creation and queueing done*/
	}

end3: 
	g_list_foreach (list, (GFunc) g_object_unref, NULL);
	g_list_free (list);
	list = NULL;
end2:
	g_free (t_str);
	g_free (time_string);
	g_free (container_id);
end1:
	if (is_locked)
		CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
	return;
}

static void
gw_update_cache (CamelFolder *folder, GList *list, CamelException *ex, gboolean uid_flag) 
{
	CamelGroupwiseMessageInfo *mi = NULL;
	CamelMessageInfo *pmi = NULL;
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (folder->parent_store);
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER(folder);
	CamelGroupwiseStorePrivate *priv = gw_store->priv;
	EGwConnection *cnc = cnc_lookup (priv);
	GSList *attach_list = NULL;
	guint32 item_status, status_flags = 0;
	CamelFolderChangeInfo *changes = NULL;
	gboolean exists = FALSE;
	GString *str = g_string_new (NULL);
	const char *priority = NULL;
	char *container_id = NULL;
	gboolean is_junk = FALSE;
	EGwItemStatus status;
	GList *item_list = list;
	int total_items = g_list_length (item_list), i=0;

	CAMEL_SERVICE_ASSERT_LOCKED (gw_store, connect_lock);

	changes = camel_folder_change_info_new ();
	container_id = g_strdup (camel_groupwise_store_container_id_lookup (gw_store, folder->full_name));
	if (!container_id) {
		g_warning ("\nERROR - Container id not present. Cannot refresh info\n");
		camel_folder_change_info_free (changes);
		return;
	}

	if (!strcmp (folder->full_name, JUNK_FOLDER)) {
		is_junk = TRUE;
	}

	camel_operation_start (NULL, _("Fetching summary information for new messages in %s"), folder->name);

	for ( ; item_list != NULL ; item_list = g_list_next (item_list) ) {
		EGwItem *temp_item ;
		EGwItem *item;
		EGwItemType type = E_GW_ITEM_TYPE_UNKNOWN;
		EGwItemOrganizer *org;
		char *temp_date = NULL;
		const char *id;
		GSList *recp_list = NULL;
		status_flags = 0;
		CamelStream *cache_stream, *t_cache_stream;
		CamelMimeMessage *mail_msg = NULL;
		gboolean is_sent_folder = FALSE;

		exists = FALSE;

		if (uid_flag == FALSE) {
			temp_item = (EGwItem *)item_list->data;
			id = e_gw_item_get_id (temp_item);
		} else 
			id = (char *) item_list->data;

		camel_operation_progress (NULL, (100*i)/total_items);

		status = e_gw_connection_get_item (cnc, container_id, id, "peek default distribution recipient message attachments subject notification created recipientStatus status size", &item);
		if (status != E_GW_CONNECTION_STATUS_OK) {
			i++;
			continue;
		}

		/************************ First populate summary *************************/
		mi = NULL;
		pmi = NULL;
		pmi = camel_folder_summary_uid (folder->summary, id);
		if (pmi) {
			exists = TRUE;
			camel_message_info_ref (pmi);
			mi = (CamelGroupwiseMessageInfo *)pmi;
		}

		if (!exists) {
			type = e_gw_item_get_item_type (item);
			if ((type == E_GW_ITEM_TYPE_CONTACT) || (type == E_GW_ITEM_TYPE_UNKNOWN)) {
				exists = FALSE;
				continue;
			}

			mi = (CamelGroupwiseMessageInfo *)camel_message_info_new (folder->summary); 
			if (mi->info.content == NULL) {
				mi->info.content = camel_folder_summary_content_info_new (folder->summary);
				mi->info.content->type = camel_content_type_new ("multipart", "mixed");
			}
		}

		/*all items in the Junk Mail folder should have this flag set*/
		if (is_junk)
			mi->info.flags |= CAMEL_GW_MESSAGE_JUNK;

		item_status = e_gw_item_get_item_status (item);
		if (item_status & E_GW_ITEM_STAT_READ)
			status_flags |= CAMEL_MESSAGE_SEEN;
		else 
			mi->info.flags &= ~CAMEL_MESSAGE_SEEN;

		if (item_status & E_GW_ITEM_STAT_REPLIED)
			status_flags |= CAMEL_MESSAGE_ANSWERED;
		if (exists) 
			mi->info.flags |= status_flags;
		else 
			mi->info.flags = status_flags;

		priority = e_gw_item_get_priority (item);
		if (priority && !(g_ascii_strcasecmp (priority,"High"))) {
			mi->info.flags |= CAMEL_MESSAGE_FLAGGED;
		}

		attach_list = e_gw_item_get_attach_id_list (item);
		if (attach_list)  {
			GSList *al = attach_list;
			gboolean has_attachments = TRUE;
			EGwItemAttachment *attach = (EGwItemAttachment *)al->data;

			if (!g_ascii_strcasecmp (attach->name, "Mime.822") ||
					!g_ascii_strcasecmp (attach->name, "TEXT.htm")) 
				has_attachments = FALSE;

			if (has_attachments)
				mi->info.flags |= CAMEL_MESSAGE_ATTACHMENTS;
		}

		org = e_gw_item_get_organizer (item); 
		if (org) {
			GString *str;
			str = g_string_new ("");
			if (org->display_name && org->display_name[0]) 
				str = g_string_append (str, org->display_name);
			if (org->email && org->email[0]) { 
				g_string_append (str, "<");
				str = g_string_append (str, org->email);
				g_string_append (str, ">");
			}
			mi->info.from = camel_pstring_strdup (str->str);
			g_string_free (str, TRUE);
		}
		g_string_truncate (str, 0);
		recp_list = e_gw_item_get_recipient_list (item);
		if (recp_list) {
			GSList *rl;
			int i = 0;
			for (rl = recp_list; rl != NULL; rl = rl->next) {
				EGwItemRecipient *recp = (EGwItemRecipient *) rl->data;
				if (recp->type == E_GW_ITEM_RECIPIENT_TO) {
					if (i)
						str = g_string_append (str, ", ");
					g_string_append_printf (str,"%s <%s>", recp->display_name, recp->email);
					i++;
				}
			}
			if (exists)
				camel_pstring_free(mi->info.to);
			mi->info.to = camel_pstring_strdup (str->str);
			g_string_truncate (str, 0);
		}

		if (type == E_GW_ITEM_TYPE_APPOINTMENT
				|| type ==  E_GW_ITEM_TYPE_NOTE 
				|| type ==  E_GW_ITEM_TYPE_TASK ) {
			temp_date = e_gw_item_get_start_date (item);
			if (temp_date) {
				time_t time = e_gw_connection_get_date_from_string (temp_date);
				time_t actual_time = camel_header_decode_date (ctime(&time), NULL);
				mi->info.date_sent = mi->info.date_received = actual_time;
			}
		} else {
			temp_date = e_gw_item_get_delivered_date(item);
			if (temp_date) {
				time_t time = e_gw_connection_get_date_from_string (temp_date);
				time_t actual_time = camel_header_decode_date (ctime(&time), NULL);
				mi->info.date_sent = mi->info.date_received = actual_time;
			} else {
				time_t time;
				time_t actual_time;
				temp_date = e_gw_item_get_creation_date (item);
				time = e_gw_connection_get_date_from_string (temp_date);
				actual_time = camel_header_decode_date (ctime(&time), NULL);
				mi->info.date_sent = mi->info.date_received = actual_time;
			}
		}

		if (!exists) {
			mi->info.uid = g_strdup (e_gw_item_get_id(item));
			mi->info.size = e_gw_item_get_mail_size (item);	
			mi->info.subject = camel_pstring_strdup(e_gw_item_get_subject(item));
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

		if (!strcmp (folder->full_name, "Sent Items"))
			is_sent_folder = TRUE;
		/******************** Begine Caching ************************/
		/* add to cache if its a new message*/
		t_cache_stream  = camel_data_cache_get (gw_folder->cache, "cache", id, ex);
		if (t_cache_stream && !is_sent_folder) {
			camel_object_unref (t_cache_stream);

			mail_msg = groupwise_folder_item_to_msg (folder, item, ex);
			if (mail_msg)
				camel_medium_set_header (CAMEL_MEDIUM (mail_msg), "X-Evolution-Source", groupwise_base_url_lookup (priv));

			CAMEL_GROUPWISE_FOLDER_LOCK (folder, cache_lock);
			if ((cache_stream = camel_data_cache_add (gw_folder->cache, "cache", id, NULL))) {
				if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *) mail_msg, 	cache_stream) == -1 || camel_stream_flush (cache_stream) == -1)
					camel_data_cache_remove (gw_folder->cache, "cache", id, NULL);
				camel_object_unref (cache_stream);
			}

			camel_object_unref (mail_msg);
			CAMEL_GROUPWISE_FOLDER_UNLOCK (folder, cache_lock);
		}
		/******************** Caching stuff ends *************************/
		i++;
	}
	camel_operation_end (NULL);
	g_free (container_id);
	g_string_free (str, TRUE);
	camel_object_trigger_event (folder, "folder_changed", changes);

	camel_folder_change_info_free (changes);
}

void
gw_update_summary ( CamelFolder *folder, GList *list,CamelException *ex) 
{
	CamelGroupwiseMessageInfo *mi = NULL;
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (folder->parent_store);
	GSList *attach_list = NULL;
	guint32 item_status, status_flags = 0;
	CamelFolderChangeInfo *changes = NULL;
	gboolean exists = FALSE;
	GString *str = g_string_new (NULL);
	const char *priority = NULL;
	char *container_id = NULL;
	gboolean is_junk = FALSE;
	GList *item_list = list;

	/*Assert lock???*/
	changes = camel_folder_change_info_new ();
	container_id = g_strdup (camel_groupwise_store_container_id_lookup (gw_store, folder->full_name));
	if (!container_id) {
		g_warning ("\nERROR - Container id not present. Cannot refresh info\n");
		camel_folder_change_info_free (changes);
		return;
	}

	if (!strcmp (folder->full_name, JUNK_FOLDER)) {
		is_junk = TRUE;
	}

	for (; item_list != NULL ; item_list = g_list_next (item_list) ) {
		EGwItem *item = (EGwItem *)item_list->data;
		EGwItemType type = E_GW_ITEM_TYPE_UNKNOWN;
		EGwItemOrganizer *org;
		char *temp_date = NULL;
		const char *id;
		GSList *recp_list = NULL;
		status_flags = 0;

		id = e_gw_item_get_id (item);

		mi = (CamelGroupwiseMessageInfo *)camel_folder_summary_uid (folder->summary, id);
		if (mi) 
			exists = TRUE;

		if (!exists) {
			type = e_gw_item_get_item_type (item);
			if ((type == E_GW_ITEM_TYPE_CONTACT) || (type == E_GW_ITEM_TYPE_UNKNOWN)) {
				exists = FALSE;
				continue;
			}

			mi = camel_message_info_new (folder->summary); 
			if (mi->info.content == NULL) {
				mi->info.content = camel_folder_summary_content_info_new (folder->summary);
				mi->info.content->type = camel_content_type_new ("multipart", "mixed");
			}
		}

		/*all items in the Junk Mail folder should have this flag set*/
		if (is_junk)
			mi->info.flags |= CAMEL_GW_MESSAGE_JUNK;

		item_status = e_gw_item_get_item_status (item);
		if (item_status & E_GW_ITEM_STAT_READ)
			status_flags |= CAMEL_MESSAGE_SEEN;
		if (item_status & E_GW_ITEM_STAT_REPLIED)
			status_flags |= CAMEL_MESSAGE_ANSWERED;

		if (!strcmp (folder->full_name, "Trash"))
			status_flags |= CAMEL_MESSAGE_SEEN;

		mi->info.flags |= status_flags;


		priority = e_gw_item_get_priority (item);
		if (priority && !(g_ascii_strcasecmp (priority,"High"))) {
			mi->info.flags |= CAMEL_MESSAGE_FLAGGED;
		}

		attach_list = e_gw_item_get_attach_id_list (item);
		if (attach_list)  {
			GSList *al = attach_list;
			gboolean has_attachments = TRUE;
			EGwItemAttachment *attach = (EGwItemAttachment *)al->data;

			if (!g_ascii_strcasecmp (attach->name, "Mime.822") ||
					!g_ascii_strcasecmp (attach->name, "TEXT.htm")) 
				has_attachments = FALSE;

			if (has_attachments)
				mi->info.flags |= CAMEL_MESSAGE_ATTACHMENTS;
		}

		org = e_gw_item_get_organizer (item); 
		if (org) {
			GString *str;
			str = g_string_new ("");
			if (org->display_name && org->display_name[0]) 
				str = g_string_append (str, org->display_name);
			if (org->email && org->email[0]) { 
				g_string_append (str, "<");
				str = g_string_append (str, org->email);
				g_string_append (str, ">");
			}
			mi->info.from = camel_pstring_strdup (str->str);
			g_string_free (str, TRUE);
		}
		g_string_truncate (str, 0);
		recp_list = e_gw_item_get_recipient_list (item);
		if (recp_list) {
			GSList *rl;
			int i = 0;
			for (rl = recp_list; rl != NULL; rl = rl->next) {
				EGwItemRecipient *recp = (EGwItemRecipient *) rl->data;
				if (recp->type == E_GW_ITEM_RECIPIENT_TO) {
					if (i)
						str = g_string_append (str, ", ");
					g_string_append_printf (str,"%s <%s>", recp->display_name, recp->email);
					i++;
				}
			}
			mi->info.to = camel_pstring_strdup (str->str);
			g_string_truncate (str, 0);
		}

		if (type == E_GW_ITEM_TYPE_APPOINTMENT
				|| type ==  E_GW_ITEM_TYPE_NOTE 
				|| type ==  E_GW_ITEM_TYPE_TASK ) {
			temp_date = e_gw_item_get_start_date (item);
			if (temp_date) {
				time_t time = e_gw_connection_get_date_from_string (temp_date);
				time_t actual_time = camel_header_decode_date (ctime(&time), NULL);
				mi->info.date_sent = mi->info.date_received = actual_time;
			} 
		} else {
			temp_date = e_gw_item_get_delivered_date(item);
			if (temp_date) {
				time_t time = e_gw_connection_get_date_from_string (temp_date);
				time_t actual_time = camel_header_decode_date (ctime(&time), NULL);
				mi->info.date_sent = mi->info.date_received = actual_time;
			} else {
				time_t time;
				time_t actual_time;
				temp_date = e_gw_item_get_creation_date (item);
				time = e_gw_connection_get_date_from_string (temp_date);
				actual_time = camel_header_decode_date (ctime(&time), NULL);
				mi->info.date_sent = mi->info.date_received = actual_time;
			}
		}

		mi->info.uid = g_strdup(e_gw_item_get_id(item));
		if (!exists)
			mi->info.size = e_gw_item_get_mail_size (item);	
		mi->info.subject = camel_pstring_strdup(e_gw_item_get_subject(item));

		if (exists) {
			camel_folder_change_info_change_uid (changes, e_gw_item_get_id (item));
			camel_message_info_free (&mi->info);
		} else {
			camel_folder_summary_add (folder->summary,(CamelMessageInfo *)mi);
			camel_folder_change_info_add_uid (changes, mi->info.uid);
			camel_folder_change_info_recent_uid (changes, mi->info.uid);
		}

		exists = FALSE;
	}
	g_free (container_id);
	g_string_free (str, TRUE);
	camel_object_trigger_event (folder, "folder_changed", changes);

	camel_folder_change_info_free (changes);
}

static CamelMimeMessage *
groupwise_folder_item_to_msg( CamelFolder *folder,
		EGwItem *item,
		CamelException *ex )
{
	CamelMimeMessage *msg = NULL;
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE(folder->parent_store);
	CamelGroupwiseStorePrivate  *priv = gw_store->priv;
	const char *container_id = NULL;
	GSList *attach_list = NULL;
	EGwItemType type;
	EGwConnectionStatus status;
	EGwConnection *cnc;
	CamelMultipart *multipart;
	int errno;
	char *body = NULL;
	const char *uid = NULL;
	gboolean is_text_html = FALSE;
	gboolean is_text_html_embed = FALSE;
	gboolean is_base64_encoded = FALSE;

	uid = e_gw_item_get_id(item);
	cnc = cnc_lookup (priv);
	container_id = camel_groupwise_store_container_id_lookup (gw_store, folder->full_name);

	attach_list = e_gw_item_get_attach_id_list (item);
	if (attach_list) {
		//int attach_count = g_slist_length (attach_list);
		GSList *al = attach_list;
		EGwItemAttachment *attach = (EGwItemAttachment *)al->data;
		char *attachment = NULL;
		int len = 0;

		if (!g_ascii_strcasecmp (attach->name, "Mime.822") ||
		    !g_ascii_strcasecmp (attach->name, "TEXT.htm") ||
		    !g_ascii_strcasecmp (attach->name, "Header")) {

			status = e_gw_connection_get_attachment (cnc,
					attach->id, 0, -1,
					(const char **)&attachment, &len);
			if (status != E_GW_CONNECTION_STATUS_OK) {
				g_warning ("Could not get attachment\n");
				camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Could not get message"));
				return NULL;
			}
			if (attachment && attachment[0] && (len !=0) ) {
				if (!g_ascii_strcasecmp (attach->name, "TEXT.htm")) {
					body = g_strdup (attachment);
					g_free (attachment);
					is_text_html = TRUE;
				} 
			}//if attachment and len
		} // if Mime.822 or TEXT.htm
	}//if attach_list


	msg = camel_mime_message_new ();
	multipart = camel_multipart_new ();

	camel_mime_message_set_message_id (msg, uid);
	type = e_gw_item_get_item_type (item);
	if (type == E_GW_ITEM_TYPE_NOTIFICATION)
		camel_medium_add_header ( CAMEL_MEDIUM (msg), "X-Notification", "shared-folder");

	/*If the reply-requested flag is set. Append the mail message with the
	 *          * approprite detail*/
	if (e_gw_item_get_reply_request (item)) {
		char *reply_within;
		const char *mess = e_gw_item_get_message (item);
		char *value;

		reply_within = e_gw_item_get_reply_within (item);
		if (reply_within) {
			time_t t;
			char *temp;

			t = e_gw_connection_get_date_from_string (reply_within);
			temp = ctime (&t);
			temp [strlen (temp)-1] = '\0';
			value = g_strconcat (N_("Reply Requested: by "), temp, "\n\n", mess ? mess : "", NULL);
			e_gw_item_set_message (item, (const char *) value);
			g_free (value);

		} else {
			value = g_strconcat (N_("Reply Requested: When convenient"), "\n\n", mess ? mess : "", NULL);
			e_gw_item_set_message (item, (const char *) value);
			g_free (value);
		}
	}

	groupwise_populate_msg_body_from_item (cnc, multipart, item, body);

	/*Set recipient details*/
	groupwise_msg_set_recipient_list (msg, item);
	groupwise_populate_details_from_item (msg, item);
	/*Now set attachments*/
	if (attach_list) {
		gboolean has_boundary = FALSE;
		GSList *al;

		for (al = attach_list ; al != NULL ; al = al->next) {
			EGwItemAttachment *attach = (EGwItemAttachment *)al->data;
			char *attachment = NULL;
			int len = 0;
			CamelMimePart *part;
			EGwItem *temp_item;
			is_base64_encoded = FALSE;

			if (attach->contentid && (is_text_html_embed != TRUE))
				is_text_html_embed = TRUE;
			if ( !g_ascii_strcasecmp (attach->name, "TEXT.htm") ||
			     !g_ascii_strcasecmp (attach->name, "Mime.822") ||
			     !g_ascii_strcasecmp (attach->name, "Header"))
				continue;

			if ( (attach->item_reference) && (!g_ascii_strcasecmp (attach->item_reference, "1")) ) {
				CamelMimeMessage *temp_msg = NULL;
				status = e_gw_connection_get_item (cnc, container_id, attach->id, "default distribution recipient message attachments subject notification created recipientStatus status startDate", &temp_item);
				if (status != E_GW_CONNECTION_STATUS_OK) {
					g_warning ("Could not get attachment\n");
					continue;
				}
				temp_msg = groupwise_folder_item_to_msg(folder, temp_item, ex);
				if (temp_msg) {
					CamelContentType *ct = camel_content_type_new("message", "rfc822");
					part = camel_mime_part_new ();
					camel_data_wrapper_set_mime_type_field(CAMEL_DATA_WRAPPER (temp_msg), ct);
					camel_content_type_unref(ct);
					camel_medium_set_content_object ( CAMEL_MEDIUM (part),CAMEL_DATA_WRAPPER(temp_msg));
					camel_multipart_add_part (multipart,part);
					camel_object_unref (temp_msg);
					camel_object_unref (part);
				}
				g_object_unref (temp_item);
			} else {
				if (attach->size > MAX_ATTACHMENT_SIZE) {
					long count = 0;
					int i, t_len=0, offset=0, t_offset=0;
					char *t_attach = NULL;
					GString *gstr = g_string_new (NULL);

					count = (attach->size)/(1024*1024);
					count++;
					len = 0;
					for (i = 0; i<count; i++) {
						status = e_gw_connection_get_attachment_base64 (cnc, 
								attach->id, t_offset, MAX_ATTACHMENT_SIZE, 
								(const char **)&t_attach, &t_len, &offset);
						if (status == E_GW_CONNECTION_STATUS_OK) {
							gstr = g_string_append (gstr, t_attach);
							t_offset = offset;
							g_free (t_attach);
							t_attach = NULL;
							t_len = 0;
						}
					}
					attachment = soup_base64_decode (gstr->str, &len);
					g_string_free (gstr, TRUE);
					is_base64_encoded = FALSE;
				} else {
					status = e_gw_connection_get_attachment (cnc, 
							attach->id, 0, -1, 
							(const char **)&attachment, &len);
				}
				if (status != E_GW_CONNECTION_STATUS_OK) {
					g_warning ("Could not get attachment\n");
					continue;
				}
				if (attachment && (len !=0) ) {
					part = camel_mime_part_new ();
					/*multiparts*/
					if (is_text_html_embed) {
						camel_mime_part_set_filename(part, g_strdup(attach->name));
						camel_data_wrapper_set_mime_type(CAMEL_DATA_WRAPPER (multipart), "multipart/related");
						has_boundary = TRUE;
						camel_content_type_set_param(CAMEL_DATA_WRAPPER (multipart)->mime_type, "type", "multipart/alternative");
						if (attach->contentid) {
							gchar **t;
							t= g_strsplit_set (attach->contentid, "<>", -1);
							if (!t[1]) 
								camel_mime_part_set_content_id (part, attach->contentid);
							else 
								camel_mime_part_set_content_id (part, t[1]);
							g_strfreev (t);
							camel_mime_part_set_content_location (part, attach->name);
						}
					} else if (attach->contentType && 
							!strcmp (attach->contentType, "application/pgp-signature")) {
						camel_mime_part_set_filename(part, g_strdup(attach->name));
						camel_data_wrapper_set_mime_type(CAMEL_DATA_WRAPPER (multipart), "multipart/signed");
						has_boundary = TRUE;
						camel_content_type_set_param(CAMEL_DATA_WRAPPER (multipart)->mime_type, "protocol", attach->contentType);
					} else if (attach->contentType &&
							!strcmp (attach->contentType, "application/pgp-encrypted")) {
						camel_mime_part_set_filename(part, g_strdup(attach->name));
						camel_data_wrapper_set_mime_type(CAMEL_DATA_WRAPPER (multipart), "multipart/encrypted");
						has_boundary = TRUE;
						camel_content_type_set_param(CAMEL_DATA_WRAPPER (multipart)->mime_type, "protocol", attach->contentType);
					} else {
						camel_mime_part_set_filename(part, g_strdup(attach->name));
						camel_mime_part_set_content_id (part, attach->contentid);
					}

					//camel_mime_part_set_filename(part, g_strdup(attach->name));
					if (attach->contentType) {
						if (is_base64_encoded)
							camel_mime_part_set_encoding (part, CAMEL_TRANSFER_ENCODING_BASE64);
						camel_mime_part_set_content(part, attachment, len, attach->contentType);
						camel_content_type_set_param (((CamelDataWrapper *) part)->mime_type, "name", attach->name);
					} else {
							camel_mime_part_set_content(part, attachment, len, "text/plain");
					}
					if (!has_boundary)
						camel_data_wrapper_set_mime_type(CAMEL_DATA_WRAPPER (multipart),"multipart/digest");

					camel_multipart_set_boundary(multipart, NULL);
					camel_multipart_add_part (multipart, part);

					camel_object_unref (part);
					g_free (attachment);
				} /* if attachment */
			}
		} /* end of for*/

	}/* if attach_list */
	/********************/

	camel_medium_set_content_object(CAMEL_MEDIUM (msg), CAMEL_DATA_WRAPPER(multipart));

	camel_object_unref (multipart);

	if (body)
		g_free (body);

	return msg;
}

static void
gw_update_all_items (CamelFolder *folder, GList *item_list, CamelException *ex) 
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (folder);
	GPtrArray *summary = NULL;
	int index = 0;
	GList *temp;
	CamelFolderChangeInfo *changes = NULL;
	CamelMessageInfo *info; 
	changes = camel_folder_change_info_new ();

	item_list = g_list_reverse (item_list);

	summary = camel_folder_get_summary (folder);
	/*item_ids : List of ids from the summary*/
	while (index < summary->len) {
		info = g_ptr_array_index (summary, index);
		temp = NULL; 

		if (item_list) {
			temp = g_list_find_custom (item_list, (const char *)info->uid, (GCompareFunc) strcmp);
		}

		if (!temp) {
			CAMEL_GROUPWISE_FOLDER_LOCK (folder, cache_lock);
			camel_folder_summary_remove_uid (folder->summary, info->uid);
			camel_data_cache_remove (gw_folder->cache, "cache", info->uid, ex);
			camel_folder_change_info_remove_uid (changes, info->uid);
			CAMEL_GROUPWISE_FOLDER_UNLOCK (folder, cache_lock);
		} else { 
			item_list = g_list_delete_link (item_list, temp);
		}
		index ++;
	}

	camel_object_trigger_event (folder, "folder_changed", changes);

	if (item_list) {
		CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (folder->parent_store);

		CAMEL_SERVICE_LOCK (gw_store, connect_lock);
		gw_update_cache (folder, item_list, ex, TRUE);	
		CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);

		g_list_foreach (item_list, (GFunc)g_free, NULL);	
		g_list_free (item_list);
	}

	camel_folder_free_summary (folder, summary);
}

static void
groupwise_append_message (CamelFolder *folder, CamelMimeMessage *message,
		const CamelMessageInfo *info, char **appended_uid,
		CamelException *ex)
{
	const char *container_id = NULL;
	CamelGroupwiseStore *gw_store= CAMEL_GROUPWISE_STORE(folder->parent_store);
	CamelGroupwiseStorePrivate  *priv = gw_store->priv;
	CamelOfflineStore *offline = (CamelOfflineStore *) folder->parent_store;
	CamelAddress *recipients;
	EGwConnectionStatus status;
	EGwConnection *cnc;
	EGwItem *item;
	char *id;
	gboolean is_ok = FALSE;

	if (!strcmp (folder->name, RECEIVED))
		is_ok = TRUE;
	if(!strcmp (folder->name, SENT))
		is_ok = TRUE;

	if (!is_ok) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot append message to folder `%s': %s"),
				folder->full_name, e_gw_connection_get_error_message (status));
		return;
	}

	if (offline->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		camel_groupwise_journal_append ((CamelGroupwiseJournal *) ((CamelGroupwiseFolder *)folder)->journal, message, info, appended_uid, ex);
		return;
	}
	cnc = cnc_lookup (priv);

	CAMEL_SERVICE_LOCK (folder->parent_store, connect_lock);
	/*Get the container id*/
	container_id = camel_groupwise_store_container_id_lookup (gw_store, folder->full_name) ;

	/* FIXME Separate To/CC/BCC? */
	recipients = CAMEL_ADDRESS (camel_internet_address_new ());
	camel_address_cat (recipients, CAMEL_ADDRESS (camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO)));
	camel_address_cat (recipients, CAMEL_ADDRESS (camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC)));
	camel_address_cat (recipients, CAMEL_ADDRESS (camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_BCC)));

	item = camel_groupwise_util_item_from_message (cnc, message, CAMEL_ADDRESS (message->from), recipients);
	/*Set the source*/
	if (!strcmp (folder->name, RECEIVED))
		e_gw_item_set_source (item, "received");
	if (!strcmp (folder->name, SENT))
		e_gw_item_set_source (item, "sent");
	if (!strcmp (folder->name, DRAFT))
		e_gw_item_set_source (item, "draft");
	if (!strcmp (folder->name, PERSONAL))
		e_gw_item_set_source (item, "personal");
	/*set container id*/
	e_gw_item_set_container_id (item, container_id);

	status = e_gw_connection_create_item (cnc, item, &id);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot create message: %s"),
				e_gw_connection_get_error_message (status));

		if (appended_uid)
			*appended_uid = NULL;
		CAMEL_SERVICE_UNLOCK (folder->parent_store, connect_lock);
		return;
	}

	status = e_gw_connection_add_item (cnc, container_id, id);
	g_message ("Adding %s to %s", id, container_id);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot append message to folder `%s': %s"),
				folder->full_name, e_gw_connection_get_error_message (status));

		if (appended_uid)
			*appended_uid = NULL;

		CAMEL_SERVICE_UNLOCK (folder->parent_store, connect_lock);
		return;
	}

	if (appended_uid)
		*appended_uid = g_strdup (id);	
	g_free (id);
	CAMEL_SERVICE_UNLOCK (folder->parent_store, connect_lock);
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

static void
groupwise_transfer_messages_to (CamelFolder *source, GPtrArray *uids, 
		CamelFolder *destination, GPtrArray **transferred_uids, 
		gboolean delete_originals, CamelException *ex)
{
	int count, index = 0;
	GList *item_ids = NULL;
	const char *source_container_id = NULL, *dest_container_id = NULL;
	CamelGroupwiseStore *gw_store= CAMEL_GROUPWISE_STORE(source->parent_store);
	CamelOfflineStore *offline = (CamelOfflineStore *) destination->parent_store;
	CamelGroupwiseStorePrivate  *priv = gw_store->priv;
	EGwConnectionStatus status;
	EGwConnection *cnc;

	count = camel_folder_summary_count (destination->summary);
	qsort (uids->pdata, uids->len, sizeof (void *), uid_compar);

	while (index < uids->len) {
		item_ids = g_list_append (item_ids, g_ptr_array_index (uids, index));
		index ++;
	}

	if (transferred_uids)
		*transferred_uids = NULL;

	if (delete_originals) 
		source_container_id = camel_groupwise_store_container_id_lookup (gw_store, source->full_name) ;
	else
		source_container_id = NULL;
	dest_container_id = camel_groupwise_store_container_id_lookup (gw_store, destination->full_name) ;

	CAMEL_SERVICE_LOCK (source->parent_store, connect_lock);
	/* check for offline operation */
	if (offline->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		CamelGroupwiseJournal *journal = (CamelGroupwiseJournal *) ((CamelGroupwiseFolder *) destination)->journal;
		CamelMimeMessage *message;
		GList *l;
		int i;

		for (l = item_ids, i = 0; l; l = l->next, i++) {
			CamelMessageInfo *info;

			if (!(info = camel_folder_summary_uid (source->summary, uids->pdata[i])))
				continue;

			if (!(message = groupwise_folder_get_message (source, camel_message_info_uid (info), ex)))
				break;

			camel_groupwise_journal_transfer (journal, (CamelGroupwiseFolder *)source, message, info, uids->pdata[i], NULL, ex);
			camel_object_unref (message);

			if (camel_exception_is_set (ex))
				break;

			if (delete_originals)
				camel_folder_delete_message(source, camel_message_info_uid (info));
		}

		CAMEL_SERVICE_UNLOCK (source->parent_store, connect_lock);
		return;
	}

	cnc = cnc_lookup (priv);

	index = 0;
	while (index < uids->len) {
		int count;
		count = camel_folder_summary_count (destination->summary);

		if (delete_originals) 
			status = e_gw_connection_move_item (cnc, (const char *)uids->pdata[index], 
					dest_container_id, source_container_id);
		else
			status = e_gw_connection_move_item (cnc, (const char *)uids->pdata[index], 
					dest_container_id, NULL);
		if (status == E_GW_CONNECTION_STATUS_OK) {
			if (delete_originals) 
				camel_folder_delete_message(source, uids->pdata[index]);
		} else {
			g_warning ("Warning!! Could not move item : %s\n", (char *)uids->pdata[index]);
		}
		index ++;
	}
	/* Refresh the destination folder, if its not refreshed already */
	if (gw_store->current_folder != destination || 
			camel_folder_summary_count (destination->summary) == count)
		camel_folder_refresh_info (destination, ex);

	camel_folder_summary_touch (source->summary);
	camel_folder_summary_touch (destination->summary);

	gw_store->current_folder = source;

	CAMEL_SERVICE_UNLOCK (source->parent_store, connect_lock);
}

static void
groupwise_expunge (CamelFolder *folder, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE(folder->parent_store);
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (folder);
	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	CamelGroupwiseMessageInfo *ginfo;
	CamelMessageInfo *info;
	char *container_id;
	EGwConnection *cnc;
	EGwConnectionStatus status;
	CamelFolderChangeInfo *changes;
	int i, max;
	gboolean delete = FALSE;


	cnc = cnc_lookup (priv);
	if (!cnc)
		return;

	if (!strcmp (folder->full_name, "Trash")) {
		CAMEL_SERVICE_LOCK (groupwise_store, connect_lock);
		status = e_gw_connection_purge_deleted_items (cnc);
		if (status == E_GW_CONNECTION_STATUS_OK) {
			camel_folder_freeze (folder);
			groupwise_summary_clear (folder->summary, TRUE);
			camel_folder_thaw (folder);
		} else
			g_warning ("Could not Empty Trash\n");
		CAMEL_SERVICE_UNLOCK (groupwise_store, connect_lock);
		return;
	}

	changes = camel_folder_change_info_new ();

	container_id =  g_strdup (camel_groupwise_store_container_id_lookup (groupwise_store, folder->full_name)) ;

	max = camel_folder_summary_count (folder->summary);
	for (i = 0; i < max; i++) {
		info = camel_folder_summary_index (folder->summary, i);
		ginfo = (CamelGroupwiseMessageInfo *) info;
		if (ginfo && (ginfo->info.flags & CAMEL_MESSAGE_DELETED)) {
			const char *uid = camel_message_info_uid (info);
			CAMEL_SERVICE_LOCK (groupwise_store, connect_lock);
			status = e_gw_connection_remove_item (cnc, container_id, uid);
			if (status == E_GW_CONNECTION_STATUS_OK) {
				CAMEL_GROUPWISE_FOLDER_LOCK (folder, cache_lock);
				camel_folder_change_info_remove_uid (changes, (char *) uid);
				camel_folder_summary_remove (folder->summary, info);
				camel_data_cache_remove(gw_folder->cache, "cache", uid, ex);
				CAMEL_GROUPWISE_FOLDER_UNLOCK (folder, cache_lock);
				delete = TRUE;
				i--;  max--;
			}
			CAMEL_SERVICE_UNLOCK (groupwise_store, connect_lock);
		}
		camel_message_info_free (info);
	}

	if (delete)
		camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", changes);


	g_free (container_id);
	camel_folder_change_info_free (changes);
}

static void
camel_groupwise_folder_class_init (CamelGroupwiseFolderClass *camel_groupwise_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_groupwise_folder_class);

	parent_class = CAMEL_OFFLINE_FOLDER_CLASS (camel_type_get_global_classfuncs (camel_offline_folder_get_type ()));

	((CamelObjectClass *) camel_groupwise_folder_class)->getv = gw_getv;

	camel_folder_class->get_message = groupwise_folder_get_message;
	camel_folder_class->rename = groupwise_folder_rename;
	camel_folder_class->search_by_expression = groupwise_folder_search_by_expression;
	camel_folder_class->search_by_uids = groupwise_folder_search_by_uids; 
	camel_folder_class->search_free = groupwise_folder_search_free;
	camel_folder_class->append_message = groupwise_append_message;
	camel_folder_class->refresh_info = groupwise_refresh_info;
	camel_folder_class->sync = groupwise_sync;
	camel_folder_class->expunge = groupwise_expunge;
	camel_folder_class->transfer_messages_to = groupwise_transfer_messages_to;
}

static void
camel_groupwise_folder_init (gpointer object, gpointer klass)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (object);
	CamelFolder *folder = CAMEL_FOLDER (object);


	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN;

	folder->folder_flags = CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY | CAMEL_FOLDER_HAS_SEARCH_CAPABILITY;

	gw_folder->priv = g_malloc0 (sizeof(*gw_folder->priv));

#ifdef ENABLE_THREADS
	gw_folder->priv->search_lock = e_mutex_new(E_MUTEX_SIMPLE);
	gw_folder->priv->cache_lock = e_mutex_new(E_MUTEX_REC);
#endif 
	gw_folder->need_rescan = TRUE;
}

static void
camel_groupwise_folder_finalize (CamelObject *object)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (object);

	if (gw_folder->priv)
		g_free(gw_folder->priv);
	if (gw_folder->cache)
		camel_object_unref (gw_folder->cache);
	if (gw_folder->search)
		camel_object_unref (gw_folder->search);

}

CamelType
camel_groupwise_folder_get_type (void)
{
	static CamelType camel_groupwise_folder_type = CAMEL_INVALID_TYPE;


	if (camel_groupwise_folder_type == CAMEL_INVALID_TYPE) {
		camel_groupwise_folder_type =
			camel_type_register (camel_offline_folder_get_type (),
					"CamelGroupwiseFolder",
					sizeof (CamelGroupwiseFolder),
					sizeof (CamelGroupwiseFolderClass),
					(CamelObjectClassInitFunc) camel_groupwise_folder_class_init,
					NULL,
					(CamelObjectInitFunc) camel_groupwise_folder_init,
					(CamelObjectFinalizeFunc) camel_groupwise_folder_finalize);
	}

	return camel_groupwise_folder_type;
}

static int
gw_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	CamelFolder *folder = (CamelFolder *)object;
	int i, count = 0;
	guint32 tag;

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

void 
convert_to_calendar (EGwItem *item, char **str, int *len)
{
	EGwItemOrganizer *org = NULL;
	GSList *recp_list = NULL;
	GSList *attach_list = NULL;
	GString *gstr = g_string_new (NULL);
	int recur_key = 0;
	char **tmp = NULL;
	const char *temp = NULL;

	tmp = g_strsplit (e_gw_item_get_id (item), "@", -1);

	gstr = g_string_append (gstr, "BEGIN:VCALENDAR\n");
	gstr = g_string_append (gstr, "METHOD:REQUEST\n");
	gstr = g_string_append (gstr, "BEGIN:VEVENT\n");

	if ((recur_key = e_gw_item_get_recurrence_key (item)) != 0) {
		char *recur_k = g_strdup_printf ("%d", recur_key); 

		g_string_append_printf (gstr, "UID:%s\n", recur_k);
		g_string_append_printf (gstr, "X-GW-RECURRENCE-KEY:%s\n", recur_k);

		g_free (recur_k);
	} else
		g_string_append_printf (gstr, "UID:%s\n",e_gw_item_get_icalid (item));

	g_string_append_printf (gstr, "X-GWITEM-TYPE:APPOINTMENT\n");
	g_string_append_printf (gstr, "DTSTART:%s\n",e_gw_item_get_start_date (item));
	g_string_append_printf (gstr, "SUMMARY:%s\n", e_gw_item_get_subject (item));

	temp = e_gw_item_get_message (item);
	if (temp) {
		g_string_append(gstr, "DESCRIPTION:");
		while (*temp) {
			if (*temp == '\n')
				g_string_append(gstr, "\\n");
			else
				g_string_append_c(gstr, *temp);
			temp++;
		}
		g_string_append(gstr, "\n");	
	}

	g_string_append_printf (gstr, "DTSTAMP:%s\n", e_gw_item_get_creation_date (item));
	g_string_append_printf (gstr, "X-GWMESSAGEID:%s\n", e_gw_item_get_id (item));
	g_string_append_printf (gstr, "X-GWSHOW-AS:BUSY\n");
	g_string_append_printf (gstr, "X-GWRECORDID:%s\n", tmp[0]);

	org = e_gw_item_get_organizer (item);
	if (org)
		g_string_append_printf (gstr, "ORGANIZER;CN= %s;ROLE= CHAIR;\n MAILTO:%s\n", 
				org->display_name, org->email);

	recp_list = e_gw_item_get_recipient_list (item);
	if (recp_list) {
		GSList *rl ;

		for (rl = recp_list ; rl != NULL ; rl = rl->next) {
			EGwItemRecipient *recp = (EGwItemRecipient *) rl->data;
			g_string_append_printf (gstr, 
					"ATTENDEE;CN= %s;ROLE= REQ-PARTICIPANT:\nMAILTO:%s\n",
					recp->display_name, recp->email);
		}
	}

	g_string_append_printf (gstr, "DTEND:%s\n", e_gw_item_get_end_date (item));

	temp = NULL;
	temp = e_gw_item_get_place (item);
	if (temp)
		g_string_append_printf (gstr, "LOCATION:%s\n", temp);

	temp = NULL;
	temp = e_gw_item_get_task_priority (item);
	if (temp)
		g_string_append_printf (gstr, "PRIORITY:%s\n", temp);

	temp = NULL;
	attach_list = e_gw_item_get_attach_id_list (item);
	if (attach_list) {
		GSList *al;

		for (al = attach_list ; al != NULL ; al = al->next) {
			EGwItemAttachment *attach = (EGwItemAttachment *)al->data;
			g_string_append_printf (gstr, "ATTACH:%s\n", attach->id);
		}
	}
	gstr = g_string_append (gstr, "END:VEVENT\n");
	gstr = g_string_append (gstr, "END:VCALENDAR\n");

	*str = gstr->str;
	*len = gstr->len;

	g_string_free (gstr, FALSE);
	g_strfreev (tmp);
}

void 
convert_to_task (EGwItem *item, char **str, int *len)
{
	EGwItemOrganizer *org = NULL;
	GSList *recp_list = NULL;
	GString *gstr = g_string_new (NULL);
	char **tmp = NULL;
	const char *temp = NULL;

	tmp = g_strsplit (e_gw_item_get_id (item), "@", -1);

	gstr = g_string_append (gstr, "BEGIN:VCALENDAR\n");
	gstr = g_string_append (gstr, "METHOD:REQUEST\n");
	gstr = g_string_append (gstr, "BEGIN:VTODO\n");
	g_string_append_printf (gstr, "UID:%s\n",e_gw_item_get_icalid (item));
	g_string_append_printf (gstr, "DTSTART:%s\n",e_gw_item_get_start_date (item));
	g_string_append_printf (gstr, "SUMMARY:%s\n", e_gw_item_get_subject (item));
	g_string_append_printf (gstr, "DESCRIPTION:%s\n", e_gw_item_get_message (item));
	g_string_append_printf (gstr, "DTSTAMP:%s\n", e_gw_item_get_creation_date (item));
	g_string_append_printf (gstr, "X-GWMESSAGEID:%s\n", e_gw_item_get_id (item));
	g_string_append_printf (gstr, "X-GWSHOW-AS:BUSY\n");
	g_string_append_printf (gstr, "X-GWRECORDID:%s\n", tmp[0]);

	org = e_gw_item_get_organizer (item);
	if (org)
		g_string_append_printf (gstr, "ORGANIZER;CN= %s;ROLE= CHAIR;\n MAILTO:%s\n", 
				org->display_name, org->email);

	recp_list = e_gw_item_get_recipient_list (item);
	if (recp_list) {
		GSList *rl;

		for (rl = recp_list ; rl != NULL ; rl = rl->next) {
			EGwItemRecipient *recp = (EGwItemRecipient *) rl->data;
			g_string_append_printf (gstr, 
					"ATTENDEE;CN= %s;ROLE= REQ-PARTICIPANT:\nMAILTO:%s\n",
					recp->display_name, recp->email);
		}
	}

	g_string_append_printf (gstr, "DTEND:%s\n", e_gw_item_get_end_date (item));

	temp = e_gw_item_get_place (item);
	if (temp)
		g_string_append_printf (gstr, "LOCATION:%s\n", temp);

	temp = NULL;
	temp = e_gw_item_get_task_priority (item);
	if (temp)
		g_string_append_printf (gstr, "PRIORITY:%s\n", temp);

	temp = NULL;
	temp = e_gw_item_get_due_date (item);
	if (temp)
		g_string_append_printf (gstr, "DUE:%s\n", temp);
	gstr = g_string_append (gstr, "END:VTODO\n");
	gstr = g_string_append (gstr, "END:VCALENDAR\n");


	*str = g_strdup (gstr->str);
	*len = gstr->len;

	g_string_free (gstr, TRUE);
	g_strfreev (tmp);
}

/** End **/

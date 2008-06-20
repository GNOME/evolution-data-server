/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "camel-data-cache.h"
#include "camel-file-utils.h"
#include "camel-folder-summary.h"
#include "camel-folder.h"

#include "camel-groupwise-folder.h"
#include "camel-groupwise-journal.h"
#include "camel-groupwise-store.h"

#define d(x) x


static void camel_groupwise_journal_class_init (CamelGroupwiseJournalClass *klass);
static void camel_groupwise_journal_init (CamelGroupwiseJournal *journal, CamelGroupwiseJournalClass *klass);
static void camel_groupwise_journal_finalize (CamelObject *object);

static void groupwise_entry_free (CamelOfflineJournal *journal, EDListNode *entry);
static EDListNode *groupwise_entry_load (CamelOfflineJournal *journal, FILE *in);
static int groupwise_entry_write (CamelOfflineJournal *journal, EDListNode *entry, FILE *out);
static int groupwise_entry_play (CamelOfflineJournal *journal, EDListNode *entry, CamelException *ex);


static CamelOfflineJournalClass *parent_class = NULL;


CamelType
camel_groupwise_journal_get_type (void)
{
	static CamelType type = NULL;
	
	if (!type) {
		type = camel_type_register (camel_offline_journal_get_type (),
					    "CamelGroupwiseJournal",
					    sizeof (CamelGroupwiseJournal),
					    sizeof (CamelGroupwiseJournalClass),
					    (CamelObjectClassInitFunc) camel_groupwise_journal_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_groupwise_journal_init,
					    (CamelObjectFinalizeFunc) camel_groupwise_journal_finalize);
	}
	
	return type;
}

static void
camel_groupwise_journal_class_init (CamelGroupwiseJournalClass *klass)
{
	CamelOfflineJournalClass *journal_class = (CamelOfflineJournalClass *) klass;
	
	parent_class = (CamelOfflineJournalClass *) camel_type_get_global_classfuncs (CAMEL_TYPE_OFFLINE_JOURNAL);
	
	journal_class->entry_free = groupwise_entry_free;
	journal_class->entry_load = groupwise_entry_load;
	journal_class->entry_write = groupwise_entry_write;
	journal_class->entry_play = groupwise_entry_play;
}

static void
camel_groupwise_journal_init (CamelGroupwiseJournal *journal, CamelGroupwiseJournalClass *klass)
{
	
}

static void
camel_groupwise_journal_finalize (CamelObject *object)
{
	
}

static void
groupwise_entry_free (CamelOfflineJournal *journal, EDListNode *entry)
{
	CamelGroupwiseJournalEntry *groupwise_entry = (CamelGroupwiseJournalEntry *) entry;
	
	g_free (groupwise_entry->uid);
	g_free (groupwise_entry->original_uid);
	g_free (groupwise_entry->source_container);
	g_free (groupwise_entry);
}

static EDListNode *
groupwise_entry_load (CamelOfflineJournal *journal, FILE *in)
{
	CamelGroupwiseJournalEntry *entry;
	
	entry = g_malloc0 (sizeof (CamelGroupwiseJournalEntry));
	
	if (camel_file_util_decode_uint32 (in, &entry->type) == -1)
		goto exception;
	
	switch (entry->type) {
	case CAMEL_GROUPWISE_JOURNAL_ENTRY_APPEND:
		if (camel_file_util_decode_string (in, &entry->uid) == -1)
			goto exception;
		break;
	case CAMEL_GROUPWISE_JOURNAL_ENTRY_TRANSFER:
		if (camel_file_util_decode_string (in, &entry->uid) == -1)
			goto exception;
		if (camel_file_util_decode_string (in, &entry->original_uid) == -1)
			goto exception;
		if (camel_file_util_decode_string (in, &entry->source_container) == -1)
			goto exception;
		break;
	default:
		goto exception;
	}
	
	return (EDListNode *) entry;
	
 exception:
	
	if (entry->type == CAMEL_GROUPWISE_JOURNAL_ENTRY_TRANSFER)
		g_free (entry->source_container);
	
	g_free (entry->uid);
	g_free (entry);
	
	return NULL;
}

static int
groupwise_entry_write (CamelOfflineJournal *journal, EDListNode *entry, FILE *out)
{
	CamelGroupwiseJournalEntry *groupwise_entry = (CamelGroupwiseJournalEntry *) entry;
	
	if (camel_file_util_encode_uint32 (out, groupwise_entry->type) == -1)
		return -1;
	
	switch (groupwise_entry->type) {
	case CAMEL_GROUPWISE_JOURNAL_ENTRY_APPEND:
		if (camel_file_util_encode_string (out, groupwise_entry->uid))
			return -1;
		break;
	case CAMEL_GROUPWISE_JOURNAL_ENTRY_TRANSFER:
		if (camel_file_util_encode_string (out, groupwise_entry->uid))
			return -1;
		if (camel_file_util_encode_string (out, groupwise_entry->original_uid))
			return -1;
		if (camel_file_util_encode_string (out, groupwise_entry->source_container))
			return -1;
		break;
	default:
		g_assert_not_reached ();
	}
	
	return 0;
}

static void
gw_message_info_dup_to (CamelMessageInfoBase *dest, CamelMessageInfoBase *src)
{
	camel_flag_list_copy (&dest->user_flags, &src->user_flags);
	camel_tag_list_copy (&dest->user_tags, &src->user_tags);
	dest->date_received = src->date_received;
	dest->date_sent = src->date_sent;
	dest->flags = src->flags;
	dest->size = src->size;
}

static int
groupwise_entry_play_append (CamelOfflineJournal *journal, CamelGroupwiseJournalEntry *entry, CamelException *ex)
{
	CamelGroupwiseFolder *gw_folder = (CamelGroupwiseFolder *) journal->folder;
	CamelFolder *folder = journal->folder;
	CamelMimeMessage *message;
	CamelMessageInfo *info;
	CamelStream *stream;
	CamelException lex;
	
	/* if the message isn't in the cache, the user went behind our backs so "not our problem" */
	if (!gw_folder->cache || !(stream = camel_data_cache_get (gw_folder->cache, "cache", entry->uid, ex)))
		goto done;
	
	message = camel_mime_message_new ();
	if (camel_data_wrapper_construct_from_stream ((CamelDataWrapper *) message, stream) == -1) {
		camel_object_unref (message);
		camel_object_unref (stream);
		goto done;
	}
	
	camel_object_unref (stream);
	
	if (!(info = camel_folder_summary_uid (folder->summary, entry->uid))) {
		/* Note: this should never happen, but rather than crash lets make a new info */
		info = camel_message_info_new (NULL);
	}
	
	camel_exception_init (&lex);
	camel_folder_append_message (folder, message, info, NULL, &lex);
	camel_message_info_free (info);
	camel_object_unref (message);
	
	if (camel_exception_is_set (&lex)) {
		camel_exception_xfer (ex, &lex);
		return -1;
	}
	
 done:
	
	camel_folder_summary_remove_uid (folder->summary, entry->uid);
	camel_data_cache_remove (gw_folder->cache, "cache", entry->uid, NULL);
	
	return 0;
}

static int
groupwise_entry_play_transfer (CamelOfflineJournal *journal, CamelGroupwiseJournalEntry *entry, CamelException *ex)
{
	CamelGroupwiseFolder *gw_folder = (CamelGroupwiseFolder *) journal->folder;
	CamelFolder *folder = journal->folder;
	CamelGroupwiseMessageInfo *real;
	CamelMessageInfoBase *info;
	GPtrArray *xuids, *uids;
	CamelException lex;
	CamelFolder *src;
	const char *name;
	
	if (!(info = (CamelMessageInfoBase *) camel_folder_summary_uid (folder->summary, entry->uid))) {
		/* Note: this should never happen, but rather than crash lets make a new info */
		info = camel_message_info_new (NULL);
	}
	
	name = camel_groupwise_store_folder_lookup ((CamelGroupwiseStore *) folder->parent_store, entry->source_container);
	if (name && (src = camel_store_get_folder (folder->parent_store, name, 0, ex))) {
		uids = g_ptr_array_sized_new (1);
		g_ptr_array_add (uids, entry->original_uid);
		
		camel_exception_init (&lex);
		camel_folder_transfer_messages_to (src, uids, folder, &xuids, FALSE, &lex);
		if (!camel_exception_is_set (&lex)) {
			real = (CamelGroupwiseMessageInfo *) camel_folder_summary_uid (folder->summary, xuids->pdata[0]);
			
			/* transfer all the system flags, user flags/tags, etc */
			gw_message_info_dup_to ((CamelMessageInfoBase *) real, (CamelMessageInfoBase *) info);
			camel_message_info_free (real);
		} else {
			camel_exception_xfer (ex, &lex);
			goto exception;
		}
		
		g_ptr_array_free (xuids, TRUE);
		g_ptr_array_free (uids, TRUE);
		camel_object_unref (src);
	} else if (!name) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot get folder container %s"),
				      entry->source_container);
		goto exception;
	}
	
	/* message was successfully transferred, remove the fake item from the cache/summary */
	camel_folder_summary_remove_uid (folder->summary, entry->uid);
	camel_data_cache_remove (gw_folder->cache, "cache", entry->uid, NULL);
	camel_message_info_free (info);
	
	return 0;
	
 exception:
	
	camel_message_info_free (info);
	
	return -1;
}

static int
groupwise_entry_play (CamelOfflineJournal *journal, EDListNode *entry, CamelException *ex)
{
	CamelGroupwiseJournalEntry *groupwise_entry = (CamelGroupwiseJournalEntry *) entry;
	
	switch (groupwise_entry->type) {
	case CAMEL_GROUPWISE_JOURNAL_ENTRY_APPEND:
		return groupwise_entry_play_append (journal, groupwise_entry, ex);
	case CAMEL_GROUPWISE_JOURNAL_ENTRY_TRANSFER:
		return groupwise_entry_play_transfer (journal, groupwise_entry, ex);
	default:
		g_assert_not_reached ();
		return -1;
	}
}



CamelOfflineJournal *
camel_groupwise_journal_new (CamelGroupwiseFolder *folder, const char *filename)
{
	CamelOfflineJournal *journal;
	
	g_return_val_if_fail (CAMEL_IS_GROUPWISE_FOLDER (folder), NULL);
	
	journal = (CamelOfflineJournal *) camel_object_new (camel_groupwise_journal_get_type ());
	camel_offline_journal_construct (journal, (CamelFolder *) folder, filename);
	
	return journal;
}

static gboolean
update_cache (CamelGroupwiseJournal *groupwise_journal, CamelMimeMessage *message,
	      const CamelMessageInfo *mi, char **updated_uid, CamelException *ex)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) groupwise_journal;
	CamelGroupwiseFolder *groupwise_folder = (CamelGroupwiseFolder *) journal->folder;
	CamelFolder *folder = (CamelFolder *) journal->folder;
	CamelMessageInfo *info;
	CamelStream *cache;
	guint32 nextuid;
	char *uid;
	
	if (groupwise_folder->cache == NULL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Cannot append message in offline mode: cache unavailable"));
		return FALSE;
	}
	
	nextuid = camel_folder_summary_next_uid (folder->summary);
	uid = g_strdup_printf ("-%u", nextuid);
	
	if (!(cache = camel_data_cache_add (groupwise_folder->cache, "cache", uid, ex))) {
		folder->summary->nextuid--;
		g_free (uid);
		return FALSE;
	}
	
	if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *) message, cache) == -1
	    || camel_stream_flush (cache) == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot append message in offline mode: %s"),
				      g_strerror (errno));
		camel_data_cache_remove (groupwise_folder->cache, "cache", uid, NULL);
		folder->summary->nextuid--;
		camel_object_unref (cache);
		g_free (uid);
		return FALSE;
	}
	
	camel_object_unref (cache);
	
	info = camel_folder_summary_info_new_from_message (folder->summary, message);
	g_free(info->uid);
	info->uid = g_strdup (uid);
	
	gw_message_info_dup_to ((CamelMessageInfoBase *) info, (CamelMessageInfoBase *) mi);
	
	camel_folder_summary_add (folder->summary, info);
	
	if (updated_uid)
		*updated_uid = g_strdup (uid);
	
	g_free (uid);
	
	return TRUE;
}

void
camel_groupwise_journal_append (CamelGroupwiseJournal *groupwise_journal, CamelMimeMessage *message,
				const CamelMessageInfo *mi, char **appended_uid, CamelException *ex)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) groupwise_journal;
	CamelGroupwiseJournalEntry *entry;
	char *uid;
	
	if (!update_cache (groupwise_journal, message, mi, &uid, ex))
		return;
	
	entry = g_new (CamelGroupwiseJournalEntry, 1);
	entry->type = CAMEL_GROUPWISE_JOURNAL_ENTRY_APPEND;
	entry->uid = uid;
				
	e_dlist_addtail (&journal->queue, (EDListNode *) entry);
	
	if (appended_uid)
		*appended_uid = g_strdup (uid);
}

void
camel_groupwise_journal_transfer (CamelGroupwiseJournal *groupwise_journal, CamelGroupwiseFolder *source_folder,
				  CamelMimeMessage *message,  const CamelMessageInfo *mi,
				  const char *original_uid, char **transferred_uid,
				  CamelException *ex)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) groupwise_journal;
	CamelGroupwiseStore *gw_store= CAMEL_GROUPWISE_STORE(journal->folder->parent_store) ;
	CamelGroupwiseJournalEntry *entry;
	char *uid;
	
	if (!update_cache (groupwise_journal, message, mi, &uid, ex))
		return;
	
	entry = g_new (CamelGroupwiseJournalEntry, 1);
	entry->type = CAMEL_GROUPWISE_JOURNAL_ENTRY_APPEND;
	entry->uid = uid;
	entry->original_uid = g_strdup (original_uid);
	entry->source_container = g_strdup (camel_groupwise_store_container_id_lookup (gw_store, ((CamelFolder *)source_folder)->name));
	
	e_dlist_addtail (&journal->queue, (EDListNode *) entry);
	
	if (transferred_uid)
		*transferred_uid = g_strdup (uid);
}

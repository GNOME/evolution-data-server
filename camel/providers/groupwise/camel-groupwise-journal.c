/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *
 *  Copyright 2004 Novell, Inc. (www.novell.com)
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
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#include <camel/camel-i18n.h>
#include <camel/camel-folder.h>
#include <camel/camel-file-utils.h>
#include <camel/camel-folder-summary.h>
#include <camel/camel-data-cache.h>

#include "camel-groupwise-folder.h"
#include "camel-groupwise-store.h"
#include "camel-groupwise-journal.h"


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
	static CamelType type = 0;
	
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
		if (camel_file_util_decode_string (in, &entry->source_container) == -1)
			goto exception;		
		break;
	default:
		goto exception;
	}
	
	return (EDListNode *) entry;
	
 exception:
	
	switch (entry->type) {
	case CAMEL_GROUPWISE_JOURNAL_ENTRY_APPEND:
		g_free (entry->uid);
		break;
	case CAMEL_GROUPWISE_JOURNAL_ENTRY_TRANSFER:
		g_free (entry->uid);
		g_free (entry->source_container);
		break;

	default:
		g_assert_not_reached ();
	}
	
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
		if (camel_file_util_encode_string (out, groupwise_entry->source_container))
			return -1;
		break;
	default:
		g_assert_not_reached ();
	}
	
	return 0;
}

static int
play_update (CamelOfflineJournal *journal, CamelGroupwiseJournalEntry *entry, gboolean transfer, CamelException *ex)
{
	CamelGroupwiseFolder *groupwise_folder = (CamelGroupwiseFolder *) journal->folder;
	CamelFolder *folder = journal->folder;
	CamelMimeMessage *message;
	CamelMessageInfo *info;
	CamelStream *stream;
	CamelException lex;
	
	/* if the message isn't in the cache, the user went behind our backs so "not our problem" */
	if (!groupwise_folder->cache || !(stream = camel_data_cache_get (groupwise_folder->cache, "cache", entry->uid, ex)))
		goto done;
	
	message = camel_mime_message_new ();
	if (camel_data_wrapper_construct_from_stream ((CamelDataWrapper *) message, stream) == -1) {
		camel_object_unref (message);
		camel_object_unref (stream);
		goto done;
	}
	
	camel_object_unref (stream);
	
	if (!(info = camel_folder_summary_uid (folder->summary, entry->uid))) {
		/* info not in the summary, either because the summary
		 * got corrupted or because the previous time this
		 * journal was replay'd, it failed [1] */
		info = camel_message_info_new (NULL);
	}
	
	camel_exception_init (&lex);
	if (transfer) {
		GPtrArray *uids;
		CamelFolder *source_folder;
		const char *name;
		
		name = camel_groupwise_store_folder_lookup ((CamelGroupwiseStore *)folder->parent_store, entry->source_container);
		if (name) {
			source_folder = camel_store_get_folder (folder->parent_store, name, 0, &lex);
			
			if (source_folder) {
				uids = g_ptr_array_sized_new (1);
				g_ptr_array_add (uids, entry->original_uid);
				
				camel_folder_transfer_messages_to (source_folder, uids, folder, NULL, FALSE, &lex);
				
				g_ptr_array_free (uids, FALSE);
			}
		} else {
			camel_exception_setv (&lex, CAMEL_EXCEPTION_SYSTEM, _("Cannot get folder container %s"),
					      entry->source_container);
		}
	} else {
		camel_folder_append_message (folder, message, info, NULL, &lex);
	}
	camel_message_info_free (info);
	camel_object_unref (message);
	
	if (camel_exception_is_set (&lex)) {
		/* [1] remove the summary even if we fail or the next
		 * summary downsync will break because info indexes
		 * will be wrong
		 *
		 * FIXME: we really need to save these info's to a
		 * temp location and then restore them after the
		 * summary downsync finishes. */
		camel_folder_summary_remove_uid (folder->summary, entry->uid);
		camel_exception_xfer (ex, &lex);
		return -1;
	}
	
 done:
	
	camel_folder_summary_remove_uid (folder->summary, entry->uid);
	camel_data_cache_remove (groupwise_folder->cache, "cache", entry->uid, NULL);
	
	return 0;
}

static int
groupwise_entry_play_append (CamelOfflineJournal *journal, CamelGroupwiseJournalEntry *entry, CamelException *ex)
{
	return play_update (journal, entry, FALSE, ex);
}

static int
groupwise_entry_play_transfer (CamelOfflineJournal *journal, CamelGroupwiseJournalEntry *entry, CamelException *ex)
{
	return play_update (journal, entry, TRUE, ex);
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
	CamelMessageInfoBase *a, *b;
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
	info->uid = g_strdup (uid);
	
	a = (CamelMessageInfoBase *) info;
	b = (CamelMessageInfoBase *) mi;
	
	camel_flag_list_copy (&a->user_flags, &b->user_flags);
	camel_tag_list_copy (&a->user_tags, &b->user_tags);
	a->date_received = b->date_received;
	a->date_sent = b->date_sent;
	a->flags = b->flags;
	a->size = b->size;
	
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

	g_free (uid);
}

void
camel_groupwise_journal_transfer (CamelGroupwiseJournal *groupwise_journal, CamelGroupwiseFolder *source_folder, CamelMimeMessage *message, 
				  const CamelMessageInfo *mi, const char *original_uid, char **transferred_uid, CamelException *ex)
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

	g_free (uid);
}

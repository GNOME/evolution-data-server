/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *
 *  Copyright 2004 Novell, Inc. (www.novell.com)
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

#include "camel-imap4-folder.h"
#include "camel-imap4-journal.h"


#define d(x) x


static void camel_imap4_journal_class_init (CamelIMAP4JournalClass *klass);
static void camel_imap4_journal_init (CamelIMAP4Journal *journal, CamelIMAP4JournalClass *klass);
static void camel_imap4_journal_finalize (CamelObject *object);

static void imap4_entry_free (CamelOfflineJournal *journal, EDListNode *entry);
static EDListNode *imap4_entry_load (CamelOfflineJournal *journal, FILE *in);
static int imap4_entry_write (CamelOfflineJournal *journal, EDListNode *entry, FILE *out);
static int imap4_entry_play (CamelOfflineJournal *journal, EDListNode *entry, CamelException *ex);


static CamelOfflineJournalClass *parent_class = NULL;


CamelType
camel_imap4_journal_get_type (void)
{
	static CamelType type = 0;
	
	if (!type) {
		type = camel_type_register (camel_offline_journal_get_type (),
					    "CamelIMAP4Journal",
					    sizeof (CamelIMAP4Journal),
					    sizeof (CamelIMAP4JournalClass),
					    (CamelObjectClassInitFunc) camel_imap4_journal_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_imap4_journal_init,
					    (CamelObjectFinalizeFunc) camel_imap4_journal_finalize);
	}
	
	return type;
}

static void
camel_imap4_journal_class_init (CamelIMAP4JournalClass *klass)
{
	CamelOfflineJournalClass *journal_class = (CamelOfflineJournalClass *) klass;
	
	parent_class = (CamelOfflineJournalClass *) camel_type_get_global_classfuncs (CAMEL_TYPE_OFFLINE_JOURNAL);
	
	journal_class->entry_free = imap4_entry_free;
	journal_class->entry_load = imap4_entry_load;
	journal_class->entry_write = imap4_entry_write;
	journal_class->entry_play = imap4_entry_play;
}

static void
camel_imap4_journal_init (CamelIMAP4Journal *journal, CamelIMAP4JournalClass *klass)
{
	journal->failed = g_ptr_array_new ();
}

static void
camel_imap4_journal_finalize (CamelObject *object)
{
	CamelIMAP4Journal *journal = (CamelIMAP4Journal *) object;
	int i;
	
	if (journal->failed) {
		for (i = 0; i < journal->failed->len; i++)
			camel_message_info_free (journal->failed->pdata[i]);
		g_ptr_array_free (journal->failed, TRUE);
	}
}

static void
imap4_entry_free (CamelOfflineJournal *journal, EDListNode *entry)
{
	CamelIMAP4JournalEntry *imap4_entry = (CamelIMAP4JournalEntry *) entry;
	
	g_free (imap4_entry->v.append_uid);
	g_free (imap4_entry);
}

static EDListNode *
imap4_entry_load (CamelOfflineJournal *journal, FILE *in)
{
	CamelIMAP4JournalEntry *entry;
	
	entry = g_malloc0 (sizeof (CamelIMAP4JournalEntry));
	
	if (camel_file_util_decode_uint32 (in, &entry->type) == -1)
		goto exception;
	
	switch (entry->type) {
	case CAMEL_IMAP4_JOURNAL_ENTRY_APPEND:
		if (camel_file_util_decode_string (in, &entry->v.append_uid) == -1)
			goto exception;
		
		break;
	default:
		goto exception;
	}
	
	return (EDListNode *) entry;
	
 exception:
	
	switch (entry->type) {
	case CAMEL_IMAP4_JOURNAL_ENTRY_APPEND:
		g_free (entry->v.append_uid);
		break;
	default:
		g_assert_not_reached ();
	}
	
	g_free (entry);
	
	return NULL;
}

static int
imap4_entry_write (CamelOfflineJournal *journal, EDListNode *entry, FILE *out)
{
	CamelIMAP4JournalEntry *imap4_entry = (CamelIMAP4JournalEntry *) entry;
	
	if (camel_file_util_encode_uint32 (out, imap4_entry->type) == -1)
		return -1;
	
	switch (imap4_entry->type) {
	case CAMEL_IMAP4_JOURNAL_ENTRY_APPEND:
		if (camel_file_util_encode_string (out, imap4_entry->v.append_uid))
			return -1;
		
		break;
	default:
		g_assert_not_reached ();
	}
	
	return 0;
}

static void
imap4_message_info_dup_to (CamelMessageInfoBase *dest, CamelMessageInfoBase *src)
{
	camel_flag_list_copy (&dest->user_flags, &src->user_flags);
	camel_tag_list_copy (&dest->user_tags, &src->user_tags);
	dest->date_received = src->date_received;
	dest->date_sent = src->date_sent;
	dest->flags = src->flags;
	dest->size = src->size;
}

static int
imap4_entry_play_append (CamelOfflineJournal *journal, CamelIMAP4JournalEntry *entry, CamelException *ex)
{
	CamelIMAP4Folder *imap4_folder = (CamelIMAP4Folder *) journal->folder;
	CamelFolder *folder = journal->folder;
	CamelMessageInfo *info, *real;
	CamelMimeMessage *message;
	CamelStream *stream;
	CamelException lex;
	char *uid = NULL;
	
	/* if the message isn't in the cache, the user went behind our backs so "not our problem" */
	if (!imap4_folder->cache || !(stream = camel_data_cache_get (imap4_folder->cache, "cache", entry->v.append_uid, ex)))
		goto done;
	
	message = camel_mime_message_new ();
	if (camel_data_wrapper_construct_from_stream ((CamelDataWrapper *) message, stream) == -1) {
		camel_object_unref (message);
		camel_object_unref (stream);
		goto done;
	}
	
	camel_object_unref (stream);
	
	if (!(info = camel_folder_summary_uid (folder->summary, entry->v.append_uid))) {
		/* info not in the summary, either because the summary
		 * got corrupted or because the previous time this
		 * journal was replay'd, it failed [1] */
		info = camel_message_info_new (NULL);
	}
	
	camel_exception_init (&lex);
	camel_folder_append_message (folder, message, info, &uid, &lex);
	camel_object_unref (message);
	
	if (camel_exception_is_set (&lex)) {
		/* Remove the message-info from the summary even if we fail or the next
		 * summary downsync will break because info indexes will be wrong */
		if (info->summary == folder->summary) {
			camel_folder_summary_remove (folder->summary, info);
			g_ptr_array_add (((CamelIMAP4Journal *) journal)->failed, info);
		} else {
			/* info wasn't in the summary to begin with */
			camel_folder_summary_remove_uid (folder->summary, entry->v.append_uid);
			camel_message_info_free (info);
		}
		
		camel_exception_xfer (ex, &lex);
		
		return -1;
	}
	
	if (uid != NULL && (real = camel_folder_summary_uid (folder->summary, uid))) {
		/* Copy the system flags and user flags/tags over to the real
		   message-info now stored in the summary to prevent the user
		   from losing any of this meta-data */
		imap4_message_info_dup_to ((CamelMessageInfoBase *) real, (CamelMessageInfoBase *) info);
	}
	
	camel_message_info_free (info);
	g_free (uid);
	
 done:
	
	camel_folder_summary_remove_uid (folder->summary, entry->v.append_uid);
	camel_data_cache_remove (imap4_folder->cache, "cache", entry->v.append_uid, NULL);
	
	return 0;
}

static int
imap4_entry_play (CamelOfflineJournal *journal, EDListNode *entry, CamelException *ex)
{
	CamelIMAP4JournalEntry *imap4_entry = (CamelIMAP4JournalEntry *) entry;
	
	switch (imap4_entry->type) {
	case CAMEL_IMAP4_JOURNAL_ENTRY_APPEND:
		return imap4_entry_play_append (journal, imap4_entry, ex);
	default:
		g_assert_not_reached ();
		return -1;
	}
}



CamelOfflineJournal *
camel_imap4_journal_new (CamelIMAP4Folder *folder, const char *filename)
{
	CamelOfflineJournal *journal;
	
	g_return_val_if_fail (CAMEL_IS_IMAP4_FOLDER (folder), NULL);
	
	journal = (CamelOfflineJournal *) camel_object_new (camel_imap4_journal_get_type ());
	camel_offline_journal_construct (journal, (CamelFolder *) folder, filename);
	
	return journal;
}


void
camel_imap4_journal_readd_failed (CamelIMAP4Journal *journal)
{
	CamelFolderSummary *summary = ((CamelOfflineJournal *) journal)->folder->summary;
	int i;
	
	for (i = 0; i < journal->failed->len; i++)
		camel_folder_summary_add (summary, journal->failed->pdata[i]);
	
	g_ptr_array_set_size (journal->failed, 0);
}


void
camel_imap4_journal_append (CamelIMAP4Journal *imap4_journal, CamelMimeMessage *message,
			    const CamelMessageInfo *mi, char **appended_uid, CamelException *ex)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) imap4_journal;
	CamelIMAP4Folder *imap4_folder = (CamelIMAP4Folder *) journal->folder;
	CamelFolder *folder = (CamelFolder *) journal->folder;
	CamelIMAP4JournalEntry *entry;
	CamelMessageInfo *info;
	CamelStream *cache;
	guint32 nextuid;
	char *uid;
	
	if (imap4_folder->cache == NULL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Cannot append message in offline mode: cache unavailable"));
		return;
	}
	
	nextuid = camel_folder_summary_next_uid (folder->summary);
	uid = g_strdup_printf ("-%u", nextuid);
	
	if (!(cache = camel_data_cache_add (imap4_folder->cache, "cache", uid, ex))) {
		folder->summary->nextuid--;
		g_free (uid);
		return;
	}
	
	if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *) message, cache) == -1
	    || camel_stream_flush (cache) == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot append message in offline mode: %s"),
				      g_strerror (errno));
		camel_data_cache_remove (imap4_folder->cache, "cache", uid, NULL);
		folder->summary->nextuid--;
		camel_object_unref (cache);
		g_free (uid);
		return;
	}
	
	camel_object_unref (cache);
	
	entry = g_new (CamelIMAP4JournalEntry, 1);
	entry->type = CAMEL_IMAP4_JOURNAL_ENTRY_APPEND;
	entry->v.append_uid = uid;
	
	e_dlist_addtail (&journal->queue, (EDListNode *) entry);
	
	info = camel_folder_summary_info_new_from_message (folder->summary, message);
	g_free(info->uid);
	info->uid = g_strdup (uid);
	
	imap4_message_info_dup_to ((CamelMessageInfoBase *) info, (CamelMessageInfoBase *) mi);
	
	camel_folder_summary_add (folder->summary, info);
	
	if (appended_uid)
		*appended_uid = g_strdup (uid);
}

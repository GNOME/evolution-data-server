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
	
}

static void
camel_imap4_journal_finalize (CamelObject *object)
{
	
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

static int
imap4_entry_play_append (CamelOfflineJournal *journal, CamelIMAP4JournalEntry *entry, CamelException *ex)
{
	CamelIMAP4Folder *imap4_folder = (CamelIMAP4Folder *) journal->folder;
	CamelFolder *folder = journal->folder;
	CamelMimeMessage *message;
	CamelMessageInfo *info;
	CamelStream *stream;
	CamelException lex;
	
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
	camel_folder_append_message (folder, message, info, NULL, &lex);
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
		camel_folder_summary_remove_uid (folder->summary, entry->v.append_uid);
		camel_exception_xfer (ex, &lex);
		return -1;
	}
	
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
camel_imap4_journal_append (CamelIMAP4Journal *imap4_journal, CamelMimeMessage *message,
			    const CamelMessageInfo *mi, char **appended_uid, CamelException *ex)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) imap4_journal;
	CamelIMAP4Folder *imap4_folder = (CamelIMAP4Folder *) journal->folder;
	CamelFolder *folder = (CamelFolder *) journal->folder;
	CamelIMAP4JournalEntry *entry;
	CamelMessageInfoBase *a, *b;
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
	
	if (appended_uid)
		*appended_uid = g_strdup (uid);
}

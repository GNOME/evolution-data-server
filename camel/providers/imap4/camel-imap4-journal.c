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


static CamelObjectClass *parent_class = NULL;


CamelType
camel_imap4_journal_get_type (void)
{
	static CamelType type = 0;
	
	if (!type) {
		type = camel_type_register (camel_object_get_type (),
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
	parent_class = camel_type_get_global_classfuncs (CAMEL_OBJECT_TYPE);
}

static void
camel_imap4_journal_init (CamelIMAP4Journal *journal, CamelIMAP4JournalClass *klass)
{
	journal->folder = NULL;
	journal->filename = NULL;
	e_dlist_init (&journal->queue);
}

static void
camel_imap4_journal_finalize (CamelObject *object)
{
	CamelIMAP4Journal *journal = (CamelIMAP4Journal *) object;
	CamelIMAP4JournalEntry *entry;
	
	g_free (journal->filename);
	
	while ((entry = (CamelIMAP4JournalEntry *) e_dlist_remhead (&journal->queue))) {
		g_free (entry->v.append_uid);
		g_free (entry);
	}
}


static CamelIMAP4JournalEntry *
imap4_journal_entry_load (FILE *in)
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
	
	return entry;
	
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


CamelIMAP4Journal *
camel_imap4_journal_new (CamelIMAP4Folder *folder, const char *filename)
{
	CamelIMAP4Journal *journal;
	EDListNode *entry;
	FILE *fp;
	
	g_return_val_if_fail (CAMEL_IS_IMAP4_FOLDER (folder), NULL);
	
	journal = (CamelIMAP4Journal *) camel_object_new (camel_imap4_journal_get_type ());
	journal->filename = g_strdup (filename);
	journal->folder = folder;
	
	if ((fp = fopen (filename, "r"))) {
		while ((entry = (EDListNode *) imap4_journal_entry_load (fp)))
			e_dlist_addtail (&journal->queue, entry);
		
		fclose (fp);
	}
	
	return journal;
}


void
camel_imap4_journal_set_filename (CamelIMAP4Journal *journal, const char *filename)
{
	g_return_if_fail (CAMEL_IS_IMAP4_JOURNAL (journal));
	
	g_free (journal->filename);
	journal->filename = g_strdup (filename);
}


static int
imap4_journal_entry_write (CamelIMAP4JournalEntry *entry, FILE *out)
{
	if (camel_file_util_encode_uint32 (out, entry->type) == -1)
		return -1;
	
	switch (entry->type) {
	case CAMEL_IMAP4_JOURNAL_ENTRY_APPEND:
		if (camel_file_util_encode_string (out, entry->v.append_uid))
			return -1;
		
		break;
	default:
		g_assert_not_reached ();
	}
	
	return 0;
}


int
camel_imap4_journal_write (CamelIMAP4Journal *journal, CamelException *ex)
{
	CamelIMAP4JournalEntry *entry;
	EDListNode *node;
	FILE *fp;
	int fd;
	
	if ((fd = open (journal->filename, O_CREAT | O_TRUNC | O_WRONLY, 0666)) == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot write IMAP4 offline journal: %s"),
				      g_strerror (errno));
		return -1;
	}
	
	fp = fdopen (fd, "w");
	node = journal->queue.head;
	while (node->next) {
		entry = (CamelIMAP4JournalEntry *) node;
		if (imap4_journal_entry_write (entry, fp) == -1)
			goto exception;
		node = node->next;
	}
	
	if (fsync (fd) == -1)
		goto exception;
	
	fclose (fp);
	
	return 0;
	
 exception:
	
	camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
			      _("Cannot write IMAP4 offline journal: %s"),
			      g_strerror (errno));
	
	fclose (fp);
	
	return -1;
}


static int
imap4_journal_entry_play_append (CamelIMAP4Journal *journal, CamelIMAP4JournalEntry *entry, CamelException *ex)
{
	CamelIMAP4Folder *imap4_folder = journal->folder;
	CamelFolder *folder = (CamelFolder *) imap4_folder;
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
	camel_data_cache_remove (journal->folder->cache, "cache", entry->v.append_uid, NULL);
	
	return 0;
}

static int
imap4_journal_entry_play (CamelIMAP4Journal *journal, CamelIMAP4JournalEntry *entry, CamelException *ex)
{
	switch (entry->type) {
	case CAMEL_IMAP4_JOURNAL_ENTRY_APPEND:
		return imap4_journal_entry_play_append (journal, entry, ex);
	default:
		g_assert_not_reached ();
		return -1;
	}
}


int
camel_imap4_journal_replay (CamelIMAP4Journal *journal, CamelException *ex)
{
	EDListNode *node, *next;
	CamelException lex;
	int failed = 0;
	
	camel_exception_init (&lex);
	
	node = journal->queue.head;
	while (node->next) {
		next = node->next;
		if (imap4_journal_entry_play (journal, (CamelIMAP4JournalEntry *) node, &lex) == -1) {
			if (failed == 0)
				camel_exception_xfer (ex, &lex);
			camel_exception_clear (&lex);
			failed++;
		} else {
			e_dlist_remove (node);
		}
		node = next;
	}
	
	if (failed > 0)
		return -1;
	
	return 0;
}


void
camel_imap4_journal_append (CamelIMAP4Journal *journal, CamelMimeMessage *message, const CamelMessageInfo *mi, char **appended_uid, CamelException *ex)
{
	CamelFolder *folder = (CamelFolder *) journal->folder;
	CamelIMAP4JournalEntry *entry;
	CamelMessageInfoBase *a, *b;
	CamelMessageInfo *info;
	CamelStream *cache;
	guint32 nextuid;
	char *uid;
	
	if (journal->folder->cache == NULL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Cannot append message in offline mode: cache unavailable"));
		return;
	}
	
	nextuid = camel_folder_summary_next_uid (folder->summary);
	uid = g_strdup_printf ("-%u", nextuid);
	
	if (!(cache = camel_data_cache_add (journal->folder->cache, "cache", uid, ex))) {
		folder->summary->nextuid--;
		g_free (uid);
		return;
	}
	
	if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *) message, cache) == -1
	    || camel_stream_flush (cache) == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot append message in offline mode: %s"),
				      g_strerror (errno));
		camel_data_cache_remove (journal->folder->cache, "cache", uid, NULL);
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

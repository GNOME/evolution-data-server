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

#include <glib/gi18n-lib.h>

#include "camel-groupwise-folder.h"
#include "camel-groupwise-journal.h"
#include "camel-groupwise-store.h"

#define d(x)

static void groupwise_entry_free (CamelOfflineJournal *journal, CamelDListNode *entry);
static CamelDListNode *groupwise_entry_load (CamelOfflineJournal *journal, FILE *in);
static gint groupwise_entry_write (CamelOfflineJournal *journal, CamelDListNode *entry, FILE *out);
static gint groupwise_entry_play (CamelOfflineJournal *journal, CamelDListNode *entry, GError **error);

G_DEFINE_TYPE (CamelGroupwiseJournal, camel_groupwise_journal, CAMEL_TYPE_OFFLINE_JOURNAL)

static void
camel_groupwise_journal_class_init (CamelGroupwiseJournalClass *class)
{
	CamelOfflineJournalClass *offline_journal_class;

	offline_journal_class = CAMEL_OFFLINE_JOURNAL_CLASS (class);
	offline_journal_class->entry_free = groupwise_entry_free;
	offline_journal_class->entry_load = groupwise_entry_load;
	offline_journal_class->entry_write = groupwise_entry_write;
	offline_journal_class->entry_play = groupwise_entry_play;
}

static void
camel_groupwise_journal_init (CamelGroupwiseJournal *groupwise_journal)
{
}

static void
groupwise_entry_free (CamelOfflineJournal *journal, CamelDListNode *entry)
{
	CamelGroupwiseJournalEntry *groupwise_entry = (CamelGroupwiseJournalEntry *) entry;

	g_free (groupwise_entry->uid);
	g_free (groupwise_entry->original_uid);
	g_free (groupwise_entry->source_container);
	g_free (groupwise_entry);
}

static CamelDListNode *
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

	return (CamelDListNode *) entry;

 exception:

	if (entry->type == CAMEL_GROUPWISE_JOURNAL_ENTRY_TRANSFER)
		g_free (entry->source_container);

	g_free (entry->uid);
	g_free (entry);

	return NULL;
}

static gint
groupwise_entry_write (CamelOfflineJournal *journal, CamelDListNode *entry, FILE *out)
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

static gint
groupwise_entry_play_append (CamelOfflineJournal *journal, CamelGroupwiseJournalEntry *entry, GError **error)
{
	CamelGroupwiseFolder *gw_folder = (CamelGroupwiseFolder *) journal->folder;
	CamelFolder *folder = journal->folder;
	CamelMimeMessage *message;
	CamelMessageInfo *info;
	CamelStream *stream;
	gboolean success = FALSE;

	/* if the message isn't in the cache, the user went behind our backs so "not our problem" */
	if (!gw_folder->cache || !(stream = camel_data_cache_get (gw_folder->cache, "cache", entry->uid, error))) {
		success = TRUE;
		goto done;
	}

	message = camel_mime_message_new ();
	if (camel_data_wrapper_construct_from_stream ((CamelDataWrapper *) message, stream, error) == -1) {
		g_object_unref (message);
		g_object_unref (stream);
		goto done;
	}

	g_object_unref (stream);

	if (!(info = camel_folder_summary_uid (folder->summary, entry->uid))) {
		/* Note: this should never happen, but rather than crash lets make a new info */
		info = camel_message_info_new (NULL);
	}

	success = camel_folder_append_message (folder, message, info, NULL, error);
	camel_message_info_free (info);
	g_object_unref (message);

done:

	camel_folder_summary_remove_uid (folder->summary, entry->uid);
	camel_data_cache_remove (gw_folder->cache, "cache", entry->uid, NULL);

	return (success == 0);
}

static gint
groupwise_entry_play_transfer (CamelOfflineJournal *journal, CamelGroupwiseJournalEntry *entry, GError **error)
{
	CamelGroupwiseFolder *gw_folder = (CamelGroupwiseFolder *) journal->folder;
	CamelFolder *folder = journal->folder;
	CamelGroupwiseMessageInfo *real;
	CamelMessageInfoBase *info;
	GPtrArray *xuids, *uids;
	CamelFolder *src;
	CamelStore *parent_store;
	const gchar *name;

	parent_store = camel_folder_get_parent_store (folder);

	if (!(info = (CamelMessageInfoBase *) camel_folder_summary_uid (folder->summary, entry->uid))) {
		/* Note: this should never happen, but rather than crash lets make a new info */
		info = camel_message_info_new (NULL);
	}

	name = camel_groupwise_store_folder_lookup ((CamelGroupwiseStore *) parent_store, entry->source_container);
	if (name && (src = camel_store_get_folder (parent_store, name, 0, error))) {
		uids = g_ptr_array_sized_new (1);
		g_ptr_array_add (uids, entry->original_uid);

		if (camel_folder_transfer_messages_to (src, uids, folder, &xuids, FALSE, error)) {
			real = (CamelGroupwiseMessageInfo *) camel_folder_summary_uid (folder->summary, xuids->pdata[0]);

			/* transfer all the system flags, user flags/tags, etc */
			gw_message_info_dup_to ((CamelMessageInfoBase *) real, (CamelMessageInfoBase *) info);
			camel_message_info_free (real);
		} else {
			goto exception;
		}

		g_ptr_array_free (xuids, TRUE);
		g_ptr_array_free (uids, TRUE);
		g_object_unref (src);
	} else if (!name) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot get folder container %s"),
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

static gint
groupwise_entry_play (CamelOfflineJournal *journal, CamelDListNode *entry, GError **error)
{
	CamelGroupwiseJournalEntry *groupwise_entry = (CamelGroupwiseJournalEntry *) entry;

	switch (groupwise_entry->type) {
	case CAMEL_GROUPWISE_JOURNAL_ENTRY_APPEND:
		return groupwise_entry_play_append (journal, groupwise_entry, error);
	case CAMEL_GROUPWISE_JOURNAL_ENTRY_TRANSFER:
		return groupwise_entry_play_transfer (journal, groupwise_entry, error);
	default:
		g_assert_not_reached ();
		return -1;
	}
}

CamelOfflineJournal *
camel_groupwise_journal_new (CamelGroupwiseFolder *folder, const gchar *filename)
{
	CamelOfflineJournal *journal;

	g_return_val_if_fail (CAMEL_IS_GROUPWISE_FOLDER (folder), NULL);

	journal = g_object_new (CAMEL_TYPE_OFFLINE_JOURNAL, NULL);
	camel_offline_journal_construct (journal, (CamelFolder *) folder, filename);

	return journal;
}

static gboolean
update_cache (CamelGroupwiseJournal *groupwise_journal, CamelMimeMessage *message,
	      const CamelMessageInfo *mi, gchar **updated_uid, GError **error)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) groupwise_journal;
	CamelGroupwiseFolder *groupwise_folder = (CamelGroupwiseFolder *) journal->folder;
	CamelFolder *folder = (CamelFolder *) journal->folder;
	CamelMessageInfo *info;
	CamelStream *cache;
	guint32 nextuid;
	gchar *uid;

	if (groupwise_folder->cache == NULL) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot append message in offline mode: cache unavailable"));
		return FALSE;
	}

	nextuid = camel_folder_summary_next_uid (folder->summary);
	uid = g_strdup_printf ("-%u", nextuid);

	if (!(cache = camel_data_cache_add (groupwise_folder->cache, "cache", uid, error))) {
		folder->summary->nextuid--;
		g_free (uid);
		return FALSE;
	}

	if (camel_data_wrapper_write_to_stream (
		(CamelDataWrapper *) message, cache, error) == -1
	    || camel_stream_flush (cache, error) == -1) {
		g_prefix_error (
			error, _("Cannot append message in offline mode: "));
		camel_data_cache_remove (groupwise_folder->cache, "cache", uid, NULL);
		folder->summary->nextuid--;
		g_object_unref (cache);
		g_free (uid);
		return FALSE;
	}

	g_object_unref (cache);

	info = camel_folder_summary_info_new_from_message (folder->summary, message, NULL);
	camel_pstring_free(info->uid);
	info->uid = camel_pstring_strdup (uid);

	gw_message_info_dup_to ((CamelMessageInfoBase *) info, (CamelMessageInfoBase *) mi);

	camel_folder_summary_add (folder->summary, info);

	if (updated_uid)
		*updated_uid = g_strdup (uid);

	g_free (uid);

	return TRUE;
}

gboolean
camel_groupwise_journal_append (CamelGroupwiseJournal *groupwise_journal, CamelMimeMessage *message,
				const CamelMessageInfo *mi, gchar **appended_uid, GError **error)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) groupwise_journal;
	CamelGroupwiseJournalEntry *entry;
	gchar *uid;

	if (!update_cache (groupwise_journal, message, mi, &uid, error))
		return FALSE;

	entry = g_new (CamelGroupwiseJournalEntry, 1);
	entry->type = CAMEL_GROUPWISE_JOURNAL_ENTRY_APPEND;
	entry->uid = uid;

	camel_dlist_addtail (&journal->queue, (CamelDListNode *) entry);

	if (appended_uid)
		*appended_uid = g_strdup (uid);

	return TRUE;
}

gboolean
camel_groupwise_journal_transfer (CamelGroupwiseJournal *groupwise_journal, CamelGroupwiseFolder *source_folder,
				  CamelMimeMessage *message,  const CamelMessageInfo *mi,
				  const gchar *original_uid, gchar **transferred_uid,
				  GError **error)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) groupwise_journal;
	CamelGroupwiseStore *gw_store;
	CamelGroupwiseJournalEntry *entry;
	CamelStore *parent_store;
	gchar *uid;

	parent_store = camel_folder_get_parent_store (journal->folder);
	gw_store = CAMEL_GROUPWISE_STORE (parent_store);

	if (!update_cache (groupwise_journal, message, mi, &uid, error))
		return FALSE;

	entry = g_new (CamelGroupwiseJournalEntry, 1);
	entry->type = CAMEL_GROUPWISE_JOURNAL_ENTRY_APPEND;
	entry->uid = uid;
	entry->original_uid = g_strdup (original_uid);
	entry->source_container = g_strdup (camel_groupwise_store_container_id_lookup (gw_store, camel_folder_get_name (((CamelFolder *)source_folder))));

	camel_dlist_addtail (&journal->queue, (CamelDListNode *) entry);

	if (transferred_uid)
		*transferred_uid = g_strdup (uid);

	return TRUE;
}

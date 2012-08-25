/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *	Chenthill Palanisamy <pchenthill@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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
#include <errno.h>
#include <ctype.h>

#include <glib/gi18n-lib.h>

#include "camel-imap-journal.h"
#include "camel-imap-folder.h"

#define d(x)

static void imap_entry_free (CamelOfflineJournal *journal, gpointer entry);
static gpointer imap_entry_load (CamelOfflineJournal *journal, FILE *in);
static gint imap_entry_write (CamelOfflineJournal *journal, gpointer entry, FILE *out);
static gint imap_entry_play (CamelOfflineJournal *journal, gpointer entry, GCancellable *cancellable, GError **error);
static void unref_folder (gpointer key, gpointer value, gpointer data);
static void free_uids (GPtrArray *array);

G_DEFINE_TYPE (CamelIMAPJournal, camel_imap_journal, CAMEL_TYPE_OFFLINE_JOURNAL)

static void
free_uid (gpointer key,
          gpointer value,
          gpointer data)
{
	g_free (key);
	g_free (value);
}

static void
imap_journal_finalize (GObject *object)
{
	CamelIMAPJournal *journal = CAMEL_IMAP_JOURNAL (object);

	if (journal->folders) {
		g_hash_table_foreach (journal->folders, unref_folder, NULL);
		g_hash_table_destroy (journal->folders);
		journal->folders = NULL;
	}

	if (journal->uidmap) {
		g_hash_table_foreach (journal->uidmap, free_uid, NULL);
		g_hash_table_destroy (journal->uidmap);
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imap_journal_parent_class)->finalize (object);
}

static void
camel_imap_journal_class_init (CamelIMAPJournalClass *class)
{
	GObjectClass *object_class;
	CamelOfflineJournalClass *offline_journal_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = imap_journal_finalize;

	offline_journal_class = CAMEL_OFFLINE_JOURNAL_CLASS (class);
	offline_journal_class->entry_free = imap_entry_free;
	offline_journal_class->entry_load = imap_entry_load;
	offline_journal_class->entry_write = imap_entry_write;
	offline_journal_class->entry_play = imap_entry_play;
}

static void
camel_imap_journal_init (CamelIMAPJournal *journal)
{
	journal->folders = g_hash_table_new (g_str_hash, g_str_equal);
	journal->uidmap = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
unref_folder (gpointer key,
              gpointer value,
              gpointer data)
{
	g_object_unref (value);
}

static void
imap_entry_free (CamelOfflineJournal *journal,
                 gpointer entry)
{
	CamelIMAPJournalEntry *imap_entry = entry;

	switch (imap_entry->type) {
		case CAMEL_IMAP_JOURNAL_ENTRY_EXPUNGE:
			free_uids (imap_entry->uids);
			break;
		case CAMEL_IMAP_JOURNAL_ENTRY_APPEND:
			g_free (imap_entry->append_uid);
			break;
		case CAMEL_IMAP_JOURNAL_ENTRY_TRANSFER:
			free_uids (imap_entry->uids);
			g_free (imap_entry->dest_folder_name);
			break;
	}
	g_free (imap_entry);
}

static void
free_uids (GPtrArray *array)
{
	while (array->len--)
		g_free (array->pdata[array->len]);
	g_ptr_array_free (array, TRUE);
}

static GPtrArray *
copy_uids_array (GPtrArray *array)
{
	GPtrArray *res;
	guint i, sz;

	if (!array)
		return NULL;

	sz = array->len;
	res = g_ptr_array_sized_new (sz);

	for (i = 0; i < sz; i++) {
		g_ptr_array_add (res, g_strdup (g_ptr_array_index (array, i)));
	}

	return res;
}

static GPtrArray *
decode_uids (FILE *file)
{
	GPtrArray *uids;
	gchar *uid;
	guint32 i;

	if (camel_file_util_decode_uint32 (file, &i) == -1)
		return NULL;
	uids = g_ptr_array_new ();
	while (i--) {
		if (camel_file_util_decode_string (file, &uid) == -1) {
			free_uids (uids);
			return NULL;
		}
		g_ptr_array_add (uids, uid);
	}

	return uids;
}

static gpointer
imap_entry_load (CamelOfflineJournal *journal,
                 FILE *in)
{
	CamelIMAPJournalEntry *entry;

	d (g_print ("DEBUG: Loading to  the journal \n"));

	entry = g_malloc0 (sizeof (CamelIMAPJournalEntry));

	if (camel_file_util_decode_uint32 (in, &entry->type) == -1)
		goto exception;

	switch (entry->type) {
	case CAMEL_IMAP_JOURNAL_ENTRY_EXPUNGE:
		entry->uids = decode_uids (in);
		if (!entry->uids)
			goto exception;
	case CAMEL_IMAP_JOURNAL_ENTRY_APPEND:
		if (camel_file_util_decode_string (in, &entry->append_uid) == -1)
			goto exception;

		break;
	case CAMEL_IMAP_JOURNAL_ENTRY_TRANSFER:
		if (camel_file_util_decode_string (in, &entry->dest_folder_name) == -1)
			goto exception;
		entry->uids = decode_uids (in);
		if (!entry->uids)
			goto exception;
		if (camel_file_util_decode_uint32 (in, (guint32 *) &entry->move) == -1)
			goto exception;
		break;
	default:
		goto exception;
	}

	return entry;

 exception:
	switch (entry->type) {
	case CAMEL_IMAP_JOURNAL_ENTRY_APPEND:
		g_free (entry->append_uid);
		break;
	default:
		break;
	}

	g_free (entry);

	return NULL;
}

static gint
encode_uids (FILE *file,
             GPtrArray *uids)
{
	gint i, status;

	status = camel_file_util_encode_uint32 (file, uids->len);
	for (i = 0; status != -1 && i < uids->len; i++)
		status = camel_file_util_encode_string (file, uids->pdata[i]);
	return status;
}

static gint
imap_entry_write (CamelOfflineJournal *journal,
                  gpointer entry,
                  FILE *out)
{
	CamelIMAPJournalEntry *imap_entry = entry;
	GPtrArray *uids = NULL;

	if (camel_file_util_encode_uint32 (out, imap_entry->type) == -1)
		return -1;

	d (g_print ("DEBUG: Writing to  the journal \n"));
	switch (imap_entry->type) {
	case CAMEL_IMAP_JOURNAL_ENTRY_EXPUNGE:
		uids = imap_entry->uids;

		if (encode_uids (out, uids))
			return -1;
	case CAMEL_IMAP_JOURNAL_ENTRY_APPEND:
		if (camel_file_util_encode_string (out, imap_entry->append_uid))
			return -1;

		break;
	case CAMEL_IMAP_JOURNAL_ENTRY_TRANSFER:
		if (camel_file_util_encode_string (out, imap_entry->dest_folder_name))
			return -1;
		if (encode_uids (out, imap_entry->uids))
			return -1;
		if (camel_file_util_encode_uint32 (out, imap_entry->move))
			return -1;
		break;
	default:
		g_assert_not_reached ();
	}

	/* FIXME show error message */
	return 0;
}

static CamelFolder *
journal_decode_folder (CamelIMAPJournal *journal,
                       const gchar *name,
                       GCancellable *cancellable)
{
	CamelFolder *folder;
	CamelOfflineJournal *offline = CAMEL_OFFLINE_JOURNAL (journal);

	folder = g_hash_table_lookup (journal->folders, name);
	if (!folder && offline->folder && g_str_equal (camel_folder_get_full_name (offline->folder), name)) {
		folder = offline->folder;
	}
	if (!folder) {
		CamelStore *parent_store;
		gchar *msg;
		GError *local_error = NULL;

		parent_store = camel_folder_get_parent_store (
			CAMEL_OFFLINE_JOURNAL (journal)->folder);
		folder = camel_store_get_folder_sync (
			parent_store, name, 0, cancellable, &local_error);
		if (folder)
			g_hash_table_insert (
				journal->folders,
				(gchar *) name, folder);
		else {
			msg = g_strdup_printf (
				_("Could not open '%s':\n%s\n"
				"Changes made to this folder "
				"will not be resynchronized."),
				name, local_error->message);
			g_clear_error (&local_error);
			camel_session_alert_user (
				camel_service_get_session (
				CAMEL_SERVICE (parent_store)),
				CAMEL_SESSION_ALERT_WARNING,
				msg, NULL);
			g_free (msg);
		}
	}

	return folder;
}

static gint
imap_entry_play (CamelOfflineJournal *journal,
                 gpointer entry,
                 GCancellable *cancellable,
                 GError **error)
{
	CamelIMAPJournalEntry *imap_entry = entry;

	d (g_print ("DEBUG: PLaying the journal \n"));

	switch (imap_entry->type) {
	case CAMEL_IMAP_JOURNAL_ENTRY_EXPUNGE:
		camel_imap_expunge_uids_resyncing (
			journal->folder, imap_entry->uids, cancellable, NULL);
		return 0;
	case CAMEL_IMAP_JOURNAL_ENTRY_APPEND:
	{
		gchar *ret_uid = NULL;
		CamelMimeMessage *message;
		CamelMessageInfo *info;

		message = camel_folder_get_message_sync (
			journal->folder, imap_entry->append_uid,
			cancellable, error);
		if (!message) {
			/* it seems message gone, just ignore the error and continue;
			 * otherwise the entry would not be removed from the list */
			g_clear_error (error);
			return 0;
		}

		info = camel_folder_get_message_info (journal->folder, imap_entry->append_uid);
		camel_imap_append_resyncing (
			journal->folder, message, info, &ret_uid, cancellable, NULL);
		camel_folder_free_message_info (journal->folder, info);

		if (ret_uid) {
			camel_imap_journal_uidmap_add ((CamelIMAPJournal *) journal, imap_entry->append_uid, ret_uid);
			g_free (ret_uid);
		}

		return 0;
	}
	case CAMEL_IMAP_JOURNAL_ENTRY_TRANSFER:
	{
		CamelFolder *destination;
		GPtrArray *ret_uids;
		gint i;

		destination = journal_decode_folder (
			(CamelIMAPJournal *) journal,
			imap_entry->dest_folder_name, cancellable);
		if (!destination) {
			d (g_print ("Destination folder not found \n"));
			return -1;
		}

		if (!camel_imap_transfer_resyncing (
			journal->folder, imap_entry->uids, destination,
			imap_entry->move, &ret_uids, cancellable, error))
			return -1;

		if (ret_uids) {
			for (i = 0; i < imap_entry->uids->len; i++) {
				if (!ret_uids->pdata[i])
					continue;
				camel_imap_journal_uidmap_add ((CamelIMAPJournal *) journal, imap_entry->uids->pdata[i], ret_uids->pdata[i]);
				g_free (ret_uids->pdata[i]);
			}
			g_ptr_array_free (ret_uids, TRUE);
		}
		d (g_print ("Replay success \n"));
		return 0;
	}
	default:
		g_assert_not_reached ();
		return -1;
	}
}

CamelOfflineJournal *
camel_imap_journal_new (CamelImapFolder *folder,
                        const gchar *filename)
{
	CamelOfflineJournal *journal;

	g_return_val_if_fail (CAMEL_IS_IMAP_FOLDER (folder), NULL);

	d (g_print ("Creating the journal \n"));
	journal = g_object_new (CAMEL_TYPE_IMAP_JOURNAL, NULL);
	camel_offline_journal_construct (journal, (CamelFolder *) folder, filename);

	return journal;
}

void
camel_imap_journal_log (CamelOfflineJournal *journal,
                        CamelOfflineAction action,
                        ...)
{
	CamelIMAPJournalEntry *entry;
	va_list ap;

	if (!journal)
		return;

	entry = g_new0 (CamelIMAPJournalEntry, 1);
	entry->type = action;

	d (g_print ("logging the journal \n"));

	va_start (ap, action);
	switch (entry->type) {
		case CAMEL_IMAP_JOURNAL_ENTRY_EXPUNGE:
		{
			GPtrArray *uids = va_arg (ap, GPtrArray *);

			entry->uids = copy_uids_array (uids);
			break;
		}
		case CAMEL_IMAP_JOURNAL_ENTRY_APPEND:
		{
			gchar *uid = va_arg (ap, gchar *);
			entry->append_uid = g_strdup (uid);
			break;
		}
		case CAMEL_IMAP_JOURNAL_ENTRY_TRANSFER:
		{
			CamelFolder *dest = va_arg (ap, CamelFolder *);
			const gchar *full_name;

			full_name = camel_folder_get_full_name (dest);
			entry->uids = copy_uids_array (va_arg (ap, GPtrArray *));
			entry->move = va_arg (ap, gboolean);
			entry->dest_folder_name = g_strdup (full_name);
			break;
		}
	}

	va_end (ap);

	g_queue_push_tail (&journal->queue, entry);
	camel_offline_journal_write (journal, NULL);
}

static void
close_folder (gchar *name,
              CamelFolder *folder)
{
	g_free (name);
	camel_folder_synchronize_sync (folder, FALSE, NULL, NULL);
	g_object_unref (folder);
}

void
camel_imap_journal_close_folders (CamelIMAPJournal *journal)
{

	if (!journal->folders)
		return;

	g_hash_table_foreach (
		journal->folders, (GHFunc) close_folder, NULL);
	g_hash_table_remove_all (journal->folders);
}

void
camel_imap_journal_uidmap_add (CamelIMAPJournal *journal,
                               const gchar *old_uid,
                               const gchar *new_uid)
{
	g_hash_table_insert (
		journal->uidmap,
		g_strdup (old_uid),
		g_strdup (new_uid));
}

const gchar *
camel_imap_journal_uidmap_lookup (CamelIMAPJournal *journal,
                                  const gchar *uid)
{
	return g_hash_table_lookup (journal->uidmap, uid);
}

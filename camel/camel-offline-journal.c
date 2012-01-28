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
#include <glib/gstdio.h>

#include "camel-data-cache.h"
#include "camel-file-utils.h"
#include "camel-folder-summary.h"
#include "camel-folder.h"
#include "camel-offline-journal.h"
#include "camel-win32.h"

#define d(x)

G_DEFINE_TYPE (CamelOfflineJournal, camel_offline_journal, CAMEL_TYPE_OBJECT)

static void
offline_journal_finalize (GObject *object)
{
	CamelOfflineJournal *journal = CAMEL_OFFLINE_JOURNAL (object);
	gpointer entry;

	g_free (journal->filename);

	while ((entry = g_queue_pop_head (&journal->queue)))
		CAMEL_OFFLINE_JOURNAL_GET_CLASS (journal)->entry_free (journal, entry);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_offline_journal_parent_class)->finalize (object);
}

static void
camel_offline_journal_class_init (CamelOfflineJournalClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = offline_journal_finalize;
}

static void
camel_offline_journal_init (CamelOfflineJournal *journal)
{
	g_queue_init (&journal->queue);
}

/**
 * camel_offline_journal_construct:
 * @journal: a #CamelOfflineJournal object
 * @folder: a #CamelFolder object
 * @filename: a filename to save/load the journal
 *
 * Constructs a journal object.
 **/
void
camel_offline_journal_construct (CamelOfflineJournal *journal,
                                 CamelFolder *folder,
                                 const gchar *filename)
{
	gpointer entry;
	FILE *fp;

	journal->filename = g_strdup (filename);
	journal->folder = folder;

	if ((fp = g_fopen (filename, "rb"))) {
		while ((entry = CAMEL_OFFLINE_JOURNAL_GET_CLASS (journal)->entry_load (journal, fp)))
			g_queue_push_tail (&journal->queue, entry);

		fclose (fp);
	}
}

/**
 * camel_offline_journal_set_filename:
 * @journal: a #CamelOfflineJournal object
 * @filename: a filename to load/save the journal to
 *
 * Set the filename where the journal should load/save from.
 **/
void
camel_offline_journal_set_filename (CamelOfflineJournal *journal,
                                    const gchar *filename)
{
	g_return_if_fail (CAMEL_IS_OFFLINE_JOURNAL (journal));

	g_free (journal->filename);
	journal->filename = g_strdup (filename);
}

/**
 * camel_offline_journal_write:
 * @journal: a #CamelOfflineJournal object
 * @error: return location for a #GError, or %NULL
 *
 * Save the journal to disk.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_offline_journal_write (CamelOfflineJournal *journal,
                             GError **error)
{
	GList *head, *link;
	FILE *fp;
	gint fd;

	if ((fd = g_open (journal->filename, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, 0666)) == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Cannot write offline journal for folder '%s': %s"),
			camel_folder_get_full_name (journal->folder),
			g_strerror (errno));
		return -1;
	}

	fp = fdopen (fd, "w");
	if (!fp)
		goto exception;

	head = g_queue_peek_head_link (&journal->queue);

	for (link = head; link != NULL; link = g_list_next (link)) {
		gpointer entry = link->data;

		if (CAMEL_OFFLINE_JOURNAL_GET_CLASS (journal)->entry_write (journal, entry, fp) == -1)
			goto exception;
	}

	if (fsync (fd) == -1)
		goto exception;

	if (fp)
		fclose (fp);

	return 0;

 exception:

	g_set_error (
		error, G_IO_ERROR,
		g_io_error_from_errno (errno),
		_("Cannot write offline journal for folder '%s': %s"),
		camel_folder_get_full_name (journal->folder),
		g_strerror (errno));

	if (fp)
		fclose (fp);

	return -1;
}

/**
 * camel_offline_journal_replay:
 * @journal: a #CamelOfflineJournal object
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Replay all entries in the journal.
 *
 * Returns: %0 on success (no entry failed to replay) or %-1 on fail
 **/
gint
camel_offline_journal_replay (CamelOfflineJournal *journal,
                              GCancellable *cancellable,
                              GError **error)
{
	GList *head, *link;
	GQueue trash = G_QUEUE_INIT;
	GError *local_error = NULL;
	gint failed = 0;

	head = g_queue_peek_head_link (&journal->queue);

	for (link = head; link != NULL; link = g_list_next (link)) {
		gpointer entry = link->data;

		if (CAMEL_OFFLINE_JOURNAL_GET_CLASS (journal)->entry_play (
			journal, entry, cancellable, &local_error) == -1) {
			if (failed == 0) {
				g_propagate_error (error, local_error);
				local_error = NULL;
			}
			g_clear_error (&local_error);
			failed++;
		} else {
			g_queue_push_tail (&trash, link);
		}
	}

	/* Remove successfully played entries.
	 * XXX Are we leaking entries here? */
	while ((link = g_queue_pop_head (&trash)) != NULL)
		g_queue_delete_link (&journal->queue, link);

	if (failed > 0)
		return -1;

	return 0;
}

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

#ifndef CAMEL_IMAP_JOURNAL_H
#define CAMEL_IMAP_JOURNAL_H

#include <stdarg.h>
#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAP_JOURNAL \
	(camel_imap_journal_get_type ())
#define CAMEL_IMAP_JOURNAL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAP_JOURNAL, CamelIMAPJournal))
#define CAMEL_IMAP_JOURNAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAP_JOURNAL, CamelIMAPJournalClass))
#define CAMEL_IS_IMAP_JOURNAL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAP_JOURNAL))
#define CAMEL_IS_IMAP_JOURNAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAP_JOURNAL))
#define CAMEL_IMAP_JOURNAL_GET_CLASS(obj) \
	(CAMEL_CHECK_GET_CLASS \
	((obj), CAMEL_TYPE_IMAP_JOURNAL, CamelIMAPJournalClass))

G_BEGIN_DECLS

typedef struct _CamelIMAPJournal CamelIMAPJournal;
typedef struct _CamelIMAPJournalClass CamelIMAPJournalClass;
typedef struct _CamelIMAPJournalEntry CamelIMAPJournalEntry;

struct _CamelImapFolder;

typedef enum {
	CAMEL_IMAP_JOURNAL_ENTRY_EXPUNGE,
	CAMEL_IMAP_JOURNAL_ENTRY_APPEND,
	CAMEL_IMAP_JOURNAL_ENTRY_TRANSFER
} CamelOfflineAction;

struct _CamelIMAPJournalEntry {
	CamelDListNode node;

	CamelOfflineAction type;

	GPtrArray *uids;

	gchar *append_uid;
	gchar *dest_folder_name;
	gboolean move;
};

struct _CamelIMAPJournal {
	CamelOfflineJournal parent;

	GHashTable *folders;
	GHashTable *uidmap;
	gint rp_in_progress;
};

struct _CamelIMAPJournalClass {
	CamelOfflineJournalClass parent_class;

};

GType camel_imap_journal_get_type (void);

CamelOfflineJournal *camel_imap_journal_new (struct _CamelImapFolder *folder, const gchar *filename);
void camel_imap_journal_log (CamelOfflineJournal *journal, CamelOfflineAction action, ...);
void camel_imap_journal_uidmap_add (CamelIMAPJournal *journal, const gchar *old_uid, const gchar *n_uid);
const gchar *camel_imap_journal_uidmap_lookup (CamelIMAPJournal *journal, const gchar *uid);
void camel_imap_journal_close_folders (CamelIMAPJournal *journal);

G_END_DECLS

#endif /* CAMEL_IMAP_JOURNAL_H */

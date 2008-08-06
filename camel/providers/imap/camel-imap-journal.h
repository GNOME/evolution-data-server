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

#ifndef __CAMEL_IMAP_JOURNAL_H__
#define __CAMEL_IMAP_JOURNAL_H__

#include <stdarg.h>

#include <glib.h>

#include <camel/camel-list-utils.h>
#include <camel/camel-offline-journal.h>
#include <camel/camel-mime-message.h>

#define CAMEL_TYPE_IMAP_JOURNAL            (camel_imap_journal_get_type ())
#define CAMEL_IMAP_JOURNAL(obj)            (CAMEL_CHECK_CAST ((obj), CAMEL_TYPE_IMAP_JOURNAL, CamelIMAPJournal))
#define CAMEL_IMAP_JOURNAL_CLASS(klass)    (CAMEL_CHECK_CLASS_CAST ((klass), CAMEL_TYPE_IMAP_JOURNAL, CamelIMAPJournalClass))
#define CAMEL_IS_IMAP_JOURNAL(obj)         (CAMEL_CHECK_TYPE ((obj), CAMEL_TYPE_IMAP_JOURNAL))
#define CAMEL_IS_IMAP_JOURNAL_CLASS(klass) (CAMEL_CHECK_CLASS_TYPE ((klass), CAMEL_TYPE_IMAP_JOURNAL))
#define CAMEL_IMAP_JOURNAL_GET_CLASS(obj)  (CAMEL_CHECK_GET_CLASS ((obj), CAMEL_TYPE_IMAP_JOURNAL, CamelIMAPJournalClass))

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

	char *append_uid;
	char *dest_folder_name;
	gboolean move;
};

struct _CamelIMAPJournal {
	CamelOfflineJournal parent_object;

	GHashTable *folders;
	GHashTable *uidmap;	
};

struct _CamelIMAPJournalClass {
	CamelOfflineJournalClass parent_class;
	
};


CamelType camel_imap_journal_get_type (void);

CamelOfflineJournal *camel_imap_journal_new (struct _CamelImapFolder *folder, const char *filename);
void camel_imap_journal_log (CamelOfflineJournal *journal, CamelOfflineAction action, ...);
void camel_imap_journal_uidmap_add (CamelIMAPJournal *journal, const char *old_uid, const char *n_uid);
const char *camel_imap_journal_uidmap_lookup (CamelIMAPJournal *journal, const char *uid);
void camel_imap_journal_close_folders (CamelIMAPJournal *journal);

G_END_DECLS

#endif /* __CAMEL_IMAP_JOURNAL_H__ */

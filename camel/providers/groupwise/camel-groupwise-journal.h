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

#ifndef CAMEL_GROUPWISE_JOURNAL_H
#define CAMEL_GROUPWISE_JOURNAL_H

#include <stdarg.h>
#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_GROUPWISE_JOURNAL \
	(camel_groupwise_journal_get_type ())
#define CAMEL_GROUPWISE_JOURNAL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_GROUPWISE_JOURNAL, CamelGroupwiseJournal))
#define CAMEL_GROUPWISE_JOURNAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_GROUPWISE_JOURNAL, CamelGroupwiseJournalClass))
#define CAMEL_IS_GROUPWISE_JOURNAL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_GROUPWISE_JOURNAL))
#define CAMEL_IS_GROUPWISE_JOURNAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_GROUPWISE_JOURNAL))
#define CAMEL_GROUPWISE_JOURNAL_GET_CLASS(obj) \
	(CAMEL_CHECK_GET_CLASS \
	((obj), CAMEL_TYPE_GROUPWISE_JOURNAL, CamelGroupwiseJournalClass))

G_BEGIN_DECLS

typedef struct _CamelGroupwiseJournal CamelGroupwiseJournal;
typedef struct _CamelGroupwiseJournalClass CamelGroupwiseJournalClass;
typedef struct _CamelGroupwiseJournalEntry CamelGroupwiseJournalEntry;

struct _CamelGroupwiseFolder;

enum {
	CAMEL_GROUPWISE_JOURNAL_ENTRY_APPEND,
	CAMEL_GROUPWISE_JOURNAL_ENTRY_TRANSFER
};

struct _CamelGroupwiseJournalEntry {
	CamelDListNode node;

	guint32 type;

	gchar *uid;
	gchar *original_uid;
	gchar *source_container;
};

struct _CamelGroupwiseJournal {
	CamelOfflineJournal parent;

};

struct _CamelGroupwiseJournalClass {
	CamelOfflineJournalClass parent_class;

};

GType camel_groupwise_journal_get_type (void);

CamelOfflineJournal *camel_groupwise_journal_new (struct _CamelGroupwiseFolder *folder, const gchar *filename);

/* interfaces for adding a journal entry */
void camel_groupwise_journal_append (CamelGroupwiseJournal *journal, CamelMimeMessage *message, const CamelMessageInfo *mi,
				     gchar **appended_uid, CamelException *ex);
void camel_groupwise_journal_transfer (CamelGroupwiseJournal *journal, CamelGroupwiseFolder *source_folder, CamelMimeMessage *message,
				       const CamelMessageInfo *mi, const gchar *orginal_uid, gchar **transferred_uid, CamelException *ex);

G_END_DECLS

#endif /* CAMEL_GROUPWISE_JOURNAL_H */

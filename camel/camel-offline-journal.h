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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_OFFLINE_JOURNAL_H
#define CAMEL_OFFLINE_JOURNAL_H

#include <stdio.h>
#include <stdarg.h>

#include <camel/camel-list-utils.h>
#include <camel/camel-object.h>

/* Standard GObject macros */
#define CAMEL_TYPE_OFFLINE_JOURNAL \
	(camel_offline_journal_get_type ())
#define CAMEL_OFFLINE_JOURNAL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_OFFLINE_JOURNAL, CamelOfflineJournal))
#define CAMEL_OFFLINE_JOURNAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_OFFLINE_JOURNAL, CamelOfflineJournalClass))
#define CAMEL_IS_OFFLINE_JOURNAL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_OFFLINE_JOURNAL))
#define CAMEL_IS_OFFLINE_JOURNAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_OFFLINE_JOURNAL))
#define CAMEL_OFFLINE_JOURNAL_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_OFFLINE_JOURNAL, CamelOfflineJournalClass))

G_BEGIN_DECLS

typedef struct _CamelOfflineJournal CamelOfflineJournal;
typedef struct _CamelOfflineJournalClass CamelOfflineJournalClass;
typedef struct _CamelOfflineJournalEntry CamelOfflineJournalEntry;

struct _CamelFolder;

struct _CamelOfflineJournal {
	CamelObject parent;

	struct _CamelFolder *folder;
	gchar *filename;
	CamelDList queue;
};

struct _CamelOfflineJournalClass {
	CamelObjectClass parent_class;

	/* entry methods */
	void (* entry_free) (CamelOfflineJournal *journal, CamelDListNode *entry);

	CamelDListNode * (* entry_load) (CamelOfflineJournal *journal, FILE *in);
	gint (* entry_write) (CamelOfflineJournal *journal, CamelDListNode *entry, FILE *out);
	gint (* entry_play) (CamelOfflineJournal *journal, CamelDListNode *entry, CamelException *ex);
};

GType camel_offline_journal_get_type (void);

void camel_offline_journal_construct (CamelOfflineJournal *journal, struct _CamelFolder *folder, const gchar *filename);
void camel_offline_journal_set_filename (CamelOfflineJournal *journal, const gchar *filename);

gint camel_offline_journal_write (CamelOfflineJournal *journal, CamelException *ex);
gint camel_offline_journal_replay (CamelOfflineJournal *journal, CamelException *ex);

G_END_DECLS

#endif /* CAMEL_OFFLINE_JOURNAL_H */

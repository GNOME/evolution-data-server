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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */


#ifndef __CAMEL_OFFLINE_JOURNAL_H__
#define __CAMEL_OFFLINE_JOURNAL_H__

#include <stdio.h>
#include <stdarg.h>

#include <glib.h>

#include <libedataserver/e-msgport.h>
#include <camel/camel-object.h>

#define CAMEL_TYPE_OFFLINE_JOURNAL            (camel_offline_journal_get_type ())
#define CAMEL_OFFLINE_JOURNAL(obj)            (CAMEL_CHECK_CAST ((obj), CAMEL_TYPE_OFFLINE_JOURNAL, CamelOfflineJournal))
#define CAMEL_OFFLINE_JOURNAL_CLASS(klass)    (CAMEL_CHECK_CLASS_CAST ((klass), CAMEL_TYPE_OFFLINE_JOURNAL, CamelOfflineJournalClass))
#define CAMEL_IS_OFFLINE_JOURNAL(obj)         (CAMEL_CHECK_TYPE ((obj), CAMEL_TYPE_OFFLINE_JOURNAL))
#define CAMEL_IS_OFFLINE_JOURNAL_CLASS(klass) (CAMEL_CHECK_CLASS_TYPE ((klass), CAMEL_TYPE_OFFLINE_JOURNAL))
#define CAMEL_OFFLINE_JOURNAL_GET_CLASS(o) (CAMEL_OFFLINE_JOURNAL_CLASS (CAMEL_OBJECT_GET_CLASS (o)))

G_BEGIN_DECLS

typedef struct _CamelOfflineJournal CamelOfflineJournal;
typedef struct _CamelOfflineJournalClass CamelOfflineJournalClass;
typedef struct _CamelOfflineJournalEntry CamelOfflineJournalEntry;

struct _CamelFolder;

struct _CamelOfflineJournal {
	CamelObject parent_object;
	
	struct _CamelFolder *folder;
	char *filename;
	EDList queue;
};

struct _CamelOfflineJournalClass {
	CamelObjectClass parent_class;
	
	/* entry methods */
	void (* entry_free) (CamelOfflineJournal *journal, EDListNode *entry);
	
	EDListNode * (* entry_load) (CamelOfflineJournal *journal, FILE *in);
	int (* entry_write) (CamelOfflineJournal *journal, EDListNode *entry, FILE *out);
	int (* entry_play) (CamelOfflineJournal *journal, EDListNode *entry, CamelException *ex);
};


CamelType camel_offline_journal_get_type (void);

void camel_offline_journal_construct (CamelOfflineJournal *journal, struct _CamelFolder *folder, const char *filename);
void camel_offline_journal_set_filename (CamelOfflineJournal *journal, const char *filename);

int camel_offline_journal_write (CamelOfflineJournal *journal, CamelException *ex);
int camel_offline_journal_replay (CamelOfflineJournal *journal, CamelException *ex);

G_END_DECLS

#endif /* __CAMEL_OFFLINE_JOURNAL_H__ */

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


#ifndef __CAMEL_IMAP4_JOURNAL_H__
#define __CAMEL_IMAP4_JOURNAL_H__

#include <stdarg.h>

#include <glib.h>

#include <libedataserver/e-msgport.h>
#include <camel/camel-mime-message.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define CAMEL_TYPE_IMAP4_JOURNAL            (camel_imap4_journal_get_type ())
#define CAMEL_IMAP4_JOURNAL(obj)            (CAMEL_CHECK_CAST ((obj), CAMEL_TYPE_IMAP4_JOURNAL, CamelIMAP4Journal))
#define CAMEL_IMAP4_JOURNAL_CLASS(klass)    (CAMEL_CHECK_CLASS_CAST ((klass), CAMEL_TYPE_IMAP4_JOURNAL, CamelIMAP4JournalClass))
#define CAMEL_IS_IMAP4_JOURNAL(obj)         (CAMEL_CHECK_TYPE ((obj), CAMEL_TYPE_IMAP4_JOURNAL))
#define CAMEL_IS_IMAP4_JOURNAL_CLASS(klass) (CAMEL_CHECK_CLASS_TYPE ((klass), CAMEL_TYPE_IMAP4_JOURNAL))
#define CAMEL_IMAP4_JOURNAL_GET_CLASS(obj)  (CAMEL_CHECK_GET_CLASS ((obj), CAMEL_TYPE_IMAP4_JOURNAL, CamelIMAP4JournalClass))

typedef struct _CamelIMAP4Journal CamelIMAP4Journal;
typedef struct _CamelIMAP4JournalClass CamelIMAP4JournalClass;
typedef struct _CamelIMAP4JournalEntry CamelIMAP4JournalEntry;

struct _CamelIMAP4Folder;

enum {
	CAMEL_IMAP4_JOURNAL_ENTRY_APPEND,
};

struct _CamelIMAP4JournalEntry {
	EDListNode node;
	
	int type;
	
	union {
		char *append_uid;
	} v;
};

struct _CamelIMAP4Journal {
	CamelObject parent_object;
	
	struct _CamelIMAP4Folder *folder;
	char *filename;
	EDList queue;
};

struct _CamelIMAP4JournalClass {
	CamelObjectClass parent_class;
	
};


CamelType camel_imap4_journal_get_type (void);

CamelIMAP4Journal *camel_imap4_journal_new (struct _CamelIMAP4Folder *folder, const char *filename);

void camel_imap4_journal_set_filename (CamelIMAP4Journal *journal, const char *filename);

int camel_imap4_journal_write (CamelIMAP4Journal *journal, CamelException *ex);

int camel_imap4_journal_replay (CamelIMAP4Journal *journal, CamelException *ex);

/* interfaces for adding a journal entry */
void camel_imap4_journal_append (CamelIMAP4Journal *journal, CamelMimeMessage *message, const CamelMessageInfo *mi,
				 char **appended_uid, CamelException *ex);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __CAMEL_IMAP4_JOURNAL_H__ */

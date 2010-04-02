/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Camel
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
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
 *  Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef CAMEL_IMAP4_COMMAND_H
#define CAMEL_IMAP4_COMMAND_H

#include <stdarg.h>

#include <glib.h>

#include <camel/camel-stream.h>
#include <camel/camel-exception.h>
#include <camel/camel-list-utils.h>
#include <camel/camel-data-wrapper.h>

G_BEGIN_DECLS

struct _CamelIMAP4Engine;
struct _CamelIMAP4Folder;
struct _camel_imap4_token_t;

typedef struct _CamelIMAP4Command CamelIMAP4Command;
typedef struct _CamelIMAP4Literal CamelIMAP4Literal;

typedef gint (* CamelIMAP4PlusCallback) (struct _CamelIMAP4Engine *engine,
					CamelIMAP4Command *ic,
					const guchar *linebuf,
					gsize linelen, CamelException *ex);

typedef gint (* CamelIMAP4UntaggedCallback) (struct _CamelIMAP4Engine *engine,
					    CamelIMAP4Command *ic,
					    guint32 index,
					    struct _camel_imap4_token_t *token,
					    CamelException *ex);

typedef void (* CamelIMAP4CommandReset) (CamelIMAP4Command *ic, gpointer user_data);

enum {
	CAMEL_IMAP4_LITERAL_STRING,
	CAMEL_IMAP4_LITERAL_STREAM,
	CAMEL_IMAP4_LITERAL_WRAPPER,
};

struct _CamelIMAP4Literal {
	gint type;
	union {
		gchar *string;
		CamelStream *stream;
		CamelDataWrapper *wrapper;
	} literal;
};

typedef struct _CamelIMAP4CommandPart {
	struct _CamelIMAP4CommandPart *next;
	guchar *buffer;
	gsize buflen;

	CamelIMAP4Literal *literal;
} CamelIMAP4CommandPart;

enum {
	CAMEL_IMAP4_COMMAND_QUEUED,
	CAMEL_IMAP4_COMMAND_ACTIVE,
	CAMEL_IMAP4_COMMAND_COMPLETE,
	CAMEL_IMAP4_COMMAND_ERROR,
};

enum {
	CAMEL_IMAP4_RESULT_NONE,
	CAMEL_IMAP4_RESULT_OK,
	CAMEL_IMAP4_RESULT_NO,
	CAMEL_IMAP4_RESULT_BAD,
};

struct _CamelIMAP4Command {
	CamelDListNode node;

	struct _CamelIMAP4Engine *engine;

	guint ref_count:26;
	guint status:3;
	guint result:3;
	gint id;

	gchar *tag;

	GPtrArray *resp_codes;

	struct _CamelIMAP4Folder *folder;
	CamelException ex;

	/* command parts - logical breaks in the overall command based on literals */
	CamelIMAP4CommandPart *parts;

	/* current part */
	CamelIMAP4CommandPart *part;

	/* untagged handlers */
	GHashTable *untagged;

	/* '+' callback/data */
	CamelIMAP4PlusCallback plus;
	CamelIMAP4CommandReset reset;
	gpointer user_data;
};

CamelIMAP4Command *camel_imap4_command_new (struct _CamelIMAP4Engine *engine, struct _CamelIMAP4Folder *folder,
					    const gchar *format, ...);
CamelIMAP4Command *camel_imap4_command_newv (struct _CamelIMAP4Engine *engine, struct _CamelIMAP4Folder *folder,
					     const gchar *format, va_list args);

void camel_imap4_command_register_untagged (CamelIMAP4Command *ic, const gchar *atom, CamelIMAP4UntaggedCallback untagged);

void camel_imap4_command_ref (CamelIMAP4Command *ic);
void camel_imap4_command_unref (CamelIMAP4Command *ic);

/* returns 1 when complete, 0 if there is more to do, or -1 on error */
gint camel_imap4_command_step (CamelIMAP4Command *ic);

void camel_imap4_command_reset (CamelIMAP4Command *ic);

G_END_DECLS

#endif /* CAMEL_IMAP4_COMMAND_H */

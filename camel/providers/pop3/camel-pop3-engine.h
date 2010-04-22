/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef CAMEL_POP3_ENGINE_H
#define CAMEL_POP3_ENGINE_H

#include <camel/camel.h>

#include "camel-pop3-stream.h"

/* Standard GObject macros */
#define CAMEL_TYPE_POP3_ENGINE \
	(camel_pop3_engine_get_type ())
#define CAMEL_POP3_ENGINE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_POP3_ENGINE, CamelPOP3Engine))
#define CAMEL_POP3_ENGINE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_POP3_ENGINE, CamelPOP3EngineClass))
#define CAMEL_IS_POP3_ENGINE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_POP3_ENGINE))
#define CAMEL_IS_POP3_ENGINE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_POP3_ENGINE))
#define CAMEL_POP3_ENGINE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_POP3_ENGINE, CamelPOP3EngineClass))

G_BEGIN_DECLS

typedef struct _CamelPOP3Engine CamelPOP3Engine;
typedef struct _CamelPOP3EngineClass CamelPOP3EngineClass;
typedef struct _CamelPOP3Command CamelPOP3Command;

/* pop 3 connection states, actually since we're given a connected socket, we always start in auth state */
typedef enum {
	CAMEL_POP3_ENGINE_DISCONNECT = 0,
	CAMEL_POP3_ENGINE_AUTH,
	CAMEL_POP3_ENGINE_TRANSACTION,
	CAMEL_POP3_ENGINE_UPDATE
} camel_pop3_engine_t;

/* state of a command */
typedef enum {
	CAMEL_POP3_COMMAND_IDLE = 0, /* command created or queued, not yet sent (e.g. non pipelined server) */
	CAMEL_POP3_COMMAND_DISPATCHED, /* command sent to server */

	/* completion codes */
	CAMEL_POP3_COMMAND_OK,	/* plain ok response */
	CAMEL_POP3_COMMAND_DATA, /* processing command response */
	CAMEL_POP3_COMMAND_ERR	/* error response */
} camel_pop3_command_t;

/* flags for command types */
enum {
	CAMEL_POP3_COMMAND_SIMPLE = 0, /* dont expect multiline response */
	CAMEL_POP3_COMMAND_MULTI = 1 /* expect multiline response */
};

/* flags for server options */
enum {
	CAMEL_POP3_CAP_APOP = 1<<0,
	CAMEL_POP3_CAP_UIDL = 1<<1,
	CAMEL_POP3_CAP_SASL = 1<<2,
	CAMEL_POP3_CAP_TOP  = 1<<3,
	CAMEL_POP3_CAP_PIPE = 1<<4,
	CAMEL_POP3_CAP_STLS = 1<<5
};

/* enable/disable flags for the engine itself */
enum {
	CAMEL_POP3_ENGINE_DISABLE_EXTENSIONS = 1<<0
};

typedef void (*CamelPOP3CommandFunc)(CamelPOP3Engine *pe, CamelPOP3Stream *stream, gpointer data);

struct _CamelPOP3Command {
	struct _CamelPOP3Command *next;
	struct _CamelPOP3Command *prev;

	guint32 flags;
	camel_pop3_command_t state;

	CamelPOP3CommandFunc func;
	gpointer func_data;

	gint data_size;
	gchar *data;
};

struct _CamelPOP3Engine {
	CamelObject parent;

	guint32 flags;

	camel_pop3_engine_t state;

	GList *auth;		/* authtypes supported */

	guint32 capa;		/* capabilities */
	gchar *apop;		/* apop time string */

	guchar *line;	/* current line buffer */
	guint linelen;

	struct _CamelPOP3Stream *stream;

	guint sentlen;	/* data sent (so we dont overflow network buffer) */

	CamelDList active;	/* active commands */
	CamelDList queue;	/* queue of waiting commands */
	CamelDList done;	/* list of done commands, awaiting free */

	CamelPOP3Command *current; /* currently busy (downloading) response */
};

struct _CamelPOP3EngineClass {
	CamelObjectClass parent_class;
};

GType		  camel_pop3_engine_get_type	(void);

CamelPOP3Engine  *camel_pop3_engine_new		(CamelStream *source, guint32 flags);

void              camel_pop3_engine_reget_capabilities (CamelPOP3Engine *engine);

void              camel_pop3_engine_command_free(CamelPOP3Engine *pe, CamelPOP3Command *pc);

gint		  camel_pop3_engine_iterate	(CamelPOP3Engine *pe, CamelPOP3Command *pc);

CamelPOP3Command *camel_pop3_engine_command_new	(CamelPOP3Engine *pe, guint32 flags, CamelPOP3CommandFunc func, gpointer data, const gchar *fmt, ...);

G_END_DECLS

#endif /* CAMEL_POP3_ENGINE_H */

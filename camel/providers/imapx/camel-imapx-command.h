/*
 * camel-imapx-command.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef CAMEL_IMAPX_COMMAND_H
#define CAMEL_IMAPX_COMMAND_H

#include <camel.h>

#include "camel-imapx-server.h"
#include "camel-imapx-utils.h"

G_BEGIN_DECLS

/* Avoid a circular reference. */
struct _CamelIMAPXJob;

typedef struct _CamelIMAPXCommand CamelIMAPXCommand;
typedef struct _CamelIMAPXCommandPart CamelIMAPXCommandPart;

typedef void	(*CamelIMAPXCommandFunc)	(CamelIMAPXServer *is,
						 CamelIMAPXCommand *ic);

typedef enum {
	CAMEL_IMAPX_COMMAND_SIMPLE = 0,
	CAMEL_IMAPX_COMMAND_DATAWRAPPER,
	CAMEL_IMAPX_COMMAND_STREAM,
	CAMEL_IMAPX_COMMAND_AUTH,
	CAMEL_IMAPX_COMMAND_FILE,
	CAMEL_IMAPX_COMMAND_STRING,
	CAMEL_IMAPX_COMMAND_MASK = 0xff,

	/* Continuation with LITERAL+ */
	CAMEL_IMAPX_COMMAND_LITERAL_PLUS = 1 << 14,

	/* Does this command expect continuation? */
	CAMEL_IMAPX_COMMAND_CONTINUATION = 1 << 15

} CamelIMAPXCommandPartType;

struct _CamelIMAPXCommandPart {
	CamelIMAPXCommandPart *next;
	CamelIMAPXCommandPart *prev;

	CamelIMAPXCommand *parent;

	gint data_size;
	gchar *data;

	CamelIMAPXCommandPartType type;

	gint ob_size;
	gpointer ob;
};

struct _CamelIMAPXCommand {
	CamelIMAPXCommand *next, *prev;

	volatile gint ref_count;

	CamelIMAPXServer *is;
	gint pri;

	/* Command name/type (e.g. FETCH) */
	const gchar *name;

	/* Folder to select */
	CamelFolder *select;

	/* Status for command, indicates it is complete if != NULL. */
	struct _status_info *status;

	/* If the GError is set, it means we were not able to parse
	 * above status, possibly due to cancellation or I/O error. */
	GCancellable *cancellable;
	GError *error;

	guint32 tag;

	/* For building the part. */
	GString *buffer;

	CamelDList parts;
	CamelIMAPXCommandPart *current;

	/* Used for running some commands syncronously. */
	gboolean run_sync_done;
	GCond *run_sync_cond;
	GMutex *run_sync_mutex;

	/* Responsible for free'ing the command. */
	CamelIMAPXCommandFunc complete;
	struct _CamelIMAPXJob *job;
};

CamelIMAPXCommand *
		camel_imapx_command_new		(CamelIMAPXServer *is,
						 const gchar *name,
						 CamelFolder *select,
						 GCancellable *cancellable,
						 const gchar *format,
						 ...);
CamelIMAPXCommand *
		camel_imapx_command_ref		(CamelIMAPXCommand *ic);
void		camel_imapx_command_unref	(CamelIMAPXCommand *ic);
void		camel_imapx_command_add		(CamelIMAPXCommand *ic,
						 const gchar *format,
						 ...);
void		camel_imapx_command_addv	(CamelIMAPXCommand *ic,
						 const gchar *format,
						 va_list ap);
void		camel_imapx_command_add_part	(CamelIMAPXCommand *ic,
						 CamelIMAPXCommandPartType type,
						 gpointer data);
void		camel_imapx_command_close	(CamelIMAPXCommand *ic);

G_END_DECLS

#endif /* CAMEL_IMAPX_COMMAND_H */


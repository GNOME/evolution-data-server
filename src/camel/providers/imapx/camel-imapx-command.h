/*
 * camel-imapx-command.h
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef CAMEL_IMAPX_COMMAND_H
#define CAMEL_IMAPX_COMMAND_H

#include "camel-imapx-utils.h"

#define CAMEL_IS_IMAPX_COMMAND(command) \
	(camel_imapx_command_check (command))

G_BEGIN_DECLS

/* Avoid a circular reference. */
struct _CamelIMAPXServer;

typedef struct _CamelIMAPXCommand CamelIMAPXCommand;
typedef struct _CamelIMAPXCommandPart CamelIMAPXCommandPart;

typedef void	(*CamelIMAPXCommandFunc)	(struct _CamelIMAPXServer *is,
						 CamelIMAPXCommand *ic);

typedef enum {
	CAMEL_IMAPX_COMMAND_SIMPLE = 0,
	CAMEL_IMAPX_COMMAND_DATAWRAPPER,
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
	gint data_size;
	gchar *data;

	CamelIMAPXCommandPartType type;

	gint ob_size;
	gpointer ob;
	gboolean ends_with_crlf;
};

struct _CamelIMAPXCommand {
	struct _CamelIMAPXServer *is;
	gint pri;

	guint32 job_kind; /* CamelIMAPXJobKind */

	/* Status for command. */
	struct _status_info *status;

	guint32 tag;
	gboolean completed;

	GQueue parts;
	GList *current_part;

	/* array of expunged UID-s received during copy/move operation */
	GPtrArray *copy_move_expunged;
	/* folder summary with applied removals from copy_move_expunged;
	   set with the first EXPUNGE untagged response */
	GPtrArray *copy_move_summary;
	CamelFolder *copy_move_folder; /* not referenced */
};

CamelIMAPXCommand *
		camel_imapx_command_new		(struct _CamelIMAPXServer *is,
						 guint32 job_kind,
						 const gchar *format,
						 ...);
CamelIMAPXCommand *
		camel_imapx_command_ref		(CamelIMAPXCommand *ic);
void		camel_imapx_command_unref	(CamelIMAPXCommand *ic);
gboolean	camel_imapx_command_check	(CamelIMAPXCommand *ic);
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


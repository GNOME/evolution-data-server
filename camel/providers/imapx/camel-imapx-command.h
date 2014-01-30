/*
 * camel-imapx-command.h
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef CAMEL_IMAPX_COMMAND_H
#define CAMEL_IMAPX_COMMAND_H

#include "camel-imapx-mailbox.h"
#include "camel-imapx-utils.h"

#define CAMEL_IS_IMAPX_COMMAND(command) \
	(camel_imapx_command_check (command))

G_BEGIN_DECLS

/* Avoid a circular reference. */
struct _CamelIMAPXJob;
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
};

struct _CamelIMAPXCommand {
	struct _CamelIMAPXServer *is;
	gint pri;

	/* Command name/type (e.g. FETCH) */
	const gchar *name;

	/* Status for command, indicates it is complete if != NULL. */
	struct _status_info *status;

	guint32 tag;

	GQueue parts;
	GList *current_part;

	/* Responsible for free'ing the command. */
	CamelIMAPXCommandFunc complete;
};

CamelIMAPXCommand *
		camel_imapx_command_new		(struct _CamelIMAPXServer *is,
						 const gchar *name,
						 CamelIMAPXMailbox *mailbox,
						 const gchar *format,
						 ...);
CamelIMAPXCommand *
		camel_imapx_command_ref		(CamelIMAPXCommand *ic);
void		camel_imapx_command_unref	(CamelIMAPXCommand *ic);
gboolean	camel_imapx_command_check	(CamelIMAPXCommand *ic);
gint		camel_imapx_command_compare	(CamelIMAPXCommand *ic1,
						 CamelIMAPXCommand *ic2);
struct _CamelIMAPXJob *
		camel_imapx_command_get_job	(CamelIMAPXCommand *ic);
void		camel_imapx_command_set_job	(CamelIMAPXCommand *ic,
						 struct _CamelIMAPXJob *job);
CamelIMAPXMailbox *
		camel_imapx_command_ref_mailbox	(CamelIMAPXCommand *ic);
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
void		camel_imapx_command_wait	(CamelIMAPXCommand *ic);
void		camel_imapx_command_done	(CamelIMAPXCommand *ic);
void		camel_imapx_command_failed	(CamelIMAPXCommand *ic,
						 const GError *error);
gboolean	camel_imapx_command_set_error_if_failed
						(CamelIMAPXCommand *ic,
						 GError **error);

/* These are simple GQueue wrappers for CamelIMAPXCommands.
 * They help make sure reference counting is done properly.
 * Add more wrappers as needed, don't circumvent them. */

typedef struct _CamelIMAPXCommandQueue CamelIMAPXCommandQueue;

CamelIMAPXCommandQueue *
		camel_imapx_command_queue_new	(void);
void		camel_imapx_command_queue_free	(CamelIMAPXCommandQueue *queue);
void		camel_imapx_command_queue_transfer
						(CamelIMAPXCommandQueue *from,
						 CamelIMAPXCommandQueue *to);
void		camel_imapx_command_queue_push_tail
						(CamelIMAPXCommandQueue *queue,
						 CamelIMAPXCommand *ic);
void		camel_imapx_command_queue_insert_sorted
						(CamelIMAPXCommandQueue *queue,
						 CamelIMAPXCommand *ic);
gboolean	camel_imapx_command_queue_is_empty
						(CamelIMAPXCommandQueue *queue);
guint		camel_imapx_command_queue_get_length
						(CamelIMAPXCommandQueue *queue);
CamelIMAPXCommand *
		camel_imapx_command_queue_peek_head
						(CamelIMAPXCommandQueue *queue);
GList *		camel_imapx_command_queue_peek_head_link
						(CamelIMAPXCommandQueue *queue);
gboolean	camel_imapx_command_queue_remove
						(CamelIMAPXCommandQueue *queue,
						 CamelIMAPXCommand *ic);
void		camel_imapx_command_queue_delete_link
						(CamelIMAPXCommandQueue *queue,
						 GList *link);
CamelIMAPXCommand *
		camel_imapx_command_queue_ref_by_tag
						(CamelIMAPXCommandQueue *queue,
						 guint32 tag);

G_END_DECLS

#endif /* CAMEL_IMAPX_COMMAND_H */


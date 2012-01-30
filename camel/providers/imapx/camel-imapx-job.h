/*
 * camel-imapx-job.h
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

#ifndef CAMEL_IMAPX_JOB_H
#define CAMEL_IMAPX_JOB_H

#include <camel.h>

#include "camel-imapx-server.h"

G_BEGIN_DECLS

typedef struct _CamelIMAPXJob CamelIMAPXJob;

struct _uidset_state {
	gint entries, uids;
	gint total, limit;
	guint32 start;
	guint32 last;
};

struct _CamelIMAPXJob {
	GCancellable *cancellable;
	GError *error;

	/* Whether to pop a status message off the
	 * GCancellable when the job is finalized. */
	gboolean pop_operation_msg;

	void		(*start)		(CamelIMAPXServer *is,
						 CamelIMAPXJob *job);

	guint noreply:1;	/* dont wait for reply */
	guint32 type;		/* operation type */
	gint pri;		/* the command priority */
	gshort commands;	/* counts how many commands are outstanding */

	CamelFolder *folder;

	union {
		struct {
			/* in: uid requested */
			gchar *uid;
			/* in/out: message content stream output */
			CamelStream *stream;
			/* working variables */
			gsize body_offset;
			gssize body_len;
			gsize fetch_offset;
			gsize size;
			gboolean use_multi_fetch;
		} get_message;
		struct {
			/* array of refresh info's */
			GArray *infos;
			/* used for biulding uidset stuff */
			gint index;
			gint last_index;
			gboolean update_unseen;
			struct _uidset_state uidset;
			/* changes during refresh */
			CamelFolderChangeInfo *changes;
		} refresh_info;
		struct {
			GPtrArray *changed_uids;
			guint32 on_set;
			guint32 off_set;
			GArray *on_user; /* imapx_flag_change */
			GArray *off_user;
			gint unread_change;
		} sync_changes;
		struct {
			gchar *path;
			CamelMessageInfo *info;
		} append_message;
		struct {
			CamelFolder *dest;
			GPtrArray *uids;
			gboolean delete_originals;
			gint index;
			gint last_index;
			struct _uidset_state uidset;
		} copy_messages;
		struct {
			gchar *pattern;
			guint32 flags;
			const gchar *ext;
			GHashTable *folders;
		} list;

		struct {
			const gchar *folder_name;
			gboolean subscribe;
		} manage_subscriptions;

		struct {
			const gchar *ofolder_name;
			const gchar *nfolder_name;
		} rename_folder;

		const gchar *folder_name;
	} u;
};

CamelIMAPXJob *	camel_imapx_job_new		(GCancellable *cancellable);
CamelIMAPXJob *	camel_imapx_job_ref		(CamelIMAPXJob *job);
void		camel_imapx_job_unref		(CamelIMAPXJob *job);
void		camel_imapx_job_wait		(CamelIMAPXJob *job);
void		camel_imapx_job_done		(CamelIMAPXJob *job);
gpointer	camel_imapx_job_get_data	(CamelIMAPXJob *job);
void		camel_imapx_job_set_data	(CamelIMAPXJob *job,
						 gpointer data,
						 GDestroyNotify destroy_data);

G_END_DECLS

#endif /* CAMEL_IMAPX_JOB_H */


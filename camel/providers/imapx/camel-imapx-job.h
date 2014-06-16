/*
 * camel-imapx-job.h
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

#ifndef CAMEL_IMAPX_JOB_H
#define CAMEL_IMAPX_JOB_H

#include "camel-imapx-server.h"

#define CAMEL_IS_IMAPX_JOB(job) \
	(camel_imapx_job_check (job))

G_BEGIN_DECLS

typedef struct _CamelIMAPXJob CamelIMAPXJob;

struct _uidset_state {
	gint entries, uids;
	gint total, limit;
	guint32 start;
	guint32 last;
};

struct _CamelIMAPXJob {
	/* Whether to pop a status message off the
	 * GCancellable when the job is finalized. */
	gboolean pop_operation_msg;

	gboolean	(*start)		(CamelIMAPXJob *job,
						 CamelIMAPXServer *is,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*matches)		(CamelIMAPXJob *job,
						 CamelIMAPXMailbox *mailbox,
						 const gchar *uid);

	guint noreply:1;	/* dont wait for reply */
	guint32 type;		/* operation type */
	gint pri;		/* the command priority */
	volatile gint commands;	/* counts how many commands are outstanding */
};

CamelIMAPXJob *	camel_imapx_job_new		(GCancellable *cancellable);
CamelIMAPXJob *	camel_imapx_job_ref		(CamelIMAPXJob *job);
void		camel_imapx_job_unref		(CamelIMAPXJob *job);
gboolean	camel_imapx_job_check		(CamelIMAPXJob *job);
void		camel_imapx_job_cancel		(CamelIMAPXJob *job);
gboolean	camel_imapx_job_wait		(CamelIMAPXJob *job,
						 GError **error);
void		camel_imapx_job_done		(CamelIMAPXJob *job);
gboolean	camel_imapx_job_run		(CamelIMAPXJob *job,
						 CamelIMAPXServer *is,
						 GError **error);
gboolean	camel_imapx_job_matches		(CamelIMAPXJob *job,
						 CamelIMAPXMailbox *mailbox,
						 const gchar *uid);
gpointer	camel_imapx_job_get_data	(CamelIMAPXJob *job);
void		camel_imapx_job_set_data	(CamelIMAPXJob *job,
						 gpointer data,
						 GDestroyNotify destroy_data);
gboolean	camel_imapx_job_has_mailbox	(CamelIMAPXJob *job,
						 CamelIMAPXMailbox *mailbox);
CamelIMAPXMailbox *
		camel_imapx_job_ref_mailbox	(CamelIMAPXJob *job);
void		camel_imapx_job_set_mailbox	(CamelIMAPXJob *job,
						 CamelIMAPXMailbox *mailbox);
GCancellable *	camel_imapx_job_get_cancellable	(CamelIMAPXJob *job);
void		camel_imapx_job_take_error	(CamelIMAPXJob *job,
						 GError *error);
gboolean	camel_imapx_job_set_error_if_failed
						(CamelIMAPXJob *job,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_IMAPX_JOB_H */


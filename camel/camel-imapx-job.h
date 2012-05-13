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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

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
	GCancellable *cancellable;
	GError *error;

	/* Whether to pop a status message off the
	 * GCancellable when the job is finalized. */
	gboolean pop_operation_msg;

	void		(*start)		(CamelIMAPXJob *job,
						 CamelIMAPXServer *is);
	gboolean	(*matches)		(CamelIMAPXJob *job,
						 CamelFolder *folder,
						 const gchar *uid);

	guint noreply:1;	/* dont wait for reply */
	guint32 type;		/* operation type */
	gint pri;		/* the command priority */
	gshort commands;	/* counts how many commands are outstanding */

	CamelFolder *folder;
};

CamelIMAPXJob *	camel_imapx_job_new		(GCancellable *cancellable);
CamelIMAPXJob *	camel_imapx_job_ref		(CamelIMAPXJob *job);
void		camel_imapx_job_unref		(CamelIMAPXJob *job);
gboolean	camel_imapx_job_check		(CamelIMAPXJob *job);
void		camel_imapx_job_wait		(CamelIMAPXJob *job);
void		camel_imapx_job_done		(CamelIMAPXJob *job);
gboolean	camel_imapx_job_run		(CamelIMAPXJob *job,
						 CamelIMAPXServer *is,
						 GError **error);
gboolean	camel_imapx_job_matches		(CamelIMAPXJob *job,
						 CamelFolder *folder,
						 const gchar *uid);
gpointer	camel_imapx_job_get_data	(CamelIMAPXJob *job);
void		camel_imapx_job_set_data	(CamelIMAPXJob *job,
						 gpointer data,
						 GDestroyNotify destroy_data);

G_END_DECLS

#endif /* CAMEL_IMAPX_JOB_H */


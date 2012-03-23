/*
 * camel-imapx-job.c
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

#include "camel-imapx-job.h"

#include <string.h>

typedef struct _CamelIMAPXRealJob CamelIMAPXRealJob;

/* CamelIMAPXJob + some private bits */
struct _CamelIMAPXRealJob {
	CamelIMAPXJob public;

	volatile gint ref_count;

	/* Used for running some jobs synchronously. */
	GCond *done_cond;
	GMutex *done_mutex;
	gboolean done_flag;

	/* Extra job-specific data. */
	gpointer data;
	GDestroyNotify destroy_data;
};

static void
imapx_job_cancelled_cb (GCancellable *cancellable,
                        CamelIMAPXJob *job)
{
	/* Unblock camel_imapx_run_job() immediately.
	 *
	 * If camel_imapx_job_done() is called sometime later,
	 * the GCond will broadcast but no one will be listening. */

	camel_imapx_job_done (job);
}

CamelIMAPXJob *
camel_imapx_job_new (GCancellable *cancellable)
{
	CamelIMAPXRealJob *real_job;

	if (cancellable != NULL)
		g_object_ref (cancellable);

	real_job = g_slice_new0 (CamelIMAPXRealJob);

	/* Initialize private bits. */
	real_job->ref_count = 1;
	real_job->done_cond = g_cond_new ();
	real_job->done_mutex = g_mutex_new ();

	/* Initialize public bits. */
	real_job->public.cancellable = cancellable;

	return (CamelIMAPXJob *) real_job;
}

CamelIMAPXJob *
camel_imapx_job_ref (CamelIMAPXJob *job)
{
	CamelIMAPXRealJob *real_job;

	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), NULL);

	real_job = (CamelIMAPXRealJob *) job;

	g_atomic_int_inc (&real_job->ref_count);

	return job;
}

void
camel_imapx_job_unref (CamelIMAPXJob *job)
{
	CamelIMAPXRealJob *real_job;

	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	real_job = (CamelIMAPXRealJob *) job;

	if (g_atomic_int_dec_and_test (&real_job->ref_count)) {

		/* Free the public stuff. */

		g_clear_error (&real_job->public.error);

		if (real_job->public.pop_operation_msg)
			camel_operation_pop_message (
				real_job->public.cancellable);

		if (real_job->public.cancellable != NULL)
			g_object_unref (real_job->public.cancellable);

		/* Free the private stuff. */

		g_cond_free (real_job->done_cond);
		g_mutex_free (real_job->done_mutex);

		if (real_job->destroy_data != NULL)
			real_job->destroy_data (real_job->data);

		/* Fill the memory with a bit pattern before releasing
		 * it back to the slab allocator, so we can more easily
		 * identify dangling CamelIMAPXJob pointers. */
		memset (real_job, 0xaa, sizeof (CamelIMAPXRealJob));

		/* But leave the reference count set to zero, so
		 * CAMEL_IS_IMAPX_JOB can identify it as bad. */
		real_job->ref_count = 0;

		g_slice_free (CamelIMAPXRealJob, real_job);
	}
}

gboolean
camel_imapx_job_check (CamelIMAPXJob *job)
{
	CamelIMAPXRealJob *real_job;

	real_job = (CamelIMAPXRealJob *) job;

	return (real_job != NULL && real_job->ref_count > 0);
}

void
camel_imapx_job_wait (CamelIMAPXJob *job)
{
	CamelIMAPXRealJob *real_job;

	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	real_job = (CamelIMAPXRealJob *) job;

	g_mutex_lock (real_job->done_mutex);
	while (!real_job->done_flag)
		g_cond_wait (
			real_job->done_cond,
			real_job->done_mutex);
	g_mutex_unlock (real_job->done_mutex);
}

void
camel_imapx_job_done (CamelIMAPXJob *job)
{
	CamelIMAPXRealJob *real_job;

	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	real_job = (CamelIMAPXRealJob *) job;

	g_mutex_lock (real_job->done_mutex);
	real_job->done_flag = TRUE;
	g_cond_broadcast (real_job->done_cond);
	g_mutex_unlock (real_job->done_mutex);
}

gboolean
camel_imapx_job_run (CamelIMAPXJob *job,
                     CamelIMAPXServer *is,
                     GError **error)
{
	gulong cancel_id = 0;

	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (job->start != NULL, FALSE);

	if (g_cancellable_set_error_if_cancelled (job->cancellable, error))
		return FALSE;

	if (G_IS_CANCELLABLE (job->cancellable))
		cancel_id = g_cancellable_connect (
			job->cancellable,
			G_CALLBACK (imapx_job_cancelled_cb),
			camel_imapx_job_ref (job),
			(GDestroyNotify) camel_imapx_job_unref);

	job->start (job, is);

	if (!job->noreply)
		camel_imapx_job_wait (job);

	if (cancel_id > 0)
		g_cancellable_disconnect (job->cancellable, cancel_id);

	if (g_cancellable_set_error_if_cancelled (job->cancellable, error))
		return FALSE;

	if (job->error != NULL) {
		g_propagate_error (error, job->error);
		job->error = NULL;
		return FALSE;
	}

	return TRUE;
}

gboolean
camel_imapx_job_matches (CamelIMAPXJob *job,
                         CamelFolder *folder,
                         const gchar *uid)
{
	/* XXX CamelFolder can be NULL.  I'm less sure about the
	 *     message UID but let's assume that can be NULL too. */

	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	if (folder != NULL)
		g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	if (job->matches == NULL)
		return FALSE;

	return job->matches (job, folder, uid);
}

gpointer
camel_imapx_job_get_data (CamelIMAPXJob *job)
{
	CamelIMAPXRealJob *real_job;

	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), NULL);

	real_job = (CamelIMAPXRealJob *) job;

	return real_job->data;
}

void
camel_imapx_job_set_data (CamelIMAPXJob *job,
                          gpointer data,
                          GDestroyNotify destroy_data)
{
	CamelIMAPXRealJob *real_job;

	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	real_job = (CamelIMAPXRealJob *) job;

	if (real_job->destroy_data != NULL)
		real_job->destroy_data (real_job->data);

	real_job->data = data;
	real_job->destroy_data = destroy_data;
}


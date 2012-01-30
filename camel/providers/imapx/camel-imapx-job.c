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

	real_job = (CamelIMAPXRealJob *) job;

	g_return_val_if_fail (real_job != NULL, NULL);
	g_return_val_if_fail (real_job->ref_count > 0, NULL);

	g_atomic_int_inc (&real_job->ref_count);

	return job;
}

void
camel_imapx_job_unref (CamelIMAPXJob *job)
{
	CamelIMAPXRealJob *real_job;

	real_job = (CamelIMAPXRealJob *) job;

	g_return_if_fail (real_job != NULL);
	g_return_if_fail (real_job->ref_count > 0);

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

		g_slice_free (CamelIMAPXRealJob, real_job);
	}
}

void
camel_imapx_job_wait (CamelIMAPXJob *job)
{
	CamelIMAPXRealJob *real_job;

	real_job = (CamelIMAPXRealJob *) job;

	g_return_if_fail (real_job != NULL);

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

	real_job = (CamelIMAPXRealJob *) job;

	g_return_if_fail (real_job != NULL);

	g_mutex_lock (real_job->done_mutex);
	real_job->done_flag = TRUE;
	g_cond_broadcast (real_job->done_cond);
	g_mutex_unlock (real_job->done_mutex);
}

gpointer
camel_imapx_job_get_data (CamelIMAPXJob *job)
{
	CamelIMAPXRealJob *real_job;

	real_job = (CamelIMAPXRealJob *) job;

	g_return_val_if_fail (real_job != NULL, NULL);

	return real_job->data;
}

void
camel_imapx_job_set_data (CamelIMAPXJob *job,
                          gpointer data,
                          GDestroyNotify destroy_data)
{
	CamelIMAPXRealJob *real_job;

	real_job = (CamelIMAPXRealJob *) job;

	g_return_if_fail (real_job != NULL);

	if (real_job->destroy_data != NULL)
		real_job->destroy_data (real_job->data);

	real_job->data = data;
	real_job->destroy_data = destroy_data;
}


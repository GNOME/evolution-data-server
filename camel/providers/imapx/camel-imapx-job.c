/*
 * camel-imapx-job.c
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

#include "camel-imapx-job.h"

#include <string.h>

#include "camel-imapx-folder.h"

typedef struct _CamelIMAPXRealJob CamelIMAPXRealJob;

/* CamelIMAPXJob + some private bits */
struct _CamelIMAPXRealJob {
	CamelIMAPXJob public;

	volatile gint ref_count;

	GCancellable *cancellable;

	/* This is set by camel_imapx_job_take_error(),
	 * and propagated through camel_imapx_job_wait(). */
	GError *error;

	/* Used for running some jobs synchronously. */
	GCond done_cond;
	GMutex done_mutex;
	gboolean done_flag;

	/* Extra job-specific data. */
	gpointer data;
	GDestroyNotify destroy_data;

	CamelIMAPXMailbox *mailbox;
	GMutex mailbox_lock;
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

	real_job = g_slice_new0 (CamelIMAPXRealJob);

	/* Initialize private bits. */
	real_job->ref_count = 1;
	g_cond_init (&real_job->done_cond);
	g_mutex_init (&real_job->done_mutex);

	if (cancellable != NULL)
		g_object_ref (cancellable);
	real_job->cancellable = cancellable;

	g_mutex_init (&real_job->mailbox_lock);

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

		if (real_job->public.pop_operation_msg)
			camel_operation_pop_message (real_job->cancellable);

		/* Free the private stuff. */

		if (real_job->cancellable != NULL)
			g_object_unref (real_job->cancellable);

		g_clear_error (&real_job->error);

		g_cond_clear (&real_job->done_cond);
		g_mutex_clear (&real_job->done_mutex);

		if (real_job->destroy_data != NULL)
			real_job->destroy_data (real_job->data);

		g_clear_object (&real_job->mailbox);
		g_mutex_clear (&real_job->mailbox_lock);

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
camel_imapx_job_cancel (CamelIMAPXJob *job)
{
	CamelIMAPXRealJob *real_job;

	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	real_job = (CamelIMAPXRealJob *) job;

	g_cancellable_cancel (real_job->cancellable);
}

/**
 * camel_imapx_job_wait:
 * @job: a #CamelIMAPXJob
 * @error: return location for a #GError, or %NULL
 *
 * Blocks until @job completes by way of camel_imapx_job_done().  If @job
 * completed successfully, the function returns %TRUE.  If @job was given
 * a #GError by way of camel_imapx_job_take_error(), or its #GCancellable
 * was cancelled, the function sets @error and returns %FALSE.
 *
 * Returns: whether @job completed successfully
 *
 * Since: 3.10
 **/
gboolean
camel_imapx_job_wait (CamelIMAPXJob *job,
                      GError **error)
{
	CamelIMAPXRealJob *real_job;
	GCancellable *cancellable;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	real_job = (CamelIMAPXRealJob *) job;
	cancellable = camel_imapx_job_get_cancellable (job);

	g_mutex_lock (&real_job->done_mutex);
	while (!real_job->done_flag)
		g_cond_wait (
			&real_job->done_cond,
			&real_job->done_mutex);
	g_mutex_unlock (&real_job->done_mutex);

	/* Cancellation takes priority over other errors. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		success = FALSE;
	} else if (real_job->error != NULL) {
		/* Copy the error, don't propagate it.
		 * We want our GError to remain intact. */
		if (error != NULL) {
			g_warn_if_fail (*error == NULL);
			*error = g_error_copy (real_job->error);
		}
		success = FALSE;
	}

	return success;
}

void
camel_imapx_job_done (CamelIMAPXJob *job)
{
	CamelIMAPXRealJob *real_job;

	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	real_job = (CamelIMAPXRealJob *) job;

	g_mutex_lock (&real_job->done_mutex);
	real_job->done_flag = TRUE;
	g_cond_broadcast (&real_job->done_cond);
	g_mutex_unlock (&real_job->done_mutex);
}

gboolean
camel_imapx_job_run (CamelIMAPXJob *job,
                     CamelIMAPXServer *is,
                     GError **error)
{
	GCancellable *cancellable;
	gulong cancel_id = 0;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (job->start != NULL, FALSE);

	cancellable = ((CamelIMAPXRealJob *) job)->cancellable;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	if (G_IS_CANCELLABLE (cancellable))
		cancel_id = g_cancellable_connect (
			cancellable,
			G_CALLBACK (imapx_job_cancelled_cb),
			camel_imapx_job_ref (job),
			(GDestroyNotify) camel_imapx_job_unref);

	success = job->start (job, is, cancellable, error);

	if (success && !job->noreply)
		success = camel_imapx_job_wait (job, error);

	if (cancel_id > 0)
		g_cancellable_disconnect (cancellable, cancel_id);

	return success;
}

gboolean
camel_imapx_job_matches (CamelIMAPXJob *job,
                         CamelIMAPXMailbox *mailbox,
                         const gchar *uid)
{
	/* XXX CamelIMAPXMailbox can be NULL.  I'm less sure about
	 *     the message UID but let's assume that can be NULL too. */

	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	if (mailbox != NULL)
		g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);

	if (job->matches == NULL)
		return FALSE;

	return job->matches (job, mailbox, uid);
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

gboolean
camel_imapx_job_has_mailbox (CamelIMAPXJob *job,
                             CamelIMAPXMailbox *mailbox)
{
	CamelIMAPXRealJob *real_job;

	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	if (mailbox != NULL)
		g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);

	real_job = (CamelIMAPXRealJob *) job;

	/* Not necessary to lock the mutex since
	 * we're just comparing memory addresses. */

	return (mailbox == real_job->mailbox);
}

CamelIMAPXMailbox *
camel_imapx_job_ref_mailbox (CamelIMAPXJob *job)
{
	CamelIMAPXRealJob *real_job;
	CamelIMAPXMailbox *mailbox = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), NULL);

	real_job = (CamelIMAPXRealJob *) job;

	g_mutex_lock (&real_job->mailbox_lock);

	if (real_job->mailbox != NULL)
		mailbox = g_object_ref (real_job->mailbox);

	g_mutex_unlock (&real_job->mailbox_lock);

	return mailbox;
}

void
camel_imapx_job_set_mailbox (CamelIMAPXJob *job,
                             CamelIMAPXMailbox *mailbox)
{
	CamelIMAPXRealJob *real_job;

	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	real_job = (CamelIMAPXRealJob *) job;

	if (mailbox != NULL) {
		g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));
		g_object_ref (mailbox);
	}

	g_mutex_lock (&real_job->mailbox_lock);

	g_clear_object (&real_job->mailbox);
	real_job->mailbox = mailbox;

	g_mutex_unlock (&real_job->mailbox_lock);
}

GCancellable *
camel_imapx_job_get_cancellable (CamelIMAPXJob *job)
{
	CamelIMAPXRealJob *real_job;

	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), NULL);

	real_job = (CamelIMAPXRealJob *) job;

	return real_job->cancellable;
}

/**
 * camel_imapx_job_take_error:
 * @job: a #CamelIMAPXJob
 * @error: a #GError
 *
 * Takes over the caller's ownership of @error, so the caller does not
 * need to free it any more.  Call this when a #CamelIMAPXCommand fails
 * and the @job is to be aborted.
 *
 * The @error will be returned to callers of camel_imapx_job_wait() or
 * camel_imapx_job_run().
 *
 * Since: 3.10
 **/
void
camel_imapx_job_take_error (CamelIMAPXJob *job,
                            GError *error)
{
	CamelIMAPXRealJob *real_job;

	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));
	g_return_if_fail (error != NULL);

	real_job = (CamelIMAPXRealJob *) job;
	g_return_if_fail (real_job->error != error);

	g_clear_error (&real_job->error);

	real_job->error = error;  /* takes ownership */
}

/**
 * camel_imapx_job_set_error_if_failed:
 * @job: a #CamelIMAPXJob
 * @error: a location for a #GError
 *
 * Sets @error to a new GError instance and returns TRUE, if the job has set
 * an error or when it was cancelled.
 *
 * Returns: Whether the job failed.
 *
 * Since: 3.12.4
 **/
gboolean
camel_imapx_job_set_error_if_failed (CamelIMAPXJob *job,
				     GError **error)
{
	CamelIMAPXRealJob *real_job;

	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), TRUE);
	g_return_val_if_fail (error != NULL, TRUE);

	real_job = (CamelIMAPXRealJob *) job;

	if (real_job->error) {
		g_propagate_error (error, g_error_copy (real_job->error));
		return TRUE;
	}

	return g_cancellable_set_error_if_cancelled (real_job->cancellable, error);
}

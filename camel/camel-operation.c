/*
 * camel-operation.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA.
 */

#include <config.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#ifdef CAMEL_HAVE_NSS
#include <nspr.h>
#endif

#include "camel-marshal.h"
#include "camel-msgport.h"
#include "camel-operation.h"

#define CAMEL_OPERATION_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_OPERATION, CamelOperationPrivate))

#define PROGRESS_DELAY		250  /* milliseconds */
#define TRANSIENT_DELAY		4    /* seconds */

typedef struct _StatusNode StatusNode;

struct _StatusNode {
	CamelOperation *operation;
	guint source_id;  /* for timeout or idle */
	gchar *message;
	gint percent;
};

struct _CamelOperationPrivate {

	GQueue status_stack;

	CamelMsgPort *cancel_port;
#ifdef CAMEL_HAVE_NSS
	PRFileDesc *cancel_prfd;
#endif
};

enum {
	STATUS,
	LAST_SIGNAL
};

static GStaticRecMutex operation_lock = G_STATIC_REC_MUTEX_INIT;
#define LOCK() g_static_rec_mutex_lock (&operation_lock)
#define UNLOCK() g_static_rec_mutex_unlock (&operation_lock)

static GQueue operation_list = G_QUEUE_INIT;

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (CamelOperation, camel_operation, G_TYPE_CANCELLABLE)

static void
status_node_free (StatusNode *node)
{
	g_free (node->message);

	if (node->source_id > 0)
		g_source_remove (node->source_id);

	g_slice_free (StatusNode, node);
}

static gboolean
operation_emit_status_cb (StatusNode *node)
{
	StatusNode *head_node;
	gchar *message = NULL;
	gint percent = 0;

	/* Keep the operation alive until we emit the signal,
	 * otherwise it might be finalized between unlocking
	 * the mutex and emitting the signal. */
	g_object_ref (node->operation);

	LOCK ();

	node->source_id = 0;

	head_node = g_queue_peek_head (&node->operation->priv->status_stack);

	if (node == head_node) {
		message = g_strdup (node->message);
		percent = node->percent;
	}

	UNLOCK ();

	if (message != NULL) {
		g_signal_emit (
			node->operation,
			signals[STATUS], 0,
			message, percent);
		g_free (message);
	}

	g_object_unref (node->operation);

	return FALSE;
}

static void
operation_flush_msgport (CamelOperation *operation)
{
	CamelOperationPrivate *priv = operation->priv;
	CamelMsg *msg;

	LOCK ();

	while ((msg = camel_msgport_try_pop (priv->cancel_port)) != NULL)
		g_free (msg);

	UNLOCK ();
}

static void
operation_finalize (GObject *object)
{
	CamelOperationPrivate *priv;
	StatusNode *node;

	priv = CAMEL_OPERATION_GET_PRIVATE (object);

	LOCK ();

	g_queue_remove (&operation_list, object);

	operation_flush_msgport (CAMEL_OPERATION (object));
	camel_msgport_destroy (priv->cancel_port);

	while ((node = g_queue_pop_head (&priv->status_stack)) != NULL) {
		g_warning (
			"CamelOperation status stack non-empty: %s",
			node->message);
		status_node_free (node);
	}

	UNLOCK ();

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_operation_parent_class)->finalize (object);
}

static void
camel_operation_class_init (CamelOperationClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelOperationPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = operation_finalize;

	signals[STATUS] = g_signal_new (
		"status",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (CamelOperationClass, status),
		NULL, NULL,
		camel_marshal_VOID__STRING_INT,
		G_TYPE_NONE, 2,
		G_TYPE_STRING,
		G_TYPE_INT);
}

static void
camel_operation_init (CamelOperation *operation)
{
	operation->priv = CAMEL_OPERATION_GET_PRIVATE (operation);

	g_queue_init (&operation->priv->status_stack);
	operation->priv->cancel_port = camel_msgport_new ();

	LOCK ();
	g_queue_push_tail (&operation_list, operation);
	UNLOCK ();
}

/**
 * camel_operation_new:
 *
 * Create a new camel operation handle.  Camel operation handles can
 * be used in a multithreaded application (or a single operation
 * handle can be used in a non threaded appliation) to cancel running
 * operations and to obtain notification messages of the internal
 * status of messages.
 *
 * Returns: A new operation handle.
 **/
GCancellable *
camel_operation_new (void)
{
	return g_object_new (CAMEL_TYPE_OPERATION, NULL);
}

/**
 * camel_operation_cancel:
 * @operation: a #CamelOperation
 *
 * Cancel a given operation.  If @operation is %NULL then all outstanding
 * operations are cancelled.
 **/
void
camel_operation_cancel (CamelOperation *operation)
{
	if (operation != NULL) {
		CamelMsg *msg;

		g_return_if_fail (CAMEL_IS_OPERATION (operation));

		LOCK ();

		msg = g_malloc0 (sizeof (CamelMsg));
		camel_msgport_push (operation->priv->cancel_port, msg);

		UNLOCK ();

		g_cancellable_cancel (G_CANCELLABLE (operation));

	} else {
		GList *link;

		LOCK ();

		link = g_queue_peek_head_link (&operation_list);

		while (link != NULL) {
			operation = link->data;

			if (operation != NULL)
				camel_operation_cancel (operation);

			link = g_list_next (link);
		}

		UNLOCK ();
	}
}

/**
 * camel_operation_uncancel:
 * @operation: a #CamelOperation
 *
 * Uncancel a cancelled operation.
 *
 * This is useful, if e.g. you need to do some cleaning up where a
 * cancellation lying around in the same thread will abort any
 * processing.
 **/
void
camel_operation_uncancel (CamelOperation *operation)
{
	if (operation != NULL) {
		g_return_if_fail (CAMEL_IS_OPERATION (operation));
		operation_flush_msgport (operation);
		g_cancellable_reset (G_CANCELLABLE (operation));
	}
}

/**
 * camel_operation_cancel_check:
 * @operation: a #CamelOperation
 *
 * Check if cancellation has been applied to @operation.
 *
 * Returns: %TRUE if the operation has been cancelled
 **/
gboolean
camel_operation_cancel_check (CamelOperation *operation)
{
	gboolean cancelled;

	if (operation == NULL)
		return FALSE;

	LOCK ();

	cancelled = g_cancellable_is_cancelled (G_CANCELLABLE (operation));

	if (cancelled)
		operation_flush_msgport (operation);

	UNLOCK ();

	return cancelled;
}

/**
 * camel_operation_cancel_fd:
 * @operation: a #CamelOperation
 *
 * Retrieve a file descriptor that can be waited on (select, or poll)
 * for read, to asynchronously detect cancellation.
 *
 * Returns: The fd, or -1 if cancellation has not been registered
 * for this thread.
 **/
gint
camel_operation_cancel_fd (CamelOperation *operation)
{
	if (operation == NULL)
		return -1;

	return g_cancellable_get_fd (G_CANCELLABLE (operation));
}

#ifdef CAMEL_HAVE_NSS
/**
 * camel_operation_cancel_prfd:
 * @operation: a #CamelOperation
 *
 * Retrieve a file descriptor that can be waited on (select, or poll)
 * for read, to asynchronously detect cancellation.
 *
 * Returns: The fd, or %NULL if cancellation has not been registered
 * for this thread.
 **/
PRFileDesc *
camel_operation_cancel_prfd (CamelOperation *operation)
{
	CamelOperationPrivate *priv;

	if (operation == NULL)
		return NULL;

	LOCK ();

	priv = operation->priv;

	if (priv->cancel_prfd == NULL)
		priv->cancel_prfd = camel_msgport_prfd (priv->cancel_port);

	UNLOCK ();

	return priv->cancel_prfd;
}
#endif /* CAMEL_HAVE_NSS */

/**
 * camel_operation_push_message:
 * @cancellable: a #GCancellable or %NULL
 * @format: a standard printf() format string
 * @Varargs: the parameters to insert into the format string
 *
 * Call this function to describe an operation being performed.
 * Call camel_operation_progress() to report progress on the operation.
 * Call camel_operation_pop_message() when the operation is complete.
 *
 * This function only works if @cancellable is a #CamelOperation cast as a
 * #GCancellable.  If @cancellable is a plain #GCancellable or %NULL, the
 * function does nothing and returns silently.
 **/
void
camel_operation_push_message (GCancellable *cancellable,
                              const gchar *format, ...)
{
	CamelOperation *operation;
	StatusNode *node;
	va_list ap;

	if (cancellable == NULL)
		return;

	if (G_OBJECT_TYPE (cancellable) == G_TYPE_CANCELLABLE)
		return;

	g_return_if_fail (CAMEL_IS_OPERATION (cancellable));

	LOCK ();

	operation = CAMEL_OPERATION (cancellable);

	va_start (ap, format);

	node = g_slice_new0 (StatusNode);
	node->message = g_strdup_vprintf (format, ap);
	node->operation = operation;  /* not referenced */

	if (g_queue_is_empty (&operation->priv->status_stack))
		node->source_id = g_idle_add (
			(GSourceFunc) operation_emit_status_cb, node);
	else
		node->source_id = g_timeout_add_seconds (
			TRANSIENT_DELAY, (GSourceFunc)
			operation_emit_status_cb, node);

	g_queue_push_head (&operation->priv->status_stack, node);

	va_end (ap);

	UNLOCK ();
}

/**
 * camel_operation_pop_message:
 * @cancellable: a #GCancellable
 *
 * Pops the most recently pushed message.
 *
 * This function only works if @cancellable is a #CamelOperation cast as a
 * #GCancellable.  If @cancellable is a plain #GCancellable or %NULL, the
 * function does nothing and returns silently.
 **/
void
camel_operation_pop_message (GCancellable *cancellable)
{
	CamelOperation *operation;
	StatusNode *node;

	if (cancellable == NULL)
		return;

	if (G_OBJECT_TYPE (cancellable) == G_TYPE_CANCELLABLE)
		return;

	g_return_if_fail (CAMEL_IS_OPERATION (cancellable));

	LOCK ();

	operation = CAMEL_OPERATION (cancellable);
	node = g_queue_pop_head (&operation->priv->status_stack);

	if (node != NULL)
		status_node_free (node);

	node = g_queue_peek_head (&operation->priv->status_stack);

	if (node != NULL && node->source_id == 0)
		node->source_id = g_idle_add (
			(GSourceFunc) operation_emit_status_cb, node);

	UNLOCK ();
}

/**
 * camel_operation_progress:
 * @cancellable: a #GCancellable or %NULL
 * @percent: percent complete, 0 to 100.
 *
 * Report progress on the current operation.  @percent reports the current
 * percentage of completion, which should be in the range of 0 to 100.
 *
 * This function only works if @cancellable is a #CamelOperation cast as a
 * #GCancellable.  If @cancellable is a plain #GCancellable or %NULL, the
 * function does nothing and returns silently.
 **/
void
camel_operation_progress (GCancellable *cancellable,
                          gint percent)
{
	CamelOperation *operation;
	StatusNode *node;

	if (cancellable == NULL)
		return;

	if (G_OBJECT_TYPE (cancellable) == G_TYPE_CANCELLABLE)
		return;

	g_return_if_fail (CAMEL_IS_OPERATION (cancellable));

	LOCK ();

	operation = CAMEL_OPERATION (cancellable);
	node = g_queue_peek_head (&operation->priv->status_stack);

	if (node != NULL) {
		node->percent = percent;

		/* Rate limit progress updates. */
		if (node->source_id == 0)
			node->source_id = g_timeout_add (
				PROGRESS_DELAY, (GSourceFunc)
				operation_emit_status_cb, node);
	}

	UNLOCK ();
}


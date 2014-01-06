/*
 * camel-operation.c
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#include <nspr.h>

#include "camel-msgport.h"
#include "camel-operation.h"

#define CAMEL_OPERATION_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_OPERATION, CamelOperationPrivate))

#define PROGRESS_DELAY		250  /* milliseconds */
#define TRANSIENT_DELAY		250  /* milliseconds */
#define POP_MESSAGE_DELAY	1    /* seconds */

typedef struct _StatusNode StatusNode;

struct _StatusNode {
	volatile gint ref_count;
	CamelOperation *operation;
	guint source_id;  /* for timeout or idle */
	gchar *message;
	gint percent;
};

struct _CamelOperationPrivate {
	GQueue status_stack;
};

enum {
	STATUS,
	LAST_SIGNAL
};

static GRecMutex operation_lock;
#define LOCK() g_rec_mutex_lock (&operation_lock)
#define UNLOCK() g_rec_mutex_unlock (&operation_lock)

static GQueue operation_list = G_QUEUE_INIT;

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (CamelOperation, camel_operation, G_TYPE_CANCELLABLE)

static StatusNode *
status_node_new (void)
{
	StatusNode *node;

	node = g_slice_new0 (StatusNode);
	node->ref_count = 1;

	return node;
}

static StatusNode *
status_node_ref (StatusNode *node)
{
	g_return_val_if_fail (node != NULL, NULL);
	g_return_val_if_fail (node->ref_count > 0, node);

	g_atomic_int_inc (&node->ref_count);

	return node;
}

static void
status_node_unref (StatusNode *node)
{
	g_return_if_fail (node != NULL);
	g_return_if_fail (node->ref_count > 0);

	if (g_atomic_int_dec_and_test (&node->ref_count)) {

		if (node->operation != NULL)
			g_object_unref (node->operation);

		if (node->source_id > 0)
			g_source_remove (node->source_id);

		g_free (node->message);

		g_slice_free (StatusNode, node);
	}
}

static gboolean
operation_emit_status_cb (gpointer user_data)
{
	StatusNode *node = user_data;
	StatusNode *head_node;
	gboolean emit_status;

	LOCK ();

	node->source_id = 0;

	/* Check if we've been preempted by another StatusNode,
	 * or if we've been cancelled and popped off the stack. */
	head_node = g_queue_peek_head (&node->operation->priv->status_stack);
	emit_status = (node == head_node);

	UNLOCK ();

	if (emit_status)
		g_signal_emit (
			node->operation,
			signals[STATUS], 0,
			node->message,
			node->percent);

	return FALSE;
}

static void
operation_finalize (GObject *object)
{
	CamelOperationPrivate *priv;

	priv = CAMEL_OPERATION_GET_PRIVATE (object);

	LOCK ();

	g_queue_remove (&operation_list, object);

	/* Because each StatusNode holds a reference to its
	 * CamelOperation, the fact that we're being finalized
	 * implies the stack should be empty now. */
	g_warn_if_fail (g_queue_is_empty (&priv->status_stack));

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
		NULL, NULL, NULL,
		G_TYPE_NONE, 2,
		G_TYPE_STRING,
		G_TYPE_INT);
}

static void
camel_operation_init (CamelOperation *operation)
{
	operation->priv = CAMEL_OPERATION_GET_PRIVATE (operation);

	g_queue_init (&operation->priv->status_stack);

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
 * camel_operation_cancel_all:
 *
 * Cancel all outstanding operations.
 **/
void
camel_operation_cancel_all (void)
{
	GList *link;

	LOCK ();

	link = g_queue_peek_head_link (&operation_list);

	while (link != NULL) {
		GCancellable *cancellable = link->data;

		g_cancellable_cancel (cancellable);

		link = g_list_next (link);
	}

	UNLOCK ();
}

/**
 * camel_operation_push_message:
 * @cancellable: a #GCancellable or %NULL
 * @format: a standard printf() format string
 * @...: the parameters to insert into the format string
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

	node = status_node_new ();
	node->message = g_strdup_vprintf (format, ap);
	node->operation = g_object_ref (operation);

	if (g_queue_is_empty (&operation->priv->status_stack)) {
		node->source_id = g_idle_add_full (
			G_PRIORITY_DEFAULT_IDLE,
			operation_emit_status_cb,
			status_node_ref (node),
			(GDestroyNotify) status_node_unref);
	} else {
		node->source_id = g_timeout_add_full (
			G_PRIORITY_DEFAULT, TRANSIENT_DELAY,
			operation_emit_status_cb,
			status_node_ref (node),
			(GDestroyNotify) status_node_unref);
		g_source_set_name_by_id (
			node->source_id,
			"[camel] operation_emit_status_cb");
	}

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

	if (node != NULL) {
		if (node->source_id > 0) {
			g_source_remove (node->source_id);
			node->source_id = 0;
		}
		status_node_unref (node);
	}

	node = g_queue_peek_head (&operation->priv->status_stack);

	if (node != NULL) {
		if (node->source_id != 0)
			g_source_remove (node->source_id);

		node->source_id = g_timeout_add_seconds_full (
			G_PRIORITY_DEFAULT, POP_MESSAGE_DELAY,
			operation_emit_status_cb,
			status_node_ref (node),
			(GDestroyNotify) status_node_unref);
		g_source_set_name_by_id (
			node->source_id,
			"[camel] operation_emit_status_cb");
	}

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
		if (node->source_id == 0) {
			node->source_id = g_timeout_add_full (
				G_PRIORITY_DEFAULT, PROGRESS_DELAY,
				operation_emit_status_cb,
				status_node_ref (node),
				(GDestroyNotify) status_node_unref);
			g_source_set_name_by_id (
				node->source_id,
				"[camel] operation_emit_status_cb");
		}
	}

	UNLOCK ();
}


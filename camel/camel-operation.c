/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Michael Zucchi <NotZed@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#ifdef HAVE_NSS
#include <nspr.h>
#endif

#include "camel-list-utils.h"
#include "camel-operation.h"
#include "camel-msgport.h"

#define d(x)

/* ********************************************************************** */

struct _status_stack {
	guint32 flags;
	gchar *msg;
	gint pc;				/* last pc reported */
	guint stamp;		/* last stamp reported */
};

struct _CamelOperation {
	struct _CamelOperation *next;
	struct _CamelOperation *prev;

	pthread_t id;		/* id of running thread */
	guint32 flags;		/* cancelled ? */
	gint blocked;		/* cancellation blocked depth */
	gint refcount;

	CamelOperationStatusFunc status;
	gpointer status_data;
	guint status_update;

	/* stack of status messages (struct _status_stack *) */
	GSList *status_stack;
	struct _status_stack *lastreport;

	CamelMsgPort *cancel_port;
	gint cancel_fd;
#ifdef HAVE_NSS
	PRFileDesc *cancel_prfd;
#endif
};

#define CAMEL_OPERATION_CANCELLED (1<<0)
#define CAMEL_OPERATION_TRANSIENT (1<<1)

/* Delay before a transient operation has any effect on the status */
#define CAMEL_OPERATION_TRANSIENT_DELAY (5)

static pthread_mutex_t operation_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&operation_lock)
#define UNLOCK() pthread_mutex_unlock(&operation_lock)

static guint stamp (void);
static CamelDList operation_list = CAMEL_DLIST_INITIALISER(operation_list);
static pthread_key_t operation_key;
static pthread_once_t operation_once = PTHREAD_ONCE_INIT;

typedef struct _CamelOperationMsg {
	CamelMsg msg;
} CamelOperationMsg;

static void
co_createspecific(void)
{
	pthread_key_create(&operation_key, NULL);
}

static CamelOperation *
co_getcc(void)
{
	pthread_once(&operation_once, co_createspecific);

	return (CamelOperation *)pthread_getspecific(operation_key);
}

/**
 * camel_operation_new:
 * @status: Callback for receiving status messages.  This will always
 * be called with an internal lock held.
 * @status_data: User data.
 *
 * Create a new camel operation handle.  Camel operation handles can
 * be used in a multithreaded application (or a single operation
 * handle can be used in a non threaded appliation) to cancel running
 * operations and to obtain notification messages of the internal
 * status of messages.
 *
 * Returns: A new operation handle.
 **/
CamelOperation *
camel_operation_new (CamelOperationStatusFunc status, gpointer status_data)
{
	CamelOperation *cc;

	cc = g_malloc0(sizeof(*cc));

	cc->flags = 0;
	cc->blocked = 0;
	cc->refcount = 1;
	cc->status = status;
	cc->status_data = status_data;
	cc->cancel_port = camel_msgport_new();
	cc->cancel_fd = -1;

	LOCK();
	camel_dlist_addtail(&operation_list, (CamelDListNode *)cc);
	UNLOCK();

	return cc;
}

/**
 * camel_operation_mute:
 * @cc:
 *
 * mutes a camel operation permanently.  from this point on you will never
 * receive operation updates, even if more are sent.
 **/
void
camel_operation_mute(CamelOperation *cc)
{
	LOCK();
	cc->status = NULL;
	cc->status_data = NULL;
	UNLOCK();
}

/**
 * camel_operation_registered:
 *
 * Returns: the registered operation, or %NULL if none registered.
 **/
CamelOperation *
camel_operation_registered (void)
{
	CamelOperation *cc = co_getcc();

	if (cc)
		camel_operation_ref(cc);

	return cc;
}

/**
 * camel_operation_ref:
 * @cc: operation context
 *
 * Add a reference to the CamelOperation @cc.
 **/
void
camel_operation_ref (CamelOperation *cc)
{
	g_assert(cc->refcount > 0);

	LOCK();
	cc->refcount++;
	UNLOCK();
}

/**
 * camel_operation_unref:
 * @cc: operation context
 *
 * Unref and potentially free @cc.
 **/
void
camel_operation_unref (CamelOperation *cc)
{
	GSList *n;

	g_assert(cc->refcount > 0);

	LOCK();
	if (cc->refcount == 1) {
		CamelOperationMsg *msg;

		camel_dlist_remove((CamelDListNode *)cc);

		while ((msg = (CamelOperationMsg *)camel_msgport_try_pop(cc->cancel_port)))
			g_free(msg);

		camel_msgport_destroy(cc->cancel_port);

		n = cc->status_stack;
		while (n) {
			g_warning("Camel operation status stack non empty: %s", (gchar *)n->data);
			g_free(n->data);
			n = n->next;
		}
		g_slist_free(cc->status_stack);

		g_free(cc);
	} else {
		cc->refcount--;
	}
	UNLOCK();
}

/**
 * camel_operation_cancel:
 * @cc: operation context
 *
 * Cancel a given operation.  If @cc is NULL then all outstanding
 * operations are cancelled.
 **/
void
camel_operation_cancel (CamelOperation *cc)
{
	CamelOperationMsg *msg;

	LOCK();

	if (cc == NULL) {
		CamelOperation *cn;

		cc = (CamelOperation *)operation_list.head;
		cn = cc->next;
		while (cn) {
			cc->flags |= CAMEL_OPERATION_CANCELLED;
			msg = g_malloc0(sizeof(*msg));
			camel_msgport_push(cc->cancel_port, (CamelMsg *)msg);
			cc = cn;
			cn = cn->next;
		}
	} else if ((cc->flags & CAMEL_OPERATION_CANCELLED) == 0) {
		d(printf("cancelling thread %d\n", cc->id));

		cc->flags |= CAMEL_OPERATION_CANCELLED;
		msg = g_malloc0(sizeof(*msg));
		camel_msgport_push(cc->cancel_port, (CamelMsg *)msg);
	}

	UNLOCK();
}

/**
 * camel_operation_uncancel:
 * @cc: operation context
 *
 * Uncancel a cancelled operation.  If @cc is NULL then the current
 * operation is uncancelled.
 *
 * This is useful, if e.g. you need to do some cleaning up where a
 * cancellation lying around in the same thread will abort any
 * processing.
 **/
void
camel_operation_uncancel(CamelOperation *cc)
{
	if (cc == NULL)
		cc = co_getcc();

	if (cc) {
		CamelOperationMsg *msg;

		LOCK();
		while ((msg = (CamelOperationMsg *)camel_msgport_try_pop(cc->cancel_port)))
			g_free(msg);

		cc->flags &= ~CAMEL_OPERATION_CANCELLED;
		UNLOCK();
	}
}

/**
 * camel_operation_register:
 * @cc: operation context
 *
 * Register a thread or the main thread for cancellation through @cc.
 * If @cc is NULL, then a new cancellation is created for this thread.
 *
 * All calls to operation_register() should save their value and call
 * operation_register again with that, to automatically stack
 * registrations.
 *
 * Returns: the previously registered operatoin.
 *
 **/
CamelOperation *
camel_operation_register (CamelOperation *cc)
{
	CamelOperation *oldcc = co_getcc();

	pthread_setspecific(operation_key, cc);

	return oldcc;
}

/**
 * camel_operation_unregister:
 * @cc: operation context
 *
 * Unregister the current thread for all cancellations.
 **/
void
camel_operation_unregister (CamelOperation *cc)
{
	pthread_once(&operation_once, co_createspecific);
	pthread_setspecific(operation_key, NULL);
}

/**
 * camel_operation_cancel_check:
 * @cc: operation context
 *
 * Check if cancellation has been applied to @cc.  If @cc is NULL,
 * then the CamelOperation registered for the current thread is used.
 *
 * Returns: TRUE if the operation has been cancelled.
 **/
gboolean
camel_operation_cancel_check (CamelOperation *cc)
{
	CamelOperationMsg *msg;
	gint cancelled;

	d(printf("checking for cancel in thread %d\n", pthread_self()));

	if (cc == NULL)
		cc = co_getcc();

	LOCK();

	if (cc == NULL || cc->blocked > 0) {
		d(printf("ahah!  cancellation is blocked\n"));
		cancelled = FALSE;
	} else if (cc->flags & CAMEL_OPERATION_CANCELLED) {
		d(printf("previously cancelled\n"));
		cancelled = TRUE;
	} else if ((msg = (CamelOperationMsg *)camel_msgport_try_pop(cc->cancel_port))) {
		d(printf("Got cancellation message\n"));
		do {
			g_free(msg);
		} while ((msg = (CamelOperationMsg *)camel_msgport_try_pop(cc->cancel_port)));
		cc->flags |= CAMEL_OPERATION_CANCELLED;
		cancelled = TRUE;
	} else
		cancelled = FALSE;

	UNLOCK();

	return cancelled;
}

/**
 * camel_operation_cancel_fd:
 * @cc: operation context
 *
 * Retrieve a file descriptor that can be waited on (select, or poll)
 * for read, to asynchronously detect cancellation.
 *
 * Returns: The fd, or -1 if cancellation is not available
 * (blocked, or has not been registered for this thread).
 **/
gint
camel_operation_cancel_fd (CamelOperation *cc)
{
	if (cc == NULL)
		cc = co_getcc();

	if (cc == NULL || cc->blocked)
		return -1;

	LOCK();

	if (cc->cancel_fd == -1)
		cc->cancel_fd = camel_msgport_fd(cc->cancel_port);

	UNLOCK();

	return cc->cancel_fd;
}

#ifdef HAVE_NSS
/**
 * camel_operation_cancel_prfd:
 * @cc: operation context
 *
 * Retrieve a file descriptor that can be waited on (select, or poll)
 * for read, to asynchronously detect cancellation.
 *
 * Returns: The fd, or NULL if cancellation is not available
 * (blocked, or has not been registered for this thread).
 **/
PRFileDesc *
camel_operation_cancel_prfd (CamelOperation *cc)
{
	if (cc == NULL)
		cc = co_getcc();

	if (cc == NULL || cc->blocked)
		return NULL;

	LOCK();

	if (cc->cancel_prfd == NULL)
		cc->cancel_prfd = camel_msgport_prfd(cc->cancel_port);

	UNLOCK();

	return cc->cancel_prfd;
}
#endif /* HAVE_NSS */

/**
 * camel_operation_start:
 * @cc: operation context
 * @what: action being performed (printf-style format string)
 * @Varargs: varargs
 *
 * Report the start of an operation.  All start operations should have
 * similar end operations.
 **/
void
camel_operation_start (CamelOperation *cc, const gchar *what, ...)
{
	va_list ap;
	gchar *msg;
	struct _status_stack *s;

	if (cc == NULL)
		cc = co_getcc();

	if (cc == NULL)
		return;

	LOCK();

	if (cc->status == NULL) {
		UNLOCK();
		return;
	}

	va_start(ap, what);
	msg = g_strdup_vprintf(what, ap);
	va_end(ap);
	cc->status_update = 0;
	s = g_malloc0(sizeof(*s));
	s->msg = msg;
	s->flags = 0;
	cc->lastreport = s;
	cc->status_stack = g_slist_prepend(cc->status_stack, s);

	UNLOCK();

	cc->status(cc, msg, CAMEL_OPERATION_START, cc->status_data);

	d(printf("start '%s'\n", msg, pc));
}

/**
 * camel_operation_start_transient:
 * @cc: operation context
 * @what: printf-style format string describing the action being performed
 * @Varargs: varargs
 *
 * Start a transient event.  We only update this to the display if it
 * takes very long to process, and if we do, we then go back to the
 * previous state when finished.
 **/
void
camel_operation_start_transient (CamelOperation *cc, const gchar *what, ...)
{
	va_list ap;
	gchar *msg;
	struct _status_stack *s;

	if (cc == NULL)
		cc = co_getcc();

	if (cc == NULL || cc->status == NULL)
		return;

	LOCK();

	va_start(ap, what);
	msg = g_strdup_vprintf(what, ap);
	va_end(ap);
	cc->status_update = 0;
	s = g_malloc0(sizeof(*s));
	s->msg = msg;
	s->flags = CAMEL_OPERATION_TRANSIENT;
	s->stamp = stamp();
	cc->status_stack = g_slist_prepend(cc->status_stack, s);
	d(printf("start '%s'\n", msg, pc));

	UNLOCK();

	/* we dont report it yet */
	/*cc->status(cc, msg, CAMEL_OPERATION_START, cc->status_data);*/
}

static guint stamp(void)
{
	GTimeVal tv;

	g_get_current_time(&tv);
	/* update 4 times/second */
	return (tv.tv_sec * 4) + tv.tv_usec / (1000000/4);
}

/**
 * camel_operation_progress:
 * @cc: Operation to report to.
 * @pc: Percent complete, 0 to 100.
 *
 * Report progress on the current operation.  If @cc is NULL, then the
 * currently registered operation is used.  @pc reports the current
 * percentage of completion, which should be in the range of 0 to 100.
 *
 * If the total percentage is not know, then use
 * camel_operation_progress_count().
 **/
void
camel_operation_progress (CamelOperation *cc, gint pc)
{
	guint now;
	struct _status_stack *s;
	gchar *msg = NULL;

	if (cc == NULL)
		cc = co_getcc();

	if (cc == NULL)
		return;

	LOCK();

	if (cc->status == NULL || cc->status_stack == NULL) {
		UNLOCK();
		return;
	}

	s = cc->status_stack->data;
	s->pc = pc;

	/* Transient messages dont start updating till 4 seconds after
	   they started, then they update every second */
	now = stamp();
	if (cc->status_update == now) {
		cc = NULL;
	} else if (s->flags & CAMEL_OPERATION_TRANSIENT) {
		if (s->stamp + CAMEL_OPERATION_TRANSIENT_DELAY > now) {
			cc = NULL;
		} else {
			cc->status_update = now;
			cc->lastreport = s;
			msg = g_strdup(s->msg);
		}
	} else {
		s->stamp = cc->status_update = now;
		cc->lastreport = s;
		msg = g_strdup(s->msg);
	}

	UNLOCK();

	if (cc) {
		cc->status(cc, msg, pc, cc->status_data);
		g_free(msg);
	}
}

/**
 * camel_operation_end:
 * @cc: operation context
 *
 * Report the end of an operation.  If @cc is NULL, then the currently
 * registered operation is notified.
 **/
void
camel_operation_end (CamelOperation *cc)
{
	struct _status_stack *s, *p;
	guint now;
	gchar *msg = NULL;
	gint pc = 0;

	if (cc == NULL)
		cc = co_getcc();

	if (cc == NULL)
		return;

	LOCK();

	if (cc->status == NULL || cc->status_stack == NULL) {
		UNLOCK();
		return;
	}

	/* so what we do here is this.  If the operation that just
	 * ended was transient, see if we have any other transient
	 * messages that haven't been updated yet above us, otherwise,
	 * re-update as a non-transient at the last reported pc */
	now = stamp();
	s = cc->status_stack->data;
	if (s->flags & CAMEL_OPERATION_TRANSIENT) {
		if (cc->lastreport == s) {
			GSList *l = cc->status_stack->next;
			while (l) {
				p = l->data;
				if (p->flags & CAMEL_OPERATION_TRANSIENT) {
					if (p->stamp + CAMEL_OPERATION_TRANSIENT_DELAY < now) {
						msg = g_strdup(p->msg);
						pc = p->pc;
						cc->lastreport = p;
						break;
					}
				} else {
					msg = g_strdup(p->msg);
					pc = p->pc;
					cc->lastreport = p;
					break;
				}
				l = l->next;
			}
		}
		g_free(s->msg);
	} else {
		msg = s->msg;
		pc = CAMEL_OPERATION_END;
		cc->lastreport = s;
	}
	g_free(s);
	cc->status_stack = g_slist_delete_link(cc->status_stack, cc->status_stack);

	UNLOCK();

	if (msg) {
		cc->status(cc, msg, pc, cc->status_data);
		g_free(msg);
	}
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <time.h>
#include <errno.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

// fixme, use own type funcs
#include <ctype.h>

#include <nspr.h>
#include <prio.h>
#include <prerror.h>
#include <prerr.h>

#include "camel-imapx-command.h"
#include "camel-imapx-utils.h"
#include "camel-imapx-stream.h"
#include "camel-imapx-server.h"
#include "camel-imapx-folder.h"
#include "camel-imapx-settings.h"
#include "camel-imapx-store.h"
#include "camel-imapx-summary.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define c(...) camel_imapx_debug(command, __VA_ARGS__)
#define e(...) camel_imapx_debug(extra, __VA_ARGS__)

#define CIF(x) ((CamelIMAPXFolder *)x)

#define QUEUE_LOCK(x) (g_static_rec_mutex_lock(&(x)->queue_lock))
#define QUEUE_UNLOCK(x) (g_static_rec_mutex_unlock(&(x)->queue_lock))

#define IDLE_LOCK(x) (g_mutex_lock((x)->idle_lock))
#define IDLE_UNLOCK(x) (g_mutex_unlock((x)->idle_lock))

/* All comms with server go here */

/* Try pipelining fetch requests, 'in bits' */
#define MULTI_SIZE (20480)

/* How many outstanding commands do we allow before we just queue them? */
#define MAX_COMMANDS (10)

#define MAX_COMMAND_LEN 1000

extern gint camel_application_is_exiting;

struct _uidset_state {
	struct _CamelIMAPXEngine *ie;
	gint entries, uids;
	gint total, limit;
	guint32 start;
	guint32 last;
};

enum {
	SELECT_CHANGED,
	SHUTDOWN,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

void imapx_uidset_init (struct _uidset_state *ss, gint total, gint limit);
gint imapx_uidset_done (struct _uidset_state *ss, struct _CamelIMAPXCommand *ic);
gint imapx_uidset_add (struct _uidset_state *ss, struct _CamelIMAPXCommand *ic, const gchar *uid);
static gboolean imapx_command_idle_stop (CamelIMAPXServer *is, GError **error);
static gboolean imapx_continuation (CamelIMAPXServer *is, gboolean litplus, GCancellable *cancellable, GError **error);
static gboolean imapx_disconnect (CamelIMAPXServer *is);
static gint imapx_uid_cmp (gconstpointer ap, gconstpointer bp, gpointer data);

typedef gint (*CamelIMAPXEngineFunc)(struct _CamelIMAPXServer *engine, guint32 id, gpointer data);

static gboolean imapx_is_command_queue_empty (CamelIMAPXServer *is);

/* states for the connection? */
enum {
	IMAPX_DISCONNECTED,
	IMAPX_SHUTDOWN,
	IMAPX_CONNECTED,
	IMAPX_AUTHENTICATED,
	IMAPX_INITIALISED,
	IMAPX_SELECTED
};

struct _refresh_info {
	gchar *uid;
	gboolean exists;
	guint32 server_flags;
	CamelFlag *server_user_flags;
};

enum {
	IMAPX_JOB_GET_MESSAGE = 1 << 0,
	IMAPX_JOB_APPEND_MESSAGE = 1 << 1,
	IMAPX_JOB_COPY_MESSAGE = 1 << 2,
	IMAPX_JOB_FETCH_NEW_MESSAGES = 1 << 3,
	IMAPX_JOB_REFRESH_INFO = 1 << 4,
	IMAPX_JOB_SYNC_CHANGES = 1 << 5,
	IMAPX_JOB_EXPUNGE = 1 << 6,
	IMAPX_JOB_NOOP = 1 << 7,
	IMAPX_JOB_IDLE = 1 << 8,
	IMAPX_JOB_LIST = 1 << 9,
	IMAPX_JOB_MANAGE_SUBSCRIPTION = 1 << 10,
	IMAPX_JOB_CREATE_FOLDER = 1 << 11,
	IMAPX_JOB_DELETE_FOLDER = 1 << 12,
	IMAPX_JOB_RENAME_FOLDER = 1 << 13,
};

/* Operations on the store (folder_tree) will have highest priority as we know for sure they are sync
 * and user triggered. */
enum {
	IMAPX_PRIORITY_CREATE_FOLDER = 200,
	IMAPX_PRIORITY_DELETE_FOLDER = 200,
	IMAPX_PRIORITY_RENAME_FOLDER = 200,
	IMAPX_PRIORITY_MANAGE_SUBSCRIPTION = 200,
	IMAPX_PRIORITY_SYNC_CHANGES = 150,
	IMAPX_PRIORITY_EXPUNGE = 150,
	IMAPX_PRIORITY_GET_MESSAGE = 100,
	IMAPX_PRIORITY_REFRESH_INFO = 0,
	IMAPX_PRIORITY_NOOP = 0,
	IMAPX_PRIORITY_NEW_MESSAGES = 0,
	IMAPX_PRIORITY_APPEND_MESSAGE = -60,
	IMAPX_PRIIORITY_COPY_MESSAGE = -60,
	IMAPX_PRIORITY_LIST = -80,
	IMAPX_PRIORITY_IDLE = -100,
	IMAPX_PRIORITY_SYNC_MESSAGE = -120
};

struct _imapx_flag_change {
	GPtrArray *infos;
	gchar *name;
};

typedef struct _CamelIMAPXJob CamelIMAPXJob;

struct _CamelIMAPXJob {
	volatile gint ref_count;

	GCond *done_cond;
	GMutex *done_mutex;
	gboolean done_flag;

	GCancellable *cancellable;
	GError *error;

	/* Whether to pop a status message off the
	 * GCancellable when the job is finalized. */
	gboolean pop_operation_msg;

	void (*start)(CamelIMAPXServer *is, struct _CamelIMAPXJob *job);

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

static CamelIMAPXJob *imapx_match_active_job (CamelIMAPXServer *is, guint32 type, const gchar *uid);
static void imapx_job_done (CamelIMAPXServer *is, CamelIMAPXJob *job);
static gboolean imapx_run_job (CamelIMAPXServer *is, CamelIMAPXJob *job, GError **error);
static void imapx_job_fetch_new_messages_start (CamelIMAPXServer *is, CamelIMAPXJob *job);
static gint imapx_refresh_info_uid_cmp (gconstpointer ap, gconstpointer bp, gboolean ascending);
static gint imapx_uids_array_cmp (gconstpointer ap, gconstpointer bp);
static gboolean imapx_server_sync_changes (CamelIMAPXServer *is, CamelFolder *folder, gint pri, GCancellable *cancellable, GError **error);

static void	imapx_command_copy_messages_step_start
						(CamelIMAPXServer *is,
						 CamelIMAPXJob *job,
						 gint index);

enum _idle_state {
	IMAPX_IDLE_OFF,
	IMAPX_IDLE_PENDING,	/* Queue is idle; waiting to send IDLE command
				   soon if nothing more interesting happens */
	IMAPX_IDLE_ISSUED,	/* Sent IDLE command; waiting for response */
	IMAPX_IDLE_STARTED,	/* IDLE continuation received; IDLE active */
	IMAPX_IDLE_CANCEL,	/* Cancelled from ISSUED state; need to send
				   DONE as soon as we receive continuation */
};
#define IMAPX_IDLE_DWELL_TIME	2 /* Number of seconds to remain in PENDING
				     state waiting for other commands to be
				     queued, before actually sending IDLE */

struct _CamelIMAPXIdle {
	GMutex *idle_lock;
	GThread *idle_thread;

	GCond *start_watch_cond;
	GMutex *start_watch_mutex;
	gboolean start_watch_is_set;

	time_t started;
	enum _idle_state state;
	gboolean idle_exit;
};

static gboolean imapx_in_idle (CamelIMAPXServer *is);
static gboolean imapx_idle_supported (CamelIMAPXServer *is);
static void imapx_start_idle (CamelIMAPXServer *is);
static void imapx_exit_idle (CamelIMAPXServer *is);
static void imapx_init_idle (CamelIMAPXServer *is);
static gboolean imapx_stop_idle (CamelIMAPXServer *is, GError **error);
static gboolean camel_imapx_server_idle (CamelIMAPXServer *is, CamelFolder *folder, GCancellable *cancellable, GError **error);

enum {
	USE_SSL_NEVER,
	USE_SSL_ALWAYS,
	USE_SSL_WHEN_POSSIBLE
};

#define SSL_PORT_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 | CAMEL_TCP_STREAM_SSL_ENABLE_SSL3)
#define STARTTLS_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_TLS)

static gboolean imapx_select (CamelIMAPXServer *is, CamelFolder *folder, gboolean force, GCancellable *cancellable, GError **error);

G_DEFINE_TYPE (CamelIMAPXServer, camel_imapx_server, CAMEL_TYPE_OBJECT)

/*
  this creates a uid (or sequence number) set directly into a command,
  if total is set, then we break it up into total uids. (i.e. command time)
  if limit is set, then we break it up into limit entries (i.e. command length)
*/
void
imapx_uidset_init (struct _uidset_state *ss,
                   gint total,
                   gint limit)
{
	ss->uids = 0;
	ss->entries = 0;
	ss->start = 0;
	ss->last = 0;
	ss->total = total;
	ss->limit = limit;
}

gint
imapx_uidset_done (struct _uidset_state *ss,
                   CamelIMAPXCommand *ic)
{
	gint ret = 0;

	if (ss->last != 0 && ss->last != ss->start) {
		camel_imapx_command_add (ic, ":%d", ss->last);
	}

	ret = ss->last != 0;

	ss->start = 0;
	ss->last = 0;
	ss->uids = 0;
	ss->entries = 0;

	return ret;
}

gint
imapx_uidset_add (struct _uidset_state *ss,
                  CamelIMAPXCommand *ic,
                  const gchar *uid)
{
	guint32 uidn;

	uidn = strtoul (uid, NULL, 10);
	if (uidn == 0)
		return -1;

	ss->uids++;

	e(ic->is->tagprefix, "uidset add '%s'\n", uid);

	if (ss->last == 0) {
		e(ic->is->tagprefix, " start\n");
		camel_imapx_command_add (ic, "%d", uidn);
		ss->entries++;
		ss->start = uidn;
	} else {
		if (ss->last != uidn - 1) {
			if (ss->last == ss->start) {
				e(ic->is->tagprefix, " ,next\n");
				camel_imapx_command_add (ic, ",%d", uidn);
				ss->entries++;
			} else {
				e(ic->is->tagprefix, " :range\n");
				camel_imapx_command_add (ic, ":%d,%d", ss->last, uidn);
				ss->entries+=2;
			}
			ss->start = uidn;
		}
	}

	ss->last = uidn;

	if ((ss->limit && ss->entries >= ss->limit)
	    || (ss->total && ss->uids >= ss->total)) {
		e(ic->is->tagprefix, " done, %d entries, %d uids\n", ss->entries, ss->uids);
		imapx_uidset_done (ss, ic);
		return 1;
	}

	return 0;
}

/* Must hold QUEUE_LOCK */
static gboolean
imapx_command_start (CamelIMAPXServer *is,
                     CamelIMAPXCommand *ic,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelIMAPXCommandPart *cp;
	gboolean cp_continuation;
	gboolean cp_literal_plus;
	GList *head;
	gint retval;

	camel_imapx_command_close (ic);

	head = g_queue_peek_head_link (&ic->parts);
	g_return_val_if_fail (head != NULL, FALSE);
	cp = (CamelIMAPXCommandPart *) head->data;
	ic->current_part = head;

	cp_continuation = ((cp->type & CAMEL_IMAPX_COMMAND_CONTINUATION) != 0);
	cp_literal_plus = ((cp->type & CAMEL_IMAPX_COMMAND_LITERAL_PLUS) != 0);

	/* TODO: If we support literal+ we should be able to write the whole command out
	 * at this point .... >here< */

	if (cp_continuation || cp_literal_plus)
		is->literal = ic;

	g_queue_push_tail (&is->active, ic);

	g_static_rec_mutex_lock (&is->ostream_lock);

	c(is->tagprefix, "Starting command (active=%d,%s) %c%05u %s\r\n", g_queue_get_length (&is->active), is->literal?" literal":"", is->tagprefix, ic->tag, cp->data && g_str_has_prefix (cp->data, "LOGIN") ? "LOGIN..." : cp->data);
	if (is->stream != NULL) {
		gchar *string;

		string = g_strdup_printf ("%c%05u %s\r\n", is->tagprefix, ic->tag, cp->data);
		retval = camel_stream_write_string ((CamelStream *) is->stream, string, cancellable, NULL);
		g_free (string);
	} else
		retval = -1;
	if (retval == -1) {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"Failed to issue the command");
		goto err;
	}
	while (is->literal == ic && cp_literal_plus) {
		/* Sent LITERAL+ continuation immediately */
		if (!imapx_continuation (is, TRUE, cancellable, error))
			goto err;
	}

	g_static_rec_mutex_unlock (&is->ostream_lock);

	return TRUE;

err:
	g_static_rec_mutex_unlock (&is->ostream_lock);

	g_queue_remove (&is->active, ic);

	/* Send a NULL GError since we've already set a
	 * GError to get here, and we're not interested
	 * in individual command errors. */
	if (ic != NULL && ic->complete != NULL)
		ic->complete (is, ic, NULL);

	return FALSE;
}

static gboolean
duplicate_fetch_or_refresh (CamelIMAPXServer *is,
                            CamelIMAPXCommand *ic)
{
	if (!ic->job)
		return FALSE;

	if (!(ic->job->type & (IMAPX_JOB_FETCH_NEW_MESSAGES | IMAPX_JOB_REFRESH_INFO)))
		return FALSE;

	if (imapx_match_active_job (is, IMAPX_JOB_FETCH_NEW_MESSAGES | IMAPX_JOB_REFRESH_INFO, NULL)) {
		c(is->tagprefix, "Not yet sending duplicate fetch/refresh %s command\n", ic->name);
		return TRUE;
	}

	return FALSE;
}

/* See if we can start another task yet.
 *
 * If we're waiting for a literal, we cannot proceed.
 *
 * If we're about to change the folder we're
 * looking at from user-direction, we dont proceed.
 *
 * If we have a folder selected, first see if any
 * jobs are waiting on it, but only if they are
 * at least as high priority as anything we
 * have running.
 *
 * If we dont, select the first folder required,
 * then queue all the outstanding jobs on it, that
 * are at least as high priority as the first.
 *
 * must have QUEUE lock */

static void
imapx_command_start_next (CamelIMAPXServer *is,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelIMAPXCommand *first_ic;
	gint min_pri = -128;

	c(is->tagprefix, "** Starting next command\n");
	if (is->literal) {
		c(is->tagprefix, "* no; waiting for literal '%s'\n", is->literal->name);
		return;
	}

	if (is->select_pending) {
		GQueue start = G_QUEUE_INIT;
		GList *head, *link;

		c(is->tagprefix, "-- Checking job queue for non-folder jobs\n");

		head = g_queue_peek_head_link (&is->queue);

		/* Tag which commands in the queue to start. */
		for (link = head; link != NULL; link = g_list_next (link)) {
			CamelIMAPXCommand *ic = link->data;

			if (ic->pri < min_pri)
				break;

			c(is->tagprefix, "-- %3d '%s'?\n", (gint)ic->pri, ic->name);
			if (!ic->select) {
				c(is->tagprefix, "--> starting '%s'\n", ic->name);
				min_pri = ic->pri;
				g_queue_push_tail (&start, link);
			}

			if (g_queue_get_length (&start) == MAX_COMMANDS)
				break;
		}

		if (g_queue_is_empty (&start))
			c(is->tagprefix, "* no, waiting for pending select '%s'\n", camel_folder_get_full_name (is->select_pending));

		/* Start the tagged commands. */
		while ((link = g_queue_pop_head (&start)) != NULL) {
			CamelIMAPXCommand *ic = link->data;
			g_queue_delete_link (&is->queue, link);
			imapx_command_start (is, ic, cancellable, error);
		}

		return;
	}

	if (imapx_idle_supported (is) && is->state == IMAPX_SELECTED) {
		gboolean empty = imapx_is_command_queue_empty (is);

		if (imapx_in_idle (is) && !g_queue_is_empty (&is->queue)) {
			/* if imapx_stop_idle() returns FALSE, it was only
			 * pending and we can go ahead and send a new command
			 * immediately. If it returns TRUE, either it sent the
			 * DONE to exit IDLE mode, or there was an error.
			 * Either way, we do nothing more right now. */
			if (imapx_stop_idle (is, error)) {
				c(is->tagprefix, "waiting for idle to stop \n");
				return;
			}
		} else if (empty && !imapx_in_idle (is)) {
			imapx_start_idle (is);
			c(is->tagprefix, "starting idle \n");
			return;
		}
	}

	if (g_queue_is_empty (&is->queue)) {
		c(is->tagprefix, "* no, no jobs\n");
		return;
	}

	/* See if any queued jobs on this select first */
	if (is->select_folder) {
		GQueue start = G_QUEUE_INIT;
		GList *head, *link;
		gboolean commands_started = FALSE;

		c(is->tagprefix, "- we're selected on '%s', current jobs?\n",
		  camel_folder_get_full_name (is->select_folder));

		head = g_queue_peek_head_link (&is->active);

		/* Find the highest priority in the active queue. */
		for (link = head; link != NULL; link = g_list_next (link)) {
			CamelIMAPXCommand *ic = link->data;

			min_pri = MAX (min_pri, ic->pri);
			c(is->tagprefix, "-  %3d '%s'\n", (gint)ic->pri, ic->name);
		}

		if (g_queue_get_length (&is->active) >= MAX_COMMANDS) {
			c(is->tagprefix, "** too many jobs busy, waiting for results for now\n");
			return;
		}

		c(is->tagprefix, "-- Checking job queue\n");

		head = g_queue_peek_head_link (&is->queue);

		/* Tag which commands in the queue to start. */
		for (link = head; link != NULL; link = g_list_next (link)) {
			CamelIMAPXCommand *ic = link->data;

			if (is->literal != NULL)
				break;

			if (ic->pri < min_pri)
				break;

			c(is->tagprefix, "-- %3d '%s'?\n", (gint)ic->pri, ic->name);
			if (!ic->select || ((ic->select == is->select_folder) &&
					    !duplicate_fetch_or_refresh (is, ic))) {
				c(is->tagprefix, "--> starting '%s'\n", ic->name);
				min_pri = ic->pri;
				g_queue_push_tail (&start, link);
			} else {
				/* This job isn't for the selected folder, but we don't want to
				 * consider jobs with _lower_ priority than this, even if they
				 * are for the selected folder. */
				min_pri = ic->pri;
			}

			if (g_queue_get_length (&start) == MAX_COMMANDS)
				break;
		}

		/* Start the tagged commands. */
		while ((link = g_queue_pop_head (&start)) != NULL) {
			CamelIMAPXCommand *ic = link->data;
			g_queue_delete_link (&is->queue, link);
			imapx_command_start (is, ic, cancellable, error);
			commands_started = TRUE;
		}

		if (commands_started)
			return;
	}

	/* This won't be NULL because we checked for an empty queue above. */
	first_ic = g_queue_peek_head (&is->queue);

	/* If we need to select a folder for the first command, do it now,
	 * once it is complete it will re-call us if it succeeded. */
	if (first_ic->select) {
		c(is->tagprefix, "Selecting folder '%s' for command '%s'(%p)\n",
		  camel_folder_get_full_name (first_ic->select),
		  first_ic->name, first_ic);
		imapx_select (is, first_ic->select, FALSE, cancellable, error);
	} else {
		GQueue start = G_QUEUE_INIT;
		GList *head, *link;

		min_pri = first_ic->pri;

		head = g_queue_peek_head_link (&is->queue);

		/* Tag which commands in the queue to start. */
		for (link = head; link != NULL; link = g_list_next (link)) {
			CamelIMAPXCommand *ic = link->data;

			if (is->literal != NULL)
				break;

			if (ic->pri < min_pri)
				break;

			if (!ic->select || (ic->select == is->select_folder &&
					    !duplicate_fetch_or_refresh (is, ic))) {
				c(is->tagprefix, "* queueing job %3d '%s'\n", (gint)ic->pri, ic->name);
				min_pri = ic->pri;
				g_queue_push_tail (&start, link);
			}

			if (g_queue_get_length (&start) == MAX_COMMANDS)
				break;
		}

		/* Start the tagged commands. */
		while ((link = g_queue_pop_head (&start)) != NULL) {
			CamelIMAPXCommand *ic = link->data;
			g_queue_delete_link (&is->queue, link);
			imapx_command_start (is, ic, cancellable, error);
		}
	}
}

static gboolean
imapx_is_command_queue_empty (CamelIMAPXServer *is)
{
	if (!g_queue_is_empty (&is->queue))
		return FALSE;

	if (!g_queue_is_empty (&is->active))
		return FALSE;

	return TRUE;
}

static void
imapx_command_queue (CamelIMAPXServer *is,
                     CamelIMAPXCommand *ic)
{
	/* We enqueue in priority order, new messages have
	 * higher priority than older messages with the same priority */

	camel_imapx_command_close (ic);

	c(is->tagprefix, "enqueue job '%.*s'\n", ((CamelIMAPXCommandPart *)ic->parts.head->data)->data_size, ((CamelIMAPXCommandPart *)ic->parts.head->data)->data);

	QUEUE_LOCK (is);

	if (is->state == IMAPX_SHUTDOWN) {
		c(is->tagprefix, "refuse to queue job on disconnected server\n");
		if (ic->job->error == NULL)
			g_set_error (
				&ic->job->error, CAMEL_IMAPX_ERROR, 1,
				"%s", _("Server disconnected"));
		QUEUE_UNLOCK (is);

		/* Send a NULL GError since we've already set
		 * the job's GError, and we're not interested
		 * in individual command errors. */
		if (ic->complete != NULL)
			ic->complete (is, ic, NULL);
		return;
	}

	g_queue_insert_sorted (
		&is->queue, ic, (GCompareDataFunc)
		camel_imapx_command_compare, NULL);

	imapx_command_start_next (is, ic->job->cancellable, NULL);

	QUEUE_UNLOCK (is);

	return;
}

/* Must have QUEUE lock */
static CamelIMAPXCommand *
imapx_find_command_tag (CamelIMAPXServer *is,
                        guint tag)
{
	CamelIMAPXCommand *ic = NULL;
	GList *head, *link;

	QUEUE_LOCK (is);

	if (is->literal != NULL && is->literal->tag == tag) {
		ic = is->literal;
		goto exit;
	}

	head = g_queue_peek_head_link (&is->active);

	for (link = head; link != NULL; link = g_list_next (link)) {
		CamelIMAPXCommand *candidate = link->data;

		if (candidate->tag == tag) {
			ic = candidate;
			break;
		}
	}

exit:
	QUEUE_UNLOCK (is);

	return ic;
}

static gboolean
imapx_job_matches (CamelFolder *folder,
                   CamelIMAPXJob *job,
                   guint32 type,
                   const gchar *uid)
{
	switch (job->type) {
		case IMAPX_JOB_GET_MESSAGE:
			if (folder == job->folder &&
			    strcmp (job->u.get_message.uid, uid) == 0)
				return TRUE;
			break;
		case IMAPX_JOB_FETCH_NEW_MESSAGES:
		case IMAPX_JOB_REFRESH_INFO:
		case IMAPX_JOB_SYNC_CHANGES:
		case IMAPX_JOB_EXPUNGE:
			if (folder == job->folder)
				return TRUE;
			break;
		case IMAPX_JOB_LIST:
			return TRUE;
	}

	return FALSE;
}

/* Must not have QUEUE lock */
static CamelIMAPXJob *
imapx_match_active_job (CamelIMAPXServer *is,
                        guint32 type,
                        const gchar *uid)
{
	CamelIMAPXJob *job = NULL;
	GList *head, *link;

	QUEUE_LOCK (is);

	head = g_queue_peek_head_link (&is->active);

	for (link = head; link != NULL; link = g_list_next (link)) {
		CamelIMAPXCommand *ic = link->data;

		if (ic->job == NULL)
			continue;

		if (!(ic->job->type & type))
			continue;

		if (imapx_job_matches (is->select_folder, ic->job, type, uid)) {
			job = ic->job;
			break;
		}
	}

	QUEUE_UNLOCK (is);

	return job;
}

static CamelIMAPXJob *
imapx_is_job_in_queue (CamelIMAPXServer *is,
                       CamelFolder *folder,
                       guint32 type,
                       const gchar *uid)
{
	GList *head, *link;
	CamelIMAPXJob *job = NULL;
	gboolean found = FALSE;

	QUEUE_LOCK (is);

	head = g_queue_peek_head_link (&is->jobs);

	for (link = head; link != NULL; link = g_list_next (link)) {
		job = (CamelIMAPXJob *) link->data;

		if (!job || !(job->type & type))
			continue;

		if (imapx_job_matches (folder, job, type, uid)) {
			found = TRUE;
			break;
		}
	}

	QUEUE_UNLOCK (is);

	if (found)
		return job;
	else
		return NULL;
}

static void
imapx_expunge_uid_from_summary (CamelIMAPXServer *is,
                                gchar *uid,
                                gboolean unsolicited)
{
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) is->select_folder;

	if (unsolicited && ifolder->exists_on_server)
		ifolder->exists_on_server--;

	if (is->changes == NULL)
		is->changes = camel_folder_change_info_new ();

	camel_folder_summary_remove_uid (is->select_folder->summary, uid);
	is->expunged = g_list_prepend (is->expunged, uid);

	camel_folder_change_info_remove_uid (is->changes, uid);

	if (imapx_idle_supported (is) && imapx_in_idle (is)) {
		const gchar *full_name;

		full_name = camel_folder_get_full_name (is->select_folder);
		camel_db_delete_uids (is->store->cdb_w, full_name, is->expunged, NULL);
		imapx_update_store_summary (is->select_folder);
		camel_folder_changed (is->select_folder, is->changes);

		g_list_free_full (is->expunged, (GDestroyNotify) g_free);
		is->expunged = NULL;

		camel_folder_change_info_clear (is->changes);
	}
}

static gchar *
imapx_get_uid_from_index (CamelFolderSummary *summary,
                          guint id)
{
	GPtrArray *array;
	gchar *uid;

	g_return_val_if_fail (summary != NULL, NULL);

	array = camel_folder_summary_get_array (summary);
	g_return_val_if_fail (array != NULL, NULL);

	if (id < array->len) {
		camel_folder_sort_uids (camel_folder_summary_get_folder (summary), array);
		uid = g_strdup (g_ptr_array_index (array, id));
	}

	camel_folder_summary_free_array (array);

	return uid;
}

static void
invalidate_local_cache (CamelIMAPXFolder *ifolder,
                        guint64 new_uidvalidity)
{
	CamelFolder *cfolder;
	CamelFolderChangeInfo *changes;
	GPtrArray *uids;
	gint ii;

	g_return_if_fail (ifolder != NULL);

	cfolder = CAMEL_FOLDER (ifolder);
	g_return_if_fail (cfolder != NULL);

	changes = camel_folder_change_info_new ();

	uids = camel_folder_summary_get_array (cfolder->summary);
	for (ii = 0; uids && ii < uids->len; ii++) {
		const gchar *uid = uids->pdata[ii];

		if (uid)
			camel_folder_change_info_change_uid (changes, uid);
	}

	camel_folder_summary_free_array (uids);

	CAMEL_IMAPX_SUMMARY (cfolder->summary)->validity = new_uidvalidity;
	camel_folder_summary_touch (cfolder->summary);
	camel_folder_summary_save_to_db (cfolder->summary, NULL);

	camel_data_cache_clear (ifolder->cache, "cache");
	camel_data_cache_clear (ifolder->cache, "cur");

	camel_folder_changed (cfolder, changes);
	camel_folder_change_info_free (changes);
}

/* handle any untagged responses */
static gboolean
imapx_untagged (CamelIMAPXServer *is,
                GCancellable *cancellable,
                GError **error)
{
	CamelService *service;
	CamelSettings *settings;
	CamelSortType fetch_order;
	guint id, len;
	guchar *token, *p, c;
	gint tok;
	gboolean lsub = FALSE;
	struct _status_info *sinfo;

	service = CAMEL_SERVICE (is->store);
	settings = camel_service_get_settings (service);

	fetch_order = camel_imapx_settings_get_fetch_order (
		CAMEL_IMAPX_SETTINGS (settings));

	e(is->tagprefix, "got untagged response\n");
	id = 0;
	tok = camel_imapx_stream_token (is->stream, &token, &len, cancellable, error);
	if (tok < 0)
		return FALSE;

	if (tok == IMAPX_TOK_INT) {
		id = strtoul ((gchar *) token, NULL, 10);
		tok = camel_imapx_stream_token (is->stream, &token, &len, cancellable, error);
		if (tok < 0)
			return FALSE;
	}

	if (tok == '\n') {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"truncated server response");
		return FALSE;
	}

	e(is->tagprefix, "Have token '%s' id %d\n", token, id);
	p = token;
	while ((c = *p))
		*p++ = toupper((gchar) c);

	switch (imapx_tokenise ((const gchar *) token, len)) {
	case IMAPX_CAPABILITY:
		if (is->cinfo)
			imapx_free_capability (is->cinfo);
		is->cinfo = imapx_parse_capability (is->stream, cancellable, error);
		if (is->cinfo == NULL)
			return FALSE;
		c(is->tagprefix, "got capability flags %08x\n", is->cinfo->capa);
		return TRUE;
	case IMAPX_EXPUNGE: {
		guint32 expunge = id;
		CamelIMAPXJob *job = imapx_match_active_job (is, IMAPX_JOB_EXPUNGE, NULL);

		/* If there is a job running, let it handle the deletion */
		if (job)
			break;

		c(is->tagprefix, "expunged: %d\n", id);
		if (is->select_folder) {
			gchar *uid = NULL;

			uid = imapx_get_uid_from_index (is->select_folder->summary, expunge - 1);
			if (!uid)
				break;

			imapx_expunge_uid_from_summary (is, uid, TRUE);
		}

		break;
	}
	case IMAPX_VANISHED: {
		GPtrArray *uids;
		gboolean unsolicited = TRUE;
		gint i;
		guint len;
		guchar *token;
		gint tok;

		tok = camel_imapx_stream_token (is->stream, &token, &len, cancellable, error);
		if (tok < 0)
			return FALSE;
		if (tok == '(') {
			unsolicited = FALSE;
			while (tok != ')') {
				/* We expect this to be 'EARLIER' */
				tok = camel_imapx_stream_token (is->stream, &token, &len, cancellable, error);
				if (tok < 0)
					return FALSE;
			}
		} else
			camel_imapx_stream_ungettoken (is->stream, tok, token, len);

		uids = imapx_parse_uids (is->stream, cancellable, error);
		if (uids == NULL)
			return FALSE;
		for (i = 0; i < uids->len; i++) {
			gchar *uid = g_strdup_printf("%u", GPOINTER_TO_UINT(g_ptr_array_index (uids, i)));
			c(is->tagprefix, "vanished: %s\n", uid);
			imapx_expunge_uid_from_summary (is, uid, unsolicited);
		}
		g_ptr_array_free (uids, FALSE);
		break;
	}
	case IMAPX_NAMESPACE: {
		CamelIMAPXNamespaceList *nsl = NULL;

		nsl = imapx_parse_namespace_list (is->stream, cancellable, error);
		if (nsl != NULL) {
			CamelIMAPXStore *imapx_store = (CamelIMAPXStore *) is->store;
			CamelIMAPXStoreNamespace *ns;

			imapx_store->summary->namespaces = nsl;
			camel_store_summary_touch ((CamelStoreSummary *) imapx_store->summary);

			/* TODO Need to remove imapx_store->dir_sep to support multiple namespaces */
			ns = nsl->personal;
			if (ns)
				imapx_store->dir_sep = ns->sep;
		}

		return TRUE;
	}
	case IMAPX_EXISTS:
		c(is->tagprefix, "exists: %d\n", id);
		is->exists = id;

		if (is->select_folder)
			((CamelIMAPXFolder *) is->select_folder)->exists_on_server = id;

		if (imapx_idle_supported (is) && imapx_in_idle (is)) {
			if (camel_folder_summary_count (is->select_folder->summary) < id)
				imapx_stop_idle (is, error);
		}

		break;
	case IMAPX_FLAGS: {
		guint32 flags;

		imapx_parse_flags (is->stream, &flags, NULL, cancellable, error);

		c(is->tagprefix, "flags: %08x\n", flags);
		break;
	}
	case IMAPX_FETCH: {
		struct _fetch_info *finfo;

		finfo = imapx_parse_fetch (is->stream, cancellable, error);
		if (finfo == NULL) {
			imapx_free_fetch (finfo);
			return FALSE;
		}

		if ((finfo->got & (FETCH_BODY | FETCH_UID)) == (FETCH_BODY | FETCH_UID)) {
			CamelIMAPXJob *job = imapx_match_active_job (is, IMAPX_JOB_GET_MESSAGE, finfo->uid);

			/* This must've been a get-message request, fill out the body stream,
			 * in the right spot */

			if (job && job->error == NULL) {
				if (job->u.get_message.use_multi_fetch) {
					job->u.get_message.body_offset = finfo->offset;
					g_seekable_seek (G_SEEKABLE (job->u.get_message.stream), finfo->offset, G_SEEK_SET, NULL, NULL);
				}

				job->u.get_message.body_len = camel_stream_write_to_stream (finfo->body, job->u.get_message.stream, job->cancellable, &job->error);
				if (job->u.get_message.body_len == -1)
					g_prefix_error (
						&job->error,
						_("Error writing to cache stream: "));
			}
		}

		if ((finfo->got & FETCH_FLAGS) && !(finfo->got & FETCH_HEADER)) {
			CamelIMAPXJob *job = imapx_match_active_job (is, IMAPX_JOB_FETCH_NEW_MESSAGES | IMAPX_JOB_REFRESH_INFO, NULL);
			/* This is either a refresh_info job, check to see if it is and update
			 * if so, otherwise it must've been an unsolicited response, so update
			 * the summary to match */

			if (job && (finfo->got & FETCH_UID)) {
				struct _refresh_info r;

				r.uid = finfo->uid;
				finfo->uid = NULL;
				r.server_flags = finfo->flags;
				r.server_user_flags = finfo->user_flags;
				finfo->user_flags = NULL;
				r.exists = FALSE;
				g_array_append_val (job->u.refresh_info.infos, r);
			} else if (is->select_folder) {
				CamelFolder *folder;
				CamelMessageInfo *mi = NULL;
				gboolean changed = FALSE;
				gchar *uid = NULL;

				g_object_ref (is->select_folder);
				folder = is->select_folder;

				c(is->tagprefix, "flag changed: %d\n", id);

				if (finfo->got & FETCH_UID) {
					uid = finfo->uid;
					finfo->uid = NULL;
				} else {
					uid = imapx_get_uid_from_index (folder->summary, id - 1);
				}

				if (uid) {
					mi = camel_folder_summary_get (folder->summary, uid);
					if (mi) {
						/* It's unsolicited _unless_ is->select_pending (i.e. during
						 * a QRESYNC SELECT */
						changed = imapx_update_message_info_flags (mi, finfo->flags, finfo->user_flags, is->permanentflags, folder, !is->select_pending);
					} else {
						/* This (UID + FLAGS for previously unknown message) might
						 * happen during a SELECT (QRESYNC). We should use it. */
						c(is->tagprefix, "flags changed for unknown uid %s\n.", uid);
					}
					finfo->user_flags = NULL;
				}

				if (changed) {
					if (is->changes == NULL)
						is->changes = camel_folder_change_info_new ();

					camel_folder_change_info_change_uid (is->changes, uid);
					g_free (uid);
				}

				if (imapx_idle_supported (is) && changed && imapx_in_idle (is)) {
					camel_folder_summary_save_to_db (is->select_folder->summary, NULL);
					imapx_update_store_summary (is->select_folder);
					camel_folder_changed (is->select_folder, is->changes);
					camel_folder_change_info_clear (is->changes);
				}

				if (mi)
					camel_message_info_free (mi);
				g_object_unref (folder);
			}
		}

		if ((finfo->got & (FETCH_HEADER | FETCH_UID)) == (FETCH_HEADER | FETCH_UID)) {
			CamelIMAPXJob *job = imapx_match_active_job (is, IMAPX_JOB_FETCH_NEW_MESSAGES | IMAPX_JOB_REFRESH_INFO, NULL);

			/* This must be a refresh info job as well, but it has asked for
			 * new messages to be added to the index */

			if (job) {
				CamelMimeParser *mp;
				CamelMessageInfo *mi;

				/* Do we want to save these headers for later too?  Do we care? */

				mp = camel_mime_parser_new ();
				camel_mime_parser_init_with_stream (mp, finfo->header, NULL);
				mi = camel_folder_summary_info_new_from_parser (job->folder->summary, mp);
				g_object_unref (mp);

				if (mi) {
					guint32 server_flags;
					CamelFlag *server_user_flags;
					CamelMessageInfoBase *binfo;
					gboolean free_user_flags = FALSE;

					mi->uid = camel_pstring_strdup (finfo->uid);

					if (!(finfo->got & FETCH_FLAGS))
					{
						struct _refresh_info *r = NULL;
						GArray *infos = job->u.refresh_info.infos;
						gint min = job->u.refresh_info.last_index;
						gint max = job->u.refresh_info.index, mid;
						gboolean found = FALSE;

						/* array is sorted, so use a binary search */
						do {
							gint cmp = 0;

							mid = (min + max) / 2;
							r = &g_array_index (infos, struct _refresh_info, mid);
							cmp = imapx_refresh_info_uid_cmp (finfo->uid, r->uid, fetch_order == CAMEL_SORT_ASCENDING);

							if (cmp > 0)
								min = mid + 1;
							else if (cmp < 0)
								max = mid - 1;
							else
								found = TRUE;

						} while (!found && min <= max);

						if (!found)
							g_assert_not_reached ();

						server_flags = r->server_flags;
						server_user_flags = r->server_user_flags;
					} else {
						server_flags = finfo->flags;
						server_user_flags = finfo->user_flags;
						/* free user_flags ? */
						finfo->user_flags = NULL;
						free_user_flags = TRUE;
					}

					/* If the message is a really new one -- equal or higher than what
					 * we know as UIDNEXT for the folder, then it came in since we last
					 * fetched UIDNEXT and UNREAD count. We'll update UIDNEXT in the
					 * command completion, but update UNREAD count now according to the
					 * message SEEN flag */
					if (!(server_flags & CAMEL_MESSAGE_SEEN)) {
						CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->folder;
						guint64 uidl = strtoull (mi->uid, NULL, 10);

						if (uidl >= ifolder->uidnext_on_server) {
							c(is->tagprefix, "Updating unread count for new message %s\n", mi->uid);
							((CamelIMAPXFolder *) job->folder)->unread_on_server++;
						} else {
							c(is->tagprefix, "Not updating unread count for new message %s\n", mi->uid);
						}
					}

					binfo = (CamelMessageInfoBase *) mi;
					binfo->size = finfo->size;

					if (!camel_folder_summary_check_uid (job->folder->summary, mi->uid)) {
						CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->folder;
						gint cnt;

						camel_folder_summary_add (job->folder->summary, mi);
						imapx_set_message_info_flags_for_new_message (mi, server_flags, server_user_flags, job->folder);
						camel_folder_change_info_add_uid (job->u.refresh_info.changes, mi->uid);

						if (!g_hash_table_lookup (ifolder->ignore_recent, mi->uid)) {
							camel_folder_change_info_recent_uid (job->u.refresh_info.changes, mi->uid);
							g_hash_table_remove (ifolder->ignore_recent, mi->uid);
						}

						cnt = (camel_folder_summary_count (job->folder->summary) * 100 ) / ifolder->exists_on_server;
						camel_operation_progress (job->cancellable, cnt ? cnt : 1);
					}

					if (free_user_flags && server_user_flags)
						camel_flag_list_free (&server_user_flags);

				}
			}
		}

		imapx_free_fetch (finfo);
		break;
	}
	case IMAPX_LSUB:
		lsub = TRUE;
	case IMAPX_LIST: {
		struct _list_info *linfo = imapx_parse_list (is->stream, cancellable, error);
		CamelIMAPXJob *job;

		if (!linfo)
			break;

		job = imapx_match_active_job (is, IMAPX_JOB_LIST, linfo->name);

		// TODO: we want to make sure the names match?

		if (job->u.list.flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) {
			c(is->tagprefix, "lsub: '%s' (%c)\n", linfo->name, linfo->separator);

		} else {
			c(is->tagprefix, "list: '%s' (%c)\n", linfo->name, linfo->separator);
		}

		if (job && g_hash_table_lookup (job->u.list.folders, linfo->name) == NULL) {
			if (lsub)
				linfo->flags |= CAMEL_FOLDER_SUBSCRIBED;
			g_hash_table_insert (job->u.list.folders, linfo->name, linfo);
		} else {
			g_warning("got list response but no current listing job happening?\n");
			imapx_free_list (linfo);
		}
		break;
	}
	case IMAPX_RECENT:
		c(is->tagprefix, "recent: %d\n", id);
		is->recent = id;
		break;
	case IMAPX_STATUS: {
		struct _state_info *sinfo = imapx_parse_status_info (is->stream, cancellable, error);
		if (sinfo) {
			CamelIMAPXStoreSummary *s = ((CamelIMAPXStore *) is->store)->summary;
			CamelIMAPXStoreNamespace *ns;
			CamelIMAPXFolder *ifolder = NULL;;

			ns = camel_imapx_store_summary_namespace_find_full (s, sinfo->name);
			if (ns) {
				gchar *path_name;

				path_name = camel_imapx_store_summary_full_to_path (s, sinfo->name, ns->sep);
				c(is->tagprefix, "Got folder path '%s' for full '%s'\n", path_name, sinfo->name);
				if (path_name) {
					ifolder = (gpointer) camel_store_get_folder_sync (is->store, path_name, 0, cancellable, error);
					g_free (path_name);
				}
			}
			if (ifolder) {
				CamelFolder *cfolder = CAMEL_FOLDER (ifolder);

				ifolder->unread_on_server = sinfo->unseen;
				ifolder->exists_on_server = sinfo->messages;
				ifolder->modseq_on_server = sinfo->highestmodseq;
				ifolder->uidnext_on_server = sinfo->uidnext;
				ifolder->uidvalidity_on_server = sinfo->uidvalidity;
				if (sinfo->uidvalidity && sinfo->uidvalidity != ((CamelIMAPXSummary *) cfolder->summary)->validity)
					invalidate_local_cache (ifolder, sinfo->uidvalidity);
			} else {
				c(is->tagprefix, "Received STATUS for unknown folder '%s'\n", sinfo->name);
			}

			g_free (sinfo->name);
			g_free (sinfo);
		}
		break;
	}
	case IMAPX_BYE: {
		guchar *token;

		if (camel_imapx_stream_text (is->stream, &token, cancellable, NULL)) {
			c(is->tagprefix, "BYE: %s\n", token);
			g_set_error (
				error, CAMEL_IMAPX_ERROR, 1,
				"IMAP server said BYE: %s", token);
		}
		is->state = IMAPX_SHUTDOWN;
		return FALSE;
	}
	case IMAPX_PREAUTH:
		c(is->tagprefix, "preauthenticated\n");
		if (is->state < IMAPX_AUTHENTICATED)
			is->state = IMAPX_AUTHENTICATED;
		/* fall through... */
	case IMAPX_OK: case IMAPX_NO: case IMAPX_BAD:
		/* TODO: validate which ones of these can happen as unsolicited responses */
		/* TODO: handle bye/preauth differently */
		camel_imapx_stream_ungettoken (is->stream, tok, token, len);
		sinfo = imapx_parse_status (is->stream, cancellable, error);
		if (sinfo == NULL)
			return FALSE;
		switch (sinfo->condition) {
		case IMAPX_CLOSED:
			c(is->tagprefix, "previously selected folder is now closed\n");
			if (is->select_pending && !is->select_folder) {
				is->select_folder = is->select_pending;
			}
			break;
		case IMAPX_READ_WRITE:
			is->mode = IMAPX_MODE_READ | IMAPX_MODE_WRITE;
			c(is->tagprefix, "folder is read-write\n");
			break;
		case IMAPX_READ_ONLY:
			is->mode = IMAPX_MODE_READ;
			c(is->tagprefix, "folder is read-only\n");
			break;
		case IMAPX_UIDVALIDITY:
			is->uidvalidity = sinfo->u.uidvalidity;
			break;
		case IMAPX_UNSEEN:
			is->unseen = sinfo->u.unseen;
			break;
		case IMAPX_HIGHESTMODSEQ:
			is->highestmodseq = sinfo->u.highestmodseq;
			break;
		case IMAPX_PERMANENTFLAGS:
			is->permanentflags = sinfo->u.permanentflags;
			break;
		case IMAPX_UIDNEXT:
			is->uidnext = sinfo->u.uidnext;
			break;
		case IMAPX_ALERT:
			c(is->tagprefix, "ALERT!: %s\n", sinfo->text);
			break;
		case IMAPX_PARSE:
			c(is->tagprefix, "PARSE: %s\n", sinfo->text);
			break;
		case IMAPX_CAPABILITY:
			if (sinfo->u.cinfo) {
				struct _capability_info *cinfo = is->cinfo;
				is->cinfo = sinfo->u.cinfo;
				sinfo->u.cinfo = NULL;
				if (cinfo)
					imapx_free_capability (cinfo);
				c(is->tagprefix, "got capability flags %08x\n", is->cinfo->capa);
			}
			break;
		default:
			break;
		}
		imapx_free_status (sinfo);
		return TRUE;
	default:
		/* unknown response, just ignore it */
		c(is->tagprefix, "unknown token: %s\n", token);
	}

	return (camel_imapx_stream_skip (is->stream, cancellable, error) == 0);
}

/* handle any continuation requests
 * either data continuations, or auth continuation */
static gboolean
imapx_continuation (CamelIMAPXServer *is,
                    gboolean litplus,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelIMAPXCommand *ic, *newliteral = NULL;
	CamelIMAPXCommandPart *cp;
	GList *link;

	/* The 'literal' pointer is like a write-lock, nothing else
	 * can write while we have it ... so we dont need any
	 * ohter lock here.  All other writes go through
	 * queue-lock */
	if (imapx_idle_supported (is) && imapx_in_idle (is)) {
		camel_imapx_stream_skip (is->stream, cancellable, error);

		c(is->tagprefix, "Got continuation response for IDLE \n");
		IDLE_LOCK (is->idle);
		/* We might have actually sent the DONE already! */
		if (is->idle->state == IMAPX_IDLE_ISSUED)
			is->idle->state = IMAPX_IDLE_STARTED;
		else if (is->idle->state == IMAPX_IDLE_CANCEL) {
			/* IDLE got cancelled after we sent the command, while
			 * we were waiting for this continuation. Send DONE
			 * immediately. */
			if (!imapx_command_idle_stop (is, error)) {
				IDLE_UNLOCK (is->idle);
				return FALSE;
			}
			is->idle->state = IMAPX_IDLE_OFF;
		} else {
			c(is->tagprefix, "idle starts in wrong state %d\n",
				 is->idle->state);
		}
		IDLE_UNLOCK (is->idle);

		QUEUE_LOCK (is);
		is->literal = NULL;
		imapx_command_start_next (is, cancellable, error);
		QUEUE_UNLOCK (is);

		return TRUE;
	}

	ic = is->literal;
	if (!litplus) {
		if (ic == NULL) {
			camel_imapx_stream_skip (is->stream, cancellable, error);
			c(is->tagprefix, "got continuation response with no outstanding continuation requests?\n");
			return TRUE;
		}
		c(is->tagprefix, "got continuation response for data\n");
	} else {
		c(is->tagprefix, "sending LITERAL+ continuation\n");
	}

	link = ic->current_part;
	g_return_val_if_fail (link != NULL, FALSE);
	cp = (CamelIMAPXCommandPart *) link->data;

	switch (cp->type & CAMEL_IMAPX_COMMAND_MASK) {
	case CAMEL_IMAPX_COMMAND_DATAWRAPPER:
		c(is->tagprefix, "writing data wrapper to literal\n");
		camel_data_wrapper_write_to_stream_sync ((CamelDataWrapper *) cp->ob, (CamelStream *) is->stream, cancellable, NULL);
		break;
	case CAMEL_IMAPX_COMMAND_STREAM:
		c(is->tagprefix, "writing stream to literal\n");
		camel_stream_write_to_stream ((CamelStream *) cp->ob, (CamelStream *) is->stream, cancellable, NULL);
		break;
	case CAMEL_IMAPX_COMMAND_AUTH: {
		gchar *resp;
		guchar *token;

		if (camel_imapx_stream_text (is->stream, &token, cancellable, error))
			return FALSE;

		resp = camel_sasl_challenge_base64_sync (
			(CamelSasl *) cp->ob, (const gchar *) token,
			cancellable, error);
		g_free (token);
		if (resp == NULL)
			return FALSE;
		c(is->tagprefix, "got auth continuation, feeding token '%s' back to auth mech\n", resp);

		camel_stream_write ((CamelStream *) is->stream, resp, strlen (resp), cancellable, NULL);
		g_free (resp);
		/* we want to keep getting called until we get a status reponse from the server
		 * ignore what sasl tells us */
		newliteral = ic;
		/* We already ate the end of the input stream line */
		goto noskip;
		break; }
	case CAMEL_IMAPX_COMMAND_FILE: {
		CamelStream *file;

		c(is->tagprefix, "writing file '%s' to literal\n", (gchar *)cp->ob);

		// FIXME: errors
		if (cp->ob && (file = camel_stream_fs_new_with_name (cp->ob, O_RDONLY, 0, NULL))) {
			camel_stream_write_to_stream (file, (CamelStream *) is->stream, cancellable, NULL);
			g_object_unref (file);
		} else if (cp->ob_size > 0) {
			// Server is expecting data ... ummm, send it zeros?  abort?
		}
		break; }
	case CAMEL_IMAPX_COMMAND_STRING:
		camel_stream_write ((CamelStream *) is->stream, cp->ob, cp->ob_size, cancellable, NULL);
		break;
	default:
		/* should we just ignore? */
		is->literal = NULL;
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"continuation response for non-continuation request");
		return FALSE;
	}

	if (!litplus)
		camel_imapx_stream_skip (is->stream, cancellable, error);

noskip:
	link = g_list_next (link);
	if (link != NULL) {
		ic->current_part = link;
		cp = (CamelIMAPXCommandPart *) link->data;

		c(is->tagprefix, "next part of command \"%c%05u: %s\"\n", is->tagprefix, ic->tag, cp->data);
		camel_stream_write_string ((CamelStream *) is->stream, cp->data, cancellable, NULL);
		camel_stream_write_string ((CamelStream *) is->stream, "\r\n", cancellable, NULL);
		if (cp->type & (CAMEL_IMAPX_COMMAND_CONTINUATION | CAMEL_IMAPX_COMMAND_LITERAL_PLUS)) {
			newliteral = ic;
		} else {
			g_assert (g_list_next (link) == NULL);
		}
	} else {
		c(is->tagprefix, "%p: queueing continuation\n", ic);
		camel_stream_write_string((CamelStream *)is->stream, "\r\n", cancellable, NULL);
	}

	QUEUE_LOCK (is);
	is->literal = newliteral;

	if (!litplus)
		imapx_command_start_next (is, cancellable, error);
	QUEUE_UNLOCK (is);

	return TRUE;
}

/* handle a completion line */
static gboolean
imapx_completion (CamelIMAPXServer *is,
                  guchar *token,
                  gint len,
                  GCancellable *cancellable,
                  GError **error)
{
	CamelIMAPXCommand *ic;
	guint tag;

	/* Given "A0001 ...", 'A' = tag prefix, '0001' = tag. */

	if (token[0] != is->tagprefix) {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"Server sent unexpected response: %s", token);
		return FALSE;
	}

	tag = strtoul ((gchar *) token + 1, NULL, 10);

	if ((ic = imapx_find_command_tag (is, tag)) == NULL) {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"got response tag unexpectedly: %s", token);
		return FALSE;
	}

	c(is->tagprefix, "Got completion response for command %05u '%s'\n", ic->tag, ic->name);

	if (camel_folder_change_info_changed (is->changes)) {
		if (is->changes->uid_changed->len)
			camel_folder_summary_save_to_db (is->select_folder->summary, NULL);
		else {
			const gchar *full_name;

			full_name = camel_folder_get_full_name (is->select_folder);
			camel_db_delete_uids (is->store->cdb_w, full_name, is->expunged, NULL);
		}

		g_list_free_full (is->expunged, (GDestroyNotify) g_free);
		is->expunged = NULL;

		imapx_update_store_summary (is->select_folder);
		camel_folder_changed (is->select_folder, is->changes);
		camel_folder_change_info_clear (is->changes);
	}

	QUEUE_LOCK (is);

	g_queue_remove (&is->active, ic);
	g_queue_push_tail (&is->done, ic);

	if (is->literal == ic)
		is->literal = NULL;

	if (g_list_next (ic->current_part) != NULL) {
		QUEUE_UNLOCK (is);
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"command still has unsent parts? %s", ic->name);
		return FALSE;
	}

	g_queue_remove (&is->done, ic);

	QUEUE_UNLOCK (is);

	ic->status = imapx_parse_status (is->stream, cancellable, error);

	if (ic->status == NULL)
		return FALSE;

	if (ic->complete != NULL)
		if (!ic->complete (is, ic, error))
			return FALSE;

	QUEUE_LOCK (is);
	imapx_command_start_next (is, cancellable, error);
	QUEUE_UNLOCK (is);

	return TRUE;
}

static gboolean
imapx_step (CamelIMAPXServer *is,
            GCancellable *cancellable,
            GError **error)
{
	guint len;
	guchar *token;
	gint tok;

	// poll ?  wait for other stuff? loop?
	tok = camel_imapx_stream_token (is->stream, &token, &len, cancellable, error);
	if (tok < 0)
		return FALSE;

	if (tok == '*')
		return imapx_untagged (is, cancellable, error);
	else if (tok == IMAPX_TOK_TOKEN)
		return imapx_completion (is, token, len, cancellable, error);
	else if (tok == '+')
		return imapx_continuation (is, FALSE, cancellable, error);

	g_set_error (
		error, CAMEL_IMAPX_ERROR, 1,
		"unexpected server response:");

	return FALSE;
}

/* Used to run 1 command synchronously,
 * use for capa, login, and namespaces only. */
static gboolean
imapx_command_run (CamelIMAPXServer *is,
                   CamelIMAPXCommand *ic,
                   GCancellable *cancellable,
                   GError **error)
/* throws IO,PARSE exception */
{
	gboolean success = TRUE;

	camel_imapx_command_close (ic);

	QUEUE_LOCK (is);
	imapx_command_start (is, ic, cancellable, error);
	QUEUE_UNLOCK (is);

	while (success && ic->status == NULL)
		success = imapx_step (is, cancellable, error);

	if (is->literal == ic)
		is->literal = NULL;

	QUEUE_LOCK (is);
	g_queue_remove (&is->active, ic);
	QUEUE_UNLOCK (is);

	return success;
}

static gboolean
imapx_command_complete (CamelIMAPXServer *is,
                        CamelIMAPXCommand *ic,
                        GError **error)
{
	camel_imapx_command_done (ic);
	camel_imapx_command_unref (ic);

	return TRUE;
}

static void
imapx_command_cancelled (GCancellable *cancellable,
                         CamelIMAPXCommand *ic)
{
	/* Unblock imapx_command_run_sync() immediately.
	 *
	 * If camel_imapx_command_done() is called sometime later,
	 * the GCond will broadcast but no one will be listening. */

	camel_imapx_command_done (ic);
}

/* The caller should free the command as well */
static gboolean
imapx_command_run_sync (CamelIMAPXServer *is,
                        CamelIMAPXCommand *ic,
                        GCancellable *cancellable,
                        GError **error)
{
	guint cancel_id = 0;

	/* FIXME The only caller of this function currently does not set
	 *       a "complete" callback function, so we can get away with
	 *       referencing the command here and dropping the reference
	 *       in imapx_command_complete().  The queueing/dequeueing
	 *       of these things is too complex for my little mind, so
	 *       we may have to revisit the reference counting if this
	 *       function gets another caller. */

	g_warn_if_fail (ic->complete == NULL);
	ic->complete = imapx_command_complete;

	if (G_IS_CANCELLABLE (cancellable))
		cancel_id = g_cancellable_connect (
			cancellable,
			G_CALLBACK (imapx_command_cancelled),
			camel_imapx_command_ref (ic),
			(GDestroyNotify) camel_imapx_command_unref);

	/* Unref'ed in imapx_command_complete(). */
	camel_imapx_command_ref (ic);

	imapx_command_queue (is, ic);

	camel_imapx_command_wait (ic);

	if (cancel_id > 0)
		g_cancellable_disconnect (cancellable, cancel_id);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	if (camel_imapx_command_set_error_if_failed (ic, error))
		return FALSE;

	return TRUE;
}

/* ********************************************************************** */
/* Should be called when there are no more commands needed to complete the job */

static CamelIMAPXJob *
imapx_job_new (GCancellable *cancellable)
{
	CamelIMAPXJob *job;

	if (cancellable != NULL)
		g_object_ref (cancellable);

	job = g_slice_new0 (CamelIMAPXJob);
	job->ref_count = 1;
	job->done_cond = g_cond_new ();
	job->done_mutex = g_mutex_new ();
	job->cancellable = cancellable;

	return job;
}

static CamelIMAPXJob *
imapx_job_ref (CamelIMAPXJob *job)
{
	g_return_val_if_fail (job != NULL, NULL);
	g_return_val_if_fail (job->ref_count > 0, NULL);

	g_atomic_int_inc (&job->ref_count);

	return job;
}

static void
imapx_job_unref (CamelIMAPXJob *job)
{
	g_return_if_fail (job != NULL);
	g_return_if_fail (job->ref_count > 0);

	if (g_atomic_int_dec_and_test (&job->ref_count)) {

		g_cond_free (job->done_cond);
		g_mutex_free (job->done_mutex);

		g_clear_error (&job->error);

		if (job->pop_operation_msg)
			camel_operation_pop_message (job->cancellable);

		if (job->cancellable != NULL)
			g_object_unref (job->cancellable);

		g_slice_free (CamelIMAPXJob, job);
	}
}

static void
imapx_job_done (CamelIMAPXServer *is,
                CamelIMAPXJob *job)
{
	if (!job->noreply) {
		g_mutex_lock (job->done_mutex);
		job->done_flag = TRUE;
		g_cond_broadcast (job->done_cond);
		g_mutex_unlock (job->done_mutex);
	}

	QUEUE_LOCK (is);
	if (g_queue_remove (&is->jobs, job))
		imapx_job_unref (job);
	QUEUE_UNLOCK (is);
}

static void
imapx_job_cancelled (GCancellable *cancellable,
                     CamelIMAPXJob *job)
{
	/* Unblock imapx_run_job() immediately.
	 *
	 * If imapx_job_done() is called sometime later, the
	 * GCond will broadcast but no one will be listening. */

	g_mutex_lock (job->done_mutex);
	job->done_flag = TRUE;
	g_cond_broadcast (job->done_cond);
	g_mutex_unlock (job->done_mutex);
}

static gboolean
imapx_register_job (CamelIMAPXServer *is,
                    CamelIMAPXJob *job,
                    GError **error)
{
	if (is->state >= IMAPX_INITIALISED) {
		QUEUE_LOCK (is);
		g_queue_push_head (&is->jobs, imapx_job_ref (job));
		QUEUE_UNLOCK (is);

	} else {
		e (is->tagprefix, "NO connection yet, maybe user cancelled jobs earlier ?");
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Not authenticated"));
		return FALSE;
	}

	return TRUE;
}

static gboolean
imapx_run_job (CamelIMAPXServer *is,
               CamelIMAPXJob *job,
               GError **error)
{
	gulong cancel_id = 0;

	g_warn_if_fail (job->done_flag == FALSE);

	if (g_cancellable_set_error_if_cancelled (is->cancellable, error))
		return FALSE;

	if (G_IS_CANCELLABLE (job->cancellable))
		cancel_id = g_cancellable_connect (
			job->cancellable,
			G_CALLBACK (imapx_job_cancelled),
			imapx_job_ref (job),
			(GDestroyNotify) imapx_job_unref);

	job->start (is, job);

	if (!job->noreply) {
		g_mutex_lock (job->done_mutex);
		while (!job->done_flag)
			g_cond_wait (job->done_cond, job->done_mutex);
		g_mutex_unlock (job->done_mutex);
	}

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

static gboolean
imapx_submit_job (CamelIMAPXServer *is,
                  CamelIMAPXJob *job,
                  GError **error)
{
	if (!imapx_register_job (is, job, error))
		return FALSE;

	return imapx_run_job (is, job, error);
}

/* ********************************************************************** */
// IDLE support

/*TODO handle negative cases sanely */
static gboolean
imapx_command_idle_stop (CamelIMAPXServer *is,
                         GError **error)
{
	if (!is->stream || camel_stream_write_string ((CamelStream *)is->stream, "DONE\r\n", NULL, NULL) == -1) {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"Unable to issue DONE");
		c(is->tagprefix, "Failed to issue DONE to terminate IDLE\n");
		is->state = IMAPX_SHUTDOWN;
		is->parser_quit = TRUE;
		if (is->cancellable)
			g_cancellable_cancel (is->cancellable);
		return FALSE;
	}

	return TRUE;
}

static gboolean
imapx_command_idle_done (CamelIMAPXServer *is,
                         CamelIMAPXCommand *ic,
                         GError **error)
{
	CamelIMAPXIdle *idle = is->idle;
	gboolean success = TRUE;

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error performing IDLE"));
		success = FALSE;
	}

	IDLE_LOCK (idle);
	idle->state = IMAPX_IDLE_OFF;
	IDLE_UNLOCK (idle);

	imapx_job_done (is, ic->job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_idle_start (CamelIMAPXServer *is,
                      CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXCommandPart *cp;

	ic = camel_imapx_command_new (
		is, "IDLE", job->folder, "IDLE");
	ic->job = job;
	ic->pri = job->pri;
	ic->complete = imapx_command_idle_done;

	camel_imapx_command_close (ic);
	cp = g_queue_peek_head (&ic->parts);
	cp->type |= CAMEL_IMAPX_COMMAND_CONTINUATION;

	QUEUE_LOCK (is);
	IDLE_LOCK (is->idle);
	/* Don't issue it if the idle was cancelled already */
	if (is->idle->state == IMAPX_IDLE_PENDING) {
		is->idle->state = IMAPX_IDLE_ISSUED;
		imapx_command_start (is, ic, job->cancellable, &job->error);
	} else {
		imapx_job_done (is, ic->job);
		camel_imapx_command_unref (ic);
	}
	IDLE_UNLOCK (is->idle);
	QUEUE_UNLOCK (is);
}

static gboolean
camel_imapx_server_idle (CamelIMAPXServer *is,
                         CamelFolder *folder,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	job = imapx_job_new (cancellable);
	job->type = IMAPX_JOB_IDLE;
	job->start = imapx_job_idle_start;
	job->folder = folder;

	success = imapx_submit_job (is, job, error);

	imapx_job_unref (job);

	return success;
}

static gboolean
imapx_server_fetch_new_messages (CamelIMAPXServer *is,
                                 CamelFolder *folder,
                                 gboolean async,
                                 gboolean update_unseen,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	job = imapx_job_new (cancellable);
	job->type = IMAPX_JOB_FETCH_NEW_MESSAGES;
	job->start = imapx_job_fetch_new_messages_start;
	job->folder = folder;
	job->noreply = async;
	job->u.refresh_info.changes = camel_folder_change_info_new ();
	job->u.refresh_info.update_unseen = update_unseen;

	success = imapx_submit_job (is, job, error);

	if (!async)
		imapx_job_unref (job);

	return success;
}

static gpointer
imapx_idle_thread (gpointer data)
{
	CamelIMAPXServer *is = (CamelIMAPXServer *) data;
	GError *local_error = NULL;

	while (TRUE) {
		CamelIMAPXFolder *ifolder;

		g_mutex_lock (is->idle->start_watch_mutex);
		is->idle->start_watch_is_set = FALSE;
		g_mutex_unlock (is->idle->start_watch_mutex);

		IDLE_LOCK (is->idle);
		while ((ifolder = (CamelIMAPXFolder *) is->select_folder) &&
		       is->idle->state == IMAPX_IDLE_PENDING &&
		       !is->idle->idle_exit) {
			time_t dwelled = time (NULL) - is->idle->started;

			if (dwelled < IMAPX_IDLE_DWELL_TIME) {
				IDLE_UNLOCK (is->idle);
				g_usleep ((IMAPX_IDLE_DWELL_TIME - dwelled) * G_USEC_PER_SEC);
				IDLE_LOCK (is->idle);
				continue;
			}
			IDLE_UNLOCK (is->idle);

			camel_imapx_server_idle (is, (gpointer) ifolder, is->cancellable, &local_error);

			if (local_error == NULL && ifolder->exists_on_server >
			    camel_folder_summary_count (((CamelFolder *) ifolder)->summary) && imapx_is_command_queue_empty (is))
				imapx_server_fetch_new_messages (is, is->select_folder, TRUE, TRUE, is->cancellable, &local_error);

			if (local_error != NULL) {
				e (is->tagprefix, "Caught exception in idle thread:  %s \n", local_error->message);
				/* No way to asyncronously notify UI ? */
				g_clear_error (&local_error);
			}
			IDLE_LOCK (is->idle);
		}
		IDLE_UNLOCK (is->idle);

		g_mutex_lock (is->idle->start_watch_mutex);
		while (!is->idle->start_watch_is_set)
			g_cond_wait (
				is->idle->start_watch_cond,
				is->idle->start_watch_mutex);
		g_mutex_unlock (is->idle->start_watch_mutex);

		if (is->idle->idle_exit)
			break;
	}

	g_clear_error (&local_error);
	is->idle->idle_thread = NULL;
	return NULL;
}

static gboolean
imapx_stop_idle (CamelIMAPXServer *is,
                 GError **error)
{
	CamelIMAPXIdle *idle = is->idle;
	gint stopped = FALSE;
	time_t now;

	time (&now);
	IDLE_LOCK (idle);

	switch (idle->state) {
	case IMAPX_IDLE_ISSUED:
		idle->state = IMAPX_IDLE_CANCEL;
	case IMAPX_IDLE_CANCEL:
		stopped = TRUE;
		break;

	case IMAPX_IDLE_STARTED:
		/* We set 'stopped' even if sending DONE fails, to ensure that
		 * our caller doesn't try to submit its own command. */
		stopped = TRUE;
		if (!imapx_command_idle_stop (is, error))
			break;

		idle->state = IMAPX_IDLE_OFF;
		c(is->tagprefix, "Stopping idle after %ld seconds\n",
		  (long)(now - idle->started));
	case IMAPX_IDLE_PENDING:
		idle->state = IMAPX_IDLE_OFF;
	case IMAPX_IDLE_OFF:
		break;
	}
	IDLE_UNLOCK (idle);

	return stopped;
}

static void
imapx_init_idle (CamelIMAPXServer *is)
{
	is->idle = g_new0 (CamelIMAPXIdle, 1);
	is->idle->idle_lock = g_mutex_new ();
}

static void
imapx_exit_idle (CamelIMAPXServer *is)
{
	CamelIMAPXIdle *idle = is->idle;
	GThread *thread = NULL;

	if (!idle)
		return;

	IDLE_LOCK (idle);

	if (idle->idle_thread) {
		idle->idle_exit = TRUE;

		g_mutex_lock (idle->start_watch_mutex);
		idle->start_watch_is_set = TRUE;
		g_cond_broadcast (idle->start_watch_cond);
		g_mutex_unlock (idle->start_watch_mutex);

		thread = idle->idle_thread;
		idle->idle_thread = 0;
	}

	idle->idle_thread = NULL;
	IDLE_UNLOCK (idle);

	if (thread)
		g_thread_join (thread);

	g_mutex_free (idle->idle_lock);

	if (idle->start_watch_cond != NULL)
		g_cond_free (idle->start_watch_cond);

	if (idle->start_watch_mutex != NULL)
		g_mutex_free (idle->start_watch_mutex);

	g_free (is->idle);
	is->idle = NULL;
}

static void
imapx_start_idle (CamelIMAPXServer *is)
{
	CamelIMAPXIdle *idle = is->idle;

	if (camel_application_is_exiting)
		return;

	IDLE_LOCK (idle);

	g_assert (idle->state == IMAPX_IDLE_OFF);
	time (&idle->started);
	idle->state = IMAPX_IDLE_PENDING;

	if (!idle->idle_thread) {
		idle->start_watch_cond = g_cond_new ();
		idle->start_watch_mutex = g_mutex_new ();
		idle->start_watch_is_set = FALSE;

		idle->idle_thread = g_thread_create (
			(GThreadFunc) imapx_idle_thread, is, TRUE, NULL);
	} else {
		g_mutex_lock (idle->start_watch_mutex);
		idle->start_watch_is_set = TRUE;
		g_cond_broadcast (idle->start_watch_cond);
		g_mutex_unlock (idle->start_watch_mutex);
	}

	IDLE_UNLOCK (idle);
}

static gboolean
imapx_in_idle (CamelIMAPXServer *is)
{
	gboolean ret = FALSE;
	CamelIMAPXIdle *idle = is->idle;

	IDLE_LOCK (idle);
	ret = (idle->state > IMAPX_IDLE_OFF);
	IDLE_UNLOCK (idle);

	return ret;
}

static gboolean
imapx_idle_supported (CamelIMAPXServer *is)
{
	return (is->cinfo && (is->cinfo->capa & IMAPX_CAPABILITY_IDLE) != 0 && is->use_idle);
}

// end IDLE
/* ********************************************************************** */
static gboolean
imapx_command_select_done (CamelIMAPXServer *is,
                           CamelIMAPXCommand *ic,
                           GError **error)
{
	const gchar *selected_folder = NULL;
	gboolean success = TRUE;
	GError *local_error = NULL;

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		GQueue failed = G_QUEUE_INIT;
		GQueue trash = G_QUEUE_INIT;
		GList *head, *link;

		c(is->tagprefix, "Select failed\n");

		QUEUE_LOCK (is);

		head = g_queue_peek_head_link (&is->queue);

		if (is->select_pending) {
			head = g_queue_peek_head_link (&is->queue);

			for (link = head; link != NULL; link = g_list_next (link)) {
				CamelIMAPXCommand *cw = link->data;

				if (cw->select && cw->select == is->select_pending) {
					c(is->tagprefix, "Cancelling command '%s'(%p) for folder '%s'\n",
					  cw->name, cw, camel_folder_get_full_name (cw->select));
					g_queue_push_tail (&trash, link);
				}
			}
		}

		while ((link = g_queue_pop_head (&trash)) != NULL) {
			CamelIMAPXCommand *cw = link->data;
			g_queue_delete_link (&is->queue, link);
			g_queue_push_tail (&failed, cw);
		}

		QUEUE_UNLOCK (is);

		while (!g_queue_is_empty (&failed)) {
			CamelIMAPXCommand *cw;

			cw = g_queue_pop_head (&failed);

			if (ic->status)
				cw->status = imapx_copy_status (ic->status);
			if (cw->job->error == NULL) {
				if (ic->status == NULL)
					/* FIXME: why is ic->status == NULL here? It shouldn't happen. */
					g_debug ("imapx_command_select_done: ic->status is NULL.");
				g_set_error (
					&cw->job->error,
					CAMEL_IMAPX_ERROR, 1,
					"SELECT %s failed: %s",
					camel_folder_get_full_name (cw->select),
					ic->status && ic->status->text? ic->status->text:"<unknown reason>");
			}
			cw->complete (is, cw, NULL);
		}

		if (is->select_pending)
			g_object_unref (is->select_pending);

		/* A [CLOSED] status may have caused us to assume that it had happened */
		if (is->select_folder)
			is->select_folder = NULL;

		is->state = IMAPX_INITIALISED;

		g_propagate_error (error, local_error);
		success = FALSE;

	} else {
		CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) is->select_pending;
		CamelFolder *cfolder = is->select_pending;

		c(is->tagprefix, "Select ok!\n");

		if (!is->select_folder) {
			/* This could have been done earlier by a [CLOSED] status */
			is->select_folder = is->select_pending;
		}
		is->state = IMAPX_SELECTED;
		ifolder->exists_on_server = is->exists;
		ifolder->modseq_on_server = is->highestmodseq;
		if (ifolder->uidnext_on_server < is->uidnext) {
			imapx_server_fetch_new_messages (is, is->select_pending, TRUE, TRUE, NULL, NULL);
			/* We don't do this right now because we want the new messages to
			 * update the unseen count. */
			//ifolder->uidnext_on_server = is->uidnext;
		}
		ifolder->uidvalidity_on_server = is->uidvalidity;
		selected_folder = camel_folder_get_full_name (is->select_folder);

		if (is->uidvalidity && is->uidvalidity != ((CamelIMAPXSummary *) cfolder->summary)->validity)
			invalidate_local_cache (ifolder, is->uidvalidity);

#if 0
		/* This should trigger a new messages scan */
		if (is->exists != is->select_folder->summary->root_view->total_count)
			g_warning("exists is %d our summary is %d and summary exists is %d\n", is->exists,
				  is->select_folder->summary->root_view->total_count,
				  ((CamelIMAPXSummary *) is->select_folder->summary)->exists);
#endif
	}

	is->select_pending = NULL;
	camel_imapx_command_unref (ic);

	g_signal_emit (is, signals[SELECT_CHANGED], 0, selected_folder);

	return success;
}

/* Should have a queue lock. TODO Change the way select is written */
static gboolean
imapx_select (CamelIMAPXServer *is,
              CamelFolder *folder,
              gboolean forced,
              GCancellable *cancellable,
              GError **error)
{
	CamelIMAPXCommand *ic;

	/* Select is complicated by the fact we may have commands
	 * active on the server for a different selection.
	 *
	 * So this waits for any commands to complete, selects the
	 * new folder, and halts the queuing of any new commands.
	 * It is assumed whomever called is us about to issue
	 * a high-priority command anyway */

	/* TODO check locking here, pending_select will do
	 * most of the work for normal commands, but not
	 * for another select */

	if (is->select_pending)
		return TRUE;

	if (is->select_folder == folder && !forced)
		return TRUE;

	if (!g_queue_is_empty (&is->active))
		return TRUE;

	is->select_pending = folder;
	g_object_ref (folder);
	if (is->select_folder) {
		g_object_unref (is->select_folder);
		is->select_folder = NULL;
	} else {
		/* If no folder was selected, we won't get a [CLOSED] status
		 * so just point select_folder at the new folder immediately */
		is->select_folder = is->select_pending;
	}

	is->uidvalidity = 0;
	is->unseen = 0;
	is->highestmodseq = 0;
	is->permanentflags = 0;
	is->exists = 0;
	is->recent = 0;
	is->mode = 0;
	is->uidnext = 0;

	/* Hrm, what about reconnecting? */
	is->state = IMAPX_INITIALISED;

	ic = camel_imapx_command_new (
		is, "SELECT", NULL, "SELECT %f", folder);

	if (is->use_qresync) {
		CamelIMAPXSummary *isum = (CamelIMAPXSummary *) folder->summary;
		CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
		gint total = camel_folder_summary_count (folder->summary);
		gchar *firstuid, *lastuid;

		if (total && isum->modseq && ifolder->uidvalidity_on_server) {

			firstuid = imapx_get_uid_from_index (folder->summary, 0);
			lastuid = imapx_get_uid_from_index (folder->summary, total - 1);

			c(is->tagprefix, "SELECT QRESYNC %" G_GUINT64_FORMAT
			  " %" G_GUINT64_FORMAT "\n",
			  ifolder->uidvalidity_on_server, isum->modseq);

			camel_imapx_command_add (
				ic, " (QRESYNC (%"
				G_GUINT64_FORMAT " %"
				G_GUINT64_FORMAT " %s:%s",
				ifolder->uidvalidity_on_server,
				isum->modseq,
				firstuid, lastuid);

			g_free (firstuid);
			g_free (lastuid);

			if (total > 10) {
				gint i;
				GString *seqs, *uids;

				seqs = g_string_new(" ");
				uids = g_string_new(")");

				/* Include some seq/uid pairs to avoid a huge VANISHED list
				 * (see RFC5162 §3.1). Work backwards exponentially from the
				 * end of the mailbox, starting with the message 9 from the
				 * end, then 27 from the end, then 81 from the end... */
				i = 3;
				do {
					gchar buf[10];
					gchar *uid;
					i *= 3;
					if (i > total)
						i = total;

					if (i != 9) { /* If not the first time */
						g_string_prepend(seqs, ",");
						g_string_prepend(uids, ",");
					}

					/* IMAP sequence numbers are one higher than the corresponding
					 * indices in our folder summary -- they start from one, while
					 * the summary starts from zero. */
					sprintf(buf, "%d", total - i + 1);
					g_string_prepend (seqs, buf);
					uid = imapx_get_uid_from_index (folder->summary, total - i);
					g_string_prepend (uids, uid);
					g_free (uid);
				} while (i < total);

				g_string_prepend(seqs, " (");

				c(is->tagprefix, "adding QRESYNC seq/uidset %s%s\n", seqs->str, uids->str);
				camel_imapx_command_add (ic, seqs->str);
				camel_imapx_command_add (ic, uids->str);

				g_string_free (seqs, TRUE);
				g_string_free (uids, TRUE);

			}
			camel_imapx_command_add (ic, "))");
		}
	}

	ic->complete = imapx_command_select_done;
	imapx_command_start (is, ic, cancellable, error);

	return TRUE;
}

#ifndef G_OS_WIN32

/* Using custom commands to connect to IMAP servers is not supported on Win32 */

static gboolean
connect_to_server_process (CamelIMAPXServer *is,
                           const gchar *cmd,
                           GError **error)
{
	CamelNetworkSettings *network_settings;
	CamelProvider *provider;
	CamelSettings *settings;
	CamelStream *cmd_stream;
	CamelService *service;
	CamelURL url;
	gint ret, i = 0;
	gchar *buf;
	gchar *cmd_copy;
	gchar *full_cmd;
	gchar *child_env[7];
	const gchar *password;
	gchar *host;
	gchar *user;
	guint16 port;

	memset (&url, 0, sizeof (CamelURL));

	service = CAMEL_SERVICE (is->store);
	password = camel_service_get_password (service);
	provider = camel_service_get_provider (service);
	settings = camel_service_get_settings (service);
	g_return_val_if_fail (password != NULL, FALSE);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	port = camel_network_settings_get_port (network_settings);
	user = camel_network_settings_dup_user (network_settings);

	/* Put full details in the environment, in case the connection
	 * program needs them */
	camel_url_set_protocol (&url, provider->protocol);
	camel_url_set_host (&url, host);
	camel_url_set_port (&url, port);
	camel_url_set_user (&url, user);
	buf = camel_url_to_string (&url, 0);
	child_env[i++] = g_strdup_printf("URL=%s", buf);
	g_free (buf);

	child_env[i++] = g_strdup_printf("URLHOST=%s", host);
	if (port)
		child_env[i++] = g_strdup_printf("URLPORT=%u", port);
	if (user)
		child_env[i++] = g_strdup_printf("URLUSER=%s", user);
	if (password)
		child_env[i++] = g_strdup_printf("URLPASSWD=%s", password);
	child_env[i] = NULL;

	/* Now do %h, %u, etc. substitution in cmd */
	buf = cmd_copy = g_strdup (cmd);

	full_cmd = g_strdup("");

	for (;;) {
		gchar *pc;
		gchar *tmp;
		const gchar *var;
		gint len;

		pc = strchr (buf, '%');
	ignore:
		if (!pc) {
			tmp = g_strdup_printf("%s%s", full_cmd, buf);
			g_free (full_cmd);
			full_cmd = tmp;
			break;
		}

		len = pc - buf;

		var = NULL;

		switch (pc[1]) {
		case 'h':
			var = host;
			break;
		case 'u':
			var = user;
			break;
		}
		if (!var) {
			/* If there wasn't a valid %-code, with an actual
			 * variable to insert, pretend we didn't see the % */
			pc = strchr (pc + 1, '%');
			goto ignore;
		}
		tmp = g_strdup_printf("%s%.*s%s", full_cmd, len, buf, var);
		g_free (full_cmd);
		full_cmd = tmp;
		buf = pc + 2;
	}

	g_free (cmd_copy);

	g_free (host);
	g_free (user);

	cmd_stream = camel_stream_process_new ();

	ret = camel_stream_process_connect (
		CAMEL_STREAM_PROCESS (cmd_stream),
		full_cmd, (const gchar **) child_env, error);

	while (i)
		g_free (child_env[--i]);

	if (ret == -1) {
		g_object_unref (cmd_stream);
		g_free (full_cmd);
		return FALSE;
	}

	g_free (full_cmd);

	is->stream = (CamelIMAPXStream *) camel_imapx_stream_new (cmd_stream);
	g_object_unref (cmd_stream);
	is->is_process_stream = TRUE;

	return TRUE;
}
#endif /* G_OS_WIN32 */

gboolean
imapx_connect_to_server (CamelIMAPXServer *is,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelNetworkSettings *network_settings;
	CamelNetworkSecurityMethod method;
	CamelStream * tcp_stream = NULL;
	CamelSockOptData sockopt;
	CamelSettings *settings;
	CamelService *service;
	guint len;
	guchar *token;
	gint tok;
	CamelIMAPXCommand *ic;
	gboolean success = TRUE;
	gchar *host;
	GError *local_error = NULL;

#ifndef G_OS_WIN32
	gboolean use_shell_command;
	gchar *shell_command = NULL;
#endif

	service = CAMEL_SERVICE (is->store);
	settings = camel_service_get_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	method = camel_network_settings_get_security_method (network_settings);

#ifndef G_OS_WIN32
	use_shell_command = camel_imapx_settings_get_use_shell_command (
		CAMEL_IMAPX_SETTINGS (settings));

	if (use_shell_command)
		shell_command = camel_imapx_settings_dup_shell_command (
			CAMEL_IMAPX_SETTINGS (settings));

	if (shell_command != NULL) {
		gboolean success;

		success = connect_to_server_process (
			is, shell_command, &local_error);

		g_free (shell_command);

		if (success)
			goto connected;
		else
			goto exit;
	}
#endif

	tcp_stream = camel_network_service_connect_sync (
		CAMEL_NETWORK_SERVICE (is->store), cancellable, error);

	if (tcp_stream == NULL) {
		success = FALSE;
		goto exit;
	}

	is->stream = (CamelIMAPXStream *) camel_imapx_stream_new (tcp_stream);
	g_object_unref (tcp_stream);

	/* Disable Nagle - we send a lot of small requests which nagle slows down */
	sockopt.option = CAMEL_SOCKOPT_NODELAY;
	sockopt.value.no_delay = TRUE;
	camel_tcp_stream_setsockopt ((CamelTcpStream *) tcp_stream, &sockopt);

	/* Set keepalive - needed for some hosts/router configurations, we're idle a lot */
	sockopt.option = CAMEL_SOCKOPT_KEEPALIVE;
	sockopt.value.keep_alive = TRUE;
	camel_tcp_stream_setsockopt ((CamelTcpStream *) tcp_stream, &sockopt);

 connected:
	is->stream->tagprefix = is->tagprefix;
	while (1) {
		// poll ?  wait for other stuff? loop?
		if (camel_application_is_exiting || is->parser_quit) {
			g_set_error (
				error, G_IO_ERROR,
				G_IO_ERROR_CANCELLED,
				"Connection to server cancelled\n");
			success = FALSE;
			goto exit;
		}

		tok = camel_imapx_stream_token (is->stream, &token, &len, cancellable, error);
		if (tok < 0) {
			success = FALSE;
			goto exit;
		}

		if (tok == '*') {
			imapx_untagged (is, cancellable, error);
			break;
		}
		camel_imapx_stream_ungettoken (is->stream, tok, token, len);
		if (camel_imapx_stream_text (is->stream, &token, cancellable, error)) {
			success = FALSE;
			goto exit;
		}
		e(is->tagprefix, "Got unexpected line before greeting:  '%s'\n", token);
		g_free (token);
	}

	if (!is->cinfo) {
		ic = camel_imapx_command_new (
			is, "CAPABILITY", NULL, "CAPABILITY");
		if (!imapx_command_run (is, ic, cancellable, error)) {
			camel_imapx_command_unref (ic);
			success = FALSE;
			goto exit;
		}

		/* Server reported error. */
		if (ic->status->result != IMAPX_OK) {
			g_set_error (
				error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC,
				"%s", ic->status->text);
			camel_imapx_command_unref (ic);
			success = FALSE;
			goto exit;
		}

		camel_imapx_command_unref (ic);
	}

	if (method == CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT) {

		if (!(is->cinfo->capa & IMAPX_CAPABILITY_STARTTLS)) {
			g_set_error (
				&local_error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC,
				_("Failed to connect to IMAP server %s in secure mode: %s"),
				host, _("STARTTLS not supported"));
			goto exit;
		}

		ic = camel_imapx_command_new (
			is, "STARTTLS", NULL, "STARTTLS");
		if (!imapx_command_run (is, ic, cancellable, error)) {
			camel_imapx_command_unref (ic);
			success = FALSE;
			goto exit;
		}

		/* Server reported error. */
		if (ic->status->result != IMAPX_OK) {
			g_set_error (
				error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC,
				"%s", ic->status->text);
			camel_imapx_command_unref (ic);
			success = FALSE;
			goto exit;
		}

		/* See if we got new capabilities in the STARTTLS response */
		imapx_free_capability (is->cinfo);
		is->cinfo = NULL;
		if (ic->status->condition == IMAPX_CAPABILITY) {
			is->cinfo = ic->status->u.cinfo;
			ic->status->u.cinfo = NULL;
			c(is->tagprefix, "got capability flags %08x\n", is->cinfo->capa);
		}

		camel_imapx_command_unref (ic);

		if (camel_tcp_stream_ssl_enable_ssl (
			CAMEL_TCP_STREAM_SSL (tcp_stream),
			cancellable, &local_error) == -1) {
			g_prefix_error (
				&local_error,
				_("Failed to connect to IMAP server %s in secure mode: "),
				host);
			goto exit;
		}
		/* Get new capabilities if they weren't already given */
		if (!is->cinfo) {
			ic = camel_imapx_command_new (
				is, "CAPABILITY", NULL, "CAPABILITY");
			if (!imapx_command_run (is, ic, cancellable, error)) {
				camel_imapx_command_unref (ic);
				success = FALSE;
				goto exit;
			}

			camel_imapx_command_unref (ic);
		}
	}

exit:
	if (!success) {
		if (is->stream != NULL) {
			g_object_unref (is->stream);
			is->stream = NULL;
		}

		if (is->cinfo != NULL) {
			imapx_free_capability (is->cinfo);
			is->cinfo = NULL;
		}
	}

	g_free (host);

	return success;
}

CamelAuthenticationResult
camel_imapx_server_authenticate (CamelIMAPXServer *is,
                                 const gchar *mechanism,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelAuthenticationResult result;
	CamelIMAPXCommand *ic;
	CamelService *service;
	CamelSasl *sasl = NULL;
	gchar *host;
	gchar *user;

	g_return_val_if_fail (
		CAMEL_IS_IMAPX_SERVER (is),
		CAMEL_AUTHENTICATION_REJECTED);

	service = CAMEL_SERVICE (is->store);
	settings = camel_service_get_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	user = camel_network_settings_dup_user (network_settings);

	if (mechanism != NULL) {
		if (!g_hash_table_lookup (is->cinfo->auth_types, mechanism)) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("IMAP server %s does not support %s "
				  "authentication"), host, mechanism);
			result = CAMEL_AUTHENTICATION_ERROR;
			goto exit;
		}

		sasl = camel_sasl_new ("imap", mechanism, service);
		if (sasl != NULL) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("No support for %s authentication"),
				mechanism);
			result = CAMEL_AUTHENTICATION_ERROR;
			goto exit;
		}
	}

	if (sasl != NULL) {
		ic = camel_imapx_command_new (
			is, "AUTHENTICATE", NULL, "AUTHENTICATE %A", sasl);
	} else {
		const gchar *password;

		password = camel_service_get_password (service);

		if (user == NULL) {
			g_set_error_literal (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("Cannot authenticate without a username"));
			result = CAMEL_AUTHENTICATION_ERROR;
			goto exit;
		}

		if (password == NULL) {
			g_set_error_literal (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("Authentication password not available"));
			result = CAMEL_AUTHENTICATION_ERROR;
			goto exit;
		}

		ic = camel_imapx_command_new (
			is, "LOGIN", NULL, "LOGIN %s %s", user, password);
	}

	if (!imapx_command_run (is, ic, cancellable, error))
		result = CAMEL_AUTHENTICATION_ERROR;
	else if (ic->status->result == IMAPX_OK)
		result = CAMEL_AUTHENTICATION_ACCEPTED;
	else
		result = CAMEL_AUTHENTICATION_REJECTED;

	/* Forget old capabilities after login. */
	if (result == CAMEL_AUTHENTICATION_ACCEPTED) {
		if (is->cinfo) {
			imapx_free_capability (is->cinfo);
			is->cinfo = NULL;
		}

		if (ic->status->condition == IMAPX_CAPABILITY) {
			is->cinfo = ic->status->u.cinfo;
			ic->status->u.cinfo = NULL;
			c(is->tagprefix, "got capability flags %08x\n", is->cinfo->capa);
		}
	}

	camel_imapx_command_unref (ic);

	if (sasl != NULL)
		g_object_unref (sasl);

exit:
	g_free (host);
	g_free (user);

	return result;
}

static gboolean
imapx_reconnect (CamelIMAPXServer *is,
                 GCancellable *cancellable,
                 GError **error)
{
	CamelIMAPXCommand *ic;
	CamelService *service;
	CamelSession *session;
	CamelSettings *settings;
	gchar *mechanism;
	gboolean use_idle;
	gboolean use_qresync;

	service = CAMEL_SERVICE (is->store);
	session = camel_service_get_session (service);
	settings = camel_service_get_settings (service);

	mechanism = camel_network_settings_dup_auth_mechanism (
		CAMEL_NETWORK_SETTINGS (settings));

	use_idle = camel_imapx_settings_get_use_idle (
		CAMEL_IMAPX_SETTINGS (settings));

	use_qresync = camel_imapx_settings_get_use_qresync (
		CAMEL_IMAPX_SETTINGS (settings));

	if (!imapx_connect_to_server (is, cancellable, error))
		goto exception;

	if (is->state == IMAPX_AUTHENTICATED)
		goto preauthed;

	if (!camel_session_authenticate_sync (
		session, service, mechanism, cancellable, error))
		goto exception;

	/* After login we re-capa unless the server already told us */
	if (!is->cinfo) {
		ic = camel_imapx_command_new (
			is, "CAPABILITY", NULL, "CAPABILITY");
		if (!imapx_command_run (is, ic, cancellable, error)) {
			camel_imapx_command_unref (ic);
			goto exception;
		}

		camel_imapx_command_unref (ic);
	}

	is->state = IMAPX_AUTHENTICATED;

 preauthed:
	is->use_idle = use_idle;

	if (imapx_idle_supported (is))
		imapx_init_idle (is);

	/* Fetch namespaces */
	if (is->cinfo->capa & IMAPX_CAPABILITY_NAMESPACE) {
		ic = camel_imapx_command_new (
			is, "NAMESPACE", NULL, "NAMESPACE");
		if (!imapx_command_run (is, ic, cancellable, error)) {
			camel_imapx_command_unref (ic);
			goto exception;
		}

		camel_imapx_command_unref (ic);
	}

	if (use_qresync && is->cinfo->capa & IMAPX_CAPABILITY_QRESYNC) {
		ic = camel_imapx_command_new (
			is, "ENABLE", NULL, "ENABLE CONDSTORE QRESYNC");
		if (!imapx_command_run (is, ic, cancellable, error)) {
			camel_imapx_command_unref (ic);
			goto exception;
		}

		camel_imapx_command_unref (ic);

		is->use_qresync = TRUE;
	} else
		is->use_qresync = FALSE;

	if (((CamelIMAPXStore *) is->store)->summary->namespaces == NULL) {
		CamelIMAPXNamespaceList *nsl = NULL;
		CamelIMAPXStoreNamespace *ns = NULL;
		CamelIMAPXStore *imapx_store = (CamelIMAPXStore *) is->store;

		/* set a default namespace */
		nsl = g_malloc0 (sizeof (CamelIMAPXNamespaceList));
		ns = g_new0 (CamelIMAPXStoreNamespace, 1);
		ns->next = NULL;
		ns->path = g_strdup ("");
		ns->full_name = g_strdup ("");
		ns->sep = '/';
		nsl->personal = ns;
		imapx_store->summary->namespaces = nsl;
		/* FIXME needs to be identified from list response */
		imapx_store->dir_sep = ns->sep;
	}

	is->state = IMAPX_INITIALISED;

	g_free (mechanism);

	return TRUE;

exception:

	imapx_disconnect (is);

	if (is->cinfo) {
		imapx_free_capability (is->cinfo);
		is->cinfo = NULL;
	}

	g_free (mechanism);

	return FALSE;
}

/* ********************************************************************** */

static gboolean
imapx_command_fetch_message_done (CamelIMAPXServer *is,
                                  CamelIMAPXCommand *ic,
                                  GError **error)
{
	CamelIMAPXJob *job = ic->job;
	gboolean success = TRUE;
	GError *local_error = NULL;

	/* We either have more to fetch (partial mode?), we are complete,
	 * or we failed.  Failure is handled in the fetch code, so
	 * we just return the job, or keep it alive with more requests */

	job->commands--;

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error fetching message"));
		job->u.get_message.body_len = -1;

	} else if (job->u.get_message.use_multi_fetch) {
		gsize really_fetched = g_seekable_tell (G_SEEKABLE (job->u.get_message.stream));
		/* Don't automatically stop when we reach the reported message
		 * size -- some crappy servers (like Microsoft Exchange) have
		 * a tendency to lie about it. Keep going (one request at a
		 * time) until the data actually stop coming. */
		if (job->u.get_message.fetch_offset < job->u.get_message.size ||
		    job->u.get_message.fetch_offset == really_fetched) {
			camel_imapx_command_unref (ic);
			camel_operation_progress (
				job->cancellable,
				(job->u.get_message.fetch_offset *100) / job->u.get_message.size);

			ic = camel_imapx_command_new (
				is, "FETCH", job->folder,
				"UID FETCH %t (BODY.PEEK[]",
				job->u.get_message.uid);
			camel_imapx_command_add (ic, "<%u.%u>", job->u.get_message.fetch_offset, MULTI_SIZE);
			camel_imapx_command_add (ic, ")");
			ic->complete = imapx_command_fetch_message_done;
			ic->job = job;
			ic->pri = job->pri - 1;
			job->u.get_message.fetch_offset += MULTI_SIZE;
			job->commands++;
			imapx_command_queue (is, ic);
			return TRUE;
		}
	}

	if (job->commands == 0) {
		CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->folder;
		CamelStream *stream = job->u.get_message.stream;

		/* return the exception from last command */
		if (local_error != NULL) {
			if (stream)
				g_object_unref (stream);
			job->u.get_message.stream = NULL;

			g_propagate_error (error, local_error);
			local_error = NULL;
			success = FALSE;

		} else {
			CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->folder;

			if (stream) {
				gchar *tmp = camel_data_cache_get_filename (ifolder->cache, "tmp", job->u.get_message.uid, NULL);

				if (camel_stream_flush (stream, job->cancellable, &job->error) == 0 && camel_stream_close (stream, job->cancellable, &job->error) == 0) {
					gchar *cache_file = camel_data_cache_get_filename  (ifolder->cache, "cur", job->u.get_message.uid, NULL);
					gchar *temp = g_strrstr (cache_file, "/"), *dir;

					dir = g_strndup (cache_file, temp - cache_file);
					g_mkdir_with_parents (dir, 0700);
					g_free (dir);

					if (g_rename (tmp, cache_file) != 0)
						g_set_error (
							&job->error, CAMEL_IMAPX_ERROR, 1,
							"failed to copy the tmp file");
					g_free (cache_file);
				} else
					g_prefix_error (
						&job->error,
						_("Closing tmp stream failed: "));

				g_free (tmp);
				g_object_unref (job->u.get_message.stream);
				job->u.get_message.stream = camel_data_cache_get (ifolder->cache, "cur", job->u.get_message.uid, NULL);
			}
		}

		camel_data_cache_remove (ifolder->cache, "tmp", job->u.get_message.uid, NULL);
		imapx_job_done (is, job);
	}

	camel_imapx_command_unref (ic);

	g_clear_error (&local_error);

	return success;
}

static void
imapx_job_get_message_start (CamelIMAPXServer *is,
                             CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	gint i;

	if (job->u.get_message.use_multi_fetch) {
		for (i = 0; i < 3 && job->u.get_message.fetch_offset < job->u.get_message.size; i++) {
			ic = camel_imapx_command_new (
				is, "FETCH", job->folder,
				"UID FETCH %t (BODY.PEEK[]",
				job->u.get_message.uid);
			camel_imapx_command_add (ic, "<%u.%u>", job->u.get_message.fetch_offset, MULTI_SIZE);
			camel_imapx_command_add (ic, ")");
			ic->complete = imapx_command_fetch_message_done;
			ic->job = job;
			ic->pri = job->pri;
			job->u.get_message.fetch_offset += MULTI_SIZE;
			job->commands++;
			imapx_command_queue (is, ic);
		}
	} else {
		ic = camel_imapx_command_new (
			is, "FETCH", job->folder,
			"UID FETCH %t (BODY.PEEK[])",
			job->u.get_message.uid);
		ic->complete = imapx_command_fetch_message_done;
		ic->job = job;
		ic->pri = job->pri;
		job->commands++;
		imapx_command_queue (is, ic);
	}
}

/* ********************************************************************** */

static gboolean
imapx_command_copy_messages_step_done (CamelIMAPXServer *is,
                                       CamelIMAPXCommand *ic,
                                       GError **error)
{
	CamelIMAPXJob *job = ic->job;
	gint i = job->u.copy_messages.index;
	GPtrArray *uids = job->u.copy_messages.uids;
	gboolean success = TRUE;

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			&ic->job->error, "%s: ",
			_("Error copying messages"));
		success = FALSE;
		goto cleanup;
	}

	if (job->u.copy_messages.delete_originals) {
		gint j;

		for (j = job->u.copy_messages.last_index; j < i; j++)
			camel_folder_delete_message (job->folder, uids->pdata[j]);
	}

	/* TODO copy the summary and cached messages to the new folder. We might need a sorted insert to avoid refreshing the dest folder */
	if (ic->status->condition == IMAPX_COPYUID) {
		gint i;

		for (i = 0; i < ic->status->u.copyuid.copied_uids->len; i++) {
			guint32 uid = GPOINTER_TO_UINT (g_ptr_array_index (ic->status->u.copyuid.copied_uids, i));
			gchar *str = g_strdup_printf ("%d",uid);
			CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->u.copy_messages.dest;

			g_hash_table_insert (ifolder->ignore_recent, str, GINT_TO_POINTER (1));
		}

	}

	if (i < uids->len) {
		camel_imapx_command_unref (ic);
		imapx_command_copy_messages_step_start (is, job, i);
		return TRUE;
	}

cleanup:
	g_object_unref (job->u.copy_messages.dest);
	g_object_unref (job->folder);

	imapx_job_done (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_command_copy_messages_step_start (CamelIMAPXServer *is,
                                        CamelIMAPXJob *job,
                                        gint index)
{
	CamelIMAPXCommand *ic;
	GPtrArray *uids = job->u.copy_messages.uids;
	gint i = index;

	ic = camel_imapx_command_new (
		is, "COPY", job->folder, "UID COPY ");
	ic->complete = imapx_command_copy_messages_step_done;
	ic->job = job;
	ic->pri = job->pri;
	job->u.copy_messages.last_index = i;

	for (; i < uids->len; i++) {
		gint res;
		const gchar *uid = (gchar *) g_ptr_array_index (uids, i);

		res = imapx_uidset_add (&job->u.copy_messages.uidset, ic, uid);
		if (res == 1) {
			camel_imapx_command_add (ic, " %f", job->u.copy_messages.dest);
			job->u.copy_messages.index = i;
			imapx_command_queue (is, ic);
			return;
		}
	}

	job->u.copy_messages.index = i;
	if (imapx_uidset_done (&job->u.copy_messages.uidset, ic)) {
		camel_imapx_command_add (ic, " %f", job->u.copy_messages.dest);
		imapx_command_queue (is, ic);
		return;
	}
}

static void
imapx_job_copy_messages_start (CamelIMAPXServer *is,
                               CamelIMAPXJob *job)
{
	if (!imapx_server_sync_changes (
		is, job->folder, job->pri, job->cancellable, &job->error))
		imapx_job_done (is, job);

	g_ptr_array_sort (job->u.copy_messages.uids, (GCompareFunc) imapx_uids_array_cmp);
	imapx_uidset_init (&job->u.copy_messages.uidset, 0, MAX_COMMAND_LEN);
	imapx_command_copy_messages_step_start (is, job, 0);
}

/* ********************************************************************** */

static gboolean
imapx_command_append_message_done (CamelIMAPXServer *is,
                                   CamelIMAPXCommand *ic,
                                   GError **error)
{
	CamelIMAPXJob *job = ic->job;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->folder;
	CamelMessageInfo *mi;
	gchar *cur, *old_uid;
	gboolean success = TRUE;

	/* Append done.  If we the server supports UIDPLUS we will get an APPENDUID response
	 * with the new uid.  This lets us move the message we have directly to the cache
	 * and also create a correctly numbered MessageInfo, without losing any information.
	 * Otherwise we have to wait for the server to less us know it was appended. */

	mi = camel_message_info_clone (job->u.append_message.info);
	old_uid = g_strdup (job->u.append_message.info->uid);

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error appending message"));
		success = FALSE;

	} else if (ic->status->condition == IMAPX_APPENDUID) {
		c(is->tagprefix, "Got appenduid %d %d\n", (gint)ic->status->u.appenduid.uidvalidity, (gint)ic->status->u.appenduid.uid);
		if (ic->status->u.appenduid.uidvalidity == ifolder->uidvalidity_on_server) {
			CamelFolderChangeInfo *changes;
			gchar *uid;

			uid = g_strdup_printf("%u", (guint)ic->status->u.appenduid.uid);
			mi->uid = camel_pstring_add (uid, TRUE);

			cur = camel_data_cache_get_filename  (ifolder->cache, "cur", mi->uid, NULL);
			g_rename (job->u.append_message.path, cur);

			/* should we update the message count ? */
			camel_folder_summary_add (job->folder->summary, mi);
			imapx_set_message_info_flags_for_new_message (mi,
								      ((CamelMessageInfoBase *) job->u.append_message.info)->flags,
								      ((CamelMessageInfoBase *) job->u.append_message.info)->user_flags,
								      job->folder);
			changes = camel_folder_change_info_new ();
			camel_folder_change_info_add_uid (changes, mi->uid);
			camel_folder_changed (job->folder, changes);
			camel_folder_change_info_free (changes);

			g_free (cur);
		} else {
			g_message ("but uidvalidity changed \n");
		}
	}

	camel_data_cache_remove (ifolder->cache, "new", old_uid, NULL);
	g_free (old_uid);
	camel_message_info_free (job->u.append_message.info);
	g_free (job->u.append_message.path);
	g_object_unref (job->folder);

	imapx_job_done (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_append_message_start (CamelIMAPXServer *is,
                                CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	/* TODO: we could supply the original append date from the file timestamp */
	ic = camel_imapx_command_new (
		is, "APPEND", NULL,
		"APPEND %f %F %P", job->folder,
		((CamelMessageInfoBase *) job->u.append_message.info)->flags,
		((CamelMessageInfoBase *) job->u.append_message.info)->user_flags,
		job->u.append_message.path);
	ic->complete = imapx_command_append_message_done;
	ic->job = job;
	ic->pri = job->pri;
	job->commands++;
	imapx_command_queue (is, ic);
}

/* ********************************************************************** */

static gint
imapx_refresh_info_uid_cmp (gconstpointer ap,
                            gconstpointer bp,
                            gboolean ascending)
{
	guint av, bv;

	av = g_ascii_strtoull ((const gchar *) ap, NULL, 10);
	bv = g_ascii_strtoull ((const gchar *) bp, NULL, 10);

	if (av < bv)
		return ascending ? -1 : 1;
	else if (av > bv)
		return ascending ? 1 : -1;
	else
		return 0;
}

static gint
imapx_uids_array_cmp (gconstpointer ap,
                      gconstpointer bp)
{
	const gchar **a = (const gchar **) ap;
	const gchar **b = (const gchar **) bp;

	return imapx_refresh_info_uid_cmp (*a, *b, TRUE);
}

static gint
imapx_refresh_info_cmp (gconstpointer ap,
                        gconstpointer bp)
{
	const struct _refresh_info *a = ap;
	const struct _refresh_info *b = bp;

	return imapx_refresh_info_uid_cmp (a->uid, b->uid, TRUE);
}

static gint
imapx_refresh_info_cmp_descending (gconstpointer ap,
                                   gconstpointer bp)
{
	const struct _refresh_info *a = ap;
	const struct _refresh_info *b = bp;

	return imapx_refresh_info_uid_cmp (a->uid, b->uid, FALSE);

}

/* skips over non-server uids (pending appends) */
static guint
imapx_index_next (GPtrArray *uids,
                  CamelFolderSummary *s,
                  guint index)
{

	while (index < uids->len) {
		CamelMessageInfo *info;

		index++;
		if (index >= uids->len)
			break;

		info = camel_folder_summary_get (s, g_ptr_array_index (uids, index));
		if (!info)
			continue;

		if (info && (strchr (camel_message_info_uid (info), '-') != NULL)) {
			camel_message_info_free (info);
			e('?', "Ignoring offline uid '%s'\n", camel_message_info_uid(info));
		} else {
			camel_message_info_free (info);
			break;
		}
	}

	return index;
}

static gboolean
imapx_command_step_fetch_done (CamelIMAPXServer *is,
                               CamelIMAPXCommand *ic,
                               GError **error)
{
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) ic->job->folder;
	CamelIMAPXSummary *isum = (CamelIMAPXSummary *) ic->job->folder->summary;
	CamelIMAPXJob *job = ic->job;
	gint i = job->u.refresh_info.index;
	GArray *infos = job->u.refresh_info.infos;
	gboolean success = TRUE;

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error fetching message headers"));
		success = FALSE;
		goto cleanup;
	}

	if (camel_folder_change_info_changed (job->u.refresh_info.changes)) {
		imapx_update_store_summary (job->folder);
		camel_folder_summary_save_to_db (job->folder->summary, NULL);
		camel_folder_changed (job->folder, job->u.refresh_info.changes);
	}

	camel_folder_change_info_clear (job->u.refresh_info.changes);

	if (i < infos->len) {
		camel_imapx_command_unref (ic);

		ic = camel_imapx_command_new (
			is, "FETCH", job->folder, "UID FETCH ");
		ic->complete = imapx_command_step_fetch_done;
		ic->job = job;
		ic->pri = job->pri - 1;
		job->u.refresh_info.last_index = i;

		for (; i < infos->len; i++) {
			gint res;
			struct _refresh_info *r = &g_array_index (infos, struct _refresh_info, i);

			if (!r->exists) {
				res = imapx_uidset_add (&job->u.refresh_info.uidset, ic, r->uid);
				if (res == 1) {
					camel_imapx_command_add (ic, " (RFC822.SIZE RFC822.HEADER)");
					job->u.refresh_info.index = i;
					imapx_command_queue (is, ic);
					return TRUE;
				}
			}
		}

		job->u.refresh_info.index = i;
		if (imapx_uidset_done (&job->u.refresh_info.uidset, ic)) {
			camel_imapx_command_add (ic, " (RFC822.SIZE RFC822.HEADER)");
			imapx_command_queue (is, ic);
			return TRUE;
		}
	}

	if (camel_folder_summary_count (job->folder->summary)) {
		gchar *uid = imapx_get_uid_from_index (job->folder->summary,
						       camel_folder_summary_count (job->folder->summary) - 1);
		guint64 uidl = strtoull (uid, NULL, 10);
		g_free (uid);

		uidl++;

		if (uidl > ifolder->uidnext_on_server) {
			c(is->tagprefix, "Updating uidnext_on_server for '%s' to %" G_GUINT64_FORMAT "\n",
			  camel_folder_get_full_name (job->folder), uidl);
			ifolder->uidnext_on_server = uidl;
		}
	}
	isum->uidnext = ifolder->uidnext_on_server;

 cleanup:
	for (i = 0; i < infos->len; i++) {
		struct _refresh_info *r = &g_array_index (infos, struct _refresh_info, i);

		camel_flag_list_free (&r->server_user_flags);
		g_free (r->uid);
	}
	g_array_free (job->u.refresh_info.infos, TRUE);
	if (job->type == IMAPX_JOB_FETCH_NEW_MESSAGES)
		camel_folder_change_info_free (job->u.refresh_info.changes);

	imapx_job_done (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static gint
imapx_uid_cmp (gconstpointer ap,
               gconstpointer bp,
               gpointer data)
{
	const gchar *a = ap, *b = bp;
	gchar *ae, *be;
	gulong av, bv;

	av = strtoul (a, &ae, 10);
	bv = strtoul (b, &be, 10);

	if (av < bv)
		return -1;
	else if (av > bv)
		return 1;

	if (*ae == '-')
		ae++;
	if (*be == '-')
		be++;

	return strcmp (ae, be);
}

static gboolean
imapx_job_scan_changes_done (CamelIMAPXServer *is,
                             CamelIMAPXCommand *ic,
                             GError **error)
{
	CamelIMAPXJob *job = ic->job;
	CamelService *service;
	CamelSettings *settings;
	gint i;
	GArray *infos = job->u.refresh_info.infos;
	guint uidset_size;
	gboolean success = TRUE;

	service = CAMEL_SERVICE (is->store);
	settings = camel_service_get_settings (service);

	uidset_size = camel_imapx_settings_get_batch_fetch_count (
		CAMEL_IMAPX_SETTINGS (settings));

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error retrieving message"));
		success = FALSE;

	} else {
		GCompareDataFunc uid_cmp = imapx_uid_cmp;
		CamelMessageInfo *s_minfo = NULL;
		CamelIMAPXMessageInfo *info;
		CamelFolderSummary *s = job->folder->summary;
		CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->folder;
		GList *removed = NULL, *l;
		gboolean fetch_new = FALSE;
		gint i;
		guint j = 0;
		GPtrArray *uids;

		/* Actually we wanted to do this after the SELECT but before the
		 * FETCH command was issued. But this should suffice. */
		((CamelIMAPXSummary *) s)->uidnext = ifolder->uidnext_on_server;
		((CamelIMAPXSummary *) s)->modseq = ifolder->modseq_on_server;

		/* Here we do the typical sort/iterate/merge loop.
		 * If the server flags dont match what we had, we modify our
		 * flags to pick up what the server now has - but we merge
		 * not overwrite */

		/* FIXME: We also have to check the offline directory for
		 * anything missing in our summary, and also queue up jobs
		 * for all outstanding messages to be uploaded */

		/* obtain a copy to be thread safe */
		uids = camel_folder_summary_get_array (s);

		qsort (infos->data, infos->len, sizeof (struct _refresh_info), imapx_refresh_info_cmp);
		g_ptr_array_sort (uids, (GCompareFunc) imapx_uids_array_cmp);

		if (uids->len)
			s_minfo = camel_folder_summary_get (s, g_ptr_array_index (uids, 0));

		for (i = 0; i < infos->len; i++) {
			struct _refresh_info *r = &g_array_index (infos, struct _refresh_info, i);

			while (s_minfo && uid_cmp (camel_message_info_uid (s_minfo), r->uid, s) < 0) {
				const gchar *uid = camel_message_info_uid (s_minfo);

				camel_folder_change_info_remove_uid (job->u.refresh_info.changes, uid);
				removed = g_list_prepend (removed, (gpointer ) g_strdup (uid));
				camel_message_info_free (s_minfo);
				s_minfo = NULL;

				j = imapx_index_next (uids, s, j);
				if (j < uids->len)
					s_minfo = camel_folder_summary_get (s, g_ptr_array_index (uids, j));
			}

			info = NULL;
			if (s_minfo && uid_cmp (s_minfo->uid, r->uid, s) == 0) {
				info = (CamelIMAPXMessageInfo *) s_minfo;

				if (imapx_update_message_info_flags ((CamelMessageInfo *) info, r->server_flags, r->server_user_flags, is->permanentflags, job->folder, FALSE))
					camel_folder_change_info_change_uid (job->u.refresh_info.changes, camel_message_info_uid (s_minfo));
				r->exists = TRUE;
			} else
				fetch_new = TRUE;

			if (s_minfo) {
				camel_message_info_free (s_minfo);
				s_minfo = NULL;
			}

			if (j >= uids->len)
				break;

			j = imapx_index_next (uids, s, j);
			if (j < uids->len)
				s_minfo = camel_folder_summary_get (s, g_ptr_array_index (uids, j));
		}

		if (s_minfo)
			camel_message_info_free (s_minfo);

		while (j < uids->len) {
			s_minfo = camel_folder_summary_get (s, g_ptr_array_index (uids, j));

			if (!s_minfo) {
				j++;
				continue;
			}

			e(is->tagprefix, "Message %s vanished\n", s_minfo->uid);
			removed = g_list_prepend (removed, (gpointer) g_strdup (s_minfo->uid));
			camel_message_info_free (s_minfo);
			j++;
		}

		for (l = removed; l != NULL; l = g_list_next (l)) {
			gchar *uid = (gchar *) l->data;

			camel_folder_change_info_remove_uid (job->u.refresh_info.changes, uid);
			camel_folder_summary_remove_uid (s, uid);
		}

		if (removed != NULL) {
			const gchar *full_name;

			full_name = camel_folder_get_full_name (camel_folder_summary_get_folder (s));
			camel_db_delete_uids (is->store->cdb_w, full_name, removed, NULL);
			g_list_free_full (removed, (GDestroyNotify) g_free);
		}

		imapx_update_store_summary (job->folder);

		if (camel_folder_change_info_changed (job->u.refresh_info.changes))
			camel_folder_changed (job->folder, job->u.refresh_info.changes);
		camel_folder_change_info_clear (job->u.refresh_info.changes);

		camel_folder_summary_free_array (uids);

		/* If we have any new messages, download their headers, but only a few (100?) at a time */
		if (fetch_new) {
			job->pop_operation_msg = TRUE;

			camel_operation_push_message (
				job->cancellable,
				_("Fetching summary information for new messages in %s"),
				camel_folder_get_display_name (job->folder));

			imapx_uidset_init (&job->u.refresh_info.uidset, uidset_size, 0);
			/* These are new messages which arrived since we last knew the unseen count;
			 * update it as they arrive. */
			job->u.refresh_info.update_unseen = TRUE;
			return imapx_command_step_fetch_done (is, ic, error);
		}
	}

	for (i = 0; i < infos->len; i++) {
		struct _refresh_info *r = &g_array_index (infos, struct _refresh_info, i);

		camel_flag_list_free (&r->server_user_flags);
		g_free (r->uid);
	}

	/* There's no sane way to get the server-side unseen count on the
	 * select mailbox. So just work it out from the flags */
	((CamelIMAPXFolder *) job->folder)->unread_on_server = camel_folder_summary_get_unread_count (job->folder->summary);

	g_array_free (job->u.refresh_info.infos, TRUE);
	imapx_job_done (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_scan_changes_start (CamelIMAPXServer *is,
                              CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	job->pop_operation_msg = TRUE;

	camel_operation_push_message (
		job->cancellable,
		_("Scanning for changed messages in %s"),
		camel_folder_get_display_name (job->folder));

	ic = camel_imapx_command_new (
		is, "FETCH", job->folder,
		"UID FETCH 1:* (UID FLAGS)");
	ic->job = job;
	ic->complete = imapx_job_scan_changes_done;
	ic->pri = job->pri;
	job->u.refresh_info.infos = g_array_new (0, 0, sizeof (struct _refresh_info));
	imapx_command_queue (is, ic);
}

static gboolean
imapx_command_fetch_new_messages_done (CamelIMAPXServer *is,
                                       CamelIMAPXCommand *ic,
                                       GError **error)
{
	CamelIMAPXSummary *isum = (CamelIMAPXSummary *) ic->job->folder->summary;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) ic->job->folder;
	gboolean success = TRUE;

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error fetching new messages"));
		success = FALSE;
		goto exception;
	}

	if (camel_folder_change_info_changed (ic->job->u.refresh_info.changes)) {
		imapx_update_store_summary (ic->job->folder);
		camel_folder_summary_save_to_db (ic->job->folder->summary, NULL);
		camel_folder_changed (ic->job->folder, ic->job->u.refresh_info.changes);
		camel_folder_change_info_clear (ic->job->u.refresh_info.changes);
	}

	if (camel_folder_summary_count (ic->job->folder->summary)) {
		gchar *uid = imapx_get_uid_from_index (ic->job->folder->summary,
						       camel_folder_summary_count (ic->job->folder->summary) - 1);
		guint64 uidl = strtoull (uid, NULL, 10);
		g_free (uid);

		uidl++;

		if (uidl > ifolder->uidnext_on_server) {
			c(is->tagprefix, "Updating uidnext_on_server for '%s' to %" G_GUINT64_FORMAT "\n",
			  camel_folder_get_full_name (ic->job->folder), uidl);
			ifolder->uidnext_on_server = uidl;
		}
	}

	isum->uidnext = ifolder->uidnext_on_server;

exception:
	camel_folder_change_info_free (ic->job->u.refresh_info.changes);

	imapx_job_done (is, ic->job);
	camel_imapx_command_unref (ic);

	return success;
}

static gboolean
imapx_command_fetch_new_uids_done (CamelIMAPXServer *is,
                                   CamelIMAPXCommand *ic,
                                   GError **error)
{
	CamelIMAPXJob *job = ic->job;
	GArray *infos = job->u.refresh_info.infos;

	qsort (
		infos->data, infos->len,
		sizeof (struct _refresh_info),
		imapx_refresh_info_cmp_descending);

	return imapx_command_step_fetch_done (is, ic, error);
}

static void
imapx_job_fetch_new_messages_start (CamelIMAPXServer *is,
                                    CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	CamelFolder *folder = job->folder;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
	CamelService *service;
	CamelSettings *settings;
	CamelSortType fetch_order;
	guint32 total, diff;
	guint uidset_size;
	gchar *uid = NULL;

	service = CAMEL_SERVICE (is->store);
	settings = camel_service_get_settings (service);

	fetch_order = camel_imapx_settings_get_fetch_order (
		CAMEL_IMAPX_SETTINGS (settings));

	uidset_size = camel_imapx_settings_get_batch_fetch_count (
		CAMEL_IMAPX_SETTINGS (settings));

	total = camel_folder_summary_count (folder->summary);
	diff = ifolder->exists_on_server - total;

	if (total > 0) {
		guint64 uidl;
		uid = imapx_get_uid_from_index (folder->summary, total - 1);
		uidl = strtoull (uid, NULL, 10);
		g_free (uid);
		uid = g_strdup_printf ("%" G_GUINT64_FORMAT, uidl+1);
	} else
		uid = g_strdup ("1");

	job->pop_operation_msg = TRUE;

	camel_operation_push_message (
		job->cancellable,
		_("Fetching summary information for new messages in %s"),
		camel_folder_get_display_name (folder));

	if (diff > uidset_size || fetch_order == CAMEL_SORT_DESCENDING) {
		ic = camel_imapx_command_new (
			is, "FETCH", job->folder,
			"UID FETCH %s:* (UID FLAGS)", uid);
		imapx_uidset_init (&job->u.refresh_info.uidset, uidset_size, 0);
		job->u.refresh_info.infos = g_array_new (0, 0, sizeof (struct _refresh_info));
		ic->pri = job->pri;

		if (fetch_order == CAMEL_SORT_DESCENDING)
			ic->complete = imapx_command_fetch_new_uids_done;
		else
			ic->complete = imapx_command_step_fetch_done;
	} else {
		ic = camel_imapx_command_new (
			is, "FETCH", job->folder,
			"UID FETCH %s:* (RFC822.SIZE RFC822.HEADER FLAGS)", uid);
		ic->pri = job->pri;
		ic->complete = imapx_command_fetch_new_messages_done;
	}

	g_free (uid);
	ic->job = job;
	imapx_command_queue (is, ic);
}

static void
imapx_job_refresh_info_start (CamelIMAPXServer *is,
                              CamelIMAPXJob *job)
{
	guint32 total;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->folder;
	CamelIMAPXSummary *isum = (CamelIMAPXSummary *) job->folder->summary;
	CamelFolder *folder = job->folder;
	const gchar *full_name;
	gboolean need_rescan = FALSE;
	gboolean is_selected = FALSE;
	gboolean can_qresync = FALSE;

	full_name = camel_folder_get_full_name (folder);

	/* Sync changes first, else unread count will not
	 * match. Need to think about better ways for this */
	if (!imapx_server_sync_changes (
		is, folder, job->pri,
		job->cancellable, &job->error))
		goto done;

#if 0 /* There are issues with this still; continue with the buggy behaviour
	 where we issue STATUS on the current folder, for now...*/
	if (is->select_folder == folder)
		is_selected = TRUE;
#endif
	total = camel_folder_summary_count (folder->summary);

	if (ifolder->uidvalidity_on_server && isum->validity && isum->validity != ifolder->uidvalidity_on_server) {
		invalidate_local_cache (ifolder, ifolder->uidvalidity_on_server);
		need_rescan = TRUE;
	}

	/* We don't have valid unread count or modseq for currently-selected server
	 * (unless we want to re-SELECT it). We fake unread count when fetching
	 * message flags, but don't depend on modseq for the selected folder */
	if (total != ifolder->exists_on_server ||
	    isum->uidnext != ifolder->uidnext_on_server ||
	    camel_folder_summary_get_unread_count (folder->summary) != ifolder->unread_on_server ||
	    (!is_selected && isum->modseq != ifolder->modseq_on_server))
		need_rescan = TRUE;

	/* This is probably the first check of this folder after startup;
	 * use STATUS to check whether the cached summary is valid, rather
	 * than blindly updating. Only for servers which support CONDSTORE
	 * though. */
	if ((isum->modseq && !ifolder->modseq_on_server))
		need_rescan = FALSE;

	/* If we don't think there's anything to do, poke it to check */
	if (!need_rescan) {
		CamelIMAPXCommand *ic;

		if (is_selected) {
			/* We may not issue STATUS on the current folder. Use SELECT or NOOP instead. */
			if (0 /* server needs SELECT not just NOOP */) {
				if (imapx_idle_supported (is) && imapx_in_idle (is))
					if (!imapx_stop_idle (is, &job->error))
						goto done;
				/* This doesn't work -- this is an immediate command, not queued */
				if (!imapx_select (
					is, folder, TRUE,
					job->cancellable, &job->error))
					goto done;
			} else {
				/* Or maybe just NOOP, unless we're in IDLE in which case do nothing */
				if (!imapx_idle_supported (is) || !imapx_in_idle (is)) {
					if (!camel_imapx_server_noop (is, folder, job->cancellable, &job->error))
						goto done;
				}
			}
		} else {
			if (is->cinfo->capa & IMAPX_CAPABILITY_CONDSTORE)
				ic = camel_imapx_command_new (
					is, "STATUS", NULL,
					"STATUS %f (MESSAGES UNSEEN UIDVALIDITY UIDNEXT HIGHESTMODSEQ)", folder);
			else
				ic = camel_imapx_command_new (
					is, "STATUS", NULL,
					"STATUS %f (MESSAGES UNSEEN UIDVALIDITY UIDNEXT)", folder);

			ic->job = job;
			ic->pri = job->pri;

			imapx_command_run_sync (
				is, ic, job->cancellable, &job->error);

			if (ic->job->error == NULL)
				camel_imapx_command_set_error_if_failed (ic, &ic->job->error);

			g_prefix_error (
				&ic->job->error, "%s: ",
				_("Error refreshing folder"));

			if (ic->job->error != NULL) {
				camel_imapx_command_unref (ic);
				goto done;
			}

			camel_imapx_command_unref (ic);
		}

		/* Recalulate need_rescan */
		if (total != ifolder->exists_on_server ||
		    isum->uidnext != ifolder->uidnext_on_server ||
		    camel_folder_summary_get_unread_count (folder->summary) != ifolder->unread_on_server ||
		    (!is_selected && isum->modseq != ifolder->modseq_on_server))
			need_rescan = TRUE;

	}

	if (is->use_qresync && isum->modseq && ifolder->uidvalidity_on_server)
		can_qresync = TRUE;

	e(is->tagprefix, "folder %s is %sselected, total %u / %u, unread %u / %u, modseq %" G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT ", uidnext %u / %u: will %srescan\n",
	  full_name, is_selected?"": "not ", total, ifolder->exists_on_server,
	  camel_folder_summary_get_unread_count (folder->summary), ifolder->unread_on_server,
	  (guint64) isum->modseq, (guint64) ifolder->modseq_on_server,
	  isum->uidnext, ifolder->uidnext_on_server,
	  need_rescan?"":"not ");

	/* Fetch new messages first, so that they appear to the user ASAP */
	if (ifolder->exists_on_server > total ||
	    ifolder->uidnext_on_server > isum->uidnext)
	{
		if (!total)
			need_rescan = FALSE;

		if (!imapx_server_fetch_new_messages (
			is, folder, FALSE, FALSE,
			job->cancellable, &job->error))
			goto done;

		/* If QRESYNC-capable we'll have got all flags changes in SELECT */
		if (can_qresync)
			goto qresync_done;
	}

	if (!need_rescan)
		goto done;

	if (can_qresync) {
		/* Actually we only want to select it; no need for the NOOP */
		camel_imapx_server_noop (is, folder, job->cancellable, &job->error);
	qresync_done:
		isum->modseq = ifolder->modseq_on_server;
		total = camel_folder_summary_count (job->folder->summary);
		if (total != ifolder->exists_on_server ||
		    camel_folder_summary_get_unread_count (folder->summary) != ifolder->unread_on_server ||
		    (isum->modseq != ifolder->modseq_on_server)) {
			c(is->tagprefix, "Eep, after QRESYNC we're out of sync. total %u / %u, unread %u / %u, modseq %" G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT "\n",
			  total, ifolder->exists_on_server,
			  camel_folder_summary_get_unread_count (folder->summary), ifolder->unread_on_server,
			  isum->modseq, ifolder->modseq_on_server);
		} else {
			c(is->tagprefix, "OK, after QRESYNC we're still in sync. total %u / %u, unread %u / %u, modseq %" G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT "\n",
			  total, ifolder->exists_on_server,
			  camel_folder_summary_get_unread_count (folder->summary), ifolder->unread_on_server,
			  isum->modseq, ifolder->modseq_on_server);
			goto done;
		}
	}

	imapx_job_scan_changes_start (is, job);
	return;

done:
	imapx_job_done (is, job);
}

/* ********************************************************************** */

static gboolean
imapx_command_expunge_done (CamelIMAPXServer *is,
                            CamelIMAPXCommand *ic,
                            GError **error)
{
	gboolean success = TRUE;

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error expunging message"));
		success = FALSE;

	} else {
		GPtrArray *uids;
		CamelFolder *folder = ic->job->folder;
		CamelStore *parent_store;
		const gchar *full_name;

		full_name = camel_folder_get_full_name (folder);
		parent_store = camel_folder_get_parent_store (folder);

		camel_folder_summary_save_to_db (folder->summary, NULL);
		uids = camel_db_get_folder_deleted_uids (parent_store->cdb_r, full_name, &ic->job->error);

		if (uids && uids->len)	{
			CamelFolderChangeInfo *changes;
			GList *removed = NULL;
			gint i;

			changes = camel_folder_change_info_new ();
			for (i = 0; i < uids->len; i++) {
				gchar *uid = uids->pdata[i];

				camel_folder_summary_remove_uid (folder->summary, uid);
				camel_folder_change_info_remove_uid (changes, uids->pdata[i]);
				removed = g_list_prepend (removed, (gpointer) uids->pdata[i]);
			}

			camel_db_delete_uids (parent_store->cdb_w, full_name, removed, NULL);
			camel_folder_summary_save_to_db (folder->summary, NULL);
			camel_folder_changed (folder, changes);
			camel_folder_change_info_free (changes);

			g_list_free (removed);
			g_ptr_array_foreach (uids, (GFunc) camel_pstring_free, NULL);
			g_ptr_array_free (uids, TRUE);
		}
	}

	imapx_job_done (is, ic->job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_expunge_start (CamelIMAPXServer *is,
                         CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	imapx_server_sync_changes (
		is, job->folder, job->pri,
		job->cancellable, &job->error);

	/* TODO handle UIDPLUS capability */
	ic = camel_imapx_command_new (
		is, "EXPUNGE", job->folder, "EXPUNGE");
	ic->job = job;
	ic->pri = job->pri;
	ic->complete = imapx_command_expunge_done;
	imapx_command_queue (is, ic);
}

/* ********************************************************************** */

static gboolean
imapx_command_list_done (CamelIMAPXServer *is,
                         CamelIMAPXCommand *ic,
                         GError **error)
{
	gboolean success = TRUE;

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error fetching folders"));
		success = FALSE;
	}

	e (is->tagprefix, "==== list or lsub completed ==== \n");
	imapx_job_done (is, ic->job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_list_start (CamelIMAPXServer *is,
                      CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	ic = camel_imapx_command_new (
		is, "LIST", NULL,
		"%s \"\" %s",
		(job->u.list.flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) ?
			"LSUB" : "LIST",
		job->u.list.pattern);
	if (job->u.list.ext) {
		/* Hm, we need a way to add atoms _without_ quoting or using literals */
		camel_imapx_command_add (ic, " ");
		camel_imapx_command_add (ic, job->u.list.ext);
	}
	ic->pri = job->pri;
	ic->job = job;
	ic->complete = imapx_command_list_done;
	imapx_command_queue (is, ic);
}
/* ********************************************************************** */

static gchar *
imapx_encode_folder_name (CamelIMAPXStore *istore,
                          const gchar *folder_name)
{
	gchar *fname, *encoded;

	fname = camel_imapx_store_summary_full_from_path (istore->summary, folder_name);
	if (fname) {
		encoded = camel_utf8_utf7 (fname);
		g_free (fname);
	} else
		encoded = camel_utf8_utf7 (folder_name);

	return encoded;
}

static gboolean
imapx_command_subscription_done (CamelIMAPXServer *is,
                                 CamelIMAPXCommand *ic,
                                 GError **error)
{
	gboolean success = TRUE;

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error subscribing to folder"));
		success = FALSE;
	}

	imapx_job_done (is, ic->job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_manage_subscription_start (CamelIMAPXServer *is,
                                     CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	gchar *encoded_fname = NULL;

	encoded_fname = imapx_encode_folder_name (
		(CamelIMAPXStore *) is->store,
		job->u.manage_subscriptions.folder_name);
	if (job->u.manage_subscriptions.subscribe)
		ic = camel_imapx_command_new (
			is, "SUBSCRIBE", NULL,
			"SUBSCRIBE %s", encoded_fname);
	else
		ic = camel_imapx_command_new (
			is, "UNSUBSCRIBE", NULL,
			"UNSUBSCRIBE %s", encoded_fname);

	ic->pri = job->pri;
	ic->job = job;
	ic->complete = imapx_command_subscription_done;
	imapx_command_queue (is, ic);

	g_free (encoded_fname);
}

/* ********************************************************************** */

static gboolean
imapx_command_create_folder_done (CamelIMAPXServer *is,
                                  CamelIMAPXCommand *ic,
                                  GError **error)
{
	gboolean success = TRUE;

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error creating folder"));
		success = FALSE;
	}

	imapx_job_done (is, ic->job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_create_folder_start (CamelIMAPXServer *is,
                               CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	gchar *encoded_fname = NULL;

	encoded_fname = camel_utf8_utf7 (job->u.folder_name);
	ic = camel_imapx_command_new (
		is, "CREATE", NULL,
		"CREATE %s", encoded_fname);
	ic->pri = job->pri;
	ic->job = job;
	ic->complete = imapx_command_create_folder_done;
	imapx_command_queue (is, ic);

	g_free (encoded_fname);
}

/* ********************************************************************** */

static gboolean
imapx_command_delete_folder_done (CamelIMAPXServer *is,
                                  CamelIMAPXCommand *ic,
                                  GError **error)
{
	gboolean success;

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error deleting folder"));
		success = FALSE;
	}

	imapx_job_done (is, ic->job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_delete_folder_start (CamelIMAPXServer *is,
                               CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	gchar *encoded_fname = NULL;

	encoded_fname = imapx_encode_folder_name ((CamelIMAPXStore *) is->store, job->u.folder_name);

	job->folder = camel_store_get_folder_sync (
		is->store, "INBOX", 0, job->cancellable, &job->error);

	/* make sure to-be-deleted folder is not selected by selecting INBOX for this operation */
	ic = camel_imapx_command_new (
		is, "DELETE", job->folder,
		"DELETE %s", encoded_fname);
	ic->pri = job->pri;
	ic->job = job;
	ic->complete = imapx_command_delete_folder_done;
	imapx_command_queue (is, ic);

	g_free (encoded_fname);
}

/* ********************************************************************** */

static gboolean
imapx_command_rename_folder_done (CamelIMAPXServer *is,
                                  CamelIMAPXCommand *ic,
                                  GError **error)
{
	gboolean success = TRUE;

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error renaming folder"));
		success = FALSE;
	}

	imapx_job_done (is, ic->job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_rename_folder_start (CamelIMAPXServer *is,
                               CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	gchar *en_ofname = NULL, *en_nfname = NULL;

	job->folder = camel_store_get_folder_sync (
		is->store, "INBOX", 0, job->cancellable, &job->error);

	en_ofname = imapx_encode_folder_name ((CamelIMAPXStore *) is->store, job->u.rename_folder.ofolder_name);
	en_nfname = imapx_encode_folder_name ((CamelIMAPXStore *) is->store, job->u.rename_folder.nfolder_name);

	ic = camel_imapx_command_new (
		is, "RENAME", job->folder,
		"RENAME %s %s", en_ofname, en_nfname);
	ic->pri = job->pri;
	ic->job = job;
	ic->complete = imapx_command_rename_folder_done;
	imapx_command_queue (is, ic);

	g_free (en_ofname);
	g_free (en_nfname);
}

/* ********************************************************************** */

static gboolean
imapx_command_noop_done (CamelIMAPXServer *is,
                         CamelIMAPXCommand *ic,
                         GError **error)
{
	gboolean success = TRUE;

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error performing NOOP"));
		success = FALSE;
	}

	imapx_job_done (is, ic->job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_noop_start (CamelIMAPXServer *is,
                      CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	ic = camel_imapx_command_new (
		is, "NOOP", job->folder, "NOOP");

	ic->job = job;
	ic->complete = imapx_command_noop_done;
	if (job->folder)
		ic->pri = IMAPX_PRIORITY_REFRESH_INFO;
	else
		ic->pri = IMAPX_PRIORITY_NOOP;
	imapx_command_queue (is, ic);
}

/* ********************************************************************** */

/* FIXME: this is basically a copy of the same in camel-imapx-utils.c */
static struct {
	const gchar *name;
	guint32 flag;
} flags_table[] = {
	{ "\\ANSWERED", CAMEL_MESSAGE_ANSWERED },
	{ "\\DELETED", CAMEL_MESSAGE_DELETED },
	{ "\\DRAFT", CAMEL_MESSAGE_DRAFT },
	{ "\\FLAGGED", CAMEL_MESSAGE_FLAGGED },
	{ "\\SEEN", CAMEL_MESSAGE_SEEN },
	{ "\\RECENT", CAMEL_IMAPX_MESSAGE_RECENT },
	{ "JUNK", CAMEL_MESSAGE_JUNK },
	{ "NOTJUNK", CAMEL_MESSAGE_NOTJUNK }
};

/*
 *  flags 00101000
 * sflags 01001000
 * ^      01100000
 * ~flags 11010111
 * &      01000000
 *
 * &flags 00100000
 */

static gboolean
imapx_command_sync_changes_done (CamelIMAPXServer *is,
                                 CamelIMAPXCommand *ic,
                                 GError **error)
{
	CamelIMAPXJob *job = ic->job;
	CamelStore *parent_store;
	const gchar *full_name;
	gboolean success = TRUE;

	job->commands--;

	full_name = camel_folder_get_full_name (job->folder);
	parent_store = camel_folder_get_parent_store (job->folder);

	/* If this worked, we should really just update the changes that we
	 * sucessfully stored, so we dont have to worry about sending them
	 * again ...
	 * But then we'd have to track which uid's we actually updated, so
	 * its easier just to refresh all of the ones we got.
	 *
	 * Not that ... given all the asynchronicity going on, we're guaranteed
	 * that what we just set is actually what is on the server now .. but
	 * if it isn't, i guess we'll fix up next refresh */

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error syncing changes"));
		success = FALSE;

	/* lock cache ? */
	} else {
		gint i;

		for (i = 0; i < job->u.sync_changes.changed_uids->len; i++) {
			CamelIMAPXMessageInfo *xinfo = (CamelIMAPXMessageInfo *) camel_folder_summary_get (job->folder->summary,
					job->u.sync_changes.changed_uids->pdata[i]);

			if (!xinfo)
				continue;

			xinfo->server_flags = ((CamelMessageInfoBase *) xinfo)->flags & CAMEL_IMAPX_SERVER_FLAGS;
			xinfo->info.flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
			xinfo->info.dirty = TRUE;
			camel_flag_list_copy (&xinfo->server_user_flags, &xinfo->info.user_flags);

			camel_folder_summary_touch (job->folder->summary);
			camel_message_info_free (xinfo);
		}
		/* Apply the changes to server-side unread count; it won't tell
		 * us of these changes, of course. */
		((CamelIMAPXFolder *) job->folder)->unread_on_server += job->u.sync_changes.unread_change;
	}

	if (job->commands == 0) {
		if (job->folder->summary && (job->folder->summary->flags & CAMEL_SUMMARY_DIRTY) != 0) {
			CamelStoreInfo *si;

			/* ... and store's summary when folder's summary is dirty */
			si = camel_store_summary_path ((CamelStoreSummary *)((CamelIMAPXStore *) parent_store)->summary, full_name);
			if (si) {
				if (si->total != camel_folder_summary_get_saved_count (job->folder->summary) ||
				    si->unread != camel_folder_summary_get_unread_count (job->folder->summary)) {
					si->total = camel_folder_summary_get_saved_count (job->folder->summary);
					si->unread = camel_folder_summary_get_unread_count (job->folder->summary);
					camel_store_summary_touch ((CamelStoreSummary *)((CamelIMAPXStore *) parent_store)->summary);
				}

				camel_store_summary_info_free ((CamelStoreSummary *)((CamelIMAPXStore *) parent_store)->summary, si);
			}
		}

		camel_folder_summary_save_to_db (job->folder->summary, &job->error);
		camel_store_summary_save ((CamelStoreSummary *)((CamelIMAPXStore *) parent_store)->summary);

		imapx_job_done (is, job);
	}

	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_sync_changes_start (CamelIMAPXServer *is,
                              CamelIMAPXJob *job)
{
	guint32 i, j;
	struct _uidset_state ss;
	GPtrArray *uids = job->u.sync_changes.changed_uids;
	gint on;

	for (on = 0; on < 2; on++) {
		guint32 orset = on ? job->u.sync_changes.on_set : job->u.sync_changes.off_set;
		GArray *user_set = on ? job->u.sync_changes.on_user : job->u.sync_changes.off_user;

		for (j = 0; j < G_N_ELEMENTS (flags_table); j++) {
			guint32 flag = flags_table[j].flag;
			CamelIMAPXCommand *ic = NULL;

			if ((orset & flag) == 0)
				continue;

			c(is->tagprefix, "checking/storing %s flags '%s'\n", on?"on":"off", flags_table[j].name);
			imapx_uidset_init (&ss, 0, 100);
			for (i = 0; i < uids->len; i++) {
				CamelIMAPXMessageInfo *info = (CamelIMAPXMessageInfo *) camel_folder_summary_get
										(job->folder->summary, uids->pdata[i]);
				guint32 flags;
				guint32 sflags;
				gint send;

				if (!info)
					continue;

				flags = ((CamelMessageInfoBase *) info)->flags & CAMEL_IMAPX_SERVER_FLAGS;
				sflags = info->server_flags & CAMEL_IMAPX_SERVER_FLAGS;
				send = 0;

				if ( (on && (((flags ^ sflags) & flags) & flag))
				     || (!on && (((flags ^ sflags) & ~flags) & flag))) {
					if (ic == NULL) {
						ic = camel_imapx_command_new (
							is, "STORE", job->folder,
							"UID STORE ");
						ic->complete = imapx_command_sync_changes_done;
						ic->job = job;
						ic->pri = job->pri;
					}
					send = imapx_uidset_add (&ss, ic, camel_message_info_uid (info));
				}
				if (send || (i == uids->len - 1 && imapx_uidset_done (&ss, ic))) {
					job->commands++;
					camel_imapx_command_add (ic, " %tFLAGS.SILENT (%t)", on?"+":"-", flags_table[j].name);
					imapx_command_queue (is, ic);
					ic = NULL;
				}
				if (flag == CAMEL_MESSAGE_SEEN) {
					/* Remember how the server's unread count will change if this
					 * command succeeds */
					if (on)
						job->u.sync_changes.unread_change--;
					else
						job->u.sync_changes.unread_change++;
				}
				camel_message_info_free (info);
			}
		}

		if (user_set) {
			CamelIMAPXCommand *ic = NULL;

			for (j = 0; j < user_set->len; j++) {
				struct _imapx_flag_change *c = &g_array_index (user_set, struct _imapx_flag_change, j);

				imapx_uidset_init (&ss, 0, 100);
				for (i = 0; i < c->infos->len; i++) {
					CamelIMAPXMessageInfo *info = c->infos->pdata[i];

					if (ic == NULL) {
						ic = camel_imapx_command_new (
							is, "STORE", job->folder,
							"UID STORE ");
						ic->complete = imapx_command_sync_changes_done;
						ic->job = job;
						ic->pri = job->pri;
					}

					if (imapx_uidset_add (&ss, ic, camel_message_info_uid (info))
					    || (i == c->infos->len - 1 && imapx_uidset_done (&ss, ic))) {
						job->commands++;
						camel_imapx_command_add (ic, " %tFLAGS.SILENT (%t)", on?"+":"-", c->name);
						imapx_command_queue (is, ic);
						ic = NULL;
					}
				}
			}
		}
	}

	/* Since this may start in another thread ... we need to
	 * lock the commands count, ho hum */

	if (job->commands == 0) {
		imapx_job_done (is, job);
	}
}

/* we cancel all the commands and their jobs, so associated jobs will be notified */
static void
cancel_all_jobs (CamelIMAPXServer *is,
                 GError *error)
{
	CamelIMAPXCommand *ic;
	GQueue queue = G_QUEUE_INIT;

	QUEUE_LOCK (is);

	while ((ic = g_queue_pop_head (&is->queue)) != NULL)
		g_queue_push_tail (&queue, ic);

	while ((ic = g_queue_pop_head (&is->active)) != NULL)
		g_queue_push_tail (&queue, ic);

	QUEUE_UNLOCK (is);

	while ((ic = g_queue_pop_head (&queue)) != NULL) {
		if (ic->job->error == NULL)
			ic->job->error = g_error_copy (error);

		/* Send a NULL GError since we've already set
		 * the job's GError, and we're not interested
		 * in individual command errors. */
		ic->complete (is, ic, NULL);
	}
}

/* ********************************************************************** */

static void
parse_contents (CamelIMAPXServer *is,
                GCancellable *cancellable,
                GError **error)
{
	while (imapx_step (is, cancellable, error))
		if (camel_imapx_stream_buffered (is->stream) == 0)
			break;
}

/*
 * The main processing (reading) loop.
 *
 * Main area of locking required is command_queue
 * and command_start_next, the 'literal' command,
 * the jobs queue, the active queue, the queue
 * queue. */
static gpointer
imapx_parser_thread (gpointer d)
{
	CamelIMAPXServer *is = d;
	GCancellable *cancellable;
	GError *local_error = NULL;

	QUEUE_LOCK (is);
	cancellable = camel_operation_new ();
	is->cancellable = g_object_ref (cancellable);
	QUEUE_UNLOCK (is);

	while (local_error == NULL && is->stream) {
		g_cancellable_reset (cancellable);

#ifndef G_OS_WIN32
		if (is->is_process_stream)	{
			GPollFD fds[2] = { {0, 0, 0}, {0, 0, 0} };
			gint res;

			fds[0].fd = ((CamelStreamProcess *) is->stream->source)->sockfd;
			fds[0].events = G_IO_IN;
			fds[1].fd = g_cancellable_get_fd (cancellable);
			fds[1].events = G_IO_IN;
			res = g_poll (fds, 2, -1);
			if (res == -1)
				g_usleep (1) /* ?? */ ;
			else if (res == 0)
				/* timed out */;
			else if (fds[0].revents & G_IO_IN)
				parse_contents (is, cancellable, &local_error);
			g_cancellable_release_fd (cancellable);
		} else
#endif
		{
			parse_contents (is, cancellable, &local_error);
		}

		if (is->parser_quit)
			g_cancellable_cancel (cancellable);

		if (g_cancellable_is_cancelled (cancellable)) {
			gint is_empty;

			QUEUE_LOCK (is);
			is_empty = g_queue_is_empty (&is->active);
			QUEUE_UNLOCK (is);

			if (is_empty || (imapx_idle_supported (is) && imapx_in_idle (is))) {
				g_cancellable_reset (cancellable);
				g_clear_error (&local_error);
			} else {
				/* Cancelled error should be set. */
				g_warn_if_fail (local_error != NULL);
			}
		}

		/* Jump out of the loop if an error occurred. */
		if (local_error != NULL)
			break;
	}

	QUEUE_LOCK (is);
	is->state = IMAPX_SHUTDOWN;
	QUEUE_UNLOCK (is);

	cancel_all_jobs (is, local_error);

	g_clear_error (&local_error);

	QUEUE_LOCK (is);
	if (is->cancellable != NULL) {
		g_object_unref (is->cancellable);
		is->cancellable = NULL;
	}
	g_object_unref (cancellable);
	QUEUE_UNLOCK (is);

	is->parser_quit = FALSE;

	g_signal_emit (is, signals[SHUTDOWN], 0);

	return NULL;
}

static gboolean
join_helper (gpointer thread)
{
	g_thread_join (thread);
	return FALSE;
}

static void
imapx_server_dispose (GObject *object)
{
	CamelIMAPXServer *server = CAMEL_IMAPX_SERVER (object);

	QUEUE_LOCK (server);
	server->state = IMAPX_SHUTDOWN;

	server->parser_quit = TRUE;

	if (server->cancellable != NULL) {
		g_cancellable_cancel (server->cancellable);
		g_object_unref (server->cancellable);
		server->cancellable = NULL;
	}
	QUEUE_UNLOCK (server);

	if (server->parser_thread) {
		if (server->parser_thread == g_thread_self ())
			g_idle_add (&join_helper, server->parser_thread);
		else
			g_thread_join (server->parser_thread);
		server->parser_thread = NULL;
	}

	if (server->cinfo && imapx_idle_supported (server))
		imapx_exit_idle (server);

	imapx_disconnect (server);

	if (server->session != NULL) {
		g_object_unref (server->session);
		server->session = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_server_parent_class)->dispose (object);
}

static void
imapx_server_finalize (GObject *object)
{
	CamelIMAPXServer *is = CAMEL_IMAPX_SERVER (object);

	g_static_rec_mutex_free (&is->queue_lock);
	g_static_rec_mutex_free (&is->ostream_lock);
	g_mutex_free (is->fetch_mutex);
	g_cond_free (is->fetch_cond);

	camel_folder_change_info_free (is->changes);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_server_parent_class)->finalize (object);
}

static void
imapx_server_constructed (GObject *object)
{
	CamelIMAPXServer *server;
	CamelIMAPXServerClass *class;

	server = CAMEL_IMAPX_SERVER (object);
	class = CAMEL_IMAPX_SERVER_GET_CLASS (server);

	server->tagprefix = class->tagprefix;
	class->tagprefix++;
	if (class->tagprefix > 'Z')
		class->tagprefix = 'A';
}

static void
camel_imapx_server_class_init (CamelIMAPXServerClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = imapx_server_finalize;
	object_class->constructed = imapx_server_constructed;
	object_class->dispose = imapx_server_dispose;

	class->select_changed = NULL;
	class->shutdown = NULL;

	/**
	 * CamelIMAPXServer::select_changed
	 * @server: the #CamelIMAPXServer which emitted the signal
	 **/
	signals[SELECT_CHANGED] = g_signal_new (
		"select_changed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelIMAPXServerClass, select_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__STRING,
		G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * CamelIMAPXServer::shutdown
	 * @server: the #CamelIMAPXServer which emitted the signal
	 **/
	signals[SHUTDOWN] = g_signal_new (
		"shutdown",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelIMAPXServerClass, shutdown),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);

	class->tagprefix = 'A';
}

static void
camel_imapx_server_init (CamelIMAPXServer *is)
{
	g_queue_init (&is->queue);
	g_queue_init (&is->active);
	g_queue_init (&is->done);
	g_queue_init (&is->jobs);

	/* not used at the moment. Use it in future */
	is->job_timeout = 29 * 60 * 1000 * 1000;

	g_static_rec_mutex_init (&is->queue_lock);
	g_static_rec_mutex_init (&is->ostream_lock);

	is->state = IMAPX_DISCONNECTED;

	is->expunged = NULL;
	is->changes = camel_folder_change_info_new ();
	is->parser_quit = FALSE;

	is->fetch_mutex = g_mutex_new ();
	is->fetch_cond = g_cond_new ();
}

CamelIMAPXServer *
camel_imapx_server_new (CamelStore *store)
{
	CamelService *service;
	CamelSession *session;
	CamelIMAPXServer *is;

	service = CAMEL_SERVICE (store);
	session = camel_service_get_session (service);

	is = g_object_new (CAMEL_TYPE_IMAPX_SERVER, NULL);
	is->session = g_object_ref (session);
	is->store = store;

	return is;
}

static gboolean
imapx_disconnect (CamelIMAPXServer *is)
{
	gboolean ret = TRUE;

	g_static_rec_mutex_lock (&is->ostream_lock);

	if (is->stream) {
		if (camel_stream_close (is->stream->source, NULL, NULL) == -1)
			ret = FALSE;

		g_object_unref (is->stream);
		is->stream = NULL;
	}

	/* TODO need a select lock */
	if (is->select_folder) {
		g_object_unref (is->select_folder);
		is->select_folder = NULL;
	}

	if (is->select_pending) {
		g_object_unref (is->select_pending);
		is->select_pending = NULL;
	}

	if (is->cinfo) {
		imapx_free_capability (is->cinfo);
		is->cinfo = NULL;
	}

	is->state = IMAPX_DISCONNECTED;

	g_static_rec_mutex_unlock (&is->ostream_lock);

	return ret;
}

/* Client commands */
gboolean
camel_imapx_server_connect (CamelIMAPXServer *is,
                            GCancellable *cancellable,
                            GError **error)
{
	gboolean success;

	if (is->state == IMAPX_SHUTDOWN) {
		g_set_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE, "Shutting down");
		return FALSE;
	}

	if (is->state >= IMAPX_INITIALISED)
		return TRUE;

	g_static_rec_mutex_lock (&is->ostream_lock);
	success = imapx_reconnect (is, cancellable, error);
	g_static_rec_mutex_unlock (&is->ostream_lock);

	if (!success)
		return FALSE;

	is->parser_thread = g_thread_create ((GThreadFunc) imapx_parser_thread, is, TRUE, NULL);
	return TRUE;
}

static CamelStream *
imapx_server_get_message (CamelIMAPXServer *is,
                          CamelFolder *folder,
                          const gchar *uid,
                          gint pri,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelStream *stream = NULL, *tmp_stream;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
	CamelIMAPXJob *job;
	CamelMessageInfo *mi;
	gboolean registered;
	gboolean success;

	QUEUE_LOCK (is);

	if ((job = imapx_is_job_in_queue (is, folder, IMAPX_JOB_GET_MESSAGE, uid))) {
		if (pri > job->pri)
			job->pri = pri;

		/* Wait for the job to finish. This would be so much nicer if
		 * we could just use the queue lock with a GCond, but instead
		 * we have to use a GMutex. I miss the kernel waitqueues. */
		do {
			gint this;

			g_mutex_lock (is->fetch_mutex);
			this = is->fetch_count;

			QUEUE_UNLOCK (is);

			while (is->fetch_count == this)
				g_cond_wait (is->fetch_cond, is->fetch_mutex);

			g_mutex_unlock (is->fetch_mutex);

			QUEUE_LOCK (is);

		} while (imapx_is_job_in_queue (is, folder,
						IMAPX_JOB_GET_MESSAGE, uid));

		QUEUE_UNLOCK (is);

		stream = camel_data_cache_get (
			ifolder->cache, "cur", uid, error);
		if (stream == NULL)
			g_prefix_error (
				error, "Could not retrieve the message: ");
		return stream;
	}

	mi = camel_folder_summary_get (folder->summary, uid);
	if (!mi) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_UID,
			_("Cannot get message with message ID %s: %s"),
			uid, _("No such message available."));
		QUEUE_UNLOCK (is);
		return NULL;
	}

	tmp_stream = camel_data_cache_add (ifolder->cache, "tmp", uid, NULL);

	job = imapx_job_new (cancellable);
	job->pri = pri;
	job->type = IMAPX_JOB_GET_MESSAGE;
	job->start = imapx_job_get_message_start;
	job->folder = folder;
	job->u.get_message.uid = (gchar *) uid;
	job->u.get_message.stream = tmp_stream;

	if (((CamelMessageInfoBase *) mi)->size > MULTI_SIZE)
		job->u.get_message.use_multi_fetch = TRUE;

	job->u.get_message.size = ((CamelMessageInfoBase *) mi)->size;
	camel_message_info_free (mi);
	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	success = registered && imapx_run_job (is, job, error);

	if (success)
		stream = job->u.get_message.stream;
	else if (job->u.get_message.stream != NULL)
		g_object_unref (job->u.get_message.stream);

	imapx_job_unref (job);

	g_mutex_lock (is->fetch_mutex);
	is->fetch_count++;
	g_cond_broadcast (is->fetch_cond);
	g_mutex_unlock (is->fetch_mutex);

	return stream;
}

CamelStream *
camel_imapx_server_get_message (CamelIMAPXServer *is,
                                CamelFolder *folder,
                                const gchar *uid,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelStream *stream;

	stream = imapx_server_get_message (
		is, folder, uid,
		IMAPX_PRIORITY_GET_MESSAGE,
		cancellable, error);

	return stream;
}

gboolean
camel_imapx_server_sync_message (CamelIMAPXServer *is,
                                 CamelFolder *folder,
                                 const gchar *uid,
                                 GCancellable *cancellable,
                                 GError **error)
{
	gchar *cache_file = NULL;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
	CamelStream *stream;
	gboolean is_cached;
	struct stat st;

	/* Check if the cache file already exists and is non-empty. */
	cache_file = camel_data_cache_get_filename (
		ifolder->cache, "cur", uid, NULL);
	is_cached = (g_stat (cache_file, &st) == 0 && st.st_size > 0);
	g_free (cache_file);

	if (is_cached)
		return TRUE;

	stream = imapx_server_get_message (
		is, folder, uid,
		IMAPX_PRIORITY_SYNC_MESSAGE,
		cancellable, error);

	if (stream == NULL)
		return FALSE;

	g_object_unref (stream);

	return TRUE;
}

gboolean
camel_imapx_server_copy_message (CamelIMAPXServer *is,
                                 CamelFolder *source,
                                 CamelFolder *dest,
                                 GPtrArray *uids,
                                 gboolean delete_originals,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelIMAPXJob *job;

	job = imapx_job_new (cancellable);
	job->pri = IMAPX_PRIORITY_APPEND_MESSAGE;
	job->type = IMAPX_JOB_COPY_MESSAGE;
	job->start = imapx_job_copy_messages_start;
	job->folder = source;
	job->u.copy_messages.dest = dest;
	job->u.copy_messages.uids = uids;
	job->u.copy_messages.delete_originals = delete_originals;

	g_object_ref (source);
	g_object_ref (dest);

	return imapx_submit_job (is, job, error);
}

gboolean
camel_imapx_server_append_message (CamelIMAPXServer *is,
                                   CamelFolder *folder,
                                   CamelMimeMessage *message,
                                   const CamelMessageInfo *mi,
                                   GCancellable *cancellable,
                                   GError **error)
{
	gchar *uid = NULL, *tmp = NULL;
	CamelStream *stream, *filter;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
	CamelMimeFilter *canon;
	CamelIMAPXJob *job;
	CamelMessageInfo *info;
	gint res;
	gboolean success;

	/* Append just assumes we have no/a dodgy connection.  We dump stuff into the 'new'
	 * directory, and let the summary know it's there.  Then we fire off a no-reply
	 * job which will asynchronously upload the message at some point in the future,
	 * and fix up the summary to match */

	/* chen cleanup this later */
	uid = imapx_get_temp_uid ();
	stream = camel_data_cache_add (ifolder->cache, "new", uid, error);
	if (stream == NULL) {
		g_prefix_error (error, _("Cannot create spool file: "));
		g_free (uid);
		return FALSE;
	}

	filter = camel_stream_filter_new (stream);
	g_object_unref (stream);
	canon = camel_mime_filter_canon_new (CAMEL_MIME_FILTER_CANON_CRLF);
	camel_stream_filter_add ((CamelStreamFilter *) filter, canon);
	res = camel_data_wrapper_write_to_stream_sync (
		(CamelDataWrapper *) message, filter, cancellable, error);
	g_object_unref (canon);
	g_object_unref (filter);

	if (res == -1) {
		g_prefix_error (error, _("Cannot create spool file: "));
		camel_data_cache_remove (ifolder->cache, "new", uid, NULL);
		g_free (uid);
		return FALSE;
	}

	tmp = camel_data_cache_get_filename (ifolder->cache, "new", uid, NULL);
	info = camel_folder_summary_info_new_from_message ((CamelFolderSummary *) folder->summary, message, NULL);
	info->uid = camel_pstring_strdup (uid);
	if (mi)
		((CamelMessageInfoBase *) info)->flags = ((CamelMessageInfoBase *) mi)->flags;
	g_free (uid);

	/* So, we actually just want to let the server loop that
	 * messages need appending, i think.  This is so the same
	 * mechanism is used for normal uploading as well as
	 * offline re-syncing when we go back online */

	job = imapx_job_new (cancellable);
	job->pri = IMAPX_PRIORITY_APPEND_MESSAGE;
	job->type = IMAPX_JOB_APPEND_MESSAGE;
	job->start = imapx_job_append_message_start;
	job->folder = g_object_ref (folder);
	job->noreply = FALSE;
	job->u.append_message.info = info;
	job->u.append_message.path = tmp;

	success = imapx_submit_job (is, job, error);

	imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_noop (CamelIMAPXServer *is,
                         CamelFolder *folder,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	job = imapx_job_new (cancellable);
	job->type = IMAPX_JOB_NOOP;
	job->start = imapx_job_noop_start;
	job->folder = folder;
	job->pri = IMAPX_PRIORITY_NOOP;

	success = imapx_submit_job (is, job, error);

	imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_refresh_info (CamelIMAPXServer *is,
                                 CamelFolder *folder,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelIMAPXJob *job;
	gboolean registered = TRUE;
	const gchar *full_name;
	gboolean success = TRUE;

	full_name = camel_folder_get_full_name (folder);

	QUEUE_LOCK (is);

	if (imapx_is_job_in_queue (is, folder, IMAPX_JOB_REFRESH_INFO, NULL)) {
		QUEUE_UNLOCK (is);
		return TRUE;
	}

	job = imapx_job_new (cancellable);
	job->type = IMAPX_JOB_REFRESH_INFO;
	job->start = imapx_job_refresh_info_start;
	job->folder = folder;
	job->u.refresh_info.changes = camel_folder_change_info_new ();
	job->pri = IMAPX_PRIORITY_REFRESH_INFO;

	if (g_ascii_strcasecmp(full_name, "INBOX") == 0)
		job->pri += 10;

	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	success = registered && imapx_run_job (is, job, error);

	if (success && camel_folder_change_info_changed (job->u.refresh_info.changes))
		camel_folder_changed (folder, job->u.refresh_info.changes);

	camel_folder_change_info_free (job->u.refresh_info.changes);

	imapx_job_unref (job);

	return success;
}

static void
imapx_sync_free_user (GArray *user_set)
{
	gint i;

	if (user_set == NULL)
		return;

	for (i = 0; i < user_set->len; i++) {
		struct _imapx_flag_change *flag_change = &g_array_index (user_set, struct _imapx_flag_change, i);
		GPtrArray *infos = flag_change->infos;
		gint j;

		for (j = 0; j < infos->len; j++) {
			CamelMessageInfo *info = g_ptr_array_index (infos, j);
			camel_message_info_free (info);
		}

		g_ptr_array_free (infos, TRUE);
		g_free (flag_change->name);
	}
	g_array_free (user_set, TRUE);
}

static gboolean
imapx_server_sync_changes (CamelIMAPXServer *is,
                           CamelFolder *folder,
                           gint pri,
                           GCancellable *cancellable,
                           GError **error)
{
	guint i, on_orset, off_orset;
	GPtrArray *uids;
	GArray *on_user = NULL, *off_user = NULL;
	CamelIMAPXMessageInfo *info;
	CamelIMAPXJob *job;
	gboolean registered;
	gboolean success = TRUE;

	/* We calculate two masks, a mask of all flags which have been
	 * turned off and a mask of all flags which have been turned
	 * on. If either of these aren't 0, then we have work to do,
	 * and we fire off a job to do it.
 *
	 * User flags are a bit more tricky, we rely on the user
	 * flags being sorted, and then we create a bunch of lists;
	 * one for each flag being turned off, including each
	 * info being turned off, and one for each flag being turned on.
	*/
	uids = camel_folder_summary_get_changed (folder->summary);

	if (uids->len == 0) {
		camel_folder_free_uids (folder, uids);
		return TRUE;
	}

	off_orset = on_orset = 0;
	for (i = 0; i < uids->len; i++) {
		guint32 flags, sflags;
		CamelFlag *uflags, *suflags;
		guint j = 0;

		info = (CamelIMAPXMessageInfo *) camel_folder_summary_get (folder->summary, uids->pdata[i]);

		if (!info)
			continue;

		if (!(info->info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED)) {
			camel_message_info_free (info);
			continue;
		}

		flags = ((CamelMessageInfoBase *) info)->flags & CAMEL_IMAPX_SERVER_FLAGS;
		sflags = info->server_flags & CAMEL_IMAPX_SERVER_FLAGS;
		if (flags != sflags) {
			off_orset |= ( flags ^ sflags ) & ~flags;
			on_orset |= (flags ^ sflags) & flags;
		}

		uflags = ((CamelMessageInfoBase *) info)->user_flags;
		suflags = info->server_user_flags;
		while (uflags || suflags) {
			gint res;

			if (uflags) {
				if (suflags)
					res = strcmp (uflags->name, suflags->name);
				else if (uflags->name && *uflags->name)
					res = -1;
				else {
					uflags = uflags->next;
					continue;
				}
			} else {
				res = 1;
			}

			if (res == 0) {
				uflags = uflags->next;
				suflags = suflags->next;
			} else {
				GArray *user_set;
				CamelFlag *user_flag;
				struct _imapx_flag_change *change = NULL, add = { 0 };

				if (res < 0) {
					if (on_user == NULL)
						on_user = g_array_new (FALSE, FALSE, sizeof (struct _imapx_flag_change));
					user_set = on_user;
					user_flag = uflags;
					uflags = uflags->next;
				} else {
					if (off_user == NULL)
						off_user = g_array_new (FALSE, FALSE, sizeof (struct _imapx_flag_change));
					user_set = off_user;
					user_flag = suflags;
					suflags = suflags->next;
				}

				/* Could sort this and binary search */
				for (j = 0; j < user_set->len; j++) {
					change = &g_array_index (user_set, struct _imapx_flag_change, j);
					if (strcmp (change->name, user_flag->name) == 0)
						goto found;
				}
				add.name = g_strdup (user_flag->name);
				add.infos = g_ptr_array_new ();
				g_array_append_val (user_set, add);
				change = &add;
			found:
				camel_message_info_ref (info);
				g_ptr_array_add (change->infos, info);
			}
		}
		camel_message_info_free (info);
	}

	if ((on_orset | off_orset) == 0 && on_user == NULL && off_user == NULL) {
		success = TRUE;
		goto done;
	}

	/* TODO above code should go into changes_start */

	QUEUE_LOCK (is);

	if ((job = imapx_is_job_in_queue (is, folder, IMAPX_JOB_SYNC_CHANGES, NULL))) {
		if (pri > job->pri)
			job->pri = pri;

		QUEUE_UNLOCK (is);
		goto done;
	}

	job = imapx_job_new (cancellable);
	job->type = IMAPX_JOB_SYNC_CHANGES;
	job->start = imapx_job_sync_changes_start;
	job->pri = pri;
	job->folder = folder;
	job->u.sync_changes.changed_uids = uids;
	job->u.sync_changes.on_set = on_orset;
	job->u.sync_changes.off_set = off_orset;
	job->u.sync_changes.on_user = on_user;
	job->u.sync_changes.off_user = off_user;

	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	success = registered && imapx_run_job (is, job, error);

	imapx_job_unref (job);

done:
	imapx_sync_free_user (on_user);
	imapx_sync_free_user (off_user);

	camel_folder_free_uids (folder, uids);

	return success;
}

gboolean
camel_imapx_server_sync_changes (CamelIMAPXServer *is,
                                 CamelFolder *folder,
                                 GCancellable *cancellable,
                                 GError **error)
{
	return imapx_server_sync_changes (
		is, folder, IMAPX_PRIORITY_SYNC_CHANGES,
		cancellable, error);
}

/* expunge-uids? */
gboolean
camel_imapx_server_expunge (CamelIMAPXServer *is,
                            CamelFolder *folder,
                            GCancellable *cancellable,
                            GError **error)
{
	CamelIMAPXJob *job;
	gboolean registered;
	gboolean success;

	/* Do we really care to wait for this one to finish? */
	QUEUE_LOCK (is);

	if (imapx_is_job_in_queue (is, folder, IMAPX_JOB_EXPUNGE, NULL)) {
		QUEUE_UNLOCK (is);
		return TRUE;
	}

	job = imapx_job_new (cancellable);
	job->type = IMAPX_JOB_EXPUNGE;
	job->start = imapx_job_expunge_start;
	job->pri = IMAPX_PRIORITY_EXPUNGE;
	job->folder = folder;

	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	success = registered && imapx_run_job (is, job, error);

	imapx_job_unref (job);

	return success;
}

static guint
imapx_name_hash (gconstpointer key)
{
	if (g_ascii_strcasecmp(key, "INBOX") == 0)
		return g_str_hash("INBOX");
	else
		return g_str_hash (key);
}

static gint
imapx_name_equal (gconstpointer a,
                  gconstpointer b)
{
	gconstpointer aname = a, bname = b;

	if (g_ascii_strcasecmp(a, "INBOX") == 0)
		aname = "INBOX";
	if (g_ascii_strcasecmp(b, "INBOX") == 0)
		bname = "INBOX";
	return g_str_equal (aname, bname);
}

static void
imapx_list_flatten (gpointer k,
                    gpointer v,
                    gpointer d)
{
	GPtrArray *folders = d;

	g_ptr_array_add (folders, v);
}

static gint
imapx_list_cmp (gconstpointer ap,
                gconstpointer bp)
{
	struct _list_info *a = ((struct _list_info **) ap)[0];
	struct _list_info *b = ((struct _list_info **) bp)[0];

	return strcmp (a->name, b->name);
}

GPtrArray *
camel_imapx_server_list (CamelIMAPXServer *is,
                         const gchar *top,
                         guint32 flags,
                         const gchar *ext,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelIMAPXJob *job;
	GPtrArray *folders = NULL;
	gchar *encoded_name;

	encoded_name = camel_utf8_utf7 (top);

	job = imapx_job_new (cancellable);
	job->type = IMAPX_JOB_LIST;
	job->start = imapx_job_list_start;
	job->pri = IMAPX_PRIORITY_LIST;
	job->u.list.ext = ext;
	job->u.list.flags = flags;
	job->u.list.folders = g_hash_table_new (imapx_name_hash, imapx_name_equal);
	job->u.list.pattern = g_alloca (strlen (encoded_name) + 5);
	if (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE)
		sprintf(job->u.list.pattern, "%s*", encoded_name);
	else
		sprintf(job->u.list.pattern, "%s", encoded_name);

	/* sync operation which is triggered by user */
	if (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST)
		job->pri += 300;

	if (imapx_submit_job (is, job, error)) {
		folders = g_ptr_array_new ();
		g_hash_table_foreach (job->u.list.folders, imapx_list_flatten, folders);
		qsort (folders->pdata, folders->len, sizeof (folders->pdata[0]), imapx_list_cmp);
	}

	g_hash_table_destroy (job->u.list.folders);
	g_free (encoded_name);
	imapx_job_unref (job);

	return folders;
}

gboolean
camel_imapx_server_manage_subscription (CamelIMAPXServer *is,
                                        const gchar *folder_name,
                                        gboolean subscribe,
                                        GCancellable *cancellable,
                                        GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	job = imapx_job_new (cancellable);
	job->type = IMAPX_JOB_MANAGE_SUBSCRIPTION;
	job->start = imapx_job_manage_subscription_start;
	job->pri = IMAPX_PRIORITY_MANAGE_SUBSCRIPTION;
	job->u.manage_subscriptions.subscribe = subscribe;
	job->u.manage_subscriptions.folder_name = folder_name;

	success = imapx_submit_job (is, job, error);

	imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_create_folder (CamelIMAPXServer *is,
                                  const gchar *folder_name,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	job = imapx_job_new (cancellable);
	job->type = IMAPX_JOB_CREATE_FOLDER;
	job->start = imapx_job_create_folder_start;
	job->pri = IMAPX_PRIORITY_CREATE_FOLDER;
	job->u.folder_name = folder_name;

	success = imapx_submit_job (is, job, error);

	imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_delete_folder (CamelIMAPXServer *is,
                                  const gchar *folder_name,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	job = imapx_job_new (cancellable);
	job->type = IMAPX_JOB_DELETE_FOLDER;
	job->start = imapx_job_delete_folder_start;
	job->pri = IMAPX_PRIORITY_DELETE_FOLDER;
	job->u.folder_name = folder_name;

	success = imapx_submit_job (is, job, error);

	imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_rename_folder (CamelIMAPXServer *is,
                                  const gchar *old_name,
                                  const gchar *new_name,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	job = imapx_job_new (cancellable);
	job->type = IMAPX_JOB_RENAME_FOLDER;
	job->start = imapx_job_rename_folder_start;
	job->pri = IMAPX_PRIORITY_RENAME_FOLDER;
	job->u.rename_folder.ofolder_name = old_name;
	job->u.rename_folder.nfolder_name = new_name;

	success = imapx_submit_job (is, job, error);

	imapx_job_unref (job);

	return success;
}

IMAPXJobQueueInfo *
camel_imapx_server_get_job_queue_info (CamelIMAPXServer *is)
{
	IMAPXJobQueueInfo *jinfo = g_new0 (IMAPXJobQueueInfo, 1);
	CamelIMAPXJob *job = NULL;
	GList *head, *link;

	QUEUE_LOCK (is);

	jinfo->queue_len = g_queue_get_length (&is->jobs);
	jinfo->folders = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);

	head = g_queue_peek_head_link (&is->jobs);

	for (link = head; link != NULL; link = g_list_next (link)) {
		job = (CamelIMAPXJob *) link->data;

		if (job->folder) {
			const gchar *full_name = camel_folder_get_full_name (job->folder);
			g_hash_table_insert (jinfo->folders, g_strdup (full_name), GINT_TO_POINTER (1));
		}
	}

	if (is->select_folder)
		g_hash_table_insert (jinfo->folders, g_strdup (camel_folder_get_full_name (is->select_folder)), GINT_TO_POINTER (1));

	QUEUE_UNLOCK (is);

	return jinfo;
}

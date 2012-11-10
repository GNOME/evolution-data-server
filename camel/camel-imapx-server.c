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

#include "camel-imapx-server.h"

#include "camel-imapx-command.h"
#include "camel-imapx-folder.h"
#include "camel-imapx-job.h"
#include "camel-imapx-settings.h"
#include "camel-imapx-store.h"
#include "camel-imapx-stream.h"
#include "camel-imapx-summary.h"
#include "camel-imapx-utils.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define CAMEL_IMAPX_SERVER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_IMAPX_SERVER, CamelIMAPXServerPrivate))

#define c(...) camel_imapx_debug(command, __VA_ARGS__)
#define e(...) camel_imapx_debug(extra, __VA_ARGS__)

#define CIF(x) ((CamelIMAPXFolder *)x)

#define QUEUE_LOCK(x) (g_static_rec_mutex_lock(&(x)->queue_lock))
#define QUEUE_UNLOCK(x) (g_static_rec_mutex_unlock(&(x)->queue_lock))

#define IDLE_LOCK(x) (g_mutex_lock((x)->idle_lock))
#define IDLE_UNLOCK(x) (g_mutex_unlock((x)->idle_lock))

/* Try pipelining fetch requests, 'in bits' */
#define MULTI_SIZE (20480)

/* How many outstanding commands do we allow before we just queue them? */
#define MAX_COMMANDS (10)

#define MAX_COMMAND_LEN 1000

extern gint camel_application_is_exiting;

/* Job-specific structs */
typedef struct _GetMessageData GetMessageData;
typedef struct _RefreshInfoData RefreshInfoData;
typedef struct _SyncChangesData SyncChangesData;
typedef struct _AppendMessageData AppendMessageData;
typedef struct _CopyMessagesData CopyMessagesData;
typedef struct _ListData ListData;
typedef struct _ManageSubscriptionsData ManageSubscriptionsData;
typedef struct _RenameFolderData RenameFolderData;
typedef struct _CreateFolderData CreateFolderData;
typedef struct _DeleteFolderData DeleteFolderData;

struct _GetMessageData {
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
};

struct _RefreshInfoData {
	/* array of refresh info's */
	GArray *infos;
	/* used for building uidset stuff */
	gint index;
	gint last_index;
	gint fetch_msg_limit;
	CamelFetchType fetch_type;
	gboolean update_unseen;
	gboolean scan_changes;
	struct _uidset_state uidset;
	/* changes during refresh */
	CamelFolderChangeInfo *changes;
};

struct _SyncChangesData {
	CamelFolder *folder;
	GPtrArray *changed_uids;
	guint32 on_set;
	guint32 off_set;
	GArray *on_user; /* imapx_flag_change */
	GArray *off_user;
	gint unread_change;
};

struct _AppendMessageData {
	gchar *path;
	CamelMessageInfo *info;
	gchar *appended_uid;
};

struct _CopyMessagesData {
	CamelFolder *dest;
	GPtrArray *uids;
	gboolean delete_originals;
	gint index;
	gint last_index;
	struct _uidset_state uidset;
};

struct _ListData {
	gchar *pattern;
	guint32 flags;
	gchar *ext;
	GHashTable *folders;
};

struct _ManageSubscriptionsData {
	gchar *folder_name;
	gboolean subscribe;
};

struct _RenameFolderData {
	gchar *old_folder_name;
	gchar *new_folder_name;
};

struct _CreateFolderData {
	gchar *folder_name;
};

struct _DeleteFolderData {
	gchar *folder_name;
};

/* untagged response handling */

/* May need to turn this into separate,
 * subclassable GObject with proper getter/setter
 * functions so derived implementations can
 * supply their own context information.
 * The context supplied here, however, should
 * not be exposed outside CamelIMAPXServer.
 * An instance is created in imapx_untagged()
 * with a lifetime of one run of this function.
 * In order to supply a derived context instance,
 * we would need to register a derived _new()
 * function for it which will be called inside
 * imapx_untagged().
 *
 * TODO: rethink this construct.
 */
typedef struct _CamelIMAPXServerUntaggedContext CamelIMAPXServerUntaggedContext;

struct _CamelIMAPXServerUntaggedContext {
	CamelSortType fetch_order;
	guint id;
	guint len;
	guchar *token;
	gint tok;
	gboolean lsub;
	struct _status_info *sinfo;
};

/* internal untagged handler prototypes */
static gboolean	imapx_untagged_bye		(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_capability	(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_exists		(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_expunge		(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_fetch		(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_flags		(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_list		(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_lsub		(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_namespace	(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_ok_no_bad	(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_preauth		(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_recent		(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_status		(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_vanished		(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);

enum {
	IMAPX_UNTAGGED_ID_BAD = 0,
	IMAPX_UNTAGGED_ID_BYE,
	IMAPX_UNTAGGED_ID_CAPABILITY,
	IMAPX_UNTAGGED_ID_EXISTS,
	IMAPX_UNTAGGED_ID_EXPUNGE,
	IMAPX_UNTAGGED_ID_FETCH,
	IMAPX_UNTAGGED_ID_FLAGS,
	IMAPX_UNTAGGED_ID_LIST,
	IMAPX_UNTAGGED_ID_LSUB,
	IMAPX_UNTAGGED_ID_NAMESPACE,
	IMAPX_UNTAGGED_ID_NO,
	IMAPX_UNTAGGED_ID_OK,
	IMAPX_UNTAGGED_ID_PREAUTH,
	IMAPX_UNTAGGED_ID_RECENT,
	IMAPX_UNTAGGED_ID_STATUS,
	IMAPX_UNTAGGED_ID_VANISHED,
	IMAPX_UNTAGGED_LAST_ID
};

static const CamelIMAPXUntaggedRespHandlerDesc _untagged_descr[] = {
	{CAMEL_IMAPX_UNTAGGED_BAD, imapx_untagged_ok_no_bad, NULL, FALSE},
	{CAMEL_IMAPX_UNTAGGED_BYE, imapx_untagged_bye, NULL, FALSE},
	{CAMEL_IMAPX_UNTAGGED_CAPABILITY, imapx_untagged_capability, NULL, FALSE},
	{CAMEL_IMAPX_UNTAGGED_EXISTS, imapx_untagged_exists, NULL, TRUE},
	{CAMEL_IMAPX_UNTAGGED_EXPUNGE, imapx_untagged_expunge, NULL, TRUE},
	{CAMEL_IMAPX_UNTAGGED_FETCH, imapx_untagged_fetch, NULL, TRUE},
	{CAMEL_IMAPX_UNTAGGED_FLAGS, imapx_untagged_flags, NULL, TRUE},
	{CAMEL_IMAPX_UNTAGGED_LIST, imapx_untagged_list, NULL, TRUE},
	{CAMEL_IMAPX_UNTAGGED_LSUB, imapx_untagged_lsub, CAMEL_IMAPX_UNTAGGED_LIST, TRUE /*overridden */ },
	{CAMEL_IMAPX_UNTAGGED_NAMESPACE, imapx_untagged_namespace, NULL, FALSE},
	{CAMEL_IMAPX_UNTAGGED_NO, imapx_untagged_ok_no_bad, NULL, FALSE},
	{CAMEL_IMAPX_UNTAGGED_OK, imapx_untagged_ok_no_bad, NULL, FALSE},
	{CAMEL_IMAPX_UNTAGGED_PREAUTH, imapx_untagged_preauth, CAMEL_IMAPX_UNTAGGED_OK, TRUE /*overridden */ },
	{CAMEL_IMAPX_UNTAGGED_RECENT, imapx_untagged_recent, NULL, TRUE},
	{CAMEL_IMAPX_UNTAGGED_STATUS, imapx_untagged_status, NULL, TRUE},
	{CAMEL_IMAPX_UNTAGGED_VANISHED, imapx_untagged_vanished, NULL, TRUE},
};

struct _CamelIMAPXServerPrivate {
	GWeakRef store;

	CamelIMAPXServerUntaggedContext *context;
	GHashTable *untagged_handlers;

	CamelIMAPXStream *stream;
	GMutex stream_lock;
};

enum {
	PROP_0,
	PROP_STREAM,
	PROP_STORE
};

enum {
	SELECT_CHANGED,
	SHUTDOWN,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void	imapx_uidset_init		(struct _uidset_state *ss,
						 gint total,
						 gint limit);
static gint	imapx_uidset_done		(struct _uidset_state *ss,
						 CamelIMAPXCommand *ic);
static gint	imapx_uidset_add		(struct _uidset_state *ss,
						 CamelIMAPXCommand *ic,
						 const gchar *uid);

static gboolean	imapx_command_idle_stop		(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_continuation		(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 gboolean litplus,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_disconnect		(CamelIMAPXServer *is);
static gboolean	imapx_is_command_queue_empty	(CamelIMAPXServer *is);
static gint	imapx_uid_cmp			(gconstpointer ap,
						 gconstpointer bp,
						 gpointer data);

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
	IMAPX_JOB_FETCH_MESSAGES = 1 << 14,
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

static CamelIMAPXJob *
		imapx_match_active_job		(CamelIMAPXServer *is,
						 guint32 type,
						 const gchar *uid);
static void	imapx_job_fetch_new_messages_start
						(CamelIMAPXJob *job,
						 CamelIMAPXServer *is);
static gint	imapx_refresh_info_uid_cmp	(gconstpointer ap,
						 gconstpointer bp,
						 gboolean ascending);
static gint	imapx_uids_array_cmp		(gconstpointer ap,
						 gconstpointer bp);
static gboolean	imapx_server_sync_changes	(CamelIMAPXServer *is,
						 CamelFolder *folder,
						 gint pri,
						 GCancellable *cancellable,
						 GError **error);
static void	imapx_sync_free_user		(GArray *user_set);

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

typedef enum {
	IMAPX_IDLE_STOP_NOOP,
	IMAPX_IDLE_STOP_SUCCESS,
	IMAPX_IDLE_STOP_ERROR
} CamelIMAPXIdleStopResult;

static gboolean	imapx_in_idle			(CamelIMAPXServer *is);
static gboolean	imapx_idle_supported		(CamelIMAPXServer *is);
static void	imapx_start_idle		(CamelIMAPXServer *is);
static void	imapx_exit_idle			(CamelIMAPXServer *is);
static void	imapx_init_idle			(CamelIMAPXServer *is);
static CamelIMAPXIdleStopResult
		imapx_stop_idle			(CamelIMAPXServer *is,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	camel_imapx_server_idle		(CamelIMAPXServer *is,
						 CamelFolder *folder,
						 GCancellable *cancellable,
						 GError **error);

enum {
	USE_SSL_NEVER,
	USE_SSL_ALWAYS,
	USE_SSL_WHEN_POSSIBLE
};

#define SSL_PORT_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 | CAMEL_TCP_STREAM_SSL_ENABLE_SSL3)
#define STARTTLS_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_TLS)

static gboolean	imapx_select			(CamelIMAPXServer *is,
						 CamelFolder *folder,
						 gboolean force,
						 GCancellable *cancellable,
						 GError **error);

G_DEFINE_TYPE (CamelIMAPXServer, camel_imapx_server, CAMEL_TYPE_OBJECT)

static const CamelIMAPXUntaggedRespHandlerDesc *
replace_untagged_descriptor (GHashTable *untagged_handlers,
                             const gchar *key,
                             const CamelIMAPXUntaggedRespHandlerDesc *descr)
{
	const CamelIMAPXUntaggedRespHandlerDesc *prev = NULL;

	g_return_val_if_fail (untagged_handlers != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);
	/* descr may be NULL (to delete a handler) */

	prev = g_hash_table_lookup (untagged_handlers, key);
	g_hash_table_replace (
		untagged_handlers,
		g_strdup (key),
		(gpointer) descr);
	return prev;
}

static void
add_initial_untagged_descriptor (GHashTable *untagged_handlers,
                                 guint untagged_id)
{
	const CamelIMAPXUntaggedRespHandlerDesc *prev = NULL;
	const CamelIMAPXUntaggedRespHandlerDesc *cur  = NULL;

	g_return_if_fail (untagged_handlers != NULL);
	g_return_if_fail (untagged_id < IMAPX_UNTAGGED_LAST_ID);

	cur =  &(_untagged_descr[untagged_id]);
	prev = replace_untagged_descriptor (
		untagged_handlers,
		cur->untagged_response,
		cur);
	/* there must not be any previous handler here */
	g_return_if_fail (prev == NULL);
}

static GHashTable *
create_initial_untagged_handler_table (void)
{
	GHashTable *uh = g_hash_table_new_full (
		g_str_hash,
		g_str_equal,
		g_free,
		NULL);
	guint32 ii = 0;

	/* CamelIMAPXServer predefined handlers*/
	for (ii = 0; ii < IMAPX_UNTAGGED_LAST_ID; ii++)
		add_initial_untagged_descriptor (uh, ii);

	g_return_val_if_fail (g_hash_table_size (uh) == IMAPX_UNTAGGED_LAST_ID, NULL);

	return uh;
}

static void
get_message_data_free (GetMessageData *data)
{
	g_free (data->uid);

	if (data->stream != NULL)
		g_object_unref (data->stream);

	g_slice_free (GetMessageData, data);
}

static void
refresh_info_data_infos_free (RefreshInfoData *data)
{
	gint ii;

	if (!data || !data->infos)
		return;

	for (ii = 0; ii < data->infos->len; ii++) {
		struct _refresh_info *r = &g_array_index (data->infos, struct _refresh_info, ii);

		camel_flag_list_free (&r->server_user_flags);
		g_free (r->uid);
	}

	g_array_free (data->infos, TRUE);
	data->infos = NULL;
}

static void
refresh_info_data_free (RefreshInfoData *data)
{
	camel_folder_change_info_free (data->changes);
	refresh_info_data_infos_free (data);

	g_slice_free (RefreshInfoData, data);
}

static void
sync_changes_data_free (SyncChangesData *data)
{
	if (data->folder != NULL) {
		camel_folder_free_uids (data->folder, data->changed_uids);
		g_object_unref (data->folder);
	}

	imapx_sync_free_user (data->on_user);
	imapx_sync_free_user (data->off_user);

	g_slice_free (SyncChangesData, data);
}

static void
append_message_data_free (AppendMessageData *data)
{
	g_free (data->path);
	g_free (data->appended_uid);

	camel_message_info_free (data->info);

	g_slice_free (AppendMessageData, data);
}

static void
copy_messages_data_free (CopyMessagesData *data)
{
	if (data->dest != NULL)
		g_object_unref (data->dest);

	if (data->uids != NULL) {
		g_ptr_array_foreach (data->uids, (GFunc) g_free, NULL);
		g_ptr_array_free (data->uids, TRUE);
	}

	g_slice_free (CopyMessagesData, data);
}

static void
list_data_free (ListData *data)
{
	g_free (data->pattern);
	g_free (data->ext);

	g_hash_table_destroy (data->folders);

	g_slice_free (ListData, data);
}

static void
manage_subscriptions_data_free (ManageSubscriptionsData *data)
{
	g_free (data->folder_name);

	g_slice_free (ManageSubscriptionsData, data);
}

static void
rename_folder_data_free (RenameFolderData *data)
{
	g_free (data->old_folder_name);
	g_free (data->new_folder_name);

	g_slice_free (RenameFolderData, data);
}

static void
create_folder_data_free (CreateFolderData *data)
{
	g_free (data->folder_name);

	g_slice_free (CreateFolderData, data);
}

static void
delete_folder_data_free (DeleteFolderData *data)
{
	g_free (data->folder_name);

	g_slice_free (DeleteFolderData, data);
}

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

gboolean
imapx_uidset_done (struct _uidset_state *ss,
                   CamelIMAPXCommand *ic)
{
	gint ret = FALSE;

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

	e (ic->is->tagprefix, "uidset add '%s'\n", uid);

	if (ss->last == 0) {
		e (ic->is->tagprefix, " start\n");
		camel_imapx_command_add (ic, "%d", uidn);
		ss->entries++;
		ss->start = uidn;
	} else {
		if (ss->last != uidn - 1) {
			if (ss->last == ss->start) {
				e (ic->is->tagprefix, " ,next\n");
				camel_imapx_command_add (ic, ",%d", uidn);
				ss->entries++;
			} else {
				e (ic->is->tagprefix, " :range\n");
				camel_imapx_command_add (ic, ":%d,%d", ss->last, uidn);
				ss->entries+=2;
			}
			ss->start = uidn;
		}
	}

	ss->last = uidn;

	if ((ss->limit && ss->entries >= ss->limit)
	    || (ss->total && ss->uids >= ss->total)) {
		e (ic->is->tagprefix, " done, %d entries, %d uids\n", ss->entries, ss->uids);
		if (!imapx_uidset_done (ss, ic))
			return -1;
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
	CamelIMAPXStream *stream = NULL;
	CamelIMAPXCommandPart *cp;
	gboolean cp_continuation;
	gboolean cp_literal_plus;
	GList *head;
	gboolean success = FALSE;
	gchar *string;
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

	camel_imapx_command_queue_push_tail (is->active, ic);

	stream = camel_imapx_server_ref_stream (is);

	if (stream == NULL) {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"Cannot issue command, no stream available");
		goto err;
	}

	c (is->tagprefix, "Starting command (active=%d,%s) %c%05u %s\r\n", camel_imapx_command_queue_get_length (is->active), is->literal?" literal":"", is->tagprefix, ic->tag, cp->data && g_str_has_prefix (cp->data, "LOGIN") ? "LOGIN..." : cp->data);

	string = g_strdup_printf (
		"%c%05u %s\r\n", is->tagprefix, ic->tag, cp->data);
	retval = camel_stream_write_string (
		CAMEL_STREAM (stream), string, cancellable, error);
	g_free (string);

	if (retval == -1)
		goto err;

	while (is->literal == ic && cp_literal_plus) {
		/* Sent LITERAL+ continuation immediately */
		if (!imapx_continuation (is, stream, TRUE, cancellable, error))
			goto err;
	}

	success = TRUE;

	goto exit;

err:
	camel_imapx_command_queue_remove (is->active, ic);

	/* HACK: Since we're failing, make sure the command has a status
	 *       structure and the result code indicates failure, so the
	 *       ic->complete() callback does not start a new command. */
	if (ic->status == NULL)
		ic->status = g_malloc0 (sizeof (struct _status_info));
	if (ic->status->result == IMAPX_OK)
		ic->status->result = IMAPX_UNKNOWN;

	/* Send a NULL GError since we've already set a
	 * GError to get here, and we're not interested
	 * in individual command errors. */
	if (ic != NULL && ic->complete != NULL)
		ic->complete (is, ic, NULL);

exit:
	if (stream != NULL)
		g_object_unref (stream);

	return success;
}

static gboolean
duplicate_fetch_or_refresh (CamelIMAPXServer *is,
                            CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;

	job = camel_imapx_command_get_job (ic);

	if (job == NULL)
		return FALSE;

	if (!(job->type & (IMAPX_JOB_FETCH_NEW_MESSAGES | IMAPX_JOB_REFRESH_INFO | IMAPX_JOB_FETCH_MESSAGES)))
		return FALSE;

	if (imapx_match_active_job (is, IMAPX_JOB_FETCH_NEW_MESSAGES | IMAPX_JOB_REFRESH_INFO | IMAPX_JOB_FETCH_MESSAGES, NULL)) {
		c (is->tagprefix, "Not yet sending duplicate fetch/refresh %s command\n", ic->name);
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

static gboolean
imapx_command_start_next (CamelIMAPXServer *is,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelIMAPXCommand *first_ic;
	gint min_pri = -128;
	gboolean success = TRUE;

	c (is->tagprefix, "** Starting next command\n");
	if (is->literal) {
		c (is->tagprefix, "* no; waiting for literal '%s'\n", is->literal->name);
		return success;
	}

	if (is->select_pending) {
		GQueue start = G_QUEUE_INIT;
		GList *head, *link;

		c (is->tagprefix, "-- Checking job queue for non-folder jobs\n");

		head = camel_imapx_command_queue_peek_head_link (is->queue);

		/* Tag which commands in the queue to start. */
		for (link = head; link != NULL; link = g_list_next (link)) {
			CamelIMAPXCommand *ic = link->data;

			if (ic->pri < min_pri)
				break;

			c (is->tagprefix, "-- %3d '%s'?\n", (gint) ic->pri, ic->name);
			if (!ic->select) {
				c (is->tagprefix, "--> starting '%s'\n", ic->name);
				min_pri = ic->pri;
				g_queue_push_tail (&start, link);
			}

			if (g_queue_get_length (&start) == MAX_COMMANDS)
				break;
		}

		if (g_queue_is_empty (&start))
			c (is->tagprefix, "* no, waiting for pending select '%s'\n", camel_folder_get_full_name (is->select_pending));

		/* Start the tagged commands.
		 *
		 * Each command must be removed from 'is->queue' before
		 * starting it, so we temporarily reference the command
		 * to avoid accidentally finalizing it. */
		while ((link = g_queue_pop_head (&start)) != NULL) {
			CamelIMAPXCommand *ic;

			ic = camel_imapx_command_ref (link->data);
			camel_imapx_command_queue_delete_link (is->queue, link);

			success = imapx_command_start (
				is, ic, cancellable, error);

			camel_imapx_command_unref (ic);

			if (!success) {
				g_queue_clear (&start);
				return FALSE;
			}
		}

		return TRUE;
	}

	if (imapx_idle_supported (is) && is->state == IMAPX_SELECTED) {
		gboolean empty = imapx_is_command_queue_empty (is);

		if (imapx_in_idle (is) && !camel_imapx_command_queue_is_empty (is->queue)) {
			CamelIMAPXIdleStopResult stop_result;
			CamelIMAPXStream *stream;

			stop_result = IMAPX_IDLE_STOP_NOOP;
			stream = camel_imapx_server_ref_stream (is);

			if (stream != NULL) {
				stop_result = imapx_stop_idle (
					is, stream, cancellable, error);
				g_object_unref (stream);
			}

			switch (stop_result) {
				/* Proceed with the next queued command. */
				case IMAPX_IDLE_STOP_NOOP:
					break;

				case IMAPX_IDLE_STOP_SUCCESS:
					c (
						is->tagprefix,
						"waiting for idle to stop \n");
					/* if there are more pending commands,
					 * then they should be processed too */
					break;

				case IMAPX_IDLE_STOP_ERROR:
					return FALSE;
			}

		} else if (empty && !imapx_in_idle (is)) {
			imapx_start_idle (is);
			c (is->tagprefix, "starting idle \n");
			return TRUE;
		}
	}

	if (camel_imapx_command_queue_is_empty (is->queue)) {
		c (is->tagprefix, "* no, no jobs\n");
		return TRUE;
	}

	/* See if any queued jobs on this select first */
	if (is->select_folder) {
		GQueue start = G_QUEUE_INIT;
		GList *head, *link;
		gboolean commands_started = FALSE;

		c (
			is->tagprefix, "- we're selected on '%s', current jobs?\n",
			camel_folder_get_full_name (is->select_folder));

		head = camel_imapx_command_queue_peek_head_link (is->active);

		/* Find the highest priority in the active queue. */
		for (link = head; link != NULL; link = g_list_next (link)) {
			CamelIMAPXCommand *ic = link->data;

			min_pri = MAX (min_pri, ic->pri);
			c (is->tagprefix, "-  %3d '%s'\n", (gint) ic->pri, ic->name);
		}

		if (camel_imapx_command_queue_get_length (is->active) >= MAX_COMMANDS) {
			c (is->tagprefix, "** too many jobs busy, waiting for results for now\n");
			return TRUE;
		}

		c (is->tagprefix, "-- Checking job queue\n");

		head = camel_imapx_command_queue_peek_head_link (is->queue);

		/* Tag which commands in the queue to start. */
		for (link = head; link != NULL; link = g_list_next (link)) {
			CamelIMAPXCommand *ic = link->data;

			if (is->literal != NULL)
				break;

			if (ic->pri < min_pri)
				break;

			c (is->tagprefix, "-- %3d '%s'?\n", (gint) ic->pri, ic->name);
			if (!ic->select || ((ic->select == is->select_folder) &&
					    !duplicate_fetch_or_refresh (is, ic))) {
				c (is->tagprefix, "--> starting '%s'\n", ic->name);
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

		/* Start the tagged commands.
		 *
		 * Each command must be removed from 'is->queue' before
		 * starting it, so we temporarily reference the command
		 * to avoid accidentally finalizing it. */
		while ((link = g_queue_pop_head (&start)) != NULL) {
			CamelIMAPXCommand *ic;
			gboolean success;

			ic = camel_imapx_command_ref (link->data);
			camel_imapx_command_queue_delete_link (is->queue, link);

			success = imapx_command_start (
				is, ic, cancellable, error);

			camel_imapx_command_unref (ic);

			if (!success) {
				g_queue_clear (&start);
				return FALSE;
			}

			commands_started = TRUE;
		}

		if (commands_started)
			return TRUE;
	}

	/* This won't be NULL because we checked for an empty queue above. */
	first_ic = camel_imapx_command_queue_peek_head (is->queue);

	/* If we need to select a folder for the first command, do it now,
	 * once it is complete it will re-call us if it succeeded. */
	if (first_ic->select) {
		c (
			is->tagprefix, "Selecting folder '%s' for command '%s'(%p)\n",
			camel_folder_get_full_name (first_ic->select),
			first_ic->name, first_ic);
		imapx_select (is, first_ic->select, FALSE, cancellable, error);
	} else {
		GQueue start = G_QUEUE_INIT;
		GList *head, *link;

		min_pri = first_ic->pri;

		head = camel_imapx_command_queue_peek_head_link (is->queue);

		/* Tag which commands in the queue to start. */
		for (link = head; link != NULL; link = g_list_next (link)) {
			CamelIMAPXCommand *ic = link->data;

			if (is->literal != NULL)
				break;

			if (ic->pri < min_pri)
				break;

			if (!ic->select || (ic->select == is->select_folder &&
					    !duplicate_fetch_or_refresh (is, ic))) {
				c (is->tagprefix, "* queueing job %3d '%s'\n", (gint) ic->pri, ic->name);
				min_pri = ic->pri;
				g_queue_push_tail (&start, link);
			}

			if (g_queue_get_length (&start) == MAX_COMMANDS)
				break;
		}

		/* Start the tagged commands.
		 *
		 * Each command must be removed from 'is->queue' before
		 * starting it, so we temporarily reference the command
		 * to avoid accidentally finalizing it. */
		while ((link = g_queue_pop_head (&start)) != NULL) {
			CamelIMAPXCommand *ic;
			gboolean success;

			ic = camel_imapx_command_ref (link->data);
			camel_imapx_command_queue_delete_link (is->queue, link);

			success = imapx_command_start (
				is, ic, cancellable, error);

			camel_imapx_command_unref (ic);

			if (!success) {
				g_queue_clear (&start);
				return FALSE;
			}
		}
	}

	return TRUE;
}

static gboolean
imapx_is_command_queue_empty (CamelIMAPXServer *is)
{
	if (!camel_imapx_command_queue_is_empty (is->queue))
		return FALSE;

	if (!camel_imapx_command_queue_is_empty (is->active))
		return FALSE;

	return TRUE;
}

static void
imapx_command_queue (CamelIMAPXServer *is,
                     CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;

	/* We enqueue in priority order, new messages have
	 * higher priority than older messages with the same priority */

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	camel_imapx_command_close (ic);

	c (is->tagprefix, "enqueue job '%.*s'\n", ((CamelIMAPXCommandPart *) ic->parts.head->data)->data_size, ((CamelIMAPXCommandPart *) ic->parts.head->data)->data);

	QUEUE_LOCK (is);

	if (is->state == IMAPX_SHUTDOWN) {
		c (is->tagprefix, "refuse to queue job on disconnected server\n");
		if (job->error == NULL)
			g_set_error (
				&job->error, CAMEL_IMAPX_ERROR, 1,
				"%s", _("Server disconnected"));
		QUEUE_UNLOCK (is);

		/* Send a NULL GError since we've already set
		 * the job's GError, and we're not interested
		 * in individual command errors. */
		if (ic->complete != NULL)
			ic->complete (is, ic, NULL);
		return;
	}

	camel_imapx_command_queue_insert_sorted (is->queue, ic);

	/* XXX No error checking.  We don't care if this fails? */
	imapx_command_start_next (is, job->cancellable, NULL);

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

	head = camel_imapx_command_queue_peek_head_link (is->active);

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

/* Must not have QUEUE lock */
static CamelIMAPXJob *
imapx_match_active_job (CamelIMAPXServer *is,
                        guint32 type,
                        const gchar *uid)
{
	CamelIMAPXJob *match = NULL;
	GList *head, *link;

	QUEUE_LOCK (is);

	head = camel_imapx_command_queue_peek_head_link (is->active);

	for (link = head; link != NULL; link = g_list_next (link)) {
		CamelIMAPXCommand *ic = link->data;
		CamelIMAPXJob *job;

		job = camel_imapx_command_get_job (ic);

		if (job == NULL)
			continue;

		if (!(job->type & type))
			continue;

		if (camel_imapx_job_matches (job, is->select_folder, uid)) {
			match = job;
			break;
		}
	}

	QUEUE_UNLOCK (is);

	return match;
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

		if (camel_imapx_job_matches (job, folder, uid)) {
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
	CamelMessageInfo *mi;

	if (unsolicited && ifolder->exists_on_server)
		ifolder->exists_on_server--;

	if (is->changes == NULL)
		is->changes = camel_folder_change_info_new ();

	mi = camel_folder_summary_peek_loaded (is->select_folder->summary, uid);
	if (mi) {
		camel_folder_summary_remove (is->select_folder->summary, mi);
		camel_message_info_free (mi);
	} else {
		camel_folder_summary_remove_uid (is->select_folder->summary, uid);
	}

	is->expunged = g_list_prepend (is->expunged, uid);

	camel_folder_change_info_remove_uid (is->changes, uid);

	if (imapx_idle_supported (is) && imapx_in_idle (is)) {
		camel_folder_summary_save_to_db (is->select_folder->summary, NULL);
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
	gchar *uid = NULL;

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

/* untagged response handler functions */

static gboolean
imapx_untagged_capability (CamelIMAPXServer *is,
                           CamelIMAPXStream *stream,
                           GCancellable *cancellable,
                           GError **error)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	/* cancellable may be NULL */
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (is->cinfo)
		imapx_free_capability (is->cinfo);
	is->cinfo = imapx_parse_capability (stream, cancellable, error);
	if (is->cinfo == NULL)
		return FALSE;
	c (is->tagprefix, "got capability flags %08x\n", is->cinfo->capa);
	return TRUE;
}

static gboolean
imapx_untagged_expunge (CamelIMAPXServer *is,
                        CamelIMAPXStream *stream,
                        GCancellable *cancellable,
                        GError **error)
{
	guint32 expunge = 0;
	CamelIMAPXJob *job = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	/* cancellable may be NULL */
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	expunge = is->priv->context->id;
	job = imapx_match_active_job (is, IMAPX_JOB_EXPUNGE, NULL);

	/* If there is a job running, let it handle the deletion */
	if (job)
		return TRUE;

	c (is->tagprefix, "expunged: %d\n", is->priv->context->id);
	if (is->select_folder) {
		gchar *uid = NULL;

		uid = imapx_get_uid_from_index (is->select_folder->summary, expunge - 1);
		if (!uid)
			return TRUE;

		imapx_expunge_uid_from_summary (is, uid, TRUE);
	}

	return TRUE;
}

static gboolean
imapx_untagged_vanished (CamelIMAPXServer *is,
                         CamelIMAPXStream *stream,
                         GCancellable *cancellable,
                         GError **error)
{
	GPtrArray *uids = NULL;
	GList *uid_list = NULL;
	gboolean unsolicited = TRUE;
	gint i = 0;
	guint len = 0;
	guchar *token = NULL;
	gint tok = 0;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	/* cancellable may be NULL */
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	tok = camel_imapx_stream_token (stream, &token, &len, cancellable, error);
	if (tok < 0)
		return FALSE;
	if (tok == '(') {
		unsolicited = FALSE;
		while (tok != ')') {
			/* We expect this to be 'EARLIER' */
			tok = camel_imapx_stream_token (stream, &token, &len, cancellable, error);
			if (tok < 0)
				return FALSE;
		}
	} else
		camel_imapx_stream_ungettoken (stream, tok, token, len);

	uids = imapx_parse_uids (stream, cancellable, error);
	if (uids == NULL)
		return FALSE;

	if (unsolicited) {
		CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) is->select_folder;

		if (ifolder->exists_on_server < uids->len) {
			c (
				is->tagprefix, "Error: exists_on_folder %d is fewer than vanished %d\n",
				ifolder->exists_on_server, uids->len);
			ifolder->exists_on_server = 0;
		} else
			ifolder->exists_on_server -= uids->len;
	}
	if (is->changes == NULL)
		is->changes = camel_folder_change_info_new ();

	for (i = 0; i < uids->len; i++) {
		gchar *uid = g_strdup_printf ("%u", GPOINTER_TO_UINT (g_ptr_array_index (uids, i)));

		c (is->tagprefix, "vanished: %s\n", uid);

		uid_list = g_list_prepend (uid_list, uid);
		camel_folder_change_info_remove_uid (is->changes, uid);
	}
	uid_list = g_list_reverse (uid_list);
	camel_folder_summary_remove_uids (is->select_folder->summary, uid_list);
	is->expunged = g_list_concat (is->expunged, uid_list);
	g_ptr_array_free (uids, FALSE);

	return TRUE;
}

static gboolean
imapx_untagged_namespace (CamelIMAPXServer *is,
                          CamelIMAPXStream *stream,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelIMAPXNamespaceList *nsl = NULL;
	CamelIMAPXStoreNamespace *ns = NULL;
	CamelIMAPXStore *store;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	/* cancellable may be NULL */
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	nsl = imapx_parse_namespace_list (stream, cancellable, error);
	if (nsl == NULL)
		return FALSE;

	store = camel_imapx_server_ref_store (is);

	if (store->summary->namespaces)
		camel_imapx_namespace_list_clear (store->summary->namespaces);
	store->summary->namespaces = nsl;
	camel_store_summary_touch (CAMEL_STORE_SUMMARY (store->summary));

	/* TODO Need to remove store->dir_sep to support multiple namespaces */
	ns = nsl->personal;
	if (ns) {
		store->dir_sep = ns->sep;
		if (!store->dir_sep)
			store->dir_sep = '/';
	}

	g_object_unref (store);

	return TRUE;
}

static gboolean
imapx_untagged_exists (CamelIMAPXServer *is,
                       CamelIMAPXStream *stream,
                       GCancellable *cancellable,
                       GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	c (is->tagprefix, "exists: %d\n", is->priv->context->id);
	is->exists = is->priv->context->id;

	if (is->select_folder)
		((CamelIMAPXFolder *) is->select_folder)->exists_on_server = is->priv->context->id;

	if (imapx_idle_supported (is) && imapx_in_idle (is)) {
		if (camel_folder_summary_count (is->select_folder->summary) < is->priv->context->id) {
			CamelIMAPXIdleStopResult stop_result;

			stop_result = imapx_stop_idle (
				is, stream, cancellable, error);
			success = (stop_result != IMAPX_IDLE_STOP_ERROR);
		}
	}

	return success;
}

static gboolean
imapx_untagged_flags (CamelIMAPXServer *is,
                      CamelIMAPXStream *stream,
                      GCancellable *cancellable,
                      GError **error)
{
	guint32 flags;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	/* cancellable may be NULL */
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	imapx_parse_flags (stream, &flags, NULL, cancellable, error);
	c (is->tagprefix, "flags: %08x\n", flags);

	return TRUE;
}

static gboolean
imapx_untagged_fetch (CamelIMAPXServer *is,
                      CamelIMAPXStream *stream,
                      GCancellable *cancellable,
                      GError **error)
{
	struct _fetch_info *finfo;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	/* cancellable may be NULL */
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	finfo = imapx_parse_fetch (stream, cancellable, error);
	if (finfo == NULL) {
		imapx_free_fetch (finfo);
		return FALSE;
	}

	if ((finfo->got & (FETCH_BODY | FETCH_UID)) == (FETCH_BODY | FETCH_UID)) {
		CamelIMAPXJob *job = imapx_match_active_job (is, IMAPX_JOB_GET_MESSAGE, finfo->uid);
		GetMessageData *data;

		data = camel_imapx_job_get_data (job);
		g_return_val_if_fail (data != NULL, FALSE);

		/* This must've been a get-message request, fill out the body stream,
		 * in the right spot */

		if (job && job->error == NULL) {
			if (data->use_multi_fetch) {
				data->body_offset = finfo->offset;
				g_seekable_seek (G_SEEKABLE (data->stream), finfo->offset, G_SEEK_SET, NULL, NULL);
			}

			data->body_len = camel_stream_write_to_stream (finfo->body, data->stream, job->cancellable, &job->error);
			if (data->body_len == -1)
				g_prefix_error (
					&job->error,
					_("Error writing to cache stream: "));
		}
	}

	if ((finfo->got & FETCH_FLAGS) && !(finfo->got & FETCH_HEADER)) {
		CamelIMAPXJob *job = imapx_match_active_job (is, IMAPX_JOB_FETCH_NEW_MESSAGES | IMAPX_JOB_REFRESH_INFO | IMAPX_JOB_FETCH_MESSAGES, NULL);
		RefreshInfoData *data = NULL;

		if (job) {
			data = camel_imapx_job_get_data (job);
			g_return_val_if_fail (data != NULL, FALSE);
		}

		/* This is either a refresh_info job, check to see if it is and update
		 * if so, otherwise it must've been an unsolicited response, so update
		 * the summary to match */
		if (data && (finfo->got & FETCH_UID) && data->scan_changes) {
			struct _refresh_info r;

			r.uid = finfo->uid;
			finfo->uid = NULL;
			r.server_flags = finfo->flags;
			r.server_user_flags = finfo->user_flags;
			finfo->user_flags = NULL;
			r.exists = FALSE;
			g_array_append_val (data->infos, r);
		} else if (is->select_folder) {
			CamelFolder *folder;
			CamelMessageInfo *mi = NULL;
			gboolean changed = FALSE;
			gchar *uid = NULL;

			g_object_ref (is->select_folder);
			folder = is->select_folder;

			c (is->tagprefix, "flag changed: %d\n", is->priv->context->id);

			if (finfo->got & FETCH_UID) {
				uid = finfo->uid;
				finfo->uid = NULL;
			} else {
				uid = imapx_get_uid_from_index (folder->summary, is->priv->context->id - 1);
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
					c (is->tagprefix, "flags changed for unknown uid %s\n.", uid);
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
		CamelIMAPXJob *job = imapx_match_active_job (is, IMAPX_JOB_FETCH_NEW_MESSAGES | IMAPX_JOB_REFRESH_INFO | IMAPX_JOB_FETCH_MESSAGES, NULL);

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

				if (!(finfo->got & FETCH_FLAGS)) {
					RefreshInfoData *data;
					struct _refresh_info *r = NULL;
					gint min, max, mid;
					gboolean found = FALSE;

					data = camel_imapx_job_get_data (job);
					g_return_val_if_fail (data != NULL, FALSE);

					min = data->last_index;
					max = data->index - 1;

					/* array is sorted, so use a binary search */
					do {
						gint cmp = 0;

						mid = (min + max) / 2;
						r = &g_array_index (data->infos, struct _refresh_info, mid);
						cmp = imapx_refresh_info_uid_cmp (finfo->uid, r->uid, is->priv->context->fetch_order == CAMEL_SORT_ASCENDING);

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
						c (is->tagprefix, "Updating unread count for new message %s\n", mi->uid);
						((CamelIMAPXFolder *) job->folder)->unread_on_server++;
					} else {
						c (is->tagprefix, "Not updating unread count for new message %s\n", mi->uid);
					}
				}

				binfo = (CamelMessageInfoBase *) mi;
				binfo->size = finfo->size;

				if (!camel_folder_summary_check_uid (job->folder->summary, mi->uid)) {
					RefreshInfoData *data;
					CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->folder;
					gint cnt;

					data = camel_imapx_job_get_data (job);
					g_return_val_if_fail (data != NULL, FALSE);

					imapx_set_message_info_flags_for_new_message (mi, server_flags, server_user_flags, job->folder);
					camel_folder_summary_add (job->folder->summary, mi);
					camel_folder_change_info_add_uid (data->changes, mi->uid);

					if (!g_hash_table_lookup (ifolder->ignore_recent, mi->uid)) {
						camel_folder_change_info_recent_uid (data->changes, mi->uid);
						g_hash_table_remove (ifolder->ignore_recent, mi->uid);
					}

					cnt = (camel_folder_summary_count (job->folder->summary) * 100 ) / ifolder->exists_on_server;
					camel_operation_progress (job->cancellable, cnt ? cnt : 1);
				} else {
					camel_message_info_free (mi);
				}

				if (free_user_flags && server_user_flags)
					camel_flag_list_free (&server_user_flags);

			}
		}
	}

	imapx_free_fetch (finfo);

	return TRUE;
}

static gboolean
imapx_untagged_lsub (CamelIMAPXServer *is,
                     CamelIMAPXStream *stream,
                     GCancellable *cancellable,
                     GError **error)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	/* cancellable may be NULL */
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	is->priv->context->lsub = TRUE;

	return TRUE;
}

static gboolean
imapx_untagged_list (CamelIMAPXServer *is,
                     CamelIMAPXStream *stream,
                     GCancellable *cancellable,
                     GError **error)
{
	struct _list_info *linfo = NULL;
	CamelIMAPXJob *job = NULL;
	ListData *data = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	/* cancellable may be NULL */
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	linfo = imapx_parse_list (stream, cancellable, error);
	if (!linfo)
		return TRUE;

	job = imapx_match_active_job (is, IMAPX_JOB_LIST, linfo->name);

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	// TODO: we want to make sure the names match?

	if (data->flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) {
		c (is->tagprefix, "lsub: '%s' (%c)\n", linfo->name, linfo->separator);
	} else {
		c (is->tagprefix, "list: '%s' (%c)\n", linfo->name, linfo->separator);
	}

	if (job && g_hash_table_lookup (data->folders, linfo->name) == NULL) {
		if (is->priv->context->lsub)
			linfo->flags |= CAMEL_FOLDER_SUBSCRIBED;
		g_hash_table_insert (data->folders, linfo->name, linfo);
	} else {
		g_warning ("got list response but no current listing job happening?\n");
		imapx_free_list (linfo);
	}

	return TRUE;
}

static gboolean
imapx_untagged_recent (CamelIMAPXServer *is,
                       CamelIMAPXStream *stream,
                       GCancellable *cancellable,
                       GError **error)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	/* cancellable may be NULL */
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	c (is->tagprefix, "recent: %d\n", is->priv->context->id);
	is->recent = is->priv->context->id;

	return TRUE;
}

static gboolean
imapx_untagged_status (CamelIMAPXServer *is,
                       CamelIMAPXStream *stream,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelIMAPXStore *store;
	struct _state_info *sinfo = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	/* cancellable may be NULL */
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	store = camel_imapx_server_ref_store (is);

	sinfo = imapx_parse_status_info (stream, cancellable, error);

	if (sinfo) {
		CamelIMAPXStoreSummary *s = store->summary;
		CamelIMAPXStoreNamespace *ns;
		CamelFolder *folder = NULL;

		ns = camel_imapx_store_summary_namespace_find_full (s, sinfo->name);
		if (ns) {
			gchar *path_name;

			path_name = camel_imapx_store_summary_full_to_path (s, sinfo->name, ns->sep);
			c (is->tagprefix, "Got folder path '%s' for full '%s'\n", path_name, sinfo->name);
			if (path_name) {
				folder = camel_store_get_folder_sync (
					CAMEL_STORE (store),
					path_name, 0, cancellable, error);
				g_free (path_name);
			}
		}
		if (folder != NULL) {
			CamelIMAPXFolder *ifolder;

			ifolder = CAMEL_IMAPX_FOLDER (folder);
			ifolder->unread_on_server = sinfo->unseen;
			ifolder->exists_on_server = sinfo->messages;
			ifolder->modseq_on_server = sinfo->highestmodseq;
			ifolder->uidnext_on_server = sinfo->uidnext;
			ifolder->uidvalidity_on_server = sinfo->uidvalidity;
			if (sinfo->uidvalidity && sinfo->uidvalidity != ((CamelIMAPXSummary *) folder->summary)->validity)
				invalidate_local_cache (ifolder, sinfo->uidvalidity);
		} else {
			c (is->tagprefix, "Received STATUS for unknown folder '%s'\n", sinfo->name);
		}

		g_free (sinfo->name);
		g_free (sinfo);
	}

	g_object_unref (store);

	return TRUE;
}

static gboolean
imapx_untagged_bye (CamelIMAPXServer *is,
                    CamelIMAPXStream *stream,
                    GCancellable *cancellable,
                    GError **error)
{
	guchar *token = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	/* cancellable may be NULL */
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (camel_imapx_stream_text (stream, &token, cancellable, NULL)) {
		c (is->tagprefix, "BYE: %s\n", token);
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"IMAP server said BYE: %s", token);
	}
	is->state = IMAPX_SHUTDOWN;

	return FALSE;
}

static gboolean
imapx_untagged_preauth (CamelIMAPXServer *is,
                        CamelIMAPXStream *stream,
                        GCancellable *cancellable,
                        GError **error)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	/* cancellable may be NULL */
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	c (is->tagprefix, "preauthenticated\n");
	if (is->state < IMAPX_AUTHENTICATED)
		is->state = IMAPX_AUTHENTICATED;

	return TRUE;
}

static gboolean
imapx_untagged_ok_no_bad (CamelIMAPXServer *is,
                          CamelIMAPXStream *stream,
                          GCancellable *cancellable,
                          GError **error)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	/* cancellable may be NULL */
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* TODO: validate which ones of these can happen as unsolicited responses */
	/* TODO: handle bye/preauth differently */
	camel_imapx_stream_ungettoken (
		stream,
		is->priv->context->tok,
		is->priv->context->token,
		is->priv->context->len);
	is->priv->context->sinfo =
		imapx_parse_status (stream, cancellable, error);
	if (is->priv->context->sinfo == NULL)
		return FALSE;
	switch (is->priv->context->sinfo->condition) {
	case IMAPX_CLOSED:
		c (is->tagprefix, "previously selected folder is now closed\n");
		if (is->select_pending && !is->select_folder) {
			is->select_folder = is->select_pending;
		}
		break;
	case IMAPX_READ_WRITE:
		is->mode = IMAPX_MODE_READ | IMAPX_MODE_WRITE;
		c (is->tagprefix, "folder is read-write\n");
		break;
	case IMAPX_READ_ONLY:
		is->mode = IMAPX_MODE_READ;
		c (is->tagprefix, "folder is read-only\n");
		break;
	case IMAPX_UIDVALIDITY:
		is->uidvalidity = is->priv->context->sinfo->u.uidvalidity;
		break;
	case IMAPX_UNSEEN:
		is->unseen = is->priv->context->sinfo->u.unseen;
		break;
	case IMAPX_HIGHESTMODSEQ:
		is->highestmodseq = is->priv->context->sinfo->u.highestmodseq;
		break;
	case IMAPX_PERMANENTFLAGS:
		is->permanentflags = is->priv->context->sinfo->u.permanentflags;
		break;
	case IMAPX_UIDNEXT:
		is->uidnext = is->priv->context->sinfo->u.uidnext;
		break;
	case IMAPX_ALERT:
		c (is->tagprefix, "ALERT!: %s\n", is->priv->context->sinfo->text);
		break;
	case IMAPX_PARSE:
		c (is->tagprefix, "PARSE: %s\n", is->priv->context->sinfo->text);
		break;
	case IMAPX_CAPABILITY:
		if (is->priv->context->sinfo->u.cinfo) {
			struct _capability_info *cinfo = is->cinfo;
			is->cinfo = is->priv->context->sinfo->u.cinfo;
			is->priv->context->sinfo->u.cinfo = NULL;
			if (cinfo)
				imapx_free_capability (cinfo);
			c (is->tagprefix, "got capability flags %08x\n", is->cinfo->capa);
		}
		break;
	default:
		break;
	}
	imapx_free_status (is->priv->context->sinfo);

	return TRUE;
}

/* handle any untagged responses */
static gboolean
imapx_untagged (CamelIMAPXServer *is,
                CamelIMAPXStream *stream,
                GCancellable *cancellable,
                GError **error)
{
	CamelIMAPXSettings *settings;
	CamelSortType fetch_order;
	guchar *p = NULL, c;
	const gchar *token = NULL;
	gboolean ok = FALSE;

	/* If is->priv->context is not NULL here, it basically means
	 * that imapx_untagged() got called concurrently for the same
	 * CamelIMAPXServer instance. Should this ever happen, then
	 * we will need to protect this data structure with locks
	 */
	g_return_val_if_fail (is->priv->context == NULL, FALSE);
	is->priv->context = g_new0 (CamelIMAPXServerUntaggedContext, 1);

	settings = camel_imapx_server_ref_settings (is);
	fetch_order = camel_imapx_settings_get_fetch_order (settings);
	g_object_unref (settings);

	is->priv->context->lsub = FALSE;
	is->priv->context->fetch_order = fetch_order;

	e (is->tagprefix, "got untagged response\n");
	is->priv->context->id = 0;
	is->priv->context->tok = camel_imapx_stream_token (
		stream,
		&(is->priv->context->token),
		&(is->priv->context->len),
		cancellable, error);
	if (is->priv->context->tok < 0)
		goto exit;

	if (is->priv->context->tok == IMAPX_TOK_INT) {
		is->priv->context->id = strtoul (
			(gchar *) is->priv->context->token, NULL, 10);
		is->priv->context->tok = camel_imapx_stream_token (
			stream,
			&(is->priv->context->token),
			&(is->priv->context->len),
			cancellable, error);
		if (is->priv->context->tok < 0)
			goto exit;
	}

	if (is->priv->context->tok == '\n') {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"truncated server response");
		goto exit;
	}

	e (is->tagprefix, "Have token '%s' id %d\n", is->priv->context->token, is->priv->context->id);
	p = is->priv->context->token;
	while ((c = *p))
		*p++ = toupper((gchar) c);

	token = (const gchar *) is->priv->context->token; /* FIXME need 'guchar *token' here */
	while (token != NULL) {
		CamelIMAPXUntaggedRespHandlerDesc *desc = NULL;

		desc = g_hash_table_lookup (is->priv->untagged_handlers, token);
		if (desc == NULL) {
			/* unknown response, just ignore it */
			c (is->tagprefix, "unknown token: %s\n", is->priv->context->token);
			break;
		}
		if (desc->handler == NULL) {
			/* no handler function, ignore token */
			c (is->tagprefix, "no handler for token: %s\n", is->priv->context->token);
			break;
		}

		/* call the handler function */
		ok = desc->handler (is, stream, cancellable, error);
		if (!ok)
			goto exit;

		/* is there another handler next-in-line? */
		token = desc->next_response;
		if (token != NULL) {
			/* TODO do we need to update 'priv->context->token'
			 *      to the value of 'token' here, before
			 *      calling the handler next-in-line for this
			 *      specific run of imapx_untagged()?
			 *      It has not been done in the original code
			 *      in the "fall through" situation in the
			 *      token switch statement, which is what
			 *      we're mimicking here
			 */
			continue;
		}

		if (!desc->skip_stream_when_done)
			goto exit;
	}

	ok = (camel_imapx_stream_skip (stream, cancellable, error) == 0);
 exit:
	g_free (is->priv->context);
	is->priv->context = NULL;

	return ok;
}

/* handle any continuation requests
 * either data continuations, or auth continuation */
static gboolean
imapx_continuation (CamelIMAPXServer *is,
                    CamelIMAPXStream *stream,
                    gboolean litplus,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelIMAPXCommand *ic, *newliteral = NULL;
	CamelIMAPXCommandPart *cp;
	GList *link;
	gssize n_bytes_written;
	gboolean success = TRUE;

	/* The 'literal' pointer is like a write-lock, nothing else
	 * can write while we have it ... so we dont need any
	 * ohter lock here.  All other writes go through
	 * queue-lock */
	if (imapx_idle_supported (is) && imapx_in_idle (is)) {
		camel_imapx_stream_skip (stream, cancellable, error);

		c (is->tagprefix, "Got continuation response for IDLE \n");
		IDLE_LOCK (is->idle);
		/* We might have actually sent the DONE already! */
		if (is->idle->state == IMAPX_IDLE_ISSUED)
			is->idle->state = IMAPX_IDLE_STARTED;
		else if (is->idle->state == IMAPX_IDLE_CANCEL) {
			/* IDLE got cancelled after we sent the command, while
			 * we were waiting for this continuation. Send DONE
			 * immediately. */
			success = imapx_command_idle_stop (
				is, stream, cancellable, error);
			if (!success) {
				IDLE_UNLOCK (is->idle);
				return FALSE;
			}
			is->idle->state = IMAPX_IDLE_OFF;
		} else {
			c (
				is->tagprefix, "idle starts in wrong state %d\n",
				is->idle->state);
		}
		IDLE_UNLOCK (is->idle);

		QUEUE_LOCK (is);
		is->literal = NULL;
		success = imapx_command_start_next (is, cancellable, error);
		QUEUE_UNLOCK (is);

		return success;
	}

	ic = is->literal;
	if (!litplus) {
		if (ic == NULL) {
			camel_imapx_stream_skip (stream, cancellable, error);
			c (is->tagprefix, "got continuation response with no outstanding continuation requests?\n");
			return TRUE;
		}
		c (is->tagprefix, "got continuation response for data\n");
	} else {
		c (is->tagprefix, "sending LITERAL+ continuation\n");
	}

	link = ic->current_part;
	g_return_val_if_fail (link != NULL, FALSE);
	cp = (CamelIMAPXCommandPart *) link->data;

	switch (cp->type & CAMEL_IMAPX_COMMAND_MASK) {
	case CAMEL_IMAPX_COMMAND_DATAWRAPPER:
		c (is->tagprefix, "writing data wrapper to literal\n");
		n_bytes_written = camel_data_wrapper_write_to_stream_sync (
			CAMEL_DATA_WRAPPER (cp->ob),
			CAMEL_STREAM (stream),
			cancellable, error);
		if (n_bytes_written < 0)
			return FALSE;
		break;
	case CAMEL_IMAPX_COMMAND_STREAM:
		c (is->tagprefix, "writing stream to literal\n");
		n_bytes_written = camel_stream_write_to_stream (
			CAMEL_STREAM (cp->ob),
			CAMEL_STREAM (stream),
			cancellable, error);
		if (n_bytes_written < 0)
			return FALSE;
		break;
	case CAMEL_IMAPX_COMMAND_AUTH: {
		gchar *resp;
		guchar *token;

		if (camel_imapx_stream_text (stream, &token, cancellable, error))
			return FALSE;

		resp = camel_sasl_challenge_base64_sync (
			(CamelSasl *) cp->ob, (const gchar *) token,
			cancellable, error);
		g_free (token);
		if (resp == NULL)
			return FALSE;
		c (is->tagprefix, "got auth continuation, feeding token '%s' back to auth mech\n", resp);

		n_bytes_written = camel_stream_write (
			CAMEL_STREAM (stream),
			resp, strlen (resp),
			cancellable, error);
		g_free (resp);

		if (n_bytes_written < 0)
			return FALSE;

		/* we want to keep getting called until we get a status reponse from the server
		 * ignore what sasl tells us */
		newliteral = ic;
		/* We already ate the end of the input stream line */
		goto noskip;
		break; }
	case CAMEL_IMAPX_COMMAND_FILE: {
		CamelStream *file;

		c (is->tagprefix, "writing file '%s' to literal\n", (gchar *) cp->ob);

		// FIXME: errors
		if (cp->ob && (file = camel_stream_fs_new_with_name (cp->ob, O_RDONLY, 0, NULL))) {
			n_bytes_written = camel_stream_write_to_stream (
				file, CAMEL_STREAM (stream),
				cancellable, error);
			g_object_unref (file);

			if (n_bytes_written < 0)
				return FALSE;
		} else if (cp->ob_size > 0) {
			// Server is expecting data ... ummm, send it zeros?  abort?
		}
		break; }
	case CAMEL_IMAPX_COMMAND_STRING:
		n_bytes_written = camel_stream_write (
			CAMEL_STREAM (stream),
			cp->ob, cp->ob_size,
			cancellable, error);
		if (n_bytes_written < 0)
			return FALSE;
		break;
	default:
		/* should we just ignore? */
		is->literal = NULL;
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"continuation response for non-continuation request");
		return FALSE;
	}

	if (!litplus) {
		if (camel_imapx_stream_skip (stream, cancellable, error) == -1)
			return FALSE;
	}

noskip:
	link = g_list_next (link);
	if (link != NULL) {
		ic->current_part = link;
		cp = (CamelIMAPXCommandPart *) link->data;

		c (is->tagprefix, "next part of command \"%c%05u: %s\"\n", is->tagprefix, ic->tag, cp->data);

		n_bytes_written = camel_stream_write_string (
			CAMEL_STREAM (stream), cp->data, cancellable, error);
		if (n_bytes_written < 0)
			return FALSE;

		if (cp->type & (CAMEL_IMAPX_COMMAND_CONTINUATION | CAMEL_IMAPX_COMMAND_LITERAL_PLUS)) {
			newliteral = ic;
		} else {
			g_assert (g_list_next (link) == NULL);
		}
	} else {
		c (is->tagprefix, "%p: queueing continuation\n", ic);
	}

	n_bytes_written = camel_stream_write_string (
		CAMEL_STREAM (stream), "\r\n", cancellable, error);
	if (n_bytes_written < 0)
		return FALSE;

	QUEUE_LOCK (is);
	is->literal = newliteral;

	if (!litplus)
		success = imapx_command_start_next (is, cancellable, error);
	QUEUE_UNLOCK (is);

	return success;
}

/* handle a completion line */
static gboolean
imapx_completion (CamelIMAPXServer *is,
                  CamelIMAPXStream *stream,
                  guchar *token,
                  gint len,
                  GCancellable *cancellable,
                  GError **error)
{
	CamelIMAPXCommand *ic;
	gboolean success;
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

	c (is->tagprefix, "Got completion response for command %05u '%s'\n", ic->tag, ic->name);

	if (camel_folder_change_info_changed (is->changes)) {
		camel_folder_summary_save_to_db (is->select_folder->summary, NULL);

		g_list_free_full (is->expunged, (GDestroyNotify) g_free);
		is->expunged = NULL;

		imapx_update_store_summary (is->select_folder);
		camel_folder_changed (is->select_folder, is->changes);
		camel_folder_change_info_clear (is->changes);
	}

	QUEUE_LOCK (is);

	camel_imapx_command_ref (ic);
	camel_imapx_command_queue_remove (is->active, ic);
	camel_imapx_command_queue_push_tail (is->done, ic);
	camel_imapx_command_unref (ic);

	if (is->literal == ic)
		is->literal = NULL;

	if (g_list_next (ic->current_part) != NULL) {
		QUEUE_UNLOCK (is);
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"command still has unsent parts? %s", ic->name);
		return FALSE;
	}

	camel_imapx_command_queue_remove (is->done, ic);

	QUEUE_UNLOCK (is);

	ic->status = imapx_parse_status (stream, cancellable, error);

	if (ic->status == NULL)
		return FALSE;

	if (ic->complete != NULL)
		if (!ic->complete (is, ic, error))
			return FALSE;

	QUEUE_LOCK (is);
	success = imapx_command_start_next (is, cancellable, error);
	QUEUE_UNLOCK (is);

	return success;
}

static gboolean
imapx_step (CamelIMAPXServer *is,
            GCancellable *cancellable,
            GError **error)
{
	CamelIMAPXStream *stream;
	guint len;
	guchar *token;
	gint tok;
	gboolean success = FALSE;

	stream = camel_imapx_server_ref_stream (is);

	// poll ?  wait for other stuff? loop?
	tok = camel_imapx_stream_token (
		stream, &token, &len, cancellable, error);

	switch (tok) {
		case IMAPX_TOK_PROTOCOL:
		case IMAPX_TOK_ERROR:
			/* GError is already set. */
			break;
		case '*':
			success = imapx_untagged (
				is, stream, cancellable, error);
			break;
		case IMAPX_TOK_TOKEN:
			success = imapx_completion (
				is, stream, token, len, cancellable, error);
			break;
		case '+':
			success = imapx_continuation (
				is, stream, FALSE, cancellable, error);
			break;
		default:
			g_set_error (
				error, CAMEL_IMAPX_ERROR, 1,
				"unexpected server response:");
			break;
	}

	g_object_unref (stream);

	return success;
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
	camel_imapx_command_queue_remove (is->active, ic);
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

static gboolean
imapx_register_job (CamelIMAPXServer *is,
                    CamelIMAPXJob *job,
                    GError **error)
{
	if (is->state >= IMAPX_INITIALISED) {
		QUEUE_LOCK (is);
		g_queue_push_head (&is->jobs, camel_imapx_job_ref (job));
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

static void
imapx_unregister_job (CamelIMAPXServer *is,
                      CamelIMAPXJob *job)
{
	if (!job->noreply)
		camel_imapx_job_done (job);

	QUEUE_LOCK (is);
	if (g_queue_remove (&is->jobs, job))
		camel_imapx_job_unref (job);
	QUEUE_UNLOCK (is);
}

static gboolean
imapx_submit_job (CamelIMAPXServer *is,
                  CamelIMAPXJob *job,
                  GError **error)
{
	if (!imapx_register_job (is, job, error))
		return FALSE;

	return camel_imapx_job_run (job, is, error);
}

/* ********************************************************************** */
// IDLE support

/*TODO handle negative cases sanely */
static gboolean
imapx_command_idle_stop (CamelIMAPXServer *is,
                         CamelIMAPXStream *stream,
                         GCancellable *cancellable,
                         GError **error)
{
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (stream), FALSE);

	success = (camel_stream_write_string (
		CAMEL_STREAM (stream),
		"DONE\r\n", cancellable, error) != -1);

	if (!success) {
		g_prefix_error (error, "Unable to issue DONE: ");
		c (is->tagprefix, "Failed to issue DONE to terminate IDLE\n");
		is->state = IMAPX_SHUTDOWN;
		is->parser_quit = TRUE;
		if (is->cancellable)
			g_cancellable_cancel (is->cancellable);
	}

	return success;
}

static gboolean
imapx_command_idle_done (CamelIMAPXServer *is,
                         CamelIMAPXCommand *ic,
                         GError **error)
{
	CamelIMAPXIdle *idle = is->idle;
	CamelIMAPXJob *job;
	gboolean success = TRUE;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error performing IDLE"));
		success = FALSE;
	}

	IDLE_LOCK (idle);
	idle->state = IMAPX_IDLE_OFF;
	IDLE_UNLOCK (idle);

	imapx_unregister_job (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_idle_start (CamelIMAPXJob *job,
                      CamelIMAPXServer *is)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXCommandPart *cp;

	ic = camel_imapx_command_new (
		is, "IDLE", job->folder, "IDLE");
	camel_imapx_command_set_job (ic, job);
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
		imapx_unregister_job (is, job);
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

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_IDLE;
	job->start = imapx_job_idle_start;
	job->folder = folder;

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

	return success;
}

static gboolean
imapx_job_fetch_new_messages_matches (CamelIMAPXJob *job,
                                      CamelFolder *folder,
                                      const gchar *uid)
{
	return (folder == job->folder);
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
	RefreshInfoData *data;
	gboolean success;

	data = g_slice_new0 (RefreshInfoData);
	data->changes = camel_folder_change_info_new ();
	data->update_unseen = update_unseen;
	data->fetch_msg_limit = -1;

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_FETCH_NEW_MESSAGES;
	job->start = imapx_job_fetch_new_messages_start;
	job->matches = imapx_job_fetch_new_messages_matches;
	job->folder = folder;
	job->noreply = async;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) refresh_info_data_free);

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

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

static CamelIMAPXIdleStopResult
imapx_stop_idle (CamelIMAPXServer *is,
                 CamelIMAPXStream *stream,
                 GCancellable *cancellable,
                 GError **error)
{
	CamelIMAPXIdleStopResult result = IMAPX_IDLE_STOP_NOOP;
	gboolean success;
	time_t now;

	time (&now);

	IDLE_LOCK (is->idle);

	switch (is->idle->state) {
		case IMAPX_IDLE_ISSUED:
			is->idle->state = IMAPX_IDLE_CANCEL;
			/* fall through */

		case IMAPX_IDLE_CANCEL:
			result = IMAPX_IDLE_STOP_SUCCESS;
			break;

		case IMAPX_IDLE_STARTED:
			success = imapx_command_idle_stop (
				is, stream, cancellable, error);
			if (success) {
				result = IMAPX_IDLE_STOP_SUCCESS;
			} else {
				result = IMAPX_IDLE_STOP_ERROR;
				goto exit;
			}

			c (
				is->tagprefix,
				"Stopping idle after %ld seconds\n",
				(glong) (now - is->idle->started));
			/* fall through */

		case IMAPX_IDLE_PENDING:
			is->idle->state = IMAPX_IDLE_OFF;
			/* fall through */

		case IMAPX_IDLE_OFF:
			break;
	}

exit:
	IDLE_UNLOCK (is->idle);

	return result;
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
		GList *link;

		c (is->tagprefix, "Select failed\n");

		QUEUE_LOCK (is);

		if (is->select_pending) {
			GList *head = camel_imapx_command_queue_peek_head_link (is->queue);

			for (link = head; link != NULL; link = g_list_next (link)) {
				CamelIMAPXCommand *cw = link->data;

				if (cw->select && cw->select == is->select_pending) {
					c (
						is->tagprefix, "Cancelling command '%s'(%p) for folder '%s'\n",
						cw->name, cw, camel_folder_get_full_name (cw->select));
					g_queue_push_tail (&trash, link);
				}
			}
		}

		while ((link = g_queue_pop_head (&trash)) != NULL) {
			CamelIMAPXCommand *cw = link->data;
			camel_imapx_command_queue_delete_link (is->queue, link);
			g_queue_push_tail (&failed, cw);
		}

		QUEUE_UNLOCK (is);

		while (!g_queue_is_empty (&failed)) {
			CamelIMAPXCommand *cw;
			CamelIMAPXJob *job;

			cw = g_queue_pop_head (&failed);
			job = camel_imapx_command_get_job (cw);

			if (!CAMEL_IS_IMAPX_JOB (job)) {
				g_warn_if_reached ();
				continue;
			}

			if (ic->status)
				cw->status = imapx_copy_status (ic->status);
			if (job->error == NULL) {
				if (ic->status == NULL)
					/* FIXME: why is ic->status == NULL here? It shouldn't happen. */
					g_debug ("imapx_command_select_done: ic->status is NULL.");
				g_set_error (
					&job->error,
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

		c (is->tagprefix, "Select ok!\n");

		if (!is->select_folder) {
			/* This could have been done earlier by a [CLOSED] status */
			is->select_folder = is->select_pending;
		}
		is->state = IMAPX_SELECTED;
		ifolder->exists_on_server = is->exists;
		ifolder->modseq_on_server = is->highestmodseq;
		if (ifolder->uidnext_on_server < is->uidnext) {
			/* We don't want to fetch new messages if the command we selected this
			 * folder for is *already* fetching all messages (i.e. scan_changes).
			 * Bug #667725. */
			CamelIMAPXJob *job = imapx_is_job_in_queue (
				is, is->select_pending,
				IMAPX_JOB_REFRESH_INFO, NULL);
			if (job) {
				RefreshInfoData *data = camel_imapx_job_get_data (job);

				if (data->scan_changes) {
					c (is->tagprefix, "Will not fetch_new_messages when already in scan_changes\n");
					goto no_fetch_new;
				}
			}
			imapx_server_fetch_new_messages (is, is->select_pending, TRUE, TRUE, NULL, NULL);
			/* We don't do this right now because we want the new messages to
			 * update the unseen count. */
			//ifolder->uidnext_on_server = is->uidnext;
		no_fetch_new:
			;
		}
		ifolder->uidvalidity_on_server = is->uidvalidity;
		selected_folder = camel_folder_get_full_name (is->select_folder);

		if (is->uidvalidity && is->uidvalidity != ((CamelIMAPXSummary *) cfolder->summary)->validity)
			invalidate_local_cache (ifolder, is->uidvalidity);

#if 0  /* see comment for disabled bits in imapx_job_refresh_info_start() */
		/* This should trigger a new messages scan */
		if (is->exists != is->select_folder->summary->root_view->total_count)
			g_warning (
				"exists is %d our summary is %d and summary exists is %d\n", is->exists,
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

	if (!camel_imapx_command_queue_is_empty (is->active))
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

			c (
				is->tagprefix, "SELECT QRESYNC %" G_GUINT64_FORMAT
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

				seqs = g_string_new (" ");
				uids = g_string_new (")");

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
						g_string_prepend (seqs, ",");
						g_string_prepend (uids, ",");
					}

					/* IMAP sequence numbers are one higher than the corresponding
					 * indices in our folder summary -- they start from one, while
					 * the summary starts from zero. */
					sprintf (buf, "%d", total - i + 1);
					g_string_prepend (seqs, buf);
					uid = imapx_get_uid_from_index (folder->summary, total - i);
					g_string_prepend (uids, uid);
					g_free (uid);
				} while (i < total);

				g_string_prepend (seqs, " (");

				c (is->tagprefix, "adding QRESYNC seq/uidset %s%s\n", seqs->str, uids->str);
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

static CamelStream *
connect_to_server_process (CamelIMAPXServer *is,
                           const gchar *cmd,
                           GError **error)
{
	CamelNetworkSettings *network_settings;
	CamelProvider *provider;
	CamelSettings *settings;
	CamelStream *cmd_stream;
	CamelStream *imapx_stream;
	CamelIMAPXStore *store;
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

	store = camel_imapx_server_ref_store (is);

	password = camel_service_get_password (CAMEL_SERVICE (store));
	provider = camel_service_get_provider (CAMEL_SERVICE (store));
	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

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
	child_env[i++] = g_strdup_printf ("URL=%s", buf);
	g_free (buf);

	child_env[i++] = g_strdup_printf ("URLHOST=%s", host);
	if (port)
		child_env[i++] = g_strdup_printf ("URLPORT=%u", port);
	if (user)
		child_env[i++] = g_strdup_printf ("URLUSER=%s", user);
	if (password)
		child_env[i++] = g_strdup_printf ("URLPASSWD=%s", password);
	child_env[i] = NULL;

	g_object_unref (settings);
	g_object_unref (store);

	/* Now do %h, %u, etc. substitution in cmd */
	buf = cmd_copy = g_strdup (cmd);

	full_cmd = g_strdup ("");

	for (;;) {
		gchar *pc;
		gchar *tmp;
		const gchar *var;
		gint len;

		pc = strchr (buf, '%');
	ignore:
		if (!pc) {
			tmp = g_strdup_printf ("%s%s", full_cmd, buf);
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
		tmp = g_strdup_printf ("%s%.*s%s", full_cmd, len, buf, var);
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
		return NULL;
	}

	g_free (full_cmd);

	imapx_stream = camel_imapx_stream_new (cmd_stream);

	g_object_unref (cmd_stream);

	/* Server takes ownership of the IMAPX stream. */
	g_mutex_lock (&is->priv->stream_lock);
	g_warn_if_fail (is->priv->stream == NULL);
	is->priv->stream = CAMEL_IMAPX_STREAM (imapx_stream);
	is->is_process_stream = TRUE;
	g_mutex_unlock (&is->priv->stream_lock);

	g_object_notify (G_OBJECT (is), "stream");

	return imapx_stream;
}
#endif /* G_OS_WIN32 */

gboolean
imapx_connect_to_server (CamelIMAPXServer *is,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelNetworkSettings *network_settings;
	CamelNetworkSecurityMethod method;
	CamelStream *tcp_stream = NULL;
	CamelStream *imapx_stream = NULL;
	CamelSockOptData sockopt;
	CamelIMAPXStore *store;
	CamelSettings *settings;
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

	store = camel_imapx_server_ref_store (is);

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	method = camel_network_settings_get_security_method (network_settings);

#ifndef G_OS_WIN32
	use_shell_command = camel_imapx_settings_get_use_shell_command (
		CAMEL_IMAPX_SETTINGS (settings));

	if (use_shell_command)
		shell_command = camel_imapx_settings_dup_shell_command (
			CAMEL_IMAPX_SETTINGS (settings));
#endif

	g_object_unref (settings);

#ifndef G_OS_WIN32
	if (shell_command != NULL) {
		imapx_stream = connect_to_server_process (
			is, shell_command, &local_error);

		g_free (shell_command);

		if (imapx_stream != NULL)
			goto connected;
		else
			goto exit;
	}
#endif

	tcp_stream = camel_network_service_connect_sync (
		CAMEL_NETWORK_SERVICE (store), cancellable, error);

	if (tcp_stream == NULL) {
		success = FALSE;
		goto exit;
	}

	/* Disable Nagle
	 * We send a lot of small requests which nagle slows down. */
	sockopt.option = CAMEL_SOCKOPT_NODELAY;
	sockopt.value.no_delay = TRUE;
	camel_tcp_stream_setsockopt (CAMEL_TCP_STREAM (tcp_stream), &sockopt);

	/* Set Keepalive
	 * Needed for some hosts/router configurations, we're idle a lot. */
	sockopt.option = CAMEL_SOCKOPT_KEEPALIVE;
	sockopt.value.keep_alive = TRUE;
	camel_tcp_stream_setsockopt (CAMEL_TCP_STREAM (tcp_stream), &sockopt);

	imapx_stream = camel_imapx_stream_new (tcp_stream);

	/* CamelIMAPXServer takes ownership of the IMAPX stream.
	 * We need to set this right away for imapx_command_run()
	 * to work, but we delay emitting a "notify" signal until
	 * we're fully connected. */
	g_mutex_lock (&is->priv->stream_lock);
	g_warn_if_fail (is->priv->stream == NULL);
	is->priv->stream = CAMEL_IMAPX_STREAM (imapx_stream);
	g_mutex_unlock (&is->priv->stream_lock);

	g_object_unref (tcp_stream);

 connected:
	CAMEL_IMAPX_STREAM (imapx_stream)->tagprefix = is->tagprefix;

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

		tok = camel_imapx_stream_token (
			CAMEL_IMAPX_STREAM (imapx_stream),
			&token, &len, cancellable, error);
		if (tok < 0) {
			success = FALSE;
			goto exit;
		}

		if (tok == '*') {
			imapx_untagged (
				is, CAMEL_IMAPX_STREAM (imapx_stream),
				cancellable, error);
			break;
		}
		camel_imapx_stream_ungettoken (
			CAMEL_IMAPX_STREAM (imapx_stream), tok, token, len);

		success = camel_imapx_stream_text (
			CAMEL_IMAPX_STREAM (imapx_stream),
			&token, cancellable, error);
		if (!success)
			goto exit;
		e (is->tagprefix, "Got unexpected line before greeting:  '%s'\n", token);
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
			c (is->tagprefix, "got capability flags %08x\n", is->cinfo->capa);
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
	if (success) {
		g_object_notify (G_OBJECT (is), "stream");
	} else {
		g_mutex_lock (&is->priv->stream_lock);

		if (is->priv->stream != NULL) {
			g_object_unref (is->priv->stream);
			is->priv->stream = NULL;
		}

		if (is->cinfo != NULL) {
			imapx_free_capability (is->cinfo);
			is->cinfo = NULL;
		}

		g_mutex_unlock (&is->priv->stream_lock);
	}

	g_free (host);

	g_object_unref (store);

	return success;
}

CamelAuthenticationResult
camel_imapx_server_authenticate (CamelIMAPXServer *is,
                                 const gchar *mechanism,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelNetworkSettings *network_settings;
	CamelIMAPXStore *store;
	CamelService *service;
	CamelSettings *settings;
	CamelAuthenticationResult result;
	CamelIMAPXCommand *ic;
	CamelSasl *sasl = NULL;
	gchar *host;
	gchar *user;

	g_return_val_if_fail (
		CAMEL_IS_IMAPX_SERVER (is),
		CAMEL_AUTHENTICATION_REJECTED);

	store = camel_imapx_server_ref_store (is);

	service = CAMEL_SERVICE (store);
	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	user = camel_network_settings_dup_user (network_settings);

	g_object_unref (settings);

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
		if (sasl == NULL) {
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
			c (is->tagprefix, "got capability flags %08x\n", is->cinfo->capa);
		}
	}

	camel_imapx_command_unref (ic);

	if (sasl != NULL)
		g_object_unref (sasl);

exit:
	g_free (host);
	g_free (user);

	g_object_unref (store);

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
	CamelIMAPXStore *store;
	CamelSettings *settings;
	gchar *mechanism;
	gboolean use_idle;
	gboolean use_qresync;
	gboolean success = FALSE;

	store = camel_imapx_server_ref_store (is);

	service = CAMEL_SERVICE (store);
	session = camel_service_get_session (service);

	settings = camel_service_ref_settings (service);

	mechanism = camel_network_settings_dup_auth_mechanism (
		CAMEL_NETWORK_SETTINGS (settings));

	use_idle = camel_imapx_settings_get_use_idle (
		CAMEL_IMAPX_SETTINGS (settings));

	use_qresync = camel_imapx_settings_get_use_qresync (
		CAMEL_IMAPX_SETTINGS (settings));

	g_object_unref (settings);

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

	if (store->summary->namespaces == NULL) {
		CamelIMAPXNamespaceList *nsl = NULL;
		CamelIMAPXStoreNamespace *ns = NULL;

		/* set a default namespace */
		nsl = g_malloc0 (sizeof (CamelIMAPXNamespaceList));
		ns = g_new0 (CamelIMAPXStoreNamespace, 1);
		ns->next = NULL;
		ns->path = g_strdup ("");
		ns->full_name = g_strdup ("");
		ns->sep = '/';
		nsl->personal = ns;

		store->summary->namespaces = nsl;
		/* FIXME needs to be identified from list response */
		store->dir_sep = ns->sep;
	}

	is->state = IMAPX_INITIALISED;

	success = TRUE;

	goto exit;

exception:

	imapx_disconnect (is);

	if (is->cinfo) {
		imapx_free_capability (is->cinfo);
		is->cinfo = NULL;
	}

exit:
	g_free (mechanism);

	g_object_unref (store);

	return success;
}

/* ********************************************************************** */

static gboolean
imapx_command_fetch_message_done (CamelIMAPXServer *is,
                                  CamelIMAPXCommand *ic,
                                  GError **error)
{
	CamelIMAPXJob *job;
	GetMessageData *data;
	CamelIMAPXFolder *ifolder;
	gboolean success = TRUE;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	/* We either have more to fetch (partial mode?), we are complete,
	 * or we failed.  Failure is handled in the fetch code, so
	 * we just return the job, or keep it alive with more requests */

	job->commands--;

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error fetching message"));
		data->body_len = -1;

	} else if (data->use_multi_fetch) {
		gsize really_fetched = g_seekable_tell (G_SEEKABLE (data->stream));
		/* Don't automatically stop when we reach the reported message
		 * size -- some crappy servers (like Microsoft Exchange) have
		 * a tendency to lie about it. Keep going (one request at a
		 * time) until the data actually stop coming. */
		if (data->fetch_offset < data->size ||
		    data->fetch_offset == really_fetched) {
			CamelIMAPXCommand *new_ic;

			camel_operation_progress (
				job->cancellable,
				(data->fetch_offset *100) / data->size);

			new_ic = camel_imapx_command_new (
				is, "FETCH", job->folder,
				"UID FETCH %t (BODY.PEEK[]",
				data->uid);
			camel_imapx_command_add (new_ic, "<%u.%u>", data->fetch_offset, MULTI_SIZE);
			camel_imapx_command_add (new_ic, ")");
			new_ic->complete = imapx_command_fetch_message_done;
			camel_imapx_command_set_job (new_ic, job);
			new_ic->pri = job->pri - 1;
			data->fetch_offset += MULTI_SIZE;
			job->commands++;
			imapx_command_queue (is, new_ic);

			camel_imapx_command_unref (ic);

			return TRUE;
		}
	}

	/* If we have more messages to fetch, skip the rest. */
	if (job->commands > 0)
		goto exit;

	/* No more messages to fetch, let's wrap things up. */

	ifolder = CAMEL_IMAPX_FOLDER (job->folder);

	/* return the exception from last command */
	if (local_error != NULL) {
		if (data->stream != NULL) {
			g_object_unref (data->stream);
			data->stream = NULL;
		}

		g_propagate_error (error, local_error);
		local_error = NULL;
		success = FALSE;

	} else if (data->stream != NULL) {
		success =
			(camel_stream_flush (
			data->stream, job->cancellable, &job->error) == 0) &&
			(camel_stream_close (
			data->stream, job->cancellable, &job->error) == 0);

		if (success) {
			gchar *cur_filename;
			gchar *tmp_filename;
			gchar *dirname;

			cur_filename = camel_data_cache_get_filename (
				ifolder->cache, "cur", data->uid);

			tmp_filename = camel_data_cache_get_filename (
				ifolder->cache, "tmp", data->uid);

			dirname = g_path_get_dirname (cur_filename);
			g_mkdir_with_parents (dirname, 0700);
			g_free (dirname);

			if (g_rename (tmp_filename, cur_filename) != 0)
				g_set_error (
					&job->error, G_FILE_ERROR,
					g_file_error_from_errno (errno),
					"%s: %s",
					_("Failed to copy the tmp file"),
					g_strerror (errno));

			g_free (cur_filename);
			g_free (tmp_filename);

			/* Exchange the "tmp" stream for the "cur" stream. */
			g_object_unref (data->stream);
			data->stream = camel_data_cache_get (
				ifolder->cache, "cur", data->uid, &job->error);
			success = (data->stream != NULL);
		} else {
			g_prefix_error (
				&job->error, "%s: ",
				_("Failed to close the tmp stream"));
		}
	}

	camel_data_cache_remove (ifolder->cache, "tmp", data->uid, NULL);
	imapx_unregister_job (is, job);

exit:
	camel_imapx_command_unref (ic);

	g_clear_error (&local_error);

	return success;
}

static void
imapx_job_get_message_start (CamelIMAPXJob *job,
                             CamelIMAPXServer *is)
{
	CamelIMAPXCommand *ic;
	GetMessageData *data;
	gint i;

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	if (data->use_multi_fetch) {
		for (i = 0; i < 3 && data->fetch_offset < data->size; i++) {
			ic = camel_imapx_command_new (
				is, "FETCH", job->folder,
				"UID FETCH %t (BODY.PEEK[]",
				data->uid);
			camel_imapx_command_add (ic, "<%u.%u>", data->fetch_offset, MULTI_SIZE);
			camel_imapx_command_add (ic, ")");
			ic->complete = imapx_command_fetch_message_done;
			camel_imapx_command_set_job (ic, job);
			ic->pri = job->pri;
			data->fetch_offset += MULTI_SIZE;
			job->commands++;
			imapx_command_queue (is, ic);
		}
	} else {
		ic = camel_imapx_command_new (
			is, "FETCH", job->folder,
			"UID FETCH %t (BODY.PEEK[])",
			data->uid);
		ic->complete = imapx_command_fetch_message_done;
		camel_imapx_command_set_job (ic, job);
		ic->pri = job->pri;
		job->commands++;
		imapx_command_queue (is, ic);
	}
}

static gboolean
imapx_job_get_message_matches (CamelIMAPXJob *job,
                               CamelFolder *folder,
                               const gchar *uid)
{
	GetMessageData *data;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	if (folder != job->folder)
		return FALSE;

	if (g_strcmp0 (uid, data->uid) != 0)
		return FALSE;

	return TRUE;
}

/* ********************************************************************** */

static gboolean
imapx_command_copy_messages_step_done (CamelIMAPXServer *is,
                                       CamelIMAPXCommand *ic,
                                       GError **error)
{
	CamelIMAPXJob *job;
	CopyMessagesData *data;
	GPtrArray *uids;
	gint i;
	gboolean success = TRUE;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	uids = data->uids;
	i = data->index;

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			&job->error, "%s: ",
			_("Error copying messages"));
		success = FALSE;
		goto cleanup;
	}

	if (data->delete_originals) {
		gint j;

		for (j = data->last_index; j < i; j++)
			camel_folder_delete_message (job->folder, uids->pdata[j]);
	}

	/* TODO copy the summary and cached messages to the new folder. We might need a sorted insert to avoid refreshing the dest folder */
	if (ic->status && ic->status->condition == IMAPX_COPYUID) {
		gint i;

		for (i = 0; i < ic->status->u.copyuid.copied_uids->len; i++) {
			guint32 uid = GPOINTER_TO_UINT (g_ptr_array_index (ic->status->u.copyuid.copied_uids, i));
			gchar *str = g_strdup_printf ("%d",uid);
			CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) data->dest;

			g_hash_table_insert (ifolder->ignore_recent, str, GINT_TO_POINTER (1));
		}

	}

	if (i < uids->len) {
		camel_imapx_command_unref (ic);
		imapx_command_copy_messages_step_start (is, job, i);
		return TRUE;
	}

cleanup:
	g_object_unref (job->folder);

	imapx_unregister_job (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_command_copy_messages_step_start (CamelIMAPXServer *is,
                                        CamelIMAPXJob *job,
                                        gint index)
{
	CamelIMAPXCommand *ic;
	CopyMessagesData *data;
	GPtrArray *uids;
	gint i = index;

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	uids = data->uids;

	ic = camel_imapx_command_new (
		is, "COPY", job->folder, "UID COPY ");
	ic->complete = imapx_command_copy_messages_step_done;
	camel_imapx_command_set_job (ic, job);
	ic->pri = job->pri;
	data->last_index = i;

	for (; i < uids->len; i++) {
		gint res;
		const gchar *uid = (gchar *) g_ptr_array_index (uids, i);

		res = imapx_uidset_add (&data->uidset, ic, uid);
		if (res == 1) {
			camel_imapx_command_add (ic, " %f", data->dest);
			data->index = i + 1;
			imapx_command_queue (is, ic);
			return;
		}
	}

	data->index = i;
	if (imapx_uidset_done (&data->uidset, ic)) {
		camel_imapx_command_add (ic, " %f", data->dest);
		imapx_command_queue (is, ic);
		return;
	}
}

static void
imapx_job_copy_messages_start (CamelIMAPXJob *job,
                               CamelIMAPXServer *is)
{
	CopyMessagesData *data;

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	if (!imapx_server_sync_changes (
		is, job->folder, job->pri, job->cancellable, &job->error))
		imapx_unregister_job (is, job);

	g_ptr_array_sort (data->uids, (GCompareFunc) imapx_uids_array_cmp);
	imapx_uidset_init (&data->uidset, 0, MAX_COMMAND_LEN);
	imapx_command_copy_messages_step_start (is, job, 0);
}

/* ********************************************************************** */

static gboolean
imapx_command_append_message_done (CamelIMAPXServer *is,
                                   CamelIMAPXCommand *ic,
                                   GError **error)
{
	CamelIMAPXJob *job;
	CamelIMAPXFolder *ifolder;
	CamelMessageInfo *mi;
	AppendMessageData *data;
	gchar *cur, *old_uid;
	gboolean success = TRUE;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	ifolder = (CamelIMAPXFolder *) job->folder;

	/* Append done.  If we the server supports UIDPLUS we will get an APPENDUID response
	 * with the new uid.  This lets us move the message we have directly to the cache
	 * and also create a correctly numbered MessageInfo, without losing any information.
	 * Otherwise we have to wait for the server to less us know it was appended. */

	mi = camel_message_info_clone (data->info);
	old_uid = g_strdup (data->info->uid);

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error appending message"));
		success = FALSE;

	} else if (ic->status && ic->status->condition == IMAPX_APPENDUID) {
		c (is->tagprefix, "Got appenduid %d %d\n", (gint) ic->status->u.appenduid.uidvalidity, (gint) ic->status->u.appenduid.uid);
		if (ic->status->u.appenduid.uidvalidity == ifolder->uidvalidity_on_server) {
			CamelFolderChangeInfo *changes;

			data->appended_uid = g_strdup_printf ("%u", (guint) ic->status->u.appenduid.uid);
			mi->uid = camel_pstring_add (data->appended_uid, FALSE);

			cur = camel_data_cache_get_filename  (ifolder->cache, "cur", mi->uid);
			g_rename (data->path, cur);

			/* should we update the message count ? */
			imapx_set_message_info_flags_for_new_message (mi,
								      ((CamelMessageInfoBase *) data->info)->flags,
								      ((CamelMessageInfoBase *) data->info)->user_flags,
								      job->folder);
			camel_folder_summary_add (job->folder->summary, mi);
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
	g_object_unref (job->folder);

	imapx_unregister_job (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_append_message_start (CamelIMAPXJob *job,
                                CamelIMAPXServer *is)
{
	CamelIMAPXCommand *ic;
	AppendMessageData *data;

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	/* TODO: we could supply the original append date from the file timestamp */
	ic = camel_imapx_command_new (
		is, "APPEND", NULL,
		"APPEND %f %F %P", job->folder,
		((CamelMessageInfoBase *) data->info)->flags,
		((CamelMessageInfoBase *) data->info)->user_flags,
		data->path);
	ic->complete = imapx_command_append_message_done;
	camel_imapx_command_set_job (ic, job);
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
			e ('?', "Ignoring offline uid '%s'\n", camel_message_info_uid (info));
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
	CamelIMAPXFolder *ifolder;
	CamelIMAPXSummary *isum;
	CamelIMAPXJob *job;
	RefreshInfoData *data;
	gint i;
	gboolean success = TRUE;
	CamelIMAPXSettings *settings;
	guint batch_count;
	gboolean mobile_mode;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	data->scan_changes = FALSE;

	ifolder = (CamelIMAPXFolder *) job->folder;
	isum = (CamelIMAPXSummary *) job->folder->summary;

	settings = camel_imapx_server_ref_settings (is);
	batch_count = camel_imapx_settings_get_batch_fetch_count (settings);
	mobile_mode = camel_imapx_settings_get_mobile_mode (settings);
	g_object_unref (settings);

	i = data->index;

	//printf ("%s: Mobile mode: %d Fetch Count %d\n", camel_folder_get_display_name (job->folder), mobile_mode, batch_count);
	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error fetching message headers"));
		success = FALSE;
		goto cleanup;
	}

	if (camel_folder_change_info_changed (data->changes)) {
		imapx_update_store_summary (job->folder);
		camel_folder_summary_save_to_db (job->folder->summary, NULL);
		camel_folder_changed (job->folder, data->changes);
	}

	camel_folder_change_info_clear (data->changes);

	if (i < data->infos->len) {
		gint total = camel_folder_summary_count (job->folder->summary);
		gint fetch_limit = data->fetch_msg_limit;

		camel_imapx_command_unref (ic);

		ic = camel_imapx_command_new (
			is, "FETCH", job->folder, "UID FETCH ");
		ic->complete = imapx_command_step_fetch_done;
		camel_imapx_command_set_job (ic, job);
		ic->pri = job->pri - 1;

		//printf ("Total: %d: %d, %d, %d\n", total, fetch_limit, i, data->last_index);
		data->last_index = i;

		/* If its mobile client and  when total=0 (new account setup) fetch only one batch of mails,
 		 * on futher attempts download all new mails as per the limit. */
		//printf ("Total: %d: %d\n", total, fetch_limit);
		for (; i < data->infos->len && (!mobile_mode || (total && i == 0) || ((fetch_limit != -1 && i < fetch_limit) || (fetch_limit == -1 && i < batch_count))); i++) {

			gint res;
			struct _refresh_info *r = &g_array_index (data->infos, struct _refresh_info, i);

			if (!r->exists) {
				res = imapx_uidset_add (&data->uidset, ic, r->uid);
				if (res == 1) {
					camel_imapx_command_add (ic, " (RFC822.SIZE RFC822.HEADER)");
					data->index = i + 1;
					imapx_command_queue (is, ic);
					return TRUE;
				}
			}
		}

		//printf ("Existing : %d Gonna fetch in %s for %d/%d\n", total, camel_folder_get_full_name (job->folder), i, data->infos->len);
		data->index = data->infos->len;
		if (imapx_uidset_done (&data->uidset, ic)) {
			camel_imapx_command_add (ic, " (RFC822.SIZE RFC822.HEADER)");

			imapx_command_queue (is, ic);
			return TRUE;
		}
	}

	if (camel_folder_summary_count (job->folder->summary)) {
		gchar *uid = imapx_get_uid_from_index (
			job->folder->summary,
			camel_folder_summary_count (job->folder->summary) - 1);
		guint64 uidl = strtoull (uid, NULL, 10);
		g_free (uid);

		uidl++;

		if (uidl > ifolder->uidnext_on_server) {
			c (
				is->tagprefix, "Updating uidnext_on_server for '%s' to %" G_GUINT64_FORMAT "\n",
				camel_folder_get_full_name (job->folder), uidl);
			ifolder->uidnext_on_server = uidl;
		}
	}
	isum->uidnext = ifolder->uidnext_on_server;

 cleanup:
	refresh_info_data_infos_free (data);

	imapx_unregister_job (is, job);
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
	CamelIMAPXJob *job;
	CamelIMAPXSettings *settings;
	RefreshInfoData *data;
	guint uidset_size;
	gboolean success = TRUE;
	gboolean mobile_mode;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	data->scan_changes = FALSE;

	settings = camel_imapx_server_ref_settings (is);
	uidset_size = camel_imapx_settings_get_batch_fetch_count (settings);
	mobile_mode = camel_imapx_settings_get_mobile_mode (settings);
	g_object_unref (settings);

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

		qsort (data->infos->data, data->infos->len, sizeof (struct _refresh_info), imapx_refresh_info_cmp);
		g_ptr_array_sort (uids, (GCompareFunc) imapx_uids_array_cmp);

		if (uids->len)
			s_minfo = camel_folder_summary_get (s, g_ptr_array_index (uids, 0));

		for (i = 0; i < data->infos->len; i++) {
			struct _refresh_info *r = &g_array_index (data->infos, struct _refresh_info, i);

			while (s_minfo && uid_cmp (camel_message_info_uid (s_minfo), r->uid, s) < 0) {
				const gchar *uid = camel_message_info_uid (s_minfo);

				camel_folder_change_info_remove_uid (data->changes, uid);
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
					camel_folder_change_info_change_uid (data->changes, camel_message_info_uid (s_minfo));
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

			e (is->tagprefix, "Message %s vanished\n", s_minfo->uid);
			removed = g_list_prepend (removed, (gpointer) g_strdup (s_minfo->uid));
			camel_message_info_free (s_minfo);
			j++;
		}

		for (l = removed; l != NULL; l = g_list_next (l)) {
			gchar *uid = (gchar *) l->data;

			camel_folder_change_info_remove_uid (data->changes, uid);
		}

		if (removed != NULL) {
			camel_folder_summary_remove_uids (s, removed);
			camel_folder_summary_touch (s);

			g_list_free_full (removed, (GDestroyNotify) g_free);
		}

		camel_folder_summary_save_to_db (s, NULL);
		imapx_update_store_summary (job->folder);

		if (camel_folder_change_info_changed (data->changes))
			camel_folder_changed (job->folder, data->changes);
		camel_folder_change_info_clear (data->changes);

		camel_folder_summary_free_array (uids);

		/* If we have any new messages, download their headers, but only a few (100?) at a time */
		if (fetch_new) {
			job->pop_operation_msg = TRUE;

			camel_operation_push_message (
				job->cancellable,
				_("Fetching summary information for new messages in '%s'"),
				camel_folder_get_display_name (job->folder));

			imapx_uidset_init (&data->uidset, uidset_size, 0);
			/* These are new messages which arrived since we last knew the unseen count;
			 * update it as they arrive. */
			data->update_unseen = TRUE;
			return imapx_command_step_fetch_done (is, ic, error);
		}
	}

	refresh_info_data_infos_free (data);

	/* There's no sane way to get the server-side unseen count on the
	 * select mailbox. So just work it out from the flags if its not in
	 * mobile mode. In mobile mode we would have this filled up already
	 * with a STATUS command.
	 **/
	if (!mobile_mode)
		((CamelIMAPXFolder *) job->folder)->unread_on_server = camel_folder_summary_get_unread_count (job->folder->summary);

	imapx_unregister_job (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_scan_changes_start (CamelIMAPXJob *job,
                              CamelIMAPXServer *is)
{
	CamelIMAPXCommand *ic;
	RefreshInfoData *data;
	CamelIMAPXSettings *settings;
	gboolean mobile_mode;
	gchar *uid = NULL;

	settings = camel_imapx_server_ref_settings (is);
	mobile_mode = camel_imapx_settings_get_mobile_mode (settings);
	g_object_unref (settings);

	if (mobile_mode)
		uid = imapx_get_uid_from_index (job->folder->summary, 0);

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	job->pop_operation_msg = TRUE;

	camel_operation_push_message (
		job->cancellable,
		_("Scanning for changed messages in '%s'"),
		camel_folder_get_display_name (job->folder));

	e (
		'E', "Scanning from %s in %s\n", uid ? uid : "start",
		camel_folder_get_full_name (job->folder));

	ic = camel_imapx_command_new (
		is, "FETCH", job->folder,
		"UID FETCH %s:* (UID FLAGS)", uid ? uid : "1");
	camel_imapx_command_set_job (ic, job);
	ic->complete = imapx_job_scan_changes_done;

	data->scan_changes = TRUE;
	ic->pri = job->pri;
	refresh_info_data_infos_free (data);
	data->infos = g_array_new (0, 0, sizeof (struct _refresh_info));
	imapx_command_queue (is, ic);
	g_free (uid);
}

static gboolean
imapx_command_fetch_new_messages_done (CamelIMAPXServer *is,
                                       CamelIMAPXCommand *ic,
                                       GError **error)
{
	CamelIMAPXJob *job;
	CamelIMAPXSummary *isum;
	CamelIMAPXFolder *ifolder;
	RefreshInfoData *data;
	gboolean success = TRUE;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	ifolder = (CamelIMAPXFolder *) job->folder;
	isum = (CamelIMAPXSummary *) job->folder->summary;

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error fetching new messages"));
		success = FALSE;
		goto exception;
	}

	if (camel_folder_change_info_changed (data->changes)) {
		camel_folder_summary_save_to_db (job->folder->summary, NULL);
		imapx_update_store_summary (job->folder);
		camel_folder_changed (job->folder, data->changes);
		camel_folder_change_info_clear (data->changes);
	}

	if (camel_folder_summary_count (job->folder->summary)) {
		gchar *uid = imapx_get_uid_from_index (
			job->folder->summary,
			camel_folder_summary_count (job->folder->summary) - 1);
		guint64 uidl = strtoull (uid, NULL, 10);
		g_free (uid);

		uidl++;

		if (uidl > ifolder->uidnext_on_server) {
			c (
				is->tagprefix, "Updating uidnext_on_server for '%s' to %" G_GUINT64_FORMAT "\n",
				camel_folder_get_full_name (job->folder), uidl);
			ifolder->uidnext_on_server = uidl;
		}
	}

	isum->uidnext = ifolder->uidnext_on_server;

exception:
	imapx_unregister_job (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static gboolean
imapx_command_fetch_new_uids_done (CamelIMAPXServer *is,
                                   CamelIMAPXCommand *ic,
                                   GError **error)
{
	CamelIMAPXJob *job;
	RefreshInfoData *data;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	data->scan_changes = FALSE;

	qsort (
		data->infos->data,
		data->infos->len,
		sizeof (struct _refresh_info),
		imapx_refresh_info_cmp_descending);

	return imapx_command_step_fetch_done (is, ic, error);
}

static void
imapx_job_fetch_new_messages_start (CamelIMAPXJob *job,
                                    CamelIMAPXServer *is)
{
	CamelIMAPXCommand *ic;
	CamelFolder *folder = job->folder;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
	CamelIMAPXSettings *settings;
	CamelSortType fetch_order;
	RefreshInfoData *data;
	guint32 total, diff;
	guint uidset_size;
	gchar *uid = NULL;

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	settings = camel_imapx_server_ref_settings (is);
	fetch_order = camel_imapx_settings_get_fetch_order (settings);
	uidset_size = camel_imapx_settings_get_batch_fetch_count (settings);
	g_object_unref (settings);

	total = camel_folder_summary_count (folder->summary);
	diff = ifolder->exists_on_server - total;

	if (total > 0) {
		guint64 uidl;
		uid = imapx_get_uid_from_index (folder->summary, total - 1);
		uidl = strtoull (uid, NULL, 10);
		g_free (uid);
		uid = g_strdup_printf ("%" G_GUINT64_FORMAT, uidl + 1);
	} else
		uid = g_strdup ("1");

	job->pop_operation_msg = TRUE;

	camel_operation_push_message (
		job->cancellable,
		_("Fetching summary information for new messages in '%s'"),
		camel_folder_get_display_name (folder));

	//printf ("Fetch order: %d/%d\n", fetch_order, CAMEL_SORT_DESCENDING);
	if (diff > uidset_size || fetch_order == CAMEL_SORT_DESCENDING) {
		ic = camel_imapx_command_new (
			is, "FETCH", job->folder,
			"UID FETCH %s:* (UID FLAGS)", uid);
		imapx_uidset_init (&data->uidset, uidset_size, 0);
		refresh_info_data_infos_free (data);
		data->infos = g_array_new (0, 0, sizeof (struct _refresh_info));
		ic->pri = job->pri;

		data->scan_changes = TRUE;

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
	camel_imapx_command_set_job (ic, job);
	imapx_command_queue (is, ic);
}

static void
imapx_job_fetch_messages_start (CamelIMAPXJob *job,
                                CamelIMAPXServer *is)
{
	CamelIMAPXCommand *ic;
	CamelFolder *folder = job->folder;
	guint32 total;
	gchar *start_uid = NULL, *end_uid = NULL;
	CamelFetchType ftype;
	gint fetch_limit;
	CamelSortType fetch_order;
	CamelIMAPXSettings *settings;
	guint uidset_size;
	RefreshInfoData *data;

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	settings = camel_imapx_server_ref_settings (is);
	fetch_order = camel_imapx_settings_get_fetch_order (settings);
	uidset_size = camel_imapx_settings_get_batch_fetch_count (settings);
	g_object_unref (settings);

	total = camel_folder_summary_count (folder->summary);

	ftype = data->fetch_type;
	fetch_limit = data->fetch_msg_limit;

	if (ftype == CAMEL_FETCH_NEW_MESSAGES ||
		(ftype ==  CAMEL_FETCH_OLD_MESSAGES && total <=0 )) {

		gchar *uid;

		if (total > 0) {
			/* This means that we are fetching limited number of new mails */
			uid = g_strdup_printf ("%d", total);
		} else {
			/* For empty accounts, we always fetch the specified number of new mails independent of
			 * being asked to fetch old or new.
			 */
			uid = g_strdup ("1");
		}

		if (ftype == CAMEL_FETCH_NEW_MESSAGES) {
			/* We need to issue Status command to get the total unread count */
			ic = camel_imapx_command_new (
				is, "STATUS", NULL,
				"STATUS %f (MESSAGES UNSEEN UIDVALIDITY UIDNEXT)", folder);
			camel_imapx_command_set_job (ic, job);
			ic->pri = job->pri;

			imapx_command_run_sync (is, ic, job->cancellable, &job->error);

			job = camel_imapx_command_get_job (ic);
			g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

			if (job->error != NULL || camel_imapx_command_set_error_if_failed (ic, &job->error)) {
				g_prefix_error (
					&job->error, "%s: ",
					_("Error while fetching messages"));
			}

			camel_imapx_command_unref (ic);
		}

		camel_operation_push_message (
			job->cancellable, ngettext (
			"Fetching summary information for %d message in '%s'",
			"Fetching summary information for %d messages in '%s'",
			data->fetch_msg_limit),
			data->fetch_msg_limit,
			camel_folder_get_display_name (folder));

		/* New account and fetching old messages, we would return just the limited number of newest messages */
		ic = camel_imapx_command_new (
			is, "FETCH", job->folder,
			"UID FETCH %s:* (UID FLAGS)", uid);

		imapx_uidset_init (&data->uidset, uidset_size, 0);
		refresh_info_data_infos_free (data);
		data->infos = g_array_new (0, 0, sizeof (struct _refresh_info));
		ic->pri = job->pri;

		data->scan_changes = TRUE;

		if (fetch_order == CAMEL_SORT_DESCENDING)
			ic->complete = imapx_command_fetch_new_uids_done;
		else
			ic->complete = imapx_command_step_fetch_done;

		g_free (uid);

		camel_imapx_command_set_job (ic, job);
		imapx_command_queue (is, ic);

	} else if (ftype == CAMEL_FETCH_OLD_MESSAGES && total > 0) {
		guint64 uidl;
		start_uid = imapx_get_uid_from_index (folder->summary, 0);
		uidl = strtoull (start_uid, NULL, 10);
		end_uid = g_strdup_printf ("%" G_GINT64_MODIFIER "d", (((gint) uidl) - fetch_limit > 0) ? (uidl - fetch_limit) : 1);

		camel_operation_push_message (
			job->cancellable, ngettext (
			"Fetching summary information for %d message in '%s'",
			"Fetching summary information for %d messages in '%s'",
			data->fetch_msg_limit),
			data->fetch_msg_limit,
			camel_folder_get_display_name (folder));

		ic = camel_imapx_command_new (
			is, "FETCH", job->folder,
			"UID FETCH %s:%s (RFC822.SIZE RFC822.HEADER FLAGS)", start_uid, end_uid);
		ic->pri = job->pri;
		ic->complete = imapx_command_fetch_new_messages_done;

		g_free (start_uid);
		g_free (end_uid);

		camel_imapx_command_set_job (ic, job);
		imapx_command_queue (is, ic);

	} else {
		g_error ("Shouldn't reach here. Incorrect fetch type");
	}
}

static void
imapx_job_refresh_info_start (CamelIMAPXJob *job,
                              CamelIMAPXServer *is)
{
	guint32 total;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->folder;
	CamelIMAPXSummary *isum = (CamelIMAPXSummary *) job->folder->summary;
	CamelFolder *folder = job->folder;
	const gchar *full_name;
	gboolean need_rescan = FALSE;
	gboolean is_selected = FALSE;
	gboolean can_qresync = FALSE;
	CamelIMAPXSettings *settings;
	gboolean mobile_mode;

	settings = camel_imapx_server_ref_settings (is);
	mobile_mode = camel_imapx_settings_get_mobile_mode (settings);
	g_object_unref (settings);

	full_name = camel_folder_get_full_name (folder);

	/* Sync changes first, else unread count will not
	 * match. Need to think about better ways for this */
	if (!imapx_server_sync_changes (
		is, folder, job->pri,
		job->cancellable, &job->error))
		goto done;

#if 0	/* There are issues with this still; continue with the buggy
	 * behaviour where we issue STATUS on the current folder, for now. */
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

		#if 0  /* see comment for disabled bits above */
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
		} else
		#endif
		{
			if (is->cinfo->capa & IMAPX_CAPABILITY_CONDSTORE)
				ic = camel_imapx_command_new (
					is, "STATUS", NULL,
					"STATUS %f (MESSAGES UNSEEN UIDVALIDITY UIDNEXT HIGHESTMODSEQ)", folder);
			else
				ic = camel_imapx_command_new (
					is, "STATUS", NULL,
					"STATUS %f (MESSAGES UNSEEN UIDVALIDITY UIDNEXT)", folder);

			camel_imapx_command_set_job (ic, job);
			ic->pri = job->pri;

			imapx_command_run_sync (
				is, ic, job->cancellable, &job->error);

			job = camel_imapx_command_get_job (ic);
			g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

			if (job->error != NULL || camel_imapx_command_set_error_if_failed (ic, &job->error)) {
				g_prefix_error (
					&job->error, "%s: ",
					_("Error refreshing folder"));
			}

			if (job->error != NULL) {
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

	} else if (mobile_mode) {
		/* We need to issue Status command to get the total unread count */
		CamelIMAPXCommand *ic;

		ic = camel_imapx_command_new (
			is, "STATUS", NULL,
			"STATUS %f (MESSAGES UNSEEN UIDVALIDITY UIDNEXT)", folder);
		camel_imapx_command_set_job (ic, job);
		ic->pri = job->pri;

		imapx_command_run_sync (is, ic, job->cancellable, &job->error);

		job = camel_imapx_command_get_job (ic);
		g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

		if (job->error != NULL || camel_imapx_command_set_error_if_failed (ic, &job->error)) {
			g_prefix_error (
				&job->error, "%s: ",
				_("Error refreshing folder"));
		}

		if (job->error != NULL) {
			camel_imapx_command_unref (ic);
			goto done;
		}
		camel_imapx_command_unref (ic);
	}

	if (is->use_qresync && isum->modseq && ifolder->uidvalidity_on_server)
		can_qresync = TRUE;

	e (
		is->tagprefix, "folder %s is %sselected, total %u / %u, unread %u / %u, modseq %" G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT ", uidnext %u / %u: will %srescan\n",
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
			c (
				is->tagprefix, "Eep, after QRESYNC we're out of sync. total %u / %u, unread %u / %u, modseq %" G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT "\n",
				total, ifolder->exists_on_server,
				camel_folder_summary_get_unread_count (folder->summary), ifolder->unread_on_server,
				isum->modseq, ifolder->modseq_on_server);
		} else {
			c (
				is->tagprefix, "OK, after QRESYNC we're still in sync. total %u / %u, unread %u / %u, modseq %" G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT "\n",
				total, ifolder->exists_on_server,
				camel_folder_summary_get_unread_count (folder->summary), ifolder->unread_on_server,
				isum->modseq, ifolder->modseq_on_server);
			goto done;
		}
	}

	imapx_job_scan_changes_start (job, is);
	return;

done:
	imapx_unregister_job (is, job);
}

static gboolean
imapx_job_refresh_info_matches (CamelIMAPXJob *job,
                                CamelFolder *folder,
                                const gchar *uid)
{
	return (folder == job->folder);
}

/* ********************************************************************** */

static gboolean
imapx_command_expunge_done (CamelIMAPXServer *is,
                            CamelIMAPXCommand *ic,
                            GError **error)
{
	CamelIMAPXJob *job;
	gboolean success = TRUE;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error expunging message"));
		success = FALSE;

	} else {
		GPtrArray *uids;
		CamelFolder *folder = job->folder;
		CamelStore *parent_store;
		const gchar *full_name;

		full_name = camel_folder_get_full_name (folder);
		parent_store = camel_folder_get_parent_store (folder);

		camel_folder_summary_save_to_db (folder->summary, NULL);
		uids = camel_db_get_folder_deleted_uids (parent_store->cdb_r, full_name, &job->error);

		if (uids && uids->len)	{
			CamelFolderChangeInfo *changes;
			GList *removed = NULL;
			gint i;

			changes = camel_folder_change_info_new ();
			for (i = 0; i < uids->len; i++) {
				gchar *uid = uids->pdata[i];
				CamelMessageInfo *mi;

				mi = camel_folder_summary_peek_loaded (folder->summary, uid);
				if (mi) {
					camel_folder_summary_remove (folder->summary, mi);
					camel_message_info_free (mi);
				} else {
					camel_folder_summary_remove_uid (folder->summary, uid);
				}

				camel_folder_change_info_remove_uid (changes, uids->pdata[i]);
				removed = g_list_prepend (removed, (gpointer) uids->pdata[i]);
			}

			camel_folder_summary_save_to_db (folder->summary, NULL);
			camel_folder_changed (folder, changes);
			camel_folder_change_info_free (changes);

			g_list_free (removed);
			g_ptr_array_foreach (uids, (GFunc) camel_pstring_free, NULL);
			g_ptr_array_free (uids, TRUE);
		}
	}

	imapx_unregister_job (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_expunge_start (CamelIMAPXJob *job,
                         CamelIMAPXServer *is)
{
	CamelIMAPXCommand *ic;

	imapx_server_sync_changes (
		is, job->folder, job->pri,
		job->cancellable, &job->error);

	/* TODO handle UIDPLUS capability */
	ic = camel_imapx_command_new (
		is, "EXPUNGE", job->folder, "EXPUNGE");
	camel_imapx_command_set_job (ic, job);
	ic->pri = job->pri;
	ic->complete = imapx_command_expunge_done;

	imapx_command_queue (is, ic);
}

static gboolean
imapx_job_expunge_matches (CamelIMAPXJob *job,
                           CamelFolder *folder,
                           const gchar *uid)
{
	return (folder == job->folder);
}

/* ********************************************************************** */

static gboolean
imapx_command_list_done (CamelIMAPXServer *is,
                         CamelIMAPXCommand *ic,
                         GError **error)
{
	CamelIMAPXJob *job;
	gboolean success = TRUE;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error fetching folders"));
		success = FALSE;
	}

	e (is->tagprefix, "==== list or lsub completed ==== \n");
	imapx_unregister_job (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_list_start (CamelIMAPXJob *job,
                      CamelIMAPXServer *is)
{
	CamelIMAPXCommand *ic;
	ListData *data;

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	ic = camel_imapx_command_new (
		is, "LIST", NULL,
		"%s \"\" %s",
		(data->flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) ?
			"LSUB" : "LIST",
		data->pattern);
	if (data->ext) {
		/* Hm, we need a way to add atoms _without_ quoting or using literals */
		camel_imapx_command_add (ic, " ");
		camel_imapx_command_add (ic, data->ext);
	}
	ic->pri = job->pri;
	camel_imapx_command_set_job (ic, job);
	ic->complete = imapx_command_list_done;

	imapx_command_queue (is, ic);
}

static gboolean
imapx_job_list_matches (CamelIMAPXJob *job,
                        CamelFolder *folder,
                        const gchar *uid)
{
	return TRUE;  /* matches everything */
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
	CamelIMAPXJob *job;
	gboolean success = TRUE;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error subscribing to folder"));
		success = FALSE;
	}

	imapx_unregister_job (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_manage_subscription_start (CamelIMAPXJob *job,
                                     CamelIMAPXServer *is)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXStore *store;
	ManageSubscriptionsData *data;
	gchar *encoded_fname = NULL;

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	store = camel_imapx_server_ref_store (is);

	encoded_fname = imapx_encode_folder_name (store, data->folder_name);

	if (data->subscribe)
		ic = camel_imapx_command_new (
			is, "SUBSCRIBE", NULL,
			"SUBSCRIBE %s", encoded_fname);
	else
		ic = camel_imapx_command_new (
			is, "UNSUBSCRIBE", NULL,
			"UNSUBSCRIBE %s", encoded_fname);

	ic->pri = job->pri;
	camel_imapx_command_set_job (ic, job);
	ic->complete = imapx_command_subscription_done;
	imapx_command_queue (is, ic);

	g_free (encoded_fname);

	g_object_unref (store);
}

/* ********************************************************************** */

static gboolean
imapx_command_create_folder_done (CamelIMAPXServer *is,
                                  CamelIMAPXCommand *ic,
                                  GError **error)
{
	CamelIMAPXJob *job;
	gboolean success = TRUE;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error creating folder"));
		success = FALSE;
	}

	imapx_unregister_job (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_create_folder_start (CamelIMAPXJob *job,
                               CamelIMAPXServer *is)
{
	CamelIMAPXCommand *ic;
	CreateFolderData *data;
	gchar *encoded_fname = NULL;

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	encoded_fname = camel_utf8_utf7 (data->folder_name);
	ic = camel_imapx_command_new (
		is, "CREATE", NULL,
		"CREATE %s", encoded_fname);
	ic->pri = job->pri;
	camel_imapx_command_set_job (ic, job);
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
	CamelIMAPXJob *job;
	gboolean success;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error deleting folder"));
		success = FALSE;
	}

	imapx_unregister_job (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_delete_folder_start (CamelIMAPXJob *job,
                               CamelIMAPXServer *is)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXStore *store;
	DeleteFolderData *data;
	gchar *encoded_fname = NULL;

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	store = camel_imapx_server_ref_store (is);

	encoded_fname = imapx_encode_folder_name (store, data->folder_name);

	job->folder = camel_store_get_folder_sync (
		CAMEL_STORE (store), "INBOX", 0,
		job->cancellable, &job->error);

	/* Make sure the to-be-deleted folder is not
	 * selected by selecting INBOX for this operation. */
	ic = camel_imapx_command_new (
		is, "DELETE", job->folder,
		"DELETE %s", encoded_fname);
	ic->pri = job->pri;
	camel_imapx_command_set_job (ic, job);
	ic->complete = imapx_command_delete_folder_done;
	imapx_command_queue (is, ic);

	g_free (encoded_fname);

	g_object_unref (store);
}

/* ********************************************************************** */

static gboolean
imapx_command_rename_folder_done (CamelIMAPXServer *is,
                                  CamelIMAPXCommand *ic,
                                  GError **error)
{
	CamelIMAPXJob *job;
	gboolean success = TRUE;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error renaming folder"));
		success = FALSE;
	}

	imapx_unregister_job (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_rename_folder_start (CamelIMAPXJob *job,
                               CamelIMAPXServer *is)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXStore *store;
	RenameFolderData *data;
	gchar *en_ofname = NULL, *en_nfname = NULL;

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	store = camel_imapx_server_ref_store (is);

	job->folder = camel_store_get_folder_sync (
		CAMEL_STORE (store), "INBOX", 0,
		job->cancellable, &job->error);

	en_ofname = imapx_encode_folder_name (store, data->old_folder_name);
	en_nfname = imapx_encode_folder_name (store, data->new_folder_name);

	ic = camel_imapx_command_new (
		is, "RENAME", job->folder,
		"RENAME %s %s", en_ofname, en_nfname);
	ic->pri = job->pri;
	camel_imapx_command_set_job (ic, job);
	ic->complete = imapx_command_rename_folder_done;
	imapx_command_queue (is, ic);

	g_free (en_ofname);
	g_free (en_nfname);

	g_object_unref (store);
}

/* ********************************************************************** */

static gboolean
imapx_command_noop_done (CamelIMAPXServer *is,
                         CamelIMAPXCommand *ic,
                         GError **error)
{
	CamelIMAPXJob *job;
	gboolean success = TRUE;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	if (camel_imapx_command_set_error_if_failed (ic, error)) {
		g_prefix_error (
			error, "%s: ",
			_("Error performing NOOP"));
		success = FALSE;
	}

	imapx_unregister_job (is, job);
	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_noop_start (CamelIMAPXJob *job,
                      CamelIMAPXServer *is)
{
	CamelIMAPXCommand *ic;

	ic = camel_imapx_command_new (
		is, "NOOP", job->folder, "NOOP");

	camel_imapx_command_set_job (ic, job);
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
	CamelIMAPXJob *job;
	CamelStore *parent_store;
	SyncChangesData *data;
	const gchar *full_name;
	CamelIMAPXSettings *settings;
	gboolean mobile_mode;
	gboolean success = TRUE;

	job = camel_imapx_command_get_job (ic);
	g_return_val_if_fail (CAMEL_IS_IMAPX_JOB (job), FALSE);

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	settings = camel_imapx_server_ref_settings (is);
	mobile_mode = camel_imapx_settings_get_mobile_mode (settings);
	g_object_unref (settings);

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

		for (i = 0; i < data->changed_uids->len; i++) {
			CamelIMAPXMessageInfo *xinfo = (CamelIMAPXMessageInfo *) camel_folder_summary_get (job->folder->summary,
					data->changed_uids->pdata[i]);

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
		((CamelIMAPXFolder *) job->folder)->unread_on_server += data->unread_change;
	}

	if (job->commands == 0) {
		if (job->folder->summary && (job->folder->summary->flags & CAMEL_FOLDER_SUMMARY_DIRTY) != 0) {
			CamelStoreInfo *si;

			/* ... and store's summary when folder's summary is dirty */
			si = camel_store_summary_path ((CamelStoreSummary *)((CamelIMAPXStore *) parent_store)->summary, full_name);
			if (si) {
				if (si->total != camel_folder_summary_get_saved_count (job->folder->summary) ||
				    si->unread != camel_folder_summary_get_unread_count (job->folder->summary)) {
					si->total = camel_folder_summary_get_saved_count (job->folder->summary);
					if (!mobile_mode) /* Don't mess with server's unread count in mobile mode, as what we have downloaded is little */
						si->unread = camel_folder_summary_get_unread_count (job->folder->summary);
					camel_store_summary_touch ((CamelStoreSummary *)((CamelIMAPXStore *) parent_store)->summary);
				}

				camel_store_summary_info_free ((CamelStoreSummary *)((CamelIMAPXStore *) parent_store)->summary, si);
			}
		}

		camel_folder_summary_save_to_db (job->folder->summary, &job->error);
		camel_store_summary_save ((CamelStoreSummary *)((CamelIMAPXStore *) parent_store)->summary);

		imapx_unregister_job (is, job);
	}

	camel_imapx_command_unref (ic);

	return success;
}

static void
imapx_job_sync_changes_start (CamelIMAPXJob *job,
                              CamelIMAPXServer *is)
{
	SyncChangesData *data;
	guint32 i, j;
	struct _uidset_state ss;
	GPtrArray *uids;
	gint on;

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	uids = data->changed_uids;

	for (on = 0; on < 2; on++) {
		guint32 orset = on ? data->on_set : data->off_set;
		GArray *user_set = on ? data->on_user : data->off_user;

		for (j = 0; j < G_N_ELEMENTS (flags_table); j++) {
			guint32 flag = flags_table[j].flag;
			CamelIMAPXCommand *ic = NULL;

			if ((orset & flag) == 0)
				continue;

			c (is->tagprefix, "checking/storing %s flags '%s'\n", on?"on":"off", flags_table[j].name);
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
						camel_imapx_command_set_job (ic, job);
						ic->pri = job->pri;
					}
					send = imapx_uidset_add (&ss, ic, camel_message_info_uid (info));
				}
				if (send == 1 || (i == uids->len - 1 && imapx_uidset_done (&ss, ic))) {
					job->commands++;
					camel_imapx_command_add (ic, " %tFLAGS.SILENT (%t)", on?"+":"-", flags_table[j].name);
					imapx_command_queue (is, ic);
					ic = NULL;
				}
				if (flag == CAMEL_MESSAGE_SEEN) {
					/* Remember how the server's unread count will change if this
					 * command succeeds */
					if (on)
						data->unread_change--;
					else
						data->unread_change++;
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
						camel_imapx_command_set_job (ic, job);
						ic->pri = job->pri;
					}

					if (imapx_uidset_add (&ss, ic, camel_message_info_uid (info)) == 1
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
		imapx_unregister_job (is, job);
	}
}

static gboolean
imapx_job_sync_changes_matches (CamelIMAPXJob *job,
                                CamelFolder *folder,
                                const gchar *uid)
{
	return (folder == job->folder);
}

/* we cancel all the commands and their jobs, so associated jobs will be notified */
static void
cancel_all_jobs (CamelIMAPXServer *is,
                 GError *error)
{
	CamelIMAPXCommandQueue *queue;
	GList *head, *link;

	/* Transfer all pending and active commands to a separate
	 * command queue to complete them without holding QUEUE_LOCK. */

	queue = camel_imapx_command_queue_new ();

	QUEUE_LOCK (is);

	camel_imapx_command_queue_transfer (is->queue, queue);
	camel_imapx_command_queue_transfer (is->active, queue);

	QUEUE_UNLOCK (is);

	head = camel_imapx_command_queue_peek_head_link (queue);

	for (link = head; link != NULL; link = g_list_next (link)) {
		CamelIMAPXCommand *ic = link->data;
		CamelIMAPXJob *job;

		/* Sanity check the CamelIMAPXCommand before proceeding.
		 * XXX We are actually getting reports of crashes here...
		 *     not sure how this is happening but it's happening. */
		if (ic == NULL)
			continue;

		/* Similarly with the CamelIMAPXJob contained within. */
		job = camel_imapx_command_get_job (ic);
		if (!CAMEL_IS_IMAPX_JOB (job))
			continue;

		if (job->error == NULL)
			job->error = g_error_copy (error);

		/* Send a NULL GError since we've already set
		 * the job's GError, and we're not interested
		 * in individual command errors. */
		ic->complete (is, ic, NULL);
	}

	camel_imapx_command_queue_free (queue);
}

/* ********************************************************************** */

static void
parse_contents (CamelIMAPXServer *is,
                GCancellable *cancellable,
                GError **error)
{
	CamelIMAPXStream *stream;

	stream = camel_imapx_server_ref_stream (is);
	g_return_if_fail (stream != NULL);

	while (imapx_step (is, cancellable, error))
		if (camel_imapx_stream_buffered (stream) == 0)
			break;

	g_object_unref (stream);
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
	CamelIMAPXStream *stream;
	GCancellable *cancellable;
	gboolean have_stream;
	GError *local_error = NULL;

	QUEUE_LOCK (is);
	cancellable = camel_operation_new ();
	is->cancellable = g_object_ref (cancellable);
	QUEUE_UNLOCK (is);

	stream = camel_imapx_server_ref_stream (is);
	if (stream != NULL) {
		have_stream = TRUE;
		g_object_unref (stream);
	} else {
		have_stream = FALSE;
	}

	/* FIXME This should really be a GMainLoop instead of a 'while' loop.
	 *       Testing for a stream on each loop iteration is pretty hokey.
	 *       Disconnecting the stream could just terminate the parser
	 *       thread's main loop. */
	while (local_error == NULL && have_stream) {
		g_cancellable_reset (cancellable);

#ifndef G_OS_WIN32
		if (is->is_process_stream)	{
			GPollFD fds[2] = { {0, 0, 0}, {0, 0, 0} };
			CamelStream *source;
			gint res;

			stream = camel_imapx_server_ref_stream (is);
			source = camel_imapx_stream_ref_source (stream);

			fds[0].fd = CAMEL_STREAM_PROCESS (source)->sockfd;
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

			g_object_unref (source);
			g_object_unref (stream);
		} else
#endif
		{
			parse_contents (is, cancellable, &local_error);
		}

		if (is->parser_quit)
			g_cancellable_cancel (cancellable);
		else if (g_cancellable_is_cancelled (cancellable)) {
			gint is_empty;

			QUEUE_LOCK (is);
			is_empty = camel_imapx_command_queue_is_empty (is->active);
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

		stream = camel_imapx_server_ref_stream (is);
		if (stream != NULL) {
			have_stream = TRUE;
			g_object_unref (stream);
		} else {
			have_stream = FALSE;
		}
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
imapx_server_set_store (CamelIMAPXServer *server,
                        CamelIMAPXStore *store)
{
	g_return_if_fail (CAMEL_IS_IMAPX_STORE (store));

	g_weak_ref_set (&server->priv->store, store);
}

static void
imapx_server_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_STORE:
			imapx_server_set_store (
				CAMEL_IMAPX_SERVER (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_server_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_STREAM:
			g_value_take_object (
				value,
				camel_imapx_server_ref_stream (
				CAMEL_IMAPX_SERVER (object)));
			return;

		case PROP_STORE:
			g_value_take_object (
				value,
				camel_imapx_server_ref_store (
				CAMEL_IMAPX_SERVER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
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

	g_weak_ref_set (&server->priv->store, NULL);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_server_parent_class)->dispose (object);
}

static void
imapx_server_finalize (GObject *object)
{
	CamelIMAPXServer *is = CAMEL_IMAPX_SERVER (object);

	g_mutex_clear (&is->priv->stream_lock);

	camel_imapx_command_queue_free (is->queue);
	camel_imapx_command_queue_free (is->active);
	camel_imapx_command_queue_free (is->done);

	is->queue = NULL;
	is->active = NULL;
	is->done = NULL;

	g_static_rec_mutex_free (&is->queue_lock);
	g_mutex_free (is->fetch_mutex);
	g_cond_free (is->fetch_cond);

	camel_folder_change_info_free (is->changes);

	g_free (is->priv->context);
	g_hash_table_destroy (is->priv->untagged_handlers);

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

	g_type_class_add_private (class, sizeof (CamelIMAPXServerPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = imapx_server_set_property;
	object_class->get_property = imapx_server_get_property;
	object_class->finalize = imapx_server_finalize;
	object_class->dispose = imapx_server_dispose;
	object_class->constructed = imapx_server_constructed;

	class->select_changed = NULL;
	class->shutdown = NULL;

	g_object_class_install_property (
		object_class,
		PROP_STREAM,
		g_param_spec_object (
			"stream",
			"Stream",
			"IMAP network stream",
			CAMEL_TYPE_IMAPX_STREAM,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_STORE,
		g_param_spec_object (
			"store",
			"Store",
			"IMAPX store for this server",
			CAMEL_TYPE_IMAPX_STORE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

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
	is->priv = CAMEL_IMAPX_SERVER_GET_PRIVATE (is);

	is->priv->untagged_handlers = create_initial_untagged_handler_table ();

	g_mutex_init (&is->priv->stream_lock);

	is->queue = camel_imapx_command_queue_new ();
	is->active = camel_imapx_command_queue_new ();
	is->done = camel_imapx_command_queue_new ();

	g_queue_init (&is->jobs);

	/* not used at the moment. Use it in future */
	is->job_timeout = 29 * 60 * 1000 * 1000;

	g_static_rec_mutex_init (&is->queue_lock);

	is->state = IMAPX_DISCONNECTED;

	is->expunged = NULL;
	is->changes = camel_folder_change_info_new ();
	is->parser_quit = FALSE;

	is->fetch_mutex = g_mutex_new ();
	is->fetch_cond = g_cond_new ();
}

CamelIMAPXServer *
camel_imapx_server_new (CamelIMAPXStore *store)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_STORE (store), NULL);

	return g_object_new (
		CAMEL_TYPE_IMAPX_SERVER,
		"store", store, NULL);
}

CamelIMAPXStore *
camel_imapx_server_ref_store (CamelIMAPXServer *server)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (server), NULL);

	return g_weak_ref_get (&server->priv->store);
}

CamelIMAPXSettings *
camel_imapx_server_ref_settings (CamelIMAPXServer *server)
{
	CamelIMAPXStore *store;
	CamelSettings *settings;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (server), NULL);

	store = camel_imapx_server_ref_store (server);
	settings = camel_service_ref_settings (CAMEL_SERVICE (store));
	g_object_unref (store);

	return CAMEL_IMAPX_SETTINGS (settings);
}

CamelIMAPXStream *
camel_imapx_server_ref_stream (CamelIMAPXServer *server)
{
	CamelIMAPXStream *stream = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (server), NULL);

	g_mutex_lock (&server->priv->stream_lock);

	if (server->priv->stream != NULL)
		stream = g_object_ref (server->priv->stream);

	g_mutex_unlock (&server->priv->stream_lock);

	return stream;
}

static gboolean
imapx_disconnect (CamelIMAPXServer *is)
{
	gboolean ret = TRUE;

	g_mutex_lock (&is->priv->stream_lock);

	if (is->priv->stream != NULL) {
		CamelStream *stream = CAMEL_STREAM (is->priv->stream);

		if (camel_stream_close (stream, NULL, NULL) == -1)
			ret = FALSE;

		g_object_unref (is->priv->stream);
		is->priv->stream = NULL;
	}

	g_mutex_unlock (&is->priv->stream_lock);

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

	g_object_notify (G_OBJECT (is), "stream");

	return ret;
}

/* Client commands */
gboolean
camel_imapx_server_connect (CamelIMAPXServer *is,
                            GCancellable *cancellable,
                            GError **error)
{
	if (is->state == IMAPX_SHUTDOWN) {
		g_set_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE, "Shutting down");
		return FALSE;
	}

	if (is->state >= IMAPX_INITIALISED)
		return TRUE;

	if (!imapx_reconnect (is, cancellable, error))
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
	CamelStream *stream = NULL;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
	CamelIMAPXJob *job;
	CamelMessageInfo *mi;
	GetMessageData *data;
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

	data = g_slice_new0 (GetMessageData);
	data->uid = g_strdup (uid);
	data->stream = camel_data_cache_add (ifolder->cache, "tmp", uid, NULL);
	data->size = ((CamelMessageInfoBase *) mi)->size;
	if (data->size > MULTI_SIZE)
		data->use_multi_fetch = TRUE;

	job = camel_imapx_job_new (cancellable);
	job->pri = pri;
	job->type = IMAPX_JOB_GET_MESSAGE;
	job->start = imapx_job_get_message_start;
	job->matches = imapx_job_get_message_matches;
	job->folder = folder;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) get_message_data_free);

	camel_message_info_free (mi);
	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	success = registered && camel_imapx_job_run (job, is, error);

	if (success)
		stream = g_object_ref (data->stream);

	camel_imapx_job_unref (job);

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
		ifolder->cache, "cur", uid);
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
	CopyMessagesData *data;
	gint ii;

	data = g_slice_new0 (CopyMessagesData);
	data->dest = g_object_ref (dest);
	data->uids = g_ptr_array_new ();
	data->delete_originals = delete_originals;

	for (ii = 0; ii < uids->len; ii++)
		g_ptr_array_add (data->uids, g_strdup (uids->pdata[ii]));

	job = camel_imapx_job_new (cancellable);
	job->pri = IMAPX_PRIORITY_APPEND_MESSAGE;
	job->type = IMAPX_JOB_COPY_MESSAGE;
	job->start = imapx_job_copy_messages_start;
	job->folder = g_object_ref (source);

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) copy_messages_data_free);

	return imapx_submit_job (is, job, error);
}

gboolean
camel_imapx_server_append_message (CamelIMAPXServer *is,
                                   CamelFolder *folder,
                                   CamelMimeMessage *message,
                                   const CamelMessageInfo *mi,
                                   gchar **appended_uid,
                                   GCancellable *cancellable,
                                   GError **error)
{
	gchar *uid = NULL, *path = NULL;
	CamelStream *stream, *filter;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
	CamelMimeFilter *canon;
	CamelIMAPXJob *job;
	CamelMessageInfo *info;
	AppendMessageData *data;
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

	path = camel_data_cache_get_filename (ifolder->cache, "new", uid);
	info = camel_folder_summary_info_new_from_message ((CamelFolderSummary *) folder->summary, message, NULL);
	info->uid = camel_pstring_strdup (uid);
	if (mi) {
		CamelMessageInfoBase *base_info = (CamelMessageInfoBase *) info;

		base_info->flags = camel_message_info_flags (mi);
		base_info->size = camel_message_info_size (mi);

		if ((is->permanentflags & CAMEL_MESSAGE_USER) != 0) {
			const CamelFlag *flag;
			const CamelTag *tag;

			flag = camel_message_info_user_flags (mi);
			while (flag) {
				if (flag->name && *flag->name)
					camel_flag_set (&base_info->user_flags, flag->name, TRUE);
				flag = flag->next;
			}

			tag = camel_message_info_user_tags (mi);
			while (tag) {
				if (tag->name && *tag->name)
					camel_tag_set (&base_info->user_tags, tag->name, tag->value);
				tag = tag->next;
			}
		}
	}

	g_free (uid);

	/* So, we actually just want to let the server loop that
	 * messages need appending, i think.  This is so the same
	 * mechanism is used for normal uploading as well as
	 * offline re-syncing when we go back online */

	data = g_slice_new0 (AppendMessageData);
	data->info = info;  /* takes ownership */
	data->path = path;  /* takes ownership */
	data->appended_uid = NULL;

	job = camel_imapx_job_new (cancellable);
	job->pri = IMAPX_PRIORITY_APPEND_MESSAGE;
	job->type = IMAPX_JOB_APPEND_MESSAGE;
	job->start = imapx_job_append_message_start;
	job->folder = g_object_ref (folder);
	job->noreply = FALSE;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) append_message_data_free);

	success = imapx_submit_job (is, job, error);

	if (appended_uid) {
		*appended_uid = data->appended_uid;
		data->appended_uid = NULL;
	}

	camel_imapx_job_unref (job);

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

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_NOOP;
	job->start = imapx_job_noop_start;
	job->folder = folder;
	job->pri = IMAPX_PRIORITY_NOOP;

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_refresh_info (CamelIMAPXServer *is,
                                 CamelFolder *folder,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelIMAPXJob *job;
	RefreshInfoData *data;
	gboolean registered = TRUE;
	const gchar *full_name;
	gboolean success = TRUE;

	full_name = camel_folder_get_full_name (folder);

	QUEUE_LOCK (is);

	/* Both RefreshInfo and Fetch messages can't operate simultaneously */
	if (imapx_is_job_in_queue (is, folder, IMAPX_JOB_REFRESH_INFO, NULL) ||
		imapx_is_job_in_queue (is, folder, IMAPX_JOB_FETCH_MESSAGES, NULL)) {
		QUEUE_UNLOCK (is);
		return TRUE;
	}

	data = g_slice_new0 (RefreshInfoData);
	data->changes = camel_folder_change_info_new ();

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_REFRESH_INFO;
	job->start = imapx_job_refresh_info_start;
	job->matches = imapx_job_refresh_info_matches;
	job->folder = folder;
	job->pri = IMAPX_PRIORITY_REFRESH_INFO;

	if (g_ascii_strcasecmp (full_name, "INBOX") == 0)
		job->pri += 10;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) refresh_info_data_free);

	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	success = registered && camel_imapx_job_run (job, is, error);

	if (success && camel_folder_change_info_changed (data->changes))
		camel_folder_changed (folder, data->changes);

	camel_imapx_job_unref (job);

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
	SyncChangesData *data;
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
				else if (*uflags->name)
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
		imapx_sync_free_user (on_user);
		imapx_sync_free_user (off_user);
		camel_folder_free_uids (folder, uids);

		return TRUE;
	}

	/* TODO above code should go into changes_start */

	QUEUE_LOCK (is);

	if ((job = imapx_is_job_in_queue (is, folder, IMAPX_JOB_SYNC_CHANGES, NULL))) {
		if (pri > job->pri)
			job->pri = pri;

		QUEUE_UNLOCK (is);

		imapx_sync_free_user (on_user);
		imapx_sync_free_user (off_user);
		camel_folder_free_uids (folder, uids);

		return TRUE;
	}

	data = g_slice_new0 (SyncChangesData);
	data->folder = g_object_ref (folder);
	data->changed_uids = uids;  /* takes ownership */
	data->on_set = on_orset;
	data->off_set = off_orset;
	data->on_user = on_user;  /* takes ownership */
	data->off_user = off_user;  /* takes ownership */

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_SYNC_CHANGES;
	job->start = imapx_job_sync_changes_start;
	job->matches = imapx_job_sync_changes_matches;
	job->pri = pri;
	job->folder = folder;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) sync_changes_data_free);

	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	success = registered && camel_imapx_job_run (job, is, error);

	camel_imapx_job_unref (job);

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

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_EXPUNGE;
	job->start = imapx_job_expunge_start;
	job->matches = imapx_job_expunge_matches;
	job->pri = IMAPX_PRIORITY_EXPUNGE;
	job->folder = folder;

	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	success = registered && camel_imapx_job_run (job, is, error);

	camel_imapx_job_unref (job);

	return success;
}

static guint
imapx_name_hash (gconstpointer key)
{
	if (g_ascii_strcasecmp (key, "INBOX") == 0)
		return g_str_hash ("INBOX");
	else
		return g_str_hash (key);
}

static gint
imapx_name_equal (gconstpointer a,
                  gconstpointer b)
{
	gconstpointer aname = a, bname = b;

	if (g_ascii_strcasecmp (a, "INBOX") == 0)
		aname = "INBOX";
	if (g_ascii_strcasecmp (b, "INBOX") == 0)
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
	ListData *data;
	gchar *encoded_name;

	encoded_name = camel_utf8_utf7 (top);

	data = g_slice_new0 (ListData);
	data->flags = flags;
	data->ext = g_strdup (ext);
	data->folders = g_hash_table_new (imapx_name_hash, imapx_name_equal);

	if (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE)
		data->pattern = g_strdup_printf ("%s*", encoded_name);
	else
		data->pattern = g_strdup (encoded_name);

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_LIST;
	job->start = imapx_job_list_start;
	job->matches = imapx_job_list_matches;
	job->pri = IMAPX_PRIORITY_LIST;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) list_data_free);

	/* sync operation which is triggered by user */
	if (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST)
		job->pri += 300;

	if (imapx_submit_job (is, job, error)) {
		folders = g_ptr_array_new ();
		g_hash_table_foreach (data->folders, imapx_list_flatten, folders);
		qsort (folders->pdata, folders->len, sizeof (folders->pdata[0]), imapx_list_cmp);
	}

	g_free (encoded_name);
	camel_imapx_job_unref (job);

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
	ManageSubscriptionsData *data;
	gboolean success;

	data = g_slice_new0 (ManageSubscriptionsData);
	data->folder_name = g_strdup (folder_name);
	data->subscribe = subscribe;

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_MANAGE_SUBSCRIPTION;
	job->start = imapx_job_manage_subscription_start;
	job->pri = IMAPX_PRIORITY_MANAGE_SUBSCRIPTION;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) manage_subscriptions_data_free);

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_create_folder (CamelIMAPXServer *is,
                                  const gchar *folder_name,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelIMAPXJob *job;
	CreateFolderData *data;
	gboolean success;

	data = g_slice_new0 (CreateFolderData);
	data->folder_name = g_strdup (folder_name);

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_CREATE_FOLDER;
	job->start = imapx_job_create_folder_start;
	job->pri = IMAPX_PRIORITY_CREATE_FOLDER;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) create_folder_data_free);

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_delete_folder (CamelIMAPXServer *is,
                                  const gchar *folder_name,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelIMAPXJob *job;
	DeleteFolderData *data;
	gboolean success;

	data = g_slice_new0 (DeleteFolderData);
	data->folder_name = g_strdup (folder_name);

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_DELETE_FOLDER;
	job->start = imapx_job_delete_folder_start;
	job->pri = IMAPX_PRIORITY_DELETE_FOLDER;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) delete_folder_data_free);

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

	return success;
}

static gboolean
imapx_job_fetch_messages_matches (CamelIMAPXJob *job,
                                      CamelFolder *folder,
                                      const gchar *uid)
{
	return (folder == job->folder);
}

gboolean
camel_imapx_server_fetch_messages (CamelIMAPXServer *is,
                                   CamelFolder *folder,
                                   CamelFetchType type,
                                   gint limit,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelIMAPXJob *job;
	RefreshInfoData *data;
	gboolean registered = TRUE;
	const gchar *full_name;
	gboolean success = TRUE;
	guint64 firstuid, newfirstuid;
	gchar *uid;
	gint old_len;

	old_len = camel_folder_summary_count (folder->summary);
	uid = imapx_get_uid_from_index (folder->summary, 0);
	firstuid = strtoull (uid, NULL, 10);
	g_free (uid);

	QUEUE_LOCK (is);

	/* Both RefreshInfo and Fetch messages can't operate simultaneously */
	if (imapx_is_job_in_queue (is, folder, IMAPX_JOB_REFRESH_INFO, NULL) ||
		imapx_is_job_in_queue (is, folder, IMAPX_JOB_FETCH_MESSAGES, NULL)) {
		QUEUE_UNLOCK (is);
		return TRUE;
	}

	data = g_slice_new0 (RefreshInfoData);
	data->changes = camel_folder_change_info_new ();
	data->fetch_msg_limit = limit;
	data->fetch_type = type;

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_FETCH_MESSAGES;
	job->start = imapx_job_fetch_messages_start;
	job->matches = imapx_job_fetch_messages_matches;
	job->folder = folder;
	job->pri = IMAPX_PRIORITY_NEW_MESSAGES;

	full_name = camel_folder_get_full_name (folder);

	if (g_ascii_strcasecmp (full_name, "INBOX") == 0)
		job->pri += 10;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) refresh_info_data_free);

	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	success = registered && camel_imapx_job_run (job, is, error);

	if (success && camel_folder_change_info_changed (data->changes) && camel_folder_change_info_changed (data->changes))
		camel_folder_changed (folder, data->changes);

	uid = imapx_get_uid_from_index (folder->summary, 0);
	newfirstuid = strtoull (uid, NULL, 10);
	g_free (uid);

	camel_imapx_job_unref (job);

	if (type == CAMEL_FETCH_OLD_MESSAGES && firstuid == newfirstuid)
		return FALSE; /* No more old messages */
	else if (type == CAMEL_FETCH_NEW_MESSAGES &&
			old_len == camel_folder_summary_count (folder->summary))
		return FALSE; /* No more new messages */

	return TRUE;
}

gboolean
camel_imapx_server_rename_folder (CamelIMAPXServer *is,
                                  const gchar *old_name,
                                  const gchar *new_name,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelIMAPXJob *job;
	RenameFolderData *data;
	gboolean success;

	data = g_slice_new0 (RenameFolderData);
	data->old_folder_name = g_strdup (old_name);
	data->new_folder_name = g_strdup (new_name);

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_RENAME_FOLDER;
	job->start = imapx_job_rename_folder_start;
	job->pri = IMAPX_PRIORITY_RENAME_FOLDER;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) rename_folder_data_free);

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

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

/**
 * camel_imapx_server_register_untagged_handler:
 * @is: a #CamelIMAPXServer instance
 * @untagged_response: a string representation of the IMAP
 *                     untagged response code. Must be
 *                     all-uppercase with underscores allowed
 *                     (see RFC 3501)
 * @desc: a #CamelIMAPXUntaggedRespHandlerDesc handler description
 *        structure. The descriptor structure is expected to
 *        remain stable over the lifetime of the #CamelIMAPXServer
 *        instance it was registered with. It is the responsibility
 *        of the caller to ensure this
 *
 * Register a new handler function for IMAP untagged responses.
 * Pass in a NULL descriptor to delete an existing handler (the
 * untagged response will remain known, but will no longer be acted
 * upon if the handler is deleted). The return value is intended
 * to be used in cases where e.g. an extension to existing handler
 * code is implemented with just some new code to be run before
 * or after the original handler code
 *
 * Returns: the #CamelIMAPXUntaggedRespHandlerDesc previously
 *          registered for this untagged response, if any,
 *          NULL otherwise.
 *
 * Since: 3.6
 */
const CamelIMAPXUntaggedRespHandlerDesc *
camel_imapx_server_register_untagged_handler (CamelIMAPXServer *is,
                                              const gchar *untagged_response,
                                              const CamelIMAPXUntaggedRespHandlerDesc *desc)
{
	const CamelIMAPXUntaggedRespHandlerDesc *previous = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), NULL);
	g_return_val_if_fail (untagged_response != NULL, NULL);
	/* desc may be NULL */

	previous = replace_untagged_descriptor (
		is->priv->untagged_handlers,
		untagged_response, desc);

	return previous;
}

gboolean
camel_imapx_server_command_run (CamelIMAPXServer *is,
                                CamelIMAPXCommand *ic,
                                GCancellable *cancellable,
                                GError **error)
{
	gboolean ok = FALSE;
	CamelIMAPXJob *job = NULL;
	gboolean local_job = FALSE;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_COMMAND (ic), FALSE);
	/* cancellable may be NULL */
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	job = camel_imapx_command_get_job (ic);
	if (job == NULL) {
		job = camel_imapx_job_new (cancellable);
		camel_imapx_command_set_job (ic, job);
		local_job = TRUE;
	}

	ok = imapx_command_run_sync (is, ic, cancellable, error);

	if (local_job)
		camel_imapx_command_set_job (ic, NULL);

	return ok;
}

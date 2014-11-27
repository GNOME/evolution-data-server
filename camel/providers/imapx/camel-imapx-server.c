/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or modify it
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* XXX Disable deprecation warnings until we require GLib 2.40.
 *
 *     This silences warnings for using GSubprocess functions, which are
 *     only available as of GLib 2.39.  But we do so conditionally, with
 *     GLIB_CHECK_VERSION macros. */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include <gio/gnetworking.h>

#ifndef G_OS_WIN32
#include <glib-unix.h>
#endif /* G_OS_WIN32 */

#include "camel-imapx-server.h"

#include "camel-imapx-folder.h"
#include "camel-imapx-input-stream.h"
#include "camel-imapx-job.h"
#include "camel-imapx-logger.h"
#include "camel-imapx-settings.h"
#include "camel-imapx-store.h"
#include "camel-imapx-summary.h"
#include "camel-imapx-utils.h"

#define CAMEL_IMAPX_SERVER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_IMAPX_SERVER, CamelIMAPXServerPrivate))

#define c(...) camel_imapx_debug(command, __VA_ARGS__)
#define e(...) camel_imapx_debug(extra, __VA_ARGS__)

#define QUEUE_LOCK(x) (g_rec_mutex_lock(&(x)->queue_lock))
#define QUEUE_UNLOCK(x) (g_rec_mutex_unlock(&(x)->queue_lock))

/* Try pipelining fetch requests, 'in bits' */
#define MULTI_SIZE (32768 * 8)

/* How many outstanding commands do we allow before we just queue them? */
#define MAX_COMMANDS (10)

#define MAX_COMMAND_LEN 1000

/* Ping the server after a period of inactivity to avoid being logged off.
 * Using a 29 minute inactivity timeout as recommended in RFC 2177 (IDLE). */
#define INACTIVITY_TIMEOUT_SECONDS (29 * 60)

#ifdef G_OS_WIN32
#ifdef gmtime_r
#undef gmtime_r
#endif

/* The gmtime() in Microsoft's C library is MT-safe */
#define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)
#endif

G_DEFINE_QUARK (camel-imapx-server-error-quark, camel_imapx_server_error)

extern gint camel_application_is_exiting;

/* Job-specific structs */
typedef struct _GetMessageData GetMessageData;
typedef struct _RefreshInfoData RefreshInfoData;
typedef struct _SyncChangesData SyncChangesData;
typedef struct _AppendMessageData AppendMessageData;
typedef struct _CopyMessagesData CopyMessagesData;
typedef struct _ListData ListData;
typedef struct _MailboxData MailboxData;
typedef struct _SearchData SearchData;

struct _GetMessageData {
	/* in: uid requested */
	gchar *uid;
	CamelDataCache *message_cache;
	/* in/out: message content stream output */
	GIOStream *stream;
	/* working variables */
	gsize body_offset;
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
	gboolean own_allocated_changed_uids;
	guint32 on_set;
	guint32 off_set;
	GArray *on_user; /* imapx_flag_change */
	GArray *off_user;
	gint unread_change;

	/* Remove recently set DELETED flags before synchronizing.
	 * This is only set when using a real Trash folder and NOT
	 * about to expunge the folder. */
	gboolean remove_deleted_flags;
};

struct _AppendMessageData {
	gchar *path;
	CamelMessageInfo *info;
	gchar *appended_uid;
	time_t date_time; /* message's date/time, in UTC */
};

struct _CopyMessagesData {
	CamelIMAPXMailbox *destination;
	GPtrArray *uids;
	gboolean delete_originals;
	gboolean use_move_command;
	gint index;
	gint last_index;
	struct _uidset_state uidset;
};

struct _ListData {
	gchar *pattern;
};

struct _MailboxData {
	CamelIMAPXMailbox *mailbox;
	gchar *mailbox_name;
};

struct _SearchData {
	gchar *criteria;
	GArray *results;
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
	gulong id;
	guint len;
	guchar *token;
	gint tok;
	gboolean lsub;
	struct _status_info *sinfo;
};

/* internal untagged handler prototypes */
static gboolean	imapx_untagged_bye		(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_capability	(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_exists		(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_expunge		(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_fetch		(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_flags		(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_list		(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_lsub		(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_namespace	(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_ok_no_bad	(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_preauth		(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_quota		(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_quotaroot	(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_recent		(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_search		(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_status		(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imapx_untagged_vanished		(CamelIMAPXServer *is,
						 GInputStream *input_stream,
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
	IMAPX_UNTAGGED_ID_QUOTA,
	IMAPX_UNTAGGED_ID_QUOTAROOT,
	IMAPX_UNTAGGED_ID_RECENT,
	IMAPX_UNTAGGED_ID_SEARCH,
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
	{CAMEL_IMAPX_UNTAGGED_LSUB, imapx_untagged_lsub, NULL, TRUE},
	{CAMEL_IMAPX_UNTAGGED_NAMESPACE, imapx_untagged_namespace, NULL, FALSE},
	{CAMEL_IMAPX_UNTAGGED_NO, imapx_untagged_ok_no_bad, NULL, FALSE},
	{CAMEL_IMAPX_UNTAGGED_OK, imapx_untagged_ok_no_bad, NULL, FALSE},
	{CAMEL_IMAPX_UNTAGGED_PREAUTH, imapx_untagged_preauth, CAMEL_IMAPX_UNTAGGED_OK, TRUE /*overridden */ },
	{CAMEL_IMAPX_UNTAGGED_QUOTA, imapx_untagged_quota, NULL, FALSE},
	{CAMEL_IMAPX_UNTAGGED_QUOTAROOT, imapx_untagged_quotaroot, NULL, FALSE},
	{CAMEL_IMAPX_UNTAGGED_RECENT, imapx_untagged_recent, NULL, TRUE},
	{CAMEL_IMAPX_UNTAGGED_SEARCH, imapx_untagged_search, NULL, FALSE},
	{CAMEL_IMAPX_UNTAGGED_STATUS, imapx_untagged_status, NULL, TRUE},
	{CAMEL_IMAPX_UNTAGGED_VANISHED, imapx_untagged_vanished, NULL, TRUE},
};

typedef enum {
	IMAPX_IDLE_OFF,
	IMAPX_IDLE_PENDING,	/* Queue is idle; waiting to send IDLE command
				   soon if nothing more interesting happens */
	IMAPX_IDLE_ISSUED,	/* Sent IDLE command; waiting for response */
	IMAPX_IDLE_STARTED,	/* IDLE continuation received; IDLE active */
	IMAPX_IDLE_CANCEL,	/* Cancelled from ISSUED state; need to send
				   DONE as soon as we receive continuation */
	IMAPX_IDLE_WAIT_DONE	/* DONE was issued, waiting for a confirmation response */
} CamelIMAPXIdleState;

#define IMAPX_IDLE_DWELL_TIME	2 /* Number of seconds to remain in PENDING
				     state waiting for other commands to be
				     queued, before actually sending IDLE */

typedef enum {
	IMAPX_IDLE_STOP_NOOP,
	IMAPX_IDLE_STOP_SUCCESS,
	IMAPX_IDLE_STOP_ERROR
} CamelIMAPXIdleStopResult;

struct _CamelIMAPXServerPrivate {
	GWeakRef store;

	CamelIMAPXServerUntaggedContext *context;
	GHashTable *untagged_handlers;

	/* The 'stream_lock' also guards the GSubprocess. */
	GInputStream *input_stream;
	GOutputStream *output_stream;
	GIOStream *connection;
#if GLIB_CHECK_VERSION(2,39,0)
	GSubprocess *subprocess;
#endif
	GMutex stream_lock;

	GThread *parser_thread;
	GMainLoop *parser_main_loop;
	GMainContext *parser_main_context;
	GWeakRef parser_cancellable;

	GMutex shutdown_error_lock;
	GError *shutdown_error;

	GSource *inactivity_timeout;
	GMutex inactivity_timeout_lock;

	/* Info on currently selected folder. */
	GMutex select_lock;
	GWeakRef select_mailbox;
	GWeakRef select_closing;
	GWeakRef select_pending;
	CamelFolderChangeInfo *changes;

	/* Data items to request in STATUS commands:
	 * STATUS $mailbox_name ($status_data_items) */
	gchar *status_data_items;

	/* Return options for extended LIST commands:
	 * LIST "" $pattern RETURN ($list_return_opts) */
	gchar *list_return_opts;

	/* Untagged SEARCH data gets deposited here.
	 * The search command should claim the results
	 * when finished and reset the pointer to NULL. */
	GArray *search_results;
	GMutex search_results_lock;

	GHashTable *known_alerts;
	GMutex known_alerts_lock;

	/* INBOX separator character, so we can correctly normalize
	 * INBOX and descendants of INBOX in IMAP responses that do
	 * not include a separator character with the mailbox name,
	 * such as STATUS.  Used for camel_imapx_parse_mailbox(). */
	gchar inbox_separator;

	/* IDLE support */
	GRecMutex idle_lock;
	GThread *idle_thread;
	GMainLoop *idle_main_loop;
	GMainContext *idle_main_context;
	GSource *idle_pending;
	CamelIMAPXIdleState idle_state;

	GMutex jobs_prop_lock;
	GHashTable *jobs_prop_folder_paths;
	gint jobs_prop_command_count; /* without IDLE command */
	gint jobs_prop_expensive_command_count;
};

enum {
	PROP_0,
	PROP_STORE
};

enum {
	MAILBOX_SELECT,
	MAILBOX_CLOSED,
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
						 GError **error);
static gboolean	imapx_continuation		(CamelIMAPXServer *is,
						 GInputStream *input_stream,
						 GOutputStream *output_stream,
						 gboolean litplus,
						 GCancellable *cancellable,
						 GError **error);
static void	imapx_disconnect		(CamelIMAPXServer *is);
static gboolean	imapx_is_command_queue_empty	(CamelIMAPXServer *is);
static gint	imapx_uid_cmp			(gconstpointer ap,
						 gconstpointer bp,
						 gpointer data);
static void	imapx_command_start_next	(CamelIMAPXServer *is);

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
	IMAPX_JOB_CREATE_MAILBOX = 1 << 10,
	IMAPX_JOB_DELETE_MAILBOX = 1 << 11,
	IMAPX_JOB_RENAME_MAILBOX = 1 << 12,
	IMAPX_JOB_SUBSCRIBE_MAILBOX = 1 << 13,
	IMAPX_JOB_UNSUBSCRIBE_MAILBOX = 1 << 14,
	IMAPX_JOB_UPDATE_QUOTA_INFO = 1 << 15,
	IMAPX_JOB_UID_SEARCH = 1 << 16
};

/* Mailbox management operations have highest priority
 * since we know for sure that they are user triggered. */
enum {
	IMAPX_PRIORITY_MAILBOX_MGMT = 200,
	IMAPX_PRIORITY_SYNC_CHANGES = 150,
	IMAPX_PRIORITY_EXPUNGE = 150,
	IMAPX_PRIORITY_SEARCH = 150,
	IMAPX_PRIORITY_GET_MESSAGE = 100,
	IMAPX_PRIORITY_REFRESH_INFO = 0,
	IMAPX_PRIORITY_NOOP = 0,
	IMAPX_PRIORITY_NEW_MESSAGES = 0,
	IMAPX_PRIORITY_APPEND_MESSAGE = -60,
	IMAPX_PRIORITY_COPY_MESSAGE = -60,
	IMAPX_PRIORITY_LIST = -80,
	IMAPX_PRIORITY_IDLE = -100,
	IMAPX_PRIORITY_SYNC_MESSAGE = -120,
	IMAPX_PRIORITY_UPDATE_QUOTA_INFO = -80
};

struct _imapx_flag_change {
	GPtrArray *infos;
	gchar *name;
};

static CamelIMAPXJob *
		imapx_match_active_job		(CamelIMAPXServer *is,
						 guint32 type,
						 const gchar *uid);
static gboolean	imapx_job_fetch_new_messages_start
						(CamelIMAPXJob *job,
						 CamelIMAPXServer *is,
						 GCancellable *cancellable,
						 GError **error);
static gint	imapx_refresh_info_uid_cmp	(gconstpointer ap,
						 gconstpointer bp,
						 gboolean ascending);
static gint	imapx_uids_array_cmp		(gconstpointer ap,
						 gconstpointer bp);
static gboolean	imapx_server_sync_changes	(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 guint32 job_type,
						 gint pri,
						 GCancellable *cancellable,
						 GError **error);
static void	imapx_sync_free_user		(GArray *user_set);

static gboolean	imapx_command_copy_messages_step_start
						(CamelIMAPXServer *is,
						 CamelIMAPXJob *job,
						 gint index,
						 GError **error);
static gboolean	imapx_job_noop_start		(CamelIMAPXJob *job,
						 CamelIMAPXServer *is,
						 GCancellable *cancellable,
						 GError **error);

static gboolean	imapx_in_idle			(CamelIMAPXServer *is);
static gboolean	imapx_use_idle		(CamelIMAPXServer *is);
static void	imapx_start_idle		(CamelIMAPXServer *is);
static CamelIMAPXIdleStopResult
		imapx_stop_idle			(CamelIMAPXServer *is,
						 GError **error);
static gboolean	camel_imapx_server_idle		(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 GCancellable *cancellable,
						 GError **error);

static void	imapx_maybe_select		(CamelIMAPXServer *is,
						 CamelIMAPXJob *job,
						 CamelIMAPXMailbox *mailbox);

G_DEFINE_TYPE (CamelIMAPXServer, camel_imapx_server, G_TYPE_OBJECT)

static GWeakRef *
imapx_weak_ref_new (gpointer object)
{
	GWeakRef *weak_ref;

	/* XXX Might want to expose this in Camel's public API if it
	 *     proves useful elsewhere.  Based on e_weak_ref_new(). */

	weak_ref = g_slice_new0 (GWeakRef);
	g_weak_ref_init (weak_ref, object);

	return weak_ref;
}

static void
imapx_weak_ref_free (GWeakRef *weak_ref)
{
	g_return_if_fail (weak_ref != NULL);

	/* XXX Might want to expose this in Camel's public API if it
	 *     proves useful elsewhere.  Based on e_weak_ref_free(). */

	g_weak_ref_clear (weak_ref);
	g_slice_free (GWeakRef, weak_ref);
}

static void
imapx_server_set_shutdown_error (CamelIMAPXServer *imapx_server,
				 const GError *error)
{
	g_mutex_lock (&imapx_server->priv->shutdown_error_lock);

	if (error != imapx_server->priv->shutdown_error) {
		g_clear_error (&imapx_server->priv->shutdown_error);
		if (error)
			imapx_server->priv->shutdown_error = g_error_copy (error);
	}

	g_mutex_unlock (&imapx_server->priv->shutdown_error_lock);
}

static GError *
imapx_server_dup_shutdown_error (CamelIMAPXServer *imapx_server)
{
	GError *error = NULL;

	g_mutex_lock (&imapx_server->priv->shutdown_error_lock);

	if (imapx_server->priv->shutdown_error)
		error = g_error_copy (imapx_server->priv->shutdown_error);

	g_mutex_unlock (&imapx_server->priv->shutdown_error_lock);

	return error;
}

static void
imapx_server_command_added (CamelIMAPXServer *imapx_server,
			    CamelIMAPXCommand *command)
{
	CamelIMAPXJob *job;

	g_return_if_fail (command != NULL);

	g_mutex_lock (&imapx_server->priv->jobs_prop_lock);

	job = camel_imapx_command_get_job (command);

	if (job) {
		/* without IDLE commands */
		if (!(job->type & IMAPX_JOB_IDLE))
			imapx_server->priv->jobs_prop_command_count++;

		if ((job->type & (IMAPX_JOB_FETCH_NEW_MESSAGES | IMAPX_JOB_REFRESH_INFO)) != 0)
			imapx_server->priv->jobs_prop_expensive_command_count++;
	}

	g_mutex_unlock (&imapx_server->priv->jobs_prop_lock);
}

static void
imapx_server_command_removed (CamelIMAPXServer *imapx_server,
			      CamelIMAPXCommand *command)
{
	CamelIMAPXJob *job;

	g_return_if_fail (command != NULL);

	g_mutex_lock (&imapx_server->priv->jobs_prop_lock);

	job = camel_imapx_command_get_job (command);

	if (job) {
		/* without IDLE commands */
		if (!(job->type & IMAPX_JOB_IDLE)) {
			imapx_server->priv->jobs_prop_command_count--;
			g_warn_if_fail (imapx_server->priv->jobs_prop_command_count >= 0);
		}

		if ((job->type & (IMAPX_JOB_FETCH_NEW_MESSAGES | IMAPX_JOB_REFRESH_INFO)) != 0) {
			imapx_server->priv->jobs_prop_expensive_command_count--;
			g_warn_if_fail (imapx_server->priv->jobs_prop_expensive_command_count >= 0);
		}
	}

	g_mutex_unlock (&imapx_server->priv->jobs_prop_lock);
}

static void
imapx_server_add_job_mailbox (CamelIMAPXServer *imapx_server,
			      CamelIMAPXMailbox *mailbox)
{
	gchar *folder_path;
	gint n_stored;

	g_return_if_fail (mailbox != NULL);

	g_mutex_lock (&imapx_server->priv->jobs_prop_lock);

	folder_path = camel_imapx_mailbox_dup_folder_path (mailbox);

	n_stored = GPOINTER_TO_INT (g_hash_table_lookup (imapx_server->priv->jobs_prop_folder_paths, folder_path));
	/* takes ownership of folder_path */
	g_hash_table_insert (imapx_server->priv->jobs_prop_folder_paths, folder_path, GINT_TO_POINTER (n_stored + 1));

	g_mutex_unlock (&imapx_server->priv->jobs_prop_lock);
}

static void
imapx_server_remove_job_mailbox (CamelIMAPXServer *imapx_server,
				 CamelIMAPXMailbox *mailbox)
{
	gchar *folder_path;
	gint n_stored;

	g_return_if_fail (mailbox != NULL);

	g_mutex_lock (&imapx_server->priv->jobs_prop_lock);

	folder_path = camel_imapx_mailbox_dup_folder_path (mailbox);

	n_stored = GPOINTER_TO_INT (g_hash_table_lookup (imapx_server->priv->jobs_prop_folder_paths, folder_path));
	if (!camel_imapx_mailbox_is_inbox (camel_imapx_mailbox_get_name (mailbox)))
		g_warn_if_fail (n_stored >= 1);

	n_stored--;
	if (n_stored > 0) {
		/* takes ownership of folder_path */
		g_hash_table_insert (imapx_server->priv->jobs_prop_folder_paths, folder_path, GINT_TO_POINTER (n_stored));
	} else {
		g_hash_table_remove (imapx_server->priv->jobs_prop_folder_paths, folder_path);
		g_free (folder_path);
	}

	g_mutex_unlock (&imapx_server->priv->jobs_prop_lock);
}

static void
imapx_server_job_added (CamelIMAPXServer *imapx_server,
			CamelIMAPXJob *job)
{
	CamelIMAPXMailbox *mailbox;

	g_return_if_fail (job != NULL);

	mailbox = camel_imapx_job_ref_mailbox (job);

	if (mailbox != NULL) {
		imapx_server_add_job_mailbox (imapx_server, mailbox);
		g_object_unref (mailbox);
	}
}

static void
imapx_server_job_removed (CamelIMAPXServer *imapx_server,
			  CamelIMAPXJob *job)
{
	CamelIMAPXMailbox *mailbox;

	g_return_if_fail (job != NULL);

	mailbox = camel_imapx_job_ref_mailbox (job);

	if (mailbox != NULL) {
		imapx_server_remove_job_mailbox (imapx_server, mailbox);
		g_object_unref (mailbox);
	}
}

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
	const CamelIMAPXUntaggedRespHandlerDesc *cur = NULL;

	g_return_if_fail (untagged_handlers != NULL);
	g_return_if_fail (untagged_id < IMAPX_UNTAGGED_LAST_ID);

	cur = &(_untagged_descr[untagged_id]);
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
		camel_strcase_hash,
		camel_strcase_equal,
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

	g_clear_object (&data->message_cache);
	g_clear_object (&data->stream);

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
	if (data->changes != NULL)
		camel_folder_change_info_free (data->changes);

	refresh_info_data_infos_free (data);

	g_slice_free (RefreshInfoData, data);
}

static void
sync_changes_data_free (SyncChangesData *data)
{
	if (data->folder != NULL) {
		if (!data->own_allocated_changed_uids)
			camel_folder_free_uids (data->folder, data->changed_uids);
		g_object_unref (data->folder);
	}

	if (data->own_allocated_changed_uids && data->changed_uids) {
		g_ptr_array_foreach (data->changed_uids, (GFunc) camel_pstring_free, NULL);
		g_ptr_array_free (data->changed_uids, TRUE);
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

	camel_message_info_unref (data->info);

	g_slice_free (AppendMessageData, data);
}

static void
copy_messages_data_free (CopyMessagesData *data)
{
	g_clear_object (&data->destination);

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

	g_slice_free (ListData, data);
}

static void
mailbox_data_free (MailboxData *data)
{
	g_clear_object (&data->mailbox);
	g_free (data->mailbox_name);

	g_slice_free (MailboxData, data);
}

static void
search_data_free (SearchData *data)
{
	g_free (data->criteria);

	if (data->results != NULL)
		g_array_unref (data->results);

	g_slice_free (SearchData, data);
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

static gboolean
imapx_register_job (CamelIMAPXServer *is,
                    CamelIMAPXJob *job,
                    GError **error)
{
	if (is->state >= IMAPX_INITIALISED) {
		QUEUE_LOCK (is);
		g_queue_push_head (&is->jobs, camel_imapx_job_ref (job));
		imapx_server_job_added (is, job);
		QUEUE_UNLOCK (is);

	} else if (is->state <= IMAPX_SHUTDOWN) {
		e (is->tagprefix, "Server is shutdown/disconnected, try reconnect.");
		g_set_error (error,
			CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT,
			_("Not authenticated"));
		return FALSE;
	} else {
		e (is->tagprefix, "Not connected yet, maybe user cancelled jobs earlier?");
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

	if (g_queue_remove (&is->jobs, job)) {
		imapx_server_job_removed (is, job);
		camel_imapx_job_unref (job);
	}

	imapx_command_start_next (is);

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

static CamelFolder *
imapx_server_ref_folder (CamelIMAPXServer *is,
                         CamelIMAPXMailbox *mailbox)
{
	CamelFolder *folder;
	CamelIMAPXStore *store;
	gchar *folder_path;
	GError *local_error = NULL;

	store = camel_imapx_server_ref_store (is);

	folder_path = camel_imapx_mailbox_dup_folder_path (mailbox);

	folder = camel_store_get_folder_sync (
		CAMEL_STORE (store), folder_path, 0, NULL, &local_error);

	g_free (folder_path);

	g_object_unref (store);

	/* Sanity check. */
	g_warn_if_fail (
		((folder != NULL) && (local_error == NULL)) ||
		((folder == NULL) && (local_error != NULL)));

	if (local_error != NULL) {
		g_warning (
			"%s: Failed to get folder for '%s': %s",
			G_STRFUNC, camel_imapx_mailbox_get_name (mailbox), local_error->message);
		g_error_free (local_error);
	}

	return folder;
}

static void
imapx_server_stash_command_arguments (CamelIMAPXServer *is)
{
	GString *buffer;

	/* Stash some reusable capability-based command arguments. */

	buffer = g_string_new ("MESSAGES UNSEEN UIDVALIDITY UIDNEXT");
	if (CAMEL_IMAPX_HAVE_CAPABILITY (is->cinfo, CONDSTORE))
		g_string_append (buffer, " HIGHESTMODSEQ");
	g_free (is->priv->status_data_items);
	is->priv->status_data_items = g_string_free (buffer, FALSE);

	g_free (is->priv->list_return_opts);
	if (CAMEL_IMAPX_HAVE_CAPABILITY (is->cinfo, LIST_EXTENDED)) {
		buffer = g_string_new ("CHILDREN SUBSCRIBED");
		if (CAMEL_IMAPX_HAVE_CAPABILITY (is->cinfo, LIST_STATUS))
			g_string_append_printf (
				buffer, " STATUS (%s)",
				is->priv->status_data_items);
		if (CAMEL_IMAPX_HAVE_CAPABILITY (is->cinfo, SPECIAL_USE))
			g_string_append_printf (buffer, " SPECIAL-USE");
		is->priv->list_return_opts = g_string_free (buffer, FALSE);
	} else {
		is->priv->list_return_opts = NULL;
	}
}

static gboolean
imapx_server_inactivity_timeout_cb (gpointer data)
{
	CamelIMAPXServer *is;
	gboolean result = G_SOURCE_REMOVE;

	is = g_weak_ref_get (data);

	if (is == NULL)
		return result;

	/* IDLE command may still be active, and any other active
	 * commands would have reset this timeout.  So just check
	 * for any queued-but-not-yet-active commands. */

	if (!camel_imapx_command_queue_is_empty (is->queue)) {
		/* Do nothing. */

	} else if (imapx_in_idle (is)) {

		/* Stop and restart the IDLE command. */
		switch (imapx_stop_idle (is, NULL)) {
			case IMAPX_IDLE_STOP_SUCCESS:
				imapx_start_idle (is);
				/* fall through */

			case IMAPX_IDLE_STOP_NOOP:
				result = G_SOURCE_CONTINUE;
				break;

			default:
				break;
		}

	} else {
		CamelIMAPXJob *job;
		GCancellable *cancellable;
		GError *local_error = NULL;

		/* Submit a NOOP job but indicate we don't need a
		 * reply when finished.  So this should NOT block. */

		cancellable = g_weak_ref_get (&is->priv->parser_cancellable);

		job = camel_imapx_job_new (cancellable);
		job->type = IMAPX_JOB_NOOP;
		job->start = imapx_job_noop_start;
		job->pri = IMAPX_PRIORITY_NOOP;
		job->noreply = TRUE;

		imapx_submit_job (is, job, &local_error);

		if (local_error != NULL) {
			g_warning ("%s: %s", G_STRFUNC, local_error->message);
			g_error_free (local_error);
		}

		camel_imapx_job_unref (job);

		g_clear_object (&cancellable);
	}

	g_object_unref (is);

	return result;
}

static void
imapx_server_reset_inactivity_timer (CamelIMAPXServer *is)
{
	g_mutex_lock (&is->priv->inactivity_timeout_lock);

	if (is->priv->inactivity_timeout != NULL) {
		g_source_destroy (is->priv->inactivity_timeout);
		g_source_unref (is->priv->inactivity_timeout);
	}

	is->priv->inactivity_timeout =
		g_timeout_source_new_seconds (INACTIVITY_TIMEOUT_SECONDS);
	g_source_set_callback (
		is->priv->inactivity_timeout,
		imapx_server_inactivity_timeout_cb,
		imapx_weak_ref_new (is),
		(GDestroyNotify) imapx_weak_ref_free);
	g_source_attach (
		is->priv->inactivity_timeout,
		is->priv->parser_main_context);

	g_mutex_unlock (&is->priv->inactivity_timeout_lock);
}

static gint
imapx_server_set_connection_timeout (GIOStream *connection,
				     gint timeout_seconds)
{
	GSocket *socket;
	gint previous_timeout = -1;

	if (!G_IS_SOCKET_CONNECTION (connection))
		return previous_timeout;

	socket = g_socket_connection_get_socket (connection);
	if (socket) {
		previous_timeout = g_socket_get_timeout (socket);
		g_socket_set_timeout (socket, timeout_seconds);
	}

	return previous_timeout;
}

/* Must hold QUEUE_LOCK */
static void
imapx_command_start (CamelIMAPXServer *is,
                     CamelIMAPXCommand *ic)
{
	CamelIMAPXCommandPart *cp;
	CamelIMAPXJob *job;
	GInputStream *input_stream = NULL;
	GOutputStream *output_stream = NULL;
	GCancellable *cancellable = NULL;
	gboolean cp_continuation;
	gboolean cp_literal_plus;
	GList *head;
	gchar *string;
	GError *local_error = NULL;

	camel_imapx_command_close (ic);

	head = g_queue_peek_head_link (&ic->parts);
	g_return_if_fail (head != NULL);
	cp = (CamelIMAPXCommandPart *) head->data;
	ic->current_part = head;

	cp_continuation = ((cp->type & CAMEL_IMAPX_COMMAND_CONTINUATION) != 0);
	cp_literal_plus = ((cp->type & CAMEL_IMAPX_COMMAND_LITERAL_PLUS) != 0);

	/* TODO: If we support literal+ we should be able to write the whole command out
	 * at this point .... >here< */

	if (cp_continuation || cp_literal_plus)
		is->literal = ic;

	camel_imapx_command_queue_push_tail (is->active, ic);
	imapx_server_command_added (is, ic);

	job = camel_imapx_command_get_job (ic);
	if (job && g_cancellable_set_error_if_cancelled (camel_imapx_job_get_cancellable (job), &local_error)) {
		camel_imapx_job_take_error (job, local_error);
		local_error = NULL;

		camel_imapx_command_queue_remove (is->active, ic);
		imapx_server_command_removed (is, ic);

		if (ic->complete != NULL)
			ic->complete (is, ic);

		if (is->literal == ic)
			is->literal = NULL;

		goto exit;
	}


	input_stream = camel_imapx_server_ref_input_stream (is);
	output_stream = camel_imapx_server_ref_output_stream (is);
	cancellable = g_weak_ref_get (&is->priv->parser_cancellable);

	if (output_stream == NULL) {
		local_error = g_error_new_literal (
			CAMEL_IMAPX_ERROR, 1,
			"Cannot issue command, no stream available");
		goto fail;
	}

	c (
		is->tagprefix,
		"Starting command (active=%d,%s) %c%05u %s\r\n",
		camel_imapx_command_queue_get_length (is->active),
		is->literal ? " literal" : "",
		is->tagprefix,
		ic->tag,
		cp->data && g_str_has_prefix (cp->data, "LOGIN") ?
			"LOGIN..." : cp->data);

	string = g_strdup_printf (
		"%c%05u %s\r\n", is->tagprefix, ic->tag, cp->data);
	g_mutex_lock (&is->priv->stream_lock);
	g_output_stream_write_all (
		output_stream, string, strlen (string),
		NULL, cancellable, &local_error);
	g_mutex_unlock (&is->priv->stream_lock);
	g_free (string);

	if (local_error != NULL)
		goto fail;

	while (is->literal == ic && cp_literal_plus) {
		/* Sent LITERAL+ continuation immediately */
		imapx_continuation (
			is, input_stream, output_stream,
			TRUE, cancellable, &local_error);
		if (local_error != NULL)
			goto fail;
	}

	imapx_server_reset_inactivity_timer (is);

	goto exit;

fail:
	camel_imapx_command_queue_remove (is->active, ic);
	imapx_server_command_removed (is, ic);

	/* Break the parser thread out of its loop so it disconnects. */
	g_main_loop_quit (is->priv->parser_main_loop);
	g_cancellable_cancel (cancellable);

	/* Hand the error off to the command that we failed to start. */
	camel_imapx_command_failed (ic, local_error);

	if (ic->complete != NULL)
		ic->complete (is, ic);

	g_error_free (local_error);

exit:
	g_clear_object (&input_stream);
	g_clear_object (&output_stream);
	g_clear_object (&cancellable);
}

static gboolean
imapx_is_duplicate_fetch_or_refresh (CamelIMAPXServer *is,
                                     CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	guint32 job_types;

	/* Job types to match. */
	job_types =
		IMAPX_JOB_FETCH_NEW_MESSAGES |
		IMAPX_JOB_REFRESH_INFO;

	job = camel_imapx_command_get_job (ic);

	if (job == NULL)
		return FALSE;

	if ((job->type & job_types) == 0)
		return FALSE;

	if (imapx_match_active_job (is, job_types, NULL) == NULL)
		return FALSE;

	c (is->tagprefix, "Not yet sending duplicate fetch/refresh %s command\n", ic->name);

	return TRUE;
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
imapx_command_start_next (CamelIMAPXServer *is)
{
	CamelIMAPXCommand *first_ic;
	CamelIMAPXMailbox *mailbox;
	gint min_pri = -128;

	c (is->tagprefix, "** Starting next command\n");
	if (is->literal) {
		c (
			is->tagprefix,
			"* no; waiting for literal '%s'\n",
			is->literal->name);
		return;
	}

	g_mutex_lock (&is->priv->select_lock);
	mailbox = g_weak_ref_get (&is->priv->select_pending);
	g_mutex_unlock (&is->priv->select_lock);
	if (mailbox != NULL) {
		CamelIMAPXCommand *start_ic = NULL;
		GList *head, *link;

		c (
			is->tagprefix,
			"-- Checking job queue for non-mailbox jobs\n");

		head = camel_imapx_command_queue_peek_head_link (is->queue);

		/* Tag which commands in the queue to start. */
		for (link = head; link != NULL && !start_ic; link = g_list_next (link)) {
			CamelIMAPXCommand *ic = link->data;
			CamelIMAPXMailbox *ic_mailbox;

			if (ic->pri < min_pri)
				break;

			c (
				is->tagprefix,
				"-- %3d '%s'?\n",
				(gint) ic->pri, ic->name);

			ic_mailbox = camel_imapx_command_ref_mailbox (ic);

			if (ic_mailbox == NULL) {
				c (
					is->tagprefix,
					"--> starting '%s'\n",
					ic->name);
				min_pri = ic->pri;

				/* Each command must be removed from 'is->queue' before
				 * starting it, so we temporarily reference the command
				 * to avoid accidentally finalizing it. */
				start_ic = camel_imapx_command_ref (ic);
			}

			g_clear_object (&ic_mailbox);
		}

		if (!start_ic)
			c (
				is->tagprefix,
				"* no, waiting for pending select '%s'\n",
				camel_imapx_mailbox_get_name (mailbox));

		/* Start the tagged command */
		if (start_ic) {
			camel_imapx_command_queue_remove (is->queue, start_ic);
			imapx_server_command_removed (is, start_ic);
			imapx_command_start (is, start_ic);
			camel_imapx_command_unref (start_ic);
		}

		g_clear_object (&mailbox);

		return;
	}

	if (is->state == IMAPX_SELECTED) {
		gboolean stop_idle;
		gboolean start_idle;

		stop_idle =
			imapx_in_idle (is) &&
			!camel_imapx_command_queue_is_empty (is->queue);

		start_idle =
			imapx_use_idle (is) &&
			!imapx_in_idle (is) &&
			imapx_is_command_queue_empty (is);

		if (stop_idle) {
			switch (imapx_stop_idle (is, NULL)) {
				/* Proceed with the next queued command. */
				case IMAPX_IDLE_STOP_NOOP:
					break;

				case IMAPX_IDLE_STOP_SUCCESS:
					c (
						is->tagprefix,
						"waiting for idle to stop \n");
					/* if there are more pending commands,
					 * then they should be processed too */
					return;

				case IMAPX_IDLE_STOP_ERROR:
					return;
			}

		} else if (start_idle) {
			imapx_start_idle (is);
			c (is->tagprefix, "starting idle \n");
			return;
		}
	}

	if (camel_imapx_command_queue_is_empty (is->queue)) {
		c (is->tagprefix, "* no, no jobs\n");
		return;
	}

	/* See if any queued jobs on this select first */
	g_mutex_lock (&is->priv->select_lock);
	mailbox = g_weak_ref_get (&is->priv->select_mailbox);
	g_mutex_unlock (&is->priv->select_lock);
	if (mailbox != NULL) {
		CamelIMAPXCommand *start_ic = NULL;
		GList *head, *link;

		c (
			is->tagprefix,
			"- we're selected on '%s', current jobs?\n",
			camel_imapx_mailbox_get_name (mailbox));

		head = camel_imapx_command_queue_peek_head_link (is->active);

		/* Find the highest priority in the active queue. */
		for (link = head; link != NULL; link = g_list_next (link)) {
			CamelIMAPXCommand *ic = link->data;

			min_pri = MAX (min_pri, ic->pri);
			c (
				is->tagprefix,
				"-  %3d '%s'\n",
				(gint) ic->pri, ic->name);
		}

		if (camel_imapx_command_queue_get_length (is->active) >= MAX_COMMANDS) {
			c (
				is->tagprefix,
				"** too many jobs busy, "
				"waiting for results for now\n");
			g_object_unref (mailbox);
			return;
		}

		c (is->tagprefix, "-- Checking job queue\n");

		head = camel_imapx_command_queue_peek_head_link (is->queue);

		/* Tag which commands in the queue to start. */
		for (link = head; link != NULL && !start_ic; link = g_list_next (link)) {
			CamelIMAPXCommand *ic = link->data;
			CamelIMAPXMailbox *ic_mailbox;
			gboolean okay_to_start;

			if (is->literal != NULL)
				break;

			if (ic->pri < min_pri)
				break;

			c (
				is->tagprefix,
				"-- %3d '%s'?\n",
				(gint) ic->pri, ic->name);

			ic_mailbox = camel_imapx_command_ref_mailbox (ic);

			okay_to_start =
				(ic_mailbox == NULL) ||
				(ic_mailbox == mailbox &&
				!imapx_is_duplicate_fetch_or_refresh (is, ic));

			if (okay_to_start) {
				c (
					is->tagprefix,
					"--> starting '%s'\n",
					ic->name);
				min_pri = ic->pri;
				/* Each command must be removed from 'is->queue' before
				 * starting it, so we temporarily reference the command
				 * to avoid accidentally finalizing it. */
				start_ic = camel_imapx_command_ref (ic);
			} else {
				/* This job isn't for the selected mailbox,
				 * but we don't want to consider jobs with
				 * lower priority than this, even if they
				 * are for the selected mailbox. */
				min_pri = ic->pri;
			}

			g_clear_object (&ic_mailbox);
		}

		g_clear_object (&mailbox);

		/* Start the tagged command */
		if (start_ic) {
			camel_imapx_command_queue_remove (is->queue, start_ic);
			imapx_server_command_removed (is, start_ic);
			imapx_command_start (is, start_ic);
			camel_imapx_command_unref (start_ic);

			return;
		}
	}

	/* This won't be NULL because we checked for an empty queue above. */
	first_ic = camel_imapx_command_queue_peek_head (is->queue);

	/* If we need to select a mailbox for the first command, do
	 * so now.  It will re-call us if it completes successfully. */
	mailbox = camel_imapx_command_ref_mailbox (first_ic);
	if (mailbox != NULL) {
		CamelIMAPXJob *job;

		c (
			is->tagprefix,
			"Selecting mailbox '%s' for command '%s'(%p)\n",
			camel_imapx_mailbox_get_name (mailbox),
			first_ic->name, first_ic);

		/* Associate the SELECT command with the CamelIMAPXJob
		 * that triggered it.  Then if the SELECT command fails
		 * we have some destination to propagate the GError to. */
		job = camel_imapx_command_get_job (first_ic);
		imapx_maybe_select (is, job, mailbox);

		g_clear_object (&mailbox);

	} else {
		CamelIMAPXCommand *start_ic = NULL;
		GList *head, *link;

		min_pri = first_ic->pri;

		g_mutex_lock (&is->priv->select_lock);
		mailbox = g_weak_ref_get (&is->priv->select_mailbox);
		g_mutex_unlock (&is->priv->select_lock);

		head = camel_imapx_command_queue_peek_head_link (is->queue);

		/* Tag which commands in the queue to start. */
		for (link = head; link != NULL && !start_ic; link = g_list_next (link)) {
			CamelIMAPXCommand *ic = link->data;
			CamelIMAPXMailbox *ic_mailbox;
			gboolean okay_to_start;

			if (is->literal != NULL)
				break;

			if (ic->pri < min_pri)
				break;

			ic_mailbox = camel_imapx_command_ref_mailbox (ic);

			okay_to_start =
				(ic_mailbox == NULL) ||
				(ic_mailbox == mailbox &&
				!imapx_is_duplicate_fetch_or_refresh (is, ic));

			if (okay_to_start) {
				c (
					is->tagprefix,
					"* queueing job %3d '%s'\n",
					(gint) ic->pri, ic->name);
				min_pri = ic->pri;
				/* Each command must be removed from 'is->queue' before
				 * starting it, so we temporarily reference the command
				 * to avoid accidentally finalizing it. */
				start_ic = camel_imapx_command_ref (ic);
			}

			g_clear_object (&ic_mailbox);
		}

		g_clear_object (&mailbox);

		/* Start the tagged command */
		if (start_ic) {
			camel_imapx_command_queue_remove (is->queue, start_ic);
			imapx_server_command_removed (is, start_ic);
			imapx_command_start (is, start_ic);
			camel_imapx_command_unref (start_ic);
		}
	}
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

	c (
		is->tagprefix,
		"enqueue job '%.*s'\n",
		((CamelIMAPXCommandPart *) ic->parts.head->data)->data_size,
		((CamelIMAPXCommandPart *) ic->parts.head->data)->data);

	QUEUE_LOCK (is);

	if (is->state == IMAPX_SHUTDOWN) {
		GError *local_error = NULL;

		c (is->tagprefix, "refuse to queue job on disconnected server\n");

		local_error = g_error_new (
			CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT,
			"%s", _("Server disconnected"));
		camel_imapx_command_failed (ic, local_error);
		g_error_free (local_error);

		QUEUE_UNLOCK (is);

		if (ic->complete != NULL)
			ic->complete (is, ic);

		return;
	}

	camel_imapx_command_queue_insert_sorted (is->queue, ic);
	imapx_server_command_added (is, ic);

	imapx_command_start_next (is);

	QUEUE_UNLOCK (is);
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
		CamelIMAPXMailbox *mailbox;
		CamelIMAPXJob *job;
		gboolean job_matches;

		job = camel_imapx_command_get_job (ic);

		if (job == NULL)
			continue;

		if (!(job->type & type))
			continue;

		g_mutex_lock (&is->priv->select_lock);
		mailbox = g_weak_ref_get (&is->priv->select_mailbox);
		g_mutex_unlock (&is->priv->select_lock);

		job_matches = camel_imapx_job_matches (job, mailbox, uid);
		g_clear_object (&mailbox);

		if (job_matches) {
			match = job;
			break;
		}
	}

	QUEUE_UNLOCK (is);

	return match;
}

/* Do *not* call this when the queue_lock is held, it can cause
   deadlock when searching between multiple servers */
static CamelIMAPXJob *
imapx_server_ref_job (CamelIMAPXServer *imapx_server,
		      CamelIMAPXMailbox *mailbox,
		      guint32 job_type,
		      const gchar *uid)
{
	CamelIMAPXStore *imapx_store;
	CamelIMAPXJob *job;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (imapx_server), NULL);

	/* first try its own queue */
	job = camel_imapx_server_ref_job (imapx_server, mailbox, job_type, uid);
	if (job)
		return job;

	/* then try queue for all the opened servers */
	imapx_store = camel_imapx_server_ref_store (imapx_server);
	if (!imapx_store)
		return NULL;

	job = camel_imapx_store_ref_job (imapx_store, mailbox, job_type, uid);

	g_object_unref (imapx_store);

	return job;
}

static void
imapx_expunge_uid_from_summary (CamelIMAPXServer *is,
                                gchar *uid,
                                gboolean unsolicited)
{
	CamelFolder *folder;
	CamelIMAPXMailbox *mailbox;
	CamelMessageInfo *mi;
	guint32 messages;

	g_mutex_lock (&is->priv->select_lock);
	mailbox = g_weak_ref_get (&is->priv->select_mailbox);
	g_mutex_unlock (&is->priv->select_lock);

	g_return_if_fail (mailbox != NULL);

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_if_fail (folder != NULL);

	messages = camel_imapx_mailbox_get_messages (mailbox);

	if (unsolicited && messages > 0)
		camel_imapx_mailbox_set_messages (mailbox, messages - 1);

	if (is->priv->changes == NULL)
		is->priv->changes = camel_folder_change_info_new ();

	mi = camel_folder_summary_peek_loaded (folder->summary, uid);
	if (mi) {
		camel_folder_summary_remove (folder->summary, mi);
		camel_message_info_unref (mi);
	} else {
		camel_folder_summary_remove_uid (folder->summary, uid);
	}

	camel_folder_change_info_remove_uid (is->priv->changes, uid);

	if (imapx_in_idle (is)) {
		camel_folder_summary_save_to_db (folder->summary, NULL);
		imapx_update_store_summary (folder);
		camel_folder_changed (folder, is->priv->changes);

		camel_folder_change_info_clear (is->priv->changes);
	}

	g_object_unref (folder);
	g_object_unref (mailbox);
}

/* untagged response handler functions */

static gboolean
imapx_untagged_capability (CamelIMAPXServer *is,
                           GInputStream *input_stream,
                           GCancellable *cancellable,
                           GError **error)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	if (is->cinfo != NULL)
		imapx_free_capability (is->cinfo);

	is->cinfo = imapx_parse_capability (
		CAMEL_IMAPX_INPUT_STREAM (input_stream), cancellable, error);

	if (is->cinfo == NULL)
		return FALSE;

	c (is->tagprefix, "got capability flags %08x\n", is->cinfo->capa);

	imapx_server_stash_command_arguments (is);

	return TRUE;
}

static gboolean
imapx_untagged_expunge (CamelIMAPXServer *is,
                        GInputStream *input_stream,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelIMAPXMailbox *mailbox;
	CamelIMAPXJob *job = NULL;
	guint32 expunge = 0;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	expunge = is->priv->context->id;
	job = imapx_match_active_job (is, IMAPX_JOB_EXPUNGE, NULL);

	/* If there is a job running, let it handle the deletion */
	if (job != NULL)
		return TRUE;

	c (is->tagprefix, "expunged: %lu\n", is->priv->context->id);

	g_mutex_lock (&is->priv->select_lock);
	mailbox = g_weak_ref_get (&is->priv->select_mailbox);
	g_mutex_unlock (&is->priv->select_lock);

	if (mailbox != NULL) {
		CamelFolder *folder;
		gchar *uid;

		folder = imapx_server_ref_folder (is, mailbox);
		g_return_val_if_fail (folder != NULL, FALSE);

		uid = camel_imapx_dup_uid_from_summary_index (
			folder, expunge - 1);

		if (uid != NULL)
			imapx_expunge_uid_from_summary (is, uid, TRUE);

		g_object_unref (folder);
		g_object_unref (mailbox);
	}

	return TRUE;
}

static gboolean
imapx_untagged_vanished (CamelIMAPXServer *is,
                         GInputStream *input_stream,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelFolder *folder;
	CamelIMAPXMailbox *mailbox;
	GArray *uids;
	GList *uid_list = NULL;
	gboolean unsolicited = TRUE;
	guint ii = 0;
	guint len = 0;
	guchar *token = NULL;
	gint tok = 0;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	tok = camel_imapx_input_stream_token (
		CAMEL_IMAPX_INPUT_STREAM (input_stream),
		&token, &len, cancellable, error);
	if (tok < 0)
		return FALSE;
	if (tok == '(') {
		unsolicited = FALSE;
		while (tok != ')') {
			/* We expect this to be 'EARLIER' */
			tok = camel_imapx_input_stream_token (
				CAMEL_IMAPX_INPUT_STREAM (input_stream),
				&token, &len, cancellable, error);
			if (tok < 0)
				return FALSE;
		}
	} else {
		camel_imapx_input_stream_ungettoken (
			CAMEL_IMAPX_INPUT_STREAM (input_stream),
			tok, token, len);
	}

	uids = imapx_parse_uids (
		CAMEL_IMAPX_INPUT_STREAM (input_stream), cancellable, error);
	if (uids == NULL)
		return FALSE;

	g_mutex_lock (&is->priv->select_lock);
	mailbox = g_weak_ref_get (&is->priv->select_mailbox);
	g_mutex_unlock (&is->priv->select_lock);

	g_return_val_if_fail (mailbox != NULL, FALSE);

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_val_if_fail (folder != NULL, FALSE);

	if (unsolicited) {
		guint32 messages;

		messages = camel_imapx_mailbox_get_messages (mailbox);

		if (messages < uids->len) {
			c (
				is->tagprefix,
				"Error: mailbox messages (%u) is "
				"fewer than vanished %u\n",
				messages, uids->len);
			messages = 0;
		} else {
			messages -= uids->len;
		}

		camel_imapx_mailbox_set_messages (mailbox, messages);
	}

	if (is->priv->changes == NULL)
		is->priv->changes = camel_folder_change_info_new ();

	for (ii = 0; ii < uids->len; ii++) {
		guint32 uid;
		gchar *str;

		uid = g_array_index (uids, guint32, ii);

		e (is->tagprefix, "vanished: %u\n", uid);

		str = g_strdup_printf ("%u", uid);
		uid_list = g_list_prepend (uid_list, str);
		camel_folder_change_info_remove_uid (is->priv->changes, str);
	}

	uid_list = g_list_reverse (uid_list);
	camel_folder_summary_remove_uids (folder->summary, uid_list);

	/* If the response is truly unsolicited (e.g. via NOTIFY)
	 * then go ahead and emit the change notification now. */
	if (camel_imapx_command_queue_is_empty (is->queue)) {
		camel_folder_summary_save_to_db (folder->summary, NULL);
		imapx_update_store_summary (folder);
		camel_folder_changed (folder, is->priv->changes);
		camel_folder_change_info_clear (is->priv->changes);
	}

	g_list_free_full (uid_list, (GDestroyNotify) g_free);
	g_array_free (uids, TRUE);

	g_object_unref (folder);
	g_object_unref (mailbox);

	return TRUE;
}

static gboolean
imapx_untagged_namespace (CamelIMAPXServer *is,
                          GInputStream *input_stream,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelIMAPXNamespaceResponse *response;
	CamelIMAPXStore *imapx_store;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	response = camel_imapx_namespace_response_new (
		CAMEL_IMAPX_INPUT_STREAM (input_stream), cancellable, error);

	if (response == NULL)
		return FALSE;

	imapx_store = camel_imapx_server_ref_store (is);
	camel_imapx_store_set_namespaces (imapx_store, response);

	g_clear_object (&imapx_store);
	g_object_unref (response);

	return TRUE;
}

static gboolean
imapx_untagged_exists (CamelIMAPXServer *is,
                       GInputStream *input_stream,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelFolder *folder;
	CamelIMAPXMailbox *mailbox;
	guint32 exists;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	mailbox = camel_imapx_server_ref_selected (is);

	if (mailbox == NULL) {
		g_warning ("%s: No mailbox available", G_STRFUNC);
		return TRUE;
	}

	exists = (guint32) is->priv->context->id;

	camel_imapx_mailbox_set_messages (mailbox, exists);

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_val_if_fail (folder != NULL, FALSE);

	if (imapx_in_idle (is)) {
		guint count;

		count = camel_folder_summary_count (folder->summary);
		if (count < exists) {
			CamelIMAPXIdleStopResult stop_result;

			stop_result = imapx_stop_idle (is, error);
			success = (stop_result != IMAPX_IDLE_STOP_ERROR);
		}
	}

	g_object_unref (folder);
	g_object_unref (mailbox);

	return success;
}

static gboolean
imapx_untagged_flags (CamelIMAPXServer *is,
                      GInputStream *input_stream,
                      GCancellable *cancellable,
                      GError **error)
{
	guint32 flags = 0;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	success = imapx_parse_flags (
		CAMEL_IMAPX_INPUT_STREAM (input_stream),
		&flags, NULL, cancellable, error);

	c (is->tagprefix, "flags: %08x\n", flags);

	return success;
}

static gboolean
imapx_untagged_fetch (CamelIMAPXServer *is,
                      GInputStream *input_stream,
                      GCancellable *cancellable,
                      GError **error)
{
	struct _fetch_info *finfo;
	gboolean got_body_header;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	finfo = imapx_parse_fetch (
		CAMEL_IMAPX_INPUT_STREAM (input_stream), cancellable, error);
	if (finfo == NULL) {
		imapx_free_fetch (finfo);
		return FALSE;
	}

	/* Some IMAP servers respond with BODY[HEADER] when
	 * asked for RFC822.HEADER.  Treat them equivalently. */
	got_body_header =
		((finfo->got & FETCH_HEADER) == 0) &&
		(finfo->header == NULL) &&
		((finfo->got & FETCH_BODY) != 0) &&
		(g_strcmp0 (finfo->section, "HEADER") == 0);

	if (got_body_header) {
		finfo->got |= FETCH_HEADER;
		finfo->got &= ~FETCH_BODY;
		finfo->header = finfo->body;
		finfo->body = NULL;
	}

	if ((finfo->got & (FETCH_BODY | FETCH_UID)) == (FETCH_BODY | FETCH_UID)) {
		CamelIMAPXJob *job;
		GetMessageData *data;

		job = imapx_match_active_job (
			is, IMAPX_JOB_GET_MESSAGE, finfo->uid);
		if (job == NULL) {
			g_warn_if_reached ();
			return FALSE;
		}

		data = camel_imapx_job_get_data (job);
		g_return_val_if_fail (data != NULL, FALSE);

		/* This must've been a get-message request,
		 * fill out the body stream, in the right spot. */

		if (job != NULL) {
			GOutputStream *output_stream;
			gconstpointer body_data;
			gsize body_size;

			if (data->use_multi_fetch) {
				data->body_offset = finfo->offset;
				g_seekable_seek (
					G_SEEKABLE (data->stream),
					finfo->offset, G_SEEK_SET,
					NULL, NULL);
			}

			output_stream =
				g_io_stream_get_output_stream (data->stream);

			body_data = g_bytes_get_data (finfo->body, &body_size);

			/* Sometimes the server, like Microsoft Exchange, reports larger message
			   size than it actually is, which results in no data being read from
			   the server for that particular offset. */
			if (body_size) {
				g_mutex_lock (&is->priv->stream_lock);
				if (!g_output_stream_write_all (
					output_stream, body_data, body_size,
					NULL, cancellable, error)) {
					g_mutex_unlock (&is->priv->stream_lock);
					g_prefix_error (
						error, "%s: ",
						_("Error writing to cache stream"));
					return FALSE;
				}
				g_mutex_unlock (&is->priv->stream_lock);
			}
		}
	}

	if ((finfo->got & FETCH_FLAGS) && !(finfo->got & FETCH_HEADER)) {
		CamelIMAPXJob *job;
		CamelIMAPXMailbox *select_mailbox;
		CamelIMAPXMailbox *select_pending;
		RefreshInfoData *data = NULL;

		job = imapx_match_active_job (
			is, IMAPX_JOB_FETCH_NEW_MESSAGES |
			IMAPX_JOB_REFRESH_INFO, NULL);

		if (job != NULL) {
			data = camel_imapx_job_get_data (job);
			g_return_val_if_fail (data != NULL, FALSE);
		}

		g_mutex_lock (&is->priv->select_lock);
		select_mailbox = g_weak_ref_get (&is->priv->select_mailbox);
		select_pending = g_weak_ref_get (&is->priv->select_pending);
		g_mutex_unlock (&is->priv->select_lock);

		/* This is either a refresh_info job, check to see if it is
		 * and update if so, otherwise it must've been an unsolicited
		 * response, so update the summary to match. */
		if (data && (finfo->got & FETCH_UID) && data->scan_changes) {
			struct _refresh_info r;

			r.uid = finfo->uid;
			finfo->uid = NULL;
			r.server_flags = finfo->flags;
			r.server_user_flags = finfo->user_flags;
			finfo->user_flags = NULL;
			r.exists = FALSE;
			g_array_append_val (data->infos, r);

		} else if (select_mailbox != NULL) {
			CamelFolder *select_folder;
			CamelMessageInfo *mi = NULL;
			gboolean changed = FALSE;
			gchar *uid = NULL;

			c (is->tagprefix, "flag changed: %lu\n", is->priv->context->id);

			select_folder =
				imapx_server_ref_folder (is, select_mailbox);
			g_return_val_if_fail (select_folder != NULL, FALSE);

			if (finfo->got & FETCH_UID) {
				uid = finfo->uid;
				finfo->uid = NULL;
			} else {
				uid = camel_imapx_dup_uid_from_summary_index (
					select_folder,
					is->priv->context->id - 1);
			}

			if (uid) {
				mi = camel_folder_summary_get (
					select_folder->summary, uid);
				if (mi) {
					/* It's unsolicited _unless_ select_pending (i.e. during
					 * a QRESYNC SELECT */
					changed = imapx_update_message_info_flags (
						mi, finfo->flags,
						finfo->user_flags,
						camel_imapx_mailbox_get_permanentflags (select_mailbox),
						select_folder,
						(select_pending == NULL));
				} else {
					/* This (UID + FLAGS for previously unknown message) might
					 * happen during a SELECT (QRESYNC). We should use it. */
					c (is->tagprefix, "flags changed for unknown uid %s\n.", uid);
				}
				finfo->user_flags = NULL;
			}

			if (changed) {
				if (is->priv->changes == NULL)
					is->priv->changes =
						camel_folder_change_info_new ();

				camel_folder_change_info_change_uid (
					is->priv->changes, uid);
			}
			g_free (uid);

			if (changed && imapx_in_idle (is)) {
				camel_folder_summary_save_to_db (
					select_folder->summary, NULL);
				imapx_update_store_summary (select_folder);
				camel_folder_changed (
					select_folder, is->priv->changes);
				camel_folder_change_info_clear (
					is->priv->changes);
			}

			if (mi)
				camel_message_info_unref (mi);

			g_object_unref (select_folder);
		}

		g_clear_object (&select_mailbox);
		g_clear_object (&select_pending);
	}

	if ((finfo->got & (FETCH_HEADER | FETCH_UID)) == (FETCH_HEADER | FETCH_UID)) {
		CamelIMAPXJob *job;

		/* This must be a refresh info job as well, but it has
		 * asked for new messages to be added to the index. */

		job = imapx_match_active_job (
			is, IMAPX_JOB_FETCH_NEW_MESSAGES |
			IMAPX_JOB_REFRESH_INFO, NULL);

		if (job != NULL) {
			CamelIMAPXMailbox *mailbox;
			CamelFolder *folder;
			CamelMimeParser *mp;
			CamelMessageInfo *mi;
			guint32 messages;
			guint32 unseen;
			guint32 uidnext;

			mailbox = camel_imapx_job_ref_mailbox (job);
			g_return_val_if_fail (mailbox != NULL, FALSE);

			folder = imapx_server_ref_folder (is, mailbox);
			g_return_val_if_fail (folder != NULL, FALSE);

			messages = camel_imapx_mailbox_get_messages (mailbox);
			unseen = camel_imapx_mailbox_get_unseen (mailbox);
			uidnext = camel_imapx_mailbox_get_uidnext (mailbox);

			/* Do we want to save these headers for later too?  Do we care? */

			mp = camel_mime_parser_new ();
			camel_mime_parser_init_with_bytes (mp, finfo->header);
			mi = camel_folder_summary_info_new_from_parser (folder->summary, mp);
			g_object_unref (mp);

			if (mi != NULL) {
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
						cmp = imapx_refresh_info_uid_cmp (
							finfo->uid,
							r->uid,
							is->priv->context->fetch_order == CAMEL_SORT_ASCENDING);

						if (cmp > 0)
							min = mid + 1;
						else if (cmp < 0)
							max = mid - 1;
						else
							found = TRUE;

					} while (!found && min <= max);

					g_return_val_if_fail (found, FALSE);

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
					guint64 uidl;

					uidl = strtoull (mi->uid, NULL, 10);

					if (uidl >= uidnext) {
						c (is->tagprefix, "Updating unseen count for new message %s\n", mi->uid);
						camel_imapx_mailbox_set_unseen (mailbox, unseen + 1);
					} else {
						c (is->tagprefix, "Not updating unseen count for new message %s\n", mi->uid);
					}
				}

				binfo = (CamelMessageInfoBase *) mi;
				binfo->size = finfo->size;

				camel_folder_summary_lock (folder->summary);

				if (!camel_folder_summary_check_uid (folder->summary, mi->uid)) {
					RefreshInfoData *data;

					data = camel_imapx_job_get_data (job);
					g_return_val_if_fail (data != NULL, FALSE);

					imapx_set_message_info_flags_for_new_message (mi, server_flags, server_user_flags, FALSE, NULL, camel_imapx_mailbox_get_permanentflags (mailbox));
					camel_folder_summary_add (folder->summary, mi);
					camel_folder_change_info_add_uid (data->changes, mi->uid);

					camel_folder_change_info_recent_uid (data->changes, mi->uid);

					if (messages > 0) {
						gint cnt = (camel_folder_summary_count (folder->summary) * 100) / messages;
						camel_operation_progress (cancellable, cnt ? cnt : 1);
					}
				} else {
					camel_message_info_unref (mi);
				}

				camel_folder_summary_unlock (folder->summary);

				if (free_user_flags && server_user_flags)
					camel_flag_list_free (&server_user_flags);
			}

			g_object_unref (folder);
			g_object_unref (mailbox);
		}
	}

	imapx_free_fetch (finfo);

	return TRUE;
}

static gboolean
imapx_untagged_lsub (CamelIMAPXServer *is,
                     GInputStream *input_stream,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelIMAPXListResponse *response;
	CamelIMAPXStore *imapx_store;
	const gchar *mailbox_name;
	gchar separator;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	/* LSUB response is syntactically compatible with LIST response. */
	response = camel_imapx_list_response_new (
		CAMEL_IMAPX_INPUT_STREAM (input_stream), cancellable, error);
	if (response == NULL)
		return FALSE;

	camel_imapx_list_response_add_attribute (
		response, CAMEL_IMAPX_LIST_ATTR_SUBSCRIBED);

	mailbox_name = camel_imapx_list_response_get_mailbox_name (response);
	separator = camel_imapx_list_response_get_separator (response);

	/* Record the INBOX separator character once we know it. */
	if (camel_imapx_mailbox_is_inbox (mailbox_name))
		is->priv->inbox_separator = separator;

	imapx_store = camel_imapx_server_ref_store (is);
	camel_imapx_store_handle_lsub_response (imapx_store, is, response);

	g_clear_object (&imapx_store);
	g_clear_object (&response);

	return TRUE;
}

static gboolean
imapx_untagged_list (CamelIMAPXServer *is,
                     GInputStream *input_stream,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelIMAPXListResponse *response;
	CamelIMAPXStore *imapx_store;
	const gchar *mailbox_name;
	gchar separator;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	response = camel_imapx_list_response_new (
		CAMEL_IMAPX_INPUT_STREAM (input_stream), cancellable, error);
	if (response == NULL)
		return FALSE;

	mailbox_name = camel_imapx_list_response_get_mailbox_name (response);
	separator = camel_imapx_list_response_get_separator (response);

	/* Record the INBOX separator character once we know it. */
	if (camel_imapx_mailbox_is_inbox (mailbox_name))
		is->priv->inbox_separator = separator;

	imapx_store = camel_imapx_server_ref_store (is);
	camel_imapx_store_handle_list_response (imapx_store, is, response);

	g_clear_object (&imapx_store);
	g_clear_object (&response);

	return TRUE;
}

static gboolean
imapx_untagged_quota (CamelIMAPXServer *is,
                      GInputStream *input_stream,
                      GCancellable *cancellable,
                      GError **error)
{
	gchar *quota_root_name = NULL;
	CamelFolderQuotaInfo *quota_info = NULL;
	gboolean success;

	success = camel_imapx_parse_quota (
		CAMEL_IMAPX_INPUT_STREAM (input_stream),
		cancellable, &quota_root_name, &quota_info, error);

	/* Sanity check */
	g_return_val_if_fail (
		(success && (quota_root_name != NULL)) ||
		(!success && (quota_root_name == NULL)), FALSE);

	if (success) {
		CamelIMAPXStore *store;

		store = camel_imapx_server_ref_store (is);
		camel_imapx_store_set_quota_info (
			store, quota_root_name, quota_info);
		g_object_unref (store);

		g_free (quota_root_name);
		camel_folder_quota_info_free (quota_info);
	}

	return success;
}

static gboolean
imapx_untagged_quotaroot (CamelIMAPXServer *is,
                          GInputStream *input_stream,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelIMAPXStore *imapx_store;
	CamelIMAPXMailbox *mailbox;
	gchar *mailbox_name = NULL;
	gchar **quota_roots = NULL;
	gboolean success;

	success = camel_imapx_parse_quotaroot (
		CAMEL_IMAPX_INPUT_STREAM (input_stream),
		cancellable, &mailbox_name, &quota_roots, error);

	/* Sanity check */
	g_return_val_if_fail (
		(success && (mailbox_name != NULL)) ||
		(!success && (mailbox_name == NULL)), FALSE);

	if (!success)
		return FALSE;

	imapx_store = camel_imapx_server_ref_store (is);
	mailbox = camel_imapx_store_ref_mailbox (imapx_store, mailbox_name);
	g_clear_object (&imapx_store);

	if (mailbox != NULL) {
		camel_imapx_mailbox_set_quota_roots (
			mailbox, (const gchar **) quota_roots);
		g_object_unref (mailbox);
	} else {
		g_warning (
			"%s: Unknown mailbox '%s'",
			G_STRFUNC, mailbox_name);
	}

	g_free (mailbox_name);
	g_strfreev (quota_roots);

	return TRUE;
}

static gboolean
imapx_untagged_recent (CamelIMAPXServer *is,
                       GInputStream *input_stream,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelIMAPXMailbox *mailbox;
	guint32 recent;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	mailbox = camel_imapx_server_ref_selected (is);

	if (mailbox == NULL) {
		g_warning ("%s: No mailbox available", G_STRFUNC);
		return TRUE;
	}

	recent = (guint32) is->priv->context->id;

	camel_imapx_mailbox_set_recent (mailbox, recent);

	g_object_unref (mailbox);

	return TRUE;
}

static gboolean
imapx_untagged_search (CamelIMAPXServer *is,
                       GInputStream *input_stream,
                       GCancellable *cancellable,
                       GError **error)
{
	GArray *search_results;
	gint tok;
	guint len;
	guchar *token;
	guint64 number;
	gboolean success = FALSE;

	search_results = g_array_new (FALSE, FALSE, sizeof (guint64));

	while (TRUE) {
		gboolean success;

		/* Peek at the next token, and break
		 * out of the loop if we get a newline. */
		tok = camel_imapx_input_stream_token (
			CAMEL_IMAPX_INPUT_STREAM (input_stream),
			&token, &len, cancellable, error);
		if (tok == '\n')
			break;
		if (tok == IMAPX_TOK_ERROR)
			goto exit;
		camel_imapx_input_stream_ungettoken (
			CAMEL_IMAPX_INPUT_STREAM (input_stream),
			tok, token, len);

		success = camel_imapx_input_stream_number (
			CAMEL_IMAPX_INPUT_STREAM (input_stream),
			&number, cancellable, error);

		if (!success)
			goto exit;

		g_array_append_val (search_results, number);
	}

	g_mutex_lock (&is->priv->search_results_lock);

	if (is->priv->search_results == NULL)
		is->priv->search_results = g_array_ref (search_results);
	else
		g_warning ("%s: Conflicting search results", G_STRFUNC);

	g_mutex_unlock (&is->priv->search_results_lock);

	success = TRUE;

exit:
	g_array_unref (search_results);

	return success;
}

static gboolean
imapx_untagged_status (CamelIMAPXServer *is,
                       GInputStream *input_stream,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelIMAPXStatusResponse *response;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXMailbox *mailbox;
	const gchar *mailbox_name;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	response = camel_imapx_status_response_new (
		CAMEL_IMAPX_INPUT_STREAM (input_stream),
		is->priv->inbox_separator, cancellable, error);
	if (response == NULL)
		return FALSE;

	mailbox_name = camel_imapx_status_response_get_mailbox_name (response);

	imapx_store = camel_imapx_server_ref_store (is);
	mailbox = camel_imapx_store_ref_mailbox (imapx_store, mailbox_name);

	if (mailbox != NULL) {
		camel_imapx_mailbox_handle_status_response (mailbox, response);
		camel_imapx_store_emit_mailbox_updated (imapx_store, mailbox);
		g_object_unref (mailbox);
	}

	g_clear_object (&imapx_store);
	g_object_unref (response);

	return TRUE;
}

static gboolean
imapx_untagged_bye (CamelIMAPXServer *is,
                    GInputStream *input_stream,
                    GCancellable *cancellable,
                    GError **error)
{
	guchar *token = NULL;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	success = camel_imapx_input_stream_text (
		CAMEL_IMAPX_INPUT_STREAM (input_stream),
		&token, cancellable, error);

	/* XXX It's weird to be setting an error on success,
	 *     but it's to indicate the server hung up on us. */
	if (success) {
		c (is->tagprefix, "BYE: %s\n", token);
		g_set_error (
			error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT,
			"IMAP server said BYE: %s", token);
	}

	g_free (token);

	is->state = IMAPX_SHUTDOWN;

	return FALSE;
}

static gboolean
imapx_untagged_preauth (CamelIMAPXServer *is,
                        GInputStream *input_stream,
                        GCancellable *cancellable,
                        GError **error)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	c (is->tagprefix, "preauthenticated\n");
	if (is->state < IMAPX_AUTHENTICATED)
		is->state = IMAPX_AUTHENTICATED;

	return TRUE;
}

static gboolean
imapx_untagged_ok_no_bad (CamelIMAPXServer *is,
                          GInputStream *input_stream,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelIMAPXMailbox *mailbox;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	/* TODO: validate which ones of these can happen as unsolicited responses */
	/* TODO: handle bye/preauth differently */
	camel_imapx_input_stream_ungettoken (
		CAMEL_IMAPX_INPUT_STREAM (input_stream),
		is->priv->context->tok,
		is->priv->context->token,
		is->priv->context->len);

	/* These untagged responses can belong to ongoing SELECT command, thus
	   to the pending select mailbox, not to the currently selected or closing
	   mailbox, thus prefer the select pending mailbox, from the other two.
	   This makes sure that for example UIDVALIDITY is not incorrectly
	   overwritten with a value from a different mailbox, thus the offline
	   cache will persist, instead of being vanished.
	*/
	g_mutex_lock (&is->priv->select_lock);

	mailbox = g_weak_ref_get (&is->priv->select_pending);
	if (!mailbox)
		mailbox = g_weak_ref_get (&is->priv->select_mailbox);
	if (!mailbox)
		mailbox = g_weak_ref_get (&is->priv->select_closing);

	g_mutex_unlock (&is->priv->select_lock);

	is->priv->context->sinfo = imapx_parse_status (
		CAMEL_IMAPX_INPUT_STREAM (input_stream),
		mailbox, cancellable, error);

	g_clear_object (&mailbox);

	if (is->priv->context->sinfo == NULL)
		return FALSE;

	switch (is->priv->context->sinfo->condition) {
	case IMAPX_CLOSED:
		c (
			is->tagprefix,
			"previously selected mailbox is now closed\n");
		{
			CamelIMAPXMailbox *select_mailbox;
			CamelIMAPXMailbox *select_closing;
			CamelIMAPXMailbox *select_pending;

			g_mutex_lock (&is->priv->select_lock);

			select_mailbox =
				g_weak_ref_get (&is->priv->select_mailbox);
			select_closing =
				g_weak_ref_get (&is->priv->select_closing);
			select_pending =
				g_weak_ref_get (&is->priv->select_pending);

			if (select_mailbox == NULL)
				g_weak_ref_set (
					&is->priv->select_mailbox,
					select_pending);

			g_weak_ref_set (&is->priv->select_closing, NULL);

			g_mutex_unlock (&is->priv->select_lock);

			if (select_closing != NULL)
				g_signal_emit (
					is, signals[MAILBOX_CLOSED], 0,
					select_closing);

			g_clear_object (&select_mailbox);
			g_clear_object (&select_closing);
			g_clear_object (&select_pending);
		}
		break;
	case IMAPX_ALERT:
		c (is->tagprefix, "ALERT!: %s\n", is->priv->context->sinfo->text);
		{
			const gchar *alert_message;
			gboolean emit_alert = FALSE;

			g_mutex_lock (&is->priv->known_alerts_lock);

			alert_message = is->priv->context->sinfo->text;

			if (alert_message != NULL) {
				emit_alert = !g_hash_table_contains (
					is->priv->known_alerts,
					alert_message);
			}

			if (emit_alert) {
				CamelIMAPXStore *store;
				CamelService *service;
				CamelSession *session;

				store = camel_imapx_server_ref_store (is);

				g_hash_table_add (
					is->priv->known_alerts,
					g_strdup (alert_message));

				service = CAMEL_SERVICE (store);
				session = camel_service_ref_session (service);

				camel_session_user_alert (
					session, service,
					CAMEL_SESSION_ALERT_WARNING,
					alert_message);

				g_object_unref (session);
				g_object_unref (store);
			}

			g_mutex_unlock (&is->priv->known_alerts_lock);
		}
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
			c (is->tagprefix, "got capability flags %08x\n", is->cinfo ? is->cinfo->capa : 0xFFFFFFFF);
			imapx_server_stash_command_arguments (is);
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
                GInputStream *input_stream,
                GCancellable *cancellable,
                GError **error)
{
	CamelIMAPXSettings *settings;
	CamelSortType fetch_order;
	guchar *p = NULL, c;
	const gchar *token = NULL;
	gboolean success = FALSE;

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
	is->priv->context->tok = camel_imapx_input_stream_token (
		CAMEL_IMAPX_INPUT_STREAM (input_stream),
		&(is->priv->context->token),
		&(is->priv->context->len),
		cancellable, error);
	if (is->priv->context->tok < 0)
		goto exit;

	if (is->priv->context->tok == IMAPX_TOK_INT) {
		is->priv->context->id = strtoul (
			(gchar *) is->priv->context->token, NULL, 10);
		is->priv->context->tok = camel_imapx_input_stream_token (
			CAMEL_IMAPX_INPUT_STREAM (input_stream),
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

	e (is->tagprefix, "Have token '%s' id %lu\n", is->priv->context->token, is->priv->context->id);
	p = is->priv->context->token;
	while ((c = *p))
		*p++ = g_ascii_toupper ((gchar) c);

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
		success = desc->handler (is, input_stream, cancellable, error);
		if (!success)
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

	success = camel_imapx_input_stream_skip (
		CAMEL_IMAPX_INPUT_STREAM (input_stream), cancellable, error);

exit:
	g_free (is->priv->context);
	is->priv->context = NULL;

	return success;
}

/* handle any continuation requests
 * either data continuations, or auth continuation */
static gboolean
imapx_continuation (CamelIMAPXServer *is,
                    GInputStream *input_stream,
                    GOutputStream *output_stream,
                    gboolean litplus,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelIMAPXCommand *ic, *newliteral = NULL;
	CamelIMAPXCommandPart *cp;
	GList *link;
	gssize n_bytes_written;
	gboolean success;

	/* The 'literal' pointer is like a write-lock, nothing else
	 * can write while we have it ... so we dont need any
	 * ohter lock here.  All other writes go through
	 * queue-lock */
	if (imapx_in_idle (is)) {
		success = camel_imapx_input_stream_skip (
			CAMEL_IMAPX_INPUT_STREAM (input_stream),
			cancellable, error);

		if (!success)
			return FALSE;

		c (is->tagprefix, "Got continuation response for IDLE \n");
		g_rec_mutex_lock (&is->priv->idle_lock);
		/* We might have actually sent the DONE already! */
		if (is->priv->idle_state == IMAPX_IDLE_ISSUED) {
			is->priv->idle_state = IMAPX_IDLE_STARTED;
		} else if (is->priv->idle_state == IMAPX_IDLE_CANCEL) {
			/* IDLE got cancelled after we sent the command, while
			 * we were waiting for this continuation. Send DONE
			 * immediately. */
			if (!imapx_command_idle_stop (is, error)) {
				g_rec_mutex_unlock (&is->priv->idle_lock);
				return FALSE;
			}
			is->priv->idle_state = IMAPX_IDLE_WAIT_DONE;
		} else if (is->priv->idle_state == IMAPX_IDLE_WAIT_DONE) {
			/* Do nothing, just wait */
		} else {
			c (
				is->tagprefix, "idle starts in wrong state %d\n",
				is->priv->idle_state);
		}
		g_rec_mutex_unlock (&is->priv->idle_lock);

		QUEUE_LOCK (is);
		is->literal = NULL;
		imapx_command_start_next (is);
		QUEUE_UNLOCK (is);

		return TRUE;
	}

	ic = is->literal;
	if (!litplus) {
		if (ic == NULL) {
			c (is->tagprefix, "got continuation response with no outstanding continuation requests?\n");
			return camel_imapx_input_stream_skip (
				CAMEL_IMAPX_INPUT_STREAM (input_stream),
				cancellable, error);
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
		n_bytes_written =
			camel_data_wrapper_write_to_output_stream_sync (
				CAMEL_DATA_WRAPPER (cp->ob),
				output_stream, cancellable, error);
		if (n_bytes_written < 0)
			return FALSE;
		break;
	case CAMEL_IMAPX_COMMAND_AUTH: {
		gchar *resp;
		guchar *token;

		success = camel_imapx_input_stream_text (
			CAMEL_IMAPX_INPUT_STREAM (input_stream),
			&token, cancellable, error);

		if (!success)
			return FALSE;

		resp = camel_sasl_challenge_base64_sync (
			(CamelSasl *) cp->ob, (const gchar *) token,
			cancellable, error);
		g_free (token);
		if (resp == NULL)
			return FALSE;
		c (is->tagprefix, "got auth continuation, feeding token '%s' back to auth mech\n", resp);

		g_mutex_lock (&is->priv->stream_lock);
		n_bytes_written = g_output_stream_write_all (
			output_stream, resp, strlen (resp),
			NULL, cancellable, error);
		g_mutex_unlock (&is->priv->stream_lock);
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
		GFile *file;
		GFileInputStream *file_input_stream;

		c (is->tagprefix, "writing file '%s' to literal\n", (gchar *) cp->ob);

		file = g_file_new_for_path (cp->ob);
		file_input_stream = g_file_read (file, cancellable, error);
		g_object_unref (file);

		if (file_input_stream == NULL)
			return FALSE;

		g_mutex_lock (&is->priv->stream_lock);
		n_bytes_written = g_output_stream_splice (
			output_stream,
			G_INPUT_STREAM (file_input_stream),
			G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE,
			cancellable, error);
		g_mutex_unlock (&is->priv->stream_lock);

		g_object_unref (file_input_stream);

		if (n_bytes_written < 0)
			return FALSE;

		break; }
	case CAMEL_IMAPX_COMMAND_STRING:
		g_mutex_lock (&is->priv->stream_lock);
		n_bytes_written = g_output_stream_write_all (
			output_stream, cp->ob, cp->ob_size,
			NULL, cancellable, error);
		g_mutex_unlock (&is->priv->stream_lock);
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
		success = camel_imapx_input_stream_skip (
			CAMEL_IMAPX_INPUT_STREAM (input_stream),
			cancellable, error);

		if (!success)
			return FALSE;
	}

noskip:
	link = g_list_next (link);
	if (link != NULL) {
		ic->current_part = link;
		cp = (CamelIMAPXCommandPart *) link->data;

		c (is->tagprefix, "next part of command \"%c%05u: %s\"\n", is->tagprefix, ic->tag, cp->data);

		g_mutex_lock (&is->priv->stream_lock);
		n_bytes_written = g_output_stream_write_all (
			output_stream, cp->data, strlen (cp->data),
			NULL, cancellable, error);
		g_mutex_unlock (&is->priv->stream_lock);
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

	g_mutex_lock (&is->priv->stream_lock);
	n_bytes_written = g_output_stream_write_all (
		output_stream, "\r\n", 2, NULL, cancellable, error);
	g_mutex_unlock (&is->priv->stream_lock);
	if (n_bytes_written < 0)
		return FALSE;

	QUEUE_LOCK (is);
	is->literal = newliteral;

	if (!litplus)
		imapx_command_start_next (is);
	QUEUE_UNLOCK (is);

	return TRUE;
}

/* handle a completion line */
static gboolean
imapx_completion (CamelIMAPXServer *is,
                  GInputStream *input_stream,
                  guchar *token,
                  gint len,
                  GCancellable *cancellable,
                  GError **error)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXMailbox *mailbox;
	gboolean success = FALSE;
	guint tag;

	/* Given "A0001 ...", 'A' = tag prefix, '0001' = tag. */

	if (token[0] != is->tagprefix) {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"Server sent unexpected response: %s", token);
		return FALSE;
	}

	tag = strtoul ((gchar *) token + 1, NULL, 10);

	QUEUE_LOCK (is);

	if (is->literal != NULL && is->literal->tag == tag)
		ic = camel_imapx_command_ref (is->literal);
	else
		ic = camel_imapx_command_queue_ref_by_tag (is->active, tag);

	QUEUE_UNLOCK (is);

	if (ic == NULL) {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"got response tag unexpectedly: %s", token);
		return FALSE;
	}

	c (is->tagprefix, "Got completion response for command %05u '%s'\n", ic->tag, ic->name);

	if (camel_folder_change_info_changed (is->priv->changes)) {
		CamelFolder *folder;
		CamelIMAPXMailbox *mailbox;

		g_mutex_lock (&is->priv->select_lock);
		mailbox = g_weak_ref_get (&is->priv->select_mailbox);
		g_mutex_unlock (&is->priv->select_lock);

		g_return_val_if_fail (mailbox != NULL, FALSE);

		folder = imapx_server_ref_folder (is, mailbox);
		g_return_val_if_fail (folder != NULL, FALSE);

		camel_folder_summary_save_to_db (folder->summary, NULL);

		imapx_update_store_summary (folder);
		camel_folder_changed (folder, is->priv->changes);
		camel_folder_change_info_clear (is->priv->changes);

		g_object_unref (folder);
		g_object_unref (mailbox);
	}

	QUEUE_LOCK (is);

	/* Move the command from the active queue to the done queue.
	 * We're holding our own reference to the command so there's
	 * no risk of accidentally finalizing it here. */
	camel_imapx_command_queue_remove (is->active, ic);
	imapx_server_command_removed (is, ic);
	camel_imapx_command_queue_push_tail (is->done, ic);

	if (is->literal == ic)
		is->literal = NULL;

	if (g_list_next (ic->current_part) != NULL) {
		QUEUE_UNLOCK (is);
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"command still has unsent parts? %s", ic->name);
		goto exit;
	}

	camel_imapx_command_queue_remove (is->done, ic);

	QUEUE_UNLOCK (is);

	mailbox = camel_imapx_server_ref_selected (is);

	ic->status = imapx_parse_status (
		CAMEL_IMAPX_INPUT_STREAM (input_stream),
		mailbox, cancellable, error);

	g_clear_object (&mailbox);

	if (ic->status == NULL)
		goto exit;

	if (ic->complete != NULL)
		ic->complete (is, ic);

	success = TRUE;

exit:
	QUEUE_LOCK (is);
	imapx_command_start_next (is);
	QUEUE_UNLOCK (is);

	camel_imapx_command_unref (ic);

	return success;
}

static gboolean
imapx_step (CamelIMAPXServer *is,
            GInputStream *input_stream,
            GCancellable *cancellable,
            GError **error)
{
	GOutputStream *output_stream;
	guint len;
	guchar *token;
	gint tok;
	gboolean success = FALSE;

	// poll ? wait for other stuff? loop?
	tok = camel_imapx_input_stream_token (
		CAMEL_IMAPX_INPUT_STREAM (input_stream),
		&token, &len, cancellable, error);

	output_stream = camel_imapx_server_ref_output_stream (is);
	g_return_val_if_fail (output_stream != NULL, FALSE);

	switch (tok) {
		case IMAPX_TOK_ERROR:
			/* GError is already set. */
			break;
		case '*':
			success = imapx_untagged (
				is, input_stream, cancellable, error);
			break;
		case IMAPX_TOK_TOKEN:
			success = imapx_completion (
				is, input_stream,
				token, len, cancellable, error);
			break;
		case '+':
			success = imapx_continuation (
				is, input_stream, output_stream,
				FALSE, cancellable, error);
			break;
		default:
			g_set_error (
				error, CAMEL_IMAPX_ERROR, 1,
				"unexpected server response:");
			break;
	}

	g_clear_object (&output_stream);

	return success;
}

/* Used to run 1 command synchronously,
 * use for capa, login, and namespaces only. */
static gboolean
imapx_command_run (CamelIMAPXServer *is,
                   CamelIMAPXCommand *ic,
                   GCancellable *cancellable,
                   GError **error)
{
	GInputStream *input_stream;
	gboolean success = TRUE;

	input_stream = camel_imapx_server_ref_input_stream (is);
	g_return_val_if_fail (input_stream != NULL, FALSE);

	camel_imapx_command_close (ic);

	QUEUE_LOCK (is);
	imapx_command_start (is, ic);
	QUEUE_UNLOCK (is);

	while (success && ic->status == NULL)
		success = imapx_step (is, input_stream, cancellable, error);

	if (is->literal == ic)
		is->literal = NULL;

	QUEUE_LOCK (is);
	camel_imapx_command_queue_remove (is->active, ic);
	imapx_server_command_removed (is, ic);
	QUEUE_UNLOCK (is);

	g_object_unref (input_stream);

	return success;
}

static void
imapx_command_complete (CamelIMAPXServer *is,
                        CamelIMAPXCommand *ic)
{
	camel_imapx_command_done (ic);
	camel_imapx_command_unref (ic);
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
	gboolean success = TRUE;

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

	if (camel_imapx_command_set_error_if_failed (ic, error))
		return FALSE;

	return success;
}

static gboolean
imapx_ensure_mailbox_permanentflags (CamelIMAPXServer *is,
				     CamelIMAPXMailbox *mailbox,
				     GCancellable *cancellable,
				     GError **error)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);

	if (camel_imapx_mailbox_get_permanentflags (mailbox) != ~0)
		return TRUE;

	/* This will also invoke SELECT command, which updates PERMANENTFLAGS
	   for the mailbox. There might be possible to use EXAMINE for it,
	   but some servers do not return the same set of flags as with SELECT.
	   It's a little hack on top of the IMAPx implementation. */
	return camel_imapx_server_noop (is, mailbox, cancellable, error);
}

/* ********************************************************************** */
// IDLE support

/*TODO handle negative cases sanely */
static gboolean
imapx_command_idle_stop (CamelIMAPXServer *is,
                         GError **error)
{
	GOutputStream *output_stream;
	GCancellable *cancellable;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	output_stream = camel_imapx_server_ref_output_stream (is);
	g_return_val_if_fail (output_stream != NULL, FALSE);

	cancellable = g_weak_ref_get (&is->priv->parser_cancellable);

	g_mutex_lock (&is->priv->stream_lock);
	success = g_output_stream_write_all (
		output_stream, "DONE\r\n", 6, NULL, cancellable, error);
	g_mutex_unlock (&is->priv->stream_lock);

	if (!success) {
		g_prefix_error (error, "Unable to issue DONE: ");
		c (is->tagprefix, "Failed to issue DONE to terminate IDLE\n");
		is->state = IMAPX_SHUTDOWN;
		g_main_loop_quit (is->priv->parser_main_loop);
	}

	g_clear_object (&cancellable);
	g_clear_object (&output_stream);

	return success;
}

static void
imapx_command_idle_done (CamelIMAPXServer *is,
                         CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error performing IDLE"));
		camel_imapx_job_take_error (job, local_error);
	}

	g_rec_mutex_lock (&is->priv->idle_lock);
	is->priv->idle_state = IMAPX_IDLE_OFF;
	g_rec_mutex_unlock (&is->priv->idle_lock);

	imapx_unregister_job (is, job);
}

static gboolean
imapx_job_idle_start (CamelIMAPXJob *job,
                      CamelIMAPXServer *is,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXCommandPart *cp;
	CamelIMAPXMailbox *mailbox;
	gboolean success = TRUE;

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_val_if_fail (mailbox != NULL, FALSE);

	ic = camel_imapx_command_new (
		is, "IDLE", mailbox, "IDLE");
	camel_imapx_command_set_job (ic, job);
	ic->pri = job->pri;
	ic->complete = imapx_command_idle_done;

	camel_imapx_command_close (ic);
	cp = g_queue_peek_head (&ic->parts);
	cp->type |= CAMEL_IMAPX_COMMAND_CONTINUATION;

	QUEUE_LOCK (is);
	g_rec_mutex_lock (&is->priv->idle_lock);
	/* Don't issue it if the idle was cancelled already */
	if (is->priv->idle_state == IMAPX_IDLE_PENDING) {
		is->priv->idle_state = IMAPX_IDLE_ISSUED;

		if (camel_imapx_command_queue_is_empty (is->active)) {
			imapx_command_start (is, ic);
		} else {
			c (is->tagprefix, "finally cancelling IDLE, other command was quicker\n");
			is->priv->idle_state = IMAPX_IDLE_OFF;
			imapx_unregister_job (is, job);
		}
	} else {
		imapx_unregister_job (is, job);
	}
	g_rec_mutex_unlock (&is->priv->idle_lock);
	QUEUE_UNLOCK (is);

	camel_imapx_command_unref (ic);

	g_object_unref (mailbox);

	return success;
}

static gboolean
camel_imapx_server_idle (CamelIMAPXServer *is,
                         CamelIMAPXMailbox *mailbox,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelIMAPXJob *job;
	gint previous_connection_timeout;
	gboolean success;

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_IDLE;
	job->start = imapx_job_idle_start;

	camel_imapx_job_set_mailbox (job, mailbox);

	previous_connection_timeout = imapx_server_set_connection_timeout (is->priv->connection, 0);

	success = imapx_submit_job (is, job, error);

	if (previous_connection_timeout >= 0)
		imapx_server_set_connection_timeout (is->priv->connection, previous_connection_timeout);

	camel_imapx_job_unref (job);

	return success;
}

static gboolean
imapx_job_fetch_new_messages_matches (CamelIMAPXJob *job,
                                      CamelIMAPXMailbox *mailbox,
                                      const gchar *uid)
{
	return camel_imapx_job_has_mailbox (job, mailbox);
}

static gboolean
imapx_server_fetch_new_messages (CamelIMAPXServer *is,
                                 CamelIMAPXMailbox *mailbox,
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

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_FETCH_NEW_MESSAGES;
	job->start = imapx_job_fetch_new_messages_start;
	job->matches = imapx_job_fetch_new_messages_matches;
	job->noreply = async;

	camel_imapx_job_set_mailbox (job, mailbox);

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) refresh_info_data_free);

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

	return success;
}

static gboolean
imapx_call_idle (gpointer data)
{
	CamelFolder *folder;
	CamelIMAPXServer *is;
	CamelIMAPXMailbox *mailbox;
	GCancellable *cancellable;
	GError *local_error = NULL;

	is = g_weak_ref_get (data);

	if (is == NULL)
		goto exit;

	/* XXX Rename to 'pending_lock'? */
	g_rec_mutex_lock (&is->priv->idle_lock);
	g_source_unref (is->priv->idle_pending);
	is->priv->idle_pending = NULL;

	if (is->priv->idle_state != IMAPX_IDLE_PENDING) {
		g_rec_mutex_unlock (&is->priv->idle_lock);
		goto exit;
	}

	g_rec_mutex_unlock (&is->priv->idle_lock);

	g_mutex_lock (&is->priv->select_lock);
	mailbox = g_weak_ref_get (&is->priv->select_mailbox);
	g_mutex_unlock (&is->priv->select_lock);

	if (mailbox == NULL)
		goto exit;

	folder = imapx_server_ref_folder (is, mailbox);
	if (folder == NULL)
		goto exit;

	cancellable = g_weak_ref_get (&is->priv->parser_cancellable);

	/* We block here until the IDLE command completes. */
	camel_imapx_server_idle (is, mailbox, cancellable, &local_error);

	if (local_error == NULL) {
		gboolean have_new_messages;
		gboolean fetch_new_messages;

		have_new_messages =
			camel_imapx_mailbox_get_messages (mailbox) >
			camel_folder_summary_count (folder->summary);

		fetch_new_messages =
			have_new_messages &&
			imapx_is_command_queue_empty (is);

		if (fetch_new_messages)
			imapx_server_fetch_new_messages (
				is, mailbox, TRUE, TRUE,
				cancellable, &local_error);
	}

	/* XXX Need a better way to propagate IDLE errors. */
	if (local_error != NULL) {
		if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
		    is->state != IMAPX_SHUTDOWN)
			g_warning ("%s: %s", G_STRFUNC, local_error->message);
		g_clear_error (&local_error);
	}

	g_clear_object (&folder);
	g_clear_object (&cancellable);

exit:
	g_clear_object (&is);

	return G_SOURCE_REMOVE;
}

static gpointer
imapx_idle_thread (gpointer data)
{
	CamelIMAPXServer *is = (CamelIMAPXServer *) data;
	GSource *pending;

	g_main_context_push_thread_default (is->priv->idle_main_context);

	/* Schedule the first IDLE command after a brief "dwell"
	 * delay so any other pending commands get priority.
	 *
	 * XXX Don't fully understand why this is necessary, but
	 *     for now just adapting old code and hoping to avoid
	 *     regressions.
	 */

	g_rec_mutex_lock (&is->priv->idle_lock);

	g_warn_if_fail (is->priv->idle_pending == NULL);
	pending = g_timeout_source_new_seconds (IMAPX_IDLE_DWELL_TIME);
	g_source_set_name (pending, "imapx_call_idle");
	g_source_set_callback (
		pending, imapx_call_idle,
		imapx_weak_ref_new (is),
		(GDestroyNotify) imapx_weak_ref_free);
	g_source_attach (pending, is->priv->idle_main_context);
	is->priv->idle_pending = g_source_ref (pending);
	g_source_unref (pending);

	g_rec_mutex_unlock (&is->priv->idle_lock);

	g_main_loop_run (is->priv->idle_main_loop);

	g_main_context_pop_thread_default (is->priv->idle_main_context);

	g_object_unref (is);

	return NULL;
}

static CamelIMAPXIdleStopResult
imapx_stop_idle (CamelIMAPXServer *is,
                 GError **error)
{
	CamelIMAPXIdleStopResult result = IMAPX_IDLE_STOP_NOOP;
	time_t now;

	time (&now);

	g_rec_mutex_lock (&is->priv->idle_lock);

	switch (is->priv->idle_state) {
		case IMAPX_IDLE_ISSUED:
			is->priv->idle_state = IMAPX_IDLE_CANCEL;
			result = IMAPX_IDLE_STOP_SUCCESS;
			break;

		case IMAPX_IDLE_CANCEL:
		case IMAPX_IDLE_WAIT_DONE:
			result = IMAPX_IDLE_STOP_SUCCESS;
			break;

		case IMAPX_IDLE_STARTED:
			if (imapx_command_idle_stop (is, error)) {
				result = IMAPX_IDLE_STOP_SUCCESS;
				is->priv->idle_state = IMAPX_IDLE_WAIT_DONE;
			} else {
				result = IMAPX_IDLE_STOP_ERROR;
				is->priv->idle_state = IMAPX_IDLE_OFF;
				goto exit;
			}
			break;

		case IMAPX_IDLE_PENDING:
			is->priv->idle_state = IMAPX_IDLE_OFF;
			break;

		case IMAPX_IDLE_OFF:
			break;
	}

exit:
	g_rec_mutex_unlock (&is->priv->idle_lock);

	return result;
}

static void
imapx_start_idle (CamelIMAPXServer *is)
{
	if (camel_application_is_exiting)
		return;

	g_rec_mutex_lock (&is->priv->idle_lock);

	if (is->priv->idle_state != IMAPX_IDLE_OFF) {
		g_warn_if_fail (is->priv->idle_state == IMAPX_IDLE_OFF);
		g_rec_mutex_unlock (&is->priv->idle_lock);
		return;
	}

	is->priv->idle_state = IMAPX_IDLE_PENDING;

	if (is->priv->idle_thread == NULL) {
		is->priv->idle_thread = g_thread_new (
			NULL, imapx_idle_thread, g_object_ref (is));

	} else if (is->priv->idle_pending == NULL) {
		GSource *pending;

		pending = g_idle_source_new ();
		g_source_set_name (pending, "imapx_call_idle");
		g_source_set_callback (
			pending, imapx_call_idle,
			imapx_weak_ref_new (is),
			(GDestroyNotify) imapx_weak_ref_free);
		g_source_attach (pending, is->priv->idle_main_context);
		is->priv->idle_pending = g_source_ref (pending);
		g_source_unref (pending);
	}

	g_rec_mutex_unlock (&is->priv->idle_lock);
}

static gboolean
imapx_in_idle (CamelIMAPXServer *is)
{
	gboolean in_idle = FALSE;

	g_rec_mutex_lock (&is->priv->idle_lock);

	if (is->priv->idle_thread != NULL)
		in_idle = (is->priv->idle_state > IMAPX_IDLE_OFF);

	g_rec_mutex_unlock (&is->priv->idle_lock);

	return in_idle;
}

static gboolean
imapx_use_idle (CamelIMAPXServer *is)
{
	gboolean use_idle = FALSE;

	/* No need for IDLE if the server supports NOTIFY. */
	if (CAMEL_IMAPX_HAVE_CAPABILITY (is->cinfo, NOTIFY))
		return FALSE;

	if (CAMEL_IMAPX_HAVE_CAPABILITY (is->cinfo, IDLE)) {
		CamelIMAPXSettings *settings;

		settings = camel_imapx_server_ref_settings (is);
		use_idle = camel_imapx_settings_get_use_idle (settings);
		g_object_unref (settings);
	}

	return use_idle;
}

// end IDLE
/* ********************************************************************** */
static void
imapx_command_select_done (CamelIMAPXServer *is,
                           CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	CamelIMAPXMailbox *select_closing;
	CamelIMAPXMailbox *select_pending;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		CamelIMAPXCommandQueue *failed;
		GQueue trash = G_QUEUE_INIT;
		GList *list, *link;
		gboolean noperm_error;

		c (is->tagprefix, "Select failed: %s\n", local_error ? local_error->message : "Unknown error");

		g_mutex_lock (&is->priv->select_lock);
		select_closing = g_weak_ref_get (&is->priv->select_closing);
		select_pending = g_weak_ref_get (&is->priv->select_pending);
		g_weak_ref_set (&is->priv->select_mailbox, NULL);
		g_weak_ref_set (&is->priv->select_closing, NULL);
		g_weak_ref_set (&is->priv->select_pending, NULL);
		is->state = IMAPX_INITIALISED;
		g_mutex_unlock (&is->priv->select_lock);

		failed = camel_imapx_command_queue_new ();

		QUEUE_LOCK (is);

		noperm_error = select_pending != NULL && ic->status && ic->status->result == IMAPX_NO &&
			(ic->status->condition == IMAPX_NOPERM || ic->status->condition == IMAPX_UNKNOWN);

		if (select_pending != NULL) {
			GList *head = camel_imapx_command_queue_peek_head_link (is->queue);

			for (link = head; link != NULL; link = g_list_next (link)) {
				CamelIMAPXCommand *cw = link->data;
				CamelIMAPXMailbox *cw_mailbox;

				cw_mailbox = camel_imapx_command_ref_mailbox (cw);

				if (cw_mailbox == select_pending) {
					c (
						is->tagprefix,
						"Cancelling command '%s'(%p) "
						"for mailbox '%s'\n",
						cw->name, cw,
						camel_imapx_mailbox_get_name (cw_mailbox));
					g_queue_push_tail (&trash, link);
				}

				g_clear_object (&cw_mailbox);
			}
		}

		if (noperm_error) {
			/* This avoids another SELECT try on this mailbox;
			   the mailbox can be write-only in this case. */
			if (camel_imapx_mailbox_get_permanentflags (select_pending) == ~0)
				camel_imapx_mailbox_set_permanentflags (select_pending, 0);
		}

		while ((link = g_queue_pop_head (&trash)) != NULL) {
			CamelIMAPXCommand *cw = link->data;
			camel_imapx_command_ref (cw);
			camel_imapx_command_queue_delete_link (is->queue, link);
			imapx_server_command_removed (is, cw);
			camel_imapx_command_queue_push_tail (failed, cw);
			camel_imapx_command_unref (cw);
		}

		QUEUE_UNLOCK (is);

		list = camel_imapx_command_queue_peek_head_link (failed);

		for (link = list; link != NULL; link = g_list_next (link)) {
			CamelIMAPXCommand *cw = link->data;
			CamelIMAPXJob *failed_job;

			failed_job = camel_imapx_command_get_job (cw);

			if (!CAMEL_IS_IMAPX_JOB (failed_job)) {
				g_warn_if_reached ();
				continue;
			}

			if (!noperm_error)
				camel_imapx_job_cancel (failed_job);

			if (ic->status)
				cw->status = imapx_copy_status (ic->status);

			cw->complete (is, cw);
		}

		camel_imapx_command_queue_free (failed);

		camel_imapx_job_take_error (job, local_error);
		imapx_unregister_job (is, job);

	} else {
		CamelFolder *folder;
		CamelIMAPXSummary *imapx_summary;
		guint32 uidnext;

		c (is->tagprefix, "Select ok!\n");

		g_mutex_lock (&is->priv->select_lock);
		select_closing = g_weak_ref_get (&is->priv->select_closing);
		select_pending = g_weak_ref_get (&is->priv->select_pending);
		g_weak_ref_set (&is->priv->select_mailbox, select_pending);
		g_weak_ref_set (&is->priv->select_closing, NULL);
		g_weak_ref_set (&is->priv->select_pending, NULL);
		is->state = IMAPX_SELECTED;
		g_mutex_unlock (&is->priv->select_lock);

		/* We should have a strong reference
		 * on the newly-selected CamelFolder. */
		folder = imapx_server_ref_folder (is, select_pending);
		g_return_if_fail (folder != NULL);

		uidnext = camel_imapx_mailbox_get_uidnext (select_pending);
		imapx_summary = CAMEL_IMAPX_SUMMARY (folder->summary);

		if (imapx_summary->uidnext < uidnext) {
			/* We don't want to fetch new messages if the command we selected this
			 * folder for is *already* fetching all messages (i.e. scan_changes).
			 * Bug #667725. */
			CamelIMAPXJob *job = imapx_server_ref_job (
				is, select_pending,
				IMAPX_JOB_REFRESH_INFO, NULL);
			if (job) {
				camel_imapx_job_unref (job);
				c (
					is->tagprefix,
					"Will not fetch_new_messages when already refreshing information\n");
			} else {
				imapx_server_fetch_new_messages (is, select_pending, TRUE, TRUE, NULL, NULL);
			}
		}

#if 0  /* see comment for disabled bits in imapx_job_refresh_info_start() */
		/* This should trigger a new messages scan */
		if (is->exists != folder->summary->root_view->total_count)
			g_warning (
				"exists is %d our summary is %d and summary exists is %d\n", is->exists,
				folder->summary->root_view->total_count,
				((CamelIMAPXSummary *) folder->summary)->exists);
#endif

		g_clear_object (&folder);
	}

	if (select_closing != NULL)
		g_signal_emit (is, signals[MAILBOX_CLOSED], 0, select_closing);

	g_clear_object (&select_closing);
	g_clear_object (&select_pending);
}

/* Should have a queue lock. TODO Change the way select is written */
static void
imapx_maybe_select (CamelIMAPXServer *is,
                    CamelIMAPXJob *job,
                    CamelIMAPXMailbox *mailbox)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXMailbox *select_mailbox;
	CamelIMAPXMailbox *select_pending;
	gboolean nothing_to_do = FALSE;

	/* Select is complicated by the fact we may have commands
	 * active on the server for a different selection.
	 *
	 * So this waits for any commands to complete, selects the
	 * new mailbox, and halts the queuing of any new commands.
	 * It is assumed whomever called us is about to issue a
	 * high-priority command anyway. */

	g_mutex_lock (&is->priv->select_lock);

	select_mailbox = g_weak_ref_get (&is->priv->select_mailbox);
	select_pending = g_weak_ref_get (&is->priv->select_pending);

	if (select_pending != NULL) {
		nothing_to_do = TRUE;
	} else if (select_mailbox == mailbox) {
		nothing_to_do = TRUE;
	} else if (!camel_imapx_command_queue_is_empty (is->active)) {
		nothing_to_do = TRUE;
	} else {
		g_weak_ref_set (&is->priv->select_pending, mailbox);

		if (select_mailbox != NULL) {
			g_weak_ref_set (&is->priv->select_mailbox, NULL);
		} else {
			/* If no mailbox was selected, we won't get a
			 * [CLOSED] status so just point select_mailbox
			 * at the newly-selected mailbox immediately. */
			g_weak_ref_set (&is->priv->select_mailbox, mailbox);
		}

		g_weak_ref_set (&is->priv->select_closing, select_mailbox);

		/* Hrm, what about reconnecting? */
		is->state = IMAPX_INITIALISED;
	}

	g_clear_object (&select_mailbox);
	g_clear_object (&select_pending);

	g_mutex_unlock (&is->priv->select_lock);

	if (nothing_to_do)
		return;

	g_signal_emit (is, signals[MAILBOX_SELECT], 0, mailbox);

	ic = camel_imapx_command_new (
		is, "SELECT", NULL, "SELECT %M", mailbox);

	if (is->use_qresync) {
		CamelFolder *folder;

		folder = imapx_server_ref_folder (is, mailbox);
		camel_imapx_command_add_qresync_parameter (ic, folder);
		g_clear_object (&folder);
	}

	ic->complete = imapx_command_select_done;
	camel_imapx_command_set_job (ic, job);

	imapx_command_start (is, ic);

	camel_imapx_command_unref (ic);
}

static void
imapx_server_set_streams (CamelIMAPXServer *is,
                          GInputStream *input_stream,
                          GOutputStream *output_stream)
{
	GConverter *logger;

	if (input_stream != NULL) {
		GInputStream *temp_stream;

		/* The logger produces debugging output. */
		logger = camel_imapx_logger_new (is->tagprefix);
		input_stream = g_converter_input_stream_new (
			input_stream, logger);
		g_clear_object (&logger);

		/* Buffer the input stream for parsing. */
		temp_stream = camel_imapx_input_stream_new (input_stream);
		g_object_bind_property (
			temp_stream, "close-base-stream",
			input_stream, "close-base-stream",
			G_BINDING_SYNC_CREATE);
		g_object_unref (input_stream);
		input_stream = temp_stream;
	}

	if (output_stream != NULL) {
		/* The logger produces debugging output. */
		logger = camel_imapx_logger_new (is->tagprefix);
		output_stream = g_converter_output_stream_new (
			output_stream, logger);
		g_clear_object (&logger);
	}

	g_mutex_lock (&is->priv->stream_lock);

	/* Don't close the base streams so STARTTLS works correctly. */

	if (G_IS_FILTER_INPUT_STREAM (is->priv->input_stream)) {
		g_filter_input_stream_set_close_base_stream (
			G_FILTER_INPUT_STREAM (is->priv->input_stream),
			FALSE);
	}

	if (G_IS_FILTER_OUTPUT_STREAM (is->priv->output_stream)) {
		g_filter_output_stream_set_close_base_stream (
			G_FILTER_OUTPUT_STREAM (is->priv->output_stream),
			FALSE);
	}

	g_clear_object (&is->priv->input_stream);
	is->priv->input_stream = input_stream;

	g_clear_object (&is->priv->output_stream);
	is->priv->output_stream = output_stream;

	g_mutex_unlock (&is->priv->stream_lock);
}

#if GLIB_CHECK_VERSION(2,39,0)
#ifdef G_OS_UNIX
static void
imapx_server_child_process_setup (gpointer user_data)
{
	gint fd;

	setsid ();

#ifdef TIOCNOTTY
	/* Detach from the controlling tty if we have one.  Otherwise,
	 * SSH might do something stupid like trying to use it instead
	 * of running $SSH_ASKPASS. */
	if ((fd = open ("/dev/tty", O_RDONLY)) != -1) {
		ioctl (fd, TIOCNOTTY, NULL);
		close (fd);
	}
#endif /* TIOCNOTTY */
}
#endif /* G_OS_UNIX */
#endif

static gboolean
connect_to_server_process (CamelIMAPXServer *is,
                           const gchar *cmd,
                           GError **error)
{
#if GLIB_CHECK_VERSION(2,39,0)
	GSubprocessLauncher *launcher;
	GSubprocess *subprocess = NULL;
	CamelNetworkSettings *network_settings;
	CamelProvider *provider;
	CamelSettings *settings;
	CamelIMAPXStore *store;
	CamelURL url;
	gchar **argv = NULL;
	gchar *buf;
	gchar *cmd_copy;
	gchar *full_cmd;
	const gchar *password;
	gchar *host;
	gchar *user;
	guint16 port;

	memset (&url, 0, sizeof (CamelURL));

	launcher = g_subprocess_launcher_new (
		G_SUBPROCESS_FLAGS_STDIN_PIPE |
		G_SUBPROCESS_FLAGS_STDOUT_PIPE |
		G_SUBPROCESS_FLAGS_STDERR_SILENCE);

#ifdef G_OS_UNIX
	g_subprocess_launcher_set_child_setup (
		launcher, imapx_server_child_process_setup,
		NULL, (GDestroyNotify) NULL);
#endif

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

	g_subprocess_launcher_setenv (launcher, "URL", buf, TRUE);
	g_subprocess_launcher_setenv (launcher, "URLHOST", host, TRUE);

	if (port > 0) {
		gchar *port_string;

		port_string = g_strdup_printf ("%u", port);
		g_subprocess_launcher_setenv (
			launcher, "URLPORT", port_string, TRUE);
		g_free (port_string);
	}

	if (user != NULL) {
		g_subprocess_launcher_setenv (
			launcher, "URLPORT", user, TRUE);
	}

	if (password != NULL) {
		g_subprocess_launcher_setenv (
			launcher, "URLPASSWD", password, TRUE);
	}

	g_free (buf);

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

	if (g_shell_parse_argv (full_cmd, NULL, &argv, error)) {
		subprocess = g_subprocess_launcher_spawnv (
			launcher, (const gchar * const *) argv, error);
		g_strfreev (argv);
	}

	g_free (full_cmd);
	g_object_unref (launcher);

	if (subprocess != NULL) {
		GInputStream *input_stream;
		GOutputStream *output_stream;

		g_mutex_lock (&is->priv->stream_lock);
		g_warn_if_fail (is->priv->subprocess == NULL);
		is->priv->subprocess = g_object_ref (subprocess);
		g_mutex_unlock (&is->priv->stream_lock);

		input_stream = g_subprocess_get_stdout_pipe (subprocess);
		output_stream = g_subprocess_get_stdin_pipe (subprocess);

		imapx_server_set_streams (is, input_stream, output_stream);

		g_object_unref (subprocess);
	}

	return TRUE;

#else /* GLIB_CHECK_VERSION(2,39,0) */

	g_set_error_literal (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		"GLib 2.39 or later is required to connect "
		"to an IMAP server through a shell command");

	return FALSE;
#endif
}

gboolean
imapx_connect_to_server (CamelIMAPXServer *is,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelNetworkSettings *network_settings;
	CamelNetworkSecurityMethod method;
	CamelIMAPXStore *store;
	CamelSettings *settings;
	GIOStream *connection = NULL;
	GIOStream *tls_stream;
	GSocket *socket;
	guint len;
	guchar *token;
	gint tok;
	CamelIMAPXCommand *ic;
	gchar *shell_command = NULL;
	gboolean use_shell_command;
	gboolean success = TRUE;
	gchar *host;
	GError *local_error = NULL;

	store = camel_imapx_server_ref_store (is);

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	method = camel_network_settings_get_security_method (network_settings);

	use_shell_command = camel_imapx_settings_get_use_shell_command (
		CAMEL_IMAPX_SETTINGS (settings));

	if (use_shell_command)
		shell_command = camel_imapx_settings_dup_shell_command (
			CAMEL_IMAPX_SETTINGS (settings));

	g_object_unref (settings);

	if (shell_command != NULL) {
		success = connect_to_server_process (is, shell_command, error);

		g_free (shell_command);

		if (success)
			goto connected;
		else
			goto exit;
	}

	connection = camel_network_service_connect_sync (
		CAMEL_NETWORK_SERVICE (store), cancellable, error);

	if (connection != NULL) {
		GInputStream *input_stream;
		GOutputStream *output_stream;

		/* Disable the Nagle algorithm with TCP_NODELAY, since IMAP
		 * commands should be issued immediately even we've not yet
		 * received a response to a previous command. */
		socket = g_socket_connection_get_socket (
			G_SOCKET_CONNECTION (connection));
		g_socket_set_option (
			socket, IPPROTO_TCP, TCP_NODELAY, 1, &local_error);
		if (local_error != NULL) {
			/* Failure to set the socket option is non-fatal. */
			g_warning ("%s: %s", G_STRFUNC, local_error->message);
			g_clear_error (&local_error);
		}

		g_mutex_lock (&is->priv->stream_lock);
		g_warn_if_fail (is->priv->connection == NULL);
		is->priv->connection = g_object_ref (connection);
		g_mutex_unlock (&is->priv->stream_lock);

		input_stream = g_io_stream_get_input_stream (connection);
		output_stream = g_io_stream_get_output_stream (connection);

		imapx_server_set_streams (is, input_stream, output_stream);

		/* Hang on to the connection reference in case we need to
		 * issue STARTTLS below. */
	} else {
		success = FALSE;
		goto exit;
	}

connected:
	while (1) {
		GInputStream *input_stream;

		input_stream = camel_imapx_server_ref_input_stream (is);

		tok = camel_imapx_input_stream_token (
			CAMEL_IMAPX_INPUT_STREAM (input_stream),
			&token, &len, cancellable, error);

		if (tok < 0) {
			success = FALSE;

		} else if (tok == '*') {
			success = imapx_untagged (
				is, input_stream, cancellable, error);

			if (success) {
				g_object_unref (input_stream);
				break;
			}

		} else {
			camel_imapx_input_stream_ungettoken (
				CAMEL_IMAPX_INPUT_STREAM (input_stream),
				tok, token, len);

			success = camel_imapx_input_stream_text (
				CAMEL_IMAPX_INPUT_STREAM (input_stream),
				&token, cancellable, error);

			g_free (token);
		}

		g_object_unref (input_stream);

		if (!success)
			goto exit;
	}

	if (!is->cinfo) {
		ic = camel_imapx_command_new (
			is, "CAPABILITY", NULL, "CAPABILITY");

		success = imapx_command_run (is, ic, cancellable, error);

		/* Server reported error. */
		if (success && ic->status->result != IMAPX_OK) {
			g_set_error (
				error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC,
				"%s", ic->status->text);
			success = FALSE;
		}

		camel_imapx_command_unref (ic);

		if (!success)
			goto exit;
	}

	if (method == CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT) {

		if (CAMEL_IMAPX_LACK_CAPABILITY (is->cinfo, STARTTLS)) {
			g_set_error (
				&local_error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC,
				_("Failed to connect to IMAP server %s in secure mode: %s"),
				host, _("STARTTLS not supported"));
			goto exit;
		}

		ic = camel_imapx_command_new (
			is, "STARTTLS", NULL, "STARTTLS");

		success = imapx_command_run (is, ic, cancellable, error);

		/* Server reported error. */
		if (success && ic->status->result != IMAPX_OK) {
			g_set_error (
				error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC,
				"%s", ic->status->text);
			success = FALSE;
		}

		if (success) {
			/* See if we got new capabilities
			 * in the STARTTLS response. */
			imapx_free_capability (is->cinfo);
			is->cinfo = NULL;
			if (ic->status->condition == IMAPX_CAPABILITY) {
				is->cinfo = ic->status->u.cinfo;
				ic->status->u.cinfo = NULL;
				c (is->tagprefix, "got capability flags %08x\n", is->cinfo ? is->cinfo->capa : 0xFFFFFFFF);
				imapx_server_stash_command_arguments (is);
			}
		}

		camel_imapx_command_unref (ic);

		if (!success)
			goto exit;

		tls_stream = camel_network_service_starttls (
			CAMEL_NETWORK_SERVICE (store), connection, error);

		if (tls_stream != NULL) {
			GInputStream *input_stream;
			GOutputStream *output_stream;

			g_mutex_lock (&is->priv->stream_lock);
			g_object_unref (is->priv->connection);
			is->priv->connection = g_object_ref (tls_stream);
			g_mutex_unlock (&is->priv->stream_lock);

			input_stream =
				g_io_stream_get_input_stream (tls_stream);
			output_stream =
				g_io_stream_get_output_stream (tls_stream);

			imapx_server_set_streams (
				is, input_stream, output_stream);

			g_object_unref (tls_stream);
		} else {
			g_prefix_error (
				error,
				_("Failed to connect to IMAP server %s in secure mode: "),
				host);
			success = FALSE;
			goto exit;
		}

		/* Get new capabilities if they weren't already given */
		if (is->cinfo == NULL) {
			ic = camel_imapx_command_new (
				is, "CAPABILITY", NULL, "CAPABILITY");
			success = imapx_command_run (is, ic, cancellable, error);
			camel_imapx_command_unref (ic);

			if (!success)
				goto exit;
		}
	}

exit:
	if (!success) {
		g_mutex_lock (&is->priv->stream_lock);

		g_clear_object (&is->priv->input_stream);
		g_clear_object (&is->priv->output_stream);
		g_clear_object (&is->priv->connection);
#if GLIB_CHECK_VERSION(2,39,0)
		g_clear_object (&is->priv->subprocess);
#endif

		if (is->cinfo != NULL) {
			imapx_free_capability (is->cinfo);
			is->cinfo = NULL;
		}

		g_mutex_unlock (&is->priv->stream_lock);
	}

	g_free (host);

	g_clear_object (&connection);
	g_clear_object (&store);

	return success;
}

gboolean
camel_imapx_server_is_connected (CamelIMAPXServer *imapx_server)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (imapx_server), FALSE);

	return imapx_server->state >= IMAPX_CONNECTED;
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
		CAMEL_AUTHENTICATION_ERROR);

	store = camel_imapx_server_ref_store (is);

	service = CAMEL_SERVICE (store);
	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	user = camel_network_settings_dup_user (network_settings);

	g_object_unref (settings);

	if (mechanism != NULL) {
		if (is->cinfo && !g_hash_table_lookup (is->cinfo->auth_types, mechanism)) {
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
	else if (ic->status->result == IMAPX_NO) {
		if (camel_imapx_store_is_connecting_concurrent_connection (store)) {
			/* At least one connection succeeded, probably max connection limit
			   set on the server had been reached, thus use special error code
			   for it, to instruct the connection manager to decrease the limit
			   and use already created connection. */
			g_set_error_literal (
				error, CAMEL_IMAPX_SERVER_ERROR,
				CAMEL_IMAPX_SERVER_ERROR_CONCURRENT_CONNECT_FAILED,
				ic->status->text ? ic->status->text : _("Unknown error"));
			result = CAMEL_AUTHENTICATION_ERROR;
		} else {
			result = CAMEL_AUTHENTICATION_REJECTED;
		}
	} else {
		g_set_error_literal (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			ic->status->text ? ic->status->text : _("Unknown error"));
		result = CAMEL_AUTHENTICATION_ERROR;
	}

	/* Forget old capabilities after login. */
	if (result == CAMEL_AUTHENTICATION_ACCEPTED) {
		if (is->cinfo) {
			imapx_free_capability (is->cinfo);
			is->cinfo = NULL;
		}

		if (ic->status->condition == IMAPX_CAPABILITY) {
			is->cinfo = ic->status->u.cinfo;
			ic->status->u.cinfo = NULL;
			c (is->tagprefix, "got capability flags %08x\n", is->cinfo ? is->cinfo->capa : 0xFFFFFFFF);
			imapx_server_stash_command_arguments (is);
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
	gboolean use_qresync;
	gboolean success = FALSE;

	store = camel_imapx_server_ref_store (is);

	service = CAMEL_SERVICE (store);
	session = camel_service_ref_session (service);

	settings = camel_service_ref_settings (service);

	mechanism = camel_network_settings_dup_auth_mechanism (
		CAMEL_NETWORK_SETTINGS (settings));

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

	/* After login we re-capa unless the server already told us. */
	if (is->cinfo == NULL) {
		GError *local_error = NULL;

		ic = camel_imapx_command_new (
			is, "CAPABILITY", NULL, "CAPABILITY");
		imapx_command_run (is, ic, cancellable, &local_error);
		camel_imapx_command_unref (ic);

		if (local_error != NULL) {
			g_propagate_error (error, local_error);
			goto exception;
		}
	}

	is->state = IMAPX_AUTHENTICATED;

preauthed:
	/* Fetch namespaces (if supported). */
	if (CAMEL_IMAPX_HAVE_CAPABILITY (is->cinfo, NAMESPACE)) {
		GError *local_error = NULL;

		ic = camel_imapx_command_new (
			is, "NAMESPACE", NULL, "NAMESPACE");
		imapx_command_run (is, ic, cancellable, &local_error);
		camel_imapx_command_unref (ic);

		if (local_error != NULL) {
			g_propagate_error (error, local_error);
			goto exception;
		}
	}

	/* Enable quick mailbox resynchronization (if supported). */
	if (use_qresync && CAMEL_IMAPX_HAVE_CAPABILITY (is->cinfo, QRESYNC)) {
		GError *local_error = NULL;

		ic = camel_imapx_command_new (
			is, "ENABLE", NULL, "ENABLE CONDSTORE QRESYNC");
		imapx_command_run (is, ic, cancellable, &local_error);
		camel_imapx_command_unref (ic);

		if (local_error != NULL) {
			g_propagate_error (error, local_error);
			goto exception;
		}

		is->use_qresync = TRUE;
	} else {
		is->use_qresync = FALSE;
	}

	/* Set NOTIFY options after enabling QRESYNC (if supported). */
	if (CAMEL_IMAPX_HAVE_CAPABILITY (is->cinfo, NOTIFY)) {
		GError *local_error = NULL;

		/* XXX The list of FETCH attributes is negotiable. */
		ic = camel_imapx_command_new (
			is, "NOTIFY", NULL, "NOTIFY SET "
			"(selected "
			"(MessageNew (UID RFC822.SIZE RFC822.HEADER FLAGS)"
			" MessageExpunge"
			" FlagChange)) "
			"(personal "
			"(MessageNew"
			" MessageExpunge"
			" MailboxName"
			" SubscriptionChange))");
		imapx_command_run (is, ic, cancellable, &local_error);
		camel_imapx_command_unref (ic);

		if (local_error != NULL) {
			g_propagate_error (error, local_error);
			goto exception;
		}
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

	g_object_unref (session);
	g_object_unref (store);

	return success;
}

/* ********************************************************************** */

static void
imapx_command_fetch_message_done (CamelIMAPXServer *is,
                                  CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	GetMessageData *data;
	CamelIMAPXMailbox *mailbox;
	GCancellable *cancellable;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	/* This is only for pushing status messages. */
	cancellable = camel_imapx_job_get_cancellable (job);

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_if_fail (mailbox != NULL);

	/* We either have more to fetch (partial mode?), we are complete,
	 * or we failed.  Failure is handled in the fetch code, so
	 * we just return the job, or keep it alive with more requests */

	g_atomic_int_add (&job->commands, -1);

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error fetching message"));

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
				cancellable,
				(data->fetch_offset *100) / data->size);

			new_ic = camel_imapx_command_new (
				is, "FETCH", mailbox,
				"UID FETCH %t (BODY.PEEK[]",
				data->uid);
			camel_imapx_command_add (new_ic, "<%u.%u>", data->fetch_offset, MULTI_SIZE);
			camel_imapx_command_add (new_ic, ")");
			new_ic->complete = imapx_command_fetch_message_done;
			camel_imapx_command_set_job (new_ic, job);
			new_ic->pri = job->pri - 1;
			data->fetch_offset += MULTI_SIZE;
			g_atomic_int_add (&job->commands, 1);

			imapx_command_queue (is, new_ic);

			camel_imapx_command_unref (new_ic);

			goto exit;
		}
	}

	/* If we have more messages to fetch, skip the rest. */
	if (g_atomic_int_get (&job->commands) > 0) {
		/* Make sure no command will starve in a queue */
		QUEUE_LOCK (is);
		imapx_command_start_next (is);
		QUEUE_UNLOCK (is);

		goto exit;
	}

	/* No more messages to fetch, let's wrap things up. */

	if (local_error == NULL) {
		g_io_stream_close (data->stream, cancellable, &local_error);
		g_prefix_error (
			&local_error, "%s: ",
			_("Failed to close the tmp stream"));
	}

	if (local_error == NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error fetching message"));
	}

	if (local_error == NULL) {
		gchar *cur_filename;
		gchar *tmp_filename;
		gchar *dirname;

		cur_filename = camel_data_cache_get_filename (
			data->message_cache, "cur", data->uid);

		tmp_filename = camel_data_cache_get_filename (
			data->message_cache, "tmp", data->uid);

		dirname = g_path_get_dirname (cur_filename);
		g_mkdir_with_parents (dirname, 0700);
		g_free (dirname);

		if (g_rename (tmp_filename, cur_filename) == 0) {
			/* Exchange the "tmp" stream for the "cur" stream. */
			g_clear_object (&data->stream);
			data->stream = camel_data_cache_get (
				data->message_cache, "cur",
				data->uid, &local_error);
		} else {
			g_set_error (
				&local_error, G_FILE_ERROR,
				g_file_error_from_errno (errno),
				"%s: %s",
				_("Failed to copy the tmp file"),
				g_strerror (errno));
		}

		g_free (cur_filename);
		g_free (tmp_filename);
	}

	/* Delete the 'tmp' file only if the operation wasn't cancelled. It's because
	   cancelled operations end before they are properly finished (IMAP-protocol speaking),
	   thus if any other GET_MESSAGE operation was waiting for this job, then it
	   realized that the message was not downloaded and opened its own "tmp" file, but
	   of the same name, thus this remove would drop file which could be used
	   by a different GET_MESSAGE job. */
	if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		camel_data_cache_remove (data->message_cache, "tmp", data->uid, NULL);

	/* Avoid possible use-after-free when the imapx_unregister_job() can
	   also free the 'job' structure. */
	if (local_error != NULL) {
		camel_imapx_job_take_error (job, local_error);
		local_error = NULL;
	}

	imapx_unregister_job (is, job);

exit:
	if (local_error != NULL)
		camel_imapx_job_take_error (job, local_error);

	g_object_unref (mailbox);
}

static gboolean
imapx_job_get_message_start (CamelIMAPXJob *job,
                             CamelIMAPXServer *is,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXMailbox *mailbox;
	GetMessageData *data;
	gint i;
	gboolean success = TRUE;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_val_if_fail (mailbox != NULL, FALSE);

	if (data->use_multi_fetch) {
		for (i = 0; i < 3 && data->fetch_offset < data->size; i++) {
			ic = camel_imapx_command_new (
				is, "FETCH", mailbox,
				"UID FETCH %t (BODY.PEEK[]",
				data->uid);
			camel_imapx_command_add (ic, "<%u.%u>", data->fetch_offset, MULTI_SIZE);
			camel_imapx_command_add (ic, ")");
			ic->complete = imapx_command_fetch_message_done;
			camel_imapx_command_set_job (ic, job);
			ic->pri = job->pri;
			data->fetch_offset += MULTI_SIZE;
			g_atomic_int_add (&job->commands, 1);

			imapx_command_queue (is, ic);

			camel_imapx_command_unref (ic);
		}
	} else {
		ic = camel_imapx_command_new (
			is, "FETCH", mailbox,
			"UID FETCH %t (BODY.PEEK[])",
			data->uid);
		ic->complete = imapx_command_fetch_message_done;
		camel_imapx_command_set_job (ic, job);
		ic->pri = job->pri;
		g_atomic_int_add (&job->commands, 1);

		imapx_command_queue (is, ic);

		camel_imapx_command_unref (ic);
	}

	g_object_unref (mailbox);

	return success;
}

static gboolean
imapx_job_get_message_matches (CamelIMAPXJob *job,
                               CamelIMAPXMailbox *mailbox,
                               const gchar *uid)
{
	GetMessageData *data;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	if (!camel_imapx_job_has_mailbox (job, mailbox))
		return FALSE;

	if (g_strcmp0 (uid, data->uid) != 0)
		return FALSE;

	return TRUE;
}

/* ********************************************************************** */

static void
imapx_command_copy_messages_step_done (CamelIMAPXServer *is,
                                       CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	CamelFolder *folder;
	CamelIMAPXMailbox *mailbox;
	CopyMessagesData *data;
	GPtrArray *uids;
	gint i;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_if_fail (mailbox != NULL);

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_if_fail (folder != NULL);

	uids = data->uids;
	i = data->index;

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		if (data->use_move_command)
			g_prefix_error (
				&local_error, "%s: ",
				_("Error moving messages"));
		else
			g_prefix_error (
				&local_error, "%s: ",
				_("Error copying messages"));
		camel_imapx_job_take_error (job, local_error);
		goto exit;
	}

	if (ic->status && ic->status->u.copyuid.uids && ic->status->u.copyuid.copied_uids &&
	    ic->status->u.copyuid.uids->len == ic->status->u.copyuid.copied_uids->len) {
		CamelFolder *destination;

		destination = imapx_server_ref_folder (is, data->destination);
		if (destination) {
			CamelMessageInfo *source_info, *destination_info;
			CamelFolderChangeInfo *changes;
			gint ii;

			changes = camel_folder_change_info_new ();

			for (ii = 0; ii < ic->status->u.copyuid.uids->len; ii++) {
				gchar *uid;
				gboolean is_new = FALSE;

				uid = g_strdup_printf ("%d", g_array_index (ic->status->u.copyuid.uids, guint32, ii));
				source_info = camel_folder_summary_get (folder->summary, uid);
				g_free (uid);

				if (!source_info)
					continue;

				uid = g_strdup_printf ("%d", g_array_index (ic->status->u.copyuid.copied_uids, guint32, ii));
				destination_info = camel_folder_summary_get (folder->summary, uid);

				if (!destination_info) {
					is_new = TRUE;
					destination_info = camel_message_info_clone (source_info);
					destination_info->summary = destination->summary;
					camel_pstring_free (destination_info->uid);
					destination_info->uid = camel_pstring_strdup (uid);
				}

				g_free (uid);

				imapx_set_message_info_flags_for_new_message (
					destination_info,
					((CamelMessageInfoBase *) source_info)->flags,
					((CamelMessageInfoBase *) source_info)->user_flags,
					TRUE,
					((CamelMessageInfoBase *) source_info)->user_tags,
					camel_imapx_mailbox_get_permanentflags (data->destination));
				if (is_new)
					camel_folder_summary_add (destination->summary, destination_info);
				camel_folder_change_info_add_uid (changes, destination_info->uid);

				camel_message_info_unref (source_info);
				if (!is_new)
					camel_message_info_unref (destination_info);
			}

			if (camel_folder_change_info_changed (changes)) {
				camel_folder_summary_touch (destination->summary);
				camel_folder_summary_save_to_db (destination->summary, NULL);
				camel_folder_changed (destination, changes);
			}

			camel_folder_change_info_free (changes);
			g_object_unref (destination);
		}
	}

	if (data->delete_originals) {
		gint j;

		for (j = data->last_index; j < i; j++)
			camel_folder_delete_message (folder, uids->pdata[j]);
	}

	if (i < uids->len) {
		imapx_command_copy_messages_step_start (
			is, job, i, &local_error);

		if (local_error != NULL)
			camel_imapx_job_take_error (job, local_error);
	}

exit:
	g_object_unref (folder);
	g_object_unref (mailbox);

	imapx_unregister_job (is, job);
}

static gboolean
imapx_command_copy_messages_step_start (CamelIMAPXServer *is,
                                        CamelIMAPXJob *job,
                                        gint index,
                                        GError **error)
{
	CamelIMAPXMailbox *mailbox;
	CamelIMAPXCommand *ic;
	CopyMessagesData *data;
	GPtrArray *uids;
	gint i = index;
	gboolean success = TRUE;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_val_if_fail (mailbox != NULL, FALSE);

	uids = data->uids;

	if (data->use_move_command)
		ic = camel_imapx_command_new (is, "MOVE", mailbox, "UID MOVE ");
	else
		ic = camel_imapx_command_new (is, "COPY", mailbox, "UID COPY ");
	ic->complete = imapx_command_copy_messages_step_done;
	camel_imapx_command_set_job (ic, job);
	ic->pri = job->pri;
	data->last_index = i;

	g_object_unref (mailbox);

	for (; i < uids->len; i++) {
		gint res;
		const gchar *uid = (gchar *) g_ptr_array_index (uids, i);

		res = imapx_uidset_add (&data->uidset, ic, uid);
		if (res == 1) {
			camel_imapx_command_add (ic, " %M", data->destination);
			data->index = i + 1;
			imapx_command_queue (is, ic);
			goto exit;
		}
	}

	data->index = i;
	if (imapx_uidset_done (&data->uidset, ic)) {
		camel_imapx_command_add (ic, " %M", data->destination);
		imapx_command_queue (is, ic);
		goto exit;
	}

exit:
	camel_imapx_command_unref (ic);

	return success;
}

static gboolean
imapx_job_copy_messages_start (CamelIMAPXJob *job,
                               CamelIMAPXServer *is,
                               GCancellable *cancellable,
                               GError **error)
{
	CamelIMAPXMailbox *mailbox;
	CopyMessagesData *data;
	gboolean success;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_val_if_fail (mailbox != NULL, FALSE);

	success = imapx_server_sync_changes (
		is, mailbox, job->type, job->pri, cancellable, error);
	if (!success)
		imapx_unregister_job (is, job);

	/* XXX Should we still do this even if a failure occurred? */
	g_ptr_array_sort (data->uids, (GCompareFunc) imapx_uids_array_cmp);
	imapx_uidset_init (&data->uidset, 0, MAX_COMMAND_LEN);

	g_object_unref (mailbox);

	return imapx_command_copy_messages_step_start (is, job, 0, error);
}

/* ********************************************************************** */

static void
imapx_command_append_message_done (CamelIMAPXServer *is,
                                   CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	CamelIMAPXFolder *ifolder;
	CamelIMAPXMailbox *mailbox;
	CamelFolder *folder;
	CamelMessageInfo *mi;
	AppendMessageData *data;
	gchar *cur, *old_uid;
	guint32 uidvalidity;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_if_fail (mailbox != NULL);

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_if_fail (folder != NULL);

	uidvalidity = camel_imapx_mailbox_get_uidvalidity (mailbox);

	ifolder = CAMEL_IMAPX_FOLDER (folder);

	/* Append done.  If we the server supports UIDPLUS we will get
	 * an APPENDUID response with the new uid.  This lets us move the
	 * message we have directly to the cache and also create a correctly
	 * numbered MessageInfo, without losing any information.  Otherwise
	 * we have to wait for the server to let us know it was appended. */

	mi = camel_message_info_clone (data->info);
	old_uid = g_strdup (data->info->uid);

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error appending message"));
		camel_imapx_job_take_error (job, local_error);

	} else if (ic->status && ic->status->condition == IMAPX_APPENDUID) {
		c (is->tagprefix, "Got appenduid %d %d\n", (gint) ic->status->u.appenduid.uidvalidity, (gint) ic->status->u.appenduid.uid);
		if (ic->status->u.appenduid.uidvalidity == uidvalidity) {
			CamelFolderChangeInfo *changes;

			data->appended_uid = g_strdup_printf ("%u", (guint) ic->status->u.appenduid.uid);
			mi->uid = camel_pstring_add (data->appended_uid, FALSE);

			cur = camel_data_cache_get_filename  (ifolder->cache, "cur", mi->uid);
			if (g_rename (data->path, cur) == -1 && errno != ENOENT) {
				g_warning ("%s: Failed to rename '%s' to '%s': %s", G_STRFUNC, data->path, cur, g_strerror (errno));
			}

			imapx_set_message_info_flags_for_new_message (
				mi,
				((CamelMessageInfoBase *) data->info)->flags,
				((CamelMessageInfoBase *) data->info)->user_flags,
				TRUE,
				((CamelMessageInfoBase *) data->info)->user_tags,
				camel_imapx_mailbox_get_permanentflags (mailbox));
			camel_folder_summary_add (folder->summary, mi);
			changes = camel_folder_change_info_new ();
			camel_folder_change_info_add_uid (changes, mi->uid);
			camel_folder_changed (folder, changes);
			camel_folder_change_info_free (changes);

			g_free (cur);
		} else {
			c (is->tagprefix, "but uidvalidity changed \n");
		}
	}

	camel_data_cache_remove (ifolder->cache, "new", old_uid, NULL);
	g_free (old_uid);

	g_object_unref (folder);
	g_object_unref (mailbox);

	imapx_unregister_job (is, job);
}

static const gchar *
get_month_str (gint month)
{
	static const gchar tm_months[][4] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};

	if (month < 1 || month > 12)
		return NULL;

	return tm_months[month - 1];
}

static gboolean
imapx_job_append_message_start (CamelIMAPXJob *job,
                                CamelIMAPXServer *is,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelIMAPXMailbox *mailbox;
	CamelIMAPXCommand *ic;
	AppendMessageData *data;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_val_if_fail (mailbox != NULL, FALSE);

	if (data->date_time > 0) {
		gchar *date_time;
		struct tm stm;

		gmtime_r (&data->date_time, &stm);

		/* Store always in UTC */
		date_time = g_strdup_printf (
			"\"%02d-%s-%04d %02d:%02d:%02d +0000\"",
			stm.tm_mday,
			get_month_str (stm.tm_mon + 1),
			stm.tm_year + 1900,
			stm.tm_hour,
			stm.tm_min,
			stm.tm_sec);

		ic = camel_imapx_command_new (
			is, "APPEND", NULL,
			"APPEND %M %F %t %P", mailbox,
			((CamelMessageInfoBase *) data->info)->flags,
			((CamelMessageInfoBase *) data->info)->user_flags,
			date_time,
			data->path);

		g_free (date_time);
	} else {
		ic = camel_imapx_command_new (
			is, "APPEND", NULL,
			"APPEND %M %F %P", mailbox,
			((CamelMessageInfoBase *) data->info)->flags,
			((CamelMessageInfoBase *) data->info)->user_flags,
			data->path);
	}

	ic->complete = imapx_command_append_message_done;
	camel_imapx_command_set_job (ic, job);
	ic->pri = job->pri;
	g_atomic_int_add (&job->commands, 1);

	imapx_command_queue (is, ic);

	camel_imapx_command_unref (ic);

	g_object_unref (mailbox);

	return TRUE;
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
			camel_message_info_unref (info);
			e ('?', "Ignoring offline uid '%s'\n", camel_message_info_uid (info));
		} else {
			camel_message_info_unref (info);
			break;
		}
	}

	return index;
}

static void
imapx_command_step_fetch_done (CamelIMAPXServer *is,
                               CamelIMAPXCommand *ic)
{
	CamelIMAPXMailbox *mailbox;
	CamelIMAPXSummary *isum;
	CamelIMAPXJob *job;
	CamelFolder *folder;
	RefreshInfoData *data;
	gint i;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_if_fail (mailbox != NULL);

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_if_fail (folder != NULL);

	data->scan_changes = FALSE;

	isum = CAMEL_IMAPX_SUMMARY (folder->summary);

	i = data->index;

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error fetching message headers"));
		camel_imapx_job_take_error (job, local_error);
		goto exit;
	}

	if (camel_folder_change_info_changed (data->changes)) {
		imapx_update_store_summary (folder);
		camel_folder_summary_save_to_db (folder->summary, NULL);
		camel_folder_changed (folder, data->changes);
	}

	camel_folder_change_info_clear (data->changes);

	if (i < data->infos->len) {
		ic = camel_imapx_command_new (
			is, "FETCH", mailbox, "UID FETCH ");
		ic->complete = imapx_command_step_fetch_done;
		camel_imapx_command_set_job (ic, job);
		ic->pri = job->pri - 1;

		data->last_index = i;

		for (; i < data->infos->len; i++) {
			gint res;
			struct _refresh_info *r = &g_array_index (data->infos, struct _refresh_info, i);

			if (!r->exists) {
				res = imapx_uidset_add (&data->uidset, ic, r->uid);
				if (res == 1) {
					camel_imapx_command_add (ic, " (RFC822.SIZE RFC822.HEADER)");
					data->index = i + 1;

					imapx_command_queue (is, ic);

					camel_imapx_command_unref (ic);

					g_object_unref (folder);
					g_object_unref (mailbox);

					return;
				}
			}
		}

		data->index = data->infos->len;
		if (imapx_uidset_done (&data->uidset, ic)) {
			camel_imapx_command_add (ic, " (RFC822.SIZE RFC822.HEADER)");

			imapx_command_queue (is, ic);

			camel_imapx_command_unref (ic);

			g_object_unref (folder);
			g_object_unref (mailbox);

			return;
		}

		/* XXX What fate for our newly-created but unsubmitted
		 *     CamelIMAPXCommand if we get here?  I guess just
		 *     discard it and move on?  Also warn so I know if
		 *     we're actually taking this branch for real. */
		camel_imapx_command_unref (ic);
		g_warn_if_reached ();
	}

	if (camel_folder_summary_count (folder->summary)) {
		gchar *uid;
		guint32 uidl;
		guint32 uidnext;

		uid = camel_imapx_dup_uid_from_summary_index (
			folder,
			camel_folder_summary_count (folder->summary) - 1);
		if (uid) {
			uidl = (guint32) strtoull (uid, NULL, 10);
			g_free (uid);

			uidl++;

			uidnext = camel_imapx_mailbox_get_uidnext (mailbox);

			if (uidl > uidnext) {
				c (
					is->tagprefix,
					"Updating uidnext for '%s' to %ul\n",
					camel_imapx_mailbox_get_name (mailbox),
					uidl);
				camel_imapx_mailbox_set_uidnext (mailbox, uidl);
			}
		}
	}

	isum->uidnext = camel_imapx_mailbox_get_uidnext (mailbox);

exit:
	refresh_info_data_infos_free (data);

	g_object_unref (folder);
	g_object_unref (mailbox);

	imapx_unregister_job (is, job);
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

static void
imapx_job_scan_changes_done (CamelIMAPXServer *is,
                             CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	CamelIMAPXMailbox *mailbox;
	CamelIMAPXSettings *settings;
	CamelFolder *folder;
	RefreshInfoData *data;
	GCancellable *cancellable;
	guint uidset_size;
	guint32 unseen;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	/* This is only for pushing status messages. */
	cancellable = camel_imapx_job_get_cancellable (job);

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_if_fail (mailbox != NULL);

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_if_fail (folder != NULL);

	data->scan_changes = FALSE;

	settings = camel_imapx_server_ref_settings (is);
	uidset_size = camel_imapx_settings_get_batch_fetch_count (settings);
	g_object_unref (settings);

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error retrieving message"));
		camel_imapx_job_take_error (job, local_error);

	} else {
		GCompareDataFunc uid_cmp = imapx_uid_cmp;
		CamelMessageInfo *s_minfo = NULL;
		CamelIMAPXMessageInfo *info;
		CamelFolderSummary *s = folder->summary;
		GList *removed = NULL, *l;
		gboolean fetch_new = FALSE;
		gint i;
		guint j = 0;
		GPtrArray *uids;

		/* Actually we wanted to do this after the SELECT but before the
		 * FETCH command was issued. But this should suffice. */
		((CamelIMAPXSummary *) s)->uidnext =
			camel_imapx_mailbox_get_uidnext (mailbox);
		((CamelIMAPXSummary *) s)->modseq =
			camel_imapx_mailbox_get_highestmodseq (mailbox);

		/* Here we do the typical sort/iterate/merge loop.
		 * If the server flags dont match what we had, we modify our
		 * flags to pick up what the server now has - but we merge
		 * not overwrite */

		/* FIXME: We also have to check the offline directory for
		 * anything missing in our summary, and also queue up jobs
		 * for all outstanding messages to be uploaded */

		camel_folder_summary_lock (s);

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
				camel_message_info_unref (s_minfo);
				s_minfo = NULL;

				j = imapx_index_next (uids, s, j);
				if (j < uids->len)
					s_minfo = camel_folder_summary_get (s, g_ptr_array_index (uids, j));
			}

			info = NULL;
			if (s_minfo && uid_cmp (s_minfo->uid, r->uid, s) == 0) {
				info = (CamelIMAPXMessageInfo *) s_minfo;

				if (imapx_update_message_info_flags (
						(CamelMessageInfo *) info,
						r->server_flags,
						r->server_user_flags,
						camel_imapx_mailbox_get_permanentflags (mailbox),
						folder, FALSE))
					camel_folder_change_info_change_uid (
						data->changes,
						camel_message_info_uid (s_minfo));
				r->exists = TRUE;
			} else
				fetch_new = TRUE;

			if (s_minfo) {
				camel_message_info_unref (s_minfo);
				s_minfo = NULL;
			}

			if (j >= uids->len)
				break;

			j = imapx_index_next (uids, s, j);
			if (j < uids->len)
				s_minfo = camel_folder_summary_get (s, g_ptr_array_index (uids, j));
		}

		if (s_minfo)
			camel_message_info_unref (s_minfo);

		while (j < uids->len) {
			s_minfo = camel_folder_summary_get (s, g_ptr_array_index (uids, j));

			if (!s_minfo) {
				j++;
				continue;
			}

			e (is->tagprefix, "Message %s vanished\n", s_minfo->uid);
			removed = g_list_prepend (removed, (gpointer) g_strdup (s_minfo->uid));
			camel_message_info_unref (s_minfo);
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
		imapx_update_store_summary (folder);

		camel_folder_summary_unlock (s);

		if (camel_folder_change_info_changed (data->changes))
			camel_folder_changed (folder, data->changes);
		camel_folder_change_info_clear (data->changes);

		camel_folder_summary_free_array (uids);

		/* If we have any new messages, download their headers, but only a few (100?) at a time */
		if (fetch_new) {
			job->pop_operation_msg = TRUE;

			camel_operation_push_message (
				cancellable,
				_("Fetching summary information for new messages in '%s'"),
				camel_folder_get_display_name (folder));

			imapx_uidset_init (&data->uidset, uidset_size, 0);
			/* These are new messages which arrived since we last knew the unseen count;
			 * update it as they arrive. */
			data->update_unseen = TRUE;

			g_object_unref (folder);
			g_object_unref (mailbox);

			return imapx_command_step_fetch_done (is, ic);
		}
	}

	refresh_info_data_infos_free (data);

	/* There's no sane way to get the server-side unseen count
	 * on the select mailbox, so just work it out from the flags. */
	unseen = camel_folder_summary_get_unread_count (folder->summary);
	camel_imapx_mailbox_set_unseen (mailbox, unseen);

	g_object_unref (folder);
	g_object_unref (mailbox);

	imapx_unregister_job (is, job);
}

static gboolean
imapx_job_scan_changes_start (CamelIMAPXJob *job,
                              CamelIMAPXServer *is,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelFolder *folder;
	CamelIMAPXCommand *ic;
	CamelIMAPXMailbox *mailbox;
	RefreshInfoData *data;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_val_if_fail (mailbox != NULL, FALSE);

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_val_if_fail (folder != NULL, FALSE);

	job->pop_operation_msg = TRUE;

	camel_operation_push_message (
		cancellable,
		_("Scanning for changed messages in '%s'"),
		camel_folder_get_display_name (folder));

	ic = camel_imapx_command_new (
		is, "FETCH", mailbox,
		"UID FETCH 1:* (UID FLAGS)");
	camel_imapx_command_set_job (ic, job);
	ic->complete = imapx_job_scan_changes_done;

	data->scan_changes = TRUE;
	ic->pri = job->pri;
	refresh_info_data_infos_free (data);
	data->infos = g_array_new (0, 0, sizeof (struct _refresh_info));

	imapx_command_queue (is, ic);

	camel_imapx_command_unref (ic);

	g_object_unref (folder);
	g_object_unref (mailbox);

	return TRUE;
}

static void
imapx_command_fetch_new_messages_done (CamelIMAPXServer *is,
                                       CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	CamelIMAPXSummary *isum;
	CamelIMAPXMailbox *mailbox;
	CamelFolder *folder;
	RefreshInfoData *data;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_if_fail (mailbox != NULL);

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_if_fail (folder != NULL);

	isum = CAMEL_IMAPX_SUMMARY (folder->summary);

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error fetching new messages"));
		camel_imapx_job_take_error (job, local_error);
		goto exit;
	}

	if (camel_folder_change_info_changed (data->changes)) {
		camel_folder_summary_save_to_db (folder->summary, NULL);
		imapx_update_store_summary (folder);
		camel_folder_changed (folder, data->changes);
		camel_folder_change_info_clear (data->changes);
	}

	if (camel_folder_summary_count (folder->summary)) {
		gchar *uid;
		guint32 uidl;
		guint32 uidnext;

		uid = camel_imapx_dup_uid_from_summary_index (
			folder,
			camel_folder_summary_count (folder->summary) - 1);
		if (uid) {
			uidl = (guint32) strtoull (uid, NULL, 10);
			g_free (uid);

			uidl++;

			uidnext = camel_imapx_mailbox_get_uidnext (mailbox);

			if (uidl > uidnext) {
				c (
					is->tagprefix,
					"Updating uidnext for '%s' to %ul\n",
					camel_imapx_mailbox_get_name (mailbox),
					uidl);
				camel_imapx_mailbox_set_uidnext (mailbox, uidl);
			}
		}
	}

	isum->uidnext = camel_imapx_mailbox_get_uidnext (mailbox);

exit:
	g_object_unref (folder);
	g_object_unref (mailbox);

	imapx_unregister_job (is, job);
}

static void
imapx_command_fetch_new_uids_done (CamelIMAPXServer *is,
                                   CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	RefreshInfoData *data;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	data->scan_changes = FALSE;

	qsort (
		data->infos->data,
		data->infos->len,
		sizeof (struct _refresh_info),
		imapx_refresh_info_cmp_descending);

	imapx_command_step_fetch_done (is, ic);
}

static gboolean
imapx_job_fetch_new_messages_start (CamelIMAPXJob *job,
                                    CamelIMAPXServer *is,
                                    GCancellable *cancellable,
                                    GError **error)
{
	CamelIMAPXCommand *ic;
	CamelFolder *folder;
	CamelIMAPXMailbox *mailbox;
	CamelIMAPXSettings *settings;
	CamelSortType fetch_order;
	RefreshInfoData *data;
	guint32 total, diff;
	guint32 messages;
	guint uidset_size;
	gchar *uid = NULL;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_val_if_fail (mailbox != NULL, FALSE);

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_val_if_fail (folder != NULL, FALSE);

	settings = camel_imapx_server_ref_settings (is);
	fetch_order = camel_imapx_settings_get_fetch_order (settings);
	uidset_size = camel_imapx_settings_get_batch_fetch_count (settings);
	g_object_unref (settings);

	messages = camel_imapx_mailbox_get_messages (mailbox);

	total = camel_folder_summary_count (folder->summary);
	diff = messages - total;

	if (total > 0) {
		guint64 uidl;
		uid = camel_imapx_dup_uid_from_summary_index (folder, total - 1);
		if (uid) {
			uidl = strtoull (uid, NULL, 10);
			g_free (uid);
			uid = g_strdup_printf ("%" G_GUINT64_FORMAT, uidl + 1);
		} else {
			uid = g_strdup ("1");
		}
	} else
		uid = g_strdup ("1");

	job->pop_operation_msg = TRUE;

	camel_operation_push_message (
		cancellable,
		_("Fetching summary information for new messages in '%s'"),
		camel_folder_get_display_name (folder));

	if (diff > uidset_size || fetch_order == CAMEL_SORT_DESCENDING) {
		ic = camel_imapx_command_new (
			is, "FETCH", mailbox,
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
			is, "FETCH", mailbox,
			"UID FETCH %s:* (RFC822.SIZE RFC822.HEADER FLAGS)", uid);
		ic->pri = job->pri;
		ic->complete = imapx_command_fetch_new_messages_done;
	}

	camel_imapx_command_set_job (ic, job);

	imapx_command_queue (is, ic);

	camel_imapx_command_unref (ic);

	g_free (uid);

	g_object_unref (folder);
	g_object_unref (mailbox);

	return TRUE;
}

static gboolean
imapx_job_refresh_info_start (CamelIMAPXJob *job,
                              CamelIMAPXServer *is,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelIMAPXSummary *isum;
	CamelFolder *folder;
	CamelIMAPXMailbox *mailbox;
	const gchar *full_name;
	gboolean need_rescan = FALSE;
	gboolean is_selected = FALSE;
	gboolean can_qresync = FALSE;
	gboolean success;
	guint32 messages;
	guint32 unseen;
	guint32 uidnext;
	guint32 uidvalidity;
	guint64 highestmodseq;
	guint32 total;

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_val_if_fail (mailbox != NULL, FALSE);

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_val_if_fail (folder != NULL, FALSE);

	isum = CAMEL_IMAPX_SUMMARY (folder->summary);

	full_name = camel_folder_get_full_name (folder);

	/* Sync changes first, else unread count will not
	 * match. Need to think about better ways for this */
	success = imapx_server_sync_changes (
		is, mailbox, job->type, job->pri, cancellable, error);
	if (!success)
		goto done;

	messages = camel_imapx_mailbox_get_messages (mailbox);
	unseen = camel_imapx_mailbox_get_unseen (mailbox);
	uidnext = camel_imapx_mailbox_get_uidnext (mailbox);
	uidvalidity = camel_imapx_mailbox_get_uidvalidity (mailbox);
	highestmodseq = camel_imapx_mailbox_get_highestmodseq (mailbox);

#if 0	/* There are issues with this still; continue with the buggy
	 * behaviour where we issue STATUS on the current folder, for now. */
	if (is->priv->select_folder == folder)
		is_selected = TRUE;
#endif
	total = camel_folder_summary_count (folder->summary);

	if (uidvalidity > 0 && uidvalidity != isum->validity)
		need_rescan = TRUE;

	/* We don't have valid unread count or modseq for currently-selected server
	 * (unless we want to re-SELECT it). We fake unread count when fetching
	 * message flags, but don't depend on modseq for the selected folder */
	if (total != messages ||
	    isum->uidnext != uidnext ||
	    camel_folder_summary_get_unread_count (folder->summary) != unseen ||
	    (!is_selected && isum->modseq != highestmodseq))
		need_rescan = TRUE;

	/* This is probably the first check of this folder after startup;
	 * use STATUS to check whether the cached summary is valid, rather
	 * than blindly updating. Only for servers which support CONDSTORE
	 * though. */
	if (isum->modseq > 0 && highestmodseq == 0)
		need_rescan = FALSE;

	/* If we don't think there's anything to do, poke it to check */
	if (!need_rescan) {
		CamelIMAPXCommand *ic;

		#if 0  /* see comment for disabled bits above */
		if (is_selected) {
			/* We may not issue STATUS on the current folder. Use SELECT or NOOP instead. */
			if (0 /* server needs SELECT not just NOOP */) {
				if (imapx_in_idle (is))
					if (!imapx_stop_idle (is, error))
						goto done;
				/* This doesn't work -- this is an immediate command, not queued */
				imapx_maybe_select (is, folder)
			} else {
				/* Or maybe just NOOP, unless we're in IDLE in which case do nothing */
				if (!imapx_in_idle (is)) {
					if (!camel_imapx_server_noop (is, folder, cancellable, error))
						goto done;
				}
			}
		} else
		#endif
		{
			ic = camel_imapx_command_new (
				is, "STATUS", NULL, "STATUS %M (%t)",
				mailbox, is->priv->status_data_items);

			camel_imapx_command_set_job (ic, job);
			ic->pri = job->pri;

			success = imapx_command_run_sync (
				is, ic, cancellable, error);

			camel_imapx_command_unref (ic);

			if (!success) {
				g_prefix_error (
					error, "%s: ",
					_("Error refreshing folder"));
				goto done;
			}
		}

		/* Recalulate need_rescan */

		messages = camel_imapx_mailbox_get_messages (mailbox);
		unseen = camel_imapx_mailbox_get_unseen (mailbox);
		uidnext = camel_imapx_mailbox_get_uidnext (mailbox);
		highestmodseq = camel_imapx_mailbox_get_highestmodseq (mailbox);

		if (total != messages ||
		    isum->uidnext != uidnext ||
		    camel_folder_summary_get_unread_count (folder->summary) != unseen ||
		    (!is_selected && isum->modseq != highestmodseq))
			need_rescan = TRUE;
	}

	messages = camel_imapx_mailbox_get_messages (mailbox);
	unseen = camel_imapx_mailbox_get_unseen (mailbox);
	uidnext = camel_imapx_mailbox_get_uidnext (mailbox);
	uidvalidity = camel_imapx_mailbox_get_uidvalidity (mailbox);
	highestmodseq = camel_imapx_mailbox_get_highestmodseq (mailbox);

	if (is->use_qresync && isum->modseq > 0 && uidvalidity > 0)
		can_qresync = TRUE;

	e (
		is->tagprefix,
		"folder %s is %sselected, "
		"total %u / %u, unread %u / %u, modseq %"
		G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT
		", uidnext %u / %u: will %srescan\n",
		full_name,
		is_selected ? "" : "not ",
		total,
		messages,
		camel_folder_summary_get_unread_count (folder->summary),
		unseen,
		isum->modseq,
		highestmodseq,
		isum->uidnext,
		uidnext,
		need_rescan ? "" : "not ");

	/* Fetch new messages first, so that they appear to the user ASAP */
	if (messages > total || uidnext > isum->uidnext) {
		if (!total)
			need_rescan = FALSE;

		success = imapx_server_fetch_new_messages (
			is, mailbox, FALSE, FALSE, cancellable, error);
		if (!success)
			goto done;

		/* If QRESYNC-capable we'll have got all flags changes in SELECT */
		if (can_qresync)
			goto qresync_done;
	}

	if (!need_rescan)
		goto done;

	if (can_qresync) {
		/* Actually we only want to select it; no need for the NOOP */
		success = camel_imapx_server_noop (
			is, mailbox, cancellable, error);
		if (!success)
			goto done;
	qresync_done:
		messages = camel_imapx_mailbox_get_messages (mailbox);
		unseen = camel_imapx_mailbox_get_unseen (mailbox);
		highestmodseq = camel_imapx_mailbox_get_highestmodseq (mailbox);

		isum->modseq = highestmodseq;
		total = camel_folder_summary_count (folder->summary);
		if (total != messages ||
		    camel_folder_summary_get_unread_count (folder->summary) != unseen ||
		    (isum->modseq != highestmodseq)) {
			c (
				is->tagprefix,
				"Eep, after QRESYNC we're out of sync. "
				"total %u / %u, unread %u / %u, modseq %"
				G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT "\n",
				total, messages,
				camel_folder_summary_get_unread_count (folder->summary),
				unseen,
				isum->modseq,
				highestmodseq);
		} else {
			c (
				is->tagprefix,
				"OK, after QRESYNC we're still in sync. "
				"total %u / %u, unread %u / %u, modseq %"
				G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT "\n",
				total, messages,
				camel_folder_summary_get_unread_count (folder->summary),
				unseen,
				isum->modseq,
				highestmodseq);
			goto done;
		}
	}

	g_object_unref (folder);
	g_object_unref (mailbox);

	return imapx_job_scan_changes_start (job, is, cancellable, error);

done:
	g_object_unref (folder);
	g_object_unref (mailbox);

	imapx_unregister_job (is, job);

	return success;
}

static gboolean
imapx_job_refresh_info_matches (CamelIMAPXJob *job,
                                CamelIMAPXMailbox *mailbox,
                                const gchar *uid)
{
	return camel_imapx_job_has_mailbox (job, mailbox);
}

/* ********************************************************************** */

static void
imapx_command_expunge_done (CamelIMAPXServer *is,
                            CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	CamelIMAPXMailbox *mailbox;
	CamelFolder *folder;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_if_fail (mailbox != NULL);

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_if_fail (folder != NULL);

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error expunging message"));
		camel_imapx_job_take_error (job, local_error);

	} else {
		GPtrArray *uids;
		CamelStore *parent_store;
		const gchar *full_name;

		full_name = camel_folder_get_full_name (folder);
		parent_store = camel_folder_get_parent_store (folder);

		camel_folder_summary_lock (folder->summary);

		camel_folder_summary_save_to_db (folder->summary, NULL);
		uids = camel_db_get_folder_deleted_uids (parent_store->cdb_r, full_name, NULL);

		if (uids && uids->len) {
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
					camel_message_info_unref (mi);
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
		}

		if (uids)
			g_ptr_array_free (uids, TRUE);

		camel_folder_summary_unlock (folder->summary);
	}

	g_object_unref (folder);
	g_object_unref (mailbox);

	imapx_unregister_job (is, job);
}

static gboolean
imapx_job_expunge_start (CamelIMAPXJob *job,
                         CamelIMAPXServer *is,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXMailbox *mailbox;
	gboolean success;

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_val_if_fail (mailbox != NULL, FALSE);

	success = imapx_server_sync_changes (
		is, mailbox, job->type, job->pri, cancellable, error);

	if (success) {
		/* TODO handle UIDPLUS capability */
		ic = camel_imapx_command_new (
			is, "EXPUNGE", mailbox, "EXPUNGE");
		camel_imapx_command_set_job (ic, job);
		ic->pri = job->pri;
		ic->complete = imapx_command_expunge_done;

		imapx_command_queue (is, ic);

		camel_imapx_command_unref (ic);
	}

	g_object_unref (mailbox);

	return success;
}

static gboolean
imapx_job_expunge_matches (CamelIMAPXJob *job,
                           CamelIMAPXMailbox *mailbox,
                           const gchar *uid)
{
	return camel_imapx_job_has_mailbox (job, mailbox);
}

/* ********************************************************************** */

static void
imapx_command_list_done (CamelIMAPXServer *is,
                         CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error fetching folders"));
		camel_imapx_job_take_error (job, local_error);
	}

	e (is->tagprefix, "==== list or lsub completed ==== \n");
	imapx_unregister_job (is, job);
}

static void
imapx_command_list_lsub (CamelIMAPXServer *is,
                         CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	ListData *data;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error fetching folders"));
		camel_imapx_job_take_error (job, local_error);
		imapx_unregister_job (is, job);

	} else {
		ic = camel_imapx_command_new (
			is, "LIST", NULL,
			"LSUB \"\" %s",
			data->pattern);

		ic->pri = job->pri;
		camel_imapx_command_set_job (ic, job);
		ic->complete = imapx_command_list_done;

		imapx_command_queue (is, ic);

		camel_imapx_command_unref (ic);
	}
}

static gboolean
imapx_job_list_start (CamelIMAPXJob *job,
                      CamelIMAPXServer *is,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelIMAPXCommand *ic;
	ListData *data;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	if (is->priv->list_return_opts != NULL) {
		ic = camel_imapx_command_new (
			is, "LIST", NULL,
			"LIST \"\" %s RETURN (%t)",
			data->pattern,
			is->priv->list_return_opts);
		ic->complete = imapx_command_list_done;
	} else {
		ic = camel_imapx_command_new (
			is, "LIST", NULL,
			"LIST \"\" %s",
			data->pattern);
		ic->complete = imapx_command_list_lsub;
	}

	ic->pri = job->pri;
	camel_imapx_command_set_job (ic, job);

	imapx_command_queue (is, ic);

	camel_imapx_command_unref (ic);

	return TRUE;
}

static gboolean
imapx_job_list_matches (CamelIMAPXJob *job,
                        CamelIMAPXMailbox *mailbox,
                        const gchar *uid)
{
	return TRUE;  /* matches everything */
}

/* ********************************************************************** */

static void
imapx_command_create_mailbox_done (CamelIMAPXServer *is,
                                   CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error creating folder"));
		camel_imapx_job_take_error (job, local_error);
	}

	imapx_unregister_job (is, job);
}

static gboolean
imapx_job_create_mailbox_start (CamelIMAPXJob *job,
                                CamelIMAPXServer *is,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelIMAPXCommand *ic;
	MailboxData *data;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	ic = camel_imapx_command_new (
		is, "CREATE", NULL, "CREATE %m",
		data->mailbox_name);
	ic->pri = job->pri;
	camel_imapx_command_set_job (ic, job);
	ic->complete = imapx_command_create_mailbox_done;

	imapx_command_queue (is, ic);

	camel_imapx_command_unref (ic);

	return TRUE;
}

/* ********************************************************************** */

static void
imapx_command_delete_mailbox_done (CamelIMAPXServer *is,
                                   CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	MailboxData *data;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error deleting folder"));
		camel_imapx_job_take_error (job, local_error);

	} else {
		CamelIMAPXStore *imapx_store;

		/* Perform the same processing as imapx_untagged_list()
		 * would if the server notified us of a deleted mailbox. */

		imapx_store = camel_imapx_server_ref_store (is);

		camel_imapx_mailbox_deleted (data->mailbox);
		camel_imapx_store_emit_mailbox_updated (imapx_store, data->mailbox);

		g_clear_object (&imapx_store);
	}

	imapx_unregister_job (is, job);
}

static gboolean
imapx_job_delete_mailbox_start (CamelIMAPXJob *job,
                                CamelIMAPXServer *is,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelIMAPXStore *imapx_store;
	CamelIMAPXCommand *ic;
	MailboxData *data;
	CamelIMAPXMailbox *inbox;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	imapx_store = camel_imapx_server_ref_store (is);
	/* Keep going, even if this returns NULL. */
	inbox = camel_imapx_store_ref_mailbox (imapx_store, "INBOX");
	g_clear_object (&imapx_store);

	/* Make sure the to-be-deleted folder is not
	 * selected by selecting INBOX for this operation. */
	ic = camel_imapx_command_new (
		is, "DELETE", inbox,
		"DELETE %M", data->mailbox);
	ic->pri = job->pri;
	camel_imapx_command_set_job (ic, job);
	ic->complete = imapx_command_delete_mailbox_done;

	imapx_command_queue (is, ic);

	camel_imapx_command_unref (ic);

	g_clear_object (&inbox);

	return TRUE;
}

/* ********************************************************************** */

static void
imapx_command_rename_mailbox_done (CamelIMAPXServer *is,
                                   CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	MailboxData *data;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error renaming folder"));
		camel_imapx_job_take_error (job, local_error);

	} else {
		CamelIMAPXStore *imapx_store;

		/* Perform the same processing as imapx_untagged_list()
		 * would if the server notified us of a renamed mailbox. */

		imapx_store = camel_imapx_server_ref_store (is);
		camel_imapx_store_handle_mailbox_rename (imapx_store, data->mailbox, data->mailbox_name);

		g_clear_object (&imapx_store);
	}

	imapx_unregister_job (is, job);
}

static gboolean
imapx_job_rename_mailbox_start (CamelIMAPXJob *job,
                                CamelIMAPXServer *is,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXMailbox *inbox;
	MailboxData *data;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	imapx_store = camel_imapx_server_ref_store (is);
	inbox = camel_imapx_store_ref_mailbox (imapx_store, "INBOX");
	g_clear_object (&imapx_store);
	g_return_val_if_fail (inbox != NULL, FALSE);

	camel_imapx_job_set_mailbox (job, inbox);

	ic = camel_imapx_command_new (
		is, "RENAME", inbox, "RENAME %M %m",
		data->mailbox, data->mailbox_name);
	ic->pri = job->pri;
	camel_imapx_command_set_job (ic, job);
	ic->complete = imapx_command_rename_mailbox_done;

	imapx_command_queue (is, ic);

	camel_imapx_command_unref (ic);

	g_object_unref (inbox);

	return TRUE;
}

/* ********************************************************************** */

static void
imapx_command_subscribe_mailbox_done (CamelIMAPXServer *is,
                                      CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	MailboxData *data;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error subscribing to folder"));
		camel_imapx_job_take_error (job, local_error);

	} else {
		CamelIMAPXStore *imapx_store;

		/* Perform the same processing as imapx_untagged_list()
		 * would if the server notified us of a subscription. */

		imapx_store = camel_imapx_server_ref_store (is);

		camel_imapx_mailbox_subscribed (data->mailbox);
		camel_imapx_store_emit_mailbox_updated (imapx_store, data->mailbox);

		g_clear_object (&imapx_store);
	}

	imapx_unregister_job (is, job);
}

static gboolean
imapx_job_subscribe_mailbox_start (CamelIMAPXJob *job,
                                   CamelIMAPXServer *is,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelIMAPXCommand *ic;
	MailboxData *data;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	ic = camel_imapx_command_new (
		is, "SUBSCRIBE", NULL,
		"SUBSCRIBE %M", data->mailbox);

	ic->pri = job->pri;
	camel_imapx_command_set_job (ic, job);
	ic->complete = imapx_command_subscribe_mailbox_done;

	imapx_command_queue (is, ic);

	camel_imapx_command_unref (ic);

	return TRUE;
}

/* ********************************************************************** */

static void
imapx_command_unsubscribe_mailbox_done (CamelIMAPXServer *is,
                                        CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	MailboxData *data;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error unsubscribing from folder"));
		camel_imapx_job_take_error (job, local_error);

	} else {
		CamelIMAPXStore *imapx_store;

		/* Perform the same processing as imapx_untagged_list()
		 * would if the server notified us of an unsubscription. */

		imapx_store = camel_imapx_server_ref_store (is);

		camel_imapx_mailbox_unsubscribed (data->mailbox);
		camel_imapx_store_emit_mailbox_updated (imapx_store, data->mailbox);

		g_clear_object (&imapx_store);
	}

	imapx_unregister_job (is, job);
}

static gboolean
imapx_job_unsubscribe_mailbox_start (CamelIMAPXJob *job,
                                     CamelIMAPXServer *is,
                                     GCancellable *cancellable,
                                     GError **error)
{
	CamelIMAPXCommand *ic;
	MailboxData *data;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	ic = camel_imapx_command_new (
		is, "UNSUBSCRIBE", NULL,
		"UNSUBSCRIBE %M", data->mailbox);

	ic->pri = job->pri;
	camel_imapx_command_set_job (ic, job);
	ic->complete = imapx_command_unsubscribe_mailbox_done;

	imapx_command_queue (is, ic);

	camel_imapx_command_unref (ic);

	return TRUE;
}

/* ********************************************************************** */

static void
imapx_command_update_quota_info_done (CamelIMAPXServer *is,
                                      CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error retrieving quota information"));
		camel_imapx_job_take_error (job, local_error);
	}

	imapx_unregister_job (is, job);
}

static gboolean
imapx_job_update_quota_info_start (CamelIMAPXJob *job,
                                   CamelIMAPXServer *is,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXMailbox *mailbox;

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_val_if_fail (mailbox != NULL, FALSE);

	ic = camel_imapx_command_new (
		is, "GETQUOTAROOT", NULL,
		"GETQUOTAROOT %M", mailbox);
	ic->pri = job->pri;
	camel_imapx_command_set_job (ic, job);
	ic->complete = imapx_command_update_quota_info_done;

	imapx_command_queue (is, ic);

	camel_imapx_command_unref (ic);

	g_clear_object (&mailbox);

	return TRUE;
}

/* ********************************************************************** */

static void
imapx_command_uid_search_done (CamelIMAPXServer *is,
                               CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	SearchData *data;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (&local_error, "%s: ", _("Search failed"));
		camel_imapx_job_take_error (job, local_error);
	}

	/* Don't worry about the success state and presence of search
	 * results not agreeing here.  camel_imapx_server_uid_search()
	 * will disregard the search results if an error occurred. */
	g_mutex_lock (&is->priv->search_results_lock);
	data->results = is->priv->search_results;
	is->priv->search_results = NULL;
	g_mutex_unlock (&is->priv->search_results_lock);

	imapx_unregister_job (is, job);
}

static gboolean
imapx_job_uid_search_start (CamelIMAPXJob *job,
                            CamelIMAPXServer *is,
                            GCancellable *cancellable,
                            GError **error)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXMailbox *mailbox;
	SearchData *data;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_val_if_fail (mailbox != NULL, FALSE);

	ic = camel_imapx_command_new (
		is, "UID SEARCH", mailbox,
		"UID SEARCH %t", data->criteria);
	ic->pri = job->pri;
	camel_imapx_command_set_job (ic, job);
	ic->complete = imapx_command_uid_search_done;

	imapx_command_queue (is, ic);

	camel_imapx_command_unref (ic);

	g_object_unref (mailbox);

	return TRUE;
}

/* ********************************************************************** */

static void
imapx_command_noop_done (CamelIMAPXServer *is,
                         CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error performing NOOP"));
		camel_imapx_job_take_error (job, local_error);
	}

	imapx_unregister_job (is, job);
}

static gboolean
imapx_job_noop_start (CamelIMAPXJob *job,
                      CamelIMAPXServer *is,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXMailbox *mailbox;

	/* This may be NULL. */
	mailbox = camel_imapx_job_ref_mailbox (job);

	ic = camel_imapx_command_new (
		is, "NOOP", mailbox, "NOOP");

	camel_imapx_command_set_job (ic, job);
	ic->complete = imapx_command_noop_done;
	if (mailbox != NULL)
		ic->pri = IMAPX_PRIORITY_REFRESH_INFO;
	else
		ic->pri = IMAPX_PRIORITY_NOOP;

	imapx_command_queue (is, ic);

	camel_imapx_command_unref (ic);

	g_clear_object (&mailbox);

	return TRUE;
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

static void
imapx_command_sync_changes_done (CamelIMAPXServer *is,
                                 CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job;
	CamelIMAPXMailbox *mailbox;
	CamelFolder *folder;
	CamelStore *parent_store;
	SyncChangesData *data;
	const gchar *full_name;
	GError *local_error = NULL;

	job = camel_imapx_command_get_job (ic);
	g_return_if_fail (CAMEL_IS_IMAPX_JOB (job));

	data = camel_imapx_job_get_data (job);
	g_return_if_fail (data != NULL);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_if_fail (mailbox != NULL);

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_if_fail (folder != NULL);

	g_atomic_int_add (&job->commands, -1);

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	/* If this worked, we should really just update the changes that we
	 * sucessfully stored, so we dont have to worry about sending them
	 * again ...
	 * But then we'd have to track which uid's we actually updated, so
	 * its easier just to refresh all of the ones we got.
	 *
	 * Not that ... given all the asynchronicity going on, we're guaranteed
	 * that what we just set is actually what is on the server now .. but
	 * if it isn't, i guess we'll fix up next refresh */

	if (camel_imapx_command_set_error_if_failed (ic, &local_error)) {
		g_prefix_error (
			&local_error, "%s: ",
			_("Error syncing changes"));
		camel_imapx_job_take_error (job, local_error);
		imapx_unregister_job (is, job);
		goto exit;

	/* lock cache ? */
	} else {
		guint32 unseen, permanentflags;
		gint i;

		permanentflags = camel_imapx_mailbox_get_permanentflags (mailbox);

		for (i = 0; i < data->changed_uids->len; i++) {
			CamelIMAPXMessageInfo *xinfo = (CamelIMAPXMessageInfo *) camel_folder_summary_get (folder->summary,
					data->changed_uids->pdata[i]);

			if (!xinfo)
				continue;

			xinfo->server_flags = xinfo->info.flags & CAMEL_IMAPX_SERVER_FLAGS;
			if (!data->remove_deleted_flags ||
			    !(xinfo->info.flags & CAMEL_MESSAGE_DELETED)) {
				xinfo->info.flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
			} else {
				/* to stare back the \Deleted flag */
				xinfo->server_flags &= ~CAMEL_MESSAGE_DELETED;
				xinfo->info.flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
			}
			xinfo->info.dirty = TRUE;
			if ((permanentflags & CAMEL_MESSAGE_USER) != 0 ||
			    camel_flag_list_size (&xinfo->server_user_flags) == 0)
				camel_flag_list_copy (&xinfo->server_user_flags, &xinfo->info.user_flags);

			camel_folder_summary_touch (folder->summary);
			camel_message_info_unref (xinfo);
		}

		/* Apply the changes to server-side unread count; it won't tell
		 * us of these changes, of course. */
		unseen = camel_imapx_mailbox_get_unseen (mailbox);
		unseen += data->unread_change;
		camel_imapx_mailbox_set_unseen (mailbox, unseen);
	}

	if (g_atomic_int_get (&job->commands) == 0) {
		if (folder->summary && (folder->summary->flags & CAMEL_FOLDER_SUMMARY_DIRTY) != 0) {
			CamelStoreInfo *si;

			/* ... and store's summary when folder's summary is dirty */
			si = camel_store_summary_path (CAMEL_IMAPX_STORE (parent_store)->summary, full_name);
			if (si) {
				if (si->total != camel_folder_summary_get_saved_count (folder->summary) ||
				    si->unread != camel_folder_summary_get_unread_count (folder->summary)) {
					si->total = camel_folder_summary_get_saved_count (folder->summary);
					si->unread = camel_folder_summary_get_unread_count (folder->summary);
					camel_store_summary_touch (CAMEL_IMAPX_STORE (parent_store)->summary);
				}

				camel_store_summary_info_unref (CAMEL_IMAPX_STORE (parent_store)->summary, si);
			}
		}

		camel_folder_summary_save_to_db (folder->summary, NULL);
		camel_store_summary_save (CAMEL_IMAPX_STORE (parent_store)->summary);

		imapx_unregister_job (is, job);
	} else {
		/* Make sure no command will starve in a queue */
		QUEUE_LOCK (is);
		imapx_command_start_next (is);
		QUEUE_UNLOCK (is);
	}

exit:
	g_object_unref (folder);
	g_object_unref (mailbox);
}

static gboolean
imapx_job_sync_changes_start (CamelIMAPXJob *job,
                              CamelIMAPXServer *is,
                              GCancellable *cancellable,
                              GError **error)
{
	SyncChangesData *data;
	CamelFolder *folder;
	CamelIMAPXMailbox *mailbox;
	guint32 i, j, permanentflags;
	struct _uidset_state ss;
	GPtrArray *uids;
	gint on;

	data = camel_imapx_job_get_data (job);
	g_return_val_if_fail (data != NULL, FALSE);

	mailbox = camel_imapx_job_ref_mailbox (job);
	g_return_val_if_fail (mailbox != NULL, FALSE);

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_val_if_fail (folder != NULL, FALSE);

	permanentflags = camel_imapx_mailbox_get_permanentflags (mailbox);
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
				CamelIMAPXMessageInfo *info;
				gboolean remove_deleted_flag;
				guint32 flags;
				guint32 sflags;
				gint send;

				info = (CamelIMAPXMessageInfo *)
					camel_folder_summary_get (
						folder->summary,
						uids->pdata[i]);

				if (info == NULL)
					continue;

				flags = (info->info.flags & CAMEL_IMAPX_SERVER_FLAGS) & permanentflags;
				sflags = (info->server_flags & CAMEL_IMAPX_SERVER_FLAGS) & permanentflags;
				send = 0;

				remove_deleted_flag =
					data->remove_deleted_flags &&
					(flags & CAMEL_MESSAGE_DELETED);

				if (remove_deleted_flag) {
					/* Remove the DELETED flag so the
					 * message appears normally in the
					 * real Trash folder when copied. */
					flags &= ~CAMEL_MESSAGE_DELETED;
				}

				if ( (on && (((flags ^ sflags) & flags) & flag))
				     || (!on && (((flags ^ sflags) & ~flags) & flag))) {
					if (ic == NULL) {
						ic = camel_imapx_command_new (
							is, "STORE", mailbox,
							"UID STORE ");
						ic->complete = imapx_command_sync_changes_done;
						camel_imapx_command_set_job (ic, job);
						ic->pri = job->pri;
					}
					send = imapx_uidset_add (&ss, ic, camel_message_info_uid (info));
				}
				if (send == 1 || (i == uids->len - 1 && ic && imapx_uidset_done (&ss, ic))) {
					g_atomic_int_add (&job->commands, 1);
					camel_imapx_command_add (ic, " %tFLAGS.SILENT (%t)", on?"+":"-", flags_table[j].name);
					imapx_command_queue (is, ic);
					camel_imapx_command_unref (ic);
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

				/* The second round and the server doesn't support saving user flags,
				   thus store them at least locally */
				if (on && (permanentflags & CAMEL_MESSAGE_USER) == 0) {
					camel_flag_list_copy (&info->server_user_flags, &info->info.user_flags);
				}

				camel_message_info_unref (info);
			}

			g_warn_if_fail (ic == NULL);
		}

		if (user_set && (permanentflags & CAMEL_MESSAGE_USER) != 0) {
			CamelIMAPXCommand *ic = NULL;

			for (j = 0; j < user_set->len; j++) {
				struct _imapx_flag_change *c = &g_array_index (user_set, struct _imapx_flag_change, j);

				imapx_uidset_init (&ss, 0, 100);
				for (i = 0; i < c->infos->len; i++) {
					CamelIMAPXMessageInfo *info = c->infos->pdata[i];

					if (ic == NULL) {
						ic = camel_imapx_command_new (
							is, "STORE", mailbox,
							"UID STORE ");
						ic->complete = imapx_command_sync_changes_done;
						camel_imapx_command_set_job (ic, job);
						ic->pri = job->pri;
					}

					if (imapx_uidset_add (&ss, ic, camel_message_info_uid (info)) == 1
					    || (i == c->infos->len - 1 && imapx_uidset_done (&ss, ic))) {
						g_atomic_int_add (&job->commands, 1);
						camel_imapx_command_add (ic, " %tFLAGS.SILENT (%t)", on?"+":"-", c->name);
						imapx_command_queue (is, ic);
						camel_imapx_command_unref (ic);
						ic = NULL;
					}
				}
			}
		}
	}

	g_object_unref (folder);
	g_object_unref (mailbox);

	if (g_atomic_int_get (&job->commands) == 0) {
		imapx_unregister_job (is, job);
	} else {
		/* Make sure no command will starve in a queue */
		QUEUE_LOCK (is);
		imapx_command_start_next (is);
		QUEUE_UNLOCK (is);
	}

	return TRUE;
}

static gboolean
imapx_job_sync_changes_matches (CamelIMAPXJob *job,
                                CamelIMAPXMailbox *mailbox,
                                const gchar *uid)
{
	return camel_imapx_job_has_mailbox (job, mailbox);
}

static void
imapx_abort_all_commands (CamelIMAPXServer *is,
                          const GError *error)
{
	CamelIMAPXCommandQueue *queue;
	GList *head, *link;

	/* Transfer all pending and active commands to a separate
	 * command queue to complete them without holding QUEUE_LOCK. */

	queue = camel_imapx_command_queue_new ();

	imapx_server_set_shutdown_error (is, error);

	QUEUE_LOCK (is);

	camel_imapx_command_queue_transfer (is->queue, queue);
	camel_imapx_command_queue_transfer (is->active, queue);

	head = camel_imapx_command_queue_peek_head_link (queue);
	for (link = head; link != NULL; link = g_list_next (link)) {
		CamelIMAPXCommand *ic = link->data;

		if (ic)
			imapx_server_command_removed (is, ic);
	}

	QUEUE_UNLOCK (is);

	head = camel_imapx_command_queue_peek_head_link (queue);

	for (link = head; link != NULL; link = g_list_next (link)) {
		CamelIMAPXCommand *ic = link->data;

		/* Sanity check the CamelIMAPXCommand before proceeding.
		 * XXX We are actually getting reports of crashes here...
		 *     not sure how this is happening but it's happening. */
		if (ic == NULL)
			continue;

		/* Insert an error into the CamelIMAPXCommand to be
		 * propagated when the completion callback function
		 * calls camel_imapx_command_set_error_if_failed(). */
		camel_imapx_command_failed (ic, error);

		/* Invoke the completion callback function so it can
		 * perform any cleanup processing and unregister its
		 * CamelIMAPXJob. */
		ic->complete (is, ic);
	}

	camel_imapx_command_queue_free (queue);
}

/* ********************************************************************** */

static gboolean
imapx_ready_to_read (GInputStream *input_stream,
                     CamelIMAPXServer *is)
{
	GOutputStream *output_stream;
	GCancellable *cancellable;
	GError *local_error = NULL;

	/* XXX Don't use the passed in GInputStream because that's
	 *     the CamelIMAPXInputStream base stream.  We need the
	 *     CamelIMAPXInputStream itself. */

	input_stream = camel_imapx_server_ref_input_stream (is);
	output_stream = camel_imapx_server_ref_output_stream (is);

	cancellable = g_weak_ref_get (&is->priv->parser_cancellable);

	while (imapx_step (is, input_stream, cancellable, &local_error)) {
		gint bytes_buffered;

		bytes_buffered = camel_imapx_input_stream_buffered (
			CAMEL_IMAPX_INPUT_STREAM (input_stream));
		if (bytes_buffered == 0)
			break;
	}

	if (g_cancellable_is_cancelled (cancellable)) {
		gboolean active_queue_is_empty, is_shutdown_request;

		QUEUE_LOCK (is);
		active_queue_is_empty =
			camel_imapx_command_queue_is_empty (is->active);
		is_shutdown_request = is->state == IMAPX_SHUTDOWN;
		QUEUE_UNLOCK (is);

		if (!is_shutdown_request && (active_queue_is_empty || imapx_in_idle (is))) {
			g_cancellable_reset (cancellable);
			g_clear_error (&local_error);
		} else {
			/* Cancelled error should be set. */
			g_warn_if_fail (local_error != NULL);
		}
	}

	g_clear_object (&input_stream);
	g_clear_object (&output_stream);
	g_clear_object (&cancellable);

	if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
		QUEUE_LOCK (is);
		if (camel_imapx_command_queue_is_empty (is->active) && is->state != IMAPX_SHUTDOWN) {
			camel_imapx_debug (io, is->tagprefix, "Ignoring timeout error, nothing was waiting (original error: %s)\n", local_error->message);
			g_clear_error (&local_error);
		}
		QUEUE_UNLOCK (is);
	}

	if (local_error != NULL) {
		camel_imapx_debug (io, is->tagprefix, "Data read failed with error '%s'\n", local_error->message);

		/* Sadly, G_IO_ERROR_FAILED is also used for 'Connection reset by peer' error */
		if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_FAILED) ||
		    g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
			local_error->domain = CAMEL_IMAPX_SERVER_ERROR;
			local_error->code = CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT;
		}

		imapx_server_set_shutdown_error (is, local_error);

		/* Call the signal early, certain thread interleaving can cause the closed connection
		   being reused on the following reconnect attempt. There is also re-setting
		   the shutdown_error above, because the signal handler in connection manager
		   also calls camel_imapx_server_shutdown(), but without the error, while we want
		   to have there propagated the "try reconnect" error instead. As there is no
		   guarantee that it'll be called, then we also quit the parser's mainloop and
		   call the imapx_abort_all_commands() below - just in case. */
		g_signal_emit (is, signals[SHUTDOWN], 0, local_error);

		g_main_loop_quit (is->priv->parser_main_loop);
		imapx_abort_all_commands (is, local_error);
		g_clear_error (&local_error);
		return G_SOURCE_REMOVE;
	}

	return G_SOURCE_CONTINUE;
}

/*
 * The main processing (reading) loop.
 *
 * Main area of locking required is command_queue
 * and command_start_next, the 'literal' command,
 * the jobs queue, the active queue, the queue
 * queue. */
static gpointer
imapx_parser_thread (gpointer user_data)
{
	CamelIMAPXServer *is;
	GInputStream *input_stream;
	GCancellable *cancellable;
	GSource *pollable_source;
	GError *shutdown_error;

	is = CAMEL_IMAPX_SERVER (user_data);

	/* Do not use CamelOperation here, because it can be cancelled at
	 * an application end with camel_operation_cancel_all() call, which
	 * is done too early, before any pending jobs are properly finished
	 * (it can be IDLE job, or save of folder changes back to the server).
	 */
	cancellable = g_cancellable_new ();
	g_weak_ref_set (&is->priv->parser_cancellable, cancellable);

	input_stream = camel_imapx_server_ref_input_stream (is);
	g_return_val_if_fail (input_stream != NULL, NULL);

	g_main_context_push_thread_default (is->priv->parser_main_context);

	pollable_source = g_pollable_input_stream_create_source (
		G_POLLABLE_INPUT_STREAM (input_stream), cancellable);
	g_source_set_callback (
		pollable_source,
		(GSourceFunc) imapx_ready_to_read,
		g_object_ref (is),
		(GDestroyNotify) g_object_unref);
	g_source_attach (
		pollable_source,
		is->priv->parser_main_context);
	g_source_unref (pollable_source);

	g_clear_object (&cancellable);
	g_clear_object (&input_stream);

	g_main_loop_run (is->priv->parser_main_loop);

	QUEUE_LOCK (is);
	is->state = IMAPX_SHUTDOWN;
	QUEUE_UNLOCK (is);

	g_main_context_pop_thread_default (is->priv->parser_main_context);

	shutdown_error = imapx_server_dup_shutdown_error (is);

	g_signal_emit (is, signals[SHUTDOWN], 0, shutdown_error);

	g_clear_error (&shutdown_error);

	g_object_unref (is);

	return NULL;
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
	gboolean idle_main_loop_is_running;
	gboolean parser_main_loop_is_running;

	/* Server should be shut down already.  Warn if
	 * the idle or parser threads are still running. */
	idle_main_loop_is_running =
		g_main_loop_is_running (server->priv->idle_main_loop);
	parser_main_loop_is_running =
		g_main_loop_is_running (server->priv->parser_main_loop);
	g_warn_if_fail (!idle_main_loop_is_running);
	g_warn_if_fail (!parser_main_loop_is_running);

	if (server->priv->parser_thread != NULL) {
		g_thread_unref (server->priv->parser_thread);
		server->priv->parser_thread = NULL;
	}

	if (server->priv->idle_thread != NULL) {
		g_thread_unref (server->priv->idle_thread);
		server->priv->idle_thread = NULL;
	}

	imapx_disconnect (server);

	g_weak_ref_set (&server->priv->store, NULL);

#if GLIB_CHECK_VERSION(2,39,0)
	g_clear_object (&server->priv->subprocess);
#endif

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

	g_rec_mutex_clear (&is->queue_lock);
	g_mutex_clear (&is->priv->select_lock);

	g_main_loop_unref (is->priv->parser_main_loop);
	g_main_context_unref (is->priv->parser_main_context);

	camel_folder_change_info_free (is->priv->changes);

	g_free (is->priv->context);
	g_hash_table_destroy (is->priv->untagged_handlers);

	if (is->priv->inactivity_timeout != NULL)
		g_source_unref (is->priv->inactivity_timeout);
	g_mutex_clear (&is->priv->inactivity_timeout_lock);

	g_free (is->priv->status_data_items);
	g_free (is->priv->list_return_opts);

	if (is->priv->search_results != NULL)
		g_array_unref (is->priv->search_results);
	g_mutex_clear (&is->priv->search_results_lock);

	g_hash_table_destroy (is->priv->known_alerts);
	g_mutex_clear (&is->priv->known_alerts_lock);

	g_rec_mutex_clear (&is->priv->idle_lock);
	g_main_loop_unref (is->priv->idle_main_loop);
	g_main_context_unref (is->priv->idle_main_context);

	g_mutex_clear (&is->priv->jobs_prop_lock);
	g_hash_table_destroy (is->priv->jobs_prop_folder_paths);

	g_mutex_clear (&is->priv->shutdown_error_lock);
	g_clear_error (&is->priv->shutdown_error);

	g_weak_ref_clear (&is->priv->store);
	g_weak_ref_clear (&is->priv->parser_cancellable);
	g_weak_ref_clear (&is->priv->select_mailbox);
	g_weak_ref_clear (&is->priv->select_closing);
	g_weak_ref_clear (&is->priv->select_pending);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_server_parent_class)->finalize (object);
}

static void
imapx_server_constructed (GObject *object)
{
	CamelIMAPXServer *server;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_imapx_server_parent_class)->constructed (object);

	server = CAMEL_IMAPX_SERVER (object);
	server->tagprefix = 'Z';
}

static void
imapx_server_mailbox_select (CamelIMAPXServer *is,
                             CamelIMAPXMailbox *mailbox)
{
	imapx_server_add_job_mailbox (is, mailbox);

	e (
		is->tagprefix,
		"%s::mailbox-select (\"%s\")\n",
		G_OBJECT_TYPE_NAME (is),
		camel_imapx_mailbox_get_name (mailbox));
}

static void
imapx_server_mailbox_closed (CamelIMAPXServer *is,
                             CamelIMAPXMailbox *mailbox)
{
	imapx_server_remove_job_mailbox (is, mailbox);

	e (
		is->tagprefix,
		"%s::mailbox-closed (\"%s\")\n",
		G_OBJECT_TYPE_NAME (is),
		camel_imapx_mailbox_get_name (mailbox));
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

	class->mailbox_select = imapx_server_mailbox_select;
	class->mailbox_closed = imapx_server_mailbox_closed;

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

	signals[MAILBOX_SELECT] = g_signal_new (
		"mailbox-select",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (CamelIMAPXServerClass, mailbox_select),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		CAMEL_TYPE_IMAPX_MAILBOX);

	signals[MAILBOX_CLOSED] = g_signal_new (
		"mailbox-closed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (CamelIMAPXServerClass, mailbox_closed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		CAMEL_TYPE_IMAPX_MAILBOX);

	/**
	 * CamelIMAPXServer::shutdown
	 * @server: the #CamelIMAPXServer which emitted the signal
	 * @error: a #GError, which caused the shutdown; can be %NULL
	 **/
	signals[SHUTDOWN] = g_signal_new (
		"shutdown",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelIMAPXServerClass, shutdown),
		NULL, NULL,
		g_cclosure_marshal_VOID__BOXED,
		G_TYPE_NONE, 1, G_TYPE_ERROR);
}

static void
camel_imapx_server_init (CamelIMAPXServer *is)
{
	GMainContext *main_context;

	is->priv = CAMEL_IMAPX_SERVER_GET_PRIVATE (is);

	is->priv->untagged_handlers = create_initial_untagged_handler_table ();

	g_mutex_init (&is->priv->stream_lock);
	g_mutex_init (&is->priv->inactivity_timeout_lock);
	g_mutex_init (&is->priv->select_lock);
	g_mutex_init (&is->priv->search_results_lock);
	g_mutex_init (&is->priv->known_alerts_lock);
	g_mutex_init (&is->priv->jobs_prop_lock);
	g_mutex_init (&is->priv->shutdown_error_lock);

	g_weak_ref_init (&is->priv->store, NULL);
	g_weak_ref_init (&is->priv->parser_cancellable, NULL);
	g_weak_ref_init (&is->priv->select_mailbox, NULL);
	g_weak_ref_init (&is->priv->select_closing, NULL);
	g_weak_ref_init (&is->priv->select_pending, NULL);

	is->priv->jobs_prop_folder_paths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	is->priv->jobs_prop_command_count = 0;
	is->priv->jobs_prop_expensive_command_count = 0;

	is->queue = camel_imapx_command_queue_new ();
	is->active = camel_imapx_command_queue_new ();
	is->done = camel_imapx_command_queue_new ();

	g_queue_init (&is->jobs);

	g_rec_mutex_init (&is->queue_lock);

	is->state = IMAPX_DISCONNECTED;

	is->priv->changes = camel_folder_change_info_new ();

	is->priv->known_alerts = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	/* Initialize parser thread structs. */

	main_context = g_main_context_new ();

	is->priv->parser_main_loop = g_main_loop_new (main_context, FALSE);
	is->priv->parser_main_context = g_main_context_ref (main_context);
	is->priv->shutdown_error = NULL;

	g_main_context_unref (main_context);

	/* Initialize IDLE thread structs. */

	main_context = g_main_context_new ();

	g_rec_mutex_init (&is->priv->idle_lock);
	is->priv->idle_main_loop = g_main_loop_new (main_context, FALSE);
	is->priv->idle_main_context = g_main_context_ref (main_context);

	g_main_context_unref (main_context);
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

/**
 * camel_imapx_server_ref_input_stream:
 * @is: a #CamelIMAPXServer
 *
 * Returns the #GInputStream for @is, which is owned by either a
 * #GTcpConnection or a #GSubprocess.  If the #CamelIMAPXServer is not
 * yet connected or has lost its connection, the function returns %NULL.
 *
 * The returned #GInputStream is referenced for thread-safety and must
 * be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: a #GInputStream, or %NULL
 *
 * Since: 3.12
 **/
GInputStream *
camel_imapx_server_ref_input_stream (CamelIMAPXServer *is)
{
	GInputStream *input_stream = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), NULL);

	g_mutex_lock (&is->priv->stream_lock);

	if (is->priv->input_stream != NULL)
		input_stream = g_object_ref (is->priv->input_stream);

	g_mutex_unlock (&is->priv->stream_lock);

	return input_stream;
}

/**
 * camel_imapx_server_ref_output_stream:
 * @is: a #CamelIMAPXServer
 *
 * Returns the #GOutputStream for @is, which is owned by either a
 * #GTcpConnection or a #GSubprocess.  If the #CamelIMAPXServer is not
 * yet connected or has lost its connection, the function returns %NULL.
 *
 * The returned #GOutputStream is referenced for thread-safety and must
 * be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: a #GOutputStream, or %NULL
 *
 * Since: 3.12
 **/
GOutputStream *
camel_imapx_server_ref_output_stream (CamelIMAPXServer *is)
{
	GOutputStream *output_stream = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), NULL);

	g_mutex_lock (&is->priv->stream_lock);

	if (is->priv->output_stream != NULL)
		output_stream = g_object_ref (is->priv->output_stream);

	g_mutex_unlock (&is->priv->stream_lock);

	return output_stream;
}

/**
 * camel_imapx_server_ref_selected:
 * @is: a #CamelIMAPXServer
 *
 * Returns the #CamelIMAPXMailbox representing the currently selected
 * mailbox (or mailbox <emphasis>being</emphasis> selected if a SELECT
 * command is in progress) on the IMAP server, or %NULL if no mailbox
 * is currently selected or being selected on the server.
 *
 * The returned #CamelIMAPXMailbox is reference for thread-safety and
 * should be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: a #CamelIMAPXMailbox, or %NULL
 *
 * Since: 3.12
 **/
CamelIMAPXMailbox *
camel_imapx_server_ref_selected (CamelIMAPXServer *is)
{
	CamelIMAPXMailbox *mailbox;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), NULL);

	g_mutex_lock (&is->priv->select_lock);

	mailbox = g_weak_ref_get (&is->priv->select_mailbox);
	if (mailbox == NULL)
		mailbox = g_weak_ref_get (&is->priv->select_closing);
	if (mailbox == NULL)
		mailbox = g_weak_ref_get (&is->priv->select_pending);

	g_mutex_unlock (&is->priv->select_lock);

	return mailbox;
}

static void
imapx_disconnect (CamelIMAPXServer *is)
{
	g_mutex_lock (&is->priv->stream_lock);

	g_clear_object (&is->priv->input_stream);
	g_clear_object (&is->priv->output_stream);
	g_clear_object (&is->priv->connection);
#if GLIB_CHECK_VERSION(2,39,0)
	g_clear_object (&is->priv->subprocess);
#endif

	g_mutex_unlock (&is->priv->stream_lock);

	g_mutex_lock (&is->priv->select_lock);
	g_weak_ref_set (&is->priv->select_mailbox, NULL);
	g_weak_ref_set (&is->priv->select_closing, NULL);
	g_weak_ref_set (&is->priv->select_pending, NULL);
	g_mutex_unlock (&is->priv->select_lock);

	if (is->cinfo) {
		imapx_free_capability (is->cinfo);
		is->cinfo = NULL;
	}

	is->state = IMAPX_DISCONNECTED;
}

/* Client commands */
gboolean
camel_imapx_server_connect (CamelIMAPXServer *is,
                            GCancellable *cancellable,
                            GError **error)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);

	if (is->state == IMAPX_SHUTDOWN) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			"Shutting down");
		return FALSE;
	}

	if (is->state >= IMAPX_INITIALISED)
		return TRUE;

	if (!imapx_reconnect (is, cancellable, error))
		return FALSE;

	is->priv->parser_thread = g_thread_new (
		NULL, imapx_parser_thread, g_object_ref (is));

	if (CAMEL_IMAPX_LACK_CAPABILITY (is->cinfo, NAMESPACE)) {
		/* This also creates a needed faux NAMESPACE */
		if (!camel_imapx_server_list (is, "INBOX", 0, cancellable, error))
			return FALSE;
	}

	return TRUE;
}

static CamelStream *
imapx_server_get_message (CamelIMAPXServer *is,
                          CamelIMAPXMailbox *mailbox,
                          CamelFolderSummary *summary,
                          CamelDataCache *message_cache,
                          const gchar *message_uid,
                          gint pri,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelStream *stream = NULL;
	CamelIMAPXJob *job;
	CamelMessageInfo *mi;
	GIOStream *cache_stream;
	GetMessageData *data;
	gboolean registered;

	job = imapx_server_ref_job (is, mailbox, IMAPX_JOB_GET_MESSAGE, message_uid);

	if (job != NULL) {
		/* Promote the existing GET_MESSAGE
		 * job's priority if ours is higher. */
		if (pri > job->pri)
			job->pri = pri;

		/* Wait for the job to finish. */
		camel_imapx_job_wait (job, NULL);
		camel_imapx_job_unref (job);

		/* Disregard errors here.  If we failed to retreive the
		 * message from cache (implying the job we were waiting
		 * on failed or got cancelled), we'll just re-fetch it. */
		cache_stream = camel_data_cache_get (
			message_cache, "cur", message_uid, NULL);
		if (cache_stream != NULL) {
			stream = camel_stream_new (cache_stream);
			g_object_unref (cache_stream);
			return stream;
		}
	}

	QUEUE_LOCK (is);

	mi = camel_folder_summary_get (summary, message_uid);
	if (mi == NULL) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_UID,
			_("Cannot get message with message ID %s: %s"),
			message_uid, _("No such message available."));
		QUEUE_UNLOCK (is);
		return NULL;
	}

	/* This makes sure that if any file is left on the disk, it is not reused.
	   That can happen when the previous message download had been cancelled
	   or finished with an error. */
	camel_data_cache_remove (message_cache, "tmp", message_uid, NULL);

	cache_stream = camel_data_cache_add (message_cache, "tmp", message_uid, error);
	if (cache_stream == NULL) {
		QUEUE_UNLOCK (is);
		return NULL;
	}

	data = g_slice_new0 (GetMessageData);
	data->uid = g_strdup (message_uid);
	data->message_cache = g_object_ref (message_cache);
	data->stream = g_object_ref (cache_stream);
	data->size = ((CamelMessageInfoBase *) mi)->size;
	if (data->size > MULTI_SIZE)
		data->use_multi_fetch = TRUE;

	job = camel_imapx_job_new (cancellable);
	job->pri = pri;
	job->type = IMAPX_JOB_GET_MESSAGE;
	job->start = imapx_job_get_message_start;
	job->matches = imapx_job_get_message_matches;

	camel_imapx_job_set_mailbox (job, mailbox);

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) get_message_data_free);

	g_clear_object (&cache_stream);
	camel_message_info_unref (mi);

	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	if (registered && camel_imapx_job_run (job, is, error))
		stream = camel_stream_new (data->stream);

	camel_imapx_job_unref (job);

	return stream;
}

CamelStream *
camel_imapx_server_get_message (CamelIMAPXServer *is,
                                CamelIMAPXMailbox *mailbox,
                                CamelFolderSummary *summary,
                                CamelDataCache *message_cache,
                                const gchar *message_uid,
                                GCancellable *cancellable,
                                GError **error)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), NULL);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), NULL);
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), NULL);
	g_return_val_if_fail (CAMEL_IS_DATA_CACHE (message_cache), NULL);
	g_return_val_if_fail (message_uid != NULL, NULL);

	return imapx_server_get_message (
		is, mailbox, summary,
		message_cache, message_uid,
		IMAPX_PRIORITY_GET_MESSAGE,
		cancellable, error);
}

gboolean
camel_imapx_server_sync_message (CamelIMAPXServer *is,
                                 CamelIMAPXMailbox *mailbox,
                                 CamelFolderSummary *summary,
                                 CamelDataCache *message_cache,
                                 const gchar *message_uid,
                                 GCancellable *cancellable,
                                 GError **error)
{
	gchar *cache_file = NULL;
	gboolean is_cached;
	struct stat st;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), FALSE);
	g_return_val_if_fail (CAMEL_IS_DATA_CACHE (message_cache), FALSE);
	g_return_val_if_fail (message_uid != NULL, FALSE);

	/* Check if the cache file already exists and is non-empty. */
	cache_file = camel_data_cache_get_filename (
		message_cache, "cur", message_uid);
	is_cached = (g_stat (cache_file, &st) == 0 && st.st_size > 0);
	g_free (cache_file);

	if (!is_cached) {
		CamelStream *stream;

		stream = imapx_server_get_message (
			is, mailbox, summary,
			message_cache, message_uid,
			IMAPX_PRIORITY_SYNC_MESSAGE,
			cancellable, error);

		success = (stream != NULL);

		g_clear_object (&stream);
	}

	return success;
}

gboolean
camel_imapx_server_copy_message (CamelIMAPXServer *is,
                                 CamelIMAPXMailbox *mailbox,
                                 CamelIMAPXMailbox *destination,
                                 GPtrArray *uids,
                                 gboolean delete_originals,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelIMAPXJob *job;
	CopyMessagesData *data;
	gint ii;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (destination), FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	/* That's okay if the "SELECT" fails here, as it can be due to
	   the folder being write-only; just ignore the error and continue. */
	imapx_ensure_mailbox_permanentflags (is, destination, cancellable, NULL);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	data = g_slice_new0 (CopyMessagesData);
	data->destination = g_object_ref (destination);
	data->uids = g_ptr_array_new ();
	data->delete_originals = delete_originals;

	/* If we're moving messages, prefer "UID MOVE" if supported. */
	if (data->delete_originals) {
		if (CAMEL_IMAPX_HAVE_CAPABILITY (is->cinfo, MOVE)) {
			data->delete_originals = FALSE;
			data->use_move_command = TRUE;
		}
	}

	for (ii = 0; ii < uids->len; ii++)
		g_ptr_array_add (data->uids, g_strdup (uids->pdata[ii]));

	job = camel_imapx_job_new (cancellable);
	job->pri = IMAPX_PRIORITY_COPY_MESSAGE;
	job->type = IMAPX_JOB_COPY_MESSAGE;
	job->start = imapx_job_copy_messages_start;

	camel_imapx_job_set_mailbox (job, mailbox);

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) copy_messages_data_free);

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_append_message (CamelIMAPXServer *is,
                                   CamelIMAPXMailbox *mailbox,
                                   CamelFolderSummary *summary,
                                   CamelDataCache *message_cache,
                                   CamelMimeMessage *message,
                                   const CamelMessageInfo *mi,
                                   gchar **appended_uid,
                                   GCancellable *cancellable,
                                   GError **error)
{
	gchar *uid = NULL, *path = NULL;
	CamelMimeFilter *filter;
	CamelIMAPXJob *job;
	CamelMessageInfo *info;
	GIOStream *base_stream;
	GOutputStream *output_stream;
	GOutputStream *filter_stream;
	AppendMessageData *data;
	gint res;
	time_t date_time;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), FALSE);
	g_return_val_if_fail (CAMEL_IS_DATA_CACHE (message_cache), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), FALSE);
	/* CamelMessageInfo can be NULL. */

	/* That's okay if the "SELECT" fails here, as it can be due to
	   the folder being write-only; just ignore the error and continue. */
	imapx_ensure_mailbox_permanentflags (is, mailbox, cancellable, NULL);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	/* Append just assumes we have no/a dodgy connection.  We dump
	 * stuff into the 'new' directory, and let the summary know it's
	 * there.  Then we fire off a no-reply job which will asynchronously
	 * upload the message at some point in the future, and fix up the
	 * summary to match */

	/* chen cleanup this later */
	uid = imapx_get_temp_uid ();
	base_stream = camel_data_cache_add (message_cache, "new", uid, error);
	if (base_stream == NULL) {
		g_prefix_error (error, _("Cannot create spool file: "));
		g_free (uid);
		return FALSE;
	}

	output_stream = g_io_stream_get_output_stream (base_stream);
	filter = camel_mime_filter_canon_new (CAMEL_MIME_FILTER_CANON_CRLF);
	filter_stream = camel_filter_output_stream_new (output_stream, filter);

	g_filter_output_stream_set_close_base_stream (
		G_FILTER_OUTPUT_STREAM (filter_stream), FALSE);

	res = camel_data_wrapper_write_to_output_stream_sync (
		CAMEL_DATA_WRAPPER (message),
		filter_stream, cancellable, error);

	g_object_unref (base_stream);
	g_object_unref (filter_stream);
	g_object_unref (filter);

	if (res == -1) {
		g_prefix_error (error, _("Cannot create spool file: "));
		camel_data_cache_remove (message_cache, "new", uid, NULL);
		g_free (uid);
		return FALSE;
	}

	date_time = camel_mime_message_get_date (message, NULL);
	path = camel_data_cache_get_filename (message_cache, "new", uid);
	info = camel_folder_summary_info_new_from_message (
		summary, message, NULL);
	info->uid = camel_pstring_strdup (uid);
	if (mi != NULL) {
		CamelMessageInfoBase *base_info = (CamelMessageInfoBase *) info;
		const CamelFlag *flag;
		const CamelTag *tag;

		base_info->flags = camel_message_info_flags (mi);
		base_info->size = camel_message_info_size (mi);

		flag = camel_message_info_user_flags (mi);
		while (flag != NULL) {
			if (*flag->name != '\0')
				camel_flag_set (
					&base_info->user_flags,
					flag->name, TRUE);
			flag = flag->next;
		}

		tag = camel_message_info_user_tags (mi);
		while (tag != NULL) {
			if (*tag->name != '\0')
				camel_tag_set (
					&base_info->user_tags,
					tag->name, tag->value);
			tag = tag->next;
		}

		if (date_time <= 0)
			date_time = camel_message_info_date_received (mi);
	}

	g_free (uid);

	if (camel_mime_message_has_attachment (message))
		((CamelMessageInfoBase *) info)->flags |= CAMEL_MESSAGE_ATTACHMENTS;

	/* So, we actually just want to let the server loop that
	 * messages need appending, i think.  This is so the same
	 * mechanism is used for normal uploading as well as
	 * offline re-syncing when we go back online */

	data = g_slice_new0 (AppendMessageData);
	data->info = info;  /* takes ownership */
	data->path = path;  /* takes ownership */
	data->date_time = date_time;
	data->appended_uid = NULL;

	job = camel_imapx_job_new (cancellable);
	job->pri = IMAPX_PRIORITY_APPEND_MESSAGE;
	job->type = IMAPX_JOB_APPEND_MESSAGE;
	job->start = imapx_job_append_message_start;
	job->noreply = FALSE;

	camel_imapx_job_set_mailbox (job, mailbox);

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) append_message_data_free);

	success = imapx_submit_job (is, job, error);

	if (appended_uid != NULL) {
		*appended_uid = data->appended_uid;
		data->appended_uid = NULL;
	}

	camel_imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_noop (CamelIMAPXServer *is,
                         CamelIMAPXMailbox *mailbox,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	/* Mailbox may be NULL. */

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_NOOP;
	job->start = imapx_job_noop_start;
	job->pri = IMAPX_PRIORITY_NOOP;

	camel_imapx_job_set_mailbox (job, mailbox);

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

	return success;
}

CamelFolderChangeInfo *
camel_imapx_server_refresh_info (CamelIMAPXServer *is,
                                 CamelIMAPXMailbox *mailbox,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelIMAPXJob *job;
	RefreshInfoData *data;
	CamelFolderChangeInfo *changes = NULL;
	gboolean registered = TRUE;
	const gchar *mailbox_name;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), NULL);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), NULL);

	/* Don't run concurrent refreshes on the same mailbox.
	 * If a refresh is already in progress, let it finish
	 * and return no changes for this refresh request. */
	job = imapx_server_ref_job (is, mailbox, IMAPX_JOB_REFRESH_INFO, NULL);

	if (job != NULL) {
		camel_imapx_job_unref (job);
		return camel_folder_change_info_new ();
	}

	if (!imapx_ensure_mailbox_permanentflags (is, mailbox, cancellable, error))
		return NULL;

	QUEUE_LOCK (is);

	data = g_slice_new0 (RefreshInfoData);
	data->changes = camel_folder_change_info_new ();

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_REFRESH_INFO;
	job->start = imapx_job_refresh_info_start;
	job->matches = imapx_job_refresh_info_matches;
	job->pri = IMAPX_PRIORITY_REFRESH_INFO;

	camel_imapx_job_set_mailbox (job, mailbox);

	mailbox_name = camel_imapx_mailbox_get_name (mailbox);

	if (camel_imapx_mailbox_is_inbox (mailbox_name))
		job->pri += 10;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) refresh_info_data_free);

	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	if (registered)
		camel_imapx_mailbox_lock_update (mailbox);

	if (registered && camel_imapx_job_run (job, is, error)) {
		changes = data->changes;
		data->changes = NULL;
	}

	if (registered)
		camel_imapx_mailbox_unlock_update (mailbox);

	camel_imapx_job_unref (job);

	return changes;
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
			camel_message_info_unref (info);
		}

		g_ptr_array_free (infos, TRUE);
		g_free (flag_change->name);
	}
	g_array_free (user_set, TRUE);
}

static void
imapx_unset_folder_flagged_flag (CamelFolderSummary *summary,
				 GPtrArray *changed_uids,
				 gboolean except_deleted_messages)
{
	CamelMessageInfo *info;
	gboolean changed = FALSE;
	gint ii;

	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary));
	g_return_if_fail (changed_uids != NULL);

	for (ii = 0; ii < changed_uids->len; ii++) {
		info = camel_folder_summary_get (summary, changed_uids->pdata[ii]);

		if (info) {
			CamelMessageInfoBase *mi = (CamelMessageInfoBase *) info;

			/* some infos could be only 'dirty' (needed to save into summary) */
			if ((mi->flags & CAMEL_MESSAGE_FOLDER_FLAGGED) != 0 &&
			   (!except_deleted_messages || (mi->flags & CAMEL_MESSAGE_DELETED) == 0)) {
				mi->flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
				mi->dirty = TRUE;
				changed = TRUE;
			}

			camel_message_info_unref (info);
		}
	}

	if (changed) {
		camel_folder_summary_touch (summary);
		camel_folder_summary_save_to_db (summary, NULL);
	}
}

static gboolean
imapx_server_sync_changes (CamelIMAPXServer *is,
                           CamelIMAPXMailbox *mailbox,
                           guint32 job_type,
                           gint pri,
                           GCancellable *cancellable,
                           GError **error)
{
	guint i, on_orset, off_orset;
	GPtrArray *changed_uids;
	GArray *on_user = NULL, *off_user = NULL;
	CamelFolder *folder;
	CamelIMAPXMessageInfo *info;
	CamelIMAPXJob *job;
	CamelIMAPXSettings *settings;
	SyncChangesData *data;
	gboolean use_real_junk_path;
	gboolean use_real_trash_path;
	gboolean remove_deleted_flags;
	gboolean nothing_to_do;
	gboolean registered;
	gboolean own_allocated_changed_uids = FALSE;
	gboolean success = TRUE;

	folder = imapx_server_ref_folder (is, mailbox);
	g_return_val_if_fail (folder != NULL, FALSE);

	if (!imapx_ensure_mailbox_permanentflags (is, mailbox, cancellable, error))
		return FALSE;

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
	changed_uids = camel_folder_summary_get_changed (folder->summary);

	if (changed_uids->len == 0) {
		camel_folder_free_uids (folder, changed_uids);
		g_object_unref (folder);
		return TRUE;
	}

	settings = camel_imapx_server_ref_settings (is);
	use_real_junk_path =
		camel_imapx_settings_get_use_real_junk_path (settings);
	use_real_trash_path =
		camel_imapx_settings_get_use_real_trash_path (settings);
	g_object_unref (settings);

	remove_deleted_flags = use_real_trash_path && (job_type != IMAPX_JOB_EXPUNGE) != 0;

	off_orset = on_orset = 0;
	for (i = 0; i < changed_uids->len; i++) {
		guint32 flags, sflags;
		CamelFlag *uflags, *suflags;
		const gchar *uid;
		gboolean move_to_real_junk;
		gboolean move_to_real_trash;
		guint j = 0;

		uid = g_ptr_array_index (changed_uids, i);

		info = (CamelIMAPXMessageInfo *)
			camel_folder_summary_get (folder->summary, uid);

		if (info == NULL)
			continue;

		if (!(info->info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED)) {
			camel_message_info_unref (info);
			continue;
		}

		flags = info->info.flags & CAMEL_IMAPX_SERVER_FLAGS;
		sflags = info->server_flags & CAMEL_IMAPX_SERVER_FLAGS;

		move_to_real_junk =
			use_real_junk_path &&
			(flags & CAMEL_MESSAGE_JUNK);

		move_to_real_trash =
			use_real_trash_path &&
			(flags & CAMEL_MESSAGE_DELETED);

		if (move_to_real_junk)
			camel_imapx_folder_add_move_to_real_junk (
				CAMEL_IMAPX_FOLDER (folder), uid);

		if (move_to_real_trash)
			camel_imapx_folder_add_move_to_real_trash (
				CAMEL_IMAPX_FOLDER (folder), uid);

		if (flags != sflags) {
			off_orset |= (flags ^ sflags) & ~flags;
			on_orset |= (flags ^ sflags) & flags;
		}

		uflags = info->info.user_flags;
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

		camel_message_info_unref (info);
	}

	nothing_to_do =
		(on_orset == 0) &&
		(off_orset == 0) &&
		(on_user == NULL) &&
		(off_user == NULL);

	if (nothing_to_do) {
		imapx_sync_free_user (on_user);
		imapx_sync_free_user (off_user);
		imapx_unset_folder_flagged_flag (folder->summary, changed_uids, remove_deleted_flags);
		camel_folder_free_uids (folder, changed_uids);
		g_object_unref (folder);
		return TRUE;
	}

	/* TODO above code should go into changes_start */

	job = imapx_server_ref_job (is, mailbox, IMAPX_JOB_SYNC_CHANGES, NULL);

	if (job != NULL) {
		GPtrArray *new_changed_uids;
		GHashTable *known_uids;
		GHashTableIter iter;
		gpointer key, value;
		gint ii;

		known_uids = g_hash_table_new (g_str_hash, g_str_equal);
		data = camel_imapx_job_get_data (job);

		if (data && data->changed_uids) {
			for (ii = 0; ii < changed_uids->len; ii++) {
				g_hash_table_insert (known_uids, changed_uids->pdata[ii], GINT_TO_POINTER (1));
			}

			for (ii = 0; ii < data->changed_uids->len; ii++) {
				g_hash_table_remove (known_uids, data->changed_uids->pdata[ii]);
			}
		}

		if (g_hash_table_size (known_uids) == 0) {
			/* The pending job stores changes for the same UIDs */
			if (pri > job->pri)
				job->pri = pri;

			camel_imapx_job_unref (job);

			imapx_sync_free_user (on_user);
			imapx_sync_free_user (off_user);
			camel_folder_free_uids (folder, changed_uids);
			g_object_unref (folder);
			g_hash_table_destroy (known_uids);
			return TRUE;
		}

		new_changed_uids = g_ptr_array_sized_new (g_hash_table_size (known_uids));

		/* What left in known_uids are message info changes which are not being
		   saved in the pending job */

		g_hash_table_iter_init (&iter, known_uids);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			g_ptr_array_add (new_changed_uids, (gpointer) camel_pstring_strdup (key));
		}

		g_hash_table_destroy (known_uids);

		camel_folder_free_uids (folder, changed_uids);
		changed_uids = new_changed_uids;

		/* Why would anyone define a virtual function for the free on the folder? */
		own_allocated_changed_uids = TRUE;
	}

	QUEUE_LOCK (is);

	data = g_slice_new0 (SyncChangesData);
	data->folder = g_object_ref (folder);
	data->changed_uids = changed_uids;  /* takes ownership */
	data->own_allocated_changed_uids = own_allocated_changed_uids;
	data->on_set = on_orset;
	data->off_set = off_orset;
	data->on_user = on_user;  /* takes ownership */
	data->off_user = off_user;  /* takes ownership */
	data->remove_deleted_flags = remove_deleted_flags;

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_SYNC_CHANGES;
	job->start = imapx_job_sync_changes_start;
	job->matches = imapx_job_sync_changes_matches;
	job->pri = pri;

	camel_imapx_job_set_mailbox (job, mailbox);

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) sync_changes_data_free);

	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	if (job_type == IMAPX_JOB_SYNC_CHANGES && registered)
		camel_imapx_mailbox_lock_update (mailbox);

	success = registered && camel_imapx_job_run (job, is, error);

	if (job_type == IMAPX_JOB_SYNC_CHANGES && registered)
		camel_imapx_mailbox_unlock_update (mailbox);

	camel_imapx_job_unref (job);

	g_object_unref (folder);

	return success;
}

gboolean
camel_imapx_server_sync_changes (CamelIMAPXServer *is,
                                 CamelIMAPXMailbox *mailbox,
                                 GCancellable *cancellable,
                                 GError **error)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);

	return imapx_server_sync_changes (
		is, mailbox,
		IMAPX_JOB_SYNC_CHANGES,
		IMAPX_PRIORITY_SYNC_CHANGES,
		cancellable, error);
}

/* expunge-uids? */
gboolean
camel_imapx_server_expunge (CamelIMAPXServer *is,
                            CamelIMAPXMailbox *mailbox,
                            GCancellable *cancellable,
                            GError **error)
{
	CamelIMAPXJob *job;
	gboolean registered;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);

	/* Do we really care to wait for this one to finish? */
	job = imapx_server_ref_job (is, mailbox, IMAPX_JOB_EXPUNGE, NULL);

	if (job != NULL) {
		camel_imapx_job_unref (job);
		return TRUE;
	}

	QUEUE_LOCK (is);

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_EXPUNGE;
	job->start = imapx_job_expunge_start;
	job->matches = imapx_job_expunge_matches;
	job->pri = IMAPX_PRIORITY_EXPUNGE;

	camel_imapx_job_set_mailbox (job, mailbox);

	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	success = registered && camel_imapx_job_run (job, is, error);

	camel_imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_list (CamelIMAPXServer *is,
                         const gchar *pattern,
                         CamelStoreGetFolderInfoFlags flags,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelIMAPXJob *job;
	ListData *data;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (pattern != NULL, FALSE);

	data = g_slice_new0 (ListData);
	data->pattern = g_strdup (pattern);

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

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_create_mailbox (CamelIMAPXServer *is,
                                   const gchar *mailbox_name,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelIMAPXJob *job;
	MailboxData *data;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (mailbox_name != NULL, FALSE);

	data = g_slice_new0 (MailboxData);
	data->mailbox_name = g_strdup (mailbox_name);

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_CREATE_MAILBOX;
	job->start = imapx_job_create_mailbox_start;
	job->pri = IMAPX_PRIORITY_MAILBOX_MGMT;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) mailbox_data_free);

	success = imapx_submit_job (is, job, error);

	if (success) {
		gchar *utf7_pattern;

		utf7_pattern = camel_utf8_utf7 (mailbox_name);

		/* List the new mailbox so we trigger our untagged
		 * LIST handler.  This simulates being notified of
		 * a newly-created mailbox, so we can just let the
		 * callback functions handle the bookkeeping. */
		success = camel_imapx_server_list (
			is, utf7_pattern, 0, cancellable, error);

		g_free (utf7_pattern);
	}

	camel_imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_delete_mailbox (CamelIMAPXServer *is,
                                   CamelIMAPXMailbox *mailbox,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelIMAPXJob *job;
	MailboxData *data;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);

	/* Avoid camel_imapx_job_set_mailbox() here.  We
	 * don't want to select the mailbox to be deleted. */

	data = g_slice_new0 (MailboxData);
	data->mailbox = g_object_ref (mailbox);

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_DELETE_MAILBOX;
	job->start = imapx_job_delete_mailbox_start;
	job->pri = IMAPX_PRIORITY_MAILBOX_MGMT;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) mailbox_data_free);

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_rename_mailbox (CamelIMAPXServer *is,
                                   CamelIMAPXMailbox *mailbox,
                                   const gchar *new_mailbox_name,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelIMAPXJob *job;
	MailboxData *data;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);
	g_return_val_if_fail (new_mailbox_name != NULL, FALSE);

	/* Avoid camel_imapx_job_set_mailbox() here.  We
	 * don't want to select the mailbox to be renamed. */

	data = g_slice_new0 (MailboxData);
	data->mailbox = g_object_ref (mailbox);
	data->mailbox_name = g_strdup (new_mailbox_name);

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_RENAME_MAILBOX;
	job->start = imapx_job_rename_mailbox_start;
	job->pri = IMAPX_PRIORITY_MAILBOX_MGMT;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) mailbox_data_free);

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_subscribe_mailbox (CamelIMAPXServer *is,
                                      CamelIMAPXMailbox *mailbox,
                                      GCancellable *cancellable,
                                      GError **error)
{
	CamelIMAPXJob *job;
	MailboxData *data;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);

	/* Avoid camel_imapx_job_set_mailbox() here.  We
	 * don't want to select the mailbox to be subscribed. */

	data = g_slice_new0 (MailboxData);
	data->mailbox = g_object_ref (mailbox);

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_SUBSCRIBE_MAILBOX;
	job->start = imapx_job_subscribe_mailbox_start;
	job->pri = IMAPX_PRIORITY_MAILBOX_MGMT;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) mailbox_data_free);

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_unsubscribe_mailbox (CamelIMAPXServer *is,
                                        CamelIMAPXMailbox *mailbox,
                                        GCancellable *cancellable,
                                        GError **error)
{
	CamelIMAPXJob *job;
	MailboxData *data;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);

	/* Avoid camel_imapx_job_set_mailbox() here.  We
	 * don't want to select the mailbox to be unsubscribed. */

	data = g_slice_new0 (MailboxData);
	data->mailbox = g_object_ref (mailbox);

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_UNSUBSCRIBE_MAILBOX;
	job->start = imapx_job_unsubscribe_mailbox_start;
	job->pri = IMAPX_PRIORITY_MAILBOX_MGMT;

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) mailbox_data_free);

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

	return success;
}

gboolean
camel_imapx_server_update_quota_info (CamelIMAPXServer *is,
                                      CamelIMAPXMailbox *mailbox,
                                      GCancellable *cancellable,
                                      GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);

	if (CAMEL_IMAPX_LACK_CAPABILITY (is->cinfo, QUOTA)) {
		g_set_error_literal (
			error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			_("IMAP server does not support quotas"));
		return FALSE;
	}

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_UPDATE_QUOTA_INFO;
	job->start = imapx_job_update_quota_info_start;
	job->pri = IMAPX_PRIORITY_UPDATE_QUOTA_INFO;

	camel_imapx_job_set_mailbox (job, mailbox);

	success = imapx_submit_job (is, job, error);

	camel_imapx_job_unref (job);

	return success;
}

GPtrArray *
camel_imapx_server_uid_search (CamelIMAPXServer *is,
                               CamelIMAPXMailbox *mailbox,
                               const gchar *criteria,
                               GCancellable *cancellable,
                               GError **error)
{
	CamelIMAPXJob *job;
	SearchData *data;
	GPtrArray *results = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), NULL);
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), NULL);
	g_return_val_if_fail (criteria != NULL, NULL);

	data = g_slice_new0 (SearchData);
	data->criteria = g_strdup (criteria);

	job = camel_imapx_job_new (cancellable);
	job->type = IMAPX_JOB_UID_SEARCH;
	job->start = imapx_job_uid_search_start;
	job->pri = IMAPX_PRIORITY_SEARCH;

	camel_imapx_job_set_mailbox (job, mailbox);

	camel_imapx_job_set_data (
		job, data, (GDestroyNotify) search_data_free);

	if (imapx_submit_job (is, job, error)) {
		guint ii;

		/* Convert the numeric UIDs to strings. */

		g_return_val_if_fail (data->results != NULL, NULL);

		results = g_ptr_array_new_full (
			data->results->len,
			(GDestroyNotify) camel_pstring_free);

		for (ii = 0; ii < data->results->len; ii++) {
			const gchar *pooled_uid;
			guint64 numeric_uid;
			gchar *alloced_uid;

			numeric_uid = g_array_index (
				data->results, guint64, ii);
			alloced_uid = g_strdup_printf (
				"%" G_GUINT64_FORMAT, numeric_uid);
			pooled_uid = camel_pstring_add (alloced_uid, TRUE);
			g_ptr_array_add (results, (gpointer) pooled_uid);
		}
	}

	camel_imapx_job_unref (job);

	return results;
}

gboolean
camel_imapx_server_folder_name_in_jobs (CamelIMAPXServer *imapx_server,
					const gchar *folder_path)
{
	gboolean res;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (imapx_server), FALSE);
	g_return_val_if_fail (folder_path != NULL, FALSE);

	g_mutex_lock (&imapx_server->priv->jobs_prop_lock);

	res = GPOINTER_TO_INT (g_hash_table_lookup (imapx_server->priv->jobs_prop_folder_paths, folder_path)) > 0;

	g_mutex_unlock (&imapx_server->priv->jobs_prop_lock);

	return res;
}

gboolean
camel_imapx_server_has_expensive_command (CamelIMAPXServer *imapx_server)
{
	gboolean res;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (imapx_server), FALSE);

	g_mutex_lock (&imapx_server->priv->jobs_prop_lock);

	res = imapx_server->priv->jobs_prop_expensive_command_count > 0;

	g_mutex_unlock (&imapx_server->priv->jobs_prop_lock);

	return res;
}

gint
camel_imapx_server_get_command_count (CamelIMAPXServer *imapx_server)
{
	guint32 res;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (imapx_server), -1);

	g_mutex_lock (&imapx_server->priv->jobs_prop_lock);

	res = imapx_server->priv->jobs_prop_command_count;

	g_mutex_unlock (&imapx_server->priv->jobs_prop_lock);

	return res;
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

/**
 * camel_imapx_server_is_job_in_queue:
 * @imapx_server: a #CamelIMAPXServer instance
 * @mailbox: a mailbox to search job for
 * @job_type: a job type specifier to search for
 * @uid: optional message UID for which the job might be searched
 *
 * Searches queue of jobs for the particular job. The returned job
 * is referenced for thread safety, unref it with camel_imapx_job_unref().
 *
 * Returns: %NULL, if such job could not be found, or a referenced job.
 **/
CamelIMAPXJob *
camel_imapx_server_ref_job (CamelIMAPXServer *imapx_server,
			    CamelIMAPXMailbox *mailbox,
			    guint32 job_type,
			    const gchar *uid)
{
	GList *head, *link;
	CamelIMAPXJob *job = NULL;
	gboolean found = FALSE;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (imapx_server), NULL);

	QUEUE_LOCK (imapx_server);

	head = g_queue_peek_head_link (&imapx_server->jobs);

	for (link = head; link != NULL; link = g_list_next (link)) {
		job = (CamelIMAPXJob *) link->data;

		if (!job || !(job->type & job_type))
			continue;

		if (camel_imapx_job_matches (job, mailbox, uid)) {
			found = TRUE;
			camel_imapx_job_ref (job);
			break;
		}
	}

	QUEUE_UNLOCK (imapx_server);

	return found ? job : NULL;
}

/**
 * camel_imapx_server_shutdown:
 * @is: a #CamelIMAPXServer
 * @error: a #GError with which cancel any pending jobs
 *
 * Signals the server to shut down command processing. A #CamelIMAPXStore
 * should call this immediately before unreferencing its server instance.
 * Note, the server instance may linger a short time after this function
 * returns as its own worker threads finish.
 *
 * Since: 3.12
 **/
void
camel_imapx_server_shutdown (CamelIMAPXServer *is,
			     const GError *error)
{
	GCancellable *cancellable;
	GError *shutdown_error_copy = NULL;

	g_return_if_fail (CAMEL_IS_IMAPX_SERVER (is));

	QUEUE_LOCK (is);

	is->state = IMAPX_SHUTDOWN;

	cancellable = g_weak_ref_get (&is->priv->parser_cancellable);

	QUEUE_UNLOCK (is);

	if (!error) {
		shutdown_error_copy = imapx_server_dup_shutdown_error (is);
		error = shutdown_error_copy;
	}

	if (error) {
		imapx_abort_all_commands (is, error);
	} else {
		GError *local_error = NULL;

		g_set_error (
			&local_error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			"Shutting down");

		imapx_abort_all_commands (is, local_error);

		g_clear_error (&local_error);
	}

	g_main_loop_quit (is->priv->idle_main_loop);
	g_main_loop_quit (is->priv->parser_main_loop);

	g_cancellable_cancel (cancellable);
	g_clear_object (&cancellable);
	g_clear_error (&shutdown_error_copy);
}

static const gchar *
imapx_server_get_job_type_name (CamelIMAPXJob *job)
{
	if (!job)
		return "[null]";

	switch (job->type) {
	case IMAPX_JOB_GET_MESSAGE:
		return "GET_MESSAGE";
	case IMAPX_JOB_APPEND_MESSAGE:
		return "APPEND_MESSAGE";
	case IMAPX_JOB_COPY_MESSAGE:
		return "COPY_MESSAGE";
	case IMAPX_JOB_FETCH_NEW_MESSAGES:
		return "FETCH_NEW_MESSAGES";
	case IMAPX_JOB_REFRESH_INFO:
		return "REFRESH_INFO";
	case IMAPX_JOB_SYNC_CHANGES:
		return "SYNC_CHANGES";
	case IMAPX_JOB_EXPUNGE:
		return "EXPUNGE";
	case IMAPX_JOB_NOOP:
		return "NOOP";
	case IMAPX_JOB_IDLE:
		return "IDLE";
	case IMAPX_JOB_LIST:
		return "LIST";
	case IMAPX_JOB_CREATE_MAILBOX:
		return "CREATE_MAILBOX";
	case IMAPX_JOB_DELETE_MAILBOX:
		return "DELETE_MAILBOX";
	case IMAPX_JOB_RENAME_MAILBOX:
		return "RENAME_MAILBOX";
	case IMAPX_JOB_SUBSCRIBE_MAILBOX:
		return "SUBSCRIBE_MAILBOX";
	case IMAPX_JOB_UNSUBSCRIBE_MAILBOX:
		return "UNSUBSCRIBE_MAILBOX";
	case IMAPX_JOB_UPDATE_QUOTA_INFO:
		return "UPDATE_QUOTA_INFO";
	case IMAPX_JOB_UID_SEARCH:
		return "UID_SEARCH";
	}

	return "???";
}

static void
imapx_server_dump_one_queue (CamelIMAPXCommandQueue *queue,
			     const gchar *queue_name)
{
	GList *iter;
	gint ii;

	g_return_if_fail (queue != NULL);
	g_return_if_fail (queue_name != NULL);

	if (camel_imapx_command_queue_is_empty (queue))
		return;

	printf ("      Content of '%s':\n", queue_name);

	for (ii = 0, iter = camel_imapx_command_queue_peek_head_link (queue); iter != NULL; iter = g_list_next (iter), ii++) {
		CamelIMAPXCommand *ic = iter->data;
		CamelIMAPXJob *job = camel_imapx_command_get_job (ic);

		printf ("         [%d] command:%p for job:%p (type:0x%x %s)\n", ii, ic, job, job ? job->type : 0, imapx_server_get_job_type_name (job));
	}
}

/* for debugging purposes only */
void
camel_imapx_server_dump_queue_status (CamelIMAPXServer *imapx_server)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SERVER (imapx_server));

	QUEUE_LOCK (imapx_server);

	printf ("   Queue status for server %p: jobs:%d queued:%d active:%d done:%d\n", imapx_server,
		g_queue_get_length (&imapx_server->jobs),
		camel_imapx_command_queue_get_length (imapx_server->queue),
		camel_imapx_command_queue_get_length (imapx_server->active),
		camel_imapx_command_queue_get_length (imapx_server->done));

	if (!g_queue_is_empty (&imapx_server->jobs)) {
		GList *iter;
		gint ii;

		printf ("      Content of 'jobs':\n");

		for (ii = 0, iter = g_queue_peek_head_link (&imapx_server->jobs); iter != NULL; iter = g_list_next (iter), ii++) {
			CamelIMAPXJob *job = iter->data;

			printf ("         [%d] job:%p (type:0x%x %s) with pending commands:%d\n", ii, job, job ? job->type : 0,
				imapx_server_get_job_type_name (job),
				job ? g_atomic_int_get (&job->commands) : -1);
		}
	}

	imapx_server_dump_one_queue (imapx_server->queue, "queue");
	imapx_server_dump_one_queue (imapx_server->active, "active");
	imapx_server_dump_one_queue (imapx_server->done, "done");

	QUEUE_UNLOCK (imapx_server);
}
